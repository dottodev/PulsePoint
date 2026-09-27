// The particle and flash system.
//
// Modes never spawn particles themselves; they append an FxRequest to their
// view and this system turns it into motion.  That keeps the effects identical
// everywhere and means a mode can be tested with the effect system switched off.
//
// The pool is fixed size and allocated once.  A frame that asks for more
// particles than the cap gets the oldest recycled, which on a mid-range device
// is the difference between a smooth frame and a stutter.
#pragma once

#include "../core/clock.h"
#include "../core/config.h"
#include "../core/math_util.h"
#include "../core/rng.h"
#include "../game/modes.h"
#include "../gfx/renderer.h"

namespace pp {
namespace ui {

class FxSystem {
 public:
  void init(Rect bounds, uint64_t seed);
  void setBounds(Rect bounds);
  // Consumes a mode's per-frame effect requests.
  void spawn(const RoundView& view, Nanos nowNs);
  void update(Nanos nowNs, float dtSeconds);
  void draw(gfx::Renderer& r, Nanos nowNs) const;

  void clear();
  // A short label that rises from where a target was answered, used for the
  // reaction time of the press that just landed.
  void popText(Vec2 pos, const char* text, float sizePx, float life = 0.7f);
  int liveParticles() const { return live_; }
  float screenPulse() const { return pulse_; }
  // A brief full-screen brightening, used on a personal best and on a MAX level
  // up.  Drawn as a flat overlay so it reads as a hit on the whole screen.
  float flash() const { return flash_; }

 private:
  struct Particle {
    Vec2 pos;
    Vec2 vel;
    float life = 0.0f;     // seconds remaining
    float maxLife = 1.0f;
    float size = 2.0f;
    float spin = 0.0f;
    float rot = 0.0f;
    float alpha = 1.0f;
    float drag = 3.0f;
    Color color = rgb(1.0f);
  };

  void emit(Vec2 pos, int count, float speedMin, float speedMax, float lifeMin, float lifeMax,
            float sizeMin, float sizeMax, float drag, Color c, Nanos nowNs);
  int alloc();

  Particle pool_[cfg::kMaxParticles];
  int live_ = 0;
  Rect bounds_;
  Rng rng_{};
  float pulse_ = 0.0f;
  float pulseDecay_ = 8.0f;
  float flash_ = 0.0f;
  float flashDecay_ = 4.0f;
  Nanos lastNs_ = 0;
  // Text pops, e.g. the reaction time that appears where a target was hit.
  struct Pop {
    bool active = false;
    Vec2 pos;
    char text[12];
    float age = 0.0f;
    float life = 0.7f;
    float sizePx = 26.0f;
    float risePx = 46.0f;
  };
  static constexpr int kMaxPops = 6;
  Pop pops_[kMaxPops];
};

}  // namespace ui
}  // namespace pp
