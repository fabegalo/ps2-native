import QtQuick
import PS2Native

Rectangle {
    id: card
    default property alias content: body.data
    property int padding: 20
    property bool glow: false

    color: Theme.surface
    radius: Theme.radiusLarge
    border.color: glow ? Theme.alpha(Theme.accent, 0.45) : Theme.border
    border.width: 1
    // Content: one child item with anchors.fill: parent; its implicit size sizes the card.
    implicitHeight: (body.children.length > 0 ? body.children[0].implicitHeight : 0) + padding * 2

    // Thin top highlight, like light catching the edge of the console.
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.radiusLarge
        anchors.rightMargin: Theme.radiusLarge
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.alpha(card.glow ? Theme.cyan : Theme.accentBright, card.glow ? 0.5 : 0.18) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
    Item {
        id: body
        anchors.fill: parent
        anchors.margins: card.padding
    }
}
