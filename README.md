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

## Replaying a recorded CSV

`CLUSTER_DEMO` sweeps a made-up ramp. To watch speed and power move on *real*
data, replay a recording into the shared memory the cluster already reads —
no STM32, no Pi. The cluster cannot tell the difference: same `shm_region_t`,
same 200-row blocks, same 100 Hz cadence.

Build it once. It needs `motor_shm.h` from the producer tree, which is not
vendored here, so the include path points next door:

```bash
gcc -std=c11 -O2 -I../../motor-data-producer tools/csvreplay.c -o /tmp/csvreplay -lrt
```

Then, in one terminal:

```bash
/tmp/csvreplay ~/projects/Graduation_project/AI/data/data_for_anomaly/normal/long/mafkok_pcb_large.csv
```

and in another:

```bash
./build/Desktop_Qt_6_10_3-Debug/deploy/appCluster
```

Start the replayer first — the cluster gives up after ~5 s if the region is not
there. It loops until Ctrl-C and prints the speed command once a second, so you
can check the cluster against it:

```
loaded 595200 rows (29.8 s at 20000 Hz)
replaying into /motor_ctrl -- start the cluster now, Ctrl-C to stop
  t=  1.0s  speed_cmd=1633 counts
  t=  2.0s  speed_cmd=2286 counts
```

Do **not** set `CLUSTER_DEMO` or `CLUSTER_SPEED` at the same time — both
override the backend, so the readouts would ignore the file.

Any CSV works as long as it has these columns, one row per 20 kHz sample:

```
timestamp,current_0..current_7,vib_x,vib_y,vib_z,rpm
```

`current_3` is the speed command and `current_0..2` / `current_4..6` are the
phase currents and voltages — the full assignment is `MotorChannel` in
`SpiReader.h`. Malformed lines are skipped.

### If the numbers look wrong rather than absent

The scale factors in `cluster.h` were calibrated against the recording above,
not measured off the hardware:

| Constant | Set from |
| --- | --- |
| `SPEED_CMD_ZERO_COUNTS` | The command idles at ~1055 counts, **not 0** — with a zero of 0 the cluster reads ~64 km/h standing still. |
| `KMH_PER_COUNT` | ~1055 idle to ~4089 full, mapped to 0..240 km/h. |
| `VOLTS_PER_COUNT` | Full-load phase RMS of 952 counts of voltage against 1442 of current, put at 6 kW so it spans the 0..6 scale. |
| `POWER_FACTOR` | A flat 0.85. `V_rms * I_rms` is apparent power and ignores the phase angle, so it reads high on a motor. |

A different rig will idle somewhere else and swing a different amount. Fix the
zero first — an offset there shows as speed at a standstill, which is the
obvious symptom — then the span.

QNX cross build:

```bash
rm -rf build-qnx
source ~/qnx-rpi5/repo/qnx800/qnxsdp-env.sh
echo "$QNX_TARGET"      # should now point into ~/qnx-rpi5/repo/qnx800/target/qnx
which qcc               # should agree with $QNX_HOST
~/qt6-qnx/bin/qt-cmake -S . -B build-qnx -G Ninja -DCMAKE_BUILD_TYPE=Release -DQNX_TARGET_ARCH=gcc_ntoaarch64le
cmake --build build-qnx
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
