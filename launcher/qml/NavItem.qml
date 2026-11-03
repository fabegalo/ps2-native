import QtQuick
import PS2Native

Item {
    id: root
    property string label: ""
    property string iconName: ""
    property bool active: false
    property bool collapsed: false
    property string badge: ""
    signal clicked()

    implicitHeight: 44
    activeFocusOnTab: true
    Accessible.role: Accessible.PageTab
    Accessible.name: label
    Accessible.onPressAction: root.clicked()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root.active ? Theme.alpha(Theme.accent, 0.16) : mouse.containsMouse ? Theme.surfaceHover : "transparent"
        border.width: root.active || root.activeFocus ? 1 : 0
        border.color: root.activeFocus ? Theme.cyan : Theme.alpha(Theme.accent, 0.35)
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    Rectangle {
        visible: root.active
        width: 3
        height: 20
        radius: 2
        anchors.left: parent.left
        anchors.leftMargin: -1
        anchors.verticalCenter: parent.verticalCenter
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.cyan }
            GradientStop { position: 1; color: Theme.accent }
        }
    }
    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: root.collapsed ? (root.width - 20) / 2 : 14
        spacing: 12
        Icon {
            name: root.iconName
            size: 20
            color: root.active ? Theme.cyan : mouse.containsMouse ? Theme.text : Theme.textMuted
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: !root.collapsed
            text: root.label
            color: root.active ? Theme.text : mouse.containsMouse ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.bodySize
            font.weight: root.active ? Font.DemiBold : Font.Medium
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    Badge {
        visible: root.badge.length > 0 && !root.collapsed
        text: root.badge
        small: true
        tone: Theme.accentBright
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
    HintToolTip {
        visible: root.collapsed && mouse.containsMouse
        text: root.label
    }
}
