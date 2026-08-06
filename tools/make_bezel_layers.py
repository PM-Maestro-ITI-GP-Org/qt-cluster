#!/usr/bin/env python3
"""Generate the cluster's bezel layers from the source artwork.

Reads ../background (a JPEG with the transparency checkerboard baked in as
real pixels) and writes two PNGs next to the other images:

    cluster_bezel_base.png   bezel with the neon ring switched off
    cluster_bezel_glow.png   the neon ring alone, on transparent

Main.qml draws the base always and reveals the glow bottom-up with speed, so
the split has to be clean: anything of the ring left behind in the base shows
as a permanent glow that never goes dark at a standstill.

Run from anywhere:  python3 tools/make_bezel_layers.py
"""

import os
from collections import deque

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "background")
IMAGES = os.path.join(ROOT, "images")

# --- Checkerboard and grey frame -------------------------------------------
# Both are removed in one pass: flood from the four corners through every
# pixel brighter than OUTSIDE_LUM_CUT. That covers the baked-in transparency
# checkerboard (luminance 232-255) and the moulded grey rim it touches
# (100-215), and stops at the dark bezel body (~10).
#
# Connectivity is what makes this safe. A colour rule alone also matches the
# fuel and temperature icons inside the lens -- they are bright neutral greys
# too -- but those are ringed by dark bezel, so a flood from the border never
# reaches them.
#
# PIL's ImageDraw.floodfill is not used: it bails out early whenever the fill
# colour is within `thresh` of the seed colour, which silently keys nothing.
#
# The cut sits just above the bezel body (luminance ~10) rather than just below
# the frame (~100): the frame's anti-aliased edge fades into the low 20s, and
# leaving that behind draws a faint outline of the old silhouette. 26 is the
# floor -- at 22 the flood bridges into the lens and eats the temperature icon
# and the ring.
OUTSIDE_LUM_CUT = 26.0

# --- Neon split ------------------------------------------------------------
# Ramp over "coolness", max(g, b) - r.
#
# Blue-excess (b - max(r, g)) is the obvious metric and it is WRONG here: the
# brightest part of the ring is blown out to cyan, e.g. (181, 255, 255), where
# b - max(g, b) is 0. That left the ring's hottest core sitting in the base
# layer as a white line that never went dark. max(g, b) - r scores that core at
# 74 while neutral greys -- the frame at (250, 250, 252) -- score 2.
#
# Ambient bezel navy lands around 20-23, so the ramp starts just above it.
NEON_LO, NEON_HI = 24.0, 60.0
NEON_OFF = (10, 15, 30)      # colour the ring fades to when unlit

# --- Flatten ---------------------------------------------------------------
# Everything at or below the lens interior's brightness collapses to one flat
# colour, so the bezel silhouette disappears and the screen reads as a plain
# rectangle. Only pixels brighter than the interior survive: the road graphic,
# the fuel and temperature icons, the moulding.
#
# This is done by brightness rather than by cutting the lens out, because the
# lens is not separable by flood fill. Its boundary leaks in both directions:
# the ring is not a closed barrier (a flood from the border reaches the middle)
# and the interior is not distinct enough from the bezel body (a flood from the
# centre escapes to the border).
#
# FLAT must match Window.color in Main.qml or the artwork's edges will show as
# a rectangle against the letterboxed area.
FLAT = (13, 20, 36)          # == #0d1424, sampled from the lens interior
KEEP_LO, KEEP_HI = 26.0, 38.0

# --- Always-on baseline ----------------------------------------------------
# The ring is not fully removed from the base: a fraction of it stays behind so
# it is present at every speed, and the glow layer brightens it from there
# rather than revealing it out of nothing.
#
# Two levels, because the ring's two parts want different treatment. The
# diffuse blue halo should read as always lit, so it keeps half its strength.
# The bright core line should read as unlit-but-there -- just perceptible --
# so it keeps very little. CORENESS_LO/HI select between them by luminance:
# the halo sits around 40-90, the core runs past 200.
# --- Icons to drop ---------------------------------------------------------
# The fuel and coolant-temperature symbols are painted into the artwork and
# there is no signal behind either of them, so they are erased. Each is removed
# as the bright blob containing its seed pixel rather than as a rectangle: the
# road graphic runs right past both, and a box big enough to cover the icon
# also clips the line. The cut is high enough that the blob does not spread
# into that line, and the dilation mops up the anti-aliased fringe.
#
# The temperature symbol needs extra seeds: the little coolant waves under the
# thermometer are separate blobs, so a flood from its centre never reaches them
# and they survive as specks.
ICON_SEEDS = [(370, 312),
              (671, 311), (664, 315), (665, 318), (676, 318), (677, 315)]
ICON_LUM_CUT = 90.0
ICON_DILATE = 3

BASE_HALO = 0.50
BASE_CORE = 0.035
CORENESS_LO, CORENESS_HI = 60.0, 180.0


def main():
    im = Image.open(SRC).convert("RGB")
    W, H = im.size
    src = im.load()

    outside = _flood_outside(src, W, H)
    icons = _icon_mask(src, W, H)

    base = Image.new("RGBA", (W, H))
    glow = Image.new("RGBA", (W, H))
    bp, gp = base.load(), glow.load()

    for y in range(H):
        for x in range(W):
            if outside[y * W + x] or icons[y * W + x]:
                bp[x, y] = FLAT + (255,)       # frame and dropped icons -> flat
                continue

            r, g, b = src[x, y]

            t = _clamp((max(g, b) - r - NEON_LO) / (NEON_HI - NEON_LO))
            # min() so ambient pixels darker than NEON_OFF are never brightened
            # by the lerp -- they should only ever get darker as the ring lifts.
            unlit = (min(r, int(r + (NEON_OFF[0] - r) * t)),
                     min(g, int(g + (NEON_OFF[1] - g) * t)),
                     min(b, int(b + (NEON_OFF[2] - b) * t)))

            # Collapse to FLAT below the interior's brightness. The ring is
            # already darkened by now, so its unlit pixels fall through here
            # too and leave no trace of the lens outline.
            lum = (unlit[0] * 299 + unlit[1] * 587 + unlit[2] * 114) / 1000.0
            # (1 - t) so anything partly claimed by the glow is pushed toward
            # FLAT in proportion. Without it the ring's outer halo -- only
            # partly darkened, so still brighter than KEEP_LO -- survives in
            # the base as a permanent outline of the lens.
            k = _clamp((lum - KEEP_LO) / (KEEP_HI - KEEP_LO)) * (1.0 - t)
            flat = (FLAT[0] + (unlit[0] - FLAT[0]) * k,
                    FLAT[1] + (unlit[1] - FLAT[1]) * k,
                    FLAT[2] + (unlit[2] - FLAT[2]) * k)

            # Put the ring back at its always-on baseline, blending from the
            # flattened result toward the original. t scales it so non-ring
            # pixels are untouched.
            orig_lum = (r * 299 + g * 587 + b * 114) / 1000.0
            coreness = _clamp((orig_lum - CORENESS_LO) / (CORENESS_HI - CORENESS_LO))
            keep = t * (BASE_HALO + (BASE_CORE - BASE_HALO) * coreness)

            bp[x, y] = (int(flat[0] + (r - flat[0]) * keep),
                        int(flat[1] + (g - flat[1]) * keep),
                        int(flat[2] + (b - flat[2]) * keep),
                        255)
            gp[x, y] = (r, g, b, int(255 * t))

    base.save(os.path.join(IMAGES, "cluster_bezel_base.png"))
    glow.save(os.path.join(IMAGES, "cluster_bezel_glow.png"))
    print("wrote cluster_bezel_base.png and cluster_bezel_glow.png (%dx%d)" % (W, H))


def _flood_outside(src, W, H):
    """Flat bitmap of pixels reachable from the border through bright pixels."""
    seen = bytearray(W * H)
    q = deque()

    def push(x, y):
        i = y * W + x
        if seen[i]:
            return
        r, g, b = src[x, y]
        if (r * 299 + g * 587 + b * 114) / 1000.0 < OUTSIDE_LUM_CUT:
            return
        seen[i] = 1
        q.append((x, y))

    for x in range(W):
        push(x, 0)
        push(x, H - 1)
    for y in range(H):
        push(0, y)
        push(W - 1, y)

    while q:
        x, y = q.popleft()
        if x > 0:
            push(x - 1, y)
        if x < W - 1:
            push(x + 1, y)
        if y > 0:
            push(x, y - 1)
        if y < H - 1:
            push(x, y + 1)

    return seen


def _icon_mask(src, W, H):
    """Flat bitmap of the painted-in icons, grown by ICON_DILATE."""
    mask = bytearray(W * H)

    for sx, sy in ICON_SEEDS:
        q = deque([(sx, sy)])
        mask[sy * W + sx] = 1
        while q:
            x, y = q.popleft()
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if not (0 <= nx < W and 0 <= ny < H) or mask[ny * W + nx]:
                    continue
                r, g, b = src[nx, ny]
                if (r * 299 + g * 587 + b * 114) / 1000.0 < ICON_LUM_CUT:
                    continue
                mask[ny * W + nx] = 1
                q.append((nx, ny))

    if ICON_DILATE:
        grown = bytearray(mask)
        for y in range(H):
            for x in range(W):
                if not mask[y * W + x]:
                    continue
                for dy in range(-ICON_DILATE, ICON_DILATE + 1):
                    for dx in range(-ICON_DILATE, ICON_DILATE + 1):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < W and 0 <= ny < H:
                            grown[ny * W + nx] = 1
        mask = grown

    return mask


def _clamp(v):
    return 0.0 if v < 0 else (1.0 if v > 1 else v)


if __name__ == "__main__":
    main()
