#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <string>

#include "include/core/SkRefCnt.h"

class SkCanvas;
class SkImage;

namespace fiada {

struct Input {
  bool accelerate{};
  bool brake{};
  bool left{};
  bool right{};
  bool reset{};
  bool drift{};
};

class Game {
 public:
  Game();
  void resize(int width, int height);
  void update(float dt, const Input& input);
  void render(SkCanvas& canvas);

 private:
  void reset();
  bool onRoad(float x, float y) const;
  void loadAssets();
  void drawTrack(SkCanvas& canvas) const;
  void drawHud(SkCanvas& canvas) const;
  void drawCar(SkCanvas& canvas) const;

  int width_{1280};
  int height_{720};
  float x_{-90.0F};
  float y_{-230.0F};
  float angle_{};
  float velocityX_{};
  float velocityY_{};
  float yawRate_{};
  float throttle_{};
  float brake_{};
  float steer_{};
  float cameraX_{};
  float cameraY_{};
  float driftCharge_{};
  float turboTime_{};
  bool wasDrifting_{};
  float previousX_{-90.0F};
  float lapTime_{};
  float bestLap_{};
  int laps_{};
  bool passedHalfway_{};
  int checkpoint_{};
  bool resetHeld_{};
  sk_sp<SkImage> car_;
  sk_sp<SkImage> cone_;
};

}  // namespace fiada

