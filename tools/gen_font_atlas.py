#!/usr/bin/env python3
"""
PulsePoint font atlas generator.

Rasterises the two bundled OpenType fonts into a single-channel signed-distance
field (SDF) atlas and emits a self-contained C++ translation unit that the game
links against.  Nothing is read from disk at runtime: the atlas is embedded as
base64 in the generated source, so the Android build has no asset pipeline, no
PNG decoder and no font rasteriser dependency.

Output:
  app/src/main/cpp/generated/font_atlas.h
  app/src/main/cpp/generated/font_atlas_data.cpp

Usage:
  python3 tools/gen_font_atlas.py
"""

import base64
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import distance_transform_edt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT_DIR = os.path.join(ROOT, "third_party", "fonts")
OUT_DIR = os.path.join(ROOT, "app", "src", "main", "cpp", "generated")

# Rasterisation parameters.
EM_PX = 32          # em size of the baked SDF
SPREAD_PX = 5.0     # distance range encoded into the 0..1 field
PAD_PX = int(SPREAD_PX) + 2
SUPERSAMPLE = 2     # rasterise at 2x then box-downsample for a clean gradient

# Display face: title, headings, mode names, hero numerals.
# Technical face: body copy, labels, statistics tables.
# name, file, variable-font weight axis
FACES = [
    ("display", "Orbitron.ttf", 900),
    ("tech", "JetBrainsMono.ttf", 700),
]

# ASCII plus the small set of typographic marks that both faces actually carry.
# Anything else (arrows, stars, ticks) is drawn as geometry by the UI instead,
# which also keeps the interface visually consistent between the two faces.
CHARSET = [chr(c) for c in range(0x20, 0x7F)] + [
    "\u00b0",  # DEGREE SIGN
    "\u00d7",  # MULTIPLICATION SIGN
    "\u2013",  # EN DASH
    "\u2014",  # EM DASH
    "\u2018",  # LEFT SINGLE QUOTE
    "\u2019",  # RIGHT SINGLE QUOTE
    "\u201c",  # LEFT DOUBLE QUOTE
    "\u201d",  # RIGHT DOUBLE QUOTE
]

ATLAS_MAX_WIDTH = 1024


def glyph_sdf(font_ss, font_unit, ch):
    """Rasterise one glyph and return (field, plane_box, advance_em).

    plane_box is (left, bottom, right, top) in em units relative to the pen
    origin on the baseline, with +y pointing up, already rounded outwards to
    the atlas pixel grid.  field is float32 with 1.0 deep inside the glyph,
    0.5 on the contour and 0.0 outside.
    """
    advance = font_unit.getlength(ch) / EM_PX
    bbox = font_unit.getbbox(ch, anchor="ls")
    if bbox is None:
        bbox = (0, 0, 0, 0)
    # Pillow reports the anchor-relative layout box with y growing downwards,
    # so y0 is the top edge (negative == above the baseline) and y1 the bottom.
    x0, y0, x1, y1 = (float(v) for v in bbox)
    up = -y0
    down = -y1
    if x1 - x0 <= 0.0 or (up - down) <= 0.0:
        return None, (0.0, 0.0, 0.0, 0.0), advance

    # Plane box: layout bounds grown by the padding, rounded outwards so the
    # atlas cell and the reported plane agree to the pixel.
    pl = math.floor(x0 - PAD_PX)
    pb = math.floor(down - PAD_PX)
    pr = math.ceil(x1 + PAD_PX)
    pt = math.ceil(up + PAD_PX)
    cw = pr - pl
    chh = pt - pb

    ss = SUPERSAMPLE
    canvas = Image.new("L", (cw * ss, chh * ss), 0)
    draw = ImageDraw.Draw(canvas)
    # Canvas row 0 sits at plane-y == pt and canvas column 0 at plane-x == pl.
    draw.text((-pl * ss, pt * ss), ch, font=font_ss, fill=255, anchor="ls")
    # Box-downsample to 1:1 for a smooth distance ramp.
    small = canvas.resize((cw, chh), Image.BOX)
    alpha = np.asarray(small, dtype=np.float32) / 255.0

    # Hard mask for the Euclidean transforms, using the 0.5 iso-level of the
    # antialiased coverage so partially covered pixels land on the contour.
    inside = alpha >= 0.5
    if not inside.any() or inside.all():
        return None, (0.0, 0.0, 0.0, 0.0), advance

    d_out = distance_transform_edt(~inside)
    d_in = distance_transform_edt(inside)
    sd = d_out - d_in  # >0 outside, <0 inside, in pixels

    # Coverage-style field: 1.0 deep inside, 0.5 on the contour, 0.0 outside.
    field = 0.5 - sd / (2.0 * SPREAD_PX)
    np.clip(field, 0.0, 1.0, out=field)

    # Plane box in em units, already snapped to the atlas pixel grid above, so
    # that it matches the em-unit advance reported for the glyph.
    return (
        field.astype(np.float32),
        (pl / EM_PX, pb / EM_PX, pr / EM_PX, pt / EM_PX),
        advance,
    )


def pack(cells, width=ATLAS_MAX_WIDTH):
    """Shelf-pack cells [(w, h, payload_index)] into a single-row-per-shelf bin.

    Returns (width, height, placements) where placements[i] == (x, y) for cells[i].
    """
    order = sorted(range(len(cells)), key=lambda i: (-cells[i][1], cells[i][0]))

    placements = [None] * len(cells)
    x = 0
    y = 0
    shelf_h = 0
    for i in order:
        w, h, _ = cells[i]
        if x + w > width:
            x = 0
            y += shelf_h
            shelf_h = 0
        placements[i] = (x, y)
        x += w
        shelf_h = max(shelf_h, h)
    height = y + shelf_h
    return width, height, placements


def build_face(path, weight):
    def load(size):
        f = ImageFont.truetype(path, size)
        try:
            f.set_variation_by_axes([weight])
        except Exception:
            pass
        return f

    font_ss = load(EM_PX * SUPERSAMPLE)
    # Metrics are always measured at 1x so plane units are in em-px.
    font_unit = load(EM_PX)

    ascent, descent = font_unit.getmetrics()
    entries = []
    for ch in CHARSET:
        field, plane, advance = glyph_sdf(font_ss, font_unit, ch)
        if field is not None:
            h, w = field.shape
        else:
            w = h = 0
            plane = (0.0, 0.0, 0.0, 0.0)
        entries.append(
            {
                "cp": ord(ch),
                "ch": ch,
                "field": field,
                "w": w,
                "h": h,
                "plane": plane,
                "advance": advance,
            }
        )

    # Fonts substitute a .notdef box for codepoints they do not cover, which
    # would bake a stray rectangle into the atlas.  Rasterise a codepoint that
    # is guaranteed to be unmapped and treat an identical bitmap as missing.
    def ink_signature(ch):
        m = font_unit.getmask(ch, anchor="ls")
        return (m.size, bytes(bytearray(m)))

    notdef = ink_signature("\ue000")
    missing = []
    for e in entries:
        if e["field"] is None or e["ch"] == " ":
            continue
        if ink_signature(e["ch"]) == notdef:
            missing.append(e["ch"])
    if missing:
        print("  %s: no glyph for %s" % (path.rsplit("/", 1)[-1], " ".join(missing)))
    entries = [
        e for e in entries if e["field"] is not None or e["ch"] == " "
    ]
    entries = [e for e in entries if e["ch"] not in missing]

    cells = [
        (e["w"], e["h"], i) for i, e in enumerate(entries) if e["field"] is not None
    ]
    width, height, cell_pos = pack(cells)
    pos_of_entry = {}
    for slot, (w, h, entry_index) in enumerate(cells):
        pos_of_entry[entry_index] = cell_pos[slot]
    for i, e in enumerate(entries):
        e["px"], e["py"] = pos_of_entry.get(i, (0, 0))

    atlas = np.zeros((height, width), dtype=np.float32)
    for i, e in enumerate(entries):
        if e["field"] is None:
            continue
        px, py = e["px"], e["py"]
        atlas[py : py + e["h"], px : px + e["w"]] = e["field"]

    return {
        "entries": entries,
        "atlas": atlas,
        "width": width,
        "height": height,
        "ascent": ascent / EM_PX,
        "descent": descent / EM_PX,
    }


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    faces = {}
    for name, filename, weight in FACES:
        path = os.path.join(FONT_DIR, filename)
        if not os.path.exists(path):
            sys.exit("missing font: %s" % path)
        print("rasterising %s (weight %d) ..." % (name, weight))
        faces[name] = build_face(path, weight)

    # Merge every face into one atlas image.
    total_w = max(f["width"] for f in faces.values())
    total_h = sum(f["height"] for f in faces.values())
    merged = np.zeros((total_h, total_w), dtype=np.float32)
    offsets = {}
    y = 0
    for name, _filename, _weight in FACES:
        f = faces[name]
        merged[y : y + f["height"], : f["width"]] = f["atlas"]
        offsets[name] = y
        y += f["height"]

    # Quantise, then emit an 8-bit greyscale PNG.  The runtime inflates it with
    # zlib, so the generated source stays small and no asset path is involved.
    quant = np.rint(np.clip(merged, 0.0, 1.0) * 255.0).astype(np.uint8)
    img = Image.fromarray(quant, mode="L")
    tmp_png = os.path.join(OUT_DIR, "font_atlas.png")
    img.save(tmp_png, optimize=True, compress_level=9)
    with open(tmp_png, "rb") as fh:
        payload = fh.read()
    os.remove(tmp_png)

    print(
        "atlas %dx%d  raw=%d KiB  png=%d KiB  base64=%d KiB"
        % (
            total_w,
            total_h,
            quant.size / 1024.0,
            len(payload) / 1024.0,
            len(payload) * 4 / 3 / 1024.0,
        )
    )

    b64 = base64.b64encode(payload).decode("ascii")
    lines_b64 = [b64[i : i + 96] for i in range(0, len(b64), 96)]

    out = []
    w = out.append
    w("// Generated by tools/gen_font_atlas.py -- do not edit by hand.")
    w("//")
    w("// Source faces (SIL Open Font License 1.1, see third_party/fonts):")
    for name, filename, weight in FACES:
        w("//   %-8s <- %s (wght=%d)" % (name, filename, weight))
    w("//")
    w("// Single-channel SDF atlas stored as a coverage field: 1.0 deep inside a")
    w("// glyph, 0.5 exactly on the contour, 0.0 outside.  Plane coordinates are in")
    w("// em units relative to the pen origin on the baseline, with +y up.")
    w("// Glyph uv values are absolute atlas coordinates for the whole texture.")
    w("//")
    w("// The image is an 8-bit greyscale PNG, base64 encoded below; the runtime")
    w("// inflates it with zlib, so no asset path or image decoder is needed.")
    w("")
    w('#include "font_atlas.h"')
    w("")
    w("#include <cstring>")
    w("")
    w("namespace pp {")
    w("namespace {")
    w("")

    for name, _filename, _weight in FACES:
        f = faces[name]
        n = len(f["entries"])
        w("const Glyph kGlyphs_%s[%d] = {" % (name, n))
        for e in f["entries"]:
            px, py = e["px"], e["py"]
            u0 = px / float(total_w)
            v0 = (py + offsets[name]) / float(total_h)
            u1 = (px + e["w"]) / float(total_w)
            v1 = (py + offsets[name] + e["h"]) / float(total_h)
            pl, pb, pr, pt = e["plane"]
            w(
                "  {%.6ff,%.6ff,%.6ff,%.6ff, %.5ff,%.5ff,%.5ff,%.5ff, %.5ff, %d},"
                % (u0, v0, u1, v1, pl, pb, pr, pt, e["advance"], e["cp"])
            )
        w("};")
        w("")

    w("}  // namespace")
    w("")
    w("const FontAtlas kFontDisplay = {")
    w('  "display",')
    w("  kGlyphs_display,")
    w("  %d,  // glyphCount" % len(faces["display"]["entries"]))
    w("  %d,  // emPx" % EM_PX)
    w("  %d,  // texWidth" % total_w)
    w("  %d,  // texHeight" % total_h)
    w("  %.6ff,  // ascent" % faces["display"]["ascent"])
    w("  %.6ff,  // descent" % faces["display"]["descent"])
    w("  %.6ff,  // lineHeight" % (faces["display"]["ascent"] + faces["display"]["descent"]))
    w("};")
    w("")
    w("const FontAtlas kFontTech = {")
    w('  "tech",')
    w("  kGlyphs_tech,")
    w("  %d,  // glyphCount" % len(faces["tech"]["entries"]))
    w("  %d,  // emPx" % EM_PX)
    w("  %d,  // texWidth" % total_w)
    w("  %d,  // texHeight" % total_h)
    w("  %.6ff,  // ascent" % faces["tech"]["ascent"])
    w("  %.6ff,  // descent" % faces["tech"]["descent"])
    w("  %.6ff,  // lineHeight" % (faces["tech"]["ascent"] + faces["tech"]["descent"]))
    w("};")
    w("")
    w("// 8-bit greyscale PNG of the SDF field, base64 encoded.  Inflated by")
    w("// font_atlas_decode.cpp with zlib at startup.")
    w("const char kAtlasBase64[] =")
    for ln in lines_b64:
        w('  "%s"' % ln)
    w("  ;")
    w("")
    w("}  // namespace pp")
    w("")

    with open(os.path.join(OUT_DIR, "font_atlas_data.cpp"), "w") as fh:
        fh.write("\n".join(out))

    header = []
    hw = header.append
    hw("// Generated by tools/gen_font_atlas.py -- do not edit by hand.")
    hw("#pragma once")
    hw("")
    hw("#include <cstdint>")
    hw("")
    hw("namespace pp {")
    hw("")
    hw("struct Glyph {")
    hw("  float u0, v0, u1, v1;                  // atlas uv, v grows downwards")
    hw("  float planeLeft, planeBottom, planeRight, planeTop;  // em units, +y up")
    hw("  float advance;                        // em units")
    hw("  int32_t codepoint;")
    hw("};")
    hw("")
    hw("struct FontAtlas {")
    hw("  const char* name;")
    hw("  const Glyph* glyphs;")
    hw("  int glyphCount;")
    hw("  int emPx;")
    hw("  int texWidth;")
    hw("  int texHeight;")
    hw("  float ascent;       // em units above the baseline")
    hw("  float descent;      // em units below the baseline (positive)")
    hw("  float lineHeight;   // em units")
    hw("};")
    hw("")
    hw("extern const FontAtlas kFontDisplay;")
    hw("extern const FontAtlas kFontTech;")
    hw("")
    hw("// Base64 of an 8-bit greyscale PNG holding the whole atlas.")
    hw("extern const char kAtlasBase64[];")
    hw("")
    hw("// Inflates the embedded SDF atlas. Returns false only on a corrupt build.")
    hw("bool decodeFontAtlas();")
    hw("")
    hw("// R8 atlas pixels; valid once decodeFontAtlas() has returned true.")
    hw("const uint8_t* fontAtlasPixels();")
    hw("int fontAtlasWidth();")
    hw("int fontAtlasHeight();")
    hw("")
    hw("}  // namespace pp")
    with open(os.path.join(OUT_DIR, "font_atlas.h"), "w") as fh:
        fh.write("\n".join(header) + "\n")

    print("wrote %s" % os.path.join(OUT_DIR, "font_atlas.h"))
    print("wrote %s" % os.path.join(OUT_DIR, "font_atlas_data.cpp"))


if __name__ == "__main__":
    main()
