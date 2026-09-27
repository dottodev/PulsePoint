#include "app.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../core/rng.h"
#include "../meta/stats.h"
#include "../ui/theme.h"

namespace pp {

namespace {

// A tap is held for this long before the UI sees a release, which is what makes
// a button press animate instead of blinking.
constexpr Nanos kTapHoldNs = 55 * kNsPerMs;
constexpr Nanos kSaveDebounceNs = 700 * kNsPerMs;
constexpr int kMaxTapsPerFrame = 8;

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool App::init(const char* filesDir) {
  filesDir_ = filesDir;
  startNs_ = monotonicNow();
  lastFrameNs_ = startNs_;

  if (!renderer_.init(0, 0)) {
    std::printf("[pp] renderer initialisation failed\n");
    return false;
  }

  const char* dir = filesDir_ != nullptr ? filesDir_ : ".";
  if (!loadProfileFromFile(dir, &profile_)) {
    profile_ = Profile{};
    std::printf("[pp] no saved profile; starting fresh\n");
  }
  profile_.settings.clampToValid();
  settings_ = &profile_.settings;
  seedAchievementState(achievements_, profile_.stats, profile_.achievementMask);

  synth_.init(48000);
  synth_.setSoundEnabled(settings_->sound);
  synth_.setMusicEnabled(settings_->music);

  input_.configure(1.0f);
  ambientSeed_ = randomSeed();
  fx_.init(safeArea(), randomSeed());
  initAmbient();

  screen_ = Screen::MainMenu;
  transitionStart_ = startNs_;
  transitioning_ = true;
  return true;
}

void App::shutdown() {
  if (filesDir_ != nullptr) saveProfileToFile(filesDir_, profile_);
  synth_.shutdown();
  renderer_.shutdown();
}

void App::initAmbient() {
  Rng r(ambientSeed_);
  for (int i = 0; i < kAmbientParticles; ++i) {
    ambientDots_[i].x = r.nextFloat();
    ambientDots_[i].y = r.nextFloat();
    ambientDots_[i].speed = r.range(0.006f, 0.022f);
    ambientDots_[i].size = r.range(1.0f, 2.6f);
    ambientDots_[i].phase = r.range(0.0f, 6.2831853f);
  }
}

void App::onSurfaceChanged(int widthPx, int heightPx, float density, float safeTopPx,
                           float safeBottomPx) {
  surfaceWidth_ = static_cast<float>(widthPx);
  surfaceHeight_ = static_cast<float>(heightPx);
  density_ = density > 0.1f ? density : 1.0f;
  safeTopPx_ = safeTopPx;
  safeBottomPx_ = safeBottomPx;

  // The interface is authored against a virtual short edge, so a tablet does not
  // become a stretched phone and a dense small phone does not become unreadable.
  uiScale_ = clampf(std::min(surfaceWidth_, surfaceHeight_) / (cfg::kDesignShortEdgeDp * density_),
                    0.80f, 1.45f);

  input_.configure(density_);
  fx_.setBounds(modeContext().area);
  initAmbient();
}

void App::onPause() {
  // A round in progress is abandoned rather than resumed: an interrupted
  // reaction measurement is not a measurement.
  if (roundActive_) {
    mode_.end(monotonicNow());
    roundActive_ = false;
  }
  if (filesDir_ != nullptr) {
    saveProfileToFile(filesDir_, profile_);
    profileDirty_ = false;
  }
  synth_.setMusicDucked(true);
}

void App::onHaptic(int strength) { (void)strength; }

void App::onResume() {
  lastFrameNs_ = monotonicNow();
  input_.reset();
  pointer_ = Vec2{};
  holdUntilNs_ = 0;
  holdActive_ = false;
  synth_.setMusicDucked(false);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

float App::dp(float value) const { return value * density_ * uiScale_; }

Rect App::safeArea() const {
  const float m = dp(cfg::kScreenMarginDp);
  const float top = safeTopPx_ + m;
  float bottom = surfaceHeight_ - safeBottomPx_ - m;
  // The FPS readout owns a strip below the layout while it is on, so no
  // footer, button or progress bar can ever land underneath it.  The shift
  // only exists in dev mode; players never see the layout move.
  if (settings_ != nullptr && settings_->showFps) bottom -= dp(22.0f);
  return {m, top, std::max(dp(120.0f), surfaceWidth_ - m * 2.0f), std::max(dp(120.0f), bottom - top)};
}

ModeContext App::modeContext() const {
  ModeContext ctx;
  ctx.area = safeArea();
  // The QUIT pill is 32 dp tall at the top of the area and the progress
  // readout sits at the bottom; targets stay a finger-margin clear of both.
  // Callers inset their own radius on top of this.
  const Rect& area = ctx.area;
  const float hudTopPx = (32.0f + 12.0f) * density_;
  const float hudBottomPx = 20.0f * density_;
  ctx.spawn = Rect{area.x, area.y + hudTopPx, area.w, area.h - hudTopPx - hudBottomPx};
  ctx.density = density_;
  ctx.scale = uiScale_;
  ctx.aspect = surfaceHeight_ > 0.0f ? surfaceWidth_ / surfaceHeight_ : 0.5f;
  ctx.safeTop = safeTopPx_;
  ctx.safeBottom = safeBottomPx_;
  return ctx;
}

float App::calibrationMs() const {
  if (settings_ == nullptr) return 0.0f;
  if (settings_->calibrationMs != Settings::kCalibrationAuto) {
    return static_cast<float>(settings_->calibrationMs);
  }
  // Half the frame interval: the standard first-order estimate of how long after
  // a stimulus is stamped the photons actually leave the panel.  Clamped so a
  // stutter cannot inflate a score and a stalled frame cannot erase one.
  return clampf(msFromNs(avgFrameNs_) * 0.5f, cfg::kDefaultAutoCalibrationMinMs,
                cfg::kDefaultAutoCalibrationMaxMs);
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

void App::navigate(Screen next, audio::Sfx sfx) {
  if (next == screen_ && !transitioning_) return;
  playSfx(sfx, 0.9f);
  goTo(next);
}

void App::goTo(Screen next) {
  if (next == screen_) return;
  screen_ = next;
  transitionStart_ = monotonicNow();
  transitionDur_ = cfg::kScreenTransitionSeconds;
  transitioning_ = true;
}

bool App::inputLocked() const {
  return transitioning_ || overlayActive_ || queueCount_ > 0;
}

// ---------------------------------------------------------------------------
// Round flow
// ---------------------------------------------------------------------------

void App::startRound(Mode mode, Nanos now) {
  pendingMode_ = mode;
  fx_.clear();
  abortRound_ = false;
  scoreCountUpDone_ = false;
  displayedScore_ = 0.0f;
  lastRoundWasRecord_ = false;
  mode_.setCalibrationMs(calibrationMs());
  lastCalibrationMs_ = static_cast<int32_t>(calibrationMs() + 0.5f);
  mode_.begin(mode, randomSeed(), now, modeContext());
  roundActive_ = true;
  roundStartNs_ = now;
  playSfx(audio::Sfx::UiTap, 0.8f);
  synth_.setMusicDucked(false);
  goTo(Screen::Playing);
}

void App::finishRound(Nanos now) {
  roundActive_ = false;
  lastSummary_ = mode_.session().summary();
  lastSummary_.durationNs = elapsedSince(now, roundStartNs_);
  if (lastSummary_.survivalNs == 0) lastSummary_.survivalNs = lastSummary_.durationNs;
  finishedMode_ = lastSummary_.mode;

  profile_.stats.noteRound(finishedMode_, lastSummary_);

  // noteRound has already folded the round into every lifetime counter, including
  // the per-mode personal best; the record flag is only a question of whether this
  // particular round was the one that set it.
  const ModeStats& ms = profile_.stats.modes[static_cast<int>(finishedMode_)];
  lastRoundWasRecord_ = static_cast<uint64_t>(lastSummary_.score) > ms.bestScore;
  scoreCountUpDone_ = false;
  displayedScore_ = 0.0f;
  // Every reveal on the result screen counts from here, not from when the round
  // started: by the time RESULT appears the round has already run for seconds, so
  // counting from the round would make the reveal and the count-up instant.
  resultStartNs_ = now;

  int unlocks[8];
  const int n = collectUnlocks(achievements_, profile_.stats, &lastSummary_, unlocks, 8);
  profileDirty_ = true;

  for (int i = 0; i < n; ++i) {
    if (queueCount_ >= static_cast<int>(sizeof(queue_) / sizeof(queue_[0]))) break;
    queue_[queueCount_].index = unlocks[i];
    queue_[queueCount_].queuedNs = now;
    ++queueCount_;
  }

  if (lastSummary_.hits > 0) playSfx(audio::Sfx::GameOver, 0.6f);
  if (lastRoundWasRecord_) playSfx(audio::Sfx::PersonalBest, 0.85f);
  saveProfileIfDirty(now);
}

void App::onModeFinished(Nanos now) {
  if (!roundActive_) return;
  finishRound(now);
  goTo(Screen::Result);
}

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------

void App::drainInput(Nanos now) {
  Tap taps[kMaxTapsPerFrame];
  const int n = input_.drain(taps, kMaxTapsPerFrame);
  pressedThisFrame_ = false;
  releasedThisFrame_ = false;

  if (n > 0) {
    // The first tap becomes the pointer, so buttons and drag-scroll see it.
    pointer_ = taps[0].pos;
    pressedThisFrame_ = true;
    holdActive_ = true;
    holdUntilNs_ = now + kTapHoldNs;
    // A live round consumes every accepted tap, including the first.  During
    // gameplay the pointer is not a UI cursor, it is a measurement, and the
    // router has already removed duplicates of the same physical touch.
    const bool toRound = screen_ == Screen::Playing && roundActive_ && !inputLocked();
    for (int i = 0; i < n; ++i) {
      if (toRound) {
        deliverTapToRound(taps[i]);
      } else {
        handleExtraTap(taps[i]);
      }
    }
  } else if (holdActive_ && now >= holdUntilNs_) {
    releasedThisFrame_ = true;
    holdActive_ = false;
  }
}

void App::handleExtraTap(const Tap& tap) {
  // Menu and result screens have no use for a second tap in the same frame, but
  // a live round does: two targets answered inside one frame is a real thing a
  // fast player does, and dropping it would understate the round.
  if (screen_ == Screen::Playing && roundActive_ && !inputLocked()) {
    deliverTapToRound(tap);
  }
}

void App::deliverTapToRound(const Tap& tap) {
  // The abort control is resolved before the mode, so a player can always leave
  // a round no matter where the target happens to be.
  const Rect area = safeArea();
  if (Rect{area.x, area.y, dp(70.0f), dp(40.0f)}.expanded(dp(4.0f)).contains(tap.pos)) {
    abortRound_ = true;
    return;
  }
  const TapResult r = mode_.onPress(tap.pos, tap.timeNs);
  if (!r.consumed) return;

  switch (r.kind) {
    case PressKind::Hit: {
      playSfx(audio::Sfx::Hit, 0.9f, static_cast<float>(r.reactionUs) / 1000.0f);
      char buf[12];
      std::snprintf(buf, sizeof(buf), "%u MS", r.reactionUs / 1000u);
      fx_.popText(tap.pos, buf, dp(20.0f));
      const int streak = mode_.session().summary().streak;
      if (streak > 0 && streak % 5 == 0) {
        playSfx(audio::Sfx::Combo, 0.8f, static_cast<float>(streak));
      }
      break;
    }
    case PressKind::WrongTarget: playSfx(audio::Sfx::Wrong, 0.85f); break;
    case PressKind::FalseStart: playSfx(audio::Sfx::FalseStart, 0.9f); break;
    // A stray press on an empty screen is intentionally silent: it is noise the
    // player made, not feedback they asked for.
    case PressKind::Spam: break;
  }
  drainModeFx();
}

void App::drainModeFx() {
  // ModeRunner hands out the effect list for one frame; the FX system consumes it
  // and clears the count so nothing is played twice.
  RoundView& v = mode_.mutableView();
  if (v.fxCount > 0) {
    fx_.spawn(v, monotonicNow());
    v.fxCount = 0;
  }
}

void App::updatePlaying(Nanos now) {
  if (!roundActive_) return;
  if (abortRound_) {
    // An abandoned run still counts as played: the presses, the samples and the
    // time are real, so they are kept.  Only the targets that never appeared are
    // missing, which is exactly what happened.
    abortRound_ = false;
    mode_.end(now);
    onModeFinished(now);
    return;
  }
  mode_.update(now, modeContext());
  drainModeFx();
  if (mode_.finished()) onModeFinished(now);
}

void App::updateAmbient(Nanos now, float dt) {
  bgTime_ += dt;
  fx_.update(now, dt);
  synth_.setMusicDucked(screen_ == Screen::Playing || screen_ == Screen::Ready);

  if (transitioning_) {
    const float t = clamp01(msFromNs(elapsedSince(now, transitionStart_)) /
                           std::max(1.0f, transitionDur_ * 1000.0f));
    if (t >= 1.0f) transitioning_ = false;
  }

  // Achievement overlay queue: pop the next one, play it out, then continue.
  if (overlayActive_) {
    const float shown = secFromNs(elapsedSince(now, overlayStartNs_));
    const bool wantsMin = shown >= overlayMinSeconds_;
    const bool wantsMax = shown >= cfg::kAchievementShowSeconds;
    if (wantsMax || (wantsMin && (pressedThisFrame_ || releasedThisFrame_))) {
      overlayActive_ = false;
      overlayIndex_ = -1;
      if (queueCount_ > 0) {
        --queueCount_;
        for (int i = 0; i < queueCount_; ++i) queue_[i] = queue_[i + 1];
      }
    }
  } else if (queueCount_ > 0) {
    overlayActive_ = true;
    overlayIndex_ = queue_[0].index;
    overlayStartNs_ = now;
    // The modal replaces the round's feedback: a reaction popup ("432 MS")
    // left over from the final press would float up through it, and popup
    // text uploads in the text pass, above every panel.  The numbers all live
    // on the result screen behind it.
    fx_.clear();
    playSfx(audio::Sfx::Achievement, 1.0f);
  }
}

void App::applyPendingResults(Nanos now) {
  if (screen_ != Screen::Result || scoreCountUpDone_) return;
  const float target = static_cast<float>(lastSummary_.score);
  if (target <= 0.0f) {
    displayedScore_ = 0.0f;
    scoreCountUpDone_ = true;
    return;
  }
  const float t = clamp01(secFromNs(elapsedSince(now, resultStartNs_)) / cfg::kScoreCountUpSeconds);
  displayedScore_ = easeOutCubic(t) * target;
  if (t >= 1.0f) {
    displayedScore_ = target;
    scoreCountUpDone_ = true;
  }
}

void App::saveProfileIfDirty(Nanos now) {
  if (!profileDirty_ || filesDir_ == nullptr) return;
  // Debounced, plus a hard write on the way out, so a long round does not hammer
  // the filesystem.
  if (elapsedSince(now, lastSaveNs_) < kSaveDebounceNs) return;
  if (saveProfileToFile(filesDir_, profile_)) profileDirty_ = false;
  lastSaveNs_ = now;
}

void App::playSfx(audio::Sfx id, float intensity, float paramMs) {
  if (settings_ != nullptr && !settings_->sound) return;
  synth_.trigger(id, intensity, paramMs);
}

int App::renderAudio(int16_t* out, int frames) { return synth_.render(out, frames); }

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void App::onDrawFrame() {
  const Nanos now = monotonicNow();
  Nanos frameNs = elapsedSince(now, lastFrameNs_);
  lastFrameNs_ = now;
  if (frameNs <= 0) frameNs = 1000000;
  // A long stall must not teleport the animation, but it also must not poison
  // the frame-time average that drives the latency calibration.
  const Nanos clampedFrame = frameNs > 500 * kNsPerMs ? 500 * kNsPerMs : frameNs;
  avgFrameNs_ = (avgFrameNs_ * 7 + clampedFrame) / 8;
  const float dt = static_cast<float>(clampedFrame) / static_cast<float>(kNsPerSec);

  fpsAccum_ += dt;
  ++frames_;
  if (fpsAccum_ >= 0.25f) {
    fps_ = static_cast<float>(frames_) / fpsAccum_;
    frames_ = 0;
    fpsAccum_ = 0.0f;
  }

  drainInput(now);
  updatePlaying(now);
  updateAmbient(now, dt);
  saveProfileIfDirty(now);

  // ---- draw ---------------------------------------------------------------
  renderer_.beginFrame(surfaceWidth_, surfaceHeight_, secFromNs(elapsedSince(now, startNs_)));
  renderer_.setClearColor(ui::theme::kVoid);

  ui::Frame frame;
  frame.r = &renderer_;
  frame.density = density_;
  frame.scale = uiScale_;
  frame.now = secFromNs(elapsedSince(now, startNs_));
  frame.dt = dt;
  frame.pointer = pointer_;
  frame.pointerDown = holdActive_;
  frame.pointerPressed = pressedThisFrame_;
  frame.pointerReleased = releasedThisFrame_;
  frame.inputLocked = inputLocked();
  ui_.begin(frame);

  drawBackground(now, dt);

  // A live overlay is a modal takeover, not a banner: the screen underneath is
  // hidden rather than dimmed.  Dimming cannot work here anyway -- the veil is
  // a shape, and all text uploads in a later draw call, so labels would shine
  // through at full brightness and collide with the overlay's own words.
  if (!overlayActive_) {
    switch (screen_) {
      case Screen::Boot: break;
      case Screen::MainMenu: drawMenu(now); break;
      case Screen::ModeSelect: drawModeSelect(now); break;
      case Screen::Ready: drawReady(now); break;
      case Screen::Playing: drawPlaying(now); break;
      case Screen::Result: drawResult(now); break;
      case Screen::Achievements: drawAchievements(now); break;
      case Screen::Statistics: drawStatistics(now); break;
      case Screen::Settings: drawSettings(now); break;
    }
  }

  // Particles and the screen flash sit above whichever screen is up, so the burst
  // from the press that ended a run is not cut off by the transition.
  fx_.draw(renderer_, now);
  drawTransition(now);
  drawAchievementOverlay(now);
  drawDebug(now);

  ui_.end();
  renderer_.endFrame();
}

// ---------------------------------------------------------------------------
// Shared drawing helpers (screens.cpp)
// ---------------------------------------------------------------------------

void App::drawBackground(Nanos now, float dt) {
  (void)now;
  gfx::Renderer& r = renderer_;
  const Rect area = safeArea();

  // A slowly travelling grid of hairlines: enough motion to read as alive,
  // structured enough to stay minimal.
  const float t = bgTime_;
  const float spacing = dp(46.0f);
  const float offset = fmodf(t * dp(7.0f), spacing);
  for (float y = area.y - spacing + offset; y < area.bottom(); y += spacing) {
    r.rect({area.x, y, area.w, 1.0f}, ui::theme::kVoid, 0.0f);
  }
  for (float x = area.x - spacing + offset * 0.6f; x < area.right(); x += spacing) {
    r.rect({x, area.y, 1.0f, area.h}, ui::theme::kVoid, 0.0f);
  }
  // Same lines again, dimmed, drifting the other way.  Two passes give depth
  // without a single gradient.
  for (float y = area.y - spacing - offset * 0.8f; y < area.bottom(); y += spacing * 2.0f) {
    r.rect({area.x, y, area.w, 1.0f}, ui::theme::kVoid, 0.0f);
  }

  // Ambient motes.  Fixed count, wrapped positions, no allocation.
  for (int i = 0; i < kAmbientParticles; ++i) {
    AmbientDot& d = ambientDots_[i];
    d.y -= d.speed * dt;
    if (d.y < -0.05f) {
      d.y = 1.05f;
      d.x = static_cast<float>(static_cast<uint32_t>(now / 1000000ull + i * 2654435761u) % 1000u) /
            1000.0f;
    }
    const float twinkle = 0.5f + 0.5f * std::sin(t * 1.6f + d.phase);
    const float size = dp(d.size);
    const Vec2 p{area.x + d.x * area.w, area.y + d.y * area.h};
    r.rect(Rect::fromCenter(p, size, size), withAlpha(ui::theme::kHairlineStrong, 0.55f * twinkle),
           0.0f);
  }

  // Corner brackets: a cheap frame that makes the play area legible.
  const float arm = dp(16.0f);
  const Color c = ui::theme::kHairline;
  const float inset = dp(6.0f);
  const float x0 = area.x - inset;
  const float y0 = area.y - inset;
  const float x1 = area.right() + inset;
  const float y1 = area.bottom() + inset;
  r.rect({x0, y0, arm, 1.5f}, c);
  r.rect({x0, y0, 1.5f, arm}, c);
  r.rect({x1 - arm, y0, arm, 1.5f}, c);
  r.rect({x1 - 1.5f, y0, 1.5f, arm}, c);
  r.rect({x0, y1 - 1.5f, arm, 1.5f}, c);
  r.rect({x0, y1 - arm, 1.5f, arm}, c);
  r.rect({x1 - arm, y1 - 1.5f, arm, 1.5f}, c);
  r.rect({x1 - 1.5f, y1 - arm, 1.5f, arm}, c);
}

}  // namespace pp
