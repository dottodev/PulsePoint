#include "widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace pp {
namespace ui {

namespace {

// A finger has to move this far before a drag becomes a scroll rather than a
// tap.  Matches the platform's own touch slop, so the two never disagree.
constexpr float kDragSlopDp = 9.0f;
// Fling decay.  Short enough that a flick settles before the player reads it as
// a new position.
constexpr float kFlingHalfLife = 0.055f;

}  // namespace

void Ui::begin(const Frame& f) {
  frame_ = f;
  density_ = f.density > 0.0f ? f.density : 1.0f;
  scale_ = f.scale > 0.0f ? f.scale : 1.0f;
  scrollOpen_ = false;
}

void Ui::end() {
  for (int i = 0; i < kMaxSlots; ++i) {
    Slot& s = slots_[i];
    if (!s.used) continue;
    if (frame_.now - s.lastTouch > 5.0f && s.press < 0.002f && s.hover < 0.002f &&
        std::fabs(s.scrollVel) < 1.0f) {
      s.used = false;
    }
  }
  scrollOpen_ = false;
  dragCaptured_ = false;
}

Ui::Slot* Ui::slotFor(int id, bool create) {
  for (int i = 0; i < kMaxSlots; ++i) {
    if (slots_[i].used && slots_[i].id == id) return &slots_[i];
  }
  if (!create) return nullptr;
  for (int i = 0; i < kMaxSlots; ++i) {
    if (!slots_[i].used) {
      slots_[i] = Slot{};
      slots_[i].used = true;
      slots_[i].id = id;
      return &slots_[i];
    }
  }
  // Full: reuse the least recently touched slot.  With 64 slots and a dozen
  // live widgets this never happens, and degrading beats dropping interaction.
  int oldest = 0;
  for (int i = 1; i < kMaxSlots; ++i) {
    if (slots_[i].lastTouch < slots_[oldest].lastTouch) oldest = i;
  }
  slots_[oldest] = Slot{};
  slots_[oldest].used = true;
  slots_[oldest].id = id;
  return &slots_[oldest];
}

float Ui::pressOf(int id) {
  Slot* s = slotFor(id, false);
  return s != nullptr ? s->press : 0.0f;
}

// ---------------------------------------------------------------------------
// Scroll
// ---------------------------------------------------------------------------

float Ui::beginScroll(int id, Rect viewport, float contentHeight) {
  scrollOpen_ = true;
  scrollMax_ = std::max(0.0f, contentHeight - viewport.h);

  Slot* s = slotFor(id, true);
  s->lastTouch = frame_.now;

  const float slop = dp(kDragSlopDp);
  if (frame_.pointerPressed && !frame_.inputLocked && viewport.contains(frame_.pointer)) {
    scrollCandidate_ = true;
    dragCaptured_ = false;
    dragStartY_ = frame_.pointer.y;
    dragLastY_ = frame_.pointer.y;
    s->scrollVel = 0.0f;
  } else if (frame_.pointerDown && scrollCandidate_) {
    const float moved = frame_.pointer.y - dragStartY_;
    if (std::fabs(moved) > slop) {
      dragCaptured_ = true;
      scrollCandidate_ = false;
      dragLastY_ = frame_.pointer.y;
    }
  }

  if (dragCaptured_ && frame_.pointerDown) {
    const float delta = dragLastY_ - frame_.pointer.y;
    dragLastY_ = frame_.pointer.y;
    s->scroll += delta;
    s->scrollVel = delta / std::max(frame_.dt, 1.0f / 240.0f);
  } else if (!frame_.pointerDown) {
    if (scrollCandidate_) {
      scrollCandidate_ = false;
    } else {
      // Inertia, and it dies fast.
      const float move = s->scrollVel * frame_.dt;
      s->scroll += move;
      s->scrollVel = damp(s->scrollVel, 0.0f, kFlingHalfLife, frame_.dt);
      if (std::fabs(s->scrollVel) < 4.0f) s->scrollVel = 0.0f;
    }
  }

  s->scroll = clampf(s->scroll, 0.0f, scrollMax_);
  // Rubber band at the ends, released over the next frames.
  scrollOffset_ = s->scroll;
  return scrollOffset_;
}

void Ui::endScroll() {
  if (scrollOpen_ && !frame_.pointerDown) scrollCandidate_ = false;
  scrollOpen_ = false;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

void Ui::measure(const char* str, float sizeDp, bool display, float tracking, float* outWidth,
                 float* outAscent, float* outDescent) {
  if (frame_.r == nullptr || str == nullptr) {
    if (outWidth != nullptr) *outWidth = 0.0f;
    if (outAscent != nullptr) *outAscent = 0.0f;
    if (outDescent != nullptr) *outDescent = 0.0f;
    return;
  }
  gfx::TextStyle style;
  style.face = display ? &theme::display() : &theme::tech();
  style.sizePx = dp(sizeDp);
  style.tracking = tracking;
  const gfx::TextMetrics m = frame_.r->measure(str, style);
  if (outWidth != nullptr) *outWidth = m.width;
  if (outAscent != nullptr) *outAscent = m.ascent;
  if (outDescent != nullptr) *outDescent = m.descent;
}

void Ui::text(Vec2 at, const char* str, float sizeDp, Color color, HAlign halign, VAlign valign,
              bool display, float tracking) {
  if (frame_.r == nullptr || str == nullptr || str[0] == '\0') return;
  gfx::TextStyle style;
  style.face = display ? &theme::display() : &theme::tech();
  style.sizePx = dp(sizeDp);
  style.tracking = tracking;
  style.color = color;
  frame_.r->text(at.x, at.y, str, style, halign, valign);
}

void Ui::textMono(Vec2 at, const char* str, float sizeDp, Color color, HAlign halign,
                  VAlign valign) {
  text(at, str, sizeDp, color, halign, valign, false, theme::kTechTracking);
}

void Ui::textDisplay(Vec2 at, const char* str, float sizeDp, Color color, HAlign halign,
                     VAlign valign, float tracking) {
  text(at, str, sizeDp, color, halign, valign, true, tracking);
}

// ---------------------------------------------------------------------------
// Chrome
// ---------------------------------------------------------------------------

float Ui::textParagraph(Vec2 at, const char* str, float sizeDp, Color color, float maxWidth,
                        float lineHeightDp, bool display, float tracking) {
  if (frame_.r == nullptr || str == nullptr || str[0] == '\0' || maxWidth <= 0.0f) return 0.0f;
  const float lh = dp(lineHeightDp);

  // No allocation: a line is assembled in a fixed buffer, which is ample for the
  // one-sentence blurbs this is called with and is checked anyway.
  constexpr size_t kMaxLine = 192;
  char line[kMaxLine];
  size_t len = 0;
  int lines = 0;

  auto flush = [&] {
    if (len == 0) return;
    line[len] = '\0';
    text({at.x, at.y + static_cast<float>(lines) * lh + lh * 0.5f}, line, sizeDp, color,
         HAlign::Center, VAlign::Middle, display, tracking);
    ++lines;
    len = 0;
  };
  // The width of the whole candidate line, which is the only width the decision
  // needs: accumulating per-word widths double-counts everything but the last.
  auto widthOf = [&](const char* s, size_t n) {
    char probe[kMaxLine];
    if (n >= kMaxLine) return maxWidth + 1.0f;  // Unsplittable: force a break.
    std::memcpy(probe, s, n);
    probe[n] = '\0';
    float w = 0.0f;
    measure(probe, sizeDp, display, tracking, &w, nullptr, nullptr);
    return w;
  };

  const char* p = str;
  while (*p != '\0') {
    while (*p == ' ') ++p;
    if (*p == '\0') break;
    const char* wordEnd = p;
    while (*wordEnd != '\0' && *wordEnd != ' ') ++wordEnd;
    const size_t wordLen = static_cast<size_t>(wordEnd - p);

    if (len != 0 && len + 1 + wordLen < kMaxLine) {
      line[len] = ' ';
      const float w = widthOf(line, len + 1 + wordLen);
      if (w > maxWidth) {
        flush();
        if (wordLen < kMaxLine) {
          std::memcpy(line, p, wordLen);
          len = wordLen;
        }
        p = wordEnd;
        continue;
      }
      std::memcpy(line + len + 1, p, wordLen);
      len += 1 + wordLen;
    } else if (len == 0 && wordLen < kMaxLine) {
      std::memcpy(line, p, wordLen);
      len = wordLen;
    }
    p = wordEnd;
  }
  flush();
  return static_cast<float>(lines) * lh;
}

void Ui::dot(Vec2 at, float sizeDp, Color c) {
  if (frame_.r == nullptr) return;
  const float s = dp(sizeDp);
  frame_.r->rect(Rect::fromCenter(at, s, s), c, 0.0f);
}

void Ui::panel(Rect bounds, float cornerDp, Color fill, float borderPx, Color borderColor) {
  if (frame_.r == nullptr) return;
  // A border is a ring: the renderer's stroke mode cuts the shape back by the
  // border width, so drawing the ring and then the fill underneath gives a
  // clean one-pixel edge at any density.
  if (borderPx > 0.0f) frame_.r->rect(bounds, borderColor, dp(cornerDp), borderPx, 0.0f);
  const Rect inner = borderPx > 0.0f ? bounds.inset(borderPx) : bounds;
  frame_.r->rect(inner, fill, dp(cornerDp));
}

void Ui::divider(float x0, float x1, float y, Color color) {
  if (frame_.r == nullptr) return;
  frame_.r->rect({x0, y - 0.5f, x1 - x0, 1.0f}, color);
}

void Ui::progressBar(Rect bounds, float t, Color fill, Color track) {
  if (frame_.r == nullptr) return;
  const float radius = bounds.h * 0.5f;
  frame_.r->rect(bounds, track, radius);
  const float clamped = clamp01(t);
  if (clamped <= 0.0f) return;
  const float w = std::max(bounds.h * 0.95f, bounds.w * clamped);
  frame_.r->rect({bounds.x, bounds.y, w, bounds.h}, fill, radius);
}

void Ui::statRow(Rect bounds, const char* label, const char* value, Color valueColor,
                 float valueSizeDp) {
  const float labelSize = cfg::kBodySizeDp;
  const float valueSize = valueSizeDp > 0.0f ? valueSizeDp : cfg::kBodySizeDp;
  text({bounds.x, bounds.centerY()}, label, labelSize, theme::kTextDim, HAlign::Left, VAlign::Middle,
       false, theme::kTechTracking);
  text({bounds.right(), bounds.centerY()}, value, valueSize, valueColor, HAlign::Right,
       VAlign::Middle, false, theme::kTechTracking);
}

void Ui::progressLine(Rect bounds, const char* label, double current, double target,
                      const char* unitLabel) {
  char buf[112];
  if (target > 0.0) {
    std::snprintf(buf, sizeof(buf), "%s   %lld / %lld %s", label, static_cast<long long>(current),
                  static_cast<long long>(target), unitLabel != nullptr ? unitLabel : "");
  } else {
    std::snprintf(buf, sizeof(buf), "%s   %s", label,
                  current > 0.0 ? "COMPLETE" : "LOCKED");
  }
  textMono({bounds.x, bounds.centerY()}, buf, cfg::kSmallSizeDp, theme::kTextFaint, HAlign::Left,
           VAlign::Middle);
}

void Ui::barChart(Rect bounds, const float* values, int count, float maxValue, Color fill) {
  if (frame_.r == nullptr || count <= 0 || values == nullptr) return;
  const float top = maxValue > 0.0f ? maxValue : 1.0f;
  const float gap = dp(1.5f);
  const float bw = (bounds.w - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
  for (int i = 0; i < count; ++i) {
    const float v = clamp01(values[i] / top);
    const float h = std::max(dp(1.5f), bounds.h * v);
    const float x = bounds.x + static_cast<float>(i) * (bw + gap);
    frame_.r->rect({x, bounds.bottom() - h, bw, h}, fill, dp(0.5f));
  }
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

bool Ui::button(int id, Rect bounds, const char* label, const Style& style) {
  if (frame_.r == nullptr) return false;
  Slot* s = slotFor(id, true);
  s->lastTouch = frame_.now;

  const bool enabled = style.enabled && !frame_.inputLocked;
  const Rect hit = bounds.expanded(dp(4.0f));
  const bool canClick = enabled && !frame_.dragCaptured;

  bool clicked = false;
  if (canClick) {
    if (frame_.pointerPressed && hit.contains(frame_.pointer)) {
      s->down = true;
    } else if (frame_.pointerReleased) {
      if (s->down && hit.contains(frame_.pointer)) clicked = true;
      s->down = false;
    }
  } else {
    s->down = false;
  }

  const bool hovered = enabled && hit.contains(frame_.pointer);
  // Very short time constants: 45 ms in, 70 ms out.  Anything slower reads as
  // input lag in a game about reaction speed.
  s->press = damp(s->press, s->down ? 1.0f : 0.0f, 0.020f, frame_.dt);
  s->hover = damp(s->hover, hovered && !s->down ? 1.0f : 0.0f, hovered ? 0.045f : 0.070f, frame_.dt);

  const float press = s->press;
  const float scale = 1.0f - 0.035f * press;
  const Rect b = Rect::fromCenter(bounds.center(), bounds.w * scale, bounds.h * scale);

  Color fill = theme::kVoid;
  Color stroke = theme::kVoid;
  Color fg = theme::kText;
  float strokePx = 0.0f;

  switch (style.variant) {
    case Variant::Primary:
      fill = lerpColor(theme::kBright, theme::kHighlight, press);
      fg = theme::kVoid;
      break;
    case Variant::Secondary:
      stroke = lerpColor(theme::kHairlineStrong, theme::kTextDim, s->hover);
      strokePx = dp(1.0f);
      fg = lerpColor(theme::kText, theme::kBright, s->hover);
      break;
    case Variant::Ghost:
      fg = lerpColor(theme::kTextDim, theme::kBright, std::max(s->hover, press));
      break;
    case Variant::Muted:
      stroke = theme::kHairline;
      strokePx = dp(1.0f);
      fg = lerpColor(theme::kTextFaint, theme::kTextDim, std::max(s->hover, press));
      break;
  }
  if (!style.enabled) {
    fill = theme::kVoid;
    stroke = theme::kHairline;
    strokePx = dp(1.0f);
    fg = theme::kTextGhost;
  }

  if (style.variant != Variant::Ghost) {
    if (fill.a > 0.0f) frame_.r->rect(b, fill, dp(style.cornerDp));
    if (strokePx > 0.0f) frame_.r->rect(b, stroke, dp(style.cornerDp), strokePx, 0.0f);
  }

  const float textSize = style.textDp > 0.0f ? style.textDp : bounds.h / (density_ * scale_) * 0.30f;
  text({b.centerX(), b.centerY() + dp(0.5f)}, label, textSize, fg, HAlign::Center, VAlign::Middle,
       true, style.tracking);
  return clicked;
}

bool Ui::tapArea(int id, Rect bounds) {
  Slot* s = slotFor(id, true);
  s->lastTouch = frame_.now;
  const bool enabled = !frame_.inputLocked;
  const Rect hit = bounds.expanded(dp(4.0f));
  const bool canClick = enabled && !frame_.dragCaptured;
  bool clicked = false;
  if (canClick) {
    if (frame_.pointerPressed && hit.contains(frame_.pointer)) {
      s->down = true;
    } else if (frame_.pointerReleased) {
      if (s->down && hit.contains(frame_.pointer)) clicked = true;
      s->down = false;
    }
  } else {
    s->down = false;
  }
  s->press = damp(s->press, s->down ? 1.0f : 0.0f, 0.020f, frame_.dt);
  s->hover = damp(s->hover, (enabled && hit.contains(frame_.pointer) && !s->down) ? 1.0f : 0.0f,
                  0.045f, frame_.dt);
  return clicked;
}

void Ui::togglePill(Rect bounds, bool on) {
  if (frame_.r == nullptr) return;
  const float h = std::min(bounds.h, dp(30.0f));
  const Rect pill{bounds.right() - dp(58.0f), bounds.centerY() - h * 0.5f, dp(58.0f), h};
  // Off is a dim outline, on is filled: the same primary/secondary language the
  // buttons use, at a size that fits inside a settings row.
  if (on) {
    frame_.r->rect(pill, theme::kBright, h * 0.5f);
    text({pill.centerX(), pill.centerY()}, "ON", 10.0f, theme::kVoid, HAlign::Center,
         VAlign::Middle, true, theme::kDisplayTracking);
  } else {
    frame_.r->rect(pill, theme::kVoid, h * 0.5f);
    frame_.r->rect(pill, theme::kHairlineStrong, h * 0.5f, dp(1.0f), dp(1.0f));
    text({pill.centerX(), pill.centerY()}, "OFF", 10.0f, theme::kTextFaint, HAlign::Center,
         VAlign::Middle, true, theme::kDisplayTracking);
  }
}

namespace {
// One layout for the stepper, shared by the control and its readout so the value
// text can never end up centred on a different box than the one that was drawn.
struct StepperGeom {
  Rect minus;
  Rect value;
  Rect plus;
};
StepperGeom stepperGeom(Rect bounds, float scale) {
  const float s = std::min(bounds.h, 34.0f * scale);
  const float step = 38.0f * scale;
  const float gap = 4.0f * scale;
  const float valueW = 56.0f * scale;
  const float y = bounds.centerY() - s * 0.5f;
  StepperGeom g;
  g.plus = {bounds.right() - step, y, step, s};
  g.value = {g.plus.x - gap - valueW, y, valueW, s};
  g.minus = {g.value.x - gap - step, y, step, s};
  return g;
}
}  // namespace

bool Ui::stepperControl(int id, Rect bounds, int* value, int minValue, int maxValue) {
  if (frame_.r == nullptr || value == nullptr) return false;
  slotFor(id, true)->lastTouch = frame_.now;
  const StepperGeom g = stepperGeom(bounds, dp(1.0f));
  const Rect& minus = g.minus;
  const Rect& plus = g.plus;
  const Rect& valueBox = g.value;

  bool changed = false;
  // Both ends are always drawn; a disabled one is dimmed rather than absent,
  // because a stepper with a missing button reads as a layout bug.
  const bool canDown = *value > minValue;
  const bool canUp = *value < maxValue;
  Style arrow;
  arrow.variant = Variant::Secondary;
  arrow.textDp = 13.0f;
  if (canDown && button(id * 7 + 1, minus, "-", arrow)) {
    --*value;
    changed = true;
  } else if (!canDown) {
    // rect() paints fill and border in one colour, so a disabled end is a dim
    // fill rather than an outline that would come out invisible on black.
    frame_.r->rect(minus, theme::kSurface, dp(theme::kButtonRadiusDp));
    text({minus.centerX(), minus.centerY()}, "-", 13.0f, theme::kTextGhost, HAlign::Center,
         VAlign::Middle, true);
  }
  if (canUp && button(id * 7 + 2, plus, "+", arrow)) {
    ++*value;
    changed = true;
  } else if (!canUp) {
    frame_.r->rect(plus, theme::kSurface, dp(theme::kButtonRadiusDp));
    text({plus.centerX(), plus.centerY()}, "+", 13.0f, theme::kTextGhost, HAlign::Center,
         VAlign::Middle, true);
  }
  frame_.r->rect(valueBox, theme::kSurfaceRaised, dp(theme::kButtonRadiusDp));
  text({valueBox.centerX(), valueBox.centerY()}, "", 11.0f, theme::kText, HAlign::Center,
       VAlign::Middle);
  return changed;
}

void Ui::settingsRow(Rect bounds, const char* label, const char* caption) {
  text({bounds.x + dp(4.0f), bounds.y + bounds.h * 0.33f}, label, 12.0f, theme::kText,
       HAlign::Left, VAlign::Middle, true, theme::kDisplayTracking);
  if (caption != nullptr && caption[0] != '\0') {
    textMono({bounds.x + dp(4.0f), bounds.y + bounds.h * 0.68f}, caption, cfg::kSmallSizeDp,
             theme::kTextFaint, HAlign::Left, VAlign::Middle);
  }
}

void Ui::stepperValue(Rect bounds, const char* valueText) {
  if (frame_.r == nullptr) return;
  text({stepperGeom(bounds, dp(1.0f)).value.centerX(), bounds.centerY()}, valueText, 11.0f,
       theme::kText,
       HAlign::Center, VAlign::Middle, true);
}
}  // namespace ui
}  // namespace pp
