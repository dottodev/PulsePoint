#include "session.h"

#include <cmath>

#include "../core/config.h"
#include "../core/math_util.h"
#include "../meta/stats.h"

namespace pp {

namespace {

float refMsForMode(Mode m) {
  switch (m) {
    case Mode::Reflex: return cfg::kReflexRefMs;
    case Mode::Flick: return cfg::kFlickRefMs;
    case Mode::Focus: return cfg::kFocusRefMs;
    case Mode::Max: return cfg::kMaxRefMs;
  }
  return cfg::kReflexRefMs;
}

}  // namespace

// ---------------------------------------------------------------------------
// RoundSummary
// ---------------------------------------------------------------------------

void RoundSummary::addSample(uint32_t us) {
  ++samples;
  sumUs += static_cast<double>(us);
  sumSqUs += static_cast<double>(us) * static_cast<double>(us);
  if (bestUs == 0 || us < bestUs) bestUs = us;
  if (us > worstUs) worstUs = us;
  if (us >= kPlausibleMinReactionUs && (bestPlausibleUs == 0 || us < bestPlausibleUs)) {
    bestPlausibleUs = us;
  }
  if (us < kPlausibleMinReactionUs) suspectedGlitch = true;
}

float RoundSummary::averageMs() const {
  if (samples <= 0) return 0.0f;
  return static_cast<float>(sumUs / static_cast<double>(samples) / 1000.0);
}

float RoundSummary::computeConsistency(float fullAtCv, float zeroAtCv, int minSamples) const {
  if (samples < minSamples || samples < 2) return 0.0f;
  const double n = static_cast<double>(samples);
  const double mean = sumUs / n;
  if (mean <= 1.0) return 0.0f;
  // Two-pass-free variance: E[x^2] - E[x]^2, clamped for float safety.
  const double variance = sumSqUs / n - mean * mean;
  const double sd = variance > 0.0 ? std::sqrt(variance) : 0.0;
  const float cv = static_cast<float>(sd / mean);
  if (cv <= fullAtCv) return 1.0f;
  if (cv >= zeroAtCv) return 0.0f;
  const float t = (cv - fullAtCv) / (zeroAtCv - fullAtCv);
  return 1.0f - t;
}

// ---------------------------------------------------------------------------
// Scorer
// ---------------------------------------------------------------------------

void Scorer::begin(float refMs) {
  refMs_ = refMs > 1.0f ? refMs : 1.0f;
  raw_ = 0;
  speedBonus_ = 0;
  penalty_ = 0;
}

void Scorer::onHit(uint32_t rtUs, float runningAccuracy, float consistency, int streak,
                   float levelFactor) {
  const float rtMs = static_cast<float>(rtUs) / 1000.0f;
  const float speed = clamp01((refMs_ - rtMs) / refMs_);
  const float speedPoints = static_cast<float>(cfg::kSpeedPointsMax) * std::pow(speed, cfg::kSpeedExponent);

  const float acc = clamp01(runningAccuracy);
  const float accMult = cfg::kAccuracyFloor + (1.0f - cfg::kAccuracyFloor) * std::pow(acc, cfg::kAccuracyExponent);

  const float cons = clamp01(consistency);
  const float consMult = cfg::kConsistencyFloor + (1.0f - cfg::kConsistencyFloor) * cons;

  const int capped = streak < 0 ? 0 : (streak > cfg::kStreakCap ? cfg::kStreakCap : streak);
  const float streakMult = 1.0f + static_cast<float>(capped) * cfg::kStreakPerHit;

  const float level = levelFactor > 0.0f ? levelFactor : 1.0f;

  const float points = speedPoints * accMult * consMult * streakMult * level;
  const int gained = static_cast<int>(points + 0.5f);
  raw_ += gained;
  speedBonus_ += gained;
}

void Scorer::onPenalty(int points) {
  if (points <= 0) return;
  raw_ -= points;
  penalty_ += points;
  if (raw_ < 0) raw_ = 0;
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

void Session::begin(Mode mode, uint64_t seed, Nanos nowNs) {
  summary_ = RoundSummary{};
  summary_.mode = mode;
  seed_ = seed;
  rng_.seed(seed);
  startNs_ = nowNs;
  firstStimulusNs_ = 0;
  stimulusNs_ = 0;
  stimulusActive_ = false;
  roundActive_ = true;
  streak_ = 0;
  scorer_.begin(refMsForMode(mode));
  refresh();
}

void Session::stimulusAppeared(Nanos t) {
  stimulusActive_ = true;
  stimulusNs_ = t;
  if (firstStimulusNs_ == 0) firstStimulusNs_ = t;
  ++summary_.trials;
}

void Session::stimulusExpired() {
  // Only counts if a stimulus is genuinely still waiting: a wrong press already
  // retired the trial, and a mode must not be able to double-charge a miss.
  if (!stimulusActive_) return;
  stimulusActive_ = false;
  ++summary_.misses;
  streak_ = 0;
}

void Session::registerPress(PressKind kind, uint32_t rtUs, float levelFactor) {
  if (!roundActive_) return;  // Input after a round ends is ignored entirely.

  switch (kind) {
    case PressKind::Hit: {
      ++summary_.totalPresses;
      ++summary_.hits;
      ++streak_;
      if (streak_ > summary_.streak) summary_.streak = streak_;
      summary_.addSample(rtUs);
      stimulusActive_ = false;
      const float acc = static_cast<float>(summary_.hits) / static_cast<float>(summary_.totalPresses);
      scorer_.onHit(rtUs, acc, summary_.computeConsistency(cfg::kConsistencyFullAtCv,
                                                           cfg::kConsistencyZeroAtCv,
                                                           cfg::kConsistencyMinSamples),
                    streak_, levelFactor);
      break;
    }
    case PressKind::WrongTarget: {
      ++summary_.totalPresses;
      ++summary_.wrongPresses;
      ++summary_.misses;
      streak_ = 0;
      stimulusActive_ = false;
      scorer_.onPenalty(cfg::kWrongTargetPenalty);
      break;
    }
    case PressKind::FalseStart: {
      ++summary_.totalPresses;
      ++summary_.falseStarts;
      ++summary_.misses;
      streak_ = 0;
      scorer_.onPenalty(cfg::kFalseStartPenalty);
      break;
    }
    case PressKind::Spam: {
      ++summary_.totalPresses;
      ++summary_.misses;
      streak_ = 0;
      scorer_.onPenalty(cfg::kSpamPenalty);
      break;
    }
  }
  refresh();
}

void Session::endRound(Nanos now) {
  if (!roundActive_) return;
  roundActive_ = false;
  stimulusActive_ = false;
  summary_.durationNs = elapsedNs(now);
  summary_.survivalNs = liveNs(now);
  refresh();
}

void Session::refresh() {
  summary_.accuracy =
      summary_.totalPresses > 0
          ? static_cast<float>(summary_.hits) / static_cast<float>(summary_.totalPresses)
          : 0.0f;
  summary_.consistency = summary_.computeConsistency(cfg::kConsistencyFullAtCv,
                                                     cfg::kConsistencyZeroAtCv,
                                                     cfg::kConsistencyMinSamples);
  summary_.score = scorer_.score();
  summary_.speedBonusPoints = scorer_.speedBonus();
  summary_.penaltyPoints = scorer_.penalties();
  summary_.perfect = summary_.trials > 0 && summary_.hits == summary_.trials &&
                     summary_.totalPresses == summary_.hits && summary_.falseStarts == 0;
}

}  // namespace pp
