import QtQuick
import PS2Native

// Button that stays hoverable when unavailable, so it can say why.
Item {
    id: root
    property string text: ""
    property string iconName: ""
    property string variant: "secondary" // primary | secondary | ghost | danger
    property bool available: true
    property string reason: ""      // shown when unavailable
    property string tooltip: ""
    property bool compact: false
    property bool busy: false
    signal clicked()

    readonly property bool hovered: mouse.containsMouse
    readonly property bool pressed: mouse.pressed
    readonly property color foreground: !available ? Theme.textFaint
                                       : variant === "primary" ? "#ffffff"
                                       : variant === "danger" ? Theme.danger
                                       : Theme.text

    implicitHeight: compact ? 32 : 40
    implicitWidth: content.implicitWidth + (compact ? 24 : 34)
    opacity: available || variant === "primary" ? 1.0 : 0.6
    activeFocusOnTab: available
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.description: available ? tooltip : reason
    Accessible.onPressAction: if (available) root.clicked()

    Rectangle {
        id: background
        anchors.fill: parent
        radius: Theme.radius
        border.width: root.variant === "ghost" ? 0 : 1
        border.color: !root.available && root.variant === "primary" ? Theme.border
                     : root.variant === "primary" ? Theme.alpha(Theme.cyan, root.hovered && root.available ? 0.55 : 0.25)
                     : root.variant === "danger" ? Theme.alpha(Theme.danger, 0.35)
                     : root.hovered && root.available ? Theme.borderStrong : Theme.border
        color: !root.available && root.variant === "primary" ? Theme.surfaceSunken
             : root.variant === "primary" ? "transparent"
             : root.variant === "ghost" ? (root.hovered && root.available ? Theme.surfaceHover : "transparent")
             : root.variant === "danger" ? (root.hovered && root.available ? Theme.alpha(Theme.danger, 0.16) : Theme.alpha(Theme.danger, 0.08))
             : (root.pressed ? Theme.surface : root.hovered && root.available ? Theme.surfaceHover : Theme.surfaceHigh)
        gradient: root.variant === "primary" && root.available ? primaryGradient : null
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    Gradient {
        id: primaryGradient
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: root.pressed ? Theme.accentDeep : root.hovered && root.available ? Theme.accentBright : Theme.accent }
        GradientStop { position: 1.0; color: root.pressed ? Theme.accent : root.hovered && root.available ? "#3fb4ff" : "#2a8dff" }
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: Theme.radius + 3
        color: "transparent"
        border.width: 2
        border.color: Theme.cyan
        visible: root.activeFocus
    }
    Row {
        id: content
        anchors.centerIn: parent
        spacing: 8
        Spinner {
            visible: root.busy
            size: root.compact ? 14 : 16
            color: root.foreground
            anchors.verticalCenter: parent.verticalCenter
        }
        Icon {
            visible: root.iconName.length > 0 && !root.busy
            name: !root.available && root.variant === "primary" ? "lock" : root.iconName
            color: root.foreground
            size: root.compact ? 15 : 17
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            visible: text.length > 0
            color: root.foreground
            font.pixelSize: root.compact ? 13 : 14
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.available ? Qt.PointingHandCursor : Qt.ForbiddenCursor
        onClicked: if (root.available && !root.busy) root.clicked()
    }
    Keys.onReturnPressed: if (available && !busy) root.clicked()
    Keys.onSpacePressed: if (available && !busy) root.clicked()

    HintToolTip {
        visible: root.hovered && text.length > 0
        text: !root.available && root.reason.length > 0 ? root.reason : root.tooltip
    }
}
