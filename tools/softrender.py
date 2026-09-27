#!/usr/bin/env python3
"""
Rasterises the game's recorded draw calls into PNGs.

A CPU reference of the same maths the fragment shaders use, so the output is what
the GPU should be producing.  The vertices come from the real renderer via
hosttest/softrender.cpp, so the layout, the colours and the text are the
game's, not a reimplementation of them.

    python3 tools/build_host_tests.py pulsepoint_softrender
    ./build-host/pulsepoint_softrender build-host/frames
    python3 tools/softrender.py build-host/frames

Shapes are the same signed distance fields, evaluated in pixels, with fwidth
replaced by an exact one-pixel width.  Text is the same coverage field, sampled
bilinearly.
"""

import os
import re
import struct
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ATLAS_SRC = os.path.join(ROOT, "app/src/main/cpp/generated/font_atlas_data.cpp")

VERT_FLOATS = 16
VERT_BYTES = VERT_FLOATS * 4
VERT_PER_QUAD = 6

# Mirrors gfx::ShapeKind.
RECT, DISC, RING, RING_ARC, SEGMENT, GLOW, TRIANGLE, VIGNETTE = range(8)

SPREAD_PX = 5.0
EM_PX = 32.0


# ---------------------------------------------------------------------------
# Font atlas
# ---------------------------------------------------------------------------


def load_atlas():
    src = open(ATLAS_SRC).read()
    b64 = "".join(re.findall(r'^  "([A-Za-z0-9+/=]+)"$', src, re.M))
    import base64
    import io

    img = Image.open(io.BytesIO(base64.b64decode(b64))).convert("L")
    return np.asarray(img, dtype=np.float32) / 255.0


def sample_bilinear(field, u, v):
    """u is (1, W), v is (H, 1); the result is (H, W)."""
    h, w = field.shape
    x = np.clip(np.broadcast_to(u, (v.shape[0], u.shape[1])) * w - 0.5, 0.0, w - 1.001)
    y = np.clip(np.broadcast_to(v, (v.shape[0], u.shape[1])) * h - 0.5, 0.0, h - 1.001)
    x0 = x.astype(np.int32)
    y0 = y.astype(np.int32)
    fx = x - x0
    fy = y - y0
    return (
        field[y0, x0] * (1 - fx) * (1 - fy)
        + field[y0, x0 + 1] * fx * (1 - fy)
        + field[y0 + 1, x0] * (1 - fx) * fy
        + field[y0 + 1, x0 + 1] * fx * fy
    )


# ---------------------------------------------------------------------------
# Shape maths, matching the fragment shaders
# ---------------------------------------------------------------------------


def sd_rounded_box(px, py, hw, hh, r):
    r = np.minimum(np.minimum(r, hw), hh)
    qx = np.abs(px) - (hw - r)
    qy = np.abs(py) - (hh - r)
    outside = np.sqrt(np.maximum(qx, 0.0) ** 2 + np.maximum(qy, 0.0) ** 2)
    return outside + np.minimum(np.maximum(qx, qy), 0.0) - r


def sd_segment(px, py, ax, ay, bx, by):
    pax, pay = px - ax, py - ay
    bax, bay = bx - ax, by - ay
    denom = max(bax * bax + bay * bay, 1e-6)
    h = np.clip((pax * bax + pay * bay) / denom, 0.0, 1.0)
    return np.sqrt((pax - bax * h) ** 2 + (pay - bay * h) ** 2)


def sd_equilateral(px, py, r):
    k = 1.7320508075688772
    qx = np.abs(px) - r
    qy = py + r / k
    flip = (qx + k * qy) > 0.0
    nx = np.where(flip, (qx - k * qy) * 0.5, qx)
    ny = np.where(flip, (-k * qx - qy) * 0.5, qy)
    nx = nx - np.clip(nx, -2.0 * r, 0.0)
    return -np.sqrt(nx**2 + ny**2) * np.sign(ny)


def shape_distance(kind, px, py, p):
    p0, p1, p2, p3, hw, hh = p
    if kind == RECT:
        return sd_rounded_box(px, py, hw, hh, p0), p1, p2
    if kind == DISC:
        return np.sqrt(px**2 + py**2) - hw, p0, 0.0
    if kind == RING:
        return np.abs(np.sqrt(px**2 + py**2) - hw) - max(p0, 0.0) * 0.5, 0.0, 0.0
    if kind == RING_ARC:
        ring = np.abs(np.sqrt(px**2 + py**2) - p3) - max(p0, 0.0) * 0.5
        rel = np.mod(np.arctan2(py, px) - p1, 2.0 * np.pi)
        sweep = p2
        outside = (rel > sweep) if sweep >= 0 else (rel < sweep + 2.0 * np.pi)
        return np.where(outside, ring + np.sqrt(px**2 + py**2) + 1000.0, ring), 0.0, 0.0
    if kind == SEGMENT:
        return sd_segment(px, py, p0, p1, p2, p3) - max(hw, 0.25) * 0.5, 0.0, 0.0
    if kind == GLOW:
        # Signed like the other distances: negative inside, zero on the rim,
        # positive outside, so the quad corners fade to nothing instead of
        # sitting at half coverage as a visible square plate.
        t = np.sqrt(px**2 + py**2) / max(hw, 0.5)
        return (np.power(t, max(p0, 0.2)) - 1.0) * hw, 0.0, 0.0
    if kind == TRIANGLE:
        a = p0
        c, s = np.cos(a), np.sin(a)
        rx = px * c + py * s
        ry = -px * s + py * c
        return sd_equilateral(rx, ry, hw * 0.8660254), 0.0, 0.0
    if kind == VIGNETTE:
        t = np.sqrt((px / max(hw, 1.0)) ** 2 + (py / max(hh, 1.0)) ** 2)
        return np.full_like(px, 1e9), 0.0, 0.0  # handled specially
    return np.full_like(px, 1e9), 0.0, 0.0


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0 + 1e-9), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# ---------------------------------------------------------------------------
# Rasterisation
# ---------------------------------------------------------------------------


class Canvas:
    def __init__(self, w, h):
        self.w = w
        self.h = h
        # Premultiplied, exactly like the shaders emit, blended with ONE,
        # ONE_MINUS_SRC_ALPHA.
        self.rgb = np.zeros((h, w, 3), dtype=np.float32)
        self.a = np.zeros((h, w), dtype=np.float32)

    def blend(self, x0, y0, x1, y1, colour, coverage):
        """colour is (r, g, b, a); coverage is a float array over the box."""
        if x1 <= x0 or y1 <= y0:
            return
        px = np.arange(x0, x1, dtype=np.float32) + 0.5
        py = np.arange(y0, y1, dtype=np.float32) + 0.5
        cov = coverage * colour[3]
        if cov.max() <= 0.0:
            return
        # Premultiplied source over destination.
        self.rgb[y0:y1, x0:x1] = colour[:3] * cov[..., None] + self.rgb[y0:y1, x0:x1] * (
            1.0 - cov[..., None]
        )
        self.a[y0:y1, x0:x1] = cov + self.a[y0:y1, x0:x1] * (1.0 - cov)

    def to_image(self):
        out = np.zeros((self.h, self.w, 3), dtype=np.float32)
        # The frame is composited over black, which is the clear colour.
        out[:] = self.rgb
        return (np.clip(out, 0.0, 1.0) * 255.0).astype(np.uint8)


def raster_shape(canvas, quad):
    """quad: 6 vertices x 16 floats.

    a_pos is the corner's screen position and a_uv is that same corner's offset
    from the shape's centre, so the centre is the difference.  The fragment shader
    works entirely in the offset space, and so does this.
    """
    v = quad
    x0 = float(min(c[0] for c in v))
    x1 = float(max(c[0] for c in v))
    y0 = float(min(c[1] for c in v))
    y1 = float(max(c[1] for c in v))
    cx = float(v[0][0]) - float(v[0][2])
    cy = float(v[0][1]) - float(v[0][3])
    ix0 = max(0, int(np.floor(x0)) - 1)
    ix1 = min(canvas.w, int(np.ceil(x1)) + 1)
    iy0 = max(0, int(np.floor(y0)) - 1)
    iy1 = min(canvas.h, int(np.ceil(y1)) + 1)
    if ix1 <= ix0 or iy1 <= iy0:
        return

    px = (np.arange(ix0, ix1, dtype=np.float32) + 0.5 - cx)[None, :]
    py = (np.arange(iy0, iy1, dtype=np.float32) + 0.5 - cy)[:, None]

    r, g, b, a = v[0][4:8]
    p0, p1, p2, p3 = v[0][8:12]
    hw, hh, kind, _spare = v[0][12:16]
    kind = int(kind + 0.5)

    if kind == VIGNETTE:
        t = np.sqrt((px / max(hw, 1.0)) ** 2 + (py / max(hh, 1.0)) ** 2)
        cov = smoothstep(p0, p1, np.broadcast_to(t, (py.size, px.size)))
        canvas.blend(ix0, iy0, ix1, iy1, (r, g, b, a), cov)
        return

    d, border, feather = shape_distance(kind, px, py, (p0, p1, p2, p3, hw, hh))
    # One screen pixel, exactly: the distance is already in pixels.
    aa = 1.0
    cov = 1.0 - smoothstep(-aa, aa, d)
    if border > 0.0:
        cov = cov * smoothstep(-aa, aa, d + border)
    if feather > 0.0:
        cov = cov * (1.0 - smoothstep(-feather, feather, d))
    canvas.blend(ix0, iy0, ix1, iy1, (r, g, b, a), cov)


def raster_glyph(canvas, quad, atlas, tex_h):
    v = quad
    ix0 = max(0, int(np.floor(min(c[0] for c in v))) - 1)
    ix1 = min(canvas.w, int(np.ceil(max(c[0] for c in v))) + 1)
    iy0 = max(0, int(np.floor(min(c[1] for c in v))) - 1)
    iy1 = min(canvas.h, int(np.ceil(max(c[1] for c in v))) + 1)
    if ix1 <= ix0 or iy1 <= iy0:
        return
    r, green, b, a = v[0][4:8]
    outline, glow = v[0][8], v[0][9]

    # Corner 0 is top-left and corner 2 is bottom-right, which is all the
    # interpolation needs.
    x_tl, y_tl, u_tl, v_tl = v[0][0], v[0][1], v[0][2], v[0][3]
    x_br, y_br, u_br, v_br = v[2][0], v[2][1], v[2][2], v[2][3]

    gx = np.arange(ix0, ix1, dtype=np.float32) + 0.5
    gy = np.arange(iy0, iy1, dtype=np.float32) + 0.5
    fx = ((gx - x_tl) / max(x_br - x_tl, 1e-6))
    fy = ((gy - y_tl) / max(y_br - y_tl, 1e-6))
    # Both axes are expanded to the full box, so every pixel samples one texel.
    u = (u_tl + (u_br - u_tl) * fx)[None, :]
    vv = (v_tl + (v_br - v_tl) * fy)[:, None]
    field = sample_bilinear(atlas, u, vv)

    # One screen pixel, in field units.
    #
    # The field spans 1.0 across 2*SPREAD_PX em-px, and the quad's uv extent maps
    # to a plane box whose em height is uv_extent * texHeight / EM_PX.  So the
    # glyph's pixel size is quad_px * EM_PX / (uv_extent * texHeight), and one
    # screen pixel is EM_PX / (2*SPREAD_PX * that) field units -- which reduces
    # to the expression below without ever needing the font size.
    quad_px = max(abs(y_br - y_tl), 1.0)
    uv_h = max(abs(v_br - v_tl), 1e-6)
    per_pixel = uv_h * tex_h / (2.0 * SPREAD_PX * quad_px)

    fill = smoothstep(0.5 - per_pixel, 0.5 + per_pixel, field)
    outline_cov = np.zeros_like(field)
    if outline > 0.0:
        o = outline * per_pixel
        outline_cov = smoothstep(0.5 - o - per_pixel, 0.5 - o + per_pixel, field) * (1.0 - fill)
    glow_cov = np.zeros_like(field)
    if glow > 0.0:
        spread = glow * per_pixel
        glow_cov = smoothstep(0.5 - spread - per_pixel, 0.5 - spread + per_pixel, field) * (
            1.0 - np.maximum(fill, outline_cov)
        )
    coverage = np.maximum(fill, outline_cov)
    energy = np.clip(fill + outline_cov * 0.5 + glow_cov * 0.25, 0.0, 1.0)
    canvas.blend(ix0, iy0, ix1, iy1, (r, green, b, a), coverage * energy)


def render_frame(shape_file, text_file, width, height, atlas, tex_h):
    canvas = Canvas(width, height)
    if os.path.exists(shape_file):
        raw = np.fromfile(shape_file, dtype=np.float32)
        n = raw.size // VERT_FLOATS
        raw = raw[: n * VERT_FLOATS].reshape(n, VERT_FLOATS)
        for i in range(0, n - VERT_PER_QUAD + 1, VERT_PER_QUAD):
            raster_shape(canvas, raw[i : i + VERT_PER_QUAD])
    if os.path.exists(text_file):
        raw = np.fromfile(text_file, dtype=np.float32)
        n = raw.size // VERT_FLOATS
        raw = raw[: n * VERT_FLOATS].reshape(n, VERT_FLOATS)
        for i in range(0, n - VERT_PER_QUAD + 1, VERT_PER_QUAD):
            raster_glyph(canvas, raw[i : i + VERT_PER_QUAD], atlas, tex_h)
    return canvas


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "build-host/frames"
    if not os.path.isdir(src):
        sys.exit("no such directory: %s" % src)
    atlas = load_atlas()

    frames = {}
    for name in sorted(os.listdir(src)):
        m = re.fullmatch(r"(.+)_pass(\d)\.bin", name)
        if m:
            frames.setdefault(m.group(1), {})[int(m.group(2))] = os.path.join(src, name)
    if not frames:
        sys.exit("no frame dumps in %s" % src)

    width, height = 1080, 2340
    tex_h = atlas.shape[0]
    made = []
    for index in sorted(frames):
        passes = frames[index]
        canvas = render_frame(
            passes.get(0, ""), passes.get(1, ""), width, height, atlas, tex_h
        )
        out = os.path.join(src, index + ".png")
        Image.fromarray(canvas.to_image()).save(out)
        made.append(out)
    for p in made:
        print("wrote %s" % p)


if __name__ == "__main__":
    main()
