import QtQuick
import PS2Native

Rectangle {
    id: badge
    property string text: ""
    property color tone: Theme.textMuted
    property string iconName: ""
    property bool dot: false
    property bool small: false

    implicitHeight: small ? 20 : 24
    implicitWidth: row.implicitWidth + (small ? 14 : 18)
    radius: height / 2
    color: Theme.alpha(tone, 0.12)
    border.color: Theme.alpha(tone, 0.32)
    border.width: 1

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5
        Rectangle {
            visible: badge.dot
            width: 6
            height: 6
            radius: 3
            color: badge.tone
            anchors.verticalCenter: parent.verticalCenter
        }
        Icon {
            visible: badge.iconName.length > 0
            name: badge.iconName
            color: badge.tone
            size: badge.small ? 12 : 13
            stroke: 2
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: badge.text
            color: badge.tone
            font.pixelSize: badge.small ? 11 : 12
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
