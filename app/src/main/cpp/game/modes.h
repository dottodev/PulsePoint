// Gameplay modes and the view model they emit.
//
// A mode owns *rules* only.  It never draws anything and never touches OpenGL:
// it maintains targets, decides what a tap did, updates the session, and fills a
// plain `RoundView` describing what should be on screen.  The renderer and the
// UI read that struct, which is what lets every mode be tested headlessly and
// keeps the frame budget in one place.
#pragma once

#include <cstdint>

#include "../core/clock.h"
#include "../core/math_util.h"
#include "../core/rng.h"
#include "session.h"

namespace pp {

// How a target should be drawn.  FOCUS uses `mark` to make the real target
// visually distinct from its decoys while keeping the search hard.
enum class MarkKind : uint8_t {
  None,
  Bullseye,  // A centre dot.  This is the FOCUS target.
  Plus,
  Cross,
  Slash,
  Square,
  Triangle,
  Arc,
};

struct TargetView {
  Vec2 center;
  float radius = 0.0f;
  bool isTarget = true;   // FOCUS: the one that counts.
  MarkKind mark = MarkKind::Bullseye;
  float alpha = 1.0f;
  float spawnProgress = 1.0f;  // 0 -> 1 while the target scales in.
  float hitFlash = 0.0f;        // 1 -> 0 after a successful press.
  float wobble = 0.0f;          // MAX: movement phase, for the drift wobble.
  int id = 0;
};

// Transient effects a mode wants the FX system to play.  The mode never owns
// them; it just appends, the FX system consumes and clears.
enum class FxKind : uint8_t {
  HitBurst,
  MissBurst,
  WrongBurst,
  LevelUp,
  ScreenPulse,
  ComboPop,
  FalseStart,
  SpawnBlip,
};

struct FxRequest {
  FxKind kind = FxKind::HitBurst;
  Vec2 pos;
  float strength = 1.0f;
};

struct RoundView {
  static constexpr int kMaxTargets = 12;
  static constexpr int kMaxFx = 8;

  TargetView targets[kMaxTargets];
  int activeTargets = 0;

  FxRequest fx[kMaxFx];
  int fxCount = 0;

  // A ring drawn around the target that is currently answerable, so the
  // player can see the response window closing.
  bool showWindowRing = false;
  float windowProgress = 0.0f;

  // Bumped every time a new stimulus appears; the UI and tests use it to detect
  // a stimulus without having to inspect the target list.
  int stimulusSerial = 0;

  // Retires the live targets but keeps this frame's effect requests, which is
  // what a mode wants when a press both resolves a target and spawns an effect.
  void clearTargets() {
    activeTargets = 0;
    showWindowRing = false;
    windowProgress = 0.0f;
  }
  void clear() {
    clearTargets();
    fxCount = 0;
  }
  void addFx(FxKind kind, Vec2 pos, float strength = 1.0f) {
    if (fxCount >= kMaxFx) return;
    fx[fxCount].kind = kind;
    fx[fxCount].pos = pos;
    fx[fxCount].strength = strength;
    ++fxCount;
  }
};

struct TapResult {
  bool consumed = false;      // The tap was used by a gameplay rule.
  PressKind kind = PressKind::Spam;
  Vec2 pos;
  uint32_t reactionUs = 0;
};

struct ModeContext {
  Rect area;          // Playable area in pixels, already inset for safe areas.
  // Target spawn area: area minus the HUD strips.  A target under the QUIT
  // pill is unanswerable -- the abort tap wins over the answer -- and one
  // under the progress bar is unreadable.  Empty unless the app fills it in;
  // placement falls back to area (which is what bare unit tests get).
  Rect spawn;
  float density = 1.0f;  // Pixels per dp.
  float scale = 1.0f;    // Extra UI scale from the display-size setting.
  float aspect = 0.5f;   // width / height
  float safeTop = 0.0f;
  float safeBottom = 0.0f;
};

// One mode's worth of state.  Only one is ever live.
class ModeRunner {
 public:
  void begin(Mode mode, uint64_t seed, Nanos nowNs, const ModeContext& ctx);
  void update(Nanos nowNs, const ModeContext& ctx);
  TapResult onPress(Vec2 pos, Nanos tapNs);

  void end(Nanos nowNs);

  bool finished() const { return finished_; }
  Mode mode() const { return mode_; }
  int level() const { return level_; }
  int targetCount() const { return view_.activeTargets; }
  const RoundView& view() const { return view_; }
  // Effect requests are cleared by the FX system after it consumes them.
  RoundView& mutableView() { return view_; }
  Session& session() { return session_; }
  const Session& session() const { return session_; }
  // Stimulus timestamp of the currently answerable target, 0 if none.
  Nanos stimulusNs() const { return activeStimulusNs_; }

  void setCalibrationMs(float ms) { calibrationMs_ = ms; }
  // Reaction time of the most recent successful press, in microseconds.
  uint32_t lastReactionUs() const { return lastReactionUs_; }
  bool hasLastReaction() const { return hasLastReaction_; }

 private:
  void beginReflex(Nanos nowNs, const ModeContext& ctx);
  void beginFlick(Nanos nowNs, const ModeContext& ctx);
  void beginFocus(Nanos nowNs, const ModeContext& ctx);
  void beginMax(Nanos nowNs, const ModeContext& ctx);

  void updateReflex(Nanos nowNs, const ModeContext& ctx);
  void updateFlick(Nanos nowNs, const ModeContext& ctx);
  void updateFocus(Nanos nowNs, const ModeContext& ctx);
  void updateMax(Nanos nowNs, const ModeContext& ctx);

  TapResult pressReflex(Vec2 pos, Nanos tapNs, const ModeContext& ctx);
  TapResult pressFlick(Vec2 pos, Nanos tapNs, const ModeContext& ctx);
  TapResult pressFocus(Vec2 pos, Nanos tapNs, const ModeContext& ctx);
  TapResult pressMax(Vec2 pos, Nanos tapNs, const ModeContext& ctx);

  void spawnReflexTarget(Nanos nowNs, const ModeContext& ctx);
  void spawnFocusGroup(Nanos nowNs, const ModeContext& ctx);
  void spawnFlickBatch(Nanos nowNs, const ModeContext& ctx);

  // Applies the display-latency compensation to a stimulus timestamp and
  // converts an input timestamp into a reaction time in microseconds.
  uint32_t reactionFrom(Nanos stimulusNs, Nanos tapNs) const;
  float levelFactor() const;

  bool hitTest(Vec2 pos, const TargetView& t, const ModeContext& ctx) const;
  // Not const: placement consumes randomness.
  Vec2 placeTarget(float radius, const ModeContext& ctx, bool avoidPrevious);
  float difficulty() const;
  bool anyTargetsLive() const { return view_.activeTargets > 0; }

  Mode mode_ = Mode::Reflex;
  Session session_{};
  RoundView view_{};
  Rng rng_{};
  ModeContext lastCtx_{};
  bool finished_ = false;
  bool hasLastReaction_ = false;
  uint32_t lastReactionUs_ = 0;
  int level_ = 1;
  Nanos startedNs_ = 0;
  Nanos activeStimulusNs_ = 0;
  Nanos nextSpawnNs_ = 0;
  Nanos responseWindowNs_ = 0;
  float calibrationMs_ = 0.0f;
  int trialIndex_ = 0;
  int targetSerial_ = 0;
  int nextSpawnIn_ = 0;      // FOCUS/MAX: how many more targets join the field.
  float nextSpawnTimer_ = 0.0f;
  Vec2 lastSpawnPos_{};
  // Scales the minimum distance between consecutive spawns; FLICK raises it as
  // the round progresses, so the player has to travel further as well as faster.
  float separationScale_ = 1.0f;
  float movementSeed_ = 0.0f;
  Vec2 movementDir_{1.0f, 0.0f};
  bool finishedReported_ = false;
};

}  // namespace pp
