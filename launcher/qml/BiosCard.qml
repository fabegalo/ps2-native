import QtQuick
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

Rectangle {
    id: card
    property string title: ""
    property string romver: ""
    property string version: ""
    property string region: ""
    property string consoleKind: ""
    property string buildDate: ""
    property string status: "pending"
    property string error: ""
    property bool active: false
    property bool selected: false
    signal activated()

    readonly property bool valid: status === "valid"
    implicitHeight: 92
    radius: Theme.radiusLarge
    color: selected ? Theme.alpha(Theme.accent, 0.12) : mouse.containsMouse ? Theme.surfaceHover : Theme.surface
    border.color: selected ? Theme.alpha(Theme.cyan, 0.55) : Theme.border
    Behavior on color { ColorAnimation { duration: Theme.fast } }
    Accessible.role: Accessible.ListItem
    Accessible.name: title + " " + romver

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14
        Rectangle {
            Layout.preferredWidth: 52
            Layout.preferredHeight: 52
            radius: Theme.radius + 2
            gradient: Gradient {
                GradientStop { position: 0; color: card.valid ? Theme.alpha(Theme.accent, 0.35) : Theme.alpha(Theme.danger, 0.22) }
                GradientStop { position: 1; color: card.valid ? Theme.alpha(Theme.accentDeep, 0.25) : Theme.alpha(Theme.danger, 0.08) }
            }
            border.color: card.valid ? Theme.alpha(Theme.cyan, 0.35) : Theme.alpha(Theme.danger, 0.35)
            Column {
                anchors.centerIn: parent
                spacing: 0
                Icon {
                    visible: !card.valid
                    name: card.status === "pending" ? "clock" : "alert"
                    size: 22
                    color: card.status === "pending" ? Theme.textMuted : Theme.danger
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    visible: card.valid
                    text: card.version
                    color: Theme.text
                    font.pixelSize: 17
                    font.weight: Font.Bold
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    visible: card.valid
                    text: card.consoleKind
                    color: Theme.cyan
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
        Column {
            Layout.fillWidth: true
            spacing: 4
            Text {
                width: parent.width
                text: card.title
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: card.valid ? card.romver : card.status === "pending" ? qsTr("Identifying…") : card.error
                color: card.valid ? Theme.textMuted : card.status === "pending" ? Theme.textFaint : Theme.danger
                font.family: card.valid ? Launcher.monoFamily : Qt.application.font.family
                font.pixelSize: Theme.captionSize + 1
                elide: Text.ElideRight
            }
            Row {
                spacing: 6
                visible: card.valid
                Badge { text: card.region; tone: Theme.textMuted; small: true }
                Badge { text: card.buildDate; tone: Theme.textFaint; small: true }
                Badge { visible: card.active; text: qsTr("Active"); tone: Theme.success; dot: true; small: true }
            }
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: card.activated()
    }
}
