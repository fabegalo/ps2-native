import QtQuick
import PS2Native

// Indeterminate progress arc (Canvas: works with every scene graph backend).
Item {
    id: spinner
    property real size: 18
    property color color: Theme.cyan
    property bool running: visible
    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, r = w / 2 - 1.5
            ctx.lineWidth = Math.max(1.6, w / 9)
            ctx.lineCap = "round"
            ctx.strokeStyle = Qt.rgba(spinner.color.r, spinner.color.g, spinner.color.b, 0.18)
            ctx.beginPath()
            ctx.arc(w / 2, w / 2, r, 0, Math.PI * 2)
            ctx.stroke()
            ctx.strokeStyle = spinner.color
            ctx.beginPath()
            ctx.arc(w / 2, w / 2, r, -Math.PI / 2, Math.PI * 0.35)
            ctx.stroke()
        }
        onWidthChanged: requestPaint()
        Connections {
            target: spinner
            function onColorChanged() { canvas.requestPaint() }
        }
        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: spinner.running && !Theme.reduceMotion
        }
    }
}
