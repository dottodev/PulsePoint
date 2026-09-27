// Headless test harness for the platform-independent half of PulsePoint.
//
// The game logic, scoring, statistics, achievements, save file and input
// de-duplication have no OpenGL or JNI dependency, which means all of it can be
// exercised on a desktop machine.  That is the point of this binary: reaction
// timing and score integrity are far too easy to get subtly wrong to only find
// out on a phone.
//
// Build and run:
//   python3 tools/build_host_tests.py
//   ./build-host/pulsepoint_tests
//
// Pass a filter substring to run a subset:
//   ./build-host/pulsepoint_tests score
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../app/src/main/cpp/core/clock.h"
#include "../app/src/main/cpp/core/config.h"
#include "../app/src/main/cpp/core/input.h"
#include "../app/src/main/cpp/core/rng.h"
#include "../app/src/main/cpp/game/modes.h"
#include "../app/src/main/cpp/game/session.h"
#include "../app/src/main/cpp/meta/achievements.h"
#include "../app/src/main/cpp/meta/profile.h"
#include "../app/src/main/cpp/generated/font_atlas.h"
#include "../app/src/main/cpp/meta/stats.h"
#include "../app/src/main/cpp/audio/synth.h"
#include "../app/src/main/cpp/ui/format.h"

namespace {

int g_failures = 0;
int g_checks = 0;
const char* g_currentTest = "";

void reportFailure(const char* file, int line, const char* expr) {
  ++g_failures;
  std::printf("  FAIL %s:%d  %s   [%s]\n", file, line, expr, g_currentTest);
}

#define CHECK(cond)                                    \
  do {                                                  \
    ++g_checks;                                         \
    if (!(cond)) reportFailure(__FILE__, __LINE__, #cond); \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                    \
  do {                                                                          \
    ++g_checks;                                                                 \
    const double va = static_cast<double>(a);                                   \
    const double vb = static_cast<double>(b);                                   \
    if (!(vb - va <= (tol) && va - vb <= (tol))) {                              \
      char buf[160];                                                            \
      std::snprintf(buf, sizeof(buf), "%s (%.6f) ~= %s (%.6f)", #a, va, #b, vb); \
      reportFailure(__FILE__, __LINE__, buf);                                   \
    }                                                                           \
  } while (0)

void beginTest(const char* name) {
  g_currentTest = name;
  std::printf("- %s\n", name);
}

bool shouldRun(const char* filter, const char* name) {
  return filter == nullptr || std::strlen(filter) == 0 || std::strstr(name, filter) != nullptr;
}

using TestFn = void (*)();

struct TestCase {
  const char* name;
  TestFn fn;
};

std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct Registrar {
  Registrar(const char* name, TestFn fn) { registry().push_back({name, fn}); }
};

#define TEST(name)                                    \
  void test_##name();                                 \
  Registrar reg_##name(#name, test_##name);           \
  void test_##name()

// ---------------------------------------------------------------------------
// Helpers that drive the game the way a player would.
// ---------------------------------------------------------------------------

using pp::Mode;
using pp::Nanos;
using pp::PressKind;
using pp::RoundSummary;
using pp::Session;

struct Harness {
  Session session;
  Nanos now = 1'000'000'000;
  Mode mode = Mode::Reflex;
  int completed = 0;

  void begin(Mode m, uint64_t seed = 12345) {
    mode = m;
    session.begin(m, seed, now);
  }

  // Simulates a clean, correctly-timed response.
  void hit(uint32_t rtUs, float levelFactor = 1.0f) {
    now += static_cast<Nanos>(rtUs) * 1000;
    session.registerPress(PressKind::Hit, rtUs, levelFactor);
  }
};

uint32_t usOf(float ms) { return static_cast<uint32_t>(ms * 1000.0f + 0.5f); }

// ---------------------------------------------------------------------------
// Core
// ---------------------------------------------------------------------------

TEST(clock_is_monotonic_and_nanosecond_granular) {
  const Nanos a = pp::monotonicNow();
  Nanos last = a;
  int increases = 0;
  for (int i = 0; i < 200000; ++i) {
    const Nanos t = pp::monotonicNow();
    if (t >= last) ++increases;
    last = t;
  }
  CHECK(increases == 200000);
  CHECK(last > a);
  // Must not be a millisecond clock: two consecutive reads 20 ns apart should
  // be distinguishable.  Over 200k reads the accumulated time is far larger
  // than 200k ms on any real platform unless the clock has ns resolution.
  CHECK((last - a) / pp::kNsPerMs <= 200000);
  CHECK(pp::msFromNs(1500 * pp::kNsPerMs) > 1499.9f);
  CHECK(pp::msFromNs(1500 * pp::kNsPerMs) < 1500.1f);
}

TEST(rng_is_deterministic_and_uniform) {
  pp::Rng a(42), b(42);
  for (int i = 0; i < 1000; ++i) CHECK(a.nextU64() == b.nextU64());

  pp::Rng r(7);
  int buckets[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  for (int i = 0; i < 100000; ++i) {
    const float f = r.nextFloat();
    CHECK(f >= 0.0f && f < 1.0f);
    ++buckets[static_cast<int>(f * 10.0f)];
  }
  for (int i = 0; i < 10; ++i) CHECK(buckets[i] > 8000 && buckets[i] < 12000);

  pp::Rng c(99), d(100);
  bool differs = false;
  for (int i = 0; i < 8; ++i) differs = differs || (c.nextU64() != d.nextU64());
  CHECK(differs);
}

TEST(input_dedup_rejects_duplicate_deliveries) {
  pp::InputRouter router;
  router.configure(1.0f);

  pp::Tap taps[8];
  const Nanos t0 = 5'000'000'000;

  auto down = [&](int id, float x, float y, Nanos t) {
    pp::PointerSample s;
    s.pointerId = id;
    s.phase = pp::PointerPhase::Down;
    s.pos = {x, y};
    s.timeNs = t;
    s.valid = true;
    router.push(s);
  };
  auto up = [&](int id, Nanos t) {
    pp::PointerSample s;
    s.pointerId = id;
    s.phase = pp::PointerPhase::Up;
    s.pos = {0, 0};
    s.timeNs = t;
    s.valid = true;
    router.push(s);
  };

  // 1. A single clean tap is delivered once.
  down(0, 100, 200, t0);
  up(0, t0 + 40 * pp::kNsPerMs);
  CHECK(router.drain(taps, 8) == 1);
  CHECK(taps[0].timeNs == t0);
  CHECK(taps[0].pos.x == 100 && taps[0].pos.y == 200);

  // 2. The same physical tap re-delivered with an identical timestamp is dropped.
  router.reset();
  down(0, 100, 200, t0);
  up(0, t0 + 40 * pp::kNsPerMs);
  down(0, 100, 200, t0);
  CHECK(router.drain(taps, 8) == 1);

  // 3. Re-delivered one frame later at the same spot is still one tap.
  router.reset();
  down(0, 100, 200, t0);
  up(0, t0 + 5 * pp::kNsPerMs);
  down(0, 100, 200, t0 + 16 * pp::kNsPerMs);
  CHECK(router.drain(taps, 8) == 1);

  // 4. A genuine fast second tap somewhere else is preserved: this is the case
  //    a naive "debounce everything" filter would break.
  router.reset();
  down(0, 100, 200, t0);
  up(0, t0 + 4 * pp::kNsPerMs);
  down(0, 500, 600, t0 + 40 * pp::kNsPerMs);
  CHECK(router.drain(taps, 8) == 2);
  CHECK(taps[1].pos.x == 500);

  // 5. A second finger while the first is down is not a reaction measurement.
  router.reset();
  down(0, 100, 200, t0);
  down(1, 300, 400, t0 + 20 * pp::kNsPerMs);
  up(0, t0 + 30 * pp::kNsPerMs);
  CHECK(router.drain(taps, 8) == 1);

  // 6. An implausible burst at the same spot collapses to a single tap.
  router.reset();
  for (int i = 0; i < 12; ++i) {
    down(0, 250, 250, t0 + i * 1'000'000);
    up(0, t0 + i * 1'000'000 + 200'000);
  }
  const int n = router.drain(taps, 8);
  CHECK(n == 1);
}

// ---------------------------------------------------------------------------
// Scoring
// ---------------------------------------------------------------------------

TEST(score_rewards_speed) {
  Harness h;
  h.begin(Mode::Reflex);
  for (int i = 0; i < 8; ++i) h.hit(usOf(150));
  const int fast = h.session.scorer().score();

  Harness s;
  s.begin(Mode::Reflex);
  for (int i = 0; i < 8; ++i) s.hit(usOf(340));
  const int slow = s.session.scorer().score();

  CHECK(fast > slow);
  CHECK(slow > 0);
  // The speed term is a steep power curve: a 150 ms reaction must be worth
  // clearly more than twice a 340 ms one, otherwise the score stops tracking
  // the thing the game is about.
  CHECK(fast > slow * 2);
}

TEST(score_punishes_spam) {
  // Mashing at nothing, then getting one lucky early hit, must not beat a clean
  // run.  This is the single most important score property.
  Harness spam;
  spam.begin(Mode::Reflex);
  for (int i = 0; i < 40; ++i) spam.session.registerPress(PressKind::Spam, 0, 1.0f);
  spam.hit(usOf(160));

  Harness clean;
  clean.begin(Mode::Reflex);
  for (int i = 0; i < 5; ++i) clean.hit(usOf(200));

  CHECK(clean.session.scorer().score() > spam.session.scorer().score() * 3);
  // Raw score never goes below zero, so a mash-heavy run cannot be a strategy.
  CHECK(spam.session.scorer().score() >= 0);
  CHECK(clean.session.summary().accuracy > spam.session.summary().accuracy);
}

TEST(score_penalises_false_starts_and_breaks_streak) {
  Harness h;
  h.begin(Mode::Reflex);
  for (int i = 0; i < 5; ++i) h.hit(usOf(200));
  CHECK(h.session.summary().streak == 5);
  const int before = h.session.scorer().score();

  h.session.registerPress(PressKind::FalseStart, 0, 1.0f);
  CHECK(h.session.summary().falseStarts == 1);
  CHECK(h.session.summary().streak == 5);  // Best streak is a high-water mark.
  CHECK(h.session.scorer().score() < before);
}

TEST(score_rewards_streak_and_difficulty) {
  // Same six reactions, same accuracy, same consistency.  The only difference is
  // whether they happened in one round (streak climbs to 6) or in six separate
  // rounds (streak never leaves 1).
  Harness chain, hard;
  chain.begin(Mode::Reflex);
  hard.begin(Mode::Max);
  for (int i = 0; i < 6; ++i) {
    chain.hit(usOf(220), 1.0f);
    hard.hit(usOf(220), 2.0f);
  }
  const int chainScore = chain.session.scorer().score();
  CHECK(chainScore > 0);

  // Six isolated hits at streak 1 are worth less than one six-hit chain.
  int soloScore = 0;
  for (int i = 0; i < 6; ++i) {
    Harness one;
    one.begin(Mode::Reflex, 5000 + i);
    one.hit(usOf(220), 1.0f);
    soloScore += one.session.scorer().score();
  }
  CHECK(chainScore > soloScore);
  // And the same reactions at a higher difficulty level are worth more.
  CHECK(hard.session.scorer().score() > chainScore);
}

TEST(consistency_measures_spread_not_speed) {
  RoundSummary tight, loose, fast;
  for (int i = 0; i < 10; ++i) {
    tight.addSample(usOf(200 + (i % 2) * 4));
    loose.addSample(usOf(120 + i * 60));
    fast.addSample(usOf(140));
  }
  const float ct = tight.computeConsistency(0.16f, 0.55f, 6);
  const float cl = loose.computeConsistency(0.16f, 0.55f, 6);
  const float cf = fast.computeConsistency(0.16f, 0.55f, 6);
  CHECK(ct > 0.95f);
  CHECK(cl < 0.4f);
  CHECK(ct - cl > 0.5f);
  // Consistency is about spread, so a fast but rock-steady round scores full.
  CHECK_NEAR(cf, 1.0f, 0.001);
  // Too few samples is not "perfectly consistent", it is "unknown".
  RoundSummary tiny;
  tiny.addSample(usOf(200));
  CHECK(tiny.computeConsistency(0.16f, 0.55f, 6) == 0.0f);
}

TEST(reaction_precision_exceeds_display_precision) {
  // Two stimuli 500 microseconds apart.  Internally they are distinct; on the
  // result screen both read as "200 MS", which is the point: the stored value
  // is finer than anything the player is shown.
  RoundSummary a, b;
  a.addSample(200400);
  b.addSample(200900);
  CHECK(a.bestUs != b.bestUs);
  CHECK(a.bestUs / 1000u == b.bestUs / 1000u);
  CHECK_NEAR(a.averageMs(), 200.4f, 0.01);
  CHECK_NEAR(b.averageMs(), 200.9f, 0.01);
}

TEST(plausibility_floor_flags_glitches) {
  RoundSummary glitchy, honest;
  glitchy.addSample(12000);  // 12 ms: physically impossible from a touch screen.
  glitchy.addSample(210000);
  honest.addSample(210000);
  CHECK(glitchy.suspectedGlitch);
  CHECK(!honest.suspectedGlitch);
  CHECK(glitchy.bestPlausibleUs == 210000u);
  CHECK(honest.bestPlausibleUs == 210000u);
}

// ---------------------------------------------------------------------------
// Session / stats
// ---------------------------------------------------------------------------

TEST(session_counts_every_outcome_exactly_once) {
  Harness h;
  h.begin(Mode::Reflex, 9);
  h.session.stimulusAppeared(h.now);
  h.hit(usOf(200));
  CHECK(h.session.summary().trials == 1);
  CHECK(h.session.summary().hits == 1);
  CHECK(h.session.summary().totalPresses == 1);
  CHECK(!h.session.stimulusActive());

  h.session.registerPress(PressKind::Spam, 0, 1.0f);
  h.session.registerPress(PressKind::FalseStart, 0, 1.0f);
  h.session.registerPress(PressKind::WrongTarget, 0, 1.0f);
  const RoundSummary& s = h.session.summary();
  CHECK(s.totalPresses == 4);
  CHECK(s.hits == 1);
  CHECK(s.misses == 3);
  CHECK(s.falseStarts == 1);
  CHECK(s.wrongPresses == 1);
  CHECK_NEAR(s.accuracy, 0.25f, 0.0001f);
}

TEST(session_ignores_input_after_the_round_ends) {
  Harness h;
  h.begin(Mode::Reflex);
  h.session.stimulusAppeared(h.now);
  h.hit(usOf(200));
  h.now += 5 * pp::kNsPerSec;
  h.session.endRound(h.now);
  const int presses = h.session.summary().totalPresses;

  h.session.registerPress(PressKind::Hit, usOf(180), 1.0f);
  h.session.registerPress(PressKind::Spam, 0, 1.0f);
  CHECK(h.session.summary().totalPresses == presses);
  CHECK(h.session.summary().hits == 1);
  // The round clock starts when the round begins, not when it is abandoned.
  CHECK(h.session.summary().durationNs == h.now - h.session.startedNs());
}

TEST(session_does_not_double_count_a_miss) {
  Harness h;
  h.begin(Mode::Reflex);
  h.session.stimulusAppeared(h.now);
  h.session.registerPress(PressKind::WrongTarget, 0, 1.0f);
  // A mode that also calls stimulusExpired afterwards must not charge twice.
  h.session.stimulusExpired();
  h.session.stimulusExpired();
  CHECK(h.session.summary().misses == 1);
}

TEST(stats_aggregate_lifetime_and_per_mode) {
  pp::Stats st;
  for (int g = 0; g < 3; ++g) {
    Harness h;
    h.begin(g == 1 ? Mode::Flick : Mode::Reflex, 1000 + g);
    for (int i = 0; i < 4; ++i) {
      h.session.stimulusAppeared(h.now);
      h.hit(usOf(180 + i * 20));
    }
    h.now += 3 * pp::kNsPerSec;
    h.session.endRound(h.now);
    st.noteRound(h.session.mode(), h.session.summary());
  }
  CHECK(st.totalGames == 3);
  CHECK(st.totalHits == 12);
  CHECK(st.totalPresses == 12);
  CHECK(st.reactionSamples == 12);
  CHECK(st.bestReactionUs == usOf(180));
  CHECK_NEAR(st.averageReactionMs(), 210.0f, 0.01f);
  CHECK(st.modes[0].games == 2);
  CHECK(st.modes[1].games == 1);
  CHECK(st.modes[2].games == 0);
  CHECK(st.modes[1].bestReactionUs == usOf(180));
  CHECK(st.modes[0].bestReactionUs == usOf(180));
  CHECK(st.bestAccuracy > 0.99f);
  CHECK(st.bestStreak == 4);
}

TEST(stats_time_played_accumulates) {
  pp::Stats st;
  Harness h;
  h.begin(Mode::Max);
  h.now += 42 * pp::kNsPerSec;
  h.session.endRound(h.now);
  st.noteRound(Mode::Max, h.session.summary());
  CHECK(st.totalTimePlayedNs == 42ull * pp::kNsPerSec);
  CHECK_NEAR(st.timePlayedHours(), 42.0f / 3600.0f, 1e-6);
}

// ---------------------------------------------------------------------------
// Achievements
// ---------------------------------------------------------------------------

void seedWith(pp::AchievementState& st, pp::Stats& stats) {
  pp::seedAchievementState(st, stats, 0);
}

TEST(achievement_table_is_complete_and_well_formed) {
  pp::Stats stats;
  pp::AchievementState st;
  seedWith(st, stats);
  CHECK(st.unlockedCount == 0);

  // Exactly 30 entries, 25 visible plus 5 hidden.
  CHECK(pp::kAchievementCount == 30);
  CHECK(pp::kVisibleAchievementCount == 25);
  CHECK(pp::kHiddenAchievementCount == 5);

  for (int i = 0; i < pp::kAchievementCount; ++i) {
    const pp::AchievementDef& d = st.defs[i];
    CHECK(d.id != nullptr && d.id[0] != '\0');
    CHECK(d.name != nullptr && d.name[0] != '\0');
    CHECK(d.description != nullptr && d.description[0] != '\0');
    CHECK(!d.achieved);
  }
  // Names and ids must be unique across the table.
  for (int i = 0; i < pp::kAchievementCount; ++i) {
    for (int j = i + 1; j < pp::kAchievementCount; ++j) {
      CHECK(std::strcmp(st.defs[i].id, st.defs[j].id) != 0);
      CHECK(std::strcmp(st.defs[i].name, st.defs[j].name) != 0);
      CHECK(static_cast<int>(st.defs[i].icon) != static_cast<int>(st.defs[j].icon));
    }
  }
  for (int i = 0; i < pp::kHiddenAchievementCount; ++i) {
    CHECK(st.defs[pp::kVisibleAchievementCount + i].secret);
    CHECK(st.defs[pp::kVisibleAchievementCount + i].secretRequirement != nullptr);
  }
  for (int i = 0; i < pp::kVisibleAchievementCount; ++i) CHECK(!st.defs[i].secret);
}

TEST(achievements_unlock_on_lifetime_counters) {
  pp::Stats stats;
  stats.totalGames = 10;
  stats.totalHits = 1000;
  stats.bestStreak = 50;
  pp::AchievementState st;
  seedWith(st, stats);

  int unlocked[40];
  const int n = pp::collectUnlocks(st, stats, nullptr, unlocked, 40);
  CHECK(n >= 5);
  CHECK(st.defs[0].achieved);   // First Pulse
  CHECK(st.defs[1].achieved);   // Getting Started
  CHECK(st.defs[5].achieved);   // First Blood
  CHECK(st.defs[11].achieved);  // Combo
  CHECK(st.defs[13].achieved);  // Machine
  CHECK(st.defs[14].achieved);  // Century
  CHECK(st.defs[15].achieved);  // Thousand
  CHECK(!st.defs[16].achieved); // Ten Thousand: 10,000 hits is out of reach
  CHECK(st.unlockedCount == n);
}

TEST(above_human_achievements_require_performance_not_volume) {
  // Thousands of games must not unlock a single hidden achievement.
  pp::Stats grind;
  grind.totalGames = 100000;
  grind.totalHits = 1000000;
  grind.bestStreak = 9999;
  grind.bestAccuracy = 1.0f;
  grind.modes[0].bestScore = 999999;
  pp::AchievementState st;
  seedWith(st, grind);
  pp::collectUnlocks(st, grind, nullptr, nullptr, 0);
  for (int i = 0; i < pp::kHiddenAchievementCount; ++i) {
    CHECK(!st.defs[pp::kVisibleAchievementCount + i].achieved);
  }

  // One fast reaction unlocks exactly the thresholds it beats, and only those.
  pp::Stats fast;
  fast.bestReactionUs = 120000;  // 120 ms
  pp::AchievementState st2;
  seedWith(st2, fast);
  pp::collectUnlocks(st2, fast, nullptr, nullptr, 0);
  CHECK(st2.defs[25].achieved);  // HUMAN+   (<150)
  CHECK(st2.defs[26].achieved);  // HYPERREFLEX (<130)
  CHECK(!st2.defs[27].achieved); // OVERDRIVE (<110)
  CHECK(!st2.defs[28].achieved); // TRANSCEND  (<90)
  CHECK(!st2.defs[29].achieved); // PULSEPOINT (<70)
  CHECK(st2.defs[27].secret);    // still masked
}

TEST(above_human_thresholds_fire_in_order) {
  const uint32_t thresholds[5] = {150000, 130000, 110000, 90000, 70000};
  for (int k = 0; k < 5; ++k) {
    pp::Stats s;
    s.bestReactionUs = thresholds[k] - 1000;
    pp::AchievementState st;
    seedWith(st, s);
    pp::collectUnlocks(st, s, nullptr, nullptr, 0);
    for (int i = 0; i < 5; ++i) {
      const bool shouldBe = i <= k;
      CHECK(st.defs[25 + i].achieved == shouldBe);
    }
  }
  // Exactly on the threshold does not count: "below 150" means below.
  pp::Stats edge;
  edge.bestReactionUs = 150000;
  pp::AchievementState st;
  seedWith(st, edge);
  pp::collectUnlocks(st, edge, nullptr, nullptr, 0);
  CHECK(!st.defs[25].achieved);
}

TEST(above_human_ignores_implausible_glitches) {
  pp::Stats s;
  s.bestReactionUs = 9000;  // A 9 ms reading is a timing artefact.
  pp::AchievementState st;
  seedWith(st, s);
  pp::collectUnlocks(st, s, nullptr, nullptr, 0);
  for (int i = 0; i < 5; ++i) CHECK(!st.defs[25 + i].achieved);
}

TEST(round_achievements_need_a_round) {
  pp::Stats stats;
  pp::AchievementState st;
  seedWith(st, stats);

  RoundSummary perfect;
  perfect.trials = 10;
  perfect.hits = 10;
  perfect.totalPresses = 10;
  perfect.accuracy = 1.0f;
  perfect.perfect = true;
  perfect.consistency = 0.9f;
  const int n = pp::collectUnlocks(st, stats, &perfect, nullptr, 0);
  CHECK(n >= 3);
  CHECK(st.defs[9].achieved);   // Precision
  CHECK(st.defs[10].achieved);  // Perfect
  CHECK(st.defs[17].achieved);  // No Mistakes
  CHECK(st.defs[18].achieved);  // Consistent
}

TEST(achievement_progress_is_reported) {
  pp::Stats stats;
  stats.totalGames = 428;
  stats.totalHits = 10428;
  pp::AchievementState st;
  seedWith(st, stats);
  CHECK_NEAR(st.defs[15].current, 10428.0, 0.5);   // Thousand
  CHECK_NEAR(st.defs[15].target, 1000.0, 0.5);
  CHECK_NEAR(st.defs[16].current, 10428.0, 0.5);   // Ten Thousand, partially done
  CHECK_NEAR(st.defs[16].target, 10000.0, 0.5);
  CHECK_NEAR(st.defs[2].current, 428.0, 0.5);      // Again
  CHECK(st.defs[6].current >= 0.0);                // Sharp: no reaction yet
}

TEST(achievement_unlocks_persist_through_a_save) {
  pp::Stats stats;
  stats.totalGames = 10;
  pp::AchievementState st;
  seedWith(st, stats);
  pp::collectUnlocks(st, stats, nullptr, nullptr, 0);
  const int unlockedBefore = st.unlockedCount;
  CHECK(unlockedBefore > 0);

  uint32_t mask = 0;
  for (int i = 0; i < pp::kAchievementCount; ++i) {
    if (st.defs[i].achieved) mask |= (1u << i);
  }

  pp::Profile p;
  p.stats = stats;
  p.achievementMask = mask;
  uint8_t blob[4096];
  const size_t n = pp::serializeProfile(p, blob, sizeof(blob));
  CHECK(n == pp::profileBlobSize());

  pp::Profile loaded;
  CHECK(pp::deserializeProfile(blob, n, &loaded));
  CHECK(loaded.achievementMask == mask);

  pp::AchievementState restored;
  pp::seedAchievementState(restored, loaded.stats, loaded.achievementMask);
  CHECK(restored.unlockedCount == unlockedBefore);
  // And nothing re-unlocks: the evaluator must be idempotent.
  const int again = pp::collectUnlocks(restored, loaded.stats, nullptr, nullptr, 0);
  CHECK(again == 0);
}

// ---------------------------------------------------------------------------
// Save file
// ---------------------------------------------------------------------------

TEST(profile_round_trips) {
  pp::Profile p;
  p.stats.totalPresses = 123456;
  p.stats.totalHits = 100000;
  p.stats.totalMisses = 23456;
  p.stats.totalFalseStarts = 99;
  p.stats.totalGames = 777;
  p.stats.totalTimePlayedNs = 98765432101ull;
  p.stats.bestReactionUs = 142000;
  p.stats.reactionUsSum = 5000000;
  p.stats.reactionSamples = 2500;
  p.stats.bestAccuracy = 0.9876f;
  p.stats.bestStreak = 61;
  p.stats.totalScore = 999999;
  for (int i = 0; i < pp::kModeCount; ++i) {
    pp::ModeStats& m = p.stats.modes[i];
    m.games = static_cast<uint64_t>(i + 1) * 11;
    m.bestScore = static_cast<uint64_t>(i) * 4321;
    m.bestReactionUs = 200000 + static_cast<uint32_t>(i) * 1000;
    m.bestLevel = static_cast<uint32_t>(i) * 3;
    m.bestSurvivalNs = static_cast<uint64_t>(i) * 1000000;
    m.lastPlayedUnixMs = 1700000000000ull + static_cast<uint64_t>(i);
  }
  p.achievementMask = 0x3FFFFFFFu & 0x15555555u;
  p.settings.sound = false;
  p.settings.music = true;
  p.settings.calibrationMs = 7;
  p.settings.hapticsStrength = 0.42f;

  uint8_t blob[4096];
  const size_t n = pp::serializeProfile(p, blob, sizeof(blob));
  CHECK(n > 0);

  pp::Profile q;
  CHECK(pp::deserializeProfile(blob, n, &q));
  CHECK(q.stats.totalPresses == p.stats.totalPresses);
  CHECK(q.stats.bestReactionUs == p.stats.bestReactionUs);
  CHECK(q.stats.bestStreak == p.stats.bestStreak);
  CHECK(q.stats.bestAccuracy == p.stats.bestAccuracy);
  CHECK(q.achievementMask == p.achievementMask);
  CHECK(q.settings.sound == false);
  CHECK(q.settings.music == true);
  CHECK(q.settings.calibrationMs == 7);
  CHECK_NEAR(q.settings.hapticsStrength, 0.42f, 1e-6);
  for (int i = 0; i < pp::kModeCount; ++i) {
    CHECK(q.stats.modes[i].games == p.stats.modes[i].games);
    CHECK(q.stats.modes[i].bestScore == p.stats.modes[i].bestScore);
    CHECK(q.stats.modes[i].bestLevel == p.stats.modes[i].bestLevel);
    CHECK(q.stats.modes[i].bestSurvivalNs == p.stats.modes[i].bestSurvivalNs);
  }
}

TEST(profile_rejects_corrupt_input) {
  pp::Profile p;
  p.stats.totalGames = 5;
  uint8_t blob[4096];
  const size_t n = pp::serializeProfile(p, blob, sizeof(blob));
  pp::Profile q;

  // Truncated.
  CHECK(!pp::deserializeProfile(blob, n / 2, &q));
  // Flipped byte.
  blob[100] = static_cast<uint8_t>(blob[100] ^ 0xFF);
  CHECK(!pp::deserializeProfile(blob, n, &q));
  blob[100] = static_cast<uint8_t>(blob[100] ^ 0xFF);
  CHECK(pp::deserializeProfile(blob, n, &q));
  // Wrong magic.
  blob[0] = 0;
  CHECK(!pp::deserializeProfile(blob, n, &q));
  // Wrong version.
  blob[0] = 0;
  blob[1] = 0;
  blob[2] = 0;
  blob[3] = 0;
  CHECK(!pp::deserializeProfile(blob, n, &q));
  // Out-of-range settings are clamped rather than trusted.
  pp::Profile w;
  w.settings.calibrationMs = 99999;
  w.settings.hapticsStrength = 12.0f;
  const size_t n2 = pp::serializeProfile(w, blob, sizeof(blob));
  CHECK(pp::deserializeProfile(blob, n2, &q));
  CHECK(q.settings.calibrationMs <= 60);
  CHECK(q.settings.hapticsStrength <= 1.0f);
}

TEST(profile_file_io_is_atomic_and_recovers) {
  const char* dir = "build-host/testprofile";
  std::string cmd = "mkdir -p ";
  cmd += dir;
  if (std::system(cmd.c_str()) != 0) {
    std::printf("  (skipped: could not create %s)\n", dir);
    return;
  }

  pp::Profile p;
  p.stats.totalGames = 11;
  CHECK(pp::saveProfileToFile(dir, p));
  pp::Profile q;
  CHECK(pp::loadProfileFromFile(dir, &q));
  CHECK(q.stats.totalGames == 11);

  // Overwrite: the previous good copy becomes the backup.
  p.stats.totalGames = 22;
  CHECK(pp::saveProfileToFile(dir, p));
  CHECK(pp::loadProfileFromFile(dir, &q));
  CHECK(q.stats.totalGames == 22);

  // Corrupt the primary file: loading must fall back to the backup.
  char path[512];
  std::snprintf(path, sizeof(path), "%s/profile.bin", dir);
  std::FILE* f = std::fopen(path, "wb");
  CHECK(f != nullptr);
  if (f != nullptr) {
    std::fwrite("garbage", 1, 7, f);
    std::fclose(f);
  }
  pp::Profile r;
  CHECK(pp::loadProfileFromFile(dir, &r));
  CHECK(r.stats.totalGames == 11);

  // No file at all: not an error, just a fresh profile.
  std::snprintf(path, sizeof(path), "%s/profile.bin", dir);
  std::remove(path);
  std::snprintf(path, sizeof(path), "%s/profile.bak", dir);
  std::remove(path);
  pp::Profile none;
  CHECK(!pp::loadProfileFromFile(dir, &none));
  CHECK(none.stats.totalGames == 0);
}

// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------

TEST(modes_are_deterministic_for_a_seed) {
  for (int m = 0; m < pp::kModeCount; ++m) {
    const Mode mode = static_cast<Mode>(m);
    pp::ModeContext ctx;
    ctx.area = {0, 0, 1080, 1920};
    ctx.density = 3.0f;
    ctx.scale = 1.0f;

    // A fixed start time, so the two runs see an identical timeline.
    const Nanos t0 = 1'700'000'000'000'000'000ll;
    pp::ModeRunner a, b;
    a.begin(mode, 0xC0FFEE, t0, ctx);
    b.begin(mode, 0xC0FFEE, t0, ctx);

    Nanos t = t0;
    for (int i = 0; i < 240; ++i) {
      t += 16 * pp::kNsPerMs;
      a.update(t, ctx);
      b.update(t, ctx);
    }
    // Two runs from the same seed and the same simulated timeline must agree
    // exactly, which is what makes a bug report replayable.
    CHECK(a.targetCount() == b.targetCount());
    CHECK(a.view().activeTargets == b.view().activeTargets);
  }
}

TEST(every_mode_produces_a_playable_round) {
  for (int m = 0; m < pp::kModeCount; ++m) {
    const Mode mode = static_cast<Mode>(m);
    pp::ModeContext ctx;
    ctx.area = {0, 0, 1080, 1920};
    ctx.density = 3.0f;
    ctx.scale = 1.0f;

    pp::ModeRunner runner;
    Nanos t = pp::monotonicNow();
    runner.begin(mode, 777, t, ctx);

    // An "oracle" that answers the first live target as fast as it can, the way
    // a perfect player would.  MAX is endless by design, so it is bounded by a
    // hit count instead of by the round ending.
    const int hitTarget = (mode == Mode::Max) ? 30 : 0;
    int answered = 0;
    for (int frame = 0; frame < 60 * 90; ++frame) {
      t += 16 * pp::kNsPerMs;
      runner.update(t, ctx);

      const pp::RoundView& v = runner.view();
      if (v.activeTargets > 0) {
        const pp::TapResult r = runner.onPress(v.targets[0].center, t + 3 * pp::kNsPerMs);
        if (r.consumed) ++answered;
      }
      if (mode == Mode::Max) {
        if (runner.session().summary().hits >= hitTarget) break;
      } else if (runner.finished()) {
        break;
      }
    }

    if (mode == Mode::Max) {
      // MAX only ends on a mistake, so the oracle's clean run is closed out
      // deliberately before the survival clock is read.
      t += 500 * pp::kNsPerMs;
      runner.update(t, ctx);
      runner.onPress({1.0f, 1.0f}, t);
      CHECK(runner.finished());
    }

    const RoundSummary& s = runner.session().summary();
    if (mode == Mode::Max) {
      CHECK(s.hits >= hitTarget);
      CHECK(runner.level() > 1);
      CHECK(s.level > 1);
      CHECK(s.survivalNs > 0);
      // A perfect oracle is never charged a penalty before the final mistake.
      CHECK(s.misses == 1);
      CHECK(s.falseStarts == 0);
      CHECK(s.score > 0);
    } else {
      if (!runner.finished()) reportFailure(__FILE__, __LINE__, "mode did not finish within 90 s");
      ++g_checks;
      CHECK(s.trials > 0);
      CHECK(answered > 0);
      CHECK(s.hits > 0);
      CHECK(s.bestUs > 0);
      CHECK(s.misses == 0);
      CHECK(s.score > 0);
      // A clean run must be recognised as one.
      CHECK(s.perfect);
    }
  }
}

TEST(targets_stay_inside_the_play_area) {
  for (int m = 0; m < pp::kModeCount; ++m) {
    pp::ModeContext ctx;
    ctx.area = {0, 0, 720, 1280};
    ctx.density = 2.0f;
    ctx.scale = 1.0f;
    pp::ModeRunner runner;
    Nanos t = pp::monotonicNow();
    runner.begin(static_cast<Mode>(m), 31337, t, ctx);
    for (int frame = 0; frame < 60 * 40; ++frame) {
      t += 16 * pp::kNsPerMs;
      runner.update(t, ctx);
      const pp::RoundView& v = runner.view();
      for (int i = 0; i < v.activeTargets; ++i) {
        const pp::TargetView& tgt = v.targets[i];
        CHECK(tgt.center.x - tgt.radius >= ctx.area.x - 0.5f);
        CHECK(tgt.center.y - tgt.radius >= ctx.area.y - 0.5f);
        CHECK(tgt.center.x + tgt.radius <= ctx.area.right() + 0.5f);
        CHECK(tgt.center.y + tgt.radius <= ctx.area.bottom() + 0.5f);
      }
      if (runner.finished()) break;
    }
  }
}

TEST(max_mode_ends_on_a_mistake_and_gets_harder) {
  pp::ModeContext ctx;
  ctx.area = {0, 0, 1080, 1920};
  ctx.density = 3.0f;
  ctx.scale = 1.0f;

  pp::ModeRunner runner;
  Nanos t = pp::monotonicNow();
  runner.begin(Mode::Max, 4242, t, ctx);

  float firstRadius = 0.0f;
  float lastRadius = 0.0f;
  int levelAtStart = 0;
  bool sawGrowth = false;
  for (int i = 0; i < 400 && !runner.finished(); ++i) {
    t += 20 * pp::kNsPerMs;
    runner.update(t, ctx);
    const pp::RoundView& v = runner.view();
    if (v.activeTargets > 0) {
      const float r = v.targets[0].radius;
      if (firstRadius == 0.0f) {
        firstRadius = r;
        levelAtStart = runner.level();
      }
      lastRadius = r;
      runner.onPress(v.targets[0].center, t + 4 * pp::kNsPerMs);
      if (runner.level() > levelAtStart) sawGrowth = true;
    }
  }
  CHECK(sawGrowth);
  CHECK(lastRadius < firstRadius);  // Targets shrink as the run goes on.
  CHECK(runner.level() > 1);

  // One mistake ends the run.
  t += 200 * pp::kNsPerMs;
  runner.update(t, ctx);
  CHECK(runner.view().activeTargets > 0);
  runner.onPress({5.0f, 5.0f}, t);  // Nowhere near the target.
  CHECK(runner.finished());
  CHECK(runner.session().summary().misses >= 1);
}

TEST(focus_mode_requires_the_correct_target) {
  pp::ModeContext ctx;
  ctx.area = {0, 0, 1080, 1920};
  ctx.density = 3.0f;
  ctx.scale = 1.0f;
  pp::ModeRunner runner;
  Nanos t = pp::monotonicNow();
  runner.begin(Mode::Focus, 5150, t, ctx);

  bool sawDistractor = false;
  for (int frame = 0; frame < 60 * 30 && !runner.finished(); ++frame) {
    t += 16 * pp::kNsPerMs;
    runner.update(t, ctx);
    const pp::RoundView& v = runner.view();
    if (v.activeTargets > 0) {
      int correctIndex = -1;
      for (int i = 0; i < v.activeTargets; ++i) {
        if (v.targets[i].isTarget) correctIndex = i;
        else sawDistractor = true;
      }
      CHECK(correctIndex >= 0);
      if (correctIndex >= 0) {
        runner.onPress(v.targets[correctIndex].center, t + 3 * pp::kNsPerMs);
      }
    }
  }
  CHECK(sawDistractor);
  CHECK(runner.session().summary().misses == 0);
  CHECK(runner.session().summary().correctPresses == runner.session().summary().hits);
}

TEST(focus_wrong_target_is_a_mistake) {
  pp::ModeContext ctx;
  ctx.area = {0, 0, 1080, 1920};
  ctx.density = 3.0f;
  ctx.scale = 1.0f;
  pp::ModeRunner runner;
  Nanos t = pp::monotonicNow();
  runner.begin(Mode::Focus, 606, t, ctx);
  for (int frame = 0; frame < 600; ++frame) {
    t += 16 * pp::kNsPerMs;
    runner.update(t, ctx);
    const pp::RoundView& v = runner.view();
    if (v.activeTargets > 0) {
      for (int i = 0; i < v.activeTargets; ++i) {
        if (!v.targets[i].isTarget) {
          runner.onPress(v.targets[i].center, t + 3 * pp::kNsPerMs);
          CHECK(runner.session().summary().wrongPresses == 1);
          CHECK(runner.session().summary().misses == 1);
          return;
        }
      }
    }
  }
  // Reaching here means no distractor ever appeared, which is itself a bug.
  CHECK(false);
}

TEST(reflex_delay_is_not_predictable) {
  pp::ModeContext ctx;
  ctx.area = {0, 0, 1080, 1920};
  ctx.density = 3.0f;
  ctx.scale = 1.0f;

  // Collect the gap between the round starting and the first stimulus, plus the
  // inter-stimulus gaps, across many rounds.  A fixed or trivially cycling
  // delay would show up as near-zero variance.
  pp::ModeRunner runner;
  Nanos t = pp::monotonicNow();
  runner.begin(Mode::Reflex, 0, t, ctx);
  std::vector<Nanos> gaps;
  Nanos lastStimulus = 0;
  Nanos previous = 0;
  for (int frame = 0; frame < 60 * 60 && !runner.finished(); ++frame) {
    t += 16 * pp::kNsPerMs;
    runner.update(t, ctx);
    if (runner.view().stimulusSerial != lastStimulus) {
      lastStimulus = runner.view().stimulusSerial;
      if (previous != 0) gaps.push_back(t - previous);
      previous = t;
    }
    if (runner.view().activeTargets > 0) {
      runner.onPress(runner.view().targets[0].center, t + 3 * pp::kNsPerMs);
    }
  }
  CHECK(gaps.size() >= 8);
  // Nine gaps between ten stimuli.
  CHECK(gaps.size() == 9);
  double minG = 1e18, maxG = -1e18, sumG = 0;
  for (Nanos g : gaps) {
    const double d = pp::msFromNs(g);
    minG = d < minG ? d : minG;
    maxG = d > maxG ? d : maxG;
    sumG += d;
  }
  const double mean = sumG / static_cast<double>(gaps.size());
  CHECK(maxG - minG > mean * 0.5);
  CHECK(mean > 200.0);
}

TEST(touch_slop_makes_targets_forgiving) {
  // A press a little outside the drawn radius must still count, so the game
  // measures reaction and not dexterity.
  pp::ModeContext ctx;
  ctx.area = {0, 0, 1080, 1920};
  ctx.density = 3.0f;
  ctx.scale = 1.0f;
  pp::ModeRunner runner;
  Nanos t = pp::monotonicNow();
  runner.begin(Mode::Flick, 8, t, ctx);
  for (int frame = 0; frame < 600; ++frame) {
    t += 16 * pp::kNsPerMs;
    runner.update(t, ctx);
    const pp::RoundView& v = runner.view();
    if (v.activeTargets > 0) {
      const pp::TargetView& tgt = v.targets[0];
      const float slop = pp::cfg::kTouchSlopDp * ctx.density;
      // Land just outside the visual radius but inside the slop.
      const float off = tgt.radius + slop * 0.8f;
      const pp::TapResult r = runner.onPress({tgt.center.x + off, tgt.center.y}, t + 2 * pp::kNsPerMs);
      CHECK(r.consumed);
      CHECK(r.kind == pp::PressKind::Hit);

      // Beyond the slop the same press must miss, so the tolerance is bounded.
      const float far = tgt.radius + slop * 2.5f;
      const pp::TapResult miss = runner.onPress({tgt.center.x + far, tgt.center.y}, t + 4 * pp::kNsPerMs);
      CHECK(miss.consumed);
      CHECK(miss.kind == pp::PressKind::Spam);
      return;
    }
  }
  CHECK(false);
}

TEST(press_outside_any_target_is_spam) {
  pp::ModeContext ctx;
  ctx.area = {0, 0, 1080, 1920};
  ctx.density = 3.0f;
  ctx.scale = 1.0f;
  pp::ModeRunner runner;
  Nanos t = pp::monotonicNow();
  runner.begin(Mode::Reflex, 11, t, ctx);

  // Before any stimulus: a false start.
  pp::TapResult r = runner.onPress({540, 960}, t);
  CHECK(r.consumed);
  CHECK(r.kind == pp::PressKind::FalseStart);
  CHECK(runner.session().summary().falseStarts == 1);

  // REFLEX is go/no-go: every unanswered press is a false start, and each one
  // costs the full penalty so mashing the delay can never be profitable.
  for (int i = 0; i < 30; ++i) {
    t += 30 * pp::kNsPerMs;
    runner.update(t, ctx);
    if (runner.view().activeTargets > 0) break;
    r = runner.onPress({100.0f + static_cast<float>(i), 200.0f + static_cast<float>(i)}, t);
    CHECK(r.kind == pp::PressKind::FalseStart);
  }
  CHECK(runner.session().summary().misses >= 10);
  CHECK(runner.session().scorer().penalties() >= 150 * 10);
}

// ---------------------------------------------------------------------------
// Embedded font atlas
// ---------------------------------------------------------------------------

TEST(font_atlas_decodes_and_is_self_consistent) {
  CHECK(pp::decodeFontAtlas());
  const uint8_t* px = pp::fontAtlasPixels();
  CHECK(px != nullptr);
  const int w = pp::fontAtlasWidth();
  const int h = pp::fontAtlasHeight();
  CHECK(w > 0 && h > 0);

  // Decoding is idempotent: the second call is a no-op, not a re-allocation.
  CHECK(pp::decodeFontAtlas());
  CHECK(pp::fontAtlasPixels() == px);

  const pp::FontAtlas* faces[2] = {&pp::kFontDisplay, &pp::kFontTech};
  for (int f = 0; f < 2; ++f) {
    const pp::FontAtlas& face = *faces[f];
    CHECK(face.glyphCount > 90);
    CHECK(face.emPx > 0);
    CHECK(face.ascent > 0.5f && face.ascent < 1.4f);
    CHECK(face.descent > 0.0f && face.descent < 0.6f);
    CHECK(face.lineHeight > face.ascent);

    // Every glyph's uv rectangle must match its plane box exactly, otherwise
    // text would be drawn stretched or offset by a pixel per glyph.
    for (int i = 0; i < face.glyphCount; ++i) {
      const pp::Glyph& g = face.glyphs[i];
      const float cellW = (g.u1 - g.u0) * static_cast<float>(w);
      const float cellH = (g.v1 - g.v0) * static_cast<float>(h);
      const float planeW = (g.planeRight - g.planeLeft) * static_cast<float>(face.emPx);
      const float planeH = (g.planeTop - g.planeBottom) * static_cast<float>(face.emPx);
      const bool blank = cellW <= 0.0f && cellH <= 0.0f;
      if (blank) {
        CHECK(g.advance > 0.0f);  // whitespace still has to take up room
        continue;
      }
      CHECK_NEAR(cellW, planeW, 0.02);
      CHECK_NEAR(cellH, planeH, 0.02);
      CHECK(g.advance > 0.0f);
      CHECK(g.planeBottom <= g.planeTop);
      // The contour band must actually be present in the cell, otherwise the
      // shader would render a solid block.
      bool sawContour = false;
      for (int y = 0; y < static_cast<int>(cellH) && !sawContour; ++y) {
        const int py = static_cast<int>(g.v0 * h) + y;
        if (py < 0 || py >= h) continue;
        for (int x = 0; x < static_cast<int>(cellW); ++x) {
          const int pxl = static_cast<int>(g.u0 * w) + x;
          if (pxl < 0 || pxl >= w) continue;
          const uint8_t v = px[py * w + pxl];
          if (v > 90 && v < 165) {  // the 0.5 iso-level, +/- one quantisation step
            sawContour = true;
            break;
          }
        }
      }
      CHECK(sawContour);
    }
  }

  // The two faces must agree on the codepoints they cover, so the UI can mix
  // them without a glyph silently disappearing.
  for (int i = 0; i < pp::kFontDisplay.glyphCount; ++i) {
    const int cp = pp::kFontDisplay.glyphs[i].codepoint;
    bool found = false;
    for (int j = 0; j < pp::kFontTech.glyphCount; ++j) {
      if (pp::kFontTech.glyphs[j].codepoint == cp) found = true;
    }
    CHECK(found);
  }

  // Every character the UI actually uses must exist in both faces.
  static const char* kUiStrings[] = {
      "PULSEPOINT", "TEST YOUR REFLEX.", "PLAY", "ACHIEVEMENTS", "STATISTICS", "SETTINGS",
      "REFLEX", "FLICK", "FOCUS", "MAX", "READY", "RESULT", "PLAY AGAIN", "CHANGE MODE",
      "MAIN MENU", "NEW PERSONAL BEST", "SCORE", "BEST", "AVERAGE", "ACCURACY", "STREAK",
      "SUCCESSFUL PRESSES", "TOTAL PRESSES", "TOTAL HITS", "TOTAL MISSES", "FALSE STARTS",
      "TOTAL GAMES", "TOTAL TIME PLAYED", "BEST REACTION", "BEST ACCURACY", "BEST STREAK",
      "ACHIEVEMENT UNLOCKED", "SOUND", "MUSIC", "HAPTICS", "ON", "OFF", "AUTO", "MS",
      "PRESSES", "GAMES", "LEVEL", "SCORE", "PERSONAL BEST", "GAME OVER", "TAP TO START",
      "187 MS", "0.0%", "100%", "00:00", "1,234", "0123456789", ".,:/%+-()x",
  };
  for (int s = 0; s < static_cast<int>(sizeof(kUiStrings) / sizeof(kUiStrings[0])); ++s) {
    for (const char* p = kUiStrings[s]; *p != '\0'; ++p) {
      int cp = static_cast<unsigned char>(*p);
      bool inDisplay = false, inTech = false;
      for (int i = 0; i < pp::kFontDisplay.glyphCount; ++i) {
        if (pp::kFontDisplay.glyphs[i].codepoint == cp) inDisplay = true;
      }
      for (int i = 0; i < pp::kFontTech.glyphCount; ++i) {
        if (pp::kFontTech.glyphs[i].codepoint == cp) inTech = true;
      }
      if (!inDisplay || !inTech) {
        std::printf("    missing glyph '%c' (0x%02x) display=%d tech=%d\n", *p, cp, inDisplay,
                    inTech);
      }
      CHECK(inDisplay);
      CHECK(inTech);
    }
  }
}

TEST(number_formatting_survives_a_screenshot) {
  using namespace pp::ui_helpers;
  char buf[40];

  // Grouping is measured from the right, three digits at a time.  The bug this
  // guards against put the separator one digit early, so 274 came out "27,4"
  // and 1234567 came out "12,345,67" -- both of which look like a font problem
  // in a render and nothing like an arithmetic problem in a diff.
  struct {
    uint64_t v;
    const char* want;
  } counts[] = {
      {0, "0"},       {7, "7"},          {39, "39"},        {100, "100"},
      {274, "274"},   {999, "999"},      {1000, "1,000"},   {27412, "27,412"},
      {99999, "99,999"}, {100000, "100,000"}, {1234567, "1,234,567"},
      {1000000, "1,000,000"}, {4294967295ull, "4,294,967,295"},
  };
  for (const auto& c : counts) {
    formatCount(buf, sizeof(buf), c.v);
    CHECK(std::strcmp(buf, c.want) == 0);
    if (std::strcmp(buf, c.want) != 0) {
      std::printf("    %llu -> \"%s\", wanted \"%s\"\n",
                  static_cast<unsigned long long>(c.v), buf, c.want);
    }
  }

  // A count must never overrun a short buffer, and must stay terminated.
  char small[5];
  formatCount(small, sizeof(small), 1234567ull);
  CHECK(std::strlen(small) < sizeof(small));

  formatMs(buf, sizeof(buf), 0);
  CHECK(std::strcmp(buf, "") == 0);
  formatMs(buf, sizeof(buf), 999);
  CHECK(std::strcmp(buf, "0") == 0);
  formatMs(buf, sizeof(buf), 135000);
  CHECK(std::strcmp(buf, "135") == 0);

  // A missing measurement is a dash everywhere, never a confident zero.
  formatMsOrDash(buf, sizeof(buf), 0);
  CHECK(std::strcmp(buf, "--") == 0);
  formatMsOrDash(buf, sizeof(buf), 200500);
  CHECK(std::strcmp(buf, "200") == 0);

  formatPercent(buf, sizeof(buf), 0.0f);
  CHECK(std::strcmp(buf, "--") == 0);
  formatPercent(buf, sizeof(buf), 0.231f);
  CHECK(std::strcmp(buf, "23.1%") == 0);
  formatPercent(buf, sizeof(buf), 1.0f);
  CHECK(std::strcmp(buf, "100.0%") == 0);

  formatDuration(buf, sizeof(buf), 0);
  CHECK(std::strcmp(buf, "--") == 0);
  formatDuration(buf, sizeof(buf), 20ull * 1000000000ull);
  CHECK(std::strcmp(buf, "20S") == 0);
  formatDuration(buf, sizeof(buf), 64ull * 1000000000ull);
  CHECK(std::strcmp(buf, "1M 4S") == 0);
  formatDuration(buf, sizeof(buf), 3600ull * 1000000000ull);
  CHECK(std::strcmp(buf, "1H 0M") == 0);
  formatDuration(buf, sizeof(buf), 11520ull * 1000000000ull);
  CHECK(std::strcmp(buf, "3H 12M") == 0);
}

TEST(synth_output_stays_clean_under_worst_case_load) {
  using namespace pp::audio;
  Synth s;
  s.init(44100);
  s.setSoundEnabled(true);
  s.setMusicEnabled(true);

  // Worst case first: every voice at once, several times over, at the extremes
  // of intensity and of the gameplay-pitched parameter.  Voice stealing has to
  // hold, and the soft clipper has to round off instead of crack.
  for (int round = 0; round < 3; ++round) {
    for (int id = 0; id < static_cast<int>(Sfx::Count); ++id) {
      s.trigger(static_cast<Sfx>(id), 1.0f, 90.0f);
      s.trigger(static_cast<Sfx>(id), 2.0f, 900.0f);
      s.trigger(static_cast<Sfx>(id), 0.0f, 0.0f);
    }
  }
  const int frames = 44100 * 3;
  std::vector<int16_t> buf(static_cast<size_t>(frames) * 2u);
  CHECK(s.render(buf.data(), frames) == frames);

  int peak = 0;
  long long sumL = 0, sumR = 0;
  double energy = 0.0;
  for (int f = 0; f < frames; ++f) {
    const int l = buf[static_cast<size_t>(f) * 2u];
    const int r = buf[static_cast<size_t>(f) * 2u + 1u];
    const int a = l < 0 ? -l : l, b = r < 0 ? -r : r;
    if (a > peak) peak = a;
    if (b > peak) peak = b;
    sumL += l;
    sumR += r;
    energy += static_cast<double>(l) * l + static_cast<double>(r) * r;
  }
  const double n = static_cast<double>(frames);
  const double dcL = static_cast<double>(sumL) / n;
  const double dcR = static_cast<double>(sumR) / n;
  const double rms = std::sqrt(energy / (2.0 * n));
  if (peak > 32000) std::printf("    peak %d\n", peak);
  if (rms < 10.0) std::printf("    rms %.1f (silent?)\n", rms);
  CHECK(peak <= 32000);
  CHECK(peak > 1000);  // The pile-up must actually be audible.
  CHECK(rms > 10.0);
  // DC offset under 2% of full scale: a slowly wandering baseline would eat
  // headroom and thump on Bluetooth reconnects.
  CHECK(dcL > -650.0 && dcL < 650.0);
  CHECK(dcR > -650.0 && dcR < 650.0);

  // Everything off is silence, not a stuck voice or a ringing filter.
  s.setSoundEnabled(false);
  s.setMusicEnabled(false);
  const int tail = 44100;
  std::vector<int16_t> quiet(static_cast<size_t>(tail) * 2u, 0);
  CHECK(s.render(quiet.data(), tail) == tail);
  int tailPeak = 0;
  for (int f = tail / 2; f < tail; ++f) {
    const int l = quiet[static_cast<size_t>(f) * 2u];
    const int r = quiet[static_cast<size_t>(f) * 2u + 1u];
    const int a = l < 0 ? -l : l, b = r < 0 ? -r : r;
    if (a > tailPeak) tailPeak = a;
    if (b > tailPeak) tailPeak = b;
  }
  if (tailPeak > 200) std::printf("    tail peak %d\n", tailPeak);
  CHECK(tailPeak <= 200);

  // Degenerate calls never crash and never write.
  CHECK(s.render(nullptr, 100) == 0);
  CHECK(s.render(quiet.data(), 0) == 0);
  CHECK(s.render(quiet.data(), -4) == 0);
  s.shutdown();
}

TEST(targets_never_spawn_under_the_hud) {
  for (int m = 0; m < pp::kModeCount; ++m) {
    pp::ModeContext ctx;
    ctx.area = {0, 0, 1080, 2340};
    // A HUD strip at the top and a progress bar at the bottom, as the app
    // computes them.  This regressed once: a Focus target spawned overlapping
    // the QUIT pill, where the abort tap wins over the answer.
    ctx.spawn = {0, 300, 1080, 1800};
    ctx.density = 3.0f;
    ctx.scale = 1.0f;
    pp::ModeRunner runner;
    Nanos t = pp::monotonicNow();
    runner.begin(static_cast<Mode>(m), 777, t, ctx);
    int seen = 0;
    for (int frame = 0; frame < 60 * 30; ++frame) {
      t += 16 * pp::kNsPerMs;
      runner.update(t, ctx);
      const pp::RoundView& v = runner.view();
      for (int i = 0; i < v.activeTargets; ++i) {
        const pp::TargetView& tgt = v.targets[i];
        ++seen;
        CHECK(tgt.center.x - tgt.radius >= ctx.spawn.x - 0.5f);
        CHECK(tgt.center.y - tgt.radius >= ctx.spawn.y - 0.5f);
        CHECK(tgt.center.x + tgt.radius <= ctx.spawn.right() + 0.5f);
        CHECK(tgt.center.y + tgt.radius <= ctx.spawn.bottom() + 0.5f);
      }
      if (runner.finished()) break;
    }
    // Every mode must present a target inside 30 simulated seconds, or the
    // checks above are vacuously true.
    CHECK(seen > 0);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int ran = 0;
  std::printf("PulsePoint headless tests\n\n");
  for (const TestCase& t : registry()) {
    if (!shouldRun(filter, t.name)) continue;
    beginTest(t.name);
    const int before = g_failures;
    t.fn();
    ++ran;
    if (g_failures == before) std::printf("  ok\n");
  }
  std::printf("\n%d test(s), %d check(s), %d failure(s)\n", ran, g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
