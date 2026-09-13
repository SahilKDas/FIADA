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
constexpr int kSamplesPerSegment = 12;
constexpr int kSampleCount = static_cast<int>(kCourse.size()) * kSamplesPerSegment;

enum ItemType { kNoItem=0, kDiamond=1, kFeather=2, kGoldKey=3 };
struct Shortcut { int item, start, end; SkPoint middle; };
constexpr std::array<Shortcut,3> kShortcuts{{
  {kDiamond,4,7,{260,-450}}, {kFeather,14,18,{-390,-255}}, {kGoldKey,25,29,{120,525}}
}};
constexpr std::array<int,7> kItemBoxSamples{{38,92,151,207,269,334,394}};

float segmentDistance(float x,float y,SkPoint a,SkPoint b) {
  const float abx=b.x()-a.x(), aby=b.y()-a.y(), apx=x-a.x(), apy=y-a.y();
  const float u=std::clamp((apx*abx+apy*aby)/(abx*abx+aby*aby),0.0F,1.0F);
  return std::hypot(x-(a.x()+u*abx),y-(a.y()+u*aby));
}

SkPoint coursePoint(int segment, float t) {
  const int n = static_cast<int>(kCourse.size());
  const SkPoint& p0 = kCourse[(segment - 1 + n) % n];
  const SkPoint& p1 = kCourse[segment % n];
  const SkPoint& p2 = kCourse[(segment + 1) % n];
  const SkPoint& p3 = kCourse[(segment + 2) % n];
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

float wrapAngle(float value) {
  while (value > kPi) value -= 2.0F * kPi;
  while (value < -kPi) value += 2.0F * kPi;
  return value;
}

float sigmoid(float value) { return 1.0F / (1.0F + std::exp(-value)); }

SkPath coursePath() {
  SkPath path;
  path.moveTo(kCourse[0]);
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

float shortcutDistance(float x,float y,const Shortcut& shortcut) {
  const SkPoint a=kCourse[shortcut.start], b=kCourse[shortcut.end];
  return std::min(segmentDistance(x,y,a,shortcut.middle),segmentDistance(x,y,shortcut.middle,b));
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
  x_ = kCourse[0].x();
  y_ = kCourse[0].y();
  const auto startAhead = coursePoint(0, 0.05F);
  angle_ = std::atan2(startAhead.y() - y_, startAhead.x() - x_);
  velocityX_ = velocityY_ = yawRate_ = 0.0F;
  throttle_ = brake_ = steer_ = driftCharge_ = turboTime_ = 0.0F; miniTurbos_ = 0;
  wasDrifting_ = false; itemUseHeld_=shortcutCounted_=false;
  heldItem_=shortcutItem_=kNoItem; lastItemBox_=-1; simulationTick_=0; itemEffectTime_=0.0F;
  itemPickups_=itemUses_=shortcutsTaken_=0;
  itemPickupsByType_={};shortcutsByType_={};offroadTime_=0.0F;policyTick_=0;cachedPolicyOutput_={};
  policyState_ = {};
  cameraX_ = cameraY_ = 0.0F;
  previousX_ = x_;
  previousY_ = y_;
  lapTime_ = 0.0F;
  checkpoint_ = 1;
}

bool Game::onRoad(float x, float y) const {
  if (courseDistance(x, y) < 78.0F) return true;
  if (itemEffectTime_ <= 0.0F) return false;
  for (const auto& shortcut : kShortcuts)
    if (shortcut.item == shortcutItem_ && shortcutDistance(x,y,shortcut) < (shortcut.item==kGoldKey?58.0F:105.0F)) return true;
  return false;
}

void Game::updateItems(const Input& control) {
  ++simulationTick_;
  itemEffectTime_=std::max(0.0F,itemEffectTime_-1.0F/120.0F);
  if(itemEffectTime_<=0.0F){shortcutItem_=kNoItem;shortcutCounted_=false;}
  if(control.useItem && !itemUseHeld_ && heldItem_!=kNoItem){
    shortcutItem_=heldItem_; itemEffectTime_=heldItem_==kDiamond?6.0F:6.5F;
    if(heldItem_==kDiamond) turboTime_=std::max(turboTime_,1.35F);
    shortcutCounted_=false;
    for(const auto& shortcut:kShortcuts)if(shortcut.item==heldItem_&&checkpoint_>=shortcut.start+1&&checkpoint_<=shortcut.end){
      checkpoint_=(shortcut.end+1)%static_cast<int>(kCourse.size());++shortcutsTaken_;++shortcutsByType_[shortcut.item-1];shortcutCounted_=true;
      if(trainingMode_)trainingReward_+=55.0F;
    }
    heldItem_=kNoItem; ++itemUses_;  }
  itemUseHeld_=control.useItem;
  int touching=-1;
  for(int i=0;i<static_cast<int>(kItemBoxSamples.size());++i){
    const auto p=courseSample(kItemBoxSamples[i]);
    if(std::hypot(x_-p.x(),y_-p.y())<30.0F){touching=i;break;}
  }
  if(touching<0) lastItemBox_=-1;
  if(touching>=0 && touching!=lastItemBox_ && heldItem_==kNoItem){
    const int lateralBand=static_cast<int>(std::floor((x_*0.73F+y_*1.19F)*0.25F));
    const int speedBand=static_cast<int>(std::abs(velocityX_)*7.0F);
    const std::uint32_t signature=static_cast<std::uint32_t>(touching*0x9e3779b9U)^static_cast<std::uint32_t>(laps_*977+checkpoint_*131+lateralBand*17+speedBand*29)^static_cast<std::uint32_t>(simulationTick_/9);
    heldItem_=1+static_cast<int>(mixBits(signature)%3U); ++itemPickups_; ++itemPickupsByType_[heldItem_-1]; lastItemBox_=touching;
  }
  for(const auto& shortcut:kShortcuts){
    if(shortcut.item==shortcutItem_ && shortcutDistance(x_,y_,shortcut)<(shortcut.item==kGoldKey?58.0F:105.0F)){
      const int first=shortcut.start+1, after=(shortcut.end+1)%static_cast<int>(kCourse.size());
      if(checkpoint_>=first && checkpoint_<=shortcut.end) checkpoint_=after;
      if(!shortcutCounted_){++shortcutsTaken_;++shortcutsByType_[shortcut.item-1];shortcutCounted_=true;if(trainingMode_)trainingReward_+=55.0F;}
    }
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
  float shortcutApproach=0.0F;
  const int relevantShortcutItem=heldItem_!=kNoItem?heldItem_:shortcutItem_;
  for(const auto& shortcut:kShortcuts)if(shortcut.item==relevantShortcutItem)
    shortcutApproach=std::max(shortcutApproach,std::exp(-std::hypot(x_-kCourse[shortcut.start].x(),y_-kCourse[shortcut.start].y())/85.0F));
  obs[0]=std::sin(error);obs[1]=std::cos(error);obs[2]=std::clamp(lateral/82.0F,-2.0F,2.0F);
  obs[3]=velocityX_/45.0F;obs[4]=velocityY_/15.0F;obs[5]=yawRate_/2.0F;obs[6]=curve;
  obs[7]=onRoad(x_,y_)?1.0F:0.0F;obs[8]=driftCharge_;obs[9]=std::clamp(turboTime_/1.5F,0.0F,1.0F);
  obs[10]=heldItem_==kDiamond;obs[11]=heldItem_==kFeather;obs[12]=heldItem_==kGoldKey;obs[13]=shortcutApproach;
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
  for(const auto& shortcut:kShortcuts)if(shortcut.item==relevantShortcutItem){
    const SkPoint entry=kCourse[shortcut.start],exit=kCourse[shortcut.end];
    const float entryDistance=std::hypot(x_-entry.x(),y_-entry.y());
    const float middleDistance=std::hypot(x_-shortcut.middle.x(),y_-shortcut.middle.y());
    const SkPoint waypoint=entryDistance>44.0F?entry:(middleDistance>54.0F?shortcut.middle:exit);
    const float shortcutHeading=wrapAngle(std::atan2(waypoint.y()-y_,waypoint.x()-x_)-angle_);
    obs[30]=std::sin(shortcutHeading);obs[31]=std::cos(shortcutHeading);
  }  return obs;
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
  beginTrainingEpisode(seed);
  const auto& shortcut=kShortcuts[std::clamp(itemType,1,3)-1];
  const auto start=kCourse[shortcut.start],before=courseSample(shortcut.start*kSamplesPerSegment-1);
  x_=before.x();y_=before.y();angle_=std::atan2(start.y()-y_,start.x()-x_);velocityX_=20.0F;
  heldItem_=shortcut.item;checkpoint_=shortcut.start+1;progressSample_=shortcut.start*kSamplesPerSegment-1;
  previousX_=x_;previousY_=y_;cameraX_=x_;cameraY_=y_;policyState_={};
}
Input Game::aiInput() {
  const auto obs = observation();
  if (policyWeights_.size() != policy::kParameterCount) {
    const float steer=std::clamp(1.8F*obs[0]-1.25F*obs[2]+1.4F*obs[6]-0.3F*obs[5],-1.0F,1.0F);
    return {.throttle=.72F,.brake=std::abs(obs[6])>.55F?.35F:0.0F,.steer=steer};
  }
  if ((policyTick_++ & 3) == 0) cachedPolicyOutput_=policy::forward(obs,policyWeights_,policyState_);
  const auto& output=cachedPolicyOutput_;
  const float throttle=sigmoid(output[0]), brake=sigmoid(output[1]);
  float steer=std::tanh(output[2]);
  if(shortcutItem_!=kNoItem&&obs[13]>.18F)steer=std::lerp(steer,std::clamp(1.65F*obs[30]-.22F*obs[5],-1.0F,1.0F),.72F);
  return {.throttle=throttle,.brake=brake*(1.0F-throttle),.steer=steer,
          .reset=false,.drift=sigmoid(output[3])>.40F,.toggleAi=false,.useItem=(obs[10]+obs[11]+obs[12])>.5F && obs[13]>.18F && (obs[13]>.55F || sigmoid(output[4])>.05F)};
}

void Game::update(float dt, const Input& input) {
  dt = std::min(dt, 0.05F);
  trainingReward_ = 0.0F;
  if (input.toggleAi && !aiToggleHeld_) aiEnabled_ = !aiEnabled_;
  aiToggleHeld_ = input.toggleAi;
  if (input.reset && !resetHeld_) reset();
  resetHeld_ = input.reset;
  const Input control = aiEnabled_ ? aiInput() : input;
  updateItems(control);

  // Six-state nonlinear bicycle model: compact enough for batched training.
  constexpr float mass = 1180.0F, inertia = 1760.0F;
  constexpr float frontAxle = 1.18F, rearAxle = 1.42F, gravity = 9.81F;
  const bool road = onRoad(x_, y_);
  if (!road) offroadTime_ += dt;
  const float surfaceGrip = (road ? 1.34F : 0.58F) * gripScale_;
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
  const float itemBoost = shortcutItem_==kDiamond && itemEffectTime_>0.0F ? 2400.0F : 0.0F;
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
  lapTime_ += dt;

  // Forward-biased dead-zone camera: the view trails a focus point ahead of the
  // car, placing the player behind screen center so upcoming corners stay visible.
  const float zoom = std::clamp(1.30F - std::abs(velocityX_) / 420.0F, 1.08F, 1.30F);
  const float lookAhead = std::clamp(48.0F + std::abs(velocityX_) * 2.15F, 48.0F, 165.0F);
  const float focusX = x_ + std::cos(angle_) * lookAhead;
  const float focusY = y_ + std::sin(angle_) * lookAhead;
  const float deadX = width_ * 0.10F / zoom;
  const float deadY = height_ * 0.09F / zoom;
  const bool insideFollowWorld = x_ > -850.0F && x_ < 850.0F &&
                                 y_ > -560.0F && y_ < 560.0F;
  if (insideFollowWorld) {
    if (focusX - cameraX_ > deadX) cameraX_ = focusX - deadX;
    if (focusX - cameraX_ < -deadX) cameraX_ = focusX + deadX;
    if (focusY - cameraY_ > deadY) cameraY_ = focusY - deadY;
    if (focusY - cameraY_ < -deadY) cameraY_ = focusY + deadY;
    cameraX_ = std::clamp(cameraX_, -510.0F, 510.0F);
    cameraY_ = std::clamp(cameraY_, -330.0F, 330.0F);
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
  if (escaped) {
    if (trainingMode_) { trainingReward_ -= 90.0F; trainingTerminal_ = true; }
    else reset();
    return;
  }

  // Mario Kart-style ordered key checkpoints. A gate counts only when its
  // plane is crossed forward and within the road-width span.
  const SkPoint gate = kCourse[checkpoint_];
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
  if (previousSide < 0.0F && currentSide >= 0.0F &&
      acrossGate < 88.0F && movingForward) {
    checkpoint_ = (checkpoint_ + 1) % static_cast<int>(kCourse.size());
    if (trainingMode_) trainingReward_ += 35.0F;
    if (checkpoint_ == 1) {
      ++laps_;
      if (bestLap_ == 0.0F || lapTime_ < bestLap_) bestLap_ = lapTime_;
      lapTime_ = 0.0F;
      if (trainingMode_) trainingReward_ += 500.0F;
    }
  }
}

void Game::drawTrack(SkCanvas& canvas) const {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(SkColorSetRGB(14, 18, 20));
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
  stroke.setColor(SkColorSetRGB(116, 76, 10));
  canvas.drawPath(course, stroke);
  stroke.setStrokeWidth(166.0F);
  stroke.setColor(SkColorSetRGB(232, 174, 43));
  canvas.drawPath(course, stroke);
  stroke.setStrokeWidth(148.0F);
  stroke.setColor(SkColorSetRGB(39, 40, 43));
  canvas.drawPath(course, stroke);

  stroke.setStrokeWidth(3.0F);
  stroke.setColor(SkColorSetARGB(190, 255, 216, 92));
  constexpr std::array<SkScalar, 2> dashes{24.0F, 20.0F};
  stroke.setPathEffect(SkDashPathEffect::Make(dashes.data(), static_cast<int>(dashes.size()), 0.0F));
  canvas.drawPath(course, stroke);
  stroke.setPathEffect(nullptr);

  // Item-specific alternate lanes: crystal, feather-white, and keyed gold.
  const std::array<SkColor,3> shortcutColors{SkColorSetRGB(70,220,255),SK_ColorWHITE,SkColorSetRGB(255,191,38)};
  for(std::size_t i=0;i<kShortcuts.size();++i){
    const auto& shortcut=kShortcuts[i]; SkPath lane; lane.moveTo(kCourse[shortcut.start]);
    lane.lineTo(shortcut.middle); lane.lineTo(kCourse[shortcut.end]);
    stroke.setStrokeWidth(shortcut.item==kGoldKey?92.0F:126.0F); stroke.setColor(SkColorSetARGB(120,30,24,18)); canvas.drawPath(lane,stroke);
    stroke.setStrokeWidth(shortcut.item==kGoldKey?70.0F:108.0F); stroke.setColor(SkColorSetA(shortcutColors[i],175)); canvas.drawPath(lane,stroke);
    stroke.setStrokeWidth(3.0F); stroke.setColor(shortcutColors[i]); canvas.drawPath(lane,stroke);
  }
  for(std::size_t i=0;i<kItemBoxSamples.size();++i){
    const auto p=courseSample(kItemBoxSamples[i]);
    paint.setColor(SkColorSetARGB(220,35,195,235));
    SkPath box; box.moveTo(p.x(),p.y()-15);box.lineTo(p.x()+15,p.y());box.lineTo(p.x(),p.y()+15);box.lineTo(p.x()-15,p.y());box.close();
    canvas.drawPath(box,paint); paint.setColor(SK_ColorWHITE); paint.setStyle(SkPaint::kStroke_Style);paint.setStrokeWidth(2);canvas.drawPath(box,paint);paint.setStyle(SkPaint::kFill_Style);
  }

  // Start line, rotated to the local spline normal.
  canvas.save();
  canvas.translate(kCourse[0].x(), kCourse[0].y());
  const auto startDirection = coursePoint(0, 0.05F);
  canvas.rotate(std::atan2(startDirection.y()-kCourse[0].y(), startDirection.x()-kCourse[0].x()) * 180.0F / kPi);
  constexpr float tile = 10.0F;
  for (int row = 0; row < 15; ++row)
    for (int col = 0; col < 3; ++col) {
      paint.setColor(((row + col) & 1) ? SkColorSetRGB(255, 205, 64) : SK_ColorBLACK);
      canvas.drawRect(SkRect::MakeXYWH(-15 + col * tile, -75 + row * tile, tile, tile), paint);
    }
  canvas.restore();

  if (cone_) {
    for (int i = 2; i < static_cast<int>(kCourse.size()); i += 2) {
      const SkPoint c = kCourse[i];
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
  text(canvas, std::format("LAP    {}   {:05.2f}s", laps_ + 1, lapTime_), 42, 112, 18, SK_ColorWHITE);
  const auto best = bestLap_ > 0.0F ? std::format("BEST   {:05.2f}s", bestLap_) : "BEST   --.--s";
  text(canvas, best, 42, 136, 16, SkColorSetRGB(166, 184, 205));
  const auto turbo = std::format("DRIFT  {:03.0f}%{}", driftCharge_ * 100.0F, turboTime_ > 0.0F ? "  TURBO!" : "");
  text(canvas, std::format("CHECKPOINT  {:02}/{}", checkpoint_, kCourse.size()), 174, 136, 14, SkColorSetRGB(166, 184, 205));
  text(canvas, turbo, 42, 160, 16, turboTime_ > 0.0F ? SkColorSetRGB(64, 224, 255) : SkColorSetRGB(255, 174, 62));
  const char* itemName=heldItem_==kDiamond?"DIAMOND":heldItem_==kFeather?"FEATHER":heldItem_==kGoldKey?"GOLD KEY":"EMPTY";
  text(canvas,std::format("ITEM   {}   USES {}   CUTS {}",itemName,itemUses_,shortcutsTaken_),42,184,15,SkColorSetRGB(105,224,255));
  text(canvas, "WASD / ARROWS DRIVE   HOLD SPACE DRIFT   SHIFT USE ITEM   P AI   TAB 2X AI   R RESET", 24, height_ - 24.0F, 15,
       SkColorSetARGB(220, 255, 255, 255));
}

void Game::render(SkCanvas& canvas) {
  canvas.clear(SK_ColorBLACK);
  canvas.save();
  canvas.translate(width_ * 0.5F, height_ * 0.5F);
  const float zoom = std::clamp(1.30F - std::abs(velocityX_) / 420.0F, 1.08F, 1.30F);
  canvas.scale(zoom, zoom);
  canvas.translate(-cameraX_, -cameraY_);
  drawTrack(canvas);
  drawCar(canvas);
  canvas.restore();
  drawHud(canvas);
}

}  // namespace fiada

