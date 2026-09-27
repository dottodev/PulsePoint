#include "rng.h"

#include "clock.h"

namespace pp {

uint64_t randomSeed() {
  uint64_t marker = 0;
  const uint64_t t = static_cast<uint64_t>(monotonicNow());
  Rng r(t ^ (reinterpret_cast<uintptr_t>(&marker) * 0x9E3779B97F4A7C15ull));
  return r.nextU64();
}

}  // namespace pp
