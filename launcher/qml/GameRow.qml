import QtQuick
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

Rectangle {
    id: row
    property string title: ""
    property string serial: ""
    property string region: ""
    property string videoMode: ""
    property string sizeText: ""
    property string cover: ""
    property string fileName: ""
    property string compatibility: "pending"
    property bool favorite: false
    property bool selected: false
    signal activated()

    height: 64
    radius: Theme.radius
    color: selected ? Theme.alpha(Theme.accent, 0.14) : mouse.containsMouse ? Theme.surfaceHover : "transparent"
    border.color: selected ? Theme.alpha(Theme.accent, 0.45) : "transparent"
    Accessible.role: Accessible.ListItem
    Accessible.name: title

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 16
        spacing: 14
        CoverArt {
            Layout.preferredWidth: 36
            Layout.preferredHeight: 50
            cover: row.cover
            title: ""
            serial: ""
            region: ""
            radius: 4
        }
        Column {
            Layout.fillWidth: true
            spacing: 3
            Row {
                spacing: 6
                width: parent.width
                Icon { visible: row.favorite; name: "star"; size: 13; color: Theme.warning; stroke: 2; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: row.title
                    color: Theme.text
                    font.pixelSize: Theme.bodySize
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, parent.width - 20)
                }
            }
            Text {
                width: parent.width
                text: row.fileName
                color: Theme.textFaint
                font.pixelSize: Theme.captionSize
                elide: Text.ElideMiddle
            }
        }
        Text {
            Layout.preferredWidth: 96
            text: row.serial
            color: Theme.textMuted
            font.family: Launcher.monoFamily
            font.pixelSize: Theme.captionSize + 1
        }
        Text {
            Layout.preferredWidth: 64
            text: row.region
            color: Theme.textMuted
            font.pixelSize: Theme.captionSize + 1
        }
        Text {
            Layout.preferredWidth: 72
            text: row.sizeText
            color: Theme.textFaint
            font.pixelSize: Theme.captionSize + 1
            horizontalAlignment: Text.AlignRight
        }
        Item {
            Layout.preferredWidth: 150
            Layout.fillHeight: true
            CompatBadge {
                compatibility: row.compatibility
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.activated()
    }
}
