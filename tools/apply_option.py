#!/usr/bin/env python3
"""Apply one options/*.json to Main.qml and tools/make_bezel_layers.py.

    python3 tools/apply_option.py options/042.json

Rewrites the colour block in Main.qml, the Window colour, and FLAT in the layer
script. FLAT and Window.color are set from the same value on purpose -- they
have to agree or the artwork's edges show as a rectangle.

Regenerating the layers is NOT done here, because it is only needed when the
background changed:

    python3 tools/make_bezel_layers.py
"""

import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

COLOURS = ("ringColor", "accent", "textColor", "scaleColor",
           "gearIdleColor", "iconColor", "faultColor", "carTint")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1]) as f:
        sc = json.load(f)

    path = os.path.join(ROOT, "tools", "make_bezel_layers.py")
    s = open(path).read()
    # The trailing comment is preserved rather than rewritten -- replacing the
    # whole line quietly deletes whatever note is on it.
    s = re.sub(r"^FLAT = \([^)]*\)(\s*)",
               "FLAT = (%d, %d, %d)\\1" % tuple(sc["flat"]), s, count=1, flags=re.M)
    open(path, "w").write(s)

    path = os.path.join(ROOT, "Main.qml")
    m = open(path).read()
    m = re.sub(r'^    color: "[^"]*"$', '    color: "%s"' % sc["window"],
               m, count=1, flags=re.M)
    for k in COLOURS:
        if k in sc:
            m = re.sub(r'(readonly property color %s: )"[^"]*"' % k,
                       r'\1"%s"' % sc[k], m)
    m = re.sub(r"(readonly property real carTintStrength: )[0-9.]+",
               r"\g<1>%s" % sc["carTintStrength"], m)
    open(path, "w").write(m)

    print("applied %s (%s)" % (sc["id"], sc["name"]))


if __name__ == "__main__":
    main()
