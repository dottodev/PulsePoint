#include "achievements.h"

#include <cmath>
#include <cstring>

#include "../core/config.h"

namespace pp {

namespace {

// The five hidden thresholds, in microseconds.
constexpr uint32_t kHumanUs = 150000;
constexpr uint32_t kHyperUs = 130000;
constexpr uint32_t kOverdriveUs = 110000;
constexpr uint32_t kTranscendUs = 90000;
constexpr uint32_t kPulsepointUs = 70000;

struct Row {
  const char* id;
  const char* name;
  const char* description;
  const char* progressLabel;
  IconKind icon;
  bool secret;
  ProgressStyle progress;
  double target;
  const char* secretRequirement;
};

// The single source of truth for the table.  Index order is the display order,
// and the last five entries are the hidden set.
constexpr Row kRows[kAchievementCount] = {
    // ---- Visible: showing up ---------------------------------------------
    {"first_pulse", "First Pulse", "Play your first game.", nullptr, IconKind::Pulse, false,
     ProgressStyle::None, 0.0, nullptr},
    {"getting_started", "Getting Started", "Complete 10 games.", "GAMES", IconKind::Stairs,
     false, ProgressStyle::Counter, 10.0, nullptr},
    {"again", "Again", "Play 25 games.", "GAMES", IconKind::Loop, false, ProgressStyle::Counter,
     25.0, nullptr},
    {"dedicated", "Dedicated", "Play 100 games.", "GAMES", IconKind::Pillar, false,
     ProgressStyle::Counter, 100.0, nullptr},
    {"never_enough", "Never Enough", "Play 500 games.", "GAMES", IconKind::Infinity, false,
     ProgressStyle::Counter, 500.0, nullptr},
    {"first_blood", "First Blood", "Get your first successful reaction.", nullptr,
     IconKind::Drop, false, ProgressStyle::None, 0.0, nullptr},

    // ---- Visible: speed ---------------------------------------------------
    {"sharp", "Sharp", "Achieve a reaction below 250 ms.", "MS", IconKind::Blade, false,
     ProgressStyle::Millis, 250.0, nullptr},
    {"fast", "Fast", "Achieve a reaction below 200 ms.", "MS", IconKind::Bolt, false,
     ProgressStyle::Millis, 200.0, nullptr},
    {"lightning", "Lightning", "Achieve a reaction below 175 ms.", "MS", IconKind::Flash, false,
     ProgressStyle::Millis, 175.0, nullptr},

    // ---- Visible: precision ----------------------------------------------
    {"precision", "Precision", "Achieve 95% accuracy in a game.", nullptr, IconKind::Crosshair,
     false, ProgressStyle::None, 0.0, nullptr},
    {"perfect", "Perfect", "Complete a round with 100% accuracy.", nullptr, IconKind::Diamond,
     false, ProgressStyle::None, 0.0, nullptr},
    {"combo", "Combo", "Reach a 10-hit streak.", "STREAK", IconKind::Link3, false,
     ProgressStyle::Counter, 10.0, nullptr},
    {"unstoppable", "Unstoppable", "Reach a 25-hit streak.", "STREAK", IconKind::Link4, false,
     ProgressStyle::Counter, 25.0, nullptr},
    {"machine", "Machine", "Reach a 50-hit streak.", "STREAK", IconKind::Gear, false,
     ProgressStyle::Counter, 50.0, nullptr},

    // ---- Visible: volume -------------------------------------------------
    {"century", "Century", "Make 100 successful presses.", "PRESSES", IconKind::Cents, false,
     ProgressStyle::Counter, 100.0, nullptr},
    {"thousand", "Thousand", "Make 1,000 successful presses.", "PRESSES", IconKind::Stack3,
     false, ProgressStyle::Counter, 1000.0, nullptr},
    {"ten_thousand", "Ten Thousand", "Make 10,000 successful presses.", "PRESSES",
     IconKind::Stack5, false, ProgressStyle::Counter, 10000.0, nullptr},

    // ---- Visible: execution ----------------------------------------------
    {"no_mistakes", "No Mistakes", "Complete a challenge without a miss.", nullptr,
     IconKind::Shield, false, ProgressStyle::None, 0.0, nullptr},
    {"consistent", "Consistent", "Maintain highly consistent reaction times across a challenge.",
     nullptr, IconKind::Wave, false, ProgressStyle::Consistency, 0.75, nullptr},
    {"maximum", "MAXIMUM", "Reach a major MAX milestone.", "LEVEL", IconKind::Mountain, false,
     ProgressStyle::Level, static_cast<double>(cfg::kMaxMilestoneLevel), nullptr},
    {"flick_master", "Flick Master", "Reach a high score in FLICK.", "SCORE",
     IconKind::Scatter, false, ProgressStyle::Score, static_cast<double>(cfg::kFlickMasterScore),
     nullptr},
    {"laser_focus", "Laser Focus", "Reach a high score in FOCUS.", "SCORE", IconKind::Iris,
     false, ProgressStyle::Score, static_cast<double>(cfg::kLaserFocusScore), nullptr},
    {"reflex", "Reflex", "Reach a high score in REFLEX.", "SCORE", IconKind::Hourglass, false,
     ProgressStyle::Score, static_cast<double>(cfg::kReflexChampionScore), nullptr},
    {"survivor", "Survivor", "Survive a long MAX run.", "LEVEL", IconKind::ShieldUp, false,
     ProgressStyle::Level, static_cast<double>(cfg::kMaxSurvivorLevel), nullptr},
    {"pulse_addict", "Pulse Addict", "Play 1,000 total games.", "GAMES", IconKind::Grid, false,
     ProgressStyle::Counter, 1000.0, nullptr},

    // ---- Hidden: above-human ---------------------------------------------
    {"human_plus", "HUMAN+", "A recorded reaction below 150 ms.", nullptr, IconKind::Human, true,
     ProgressStyle::None, 0.0, "REACT BELOW 150 MS"},
    {"hyperreflex", "HYPERREFLEX", "A recorded reaction below 130 ms.", nullptr, IconKind::Hyper,
     true, ProgressStyle::None, 0.0, "REACT BELOW 130 MS"},
    {"overdrive", "OVERDRIVE", "A recorded reaction below 110 ms.", nullptr, IconKind::Overdrive,
     true, ProgressStyle::None, 0.0, "REACT BELOW 110 MS"},
    {"transcend", "TRANSCEND", "A recorded reaction below 90 ms.", nullptr, IconKind::Transcend,
     true, ProgressStyle::None, 0.0, "REACT BELOW 90 MS"},
    {"pulsepoint", "PULSEPOINT", "A recorded reaction below 70 ms.", nullptr,
     IconKind::Pulsepoint, true, ProgressStyle::None, 0.0, "REACT BELOW 70 MS"},
};

uint32_t secretThresholdUs(int index) {
  switch (index) {
    case 25: return kHumanUs;
    case 26: return kHyperUs;
    case 27: return kOverdriveUs;
    case 28: return kTranscendUs;
    case 29: return kPulsepointUs;
    default: return 0;
  }
}

// Best reaction that may be trusted.  Values under the physiological floor are
// timing artefacts -- a save file can be hand-edited, so the guard lives here
// as well as in the statistics collector -- and they never progress a
// performance achievement.
uint32_t bestTrustedUs(const Stats& st) {
  return st.bestReactionUs >= kPlausibleMinReactionUs ? st.bestReactionUs : 0u;
}

double evaluate(const Row& row, int index, const Stats& st, const RoundSummary* r) {
  switch (row.progress) {
    case ProgressStyle::None:
      return 0.0;  // Boolean rows are decided below.
    case ProgressStyle::Counter: {
      // The label decides which counter, which keeps the table free of
      // per-entry lambda boilerplate.
      if (std::strcmp(row.progressLabel, "GAMES") == 0) return static_cast<double>(st.totalGames);
      if (std::strcmp(row.progressLabel, "STREAK") == 0) return static_cast<double>(st.bestStreak);
      if (std::strcmp(row.progressLabel, "PRESSES") == 0) return static_cast<double>(st.totalHits);
      return 0.0;
    }
    case ProgressStyle::Millis: {
      const uint32_t best = bestTrustedUs(st);
      if (best == 0) return static_cast<double>(row.target) * 2.0;
      return static_cast<double>(best) / 1000.0;
    }
    case ProgressStyle::Level: {
      const uint32_t lvl = st.modes[static_cast<int>(Mode::Max)].bestLevel;
      return static_cast<double>(lvl);
    }
    case ProgressStyle::Score: {
      const int modeIndex = std::strcmp(row.id, "flick_master") == 0   ? static_cast<int>(Mode::Flick)
                            : std::strcmp(row.id, "laser_focus") == 0 ? static_cast<int>(Mode::Focus)
                                                                     : static_cast<int>(Mode::Reflex);
      return static_cast<double>(st.modes[modeIndex].bestScore);
    }
    case ProgressStyle::Consistency:
      return r != nullptr ? static_cast<double>(r->consistency) : 0.0;
    case ProgressStyle::Percent:
      return r != nullptr ? static_cast<double>(r->accuracy) * 100.0 : 0.0;
  }
  return 0.0;
}

bool isAchieved(const Row& row, int index, const Stats& st, const RoundSummary* r) {
  switch (index) {
    case 0:  return st.totalGames >= 1;                       // First Pulse
    case 5:  return st.totalHits >= 1;                        // First Blood
    case 9:  return r != nullptr && r->accuracy >= 0.95f;     // Precision
    case 10: return r != nullptr && r->perfect;               // Perfect
    case 17: return r != nullptr && r->trials > 0 && r->misses == 0 &&
                     r->falseStarts == 0 && r->hits == r->trials;  // No Mistakes
    default: break;
  }
  if (index >= 25) {
    const uint32_t threshold = secretThresholdUs(index);
    const uint32_t best = bestTrustedUs(st);
    return threshold > 0 && best > 0 && best < threshold;
  }
  if (row.progress == ProgressStyle::None) return false;
  return evaluate(row, index, st, r) >= row.target;
}

}  // namespace

void seedAchievementState(AchievementState& state, const Stats& stats, uint32_t unlockMask) {
  state.unlockedCount = 0;
  state.visibleUnlocked = 0;
  state.lastUnlockIndex = -1;
  state.lastUnlockNs = 0;
  for (int i = 0; i < kAchievementCount; ++i) {
    AchievementDef& d = state.defs[i];
    const Row& row = kRows[i];
    d.id = row.id;
    d.name = row.name;
    d.description = row.description;
    d.progressLabel = row.progressLabel;
    d.icon = row.icon;
    d.secret = row.secret;
    d.progress = row.progress;
    d.target = row.target;
    d.secretRequirement = row.secretRequirement;
    d.achieved = (unlockMask & (1u << i)) != 0u;
    d.current = evaluate(row, i, stats, nullptr);
    if (d.achieved) {
      ++state.unlockedCount;
      if (!d.secret) ++state.visibleUnlocked;
    }
  }
}

int collectUnlocks(AchievementState& state, const Stats& stats, const RoundSummary* lastRound,
                   int* out, int maxOut) {
  int found = 0;
  for (int i = 0; i < kAchievementCount; ++i) {
    AchievementDef& d = state.defs[i];
    d.current = evaluate(kRows[i], i, stats, lastRound);
    if (d.achieved) continue;
    if (!isAchieved(kRows[i], i, stats, lastRound)) continue;

    d.achieved = true;
    ++state.unlockedCount;
    if (!d.secret) ++state.visibleUnlocked;
    state.lastUnlockIndex = i;
    if (out != nullptr && found < maxOut) out[found] = i;
    ++found;
  }
  return found;
}

}  // namespace pp
