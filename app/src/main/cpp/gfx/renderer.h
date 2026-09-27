// The frame renderer: two programs, two buffers, two draw calls.
//
// Public API is deliberately geometric.  Screens ask for a disc or a run of
// text; nothing above this layer knows a vertex exists.  The two batches are
// plain arrays that are refilled every frame, so a frame costs one memcpy per
// batch and no allocation at all once the buffers have reached their high-water
// mark.
#pragma once

#include <cstdint>

#include "../core/math_util.h"
#include "shaders.h"

namespace pp {

struct FontAtlas;
struct Glyph;

namespace gfx {

struct TextStyle {
  const FontAtlas* face = nullptr;
  float sizePx = 16.0f;   // Em size in pixels.
  float tracking = 0.0f;  // Extra advance per glyph, in em units.
  Color color = rgb(1.0f);
  float alpha = 1.0f;
  float outlinePx = 0.0f;
  float glowPx = 0.0f;
  // In a strict black-and-white interface text is almost always white on black;
  // an outline is for the cases where text sits on a bright target.
  bool alignCenter = false;
  bool alignRight = false;
};

enum class HAlign : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Middle, Baseline, Bottom };

struct TextMetrics {
  float width = 0.0f;
  float ascent = 0.0f;
  float descent = 0.0f;
  float height = 0.0f;
};

class Renderer {
 public:
  // `maxVertices` bounds the per-frame batch; the buffer grows once if a frame
  // ever needs more and never shrinks.
  bool init(int maxVerticesPerBatch, int maxVerticesPerTextBatch);
  void shutdown();

  // Call at the top of every frame.
  void beginFrame(float widthPx, float heightPx, float frameSeconds);
  // Flushes both batches.  Safe to call with empty batches.
  void endFrame();
  // Restores a black clear colour for the next frame.
  void setClearColor(Color c);

  float width() const { return widthPx_; }
  float height() const { return heightPx_; }
  float frameSeconds() const { return frameSeconds_; }
  int shapeVertexCount() const { return shapes_.count; }
  int textVertexCount() const { return texts_.count; }

  // ---- Shapes ------------------------------------------------------------
  void rect(Rect r, Color c, float cornerRadiusPx = 0.0f, float borderWidthPx = 0.0f,
            float featherPx = 0.0f);
  void disc(Vec2 center, float radiusPx, Color c, float borderWidthPx = 0.0f);
  void ring(Vec2 center, float radiusPx, float thicknessPx, Color c);
  void ringArc(Vec2 center, float radiusPx, float thicknessPx, float startRad, float sweepRad,
               Color c);
  void line(Vec2 a, Vec2 b, float thicknessPx, Color c);
  void triangle(Vec2 center, float radiusPx, float rotationRad, Color c);
  void glow(Vec2 center, float radiusPx, Color c, float intensity = 1.0f);
  void vignette(Rect r, float innerFrac, float outerFrac, Color c);

  // ---- Text --------------------------------------------------------------
  // Measures without drawing.  `maxChars` bounds the scan; pass 0 for "long
  // enough" (measured from the null terminator).
  TextMetrics measure(const char* text, const TextStyle& style) const;
  TextMetrics measure(const char* text, int length, const TextStyle& style) const;

  // `x, y` is the anchor point selected by halign/valign.
  void text(float x, float y, const char* str, const TextStyle& style, HAlign halign = HAlign::Left,
            VAlign valign = VAlign::Baseline);
  // Draws with the pen starting at `x`; convenient for right-aligned numbers
  // that must not jitter as their digits change.
  void textRaw(float x, float baselineY, const char* str, const TextStyle& style);

 private:
  struct Batch {
    uint32_t vbo = 0;
    int* data = nullptr;
    int capacity = 0;
    int count = 0;

    bool init(int maxVertices);
    void shutdown();
    void reset() { count = 0; }
    bool push(const float* vertex);  // 16 floats
  };

  void submitShape(Batch& batch, ShapeKind kind, Rect bounds, const Color& c, float p0, float p1,
                   float p2, float p3, float overrideHalfW = 0.0f, float overrideHalfH = 0.0f);
  bool ensureCapacity(Batch& batch, int extraVertices);
  void flush(Batch& batch, uint32_t program);
  const Glyph* glyphFor(const FontAtlas& face, uint32_t codepoint) const;

  Batch shapes_;
  Batch texts_;

  uint32_t shapeProgram_ = 0;
  uint32_t textProgram_ = 0;
  uint32_t atlasTexture_ = 0;
  uint32_t shapeVao_ = 0;
  uint32_t textVao_ = 0;

  int shapeResolutionLoc_ = -1;
  int textResolutionLoc_ = -1;
  int textAtlasLoc_ = -1;

  float widthPx_ = 1.0f;
  float heightPx_ = 1.0f;
  float frameSeconds_ = 0.0f;
  Color clearColor_ = rgb(0.0f, 1.0f);
  bool fontReady_ = false;
};

}  // namespace gfx
}  // namespace pp
