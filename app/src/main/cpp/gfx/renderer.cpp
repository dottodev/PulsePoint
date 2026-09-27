#include "renderer.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "../generated/font_atlas.h"
#include "gl.h"

namespace pp {
namespace gfx {

namespace {

// Enough for a busy frame: a few hundred shapes and a few thousand glyphs.
// The buffers never shrink, so this is a one-off cost at load.
constexpr int kDefaultShapeVertices = 4096;
constexpr int kDefaultTextVertices = 8192;

uint32_t compileShader(GLenum type, const char* source, const char* label) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok == 0) {
    GLint len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
    char log[1024] = {0};
    if (len > 0) glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
    std::printf("[gl] %s shader failed: %s\n", label, log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

uint32_t linkProgram(const char* vs, const char* fs, const char* label) {
  const GLuint v = compileShader(GL_VERTEX_SHADER, vs, label);
  if (v == 0) return 0;
  const GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, label);
  if (f == 0) {
    glDeleteShader(v);
    return 0;
  }
  const GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (ok == 0) {
    GLint len = 0;
    glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
    char log[1024] = {0};
    if (len > 0) glGetProgramInfoLog(p, sizeof(log) - 1, nullptr, log);
    std::printf("[gl] %s program link failed: %s\n", label, log);
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

void setupAttributes(uint32_t vao, uint32_t vbo) {
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  const GLsizei stride = kVertexBytes;
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(8));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(16));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(32));
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(48));
  glBindVertexArray(0);
}

// UTF-8 to codepoint.  The UI only ever emits ASCII plus a handful of Latin-1
// punctuation, but decoding properly costs nothing and keeps the atlas usable.
int decodeUtf8(const char* s, uint32_t* outCp) {
  const unsigned char c = static_cast<unsigned char>(*s);
  if (c < 0x80) {
    *outCp = c;
    return 1;
  }
  if ((c & 0xE0) == 0xC0 && (static_cast<unsigned char>(s[1]) & 0xC0) == 0x80) {
    *outCp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[1]) & 0x3Fu);
    return 2;
  }
  if ((c & 0xF0) == 0xE0 && (static_cast<unsigned char>(s[1]) & 0xC0) == 0x80 &&
      (static_cast<unsigned char>(s[2]) & 0xC0) == 0x80) {
    *outCp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[1]) & 0x3Fu) << 6) |
             (static_cast<unsigned char>(s[2]) & 0x3Fu);
    return 3;
  }
  *outCp = '?';
  return 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Batch
// ---------------------------------------------------------------------------

bool Renderer::Batch::init(int maxVertices) {
  capacity = maxVertices < 6 ? 6 : maxVertices;
  data = static_cast<int*>(calloc(static_cast<size_t>(capacity) * kVertexFloats,
                                  sizeof(int)));
  if (data == nullptr) return false;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  // GL_STATIC_DRAW on a buffer we rewrite every frame is deliberate: the driver
  // is going to re-upload regardless, and asking for DYNAMIC_DRAW makes some
  // drivers pick a slower path for a buffer that is fully rewritten.
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity * kVertexBytes), nullptr,
               GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  count = 0;
  return vbo != 0;
}

void Renderer::Batch::shutdown() {
  if (data != nullptr) {
    free(data);
    data = nullptr;
  }
  if (vbo != 0) {
    glDeleteBuffers(1, &vbo);
    vbo = 0;
  }
  capacity = 0;
  count = 0;
}

bool Renderer::Batch::push(const float* vertex) {
  if (count + 1 > capacity) return false;
  float* out = reinterpret_cast<float*>(data) + static_cast<size_t>(count) * kVertexFloats;
  for (int i = 0; i < kVertexFloats; ++i) out[i] = vertex[i];
  ++count;
  return true;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool Renderer::init(int maxVerticesPerBatch, int maxVerticesPerTextBatch) {
  shapeProgram_ = linkProgram(kShapeVertexShader, kShapeFragmentShader, "shape");
  textProgram_ = linkProgram(kTextVertexShader, kTextFragmentShader, "text");
  if (shapeProgram_ == 0 || textProgram_ == 0) return false;

  shapeResolutionLoc_ = glGetUniformLocation(shapeProgram_, "u_resolution");
  textResolutionLoc_ = glGetUniformLocation(textProgram_, "u_resolution");
  textAtlasLoc_ = glGetUniformLocation(textProgram_, "u_atlas");

  glGenVertexArrays(1, &shapeVao_);
  glGenVertexArrays(1, &textVao_);

  if (!shapes_.init(maxVerticesPerBatch > 0 ? maxVerticesPerBatch : kDefaultShapeVertices)) {
    return false;
  }
  if (!texts_.init(maxVerticesPerTextBatch > 0 ? maxVerticesPerTextBatch : kDefaultTextVertices)) {
    return false;
  }
  setupAttributes(shapeVao_, shapes_.vbo);
  setupAttributes(textVao_, texts_.vbo);

  // One texture for the whole font atlas.  GL_R8 with linear filtering is the
  // correct type for a single-channel field; sRGB would bend the 0.5 contour.
  if (decodeFontAtlas()) {
    glGenTextures(1, &atlasTexture_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R8), fontAtlasWidth(), fontAtlasHeight(),
                 0, GL_RED, GL_UNSIGNED_BYTE, fontAtlasPixels());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    fontReady_ = true;
  } else {
    std::printf("[gl] font atlas failed to decode; text will be blank\n");
  }

  glDisable(GL_DEPTH_BUFFER_BIT);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  // Premultiplied output, straight input: the shaders emit colour scaled by the
  // shape's coverage, which is premultiplied, and the atlas is straight alpha.
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  return true;
}

void Renderer::shutdown() {
  shapes_.shutdown();
  texts_.shutdown();
  if (atlasTexture_ != 0) {
    glDeleteTextures(1, &atlasTexture_);
    atlasTexture_ = 0;
  }
  if (shapeVao_ != 0) {
    glDeleteVertexArrays(1, &shapeVao_);
    shapeVao_ = 0;
  }
  if (textVao_ != 0) {
    glDeleteVertexArrays(1, &textVao_);
    textVao_ = 0;
  }
  if (shapeProgram_ != 0) {
    glDeleteProgram(shapeProgram_);
    shapeProgram_ = 0;
  }
  if (textProgram_ != 0) {
    glDeleteProgram(textProgram_);
    textProgram_ = 0;
  }
  fontReady_ = false;
}

void Renderer::setClearColor(Color c) { clearColor_ = c; }

void Renderer::beginFrame(float widthPx, float heightPx, float frameSeconds) {
  widthPx_ = widthPx > 1.0f ? widthPx : 1.0f;
  heightPx_ = heightPx > 1.0f ? heightPx : 1.0f;
  frameSeconds_ = frameSeconds;
  shapes_.reset();
  texts_.reset();

  glViewport(0, 0, static_cast<GLsizei>(widthPx_), static_cast<GLsizei>(heightPx_));
  glClearColor(clearColor_.r, clearColor_.g, clearColor_.b, clearColor_.a);
  glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::flush(Batch& batch, uint32_t program) {
  if (batch.count == 0) return;
  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
  // The whole buffer is rewritten every frame, so a single full upload is both
  // simpler and faster than tracking a dirty range.
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch.count * kVertexBytes), batch.data,
               GL_STATIC_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, batch.count);
}

void Renderer::endFrame() {
  glUseProgram(shapeProgram_);
  glUniform2f(shapeResolutionLoc_, widthPx_, heightPx_);
  glBindVertexArray(shapeVao_);
  flush(shapes_, shapeProgram_);

  glUseProgram(textProgram_);
  glUniform2f(textResolutionLoc_, widthPx_, heightPx_);
  if (textAtlasLoc_ >= 0) glUniform1i(textAtlasLoc_, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, atlasTexture_);
  glBindVertexArray(textVao_);
  flush(texts_, textProgram_);

  glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------

bool Renderer::ensureCapacity(Batch& batch, int extraVertices) {
  if (batch.count + extraVertices <= batch.capacity) return true;
  // Growing mid-frame would mean reallocating during a round, which is exactly
  // what this renderer is built to avoid.  Instead the frame is truncated: the
  // caller is a screen, and a few missing decorations are invisible, whereas a
  // heap allocation on the input path is not.
  return false;
}

void Renderer::submitShape(Batch& batch, ShapeKind kind, Rect bounds, const Color& c, float p0,
                           float p1, float p2, float p3, float overrideHalfW,
                           float overrideHalfH) {
  if (c.a <= 0.001f) return;
  if (!ensureCapacity(batch, 6)) return;

  float halfW = overrideHalfW > 0.0f ? overrideHalfW : bounds.w * 0.5f;
  float halfH = overrideHalfH > 0.0f ? overrideHalfH : bounds.h * 0.5f;
  if (halfW < 0.5f) halfW = 0.5f;
  if (halfH < 0.5f) halfH = 0.5f;
  const float cx = bounds.centerX();
  const float cy = bounds.centerY();

  const float corners[4][2] = {
      {-halfW, -halfH}, {halfW, -halfH}, {halfW, halfH}, {-halfW, halfH},
  };
  // Two triangles: 0,1,2 and 0,2,3.
  static const int kIndices[6] = {0, 1, 2, 0, 2, 3};
  const float kindF = static_cast<float>(static_cast<int>(kind));

  for (int i = 0; i < 6; ++i) {
    const float* corner = corners[kIndices[i]];
    float v[kVertexFloats];
    v[0] = cx + corner[0];
    v[1] = cy + corner[1];
    v[2] = corner[0];
    v[3] = corner[1];
    v[4] = c.r;
    v[5] = c.g;
    v[6] = c.b;
    v[7] = c.a;
    v[8] = p0;
    v[9] = p1;
    v[10] = p2;
    v[11] = p3;
    v[12] = halfW;
    v[13] = halfH;
    v[14] = kindF;
    v[15] = 0.0f;
    if (!batch.push(v)) return;
  }
}

void Renderer::rect(Rect r, Color c, float cornerRadiusPx, float borderWidthPx, float featherPx) {
  submitShape(shapes_, ShapeKind::Rect, r, c, cornerRadiusPx, borderWidthPx, featherPx, 0.0f);
}

void Renderer::disc(Vec2 center, float radiusPx, Color c, float borderWidthPx) {
  const float r = radiusPx < 0.5f ? 0.5f : radiusPx;
  submitShape(shapes_, ShapeKind::Disc, Rect::fromCenter(center, r * 2.0f, r * 2.0f), c,
              borderWidthPx, 0.0f, 0.0f, 0.0f, r, r);
}

void Renderer::ring(Vec2 center, float radiusPx, float thicknessPx, Color c) {
  const float r = radiusPx < 0.5f ? 0.5f : radiusPx;
  submitShape(shapes_, ShapeKind::Ring, Rect::fromCenter(center, r * 2.0f, r * 2.0f), c,
              thicknessPx, 0.0f, 0.0f, 0.0f, r, r);
}

void Renderer::ringArc(Vec2 center, float radiusPx, float thicknessPx, float startRad,
                       float sweepRad, Color c) {
  const float r = radiusPx < 0.5f ? 0.5f : radiusPx;
  // The quad is the whole box, and the radius travels separately, so a thick arc
  // is never clipped by its own bounding box.
  const float box = r * 2.0f + thicknessPx + 2.0f;
  submitShape(shapes_, ShapeKind::RingArc, Rect::fromCenter(center, box, box), c, thicknessPx,
              startRad, sweepRad, r);
}

void Renderer::line(Vec2 a, Vec2 b, float thicknessPx, Color c) {
  const float t = thicknessPx < 0.5f ? 0.5f : thicknessPx;
  const float minX = (a.x < b.x ? a.x : b.x) - t;
  const float minY = (a.y < b.y ? a.y : b.y) - t;
  const float maxX = (a.x > b.x ? a.x : b.x) + t;
  const float maxY = (a.y > b.y ? a.y : b.y) + t;
  Rect box{minX, minY, maxX - minX, maxY - minY};
  // Endpoints are stored relative to the box centre, which is what the fragment
  // shader works in.
  const Vec2 mid = box.center();
  submitShape(shapes_, ShapeKind::Segment, box, c, a.x - mid.x, a.y - mid.y, b.x - mid.x,
              b.y - mid.y, t, 0.0f);
}

void Renderer::triangle(Vec2 center, float radiusPx, float rotationRad, Color c) {
  const float r = radiusPx < 0.5f ? 0.5f : radiusPx;
  const float box = r * 2.4f;
  submitShape(shapes_, ShapeKind::Triangle, Rect::fromCenter(center, box, box), c, rotationRad, 0.0f,
              0.0f, 0.0f, r, r);
}

void Renderer::glow(Vec2 center, float radiusPx, Color c, float intensity) {
  const float r = radiusPx < 0.5f ? 0.5f : radiusPx;
  submitShape(shapes_, ShapeKind::Glow, Rect::fromCenter(center, r * 2.0f, r * 2.0f), c, intensity,
              0.0f, 0.0f, 0.0f, r, r);
}

void Renderer::vignette(Rect r, float innerFrac, float outerFrac, Color c) {
  submitShape(shapes_, ShapeKind::Vignette, r, c, innerFrac, outerFrac, 0.0f, 0.0f);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

const Glyph* Renderer::glyphFor(const FontAtlas& face, uint32_t codepoint) const {
  // Both atlases hold a few hundred glyphs in codepoint order, so a binary
  // search beats a hash table at this size and needs no allocation.
  int lo = 0;
  int hi = face.glyphCount - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) >> 1;
    const int32_t cp = face.glyphs[mid].codepoint;
    if (cp == static_cast<int32_t>(codepoint)) return &face.glyphs[mid];
    if (cp < static_cast<int32_t>(codepoint)) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return nullptr;
}

TextMetrics Renderer::measure(const char* text, int length, const TextStyle& style) const {
  TextMetrics m;
  if (text == nullptr || style.face == nullptr) return m;
  const FontAtlas& face = *style.face;
  m.ascent = face.ascent * style.sizePx;
  m.descent = face.descent * style.sizePx;
  m.height = face.lineHeight * style.sizePx;

  const int n = length > 0 ? length : static_cast<int>(std::strlen(text));
  float pen = 0.0f;
  int i = 0;
  while (i < n) {
    uint32_t cp = 0;
    i += decodeUtf8(text + i, &cp);
    const Glyph* g = glyphFor(face, cp);
    if (g == nullptr) g = glyphFor(face, '?');
    if (g != nullptr) {
      pen += (g->advance + style.tracking) * style.sizePx;
    }
  }
  // Trailing tracking is not part of the visible width.
  if (n > 0) pen -= style.tracking * style.sizePx;
  m.width = pen > 0.0f ? pen : 0.0f;
  return m;
}

TextMetrics Renderer::measure(const char* text, const TextStyle& style) const {
  return measure(text, 0, style);
}

void Renderer::textRaw(float x, float baselineY, const char* str, const TextStyle& style) {
  if (str == nullptr || style.face == nullptr || style.alpha <= 0.001f) return;
  if (!fontReady_) return;
  const FontAtlas& face = *style.face;
  const float s = style.sizePx;
  const Color c = withAlpha(style.color, style.alpha);

  float pen = x;
  const int n = static_cast<int>(std::strlen(str));
  int i = 0;
  while (i < n) {
    uint32_t cp = 0;
    i += decodeUtf8(str + i, &cp);
    const Glyph* g = glyphFor(face, cp);
    if (g == nullptr) g = glyphFor(face, '?');
    if (g == nullptr) continue;
    const float advance = (g->advance + style.tracking) * s;
    if (g->u1 > g->u0) {
      if (!ensureCapacity(texts_, 6)) return;
      const float x0 = pen + g->planeLeft * s;
      const float x1 = pen + g->planeRight * s;
      const float y0 = baselineY - g->planeTop * s;   // plane top -> smaller y
      const float y1 = baselineY - g->planeBottom * s;

      const float xs[4] = {x0, x1, x1, x0};
      const float ys[4] = {y0, y0, y1, y1};
      const float us[4] = {g->u0, g->u1, g->u1, g->u0};
      const float vs[4] = {g->v0, g->v0, g->v1, g->v1};
      static const int kIdx[6] = {0, 1, 2, 0, 2, 3};
      for (int k = 0; k < 6; ++k) {
        const int q = kIdx[k];
        float v[kVertexFloats];
        v[0] = xs[q];
        v[1] = ys[q];
        v[2] = us[q];
        v[3] = vs[q];
        v[4] = c.r;
        v[5] = c.g;
        v[6] = c.b;
        v[7] = c.a;
        v[8] = style.outlinePx;
        v[9] = style.glowPx;
        v[10] = 0.0f;
        v[11] = 0.0f;
        v[12] = 0.0f;
        v[13] = 0.0f;
        v[14] = 0.0f;
        v[15] = 0.0f;
        if (!texts_.push(v)) return;
      }
    }
    pen += advance;
  }
}

void Renderer::text(float x, float y, const char* str, const TextStyle& style, HAlign halign,
                    VAlign valign) {
  if (str == nullptr || style.face == nullptr) return;
  const TextMetrics m = measure(str, style);
  float px = x;
  if (halign == HAlign::Center) {
    px = x - m.width * 0.5f;
  } else if (halign == HAlign::Right) {
    px = x - m.width;
  }
  float baseline = y;
  switch (valign) {
    case VAlign::Top: baseline = y + m.ascent; break;
    case VAlign::Middle: baseline = y + (m.ascent - m.descent) * 0.5f; break;
    case VAlign::Baseline: baseline = y; break;
    case VAlign::Bottom: baseline = y - m.descent; break;
  }
  textRaw(px, baseline, str, style);
}

}  // namespace gfx
}  // namespace pp
