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
    readonly property color faultColor: "#ff2b2b"   // telltales and motor lamp
    readonly property color iconColor: "white"      // the telltales while idle

    // The car photographs are near-white, so they can be pushed to any body
    // colour by colorizing them — luminance is preserved, so the panel shading,
    // the glass and the wheels all survive. Strength 0 leaves the photo alone;
    // much past 0.5 the glass starts taking the body colour too.
    readonly property color carTint: "white"
    readonly property real carTintStrength: 0.0

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
            to: 250
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
    QtObject {
        id: live
        readonly property real speed: fixedSpeed >= 0 ? fixedSpeed : demoMode ? root.demoSpeed : Vehicle.speed
        // Watts. 25 W per km/h in the demo, so power lines up with the right
        // scale: 40 km/h is 1 kW, 80 is 2, up to 240 being 6. Without that the
        // readout and the light would disagree about which mark they are on.
        readonly property real power: fixedSpeed >= 0 ? fixedSpeed * 25 : demoMode ? root.demoSpeed * 25 : Vehicle.power
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
    readonly property real bezelScale: 1.18
    readonly property real artW: width * bezelScale
    readonly property real artUnitH: artW * (447 / 1024)
    readonly property real artH: height
    readonly property real artX: (width - artW) / 2
    readonly property real artY: (height - artH) / 2
    // ~1.14 at 1024x600. Drop artH back to artUnitH to undo the stretch.
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
    readonly property var scaleValues: [0, 40, 80, 120, 160, 200, 240]
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
            font.family: "Century Gothic"
            x: root.artX + root.artW * (root.scaleLX[index] + root.scaleInsetX) - width / 2
            y: root.artY + root.artH * root.scaleY[index] - height / 2
        }
    }

    // The right side is the power scale in kW, sharing the left side's
    // positions: 1 sits where 40 does, 2 where 80, and so on. Since the ring is
    // driven by speed, the light arriving at "1" is the same instant it arrives
    // at "40" — which only reads correctly because the demo ties power to speed
    // at 25 W per km/h, so 40 km/h is exactly 1 kW.
    readonly property var scaleValuesRight: [0, 1, 2, 3, 4, 5, 6]

    Repeater {
        model: root.scaleValuesRight.length
        delegate: Text {
            id: rightLabel
            text: root.scaleValuesRight[index]
            color: root.scaleColor
            font.pixelSize: root.artUnitH * 0.032
            font.family: "Century Gothic"

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
    readonly property real soc: demoMode ? 78 : Vehicle.battery
    readonly property real odoKm: demoMode ? 12480 : 0

    component CornerStat: Column {
        property string value: ""
        property string caption: ""
        spacing: root.artUnitH * 0.004

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: value
            color: root.textColor
            opacity: 0.92
            font.pixelSize: root.artUnitH * 0.055
            font.family: "Century Gothic"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: caption
            color: root.accent
            opacity: 0.75
            font.pixelSize: root.artUnitH * 0.026
            font.weight: Font.Bold
            font.letterSpacing: 2
            font.family: "Century Gothic"
        }
    }

    CornerStat {
        x: root.artX + root.artW * 0.225 - width / 2
        y: root.artY + root.artH * root.bottomRowY
        value: Math.round(root.soc) + "%"
        caption: "SOC"
    }

    CornerStat {
        x: root.artX + root.artW * 0.775 - width / 2
        y: root.artY + root.artH * root.bottomRowY
        value: Math.round(root.odoKm).toLocaleString(Qt.locale("en_US"), "f", 0)
        caption: "KM TOTAL"
    }

    // --- Telltales -----------------------------------------------------------
    // White when idle, flickering red when their fault is raised.
    //
    // The source PNGs are white silhouettes on transparent. That matters:
    // MultiEffect's colorization tints by luminance, so the original black line
    // art would have stayed black whatever colour was applied to it.
    //
    // None of these is wired to the backend. VehicleBackend has no signal for
    // any of them — its only real faults are overspeed, vibration and
    // overcurrent — so each is a plain property to bind later.
    property bool faultEngine: false
    property bool faultBattery: false
    property bool faultAbs: false
    property bool faultSeatbelt: false

    // --- Fault mode ----------------------------------------------------------
    // Two states for the centre of the lens. Normally the car is seen from
    // behind, sitting on the road graphic; when something is wrong the road
    // drops away and the view swaps to the car from above, so the eye goes to
    // the vehicle rather than to the driving scene.
    //
    // Driven off the telltales *and* the backend's own alerts. The telltales
    // are what the demo cycles, so the mode can be seen without hardware, and
    // criticalAlert is the one fault signal that is real today — the four lamps
    // above have nothing behind them yet.
    readonly property bool faultMode: root.faultEngine || root.faultBattery
                                      || root.faultAbs || root.faultSeatbelt
                                      || Vehicle.criticalAlert

    component Telltale: Item {
        id: lamp
        property alias icon: img.source
        property bool active: false

        Image {
            id: img
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            visible: false
        }

        MultiEffect {
            id: tint
            source: img
            anchors.fill: img
            // Colorized in both states, not just when raised: on a light
            // scheme the PNGs' own white would disappear into the background.
            // Colorizing to white is a no-op, so dark schemes are unaffected.
            colorization: 1.0
            colorizationColor: lamp.active ? root.faultColor : root.iconColor
            // Full opacity in both states: idle reads as iconColor, raised as
            // faultColor. Only the flicker takes it below 1.
            opacity: 1.0

            // Flicker only while raised; snap back to full after, so a lamp
            // never gets stranded mid-fade when its fault clears.
            SequentialAnimation on opacity {
                running: lamp.active
                loops: Animation.Infinite
                NumberAnimation {
                    to: 0.15
                    duration: 260
                }
                NumberAnimation {
                    to: 1.0
                    duration: 260
                }
                onStopped: tint.opacity = 1.0
            }
        }
    }

    // Two per side in the upper corners, following the ring's shoulder.
    readonly property real telltaleSize: root.artUnitH * 0.038
    readonly property var telltaleX: [0.1771, 0.2318, 0.7682, 0.8229]
    readonly property var telltaleY: [0.2495, 0.2153, 0.2153, 0.2495]

    Repeater {
        model: [
            {
                icon: "telltale_engine",
                on: root.faultEngine
            },
            {
                icon: "telltale_battery",
                on: root.faultBattery
            },
            {
                icon: "telltale_abs",
                on: root.faultAbs
            },
            {
                icon: "telltale_seatbelt",
                on: root.faultSeatbelt
            }
        ]
        delegate: Telltale {
            width: root.telltaleSize
            height: root.telltaleSize
            x: root.artX + root.artW * root.telltaleX[index] - width / 2
            y: root.artY + root.artH * root.telltaleY[index] - height / 2
            icon: "qrc:/images/images/" + modelData.icon + ".png"
            active: modelData.on
        }
    }

    // Demo only: cycles the lamps so the flicker can be seen without hardware.
    Timer {
        interval: 2200
        running: demoMode
        repeat: true
        property int step: 0
        onTriggered: {
            step = (step + 1) % 5;
            root.faultEngine = step === 1;
            root.faultBattery = step === 2;
            root.faultAbs = step === 3;
            root.faultSeatbelt = step === 4;
        }
    }

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
    readonly property real carHeight: 0.18      // fraction of artUnitH
    readonly property real carY: 0.576          // fraction of artH, top of the rear view
    readonly property real faultCarHeight: 0.38 // fraction of artUnitH
    readonly property real faultCarCentreY: 0.58

    Image {
        id: carRear
        source: "qrc:/images/images/car_rear.png"
        height: root.artUnitH * root.carHeight
        width: height * (800 / 513)             // the source's trimmed aspect
        x: root.artX + root.artW * 0.5 - width / 2
        y: root.artY + root.artH * root.carY
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
    // car. Both key off errorKind rather than off faultMode, so a fault that is
    // neither — the seatbelt lamp — still swaps to the overhead view but does
    // not accuse the motor of anything.
    //
    // The split follows what the backend measures. Vibration is mechanical;
    // current is electrical; an overspeed is the motor running away, so it goes
    // with mechanical. tempWarning and voltageWarning are left out: both are
    // hardcoded false in cluster.h and would only look wired.
    // Kind and symbol come out of one decision rather than two parallel ones,
    // so the banner and the icon in the lamp can never name different faults.
    // First match wins, so a mechanical fault outranks an electrical one when
    // both are up — the motor is the more urgent of the two.
    readonly property string errorKind: {
        if (Vehicle.vibWarning || Vehicle.speedWarning || root.faultEngine)
            return "MECHANICAL";
        if (root.faultAbs)
            return "MECHANICAL";
        if (Vehicle.currentWarning || root.faultBattery)
            return "ELECTRICAL";
        return "";
    }
    // Two symbols, drawn for this lamp at the size it actually renders --
    // see tools/make_fault_icons.py. The corner telltales keep their own
    // artwork; those name a component, this names a system.
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
            color: root.faultColor
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
                duration: 700
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                to: 0.75
                duration: 700
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


    // --- Gear indicator ------------------------------------------------------
    // Static: VehicleBackend exposes no gear signal. Bind `gear` to one when
    // there is something to bind it to.
    property string gear: "P"

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        // Below the artwork's road line rather than across it — at 0.80 the
        // line runs straight through the letters.
        y: root.artY + root.artH * 0.86 - height / 2
        spacing: root.artW * 0.028

        Repeater {
            model: ["P", "N", "R"]
            delegate: Text {
                text: modelData
                color: modelData === root.gear ? root.textColor : root.gearIdleColor
                font.pixelSize: root.artUnitH * 0.055
                font.weight: modelData === root.gear ? Font.Bold : Font.Light
                font.letterSpacing: 1
                font.family: "Century Gothic"
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
            font.family: "Century Gothic"
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
            font.family: "Century Gothic"
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
            font.family: "Century Gothic"
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
    property string trackTitle: demoMode ? "Midnight City — M83" : ""
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
            font.family: "Century Gothic"
            font.weight: Font.Light
        }

        Text {
            // Elapsed only. There is no duration behind it to make a remaining
            // time or a progress bar honest.
            text: root.formatElapsed(root.trackElapsed)
            color: root.textColor
            opacity: 0.45
            font.pixelSize: trackName.font.pixelSize
            font.family: "Century Gothic"
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
        Text {
            id: clock
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 0
            color: root.textColor
            opacity: 0.85
            font.pixelSize: screen.unitH * 0.09
            font.family: "Century Gothic"
            font.weight: Font.Light
            font.letterSpacing: 2
            function tick() {
                text = Qt.formatTime(new Date(), "hh:mm");
            }
            Component.onCompleted: tick()
        }

        Timer {
            interval: 1000
            running: true
            repeat: true
            onTriggered: clock.tick()
        }


        Readout {
            x: screen.w * 0.22 - width / 2
            anchors.verticalCenter: parent.verticalCenter
            value: Math.round(live.speed)
            unit: "KM/H"
            caption: "SPEED"
        }

        Readout {
            x: screen.w * 0.78 - width / 2
            anchors.verticalCenter: parent.verticalCenter
            // Backend power is watts; shown as kW, so the readout stays a
            // single digit instead of a four-digit number.
            //
            // floor, not round: rounding ticks over at the halfway point, so
            // the readout would read 2 while the light was still climbing
            // between the 1 and 2 marks. Truncating makes it change exactly as
            // the light arrives.
            value: Math.floor(live.power / 1000)
            unit: "KW"
            caption: "POWER"
        }
    }
}
