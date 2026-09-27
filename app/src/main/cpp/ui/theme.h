// The visual identity: one palette, one type scale, one set of metrics.
//
// Strictly greyscale.  `Color` cannot express a hue difference without someone
// writing one, and the greys here are chosen so that contrast ratios stay above
// the readability floor for body text while still giving the interface depth.
#pragma once

#include "../core/config.h"
#include "../core/math_util.h"
#include "../generated/font_atlas.h"

namespace pp {
namespace ui {

namespace theme {

// Backgrounds and surfaces.
constexpr Color kVoid = rgb(0.00f);
constexpr Color kSurface = rgb(0.055f);
constexpr Color kSurfaceRaised = rgb(0.10f);
constexpr Color kHairline = rgb(0.18f);
constexpr Color kHairlineStrong = rgb(0.30f);
// The moving background grid.  Barely above the void, which is the point: it has
// to be perceptible in peripheral vision and invisible where you are reading.
constexpr Color kGrid = rgb(0.045f);
constexpr Color kGridBright = rgb(0.075f);

// Text.
constexpr Color kText = rgb(0.97f);
constexpr Color kTextDim = rgb(0.62f);
constexpr Color kTextFaint = rgb(0.36f);
constexpr Color kTextGhost = rgb(0.16f);

// Accent.  Still grey: emphasis is carried by brightness, not hue.
constexpr Color kBright = rgb(1.00f);
constexpr Color kHighlight = rgb(0.80f);

// Semantic shades for outcomes, again all grey but distinguishable by value.
constexpr Color kHit = rgb(1.00f);
constexpr Color kMiss = rgb(0.42f);
constexpr Color kWarn = rgb(0.55f);
constexpr Color kLocked = rgb(0.22f);

// Feedback flashes.
constexpr Color kFlashHit = rgb(0.85f);
constexpr Color kFlashMiss = rgb(0.30f);

// ---------------------------------------------------------------------------
// Typography
// ---------------------------------------------------------------------------

// Two families only: a wide geometric face for anything that is a headline or a
// number, and a monospaced technical face for everything the player reads as
// data.  Tracking is positive on the display face because its caps are already
// wide, and zero on the technical face so digits align in columns.
inline const FontAtlas& display() { return kFontDisplay; }
inline const FontAtlas& tech() { return kFontTech; }

inline float displaySize(float dp, float density) { return dp * density; }

constexpr float kDisplayTracking = 0.055f;
constexpr float kDisplayTrackingWide = 0.14f;
constexpr float kTechTracking = 0.02f;

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

constexpr float kHairlinePx = 1.0f;
constexpr float kButtonRadiusDp = 10.0f;
constexpr float kCardRadiusDp = 14.0f;
constexpr float kGutterDp = 12.0f;

}  // namespace theme

}  // namespace ui
}  // namespace pp
