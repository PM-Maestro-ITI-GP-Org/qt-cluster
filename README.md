# qt-cluster

Instrument cluster for a 7" 1024x600 panel. The bezel artwork is the background,
speed on the left, power on the right, numbers only — no drawn gauges. The one
moving graphic is the neon ring, which lights bottom-up with speed.

## Build and run

Desktop, against the Qt Creator kit:

```bash
cmake --build build/Desktop_Qt_6_10_3-Debug -j8 && ./build/Desktop_Qt_6_10_3-Debug/deploy/appCluster
```

If that build directory does not exist yet:

```bash
/home/abdo/Qt/6.10.3/gcc_64/bin/qt-cmake -S . -B build/Desktop_Qt_6_10_3-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Two environment variables drive it without hardware:

| Variable | Effect |
| --- | --- |
| `CLUSTER_DEMO=1` | Sweeps speed 0..250 and cycles the four telltales, which is also what exercises fault mode and the now-playing strip. |
| `CLUSTER_SPEED=120` | Pins the speed. Wins over both the demo sweep and the backend — use it to park the ring on one number. |

QNX cross build:

```bash
rm -rf build-qnx
source ~/qnx-rpi5/repo/qnx800/qnxsdp-env.sh
echo "$QNX_TARGET"      # should now point into ~/qnx-rpi5/repo/qnx800/target/qnx
which qcc               # should agree with $QNX_HOST
~/qt6-qnx/bin/qt-cmake -S . -B build-qnx -G Ninja -DCMAKE_BUILD_TYPE=Release -DQNX_TARGET_ARCH=gcc_ntoaarch64le
cmake --build build-qnx
```

### Why it looks wrong on the target

Two things made the RPi5 panel disagree with the desktop, and both were in the
build rather than in the QML.

**The scene graph backend.** The Qt Quick software adaptation supports neither
item layers nor shader effects, so under it every `MultiEffect` in `Main.qml`
draws nothing — and each one is fed by a `visible: false` source that exists
only to be its input, so what disappears is not an effect over a visible item,
it is the item. The neon ring baseline, the speed-driven glow, both status
bands, their icons and the motor fault lamp all vanish; the flat bezel, the
road, the car and the text stay. That is the whole symptom, and you can
reproduce it on a desktop:

```bash
QT_QUICK_BACKEND=software ./build/Desktop_Qt_6_10_3-Debug/deploy/appCluster
```

`run.sh` used to force `QT_QUICK_BACKEND=software` unconditionally. That was
correct for the Qt it was written against — a cross-build with no OpenGL, where
Qt Quick default-selected the Vulkan RHI backend that `QQnxIntegration` has no
window case for, and qFatal'd on every launch before the window showed. It is
wrong for a Qt-for-QNX that does have OpenGL, where it strips the panel for no
reason.

Since this gets built against more than one Qt-for-QNX tree, the line is no
longer hardcoded either way: `CMakeLists.txt` reads `QT_FEATURE_opengl` from
the Qt in `Qt6_DIR` and generates the appropriate `run.sh`. Configure prints
which one you got, and a Qt without OpenGL now produces a loud warning naming
exactly what will be missing from the panel rather than failing silently.

To check a Qt tree by hand:

```bash
grep QT_FEATURE_opengl <qt-for-qnx>/include/QtGui/qtgui-config.h
```

If that is 0, the forcing is doing its job and the fix is to rebuild Qt for QNX
with OpenGL ES — the generated line then disappears on its own. If it is 1 and
the panel still comes up black, the remaining variable is on the target: Screen's
`graphics.conf` has to load the RPi5 v3d driver, and `libEGL.so.1` /
`libGLESv2.so.1` have to resolve. `QSG_INFO=1` prints the chosen RHI backend.

**The typeface did not exist on either machine.** Every `Text` asked for
`"Century Gothic"`, which is a Microsoft font; `fc-match "Century Gothic"`
returns Noto Sans. The desktop substituted quietly and looked fine. The target
had nothing to substitute *from*: `run.sh` points `FONTCONFIG_FILE` and
`QT_QPA_FONTDIR` at `deploy/lib/fonts`, and `deploy_qt.cmake` was shipping that
directory empty, because `FONT_SOURCE_DIR` defaults to `../qt6-qnx-libs/fonts`,
which is not in every checkout, and a missing font was only a warning.

So the font is embedded now — `fonts/` and `fonts.qrc`, loaded by the two
`FontLoader`s at the top of `Main.qml` — and nothing refers to a family by
name any more. To change the cluster's type, drop the files in `fonts/`, list
them in `fonts.qrc`, and point those two sources at them. `uiFont` picks up the
family name from the loader. A geometric face (URW Gothic is the free relative
of the Century Gothic originally asked for) is closer to the intent than what
ships, if you want it.

`fonts.txt` still deploys the DejaVu set as the target's *fallback* database,
and now falls back to the host's own font directories to find them.

### A note on QNX_TARGET

`QNX_TARGET` is read from the environment, but the `qt-cmake` toolchain file may
have already set it to whichever SDP that Qt was built against. When the two
differ, the recursive deploy searches the wrong sysroot and silently omits
libraries `libQt6Gui` needs — `libfontconfig`, `libfreetype`, `libpng16`,
`libjpeg`, `libtiff`. Pass it explicitly if the build warns about those:

```bash
~/qt6-qnx/bin/qt-cmake -S . -B build-qnx -G Ninja -DCMAKE_BUILD_TYPE=Release -DQNX_TARGET_ARCH=gcc_ntoaarch64le -DQt6_DIR=$HOME/qt6-qnx/lib/cmake/Qt6 -DQNX_TARGET=$QNX_TARGET
```

## Colours

Almost everything is one block at the top of `Main.qml`, just under
`Material.theme`. Edit a value, rebuild, done — no image regeneration.

| Property | What it colours |
| --- | --- |
| `ringColor` | The neon gauge light. Both halves of it — the always-on baseline and the part that lights with speed. **Use a colour at full HSV value**: the tint is applied by colorizing the artwork, which multiplies it by this swatch, so anything dimmer drags the whole ring down with it. |
| `accent` | `KM/H`, `KW`, `SOC`, `KM TOTAL`. |
| `textColor` | Every readout and label: the big numbers, the captions, the clock, the song title, the selected gear. |
| `scaleColor` | The `0..240` and `0..6` numbers around the ring. |
| `gearIdleColor` | The gears that are not selected. |
| `faultColor` | The four telltales when raised, and the lamp over the motor. |
| `iconColor` | The telltales while idle. They are colorized in both states so a light background does not swallow the PNGs' own white. |
| `carTint` / `carTintStrength` | The body colour of both cars. The photographs are near-white and are colorized by luminance, so the shading, glass and wheels survive. Strength 0 leaves them alone; much past 0.5 the glass takes the body colour too. |

```bash
cmake --build build/Desktop_Qt_6_10_3-Debug -j8
```

### The background is different — two files, and a regeneration

The artwork is stretched to cover the window edge to edge, so what you see
behind everything is not `Window.color` in `Main.qml` — it is the flat colour
the artwork's dark areas were collapsed to when the layers were generated.
Editing `Main.qml` alone changes nothing visible.

Both have to move together:

1. `FLAT` in `tools/make_bezel_layers.py` — the real background.
2. `color:` on the `Window` in `Main.qml` — must match, as `#rrggbb` or a
   colour name.

Then regenerate the layers and rebuild:

```bash
python3 tools/make_bezel_layers.py && cmake --build build/Desktop_Qt_6_10_3-Debug -j8
```

The script rewrites four PNGs in `images/` from the source `background` file.
It takes under a second. If the two colours disagree, the artwork's edges show
as a rectangle against the window.

### Don't touch these

The `GradientStop` colours inside `glowMask` in `Main.qml` say `"white"` and
`"transparent"`, but they are not colours — they are the mask that decides how
much of the ring is lit. Changing them changes where the light stops, not what
colour it is.

The ring's hue is *not* in the artwork any more. It is applied at runtime by
colorizing the two ring layers by luminance, which keeps the artwork's own
shading and moves only the hue. So there is no reason to repaint `background`
to recolour the ring.

### One thing worth knowing before picking

Red is the only colour on the screen that currently means something: the
telltales and the lamp over the motor. Using it for `ringColor` or `accent`
costs you that signal.

## Layers

`tools/make_bezel_layers.py` generates all four from the source `background`
JPEG, and must be re-run after any change to that file:

| File | What it is |
| --- | --- |
| `cluster_bezel_base.png` | Everything except the ring, the road and the frame. |
| `cluster_ring_base.png` | The ring at its always-on baseline. Tinted with `ringColor`. |
| `cluster_bezel_glow.png` | The ring at full strength. Tinted with `ringColor`, revealed bottom-up with speed. |
| `cluster_road.png` | The perspective lane graphic. Hidden in fault mode. |

Each split has to be clean in both directions: anything of the ring left in the
base is a glow that never goes dark at a standstill *and* keeps the artwork's
blue when the rest is recoloured.
