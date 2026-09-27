// The single source of truth for time in PulsePoint.
//
// Reaction times are the product this game sells, so nothing in the input or
// scoring path is allowed to derive a timestamp from frame boundaries, vsync
// callbacks, animation durations or UI timers.  Every duration the gameplay
// code measures comes from one of the clocks below.
//
// On Android, `clock_gettime(CLOCK_MONOTONIC)` and `SystemClock.elapsedRealtimeNanos()`
// read the same hardware counter, so a stimulus stamped natively and a tap
// stamped in the Java input handler can be subtracted directly with nanosecond
// resolution and no clock-offset correction.
#pragma once

#include <cstdint>

namespace pp {

using Nanos = int64_t;

constexpr Nanos kNsPerUs = 1000;
constexpr Nanos kNsPerMs = 1000000;
constexpr Nanos kNsPerSec = 1000000000;

// Monotonic, high resolution, unaffected by wall-clock adjustments.
Nanos monotonicNow();

inline Nanos nsFromMs(float ms) { return static_cast<Nanos>(ms * static_cast<float>(kNsPerMs)); }
inline Nanos nsFromSec(float s) { return static_cast<Nanos>(s * static_cast<float>(kNsPerSec)); }
inline float msFromNs(Nanos ns) { return static_cast<float>(ns) / static_cast<float>(kNsPerMs); }
inline float secFromNs(Nanos ns) { return static_cast<float>(ns) / static_cast<float>(kNsPerSec); }

// A small helper for durations that must never go negative, which happens
// constantly when a stimulus and its response land in the same frame.
inline Nanos elapsedSince(Nanos now, Nanos then) { return now > then ? now - then : 0; }

}  // namespace pp
