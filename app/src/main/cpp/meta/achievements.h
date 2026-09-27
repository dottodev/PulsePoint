// The achievement table and its unlock evaluator.
//
// Two categories, deliberately kept apart:
//
//  * 25 visible achievements.  Most are driven by lifetime counters, so they
//    unlock the moment the counter passes, and each one can show real progress
//    before it fires.
//
//  * 5 hidden "above-human" achievements.  These are performance-only: they
//    can never be granted by playing a lot, only by recording a single
//    reaction below a threshold.  They stay hidden -- name, description and
//    requirement all masked -- until they fire, and the unlock overlay is the
//    only place their requirement is ever shown.
//
// A note on what the above-human set is and is not: these are measurements of
// tap-to-stimulus latency taken by the app, not a claim that anyone has
// demonstrated superhuman biology.  Some of the thresholds sit below what is
// considered physiologically attainable with practice, which is exactly why
// they are framed as records rather than as capabilities.
#pragma once

#include <cstdint>

#include "../game/session.h"
#include "stats.h"

namespace pp {

enum class IconKind : uint8_t {
  Pulse,        // 1  First Pulse
  Stairs,       // 2  Getting Started
  Loop,         // 3  Again
  Pillar,       // 4  Dedicated
  Infinity,     // 5  Never Enough
  Drop,         // 6  First Blood
  Blade,        // 7  Sharp
  Bolt,         // 8  Fast
  Flash,        // 9  Lightning
  Crosshair,    // 10 Precision
  Diamond,      // 11 Perfect
  Link3,        // 12 Combo
  Link4,        // 13 Unstoppable
  Gear,         // 14 Machine
  Cents,        // 15 Century
  Stack3,       // 16 Thousand
  Stack5,       // 17 Ten Thousand
  Shield,       // 18 No Mistakes
  Wave,         // 19 Consistent
  Mountain,     // 20 MAXIMUM
  Scatter,      // 21 Flick Master
  Iris,         // 22 Laser Focus
  Hourglass,    // 23 Reflex
  ShieldUp,     // 24 Survivor
  Grid,         // 25 Pulse Addict
  // Hidden set.
  Human,        // 26 HUMAN+
  Hyper,        // 27 HYPERREFLEX
  Overdrive,    // 28 OVERDRIVE
  Transcend,    // 29 TRANSCEND
  Pulsepoint,   // 30 PULSEPOINT
  Count
};

constexpr int kAchievementCount = static_cast<int>(IconKind::Count);
constexpr int kHiddenAchievementCount = 5;
constexpr int kVisibleAchievementCount = kAchievementCount - kHiddenAchievementCount;

// How progress towards an achievement is expressed on the grid.
enum class ProgressStyle : uint8_t {
  None,        // Fires on a boolean, no bar.
  Counter,     // current / target counts, e.g. 10,428 / 10,000 PRESSES.
  Millis,      // current / target milliseconds, e.g. 214 / 250 MS.
  Percent,     // current / target percentage.
  Score,       // current / target score.
  Level,       // current / target level.
  Consistency, // 0..1 value against a target.
};

struct AchievementDef {
  const char* id;
  const char* name;
  const char* description;
  const char* progressLabel;  // Shown after "current / target", e.g. "PRESSES".
  IconKind icon;
  bool secret;                // Hidden until unlocked.
  ProgressStyle progress;
  double target;              // Meaning depends on `progress`.
  double current;             // Filled in by the evaluator.
  bool achieved;
  // The requirement text, only ever rendered for an unlocked secret.
  const char* secretRequirement;
};

struct AchievementState {
  AchievementDef defs[kAchievementCount];
  int unlockedCount = 0;
  int visibleUnlocked = 0;
  // Monotonic index of the last unlock animation, used to replay the flourish.
  int lastUnlockIndex = -1;
  Nanos lastUnlockNs = 0;
};

// Fills `state` with the full table and restores which entries are already
// unlocked from `unlockMask` (bit N of the mask is achievement N).  Progress for
// the lifetime-driven entries is computed immediately.
void seedAchievementState(AchievementState& state, const Stats& stats, uint32_t unlockMask);

// Re-evaluates everything and unlocks whatever has just been reached.
// Returns how many entries changed from locked to unlocked and fills `out`
// (up to `maxOut`) with the indices in unlock order.  Multiple unlocks from a
// single round are queued so the overlay can play them one after another.
// `lastRound` may be null, in which case the four round-scoped achievements are
// simply re-checked against nothing.
int collectUnlocks(AchievementState& state, const Stats& stats, const RoundSummary* lastRound,
                   int* out, int maxOut);

}  // namespace pp
