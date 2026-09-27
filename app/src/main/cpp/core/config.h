// Every tunable gameplay number, in one place.
//
// Nothing in the game modes hard-codes a duration, a size or a score weight:
// they all read from here, so balance changes are a single-file edit and can
// be sanity-checked in one glance.
//
// Units: sizes in density-independent pixels (dp), times in milliseconds,
// score in points.  The `Score` block is deliberately readable -- the score
// formula is the part of the game a player will reverse-engineer, so it should
// be explicable in a sentence.
#pragma once

#include <cstdint>

namespace pp {
namespace cfg {

// ---------------------------------------------------------------------------
// Global feel
// ---------------------------------------------------------------------------

// UI is authored against this virtual width; everything else scales from the
// device's shorter edge so a tablet does not look like a stretched phone.
constexpr float kDesignShortEdgeDp = 380.0f;

// Animation durations.  Kept short on purpose: responsiveness always wins.
constexpr float kTapFeedbackSeconds = 0.055f;
constexpr float kHitFlashSeconds = 0.14f;
constexpr float kMissFlashSeconds = 0.20f;
constexpr float kScreenTransitionSeconds = 0.16f;
constexpr float kButtonPressSeconds = 0.09f;
constexpr float kResultRevealSeconds = 0.55f;
constexpr float kAchievementShowSeconds = 2.6f;
constexpr float kAchievementMinShowSeconds = 1.8f;
constexpr float kScoreCountUpSeconds = 0.75f;

// How many targets/particles we are ever willing to have alive at once.  Hard
// caps keep the frame cost flat on low-end devices.
constexpr int kMaxParticles = 160;
constexpr int kMaxTargets = 12;

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// Extra slop added around every tappable target so fingertips do not have to be
// pixel accurate.  This is generous on purpose: a reflex game should measure
// vision and decision time, not dexterity with a 3 mm radius.
constexpr float kTouchSlopDp = 10.0f;

// A stimulus is no longer answerable after this long.  Without it a player
// could idle for a minute and still "react" to a target they stopped seeing.
constexpr float kMaxResponseMs = 2000.0f;

// Display latency compensation.  A stimulus is stamped when the frame that
// first contains it is submitted, but photons reach the eye a frame or two
// later.  AUTO subtracts half of the measured frame interval, which is the
// standard first-order correction, and can be overridden in Settings.
constexpr float kDefaultAutoCalibrationMinMs = 0.0f;
constexpr float kDefaultAutoCalibrationMaxMs = 22.0f;

// ---------------------------------------------------------------------------
// REFLEX
// ---------------------------------------------------------------------------

constexpr int kReflexTrials = 10;
// Delay before the target appears.  Wide and randomised so the rhythm of the
// game cannot be predicted; it tightens slowly across the round.
constexpr float kReflexDelayStartMs = 1600.0f;
constexpr float kReflexDelaySpanMs = 2400.0f;   // Added on top, uniformly random.
constexpr float kReflexDelayEndMs = 900.0f;     // Floor by the last trial.
constexpr float kReflexResponseWindowMs = 1500.0f;
constexpr float kReflexTargetRadiusDp = 34.0f;

// ---------------------------------------------------------------------------
// FLICK
// ---------------------------------------------------------------------------

constexpr int kFlickTargetsPerRound = 20;
constexpr float kFlickRadiusStartDp = 30.0f;
constexpr float kFlickRadiusEndDp = 15.0f;
constexpr float kFlickSpawnGapStartMs = 260.0f;
constexpr float kFlickSpawnGapEndMs = 110.0f;
constexpr float kFlickResponseWindowMs = 1200.0f;
// How many targets may be on screen at once, ramping 1 -> 3.
constexpr int kFlickMaxConcurrentStart = 1;
constexpr int kFlickMaxConcurrentEnd = 3;
// Minimum centre-to-centre distance between live targets, so nothing overlaps.
constexpr float kFlickMinSeparationDp = 74.0f;

// ---------------------------------------------------------------------------
// FOCUS
// ---------------------------------------------------------------------------

constexpr int kFocusTargetsPerRound = 12;
constexpr float kFocusRadiusStartDp = 26.0f;
constexpr float kFocusRadiusEndDp = 13.0f;
constexpr int kFocusDistractorsStart = 1;
constexpr int kFocusDistractorsEnd = 6;
constexpr float kFocusResponseWindowMs = 1400.0f;
constexpr float kFocusResponseWindowEndMs = 750.0f;
// Below this progress the distractors are hollow rings and the correct target
// is a filled disc, which makes the search trivial.  Above it every target
// becomes a filled disc and the correct one is the only bullseye.
constexpr float kFocusSimilarityRamp = 0.45f;

// ---------------------------------------------------------------------------
// MAX
// ---------------------------------------------------------------------------

// Level N is reached after N successful reactions.  One mistake of any kind
// ends the run.
constexpr float kMaxSpawnDelayStartMs = 1200.0f;
constexpr float kMaxSpawnDelayEndMs = 280.0f;
constexpr float kMaxRadiusStartDp = 30.0f;
constexpr float kMaxRadiusEndDp = 13.0f;
constexpr float kMaxMoveSpeedStartDp = 0.0f;
constexpr float kMaxMoveSpeedEndDp = 190.0f;
constexpr int kMaxDistractorLevel = 8;    // Distractors appear from this level.
constexpr float kMaxResponseWindowStartMs = 1400.0f;
constexpr float kMaxResponseWindowEndMs = 620.0f;
// Levels needed for the "MAXIMUM" and "Survivor" achievements.
constexpr int kMaxMilestoneLevel = 25;
constexpr int kMaxSurvivorLevel = 20;

// Difficulty is a 0..1 progress value shared by every mode's curve, so the
// same round index always means the same amount of challenge.
constexpr float difficultyProgress(int index, int total) {
  if (total <= 1) return 0.0f;
  const float t = static_cast<float>(index) / static_cast<float>(total - 1);
  return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

inline float mix(float a, float b, float t) { return a + (b - a) * t; }

// ---------------------------------------------------------------------------
// Score
// ---------------------------------------------------------------------------

// The reaction time that earns zero speed points, per mode.  Slower than this
// and a hit is worth its accuracy and consistency points only.
constexpr float kReflexRefMs = 380.0f;
constexpr float kFlickRefMs = 460.0f;
constexpr float kFocusRefMs = 560.0f;
constexpr float kMaxRefMs = 420.0f;

constexpr int kSpeedPointsMax = 1000;
// Exponent on the speed term.  Above 1 this makes the gap between a good and a
// great reaction large, which is what keeps score chasing honest.
constexpr float kSpeedExponent = 1.7f;
// Accuracy multiplier ranges over [kAccuracyFloor, 1].  Because accuracy is
// hits / total presses, spamming mashes it towards zero and the score with it.
constexpr float kAccuracyFloor = 0.22f;
constexpr float kAccuracyExponent = 1.4f;
// Consistency multiplier ranges over [kConsistencyFloor, 1].
constexpr float kConsistencyFloor = 0.80f;
// Each hit in the streak adds this fraction, capped at kStreakCap.
constexpr float kStreakPerHit = 0.028f;
constexpr int kStreakCap = 32;
// Penalties, in points, applied to the running score.
constexpr int kMissPenalty = 45;
constexpr int kFalseStartPenalty = 150;
constexpr int kWrongTargetPenalty = 70;
// A press that hit nothing while nothing was on screen.
constexpr int kSpamPenalty = 25;
// Level multiplier in MAX, and difficulty multiplier elsewhere.
constexpr float kMaxLevelMultiplierStep = 0.045f;
constexpr float kMaxLevelMultiplierCap = 3.0f;

// Score required for the "high score" achievements.
constexpr int kFlickMasterScore = 14000;
constexpr int kLaserFocusScore = 16000;
constexpr int kReflexChampionScore = 12000;

// Consistency is expressed as 1 - coefficient of variation, clamped.  A round
// needs at least this many samples before consistency is meaningful.
constexpr float kConsistencyFullAtCv = 0.16f;   // CV <= this earns full marks.
constexpr float kConsistencyZeroAtCv = 0.55f;   // CV >= this earns nothing.
constexpr int kConsistencyMinSamples = 6;

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

constexpr float kTouchTargetMinDp = 44.0f;
constexpr float kButtonHeightDp = 56.0f;
constexpr float kScreenMarginDp = 22.0f;
constexpr float kTitleSizeDp = 46.0f;
constexpr float kHeroNumberSizeDp = 96.0f;
constexpr float kHeadingSizeDp = 24.0f;
constexpr float kBodySizeDp = 15.0f;
constexpr float kLabelSizeDp = 12.0f;
constexpr float kSmallSizeDp = 11.0f;

}  // namespace cfg
}  // namespace pp
