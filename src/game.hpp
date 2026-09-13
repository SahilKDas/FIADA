#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "include/core/SkRefCnt.h"
#include "include/core/SkImage.h"
#include "neural_policy.hpp"

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
  bool menu{};
  bool confirm{};
  bool next{};
  bool previous{};
  bool labOverlay{};
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
  [[nodiscard]] int placement() const { return placement_; }
  [[nodiscard]] bool spectatorFastForwardAllowed() const { return aiEnabled_ || labMode_; }
  void beginTrainingEpisode(unsigned seed);
  void beginItemTrainingEpisode(int itemType, unsigned seed);
  [[nodiscard]] policy::Observation observation() const;
  [[nodiscard]] float driftCharge() const { return driftCharge_; }
  [[nodiscard]] float turboTime() const { return turboTime_; }
  [[nodiscard]] int miniTurbos() const { return miniTurbos_; }
  [[nodiscard]] int itemPickups() const { return itemPickups_; }
  [[nodiscard]] int itemUses() const { return itemUses_; }
  [[nodiscard]] int shortcutsTaken() const { return shortcutsTaken_; }
  [[nodiscard]] float lapTime() const { return lapTime_; }
  [[nodiscard]] float bestLap() const { return bestLap_; }
  [[nodiscard]] float offroadTime() const { return offroadTime_; }
  [[nodiscard]] int itemPickups(int type) const { return type >= 1 && type <= 3 ? itemPickupsByType_[type-1] : 0; }
  [[nodiscard]] int shortcutsTaken(int type) const { return type >= 1 && type <= 3 ? shortcutsByType_[type-1] : 0; }
  [[nodiscard]] float trainingReward() const { return trainingReward_; }
  [[nodiscard]] bool trainingTerminal() const { return trainingTerminal_; }
  [[nodiscard]] std::uint64_t deterministicHash() const;

 private:
  void reset();
  bool onRoad(float x, float y) const;
  void updateItems(const Input& control);
  void loadAssets();
  void drawTrack(SkCanvas& canvas) const;
  void drawHud(SkCanvas& canvas) const;
  void drawCar(SkCanvas& canvas) const;
  void updateRivals(float dt);
  void resolveRivalCollisions();
  void drawRivals(SkCanvas& canvas) const;
  void drawGrandPrixHud(SkCanvas& canvas) const;
  Input aiInput();

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
  std::array<int,3> itemPickupsByType_{};
  std::array<int,3> shortcutsByType_{};
  int heldItem_{};
  int shortcutItem_{};
  int lastItemBox_{-1};
  unsigned long long simulationTick_{};
  float itemEffectTime_{};
  float offroadTime_{};
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
  std::vector<float> policyWeights_;
  policy::State policyState_{};
  policy::Output cachedPolicyOutput_{};
  int policyTick_{};
  struct Rival { float x{},y{},angle{},speed{},progress{},turbo{}; int lap{},place{},item{},personality{}; bool finished{}; };
  std::array<Rival,7> rivals_{};
  std::array<int,8> championshipPoints_{};
  std::array<float,8> drivingProfile_{};
  int placement_{1};
  int countdownTicks_{360};
  int championshipRound_{};
  int trackIndex_{};
  bool labMode_{};
  bool championshipMode_{};
  bool overlayHeld_{};
  bool navHeld_{};
  bool raceAwarded_{};
  bool menuHeld_{};
  unsigned raceSeed_{0xF1ADAU};
  sk_sp<SkImage> car_;
  sk_sp<SkImage> cone_;
};

}  // namespace fiada

