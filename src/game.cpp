#include "game.hpp"
#include "trained_policy.hpp"

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

constexpr std::array<SkPoint, 18> kCourse{{
    {-520, -300}, {-250, -430}, {40, -470}, {300, -400},
    {520, -470}, {740, -300}, {650, -100}, {760, 100},
    {650, 310}, {420, 430}, {180, 350}, {0, 480},
    {-250, 430}, {-480, 350}, {-720, 200}, {-760, -20},
    {-600, -180}, {-390, -110},
}};
constexpr int kSamplesPerSegment = 20;
constexpr int kSampleCount = static_cast<int>(kCourse.size()) * kSamplesPerSegment;

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
  std::copy(policy::kWeights.begin(), policy::kWeights.end(), policyWeights_.begin());
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
  std::ifstream model(root / "assets/policy/fiada_policy.bin", std::ios::binary);
  if (model) model.read(reinterpret_cast<char*>(policyWeights_.data()), sizeof(policyWeights_));
}

void Game::reset() {
  x_ = kCourse[0].x();
  y_ = kCourse[0].y();
  const auto startAhead = coursePoint(0, 0.05F);
  angle_ = std::atan2(startAhead.y() - y_, startAhead.x() - x_);
  velocityX_ = velocityY_ = yawRate_ = 0.0F;
  throttle_ = brake_ = steer_ = driftCharge_ = turboTime_ = 0.0F;
  wasDrifting_ = false;
  cameraX_ = cameraY_ = 0.0F;
  previousX_ = x_;
  previousY_ = y_;
  lapTime_ = 0.0F;
  checkpoint_ = 1;
}

bool Game::onRoad(float x, float y) const {
  return courseDistance(x, y) < 76.0F;
}



std::array<float, 8> Game::observation() const {
  const int nearest = nearestCourseSample(x_, y_);
  const int targetIndex = (nearest + 18) % kSampleCount;
  const auto currentRaw = courseSample(nearest);
  const auto next = courseSample(nearest + 1);
  const auto targetRaw = courseSample(targetIndex);
  const auto futureNext = courseSample(targetIndex + 1);
  float tx = next.x()-currentRaw.x(), ty = next.y()-currentRaw.y();
  float fx = futureNext.x()-targetRaw.x(), fy = futureNext.y()-targetRaw.y();
  const float tl=std::hypot(tx,ty), fl=std::hypot(fx,fy);
  tx/=tl;ty/=tl;fx/=fl;fy/=fl;
  const SkPoint current{currentRaw.x()-ty*racingLineOffset_,currentRaw.y()+tx*racingLineOffset_};
  const SkPoint target{targetRaw.x()-fy*racingLineOffset_,targetRaw.y()+fx*racingLineOffset_};
  const float desired=std::atan2(target.y()-y_,target.x()-x_);
  const float error=wrapAngle(desired-angle_);
  const float lateral=(x_-current.x())*-ty+(y_-current.y())*tx;
  const float curve=std::atan2(tx*fy-ty*fx,tx*fx+ty*fy);
  return {std::sin(error),std::cos(error),std::clamp(lateral/76.0F,-2.0F,2.0F),
          velocityX_/45.0F,velocityY_/15.0F,yawRate_/2.0F,curve,
          onRoad(x_,y_)?1.0F:0.0F};
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

Input Game::aiInput() const {
  const auto observation = this->observation();

  std::array<float, policy::kHidden> hidden{};
  std::size_t cursor = 0;
  for (int i = 0; i < policy::kObservations; ++i)
    for (int h = 0; h < policy::kHidden; ++h)
      hidden[h] += observation[i] * policyWeights_[cursor++];
  for (float& value : hidden) value = std::tanh(value + policyWeights_[cursor++]);
  std::array<float, policy::kOutputs> output{};
  for (int h = 0; h < policy::kHidden; ++h)
    for (int o = 0; o < policy::kOutputs; ++o)
      output[o] += hidden[h] * policyWeights_[cursor++];
  for (float& value : output) value += policyWeights_[cursor++];

  float longitudinal = std::tanh(output[0]);
  const float desiredSpeed = 7.0F + 43.0F * std::exp(-5.2F * std::abs(observation[6]))
                           - 5.0F * std::min(std::abs(observation[2]), 1.0F);
  if (velocityX_ > desiredSpeed)
    longitudinal = -std::clamp((velocityX_ - desiredSpeed) / 12.0F, 0.25F, 1.0F);
  else if (std::abs(observation[2]) > 0.82F)
    longitudinal = std::min(longitudinal, 0.35F);
  return Input{.throttle = std::max(longitudinal, 0.0F),
               .brake = std::max(-longitudinal, 0.0F),
               .steer = std::clamp(std::lerp(std::tanh(output[1]),
                   std::clamp(1.8F*observation[0]-1.25F*observation[2]+1.4F*observation[6]-0.3F*observation[5],-1.0F,1.0F),
                   std::clamp(0.18F+0.38F*std::max(std::abs(observation[0]),std::abs(observation[2])),0.18F,0.72F)),-1.0F,1.0F),
               .reset = false,
               .drift = sigmoid(output[2]) > 0.62F, .toggleAi = false};
}

void Game::update(float dt, const Input& input) {
  dt = std::min(dt, 0.05F);
  trainingReward_ = 0.0F;
  if (input.toggleAi && !aiToggleHeld_) aiEnabled_ = !aiEnabled_;
  aiToggleHeld_ = input.toggleAi;
  if (input.reset && !resetHeld_) reset();
  resetHeld_ = input.reset;
  const Input control = aiEnabled_ ? aiInput() : input;

  // Six-state nonlinear bicycle model: compact enough for batched training.
  constexpr float mass = 1180.0F, inertia = 1760.0F;
  constexpr float frontAxle = 1.18F, rearAxle = 1.42F, gravity = 9.81F;
  const bool road = onRoad(x_, y_);
  const float surfaceGrip = (road ? 1.34F : 0.58F) * gripScale_;
  const float rolling = road ? 34.0F : 280.0F;
  const float gripF = surfaceGrip;
  const float gripR = surfaceGrip * (control.drift ? 0.79F : 1.0F);
  const float throttleTarget = control.throttle;
  const float brakeTarget = control.brake;
  const float steerTarget = control.steer;
  throttle_ += (throttleTarget - throttle_) * std::min(1.0F, 11.0F * dt);
  brake_ += (brakeTarget - brake_) * std::min(1.0F, 9.0F * dt);
  steer_ += (steerTarget - steer_) * std::min(1.0F, 13.0F * dt);

  const float speedAbs = std::abs(velocityX_);
  const float driftSteer = control.drift ? 1.12F : 1.0F;
  const float steering = steer_ * driftSteer * 0.42F / (1.0F + speedAbs * 0.022F);
  const float safeSpeed = std::max(speedAbs, 2.5F);
  const float slipF = std::atan2(velocityY_ + frontAxle * yawRate_, safeSpeed) - steering;
  const float slipR = std::atan2(velocityY_ - rearAxle * yawRate_, safeSpeed);

  const float slipMagnitude = std::abs(slipR);
  const bool validDrift = control.drift && road && speedAbs > 13.0F &&
                          slipMagnitude > 0.08F && slipMagnitude < 0.72F;
  if (validDrift)
    driftCharge_ = std::min(1.0F, driftCharge_ + dt * (0.20F + slipMagnitude * 0.85F));
  if (wasDrifting_ && !control.drift) {
    if (driftCharge_ >= 0.22F) turboTime_ = 0.28F + driftCharge_ * 0.92F;
    driftCharge_ = 0.0F;
  }
  if (!road) driftCharge_ = std::max(0.0F, driftCharge_ - dt * 0.75F);
  wasDrifting_ = control.drift;
  turboTime_ = std::max(0.0F, turboTime_ - dt);

  const float turboForce = turboTime_ > 0.0F ? 4400.0F : 0.0F;
  const float drive = throttle_ * 13800.0F / (1.0F + speedAbs / 48.0F) + turboForce;
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
  velocityY_ *= std::exp(-(control.drift ? 0.18F : 1.85F) * dt);
  yawRate_ = std::clamp(yawRate_ + yawAccel * dt, -2.5F, 2.5F);
  if (control.throttle < 0.01F && control.brake < 0.01F && std::hypot(velocityX_, velocityY_) < 0.35F)
    velocityX_ = velocityY_ = yawRate_ = 0.0F;

  angle_ += yawRate_ * dt;
  previousX_ = x_;
  previousY_ = y_;
  x_ += (std::cos(angle_) * velocityX_ - std::sin(angle_) * velocityY_) * dt * 14.0F;
  y_ += (std::sin(angle_) * velocityX_ + std::cos(angle_) * velocityY_) * dt * 14.0F;
  lapTime_ += dt;

  // Dead-zone camera: stationary inside the central box, follow near the track,
  // and never chase a car that has escaped the authored world.
  const float zoom = std::clamp(0.96F - std::abs(velocityX_) / 270.0F, 0.72F, 0.96F);
  const float deadX = width_ * 0.19F / zoom;
  const float deadY = height_ * 0.17F / zoom;
  const bool insideFollowWorld = x_ > -850.0F && x_ < 850.0F &&
                                 y_ > -560.0F && y_ < 560.0F;
  if (insideFollowWorld) {
    if (x_ - cameraX_ > deadX) cameraX_ = x_ - deadX;
    if (x_ - cameraX_ < -deadX) cameraX_ = x_ + deadX;
    if (y_ - cameraY_ > deadY) cameraY_ = y_ - deadY;
    if (y_ - cameraY_ < -deadY) cameraY_ = y_ + deadY;
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
                    (road ? 0.0F : 0.28F);
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

  stroke.setStrokeWidth(184.0F);
  stroke.setColor(SkColorSetRGB(116, 76, 10));
  canvas.drawPath(course, stroke);
  stroke.setStrokeWidth(170.0F);
  stroke.setColor(SkColorSetRGB(232, 174, 43));
  canvas.drawPath(course, stroke);
  stroke.setStrokeWidth(150.0F);
  stroke.setColor(SkColorSetRGB(39, 40, 43));
  canvas.drawPath(course, stroke);

  stroke.setStrokeWidth(3.0F);
  stroke.setColor(SkColorSetARGB(190, 255, 216, 92));
  constexpr std::array<SkScalar, 2> dashes{24.0F, 20.0F};
  stroke.setPathEffect(SkDashPathEffect::Make(dashes.data(), static_cast<int>(dashes.size()), 0.0F));
  canvas.drawPath(course, stroke);
  stroke.setPathEffect(nullptr);

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
  canvas.drawRoundRect(SkRect::MakeXYWH(22, 22, 300, 146), 16, 16, panel);
  text(canvas, aiEnabled_ ? "FIADA  [AI]" : "FIADA  [HUMAN]", 42, 56, 26, SkColorSetRGB(255, 202, 58));
  text(canvas, std::format("SPEED  {:03.0f} km/h", std::abs(velocityX_) * 3.6F), 42, 86, 18, SK_ColorWHITE);
  text(canvas, std::format("LAP    {}   {:05.2f}s", laps_ + 1, lapTime_), 42, 112, 18, SK_ColorWHITE);
  const auto best = bestLap_ > 0.0F ? std::format("BEST   {:05.2f}s", bestLap_) : "BEST   --.--s";
  text(canvas, best, 42, 136, 16, SkColorSetRGB(166, 184, 205));
  const auto turbo = std::format("DRIFT  {:03.0f}%{}", driftCharge_ * 100.0F, turboTime_ > 0.0F ? "  TURBO!" : "");
  text(canvas, std::format("CHECKPOINT  {:02}/{}", checkpoint_, kCourse.size()), 174, 136, 14, SkColorSetRGB(166, 184, 205));
  text(canvas, turbo, 42, 160, 16, turboTime_ > 0.0F ? SkColorSetRGB(64, 224, 255) : SkColorSetRGB(255, 174, 62));
  text(canvas, "WASD / ARROWS DRIVE   HOLD SPACE TO DRIFT   P TOGGLE AI   R RESET", 24, height_ - 24.0F, 15,
       SkColorSetARGB(220, 255, 255, 255));
}

void Game::render(SkCanvas& canvas) {
  canvas.clear(SK_ColorBLACK);
  canvas.save();
  canvas.translate(width_ * 0.5F, height_ * 0.5F);
  const float zoom = std::clamp(0.96F - std::abs(velocityX_) / 270.0F, 0.72F, 0.96F);
  canvas.scale(zoom, zoom);
  canvas.translate(-cameraX_, -cameraY_);
  drawTrack(canvas);
  drawCar(canvas);
  canvas.restore();
  drawHud(canvas);
}

}  // namespace fiada

