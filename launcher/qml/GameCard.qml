import QtQuick
import PS2Native

Item {
    id: card
    property string entryId: ""
    property string title: ""
    property string serial: ""
    property string region: ""
    property string cover: ""
    property string compatibility: "pending"
    property bool favorite: false
    property bool selected: false
    signal activated()
    signal opened()

    readonly property bool hovered: mouse.containsMouse
    readonly property bool problem: compatibility !== "runtime-unavailable" && compatibility !== "pending"

    Accessible.role: Accessible.ListItem
    Accessible.name: title + (serial.length > 0 ? ", " + serial : "") + ", " + Theme.compatLabel(compatibility)

    Item {
        id: lift
        width: parent.width
        height: parent.height
        y: card.hovered && !Theme.reduceMotion ? -4 : 0
        Behavior on y { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }

        Rectangle {
            id: glow
            x: -4; y: -4
            width: coverArt.width + 8
            height: coverArt.height + 8
            radius: Theme.radius + 4
            color: "transparent"
            border.width: 2
            border.color: card.selected ? Theme.cyan : Theme.alpha(Theme.accentBright, 0.6)
            opacity: card.selected || card.hovered ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }
        CoverArt {
            id: coverArt
            width: parent.width
            height: Math.round(width * 1.38)
            cover: card.cover
            title: card.title
            serial: card.serial
            region: card.region
            dimmed: card.problem
        }
        Icon {
            visible: card.favorite
            name: "star"
            size: 16
            stroke: 2
            color: Theme.warning
            anchors.top: coverArt.top
            anchors.right: coverArt.right
            anchors.margins: 8
        }
        Column {
            anchors.top: coverArt.bottom
            anchors.topMargin: 10
            width: parent.width
            spacing: 4
            Text {
                width: parent.width
                text: card.title
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: [card.serial, card.region].filter(s => s.length > 0).join("  ·  ") || qsTr("Not identified")
                color: Theme.textFaint
                font.pixelSize: Theme.captionSize
                elide: Text.ElideRight
            }
            CompatBadge {
                compatibility: card.compatibility
                small: true
            }
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: card.activated()
        onDoubleClicked: card.opened()
    }
}
