#!/usr/bin/env python3
"""Generate the colour-scheme grid under options/.

Writes one JSON per combination. tools/render_options.sh applies each in turn
and captures a screenshot next to it; tools/make_options.py --readme then builds
options/README.md from the JSONs plus whatever screenshots exist.

Every colour but the ring hue and the background is derived, so the schemes are
internally consistent rather than 100 unrelated guesses:

  ring        the hue, always at full HSV value. Colorization multiplies the
              artwork by this colour, so it is a ceiling -- a value below 1.0
              dims the whole ring and it stops reading as lit.
  accent      the same hue, desaturated on dark backgrounds so it separates
              from the ring, darkened on light ones so it stays legible.
  scale/gear  the same hue at low saturation, so the chrome sits in the same
              family instead of being neutral grey against a coloured ring.
  car         tinted toward the hue. Stronger on light backgrounds, where an
              untinted near-white car disappears into the page.

Run from anywhere:  python3 tools/make_options.py
"""

import colorsys
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "options")

# name, FLAT rgb, light?
BACKGROUNDS = [
    ("black",     (0, 0, 0),      False),
    ("navy",      (13, 20, 36),   False),
    ("charcoal",  (18, 18, 20),   False),
    ("deepteal",  (5, 26, 31),    False),
    ("plum",      (20, 12, 26),   False),
    ("slate",     (16, 20, 26),   False),
    ("lightgrey", (214, 214, 214), True),
    ("offwhite",  (240, 240, 244), True),
]

# name, hue degrees, saturation
HUES = [
    ("azure",   206, 0.95),
    ("cyan",    186, 1.00),
    ("teal",    168, 0.92),
    ("mint",    158, 0.88),
    ("green",   145, 0.85),
    ("lime",     88, 0.80),
    ("gold",     48, 0.90),
    ("amber",    38, 0.92),
    ("orange",   25, 0.90),
    ("coral",    12, 0.82),
    ("red",       2, 0.85),
    ("magenta", 330, 0.80),
    ("violet",  265, 0.72),
    ("indigo",  240, 0.75),
]

# Light backgrounds get a shorter list: the artwork's moulding and road are
# light-on-dark line work, so the pale schemes are shown as a caveat rather
# than as a full sweep.
LIGHT_HUES = ["azure", "teal", "green", "lime", "amber", "orange", "violet", "indigo"]


def hexof(h, s, v):
    r, g, b = colorsys.hsv_to_rgb((h % 360) / 360.0, max(0.0, min(1.0, s)),
                                  max(0.0, min(1.0, v)))
    return "#%02x%02x%02x" % (round(r * 255), round(g * 255), round(b * 255))


def scheme(idx, bg_name, flat, light, hue_name, hue, sat):
    if light:
        text, icon = "#16181a", "#2a2d30"
        accent = hexof(hue, min(1.0, sat + 0.05), 0.70)
        scale = hexof(hue, 0.22, 0.34)
        gear = hexof(hue, 0.10, 0.72)
        car_strength = 0.30
    else:
        text, icon = "white", "white"
        accent = hexof(hue, sat * 0.72, 1.0)
        scale = hexof(hue, 0.16, 0.72)
        gear = hexof(hue, 0.26, 0.36)
        car_strength = 0.18

    return {
        "id": "%03d" % idx,
        "name": "%s-%s" % (bg_name, hue_name),
        "background": bg_name,
        "hue": hue_name,
        "light": light,
        "flat": list(flat),
        "window": "#%02x%02x%02x" % flat,
        # Full value on purpose -- see the module docstring.
        "ringColor": hexof(hue, sat, 1.0),
        "accent": accent,
        "textColor": text,
        "scaleColor": scale,
        "gearIdleColor": gear,
        "iconColor": icon,
        "faultColor": "#ff2b2b",
        "carTint": hexof(hue, sat, 1.0),
        "carTintStrength": car_strength,
    }


def build():
    out = []
    idx = 1
    for bg_name, flat, light in BACKGROUNDS:
        hues = [h for h in HUES if not light or h[0] in LIGHT_HUES]
        for hue_name, hue, sat in hues:
            out.append(scheme(idx, bg_name, flat, light, hue_name, hue, sat))
            idx += 1
    return out


def readme(schemes):
    lines = [
        "# Colour options",
        "",
        "%d schemes, each a background crossed with a ring hue. Every row "
        "links to its screenshot and to the JSON that produced it." % len(schemes),
        "",
        "All rendered from the running cluster at `CLUSTER_SPEED=160`, so the "
        "ring is lit to the 160 mark and the light levels are real.",
        "",
        "**The screenshots are not tracked.** They run to roughly 200 KB each, "
        "so `options/*.png` is gitignored and the preview links below only "
        "resolve once you have rendered them yourself:",
        "",
        "```bash",
        "tools/render_options.sh          # every config, about 8s each",
        "tools/render_options.sh 1 14     # or just one background family",
        "```",
        "",
        "It restores `Main.qml` and `tools/make_bezel_layers.py` on exit, so it "
        "will not leave your tree on whichever scheme happened to be last.",
        "",
        "## How to apply one",
        "",
        "```bash",
        "python3 tools/apply_option.py options/001.json",
        "python3 tools/make_bezel_layers.py",
        "cmake --build build/Desktop_Qt_6_10_3-Debug -j8",
        "```",
        "",
        "The middle step is only needed when the background differs from what "
        "is currently built -- see the colour section of the top-level README.",
        "",
        "## Two things to watch",
        "",
        "- **`ringColor` is a ceiling, not a hint.** The ring is tinted by "
        "colorizing the artwork, which multiplies it by this colour, so a swatch "
        "below full HSV value dims the entire ring. Every `ringColor` below is "
        "at value 1.0 for that reason.",
        "- **The red and coral schemes cost you the fault signal.** The "
        "telltales, the motor lamp and the error banner are the only red things "
        "on screen, and that is what makes a fault read instantly.",
        "",
        "## Options",
        "",
        "| # | Preview | Background | Ring | Accent | Scale | Config |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for s in schemes:
        png = "%s.png" % s["id"]
        exists = os.path.exists(os.path.join(OUT, png))
        prev = "[view](%s)" % png if exists else "_not rendered_"
        lines.append(
            "| %s | %s | `%s` %s | `%s` | `%s` | `%s` | [json](%s.json) |"
            % (s["id"], prev, s["window"], s["background"], s["ringColor"],
               s["accent"], s["scaleColor"], s["id"]))
    lines.append("")
    return "\n".join(lines)


def main():
    os.makedirs(OUT, exist_ok=True)

    if "--readme" in sys.argv:
        # Describe what is on disk, not the full grid. Deleting a config you
        # do not want is a reasonable way to curate the set, and regenerating
        # the table from BACKGROUNDS x HUES would silently list it again.
        import glob
        schemes = []
        for path in sorted(glob.glob(os.path.join(OUT, "[0-9]*.json"))):
            with open(path) as f:
                schemes.append(json.load(f))
    else:
        schemes = build()
        for s in schemes:
            with open(os.path.join(OUT, "%s.json" % s["id"]), "w") as f:
                json.dump(s, f, indent=2)
                f.write("\n")
        print("wrote %d configs to options/" % len(schemes))

    with open(os.path.join(OUT, "README.md"), "w") as f:
        f.write(readme(schemes))
    print("wrote options/README.md (%d configs)" % len(schemes))


if __name__ == "__main__":
    main()
