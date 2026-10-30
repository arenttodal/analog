#!/usr/bin/env python3
"""
ANDOR ANALOG's walnut materials: a skin pack (ANDOR OSP's skin format: manifest.json and one
image per piece and level) whose pieces replace the shared ones of the same name
(osp::plugin::design::Materials::pack).

  housing  ANDOR OSP's rendered housing, its teak cheeks replaced by American walnut: an
           original, procedural wood (seeded: the same file every run), lit by the shared
           render's own cheek shading (its bevel, rounded ends and recess), so the silhouette,
           the light and the ceramic are exactly the family's.
  badge-A  layer A's badge in aged copper instead of coral (the shared render's light and
           shadow kept: only its colour moves).

No photograph and no third-party texture is used: everything new is computed here.

Usage: tools/make_walnut_skin.py <ANDOR OSP's ceramic-skin.zip> <output walnut-skin.zip>
       [--preview <dir>]  (writes crops for review)
"""

import io
import json
import sys
import zipfile

import numpy as np
from PIL import Image

SEED = 20261030

# Walnut (the visual design system's starting points; docs/WALNUT_VISUAL_DESIGN.md).
WALNUT_SHADOW = np.array([0x30, 0x21, 0x16], dtype=np.float32)
WALNUT_BASE = np.array([0x59, 0x3A, 0x2B], dtype=np.float32)
WALNUT_LIGHT = np.array([0x80, 0x57, 0x3D], dtype=np.float32)

# Aged copper for the badge (its mid-tone; the render's own light and shade around it).
COPPER_FACE = np.array([0xC8, 0x6A, 0x3C], dtype=np.float32)


def blur(a, sigma):
    """Separable Gaussian blur of a 2-D float array (edges held)."""
    if sigma <= 0:
        return a
    radius = int(np.ceil(3 * sigma))
    k = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    k /= k.sum()
    def along(arr, axis):
        pad = [(0, 0)] * arr.ndim
        pad[axis] = (radius, radius)
        padded = np.pad(arr, pad, mode="edge")
        out = np.zeros_like(arr, dtype=np.float32)
        n = arr.shape[axis]
        for i, w in enumerate(k):
            out += w * np.take(padded, np.arange(i, i + n), axis=axis)
        return out
    return along(along(a.astype(np.float32), 0), 1)


def smooth_noise_1d(n, scale, rng):
    """Value noise along one axis: random values every `scale` samples, cosine-interpolated."""
    count = int(np.ceil(n / scale)) + 2
    values = rng.standard_normal(count)
    x = np.arange(n) / scale
    i = np.floor(x).astype(int)
    f = x - i
    f = (1 - np.cos(np.pi * f)) / 2
    return values[i] * (1 - f) + values[i + 1] * f


def walnut_albedo(width, height, rng):
    """Quarter-sawn walnut, grain running down the cheek: fine, continuous lines that drift
    and waver slowly, a few broader bands of figure, pores as short dark ticks. Values in
    0..1 (0 the darkest late wood, 1 the lightest early wood)."""
    y = np.arange(height, dtype=np.float32)[:, None]
    x = np.arange(width, dtype=np.float32)[None, :]
    # The grain wanders: a slow sway and a gentle waver along its length.
    sway = smooth_noise_1d(height, 900.0, rng)[:, None] * 6.0 + smooth_noise_1d(height, 160.0, rng)[:, None] * 1.4
    u = x + sway
    # Growth lines: irregular spacing (a warped line index), each line a soft dark band.
    # (Ring widths vary a lot in walnut: from hairlines to a few millimetres.)
    steps = 0.25 + 1.6 * rng.gamma(1.4, 0.45, width + 128)
    warp = np.interp(u, np.arange(-64, width + 64), np.cumsum(1.0 / steps) / 3.2)
    phase = warp + smooth_noise_1d(height, 420.0, rng)[:, None] * 0.3
    lines = 0.5 + 0.5 * np.cos(2 * np.pi * phase)
    lines = lines ** 4.0   # narrow late-wood lines, wider early wood between them
    strength = 0.55 + 0.45 * np.clip(smooth_noise_1d(width + 64, 6.0, rng)[None, 16:16 + width], -1, 1)
    # Broad figure: bands of lighter and darker wood across the width, drifting slowly
    # along the length (the heartwood's own colour variation).
    figure = smooth_noise_1d(width + 64, 14.0, rng)[None, 16:16 + width] * 0.7 + smooth_noise_1d(height, 520.0, rng)[:, None] * 0.3
    figure += 0.35 * smooth_noise_1d(width + 64, 5.0, rng)[None, 16:16 + width] * smooth_noise_1d(height, 260.0, rng)[:, None]
    tone = 0.6 - 0.17 * lines * strength + 0.12 * figure
    # Fine fibre noise (stretched along the grain) and pores.
    fibre = rng.standard_normal((height // 8 + 2, width)).astype(np.float32)
    fibre = np.array(Image.fromarray(fibre).resize((width, height), Image.BICUBIC))
    tone += 0.035 * fibre
    pores = np.zeros((height, width), dtype=np.float32)
    for _ in range(int(width * height / 420)):
        px = rng.integers(0, width)
        py = rng.integers(0, height)
        length = rng.integers(5, 22)
        pores[py:py + length, px] += rng.uniform(0.1, 0.24)
    pores = blur(pores, 0.6)
    tone -= pores
    return np.clip(tone, 0.0, 1.0)


def colour_of(tone):
    """Walnut colour for a tone: shadow -> base -> light."""
    t = tone[..., None]
    low = WALNUT_SHADOW + (WALNUT_BASE - WALNUT_SHADOW) * np.clip(t / 0.55, 0, 1)
    high = WALNUT_BASE + (WALNUT_LIGHT - WALNUT_BASE) * np.clip((t - 0.55) / 0.45, 0, 1)
    return np.where(t < 0.55, low, high)


def walnut_housing(housing):
    """The shared housing with walnut cheeks in place of teak."""
    rgb = np.asarray(housing.convert("RGB")).astype(np.float32)
    h, w, _ = rgb.shape
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    luminance = 0.299 * r + 0.587 * g + 0.114 * b
    # The teak: warm, saturated pixels in the outer bands only (the cheeks).
    warmth = np.clip((r - b - 30.0) / 30.0, 0.0, 1.0)
    band = np.zeros((h, w), dtype=np.float32)
    edge = int(0.05 * w)
    band[:, :edge] = 1.0
    band[:, w - edge:] = 1.0
    mask = warmth * band
    mask = blur(mask, 0.7)
    mask = np.clip(mask * 1.15, 0.0, 1.0)

    # The render's lighting of each cheek: its profile across (bevel, edges) and along
    # (the rounded ends), without the teak's own grain.
    lit = np.zeros((h, w), dtype=np.float32)
    rows = slice(int(0.1 * h), int(0.9 * h))
    for side in (slice(0, edge), slice(w - edge, w)):
        cheek = luminance[:, side]
        weight = mask[:, side]
        across = np.median(np.where(weight[rows] > 0.5, cheek[rows], np.nan), axis=0)
        across = np.nan_to_num(across, nan=np.nanmedian(across))
        mid = np.nanmedian(across)
        across = across / mid
        # Along: each row's mean against the middle's (the caps darken and roll off).
        row_mean = np.nansum(cheek * weight, axis=1) / np.maximum(1e-3, weight.sum(axis=1))
        along = row_mean / np.median(row_mean[rows])
        along = blur(np.repeat(along[:, None], 3, axis=1), 3.0)[:, 1]
        lit[:, side] = np.clip(across[None, :], 0.55, 1.35) * np.clip(along[:, None], 0.4, 1.3)

    rng = np.random.default_rng(SEED)
    wood = np.zeros_like(rgb)
    for side in (slice(0, edge), slice(w - edge, w)):
        tone = walnut_albedo(side.stop - side.start, h, rng)
        wood[:, side] = colour_of(tone)
    # The cheek is a rounded post: across its width a cylinder lit from the front and a
    # little from the instrument's side (its inner edge catches the light, its outer edge
    # turns away into shadow), times the render's own light (the ends, the recess).
    form = np.ones((h, w), dtype=np.float32)
    for side, inner_on_right in ((slice(0, edge), True), (slice(w - edge, w), False)):
        weight = mask[:, side]
        cols = np.where(weight[rows].mean(axis=0) > 0.5)[0]
        if cols.size == 0:
            continue
        left, right = cols.min(), cols.max()
        x = np.arange(side.stop - side.start, dtype=np.float32)
        t = np.clip((x - left) / max(1.0, right - left), 0.0, 1.0) * 2.0 - 1.0   # -1 .. 1 across
        if not inner_on_right:
            t = -t
        normal_z = np.sqrt(np.clip(1.0 - (0.92 * t) ** 2, 0.0, 1.0))
        light = 0.58 + 0.42 * normal_z + 0.16 * t          # front-lit, leaning to the inner edge
        form[:, side] = light[None, :]
    shaded = wood * (np.clip(lit, 0.6, 1.25)[..., None] ** 0.6) * (form[..., None] ** 1.2) * 1.08
    # A low-gloss satin sheen: a soft, broad highlight down the inner face (no mirror).
    sheen = np.clip((form - 1.02) / 0.14, 0.0, 1.0) ** 2.0
    shaded += sheen[..., None] * np.array([34.0, 26.0, 19.0], dtype=np.float32)
    out = rgb * (1.0 - mask[..., None]) + shaded * mask[..., None]

    # The recess beside each cheek: the teak's warm tint on its dark rim goes with it.
    near = np.clip(blur(mask, 3.0) * 6.0, 0.0, 1.0)   # (within a few pixels of the wood only)
    rim = (1.0 - mask) * near * np.clip((r - b - 24.0) / 24.0, 0.0, 1.0)
    grey = luminance[..., None] * np.array([0.98, 0.92, 0.86], dtype=np.float32)
    out = out * (1.0 - rim[..., None] * 0.7) + (grey * 0.82) * (rim[..., None] * 0.7)
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGB")


def copper_badge(badge, face_hex):
    """The badge in copper: each saturated pixel's colour scaled from the rendered face to
    copper (light and shading kept); the shadow (neutral) as it was."""
    rgba = np.asarray(badge.convert("RGBA")).astype(np.float32)
    rgb, alpha = rgba[..., :3], rgba[..., 3:]
    face = np.array([int(face_hex[i:i + 2], 16) for i in (1, 3, 5)], dtype=np.float32)
    saturation = (rgb.max(axis=2) - rgb.min(axis=2)) / np.maximum(1.0, rgb.max(axis=2))
    weight = np.clip((saturation - 0.12) / 0.25, 0.0, 1.0)[..., None]
    scaled = rgb * (COPPER_FACE / face)[None, None, :]
    out = rgb * (1.0 - weight) + scaled * weight
    return Image.fromarray(np.concatenate([np.clip(out, 0, 255), alpha], axis=2).astype(np.uint8), "RGBA")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    source, target = sys.argv[1], sys.argv[2]
    preview = sys.argv[sys.argv.index("--preview") + 1] if "--preview" in sys.argv else None
    with zipfile.ZipFile(source) as z:
        manifest = json.loads(z.read("manifest.json"))
        pieces = {p["name"]: p for p in manifest["pieces"]}
        out_pieces = []
        files = {}
        # The housing (opaque, one level).
        housing = pieces["housing"]
        level = housing["levels"][0]
        image = walnut_housing(Image.open(io.BytesIO(z.read(level["file"]))))
        buffer = io.BytesIO()
        image.save(buffer, "JPEG", quality=93, subsampling=0, optimize=True)
        files[level["file"]] = buffer.getvalue()
        out_pieces.append(housing)
        if preview:
            image.crop((0, 0, 220, 520)).save(f"{preview}/walnut-cheek-top-left.png")
            image.crop((image.width - 220, 900, image.width, 1400)).save(f"{preview}/walnut-cheek-right.png")
            image.resize((image.width // 4, image.height // 4), Image.LANCZOS).save(f"{preview}/walnut-housing-small.png")
        # Layer A's badge (every level).
        badge = dict(pieces["badge-A"])
        for level in badge["levels"]:
            image = copper_badge(Image.open(io.BytesIO(z.read(level["file"]))), badge["face"])
            buffer = io.BytesIO()
            image.save(buffer, "PNG", optimize=True)
            files[level["file"]] = buffer.getvalue()
        badge["face"] = "#%02x%02x%02x" % tuple(int(v) for v in COPPER_FACE)
        out_pieces.append(badge)

    out_manifest = {"schemaVersion": manifest.get("schemaVersion", 1), "name": "analog-walnut",
                    "direction": "andor-analog-walnut (derived from " + manifest.get("direction", "") + ")",
                    "pieces": out_pieces}
    with zipfile.ZipFile(target, "w", zipfile.ZIP_STORED) as z:
        z.writestr("manifest.json", json.dumps(out_manifest, indent=1))
        for name, data in sorted(files.items()):
            z.writestr(name, data)
    print("wrote", target, sum(len(d) for d in files.values()), "bytes of images")


if __name__ == "__main__":
    main()
