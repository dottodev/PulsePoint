#include "settings.h"

namespace pp {

void Settings::clampToValid() {
  calibrationMs = calibrationMs < -1 ? -1 : (calibrationMs > 60 ? 60 : calibrationMs);
  if (hapticsStrength < 0.0f) hapticsStrength = 0.0f;
  if (hapticsStrength > 1.0f) hapticsStrength = 1.0f;
}

}  // namespace pp
