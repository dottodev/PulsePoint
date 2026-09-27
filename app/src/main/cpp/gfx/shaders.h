// Two shaders, two draw calls, one vertex format.
//
// The whole frame is a single dynamic vertex buffer for shapes and a single one
// for text, each uploaded once and drawn once.  That is the entire rendering
// strategy: no render targets, no per-object state changes, no allocations
// after load.
//
// Shapes are signed distance fields evaluated in the fragment shader.  Working
// in distance space means a 1 dp hairline and a 200 dp filled panel cost exactly
// the same, every edge is analytically antialiased with no textures, and shapes
// can be blended freely in submission order.
#pragma once

namespace pp {
namespace gfx {

// Shared vertex layout: four vec4 attributes, 16 floats / 64 bytes.
//
//   loc 0  a_pos      position in pixels, y down
//   loc 1  a_uv       shapes: offset from the quad centre, in pixels
//                     text:   atlas texcoord
//   loc 2  a_color    rgba
//   loc 3  a_params   p0..p3
//   loc 4  a_shape    halfW, halfH, kind, spare
//
// `a_shape` is constant across a quad, so the fragment shader can do pixel-exact
// distance maths without any uniform changes.
inline constexpr int kVertexFloats = 16;
inline constexpr int kVertexBytes = kVertexFloats * 4;
inline constexpr int kAttribs = 5;

// Shape kinds; the values must match the ones the renderer submits.
enum class ShapeKind : int {
  Rect = 0,       // p0 corner radius, p1 border width, p2 feather
  Disc = 1,       // p0 border width
  Ring = 2,       // p0 thickness
  RingArc = 3,    // p0 thickness, p1 start angle, p2 sweep, p3 radius
  Segment = 4,    // p0,p1 endpoint A, p2,p3 endpoint B, shape.x = thickness
  Glow = 5,       // p0 intensity
  Triangle = 6,   // p0 rotation in radians
  Vignette = 7,   // p0 inner radius, p1 outer radius
  Count
};

inline constexpr char kShapeVertexShader[] = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_params;
layout(location = 4) in vec4 a_shape;
uniform vec2 u_resolution;
out vec2 v_local;
out vec4 v_color;
out vec4 v_params;
flat out int v_kind;
flat out vec2 v_half;
void main() {
  vec2 ndc = vec2(a_pos.x / u_resolution.x * 2.0 - 1.0, 1.0 - a_pos.y / u_resolution.y * 2.0);
  v_local = a_uv;
  v_color = a_color;
  v_params = a_params;
  v_kind = int(a_shape.z + 0.5);
  v_half = a_shape.xy;
  gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

inline constexpr char kShapeFragmentShader[] = R"(#version 300 es
precision highp float;
in vec2 v_local;
in vec4 v_color;
in vec4 v_params;
flat in int v_kind;
flat in vec2 v_half;
out vec4 fragColor;

const float PI = 3.141592653589793;

float sdRoundedBox(vec2 p, vec2 halfSize, float r) {
  r = max(min(r, min(halfSize.x, halfSize.y)), 0.0);
  vec2 q = abs(p) - (halfSize - vec2(r));
  return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

float sdSegment(vec2 p, vec2 a, vec2 b) {
  vec2 pa = p - a;
  vec2 ba = b - a;
  float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
  return length(pa - ba * h);
}

float sdEquilateral(vec2 p, float r) {
  const float k = 1.7320508075688772;
  p.x = abs(p.x) - r;
  p.y = p.y + r / k;
  if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) * 0.5;
  p.x -= clamp(p.x, -2.0 * r, 0.0);
  return -length(p) * sign(p.y);
}

void main() {
  float d = 1e9;
  float border = 0.0;
  float feather = 0.0;

  if (v_kind == 0) {                                     // Rect
    d = sdRoundedBox(v_local, v_half, v_params.x);
    border = v_params.y;
    feather = v_params.z;
  } else if (v_kind == 1) {                              // Disc
    d = length(v_local) - v_half.x;
    border = v_params.x;
  } else if (v_kind == 2) {                              // Ring
    d = abs(length(v_local) - v_half.x) - max(v_params.x, 0.0) * 0.5;
  } else if (v_kind == 3) {                              // RingArc
    // The quad is the whole box (radius + thickness + a pixel), so the radius
    // travels in v_params.w and must be used here: v_half.x is the box half
    // extent, which would push the arc outward by half its thickness plus one.
    float ring = abs(length(v_local) - v_params.w) - max(v_params.x, 0.0) * 0.5;
    float rel = mod(atan(v_local.y, v_local.x) - v_params.y, 2.0 * PI);
    float sweep = v_params.z;
    // Angles outside the window are pushed far out, which clips the ring to the
    // arc without a second shape.
    if (sweep >= 0.0) {
      if (rel > sweep) ring += length(v_local) + 1000.0;
    } else if (rel < sweep + 2.0 * PI) {
      ring += length(v_local) + 1000.0;
    }
    d = ring;
  } else if (v_kind == 4) {                              // Segment
    d = sdSegment(v_local, v_params.xy, v_params.zw) - max(v_half.x, 0.25) * 0.5;
  } else if (v_kind == 5) {                              // Glow
    // v_params.x shapes the falloff: above 1 the core stays bright and the edge
    // falls away fast, which is what a halo behind a target wants.
    // Signed like every other distance here: negative inside, zero on the rim,
    // positive outside.  The old form clamped t to 1 and measured from the
    // other side, which left the whole exterior of the quad at half coverage --
    // a faint visible plate with hard square edges on a black field.
    float shape = max(v_params.x, 0.2);
    float t = length(v_local) / max(v_half.x, 0.5);
    d = (pow(t, shape) - 1.0) * v_half.x;
  } else if (v_kind == 6) {                              // Triangle
    float a = v_params.x;
    float c = cos(a), s = sin(a);
    d = sdEquilateral(vec2(v_local.x * c + v_local.y * s, -v_local.x * s + v_local.y * c),
                      v_half.x * 0.8660254);
  } else {                                               // Vignette
    float t = length(v_local / max(v_half, vec2(1.0)));
    fragColor = vec4(v_color.rgb, v_color.a * smoothstep(v_params.x, v_params.y, t));
    return;
  }

  float aa = max(fwidth(d), 1e-4);
  float cover = 1.0 - smoothstep(-aa, aa, d);
  if (border > 0.0) cover *= smoothstep(-aa, aa, d + border);
  if (feather > 0.0) cover *= 1.0 - smoothstep(-feather, feather, d);
  if (cover <= 0.001) discard;
  fragColor = vec4(v_color.rgb, v_color.a * cover);
}
)";

// The text program: one SDF atlas, analytic antialiasing, optional outline and
// halo.  The field is a coverage ramp with 0.5 on the contour, so one
// smoothstep at half the screen-space gradient gives a clean edge at any size.
inline constexpr char kTextVertexShader[] = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_params;
layout(location = 4) in vec4 a_shape;
uniform vec2 u_resolution;
out vec2 v_uv;
out vec4 v_color;
out vec2 v_fx;   // outline width px, glow width px
void main() {
  vec2 ndc = vec2(a_pos.x / u_resolution.x * 2.0 - 1.0, 1.0 - a_pos.y / u_resolution.y * 2.0);
  v_uv = a_uv;
  v_color = a_color;
  v_fx = a_params.xy;
  gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

inline constexpr char kTextFragmentShader[] = R"(#version 300 es
precision highp float;
in vec2 v_uv;
in vec4 v_color;
in vec2 v_fx;
uniform sampler2D u_atlas;
out vec4 fragColor;
void main() {
  float v = texture(u_atlas, v_uv).r;
  // One screen pixel expressed in field units.  fwidth is exact here because the
  // field is piecewise linear, so every glyph edge lands on the same subpixel.
  float perPixel = max(fwidth(v), 1e-5);
  float fill = smoothstep(0.5 - perPixel, 0.5 + perPixel, v);

  float outline = 0.0;
  if (v_fx.x > 0.0) {
    float o = v_fx.x * perPixel;
    outline = smoothstep(0.5 - o - perPixel, 0.5 - o + perPixel, v) * (1.0 - fill);
  }
  float glow = 0.0;
  if (v_fx.y > 0.0) {
    float g = v_fx.y * perPixel;
    glow = smoothstep(0.5 - g - perPixel, 0.5 - g + perPixel, v) * (1.0 - max(fill, outline));
  }
  if (fill <= 0.0 && outline <= 0.0 && glow <= 0.0) discard;

  float coverage = max(fill, outline);
  float energy = clamp(fill + outline * 0.5 + glow * 0.25, 0.0, 1.0);
  fragColor = vec4(v_color.rgb, v_color.a * coverage * energy);
}
)";

}  // namespace gfx
}  // namespace pp
