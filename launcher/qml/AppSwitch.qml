import QtQuick
import QtQuick.Controls.Basic
import PS2Native

Switch {
    id: control
    implicitWidth: 44
    implicitHeight: 28
    padding: 0
    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 12
        color: control.checked ? Theme.accent : Theme.surfaceHigh
        border.color: control.checked ? Theme.alpha(Theme.cyan, 0.5) : Theme.borderStrong
        opacity: control.enabled ? 1 : 0.4
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            y: 3
            width: 18
            height: 18
            radius: 9
            color: control.checked ? "#ffffff" : Theme.textMuted
            Behavior on x { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item { implicitWidth: 0 }
}
