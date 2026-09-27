// Per-round bookkeeping: presses, reaction samples, streaks, score.
//
// The Session is deliberately ignorant of what a "target" is.  Each mode calls
// it with explicit outcomes, which keeps the timing path identical for all four
// modes and makes the scoring rules testable without any rendering.
#pragma once

#include <cstdint>

#include "../core/clock.h"
#include "../core/rng.h"
#include "../meta/stats.h"

namespace pp {

// What a single accepted tap did.
enum class PressKind : uint8_t {
  Hit,          // Landed on the active target, inside the response window.
  WrongTarget,  // Landed on something that was not the target (FOCUS/FLICK).
  Spam,         // Nothing was on screen; a stray or mashed press.
  FalseStart,   // Pressed before the first stimulus of the round appeared.
};

// Reaction times below this are treated as measurement glitches rather than
// human performance: they are recorded and displayed, but they never unlock a
// performance achievement.  The lowest physiologically plausible value reported
// in the literature is well above this, and anything shorter in practice means
// the tap was timestamped before the stimulus.
constexpr uint32_t kPlausibleMinReactionUs = 45000;

struct RoundSummary {
  Mode mode = Mode::Reflex;
  int trials = 0;           // Stimuli actually presented.
  int hits = 0;
  int misses = 0;
  int falseStarts = 0;
  int wrongPresses = 0;
  int totalPresses = 0;
  int correctPresses = 0;   // FOCUS: the intended target was the one pressed.
  int streak = 0;           // Longest streak reached this round.
  int level = 1;            // MAX: level reached.
  uint32_t bestUs = 0;
  uint32_t worstUs = 0;
  double sumUs = 0.0;
  double sumSqUs = 0.0;
  int samples = 0;
  int speedBonusPoints = 0;
  int penaltyPoints = 0;
  int score = 0;
  float accuracy = 0.0f;
  float consistency = 0.0f;
  uint32_t bestPlausibleUs = 0;  // Best sample that is not a suspected glitch.
  Nanos durationNs = 0;
  Nanos survivalNs = 0;          // MAX: time from the first stimulus to the end.
  bool perfect = false;
  bool suspectedGlitch = false;

  void addSample(uint32_t us);
  float averageMs() const;
  // 1 - normalised coefficient of variation of the reaction samples.
  float computeConsistency(float fullAtCv, float zeroAtCv, int minSamples) const;
};

// Accumulating score for one round.
//
// A hit is worth speed x accuracy x consistency x streak x difficulty.  Every
// multiplier except speed and difficulty is a defence against mashing, so a
// player who trades accuracy for a lucky early hit still finishes far behind
// one who answers cleanly.
class Scorer {
 public:
  void begin(float refMs);

  void onHit(uint32_t rtUs, float runningAccuracy, float consistency, int streak,
             float levelFactor);
  void onPenalty(int points);

  int raw() const { return raw_; }
  int score() const { return raw_ > 0 ? raw_ : 0; }
  int speedBonus() const { return speedBonus_; }
  int penalties() const { return penalty_; }

 private:
  float refMs_ = 400.0f;
  int raw_ = 0;
  int speedBonus_ = 0;
  int penalty_ = 0;
};

class Session {
 public:
  void begin(Mode mode, uint64_t seed, Nanos nowNs);

  Mode mode() const { return summary_.mode; }
  uint64_t seed() const { return seed_; }
  Rng& rng() { return rng_; }
  bool roundActive() const { return roundActive_; }
  Nanos startedNs() const { return startNs_; }
  Nanos elapsedNs(Nanos now) const { return now > startNs_ ? now - startNs_ : 0; }
  // Time since the round's first stimulus; this is what MAX survival measures.
  Nanos liveNs(Nanos now) const {
    return firstStimulusNs_ > 0 ? (now > firstStimulusNs_ ? now - firstStimulusNs_ : 0) : 0;
  }

  // Called by a mode the moment a target becomes visible and answerable.
  void stimulusAppeared(Nanos t);
  bool stimulusActive() const { return stimulusActive_; }
  Nanos stimulusTimeNs() const { return stimulusNs_; }

  // Marks the current stimulus as expired without a valid press.
  void stimulusExpired();

  // Every accepted gameplay tap funnels through here exactly once.
  void registerPress(PressKind kind, uint32_t rtUs, float levelFactor);

  // FOCUS-style mode reporting whether a press landed on the intended target.
  void noteCorrectPress() { ++summary_.correctPresses; }

  const RoundSummary& summary() const { return summary_; }
  RoundSummary& mutableSummary() { return summary_; }
  const Scorer& scorer() const { return scorer_; }

  void endRound(Nanos now);

  // Recomputes the aggregate fields that the result screen and the
  // achievements read.  Called automatically on every mutation.
  void refresh();

 private:
  RoundSummary summary_{};
  Scorer scorer_{};
  Rng rng_{};
  uint64_t seed_ = 0;
  Nanos startNs_ = 0;
  Nanos firstStimulusNs_ = 0;
  Nanos stimulusNs_ = 0;
  bool stimulusActive_ = false;
  bool roundActive_ = false;
  int streak_ = 0;
};

}  // namespace pp
