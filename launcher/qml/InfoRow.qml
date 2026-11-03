import QtQuick
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

RowLayout {
    id: row
    property string label: ""
    property string value: ""
    property bool mono: false
    property bool copyable: false
    property color valueColor: Theme.text
    property int labelWidth: 132
    visible: value.length > 0
    spacing: 12

    Text {
        Layout.preferredWidth: row.labelWidth
        Layout.alignment: Qt.AlignTop
        text: row.label
        color: Theme.textFaint
        font.pixelSize: Theme.captionSize + 1
        topPadding: 2
    }
    TextEdit {
        Layout.fillWidth: true
        text: row.value
        readOnly: true
        selectByMouse: true
        color: row.valueColor
        selectionColor: Theme.alpha(Theme.accent, 0.5)
        font.pixelSize: row.mono ? Theme.captionSize + 1 : Theme.bodySize - 1
        font.family: row.mono ? Launcher.monoFamily : Qt.application.font.family
        wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
    }
    IconButton {
        visible: row.copyable
        iconName: "copy"
        size: 26
        tooltip: qsTr("Copy")
        Layout.alignment: Qt.AlignTop
        onClicked: Launcher.copyText(row.value)
    }
}
