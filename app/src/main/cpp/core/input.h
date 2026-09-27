// Touch input: capture, de-duplication and hand-off to the game loop.
//
// Latency rules that this file exists to enforce:
//   * The timestamp is captured on the thread that receives the MotionEvent,
//     the instant the event is dequeued, before any of our own work happens.
//     Nothing between the finger and this struct adds latency.
//   * Events are handed to the game thread through a wait-free single-producer
//     / single-consumer ring.  queueEvent() would add up to a full frame of
//     delay; a ring costs a few nanoseconds and keeps the measurement exact,
//     because the timestamp was taken before the hand-off.
#pragma once

#include <atomic>
#include <cstdint>

#include "clock.h"
#include "math_util.h"

namespace pp {

enum class PointerPhase : uint8_t { Down, Move, Up, Cancel };

struct PointerSample {
  int pointerId = 0;
  PointerPhase phase = PointerPhase::Down;
  Vec2 pos;
  Nanos timeNs = 0;
  bool valid = false;
};

// One accepted tap.  Everything the gameplay code needs, nothing it does not.
struct Tap {
  Vec2 pos;
  Nanos timeNs = 0;
  // True while the finger is still down.  Modes that support hold-to-charge
  // (none currently, but the data is free) or drag gestures can use it.
  bool fromPrimaryPointer = true;
};

// A tap that passed de-duplication, plus the reason it might still be ignored.
enum class TapReject : uint8_t {
  None,
  DuplicateTooSoon,     // Re-delivery of the same physical touch.
  DuplicateSameSpot,    // Re-delivery of the same physical touch, one frame later.
  PointerStillDown,     // Second finger of a multi-touch gesture.
};

class InputRouter {
 public:
  // A human cannot produce two taps closer together than roughly 40 ms, so a
  // 9 ms floor removes synthetic duplicates with a very wide safety margin
  // while never interfering with fast play.
  static constexpr Nanos kMinTapGapNs = 9 * kNsPerMs;
  // Covers a duplicate delivered one vsync later: same spot, just a hair apart.
  static constexpr Nanos kSameSpotWindowNs = 24 * kNsPerMs;
  static constexpr float kSameSpotRadiusDp = 4.0f;

  void configure(float densityDpPerPx) { sameSpotRadiusPx_ = kSameSpotRadiusDp * densityDpPerPx; }
  void reset();

  // Called from the platform input thread with raw MotionEvent data.  Safe to
  // call at any rate; events are dropped rather than blocking if the consumer
  // falls behind, which is the correct trade for a game.
  void push(const PointerSample& sample);

  // Called once per frame on the game thread.  Returns the number of taps
  // written into `out`.
  int drain(Tap* out, int maxCount);

  // Number of pushes that were dropped because the ring was full.  Surfaced in
  // the debug overlay; a non-zero value means the UI thread is outrunning us.
  uint32_t droppedSamples() const { return dropped_.load(std::memory_order_relaxed); }

 private:
  static constexpr int kCapacity = 32;

  void emit(const Vec2 pos, Nanos t, bool primary);

  struct Slot {
    Vec2 pos;
    Nanos timeNs = 0;
    uint8_t flags = 0;
  };

  Slot slots_[kCapacity] = {};
  std::atomic<int> head_{0};
  std::atomic<int> tail_{0};
  std::atomic<uint32_t> dropped_{0};

  // De-duplication state, owned by the producer thread.
  bool primaryDown_ = false;
  bool haveLastTap_ = false;
  Vec2 lastTapPos_;
  Nanos lastTapTimeNs_ = 0;
  float sameSpotRadiusPx_ = 4.0f;

  // Drain-side state.
  Tap pending_[kCapacity] = {};
};

}  // namespace pp
