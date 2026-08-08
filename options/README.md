# Colour options

84 schemes, each a background crossed with a ring hue. Every row links to its screenshot and to the JSON that produced it.

All rendered from the running cluster at `CLUSTER_SPEED=160`, so the ring is lit to the 160 mark and the light levels are real.

**The screenshots are not tracked.** They run to roughly 200 KB each, so `options/*.png` is gitignored and the preview links below only resolve once you have rendered them yourself:

```bash
tools/render_options.sh          # every config, about 8s each
tools/render_options.sh 1 14     # or just one background family
```

It restores `Main.qml` and `tools/make_bezel_layers.py` on exit, so it will not leave your tree on whichever scheme happened to be last.

## How to apply one

```bash
python3 tools/apply_option.py options/001.json
python3 tools/make_bezel_layers.py
cmake --build build/Desktop_Qt_6_10_3-Debug -j8
```

The middle step is only needed when the background differs from what is currently built -- see the colour section of the top-level README.

## Two things to watch

- **`ringColor` is a ceiling, not a hint.** The ring is tinted by colorizing the artwork, which multiplies it by this colour, so a swatch below full HSV value dims the entire ring. Every `ringColor` below is at value 1.0 for that reason.
- **The red and coral schemes cost you the fault signal.** The telltales, the motor lamp and the error banner are the only red things on screen, and that is what makes a fault read instantly.

## Options

| # | Preview | Background | Ring | Accent | Scale | Config |
| --- | --- | --- | --- | --- | --- | --- |
| 001 | _not rendered_ | `#000000` black | `#0d96ff` | `#51b3ff` | `#9aabb8` | [json](001.json) |
| 002 | [view](002.png) | `#000000` black | `#00e5ff` | `#47edff` | `#9ab5b8` | [json](002.json) |
| 003 | [view](003.png) | `#000000` black | `#14ffd0` | `#56ffdd` | `#9ab8b2` | [json](003.json) |
| 004 | [view](004.png) | `#000000` black | `#1fffad` | `#5dffc4` | `#9ab8ad` | [json](004.json) |
| 005 | [view](005.png) | `#000000` black | `#26ff81` | `#63ffa4` | `#9ab8a6` | [json](005.json) |
| 006 | [view](006.png) | `#000000` black | `#a0ff33` | `#baff6c` | `#aab89a` | [json](006.json) |
| 007 | [view](007.png) | `#000000` black | `#ffd119` | `#ffde5a` | `#b8b29a` | [json](007.json) |
| 008 | [view](008.png) | `#000000` black | `#ffa914` | `#ffc156` | `#b8ad9a` | [json](008.json) |
| 009 | [view](009.png) | `#000000` black | `#ff7919` | `#ff9f5a` | `#b8a69a` | [json](009.json) |
| 010 | [view](010.png) | `#000000` black | `#ff582e` | `#ff8768` | `#b8a09a` | [json](010.json) |
| 011 | [view](011.png) | `#000000` black | `#ff2d26` | `#ff6863` | `#b89b9a` | [json](011.json) |
| 012 | [view](012.png) | `#000000` black | `#ff3399` | `#ff6cb6` | `#b89aa9` | [json](012.json) |
| 013 | [view](013.png) | `#000000` black | `#9447ff` | `#b27bff` | `#a69ab8` | [json](013.json) |
| 014 | [view](014.png) | `#000000` black | `#4040ff` | `#7575ff` | `#9a9ab8` | [json](014.json) |
| 015 | [view](015.png) | `#0d1424` navy | `#0d96ff` | `#51b3ff` | `#9aabb8` | [json](015.json) |
| 016 | [view](016.png) | `#0d1424` navy | `#00e5ff` | `#47edff` | `#9ab5b8` | [json](016.json) |
| 017 | [view](017.png) | `#0d1424` navy | `#14ffd0` | `#56ffdd` | `#9ab8b2` | [json](017.json) |
| 018 | [view](018.png) | `#0d1424` navy | `#1fffad` | `#5dffc4` | `#9ab8ad` | [json](018.json) |
| 019 | [view](019.png) | `#0d1424` navy | `#26ff81` | `#63ffa4` | `#9ab8a6` | [json](019.json) |
| 020 | [view](020.png) | `#0d1424` navy | `#a0ff33` | `#baff6c` | `#aab89a` | [json](020.json) |
| 021 | [view](021.png) | `#0d1424` navy | `#ffd119` | `#ffde5a` | `#b8b29a` | [json](021.json) |
| 022 | [view](022.png) | `#0d1424` navy | `#ffa914` | `#ffc156` | `#b8ad9a` | [json](022.json) |
| 023 | [view](023.png) | `#0d1424` navy | `#ff7919` | `#ff9f5a` | `#b8a69a` | [json](023.json) |
| 024 | [view](024.png) | `#0d1424` navy | `#ff582e` | `#ff8768` | `#b8a09a` | [json](024.json) |
| 025 | [view](025.png) | `#0d1424` navy | `#ff2d26` | `#ff6863` | `#b89b9a` | [json](025.json) |
| 026 | [view](026.png) | `#0d1424` navy | `#ff3399` | `#ff6cb6` | `#b89aa9` | [json](026.json) |
| 027 | [view](027.png) | `#0d1424` navy | `#9447ff` | `#b27bff` | `#a69ab8` | [json](027.json) |
| 028 | [view](028.png) | `#0d1424` navy | `#4040ff` | `#7575ff` | `#9a9ab8` | [json](028.json) |
| 029 | [view](029.png) | `#121214` charcoal | `#0d96ff` | `#51b3ff` | `#9aabb8` | [json](029.json) |
| 030 | [view](030.png) | `#121214` charcoal | `#00e5ff` | `#47edff` | `#9ab5b8` | [json](030.json) |
| 031 | [view](031.png) | `#121214` charcoal | `#14ffd0` | `#56ffdd` | `#9ab8b2` | [json](031.json) |
| 032 | [view](032.png) | `#121214` charcoal | `#1fffad` | `#5dffc4` | `#9ab8ad` | [json](032.json) |
| 033 | [view](033.png) | `#121214` charcoal | `#26ff81` | `#63ffa4` | `#9ab8a6` | [json](033.json) |
| 034 | [view](034.png) | `#121214` charcoal | `#a0ff33` | `#baff6c` | `#aab89a` | [json](034.json) |
| 035 | [view](035.png) | `#121214` charcoal | `#ffd119` | `#ffde5a` | `#b8b29a` | [json](035.json) |
| 036 | [view](036.png) | `#121214` charcoal | `#ffa914` | `#ffc156` | `#b8ad9a` | [json](036.json) |
| 037 | [view](037.png) | `#121214` charcoal | `#ff7919` | `#ff9f5a` | `#b8a69a` | [json](037.json) |
| 038 | [view](038.png) | `#121214` charcoal | `#ff582e` | `#ff8768` | `#b8a09a` | [json](038.json) |
| 039 | [view](039.png) | `#121214` charcoal | `#ff2d26` | `#ff6863` | `#b89b9a` | [json](039.json) |
| 040 | [view](040.png) | `#121214` charcoal | `#ff3399` | `#ff6cb6` | `#b89aa9` | [json](040.json) |
| 041 | [view](041.png) | `#121214` charcoal | `#9447ff` | `#b27bff` | `#a69ab8` | [json](041.json) |
| 042 | [view](042.png) | `#121214` charcoal | `#4040ff` | `#7575ff` | `#9a9ab8` | [json](042.json) |
| 043 | [view](043.png) | `#051a1f` deepteal | `#0d96ff` | `#51b3ff` | `#9aabb8` | [json](043.json) |
| 044 | [view](044.png) | `#051a1f` deepteal | `#00e5ff` | `#47edff` | `#9ab5b8` | [json](044.json) |
| 045 | [view](045.png) | `#051a1f` deepteal | `#14ffd0` | `#56ffdd` | `#9ab8b2` | [json](045.json) |
| 046 | [view](046.png) | `#051a1f` deepteal | `#1fffad` | `#5dffc4` | `#9ab8ad` | [json](046.json) |
| 047 | [view](047.png) | `#051a1f` deepteal | `#26ff81` | `#63ffa4` | `#9ab8a6` | [json](047.json) |
| 048 | [view](048.png) | `#051a1f` deepteal | `#a0ff33` | `#baff6c` | `#aab89a` | [json](048.json) |
| 049 | [view](049.png) | `#051a1f` deepteal | `#ffd119` | `#ffde5a` | `#b8b29a` | [json](049.json) |
| 050 | [view](050.png) | `#051a1f` deepteal | `#ffa914` | `#ffc156` | `#b8ad9a` | [json](050.json) |
| 051 | [view](051.png) | `#051a1f` deepteal | `#ff7919` | `#ff9f5a` | `#b8a69a` | [json](051.json) |
| 052 | [view](052.png) | `#051a1f` deepteal | `#ff582e` | `#ff8768` | `#b8a09a` | [json](052.json) |
| 053 | [view](053.png) | `#051a1f` deepteal | `#ff2d26` | `#ff6863` | `#b89b9a` | [json](053.json) |
| 054 | [view](054.png) | `#051a1f` deepteal | `#ff3399` | `#ff6cb6` | `#b89aa9` | [json](054.json) |
| 055 | [view](055.png) | `#051a1f` deepteal | `#9447ff` | `#b27bff` | `#a69ab8` | [json](055.json) |
| 056 | [view](056.png) | `#051a1f` deepteal | `#4040ff` | `#7575ff` | `#9a9ab8` | [json](056.json) |
| 057 | [view](057.png) | `#140c1a` plum | `#0d96ff` | `#51b3ff` | `#9aabb8` | [json](057.json) |
| 058 | [view](058.png) | `#140c1a` plum | `#00e5ff` | `#47edff` | `#9ab5b8` | [json](058.json) |
| 059 | [view](059.png) | `#140c1a` plum | `#14ffd0` | `#56ffdd` | `#9ab8b2` | [json](059.json) |
| 060 | [view](060.png) | `#140c1a` plum | `#1fffad` | `#5dffc4` | `#9ab8ad` | [json](060.json) |
| 061 | [view](061.png) | `#140c1a` plum | `#26ff81` | `#63ffa4` | `#9ab8a6` | [json](061.json) |
| 062 | [view](062.png) | `#140c1a` plum | `#a0ff33` | `#baff6c` | `#aab89a` | [json](062.json) |
| 063 | [view](063.png) | `#140c1a` plum | `#ffd119` | `#ffde5a` | `#b8b29a` | [json](063.json) |
| 064 | [view](064.png) | `#140c1a` plum | `#ffa914` | `#ffc156` | `#b8ad9a` | [json](064.json) |
| 065 | [view](065.png) | `#140c1a` plum | `#ff7919` | `#ff9f5a` | `#b8a69a` | [json](065.json) |
| 066 | [view](066.png) | `#140c1a` plum | `#ff582e` | `#ff8768` | `#b8a09a` | [json](066.json) |
| 067 | [view](067.png) | `#140c1a` plum | `#ff2d26` | `#ff6863` | `#b89b9a` | [json](067.json) |
| 068 | [view](068.png) | `#140c1a` plum | `#ff3399` | `#ff6cb6` | `#b89aa9` | [json](068.json) |
| 069 | [view](069.png) | `#140c1a` plum | `#9447ff` | `#b27bff` | `#a69ab8` | [json](069.json) |
| 070 | [view](070.png) | `#140c1a` plum | `#4040ff` | `#7575ff` | `#9a9ab8` | [json](070.json) |
| 071 | [view](071.png) | `#10141a` slate | `#0d96ff` | `#51b3ff` | `#9aabb8` | [json](071.json) |
| 072 | [view](072.png) | `#10141a` slate | `#00e5ff` | `#47edff` | `#9ab5b8` | [json](072.json) |
| 073 | [view](073.png) | `#10141a` slate | `#14ffd0` | `#56ffdd` | `#9ab8b2` | [json](073.json) |
| 074 | [view](074.png) | `#10141a` slate | `#1fffad` | `#5dffc4` | `#9ab8ad` | [json](074.json) |
| 075 | [view](075.png) | `#10141a` slate | `#26ff81` | `#63ffa4` | `#9ab8a6` | [json](075.json) |
| 076 | [view](076.png) | `#10141a` slate | `#a0ff33` | `#baff6c` | `#aab89a` | [json](076.json) |
| 077 | [view](077.png) | `#10141a` slate | `#ffd119` | `#ffde5a` | `#b8b29a` | [json](077.json) |
| 078 | [view](078.png) | `#10141a` slate | `#ffa914` | `#ffc156` | `#b8ad9a` | [json](078.json) |
| 079 | [view](079.png) | `#10141a` slate | `#ff7919` | `#ff9f5a` | `#b8a69a` | [json](079.json) |
| 080 | [view](080.png) | `#10141a` slate | `#ff582e` | `#ff8768` | `#b8a09a` | [json](080.json) |
| 081 | [view](081.png) | `#10141a` slate | `#ff2d26` | `#ff6863` | `#b89b9a` | [json](081.json) |
| 082 | [view](082.png) | `#10141a` slate | `#ff3399` | `#ff6cb6` | `#b89aa9` | [json](082.json) |
| 083 | [view](083.png) | `#10141a` slate | `#9447ff` | `#b27bff` | `#a69ab8` | [json](083.json) |
| 084 | [view](084.png) | `#10141a` slate | `#4040ff` | `#7575ff` | `#9a9ab8` | [json](084.json) |
