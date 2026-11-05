import QtQuick
import PS2Native

Rectangle {
    id: tile
    property string value: ""
    property string label: ""
    property color tone: Theme.cyan
    implicitWidth: 96
    implicitHeight: Math.max(76, column.implicitHeight + 24)
    radius: Theme.radius
    color: Theme.surfaceSunken
    border.color: Theme.border

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Text {
            width: parent.width
            text: tile.value
            color: tile.tone
            font.pixelSize: 26
            font.weight: Font.Bold
            elide: Text.ElideRight
        }
        // Labels wrap instead of running past the tile when the card is narrow or translated.
        Text {
            width: parent.width
            text: tile.label
            color: Theme.textMuted
            font.pixelSize: Theme.captionSize
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }
}
