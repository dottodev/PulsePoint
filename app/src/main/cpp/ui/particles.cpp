#include "particles.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "theme.h"

namespace pp {
namespace ui {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float ease(float t) { return 1.0f - t * t * t; }

}  // namespace

void FxSystem::init(Rect bounds, uint64_t seed) {
  bounds_ = bounds;
  rng_.seed(seed);
  clear();
}

void FxSystem::setBounds(Rect bounds) { bounds_ = bounds; }

void FxSystem::clear() {
  for (int i = 0; i < cfg::kMaxParticles; ++i) pool_[i] = Particle{};
  live_ = 0;
  pulse_ = 0.0f;
  flash_ = 0.0f;
  for (int i = 0; i < kMaxPops; ++i) pops_[i] = Pop{};
  lastNs_ = 0;
}

int FxSystem::alloc() {
  if (live_ < cfg::kMaxParticles) return live_++;
  // Full: recycle the oldest, which is the one closest to expiring.
  int worst = 0;
  float worstLife = pool_[0].life / (pool_[0].maxLife > 0.0f ? pool_[0].maxLife : 1.0f);
  for (int i = 1; i < cfg::kMaxParticles; ++i) {
    const float f = pool_[i].life / (pool_[i].maxLife > 0.0f ? pool_[i].maxLife : 1.0f);
    if (f < worstLife) {
      worstLife = f;
      worst = i;
    }
  }
  return worst;
}

void FxSystem::emit(Vec2 pos, int count, float speedMin, float speedMax, float lifeMin,
                    float lifeMax, float sizeMin, float sizeMax, float drag, Color c, Nanos nowNs) {
  (void)nowNs;
  for (int i = 0; i < count; ++i) {
    Particle& p = pool_[alloc()];
    const float angle = rng_.range(0.0f, 2.0f * kPi);
    const float speed = rng_.range(speedMin, speedMax);
    p.pos = pos;
    p.vel = {std::cos(angle) * speed, std::sin(angle) * speed};
    p.maxLife = rng_.range(lifeMin, lifeMax);
    p.life = p.maxLife;
    p.size = rng_.range(sizeMin, sizeMax);
    p.spin = rng_.range(-9.0f, 9.0f);
    p.rot = rng_.range(0.0f, 2.0f * kPi);
    p.alpha = c.a;
    p.drag = drag;
    p.color = c;
  }
}

void FxSystem::popText(Vec2 pos, const char* text, float sizePx, float life) {
  if (text == nullptr || text[0] == '\0') return;
  Pop* slot = nullptr;
  for (int i = 0; i < kMaxPops; ++i) {
    if (!pops_[i].active) {
      slot = &pops_[i];
      break;
    }
  }
  if (slot == nullptr) slot = &pops_[0];  // Recycle the oldest-looking one.
  slot->active = true;
  slot->pos = pos;
  slot->age = 0.0f;
  slot->life = life;
  slot->sizePx = sizePx;
  slot->risePx = sizePx * 1.6f;
  const int n = static_cast<int>(strlen(text));
  const int cap = static_cast<int>(sizeof(slot->text)) - 1;
  const int copy = n > cap ? cap : n;
  memcpy(slot->text, text, static_cast<size_t>(copy));
  slot->text[copy] = '\0';
}

void FxSystem::spawn(const RoundView& view, Nanos nowNs) {
  for (int i = 0; i < view.fxCount; ++i) {
    const FxRequest& req = view.fx[i];
    const float s = req.strength;
    switch (req.kind) {
      case FxKind::HitBurst:
        // Radiating sparks plus a soft ring: fast, small, gone in a third of a
        // second so it never competes with the next target.
        emit(req.pos, 12, 120.0f, 420.0f * s, 0.18f, 0.38f, 1.5f, 3.2f, 6.0f,
             theme::kFlashHit, nowNs);
        pulse_ = std::max(pulse_, 0.35f * s);
        break;
      case FxKind::MissBurst:
        // Slow, dim, falling: reads as "too slow" without a harsh red flash.
        emit(req.pos, 8, 40.0f, 150.0f, 0.3f, 0.55f, 1.5f, 2.8f, 3.0f, theme::kFlashMiss, nowNs);
        pulse_ = std::max(pulse_, 0.18f * s);
        break;
      case FxKind::WrongBurst:
        emit(req.pos, 14, 90.0f, 320.0f, 0.25f, 0.5f, 1.5f, 3.0f, 4.5f, theme::kWarn, nowNs);
        pulse_ = std::max(pulse_, 0.4f * s);
        break;
      case FxKind::FalseStart:
        emit(req.pos, 6, 20.0f, 90.0f, 0.35f, 0.6f, 1.5f, 2.5f, 2.5f, theme::kMiss, nowNs);
        pulse_ = std::max(pulse_, 0.22f * s);
        break;
      case FxKind::SpawnBlip:
        emit(req.pos, 3, 20.0f, 70.0f, 0.14f, 0.26f, 1.0f, 1.8f, 5.0f, theme::kHighlight, nowNs);
        break;
      case FxKind::ComboPop:
        for (int k = 0; k < 5; ++k) {
          const float a = -1.5707963f + (static_cast<float>(k) - 2.0f) * 0.32f;
          Particle& p = pool_[alloc()];
          p.pos = req.pos;
          p.vel = {std::cos(a) * 200.0f, std::sin(a) * 200.0f};
          p.maxLife = 0.5f;
          p.life = p.maxLife;
          p.size = 2.0f;
          p.spin = 0.0f;
          p.rot = a;
          p.alpha = 0.9f;
          p.drag = 2.0f;
          p.color = theme::kBright;
        }
        break;
      case FxKind::LevelUp:
        emit(req.pos, 22, 160.0f, 520.0f, 0.35f, 0.7f, 1.5f, 3.4f, 2.4f, theme::kBright, nowNs);
        pulse_ = std::max(pulse_, 0.6f);
        flash_ = std::max(flash_, 0.30f);
        break;
      case FxKind::ScreenPulse:
        pulse_ = std::max(pulse_, 0.5f * s);
        break;
    }
  }
}

void FxSystem::update(Nanos nowNs, float dt) {
  if (dt < 0.0f) dt = 0.0f;
  if (dt > 0.1f) dt = 0.1f;  // A long stall must not fling particles off screen.
  lastNs_ = nowNs;

  int write = 0;
  for (int i = 0; i < cfg::kMaxParticles; ++i) {
    Particle& p = pool_[i];
    if (p.life <= 0.0f) continue;
    p.life -= dt;
    if (p.life <= 0.0f) continue;
    const float k = std::max(0.0f, 1.0f - p.drag * dt);
    p.vel.x *= k;
    p.vel.y *= k;
    p.vel.y += 220.0f * dt;  // A little gravity keeps the bursts from floating.
    p.pos.x += p.vel.x * dt;
    p.pos.y += p.vel.y * dt;
    p.rot += p.spin * dt;
    if (write != i) pool_[write] = p;
    ++write;
  }
  live_ = write;

  pulse_ = std::max(0.0f, pulse_ - pulseDecay_ * dt);
  flash_ = std::max(0.0f, flash_ - flashDecay_ * dt);

  for (int i = 0; i < kMaxPops; ++i) {
    if (!pops_[i].active) continue;
    pops_[i].age += dt;
    if (pops_[i].age >= pops_[i].life) pops_[i].active = false;
  }
}

void FxSystem::draw(gfx::Renderer& r, Nanos nowNs) const {
  (void)nowNs;
  // Full-screen response first, so everything else sits on top of it.
  if (flash_ > 0.002f) {
    r.rect({0.0f, 0.0f, r.width(), r.height()}, withAlpha(theme::kBright, clamp01(flash_)), 0.0f, 0.0f,
           0.0f);
  }

  for (int i = 0; i < live_; ++i) {
    const Particle& p = pool_[i];
    const float t = clamp01(p.life / (p.maxLife > 0.0f ? p.maxLife : 1.0f));
    const float size = p.size * (0.4f + 0.6f * t);
    const float a = p.alpha * t * t;
    if (a <= 0.004f) continue;
    if (p.spin != 0.0f) {
      // Rotating debris reads as square fragments, which suits the geometry.
      r.rect(Rect::fromCenter(p.pos, size * 2.0f, size * 2.0f), p.color, 0.0f, 0.0f, 0.0f);
    } else {
      r.disc(p.pos, size, p.color);
    }
  }

  for (int i = 0; i < kMaxPops; ++i) {
    const Pop& pop = pops_[i];
    if (!pop.active) continue;
    const float t = clamp01(pop.age / pop.life);
    gfx::TextStyle style;
    style.face = &theme::display();
    style.sizePx = pop.sizePx * (0.85f + 0.25f * ease(t));
    style.tracking = theme::kDisplayTracking;
    style.color = theme::kBright;
    style.alpha = 1.0f - ease(t) * 0.9f;
    style.glowPx = 3.0f * (1.0f - t);
    r.text(pop.pos.x, pop.pos.y - pop.risePx * ease(t), pop.text, style, gfx::HAlign::Center,
           gfx::VAlign::Middle);
  }
}

}  // namespace ui
}  // namespace pp
