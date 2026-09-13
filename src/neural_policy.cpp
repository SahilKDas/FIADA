#include "neural_policy.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>

namespace fiada::policy {

bool load(const std::filesystem::path& path, std::vector<float>& weights) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream || stream.tellg() != static_cast<std::streamoff>(kModelBytes)) return false;
  weights.resize(kParameterCount);
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(weights.data()), kModelBytes);
  return static_cast<bool>(stream);
}

bool save(const std::filesystem::path& path, const std::vector<float>& weights) {
  if (weights.size() != kParameterCount) return false;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(weights.data()), kModelBytes);
  return static_cast<bool>(stream);
}

void initialize(std::vector<float>& weights, unsigned seed) {
  weights.assign(kParameterCount, 0.0F);
  std::mt19937 random(seed);
  std::normal_distribution<float> input(0.0F, 0.055F), recurrent(0.0F, 0.006F), dense(0.0F, 0.032F);
  for (std::size_t i = kInputOffset; i < kRecurrentOffset; ++i) weights[i] = input(random);
  for (int row = 0; row < kRecurrent; ++row) {
    for (int col = 0; col < kRecurrent; ++col)
      weights[kRecurrentOffset + row * kRecurrent + col] = recurrent(random);
    weights[kRecurrentOffset + row * kRecurrent + row] += 0.72F;
  }
  for (std::size_t i = kDenseOffset; i < kDenseBiasOffset; ++i) weights[i] = dense(random);
}

Output forward(const Observation& observation, const std::vector<float>& weights,
               State& state, std::array<float, kDense>* denseOut) {
  if (weights.size() != kParameterCount) return {};
  std::array<float, kRecurrent> next{};
  for (int h = 0; h < kRecurrent; ++h) {
    float value = weights[kRecurrentBiasOffset + h];
    for (int i = 0; i < kObservations; ++i)
      value += observation[i] * weights[kInputOffset + i * kRecurrent + h];
    const std::size_t row = kRecurrentOffset + h * kRecurrent;
    for (int i = 0; i < kRecurrent; ++i) value += state.recurrent[i] * weights[row + i];
    next[h] = std::tanh(value);
  }
  state.recurrent = next;
  std::array<float, kDense> denseValues{};
  for (int d = 0; d < kDense; ++d) {
    float value = weights[kDenseBiasOffset + d];
    for (int h = 0; h < kRecurrent; ++h)
      value += next[h] * weights[kDenseOffset + h * kDense + d];
    denseValues[d] = std::tanh(value);
  }
  Output output{};
  for (int o = 0; o < kOutputs; ++o) {
    output[o] = weights[kOutputBiasOffset + o];
    for (int d = 0; d < kDense; ++d)
      output[o] += denseValues[d] * weights[kOutputOffset + d * kOutputs + o];
  }
  if (denseOut) *denseOut = denseValues;
  return output;
}

}  // namespace fiada::policy
