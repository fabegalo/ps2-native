import QtQuick
import QtQuick.Layouts
import PS2Native

RowLayout {
    id: header
    property string title: ""
    property string subtitle: ""
    property string eyebrow: ""
    default property alias actions: actionRow.data
    spacing: 16

    Column {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: 6
        Text {
            visible: header.eyebrow.length > 0
            text: header.eyebrow.toUpperCase()
            color: Theme.cyan
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 1.6
        }
        Text {
            text: header.title
            color: Theme.text
            font.pixelSize: Theme.headlineSize
            font.weight: Font.Bold
        }
        Text {
            visible: header.subtitle.length > 0
            width: parent.width
            text: header.subtitle
            color: Theme.textMuted
            font.pixelSize: Theme.bodySize
            wrapMode: Text.WordWrap
        }
    }
    Row {
        id: actionRow
        Layout.alignment: Qt.AlignBottom
        spacing: 10
    }
}
