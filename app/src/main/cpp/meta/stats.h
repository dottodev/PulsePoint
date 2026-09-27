// Lifetime and per-mode statistics.
//
// These are the numbers behind the STATISTICS screen, the achievement
// progress bars and the personal bests.  Everything is monotonic counters
// except the "best" fields, so a corrupt or truncated save degrades to a
// smaller number rather than nonsense.
#pragma once

#include <cstdint>

namespace pp {

struct RoundSummary;

enum class Mode : uint8_t { Reflex = 0, Flick = 1, Focus = 2, Max = 3 };
constexpr int kModeCount = 4;

const char* modeName(Mode m);
const char* modeTagline(Mode m);
const char* modeDescription(Mode m);
// Modes are stored in enum order; guard against a bad index from a save file.
inline bool isValidMode(int i) { return i >= 0 && i < kModeCount; }

// Per-mode lifetime record.
struct ModeStats {
  uint64_t games = 0;
  uint64_t hits = 0;
  uint64_t presses = 0;
  uint64_t misses = 0;
  uint64_t bestScore = 0;
  uint64_t totalScore = 0;
  uint32_t bestReactionUs = 0;   // 0 == no valid reaction recorded yet.
  uint64_t reactionUsSum = 0;
  uint64_t reactionSamples = 0;
  float bestAccuracy = 0.0f;
  uint32_t bestStreak = 0;
  uint32_t bestLevel = 0;        // MAX only: highest level reached.
  uint64_t bestSurvivalNs = 0;   // MAX only: longest run.
  uint64_t totalTimeNs = 0;
  uint64_t lastPlayedUnixMs = 0;
};

struct Stats {
  // Global press accounting.  These are the numbers the PRESS COUNTER screen
  // and the lifetime achievements are built on.
  uint64_t totalPresses = 0;
  uint64_t totalHits = 0;
  uint64_t totalMisses = 0;
  uint64_t totalFalseStarts = 0;
  uint64_t totalGames = 0;
  uint64_t totalTimePlayedNs = 0;

  uint32_t bestReactionUs = 0;
  uint64_t reactionUsSum = 0;
  uint64_t reactionSamples = 0;
  float bestAccuracy = 0.0f;
  uint32_t bestStreak = 0;
  uint64_t totalScore = 0;

  ModeStats modes[kModeCount];

  // Derived helpers.  All of them are safe on a fresh profile.
  float averageReactionMs() const;
  float averageReactionMs(Mode m) const;
  float accuracy() const;
  uint32_t bestReactionMs() const { return bestReactionUs / 1000u; }
  uint32_t averageReactionMsU32() const;
  float timePlayedHours() const;

  void noteRound(Mode m, const struct RoundSummary& summary);
  void reset();
};

}  // namespace pp
