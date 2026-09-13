#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <string>

#include "include/core/SkRefCnt.h"
#include "include/core/SkImage.h"

class SkCanvas;

namespace fiada {

struct Input {
  float throttle{};
  float brake{};
  float steer{};
  bool reset{};
  bool drift{};
  bool toggleAi{};
  bool useItem{};
};

class Game {
 public:
  explicit Game(bool loadAssets = true);
  void resize(int width, int height);
  void update(float dt, const Input& input);
  void render(SkCanvas& canvas);
  void enableAi() { aiEnabled_ = true; }
  [[nodiscard]] bool aiEnabled() const { return aiEnabled_; }
  [[nodiscard]] int laps() const { return laps_; }
  [[nodiscard]] int checkpoint() const { return checkpoint_; }
  void beginTrainingEpisode(unsigned seed);
  [[nodiscard]] std::array<float, 14> observation() const;
  [[nodiscard]] float driftCharge() const { return driftCharge_; }
  [[nodiscard]] float turboTime() const { return turboTime_; }
  [[nodiscard]] int miniTurbos() const { return miniTurbos_; }
  [[nodiscard]] int itemPickups() const { return itemPickups_; }
  [[nodiscard]] int itemUses() const { return itemUses_; }
  [[nodiscard]] int shortcutsTaken() const { return shortcutsTaken_; }
  [[nodiscard]] float trainingReward() const { return trainingReward_; }
  [[nodiscard]] bool trainingTerminal() const { return trainingTerminal_; }

 private:
  void reset();
  bool onRoad(float x, float y) const;
  void updateItems(const Input& control);
  void loadAssets();
  void drawTrack(SkCanvas& canvas) const;
  void drawHud(SkCanvas& canvas) const;
  void drawCar(SkCanvas& canvas) const;
  Input aiInput() const;

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
  int checkpoint_{};
  int miniTurbos_{};
  int itemPickups_{};
  int itemUses_{};
  int shortcutsTaken_{};
  int heldItem_{};
  int shortcutItem_{};
  int lastItemBox_{-1};
  unsigned long long simulationTick_{};
  float itemEffectTime_{};
  bool itemUseHeld_{};
  bool shortcutCounted_{};
  bool resetHeld_{};
  bool aiToggleHeld_{};
  bool aiEnabled_{};
  float previousY_{-205.0F};
  float gripScale_{1.0F};
  float racingLineOffset_{};
  float trainingReward_{};
  int progressSample_{};
  bool trainingMode_{};
  bool trainingTerminal_{};
  std::array<float, 232> policyWeights_{};
  sk_sp<SkImage> car_;
  sk_sp<SkImage> cone_;
};

}  // namespace fiada

