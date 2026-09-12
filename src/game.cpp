#include "game.hpp"

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

#include <windows.h>

namespace fiada {
namespace {
constexpr float kPi = 3.14159265358979323846F;

constexpr std::array<SkPoint, 12> kCourse{{
    {-455, -205}, {-180, -310}, {115, -268}, {420, -145},
    {505, 75}, {345, 270}, {80, 305}, {-75, 155},
    {-330, 285}, {-535, 115}, {-390, -35}, {-225, -105},
}};

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

Game::Game() { loadAssets(); }

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
}

void Game::reset() {
  x_ = -455.0F;
  y_ = -205.0F;
  angle_ = -0.27F;
  velocityX_ = velocityY_ = yawRate_ = 0.0F;
  throttle_ = brake_ = steer_ = driftCharge_ = turboTime_ = 0.0F;
  wasDrifting_ = false;
  cameraX_ = cameraY_ = 0.0F;
  previousX_ = x_;
  lapTime_ = 0.0F;
  passedHalfway_ = false;
  checkpoint_ = 1;
}

bool Game::onRoad(float x, float y) const {
  return courseDistance(x, y) < 76.0F;
}

void Game::update(float dt, const Input& input) {
  dt = std::min(dt, 0.05F);
  if (input.reset && !resetHeld_) reset();
  resetHeld_ = input.reset;

  // Six-state nonlinear bicycle model: compact enough for batched training.
  constexpr float mass = 1180.0F, inertia = 1760.0F;
  constexpr float frontAxle = 1.18F, rearAxle = 1.42F, gravity = 9.81F;
  const bool road = onRoad(x_, y_);
  const float surfaceGrip = road ? 1.34F : 0.58F;
  const float rolling = road ? 34.0F : 280.0F;
  const float gripF = surfaceGrip;
  const float gripR = surfaceGrip * (input.drift ? 0.79F : 1.0F);
  const float throttleTarget = input.accelerate ? 1.0F : 0.0F;
  const float brakeTarget = input.brake ? 1.0F : 0.0F;
  const float steerTarget = (input.right ? 1.0F : 0.0F) - (input.left ? 1.0F : 0.0F);
  throttle_ += (throttleTarget - throttle_) * std::min(1.0F, 11.0F * dt);
  brake_ += (brakeTarget - brake_) * std::min(1.0F, 9.0F * dt);
  steer_ += (steerTarget - steer_) * std::min(1.0F, 13.0F * dt);

  const float speedAbs = std::abs(velocityX_);
  const float driftSteer = input.drift ? 1.12F : 1.0F;
  const float steering = steer_ * driftSteer * 0.42F / (1.0F + speedAbs * 0.022F);
  const float safeSpeed = std::max(speedAbs, 2.5F);
  const float slipF = std::atan2(velocityY_ + frontAxle * yawRate_, safeSpeed) - steering;
  const float slipR = std::atan2(velocityY_ - rearAxle * yawRate_, safeSpeed);

  const float slipMagnitude = std::abs(slipR);
  const bool validDrift = input.drift && road && speedAbs > 13.0F &&
                          slipMagnitude > 0.08F && slipMagnitude < 0.72F;
  if (validDrift)
    driftCharge_ = std::min(1.0F, driftCharge_ + dt * (0.20F + slipMagnitude * 0.85F));
  if (wasDrifting_ && !input.drift) {
    if (driftCharge_ >= 0.22F) turboTime_ = 0.28F + driftCharge_ * 0.92F;
    driftCharge_ = 0.0F;
  }
  if (!road) driftCharge_ = std::max(0.0F, driftCharge_ - dt * 0.75F);
  wasDrifting_ = input.drift;
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
  if (!input.drift) {
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
  velocityY_ *= std::exp(-(input.drift ? 0.18F : 1.85F) * dt);
  yawRate_ = std::clamp(yawRate_ + yawAccel * dt, -2.5F, 2.5F);
  if (!input.accelerate && !input.brake && std::hypot(velocityX_, velocityY_) < 0.35F)
    velocityX_ = velocityY_ = yawRate_ = 0.0F;

  angle_ += yawRate_ * dt;
  previousX_ = x_;
  x_ += (std::cos(angle_) * velocityX_ - std::sin(angle_) * velocityY_) * dt * 14.0F;
  y_ += (std::sin(angle_) * velocityX_ + std::cos(angle_) * velocityY_) * dt * 14.0F;
  lapTime_ += dt;

  // Dead-zone camera: stationary inside the central box, follow near the track,
  // and never chase a car that has escaped the authored world.
  const float zoom = std::clamp(0.96F - std::abs(velocityX_) / 270.0F, 0.72F, 0.96F);
  const float deadX = width_ * 0.19F / zoom;
  const float deadY = height_ * 0.17F / zoom;
  const bool insideFollowWorld = x_ > -650.0F && x_ < 620.0F &&
                                 y_ > -390.0F && y_ < 390.0F;
  if (insideFollowWorld) {
    if (x_ - cameraX_ > deadX) cameraX_ = x_ - deadX;
    if (x_ - cameraX_ < -deadX) cameraX_ = x_ + deadX;
    if (y_ - cameraY_ > deadY) cameraY_ = y_ - deadY;
    if (y_ - cameraY_ < -deadY) cameraY_ = y_ + deadY;
    cameraX_ = std::clamp(cameraX_, -255.0F, 255.0F);
    cameraY_ = std::clamp(cameraY_, -145.0F, 145.0F);
  }
  const float screenX = width_ * 0.5F + (x_ - cameraX_) * zoom;
  const float screenY = height_ * 0.5F + (y_ - cameraY_) * zoom;
  if (screenX < -38.0F || screenX > width_ + 38.0F ||
      screenY < -38.0F || screenY > height_ + 38.0F) {
    reset();
    return;
  }

  const SkPoint target = kCourse[checkpoint_];
  if (std::hypot(x_ - target.x(), y_ - target.y()) < 105.0F) {
    checkpoint_ = (checkpoint_ + 1) % static_cast<int>(kCourse.size());
    if (checkpoint_ == 1) {
      ++laps_;
      if (bestLap_ == 0.0F || lapTime_ < bestLap_) bestLap_ = lapTime_;
      lapTime_ = 0.0F;
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
  for (int x = -1100; x < 1100; x += 110)
    canvas.drawRect(SkRect::MakeXYWH(x, -700, 54, 1400), paint);

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
  canvas.translate(-455, -205);
  canvas.rotate(-15.5F);
  constexpr float tile = 10.0F;
  for (int row = 0; row < 15; ++row)
    for (int col = 0; col < 3; ++col) {
      paint.setColor(((row + col) & 1) ? SkColorSetRGB(255, 205, 64) : SK_ColorBLACK);
      canvas.drawRect(SkRect::MakeXYWH(-15 + col * tile, -75 + row * tile, tile, tile), paint);
    }
  canvas.restore();

  if (cone_) {
    for (int i = 2; i < 12; i += 2) {
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
  text(canvas, "FIADA", 42, 56, 26, SkColorSetRGB(255, 202, 58));
  text(canvas, std::format("SPEED  {:03.0f} km/h", std::abs(velocityX_) * 3.6F), 42, 86, 18, SK_ColorWHITE);
  text(canvas, std::format("LAP    {}   {:05.2f}s", laps_ + 1, lapTime_), 42, 112, 18, SK_ColorWHITE);
  const auto best = bestLap_ > 0.0F ? std::format("BEST   {:05.2f}s", bestLap_) : "BEST   --.--s";
  text(canvas, best, 42, 136, 16, SkColorSetRGB(166, 184, 205));
  const auto turbo = std::format("DRIFT  {:03.0f}%{}", driftCharge_ * 100.0F, turboTime_ > 0.0F ? "  TURBO!" : "");
  text(canvas, turbo, 42, 160, 16, turboTime_ > 0.0F ? SkColorSetRGB(64, 224, 255) : SkColorSetRGB(255, 174, 62));
  text(canvas, "WASD / ARROWS DRIVE   HOLD SPACE TO DRIFT   RELEASE FOR TURBO   R RESET", 24, height_ - 24.0F, 15,
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

