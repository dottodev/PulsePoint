// Player-facing options.  Small, local, and never required for play.
#pragma once

#include <cstdint>

namespace pp {

struct Settings {
  bool sound = true;
  bool music = true;
  bool haptics = true;
  bool showFps = false;

  // Display-latency compensation in milliseconds, applied to the stimulus
  // timestamp so a measurement reflects the finger rather than the compositor.
  // kCalibrationAuto tracks half the observed frame interval; the manual range
  // lets a player dial in a wired headset or an external display.
  static constexpr int kCalibrationAuto = -1;
  int calibrationMs = kCalibrationAuto;

  float hapticsStrength = 0.6f;  // 0..1

  void clampToValid();
};

}  // namespace pp
