# Prompt for regenerating the cluster frame

Paste the block below into the image generator. Everything in it is a
requirement of the build pipeline, not a style preference — the notes after it
say which constraint exists for what, so they can be relaxed knowingly.

Attach the existing mockup as a style reference alongside the prompt.

---

```
A car instrument cluster housing, front-on product render. Brushed dark
titanium and gunmetal frame with soft machined bevels and a fine anodised
grain, dark charcoal inner panels, one continuous neon cyan light-tube outlining
each of the two gauge openings.

Canvas exactly 1024 x 600 pixels. The housing fills the entire canvas edge to
edge, corner to corner. No margin, no border, no matte, no drop shadow outside
the housing, no background visible around it.

Orthographic front view, dead straight on. No camera tilt, no perspective, no
vanishing point, no three-quarter angle. No environment reflections, no studio
highlights sweeping across the metal, no lens flare, no bokeh, no depth of
field.

Perfectly symmetric left to right, mirror-exact.

The image contains ONLY two things: the metal housing, and the cyan light-tube
outline around each gauge opening.

It must contain NO: numbers, digits, text, letters, labels, captions, units,
clock, gear letters, tick marks, scale markings, graduations, needles, pointers,
dials, icons, symbols, warning lamps, telltales, pictograms, car, vehicle,
road, lane lines, segmented bars, progress bars, meters, buttons, screws,
logos, badges, branding, watermarks, signatures.

Both gauge openings are completely empty: filled with flat uniform near-black
#030812, one solid colour, no gradient, no vignette, no texture, no glass
sheen, no reflection, no glow inside them. The area between the two openings,
across the middle of the cluster, is the same flat #030812 and equally empty.

Colour discipline: the cyan light tubes are the ONLY saturated colour anywhere
in the image. All metal, all panels, all shadows are strictly neutral greyscale
with zero colour cast — no blue tint, no teal sheen, no warm highlights, no
coloured ambient light spilling onto the metal from the tubes. The tubes are a
pure saturated cyan, roughly #6CDAFF, evenly lit along their whole length.

Keep each tube's glow tight to the tube — a few pixels of falloff, not a wide
bloom washing over the surrounding metal.

Leave a clear flat #030812 band roughly 40 pixels wide running just outside the
lower outer edge of each cyan tube, following its curve, free of any metal
detail, panel line, or highlight.

Crisp focus across the whole frame. Clean flat vector-like rendering, no film
grain, no noise, no JPEG artifacts. Deliver as PNG.
```

---

## Why each constraint is there

**1024 x 600, filling the canvas.** This is the panel's native resolution and
what Main.qml will draw the art into 1:1. The current `background` is 1024x447
stretched to 1208x600, which squashes it vertically and overscans it sideways;
authoring at final size drops both. `bezelScale` goes to 1.0 when this lands.

**Front-on, no perspective, no reflections.** The art is a background plate that
live elements are positioned against by measured coordinates. Anything implying
a camera position fights the flat overlays sitting on top of it.

**Mirror-exact symmetry.** Several measured tables — the status band path, the
telltale positions — are generated for the left side and mirrored. The current
artwork is not quite symmetric (its shoulders measure 14 and -12.2 degrees) and
that had to be worked around.

**No text, numbers, icons or car.** Every one of those is drawn live in QML over
the top. Baked into the plate they would double up, and they cannot be removed
afterwards without reconstructing what was behind them.

**Empty openings, flat #030812.** `FLAT` in `make_bezel_layers.py`, and the
`Window` colour in Main.qml, must agree with the plate's dark areas or the
artwork's edge shows as a rectangle against the window. A gradient or vignette
in the lens would also sit visibly behind the speed readout.

**Cyan is the only saturated colour.** This is the important one. The tubes have
to be split out of the plate into their own layer so they can be recoloured at
runtime and lit progressively with speed — that is the whole gauge. Splitting by
brightness is what the current script does and it will not work on bright metal.
Splitting by *saturation* is reliable, but only if nothing else in the image is
saturated. A blue sheen on the metal breaks it.

**Tight glow.** A wide bloom cannot be separated from the metal underneath it,
so it ends up baked into the frame and glows even at a standstill.

**The clear band outside each tube.** The charge and temperature meters are drawn
there. Metal detail underneath would read through the gaps between their bars.

## What is deliberately not requested

**No perspective road.** The lane graphic in the lens is easier to generate
directly — it is converging rails with rungs, and a script can place it to match
the overhead road already drawn in QML for fault mode. Asking the image model
for it would also break the saturation split, since it would have to be cyan.

## After the plate arrives

1. Drop it in as `background_v2.png`.
2. Retune `make_bezel_layers.py` to split on saturation rather than brightness,
   and drop its `ROAD_BOX` handling.
3. Set `bezelScale` to 1.0 and re-measure `glowY`, the telltale positions, and
   the scale label positions against the new opening shapes.
4. Re-run `make_status_bands.py`; it traces the ring, so the bands follow the new
   arc automatically. Regenerate the bar table for the new curve.
