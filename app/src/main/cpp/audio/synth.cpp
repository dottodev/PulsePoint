#include "synth.h"

#include <cmath>

#include "../core/math_util.h"

namespace pp {
namespace audio {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kMinFreq = 20.0f;
constexpr float kMaxFreq = 12000.0f;

// A minor pad: root, minor third-ish voicing, and a fifth above.  Low, dark and
// slow, so it never competes with the transients the player is listening for.
constexpr float kChordRoots[4] = {55.00f, 43.65f, 65.41f, 49.00f};   // A1, F1, C2, G1
constexpr float kChordRatios[4] = {1.0f, 1.5f, 2.0f, 3.0f};
constexpr float kChordSeconds = 6.5f;

float nextNoise(uint32_t& state) {
  // xorshift: cheap, and good enough for a noise bed.
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return static_cast<float>(state & 0xFFFFFFu) * (1.0f / 16777216.0f) * 2.0f - 1.0f;
}

}  // namespace

void Synth::init(int sampleRate) {
  sampleRate_ = sampleRate > 8000 ? sampleRate : 48000;
  for (int i = 0; i < kMaxVoices; ++i) voices_[i] = Voice{};
  for (int i = 0; i < kMusicVoices; ++i) musicPhase_[i] = 0.0f;
  musicFilter_ = 0.0f;
  musicLfo_ = 0.0f;
  musicChord_ = 0;
  musicChordTime_ = 0.0f;
  noiseState_ = 0x12345678u;
}

void Synth::shutdown() {
  for (int i = 0; i < kMaxVoices; ++i) voices_[i] = Voice{};
}

void Synth::setSoundEnabled(bool on) {
  soundEnabled_ = on;
  if (!on) {
    for (int i = 0; i < kMaxVoices; ++i) voices_[i].active = false;
  }
}

void Synth::setMusicEnabled(bool on) {
  musicEnabled_ = on;
  if (!on) musicFilter_ = 0.0f;
}

void Synth::setMusicDucked(bool ducked) { musicDucked_ = ducked; }

int Synth::allocVoice() {
  for (int i = 0; i < kMaxVoices; ++i) {
    if (!voices_[i].active) return i;
  }
  // Steal the quietest voice rather than dropping the sound: a missing hit is
  // far more noticeable than a slightly clipped one.
  int quietest = 0;
  float quietestEnv = 1e9f;
  for (int i = 0; i < kMaxVoices; ++i) {
    const float e = envAt(voices_[i]);
    if (e < quietestEnv) {
      quietestEnv = e;
      quietest = i;
    }
  }
  return quietest;
}

float Synth::waveSample(Wave w, float phase, float noiseSeed) {
  const float p = phase - std::floor(phase);
  switch (w) {
    case Wave::Sine: return std::sin(p * kTwoPi);
    case Wave::Triangle: return 4.0f * std::fabs(p - 0.5f) - 1.0f;
    case Wave::Saw: return 2.0f * p - 1.0f;
    case Wave::Noise: return noiseSeed;
  }
  return 0.0f;
}

float Synth::envAt(const Voice& v) {
  if (v.age < v.attack) return v.attack > 0.0f ? v.age / v.attack : 1.0f;
  const float t = v.age - v.attack;
  if (v.decay <= 0.0f) return v.sustain;
  if (t < v.decay) {
    const float k = t / v.decay;
    return 1.0f + (v.sustain - 1.0f) * k;
  }
  if (v.release <= 0.0f) return v.sustain;
  const float r = (t - v.decay) / v.release;
  return r >= 1.0f ? 0.0f : v.sustain * (1.0f - r);
}

float Synth::voiceSample(Voice& v, float dt) const {
  v.age += dt;
  const float env = envAt(v);
  if (env <= 0.0005f) {
    v.active = false;
    return 0.0f;
  }
  // Exponential frequency sweep, which sounds far better than a linear one on a
  // short blip.
  const float k = v.sweepSeconds > 0.0f ? clamp01(v.age / v.sweepSeconds) : 1.0f;
  const float freq = v.freqStart * std::pow(v.freqEnd / v.freqStart, k);
  const float clamped = clampf(freq, kMinFreq, kMaxFreq);
  v.phase += clamped * dt;
  if (v.phase > 1.0f) v.phase -= std::floor(v.phase);

  float s = waveSample(v.wave, v.phase, v.noiseSeed);
  // One-pole lowpass whose cutoff follows the envelope, so a hit is bright on
  // the transient and darkens as it decays.
  const float cutoff = clampf(0.08f + v.filter * env, 0.02f, 0.98f);
  v.filterState += cutoff * (s - v.filterState);
  return v.filterState * env * v.amp;
}

void Synth::trigger(Sfx id, float intensity, float paramMs) {
  if (!soundEnabled_) return;
  const float amp = clampf(intensity, 0.0f, 2.0f);

  auto voice = [&](Wave wave, float f0, float f1, float sweep, float attack, float decay,
                   float sustain, float release, float gain, float pan, float filter) -> Voice {
    Voice v;
    v.active = true;
    v.wave = wave;
    v.freqStart = f0;
    v.freqEnd = f1;
    v.sweepSeconds = sweep;
    v.attack = attack;
    v.decay = decay;
    v.sustain = sustain;
    v.release = release;
    v.amp = gain * amp;
    v.pan = clampf(pan, -1.0f, 1.0f);
    v.filter = filter;
    v.noiseSeed = nextNoise(noiseState_);
    return v;
  };

  switch (id) {
    case Sfx::UiTap:
      voices_[allocVoice()] =
          voice(Wave::Triangle, 1180.0f, 900.0f, 0.02f, 0.001f, 0.035f, 0.0f, 0.0f, 0.16f, 0.0f, 0.8f);
      break;
    case Sfx::UiBack:
      voices_[allocVoice()] =
          voice(Wave::Triangle, 620.0f, 420.0f, 0.03f, 0.001f, 0.05f, 0.0f, 0.0f, 0.15f, 0.0f, 0.6f);
      break;
    case Sfx::Spawn:
      voices_[allocVoice()] =
          voice(Wave::Sine, 1500.0f, 1050.0f, 0.018f, 0.001f, 0.030f, 0.0f, 0.0f, 0.11f, 0.0f, 0.9f);
      break;
    case Sfx::Hit: {
      // Pitched by the reaction time: 150 ms sits a fifth above 300 ms.  The
      // player learns the mapping without being told.
      const float ms = paramMs > 1.0f ? paramMs : 250.0f;
      const float t = clamp01((ms - 140.0f) / (400.0f - 140.0f));
      const float f = 520.0f + (1.0f - t) * 620.0f;
      voices_[allocVoice()] =
          voice(Wave::Sine, f * 1.6f, f, 0.010f, 0.0006f, 0.045f, 0.0f, 0.0f, 0.20f, 0.0f, 1.0f);
      voices_[allocVoice()] =
          voice(Wave::Triangle, f, f * 0.98f, 0.05f, 0.001f, 0.075f, 0.0f, 0.0f, 0.13f, 0.12f, 0.7f);
      break;
    }
    case Sfx::Miss:
      voices_[allocVoice()] =
          voice(Wave::Sine, 260.0f, 150.0f, 0.16f, 0.003f, 0.14f, 0.0f, 0.0f, 0.16f, -0.15f, 0.35f);
      break;
    case Sfx::Wrong:
      voices_[allocVoice()] =
          voice(Wave::Saw, 300.0f, 190.0f, 0.12f, 0.002f, 0.12f, 0.0f, 0.0f, 0.10f, 0.2f, 0.45f);
      break;
    case Sfx::FalseStart:
      voices_[allocVoice()] =
          voice(Wave::Triangle, 420.0f, 220.0f, 0.14f, 0.002f, 0.16f, 0.0f, 0.0f, 0.14f, -0.2f, 0.4f);
      break;
    case Sfx::Combo: {
      // Rising with the streak, capped so it never becomes shrill.
      const int step = static_cast<int>(clampf(paramMs, 0.0f, 20.0f));
      const float base = 620.0f * std::pow(1.0595f, static_cast<float>(step) * 2.0f);
      voices_[allocVoice()] = voice(Wave::Triangle, base, base * 1.5f, 0.09f, 0.002f, 0.10f, 0.0f,
                                    0.0f, 0.14f, 0.1f, 0.85f);
      break;
    }
    case Sfx::LevelUp:
      for (int i = 0; i < 3; ++i) {
        Voice v = voice(Wave::Triangle, 440.0f * std::pow(1.2599f, static_cast<float>(i)),
                        440.0f * std::pow(1.2599f, static_cast<float>(i) + 0.5f), 0.08f, 0.004f,
                        0.12f, 0.0f, 0.0f, 0.11f, 0.0f, 0.8f);
        v.age = -static_cast<float>(i) * 0.055f;  // Simple stagger, no scheduler.
        if (v.age < 0.0f) v.age = 0.0f;
        voices_[allocVoice()] = v;
      }
      break;
    case Sfx::PersonalBest: {
      const float notes[4] = {523.25f, 659.25f, 783.99f, 1046.50f};
      for (int i = 0; i < 4; ++i) {
        voices_[allocVoice()] = voice(Wave::Sine, notes[i], notes[i], 0.01f, 0.003f, 0.20f, 0.0f,
                                      0.0f, 0.13f, (static_cast<float>(i) - 1.5f) * 0.18f, 0.95f);
      }
      break;
    }
    case Sfx::Achievement: {
      // The dramatic one: a rising cluster, a sub thump and a noise sweep.
      const float notes[3] = {392.0f, 587.33f, 880.0f};
      for (int i = 0; i < 3; ++i) {
        voices_[allocVoice()] = voice(Wave::Triangle, notes[i] * 0.5f, notes[i], 0.28f, 0.006f,
                                      0.30f, 0.10f, 0.28f, 0.13f,
                                      (static_cast<float>(i) - 1.0f) * 0.25f, 0.9f);
      }
      voices_[allocVoice()] =
          voice(Wave::Sine, 110.0f, 55.0f, 0.30f, 0.004f, 0.34f, 0.0f, 0.0f, 0.22f, 0.0f, 0.25f);
      voices_[allocVoice()] =
          voice(Wave::Noise, 400.0f, 5200.0f, 0.34f, 0.010f, 0.34f, 0.0f, 0.0f, 0.05f, 0.0f, 1.0f);
      break;
    }
    case Sfx::GameOver: {
      const float notes[2] = {330.0f, 196.0f};
      for (int i = 0; i < 2; ++i) {
        voices_[allocVoice()] = voice(Wave::Sine, notes[i], notes[i] * 0.75f, 0.32f, 0.005f,
                                      0.28f, 0.05f, 0.30f, 0.16f, 0.0f, 0.5f);
      }
      break;
    }
    case Sfx::Count:
      break;
  }
}

int Synth::render(int16_t* out, int frames) {
  if (out == nullptr || frames <= 0) return 0;
  const float dt = 1.0f / static_cast<float>(sampleRate_);
  const float musicTarget = (musicEnabled_ ? (musicDucked_ ? 0.35f : 1.0f) : 0.0f) * musicGain_;

  for (int f = 0; f < frames; ++f) {
    float left = 0.0f;
    float right = 0.0f;

    for (int i = 0; i < kMaxVoices; ++i) {
      Voice& v = voices_[i];
      if (!v.active) continue;
      const float s = voiceSample(v, dt);
      // Equal-power pan, so a centred sound loses no energy.
      const float ang = (v.pan + 1.0f) * 0.25f * 3.14159265f;
      left += s * std::cos(ang);
      right += s * std::sin(ang);
    }

    // Music bed: a detuned saw pad through a slowly breathing lowpass.
    musicChordTime_ += dt;
    if (musicChordTime_ >= kChordSeconds) {
      musicChordTime_ -= kChordSeconds;
      musicChord_ = (musicChord_ + 1) & 3;
    }
    musicLfo_ += dt;
    if (musicTarget > 0.0f) {
      const float root = kChordRoots[musicChord_];
      const float breath =
          0.5f + 0.5f * std::sin(musicLfo_ * 0.42f);  // ~15 s period
      const float cutoff = 0.035f + 0.075f * breath;
      float pad = 0.0f;
      for (int v = 0; v < kMusicVoices; ++v) {
        const float detune = 1.0f + (static_cast<float>(v) - 1.5f) * 0.0035f;
        const float freq = root * kChordRatios[v] * detune;
        musicPhase_[v] += freq * dt;
        if (musicPhase_[v] > 1.0f) musicPhase_[v] -= std::floor(musicPhase_[v]);
        const float ph = musicPhase_[v];
        // Band-limited-ish saw: a handful of harmonics with rolled-off gain.
        float s = 0.0f;
        for (int h = 1; h <= 6; ++h) {
          const float amp = 1.0f / static_cast<float>(h);
          s += amp * std::sin(ph * kTwoPi * static_cast<float>(h));
        }
        pad += s * 0.06f;
      }
      const float air = nextNoise(noiseState_) * 0.012f;
      musicFilter_ += cutoff * (pad + air - musicFilter_);
      const float m = musicFilter_ * musicTarget * (0.65f + 0.35f * breath);
      left += m;
      right += m;
    } else {
      musicFilter_ += 0.02f * (0.0f - musicFilter_);
    }

    // Soft clip rather than a hard one: a hit landing on top of a chord should
    // round off, not crack.
    left = left * master_;
    right = right * master_;
    left = left / (1.0f + std::fabs(left) * 0.7f);
    right = right / (1.0f + std::fabs(right) * 0.7f);

    out[f * 2] = static_cast<int16_t>(clampf(left, -1.0f, 1.0f) * 32000.0f);
    out[f * 2 + 1] = static_cast<int16_t>(clampf(right, -1.0f, 1.0f) * 32000.0f);
  }
  return frames;
}

}  // namespace audio
}  // namespace pp
