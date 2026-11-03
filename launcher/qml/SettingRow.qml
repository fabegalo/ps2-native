import QtQuick
import QtQuick.Layouts
import PS2Native

RowLayout {
    id: row
    property string title: ""
    property string description: ""
    property string iconName: ""
    property bool available: true
    default property alias control: controlSlot.data
    spacing: 16

    Icon {
        visible: row.iconName.length > 0
        name: row.iconName
        size: 20
        color: row.available ? Theme.textMuted : Theme.textFaint
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: 2
    }
    Column {
        Layout.fillWidth: true
        spacing: 3
        Text {
            width: parent.width
            text: row.title
            color: row.available ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.bodySize
            font.weight: Font.Medium
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: row.description
            color: Theme.textFaint
            font.pixelSize: Theme.captionSize + 1
            wrapMode: Text.WordWrap
        }
    }
    Item {
        id: controlSlot
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: childrenRect.width
        implicitHeight: childrenRect.height
    }
}
