import QtQuick
import PS2Native

// Segmented selection: model is a list of { key, label, count? }.
Rectangle {
    id: group
    property var model: []
    property string current: ""
    signal picked(string key)

    implicitHeight: 40
    implicitWidth: row.implicitWidth + 8
    radius: Theme.radius
    color: Theme.surface
    border.color: Theme.border

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: group.model
            delegate: Rectangle {
                id: chip
                required property var modelData
                readonly property bool selected: group.current === modelData.key
                height: 32
                width: chipRow.implicitWidth + 24
                radius: Theme.radius - 3
                color: selected ? Theme.alpha(Theme.accent, 0.22) : chipMouse.containsMouse ? Theme.surfaceHover : "transparent"
                border.color: selected ? Theme.alpha(Theme.accent, 0.5) : "transparent"
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.label
                Accessible.checked: selected
                Row {
                    id: chipRow
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        text: chip.modelData.label
                        color: chip.selected ? Theme.text : Theme.textMuted
                        font.pixelSize: 13
                        font.weight: chip.selected ? Font.DemiBold : Font.Medium
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        visible: chip.modelData.count !== undefined
                        text: chip.modelData.count !== undefined ? chip.modelData.count : ""
                        color: chip.selected ? Theme.cyan : Theme.textFaint
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: chipMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { group.current = chip.modelData.key; group.picked(chip.modelData.key) }
                }
            }
        }
    }
}
