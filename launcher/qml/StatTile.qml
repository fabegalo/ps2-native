import QtQuick
import PS2Native

Rectangle {
    id: tile
    property string value: ""
    property string label: ""
    property color tone: Theme.cyan
    implicitHeight: 76
    radius: Theme.radius
    color: Theme.surfaceSunken
    border.color: Theme.border

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Text {
            text: tile.value
            color: tile.tone
            font.pixelSize: 26
            font.weight: Font.Bold
        }
        Text {
            text: tile.label
            color: Theme.textMuted
            font.pixelSize: Theme.captionSize
        }
    }
}
