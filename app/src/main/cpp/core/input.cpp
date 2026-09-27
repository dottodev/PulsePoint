#include "input.h"

namespace pp {
namespace {

constexpr uint8_t kFlagPrimary = 1u;

}  // namespace

void InputRouter::reset() {
  head_.store(0, std::memory_order_relaxed);
  tail_.store(0, std::memory_order_relaxed);
  dropped_.store(0, std::memory_order_relaxed);
  primaryDown_ = false;
  haveLastTap_ = false;
  lastTapTimeNs_ = 0;
  lastTapPos_ = Vec2{};
}

void InputRouter::emit(const Vec2 pos, Nanos t, bool primary) {
  const int head = head_.load(std::memory_order_relaxed);
  const int next = head + 1;
  if (next - tail_.load(std::memory_order_acquire) > kCapacity) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  Slot& s = slots_[head % kCapacity];
  s.pos = pos;
  s.timeNs = t;
  s.flags = primary ? kFlagPrimary : 0u;
  head_.store(next, std::memory_order_release);
}

void InputRouter::push(const PointerSample& sample) {
  if (!sample.valid) return;

  switch (sample.phase) {
    case PointerPhase::Down: {
      // A second finger landing while the first is still down belongs to a
      // multi-touch gesture, never to a reaction-time measurement.
      if (primaryDown_) return;
      if (haveLastTap_) {
        const Nanos gap = sample.timeNs - lastTapTimeNs_;
        if (gap >= 0 && gap < kMinTapGapNs) return;  // Same instant, re-delivered.
        if (gap >= 0 && gap < kSameSpotWindowNs && distSq(sample.pos, lastTapPos_) <=
                                                            sameSpotRadiusPx_ * sameSpotRadiusPx_) {
          return;  // Same spot a frame later: still the same physical tap.
        }
      }
      primaryDown_ = true;
      haveLastTap_ = true;
      lastTapPos_ = sample.pos;
      lastTapTimeNs_ = sample.timeNs;
      emit(sample.pos, sample.timeNs, true);
      return;
    }
    case PointerPhase::Move:
    case PointerPhase::Up:
    case PointerPhase::Cancel:
      // Any lift or cancel frees the primary pointer.  Using the pointer id
      // keeps a second finger from ending the tracked gesture.
      if (sample.pointerId == 0) primaryDown_ = false;
      return;
  }
}

int InputRouter::drain(Tap* out, int maxCount) {
  if (maxCount <= 0) return 0;
  int tail = tail_.load(std::memory_order_relaxed);
  const int head = head_.load(std::memory_order_acquire);
  int count = 0;
  while (tail != head && count < maxCount) {
    const Slot& s = slots_[tail % kCapacity];
    out[count].pos = s.pos;
    out[count].timeNs = s.timeNs;
    out[count].fromPrimaryPointer = (s.flags & kFlagPrimary) != 0;
    ++tail;
    ++count;
  }
  tail_.store(tail, std::memory_order_release);
  return count;
}

}  // namespace pp
