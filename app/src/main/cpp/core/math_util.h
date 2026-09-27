// Small fixed-size maths helpers shared by the whole game.
//
// Everything here is header-only, branch-light and allocation-free: these
// functions sit on the input and render paths and must not touch the heap.
#pragma once

#include <cmath>
#include <cstdint>

namespace pp {

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;
};

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;

  float left() const { return x; }
  float top() const { return y; }
  float right() const { return x + w; }
  float bottom() const { return y + h; }
  float centerX() const { return x + w * 0.5f; }
  float centerY() const { return y + h * 0.5f; }
  Vec2 center() const { return {centerX(), centerY()}; }
  bool contains(Vec2 p) const {
    return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
  }
  bool contains(float px, float py) const { return contains({px, py}); }
  bool overlaps(const Rect& o) const {
    return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
  }
  Rect inset(float d) const { return {x + d, y + d, w - 2.0f * d, h - 2.0f * d}; }
  Rect expanded(float d) const { return {x - d, y - d, w + 2.0f * d, h + 2.0f * d}; }
  Rect shifted(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
  static Rect fromCenter(Vec2 c, float w, float h) { return {c.x - w * 0.5f, c.y - h * 0.5f, w, h}; }
};

struct Color {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
};

// The palette is strictly greyscale: r == g == b always.  Keeping the channels
// separate means glow and blend maths stay readable while the output can never
// drift off-brand.
constexpr Color rgb(float v, float a = 1.0f) { return {v, v, v, a}; }
constexpr Color grayOf(float v, float a = 1.0f) { return {v, v, v, a}; }
constexpr Color rgba(float r, float g, float b, float a) { return {r, g, b, a}; }

inline Color withAlpha(Color c, float a) {
  c.a = a;
  return c;
}

inline Color lerpColor(Color a, Color b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
          a.a + (b.a - a.a) * t};
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Framerate-independent exponential approach.  `halfLife` is the time in
// seconds for the remaining distance to halve.
inline float damp(float current, float target, float halfLife, float dt) {
  if (halfLife <= 0.0f) return target;
  const float k = std::exp(-0.6931471805599453f * dt / halfLife);
  return target + (current - target) * k;
}

inline float smoothstep(float edge0, float edge1, float x) {
  const float t = clamp01((x - edge0) / (edge1 - edge0));
  return t * t * (3.0f - 2.0f * t);
}

inline float dist(Vec2 a, Vec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

inline float distSq(Vec2 a, Vec2 b) {
  return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
}

// Triangle wave in [0, 1] with period 1.
inline float pingpong(float t) {
  const float f = t - std::floor(t);
  return f < 0.5f ? f * 2.0f : 2.0f - f * 2.0f;
}

// Ease helpers used by the short UI animations.  All map [0, 1] -> [0, 1].
inline float easeOutCubic(float t) {
  const float u = 1.0f - clamp01(t);
  return 1.0f - u * u * u;
}
inline float easeInCubic(float t) {
  const float u = clamp01(t);
  return u * u * u;
}
inline float easeOutQuint(float t) {
  const float u = 1.0f - clamp01(t);
  return 1.0f - u * u * u * u * u;
}
inline float easeInOutCubic(float t) {
  const float u = clamp01(t);
  return u < 0.5f ? 4.0f * u * u * u : 1.0f - std::pow(-2.0f * u + 2.0f, 3.0f) * 0.5f;
}
// Snappy overshoot, used for button presses and score pops.
inline float easeOutBack(float t) {
  const float c1 = 1.70158f;
  const float c3 = c1 + 1.0f;
  const float u = clamp01(t) - 1.0f;
  return 1.0f + c3 * u * u * u + c1 * u * u;
}

}  // namespace pp
