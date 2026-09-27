#include "clock.h"

#include <ctime>

namespace pp {
namespace {

Nanos timespecToNanos(const struct timespec& ts) {
  return static_cast<Nanos>(ts.tv_sec) * kNsPerSec + static_cast<Nanos>(ts.tv_nsec);
}

}  // namespace

Nanos monotonicNow() {
  struct timespec ts;
#if defined(CLOCK_MONOTONIC_RAW)
  // RAW is not slewed by NTP, so it is the steadier of the two monotonic
  // sources.  Bionic exposes it from API 21 onwards.
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0) return timespecToNanos(ts);
#endif
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return timespecToNanos(ts);
}

}  // namespace pp
