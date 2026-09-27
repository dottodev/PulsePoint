#!/usr/bin/env python3
"""Offline preview of the generated SDF atlas.

Renders sample strings with a CPU reference of the shader maths so the baked
atlas can be eyeballed without building the app.  Not part of the game.

    python3 tools/preview_font.py out.png
"""

import base64
import io
import re
import sys

import numpy as np
from PIL import Image

SRC = "app/src/main/cpp/generated/font_atlas_data.cpp"
NUM = r"(-?\d+\.?\d*)f?"


def load(path=SRC):
    src = open(path).read()
    b64 = "".join(re.findall(r'^  "([A-Za-z0-9+/=]+)"$', src, re.M))
    img = Image.open(io.BytesIO(base64.b64decode(b64))).convert("L")
    field = np.asarray(img, dtype=np.float32) / 255.0

    def parse(name):
        m = re.search(r"const Glyph kGlyphs_%s\[\d+\] = \{(.*?)\n\};" % name, src, re.S)
        rows = [[float(x) for x in re.findall(NUM, r)] for r in re.findall(r"\{([^{}]*)\},", m.group(1))]
        return {
            int(r[9]): dict(u0=r[0], v0=r[1], u1=r[2], v1=r[3], pl=r[4], pb=r[5], pr=r[6], pt=r[7], adv=r[8])
            for r in rows
        }

    def atlas(name):
        b = re.search(r"const FontAtlas kFont%s = \{(.*?)\};" % name, src, re.S).group(1)
        n = [int(x) for x in re.findall(r"^\s*(\d+),\s*//", b, re.M)]
        return dict(emPx=n[1], w=n[2], h=n[3])

    return field, {"display": (parse("display"), atlas("Display")), "tech": (parse("tech"), atlas("Tech"))}


def render(field, faces, text, face, px, width, track=0.06, spread=5.0):
    glyphs, meta = faces[face]
    # Plane coordinates and advances are in em units, so the scale is the pixel
    # size directly.  The field was baked at meta["emPx"].
    glyphs = {k: v for k, v in glyphs.items()}
    for cp in list(glyphs):
        if cp not in glyphs:
            del glyphs[cp]
    text = "".join(c if ord(c) in glyphs else "?" for c in text)
    adv = [glyphs[ord(c)]["adv"] for c in text]
    total = sum(adv) * px + track * px * (len(text) - 1)
    height = int(px * 1.8)
    out = np.zeros((height, width), np.float32)
    pen = (width - total) / 2.0
    baseline = height * 0.72
    # Half-width, in field units, of the antialias ramp for a 1px screen pixel.
    aa = 0.5 * meta["emPx"] / (2.0 * spread * px)
    for ch, a in zip(text, adv):
        g = glyphs[ord(ch)]
        x0, x1 = pen + g["pl"] * px, pen + g["pr"] * px
        y0, y1 = baseline - g["pt"] * px, baseline - g["pb"] * px
        if g["u1"] > g["u0"] and x1 > x0 and y1 > y0:
            for yy in range(max(0, int(y0)), min(height, int(y1) + 1)):
                # Screen y grows downwards, and the atlas row 0 is the glyph top.
                vv = g["v0"] + (g["v1"] - g["v0"]) * ((yy + 0.5 - y0) / (y1 - y0))
                for xx in range(max(0, int(x0)), min(width, int(x1) + 1)):
                    uu = g["u0"] + (g["u1"] - g["u0"]) * ((xx + 0.5 - x0) / (x1 - x0))
                    fx, fy = min(max(uu * meta["w"] - 0.5, 0), meta["w"] - 1.001), min(
                        max(vv * meta["h"] - 0.5, 0), meta["h"] - 1.001
                    )
                    ix, iy = int(fx), int(fy)
                    dx, dy = fx - ix, fy - iy
                    v = (
                        field[iy, ix] * (1 - dx) * (1 - dy)
                        + field[iy, ix + 1] * dx * (1 - dy)
                        + field[iy + 1, ix] * (1 - dx) * dy
                        + field[iy + 1, ix + 1] * dx * dy
                    )
                    out[yy, xx] = max(out[yy, xx], np.clip((v - 0.5 + aa) / (2 * aa), 0, 1))
        pen += a * px + track * px
    return (255 * (1 - out)).astype(np.uint8)


def main():
    field, faces = load()
    out_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/pulsepoint_font_preview.png"
    samples = [
        ("PULSEPOINT", "display", 44),
        ("TEST YOUR REFLEX.", "tech", 21),
        ("187 MS", "display", 76),
        ("REFLEX   FOCUS   FLICK   MAX", "display", 20),
        ("10,428 / 10,000 PRESSES  \u00b7  95.4%", "tech", 17),
        ("ACHIEVEMENT UNLOCKED  \u00d7  \u00b0  \u2014  \u201cQ\u201d", "display", 18),
        ("REFLEX  \u00b7  BEST 187 MS  \u00b7  AVG 214 MS", "tech", 14),
    ]
    w = 1000
    total_h = sum(int(s * 1.78) + 10 for _, _, s in samples) + 12
    canvas = Image.new("L", (w, total_h), 0)
    y = 6
    for text, face, size in samples:
        canvas.paste(Image.fromarray(render(field, faces, text, face, size, w - 20)), (10, y))
        y += int(size * 1.78) + 10
    canvas.save(out_path)
    print("wrote %s" % out_path)


if __name__ == "__main__":
    main()
