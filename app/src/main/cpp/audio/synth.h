// Procedural audio.
//
// Every sound in PulsePoint is synthesised at trigger time: no audio assets, no
// decoding, no file handles, and the pitch of a feedback sound can be a function
// of the gameplay value that produced it -- a faster hit really does ring
// higher.  The whole synth is a few hundred bytes of state and a handful of
// oscillators, which matters because it shares a thread with the game's most
// latency-sensitive work.
#pragma once

#include <cstdint>

namespace pp {
namespace audio {

enum class Sfx : uint8_t {
  UiTap,
  UiBack,
  Spawn,
  Hit,
  Miss,
  Wrong,
  FalseStart,
  Combo,
  LevelUp,
  PersonalBest,
  Achievement,
  GameOver,
  Count
};

enum class Wave : uint8_t { Sine, Triangle, Saw, Noise };

// `intensity` scales gain and usually brightness.
// `param` is a gameplay value, typically a reaction time in milliseconds: the
// hit sound is pitched by it, so the player hears the difference between a 190
// ms and a 260 ms press without looking.
class Synth {
 public:
  static constexpr int kMaxVoices = 20;
  static constexpr int kMusicVoices = 4;

  void init(int sampleRate);
  void shutdown();

  void setSoundEnabled(bool on);
  void setMusicEnabled(bool on);

  void trigger(Sfx id, float intensity = 1.0f, float paramMs = 0.0f);
  // Called when the player leaves gameplay, to duck the music bed.
  void setMusicDucked(bool ducked);

  // Renders interleaved stereo 16-bit frames.  Never blocks and never allocates.
  int render(int16_t* out, int frames);

  int sampleRate() const { return sampleRate_; }

 private:
  struct Voice {
    bool active = false;
    Wave wave = Wave::Sine;
    float phase = 0.0f;
    float phaseInc = 0.0f;
    float freqStart = 440.0f;
    float freqEnd = 440.0f;
    float sweepSeconds = 0.05f;
    float age = 0.0f;
    float attack = 0.002f;
    float decay = 0.12f;
    float sustain = 0.0f;
    float release = 0.0f;
    float amp = 0.3f;
    float pan = 0.0f;
    float filter = 1.0f;  // 1 = open, lower is darker
    float filterState = 0.0f;
    float noiseSeed = 0.0f;
  };

  int allocVoice();
  float voiceSample(Voice& v, float dt) const;
  static float waveSample(Wave w, float phase, float noiseSeed);
  static float envAt(const Voice& v);

  Voice voices_[kMaxVoices];
  int sampleRate_ = 48000;
  bool soundEnabled_ = true;
  bool musicEnabled_ = true;
  bool musicDucked_ = false;
  float master_ = 0.9f;
  float musicGain_ = 0.32f;

  // Music state: a slow four-chord pad with a breathing filter.
  float musicPhase_[kMusicVoices] = {0.0f, 0.0f, 0.0f, 0.0f};
  float musicFilter_ = 0.0f;
  float musicLfo_ = 0.0f;
  int musicChord_ = 0;
  float musicChordTime_ = 0.0f;
  uint32_t noiseState_ = 0x12345678u;
};

}  // namespace audio
}  // namespace pp
