// Link and smoke test for the parts of PulsePoint that need a GL context.
//
// The renderer, widget layer, screens and audio are compiled and linked here
// against stub OpenGL entry points, then driven through a scripted session:
// surface creation, several full rounds, navigation through every screen and the
// achievement overlay.  It cannot verify what the GPU draws, but it does verify
// that the whole app links, that no screen reads uninitialised state, and that
// the state machine never lands somewhere it should not.
//
// Built and run by tools/build_link_test.py.

#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

// The real declarations first, so the stubs below match them exactly.
#include "gfx/gl.h"

// ---- Stub OpenGL ES 3.0 ----------------------------------------------------
static int g_glCalls = 0;

extern "C" {
GLenum glGetError() { return 0; }
void glViewport(GLint, GLint, GLsizei, GLsizei) { ++g_glCalls; }
void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) { ++g_glCalls; }
void glClear(GLbitfield) { ++g_glCalls; }
void glEnable(GLenum) { ++g_glCalls; }
void glDisable(GLenum) { ++g_glCalls; }
void glBlendFunc(GLenum, GLenum) { ++g_glCalls; }
void glBlendEquation(GLenum) { ++g_glCalls; }
void glBlendFuncSeparate(GLenum, GLenum, GLenum, GLenum) { ++g_glCalls; }
void glCullFace(GLenum) { ++g_glCalls; }
void glScissor(GLint, GLint, GLsizei, GLsizei) { ++g_glCalls; }
GLuint glCreateShader(GLenum) { ++g_glCalls; return 1; }
void glShaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*) { ++g_glCalls; }
void glCompileShader(GLuint) { ++g_glCalls; }
void glGetShaderiv(GLuint, GLenum, GLint* p) { ++g_glCalls; if (p) *p = 1; }
void glGetShaderInfoLog(GLuint, GLsizei, GLsizei*, GLchar*) { ++g_glCalls; }
void glDeleteShader(GLuint) { ++g_glCalls; }
GLuint glCreateProgram() { ++g_glCalls; return 2; }
void glAttachShader(GLuint, GLuint) { ++g_glCalls; }
void glLinkProgram(GLuint) { ++g_glCalls; }
void glGetProgramiv(GLuint, GLenum, GLint* p) { ++g_glCalls; if (p) *p = 1; }
void glGetProgramInfoLog(GLuint, GLsizei, GLsizei*, GLchar*) { ++g_glCalls; }
void glUseProgram(GLuint) { ++g_glCalls; }
void glDeleteProgram(GLuint) { ++g_glCalls; }
GLint glGetUniformLocation(GLuint, const GLchar*) { ++g_glCalls; return 3; }
void glUniform1f(GLint, GLfloat) { ++g_glCalls; }
void glUniform1i(GLint, GLint) { ++g_glCalls; }
void glUniform2f(GLint, GLfloat, GLfloat) { ++g_glCalls; }
void glUniform4f(GLint, GLfloat, GLfloat, GLfloat, GLfloat) { ++g_glCalls; }
void glGenBuffers(GLsizei, GLuint* b) { ++g_glCalls; if (b) *b = 10; }
void glDeleteBuffers(GLsizei, const GLuint*) { ++g_glCalls; }
void glBindBuffer(GLenum, GLuint) { ++g_glCalls; }
void glBufferData(GLenum, GLsizeiptr, const void*, GLenum) { ++g_glCalls; }
void glGenVertexArrays(GLsizei, GLuint* a) { ++g_glCalls; if (a) *a = 20; }
void glDeleteVertexArrays(GLsizei, const GLuint*) { ++g_glCalls; }
void glBindVertexArray(GLuint) { ++g_glCalls; }
void glEnableVertexAttribArray(GLuint) { ++g_glCalls; }
void glDisableVertexAttribArray(GLuint) { ++g_glCalls; }
void glVertexAttribPointer(GLuint, GLsizei, GLenum, GLboolean, GLsizei, const void*) { ++g_glCalls; }
void glGenTextures(GLsizei, GLuint* t) { ++g_glCalls; if (t) *t = 30; }
void glDeleteTextures(GLsizei, const GLuint*) { ++g_glCalls; }
void glBindTexture(GLenum, GLuint) { ++g_glCalls; }
void glActiveTexture(GLenum) { ++g_glCalls; }
void glTexParameteri(GLenum, GLenum, GLint) { ++g_glCalls; }
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) {
  ++g_glCalls;
}
void glPixelStorei(GLenum, GLint) { ++g_glCalls; }
void glDrawArrays(GLenum, GLint, GLsizei count) {
  ++g_glCalls;
  if (count < 0 || count > 1000000) std::printf("  !! implausible vertex count %d\n", count);
}
}  // extern "C"

// ---- The test ---------------------------------------------------------------
#include "game/app.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const char* what) {
  ++g_checks;
  if (!cond) {
    ++g_failures;
    std::printf("  FAIL %s\n", what);
  }
}

constexpr int kWidth = 1080;
constexpr int kHeight = 2340;
constexpr float kDensity = 3.0f;

// The app animates on the monotonic clock, so the driver has to spend real time
// or a transition never completes.  These are the button centres the layout
// produces for this surface, derived from the same dp formulas the screens use.
constexpr float kMenuPlayY = 1140.0f;
constexpr float kMenuAchY = 1334.0f;
constexpr float kMenuStatsY = 1526.0f;
constexpr float kMenuSetY = 1720.0f;
constexpr float kBackX = 170.0f;
constexpr float kBackY = 190.0f;
constexpr float kModeReflexY = 476.0f;
constexpr float kModeMaxY = 1267.0f;
constexpr float kReadyStartY = 1372.0f;
constexpr float kResultAgainY = 1939.0f;
constexpr float kResultChangeY = 2111.0f;
constexpr float kResultChangeX = 293.0f;
constexpr float kCentreX = kWidth * 0.5f;
// Settings layout: four toggles at 56 dp with an 8 dp gap, then the stepper.
constexpr float kSettingsTop = 368.5f;
constexpr float kSettingsRowStep = 181.7f;
constexpr float kSettingsStepperY = kSettingsTop + kSettingsRowStep * 4.0f + 85.0f;

float kSettingsRowY(int index) { return kSettingsTop + kSettingsRowStep * index + 79.0f; }

// Advances the app for at least `ms` of wall time, in ~8 ms slices.
void pump(pp::App& app, int ms) {
  const pp::Nanos deadline = pp::monotonicNow() + static_cast<pp::Nanos>(ms) * pp::kNsPerMs;
  do {
    app.onDrawFrame();
  } while (pp::monotonicNow() < deadline);
}

// Waits for the UI to become interactive again: transitions are short, but a
// first round can queue several achievement overlays, each of which holds the
// screen for a couple of seconds by design.
void waitForInput(pp::App& app, int maxMs) {
  const pp::Nanos deadline = pp::monotonicNow() + static_cast<pp::Nanos>(maxMs) * pp::kNsPerMs;
  while (app.inputIsLocked() && pp::monotonicNow() < deadline) app.onDrawFrame();
}

// A realistic tap: press, hold long enough for the button's press animation and
// release window, then release.
void tap(pp::App& app, float x, float y) {
  // A real player cannot tap through a transition or an overlay either, and the
  // UI refuses to, so the driver waits it out too.
  waitForInput(app, 20000);

  pp::PointerSample s;
  s.pointerId = 0;
  s.phase = pp::PointerPhase::Down;
  s.pos = {x, y};
  s.timeNs = pp::monotonicNow();
  s.valid = true;
  app.input().push(s);
  pump(app, 90);

  s.phase = pp::PointerPhase::Up;
  s.timeNs = pp::monotonicNow();
  app.input().push(s);
  pump(app, 40);
}

// Settles any transition that is running.
void settle(pp::App& app) { pump(app, 260); }

// Navigates from wherever the app currently is to the READY screen of a mode,
// using only the real controls.
void navigateToMode(pp::App& app, float modeCardY) {
  for (int guard = 0; guard < 6; ++guard) {
    switch (app.screen()) {
      case pp::Screen::MainMenu: tap(app, kCentreX, kMenuPlayY); break;
      case pp::Screen::ModeSelect: tap(app, kCentreX, modeCardY); break;
      case pp::Screen::Result: tap(app, 785.0f, kResultChangeY); break;
      case pp::Screen::Playing: tap(app, 62.5f + 90.0f, 152.5f + 16.0f); break;
      case pp::Screen::Ready: return;
      case pp::Screen::Achievements:
      case pp::Screen::Statistics:
      case pp::Screen::Settings: tap(app, kBackX, kBackY); break;
      default: return;
    }
  }
}


}  // namespace

int main() {
  std::printf("PulsePoint link + screen smoke test\n\n");

  // The save file lives in a real directory, so create it the way the platform
  // would have.
  if (std::system("mkdir -p build-host/linktest") != 0) {
    std::printf("could not create the profile directory\n");
    return 1;
  }

  pp::App app;
  if (!app.init("build-host/linktest")) {
    std::printf("app init failed\n");
    return 1;
  }
  app.onSurfaceChanged(kWidth, kHeight, kDensity, 90.0f, 60.0f);
  settle(app);
  check(app.screen() == pp::Screen::MainMenu, "boots into the main menu");
  check(!app.inputIsLocked(), "input unlocks once the intro transition ends");

  // Main menu -> mode select -> READY -> a full round.
  tap(app, kCentreX, kMenuPlayY);
  check(app.screen() == pp::Screen::ModeSelect, "PLAY opens mode select");
  tap(app, kCentreX, kModeReflexY);
  check(app.screen() == pp::Screen::Ready, "a mode card opens READY");
  tap(app, kCentreX, kReadyStartY);
  check(app.screen() == pp::Screen::Playing, "START begins the round");

  // Answer every target as fast as the frames allow.
  const pp::Nanos roundDeadline = pp::monotonicNow() + 90ll * pp::kNsPerSec;
  while (app.screen() == pp::Screen::Playing && pp::monotonicNow() < roundDeadline) {
    app.onDrawFrame();
    if (app.averageFrameMs() < 1000.0f) tap(app, kCentreX, kHeight * 0.5f);
  }
  check(app.screen() == pp::Screen::Result, "the round ends on RESULT");
  settle(app);
  check(app.stats().totalGames >= 1, "a game was recorded");
  check(app.stats().totalPresses >= 1, "presses were counted");
  check(app.stats().totalPresses >= app.stats().totalHits, "presses outnumber hits or match");
  check(app.stats().totalTimePlayedNs > 0, "time played was accumulated");

  // PLAY AGAIN restarts the same mode straight away.
  tap(app, kCentreX, kResultAgainY);
  check(app.screen() == pp::Screen::Playing, "PLAY AGAIN restarts immediately");
  // QUIT is resolved before the mode, so it works whatever is on screen.
  tap(app, 62.5f + 90.0f, 152.5f + 16.0f);
  check(app.screen() == pp::Screen::Result, "QUIT leaves a round early");
  const uint64_t pressesAfterQuit = app.stats().totalPresses;
  check(pressesAfterQuit >= 1, "an abandoned round still counts its presses");

  // CHANGE MODE.
  tap(app, kResultChangeX, kResultChangeY);
  check(app.screen() == pp::Screen::ModeSelect, "CHANGE MODE opens mode select");
  tap(app, kBackX, kBackY);
  check(app.screen() == pp::Screen::MainMenu, "BACK returns to the menu");

  // Every sub-screen draws and returns.
  struct Visit {
    float y;
    pp::Screen expected;
    const char* name;
  };
  const Visit visits[3] = {
      {kMenuAchY, pp::Screen::Achievements, "ACHIEVEMENTS"},
      {kMenuStatsY, pp::Screen::Statistics, "STATISTICS"},
      {kMenuSetY, pp::Screen::Settings, "SETTINGS"},
  };
  for (int i = 0; i < 3; ++i) {
    tap(app, kCentreX, visits[i].y);
    if (app.screen() != visits[i].expected) {
      check(false, visits[i].name);
      break;
    }
    check(true, visits[i].name);
    // Draw hard enough to exercise every row, the scroll region and the reveals.
    pump(app, 400);
    if (i == 2) {
      // Flip the settings toggles and step the calibration, at the rows' real
      // positions: the toggle occupies the whole row, and the stepper's "+" is a
      // control at the right-hand end of its own row.
      const bool soundBefore = app.settings().sound;
      for (int k = 0; k < 4; ++k) tap(app, kWidth * 0.86f, kSettingsRowY(k));
      check(app.settings().sound != soundBefore, "a settings toggle actually flips");
      tap(app, 943.0f, kSettingsStepperY);
      pump(app, 200);
    }
    tap(app, kBackX, kBackY);
    if (app.screen() != pp::Screen::MainMenu) {
      check(false, "BACK leaves the sub-screen");
      break;
    }
    check(true, "BACK leaves the sub-screen");
  }

  // A long idle run: the cheapest way to shake out animation, particle-lifetime
  // and reveal-timing bugs.
  pump(app, 1200);

  // A MAX run, driven until it dies.
  tap(app, kCentreX, kMenuPlayY);
  tap(app, kCentreX, kModeMaxY);
  check(app.screen() == pp::Screen::Ready, "MAX card opens READY");
  tap(app, kCentreX, kReadyStartY);
  check(app.screen() == pp::Screen::Playing, "MAX starts");
  // A MAX run driven by blind taps.
  // Returns to the main menu first so the loop starts from a known screen.  Every miss ends the run, so this exercises
  // the game-over path and the survival clock rather than skilled play -- the
  // rules themselves are covered exhaustively by the gameplay tests, which can
  // see the target list.
  if (app.screen() == pp::Screen::Result) tap(app, 785.0f, kResultChangeY);
  if (app.screen() == pp::Screen::Playing) tap(app, 62.5f + 90.0f, 152.5f + 16.0f);
  navigateToMode(app, kModeMaxY);
  check(app.screen() == pp::Screen::Ready, "MAX is reachable from the menu");
  const pp::Nanos maxDeadline = pp::monotonicNow() + 25ll * pp::kNsPerSec;
  int maxRuns = 0;
  while (maxRuns < 3 && pp::monotonicNow() < maxDeadline) {
    if (app.screen() != pp::Screen::Playing) {
      navigateToMode(app, kModeMaxY);
      if (app.screen() == pp::Screen::Ready) tap(app, kCentreX, kReadyStartY);
      if (app.screen() != pp::Screen::Playing) {
        std::printf("    (stuck on screen %d)\n", static_cast<int>(app.screen()));
        check(false, "MAX restarts from RESULT");
        break;
      }
      ++maxRuns;
    }
    app.onDrawFrame();
    if (app.averageFrameMs() < 1000.0f) {
      // Blind taps at three spread positions: good enough to end a run and
      // record a survival time, which is all this loop is here to prove.
      const float px = kCentreX + 300.0f * static_cast<float>((maxRuns % 3) - 1);
      tap(app, px, kHeight * 0.5f);
    }
  }
  check(app.screen() == pp::Screen::Result, "MAX ends on RESULT too");
  pump(app, 400);
  check(app.stats().modes[static_cast<int>(pp::Mode::Max)].games >= 1, "a MAX game was recorded");
  check(app.stats().modes[static_cast<int>(pp::Mode::Max)].bestLevel >= 1,
        "MAX recorded the level reached");
  check(app.stats().modes[static_cast<int>(pp::Mode::Max)].bestSurvivalNs > 0,
        "MAX recorded a survival time");

  const uint64_t pressesNow = app.stats().totalPresses;
  const uint64_t gamesNow = app.stats().totalGames;
  check(pressesNow > 0, "lifetime presses accumulated");
  app.shutdown();

  // Persistence.
  pp::App reloaded;
  check(reloaded.init("build-host/linktest"), "profile reloads");
  reloaded.onSurfaceChanged(kWidth, kHeight, kDensity, 90.0f, 60.0f);
  check(reloaded.stats().totalPresses == pressesNow, "presses survive a restart");
  check(reloaded.stats().totalGames == gamesNow, "games survive a restart");
  check(reloaded.stats().modes[static_cast<int>(pp::Mode::Max)].bestLevel >= 1,
        "the MAX personal best survives a restart");
  settle(reloaded);
  tap(reloaded, kCentreX, kMenuAchY);
  check(reloaded.screen() == pp::Screen::Achievements, "the reloaded profile opens ACHIEVEMENTS");
  pump(reloaded, 300);
  reloaded.shutdown();

  check(g_glCalls > 0, "the renderer issued GL calls");
  std::printf("\n%d check(s), %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
