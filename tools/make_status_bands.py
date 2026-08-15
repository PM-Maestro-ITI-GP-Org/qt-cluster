#!/usr/bin/env python3
"""Generate the mask for the two status bands.

    images/status_bands.png   both bands, white on transparent

Main.qml draws the bars as plain rectangles, all rotated the same way, and masks
them with this. That is what gets the two things rectangles cannot give on their
own: every cut stays parallel to every other, while the band's inner and outer
edges are smooth curves taken from the gauge itself.

The edges are not drawn. They are cluster_ring_base.png's own lower shoulder,
traced, smoothed, and pushed out along its normal by D_IN and D_OUT -- so the
band is parallel to the ring by construction and stays that way if the ring is
ever reshaped. Re-run this after any change to the bezel artwork.

One subtlety: the artwork is 1024x447 but is stretched to 1208x600 on screen, so
a normal offset that is uniform in this file is *not* uniform once drawn. The
offsets below are therefore applied in screen space and converted back, which is
what SX/SY are for. Getting this wrong tapers the band toward the vertical parts
of the curve.

Run from anywhere:  python3 tools/make_status_bands.py
"""

import math
import os

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGES = os.path.join(os.path.dirname(HERE), "images")

# Screen geometry Main.qml lays the artwork out with (bezelScale 1.18, 1024x600).
SX = 1.18
SY = 600.0 / 447.0

# Band extent along the artwork's x, matching statusBandX0/X1 in Main.qml.
#
# X0 is not free. Main.qml cuts the band with parallel lines at a fixed 75
# degrees, and where the ring's shoulder is steeper than that the cut runs along
# the band instead of across it -- which put a long spike on the outer tip at the
# old 0.134. Starting at 0.150 leaves the steepest tangent around 50 degrees.
# Lower this and the spike comes back; a shallower cut angle in Main.qml would
# have to raise it.
X0, X1 = 0.150, 0.283

# Offsets from the ring's traced edge, in screen pixels along its normal.
# D_OUT is bounded by the corner readouts at bottomRowY (0.882): at the band's
# inner end the ring sits at about 0.823, which leaves roughly 35px before the
# SOC value starts. 34 uses almost all of it.
D_IN, D_OUT = 6.0, 34.0

SAMPLES = 400          # points traced along the shoulder
SMOOTH = 9             # +/- window for the moving average
SS = 3                 # supersampling factor for a clean edge

# The band is flat, with only enough of a feather to keep its edges off the
# background on a hard line.
#
# It was briefly graded across its thickness -- dark at both edges, bright in the
# middle -- to copy the ring's own cross-section. That is what the ring does, but
# on these bars it reads as a shadow around the band rather than as a lit tube,
# because the bars are short and the dark edge lands right next to their cuts.
# Integration comes from the colours in Main.qml instead. Do not reintroduce it.
FEATHER = 0.8          # px of blur on the band edge, at artwork scale


def trace(ring):
    """Lowest lit pixel of the ring, column by column, in artwork pixels."""
    px = ring.load()
    w, h = ring.size
    pts = []
    for i in range(SAMPLES + 1):
        xf = X0 + (X1 - X0) * i / SAMPLES
        x = int(round(xf * w))
        for y in range(h - 1, -1, -1):
            r, g, b, a = px[x, y]
            if a > 40 and (r + g + b) / 3 > 25:
                pts.append((x, y))
                break
    return pts


def smooth(pts):
    out = []
    for i in range(len(pts)):
        lo = max(0, i - SMOOTH)
        hi = min(len(pts), i + SMOOTH + 1)
        n = hi - lo
        out.append((sum(p[0] for p in pts[lo:hi]) / n,
                    sum(p[1] for p in pts[lo:hi]) / n))
    return out


def offset(pts, d):
    """Push each point out along the curve's normal by d screen pixels.

    The normal is taken in screen space -- hence the SX/SY on the way in and
    their inverse on the way out -- so the band keeps an even thickness where
    the shoulder is steep as well as where it is flat."""
    out = []
    for i in range(len(pts)):
        a = pts[max(0, i - 4)]
        b = pts[min(len(pts) - 1, i + 4)]
        t = math.atan2((b[1] - a[1]) * SY, (b[0] - a[0]) * SX)
        # Away from the lens, i.e. down and to the left along this shoulder.
        nx, ny = -math.sin(t), math.cos(t)
        out.append((pts[i][0] + d * nx / SX, pts[i][1] + d * ny / SY))
    return out


def main():
    ring = Image.open(os.path.join(IMAGES, "cluster_ring_base.png")).convert("RGBA")
    w, h = ring.size

    sp = smooth(trace(ring))
    inner = offset(sp, D_IN)
    outer = offset(sp, D_OUT)
    poly = inner + outer[::-1]

    big = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(big)
    d.polygon([(x * SS, y * SS) for x, y in poly], fill=255)
    # Mirrored copy for the right-hand band. Reflecting the finished shape rather
    # than re-tracing keeps the pair exactly symmetric, which the artwork itself
    # is not quite.
    d.polygon([((w - x) * SS, y * SS) for x, y in poly], fill=255)
    band = big.resize((w, h), Image.LANCZOS)
    if FEATHER > 0:
        band = band.filter(ImageFilter.GaussianBlur(FEATHER))

    out = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    out.paste((255, 255, 255), (0, 0, w, h), band)
    out.putalpha(band)

    path = os.path.join(IMAGES, "status_bands.png")
    out.save(path)
    ext = band.getbbox()
    print("wrote images/status_bands.png (%dx%d), band bbox %s, peak alpha %d"
          % (w, h, ext, max(band.getextrema())))


if __name__ == "__main__":
    main()
