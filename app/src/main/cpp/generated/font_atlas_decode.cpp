// Startup decode of the embedded SDF atlas.
//
// The atlas ships as a base64 8-bit greyscale PNG inside the binary, which
// keeps the Android build free of an asset pipeline, a filesystem path and an
// image decoder library.  Decoding is a base64 pass, one zlib inflate and the
// five PNG row filters, and it happens once during load.
//
// Only what the generator emits is accepted: 8 bits per channel, greyscale,
// non-interlaced, no ancillary chunks.  Anything else is rejected rather than
// guessed at, so a bad build fails loudly instead of rendering garbage.
#include "font_atlas.h"

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace pp {
namespace {

constexpr uint8_t kPngSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

std::vector<uint8_t> g_pixels;
int g_width = 0;
int g_height = 0;
bool g_decoded = false;

int8_t base64Value(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
  if (c >= 'a' && c <= 'z') return static_cast<int8_t>(c - 'a' + 26);
  if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0' + 52);
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

bool decodeBase64(const char* in, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(strlen(in) * 3 / 4 + 4);
  uint32_t acc = 0;
  int bits = 0;
  for (const char* p = in; *p != '\0'; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '=') break;
    const int8_t v = base64Value(c);
    if (v < 0) continue;  // Whitespace and newlines in the literal.
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFFu));
    }
  }
  return out.size() >= 8;
}

uint32_t readBE32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

int paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = p > a ? p - a : a - p;
  const int pb = p > b ? p - b : b - p;
  const int pc = p > c ? p - c : c - p;
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

}  // namespace

bool decodeFontAtlas() {
  if (g_decoded) return true;

  std::vector<uint8_t> png;
  if (!decodeBase64(kAtlasBase64, png)) return false;
  if (png.size() < 8 || std::memcmp(png.data(), kPngSignature, 8) != 0) return false;

  int width = 0;
  int height = 0;
  std::vector<uint8_t> idat;
  size_t offset = 8;
  bool sawHeader = false;

  while (offset + 8 <= png.size()) {
    const uint32_t length = readBE32(png.data() + offset);
    const char* type = reinterpret_cast<const char*>(png.data() + offset + 4);
    const size_t dataAt = offset + 8;
    if (dataAt + length + 4 > png.size()) return false;

    if (std::memcmp(type, "IHDR", 4) == 0) {
      if (length < 13) return false;
      width = static_cast<int>(readBE32(png.data() + dataAt));
      height = static_cast<int>(readBE32(png.data() + dataAt + 4));
      const uint8_t bitDepth = png[dataAt + 8];
      const uint8_t colorType = png[dataAt + 9];
      const uint8_t compression = png[dataAt + 10];
      const uint8_t filterMethod = png[dataAt + 11];
      const uint8_t interlace = png[dataAt + 12];
      if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
      if (bitDepth != 8 || colorType != 0) return false;
      if (compression != 0 || filterMethod != 0 || interlace != 0) return false;
      sawHeader = true;
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      idat.insert(idat.end(), png.begin() + static_cast<long>(dataAt),
                  png.begin() + static_cast<long>(dataAt + length));
    } else if (std::memcmp(type, "IEND", 4) == 0) {
      break;
    }
    offset = dataAt + length + 4;
  }

  if (!sawHeader || idat.empty()) return false;

  // A greyscale row is one filter byte plus `width` samples.
  const size_t stride = static_cast<size_t>(width) + 1u;
  const size_t rawSize = stride * static_cast<size_t>(height);
  std::vector<uint8_t> raw(rawSize);
  uLongf destLen = static_cast<uLongf>(rawSize);
  if (uncompress(raw.data(), &destLen, idat.data(), static_cast<uLong>(idat.size())) != Z_OK) {
    return false;
  }
  if (destLen != rawSize) return false;

  // Reused across calls so a repeated decodeFontAtlas() costs nothing.
  std::vector<uint8_t>& out = g_pixels;
  out.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);

  for (int y = 0; y < height; ++y) {
    const uint8_t* line = raw.data() + stride * static_cast<size_t>(y);
    const uint8_t filter = line[0];
    const uint8_t* src = line + 1;
    uint8_t* dst = out.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
    const uint8_t* prior = y > 0 ? dst - width : nullptr;

    switch (filter) {
      case 0:
        std::memcpy(dst, src, static_cast<size_t>(width));
        break;
      case 1:
        for (int x = 0; x < width; ++x) {
          const int a = x > 0 ? dst[x - 1] : 0;
          dst[x] = static_cast<uint8_t>(src[x] + a);
        }
        break;
      case 2:
        for (int x = 0; x < width; ++x) {
          const int b = prior != nullptr ? prior[x] : 0;
          dst[x] = static_cast<uint8_t>(src[x] + b);
        }
        break;
      case 3:
        for (int x = 0; x < width; ++x) {
          const int a = x > 0 ? dst[x - 1] : 0;
          const int b = prior != nullptr ? prior[x] : 0;
          dst[x] = static_cast<uint8_t>(src[x] + ((a + b) >> 1));
        }
        break;
      case 4:
        for (int x = 0; x < width; ++x) {
          const int a = x > 0 ? dst[x - 1] : 0;
          const int b = prior != nullptr ? prior[x] : 0;
          const int c = (x > 0 && prior != nullptr) ? prior[x - 1] : 0;
          dst[x] = static_cast<uint8_t>(src[x] + paeth(a, b, c));
        }
        break;
      default:
        return false;  // Unknown filter: a corrupt build, not a warning.
    }
  }

  g_width = width;
  g_height = height;
  g_decoded = true;
  return true;
}

const uint8_t* fontAtlasPixels() { return g_pixels.empty() ? nullptr : g_pixels.data(); }
int fontAtlasWidth() { return g_width; }
int fontAtlasHeight() { return g_height; }

}  // namespace pp
