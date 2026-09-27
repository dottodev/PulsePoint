// The application: screens, state machine, and everything that ties the game
// rules to what is on the display.
//
// One class, split across two translation units: app.cpp owns the lifecycle,
// input and the state machine; screens.cpp owns the drawing.  A screen is a
// method rather than a class because a screen here has no state worth naming --
// anything persistent lives in the session, the profile, or the widget layer's
// own slot table.
#pragma once

#include <cstddef>
#include <cstdint>

#include "audio/synth.h"
#include "core/input.h"
#include "gfx/renderer.h"
#include "game/modes.h"
#include "meta/achievements.h"
#include "meta/profile.h"
#include "ui/particles.h"
#include "ui/widgets.h"

namespace pp {

enum class Screen : uint8_t {
  Boot,
  MainMenu,
  ModeSelect,
  Ready,
  Playing,
  Result,
  Achievements,
  Statistics,
  Settings,
};

// One queued achievement overlay.  Unlocks earned mid-round are queued and
// played after the round ends, so nothing is stolen from a live stimulus.
struct PendingUnlock {
  int index = -1;
  Nanos queuedNs = 0;
};

class App {
 public:
  bool init(const char* filesDir);
  void shutdown();

  // Called by the GL thread whenever the surface is created or recreated.  All
  // values are in pixels except `density`, which is pixels per dp.
  void onSurfaceChanged(int widthPx, int heightPx, float density, float safeTopPx,
                        float safeBottomPx);
  void onDrawFrame();
  void onPause();
  void onResume();
  // A haptic pulse was requested by the platform (currently unused: the game has
  // no impact feedback wired up yet).  Kept so the setting and the hook exist in
  // one place rather than being scattered through the JNI layer.
  void onHaptic(int strength);

  // The platform pushes pointer events in from the UI thread and drains them at
  // the top of the frame.
  InputRouter& input() { return input_; }

  // Pulled by the Java audio thread.
  int renderAudio(int16_t* out, int frames);

  // ---- accessors ---------------------------------------------------------
  Screen screen() const { return screen_; }
  const Stats& stats() const { return profile_.stats; }
  const Settings& settings() const { return profile_.settings; }
  float averageFrameMs() const { return msFromNs(avgFrameNs_); }
  uint32_t droppedInputSamples() const { return input_.droppedSamples(); }
  int calibrationMsUsed() const { return lastCalibrationMs_; }
  Mode selectedMode() const { return pendingMode_; }
  bool inputIsLocked() const { return inputLocked(); }

 private:
  // ---- state machine -----------------------------------------------------
  void goTo(Screen next);
  void navigate(Screen next, audio::Sfx sfx = audio::Sfx::UiTap);
  bool inputLocked() const;
  void startRound(Mode mode, Nanos now);
  void finishRound(Nanos now);
  void onModeFinished(Nanos now);
  void initAmbient();

  // ---- per-frame ---------------------------------------------------------
  void drainInput(Nanos now);
  void handleExtraTap(const Tap& tap);
  void deliverTapToRound(const Tap& tap);
  void drainModeFx();
  void updatePlaying(Nanos now);
  void updateAmbient(Nanos now, float dt);
  void applyPendingResults(Nanos now);
  void saveProfileIfDirty(Nanos now);
  void playSfx(audio::Sfx id, float intensity = 1.0f, float paramMs = 0.0f);
  float calibrationMs() const;
  void markDirty() { profileDirty_ = true; }

  // ---- drawing (screens.cpp / screens2.cpp) -----------------------------
  void drawBackground(Nanos now, float dt);
  void drawMenu(Nanos now);
  void drawModeSelect(Nanos now);
  void drawReady(Nanos now);
  void drawPlaying(Nanos now);
  void drawResult(Nanos now);
  void drawAchievements(Nanos now);
  void drawStatistics(Nanos now);
  void drawSettings(Nanos now);
  void drawTransition(Nanos now);
  void drawAchievementOverlay(Nanos now);
  void drawDebug(Nanos now);
  // Returns the y a sub-screen's content should start at, so the header owns
  // the spacing instead of every screen guessing it.
  float drawSubScreenHeader(Nanos now, const char* title, int backId,
                             const char* subtitle);
  void drawTargets(Nanos now);
  void drawHud(Nanos now);
  // The centre-of-screen prompt used by READY, MAX over and the achievement wait.
  void drawTapPrompt(Nanos now, const char* line1, const char* line2);

  // ---- helpers -----------------------------------------------------------
  Rect safeArea() const;
  ModeContext modeContext() const;
  float dp(float value) const;
  float uiScale() const { return uiScale_; }
  const char* modeBestLabel(Mode m, char* buf, size_t cap) const;
  bool isCulledForSettings(const Rect& r) const;
  void applySettingChange();
  void performReset();

  // ---- persistent state --------------------------------------------------
  gfx::Renderer renderer_;
  ui::Ui ui_;
  ui::FxSystem fx_;
  audio::Synth synth_;
  InputRouter input_;

  Profile profile_;
  AchievementState achievements_;
  Settings* settings_ = nullptr;
  const char* filesDir_ = nullptr;

  Screen screen_ = Screen::Boot;
  Mode pendingMode_ = Mode::Reflex;
  Mode finishedMode_ = Mode::Reflex;

  // Transition
  Nanos transitionStart_ = 0;
  float transitionDur_ = cfg::kScreenTransitionSeconds;
  bool transitioning_ = false;

  // Round
  ModeRunner mode_;
  bool roundActive_ = false;
  Nanos roundStartNs_ = 0;
  RoundSummary lastSummary_{};
  bool lastRoundWasRecord_ = false;
  bool scoreCountUpDone_ = false;
  float displayedScore_ = 0.0f;
  // When the RESULT screen appeared, which is what every reveal on it counts from.
  Nanos resultStartNs_ = 0;

  // Surface
  float surfaceWidth_ = 0.0f;
  float surfaceHeight_ = 0.0f;
  float density_ = 1.0f;
  float uiScale_ = 1.0f;
  float safeTopPx_ = 0.0f;
  float safeBottomPx_ = 0.0f;

  // Pointer, derived from accepted taps.
  Vec2 pointer_{};
  bool pressedThisFrame_ = false;
  bool releasedThisFrame_ = false;
  bool holdActive_ = false;
  Nanos holdUntilNs_ = 0;

  // Ambient animation
  uint64_t ambientSeed_ = 0;
  float bgTime_ = 0.0f;
  static constexpr int kAmbientParticles = 14;
  struct AmbientDot {
    float x, y, speed, size, phase;
  };
  AmbientDot ambientDots_[kAmbientParticles] = {};

  // Timing
  Nanos lastFrameNs_ = 0;
  Nanos avgFrameNs_ = 16666667;
  Nanos startNs_ = 0;
  float fps_ = 60.0f;
  uint32_t frames_ = 0;
  float fpsAccum_ = 0.0f;
  int32_t lastCalibrationMs_ = 0;

  // Achievement overlay
  PendingUnlock queue_[8];
  int queueCount_ = 0;
  int overlayIndex_ = -1;
  Nanos overlayStartNs_ = 0;
  float overlayMinSeconds_ = cfg::kAchievementMinShowSeconds;
  bool overlayActive_ = false;

  bool resetRequested_ = false;
  // Set by the QUIT control; consumed at the top of the next update.
  bool abortRound_ = false;

  bool profileDirty_ = false;
  Nanos lastSaveNs_ = 0;
};

}  // namespace pp
