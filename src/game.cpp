#include "game.hpp"
#include "neural_policy.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkTypeface.h"
#include "include/codec/SkCodec.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/core/SkPathEffect.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <numeric>

#include <windows.h>

namespace fiada {
namespace {
constexpr float kPi = 3.14159265358979323846F;

constexpr std::array<SkPoint, 35> kCourse{{
    {-650,-350}, {-520,-430}, {-360,-330}, {-200,-430}, {-20,-300},
    {150,-455}, {330,-330}, {520,-430}, {720,-330}, {780,-180},
    {690,-70}, {520,-20}, {360,-140}, {180,-20}, {20,-150},
    {-170,-60}, {-350,-150}, {-410,10}, {-310,170}, {-120,250},
    {80,130}, {260,50}, {430,160}, {610,80}, {690,240},
    {580,390}, {440,440}, {180,340}, {-50,460}, {-280,380},
    {-520,450}, {-720,300}, {-810,100}, {-770,-100}, {-700,-280},
}};
constexpr int kTrackCount = 6;
constexpr int kWildWestTrack = 5;
constexpr int kSamplesPerSegment = 12;
constexpr int kSampleCount = static_cast<int>(kCourse.size()) * kSamplesPerSegment;
int gTrackIndex = 0;
SkPoint trackControl(int index) {
  const int n=static_cast<int>(kCourse.size()); auto p=kCourse[(index%n+n)%n];
  const float x=p.x(),y=p.y();
  switch(gTrackIndex){
    case 1: return {x*.88F+std::sin(y*.012F)*105.0F,y*1.18F}; // Alpine switchbacks
    case 2: return {x*1.08F,y*.86F+std::sin(x*.015F)*135.0F}; // Volcanic foundry
    case 3: return {x*1.28F+std::sin(y*.009F)*70.0F,y*.78F};  // Coastal causeway
    case 4: return {x*.95F+std::sin(y*.021F)*55.0F,y*.95F+std::sin(x*.019F)*55.0F}; // Neon city
    case 5: return {x*3.20F,y*3.20F}; // Grand Wild West endurance
    default:return p;
  }
}

enum ItemType { kNoItem=0, kHorn=1, kDiamondRod=2 };
constexpr std::array<int,7> kItemBoxSamples{{38,92,151,207,269,334,394}};


SkPoint coursePoint(int segment, float t) {
  const int n = static_cast<int>(kCourse.size());
  const SkPoint& p0 = trackControl((segment - 1 + n) % n);
  const SkPoint& p1 = trackControl(segment % n);
  const SkPoint& p2 = trackControl((segment + 1) % n);
  const SkPoint& p3 = trackControl((segment + 2) % n);
  const float t2 = t * t, t3 = t2 * t;
  return {
      0.5F * ((2 * p1.x()) + (-p0.x() + p2.x()) * t +
              (2*p0.x() - 5*p1.x() + 4*p2.x() - p3.x()) * t2 +
              (-p0.x() + 3*p1.x() - 3*p2.x() + p3.x()) * t3),
      0.5F * ((2 * p1.y()) + (-p0.y() + p2.y()) * t +
              (2*p0.y() - 5*p1.y() + 4*p2.y() - p3.y()) * t2 +
              (-p0.y() + 3*p1.y() - 3*p2.y() + p3.y()) * t3)};
}


SkPoint courseSample(int index) {
  index = (index % kSampleCount + kSampleCount) % kSampleCount;
  return coursePoint(index / kSamplesPerSegment,
                     (index % kSamplesPerSegment) / static_cast<float>(kSamplesPerSegment));
}

int nearestCourseSample(float x, float y) {
  int bestIndex = 0;
  float bestDistance = 1.0e30F;
  for (int i = 0; i < kSampleCount; ++i) {
    const auto p = courseSample(i);
    const float distance = (p.x()-x)*(p.x()-x) + (p.y()-y)*(p.y()-y);
    if (distance < bestDistance) { bestDistance = distance; bestIndex = i; }
  }
  return bestIndex;
}

bool guardedSample(int sample){sample=(sample%kSampleCount+kSampleCount)%kSampleCount;return (sample>=48&&sample<=92)||(sample>=176&&sample<=218)||(sample>=304&&sample<=342);}

float wrapAngle(float value) {
  while (value > kPi) value -= 2.0F * kPi;
  while (value < -kPi) value += 2.0F * kPi;
  return value;
}

float sigmoid(float value) { return 1.0F / (1.0F + std::exp(-value)); }

SkPath coursePath() {
  SkPath path;
  path.moveTo(trackControl(0));
  for (int segment = 0; segment < static_cast<int>(kCourse.size()); ++segment)
    for (int step = 1; step <= 12; ++step)
      path.lineTo(coursePoint(segment, step / 12.0F));
  path.close();
  return path;
}

float courseDistance(float x, float y) {
  float best = 1.0e9F;
  SkPoint previous = coursePoint(0, 0.0F);
  for (int segment = 0; segment < static_cast<int>(kCourse.size()); ++segment) {
    for (int step = 1; step <= 12; ++step) {
      const SkPoint current = coursePoint(segment, step / 12.0F);
      const float abx = current.x() - previous.x(), aby = current.y() - previous.y();
      const float apx = x - previous.x(), apy = y - previous.y();
      const float denominator = abx * abx + aby * aby;
      const float u = std::clamp((apx * abx + apy * aby) / denominator, 0.0F, 1.0F);
      const float dx = x - (previous.x() + u * abx);
      const float dy = y - (previous.y() + u * aby);
      best = std::min(best, std::sqrt(dx * dx + dy * dy));
      previous = current;
    }
  }
  return best;
}



std::uint32_t mixBits(std::uint32_t value) {
  value ^= value >> 16; value *= 0x7feb352dU;
  value ^= value >> 15; value *= 0x846ca68bU;
  return value ^ (value >> 16);
}
sk_sp<SkImage> loadPng(const std::filesystem::path& path) {
  auto data = SkData::MakeFromFileName(path.string().c_str());
  if (!data) return nullptr;
  return SkImage::MakeFromEncoded(std::move(data));
}

void text(SkCanvas& canvas, std::string_view value, float x, float y,
          float size, SkColor color) {
  SkPaint paint;
  paint.setColor(color);
  paint.setAntiAlias(true);
  SkFont font(nullptr, size);
  canvas.drawSimpleText(value.data(), value.size(), SkTextEncoding::kUTF8,
                        x, y, font, paint);
}
}  // namespace

Game::Game(bool assets) {
  frontEnd_ = assets;
  reset();
  if (assets) loadAssets();
}

void Game::resize(int width, int height) {
  width_ = std::max(width, 1);
  height_ = std::max(height, 1);
}

void Game::loadAssets() {
  std::array<wchar_t, 32768> module{};
  const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
  const auto root = std::filesystem::path(std::wstring(module.data(), length)).parent_path();
  car_ = loadPng(root / "assets/png/player_car.png");
  cone_ = loadPng(root / "assets/png/cone.png");
  policy::load(root / "assets/policy/fiada_policy.bin", policyWeights_);
}

void Game::reset() {
  gTrackIndex = trackIndex_;
  x_ = trackControl(0).x();
  y_ = trackControl(0).y();
  const auto startAhead = coursePoint(0, 0.05F);
  angle_ = std::atan2(startAhead.y() - y_, startAhead.x() - x_);
  velocityX_ = velocityY_ = yawRate_ = 0.0F;
  throttle_ = brake_ = steer_ = driftCharge_ = turboTime_ = 0.0F; miniTurbos_ = 0;
  wasDrifting_ = false; itemUseHeld_=shortcutCounted_=false;
  heldItem_=shortcutItem_=kNoItem; lastItemBox_=-1; recentItems_={}; simulationTick_=0; itemEffectTime_=impactFxTime_=wallFxTime_=hazardFxTime_=0.0F;hazardHits_=0;
  itemPickups_=itemUses_=shortcutsTaken_=0;
  itemPickupsByType_={};shortcutsByType_={};offroadTime_=0.0F;policyTick_=0;cachedPolicyOutput_={};
  policyState_ = {};
  cameraX_ = x_ + std::cos(angle_) * 90.0F;
  cameraY_ = y_ + std::sin(angle_) * 90.0F;
  countdownTicks_ = trainingMode_ ? 0 : 360; placement_ = 1; raceAwarded_=false;
  for (int i=0;i<7;++i) { auto p=courseSample(kSampleCount-i*3-5); auto q=courseSample(kSampleCount-i*3-4); rivals_[i]={p.x(),p.y(),std::atan2(q.y()-p.y(),q.x()-p.x()),18.0F,static_cast<float>(kSampleCount-i*3-5),0.0F,0, i+2, 0, i%5, false}; }
  previousX_ = x_;
  previousY_ = y_;
  lapTime_ = 0.0F;
  checkpoint_ = 1;
}

bool Game::onRoad(float x, float y) const {
  return courseDistance(x,y) < 78.0F || (shortcutItem_==kDiamondRod && itemEffectTime_>0.0F);
}

void Game::updateItems(const Input& control) {
  ++simulationTick_;
  itemEffectTime_=std::max(0.0F,itemEffectTime_-1.0F/120.0F);impactFxTime_=std::max(0.0F,impactFxTime_-1.0F/120.0F);wallFxTime_=std::max(0.0F,wallFxTime_-1.0F/120.0F);hazardFxTime_=std::max(0.0F,hazardFxTime_-1.0F/120.0F);
  if(itemEffectTime_<=0.0F) shortcutItem_=kNoItem;
  if(control.useItem && !itemUseHeld_ && heldItem_!=kNoItem){
    const int used=heldItem_; heldItem_=kNoItem; ++itemUses_;
    if(used==kHorn){
      // A deterministic radial pressure wave: no projectile and no random aim.
      for(auto& rival:rivals_){
        const float dx=rival.x-x_,dy=rival.y-y_,distance=std::hypot(dx,dy);
        if(distance<190.0F && distance>0.01F){const float force=(190.0F-distance)/190.0F;rival.x+=dx/distance*force*20.0F;rival.y+=dy/distance*force*20.0F;rival.speed*=.52F;rival.hornFx=.32F;rival.turbo=0.0F;}
      }
      itemEffectTime_=.32F; shortcutItem_=kHorn;
    } else {
      // The rod pulls the car over any ordinary off-road cut without creating a branch lane.
      shortcutItem_=kDiamondRod; itemEffectTime_=5.5F; turboTime_=std::max(turboTime_,.55F); shortcutCounted_=false;
    }
  }
  itemUseHeld_=control.useItem;
  if(shortcutItem_==kDiamondRod && itemEffectTime_>0.0F && courseDistance(x_,y_)>=78.0F && !shortcutCounted_){
    shortcutCounted_=true;++shortcutsTaken_;++shortcutsByType_[1];if(trainingMode_)trainingReward_+=35.0F;
  }
  int touching=-1;
  for(int i=0;i<static_cast<int>(kItemBoxSamples.size());++i){const auto p=courseSample(kItemBoxSamples[i]);if(std::hypot(x_-p.x(),y_-p.y())<30.0F){touching=i;break;}}
  if(touching<0)lastItemBox_=-1;
  if(touching>=0&&touching!=lastItemBox_&&heldItem_==kNoItem){
    const std::uint32_t signature=static_cast<std::uint32_t>(touching*0x9e3779b9U)^static_cast<std::uint32_t>(simulationTick_/9+checkpoint_*131+placement_*977);
    int award=1+static_cast<int>(mixBits(signature)%2U);if(recentItems_[0]==award&&recentItems_[1]==award)award=3-award;
    recentItems_[2]=recentItems_[1];recentItems_[1]=recentItems_[0];recentItems_[0]=award;
    heldItem_=award;++itemPickups_;++itemPickupsByType_[award-1];lastItemBox_=touching;
  }
}
policy::Observation Game::observation() const {
  policy::Observation obs{};
  const int nearest=nearestCourseSample(x_,y_);
  const auto currentRaw=courseSample(nearest),next=courseSample(nearest+1);
  float tx=next.x()-currentRaw.x(),ty=next.y()-currentRaw.y();
  const float tl=std::hypot(tx,ty);tx/=tl;ty/=tl;
  const SkPoint current{currentRaw.x()-ty*racingLineOffset_,currentRaw.y()+tx*racingLineOffset_};
  const float lateral=(x_-current.x())*-ty+(y_-current.y())*tx;
  const auto target=courseSample(nearest+11),futureNext=courseSample(nearest+12);
  float fx=futureNext.x()-target.x(),fy=futureNext.y()-target.y();const float fl=std::hypot(fx,fy);fx/=fl;fy/=fl;
  const float error=wrapAngle(std::atan2(target.y()-y_,target.x()-x_)-angle_);
  const float curve=std::atan2(tx*fy-ty*fx,tx*fx+ty*fy);
  obs[0]=std::sin(error);obs[1]=std::cos(error);obs[2]=std::clamp(lateral/82.0F,-2.0F,2.0F);
  obs[3]=velocityX_/45.0F;obs[4]=velocityY_/15.0F;obs[5]=yawRate_/2.0F;obs[6]=curve;
  obs[7]=onRoad(x_,y_)?1.0F:0.0F;obs[8]=driftCharge_;obs[9]=std::clamp(turboTime_/1.5F,0.0F,1.0F);
  float nearestRival=500.0F; if(!trainingMode_)for(const auto& rival:rivals_)nearestRival=std::min(nearestRival,std::hypot(x_-rival.x,y_-rival.y));
  const float offroadNeed=std::clamp((courseDistance(x_,y_)-45.0F)/80.0F,0.0F,1.0F);
  obs[10]=heldItem_==kHorn;obs[11]=heldItem_==kDiamondRod;obs[12]=shortcutItem_==kDiamondRod;obs[13]=heldItem_==kHorn?std::clamp((190.0F-nearestRival)/190.0F,0.0F,1.0F):offroadNeed;
  constexpr std::array<int,4> lookahead{5,14,28,52};
  int cursor=14;
  for(const int offset:lookahead){
    const auto p=courseSample(nearest+offset),q=courseSample(nearest+offset+1);
    float qx=q.x()-p.x(),qy=q.y()-p.y();const float ql=std::hypot(qx,qy);qx/=ql;qy/=ql;
    const float heading=wrapAngle(std::atan2(qy,qx)-angle_);
    const float futureCurve=std::atan2(tx*qy-ty*qx,tx*qx+ty*qy);
    const float side=(x_-p.x())*-qy+(y_-p.y())*qx;
    obs[cursor++]=std::sin(heading);obs[cursor++]=std::cos(heading);
    obs[cursor++]=std::clamp(futureCurve,-1.0F,1.0F);obs[cursor++]=std::clamp(side/180.0F,-2.0F,2.0F);
  }
  const float phase=2.0F*kPi*nearest/static_cast<float>(kSampleCount);
  obs[30]=std::sin(phase);obs[31]=std::cos(phase);
  return obs;
}

void Game::beginTrainingEpisode(unsigned seed) {
  reset(); trainingMode_=true; trainingTerminal_=false; laps_=0;
  if (seed == 0) { progressSample_=0; gripScale_=1.0F; racingLineOffset_=0.0F; return; }
  std::mt19937 random(seed);
  std::uniform_int_distribution<int> sampleDistribution(0,kSampleCount-1);
  std::uniform_real_distribution<float> lateral(-42.0F,42.0F), heading(-0.24F,0.24F);
  std::uniform_real_distribution<float> speed(4.0F,24.0F), grip(0.78F,1.18F), line(-32.0F,32.0F);
  progressSample_=sampleDistribution(random);
  const auto point=courseSample(progressSample_), next=courseSample(progressSample_+1);
  float tx=next.x()-point.x(),ty=next.y()-point.y();const float length=std::hypot(tx,ty);tx/=length;ty/=length;
  const float offset=lateral(random);x_=point.x()-ty*offset;y_=point.y()+tx*offset;
  angle_=std::atan2(ty,tx)+heading(random);velocityX_=speed(random);
  gripScale_=grip(random);racingLineOffset_=line(random);
  checkpoint_=(progressSample_/kSamplesPerSegment+1)%static_cast<int>(kCourse.size());
  previousX_=x_;previousY_=y_;cameraX_=x_;cameraY_=y_;
}

void Game::beginItemTrainingEpisode(int itemType,unsigned seed){
  beginTrainingEpisode(seed); heldItem_=std::clamp(itemType,1,2); policyState_={};
  if(heldItem_==kDiamondRod){const auto p=courseSample(progressSample_),q=courseSample(progressSample_+1);float tx=q.x()-p.x(),ty=q.y()-p.y(),length=std::hypot(tx,ty);tx/=length;ty/=length;x_=p.x()-ty*105.0F;y_=p.y()+tx*105.0F;}
}
Input Game::aiInput() {
  const auto obs = observation();
  if (policyWeights_.size() != policy::kParameterCount) {
    const float steer=std::clamp(1.8F*obs[0]-1.25F*obs[2]+1.4F*obs[6]-0.3F*obs[5],-1.0F,1.0F);
    return {.throttle=.72F,.brake=std::abs(obs[6])>.55F?.35F:0.0F,.steer=steer};
  }
  if ((policyTick_++ & 3) == 0) cachedPolicyOutput_=policy::forward(obs,policyWeights_,policyState_);
  const auto& output=cachedPolicyOutput_;
  float throttle=sigmoid(output[0]), brake=sigmoid(output[1]);
  float steer=std::tanh(output[2]);
  if(shortcutItem_==kDiamondRod&&!obs[7])steer=std::clamp(steer*.78F,-1.0F,1.0F);
  if(trackIndex_==kWildWestTrack){const int nearest=nearestCourseSample(x_,y_);const auto target=courseSample(nearest+5);const float error=wrapAngle(std::atan2(target.y()-y_,target.x()-x_)-angle_);steer=std::clamp(2.15F*std::sin(error)-.28F*yawRate_,-1.0F,1.0F);throttle=std::abs(error)<.75F?.82F:.42F;brake=std::abs(error)>.95F?.28F:0.0F;}
  return {.throttle=throttle,.brake=brake*(1.0F-throttle),.steer=steer,
          .reset=false,.drift=sigmoid(output[3])>.40F,.toggleAi=false,.useItem=(obs[10]>.5F&&obs[13]>.18F)||(obs[11]>.5F&&(obs[7]<.5F||std::abs(obs[6])>.42F))||(sigmoid(output[4])>.92F&&(obs[10]+obs[11])>.5F)};
}

void Game::update(float dt, const Input& input) {
  dt = std::min(dt, 0.05F);
trainingReward_ = 0.0F;
  if (frontEnd_) {
    const bool nav=input.next||input.previous||input.brake>.5F||input.throttle>.5F;
    if(nav&&!navHeld_){const int direction=(input.next||input.brake>.5F)?1:-1;if(labTrackSelect_)trackIndex_=(trackIndex_+kTrackCount+direction)%kTrackCount;else if(!titlePage_)modeSelection_=(modeSelection_+3+direction)%3;}
    navHeld_=nav;
    if(input.menu&&!menuHeld_){if(labTrackSelect_)labTrackSelect_=false;else if(!titlePage_)titlePage_=true;}
    if(input.confirm&&!confirmHeld_){
      if(titlePage_)titlePage_=false;
      else if(labTrackSelect_){frontEnd_=false;labMode_=true;championshipMode_=false;aiEnabled_=true;reset();}
      else if(modeSelection_==2)labTrackSelect_=true;
      else {frontEnd_=false;championshipMode_=modeSelection_==1;labMode_=false;aiEnabled_=false;reset();}
    }
    confirmHeld_=input.confirm;menuHeld_=input.menu;return;
  }  if(input.menu && !menuHeld_){frontEnd_=true;titlePage_=false;labTrackSelect_=false;confirmHeld_=true;menuHeld_=true;return;}
  if(raceAwarded_&&trackIndex_==kWildWestTrack)return;
  if (input.toggleAi && !aiToggleHeld_) aiEnabled_ = !aiEnabled_;
  aiToggleHeld_ = input.toggleAi;
  if (input.labOverlay && !overlayHeld_) labMode_ = !labMode_;
  overlayHeld_ = input.labOverlay;
  menuHeld_ = input.menu;
  if (!navHeld_ && input.next) { trackIndex_=(trackIndex_+1)%kTrackCount; reset(); }
  if (!navHeld_ && input.previous) { trackIndex_=(trackIndex_+kTrackCount-1)%kTrackCount; reset(); }
  navHeld_=input.next||input.previous;
  if (input.reset && !resetHeld_) reset();
  resetHeld_ = input.reset;
  Input control = aiEnabled_ ? aiInput() : input;
  if (!trainingMode_ && countdownTicks_>0) { control.throttle=control.brake=control.steer=0.0F; control.drift=control.useItem=false; }
  updateItems(control);
  if (!trainingMode_) { if (countdownTicks_ > 0) --countdownTicks_; else updateRivals(dt); }

  // Six-state nonlinear bicycle model: compact enough for batched training.
  constexpr float mass = 1180.0F, inertia = 1760.0F;
  constexpr float frontAxle = 1.18F, rearAxle = 1.42F, gravity = 9.81F;
  const bool road = onRoad(x_, y_);
  if (!road) offroadTime_ += dt;
  const int surfaceSample=nearestCourseSample(x_,y_);
  const bool dustZone=trackIndex_==kWildWestTrack&&((surfaceSample>=105&&surfaceSample<=145)||(surfaceSample>=330&&surfaceSample<=370));
  const float surfaceGrip = (road ? 1.34F : 0.58F) * gripScale_ * (dustZone?.72F:1.0F);
  const float rolling = road ? 34.0F : 280.0F;
  const float gripF = surfaceGrip;
  const float gripR = surfaceGrip * (control.drift ? 0.90F : 1.0F);
  const float throttleTarget = control.throttle;
  const float brakeTarget = control.brake;
  const float steerTarget = control.steer;
  throttle_ += (throttleTarget - throttle_) * std::min(1.0F, 11.0F * dt);
  brake_ += (brakeTarget - brake_) * std::min(1.0F, 9.0F * dt);
  steer_ += (steerTarget - steer_) * std::min(1.0F, 13.0F * dt);

  const float speedAbs = std::abs(velocityX_);
  const float driftSteer = control.drift ? 1.06F : 1.0F;
  const float steering = steer_ * driftSteer * 0.39F / (1.0F + speedAbs * 0.012F);
  const float safeSpeed = std::max(speedAbs, 2.5F);
  const float slipF = std::atan2(velocityY_ + frontAxle * yawRate_, safeSpeed) - steering;
  const float slipR = std::atan2(velocityY_ - rearAxle * yawRate_, safeSpeed);

  const float slipMagnitude = std::abs(slipR);
  const bool validDrift = control.drift && road && speedAbs > 9.0F &&
                          slipMagnitude > 0.035F && slipMagnitude < 0.85F;
  if (validDrift)
    driftCharge_ = std::min(1.0F, driftCharge_ + dt * (0.48F + slipMagnitude * 1.10F));
  if (wasDrifting_ && !control.drift) {
    if (driftCharge_ >= 0.12F) { turboTime_ = 0.42F + driftCharge_ * 1.08F; ++miniTurbos_; }
    driftCharge_ = 0.0F;
  }
  if (!road) driftCharge_ = std::max(0.0F, driftCharge_ - dt * 0.75F);
  wasDrifting_ = control.drift;
  turboTime_ = std::max(0.0F, turboTime_ - dt);

  const float turboForce = turboTime_ > 0.0F ? 5200.0F : 0.0F;
  const float itemBoost = shortcutItem_==kDiamondRod && itemEffectTime_>0.0F ? 1600.0F : 0.0F;
  const float drive = throttle_ * 13800.0F / (1.0F + speedAbs / 48.0F) + turboForce + itemBoost;
  const float braking = brake_ * 10500.0F * (velocityX_ >= 0.0F ? 1.0F : -1.0F);
  const float requestedX = drive - braking;
  const float transfer = std::clamp(requestedX * 0.000045F, -0.22F, 0.22F);
  const float loadF = mass * gravity * (rearAxle / (frontAxle + rearAxle) - transfer);
  const float loadR = mass * gravity - loadF;
  const auto tire = [](float slip, float load, float stiffness, float grip) {
    return -grip * load * std::tanh(stiffness * slip / (grip * load));
  };
  float forceF = tire(slipF, loadF, 82000.0F, gripF);
  float forceR = tire(slipR, loadR, 91000.0F, gripR);
  const float limitR = gripR * loadR;
  const float forceX = std::clamp(requestedX, -limitR, limitR);
  const float budgetR = std::sqrt(std::max(0.0F, limitR * limitR - forceX * forceX));
  forceR = std::clamp(forceR, -budgetR, budgetR);

  // A light stability assist preserves slides but makes keyboard corrections usable.
  if (!control.drift) {
    forceR -= velocityY_ * 420.0F;
    yawRate_ *= std::exp(-1.35F * dt);
  }

  const float drag = 0.43F * velocityX_ * speedAbs + rolling * std::tanh(velocityX_);
  const float accelX = (forceX - drag - forceF * std::sin(steering)) / mass + velocityY_ * yawRate_;
  const float accelY = (forceF * std::cos(steering) + forceR) / mass - velocityX_ * yawRate_;
  float yawAccel = (frontAxle * forceF * std::cos(steering) - rearAxle * forceR) / inertia;
  // Tire-slip equations are ill-conditioned near zero speed. Blend toward the
  // stable kinematic bicycle solution until the dynamic model becomes useful.
  const float dynamicBlend = std::clamp(speedAbs / 11.0F, 0.0F, 1.0F);
  const float kinematicYaw = velocityX_ * std::tan(steering) / (frontAxle + rearAxle);
  yawAccel += (kinematicYaw - yawRate_) * (1.0F - dynamicBlend) * 9.0F;
  velocityX_ = std::clamp(velocityX_ + accelX * dt, -18.0F, turboTime_ > 0.0F ? 72.0F : 62.0F);
  velocityY_ = std::clamp(velocityY_ + accelY * dt, -24.0F, 24.0F);
  velocityY_ *= std::exp(-(control.drift ? 0.85F : 4.2F) * dt);
  yawRate_ = std::clamp(yawRate_ + yawAccel * dt, -2.5F, 2.5F);
  if (control.drift) {
    // Mild drift assist keeps slides controllable without removing counter-steer.
    const float driftStability = 1.0F - std::exp(-2.4F * dt);
    yawRate_ = std::lerp(yawRate_, kinematicYaw * 1.08F, driftStability);
  } else {
    // Strong road-car stability: preserve the nonlinear tire model but converge
    // toward the predictable bicycle yaw response used by human steering.
    const float stability = 1.0F - std::exp(-6.5F * dt);
    yawRate_ = std::lerp(yawRate_, kinematicYaw, stability);
  }
  if (!road) {
    // Grass/gravel is deliberately punitive: roughly one-third road top speed.
    velocityX_ *= std::exp(-3.8F * dt);
    velocityY_ *= std::exp(-5.0F * dt);
    velocityX_ = std::clamp(velocityX_, -7.0F, 20.5F);
  }
  if (control.throttle < 0.01F && control.brake < 0.01F && std::hypot(velocityX_, velocityY_) < 0.35F)
    velocityX_ = velocityY_ = yawRate_ = 0.0F;

  angle_ += yawRate_ * dt;
  previousX_ = x_;
  previousY_ = y_;
  x_ += (std::cos(angle_) * velocityX_ - std::sin(angle_) * velocityY_) * dt * 14.0F;
  y_ += (std::sin(angle_) * velocityX_ + std::cos(angle_) * velocityY_) * dt * 14.0F;
  const int wallSample=nearestCourseSample(x_,y_);
  if(guardedSample(wallSample)){
    const auto center=courseSample(wallSample),ahead=courseSample(wallSample+1);float tx=ahead.x()-center.x(),ty=ahead.y()-center.y();const float length=std::hypot(tx,ty);tx/=length;ty/=length;
    const float side=(x_-center.x())*-ty+(y_-center.y())*tx;
    if(std::abs(side)>78.0F&&shortcutItem_!=kDiamondRod){const float sign=side<0?-1.0F:1.0F,correction=std::min(std::abs(side)-78.0F,2.5F);x_+=ty*sign*correction;y_-=tx*sign*correction;velocityX_*=.985F;velocityY_=std::lerp(velocityY_,-velocityY_*.18F,.35F);yawRate_*=.92F;wallFxTime_=.10F;}
  }
  if(trackIndex_==kWildWestTrack)resolveHazards(dt);
  lapTime_ += dt;

  // Forward-biased dead-zone camera: the view trails a focus point ahead of the
  // car, placing the player behind screen center so upcoming corners stay visible.
  const float zoom = std::clamp(1.30F - std::abs(velocityX_) / 420.0F, 1.08F, 1.30F);
  const float lookAhead = std::clamp(48.0F + std::abs(velocityX_) * 2.15F, 48.0F, 165.0F);
  const float focusX = x_ + std::cos(angle_) * lookAhead;
  const float focusY = y_ + std::sin(angle_) * lookAhead;
  const float deadX = width_ * 0.10F / zoom;
  const float deadY = height_ * 0.09F / zoom;
  const float worldLimitX=trackIndex_==kWildWestTrack?2800.0F:850.0F,worldLimitY=trackIndex_==kWildWestTrack?1750.0F:560.0F;
  const bool insideFollowWorld=x_>-worldLimitX&&x_<worldLimitX&&y_>-worldLimitY&&y_<worldLimitY;
  if (insideFollowWorld) {
    if (focusX - cameraX_ > deadX) cameraX_ = focusX - deadX;
    if (focusX - cameraX_ < -deadX) cameraX_ = focusX + deadX;
    if (focusY - cameraY_ > deadY) cameraY_ = focusY - deadY;
    if (focusY - cameraY_ < -deadY) cameraY_ = focusY + deadY;
    const float cameraLimitX=trackIndex_==kWildWestTrack?2450.0F:510.0F,cameraLimitY=trackIndex_==kWildWestTrack?1550.0F:330.0F;
    cameraX_=std::clamp(cameraX_,-cameraLimitX,cameraLimitX);cameraY_=std::clamp(cameraY_,-cameraLimitY,cameraLimitY);
  }
  const float screenX = width_ * 0.5F + (x_ - cameraX_) * zoom;
  const float screenY = height_ * 0.5F + (y_ - cameraY_) * zoom;
  const bool escaped = screenX < -38.0F || screenX > width_ + 38.0F ||
                       screenY < -38.0F || screenY > height_ + 38.0F;
  const int newProgress = nearestCourseSample(x_, y_);
  int progressDelta = (newProgress - progressSample_ + kSampleCount/2) % kSampleCount - kSampleCount/2;
  progressDelta = std::clamp(progressDelta, -3, 12);
  progressSample_ = newProgress;
  trainingReward_ = progressDelta * 4.0F + std::max(velocityX_, 0.0F) * 0.006F -
                    (road ? 0.0F : 1.25F) + (turboTime_ > 0.0F ? 0.055F : 0.0F);
  if (!trainingMode_) resolveRivalCollisions();
  if (escaped && trackIndex_!=kWildWestTrack && (trainingMode_ || (countdownTicks_ <= 0 && simulationTick_ > 1200))) {
    if (trainingMode_) { trainingReward_ -= 90.0F; trainingTerminal_ = true; }
    else reset();
    return;
  }

  // Mario Kart-style ordered key checkpoints. A gate counts only when its
  // plane is crossed forward and within the road-width span.
  const SkPoint gate = trackControl(checkpoint_);
  const SkPoint before = coursePoint(checkpoint_, 0.0F);
  const SkPoint after = coursePoint(checkpoint_, 0.04F);
  float tangentX = after.x() - before.x(), tangentY = after.y() - before.y();
  const float tangentLength = std::hypot(tangentX, tangentY);
  tangentX /= tangentLength; tangentY /= tangentLength;
  const float previousSide = (previousX_ - gate.x()) * tangentX +
                             (previousY_ - gate.y()) * tangentY;
  const float currentSide = (x_ - gate.x()) * tangentX +
                            (y_ - gate.y()) * tangentY;
  const float acrossGate = std::abs((x_ - gate.x()) * -tangentY +
                                    (y_ - gate.y()) * tangentX);
  const float worldVx = std::cos(angle_) * velocityX_ - std::sin(angle_) * velocityY_;
  const float worldVy = std::sin(angle_) * velocityX_ + std::cos(angle_) * velocityY_;
  const bool movingForward = worldVx * tangentX + worldVy * tangentY > 1.0F;
  const int wildGateDelta=(newProgress-checkpoint_*kSamplesPerSegment+kSampleCount)%kSampleCount;
  const bool wildCheckpoint=trackIndex_==kWildWestTrack&&wildGateDelta<10&&movingForward&&progressDelta>=0;
  if ((previousSide < 0.0F && currentSide >= 0.0F && acrossGate < 138.0F && movingForward) || wildCheckpoint) {
    checkpoint_ = (checkpoint_ + 1) % static_cast<int>(kCourse.size());
    if (trainingMode_) trainingReward_ += 35.0F;
    if (checkpoint_ == 1) {
      ++laps_;
      if (bestLap_ == 0.0F || lapTime_ < bestLap_) bestLap_ = lapTime_;
      lapTime_ = 0.0F;
      if (trainingMode_) trainingReward_ += 500.0F;
    }
  }
  if (!trainingMode_ && laps_ >= (trackIndex_==kWildWestTrack?1:3) && !raceAwarded_) {
    raceAwarded_ = true;
    if(trackIndex_==kWildWestTrack){velocityX_=velocityY_=yawRate_=0.0F;}
    constexpr std::array<int,8> points{15,12,10,8,6,4,2,1};
    championshipPoints_[0] += points[std::clamp(placement_-1,0,7)];
    for (int i=0;i<7;++i) championshipPoints_[i+1] += points[std::clamp(rivals_[i].place-1,0,7)];
    drivingProfile_[0]=std::lerp(drivingProfile_[0],std::clamp(offroadTime_/std::max(bestLap_*3.0F,1.0F),0.0F,1.0F),.25F);
    drivingProfile_[1]=std::lerp(drivingProfile_[1],std::clamp(miniTurbos_/20.0F,0.0F,1.0F),.25F);
    drivingProfile_[2]=std::lerp(drivingProfile_[2],std::clamp(itemUses_/12.0F,0.0F,1.0F),.25F);
    rivals_[6].personality=drivingProfile_[1]>.45F?0:(drivingProfile_[0]>.25F?4:1);
    if (const char* local=std::getenv("LOCALAPPDATA")) {
      std::filesystem::path dir=std::filesystem::path(local)/"FIADA"; std::error_code ec; std::filesystem::create_directories(dir,ec);
      std::ofstream out(dir/"championship-v1.dat",std::ios::binary);
      out.write(reinterpret_cast<const char*>(championshipPoints_.data()),sizeof(championshipPoints_));
      out.write(reinterpret_cast<const char*>(drivingProfile_.data()),sizeof(drivingProfile_));
    }
  }
}

void Game::drawTrack(SkCanvas& canvas) const {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(trackIndex_==kWildWestTrack?SkColorSetRGB(105,62,29):SkColorSetRGB(14,18,20));
  canvas.drawPaint(paint);

  // Subtle gold terrain bands frame the circuit without competing with it.
  paint.setColor(SkColorSetRGB(25, 25, 22));
  for (int x = -1400; x < 1400; x += 110)
    canvas.drawRect(SkRect::MakeXYWH(x, -850, 54, 1700), paint);

  const SkPath course = coursePath();
  SkPaint stroke;
  stroke.setAntiAlias(true);
  stroke.setStyle(SkPaint::kStroke_Style);
  stroke.setStrokeCap(SkPaint::kRound_Cap);
  stroke.setStrokeJoin(SkPaint::kRound_Join);

  stroke.setStrokeWidth(180.0F);
  stroke.setColor(trackIndex_==kWildWestTrack?SkColorSetRGB(92,48,24):SkColorSetRGB(116,76,10));
  canvas.drawPath(course, stroke);
  stroke.setStrokeWidth(166.0F);
  stroke.setColor(trackIndex_==kWildWestTrack?SkColorSetRGB(207,137,56):SkColorSetRGB(232,174,43));
  canvas.drawPath(course, stroke);
  stroke.setStrokeWidth(148.0F);
  stroke.setColor(trackIndex_==kWildWestTrack?SkColorSetRGB(77,62,48):SkColorSetRGB(39,40,43));
  canvas.drawPath(course, stroke);

  stroke.setStrokeWidth(3.0F);
  stroke.setColor(SkColorSetARGB(190, 255, 216, 92));
  constexpr std::array<SkScalar, 2> dashes{24.0F, 20.0F};
  stroke.setPathEffect(SkDashPathEffect::Make(dashes.data(), static_cast<int>(dashes.size()), 0.0F));
  canvas.drawPath(course, stroke);
  stroke.setPathEffect(nullptr);

  // Partial guardrails protect dangerous bends while leaving deliberate off-road cuts open.
  paint.setColor(SkColorSetRGB(231,174,45));
  for(int sample=0;sample<kSampleCount;sample+=5)if(guardedSample(sample)){
    const auto c=courseSample(sample),q=courseSample(sample+1);float tx=q.x()-c.x(),ty=q.y()-c.y(),length=std::hypot(tx,ty);tx/=length;ty/=length;
    for(float side:{-1.0F,1.0F}){const float rx=c.x()-ty*side*88.0F,ry=c.y()+tx*side*88.0F;canvas.drawCircle(rx,ry,5.5F,paint);}
  }

  // Item boxes remain on the main circuit; cuts are player-chosen off-road lines.
  for(std::size_t i=0;i<kItemBoxSamples.size();++i){
    const auto p=courseSample(kItemBoxSamples[i]);
    paint.setColor(SkColorSetARGB(220,35,195,235));
    SkPath box; box.moveTo(p.x(),p.y()-15);box.lineTo(p.x()+15,p.y());box.lineTo(p.x(),p.y()+15);box.lineTo(p.x()-15,p.y());box.close();
    canvas.drawPath(box,paint); paint.setColor(SK_ColorWHITE); paint.setStyle(SkPaint::kStroke_Style);paint.setStrokeWidth(2);canvas.drawPath(box,paint);paint.setStyle(SkPaint::kFill_Style);
  }

  // Start line, rotated to the local spline normal.
  canvas.save();
  canvas.translate(trackControl(0).x(), trackControl(0).y());
  const auto startDirection = coursePoint(0, 0.05F);
  canvas.rotate(std::atan2(startDirection.y()-trackControl(0).y(), startDirection.x()-trackControl(0).x()) * 180.0F / kPi);
  constexpr float tile = 10.0F;
  for (int row = 0; row < 15; ++row)
    for (int col = 0; col < 3; ++col) {
      paint.setColor(((row + col) & 1) ? SkColorSetRGB(255, 205, 64) : SK_ColorBLACK);
      canvas.drawRect(SkRect::MakeXYWH(-15 + col * tile, -75 + row * tile, tile, tile), paint);
    }
  canvas.restore();

  if (cone_) {
    for (int i = 2; i < static_cast<int>(kCourse.size()); i += 2) {
      const SkPoint c = trackControl(i);
      canvas.drawImageRect(cone_, SkRect::MakeXYWH(c.x() - 12, c.y() - 16, 24, 32),
                           SkSamplingOptions(SkFilterMode::kLinear), nullptr);
    }
  }
}
void Game::drawCar(SkCanvas& canvas) const {
  canvas.save();
  canvas.translate(x_, y_);
  canvas.rotate(angle_ * 180.0F / kPi);
  if (car_) {
    canvas.drawImageRect(car_, SkRect::MakeXYWH(-34, -18, 68, 36),
                         SkSamplingOptions(SkFilterMode::kLinear), nullptr);
  } else {
    SkPaint fallback;
    fallback.setColor(SkColorSetRGB(238, 62, 73));
    fallback.setAntiAlias(true);
    canvas.drawRoundRect(SkRect::MakeXYWH(-32, -17, 64, 34), 9, 9, fallback);
  }
  canvas.restore();
}

void Game::drawHud(SkCanvas& canvas) const {
  SkPaint panel;
  panel.setColor(SkColorSetARGB(210, 10, 14, 20));
  panel.setAntiAlias(true);
  canvas.drawRoundRect(SkRect::MakeXYWH(22, 22, 340, 174), 16, 16, panel);
  text(canvas, aiEnabled_ ? "FIADA  [AI]" : "FIADA  [HUMAN]", 42, 56, 26, SkColorSetRGB(255, 202, 58));
  text(canvas, std::format("SPEED  {:03.0f} km/h", std::abs(velocityX_) * 3.6F), 42, 86, 18, SK_ColorWHITE);
  text(canvas, std::format("LAP    {}/{}   {:05.2f}s", std::min(laps_+1,trackIndex_==kWildWestTrack?1:3),trackIndex_==kWildWestTrack?1:3,lapTime_), 42, 112, 18, SK_ColorWHITE);
  const auto best = bestLap_ > 0.0F ? std::format("BEST   {:05.2f}s", bestLap_) : "BEST   --.--s";
  text(canvas, best, 42, 136, 16, SkColorSetRGB(166, 184, 205));
  const auto turbo = std::format("DRIFT  {:03.0f}%{}", driftCharge_ * 100.0F, turboTime_ > 0.0F ? "  TURBO!" : "");
  text(canvas, std::format("CHECKPOINT  {:02}/{}", checkpoint_, kCourse.size()), 174, 136, 14, SkColorSetRGB(166, 184, 205));
  text(canvas, turbo, 42, 160, 16, turboTime_ > 0.0F ? SkColorSetRGB(64, 224, 255) : SkColorSetRGB(255, 174, 62));
  const char* itemName=heldItem_==kHorn?"HORN":heldItem_==kDiamondRod?"DIAMOND ON A ROD":"EMPTY";
  text(canvas,std::format("ITEM   {}   USES {}   OFFROAD CUTS {}",itemName,itemUses_,shortcutsTaken_),42,184,15,SkColorSetRGB(105,224,255));
  text(canvas, "WASD / ARROWS DRIVE   HOLD SPACE DRIFT   SHIFT USE ITEM   P AI   TAB 2X AI   R RESET", 24, height_ - 24.0F, 15,
       SkColorSetARGB(220, 255, 255, 255));
}

void Game::render(SkCanvas& canvas) {
  canvas.clear(SK_ColorBLACK);
  if (frontEnd_) { drawFrontEnd(canvas); return; }
  canvas.save();
  canvas.translate(width_ * 0.5F, height_ * 0.5F);
  const float zoom = std::clamp(1.30F - std::abs(velocityX_) / 420.0F, 1.08F, 1.30F);
  canvas.scale(zoom, zoom);
  canvas.translate(-cameraX_, -cameraY_);
  drawTrack(canvas);
  drawRivals(canvas);
  drawCar(canvas);
  drawEffects(canvas);
  canvas.restore();
  drawHud(canvas);
}

void Game::drawFrontEnd(SkCanvas& canvas) const {
  SkPaint p;p.setAntiAlias(true);
p.setColor(SkColorSetRGB(7,10,16));canvas.drawPaint(p);
  p.setColor(SkColorSetRGB(34,27,13));for(int x=-height_;x<width_;x+=92)canvas.drawRect(SkRect::MakeXYWH(static_cast<float>(x),0,28.0F,static_cast<float>(height_)),p);
  p.setStyle(SkPaint::kStroke_Style);p.setStrokeWidth(4);p.setColor(SkColorSetRGB(210,153,38));canvas.drawRoundRect(SkRect::MakeXYWH(width_*.12F,height_*.10F,width_*.76F,height_*.80F),28,28,p);p.setStyle(SkPaint::kFill_Style);
  text(canvas,"FIADA",width_*.5F-150,height_*.25F,88,SkColorSetRGB(255,203,62));
  text(canvas,"FACIO LUDUM AUTOCINETUM",width_*.5F-178,height_*.31F,18,SkColorSetRGB(198,177,125));
  if(titlePage_){
    text(canvas,"NEURAL GRAND PRIX",width_*.5F-128,height_*.46F,30,SK_ColorWHITE);
    text(canvas,"PRESS ENTER  /  CONTROLLER A",width_*.5F-180,height_*.67F,22,SkColorSetRGB(78,221,255));
    text(canvas,"Eight drivers. Deterministic combat. Five circuits.",width_*.5F-230,height_*.75F,17,SkColorSetRGB(180,188,204));
    return;
  }
  if(labTrackSelect_){
    constexpr std::array<std::string_view,6> tracks{"GOLD CIRCUIT","ALPINE SWITCHBACKS","VOLCANIC FOUNDRY","COASTAL CAUSEWAY","NEON CITY","GRAND WILD WEST"};
    text(canvas,"AI LAB — SELECT TRACK",width_*.5F-165,height_*.39F,28,SK_ColorWHITE);
    p.setColor(SkColorSetARGB(235,169,112,18));canvas.drawRoundRect(SkRect::MakeXYWH(width_*.25F,height_*.48F,width_*.50F,86),14,14,p);
    text(canvas,tracks[trackIndex_],width_*.5F-145,height_*.535F,24,SK_ColorWHITE);
    text(canvas,"UP / DOWN CHOOSE    ENTER / A START    ESC / BACKSPACE BACK",width_*.5F-315,height_*.72F,16,SkColorSetRGB(78,221,255));return;
  }
  constexpr std::array<std::string_view,3> names{"QUICK RACE","CHAMPIONSHIP","AI LAB"};
  constexpr std::array<std::string_view,3> descriptions{"Choose a circuit and race seven rivals","Five-race points campaign and adaptive champion","Spectate neural drivers with live telemetry"};
  for(int i=0;i<3;++i){const float y=height_*(.40F+i*.135F);p.setColor(i==modeSelection_?SkColorSetARGB(235,169,112,18):SkColorSetARGB(220,18,24,34));canvas.drawRoundRect(SkRect::MakeXYWH(width_*.25F,y-39,width_*.50F,76),14,14,p);text(canvas,names[i],width_*.29F,y-5,24,i==modeSelection_?SK_ColorWHITE:SkColorSetRGB(185,190,202));text(canvas,descriptions[i],width_*.29F,y+20,14,i==modeSelection_?SkColorSetRGB(255,226,148):SkColorSetRGB(130,140,155));}
  text(canvas,"UP / DOWN TO SELECT    ENTER / A TO START",width_*.5F-225,height_*.85F,17,SkColorSetRGB(78,221,255));
}
void Game::updateRivals(float dt) {
  const float playerProgress = static_cast<float>(laps_ * kSampleCount + nearestCourseSample(x_, y_));
  for (int i=0;i<7;++i) {
    auto& r=rivals_[i];
    const int nearest=nearestCourseSample(r.x,r.y);
    const int look=10+(i%5==3?4:0);
    const auto target=courseSample(nearest+look);
    const float desired=std::atan2(target.y()-r.y,target.x()-r.x);
    r.angle+=std::clamp(wrapAngle(desired-r.angle),-1.8F*dt,1.8F*dt);
    float curve=std::abs(wrapAngle(std::atan2(courseSample(nearest+22).y()-r.y,courseSample(nearest+22).x()-r.x)-r.angle));
    const float personalitySpeed[5]={46,43,48,50,42};
    float wanted=personalitySpeed[i%5]-curve*13.0F;
    // Deterministic drafting and rubber-free difficulty tuning.
    float draft=0.0F;
    for(int j=0;j<7;++j) if(i!=j) {
      const float dx=rivals_[j].x-r.x,dy=rivals_[j].y-r.y,d=std::hypot(dx,dy);
      if(d<145&&d>25&&std::cos(r.angle)*dx+std::sin(r.angle)*dy>0) draft=std::max(draft,(145-d)/145.0F);
    }
    wanted+=draft*5.0F+0.0F;
    r.speed+=std::clamp(wanted-r.speed,-18.0F*dt,12.0F*dt);
    r.x+=std::cos(r.angle)*r.speed*dt*14.0F;r.y+=std::sin(r.angle)*r.speed*dt*14.0F;
    if(trackIndex_==kWildWestTrack){
      const auto base=courseSample(218),next=courseSample(219);float tx=next.x()-base.x(),ty=next.y()-base.y(),length=std::hypot(tx,ty);tx/=length;ty/=length;const float nx=-ty,ny=tx,offset=std::sin(simulationTick_*.0062F)*330.0F,trainX=base.x()+nx*offset,trainY=base.y()+ny*offset;
      if(std::abs((r.x-trainX)*tx+(r.y-trainY)*ty)<43.0F&&std::abs((r.x-trainX)*nx+(r.y-trainY)*ny)<175.0F)r.speed*=.45F;
      constexpr std::array<int,5> tumble{58,138,252,318,392};for(std::size_t h=0;h<tumble.size();++h){const auto c=courseSample(tumble[h]),q=courseSample(tumble[h]+1);float hx=q.x()-c.x(),hy=q.y()-c.y(),hl=std::hypot(hx,hy);hx/=hl;hy/=hl;const float wave=std::sin(simulationTick_*(.010F+h*.0013F)+h*1.7F)*105.0F;if(std::hypot(r.x-(c.x()-hy*wave),r.y-(c.y()+hx*wave))<34.0F)r.speed*=.80F;}
    }
    int now=nearestCourseSample(r.x,r.y); if(nearest>kSampleCount-20&&now<20)++r.lap;
    r.progress=static_cast<float>(r.lap*kSampleCount+now);
    if((simulationTick_+i*97)%1700==0)r.item=1+static_cast<int>(mixBits(static_cast<std::uint32_t>(simulationTick_+i*313))%2);
    if(r.item&&((simulationTick_+i*31)%420==0)){if(r.item==1&&std::hypot(r.x-x_,r.y-y_)<190.0F){velocityX_*=.45F;velocityY_*=.45F;}else r.turbo=1.0F;r.item=0;}
    r.turbo=std::max(0.0F,r.turbo-dt);r.hornFx=std::max(0.0F,r.hornFx-dt);
  }
  placement_=1;for(const auto&r:rivals_)if(r.progress>playerProgress)++placement_;
  for(int i=0;i<7;++i){int p=1;for(int j=0;j<7;++j)if(rivals_[j].progress>rivals_[i].progress)++p;if(playerProgress>rivals_[i].progress)++p;rivals_[i].place=p;}
}

void Game::resolveHazards(float) {
  auto tangentAt=[](int sample){const auto a=courseSample(sample),b=courseSample(sample+1);float x=b.x()-a.x(),y=b.y()-a.y(),l=std::hypot(x,y);return SkPoint::Make(x/l,y/l);};
  const auto trainBase=courseSample(218),trainTangent=tangentAt(218);const SkPoint trainNormal=SkPoint::Make(-trainTangent.y(),trainTangent.x());
  const float trainOffset=std::sin(simulationTick_*.0062F)*330.0F,trainX=trainBase.x()+trainNormal.x()*trainOffset,trainY=trainBase.y()+trainNormal.y()*trainOffset;
  const float trainAcross=(x_-trainX)*trainTangent.x()+(y_-trainY)*trainTangent.y(),trainAlong=(x_-trainX)*trainNormal.x()+(y_-trainY)*trainNormal.y();
  if(std::abs(trainAcross)<43.0F&&std::abs(trainAlong)<175.0F){velocityX_*=.42F;velocityY_=std::lerp(velocityY_,trainOffset>=0?5.0F:-5.0F,.45F);yawRate_+=trainOffset>=0?.22F:-.22F;hazardFxTime_=.28F;++hazardHits_;}
  constexpr std::array<int,5> tumbleSamples{58,138,252,318,392};
  for(std::size_t i=0;i<tumbleSamples.size();++i){const auto c=courseSample(tumbleSamples[i]);const auto t=tangentAt(tumbleSamples[i]);const float wave=std::sin(simulationTick_*(.010F+i*.0013F)+i*1.7F)*105.0F;const float hx=c.x()-t.y()*wave,hy=c.y()+t.x()*wave,dx=x_-hx,dy=y_-hy,d=std::hypot(dx,dy);if(d<34.0F){const float side=(dx*(-std::sin(angle_))+dy*std::cos(angle_))<0?-1.0F:1.0F;velocityX_*=.80F;velocityY_+=side*2.0F;yawRate_+=side*.24F;hazardFxTime_=.20F;++hazardHits_;}}
  constexpr std::array<int,4> boulderSamples{92,184,292,374};
  for(std::size_t i=0;i<boulderSamples.size();++i){const auto c=courseSample(boulderSamples[i]),t=tangentAt(boulderSamples[i]);const float side=i%2?1.0F:-1.0F,bx=c.x()-t.y()*side*48.0F,by=c.y()+t.x()*side*48.0F,dx=x_-bx,dy=y_-by,d=std::hypot(dx,dy);if(d<39.0F&&d>.01F){const float correction=std::min(39.0F-d,2.0F);x_+=dx/d*correction;y_+=dy/d*correction;velocityX_*=.90F;velocityY_+=((-std::sin(angle_)*dx+std::cos(angle_)*dy)/d)*.6F;hazardFxTime_=.12F;}}
}
void Game::resolveRivalCollisions() {
  const float ca=std::cos(angle_),sa=std::sin(angle_);
  for(auto& r:rivals_){
    const float dx=r.x-x_,dy=r.y-y_,localX=dx*ca+dy*sa,localY=-dx*sa+dy*ca,delta=r.angle-angle_;
    const float halfX=32.0F+std::abs(std::cos(delta))*30.0F+std::abs(std::sin(delta))*15.0F;
    const float halfY=16.0F+std::abs(std::sin(delta))*30.0F+std::abs(std::cos(delta))*15.0F;
    const float metric=(localX*localX)/(halfX*halfX)+(localY*localY)/(halfY*halfY);
    if(metric<1.0F&&r.turbo<=0.0F&&!(shortcutItem_==kHorn&&itemEffectTime_>0.0F)){
      float gx=localX/(halfX*halfX),gy=localY/(halfY*halfY),length=std::hypot(gx,gy);if(length<.0001F){gx=0;gy=1;length=1;}
      gx/=length;gy/=length;const float nx=gx*ca-gy*sa,ny=gx*sa+gy*ca;
      const float correction=std::min((1.0F-std::sqrt(std::max(metric,0.0F)))*halfY*.35F,1.6F);
      x_-=nx*correction;r.x+=nx*correction;const float localNormal=-sa*nx+ca*ny;
      velocityY_-=localNormal*std::min(2.2F,std::abs(velocityX_-r.speed)*.08F+.35F);velocityX_*=.992F;r.speed*=.988F;impactFxTime_=.14F;
    }
  }
}

void Game::drawHazards(SkCanvas& canvas) const {
  if(trackIndex_!=kWildWestTrack)return;
  auto tangentAt=[](int sample){const auto a=courseSample(sample),b=courseSample(sample+1);float x=b.x()-a.x(),y=b.y()-a.y(),l=std::hypot(x,y);return SkPoint::Make(x/l,y/l);};
  SkPaint p;p.setAntiAlias(true);
  for(int sample=18;sample<kSampleCount;sample+=48){const auto c=courseSample(sample),t=tangentAt(sample);const float side=(sample/48)%2?1.0F:-1.0F,cx=c.x()-t.y()*side*145.0F,cy=c.y()+t.x()*side*145.0F;p.setColor(SkColorSetRGB(36,112,57));canvas.drawRect(SkRect::MakeXYWH(cx-7,cy-32,14,64),p);canvas.drawRect(SkRect::MakeXYWH(cx-22,cy-15,44,10),p);}

  for(int sample:{115,125,135,340,350,360}){const auto c=courseSample(sample);p.setColor(SkColorSetARGB(38,225,164,72));canvas.drawCircle(c.x(),c.y(),92,p);}
  constexpr std::array<int,4> boulders{92,184,292,374};for(std::size_t i=0;i<boulders.size();++i){const auto c=courseSample(boulders[i]),t=tangentAt(boulders[i]);const float side=i%2?1.0F:-1.0F;p.setColor(SkColorSetRGB(105,62,35));canvas.drawCircle(c.x()-t.y()*side*48,c.y()+t.x()*side*48,27,p);p.setColor(SkColorSetRGB(168,104,53));canvas.drawCircle(c.x()-t.y()*side*48-5,c.y()+t.x()*side*48-6,12,p);}
  constexpr std::array<int,5> tumble{58,138,252,318,392};p.setStyle(SkPaint::kStroke_Style);p.setStrokeWidth(4);for(std::size_t i=0;i<tumble.size();++i){const auto c=courseSample(tumble[i]),t=tangentAt(tumble[i]);const float wave=std::sin(simulationTick_*(.010F+i*.0013F)+i*1.7F)*105.0F,x=c.x()-t.y()*wave,y=c.y()+t.x()*wave;p.setColor(SkColorSetRGB(184,121,53));canvas.drawCircle(x,y,22,p);canvas.drawLine(x-16,y-12,x+16,y+12,p);canvas.drawLine(x-16,y+12,x+16,y-12,p);}p.setStyle(SkPaint::kFill_Style);
  const auto base=courseSample(218),t=tangentAt(218);const SkPoint n=SkPoint::Make(-t.y(),t.x());const float offset=std::sin(simulationTick_*.0062F)*330.0F,cx=base.x()+n.x()*offset,cy=base.y()+n.y()*offset;p.setColor(SkColorSetRGB(75,32,22));for(float along:{-110.0F,0.0F,110.0F}){canvas.save();canvas.translate(cx+n.x()*along,cy+n.y()*along);canvas.rotate(std::atan2(n.y(),n.x())*180.0F/kPi);canvas.drawRoundRect(SkRect::MakeXYWH(-52,-25,104,50),6,6,p);p.setColor(SkColorSetRGB(235,177,52));canvas.drawCircle(-28,27,9,p);canvas.drawCircle(28,27,9,p);p.setColor(SkColorSetRGB(75,32,22));canvas.restore();}
}
void Game::drawRivals(SkCanvas& canvas) const {
  constexpr std::array<SkColor,7> colors{SK_ColorCYAN,SK_ColorGREEN,SK_ColorMAGENTA,SK_ColorYELLOW,SkColorSetRGB(255,120,40),SkColorSetRGB(120,140,255),SkColorSetRGB(245,245,245)};
  SkPaint p;p.setAntiAlias(true);

  for(int i=0;i<7;++i){const auto&r=rivals_[i];canvas.save();canvas.translate(r.x,r.y);canvas.rotate(r.angle*180.0F/kPi);p.setColor(colors[i]);canvas.drawRoundRect(SkRect::MakeXYWH(-30,-15,60,30),8,8,p);p.setColor(SK_ColorBLACK);canvas.drawRect(SkRect::MakeXYWH(3,-11,14,22),p);if(r.turbo>0){p.setColor(SkColorSetARGB(180,60,220,255));canvas.drawCircle(-37,0,9,p);}canvas.restore();}
}

void Game::drawEffects(SkCanvas& canvas) const {
  SkPaint p;p.setAntiAlias(true);
p.setStyle(SkPaint::kStroke_Style);
  if(turboTime_>0){p.setStrokeWidth(7);p.setColor(SkColorSetARGB(190,55,220,255));for(float side:{-10.0F,10.0F})canvas.drawLine(x_-std::cos(angle_)*30-std::sin(angle_)*side,y_-std::sin(angle_)*30+std::cos(angle_)*side,x_-std::cos(angle_)*58-std::sin(angle_)*side,y_-std::sin(angle_)*58+std::cos(angle_)*side,p);}
  if(shortcutItem_==kHorn&&itemEffectTime_>0){const float phase=1.0F-itemEffectTime_/.32F;p.setStrokeWidth(7.0F*(1-phase)+2);p.setColor(SkColorSetARGB(static_cast<U8CPU>(220*(1-phase)),255,211,70));canvas.drawCircle(x_,y_,35.0F+phase*165.0F,p);}
  for(const auto&r:rivals_)if(r.hornFx>0){const float phase=1-r.hornFx/.32F;p.setStrokeWidth(4);p.setColor(SkColorSetARGB(static_cast<U8CPU>(180*(1-phase)),255,190,35));canvas.drawCircle(r.x,r.y,30+phase*160,p);}
  if(shortcutItem_==kDiamondRod&&itemEffectTime_>0){const float bob=std::sin(simulationTick_*.12F)*7.0F,dx=x_+std::cos(angle_)*72.0F-std::sin(angle_)*bob,dy=y_+std::sin(angle_)*72.0F+std::cos(angle_)*bob;p.setStrokeWidth(3);p.setColor(SkColorSetRGB(220,230,245));canvas.drawLine(x_+std::cos(angle_)*25,y_+std::sin(angle_)*25,dx,dy,p);p.setStyle(SkPaint::kFill_Style);p.setColor(SkColorSetRGB(75,225,255));SkPath gem;gem.moveTo(dx,dy-14);gem.lineTo(dx+11,dy);gem.lineTo(dx,dy+14);gem.lineTo(dx-11,dy);gem.close();canvas.drawPath(gem,p);p.setStyle(SkPaint::kStroke_Style);}
  const float sparkTime=std::max({impactFxTime_,wallFxTime_,hazardFxTime_});if(sparkTime>0){p.setStrokeWidth(3);p.setColor(hazardFxTime_>0?SkColorSetRGB(255,90,45):(impactFxTime_>0?SkColorSetRGB(255,220,90):SkColorSetRGB(255,150,40)));for(int i=0;i<8;++i){const float a=i*kPi*.25F+simulationTick_*.17F,length=10+static_cast<float>((i*7)%13);canvas.drawLine(x_+std::cos(a)*20,y_+std::sin(a)*20,x_+std::cos(a)*(20+length),y_+std::sin(a)*(20+length),p);}}
}
void Game::drawGrandPrixHud(SkCanvas& canvas) const {
  text(canvas,std::format("PLACE {}/8  TRACK {}/6  PTS {}",placement_,trackIndex_+1,championshipPoints_[0]),width_-190.0F,58,28,SkColorSetRGB(255,205,55));
  if(raceAwarded_&&trackIndex_==kWildWestTrack)text(canvas,"FINISH — GRAND WILD WEST",width_*.5F-210,height_*.30F,36,SkColorSetRGB(255,210,70));
  if(countdownTicks_>0){const int n=(countdownTicks_+119)/120;text(canvas,n>0?std::format("{}",n):"GO!",width_*.48F,height_*.35F,72,SK_ColorWHITE);}
  if(labMode_){SkPaint p;p.setColor(SkColorSetARGB(220,5,10,18));canvas.drawRoundRect(SkRect::MakeXYWH(width_-370.0F,82,345,245),14,14,p);text(canvas,"AI LAB — LIVE TELEMETRY",width_-350.0F,112,18,SkColorSetRGB(80,220,255));text(canvas,std::format("OBS heading {:.2f} lateral {:.2f}",observation()[0],observation()[2]),width_-350.0F,142,14,SK_ColorWHITE);text(canvas,std::format("OUT throttle {:.2f} steer {:.2f}",cachedPolicyOutput_[0],cachedPolicyOutput_[2]),width_-350.0F,166,14,SK_ColorWHITE);text(canvas,std::format("HIDDEN mean {:.3f}",std::accumulate(policyState_.recurrent.begin(),policyState_.recurrent.end(),0.0F)/policyState_.recurrent.size()),width_-350.0F,190,14,SK_ColorWHITE);for(int i=0;i<7;++i)text(canvas,std::format("R{}  P{}  profile {}",i+1,rivals_[i].place,rivals_[i].personality),width_-350.0F,216+i*15,12,SkColorSetRGB(190,200,215));}
}
std::uint64_t Game::deterministicHash() const {
  std::uint64_t h=1469598103934665603ULL;
  auto add=[&](std::uint64_t v){h^=v;h*=1099511628211ULL;};
  add(simulationTick_);add(static_cast<std::uint64_t>(trackIndex_));add(static_cast<std::uint64_t>(placement_));
  add(static_cast<std::uint64_t>(std::lround(x_*16)));add(static_cast<std::uint64_t>(std::lround(y_*16)));
  for(const auto&r:rivals_){add(static_cast<std::uint64_t>(std::lround(r.x*16)));add(static_cast<std::uint64_t>(std::lround(r.y*16)));add(r.place);}
  return h;
}

}  // namespace fiada

