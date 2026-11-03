import QtQuick
import QtQuick.Controls.Basic
import PS2Native

TextField {
    id: field
    implicitHeight: 40
    leftPadding: 40
    rightPadding: text.length > 0 ? 36 : 14
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.alpha(Theme.accent, 0.5)
    selectedTextColor: Theme.text
    font.pixelSize: Theme.bodySize
    selectByMouse: true
    background: Rectangle {
        radius: Theme.radius
        color: field.activeFocus ? Theme.surfaceHigh : Theme.surface
        border.color: field.activeFocus ? Theme.alpha(Theme.cyan, 0.6) : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
    }
    Icon {
        name: "search"
        size: 17
        color: field.activeFocus ? Theme.cyan : Theme.textFaint
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
    }
    IconButton {
        visible: field.text.length > 0
        iconName: "x"
        size: 28
        tooltip: qsTr("Clear search")
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        onClicked: field.clear()
    }
    Keys.onEscapePressed: field.clear()
}
