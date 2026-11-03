import QtQuick
import PS2Native

Item {
    id: root
    property string iconName: ""
    property string tooltip: ""
    property bool available: true
    property bool checked: false
    property real size: 36
    property color tint: Theme.textMuted
    signal clicked()

    implicitWidth: size
    implicitHeight: size
    opacity: available ? 1 : 0.4
    activeFocusOnTab: available
    Accessible.role: Accessible.Button
    Accessible.name: tooltip
    Accessible.onPressAction: if (available) root.clicked()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root.checked ? Theme.alpha(Theme.accent, 0.18) : mouse.containsMouse && root.available ? Theme.surfaceHover : "transparent"
        border.width: root.checked || root.activeFocus ? 1 : 0
        border.color: root.activeFocus ? Theme.cyan : Theme.alpha(Theme.accent, 0.5)
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    Icon {
        anchors.centerIn: parent
        name: root.iconName
        size: Math.round(root.size * 0.5)
        color: root.checked ? Theme.accentBright : mouse.containsMouse ? Theme.text : root.tint
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.available ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (root.available) root.clicked()
    }
    Keys.onReturnPressed: if (available) root.clicked()
    Keys.onSpacePressed: if (available) root.clicked()
    HintToolTip {
        visible: mouse.containsMouse && root.tooltip.length > 0
        text: root.tooltip
    }
}
