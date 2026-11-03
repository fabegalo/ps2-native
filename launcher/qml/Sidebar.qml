import QtQuick
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

Rectangle {
    id: sidebar
    property int currentIndex: 0
    property bool collapsed: false
    signal navigate(int index)

    readonly property var pages: [
        { label: qsTr("Home"), icon: "home" },
        { label: qsTr("Library"), icon: "library" },
        { label: qsTr("BIOS"), icon: "chip" },
        { label: qsTr("Settings"), icon: "settings" },
        { label: qsTr("About"), icon: "info" }
    ]

    implicitWidth: collapsed ? 76 : 248
    color: Theme.sidebar
    Behavior on implicitWidth { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.alpha(Theme.accent, 0.35) }
            GradientStop { position: 0.4; color: Theme.border }
            GradientStop { position: 1; color: Theme.border }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: sidebar.collapsed ? 12 : 16
        anchors.topMargin: 22
        spacing: 6

        // Brand
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 22
            Layout.leftMargin: sidebar.collapsed ? 0 : 4
            spacing: 12
            Image {
                source: "qrc:/ps2native/assets/app-icon.svg"
                sourceSize: Qt.size(76, 76)
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                Layout.alignment: sidebar.collapsed ? Qt.AlignHCenter : Qt.AlignLeft
                Layout.fillWidth: sidebar.collapsed
                fillMode: Image.PreserveAspectFit
            }
            Column {
                visible: !sidebar.collapsed
                Layout.fillWidth: true
                spacing: 1
                Text {
                    text: "ps2-native"
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.Bold
                    font.letterSpacing: 0.2
                }
                Text {
                    text: qsTr("Desktop launcher")
                    color: Theme.textFaint
                    font.pixelSize: Theme.captionSize
                }
            }
        }

        Repeater {
            model: sidebar.pages
            delegate: NavItem {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                label: modelData.label
                iconName: modelData.icon
                collapsed: sidebar.collapsed
                active: sidebar.currentIndex === index
                badge: index === 1 && Launcher.gameCount > 0 ? Launcher.formatCount(Launcher.gameCount) : ""
                onClicked: sidebar.navigate(index)
            }
        }

        Item { Layout.fillHeight: true }

        // Background work
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            visible: Launcher.pendingTasks > 0
            radius: Theme.radius
            color: Theme.alpha(Theme.accent, 0.08)
            border.color: Theme.alpha(Theme.accent, 0.25)
            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: sidebar.collapsed ? (parent.width - 16) / 2 : 12
                spacing: 10
                Spinner { size: 16; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    visible: !sidebar.collapsed
                    width: sidebar.width - 80
                    text: Launcher.activityText
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    font.pixelSize: Theme.captionSize
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // Active system
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: sidebar.collapsed ? 52 : 64
            radius: Theme.radius
            color: biosMouse.containsMouse ? Theme.surfaceHover : Theme.surface
            border.color: Theme.border
            readonly property var bios: Launcher.activeBios
            readonly property bool ready: bios.status === "valid"
            Rectangle {
                id: statusDot
                width: 8
                height: 8
                radius: 4
                color: parent.ready ? Theme.success : Theme.warning
                anchors.left: parent.left
                anchors.leftMargin: sidebar.collapsed ? (parent.width - 8) / 2 : 14
                anchors.verticalCenter: parent.verticalCenter
            }
            Column {
                visible: !sidebar.collapsed
                anchors.left: statusDot.right
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    width: parent.width
                    text: parent.parent.ready ? qsTr("BIOS %1 · %2").arg(parent.parent.bios.info.version).arg(parent.parent.bios.info.region)
                                              : qsTr("No active BIOS")
                    color: Theme.text
                    font.pixelSize: Theme.captionSize + 1
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: parent.parent.ready ? (parent.parent.bios.consoleProfile || qsTr("No console profile"))
                                              : qsTr("Import your console's BIOS")
                    color: Theme.textFaint
                    font.pixelSize: Theme.captionSize
                    elide: Text.ElideRight
                }
            }
            MouseArea {
                id: biosMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: sidebar.navigate(2)
            }
        }
        Text {
            visible: !sidebar.collapsed
            Layout.topMargin: 6
            Layout.leftMargin: 4
            text: qsTr("v%1 · developer preview").arg(Launcher.buildInfo.version)
            color: Theme.textFaint
            font.pixelSize: 11
        }
    }
}
