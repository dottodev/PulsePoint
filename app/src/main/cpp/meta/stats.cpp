#include "stats.h"

#include <cmath>

#include "../core/clock.h"
#include "../game/session.h"

namespace pp {

namespace {

float usToMs(uint64_t us) { return static_cast<float>(us) / 1000.0f; }

}  // namespace

const char* modeName(Mode m) {
  switch (m) {
    case Mode::Reflex: return "REFLEX";
    case Mode::Flick: return "FLICK";
    case Mode::Focus: return "FOCUS";
    case Mode::Max: return "MAX";
  }
  return "REFLEX";
}

const char* modeTagline(Mode m) {
  switch (m) {
    case Mode::Reflex: return "How fast can you react?";
    case Mode::Flick: return "React. Aim. Hit.";
    case Mode::Focus: return "See the target. Ignore the rest.";
    case Mode::Max: return "How long can you survive?";
  }
  return "";
}

const char* modeDescription(Mode m) {
  switch (m) {
    case Mode::Reflex:
      return "One target. A random delay you cannot predict. Ten chances to post a "
             "number you can be proud of.";
    case Mode::Flick:
      return "Targets light up across the field, several at a time. They shrink and "
             "arrive faster the longer you hold on.";
    case Mode::Focus:
      return "One target is the real one. The rest look almost identical. Find the "
             "bullseye, ignore everything else.";
    case Mode::Max:
      return "Endless. Every clean reaction makes the next one harder. One mistake "
             "and the run is over.";
  }
  return "";
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

float Stats::averageReactionMs() const {
  if (reactionSamples == 0) return 0.0f;
  return usToMs(reactionUsSum / reactionSamples);
}

float Stats::averageReactionMs(Mode m) const {
  if (!isValidMode(static_cast<int>(m))) return 0.0f;
  const ModeStats& ms = modes[static_cast<int>(m)];
  if (ms.reactionSamples == 0) return 0.0f;
  return usToMs(ms.reactionUsSum / ms.reactionSamples);
}

uint32_t Stats::averageReactionMsU32() const {
  if (reactionSamples == 0) return 0;
  return static_cast<uint32_t>(reactionUsSum / reactionSamples / 1000ull);
}

float Stats::accuracy() const {
  if (totalPresses == 0) return 0.0f;
  return static_cast<float>(totalHits) / static_cast<float>(totalPresses);
}

float Stats::timePlayedHours() const {
  return static_cast<float>(totalTimePlayedNs) / static_cast<float>(kNsPerSec) / 3600.0f;
}


void Stats::noteRound(Mode m, const RoundSummary& s) {
  ++totalGames;
  totalPresses += static_cast<uint64_t>(s.totalPresses);
  totalHits += static_cast<uint64_t>(s.hits);
  totalMisses += static_cast<uint64_t>(s.misses);
  totalFalseStarts += static_cast<uint64_t>(s.falseStarts);
  totalTimePlayedNs += static_cast<uint64_t>(s.durationNs);
  totalScore += static_cast<uint64_t>(s.score > 0 ? s.score : 0);

  if (s.samples > 0) {
    // The round's aggregate sum and sample count give the same result as
    // iterating over the individual samples, in constant time.
    reactionSamples += static_cast<uint64_t>(s.samples);
    reactionUsSum += static_cast<uint64_t>(s.sumUs);
  }
  // Personal bests only ever come from a plausible sample: a value below the
  // physiological floor is a timing glitch, not a record.
  if (s.bestPlausibleUs > 0 && (bestReactionUs == 0 || s.bestPlausibleUs < bestReactionUs)) {
    bestReactionUs = s.bestPlausibleUs;
  }
  if (s.accuracy > bestAccuracy) bestAccuracy = s.accuracy;
  if (static_cast<uint32_t>(s.streak) > bestStreak) bestStreak = static_cast<uint32_t>(s.streak);

  if (!isValidMode(static_cast<int>(m))) return;
  ModeStats& ms = modes[static_cast<int>(m)];
  ++ms.games;
  ms.presses += static_cast<uint64_t>(s.totalPresses);
  ms.hits += static_cast<uint64_t>(s.hits);
  ms.misses += static_cast<uint64_t>(s.misses);
  ms.totalTimeNs += static_cast<uint64_t>(s.durationNs);
  if (s.score > 0) ms.totalScore += static_cast<uint64_t>(s.score);
  if (static_cast<uint64_t>(s.score) > ms.bestScore) ms.bestScore = static_cast<uint64_t>(s.score);
  if (s.samples > 0) {
    ms.reactionSamples += static_cast<uint64_t>(s.samples);
    ms.reactionUsSum += static_cast<uint64_t>(s.sumUs);
  }
  if (s.bestPlausibleUs > 0 && (ms.bestReactionUs == 0 || s.bestPlausibleUs < ms.bestReactionUs)) {
    ms.bestReactionUs = s.bestPlausibleUs;
  }
  if (s.accuracy > ms.bestAccuracy) ms.bestAccuracy = s.accuracy;
  if (static_cast<uint32_t>(s.streak) > ms.bestStreak) ms.bestStreak = static_cast<uint32_t>(s.streak);
  if (static_cast<uint32_t>(s.level) > ms.bestLevel) ms.bestLevel = static_cast<uint32_t>(s.level);
  if (static_cast<uint64_t>(s.survivalNs) > ms.bestSurvivalNs) {
    ms.bestSurvivalNs = static_cast<uint64_t>(s.survivalNs);
  }
}

void Stats::reset() { *this = Stats{}; }

}  // namespace pp
