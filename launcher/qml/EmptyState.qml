import QtQuick
import QtQuick.Layouts
import PS2Native

Item {
    id: root
    property string iconName: "disc"
    property string title: ""
    property string message: ""
    property bool compact: false
    default property alias actions: actionRow.data
    implicitHeight: column.implicitHeight + (compact ? 24 : 64)
    implicitWidth: 480

    Column {
        id: column
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 520)
        spacing: root.compact ? 10 : 14

        Item {
            width: root.compact ? 64 : 96
            height: width
            anchors.horizontalCenter: parent.horizontalCenter
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.alpha(Theme.accent, 0.22) }
                    GradientStop { position: 1; color: Theme.alpha(Theme.accent, 0.04) }
                }
                border.color: Theme.alpha(Theme.accent, 0.35)
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -10
                radius: width / 2
                color: "transparent"
                border.color: Theme.alpha(Theme.cyan, 0.08)
            }
            Icon {
                anchors.centerIn: parent
                name: root.iconName
                size: root.compact ? 28 : 42
                stroke: 1.5
                color: Theme.cyan
            }
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.title
            color: Theme.text
            font.pixelSize: root.compact ? Theme.subtitleSize : Theme.titleSize
            font.weight: Font.Bold
            wrapMode: Text.WordWrap
            topPadding: 6
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: text.length > 0
            text: root.message
            color: Theme.textMuted
            font.pixelSize: Theme.bodySize
            lineHeight: 1.25
            wrapMode: Text.WordWrap
        }
        Row {
            id: actionRow
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            topPadding: 8
        }
    }
}
