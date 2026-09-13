#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <vector>

namespace fiada::policy {

inline constexpr int kObservations = 32;
inline constexpr int kRecurrent = 768;
inline constexpr int kDense = 512;
inline constexpr int kOutputs = 5;
inline constexpr std::size_t kInputOffset = 0;
inline constexpr std::size_t kRecurrentOffset = kInputOffset + kObservations * kRecurrent;
inline constexpr std::size_t kRecurrentBiasOffset = kRecurrentOffset + kRecurrent * kRecurrent;
inline constexpr std::size_t kDenseOffset = kRecurrentBiasOffset + kRecurrent;
inline constexpr std::size_t kDenseBiasOffset = kDenseOffset + kRecurrent * kDense;
inline constexpr std::size_t kOutputOffset = kDenseBiasOffset + kDense;
inline constexpr std::size_t kOutputBiasOffset = kOutputOffset + kDense * kOutputs;
inline constexpr std::size_t kParameterCount = kOutputBiasOffset + kOutputs;
inline constexpr std::size_t kModelBytes = kParameterCount * sizeof(float);

struct State { std::array<float, kRecurrent> recurrent{}; };
using Observation = std::array<float, kObservations>;
using Output = std::array<float, kOutputs>;

bool load(const std::filesystem::path& path, std::vector<float>& weights);
bool save(const std::filesystem::path& path, const std::vector<float>& weights);
void initialize(std::vector<float>& weights, unsigned seed);
Output forward(const Observation& observation, const std::vector<float>& weights,
               State& state, std::array<float, kDense>* dense = nullptr);

}  // namespace fiada::policy
