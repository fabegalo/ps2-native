import QtQuick
import QtQuick.Controls.Basic
import PS2Native

ScrollBar {
    id: bar
    padding: 2
    minimumSize: 0.06
    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: bar.pressed ? Theme.accentBright : bar.hovered ? Theme.borderStrong : Theme.alpha(Theme.borderStrong, 0.7)
        opacity: bar.policy === ScrollBar.AlwaysOn || bar.active || bar.hovered ? 1 : 0.0
        Behavior on opacity { NumberAnimation { duration: Theme.normal } }
    }
    background: Item {}
}
