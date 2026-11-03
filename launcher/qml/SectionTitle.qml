import QtQuick
import QtQuick.Layouts
import PS2Native

RowLayout {
    id: section
    property string text: ""
    property string iconName: ""
    property string hint: ""
    default property alias trailing: trailingRow.data
    spacing: 10

    Icon {
        visible: section.iconName.length > 0
        name: section.iconName
        size: 18
        color: Theme.cyan
    }
    Text {
        text: section.text
        color: Theme.text
        font.pixelSize: Theme.subtitleSize
        font.weight: Font.DemiBold
    }
    Text {
        visible: section.hint.length > 0
        text: section.hint
        color: Theme.textFaint
        font.pixelSize: Theme.captionSize
        Layout.fillWidth: true
        elide: Text.ElideRight
    }
    Item { Layout.fillWidth: section.hint.length === 0 }
    Row {
        id: trailingRow
        spacing: 8
    }
}
