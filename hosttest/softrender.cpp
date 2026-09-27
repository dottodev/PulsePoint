// Records the game's real draw calls so they can be rasterised on a desktop.
//
// The stubs below are the only difference between this and the link test: instead
// of counting GL calls, they keep the contents of every buffer upload and dump
// the vertices of every draw call.  Feeding those to tools/softrender.py produces
// PNGs of the actual game -- the same layout code, the same colours, the same
// text, evaluated with a CPU reference of the same fragment shaders.
//
// It cannot tell you whether the GPU agrees with the maths.  It can tell you
// whether a button is off screen, two labels overlap, the contrast is wrong, or
// the title is clipped, which is most of what actually goes wrong in a UI.
//
// Usage:  ./build-host/pulsepoint_softrender <output-dir>
//         python3 tools/softrender.py <output-dir>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gfx/gl.h"

// ---------------------------------------------------------------------------
// Recorder
// ---------------------------------------------------------------------------

namespace {

struct Buffer {
  std::vector<uint8_t> data;
};

std::vector<Buffer> g_buffers(4);
int g_boundBuffer = -1;
int g_program = 0;
std::string g_outDir;
int g_draw = 0;
int g_framesDrawn = 0;
int g_width = 0;
int g_height = 0;

// Only the most recent frame is kept: the app runs a few thousand frames before
// each capture, and writing all of them would be pointless.  The renderer issues
// exactly two draws per frame, which is also the property worth asserting.
std::vector<uint8_t> g_latest[2];
int g_latestCounts[2] = {0, 0};
std::string g_captureName;

void beginFrame(int width, int height) {
  g_width = width;
  g_height = height;
  g_draw = 0;
}

// Writes the frame currently held.  Called by the driver at the moments worth
// looking at, not on every frame.
void capture(const char* name) {
  for (int pass = 0; pass < 2; ++pass) {
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s_pass%d.bin", g_outDir.c_str(), name, pass);
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) continue;
    if (!g_latest[pass].empty()) {
      std::fwrite(g_latest[pass].data(), 1, g_latest[pass].size(), f);
    }
    std::fclose(f);
  }
  std::printf("captured %s  (%d shapes, %d glyphs)\n", name, g_latestCounts[0] / 6,
              g_latestCounts[1] / 6);
}

void recordDraw(int first, int count) {
  const int pass = g_draw;
  if (pass > 1) return;
  if (g_boundBuffer < 0 || g_boundBuffer >= static_cast<int>(g_buffers.size())) return;
  if (count <= 0) return;
  const std::vector<uint8_t>& src = g_buffers[static_cast<size_t>(g_boundBuffer)].data;
  const size_t bytes = static_cast<size_t>(count) * 64u;
  const size_t offset = static_cast<size_t>(first) * 64u;
  if (src.size() < offset + bytes) return;
  g_latest[pass].assign(src.begin() + static_cast<long>(offset),
                        src.begin() + static_cast<long>(offset + bytes));
  g_latestCounts[pass] = count;
}

}  // namespace

extern "C" {
GLenum glGetError() { return 0; }
void glViewport(GLint, GLint, GLsizei, GLsizei) {}
void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glClear(GLbitfield) {}
void glEnable(GLenum) {}
void glDisable(GLenum) {}
void glBlendFunc(GLenum, GLenum) {}
void glBlendEquation(GLenum) {}
void glBlendFuncSeparate(GLenum, GLenum, GLenum, GLenum) {}
void glCullFace(GLenum) {}
void glScissor(GLint, GLint, GLsizei, GLsizei) {}
GLuint glCreateShader(GLenum) { return 1; }
void glShaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*) {}
void glCompileShader(GLuint) {}
void glGetShaderiv(GLuint, GLenum, GLint* p) { if (p) *p = 1; }
void glGetShaderInfoLog(GLuint, GLsizei, GLsizei*, GLchar*) {}
void glDeleteShader(GLuint) {}
GLuint glCreateProgram() { return 2; }
void glAttachShader(GLuint, GLuint) {}
void glLinkProgram(GLuint) {}
void glGetProgramiv(GLuint, GLenum, GLint* p) { if (p) *p = 1; }
void glGetProgramInfoLog(GLuint, GLsizei, GLsizei*, GLchar*) {}
void glDeleteProgram(GLuint) {}
GLint glGetUniformLocation(GLuint, const GLchar*) { return 3; }
void glUniform1f(GLint, GLfloat) {}
void glUniform1i(GLint, GLint) {}
void glUniform2f(GLint, GLfloat, GLfloat) {}
void glUniform4f(GLint, GLfloat, GLfloat, GLfloat, GLfloat) {}

void glGenBuffers(GLsizei n, GLuint* b) {
  for (GLsizei i = 0; i < n; ++i) b[i] = static_cast<GLuint>(g_buffers.size() + i);
  g_buffers.resize(g_buffers.size() + static_cast<size_t>(n));
}
void glDeleteBuffers(GLsizei, const GLuint*) {}
void glBindBuffer(GLenum, GLuint buffer) { g_boundBuffer = static_cast<int>(buffer); }
void glBufferData(GLenum, GLsizeiptr size, const void* data, GLenum) {
  if (g_boundBuffer < 0 || g_boundBuffer >= static_cast<int>(g_buffers.size())) return;
  Buffer& b = g_buffers[static_cast<size_t>(g_boundBuffer)];
  b.data.assign(static_cast<size_t>(size), 0);
  if (data != nullptr && size > 0) std::memcpy(b.data.data(), data, static_cast<size_t>(size));
}

void glGenVertexArrays(GLsizei, GLuint* a) { if (a) *a = 0; }
void glDeleteVertexArrays(GLsizei, const GLuint*) {}
void glBindVertexArray(GLuint) {}
void glEnableVertexAttribArray(GLuint) {}
void glDisableVertexAttribArray(GLuint) {}
void glVertexAttribPointer(GLuint, GLsizei, GLenum, GLboolean, GLsizei, const void*) {}
void glGenTextures(GLsizei n, GLuint* t) {
  for (GLsizei i = 0; i < n; ++i) t[i] = 0;
}
void glDeleteTextures(GLsizei, const GLuint*) {}
void glBindTexture(GLenum, GLuint) {}
void glActiveTexture(GLenum) {}
void glTexParameteri(GLenum, GLenum, GLint) {}
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) {}
void glPixelStorei(GLenum, GLint) {}

void glUseProgram(GLuint program) { g_program = static_cast<int>(program); }

void glDrawArrays(GLenum, GLint first, GLsizei count) {
  (void)g_program;
  // Pass 0 is the shape batch, pass 1 the text batch, matching Renderer::endFrame.
  recordDraw(first, count);
  if (g_draw >= 1) ++g_framesDrawn;
  g_draw = (g_draw + 1) % 2;
}
}  // extern "C"

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

#include "game/app.h"

namespace {

constexpr int kWidth = 1080;
constexpr int kHeight = 2340;
constexpr float kDensity = 3.0f;

constexpr float kCentreX = kWidth * 0.5f;
constexpr float kMenuPlayY = 1140.0f;
constexpr float kMenuAchY = 1334.0f;
constexpr float kMenuStatsY = 1526.0f;
constexpr float kMenuSetY = 1720.0f;
constexpr float kBackX = 170.0f;
constexpr float kBackY = 190.0f;
constexpr float kQuitX = 151.0f;
constexpr float kQuitY = 198.0f;
// Centre of the SHOW FPS row on the settings screen, measured off a render:
// the row dividers sit at y = 658, 840, 1022, 1204, so the fourth row spans
// 1022..1204.  The whole row is the tap target, so the middle is plenty.
constexpr float kSetFpsRowY = 1113.0f;
constexpr float kModeReflexY = 476.0f;
constexpr float kReadyStartY = 1372.0f;

void pump(pp::App& app, int ms) {
  const pp::Nanos deadline = pp::monotonicNow() + static_cast<pp::Nanos>(ms) * pp::kNsPerMs;
  do {
    app.onDrawFrame();
  } while (pp::monotonicNow() < deadline);
}

void waitForInput(pp::App& app, int maxMs) {
  const pp::Nanos deadline = pp::monotonicNow() + static_cast<pp::Nanos>(maxMs) * pp::kNsPerMs;
  while (app.inputIsLocked() && pp::monotonicNow() < deadline) app.onDrawFrame();
}

void tap(pp::App& app, float x, float y) {
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

// A tap that captures while the transition is still young.  A press only
// becomes a release 55 ms later (kTapHoldNs: the UI holds the pointer so a
// tap reads as a tap and not a brush), so the transition is 55 ms younger
// than the trailing pump suggests; 110 ms of it puts the wipe about a third
// of the way across, band bright and edge lines clean.
void tapQuick(pp::App& app, float x, float y) {
  waitForInput(app, 20000);
  pp::PointerSample s;
  s.pointerId = 0;
  s.phase = pp::PointerPhase::Down;
  s.pos = {x, y};
  s.timeNs = pp::monotonicNow();
  s.valid = true;
  app.input().push(s);
  pump(app, 10);
  s.phase = pp::PointerPhase::Up;
  s.timeNs = pp::monotonicNow();
  app.input().push(s);
  pump(app, 110);
}

}  // namespace

int main(int argc, char** argv) {
  g_outDir = argc > 1 ? argv[1] : "build-host/frames";
  std::string mk = "mkdir -p " + g_outDir;
  if (std::system(mk.c_str()) != 0) {
    std::printf("could not create %s\n", g_outDir.c_str());
    return 1;
  }
  if (std::system(("rm -f " + g_outDir + "/*.bin " + g_outDir + "/*.png").c_str()) != 0) {
    return 1;
  }

  pp::App app;
  if (!app.init("build-host/softrender")) return 1;
  app.onSurfaceChanged(kWidth, kHeight, kDensity, 90.0f, 60.0f);
  beginFrame(kWidth, kHeight);

  // Menu, captured a little after the intro so the layout is settled.
  pump(app, 700);
  capture("01-menu");

  // Mode select.
  tap(app, kCentreX, kMenuPlayY);
  pump(app, 400);
  capture("02-modes");

  // Ready.
  tap(app, kCentreX, kModeReflexY);
  pump(app, 400);
  capture("03-ready");

  // Gameplay, with a target on screen.  The oracle taps only when a target is
  // live, so a frame is captured mid-stimulus.
  tap(app, kCentreX, kReadyStartY);
  pump(app, 400);
  // REFLEX hides its target for a randomised delay of over a second, so the
  // captures have to be spaced in wall time rather than in frames: this host has
  // no vsync, so 500 frames is a few milliseconds.
  for (int k = 0; k < 6; ++k) {
    pump(app, 420);
    if (app.screen() != pp::Screen::Playing) break;
    char name[32];
    std::snprintf(name, sizeof(name), "04-gameplay-%d", k);
    capture(name);
  }

  // Play the round out rather than quitting it, so the result screen is captured
  // with real data in it.  REFLEX always puts its target at the centre, so a
  // periodic tap answers it; the taps that land during the delay are false
  // starts, which is exactly the behaviour the result screen has to display.
  for (int k = 0; k < 70 && app.screen() == pp::Screen::Playing; ++k) {
    pump(app, 320);
    if (app.screen() == pp::Screen::Playing) tap(app, kCentreX, kHeight * 0.5f);
  }
  // The first unlock overlay fires as soon as the round ends.  Half a second in
  // the frame, the rings and the burst are all composed at once, and the queue
  // does not advance on its own, so this is a stable moment to capture.
  pump(app, 500);
  if (app.inputIsLocked()) {
    capture("09-overlay");
  } else {
    std::printf("no overlay active; 09-overlay skipped\n");
  }
  waitForInput(app, 30000);
  pump(app, 1600);
  capture("05-result");

  // Sub-screens.  The wipe is caught with a quick tap so the band is still
  // bright: at a tenth of the way across the edge lines are at half alpha.
  if (app.screen() == pp::Screen::Result) tapQuick(app, 785.0f, 2111.0f);
  capture("10-wipe");
  pump(app, 300);
  tap(app, kCentreX, kMenuAchY);
  pump(app, 500);
  capture("06-achievements");

  tap(app, kBackX, kBackY);
  pump(app, 300);
  tap(app, kCentreX, kMenuStatsY);
  pump(app, 500);
  capture("07-statistics");

  tap(app, kBackX, kBackY);
  pump(app, 300);
  tap(app, kCentreX, kMenuSetY);
  pump(app, 500);
  capture("08-settings");

  // Flip SHOW FPS on and prove the debug line coexists with a footer.
  tap(app, kCentreX, kSetFpsRowY);
  tap(app, kBackX, kBackY);
  pump(app, 500);
  capture("11-debug");

  // The other three modes, as the player sees them.  Card centres measured off
  // the mode-select render (pitch 264 px): Flick 850, Focus 1114, Max 1378.
  // Flick spawns fast enough that two spaced captures almost always catch live
  // targets; Focus holds its field still.  Each visit ends by quitting, which
  // also proves the abort path lands on a result screen instead of stranding
  // the round -- and the Max one shows the SURVIVED line with real data.
  const char* visitCaps[] = {"12-flick", "13-focus", "14-max"};
  const float visitCards[] = {850.0f, 1114.0f, 1378.0f};
  // Milliseconds from START to the first capture.  Focus holds its first
  // field for 700 ms and Max for 1200 ms, so those need a longer warm-up to
  // be caught with a live target.
  const int visitWarmMs[] = {500, 500, 1400};
  for (int vi = 0; vi < 3; ++vi) {
    tap(app, kCentreX, kMenuPlayY);
    pump(app, 400);
    if (app.screen() != pp::Screen::ModeSelect) {
      std::printf("visit %d: not on mode select; skipped\n", vi);
      continue;
    }
    tap(app, kCentreX, visitCards[vi]);
    pump(app, 400);
    // The ready screen for every mode, not just Reflex: three more blurbs
    // through the paragraph wrapper, three more rules lines.
    if (app.screen() == pp::Screen::Ready) {
      char readyName[32];
      std::snprintf(readyName, sizeof(readyName), "%s-ready", visitCaps[vi]);
      capture(readyName);
    }
    tap(app, kCentreX, kReadyStartY);
    pump(app, visitWarmMs[vi]);
    char name[32];
    for (int k = 0; k < 2; ++k) {
      if (app.screen() != pp::Screen::Playing) break;
      std::snprintf(name, sizeof(name), "%s-%d", visitCaps[vi], k);
      capture(name);
      pump(app, 500);
    }
    if (app.screen() == pp::Screen::Playing) {
      tap(app, kQuitX, kQuitY);
      // The result reveal staggers rows over about a second; capture it
      // settled, not mid-animation.
      pump(app, 1200);
    }
    if (app.screen() == pp::Screen::Result) {
      std::snprintf(name, sizeof(name), "%s-result", visitCaps[vi]);
      capture(name);
      tap(app, 785.0f, 2111.0f);
      pump(app, 400);
    }
  }

  app.shutdown();
  std::printf("drew %d frames total; captures written to %s\n", g_framesDrawn, g_outDir.c_str());
  return 0;
}
