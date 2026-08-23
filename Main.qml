import QtQuick
import QtQuick.Effects
import QtQuick.Controls.Material

// Cluster: the bezel artwork as the background, with speed on the left and
// power on the right. Numbers only, no gauges.
Window {
    id: root
    // Target hardware: 7" panel at 1024x600. See bezelScale for how the
    // 1024x447 artwork is fitted to it.
    width: 1024
    height: 600
    visible: true
    title: "Cluster"
    // Must match FLAT in tools/make_bezel_layers.py. The artwork's dark areas
    // are collapsed to this exact colour so the bezel silhouette disappears and
    // the screen reads as a plain rectangle.
    //
    // Since the stretch, the artwork covers the window edge to edge, so this is
    // no longer what you see behind it — FLAT is. Changing it here on its own
    // has no visible effect; the layers have to be regenerated to match.
    color: "#030812"

    Material.theme: Material.Dark

    // --- Colour scheme -------------------------------------------------------
    // ringColor drives both halves of the neon ring: the always-on baseline
    // (cluster_ring_base.png) and the lit part (cluster_bezel_glow.png). Both
    // are colorized by luminance, so the artwork's own shading survives and only
    // the hue changes — which is why the ring is worth recolouring here rather
    // than repainting in the source artwork.
    //
    // The background is NOT here: it is FLAT in tools/make_bezel_layers.py, and
    // changing it means regenerating the layers.
    // See README.md for what each one touches and when a rebuild is enough.
    readonly property color ringColor: "#6cdaff" // the neon gauge light
    readonly property color accent: "#8ce2ff"       // KM/H, KW, SOC, KM TOTAL
    readonly property color textColor: "white"      // every readout and label
    readonly property color scaleColor: "#8ea3ba"   // the 0..240 / 0..6 numbers
    readonly property color gearIdleColor: "#4c5c70" // the gears not selected
    readonly property color faultColor: "#ff2b2b"   // motor lamp, code and banner
    // Caution, as against faultColor's stop-now. Amber and red are the two the
    // driver already reads without being taught them, which is the whole reason
    // severity is carried as a hue rather than as a word: "CRITICAL" spelled out
    // is slower to read at 7" than a colour is, and this cluster is numerals
    // only by design.
    readonly property color warnColor: "#ffb020"

    // --- Fault severity ------------------------------------------------------
    // 0 nothing, 1 caution, 2 critical. Nothing new is measured for this --
    // every input already existed on VehicleBackend and simply had nothing
    // bound to it:
    //
    //   criticalAlert   vibration past VIB_CRIT_G (0.60g, double the warn
    //                   threshold) or current past 1.5x CURRENT_WARN_A. A
    //                   genuine two-level distinction, from sensors.
    //   vibWarning      the same two limits at their first threshold.
    //   currentWarning
    //   aiAlert         the model's verdict, which carries NO severity of its
    //                   own -- aiFaultClass is free text and AI_ALERT_HEALTH
    //                   clamps health to a flat 0.35 however bad the fault is.
    //
    // So an AI verdict enters at caution and only the measured limits can
    // escalate it. That is a deliberate floor, not a guess dressed up as one:
    // promoting a fault to critical needs evidence, and the AI side does not
    // supply any yet. Give the shm contract a severity field and this is the
    // one place that has to change.
    //
    // speedWarning is left out on purpose, matching why it is absent from
    // criticalAlert -- see SPEED_WARN_RPM in cluster.h.
    // The severity the AI side actually publishes. It is not a field -- it is the
    // word "critical" appended to fault_class, which is how fault_tester and the
    // model both mark an urgent fault:
    //
    //     electrical_fault              advisory
    //     electrical_fault critical     urgent
    //     mechanical_fault              advisory
    //     bearing_fault critical        urgent
    //
    // Read off the live /motor_fault_override region on the target. An earlier
    // version of this file asserted the AI carried no severity at all and made
    // level 2 reachable only through criticalAlert -- which comes from measured
    // vibration and current, and which an injected fault cannot set. The effect
    // was that NO injected fault could ever go red, however urgent: every one
    // of them showed amber. This is that assertion corrected.
    readonly property bool aiCritical: Vehicle.aiFaultClass.toLowerCase().indexOf("critical") !== -1
                                       || Vehicle.aiAnomaly.toLowerCase().indexOf("critical") !== -1

    readonly property int faultLevel: (Vehicle.criticalAlert || root.aiCritical) ? 2
                                    : (Vehicle.aiAlert || Vehicle.vibWarning
                                       || Vehicle.currentWarning) ? 1 : 0

    // Severity is carried TWICE, colour and rhythm, the way the health band
    // carries its reading as both length and colour. Two channels because one
    // of them fails in the real cabin: amber and red are hard to separate on a
    // 7" panel in direct sun, and the blink rate still reads when the hue does
    // not.
    readonly property color severityColor: root.faultLevel >= 2 ? root.faultColor
                                                                : root.warnColor
    // The code blink. 260ms was the old single rate; it now means critical, and
    // caution gets a slower one that reads as insistent rather than urgent.
    readonly property int severityBlinkMs: root.faultLevel >= 2 ? 260 : 600
    // The lamp breathe under the car. Deliberately slower than the code at both
    // levels -- it is a wash of light over the whole car, and at the code's rate
    // it strobes.
    readonly property int severityLampMs: root.faultLevel >= 2 ? 350 : 700
    // The overhead road drawn in fault mode. Sampled off the rails in
    // cluster_road.png at their brightest, so the two roads are the same colour
    // and the swap does not read as a change of scene.
    readonly property color topRoadColor: "#a4aab8"

    // The car photographs are near-white, so they can be pushed to any body
    // colour by colorizing them — luminance is preserved, so the panel shading,
    // the glass and the wheels all survive. Strength 0 leaves the photo alone;
    // much past 0.5 the glass starts taking the body colour too.
    readonly property color carTint: "white"
    readonly property real carTintStrength: 0.0

    // --- Typeface ------------------------------------------------------------
    // Embedded, not looked up by name. Every Text below used to ask for
    // "Century Gothic" -- a Microsoft font that is on neither the build host
    // nor the QNX target, so neither machine was ever drawing it:
    //
    //   fc-match "Century Gothic"   ->  NotoSans-Regular.ttf
    //
    // The host silently substituted through fontconfig and looked fine. The
    // target had nothing to substitute FROM: run.sh points FONTCONFIG_FILE at
    // deploy/lib/fonts, and deploy_qt.cmake was shipping that directory empty
    // (FONT_SOURCE_DIR pointed at ../qt6-qnx-libs/fonts, which does not exist,
    // and a missing font was only a warning). So the two ends disagreed on
    // every glyph, which is half of why the panel does not match the desktop.
    //
    // Loading the file makes the answer the same on both, with no dependency on
    // what the image happens to install. The two faces register under one
    // family, so font.weight still picks between them -- Font.Bold gets the
    // bold face, and the lighter weights the design asks for (ExtraLight,
    // Light) resolve to Regular, which is what the desktop was already doing.
    //
    // To change the cluster's type, drop the .ttf files in fonts/, list them in
    // fonts.qrc, and point these two sources at them. Nothing else refers to a
    // family by name. A geometric face -- URW Gothic is the free relative of
    // the Century Gothic this originally asked for -- is the closest thing to
    // the intent, if you would rather have that than what ships here.
    FontLoader {
        id: uiFontRegular
        source: "qrc:/fonts/fonts/NotoSans-Regular.ttf"
    }
    FontLoader {
        id: uiFontBold
        source: "qrc:/fonts/fonts/NotoSans-Bold.ttf"
    }

    // The family name the loader reports, which is what Text.font.family wants.
    // Falls back to the platform default rather than to an empty string if the
    // resource ever goes missing, so a broken qrc degrades to *a* font instead
    // of to no text at all.
    readonly property string uiFont: uiFontRegular.status === FontLoader.Ready
                                     ? uiFontRegular.font.family : "sans-serif"

    // Smoothed backend values, shared by the readouts and the neon glow so the
    // number and the light ramp together instead of snapping between frames.
    // Demo sweep, active only when CLUSTER_DEMO is set in the environment (see
    // main.cpp). Lets the cluster be driven on a desktop where there is no
    // motor_controller feeding the SPI reader.
    property real demoSpeed: 0

    // Slow on purpose: the sweep exists to check where the light lands against
    // each number, which is hard to judge if it races past. Raise the two
    // durations (milliseconds) to slow it further.
    //
    // Linear rather than eased, so speed climbs at a constant rate and the
    // light spends the same time between every pair of numbers. InOutSine made
    // it crawl at the ends and rush through the middle of the scale.
    SequentialAnimation on demoSpeed {
        running: demoMode
        loops: Animation.Infinite
        NumberAnimation {
            to: Vehicle.speedMax
            duration: 40000
        }
        PauseAnimation {
            duration: 2500
        }
        NumberAnimation {
            to: 0
            duration: 30000
        }
        PauseAnimation {
            duration: 2000
        }
    }

    // Straight bindings, deliberately unanimated. A Behavior here restarts on
    // every write, so a source that changes each frame leaves the property
    // pinned at its starting value; a standalone SmoothedAnimation latches `to`
    // when it starts and never re-targets. If the SPI data turns out jittery,
    // smooth it in VehicleBackend rather than reintroducing either of those.
    // CLUSTER_SPEED wins over both the demo sweep and the backend, so the glow
    // can be parked on one number while glowY is tuned.
    // Two pairs, and which one a widget uses matters. `speed`/`power` are
    // continuous and drive the ring's glow; `speedShown`/`powerShown` change
    // twice a second and drive the numerals. See DISPLAY_PERIOD_S in cluster.h.
    QtObject {
        id: live
        readonly property real speed: fixedSpeed >= 0 ? fixedSpeed : demoMode ? root.demoSpeed : Vehicle.speed
        // Watts. Held proportional to speed across the demo, so the two dials
        // reach their matching marks together — the ring is driven by speed
        // alone, so anything else would light the power scale at the wrong
        // number. Full scale on one is full scale on the other.
        readonly property real power: fixedSpeed >= 0 ? fixedSpeed / Vehicle.speedMax * Vehicle.powerMax
                                                      : demoMode ? root.demoSpeed / Vehicle.speedMax * Vehicle.powerMax
                                                                 : Vehicle.power

        // The numerals. On hardware these come from the backend's 500ms window
        // mean; the demo has no noise to average, so it latches the sweep at
        // the same 2 Hz to keep the two paths behaving alike.
        readonly property real speedShown: (fixedSpeed >= 0 || demoMode) ? root.demoHeldSpeed
                                                                         : Vehicle.speedDisplay
        readonly property real powerShown: (fixedSpeed >= 0 || demoMode) ? root.demoHeldPower
                                                                         : Vehicle.powerDisplay
    }

    // Demo-side 2 Hz latch, mirroring what the backend does for real data.
    property real demoHeldSpeed: 0
    property real demoHeldPower: 0

    Timer {
        interval: Math.round(1000 / Vehicle.displayHz)
        running: demoMode || fixedSpeed >= 0
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            root.demoHeldSpeed = live.speed;
            root.demoHeldPower = live.power;
        }
    }

    // glowY is indexed by scaleValues, so a short array silently produces NaN
    // from the last valid entry upward and the glow stops moving.
    Component.onCompleted: {
        if (root.glowY.length !== root.scaleValues.length)
            console.warn("glowY has", root.glowY.length, "entries but scaleValues has", root.scaleValues.length + "; the glow will break above", root.scaleValues[root.glowY.length - 1], "km/h");
    }

    // Fractions of the bezel artwork occupied by its lit lens, measured off the
    // neon outline in the source image (x 146..878, y 105..350 of 1024x447).
    // Nudge these if the readouts don't sit where you want in the lens —
    // nothing else needs to change.
    readonly property real screenLeftFrac: 0.143
    readonly property real screenRightFrac: 0.857
    readonly property real screenTopFrac: 0.235
    readonly property real screenBottomFrac: 0.783

    // SOC and KM TOTAL. Pulled up from 0.905: the vertical stretch moved this
    // row down with the artwork and left it 7px off the bottom edge, which is
    // no margin at all on a panel with any overscan.
    readonly property real bottomRowY: 0.882

    // Speed at which the neon ring reaches full brightness.
    readonly property real glowTopSpeed: 250

    // Both layers are generated from the source `background` JPEG in this
    // directory: that file has the transparency checkerboard baked in as real
    // pixels, so it is keyed back out to alpha, and then the blue neon ring is
    // split off into its own layer so its brightness can be driven.
    //
    // Artwork rect in window coordinates. Both layers are placed with it, and
    // every position below is a fraction of it.
    //
    // bezelScale over 1 lets the artwork spill past the window edges so the
    // shape reads larger on a small panel. Only the artwork's empty margins go
    // off-screen: the ring occupies 0.113..0.887 of the width, so it stays
    // fully visible up to a scale of about 1.29 — which is not enough to fill
    // a 1.71:1 panel with 2.29:1 artwork, so the height is stretched instead.
    //
    // It also sets how big everything overlaid is drawn, through artUnitH, so
    // it is the knob that grows the numerals and icons along with the bezel.
    // Raising it alone does nothing to the vertical margins: those are artH's
    // job, and artFill below is what moves them.
    //
    // Two heights, and the difference is the whole trick:
    //
    //   artH      what the artwork is drawn at, so it fills the panel.
    //   artUnitH  what it would be at true aspect.
    //
    // POSITIONS are fractions of artH, so they keep tracking the artwork
    // feature they were tuned against as it stretches. SIZES are fractions of
    // artUnitH, because everything overlaid has its own aspect — glyphs, icons,
    // both cars — and measuring them against a stretched height would make them
    // proportionally wider too. That widening is what would wreck the layout;
    // the stretch itself only touches the three artwork layers.
    readonly property real bezelScale: 1.22
    readonly property real artW: width * bezelScale
    readonly property real artUnitH: artW * (447 / 1024)
    readonly property real artX: (width - artW) / 2

    // --- Vertical fit --------------------------------------------------------
    // artH used to be exactly `height`, which left the artwork's visible band
    // sitting at y 137..542 of 600: 137px of dead space above it and only 58
    // below. The imbalance is not a centring bug, it is where the lit lens sits
    // inside its own source image -- the band occupies 0.228..0.903 of the
    // artwork's height, which is off-centre by the artwork's own construction.
    //
    // Two knobs, and they do different jobs:
    //
    //   artFill   how much taller than the window to draw the artwork. Above 1
    //             the band grows and the margins close up. It costs vertical
    //             stretch -- the artwork is 2.29:1 against a 1.71:1 panel, so
    //             there is no scale that fills the height without stretching
    //             (bezelScale cannot do it: widening past ~1.29 runs the ring
    //             off the sides, see the note above). bezelStretch below is the
    //             number to watch.
    //
    //   artTopBias  which end the overflow comes off. 0.5 centres the band and
    //             leaves the two margins equal; above 0.5 takes more off the
    //             top, which is what closes the gap the eye actually notices.
    //
    // Everything overlaid tracks this automatically -- POSITIONS are fractions
    // of artH and are offset by artY, so the scale numbers, the readouts, the
    // gear row and both cars move with the artwork rather than needing their
    // own tables refitted. SIZES stay fractions of artUnitH, so the stretch
    // does not make glyphs wider; bezelScale is what grows those.
    readonly property real artFill: 1.20
    readonly property real artTopBias: 0.75
    readonly property real artH: height * artFill
    readonly property real artY: -(artH - height) * artTopBias
    // ~1.32 at 1024x600, up from ~1.14 when artH was exactly `height`. That rise
    // is the cost of closing the top margin: see artFill above for why nothing
    // else can. Set artFill to 1 and artTopBias to 0.5 to get the old fit back.
    readonly property real bezelStretch: artH / artUnitH

    Image {
        id: bezel
        x: root.artX
        y: root.artY
        width: root.artW
        height: root.artH
        source: "qrc:/images/images/cluster_bezel_base.png"
        // Stretch, not PreserveAspectFit: the rect is taller than the source's
        // aspect on purpose, and PreserveAspectFit would just letterbox inside
        // it and undo that.
        fillMode: Image.Stretch
        smooth: true
        mipmap: true
    }

    // The ring's always-on baseline, split out of the base so it can be tinted
    // with the lit part. Without the split the unlit ring keeps the artwork's
    // blue and shows as a blue outline above wherever the light has reached.
    Image {
        id: ringBaseImage
        x: root.artX
        y: root.artY
        width: root.artW
        height: root.artH
        source: "qrc:/images/images/cluster_ring_base.png"
        fillMode: Image.Stretch
        smooth: true
        mipmap: true
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        x: root.artX
        y: root.artY
        width: root.artW
        height: root.artH
        source: ringBaseImage
        colorization: 1.0
        colorizationColor: root.ringColor
    }

    // The lane graphic, split out of the base by tools/make_bezel_layers.py so
    // fault mode can drop it. It is placed with the same rect as the base and
    // carries its original position inside a full-size transparent PNG, so the
    // two cannot drift apart.
    Image {
        x: root.artX
        y: root.artY
        width: root.artW
        height: root.artH
        source: "qrc:/images/images/cluster_road.png"
        fillMode: Image.Stretch
        smooth: true
        mipmap: true
        opacity: root.faultMode ? 0.0 : 1.0
        Behavior on opacity {
            NumberAnimation {
                duration: 260
            }
        }
    }

    // --- Road motion ---------------------------------------------------------
    // The lane in cluster_road.png is a still photograph of a road: two bright
    // converging bars with faint rungs laddering the shoulders. Nothing in it
    // moves, so at 140 km/h the cluster looked exactly as it does parked. These
    // are rungs of the same kind, laid over the artwork and scrolled toward the
    // viewer at a rate set by speed.
    //
    // The geometry is measured off the artwork, not invented, so the drawn rungs
    // sit on the same lane the photograph shows. Tracking the brightest pixel of
    // each bar down the image gives a straight convergence:
    //
    //     halfWidth(y) = roadHalfW0 + roadHalfWk * y     (fractions of artW)
    //
    // which reaches zero -- the vanishing point -- at roadHorizonY. Re-measure
    // these three if the bezel artwork ever changes shape; tools/ has no script
    // for it because it is three numbers off a straight-line fit.
    readonly property real roadHorizonY: 0.5161   // where the lane converges
    readonly property real roadHalfW0: -0.29679
    readonly property real roadHalfWk: 0.57504
    // The band the artwork's own bars actually occupy. Outside it there is no
    // road to lay a rung on.
    readonly property real roadNearY: 0.725
    readonly property real roadFarY: 0.566
    // Rungs ladder the SHOULDER, from each bar outward, never across the middle
    // -- the same rule the overhead road follows, and the centre is where the
    // car goes. 0.60 of the half-width is what the artwork's own rungs measure.
    readonly property real roadShoulder: 0.60
    readonly property int roadRungs: 6

    // Perspective. On a ground plane the screen offset from the horizon goes as
    // 1/distance, so rungs evenly spaced along the road are NOT evenly spaced
    // down the screen -- they bunch toward the vanishing point. Spacing them
    // evenly in y instead reads as a ladder being winched past, not as a road.
    readonly property real roadZNear: 1 / (root.roadNearY - root.roadHorizonY)
    readonly property real roadZFar: 1 / (root.roadFarY - root.roadHorizonY)

    // Speed -> scroll. Proportional, with no cap: the ring saturates at the top
    // of the scale and the road should not, or the last 100 km/h would look the
    // same as the first.
    // Full scroll rate at full scale, and full scale is the MOTOR's, taken from
    // the backend rather than written here. SPEED_MAX_KMH is 60 on this rig --
    // 790 rpm through KMH_PER_RPM -- so a hardcoded 120 made the road crawl at
    // half rate with the throttle wide open, and no value would have been right
    // for a different motor. Same reason scaleValues is generated from
    // Vehicle.speedMax instead of being a hardcoded 0..240.
    readonly property real roadSpeedRef: Vehicle.speedMax
    readonly property real roadRateMax: 1.1       // patterns/second at full scale
    readonly property real roadRate: root.roadRateMax * live.speed / root.roadSpeedRef

    // Faded in over the bottom eighth of the scale, so a standstill leaves the
    // artwork exactly as it was and a crawl does not strobe. A fraction of the
    // scale rather than a fixed km/h, for the same reason as above.
    readonly property real roadFadeSpeed: Vehicle.speedMax * 0.12
    readonly property real roadMotionOpacity: Math.max(0, Math.min(1, live.speed / root.roadFadeSpeed)) * 0.34

    property real roadPhase: 0

    // The car sits still while the road moves under it, which is correct for a
    // chase camera -- but dead still reads as a photograph pasted on. This is a
    // very small vertical tremor, amplitude in fractions of artUnitH, driven off
    // the same phase so it costs no second timer and so its rate rises with
    // speed the way the road's does. About 1.4px at the default; enough to be
    // felt and not enough to be seen as bouncing. Set carShakeAmp to 0 to stop
    // it without touching anything else.
    readonly property real carShakeAmp: 0.0025
    readonly property real carShakeHz: 3          // cycles per road pattern
    readonly property real carShake: root.artUnitH * root.carShakeAmp
                                     * Math.sin(root.roadPhase * 2 * Math.PI * root.carShakeHz)
                                     * Math.max(0, Math.min(1, live.speed / root.roadFadeSpeed))

    // A phase accumulator rather than a looping NumberAnimation on purpose. An
    // animation latches its duration for the whole loop, so a speed change would
    // not be felt until the pattern came round again -- seconds, at low speed.
    // Stops itself when stationary, so a parked cluster runs no timer at all.
    Timer {
        interval: 16
        running: root.roadRate > 0 && !root.faultMode
        repeat: true
        onTriggered: root.roadPhase = (root.roadPhase + root.roadRate * (interval / 1000)) % 1
    }

    Item {
        id: roadMotion
        x: root.artX
        y: root.artY
        width: root.artW
        height: root.artH
        opacity: root.roadMotionOpacity
        visible: opacity > 0 && !root.faultMode

        Repeater {
            model: root.roadRungs

            delegate: Item {
                required property int index

                // Position along the band: 0 at the near end, 1 at the horizon.
                // The wrap is what makes it a loop -- as the phase advances each
                // rung walks toward the viewer and the one that runs off the
                // bottom reappears at the vanishing point.
                readonly property real t: (((index - root.roadPhase) % root.roadRungs)
                                           + root.roadRungs) % root.roadRungs / root.roadRungs
                // Not `z`: Item.z is the stacking order and is FINAL, so a
                // property of that name here fails to load the whole component.
                readonly property real dist: root.roadZNear
                                             + t * (root.roadZFar - root.roadZNear)
                readonly property real yFrac: root.roadHorizonY + 1 / dist
                readonly property real halfW: root.roadHalfW0 + root.roadHalfWk * yFrac
                readonly property real len: halfW * root.roadShoulder

                // Faded at BOTH ends, and both are needed: a rung leaving at the
                // bottom and one arriving at the horizon are the same rung one
                // wrap apart, so a hard edge at either end reads as a pop.
                readonly property real fade: Math.max(0, Math.min(1, t / 0.10))
                                             * Math.max(0, Math.min(1, (1 - t) / 0.30))

                anchors.fill: parent

                Repeater {
                    model: [-1, 1]

                    delegate: Rectangle {
                        readonly property real thick: Math.max(1, root.artUnitH * 0.020 * halfW)

                        width: root.artW * len
                        height: thick
                        x: modelData < 0 ? root.artW * (0.5 - halfW - len)
                                         : root.artW * (0.5 + halfW)
                        y: root.artH * yFrac - height / 2
                        color: root.topRoadColor
                        opacity: fade
                    }
                }
            }
        }
    }

    // --- Fault-mode road (overhead) ------------------------------------------
    // cluster_road.png seen from straight above, so swapping the two reads as the
    // camera moving rather than as a different scene. It crossfades against the
    // perspective road on the same 260ms, so the lens is never empty.
    //
    // Drawn here rather than generated: make_bezel_layers.py can only split what
    // is already in the source artwork, and there is no overhead road in it. As
    // QML the geometry below is the only thing to edit — no regeneration step.
    //
    // Every proportion below was measured off cluster_road.png rather than
    // chosen, which is what makes the two roads look like one road. Per side,
    // working outward from the empty centre lane:
    //
    //   dim band  ]  the lane edge, drawn as two lines with a dark gap between
    //   dark gap  ]  them — that gap is what gives the bar its bevelled look.
    //   bright line  The brightest thing in the graphic; sits on the OUTSIDE.
    //   shoulder     Empty, and much wider than the bar itself.
    //   outer line   Thin, about the weight of the bright line.
    //
    // The ticks ladder the shoulder — they run from the outer line to the bar,
    // and never cross the centre. The centre lane carries no marking at all;
    // that is where the car goes.
    //
    // The widths are fractions of the inner lane gap, and they hold at both ends
    // of the perspective graphic (measured at gaps of 113px and 185px, agreeing
    // to within 0.005), so they are the artwork's real proportions rather than
    // one sample. Change topRoadLane and the whole figure scales with it.
    //
    // Declared before both cars, so it paints under them and the car sits in the
    // lane rather than behind it.
    //
    // The band is bounded by what is already centred in the lens: the now-playing
    // strip at 0.17 above, the gear letters at 0.86 below.
    //
    // 0.108 puts the overhead car across 64% of the lane, which is the fraction
    // the rear-view car covers of the perspective lane it sits in — so the car
    // is the same size relative to its road in both views.
    readonly property real topRoadLane: 0.108      // inner gap, fraction of artW
    // 0.042 rather than the artwork's 0.049. The perspective bar is 10.6px wide
    // on screen where it is furthest away and 20.2px where it is nearest; a
    // constant-width bar has to pick one number out of that range, and sitting
    // just under the middle of it is what reads as the same weight.
    readonly property real topRoadDim: 0.042       // the rest are fractions of that gap
    readonly property real topRoadBevel: 0.016
    readonly property real topRoadBright: 0.033
    // 0.50, up from the artwork's 0.327. The ratio is right for a lane seen in
    // perspective, where the shoulder fan spreads wide toward the viewer, but
    // held at constant width it left the overhead road a narrow tall corridor
    // against a perspective road that is wide and low. Widening it opens the
    // footprint out; past about 0.55 the ladders start to dominate the car.
    readonly property real topRoadShoulder: 0.50
    readonly property real topRoadOuter: 0.027
    readonly property real topRoadTickW: 0.012
    // Four, matching the artwork. In perspective the ticks bunch up as they
    // recede and read as a fan; held at constant spacing they read as a ladder
    // instead, so they are kept much fainter than measured to stay a texture
    // rather than compete with the car.
    readonly property int topRoadTicks: 4
    // Band extent, fraction of artH. The top has to clear the clock, which sits
    // at the top of the `screen` item at screenTopFrac (0.235) and runs to about
    // 0.29 — at 0.25 the road ran straight up behind the digits.
    readonly property real topRoadTop: 0.35
    // Clears the gear letters, which start at 0.83. The fade has already taken
    // the road to nothing by then; lower this and it reappears behind the P N R.
    readonly property real topRoadBottom: 0.80
    // Fractions of the band faded at each end, and they are not equal. The top
    // is where the road runs out into the empty part of the lens under the
    // clock, so it needs a long ramp to not read as a cut line; the bottom is
    // hidden against the gear row and can be shorter.
    readonly property real topRoadFadeTop: 0.42
    readonly property real topRoadFadeBottom: 0.18

    // Brightness, and these are the artwork's own ratios: the dim band and the
    // ticks sit near 0.4 of the bright line, the outer line at 0.5.
    //
    // The master is 1.0 and should stay there. Measured on a horizontal slice,
    // the bright line then peaks at 154 against the perspective bar's 154 — the
    // same white, not a dimmed approximation of it. Earlier passes ran it at
    // 0.55 to hold the overall weight down, which is what made the lines look
    // washed rather than drawn; the weight belongs in the widths and the fade,
    // not in a blanket dimming of a graphic that is supposed to match.
    readonly property real topRoadOpacity: 1.0
    readonly property real topRoadDimOpacity: 0.40
    readonly property real topRoadOuterOpacity: 0.50
    readonly property real topRoadTickOpacity: 0.30

    Item {
        id: topRoad

        readonly property real lane: root.artW * root.topRoadLane
        readonly property real wDim: lane * root.topRoadDim
        readonly property real wBevel: lane * root.topRoadBevel
        readonly property real wBright: lane * root.topRoadBright
        readonly property real wShoulder: lane * root.topRoadShoulder
        readonly property real wOuter: lane * root.topRoadOuter
        // Centre to each element's inner edge, outward.
        readonly property real dBar: lane / 2
        readonly property real dBarOuter: dBar + wDim + wBevel + wBright
        readonly property real dOuter: dBarOuter + wShoulder
        readonly property real halfWidth: dOuter + wOuter

        width: halfWidth * 2
        height: root.artH * (root.topRoadBottom - root.topRoadTop)
        x: root.artX + root.artW * 0.5 - width / 2
        y: root.artY + root.artH * root.topRoadTop
        visible: false
        layer.enabled: true

        // One pass per side. `inner` is the distance from the lane centre to the
        // element's inner edge, so every rect is placed by the same rule and the
        // two sides cannot drift apart.
        Repeater {
            model: [-1, 1]

            delegate: Item {
                id: shoulder
                anchors.fill: parent
                readonly property real side: modelData

                function place(inner, w) {
                    return shoulder.side > 0 ? topRoad.width / 2 + inner
                                             : topRoad.width / 2 - inner - w;
                }

                // Ticks first, so the lines draw over their ends.
                Repeater {
                    model: root.topRoadTicks

                    delegate: Rectangle {
                        width: topRoad.wShoulder
                        height: Math.max(1, topRoad.lane * root.topRoadTickW)
                        x: shoulder.place(topRoad.dBarOuter, width)
                        y: topRoad.height * (index + 0.5) / root.topRoadTicks - height / 2
                        color: root.topRoadColor
                        opacity: root.topRoadTickOpacity
                    }
                }

                Rectangle {
                    width: Math.max(1, topRoad.wDim)
                    height: parent.height
                    x: shoulder.place(topRoad.dBar, width)
                    color: root.topRoadColor
                    opacity: root.topRoadDimOpacity
                }

                Rectangle {
                    width: Math.max(1, topRoad.wBright)
                    height: parent.height
                    x: shoulder.place(topRoad.dBar + topRoad.wDim + topRoad.wBevel, width)
                    color: root.topRoadColor
                }

                Rectangle {
                    width: Math.max(1, topRoad.wOuter)
                    height: parent.height
                    x: shoulder.place(topRoad.dOuter, width)
                    color: root.topRoadColor
                    opacity: root.topRoadOuterOpacity
                }
            }
        }
    }

    // Faded at both ends so the road runs out of the lens instead of stopping on
    // a line — the perspective road does the same thing by converging.
    Rectangle {
        id: topRoadMask
        x: topRoad.x
        y: topRoad.y
        width: topRoad.width
        height: topRoad.height
        visible: false
        layer.enabled: true
        // Smoothstep rather than a straight ramp. A linear fade leaves a visible
        // knee where it meets full strength — the line appears to start, which
        // is the thing the fade exists to avoid. The two intermediate stops per
        // end are smoothstep sampled at a quarter and three quarters (0.156 and
        // 0.844), which is close enough to the curve at this length.
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: "transparent"
            }
            GradientStop {
                position: root.topRoadFadeTop * 0.25
                color: Qt.rgba(1, 1, 1, 0.156)
            }
            GradientStop {
                position: root.topRoadFadeTop * 0.75
                color: Qt.rgba(1, 1, 1, 0.844)
            }
            GradientStop {
                position: root.topRoadFadeTop
                color: "white"
            }
            GradientStop {
                position: 1.0 - root.topRoadFadeBottom
                color: "white"
            }
            GradientStop {
                position: 1.0 - root.topRoadFadeBottom * 0.75
                color: Qt.rgba(1, 1, 1, 0.844)
            }
            GradientStop {
                position: 1.0 - root.topRoadFadeBottom * 0.25
                color: Qt.rgba(1, 1, 1, 0.156)
            }
            GradientStop {
                position: 1.0
                color: "transparent"
            }
        }
    }

    // No bloom pass here, deliberately. One was tried, on the reasoning that the
    // artwork's lines are lit rather than drawn and flat rectangles beside them
    // read as tape. It did the opposite: a blur wide enough to see spreads a 4px
    // line over about 16px, so the bars looked both wider and softer than the
    // ones they were meant to match. The artwork's own falloff is only a pixel
    // or two — far below what a blur can usefully produce at this size.
    MultiEffect {
        x: topRoad.x
        y: topRoad.y
        width: topRoad.width
        height: topRoad.height
        source: topRoad
        maskEnabled: true
        maskSource: topRoadMask
        // Same pair as the ring's mask: puts the ramp's edges at 0 and 1 so the
        // gradient's alpha passes through proportionally instead of clipping to
        // a hard edge. See the note on the ring for why min cannot stay at 0.
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
        opacity: root.faultMode ? root.topRoadOpacity : 0.0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation {
                duration: 260
            }
        }
    }

    // Half-width of the feathered boundary, as a fraction of artwork height.
    readonly property real glowSoftness: 0.01

    // Extra punch on the lit part of the ring, 0 = artwork as-is.
    //
    // Saturation is raised alongside brightness on purpose: brightness alone
    // lifts every channel toward white, so past about 0.2 the neon goes milky
    // grey and stops reading as blue.
    readonly property real glowBrightness: 0.01
    readonly property real glowSaturation: 0.02

    // The lit boundary is a fraction of the artwork rect — the mask is sized to
    // that rect, so the two share coordinates.

    // ==== TUNE THE GLOW HERE =================================================
    // Where the light stops for each value in scaleValues, as a fraction of the
    // artwork height. Smaller = higher up the ring.
    //
    // Kept separate from scaleY (the label positions) on purpose, so the light
    // can be nudged without dragging the numbers with it. Start them equal and
    // adjust from there.
    //
    // These land the light on the numbers: entries 40..240 are scaleY + 0.008.
    //
    // An entry is NOT where the light appears to stop. The boundary is feathered
    // by glowSoftness, so the visible top sits above the commanded position and
    // the entries compensate. THE COMPENSATION IS TIED TO glowSoftness: it was
    // 0.038 at a softness of 0.055 and is 0.008 at 0.01. Change the softness
    // without refitting these and the light stops short, so it needs extra
    // speed to reach each number — e.g. 171 km/h to arrive at the 160 mark.
    //
    // To refit: set the entries equal to scaleY, render at each speed with
    // CLUSTER_SPEED, difference against the standstill frame to find the
    // topmost row that gained light, and add back the offset you measure.
    //
    // If you move a number in scaleY, move its glowY entry by the same amount.
    //
    // The first entry is the standstill position: below the ring, nothing lit.
    //
    //                               0     40     80    120    160    200    240
    readonly property var glowY: [0.838, 0.78, 0.75, 0.608, 0.472, 0.33, 0.28]
    // =========================================================================

    // Speed -> lit boundary, interpolated through glowY.
    //
    // A single linear ramp cannot match the scale: the labels are not evenly
    // spaced down the ring (0->40 covers 0.015 of the artwork, 80->120 covers
    // 0.11), so a linear fill runs well ahead of the numbers by mid-scale.
    function glowEdgeFor(v) {
        var vals = root.scaleValues;
        var ys = root.glowY;
        var n = vals.length;

        // Starts at i = 0 so glowY[0] is the standstill position. It used to
        // start at 1 with a separate constant for a standstill, which left
        // glowY[0] silently unused — editing it did nothing.
        if (v <= vals[0])
            return ys[0];

        for (var i = 0; i < n - 1; ++i) {
            if (v <= vals[i + 1])
                return ys[i] + (ys[i + 1] - ys[i]) * (v - vals[i]) / (vals[i + 1] - vals[i]);
        }

        // Past the top label, carry on at the last segment's rate.
        var slope = (ys[n - 1] - ys[n - 2]) / (vals[n - 1] - vals[n - 2]);
        return ys[n - 1] + slope * (v - vals[n - 1]);
    }

    readonly property real glowEdge: glowEdgeFor(live.speed)

    // The neon ring, revealed bottom-up rather than faded: the lit part is at
    // full artwork brightness and the boundary climbs with speed.
    //
    // Masked rather than clipped, so the boundary feathers instead of being a
    // hard horizontal cut. Both source and mask are hidden layer items feeding
    // the MultiEffect; only its output is drawn.
    Image {
        id: glowImage
        x: root.artX
        y: root.artY
        width: root.artW
        height: root.artH
        source: "qrc:/images/images/cluster_bezel_glow.png"
        fillMode: Image.Stretch
        smooth: true
        mipmap: true
        visible: false
        layer.enabled: true
    }

    Rectangle {
        id: glowMask
        x: root.artX
        y: root.artY
        width: root.artW
        height: root.artH
        visible: false
        layer.enabled: true
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: "transparent"
            }
            GradientStop {
                position: Math.max(0, root.glowEdge - root.glowSoftness)
                color: "transparent"
            }
            GradientStop {
                position: Math.min(1, root.glowEdge + root.glowSoftness)
                color: "white"
            }
            GradientStop {
                position: 1.0
                color: "white"
            }
        }
    }

    MultiEffect {
        x: root.artX
        y: root.artY
        width: root.artW
        height: root.artH
        source: glowImage
        colorization: 1.0
        colorizationColor: root.ringColor
        maskEnabled: true
        maskSource: glowMask
        // MultiEffect ramps the mask as
        //   smoothstep(min*(1+s) - (s+0.001), min*(1+s), alpha),  s = spread
        // so these two values put the ramp's edges at 0.0 and 1.0 and let the
        // gradient's alpha through proportionally. Leaving min at 0 puts the
        // lower edge at -1.001, which makes the term 1 everywhere and ignores
        // the mask completely.
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0

        // The ring's bottom is dimmer in the artwork than its sides, so at low
        // speed only the weak part is lit and the whole thing reads faint.
        // Lifts the lit region without touching the always-on baseline.
        brightness: root.glowBrightness
        saturation: root.glowSaturation
    }

    // --- Scale along the ring ------------------------------------------------
    // Fixed positions rather than anything derived at run time. The ring cannot
    // be traced geometrically: ray-marching out from the lens centre misses it
    // entirely along the bottom (the lens dips lower in the middle than at the
    // sides), and it is not a closed barrier either — a flood fill from the
    // centre escapes through gaps in the glow. So these are the points marked
    // on the artwork, mirrored left-to-right for symmetry.
    //
    // They run bottom-centre round to top, the same direction the light fills,
    // so the lit edge sweeps past them in order.
    // Generated from the backend's full scale rather than written out, so the
    // marks and the value the reading saturates at cannot drift apart. Seven
    // evenly spaced steps, because glowY and scaleY are fitted to seven
    // positions — change the count and both tables need refitting.
    //
    // Was a hardcoded 0..240. The motor tops out at 800 rpm, which is 60 km/h
    // here, so the old dial used a quarter of its sweep and the top two thirds
    // were unreachable.
    readonly property var scaleValues: {
        const n = 7, out = [];
        for (let i = 0; i < n; ++i)
            out.push(Math.round(Vehicle.speedMax * i / (n - 1)));
        return out;
    }
    readonly property var scaleLX: [0.34, 0.265, 0.20, 0.17, 0.15, 0.17, 0.24]
    readonly property var scaleY: [0.75, 0.735, 0.71, 0.60, 0.4640, 0.365, 0.33]

    // Pushed inboard — left column to the right, right column to the left — so
    // the numbers clear the ring instead of sitting on the lit band.
    readonly property real scaleInsetX: 0.030

    Repeater {
        model: root.scaleValues.length
        delegate: Text {
            text: root.scaleValues[index]
            color: root.scaleColor
            font.pixelSize: root.artUnitH * 0.032
            font.family: root.uiFont
            x: root.artX + root.artW * (root.scaleLX[index] + root.scaleInsetX) - width / 2
            y: root.artY + root.artH * root.scaleY[index] - height / 2
        }
    }

    // The right side is the power scale, in watts, sharing the left side's
    // positions: the first mark sits where the speed dial's first mark does,
    // and so on. Since the ring is driven by speed, the light arriving at one
    // is the same instant it arrives at the other — which only reads correctly
    // while power tracks speed proportionally, as it does in the demo.
    //
    // Watts, not kilowatts. The motor is rated 450W, so a kW scale would have
    // read 0.4 across the whole range and never moved off its first mark.
    readonly property var scaleValuesRight: {
        const n = 7, out = [];
        for (let i = 0; i < n; ++i)
            out.push(Math.round(Vehicle.powerMax * i / (n - 1)));
        return out;
    }

    Repeater {
        model: root.scaleValuesRight.length
        delegate: Text {
            id: rightLabel
            text: root.scaleValuesRight[index]
            color: root.scaleColor
            font.pixelSize: root.artUnitH * 0.032
            font.family: root.uiFont

            // Width of the left-hand label at the same position. The right
            // numbers are one digit while the left run to three, so mirroring
            // their centres leaves the single digits sitting far inboard with a
            // visible gap to the ring. Mirroring the outer edge instead keeps
            // both columns the same distance from the light.
            TextMetrics {
                id: leftMetrics
                font: rightLabel.font
                text: root.scaleValues[index]
            }

            x: root.artX + root.artW * (1 - root.scaleLX[index] - root.scaleInsetX) + leftMetrics.width / 2 - width
            y: root.artY + root.artH * root.scaleY[index] - height / 2
        }
    }

    // --- State of charge and accumulated distance ----------------------------
    // Neither has a backend signal: VehicleBackend::battery() is hardcoded to 0
    // and there is no odometer at all. Bound so they light up correctly once
    // there is something behind them; the demo values just make the layout
    // legible without hardware.
    // Demo only, and now only for the health band: one 0..1 ramp, so every
    // fill level gets shown. Slower than the speed sweep on purpose — these
    // are 11-segment meters and a fast ramp just flickers between steps.
    property real demoLevel: 0

    SequentialAnimation on demoLevel {
        running: demoMode
        loops: Animation.Infinite
        NumberAnimation {
            to: 1
            duration: 14000
        }
        PauseAnimation {
            duration: 1200
        }
        NumberAnimation {
            to: 0
            duration: 14000
        }
        PauseAnimation {
            duration: 1200
        }
    }

    // Charge. There is no SOC sensor on the v4 wire and VehicleBackend::battery()
    // reads 0 forever, which left the left-hand band permanently empty on real
    // hardware. Rather than show a dead meter, this ticks a discharge cycle on
    // its own: starts full, counts down to empty, then starts over. It is a
    // placeholder for a real SOC signal, not a measurement -- replace with
    // Vehicle.battery the day one exists.
    property real batteryLevel: 100

    Timer {
        interval: 1200
        running: true
        repeat: true
        onTriggered: {
            root.batteryLevel -= 1;
            if (root.batteryLevel <= 0)
                root.batteryLevel = 100;
        }
    }

    // The odometer went with the KM TOTAL readout — it had no backend behind
    // it either way, VehicleBackend has no odometer at all.
    readonly property real soc: root.batteryLevel

    // What the right-hand band shows: 1 is full margin to every measured limit,
    // 0 is at one of them. VehicleBackend::health composes it; see the note
    // there for why it is a worst-of rather than a blend.
    //
    // The demo runs it inverted against demoLevel so the two bands are never at
    // the same fill and every combination shows up — full charge beside a
    // healthy motor, empty beside a failing one — which is what exercises the
    // colour ramp at both ends.
    //
    // This band used to show motor temperature. Nothing measures temperature on
    // the v4 wire, so on hardware it read 0 and never moved.

    // --- Remaining useful life -----------------------------------------------
    // The band now reads RUL -- the third of the three values the AI publishes,
    // pred_maint_result, which has been exposed as Vehicle.aiPredMaint since
    // AiReader was written and never displayed. Full band is a full service
    // interval ahead; empty is due now.
    //
    // Four months, as days. It is the denominator of the whole reading, so it is
    // a named constant rather than a literal in the expression -- change the
    // service interval here and the band follows. 30-day months: the figure is a
    // maintenance window, not a calendar date, and 121.75 would imply a precision
    // the model's output does not have.
    readonly property real rulFullDays: 120

    // pred_maint_result is FREE TEXT. Nothing in this project owns its
    // vocabulary -- same problem aiFaultClass has, and solved the same loose way
    // (see errorFault): take the first number in the string, then let a unit
    // word rescale it if one is there. A bare number is read as days, which is
    // the unit the field is expected to carry.
    //
    // Returns -1 for "no usable reading" rather than 0. Those are different: 0
    // is a real RUL that must alarm, and an empty or unparseable string must not
    // be shown as one. An AI that has said nothing yet is not a motor that is
    // out of life.
    readonly property real rulDays: {
        const s = Vehicle.aiPredMaint.trim().toLowerCase();
        if (s === "")
            return -1;

        // Anchor on the word "rul", do not just take the first number in the
        // string. The real publisher sends more than one figure:
        //
        //     "healthy, health 1.00, RUL 4.00 months (provisional)"
        //
        // and the first number there is the HEALTH score, not the life. Reading
        // it gave 1 x 30 = 30 days against a 120-day interval -- a quarter-full
        // band on a motor with a full service interval ahead of it, which is
        // both wrong and wrong in the dangerous direction. Confirmed against the
        // live /motor_ai_result region on the target.
        const i = s.indexOf("rul");
        const tail = i !== -1 ? s.substring(i + 3) : s;

        const m = tail.match(/-?\d+(\.\d+)?/);
        if (!m)
            return -1;
        let v = parseFloat(m[0]);
        if (!isFinite(v))
            return -1;
        if (v < 0)                       // past due reads as none left
            return 0;

        // Scale by the FIRST WORD after the number, not by any unit word found
        // anywhere -- for the same reason, and so "(provisional)" and any
        // trailing prose are ignored.
        //
        // Matched by prefix because the unit is not spelled consistently. The
        // live publishers send both of these:
        //
        //     "RUL: 12 h"                                    (fault_tester)
        //     "healthy, health 1.00, RUL 4.00 months (...)"  (the model)
        //
        // and a bare "h" is the common case on the urgent faults. Testing for
        // "hour"/"hr" missed it, so 12 hours of bearing life was read as 12
        // DAYS and drew a tenth-full band where it should have been at the
        // floor. Wrong, and wrong on exactly the fault that matters most.
        const after = tail.substring(m.index + m[0].length);
        const mu = after.match(/[a-z]+/);
        const u = mu ? mu[0] : "";       // "h", "hrs", "months", "days", ""
        if (u.indexOf("mo") === 0)
            v *= 30;
        else if (u.indexOf("mi") === 0)
            v /= 1440;
        else if (u.indexOf("w") === 0)
            v *= 7;
        else if (u.indexOf("h") === 0)
            v /= 24;
        return v;                        // "d", "day(s)" or no unit: already days
    }

    // -1 when there is no reading, which is what the fallback below tests.
    readonly property real rulFrac: root.rulDays < 0
                                    ? -1
                                    : Math.max(0, Math.min(1, root.rulDays / root.rulFullDays))

    // Falls back to VehicleBackend::health whenever the AI has published no
    // usable RUL -- which is most of the time on the bench, and always before
    // the first verdict lands. Showing a full band there would claim a service
    // interval nobody measured; showing an empty one would alarm on silence.
    // The measured margin is the honest thing to show in the gap.
    //
    // Note what this costs while RUL IS present: the vibration and current
    // margins stop being displayed here, and so does the AI_ALERT_HEALTH clamp.
    // If you would rather the band always show the worse of the two, make this
    // Math.min(root.rulFrac, Vehicle.health) -- one line, and the ramp, the
    // lightAtZero floor and the at-limit pulse all keep working unchanged.
    readonly property real healthFrac: demoMode
                                       ? 1 - root.demoLevel
                                       : root.rulFrac >= 0 ? root.rulFrac
                                                           : Vehicle.health

    // The SOC and KM TOTAL readouts that used to sit at bottomRowY are gone; the
    // status bands carry charge now, and there was never an odometer to read.
    // bottomRowY is still what bounds those bands from below, so it stays.

    // --- Status bars ---------------------------------------------------------
    // Two thin meters lying along the ring's lower shoulders, charge on the left
    // over the SOC readout, motor temperature on the right.
    //
    // They follow the ring rather than lying beside it. A straight bar cannot:
    // tracing the lowest lit pixel of cluster_ring_base.png across the shoulder,
    // the tangent swings from 60 degrees at the outer end to 0 at the inner one,
    // so any single rotation is wrong at both ends. An earlier version tilted a
    // rectangle by 13 degrees, the average, and sat off the artwork at the tips.
    //
    // So the path below IS that trace: the ring's own edge, smoothed, pushed 20px
    // out along its normal to clear the artwork, and resampled at equal arc
    // length. 201px of arc in 24 equal segments. Equal spacing is what lets the
    // fill treat segment i as covering i/24 to (i+1)/24 of the reading without
    // carrying a cumulative length table.
    //
    // Only the left path is stored; the right is mirrored, matching how the ring
    // scale is handled. Regenerate both if the bezel art ever changes shape.
    readonly property var statusPathX: [0.1421, 0.1451, 0.1484, 0.1523, 0.1568, 0.1616, 0.1663, 0.1713, 0.1768, 0.1823, 0.1881, 0.1942, 0.2007, 0.2073, 0.2139, 0.2208, 0.2276, 0.2345, 0.2413, 0.2482, 0.2551, 0.2620, 0.2690, 0.2759, 0.2828]
    readonly property var statusPathY: [0.7232, 0.7358, 0.7480, 0.7595, 0.7701, 0.7803, 0.7906, 0.8001, 0.8087, 0.8170, 0.8247, 0.8315, 0.8361, 0.8403, 0.8439, 0.8464, 0.8487, 0.8503, 0.8521, 0.8532, 0.8547, 0.8564, 0.8566, 0.8566, 0.8566]

    // Bars cut by parallel lines at a fixed 75 degrees, then masked to a curved
    // band. Two separate ideas, and both are needed:
    //
    //   - every cut is parallel to every other, so each bar is the strip between
    //     two parallel lines: a plain rectangle, rotated. Cuts that rotated with
    //     the curve were tried and read as bricks in an arc rather than as a
    //     scale.
    //   - the inner and outer edges come from the mask, not the geometry, so
    //     they stay smooth curves however the bars are cut.
    //
    // The mask is images/status_bands.png, generated by tools/make_status_bands.py
    // from the ring's own shoulder — so the band is parallel to the gauge by
    // construction. Re-run that script if the bezel artwork changes shape, and
    // keep statusBandX0 in step with X0 there.
    //
    // The bars are deliberately longer than the band is thick and let the mask
    // trim them; sizing them to fit exactly would need the band's thickness
    // expressed twice, here and in the script, with nothing keeping the two in
    // step.
    readonly property real statusBandX0: 0.150     // band extent, fraction of artW
    readonly property real statusBandX1: 0.283     // must match X0/X1 in the script
    readonly property int statusSegments: 11
    readonly property real statusSegDepth: 0.200   // fraction of artUnitH, before masking

    // Rotation of each bar. The cuts sit at 75 degrees, so the strip between two
    // of them runs at 75 - 90; the right-hand band mirrors to +15.
    readonly property real statusSegAngle: -15

    // Bar centres and widths, generated alongside the mask. They are a table
    // rather than a formula because the width that matters is the one you see —
    // the on-screen distance between two cuts — and the map from that back to a
    // position along the curve has no closed form.
    //
    // Widest bar 16.1px at the outer end down to 5.4px at the inner, a ratio of
    // 3. Spacing them evenly along the band instead looks wrong: where the
    // shoulder is steep it runs nearly parallel to the cut, so an evenly spaced
    // bar still comes out a thin sliver there.
    //
    // Left-hand values; the right band mirrors about the artwork centre.
    readonly property var statusSegCX: [0.15431, 0.17643, 0.19471, 0.21008, 0.22362, 0.23580, 0.24675, 0.25658, 0.26519, 0.27248, 0.27864]
    readonly property var statusSegCY: [0.76449, 0.80861, 0.83227, 0.84249, 0.84737, 0.85050, 0.85279, 0.85504, 0.85660, 0.85660, 0.85660]
    readonly property var statusSegW: [0.01332, 0.01243, 0.01154, 0.01065, 0.00976, 0.00888, 0.00799, 0.00710, 0.00621, 0.00533, 0.00444]
    // 0.040, not the 0.028 it started at. At 15px the symbols read as specks
    // and the bands looked unlabelled.
    readonly property real statusIconSize: 0.040   // fraction of artUnitH
    readonly property real statusIconGap: 0.026    // fraction of artW, past the inner tip

    // The unlit bars, and what ties the pair to the gauge. Not a grey: a dim
    // ringColor, so the whole band sits in the same family as the neon beside it
    // and the bars that are off read as unlit gauge rather than as shadow.
    //
    // Two earlier attempts at integrating them were worse and are worth not
    // repeating. A bloom under the bars smeared them. Grading the mask dark at
    // its edges — copying the ring's own cross-section — put what looked like a
    // drop shadow around the band, because these bars are short and the dark
    // edge lands right against their cuts. Colour does the job; light does not.
    readonly property color statusIdleColor: root.ringColor
    readonly property real statusIdleOpacity: 0.20
    readonly property int statusFadeMs: 260

    // --- Health colour ramp ---------------------------------------------------
    // Full health is `accent` — the same cyan the charge band opposite is drawn
    // in, so a healthy pair reads as one instrument rather than two. It cools
    // toward that cyan through the top half, turns red below the midpoint, and
    // goes dark as it approaches nothing.
    //
    // Four stops rather than a single blend, because the interesting behaviour
    // is not linear: nothing should happen in the top half beyond the colour
    // coming up to full, and everything happens in the bottom half.
    //
    // Interpolated in RGB, not HSV. Hue interpolation from cyan to red takes
    // the long way round through green and yellow, which would run a rainbow up
    // the band on the way to a warning.
    //
    // The dark end is the point of the last stop: at zero margin the band goes
    // deep red rather than bright red. A bright bar reads as a bar that is
    // *on*; the reading here is that almost nothing is left.
    //
    // Raised from rgba(0.30, 0.02, 0.02) -- RGB(77,5,5), which was so close to
    // the bezel behind it that the one segment left at zero could not be seen
    // at all. The intent was right and the value was past it: this still reads
    // as depleted, and survives a panel in daylight.
    readonly property var healthStops: [
        { t: 0.00, c: Qt.rgba(0.55, 0.06, 0.06, 1) },
        { t: 0.30, c: Qt.rgba(0.92, 0.13, 0.13, 1) },
        { t: 0.50, c: Qt.darker(root.accent, 1.45) },
        { t: 1.00, c: root.accent }
    ]

    // Smoothstep inside each segment, so the rate of change goes to zero at
    // every stop and the ramp has no visible corners where two segments meet.
    function healthColorAt(v) {
        const stops = root.healthStops;
        const t = Math.max(0, Math.min(1, v));
        for (let i = 0; i < stops.length - 1; ++i) {
            const a = stops[i], b = stops[i + 1];
            if (t > b.t && i + 2 < stops.length)
                continue;
            const u = b.t > a.t ? (t - a.t) / (b.t - a.t) : 0;
            const f = Math.max(0, Math.min(1, u));
            const s = f * f * (3 - 2 * f);
            return Qt.rgba(a.c.r + (b.c.r - a.c.r) * s,
                           a.c.g + (b.c.g - a.c.g) * s,
                           a.c.b + (b.c.b - a.c.b) * s, 1);
        }
        return stops[stops.length - 1].c;
    }

    // One mask for both bands — the PNG carries the left and right shapes, and
    // each bar's stripes only exist on its own side, so masking with the shared
    // image yields that side alone. Flat alpha with a 2px feather; see
    // tools/make_status_bands.py.
    Image {
        id: statusBandMask
        x: root.artX
        y: root.artY
        width: root.artW
        height: root.artH
        source: "qrc:/images/images/status_bands.png"
        fillMode: Image.Stretch
        smooth: true
        mipmap: true
        visible: false
        layer.enabled: true
    }

    // Both fill from the inner end outward: index statusSegments - 1 is the
    // narrow bar nearest the centre and lights first, the wide outer one last.
    // So the bars get wider as the reading climbs and the meter says "more"
    // twice — more of them, and each one bigger.
    component StatusBar: Item {
        id: sb
        property real fraction: 0            // 0..1, clamped below
        property color fillColor: root.accent
        property real side: -1               // -1 left, +1 right
        property alias icon: iconImage.source

        // Opt-in, because the two bands mean different things by empty. The
        // charge band means empty: a lone segment left lit at 0% SOC would read
        // as "some charge left", which is worse than blank. Health means the
        // opposite -- zero is a READING, the motor sitting on one of its
        // limits, and Math.round takes it to no segments at all below 1/22
        // (0.0455). Everything under that drew an entirely idle band, which is
        // what an unpowered band looks like, at exactly the reading that most
        // needs to be seen. evaluateHealth() takes health to zero on the same
        // thresholds that raise criticalAlert, so it is reachable.
        property bool lightAtZero: false

        // What the reading alone asks for, before lightAtZero has a say.
        readonly property int rawLit: Math.round(Math.max(0, Math.min(1, sb.fraction))
                                                 * root.statusSegments)
        readonly property int litCount: (sb.lightAtZero && sb.rawLit < 1) ? 1 : sb.rawLit

        // True only for the segment lightAtZero rescued -- the band is pegged at
        // the bottom of its scale, not merely low. Steady red says "nearly
        // nothing left"; this says "at the limit now", which is a different
        // reading and deserves to be told apart at a glance.
        readonly property bool atLimit: sb.lightAtZero && sb.rawLit < 1

        // A multiplier rather than an animation on the segment's own opacity.
        // `opacity` there is already a binding (lit vs idle) with a Behavior on
        // it, and `SequentialAnimation on opacity` would seize the property and
        // break both -- the same trap the notes on `live` and on the colour
        // Behavior below warn about. Pulsing a separate number and multiplying
        // it in leaves the binding intact.
        property real limitPulse: 1.0

        SequentialAnimation on limitPulse {
            running: sb.atLimit
            loops: Animation.Infinite
            // Shares the fault code's rate, so everything urgent on the panel
            // is beating together rather than at rates that fight. In practice
            // this is always the critical 260ms: health only reaches zero when
            // vibration or current is past the threshold that raises
            // criticalAlert, which puts faultLevel at 2.
            NumberAnimation {
                to: 0.25
                duration: root.severityBlinkMs
            }
            NumberAnimation {
                to: 1.0
                duration: root.severityBlinkMs
            }
            onStopped: sb.limitPulse = 1.0
        }

        anchors.fill: parent

        // Position at arc fraction `s` along the band's centreline, used only to
        // place the icon now that the bars come from their own table. The stored
        // points are equally spaced in arc length, so s maps straight onto the
        // index. The right bar is the left one reflected about centre.
        function ptX(s) {
            const n = root.statusPathX.length - 1;
            const u = Math.max(0, Math.min(1, s)) * n;
            const i = Math.min(n - 1, Math.floor(u));
            const v = root.statusPathX[i] + (root.statusPathX[i + 1] - root.statusPathX[i]) * (u - i);
            return root.artX + root.artW * (sb.side < 0 ? v : 1 - v);
        }
        function ptY(s) {
            const n = root.statusPathY.length - 1;
            const u = Math.max(0, Math.min(1, s)) * n;
            const i = Math.min(n - 1, Math.floor(u));
            const v = root.statusPathY[i] + (root.statusPathY[i + 1] - root.statusPathY[i]) * (u - i);
            return root.artY + root.artH * v;
        }
        // Every cut is parallel, so a bar is just the strip between two of them:
        // a rectangle rotated to statusSegAngle, as wide as the gap between its
        // cuts and long enough that the mask decides where it ends.
        Item {
            id: stripes
            x: root.artX
            y: root.artY
            width: root.artW
            height: root.artH
            visible: false
            layer.enabled: true

            Repeater {
                model: root.statusSegments

                delegate: Rectangle {
                    id: seg
                    required property int index

                    // Lights from the inner end, so index 0 — the wide outer bar
                    // — is the last to come on.
                    readonly property bool lit: index >= root.statusSegments - sb.litCount

                    width: root.artW * root.statusSegW[index]
                    height: root.artUnitH * root.statusSegDepth
                    x: root.artW * (sb.side < 0 ? root.statusSegCX[index]
                                                : 1 - root.statusSegCX[index]) - width / 2
                    y: root.artH * root.statusSegCY[index] - height / 2
                    rotation: sb.side < 0 ? root.statusSegAngle : -root.statusSegAngle
                    color: seg.lit ? sb.fillColor : root.statusIdleColor
                    opacity: (seg.lit ? 1.0 : root.statusIdleOpacity)
                             * (seg.lit && sb.atLimit ? sb.limitPulse : 1.0)

                    // Opacity only. `lit` is a discrete flip, so a Behavior
                    // fades a segment in cleanly.
                    //
                    // There is deliberately NO Behavior on color. The health
                    // ramp moves continuously with the reading, and a Behavior
                    // restarts on every write -- so it never finishes, and the
                    // colour stays pinned near wherever it began. It was doing
                    // exactly that here: the band held its start-up cyan all
                    // the way down to 14% health while the binding underneath
                    // was correctly reporting dark red. Same trap the note on
                    // `live` warns about. The ramp is already smooth because
                    // its input is.
                    //
                    // Off while this segment is pulsing: the Behavior would
                    // intercept every step of the pulse and damp a 260ms beat
                    // through a 260ms fade, smearing it into a dim flicker.
                    Behavior on opacity {
                        enabled: !(seg.lit && sb.atLimit)
                        NumberAnimation {
                            duration: root.statusFadeMs
                        }
                    }
                }
            }
        }

        MultiEffect {
            x: root.artX
            y: root.artY
            width: root.artW
            height: root.artH
            source: stripes
            maskEnabled: true
            maskSource: statusBandMask
            // Same pair as the ring's mask — see the note there for why min
            // cannot be left at 0.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        // At the inner tip, carried on past it along the path's closing tangent
        // so it stands off at the angle the band arrives at rather than square
        // to it. The outer tip is the wrong end: it is the high, steep one, and
        // an icon there sits out over the empty corner.
        readonly property real tipLen: Math.max(0.0001,
                                                Math.hypot(sb.ptX(1) - sb.ptX(0.98),
                                                           sb.ptY(1) - sb.ptY(0.98)))
        readonly property real tipDX: (sb.ptX(1) - sb.ptX(0.98)) / sb.tipLen
        readonly property real tipDY: (sb.ptY(1) - sb.ptY(0.98)) / sb.tipLen

        Image {
            id: iconImage
            width: root.artUnitH * root.statusIconSize
            height: width
            x: sb.ptX(1) + sb.tipDX * root.artW * root.statusIconGap - width / 2
            y: sb.ptY(1) + sb.tipDY * root.artW * root.statusIconGap - height / 2
            sourceSize.width: width * 2
            sourceSize.height: height * 2
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            visible: false
        }

        // Colorized like the telltales so the icons follow `accent` rather than
        // staying at the #00d4ff baked into the SVGs.
        MultiEffect {
            source: iconImage
            x: iconImage.x
            y: iconImage.y
            width: iconImage.width
            height: iconImage.height
            colorization: 1.0
            colorizationColor: root.accent
        }
    }

    StatusBar {
        side: -1
        fraction: root.soc / 100
        fillColor: root.accent
        icon: "qrc:/images/images/icon_battery.svg"
    }

    // Remaining useful life, from the AI's pred_maint_result over a four-month
    // service interval -- see rulDays and rulFullDays. Falls back to
    // VehicleBackend::health, the margin to whichever measured limit is closest,
    // whenever no RUL has been published. Full is good either way, so the ramp
    // and the icon carry over unchanged.
    StatusBar {
        side: 1
        fraction: root.healthFrac
        // See lightAtZero: zero margin has to stay visible, unlike zero charge.
        lightAtZero: true
        icon: "qrc:/images/images/icon_health.svg"
        // Carries the reading twice — length and colour — so a glance catches
        // it without counting bars. See healthColorAt for the ramp.
        fillColor: root.healthColorAt(root.healthFrac)
    }

    // --- Fault mode ----------------------------------------------------------
    // Two states for the centre of the lens. Normally the car is seen from
    // behind, sitting on the road graphic; when something is wrong the road
    // drops away and the view swaps to the car from above, so the eye goes to
    // the vehicle rather than to the driving scene.
    //
    // Driven entirely off the AI verdict in shared memory -- no simulated or
    // demo-injected faults. aiAlert is true whenever the anomaly field is not
    // "normal" (the anomaly/normal-classifier disagreement is folded back into
    // the normal case server-side, in motor_ai_server, so it never reaches
    // here).
    readonly property bool faultMode: Vehicle.aiAlert

    // --- Car ------------------------------------------------------------------
    // Centred between the two readouts, over the road graphic in the artwork.
    //
    // The source is cropped to its own alpha bounds, so its height maps
    // directly to what you see — no transparent margin to compensate for. Width
    // follows from the trimmed aspect (702x510) rather than being set
    // separately, so it cannot end up stretched.
    //
    // Sized so the car's width matches the road graphic's lane at the height it
    // sits at — its rear wheels land on the converging lines. Bigger and it
    // spills over them; smaller and it floats, too narrow for the lane, which
    // breaks the perspective either way.
    // In fault mode the road is gone, so the overhead car is not bound by the
    // lane and grows into the emptied lens. It reaches above the old lane band
    // but is held down at the bottom: past about 0.86 of artH the tail crosses
    // the arc the gear indicator hangs off. Raising the centre is what buys the
    // extra size — the two have to move together.
    // 0.21, not the 0.18 that fitted car_rear.png. car_rear_chase.png is a
    // tighter crop at a different aspect (1.333 against 1.560), so the same
    // height renders it 15% narrower and it stops filling the lane. 0.21 puts it
    // back at 147.7px wide, which is where the old car sat. Height is the knob
    // because width is derived from it.
    readonly property real carHeight: 0.21      // fraction of artUnitH
    readonly property real carY: 0.576          // fraction of artH, top of the rear view
    readonly property real faultCarHeight: 0.38 // fraction of artUnitH
    readonly property real faultCarCentreY: 0.58

    Image {
        id: carRear
        source: "qrc:/images/images/car_rear_chase.png"
        height: root.artUnitH * root.carHeight
        width: height * (1220 / 915)            // the source's trimmed aspect
        x: root.artX + root.artW * 0.5 - width / 2
        y: root.artY + root.artH * root.carY + root.carShake
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        layer.enabled: root.carTintStrength > 0
        layer.effect: MultiEffect {
            colorization: root.carTintStrength
            colorizationColor: root.carTint
        }
        opacity: root.faultMode ? 0.0 : 1.0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation {
                duration: 260
            }
        }
    }

    Image {
        id: carTop
        source: "qrc:/images/images/car_top.png"
        height: root.artUnitH * root.faultCarHeight
        width: height * (400 / 959)             // the source's trimmed aspect
        x: root.artX + root.artW * 0.5 - width / 2
        y: root.artY + root.artH * root.faultCarCentreY - height / 2
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        layer.enabled: root.carTintStrength > 0
        layer.effect: MultiEffect {
            colorization: root.carTintStrength
            colorizationColor: root.carTint
        }
        opacity: root.faultMode ? 1.0 : 0.0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation {
                duration: 260
            }
        }
    }

    // --- Motor fault lamp and banner -----------------------------------------
    // A red glow over the drive unit, with the kind of fault named above the
    // car. Both key off errorKind rather than off faultMode.
    //
    // Kind and code come from the fault_class field of the AI verdict in
    // shared memory -- the second of the three values motor_ai_client
    // publishes, alongside anomaly and predicted maintenance. motor_ai_server
    // guarantees fault_class is only ever "electrical" or "mechanical" when
    // anomaly is not "normal" (it folds an anomaly+normal-classification
    // disagreement back into the normal case before either reaches here), so
    // there is no case left where aiAlert is true and this falls through
    // empty.
    // Codes follow the same family scheme the sensor-driven codes used before
    // the AI verdict replaced them: 2x electrical, 3x mechanical.
    // Substring, not exact match. aiFaultClass is free text with no defined
    // vocabulary -- see AiReader.cpp -- and /motor_fault_override (fault_tester's
    // injection path) does not use the same words the real classifier does: it
    // sends "electrical_fault", not "electrical". An exact match left aiAlert
    // true (the car still swaps to the overhead view) with no icon or code to
    // go with it, which is worse than matching loosely.
    readonly property var errorFault: {
        // Keyed off aiAlert, not off the class string, and that is the whole
        // point: whenever a fault is RAISED this returns something to draw.
        //
        // It used to match only "mechanical" and "electrical" and return empty
        // otherwise. The live vocabulary is not those two words -- an urgent
        // mechanical fault publishes "bearing_fault critical", which matched
        // neither. aiAlert still went true (the class is not benign), so fault
        // mode engaged: the road dropped away and the overhead car appeared,
        // while the code, the kind label and the motor lamp -- all gated on
        // errorKind -- stayed hidden. A fault view with nothing on it, which is
        // worse than no fault view at all. Seen on the target.
        if (!Vehicle.aiAlert)
            return { kind: "", code: "" };

        const cls = Vehicle.aiFaultClass.trim().toLowerCase();
        if (cls.indexOf("electrical") !== -1)
            return { kind: "ELECTRICAL", code: "E-21" };

        // The mechanical family by the words it actually uses. Extend this list
        // rather than the generic case when a new one turns up -- landing here
        // is what gets the right icon and the 3x code.
        const mech = ["mechanical", "bearing", "rotor", "shaft", "gear",
                      "imbalance", "misalign", "vibration"];
        for (let i = 0; i < mech.length; ++i)
            if (cls.indexOf(mech[i]) !== -1)
                return { kind: "MECHANICAL", code: "E-31" };

        // Anything else raised but unrecognised. Deliberately vague rather than
        // guessed: E-01 sits outside the 2x/3x families precisely because it
        // claims no family. There is no icon for it, so the lamp lights plain --
        // the code and the word still say a fault is up and roughly nothing
        // more, which is the honest reading of a string we do not know.
        return { kind: "FAULT", code: "E-01" };
    }
    readonly property string errorKind: root.errorFault.kind
    readonly property string errorCode: root.errorFault.code
    // Two symbols, drawn for this lamp at the size it actually renders --
    // see tools/make_fault_icons.py.
    readonly property string errorIcon: root.errorKind === "MECHANICAL" ? "fault_mechanical"
                                     : root.errorKind === "ELECTRICAL" ? "fault_electrical" : ""

    // Fraction of the overhead car's own height, nose at 0. Measured off
    // car_top.png rather than guessed: the dark rear deck panel runs from
    // 0.743 to 0.898 down the car and is 0.154 tall, so the symbol is centred
    // on it and kept small enough to sit inside it.
    readonly property real motorY: 0.821
    readonly property real motorIconSize: 0.30  // fraction of the car's width

    // Blurred rather than drawn with a gradient: MultiEffect is already how the
    // ring and the telltales are lit, and QtQuick has no radial gradient
    // without pulling in QtQuick.Shapes.
    //
    // The disc is a fraction of its own container rather than the whole of it,
    // and the container is what gets blurred. MultiEffect crops to its bounds
    // and scales the source to them, so a disc filling its layer has its
    // falloff cut off square — the padding has to be inside the source.
    Item {
        id: motorLamp
        width: carTop.width * 1.1
        height: width
        x: carTop.x + carTop.width / 2 - width / 2
        y: carTop.y + carTop.height * root.motorY - height / 2
        visible: false
        layer.enabled: true

        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.52
            height: width
            radius: width / 2
            color: root.severityColor
        }
    }

    MultiEffect {
        id: motorGlow
        source: motorLamp
        x: motorLamp.x
        y: motorLamp.y
        width: motorLamp.width
        height: motorLamp.height
        blurEnabled: true
        // Enough to lose the disc's edge, not so much that the core washes
        // out: the blur spreads a fixed amount of light over a bigger area, so
        // past about 0.7 here the middle stops reading as lit at all.
        blur: 0.65
        blurMax: 40
        visible: root.errorKind !== "" && carTop.visible
        opacity: 0.75

        // Slower than the telltale flicker on purpose: this is a wash of light
        // over the car, and at the lamps' 260ms it strobes.
        SequentialAnimation on opacity {
            running: root.errorKind !== ""
            loops: Animation.Infinite
            NumberAnimation {
                to: 0.35
                duration: root.severityLampMs
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                to: 0.75
                duration: root.severityLampMs
                easing.type: Easing.InOutSine
            }
            onStopped: motorGlow.opacity = 0.75
        }
    }

    // The faulting part's own symbol, sitting in the lamp. Reuses the telltale
    // artwork, so the icon in the glow and the lamp that lit in the corner are
    // the same drawing and read as the same fault.
    //
    // Steady while the glow breathes underneath it: pulsing both makes the
    // symbol hard to identify at exactly the moment it matters.
    Image {
        source: root.errorIcon === "" ? "" : "qrc:/images/images/" + root.errorIcon + ".svg"
        // SVG: rasterise at twice the drawn size so the edges stay clean
        // if the icon is ever grown.
        sourceSize.width: width * 2
        sourceSize.height: height * 2
        width: carTop.width * root.motorIconSize
        height: width
        x: carTop.x + carTop.width / 2 - width / 2
        y: carTop.y + carTop.height * root.motorY - height / 2
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        visible: root.errorIcon !== "" && carTop.visible
    }

    // --- Error code ----------------------------------------------------------
    // The code for whatever the lamp is showing, in the empty band between the
    // clock and the car's nose. Centred, so it sits in the road's empty middle
    // lane and never lands on a line.
    //
    // Blinks at 260ms rather than the motor glow's 700ms -- a faster rate for
    // the thing naming the fault than for the wash of light under the car.
    // Both animations start when errorCode/errorKind change together, so they
    // stay in phase without being driven from one clock.
    //
    // Note this is the one place the flicker is applied to *text*. If it turns
    // out to be hard to read on the panel, raise the 0.2 floor rather than the
    // duration.
    readonly property real errorCodeY: 0.365    // fraction of artH, row centre

    Text {
        id: errorCodeText
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.artY + root.artH * root.errorCodeY - height / 2
        text: root.errorCode
        color: root.severityColor
        font.pixelSize: root.artUnitH * 0.034
        font.family: root.uiFont
        font.weight: Font.Light
        font.letterSpacing: 3
        visible: root.errorCode !== ""

        SequentialAnimation on opacity {
            running: errorCodeText.visible
            loops: Animation.Infinite
            NumberAnimation {
                to: 0.2
                duration: root.severityBlinkMs
            }
            NumberAnimation {
                to: 1.0
                duration: root.severityBlinkMs
            }
            onStopped: errorCodeText.opacity = 1.0
        }
    }

    // --- Fault kind ----------------------------------------------------------
    // The word under the code. errorKind has been computed since fault mode was
    // written and never drawn -- the note above it says "the kind of fault named
    // above the car", so the label was intended and got lost.
    //
    // It says the KIND, not the severity, and the two are split across different
    // channels on purpose. Severity is already carried twice, as colour and as
    // blink rate; spelling "CRITICAL" here would be the same bit a third time,
    // spending the only line of space there is to repeat something. The kind is
    // the thing neither colour nor rhythm can express -- and it is what makes
    // E-31 mean anything without knowing that 2x is electrical and 3x is
    // mechanical.
    //
    // Coloured by severity, so one row carries both: subsystem as text, severity
    // as hue. That also covers the case the hue alone does not -- amber against
    // red is the exact pair the commonest colour blindness collapses, and the
    // word survives it.
    //
    // STEADY while the code blinks above it, for the same reason the motor
    // symbol is steady while its lamp breathes: pulsing both makes the thing
    // hard to read at exactly the moment it matters.
    //
    // Sized off artUnitH like the code, so it does not widen with the stretch.
    // 0.026 against the code's 0.034 keeps it clearly subordinate; the wider
    // letterSpacing matches how SPEED and POWER are set under their numerals.
    readonly property real errorKindGap: 0.012   // fraction of artUnitH, below the code

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: errorCodeText.y + errorCodeText.height + root.artUnitH * root.errorKindGap
        text: root.errorKind
        color: root.severityColor
        opacity: 0.9
        font.pixelSize: root.artUnitH * 0.026
        font.family: root.uiFont
        font.weight: Font.Light
        font.letterSpacing: 4
        visible: root.errorKind !== ""
    }

    // --- Gear indicator ------------------------------------------------------
    // Derived from speed, because VehicleBackend still exposes no gear signal:
    // moving means D, stopped means P. Replace this binding the moment there is
    // a real selector to read — a cluster that infers the gear will disagree
    // with the lever the first time the car rolls in neutral.
    //
    // 0.5 km/h rather than 0, so a jittery reading at a standstill cannot flap
    // the indicator between P and D.
    //
    // R and N are drawn but cannot light: nothing here carries direction, and
    // speed alone cannot tell reverse from forward or neutral from park. They
    // are in the row because the row is a fixed legend — the selected gear is
    // picked out of it, not printed on its own.
    readonly property string gear: live.speed > 0.5 ? "D" : "P"

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        // Below the artwork's road line rather than across it — at 0.80 the
        // line runs straight through the letters.
        y: root.artY + root.artH * 0.86 - height / 2
        spacing: root.artW * 0.028

        Repeater {
            // PRND, the order of a real selector gate, and the one every driver
            // has read since the 1960s. It was P D N R here, which is not an
            // order any gate produces: it puts D next to P, a transition no
            // lever can make without passing through R and N. The row is only a
            // legend -- gear is inferred from speed and R and N cannot light --
            // but a legend that contradicts the lever beside it is worse than no
            // legend, and it costs nothing to have it agree.
            model: ["P", "R", "N", "D"]
            delegate: Text {
                text: modelData
                color: modelData === root.gear ? root.textColor : root.gearIdleColor
                font.pixelSize: root.artUnitH * 0.055
                font.weight: modelData === root.gear ? Font.Bold : Font.Light
                font.letterSpacing: 1
                font.family: root.uiFont
            }
        }
    }

    // A number with its unit and caption, sized off the lens height at true
    // aspect (screen.unitH), so the stretch does not widen the glyphs.
    // Explicit anchors rather than a Column: the gap under the big number has
    // to close up against its font leading, and a Column's spacing would apply
    // that same negative gap to the caption too and overlap it.
    component Readout: Item {
        id: ro
        property string value: "0"
        property string unit: ""
        property string caption: ""

        width: Math.max(num.width, Math.max(unitText.width, cap.width))
        height: cap.y + cap.height

        Text {
            id: num
            anchors.horizontalCenter: parent.horizontalCenter
            y: 0
            text: ro.value
            color: root.textColor
            font.pixelSize: screen.unitH * 0.28
            font.weight: Font.ExtraLight
            font.family: root.uiFont
        }
        Text {
            id: unitText
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: num.bottom
            anchors.topMargin: -screen.unitH * 0.055
            text: ro.unit
            color: root.accent
            font.pixelSize: screen.unitH * 0.07
            font.weight: Font.Bold
            font.letterSpacing: 5
            font.family: root.uiFont
        }
        Text {
            id: cap
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: unitText.bottom
            anchors.topMargin: screen.unitH * 0.02
            text: ro.caption
            color: root.textColor
            opacity: 0.45
            font.pixelSize: screen.unitH * 0.045
            font.letterSpacing: 3
            font.family: root.uiFont
        }
    }

    // --- Now playing ---------------------------------------------------------
    // Nothing feeds these yet — there is no media source in this project and no
    // link to the head unit. Bind them to one and the strip appears; an empty
    // title hides it, which is also what "nothing is playing" should look like.
    //
    // trackElapsed is seconds. It is a plain value rather than something that
    // counts itself, so whatever owns playback stays the single source of
    // truth and a pause does not need a second signal to stop a local timer.
    // Off for now. An empty title hides the whole strip, which is the same path
    // "nothing is playing" takes — so restoring it is putting the demo string
    // back, not undoing a deletion. The Row and its layout below are untouched.
    property string trackTitle: ""
    property int trackElapsed: 0

    function formatElapsed(seconds) {
        const m = Math.floor(seconds / 60);
        const s = Math.floor(seconds % 60);
        return m + ":" + (s < 10 ? "0" : "") + s;
    }

    // Demo only: walks the position so the strip is not frozen at 0:00.
    Timer {
        interval: 1000
        running: demoMode
        repeat: true
        onTriggered: root.trackElapsed = (root.trackElapsed + 1) % 244
    }

    // Above the clock, in the empty band over the lens. Placed against the
    // artwork like the telltales rather than inside the lit region, because
    // that region starts at the clock and this sits outside it.
    //
    // One row: name and elapsed at the same size, told apart by opacity, the
    // way the gear row does it. Different sizes here would need baseline
    // alignment, which a Row does not give.
    readonly property real trackY: 0.17         // fraction of artH, row centre

    Row {
        x: root.artX + root.artW * 0.5 - width / 2
        y: root.artY + root.artH * root.trackY - height / 2
        spacing: root.artW * 0.019
        visible: root.trackTitle !== ""

        Text {
            id: trackName
            // Capped, so a long title elides instead of running out under the
            // telltales in the corners.
            width: Math.min(implicitWidth, root.artW * 0.30)
            elide: Text.ElideRight
            text: root.trackTitle
            color: root.textColor
            opacity: 0.85
            font.pixelSize: root.artUnitH * 0.030
            font.family: root.uiFont
            font.weight: Font.Light
        }

        Text {
            // Elapsed only. There is no duration behind it to make a remaining
            // time or a progress bar honest.
            text: root.formatElapsed(root.trackElapsed)
            color: root.textColor
            opacity: 0.45
            font.pixelSize: trackName.font.pixelSize
            font.family: root.uiFont
            font.weight: Font.Light
            font.letterSpacing: 1
        }
    }

    // The lit region of the bezel; both readouts sit inside it.
    Item {
        id: screen
        x: root.artX + root.artW * root.screenLeftFrac
        y: root.artY + root.artH * root.screenTopFrac
        width: root.artW * (root.screenRightFrac - root.screenLeftFrac)
        height: root.artH * (root.screenBottomFrac - root.screenTopFrac)

        readonly property real w: width
        readonly property real h: height
        // The same band at the artwork's true aspect. Type inside the lens is
        // sized off this, not off h, so the vertical stretch does not scale the
        // glyphs up in both directions.
        readonly property real unitH: root.artUnitH * (root.screenBottomFrac - root.screenTopFrac)

        // Clock, sitting just below the moulding drawn into the top of the lens
        // artwork so it doesn't collide with it.
        // Time comes from NetworkClock (SNTP-disciplined, CLUSTER_TZ zone), not
        // from new Date(). The QML no longer runs a timer of its own: the
        // backend ticks on the minute boundary and this just binds, so there is
        // one clock in the process instead of a C++ one and a QML one that
        // could disagree.
        Row {
            id: clock
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 0
            spacing: screen.unitH * 0.022
            // Dimmed until a time server has actually answered, so an unsynced
            // guest reads as "time unknown" rather than as a confident wrong
            // answer. Animated so the step to synced is not a jarring pop.
            opacity: Clock.synced ? 0.85 : 0.38
            Behavior on opacity { NumberAnimation { duration: 400 } }

            Text {
                text: Clock.synced ? Clock.time : "--:--"
                color: root.textColor
                font.pixelSize: screen.unitH * 0.09
                font.family: root.uiFont
                font.weight: Font.Light
                font.letterSpacing: 2
            }
            Text {
                text: Clock.meridiem
                color: root.textColor
                // Smaller and top-aligned against the digits, the way a
                // meridiem marker is normally set.
                font.pixelSize: screen.unitH * 0.045
                font.family: root.uiFont
                font.weight: Font.Light
                font.letterSpacing: 1
                anchors.top: parent.top
                anchors.topMargin: screen.unitH * 0.012
                visible: Clock.synced
            }
        }


        Readout {
            x: screen.w * 0.22 - width / 2
            anchors.verticalCenter: parent.verticalCenter
            value: Math.round(live.speedShown)
            unit: "KM/H"
            caption: "SPEED"
        }

        Readout {
            x: screen.w * 0.78 - width / 2
            anchors.verticalCenter: parent.verticalCenter
            // Watts, shown as watts. It used to divide by 1000 for a single
            // kW digit, which on a 450W motor meant a readout permanently at 0.
            value: Math.round(live.powerShown)
            unit: "W"
            caption: "POWER"
        }
    }
}
