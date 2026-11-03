import QtQuick
import PS2Native

Rectangle {
    id: overlay
    property bool active: false
    color: Theme.alpha(Theme.bg, 0.86)
    opacity: active ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: Theme.fast } }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 28
        radius: Theme.radiusLarge + 4
        color: Theme.alpha(Theme.accent, 0.06)
        border.width: 2
        border.color: Theme.alpha(Theme.cyan, 0.55)
        Column {
            anchors.centerIn: parent
            spacing: 14
            Icon {
                name: "upload"
                size: 56
                stroke: 1.4
                color: Theme.cyan
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: qsTr("Drop to add to your library")
                color: Theme.text
                font.pixelSize: Theme.titleSize + 2
                font.weight: Font.Bold
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: qsTr("Disc images (.iso, .bin) and folders go to the library · 4 MiB ROM0 dumps go to BIOS")
                color: Theme.textMuted
                font.pixelSize: Theme.bodySize
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: qsTr("Files are referenced where they are. Nothing is copied.")
                color: Theme.textFaint
                font.pixelSize: Theme.captionSize + 1
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
}
