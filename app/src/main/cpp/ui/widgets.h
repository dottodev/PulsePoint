// A small immediate-mode widget layer.
//
// Immediate mode with persistent feel: there is no retained widget tree, but
// every interactive element has a stable integer id so its hover, press and
// scroll state survive across frames in a small fixed table.  That keeps the
// immediate-mode advantage -- adding a screen is just drawing it -- without
// rebuilding state every frame.
//
// Scrolling is handled here rather than by the app because a drag has to be able
// to *steal* the gesture: once a finger has moved past the touch slop inside a
// list, the release must not also press whatever button it happens to land on.
// Nothing in this file allocates.
#pragma once

#include "../core/clock.h"
#include "../core/config.h"
#include "../core/math_util.h"
#include "../gfx/renderer.h"
#include "theme.h"

namespace pp {
namespace ui {

// Re-exported so screens do not have to name the gfx namespace for every
// alignment argument.
using gfx::HAlign;
using gfx::VAlign;

struct Frame {
  gfx::Renderer* r = nullptr;
  float density = 1.0f;
  float scale = 1.0f;
  float now = 0.0f;            // seconds since start, for animation phases
  float dt = 0.0f;
  Vec2 pointer{};
  bool pointerDown = false;
  bool pointerPressed = false;   // went down this frame
  bool pointerReleased = false;  // went up this frame
  // Set by the scroll system when the current drag belongs to a list, so
  // buttons ignore the release.
  bool dragCaptured = false;
  // Blocks interaction without blocking drawing: used during transitions and
  // while an achievement overlay is up.
  bool inputLocked = false;
};

enum class Variant : uint8_t {
  Primary,    // Filled white on black: the one obvious action.
  Secondary,  // Outlined.
  Ghost,      // Text only.
  Muted,      // Outlined and dimmed, for the "leave this screen" corner.
};

struct Style {
  Variant variant = Variant::Primary;
  float cornerDp = theme::kButtonRadiusDp;
  float textDp = 0.0f;    // 0 -> derived from the height
  float tracking = theme::kDisplayTrackingWide;
  bool enabled = true;
};

class Ui {
 public:
  void begin(const Frame& f);
  void end();

  // ---- scroll ------------------------------------------------------------
  // Opens a scroll region.  Returns the current vertical offset in pixels.
  float beginScroll(int id, Rect viewport, float contentHeight);
  void endScroll();
  float scrollOffset() const { return scrollOffset_; }

  // ---- primitives --------------------------------------------------------
  void text(Vec2 at, const char* str, float sizeDp, Color color, HAlign halign = HAlign::Left,
            VAlign valign = VAlign::Middle, bool display = false, float tracking = 0.0f);
  void textMono(Vec2 at, const char* str, float sizeDp, Color color, HAlign halign = HAlign::Left,
                VAlign valign = VAlign::Middle);
  void textDisplay(Vec2 at, const char* str, float sizeDp, Color color, HAlign halign = HAlign::Left,
                   VAlign valign = VAlign::Middle, float tracking = theme::kDisplayTracking);
  void measure(const char* str, float sizeDp, bool display, float tracking, float* outWidth,
               float* outAscent = nullptr, float* outDescent = nullptr);

  // ---- widgets -----------------------------------------------------------
  // Returns true on the frame the press is released inside the button.
  bool button(int id, Rect bounds, const char* label, const Style& style);
  // A hit area with no chrome of its own, for rows that draw their own controls.
  // The press animation is still available through pressOf().
  bool tapArea(int id, Rect bounds);
  // The small ON/OFF indicator used by the settings rows.
  void togglePill(Rect bounds, bool on);
  // A compact - / + pair for numeric settings.  Returns true if it changed.
  bool stepperControl(int id, Rect bounds, int* value, int minValue, int maxValue);
  // The readout between a stepper's two buttons.
  void stepperValue(Rect bounds, const char* valueText);

  void panel(Rect bounds, float cornerDp = theme::kCardRadiusDp, Color fill = theme::kSurface,
             float borderPx = 0.0f, Color borderColor = theme::kHairline);
  // Greedy word wrap, one line per visual row, anchored at the top centre of
  // at.  Returns the height consumed so the caller can place what follows
  // without having to know the line count in advance.  Long mode blurbs are the
  // reason this exists: a single centred line of prose either overflows the
  // screen on both sides or has to be shrunk until it is unreadable.
  float textParagraph(Vec2 at, const char* str, float sizeDp, Color color, float maxWidth,
                      float lineHeightDp, bool display = false, float tracking = 0.0f);

  // A settings row: label, caption, and a right-hand control.
  void settingsRow(Rect bounds, const char* label, const char* caption);
  void divider(float x0, float x1, float y, Color color = theme::kHairline);
  void progressBar(Rect bounds, float t, Color fill = theme::kBright,
                   Color track = theme::kSurfaceRaised);
  void statRow(Rect bounds, const char* label, const char* value, Color valueColor = theme::kText,
               float valueSizeDp = 0.0f);
  void progressLine(Rect bounds, const char* label, double current, double target,
                    const char* unitLabel);
  void barChart(Rect bounds, const float* values, int count, float maxValue, Color fill);
  // A small square used as a separator between inline label/value groups; the
  // faces do not carry a middot, so the separator is geometry.
  void dot(Vec2 at, float sizeDp, Color c);

  // ---- helpers -----------------------------------------------------------
  float dp(float value) const { return value * density_ * scale_; }
  float width() const { return frame_.r != nullptr ? frame_.r->width() : 0.0f; }
  float height() const { return frame_.r != nullptr ? frame_.r->height() : 0.0f; }
  bool pointerIn(Rect r) const { return frame_.pointerDown && r.contains(frame_.pointer); }
  bool pointerDownIn(Rect r) const {
    return frame_.pointerPressed && !frame_.dragCaptured && r.contains(frame_.pointer);
  }
  bool pointerUpIn(Rect r) const {
    return frame_.pointerReleased && !frame_.dragCaptured && r.contains(frame_.pointer);
  }
  bool blocked() const { return frame_.inputLocked; }
  const Frame& frame() const { return frame_; }
  gfx::Renderer* renderer() const { return frame_.r; }

  // 0..1 press amount for a widget, for callers that draw their own chrome.
  float pressOf(int id);

 private:
  struct Slot {
    int id = 0;
    bool used = false;
    bool down = false;
    float press = 0.0f;
    float hover = 0.0f;
    float scroll = 0.0f;
    float scrollVel = 0.0f;
    float lastTouch = -100.0f;
  };

  static constexpr int kMaxSlots = 64;
  Slot* slotFor(int id, bool create);

  Frame frame_{};
  float density_ = 1.0f;
  float scale_ = 1.0f;
  Slot slots_[kMaxSlots] = {};

  // Scroll gesture state.
  bool scrollOpen_ = false;
  bool scrollCandidate_ = false;
  bool dragCaptured_ = false;
  float dragStartY_ = 0.0f;
  float dragLastY_ = 0.0f;
  float scrollOffset_ = 0.0f;
  float scrollMax_ = 0.0f;
};

}  // namespace ui
}  // namespace pp
