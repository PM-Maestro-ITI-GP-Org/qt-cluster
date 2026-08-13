#!/usr/bin/env python3
"""Generate the two motor-fault symbols.

    images/fault_mechanical.svg   a cog with a tooth broken off
    images/fault_electrical.svg   a bolt

These are deliberately not the corner telltales. Those are drawn at roughly
20px and are already soft; the symbol in the motor lamp lands at about 44px on
a 1024x600 panel, on top of a pulsing red glow, and has to be readable at a
glance while moving. That rules out anything with interior detail -- one bold
silhouette per symbol, nothing else.

White on transparent, like the telltales: Main.qml draws them over the lamp,
and white is what survives on top of the red.

SVG rather than PNG because Qt rasterises it at whatever size the Image asks
for, so the same file stays crisp if the icon is ever resized.

Run from anywhere:  python3 tools/make_fault_icons.py
"""

import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGES = os.path.join(os.path.dirname(HERE), "images")

BOX = 100.0                 # viewBox, all geometry is in these units
CX = CY = BOX / 2

# --- Cog --------------------------------------------------------------------
TEETH = 8
R_TIP = 47.0                # tooth tip
R_ROOT = 34.0               # between teeth
R_HUB = 12.5                # centre hole
TOOTH_FRAC = 0.46           # of each tooth's angular pitch spent at full tip
CHAMFER = 0.10              # of the pitch spent ramping between root and tip
BROKEN_TOOTH = 1            # index left at root height, i.e. snapped off
STEPS = 24                  # samples per tooth pitch


def cog_path():
    """Outline sampled as a polygon -- at 44px the curvature is invisible, and
    sampling avoids a page of arc commands that nobody can review."""
    pitch = 2 * math.pi / TEETH
    pts = []
    for i in range(TEETH * STEPS):
        a = i * pitch / STEPS
        tooth, phase = divmod(a, pitch)
        f = phase / pitch                      # 0..1 within this tooth's pitch
        if int(tooth) == BROKEN_TOOTH:
            # Not just a missing tooth -- the rim is bitten into as well. A
            # tooth merely absent reads as a gear with wider spacing at 44px;
            # a notch below the root line reads as damage.
            r = R_ROOT * 0.78
        else:
            # Trapezoid: ramp up, hold at the tip, ramp down, hold at the root.
            lo = (1.0 - TOOTH_FRAC) / 2
            hi = lo + TOOTH_FRAC
            if f < lo - CHAMFER or f > hi + CHAMFER:
                r = R_ROOT
            elif f < lo:
                r = R_ROOT + (R_TIP - R_ROOT) * (f - (lo - CHAMFER)) / CHAMFER
            elif f <= hi:
                r = R_TIP
            else:
                r = R_TIP - (R_TIP - R_ROOT) * (f - hi) / CHAMFER
        # -pi/2 so a tooth points straight up and the break sits off to one side
        pts.append((CX + r * math.cos(a - math.pi / 2),
                    CY + r * math.sin(a - math.pi / 2)))

    d = "M %.2f %.2f " % pts[0]
    d += " ".join("L %.2f %.2f" % p for p in pts[1:])
    d += " Z"
    # Second subpath, opposite winding under evenodd, punches the hub out.
    hub = []
    for i in range(48):
        a = -2 * math.pi * i / 48
        hub.append((CX + R_HUB * math.cos(a), CY + R_HUB * math.sin(a)))
    d += " M %.2f %.2f " % hub[0]
    d += " ".join("L %.2f %.2f" % p for p in hub[1:])
    d += " Z"
    return d


# --- Bolt -------------------------------------------------------------------
# Hand-placed rather than derived: a bolt is a shape you judge by eye, and the
# proportions that read well at 44px are not the ones any formula gives.
BOLT = ("M 61 6 L 25 55 L 45 55 L 39 94 L 76 43 L 55 43 Z")


def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" '
            'viewBox="0 0 %g %g" width="%g" height="%g">\n'
            '%s\n</svg>\n' % (BOX, BOX, BOX, BOX, body))


def main():
    cog = ('  <path fill="#ffffff" fill-rule="evenodd" d="%s"/>' % cog_path())
    bolt = ('  <path fill="#ffffff" d="%s"/>' % BOLT)

    for name, body in (("fault_mechanical", cog), ("fault_electrical", bolt)):
        path = os.path.join(IMAGES, "%s.svg" % name)
        with open(path, "w") as f:
            f.write(svg(body))
        print("wrote images/%s.svg" % name)


if __name__ == "__main__":
    main()
