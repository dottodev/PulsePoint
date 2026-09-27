// Thirty achievement icons, drawn as geometry.
//
// No image assets: an icon is a handful of SDF primitives inside a unit box, so
// it scales to any density, costs one shape quad each, and can be tinted to
// match the lock state.  Every icon is built from the same vocabulary -- discs,
// rings, arcs, bars, chevrons -- which is what keeps the grid looking like one
// family rather than thirty clip arts.
#pragma once

#include "../core/math_util.h"
#include "../gfx/renderer.h"
#include "../meta/achievements.h"

namespace pp {
namespace ui {

// Draws the icon centred on `center` inside a box of `size` pixels.
// `alpha` and `brightness` let the caller render a locked entry as a dim
// silhouette and an unlocked one at full strength.
void drawIcon(gfx::Renderer& r, IconKind kind, Vec2 center, float size, float brightness);

}  // namespace ui
}  // namespace pp
