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
    // the letterboxed bars blend in — the screen reads as a plain rectangle.
    color: "#0d1424"

    Material.theme: Material.Dark

    readonly property color cyan: "#00d4ff"

    // Smoothed backend values, shared by the readouts and the neon glow so the
    // number and the light ramp together instead of snapping between frames.
    // Demo sweep, active only when CLUSTER_DEMO is set in the environment (see
    // main.cpp). Lets the cluster be driven on a desktop where there is no
    // motor_controller feeding the SPI reader.
    property real demoSpeed: 0

    SequentialAnimation on demoSpeed {
        running: demoMode
        loops: Animation.Infinite
        NumberAnimation {
            to: 250
            duration: 11000
            easing.type: Easing.InOutSine
        }
        PauseAnimation {
            duration: 800
        }
        NumberAnimation {
            to: 0
            duration: 8000
            easing.type: Easing.InOutSine
        }
        PauseAnimation {
            duration: 600
        }
    }

    // Straight bindings, deliberately unanimated. A Behavior here restarts on
    // every write, so a source that changes each frame leaves the property
    // pinned at its starting value; a standalone SmoothedAnimation latches `to`
    // when it starts and never re-targets. If the SPI data turns out jittery,
    // smooth it in VehicleBackend rather than reintroducing either of those.
    QtObject {
        id: live
        readonly property real speed: demoMode ? root.demoSpeed : Vehicle.speed
        readonly property real power: demoMode ? root.demoSpeed * 0.75 : Vehicle.power
    }

    // Fractions of the bezel artwork occupied by its lit lens, measured off the
    // neon outline in the source image (x 146..878, y 105..350 of 1024x447).
    // Nudge these if the readouts don't sit where you want in the lens —
    // nothing else needs to change.
    readonly property real screenLeftFrac: 0.143
    readonly property real screenRightFrac: 0.857
    readonly property real screenTopFrac: 0.235
    readonly property real screenBottomFrac: 0.783

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
    // fully visible up to a scale of about 1.29. Aspect is always preserved —
    // the source is 2.29:1 and stretching visibly distorts the bezel.
    readonly property real bezelScale: 1.18
    readonly property real artW: width * bezelScale
    readonly property real artH: artW * (447 / 1024)
    readonly property real artX: (width - artW) / 2
    readonly property real artY: (height - artH) / 2

    Image {
        id: bezel
        x: root.artX
        y: root.artY
        width: root.artW
        height: root.artH
        source: "qrc:/images/images/cluster_bezel_base.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    // How far the neon has lit, 0 at a standstill to 1 at glowTopSpeed.
    readonly property real glowFill: Math.max(0, Math.min(1, live.speed / root.glowTopSpeed))

    // Half-width of the feathered boundary, as a fraction of artwork height.
    readonly property real glowSoftness: 0.055

    // The lit boundary, as a fraction of the artwork rect — the mask is sized
    // to that rect, so it shares its coordinates.
    //
    // The sweep runs over the neon's own span rather than the whole image: the
    // ring only occupies y 105..350 of the 447px artwork, so using the full
    // height would waste part of the range crossing empty bezel. It also
    // travels a softness beyond the ring at each end, so at 0 the whole feather
    // sits below the ring (fully dark) and at 1 above it (fully lit), instead
    // of leaving the extremes half-lit.
    readonly property real glowEdge: root.screenBottomFrac + root.glowSoftness - (root.screenBottomFrac - root.screenTopFrac + 2 * root.glowSoftness) * root.glowFill

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
        fillMode: Image.PreserveAspectFit
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
                color: modelData === root.gear ? "white" : "#4c5c70"
                font.pixelSize: root.artH * 0.055
                font.weight: modelData === root.gear ? Font.Bold : Font.Light
                font.letterSpacing: 1
                font.family: "Century Gothic"
            }
        }
    }

    // A number with its unit and caption, sized off the lens height.
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
            color: "white"
            font.pixelSize: screen.h * 0.28
            font.weight: Font.ExtraLight
            font.family: "Century Gothic"
        }
        Text {
            id: unitText
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: num.bottom
            anchors.topMargin: -screen.h * 0.055
            text: ro.unit
            color: root.cyan
            font.pixelSize: screen.h * 0.07
            font.weight: Font.Bold
            font.letterSpacing: 5
            font.family: "Century Gothic"
        }
        Text {
            id: cap
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: unitText.bottom
            anchors.topMargin: screen.h * 0.02
            text: ro.caption
            color: "white"
            opacity: 0.45
            font.pixelSize: screen.h * 0.045
            font.letterSpacing: 3
            font.family: "Century Gothic"
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

        // Clock, sitting just below the moulding drawn into the top of the lens
        // artwork so it doesn't collide with it.
        Text {
            id: clock
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 0
            color: "white"
            opacity: 0.85
            font.pixelSize: screen.h * 0.09
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
            value: Math.round(live.power)
            unit: "KW"
            caption: "POWER"
        }
    }
}
