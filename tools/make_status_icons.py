#!/usr/bin/env python3
"""Generate the two status-band symbols.

    images/icon_battery.svg   a cell with a terminal and a part charge
    images/icon_health.svg    a pulse trace

These label the bands along the ring's lower shoulders: charge on the left,
motor health on the right. They are not the corner telltales -- those name a
component and are drawn at about 20px; these name a gauge and sit at the band's
inner tip, where the only thing next to them is the band itself.

White on transparent, like the telltales and the fault symbols. Main.qml
colorizes them by luminance, so white here means "take whatever colour the
theme says" and any other colour would tint the result.

Deliberately plain. At this size interior detail turns to mush, so the battery
is an outline, a terminal and one fill bar, and the pulse is a single stroked
line -- nothing that needs more than a glance to resolve.

Run from anywhere:  python3 tools/make_status_icons.py
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGES = os.path.join(os.path.dirname(HERE), "images")

BOX = 100.0

# --- battery ----------------------------------------------------------------
# Landscape cell. The fill bar is drawn short on purpose: a full one reads as a
# solid block at 20px and loses the outline that makes it a battery at all.
BODY = (10, 28, 78, 72)       # left, top, right, bottom
WALL = 8
TERM = (78, 42, 90, 58)
FILL = (22, 40, 46, 60)
RADIUS = 6


def battery():
    l, t, r, b = BODY
    il, it, ir, ib = l + WALL, t + WALL, r - WALL, b - WALL
    return (
        '  <path fill="#ffffff" fill-rule="evenodd" '
        'd="M %g %g H %g V %g H %g Z M %g %g H %g V %g H %g Z"/>\n'
        '  <rect fill="#ffffff" x="%g" y="%g" width="%g" height="%g" rx="3"/>\n'
        '  <rect fill="#ffffff" x="%g" y="%g" width="%g" height="%g" rx="2"/>'
        % (l, t, r, b, l,
           il, it, ir, ib, il,
           TERM[0], TERM[1], TERM[2] - TERM[0], TERM[3] - TERM[1],
           FILL[0], FILL[1], FILL[2] - FILL[0], FILL[3] - FILL[1])
    )


# --- health -----------------------------------------------------------------
# A pulse trace. Stroked rather than filled: the shape is a line, and outlining
# a line by hand at this width gives mitre artefacts at the spikes that a
# stroke renderer handles for free.
#
# Asymmetric on purpose -- a tall narrow upstroke against a shorter, wider
# downstroke. A symmetric zigzag reads as a chart, not a heartbeat.
PULSE = "M 6 50 H 28 L 38 22 L 52 78 L 62 50 H 94"
PULSE_W = 9


def health():
    return ('  <path fill="none" stroke="#ffffff" stroke-width="%g" '
            'stroke-linecap="round" stroke-linejoin="round" d="%s"/>'
            % (PULSE_W, PULSE))


def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" '
            'viewBox="0 0 %g %g" width="%g" height="%g">\n%s\n</svg>\n'
            % (BOX, BOX, BOX, BOX, body))


def main():
    for name, body in (("icon_battery", battery()), ("icon_health", health())):
        path = os.path.join(IMAGES, "%s.svg" % name)
        with open(path, "w") as f:
            f.write(svg(body))
        print("wrote images/%s.svg" % name)


if __name__ == "__main__":
    main()
