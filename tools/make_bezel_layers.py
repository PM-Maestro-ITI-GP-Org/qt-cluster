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
# (100-215), and stops at the dark bezel body (under 40).
#
# Connectivity is what makes this safe. A colour rule alone also matches the
# fuel and temperature icons inside the lens -- they are bright neutral greys
# too -- but those are ringed by dark bezel, so a flood from the border never
# reaches them.
#
# PIL's ImageDraw.floodfill is not used: it bails out early whenever the fill
# colour is within `thresh` of the seed colour, which silently keys nothing.
OUTSIDE_LUM_CUT = 45.0

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


def main():
    im = Image.open(SRC).convert("RGB")
    W, H = im.size
    src = im.load()

    outside = _flood_outside(src, W, H)

    base = Image.new("RGBA", (W, H))
    glow = Image.new("RGBA", (W, H))
    bp, gp = base.load(), glow.load()

    for y in range(H):
        for x in range(W):
            if outside[y * W + x]:
                continue                      # checkerboard + frame -> transparent

            r, g, b = src[x, y]

            t = _clamp((max(g, b) - r - NEON_LO) / (NEON_HI - NEON_LO))
            alpha = 255
            # min() so ambient pixels darker than NEON_OFF are never brightened
            # by the lerp -- they should only ever get darker as the ring lifts.
            bp[x, y] = (min(r, int(r + (NEON_OFF[0] - r) * t)),
                        min(g, int(g + (NEON_OFF[1] - g) * t)),
                        min(b, int(b + (NEON_OFF[2] - b) * t)),
                        alpha)
            gp[x, y] = (r, g, b, int(alpha * t))

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


def _clamp(v):
    return 0.0 if v < 0 else (1.0 if v > 1 else v)


if __name__ == "__main__":
    main()
