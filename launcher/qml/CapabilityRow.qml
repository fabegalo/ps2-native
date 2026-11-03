import QtQuick
import QtQuick.Layouts
import PS2Native

RowLayout {
    id: row
    property string key: ""
    property string name: ""
    property string reason: ""
    property bool available: false
    property bool showReason: true
    spacing: 12

    function iconFor(k) {
        switch (k) {
        case "boot-diagnostic": return "terminal"
        case "browser": return "globe"
        case "disc-boot": return "disc"
        case "controller": return "gamepad"
        case "audio": return "speaker"
        case "memory-cards": return "memory"
        case "gs-faithful": return "monitor"
        case "gs-native": return "sparkles"
        }
        return "info"
    }

    Rectangle {
        Layout.preferredWidth: 34
        Layout.preferredHeight: 34
        Layout.alignment: Qt.AlignTop
        radius: Theme.radius
        color: row.available ? Theme.alpha(Theme.success, 0.12) : Theme.surfaceHigh
        border.color: row.available ? Theme.alpha(Theme.success, 0.35) : Theme.border
        Icon {
            anchors.centerIn: parent
            name: row.iconFor(row.key)
            size: 17
            color: row.available ? Theme.success : Theme.textFaint
        }
    }
    Column {
        Layout.fillWidth: true
        spacing: 2
        Text {
            width: parent.width
            text: row.name
            color: row.available ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.bodySize - 1
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: row.showReason
            text: row.reason
            color: Theme.textFaint
            font.pixelSize: Theme.captionSize
            wrapMode: Text.WordWrap
        }
    }
    Badge {
        Layout.alignment: Qt.AlignTop
        text: row.available ? qsTr("Available") : qsTr("Not yet")
        tone: row.available ? Theme.success : Theme.textFaint
        dot: true
        small: true
    }
}
