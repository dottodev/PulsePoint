#include "icons.h"

#include <cmath>

#include "theme.h"

namespace pp {
namespace ui {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

// A small drawing surface in unit coordinates: -1..1 on both axes, scaled to
// pixels at draw time.
//
// Every argument to a Pen helper is in *unit* space, including stroke weights.
// Mixing the two is how an icon ends up with a 200-pixel blob where a 4-pixel
// line was meant, so the conversion happens in exactly one place: `s`.
struct Pen {
  gfx::Renderer* r;
  Vec2 c;
  float s;    // pixels per unit
  Color ink;
  float w;    // default stroke weight, in units

  Vec2 pt(float x, float y) const { return {c.x + x * s, c.y + y * s}; }

  void dot(float x, float y, float radius) const { r->disc(pt(x, y), radius * s, ink); }
  void ring(float x, float y, float radius, float weight) const {
    r->ring(pt(x, y), radius * s, weight * s, ink);
  }
  void arc(float x, float y, float radius, float weight, float startDeg, float sweepDeg) const {
    r->ringArc(pt(x, y), radius * s, weight * s, startDeg * kPi / 180.0f,
               sweepDeg * kPi / 180.0f, ink);
  }
  void bar(float x, float y, float wu, float hu) const { r->rect(unit(x, y, wu, hu), ink, 0.0f); }
  Rect unit(float x, float y, float wu, float hu) const {
    return Rect::fromCenter({c.x + x * s, c.y + y * s}, wu * s, hu * s);
  }
  void stroke(float x1, float y1, float x2, float y2, float weight) const {
    r->line(pt(x1, y1), pt(x2, y2), weight * s, ink);
  }
  // Rotates a unit-space offset and returns the pixel position.
  Vec2 rotated(Vec2 at, float dx, float dy, float deg) const {
    const float a = deg * kPi / 180.0f;
    const float cs = std::cos(a);
    const float sn = std::sin(a);
    return pt(at.x + dx * cs - dy * sn, at.y + dx * sn + dy * cs);
  }
  void chevron(float x, float y, float size, float weight, float rotationDeg) const {
    const Vec2 at{x, y};
    r->line(rotated(at, -size * 0.5f, -size * 0.5f, rotationDeg),
            rotated(at, size * 0.5f, 0.0f, rotationDeg), weight * s, ink);
    r->line(rotated(at, size * 0.5f, 0.0f, rotationDeg),
            rotated(at, -size * 0.5f, size * 0.5f, rotationDeg), weight * s, ink);
  }
  void tri(float x, float y, float radius, float rotationDeg) const {
    r->triangle(pt(x, y), radius * s, rotationDeg * kPi / 180.0f, ink);
  }
};

// A ring of n radial ticks, used by several icons.
void ticks(const Pen& p, float radius, int count, float weight, float length) {
  for (int i = 0; i < count; ++i) {
    const float a = kTau * static_cast<float>(i) / static_cast<float>(count);
    const float dx = std::cos(a);
    const float dy = std::sin(a);
    p.stroke(dx * radius, dy * radius, dx * (radius + length), dy * (radius + length), weight);
  }
}

// The dominant motif for the hidden set: a target with an extra ring or
// chevron, so the family reads as "beyond the last tier" without colour.
void bullseyeBase(const Pen& p, float rings) {
  p.ring(0.0f, 0.0f, 0.86f, p.w);
  if (rings >= 2.0f) p.ring(0.0f, 0.0f, 0.60f, p.w * 0.85f);
  if (rings >= 3.0f) p.ring(0.0f, 0.0f, 0.34f, p.w * 0.7f);
  p.dot(0.0f, 0.0f, 0.10f);
}

void drawPulse(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  // A single spike: flat, spike, flat, spike, flat.  The game's whole idea in
  // one polyline.
  p.stroke(-0.85f, 0.10f, -0.40f, 0.10f, p.w);
  p.stroke(-0.40f, 0.10f, -0.18f, -0.62f, p.w);
  p.stroke(-0.18f, -0.62f, 0.10f, 0.72f, p.w);
  p.stroke(0.10f, 0.72f, 0.32f, 0.10f, p.w);
  p.stroke(0.32f, 0.10f, 0.85f, 0.10f, p.w);
}

void drawStairs(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.bar(-0.66f, 0.52f, 0.30f, 0.62);
  p.bar(0.0f, 0.22f, 0.30f, 0.98);
  p.bar(0.66f, -0.12f, 0.30f, 1.34);
}

void drawLoop(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.arc(0.0f, 0.0f, 0.62f, p.w, 40.0f, 285.0f);
  // Arrow head closing the loop.
  p.chevron(0.50f, -0.48f, 0.34f, p.w, -35.0f);
}

void drawPillar(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.stroke(-0.62f, 0.86f, 0.62f, 0.86f, p.w);
  p.stroke(-0.62f, 0.86f, -0.62f, 0.52f, p.w);
  p.stroke(0.62f, 0.86f, 0.62f, 0.52f, p.w);
  p.bar(0.0f, 0.02f, 0.42f, 1.10f);
  p.stroke(-0.72f, -0.60f, 0.72f, -0.60f, p.w);
}

void drawInfinity(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.arc(-0.42f, 0.0f, 0.40f, p.w, 0.0f, 360.0f);
  p.arc(0.42f, 0.0f, 0.40f, p.w, 0.0f, 360.0f);
  p.stroke(-0.10f, 0.0f, 0.10f, 0.0f, p.w);
}

void drawDrop(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.tri(0.0f, -0.52f, 0.46f, 180.0f);
  p.ring(0.0f, 0.10f, 0.44f, p.w);
  p.dot(0.0f, 0.10f, 0.13f);
}

void drawBlade(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  // A dagger: blade, crossguard, pommel.  The first version was a bare spike and
  // was indistinguishable from a stray mark at 30 dp.
  p.stroke(0.0f, -0.90f, 0.0f, 0.52f, p.w);
  p.stroke(-0.52f, 0.52f, 0.52f, 0.52f, p.w);
  p.stroke(0.0f, 0.52f, 0.0f, 0.86f, p.w);
  p.dot(0.0f, 0.90f, 0.09f);
  p.chevron(0.0f, -0.90f, 0.30f, p.w, 180.0f);
}

void drawBolt(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  // The one heavy mark in the set: a bolt has to read as a bolt at 30 dp.
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.20f};
  p.stroke(0.24f, -0.90f, -0.36f, 0.10f, p.w);
  p.stroke(-0.36f, 0.10f, 0.06f, 0.10f, p.w);
  p.stroke(0.06f, 0.10f, -0.24f, 0.90f, p.w);
}

void drawFlash(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  ticks(p, 0.70f, 8, p.w * 0.8f, 0.24f);
  p.ring(0.0f, 0.0f, 0.34f, p.w);
  p.dot(0.0f, 0.0f, 0.13f);
}

void drawCrosshair(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.ring(0.0f, 0.0f, 0.56f, p.w);
  p.stroke(-0.90f, 0.0f, -0.28f, 0.0f, p.w);
  p.stroke(0.28f, 0.0f, 0.90f, 0.0f, p.w);
  p.stroke(0.0f, -0.90f, 0.0f, -0.28f, p.w);
  p.stroke(0.0f, 0.28f, 0.0f, 0.90f, p.w);
  p.dot(0.0f, 0.0f, 0.09f);
}

void drawDiamond(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  // Four bars rotated into a rhombus: cheaper and crisper than a polygon.
  p.bar(0.0f, -0.52f, 0.80f, 0.80f * 0.42f);
  p.bar(0.0f, 0.52f, 0.80f, 0.80f * 0.42f);
  p.bar(-0.52f, 0.0f, 0.80f * 0.42f, 0.80f);
  p.bar(0.52f, 0.0f, 0.80f * 0.42f, 0.80f);
  p.dot(0.0f, 0.0f, 0.11f);
}

void drawChain(gfx::Renderer& r, Vec2 c, float size, float brightness, int links) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  for (int i = 0; i < links; ++i) {
    const float t = links == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(links - 1);
    p.ring((t - 0.5f) * 0.78f, 0.0f, 0.25f, p.w);
  }
}

void drawGear(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.ring(0.0f, 0.0f, 0.56f, p.w * 1.1f);
  p.ring(0.0f, 0.0f, 0.22f, p.w);
  ticks(p, 0.66f, 6, p.w, 0.24f);
}

void drawStack(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness, int rows) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  const float h = 1.5f / static_cast<float>(rows);
  for (int i = 0; i < rows; ++i) {
    const float y = -0.75f + h * (static_cast<float>(i) + 0.5f);
    const float w = 1.30f - 0.26f * static_cast<float>(i);
    p.ring(0.0f, y, w * 0.5f, p.w * 0.85f);
  }
}

void drawCents(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.ring(0.0f, 0.0f, 0.72f, p.w);
  p.stroke(-0.72f, 0.0f, 0.72f, 0.0f, p.w * 0.8f);
  p.stroke(-0.52f, -0.28f, 0.52f, -0.28f, p.w * 0.7f);
  p.stroke(-0.52f, 0.28f, 0.52f, 0.28f, p.w * 0.7f);
}

void drawShield(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.stroke(-0.60f, -0.72f, 0.60f, -0.72f, p.w);
  p.stroke(-0.60f, -0.72f, -0.60f, 0.18f, p.w);
  p.stroke(0.60f, -0.72f, 0.60f, 0.18f, p.w);
  p.chevron(0.0f, 0.18f, 1.20f, p.w, 90.0f);
  p.dot(0.0f, -0.20f, 0.12f);
}

void drawWave(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  // Two clean, parallel waves: consistency, by construction.
  for (int k = 0; k < 2; ++k) {
    const float base = -0.34f + 0.68f * static_cast<float>(k);
    for (int i = 0; i < 8; ++i) {
      const float x0 = -0.86f + 1.72f * static_cast<float>(i) / 8.0f;
      const float x1 = -0.86f + 1.72f * static_cast<float>(i + 1) / 8.0f;
      const float y0 = base + 0.16f * std::sin(static_cast<float>(i) * 1.5707963f);
      const float y1 = base + 0.16f * std::sin(static_cast<float>(i + 1) * 1.5707963f);
      p.stroke(x0, y0, x1, y1, p.w);
    }
  }
}

void drawMountain(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.stroke(-0.88f, 0.80f, -0.22f, -0.20f, p.w);
  p.stroke(-0.22f, -0.20f, 0.30f, 0.80f, p.w);
  p.stroke(0.30f, 0.80f, 0.88f, 0.10f, p.w);
  p.stroke(-0.44f, 0.24f, -0.22f, -0.20f, p.w * 0.8f);
  p.stroke(-0.02f, 0.26f, 0.30f, 0.80f, p.w * 0.8f);
  p.bar(0.0f, 0.92f, 1.9f, 0.10f);
}

void drawScatter(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.ring(-0.48f, -0.34f, 0.22f, p.w);
  p.ring(0.52f, 0.22f, 0.18f, p.w);
  p.ring(-0.10f, 0.62f, 0.14f, p.w);
  p.ring(0.62f, -0.56f, 0.13f, p.w);
  p.dot(-0.10f, -0.62f, 0.11f);
}

void drawIris(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.ring(0.0f, 0.0f, 0.84f, p.w);
  p.ring(0.0f, 0.0f, 0.48f, p.w * 0.8f);
  for (int i = 0; i < 8; ++i) {
    const float a = kTau * static_cast<float>(i) / 8.0f + 0.3926991f;
    const float dx = std::cos(a);
    const float dy = std::sin(a);
    p.stroke(dx * 0.48f, dy * 0.48f, dx * 0.84f, dy * 0.84f, p.w * 0.75f);
  }
  p.dot(0.0f, 0.0f, 0.12f);
}

void drawHourglass(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.stroke(-0.56f, -0.80f, 0.56f, -0.80f, p.w);
  p.stroke(-0.56f, 0.80f, 0.56f, 0.80f, p.w);
  p.stroke(-0.48f, -0.80f, 0.0f, 0.0f, p.w);
  p.stroke(0.48f, -0.80f, 0.0f, 0.0f, p.w);
  p.stroke(-0.48f, 0.80f, 0.0f, 0.0f, p.w);
  p.stroke(0.48f, 0.80f, 0.0f, 0.0f, p.w);
  p.tri(0.0f, 0.52f, 0.36f, 180.0f);
}

void drawShieldUp(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  p.stroke(-0.58f, -0.80f, 0.58f, -0.80f, p.w);
  p.stroke(-0.58f, -0.80f, -0.58f, 0.10f, p.w);
  p.stroke(0.58f, -0.80f, 0.58f, 0.10f, p.w);
  p.chevron(0.0f, 0.10f, 1.16f, p.w, 90.0f);
  p.chevron(0.0f, -0.62f, 0.42f, p.w * 0.8f, -90.0f);
}

void drawGrid(gfx::Renderer& r, IconKind, Vec2 c, float size, float brightness) {
  const Pen p{&r, c, size * 0.5f, grayOf(brightness), 0.13f};
  for (int i = 0; i < 3; ++i) {
    const float v = -0.52f + 0.52f * static_cast<float>(i);
    p.stroke(-0.86f, v, 0.86f, v, p.w);
    p.stroke(v, -0.86f, v, 0.86f, p.w);
  }
  p.dot(0.52f, -0.52f, 0.15f);
}

void drawHidden(const Pen& p, int tier) {
  // The hidden set shares one motif with a growing number of rings, so a
  // player who has seen one can recognise the rest.
  bullseyeBase(p, 1.0f + static_cast<float>(tier));
  if (tier >= 2) ticks(p, 0.90f, 4 + tier, p.w * 0.7f, 0.20f);
  if (tier >= 3) p.ring(0.0f, 0.0f, 0.99f, p.w * 0.6f);
  if (tier >= 4) {
    p.stroke(-0.72f, -0.72f, 0.72f, 0.72f, p.w * 0.6f);
    p.stroke(0.72f, -0.72f, -0.72f, 0.72f, p.w * 0.6f);
  }
}

}  // namespace

void drawIcon(gfx::Renderer& r, IconKind kind, Vec2 center, float size, float brightness) {
  const Pen p{&r, center, size * 0.5f, grayOf(clamp01(brightness)), 0.10f};
  switch (kind) {
    case IconKind::Pulse: drawPulse(r, kind, center, size, brightness); break;
    case IconKind::Stairs: drawStairs(r, kind, center, size, brightness); break;
    case IconKind::Loop: drawLoop(r, kind, center, size, brightness); break;
    case IconKind::Pillar: drawPillar(r, kind, center, size, brightness); break;
    case IconKind::Infinity: drawInfinity(r, kind, center, size, brightness); break;
    case IconKind::Drop: drawDrop(r, kind, center, size, brightness); break;
    case IconKind::Blade: drawBlade(r, kind, center, size, brightness); break;
    case IconKind::Bolt: drawBolt(r, kind, center, size, brightness); break;
    case IconKind::Flash: drawFlash(r, kind, center, size, brightness); break;
    case IconKind::Crosshair: drawCrosshair(r, kind, center, size, brightness); break;
    case IconKind::Diamond: drawDiamond(r, kind, center, size, brightness); break;
    case IconKind::Link3: drawChain(r, center, size, brightness, 3); break;
    case IconKind::Link4: drawChain(r, center, size, brightness, 4); break;
    case IconKind::Gear: drawGear(r, kind, center, size, brightness); break;
    case IconKind::Cents: drawCents(r, kind, center, size, brightness); break;
    case IconKind::Stack3: drawStack(r, kind, center, size, brightness, 3); break;
    case IconKind::Stack5: drawStack(r, kind, center, size, brightness, 5); break;
    case IconKind::Shield: drawShield(r, kind, center, size, brightness); break;
    case IconKind::Wave: drawWave(r, kind, center, size, brightness); break;
    case IconKind::Mountain: drawMountain(r, kind, center, size, brightness); break;
    case IconKind::Scatter: drawScatter(r, kind, center, size, brightness); break;
    case IconKind::Iris: drawIris(r, kind, center, size, brightness); break;
    case IconKind::Hourglass: drawHourglass(r, kind, center, size, brightness); break;
    case IconKind::ShieldUp: drawShieldUp(r, kind, center, size, brightness); break;
    case IconKind::Grid: drawGrid(r, kind, center, size, brightness); break;
    case IconKind::Human: drawHidden(p, 0); break;
    case IconKind::Hyper: drawHidden(p, 1); break;
    case IconKind::Overdrive: drawHidden(p, 2); break;
    case IconKind::Transcend: drawHidden(p, 3); break;
    case IconKind::Pulsepoint: drawHidden(p, 4); break;
    case IconKind::Count: break;
  }
}

}  // namespace ui
}  // namespace pp
