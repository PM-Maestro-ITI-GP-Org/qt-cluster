#!/usr/bin/env python3
"""Split the v2 frame plate into the layers Main.qml draws.

    images/v2_bezel_base.png   the metal, with the light tubes taken out
    images/v2_ring_base.png    the tubes at their always-on baseline
    images/v2_bezel_glow.png   the tubes at full strength

Input is background_v2.png -- a front-on render of the housing with no text, no
car and no road, produced from tools/artwork_prompt.md.

How the split works, and why it is not the one in make_bezel_layers.py: that
script separates the ring from the old artwork by *brightness*, which worked
when the frame was dark blue and the ring was the only bright thing in it. On a
brushed metal frame the metal is the bright thing, so brightness separates
nothing. This splits on *chroma* instead -- specifically blue minus red, which
on this plate is 0..19 across every neutral metal pixel and +40..+139 on the
tubes. That gap is the whole reason the prompt insists nothing but the tubes
carries colour.

Saturation on its own would not do it: #030812 is only a few counts off black
but reads as 83% saturated in HSV, so the background would come out as ring.

Two other things happen here:

  - The plate is made mirror-exact. Several measured tables in Main.qml are
    generated for the left side and mirrored, and the generator is close to
    symmetric but not exact (mean mirror difference 8/255).
  - The middle is flattened to FLAT. The car and the road are drawn there and
    were designed against a flat dark background; the plate puts a lit panel
    behind them, which would show through the road's gaps and around the car.

Run from anywhere:  python3 tools/make_bezel_v2.py
"""

import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
IMAGES = os.path.join(ROOT, "images")

SRC = os.path.join(ROOT, "background_v2.png")
W, H = 1024, 600

# Must equal the Window colour in Main.qml. Anywhere the two disagree, the
# plate's edge shows as a rectangle against the window.
FLAT = (3, 8, 18)

# Tube extraction. Ramp on (blue - red): everything at or under CHROMA_LO is
# metal, at or over CHROMA_HI is tube, between is the tube's own falloff.
CHROMA_LO, CHROMA_HI = 26, 70

# The always-on baseline as a fraction of the tube's full brightness. The lit
# part of the gauge is the difference between this and 1.0, so a high value
# leaves nothing for speed to reveal.
BASELINE = 0.40

# The middle, flattened so the car and road sit on the same dark they were drawn
# against rather than on the plate's lit centre panel.
#
# A rectangle alone will not do it: the panel is bounded by the tubes, so its
# width changes with height, and a rectangle wide enough for the bottom eats the
# frame's top and bottom bridges. The rectangle only says roughly where to look;
# PANEL_LO/HI decide what actually gets flattened, by luminance. The panel runs
# 32..59 and the metal 190..220, so the gap between them is wide.
MID_X0, MID_X1 = 0.34, 0.66
MID_Y0, MID_Y1 = 0.30, 0.80
MID_FEATHER = 10.0
PANEL_LO, PANEL_HI = 62, 95


def load():
    im = Image.open(SRC).convert("RGB").resize((W, H), Image.LANCZOS)
    # Mirror-exact. Averaging rather than copying one half keeps detail that
    # only the generator's weaker side happens to carry.
    return Image.blend(im, im.transpose(Image.FLIP_LEFT_RIGHT), 0.5)


def tube_alpha(im):
    """Per-pixel 0..255 confidence that a pixel belongs to a light tube."""
    r, g, b = im.split()
    # b - r, clamped at zero. ImageChops.difference would give |b - r| and pick
    # up warm metal highlights as well.
    diff = Image.eval(Image.merge("L", (b,)), lambda v: v)
    px_b = b.load()
    px_r = r.load()
    out = Image.new("L", im.size, 0)
    po = out.load()
    span = float(CHROMA_HI - CHROMA_LO)
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            d = px_b[x, y] - px_r[x, y]
            if d <= CHROMA_LO:
                continue
            po[x, y] = 255 if d >= CHROMA_HI else int((d - CHROMA_LO) / span * 255)
    return out


def flatten_middle(im):
    """Replace the centre panel with FLAT, leaving the metal alone."""
    rect = Image.new("L", im.size, 0)
    ImageDraw.Draw(rect).rectangle(
        [W * MID_X0, H * MID_Y0, W * MID_X1, H * MID_Y1], fill=255)
    rect = rect.filter(ImageFilter.GaussianBlur(MID_FEATHER))

    span = float(PANEL_HI - PANEL_LO)
    gate = im.convert("L").point(
        lambda v: 255 if v <= PANEL_LO
        else (0 if v >= PANEL_HI else int((PANEL_HI - v) / span * 255)))

    mask = ImageChops.multiply(rect, gate)
    return Image.composite(Image.new("RGB", im.size, FLAT), im, mask)


def main():
    if not os.path.exists(SRC):
        raise SystemExit("missing %s -- see tools/artwork_prompt.md" % SRC)

    im = load()
    alpha = tube_alpha(im)

    # --- base: metal with the tubes removed -----------------------------------
    # The tubes sit in a recessed channel, so what belongs underneath them is a
    # dark groove. Taking a heavily blurred copy and darkening it keeps the
    # channel's own shading instead of stamping a flat colour into it. Blurred
    # from the plate before the middle is flattened, so the channel keeps its
    # shading where it crosses the centre.
    groove = im.filter(ImageFilter.GaussianBlur(14))
    groove = Image.eval(groove, lambda v: int(v * 0.45))

    # Flatten first, groove second: the tubes are bright, so the luminance gate
    # leaves them alone, and the groove then lands on top wherever they were.
    base = Image.composite(groove, flatten_middle(im), alpha)
    base.save(os.path.join(IMAGES, "v2_bezel_base.png"))

    # --- tubes ----------------------------------------------------------------
    # Main.qml colorizes these by luminance, so the grey here is what sets how
    # bright each part of the tube reads once it is tinted with ringColor.
    lum = im.convert("L")
    glow = Image.merge("RGBA", (lum, lum, lum, alpha))
    glow.save(os.path.join(IMAGES, "v2_bezel_glow.png"))

    dim = Image.eval(lum, lambda v: int(v * BASELINE))
    Image.merge("RGBA", (dim, dim, dim, alpha)).save(
        os.path.join(IMAGES, "v2_ring_base.png"))

    lit = sum(1 for p in alpha.get_flattened_data() if p > 8)
    print("wrote v2_bezel_base.png, v2_ring_base.png, v2_bezel_glow.png (%dx%d)"
          % (W, H))
    print("  tube covers %d px (%.2f%% of the plate), bbox %s"
          % (lit, 100.0 * lit / (W * H), alpha.getbbox()))


if __name__ == "__main__":
    main()
