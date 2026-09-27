// The four modes.
//
// Rules of the house:
//   * No target ever appears outside the spawn rect (the play area minus the
//     HUD strips), and no target ever overlaps another live one.  Both are
//     asserted by the headless tests.
//   * A tap is answered against the *active* target only.  Anything else is a
//     miss, a wrong target or a stray press, never a hit.
//   * Reaction time is measured from the moment the stimulus was made
//     answerable, corrected for display latency, using the tap's own
//     timestamp.  Nothing here reads a frame counter.
#include "modes.h"

#include <algorithm>
#include <cmath>

#include "../core/config.h"
#include "../meta/stats.h"

namespace pp {

namespace {

// How many trials a mode has completed, used for the shared difficulty ramp.
int trialsDone(const Session& s) { return s.summary().hits + s.summary().misses; }

// Targets spawn inside ctx.spawn, which excludes the HUD strips: a target
// under the QUIT pill cannot be answered, because the abort tap is resolved
// before the mode ever sees it.  Contexts that leave spawn empty (bare unit
// tests) get the whole field.
const Rect& spawnRect(const ModeContext& ctx) {
  return (ctx.spawn.w > 0.0f && ctx.spawn.h > 0.0f) ? ctx.spawn : ctx.area;
}

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

// ---------------------------------------------------------------------------
// ModeRunner lifecycle
// ---------------------------------------------------------------------------

void ModeRunner::begin(Mode mode, uint64_t seed, Nanos nowNs, const ModeContext& ctx) {
  mode_ = mode;
  lastCtx_ = ctx;
  rng_.seed(seed);
  view_ = RoundView{};
  finished_ = false;
  finishedReported_ = false;
  hasLastReaction_ = false;
  lastReactionUs_ = 0;
  level_ = 1;
  startedNs_ = nowNs;
  activeStimulusNs_ = 0;
  trialIndex_ = 0;
  targetSerial_ = 0;
  nextSpawnIn_ = 0;
  nextSpawnTimer_ = 0.0f;
  movementSeed_ = rng_.range(0.0f, 6.2831853f);
  separationScale_ = 1.0f;
  movementDir_ = {1.0f, 0.0f};
  session_.begin(mode, seed, nowNs);

  switch (mode) {
    case Mode::Reflex: beginReflex(nowNs, ctx); break;
    case Mode::Flick: beginFlick(nowNs, ctx); break;
    case Mode::Focus: beginFocus(nowNs, ctx); break;
    case Mode::Max: beginMax(nowNs, ctx); break;
  }
}

void ModeRunner::end(Nanos nowNs) { session_.endRound(nowNs); }

uint32_t ModeRunner::reactionFrom(Nanos stimulusNs, Nanos tapNs) const {
  if (stimulusNs <= 0) return 0;
  // Display-latency compensation: the stimulus was stamped when the frame was
  // submitted, not when the photons arrived.
  Nanos delta = tapNs - stimulusNs;
  if (calibrationMs_ > 0.0f) delta -= nsFromMs(calibrationMs_);
  if (delta < 0) delta = 0;
  return static_cast<uint32_t>(delta / kNsPerUs);
}

float ModeRunner::levelFactor() const {
  if (mode_ != Mode::Max) return 1.0f;
  const float step = 1.0f + static_cast<float>(level_ - 1) * cfg::kMaxLevelMultiplierStep;
  return step > cfg::kMaxLevelMultiplierCap ? cfg::kMaxLevelMultiplierCap : step;
}

float ModeRunner::difficulty() const {
  switch (mode_) {
    case Mode::Reflex:
      return cfg::difficultyProgress(trialsDone(session_), cfg::kReflexTrials);
    case Mode::Flick:
      return cfg::difficultyProgress(trialsDone(session_), cfg::kFlickTargetsPerRound);
    case Mode::Focus:
      return cfg::difficultyProgress(trialsDone(session_), cfg::kFocusTargetsPerRound);
    case Mode::Max: {
      // Endless: level-based, with diminishing returns so a 200-level run is
      // not literally impossible.
      const float t = 1.0f - 1.0f / (1.0f + static_cast<float>(level_) * 0.16f);
      return clamp01(t);
    }
  }
  return 0.0f;
}

bool ModeRunner::hitTest(Vec2 pos, const TargetView& t, const ModeContext& ctx) const {
  const float slop = cfg::kTouchSlopDp * ctx.density;
  return dist(pos, t.center) <= t.radius + slop;
}

Vec2 ModeRunner::placeTarget(float radius, const ModeContext& ctx, bool avoidPrevious) {
  const Rect& a = spawnRect(ctx);
  const float lo = a.x + radius;
  const float hi = a.right() - radius;
  const float loY = a.y + radius;
  const float hiY = a.bottom() - radius;
  if (hi <= lo || hiY <= loY) return a.center();

  // Rejection sampling against the live targets.  With at most a dozen targets
  // in a phone-sized area this converges in one or two passes; the bounded loop
  // keeps it deterministic if a very small area ever fails to fit.
  const float minSep = cfg::kFlickMinSeparationDp * ctx.density * 0.5f * separationScale_;
  for (int attempt = 0; attempt < 12; ++attempt) {
    const Vec2 p{rng_.range(lo, hi), rng_.range(loY, hiY)};
    bool clash = false;
    for (int i = 0; i < view_.activeTargets; ++i) {
      if (dist(p, view_.targets[i].center) < minSep + view_.targets[i].radius) {
        clash = true;
        break;
      }
    }
    if (avoidPrevious && lastSpawnPos_.x != 0.0f) {
      if (dist(p, lastSpawnPos_) < minSep) clash = true;
    }
    if (!clash) {
      lastSpawnPos_ = p;
      return p;
    }
  }
  const Vec2 fallback{rng_.range(lo, hi), rng_.range(loY, hiY)};
  lastSpawnPos_ = fallback;
  return fallback;
}

// ---------------------------------------------------------------------------
// REFLEX
// ---------------------------------------------------------------------------

void ModeRunner::beginReflex(Nanos nowNs, const ModeContext& ctx) {
  responseWindowNs_ = nsFromMs(cfg::kReflexResponseWindowMs);
  nextSpawnNs_ = nowNs + nsFromMs(cfg::kReflexDelayStartMs);
  (void)ctx;
}

void ModeRunner::spawnReflexTarget(Nanos nowNs, const ModeContext& ctx) {
  view_.clear();
  const float d = difficulty();
  const float delayBase = cfg::mix(cfg::kReflexDelayStartMs, cfg::kReflexDelayEndMs, d);
  const float jitter = rng_.range(0.0f, cfg::kReflexDelaySpanMs);
  nextSpawnNs_ = nowNs + nsFromMs(delayBase + jitter);

  TargetView& t = view_.targets[0];
  t.center = ctx.area.center();
  t.radius = cfg::kReflexTargetRadiusDp * ctx.density;
  t.isTarget = true;
  t.mark = MarkKind::Bullseye;
  t.spawnProgress = 0.0f;
  t.id = ++targetSerial_;
  view_.activeTargets = 1;
  view_.showWindowRing = true;
  view_.windowProgress = 0.0f;
  ++view_.stimulusSerial;
  activeStimulusNs_ = nowNs;
  session_.stimulusAppeared(nowNs);
  view_.addFx(FxKind::SpawnBlip, t.center, 0.6f);
}

void ModeRunner::updateReflex(Nanos nowNs, const ModeContext& ctx) {
  view_.fxCount = 0;
  if (view_.activeTargets == 0) {
    if (session_.summary().trials >= cfg::kReflexTrials) {
      if (!finished_) {
        finished_ = true;
        session_.endRound(nowNs);
      }
      return;
    }
    if (nowNs >= nextSpawnNs_) {
      spawnReflexTarget(nowNs, ctx);
    }
    return;
  }

  TargetView& t = view_.targets[0];
  t.spawnProgress = clamp01(t.spawnProgress + 0.28f);
  const Nanos age = nowNs - activeStimulusNs_;
  view_.windowProgress = clamp01(static_cast<float>(age) / static_cast<float>(responseWindowNs_));
  if (age >= responseWindowNs_) {
    session_.stimulusExpired();
    view_.addFx(FxKind::MissBurst, t.center, 1.0f);
    ++trialIndex_;
    view_.clearTargets();
  }
}

TapResult ModeRunner::pressReflex(Vec2 pos, Nanos tapNs, const ModeContext& ctx) {
  TapResult out;
  out.pos = pos;
  if (!session_.roundActive()) return out;

  if (view_.activeTargets == 0) {
    // REFLEX is a go/no-go task: every press that is not an answer to a target
    // is a false start, and it costs the full false-start penalty.  Mashing the
    // delay is therefore strictly worse than waiting.
    out.consumed = true;
    out.kind = PressKind::FalseStart;
    session_.registerPress(PressKind::FalseStart, 0, levelFactor());
    view_.addFx(FxKind::FalseStart, pos, 0.7f);
    return out;
  }

  const TargetView& t = view_.targets[0];
  const bool inside = hitTest(pos, t, ctx);
  if (!inside) {
    out.consumed = true;
    out.kind = PressKind::Spam;
    session_.registerPress(PressKind::Spam, 0, levelFactor());
    view_.addFx(FxKind::MissBurst, pos, 0.6f);
    return out;
  }

  const Nanos age = tapNs - activeStimulusNs_;
  if (age >= responseWindowNs_) {
    // Too slow: the window closed between the last frame and this tap.
    out.consumed = true;
    out.kind = PressKind::Spam;
    session_.stimulusExpired();
    session_.registerPress(PressKind::Spam, 0, levelFactor());
    view_.addFx(FxKind::MissBurst, t.center, 0.9f);
    view_.clear();
    return out;
  }

  out.consumed = true;
  out.kind = PressKind::Hit;
  out.reactionUs = reactionFrom(activeStimulusNs_, tapNs);
  hasLastReaction_ = true;
  lastReactionUs_ = out.reactionUs;
  session_.registerPress(PressKind::Hit, out.reactionUs, levelFactor());
  view_.addFx(FxKind::HitBurst, t.center, 1.0f);
  view_.addFx(FxKind::ScreenPulse, t.center, 0.5f);
  if (session_.summary().streak > 0 && session_.summary().streak % 5 == 0) {
    view_.addFx(FxKind::ComboPop, t.center, 1.0f);
  }
  ++trialIndex_;
  activeStimulusNs_ = 0;
  view_.clearTargets();
  return out;
}

// ---------------------------------------------------------------------------
// FLICK
// ---------------------------------------------------------------------------

void ModeRunner::beginFlick(Nanos nowNs, const ModeContext& ctx) {
  responseWindowNs_ = nsFromMs(cfg::kFlickResponseWindowMs);
  nextSpawnNs_ = nowNs + nsFromMs(cfg::kFlickSpawnGapStartMs);
  (void)ctx;
}

void ModeRunner::spawnFlickBatch(Nanos nowNs, const ModeContext& ctx) {
  const float d = difficulty();
  const float radius = cfg::mix(cfg::kFlickRadiusStartDp, cfg::kFlickRadiusEndDp, d) * ctx.density;
  const int maxConcurrent =
      cfg::kFlickMaxConcurrentStart +
      static_cast<int>(cfg::mix(0.0f, static_cast<float>(cfg::kFlickMaxConcurrentEnd -
                                                          cfg::kFlickMaxConcurrentStart),
                                d));
  const int want = rng_.rangeInt(1, maxConcurrent);

  // Spawn separation is a difficulty axis in its own right, alongside size and
  // timing: later targets are pushed further from the last one, so the player has
  // to travel further as well as react faster.
  separationScale_ = 0.55f + 0.75f * d;

  int added = 0;
  for (int i = 0; i < want; ++i) {
    if (view_.activeTargets >= RoundView::kMaxTargets) break;
    TargetView& t = view_.targets[view_.activeTargets];
    t = TargetView{};
    t.center = placeTarget(radius, ctx, true);
    t.radius = radius;
    t.isTarget = true;
    t.mark = MarkKind::Bullseye;
    t.spawnProgress = 0.0f;
    t.id = ++targetSerial_;
    ++view_.activeTargets;
    ++added;
    // One trial per target so a clean round can reach 100% accuracy, while the
    // reaction clock below stays anchored to the moment the batch appeared.
    session_.stimulusAppeared(nowNs);
  }
  if (added == 0) return;

  const float gap = cfg::mix(cfg::kFlickSpawnGapStartMs, cfg::kFlickSpawnGapEndMs, d);
  nextSpawnNs_ = nowNs + nsFromMs(gap * rng_.range(0.75f, 1.25f));
  // A batch of three is three trials but one reaction clock: you are reacting
  // to the moment the group appeared, not to each target individually.
  if (activeStimulusNs_ == 0) activeStimulusNs_ = nowNs;
  ++view_.stimulusSerial;
  for (int i = 0; i < added; ++i) view_.addFx(FxKind::SpawnBlip, view_.targets[i].center, 0.5f);
}

void ModeRunner::updateFlick(Nanos nowNs, const ModeContext& ctx) {
  view_.fxCount = 0;
  const int answered = session_.summary().hits;
  if (answered >= cfg::kFlickTargetsPerRound) {
    if (!finished_) {
      finished_ = true;
      session_.endRound(nowNs);
    }
    view_.clear();
    return;
  }

  for (int i = 0; i < view_.activeTargets; ++i) {
    view_.targets[i].spawnProgress = clamp01(view_.targets[i].spawnProgress + 0.3f);
    view_.targets[i].hitFlash = std::max(0.0f, view_.targets[i].hitFlash - 0.08f);
  }

  if (view_.activeTargets > 0) {
    // A batch that is never touched times out as a whole; the per-target window
    // is enforced on press, and the batch is swept here so the field cannot
    // silently fill up.
    if (nowNs - activeStimulusNs_ >= responseWindowNs_) {
      const int n = view_.activeTargets;
      for (int i = 0; i < n; ++i) view_.addFx(FxKind::MissBurst, view_.targets[i].center, 0.8f);
      session_.stimulusExpired();
      view_.clearTargets();
      activeStimulusNs_ = 0;
      nextSpawnNs_ = nowNs + nsFromMs(200.0f);
      return;
    }
    view_.showWindowRing = true;
    view_.windowProgress =
        clamp01(static_cast<float>(nowNs - activeStimulusNs_) / static_cast<float>(responseWindowNs_));
  } else if (nowNs >= nextSpawnNs_) {
    spawnFlickBatch(nowNs, ctx);
  }
}

TapResult ModeRunner::pressFlick(Vec2 pos, Nanos tapNs, const ModeContext& ctx) {
  TapResult out;
  out.pos = pos;
  if (!session_.roundActive()) return out;

  if (view_.activeTargets == 0) {
    out.consumed = true;
    out.kind = session_.summary().trials == 0 ? PressKind::FalseStart : PressKind::Spam;
    session_.registerPress(out.kind, 0, levelFactor());
    view_.addFx(out.kind == PressKind::FalseStart ? FxKind::FalseStart : FxKind::MissBurst, pos, 0.7f);
    return out;
  }

  for (int i = 0; i < view_.activeTargets; ++i) {
    if (!hitTest(pos, view_.targets[i], ctx)) continue;
    TargetView& t = view_.targets[i];
    const Nanos age = tapNs - activeStimulusNs_;
    if (age >= responseWindowNs_) {
      out.consumed = true;
      out.kind = PressKind::Spam;
      session_.stimulusExpired();
      session_.registerPress(PressKind::Spam, 0, levelFactor());
      view_.addFx(FxKind::MissBurst, t.center, 0.9f);
      view_.clearTargets();
      return out;
    }

    out.consumed = true;
    out.kind = PressKind::Hit;
    out.reactionUs = reactionFrom(activeStimulusNs_, tapNs);
    hasLastReaction_ = true;
    lastReactionUs_ = out.reactionUs;

    // Retire just this target; the rest of the batch stays live.
    for (int j = i; j < view_.activeTargets - 1; ++j) view_.targets[j] = view_.targets[j + 1];
    --view_.activeTargets;
    session_.registerPress(PressKind::Hit, out.reactionUs, levelFactor());
    if (view_.activeTargets == 0) {
      view_.clearTargets();
      activeStimulusNs_ = 0;
    }
    view_.addFx(FxKind::HitBurst, pos, 1.0f);
    if (session_.summary().streak > 0 && session_.summary().streak % 5 == 0) {
      view_.addFx(FxKind::ComboPop, pos, 1.0f);
    }
    return out;
  }

  out.consumed = true;
  out.kind = PressKind::Spam;
  session_.registerPress(PressKind::Spam, 0, levelFactor());
  view_.addFx(FxKind::MissBurst, pos, 0.6f);
  return out;
}

// ---------------------------------------------------------------------------
// FOCUS
// ---------------------------------------------------------------------------

void ModeRunner::beginFocus(Nanos nowNs, const ModeContext& ctx) {
  nextSpawnNs_ = nowNs + nsFromMs(700.0f);
  (void)ctx;
}

void ModeRunner::spawnFocusGroup(Nanos nowNs, const ModeContext& ctx) {
  view_.clear();
  const float d = difficulty();
  const float radius = cfg::mix(cfg::kFocusRadiusStartDp, cfg::kFocusRadiusEndDp, d) * ctx.density;
  const int distractors =
      cfg::kFocusDistractorsStart +
      static_cast<int>(cfg::mix(0.0f,
                                static_cast<float>(cfg::kFocusDistractorsEnd - cfg::kFocusDistractorsStart),
                                d) + 0.5f);
  const int total = 1 + distractors;
  if (total > RoundView::kMaxTargets) return;

  // Decide which slot holds the real target so the correct one is not always
  // in the same place.
  const int correctSlot = rng_.rangeInt(0, total - 1);

  // Below the similarity ramp the decoys are hollow rings, which makes the
  // search easy; above it every target is a filled disc and only the bullseye
  // gives the answer away.
  const bool lookAlike = d >= cfg::kFocusSimilarityRamp;

  for (int i = 0; i < total; ++i) {
    TargetView& t = view_.targets[i];
    t = TargetView{};
    // A small size jitter makes the decoys harder to dismiss by outline alone.
    // The jitter is applied before placement so a jittered target can never
    // poke outside the play area.
    t.radius = radius * rng_.range(0.92f, 1.08f);
    t.center = placeTarget(t.radius, ctx, true);
    t.isTarget = (i == correctSlot);
    t.spawnProgress = 0.0f;
    t.id = ++targetSerial_;
    if (t.isTarget) {
      t.mark = MarkKind::Bullseye;
      t.alpha = 1.0f;
    } else if (lookAlike) {
      static const MarkKind kDecoys[] = {MarkKind::Plus,  MarkKind::Cross, MarkKind::Slash,
                                         MarkKind::Square, MarkKind::Triangle, MarkKind::Arc};
      t.mark = kDecoys[rng_.rangeInt(0, 5)];
      t.alpha = 1.0f;
    } else {
      t.mark = MarkKind::None;
      t.alpha = 0.85f;
    }
    ++view_.activeTargets;
  }
  // Keep the real target first in the array so callers do not have to scan, but
  // the view keeps its own index so the renderer can lay them out in the order
  // they were created.
  for (int i = 0; i < view_.activeTargets; ++i) {
    if (view_.targets[i].isTarget && i != 0) {
      TargetView tmp = view_.targets[0];
      view_.targets[0] = view_.targets[i];
      view_.targets[i] = tmp;
      break;
    }
  }

  responseWindowNs_ = nsFromMs(cfg::mix(cfg::kFocusResponseWindowMs, cfg::kFocusResponseWindowEndMs, d));
  view_.showWindowRing = true;
  view_.windowProgress = 0.0f;
  ++view_.stimulusSerial;
  activeStimulusNs_ = nowNs;
  session_.stimulusAppeared(nowNs);

  const float gap = cfg::mix(520.0f, 340.0f, d);
  nextSpawnNs_ = nowNs + nsFromMs(gap * rng_.range(0.85f, 1.15f));
  for (int i = 0; i < view_.activeTargets; ++i) {
    view_.addFx(FxKind::SpawnBlip, view_.targets[i].center, 0.35f);
  }
}

void ModeRunner::updateFocus(Nanos nowNs, const ModeContext& ctx) {
  view_.fxCount = 0;
  if (session_.summary().hits >= cfg::kFocusTargetsPerRound) {
    if (!finished_) {
      finished_ = true;
      session_.endRound(nowNs);
    }
    view_.clear();
    return;
  }
  if (view_.activeTargets == 0) {
    if (nowNs >= nextSpawnNs_) spawnFocusGroup(nowNs, ctx);
    return;
  }
  for (int i = 0; i < view_.activeTargets; ++i) {
    view_.targets[i].spawnProgress = clamp01(view_.targets[i].spawnProgress + 0.3f);
  }
  const Nanos age = nowNs - activeStimulusNs_;
  view_.windowProgress = clamp01(static_cast<float>(age) / static_cast<float>(responseWindowNs_));
  if (age >= responseWindowNs_) {
    session_.stimulusExpired();
    for (int i = 0; i < view_.activeTargets; ++i) {
      view_.addFx(FxKind::MissBurst, view_.targets[i].center, 0.5f);
    }
    view_.clearTargets();
    activeStimulusNs_ = 0;
  }
}

TapResult ModeRunner::pressFocus(Vec2 pos, Nanos tapNs, const ModeContext& ctx) {
  TapResult out;
  out.pos = pos;
  if (!session_.roundActive()) return out;

  if (view_.activeTargets == 0) {
    out.consumed = true;
    out.kind = session_.summary().trials == 0 ? PressKind::FalseStart : PressKind::Spam;
    session_.registerPress(out.kind, 0, levelFactor());
    view_.addFx(out.kind == PressKind::FalseStart ? FxKind::FalseStart : FxKind::MissBurst, pos, 0.7f);
    return out;
  }

  for (int i = 0; i < view_.activeTargets; ++i) {
    if (!hitTest(pos, view_.targets[i], ctx)) continue;
    const bool correct = view_.targets[i].isTarget;
    const Nanos age = tapNs - activeStimulusNs_;
    if (age >= responseWindowNs_) {
      out.consumed = true;
      out.kind = PressKind::Spam;
      session_.stimulusExpired();
      session_.registerPress(PressKind::Spam, 0, levelFactor());
      view_.clearTargets();
      return out;
    }
    if (!correct) {
      out.consumed = true;
      out.kind = PressKind::WrongTarget;
      session_.registerPress(PressKind::WrongTarget, 0, levelFactor());
      view_.addFx(FxKind::WrongBurst, pos, 1.0f);
      view_.addFx(FxKind::ScreenPulse, pos, 0.35f);
      view_.clearTargets();
      activeStimulusNs_ = 0;
      return out;
    }

    out.consumed = true;
    out.kind = PressKind::Hit;
    out.reactionUs = reactionFrom(activeStimulusNs_, tapNs);
    hasLastReaction_ = true;
    lastReactionUs_ = out.reactionUs;
    session_.registerPress(PressKind::Hit, out.reactionUs, levelFactor());
    session_.noteCorrectPress();
    view_.addFx(FxKind::HitBurst, pos, 1.0f);
    view_.addFx(FxKind::ScreenPulse, pos, 0.4f);
    view_.clearTargets();
    activeStimulusNs_ = 0;
    return out;
  }

  out.consumed = true;
  out.kind = PressKind::Spam;
  session_.registerPress(PressKind::Spam, 0, levelFactor());
  view_.addFx(FxKind::MissBurst, pos, 0.6f);
  return out;
}

// ---------------------------------------------------------------------------
// MAX
// ---------------------------------------------------------------------------

void ModeRunner::beginMax(Nanos nowNs, const ModeContext& ctx) {
  level_ = 1;
  nextSpawnNs_ = nowNs + nsFromMs(cfg::kMaxSpawnDelayStartMs);
  responseWindowNs_ = nsFromMs(cfg::kMaxResponseWindowStartMs);
  (void)ctx;
}

void ModeRunner::updateMax(Nanos nowNs, const ModeContext& ctx) {
  view_.fxCount = 0;
  if (finished_) return;

  const float d = difficulty();

  if (view_.activeTargets > 0) {
    // Movement: the target drifts, bouncing inside the play area.  Speed scales
    // with level so late targets are genuinely harder to track.
    const float speed = cfg::mix(cfg::kMaxMoveSpeedStartDp, cfg::kMaxMoveSpeedEndDp, d) * ctx.density;
    TargetView& t = view_.targets[0];
    if (speed > 1.0f) {
      // Fixed 1/60 s step: MAX is the one mode whose target moves, and keeping
      // the step constant keeps its bounce identical on every device.
      const float dt = 1.0f / 60.0f;
      t.center.x += movementDir_.x * speed * dt;
      t.center.y += movementDir_.y * speed * dt;
      const Rect& a = spawnRect(ctx);
      const float lo = a.x + t.radius;
      const float hi = a.right() - t.radius;
      const float loY = a.y + t.radius;
      const float hiY = a.bottom() - t.radius;
      if (hi <= lo) {
        t.center.x = a.centerX();
        movementDir_.x = -movementDir_.x;
      } else {
        if (t.center.x < lo) {
          t.center.x = lo;
          movementDir_.x = std::fabs(movementDir_.x);
        } else if (t.center.x > hi) {
          t.center.x = hi;
          movementDir_.x = -std::fabs(movementDir_.x);
        }
      }
      if (hiY <= loY) {
        t.center.y = a.centerY();
        movementDir_.y = -movementDir_.y;
      } else {
        if (t.center.y < loY) {
          t.center.y = loY;
          movementDir_.y = std::fabs(movementDir_.y);
        } else if (t.center.y > hiY) {
          t.center.y = hiY;
          movementDir_.y = -std::fabs(movementDir_.y);
        }
      }
      t.wobble = movementSeed_ + static_cast<float>(nowNs % 1000000ull) * 0.000001f;
    }
    t.spawnProgress = clamp01(t.spawnProgress + 0.3f);

    // Distractors, once the run is deep enough.  They never move and never
    // respond; they exist purely to make the real target harder to isolate.
    int extras = 0;
    if (level_ >= cfg::kMaxDistractorLevel) {
      extras = 1 + (level_ - cfg::kMaxDistractorLevel) / 4;
      if (extras > 4) extras = 4;
    }
    while (extras > 0 && view_.activeTargets < RoundView::kMaxTargets) {
      TargetView& d2 = view_.targets[view_.activeTargets];
      d2 = TargetView{};
      d2.radius = cfg::mix(cfg::kMaxRadiusStartDp, cfg::kMaxRadiusEndDp, d) * ctx.density * 0.8f;
      d2.center = placeTarget(d2.radius, ctx, true);
      d2.isTarget = false;
      d2.mark = MarkKind::None;
      d2.alpha = 0.7f;
      d2.spawnProgress = 1.0f;
      d2.id = ++targetSerial_;
      ++view_.activeTargets;
      ++extras;
    }
    while (view_.activeTargets > 1 && !view_.targets[0].isTarget) {
      // The real target is always slot 0 in MAX.
      TargetView tmp = view_.targets[0];
      view_.targets[0] = view_.targets[view_.activeTargets - 1];
      view_.targets[view_.activeTargets - 1] = tmp;
    }

    const Nanos age = nowNs - activeStimulusNs_;
    view_.showWindowRing = true;
    view_.windowProgress = clamp01(static_cast<float>(age) / static_cast<float>(responseWindowNs_));
    if (age >= responseWindowNs_) {
      session_.stimulusExpired();
      view_.addFx(FxKind::MissBurst, view_.targets[0].center, 1.0f);
      view_.clearTargets();
      activeStimulusNs_ = 0;
      if (!finished_) {
        finished_ = true;
        session_.endRound(nowNs);
      }
    }
    return;
  }

  if (nowNs >= nextSpawnNs_) {
    view_.clear();
    TargetView& t = view_.targets[0];
    t = TargetView{};
    t.radius = cfg::mix(cfg::kMaxRadiusStartDp, cfg::kMaxRadiusEndDp, d) * ctx.density;
    // Keep a moving target away from the walls so the bounce is clean.
    const Rect inner = spawnRect(ctx).inset(t.radius + 4.0f * ctx.density);
    t.center = {rng_.range(inner.left(), inner.right()), rng_.range(inner.top(), inner.bottom())};
    if (inner.right() <= inner.left()) t.center.x = spawnRect(ctx).centerX();
    if (inner.bottom() <= inner.top()) t.center.y = spawnRect(ctx).centerY();
    t.isTarget = true;
    t.mark = MarkKind::Bullseye;
    t.spawnProgress = 0.0f;
    t.id = ++targetSerial_;
    view_.activeTargets = 1;
    const float angle = rng_.range(0.0f, 2.0f * kPi);
    movementDir_ = {std::cos(angle), std::sin(angle)};
    responseWindowNs_ = nsFromMs(
        cfg::mix(cfg::kMaxResponseWindowStartMs, cfg::kMaxResponseWindowEndMs, d));
    view_.showWindowRing = true;
    view_.windowProgress = 0.0f;
    ++view_.stimulusSerial;
    activeStimulusNs_ = nowNs;
    session_.stimulusAppeared(nowNs);
    view_.addFx(FxKind::SpawnBlip, t.center, 0.7f);

    const float delay = cfg::mix(cfg::kMaxSpawnDelayStartMs, cfg::kMaxSpawnDelayEndMs, d);
    nextSpawnNs_ = nowNs + nsFromMs(delay * rng_.range(0.8f, 1.2f));
  }
}

TapResult ModeRunner::pressMax(Vec2 pos, Nanos tapNs, const ModeContext& ctx) {
  TapResult out;
  out.pos = pos;
  if (!session_.roundActive() || finished_) return out;

  if (view_.activeTargets == 0) {
    out.consumed = true;
    out.kind = session_.summary().trials == 0 ? PressKind::FalseStart : PressKind::Spam;
    session_.registerPress(out.kind, 0, levelFactor());
    view_.addFx(out.kind == PressKind::FalseStart ? FxKind::FalseStart : FxKind::MissBurst, pos, 0.7f);
    if (mode_ == Mode::Max) {
      // In MAX a single mistake of any kind ends the run.
      finished_ = true;
      session_.endRound(tapNs);
    }
    return out;
  }

  bool landedOnSomething = false;
  for (int i = 0; i < view_.activeTargets; ++i) {
    if (!hitTest(pos, view_.targets[i], ctx)) continue;
    landedOnSomething = true;
    if (!view_.targets[i].isTarget) break;  // a distractor: wrong target
    const Nanos age = tapNs - activeStimulusNs_;
    if (age >= responseWindowNs_) {
      out.consumed = true;
      out.kind = PressKind::Spam;
      session_.stimulusExpired();
      session_.registerPress(PressKind::Spam, 0, levelFactor());
      view_.clearTargets();
      activeStimulusNs_ = 0;
      finished_ = true;
      session_.endRound(tapNs);
      return out;
    }
    out.consumed = true;
    out.kind = PressKind::Hit;
    out.reactionUs = reactionFrom(activeStimulusNs_, tapNs);
    hasLastReaction_ = true;
    lastReactionUs_ = out.reactionUs;
    session_.registerPress(PressKind::Hit, out.reactionUs, levelFactor());
    view_.addFx(FxKind::HitBurst, pos, 1.0f);
    view_.addFx(FxKind::ScreenPulse, pos, 0.35f);

    ++level_;
    session_.mutableSummary().level = level_;
    if (level_ % 5 == 0) view_.addFx(FxKind::LevelUp, pos, 1.0f);
    if (session_.summary().streak > 0 && session_.summary().streak % 5 == 0) {
      view_.addFx(FxKind::ComboPop, pos, 1.0f);
    }
    view_.clearTargets();
    activeStimulusNs_ = 0;
    return out;
  }

  out.consumed = true;
  out.kind = landedOnSomething ? PressKind::WrongTarget : PressKind::Spam;
  session_.registerPress(out.kind, 0, levelFactor());
  view_.addFx(landedOnSomething ? FxKind::WrongBurst : FxKind::MissBurst, pos, 1.0f);
  view_.addFx(FxKind::ScreenPulse, pos, 0.3f);
  view_.clearTargets();
  activeStimulusNs_ = 0;
  finished_ = true;
  session_.endRound(tapNs);
  return out;
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void ModeRunner::update(Nanos nowNs, const ModeContext& ctx) {
  // Remembered so a press can be hit-tested without threading the context
  // through the JNI boundary on every tap.
  lastCtx_ = ctx;
  switch (mode_) {
    case Mode::Reflex: updateReflex(nowNs, ctx); break;
    case Mode::Flick: updateFlick(nowNs, ctx); break;
    case Mode::Focus: updateFocus(nowNs, ctx); break;
    case Mode::Max: updateMax(nowNs, ctx); break;
  }
}

TapResult ModeRunner::onPress(Vec2 pos, Nanos tapNs) {
  switch (mode_) {
    case Mode::Reflex: return pressReflex(pos, tapNs, lastCtx_);
    case Mode::Flick: return pressFlick(pos, tapNs, lastCtx_);
    case Mode::Focus: return pressFocus(pos, tapNs, lastCtx_);
    case Mode::Max: return pressMax(pos, tapNs, lastCtx_);
  }
  return TapResult{};
}

}  // namespace pp
