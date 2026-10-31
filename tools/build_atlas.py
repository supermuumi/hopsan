#!/usr/bin/env python3
"""Build the game's sprite atlas from the source art in assets/art/src/.

    python3 tools/build_atlas.py          (needs: pip install pillow numpy)

Outputs (both committed, so you only need to run this when art changes):
    assets/art/atlas.png      -- RGBA atlas, 1 texel = 1 art pixel
    src/game/atlas_data.h     -- sprite rects + ids for the game

Scale: tiles are 32x32 art pixels. In the game a tile is 16 world units,
so 1 world unit = 2 art pixels.

Sources:
    rock_fill.webp   seamless rock texture, art pixels are 8x8 source px
    back_wall.webp   seamless background wall texture, same grid
    moss_top.png     horizontally seamless moss strip on magenta
    props_sheet.webp props on magenta (freeform layout; rects listed below)
"""

import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "assets", "art", "src")
OUT_PNG = os.path.join(ROOT, "assets", "art", "atlas.png")
OUT_H = os.path.join(ROOT, "src", "game", "atlas_data.h")

TILE = 32  # art px per tile


# ---- helpers ----------------------------------------------------------------

def load_rgb(name):
    return np.asarray(Image.open(os.path.join(SRC, name)).convert("RGB")).astype(np.float32)


def magenta_mask(a):
    """True where the pixel is (close to) the magenta background, including
    the purple glow / fringe pixels generators leave around sprites."""
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    pure = (r > 170) & (b > 170) & (g < 110)
    tint = (r - g > 70) & (b - g > 70)  # purple-ish blend of art + magenta
    # dark magenta fringe: green channel far below both red and blue
    dark = (g < 0.5 * np.minimum(r, b)) & (np.minimum(r, b) > 25)
    glow = (b - np.maximum(r, g)) > 80  # lavender glow halos
    return pure | tint | dark | glow


def grid_phase(a, cell, mask=None):
    """Find the (x, y) offset of the source's art-pixel grid by minimising
    colour variance inside cells."""
    best, best_v = (0, 0), None
    for oy in range(cell):
        for ox in range(cell):
            sub = a[oy:, ox:]
            h = (sub.shape[0] // cell) * cell
            w = (sub.shape[1] // cell) * cell
            blk = sub[:h, :w].reshape(h // cell, cell, w // cell, cell, 3)
            v = blk.var(axis=(1, 3)).sum()
            if best_v is None or v < best_v:
                best, best_v = (ox, oy), v
    return best


def snap_grid(a, cell, wrap=True, alpha=None):
    """Downscale by an integer `cell` using the per-cell median colour
    (median, not mean, so cell edges don't blend into mush)."""
    ox, oy = grid_phase(a, cell)
    if wrap:
        a = np.roll(a, (-oy, -ox), axis=(0, 1))
        if alpha is not None:
            alpha = np.roll(alpha, (-oy, -ox), axis=(0, 1))
    else:
        a = a[oy:, ox:]
        if alpha is not None:
            alpha = alpha[oy:, ox:]
    h, w = a.shape[0] // cell, a.shape[1] // cell
    blk = a[: h * cell, : w * cell].reshape(h, cell, w, cell, 3).transpose(0, 2, 1, 3, 4).reshape(h, w, cell * cell, 3)
    if alpha is None:
        rgb = np.median(blk, axis=2)
        return rgb, np.ones((h, w), bool)
    al = alpha[: h * cell, : w * cell].reshape(h, cell, w, cell).transpose(0, 2, 1, 3).reshape(h, w, cell * cell)
    cov = al.mean(axis=2)
    rgb = np.zeros((h, w, 3), np.float32)
    for y in range(h):
        for x in range(w):
            sel = blk[y, x][al[y, x]]
            if len(sel):
                rgb[y, x] = np.median(sel, axis=0)
    return rgb, cov >= 0.5


def resample(a, alpha, out_w, out_h):
    """Non-integer downscale for the freeform props: coverage-weighted box
    filter for colour, majority vote for alpha."""
    h, w = alpha.shape
    rgb = np.zeros((out_h, out_w, 3), np.float32)
    keep = np.zeros((out_h, out_w), bool)
    for y in range(out_h):
        y0, y1 = int(y * h / out_h), max(int(y * h / out_h) + 1, int((y + 1) * h / out_h))
        for x in range(out_w):
            x0, x1 = int(x * w / out_w), max(int(x * w / out_w) + 1, int((x + 1) * w / out_w))
            m = alpha[y0:y1, x0:x1]
            if m.mean() >= 0.5:
                keep[y, x] = True
                px = a[y0:y1, x0:x1][m]
                rgb[y, x] = np.median(px, axis=0)
    return rgb, keep


def quantize(rgb, keep, colours):
    """Snap to a small palette so the result is clean pixel art."""
    h, w, _ = rgb.shape
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB")
    # Build the palette from opaque pixels only.
    opaque = rgb[keep]
    if len(opaque) == 0:
        return rgb
    sample = Image.fromarray(np.clip(opaque, 0, 255).astype(np.uint8).reshape(1, -1, 3), "RGB")
    pal = sample.quantize(colours, method=Image.Quantize.MEDIANCUT)
    q = img.quantize(palette=pal, dither=Image.Dither.NONE).convert("RGB")
    return np.asarray(q).astype(np.float32)


def to_rgba(rgb, keep):
    a = np.zeros(rgb.shape[:2] + (4,), np.uint8)
    a[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    a[..., 3] = np.where(keep, 255, 0)
    return a


def trim(rgba):
    ys, xs = np.where(rgba[..., 3] > 0)
    if len(ys) == 0:
        return rgba
    return rgba[ys.min(): ys.max() + 1, xs.min(): xs.max() + 1]


def adjust(rgb, contrast=1.0, bright=1.0, sat=1.0):
    mean = rgb.mean(axis=(0, 1), keepdims=True)
    out = (rgb - mean) * contrast + mean
    grey = out.mean(axis=2, keepdims=True)
    out = (out - grey) * sat + grey
    return np.clip(out * bright, 0, 255)


# ---- sources ----------------------------------------------------------------

def build_texture(name, colours, **adj):
    a = load_rgb(name)
    rgb, keep = snap_grid(a, 8, wrap=True)
    if adj:
        rgb = adjust(rgb, **adj)
    rgb = quantize(rgb, keep, colours)
    return to_rgba(rgb, keep)


def build_moss():
    a = load_rgb("moss_top.png")
    bg = magenta_mask(a)
    # pad height to a multiple of 8 with background
    pad = (-a.shape[0]) % 8 + 8
    a = np.concatenate([a, np.zeros((pad, a.shape[1], 3), np.float32)], axis=0)
    bg = np.concatenate([bg, np.ones((pad, bg.shape[1]), bool)], axis=0)
    rgb, keep = snap_grid(a, 8, wrap=True, alpha=~bg)
    rgb = quantize(rgb, keep, 10)
    rgba = to_rgba(rgb, keep)
    ys = np.where(rgba[..., 3].any(axis=1))[0]
    rgba = rgba[ys.min(): ys.max() + 1]
    # first row that's (nearly) fully covered = top of the solid moss band
    band = np.where((rgba[..., 3] > 0).mean(axis=1) > 0.95)[0]
    band_top = int(band.min()) if len(band) else 0
    return rgba, band_top


# Props: (id, (x0, y0, x1, y1) in props_sheet.webp, (max_w, max_h) art px,
#         stretch=True to force exactly that size)
PROPS = [
    ("SPR_SPIKES",   (3, 556, 84, 614),    (32, 16), True),
    ("SPR_SPRING",   (258, 546, 341, 614), (30, 22), False),
    ("SPR_CRYSTAL",  (531, 528, 580, 592), (20, 26), False),
    ("SPR_FLAG",     (783, 512, 842, 614), (24, 40), False),
    ("SPR_TORCH",    (952, 522, 1009, 614),(22, 36), False),
    ("SPR_STONE",    (513, 3, 597, 89),    (32, 32), True),
    ("SPR_PLANK",    (513, 434, 596, 454), (32, 8),  True),
    ("SPR_BEETLE_0", (13, 641, 78, 681),   (28, 18), False),
    ("SPR_BEETLE_1", (97, 641, 162, 682),  (28, 18), False),
    ("SPR_BEETLE_2", (182, 636, 247, 682), (28, 18), False),
    ("SPR_BEETLE_3", (13, 726, 78, 763),   (28, 18), False),
    ("SPR_GRASS",    (256, 718, 341, 763), (32, 16), False),
    ("SPR_SHROOMS",  (512, 716, 597, 763), (30, 16), False),
    ("SPR_FERN",     (598, 650, 690, 763), (32, 34), False),
    ("SPR_RUBBLE",   (768, 688, 854, 763), (32, 26), False),
    ("SPR_VINES",    (344, 614, 427, 752), (30, 48), False),
    ("SPR_CHAIN",    (880, 612, 908, 738), (10, 48), False),
    ("SPR_PILLAR",   (525, 238, 586, 382), (24, 60), False),
]


def build_prop(sheet, bgmask, rect, box, stretch):
    x0, y0, x1, y1 = rect
    a = sheet[y0:y1, x0:x1]
    keep = ~bgmask[y0:y1, x0:x1]
    # tighten to content first
    ys, xs = np.where(keep)
    a = a[ys.min(): ys.max() + 1, xs.min(): xs.max() + 1]
    keep = keep[ys.min(): ys.max() + 1, xs.min(): xs.max() + 1]
    h, w = keep.shape
    bw, bh = box
    if stretch:
        ow, oh = bw, bh
    else:
        s = min(bw / w, bh / h)
        ow, oh = max(1, round(w * s)), max(1, round(h * s))
    rgb, k = resample(a, keep, ow, oh)
    rgb = quantize(rgb, k, 16)
    return trim(to_rgba(rgb, k))


# ---- packing ----------------------------------------------------------------

class Packer:
    """Shelf packer with a 2px gutter around every entry. Tiling textures
    get their gutter filled by wrapping so nearest sampling never bleeds."""

    def __init__(self, width):
        self.w = width
        self.x = 0
        self.y = 0
        self.shelf_h = 0
        self.items = []  # (name, x, y, rgba, wrap_x, wrap_y)

    def add(self, name, rgba, wrap_x=False, wrap_y=False):
        h, w = rgba.shape[:2]
        g = 2
        if self.x + w + 2 * g > self.w:
            self.x = 0
            self.y += self.shelf_h
            self.shelf_h = 0
        self.items.append((name, self.x + g, self.y + g, rgba, wrap_x, wrap_y))
        self.x += w + 2 * g
        self.shelf_h = max(self.shelf_h, h + 2 * g)

    def render(self):
        H = self.y + self.shelf_h
        out = np.zeros((H, self.w, 4), np.uint8)
        for _, x, y, rgba, wx, wy in self.items:
            h, w = rgba.shape[:2]
            src = rgba
            if wx:
                src = np.concatenate([src[:, -2:], src, src[:, :2]], axis=1)
            else:
                src = np.pad(src, ((0, 0), (2, 2), (0, 0)), mode="constant")
            if wy:
                src = np.concatenate([src[-2:], src, src[:2]], axis=0)
            else:
                src = np.pad(src, ((2, 2), (0, 0), (0, 0)), mode="constant")
            out[y - 2: y + h + 2, x - 2: x + w + 2] = src
        return out


def main():
    rock = build_texture("rock_fill.webp", 16)
    back = build_texture("back_wall.webp", 12, contrast=0.7, bright=0.85, sat=0.8)
    moss, moss_band_top = build_moss()

    sheet = load_rgb("props_sheet.webp")
    bgmask = magenta_mask(sheet)
    props = [(pid, build_prop(sheet, bgmask, rect, box, st)) for pid, rect, box, st in PROPS]

    # Spikes: the source points up; bake the other three directions.
    spikes = dict(props)["SPR_SPIKES"]
    props = [p for p in props if p[0] != "SPR_SPIKES"]
    spike_dirs = [
        ("SPR_SPIKES_U", spikes),
        ("SPR_SPIKES_D", np.flipud(spikes)),
        ("SPR_SPIKES_L", np.rot90(spikes, 1)),   # tips point left
        ("SPR_SPIKES_R", np.rot90(spikes, -1)),  # tips point right
    ]

    pk = Packer(256)
    pk.add("SPR_ROCK", rock, True, True)
    pk.add("SPR_BACKWALL", back, True, True)
    pk.add("SPR_MOSS", moss, True, False)
    for pid, rgba in spike_dirs + props:
        pk.add(pid, rgba)
    atlas = pk.render()
    Image.fromarray(atlas, "RGBA").save(OUT_PNG)

    lines = [
        "// GENERATED by tools/build_atlas.py -- do not edit by hand.",
        "// Atlas texture: assets/art/atlas.png (1 texel = 1 art pixel,",
        "// 32 art px per tile, 2 art px per world unit).",
        "#pragma once",
        "",
        "namespace hop {",
        "",
        f"constexpr int kAtlasW = {atlas.shape[1]};",
        f"constexpr int kAtlasH = {atlas.shape[0]};",
        f"constexpr int kMossBandTop = {moss_band_top};  // art px of grass above the solid moss band",
        "",
        "enum SpriteId : int {",
    ]
    for name, *_ in pk.items:
        lines.append(f"    {name},")
    lines += ["    SPR_COUNT", "};", "", "struct AtlasRect { short x, y, w, h; };", "",
              "constexpr AtlasRect kSprites[SPR_COUNT] = {"]
    for name, x, y, rgba, *_ in pk.items:
        lines.append(f"    {{ {x}, {y}, {rgba.shape[1]}, {rgba.shape[0]} }},  // {name}")
    lines += ["};", "", "} // namespace hop", ""]
    with open(OUT_H, "w") as f:
        f.write("\n".join(lines))
    print(f"atlas {atlas.shape[1]}x{atlas.shape[0]}, {len(pk.items)} sprites -> {OUT_PNG}, {OUT_H}")


if __name__ == "__main__":
    sys.exit(main())
