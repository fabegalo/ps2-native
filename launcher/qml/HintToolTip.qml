import QtQuick
import QtQuick.Controls.Basic
import PS2Native

ToolTip {
    id: tip
    delay: 350
    timeout: 8000
    padding: 10
    contentItem: Text {
        text: tip.text
        color: Theme.text
        font.pixelSize: Theme.captionSize + 1
        wrapMode: Text.WordWrap
        width: Math.min(implicitWidth, 320)
    }
    background: Rectangle {
        color: Theme.surfaceHigh
        border.color: Theme.borderStrong
        radius: Theme.radiusSmall
    }
}
