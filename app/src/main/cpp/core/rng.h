// Deterministic, allocation-free pseudo random numbers.
//
// Two requirements shape this file:
//   * A round must be reproducible from a seed, so a bug report can be replayed.
//   * Nothing on the input path may allocate, so the state lives inline.
#pragma once

#include <cstdint>

namespace pp {

// xoshiro128** -- small, fast, and statistically far beyond what a reflex game
// needs, but cheap enough to use for every spawn position.
class Rng {
 public:
  Rng() { seed(0x9E3779B9u); }
  explicit Rng(uint64_t s) { seed(s); }

  void seed(uint64_t s) {
    // SplitMix64 expansion so that even a constant seed gives a good state.
    uint64_t z = s + 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 4; ++i) {
      z += 0x9E3779B97F4A7C15ull;
      uint64_t t = z;
      t = (t ^ (t >> 30)) * 0xBF58476D1CE4E5B9ull;
      t = (t ^ (t >> 27)) * 0x94D049BB133111EBull;
      s_[i] = t ^ (t >> 31);
    }
    if ((s_[0] | s_[1] | s_[2] | s_[3]) == 0) s_[0] = 0x2545F4914F6CDD1Dull;
  }

  uint64_t nextU64() {
    const uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
  }

  // Uniform in [0, 1).
  float nextFloat() {
    return static_cast<float>(nextU64() >> 40) * (1.0f / 16777216.0f);
  }

  // Uniform in [lo, hi).
  float range(float lo, float hi) { return lo + (hi - lo) * nextFloat(); }

  // Uniform in [lo, hi] as an integer.
  int rangeInt(int lo, int hi) {
    if (hi <= lo) return lo;
    const uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
    return lo + static_cast<int>(nextU64() % span);
  }

  bool chance(float p) { return nextFloat() < p; }

  // Symmetric noise in [-1, 1].
  float signedUnit() { return nextFloat() * 2.0f - 1.0f; }

 private:
  static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  uint64_t s_[4] = {0, 0, 0, 0};
};

// A non-deterministic seed source.  Mixes the monotonic clock with the address
// of a stack local so two rounds started in the same nanosecond still differ.
uint64_t randomSeed();

}  // namespace pp
