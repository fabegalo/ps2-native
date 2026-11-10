import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

Rectangle {
    id: panel
    property string entryId: ""
    property var entry: ({})
    signal closed()

    function refresh() { entry = entryId.length > 0 ? Launcher.entry(entryId) : ({}) }
    onEntryIdChanged: refresh()
    Connections {
        target: Launcher
        function onEntryChanged(id) { if (id === panel.entryId) panel.refresh() }
    }

    readonly property var info: entry.info || ({})
    readonly property string compat: entry.compatibility || "pending"

    color: Theme.surface
    radius: Theme.radiusLarge
    border.color: Theme.border

    Flickable {
        anchors.fill: parent
        anchors.margins: 1
        contentHeight: content.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: content
            x: 24
            y: 24
            width: parent.width - 48
            spacing: 18

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Disc details")
                    color: Theme.textFaint
                    font.pixelSize: Theme.captionSize
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                    Layout.fillWidth: true
                }
                IconButton {
                    iconName: "x"
                    size: 30
                    tooltip: qsTr("Close")
                    onClicked: panel.closed()
                }
            }
            CoverArt {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 190
                Layout.preferredHeight: 262
                cover: panel.entry.cover || ""
                title: panel.entry.title || ""
                serial: panel.info.serial || ""
                region: panel.info.region || ""
                media: panel.info.media || ""
                radius: Theme.radius + 2
            }
            Column {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    width: parent.width
                    text: panel.entry.title || ""
                    color: Theme.text
                    font.pixelSize: Theme.titleSize
                    font.weight: Font.Bold
                    wrapMode: Text.WordWrap
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    CompatBadge { compatibility: panel.compat }
                    Badge { visible: (panel.info.region || "").length > 0; text: panel.info.region || ""; tone: Theme.textMuted }
                    Badge { visible: (panel.info.video_mode || "").length > 0; text: panel.info.video_mode || ""; tone: Theme.textMuted }
                    Badge { visible: (panel.info.media || "").length > 0; text: panel.info.media || ""; tone: Theme.textMuted }
                }
            }

            // Compatibility explanation
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: compatText.implicitHeight + 28
                radius: Theme.radius
                color: Theme.alpha(Theme.compatColor(panel.compat), 0.08)
                border.color: Theme.alpha(Theme.compatColor(panel.compat), 0.3)
                Row {
                    id: compatText
                    x: 14
                    y: 14
                    width: parent.width - 28
                    spacing: 10
                    Icon {
                        name: Theme.compatIcon(panel.compat)
                        color: Theme.compatColor(panel.compat)
                        size: 18
                    }
                    Text {
                        width: parent.width - 28
                        text: (panel.entry.error && panel.entry.error.length > 0) ? panel.entry.error : Theme.compatDescription(panel.compat)
                        color: Theme.text
                        font.pixelSize: Theme.captionSize + 1
                        wrapMode: Text.WordWrap
                        lineHeight: 1.2
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                visible: (panel.info.warning || "").length > 0
                implicitHeight: warnText.implicitHeight + 24
                radius: Theme.radius
                color: Theme.alpha(Theme.warning, 0.08)
                border.color: Theme.alpha(Theme.warning, 0.3)
                Text {
                    id: warnText
                    x: 12
                    y: 12
                    width: parent.width - 24
                    text: panel.info.warning || ""
                    color: Theme.warning
                    font.pixelSize: Theme.captionSize + 1
                    wrapMode: Text.WordWrap
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                AppButton {
                    Layout.fillWidth: true
                    text: qsTr("Prepare")
                    available: !Launcher.gameRunning
                    reason: qsTr("Wait for the running game or preparation to finish.")
                    onClicked: Launcher.prepareGame(panel.entryId)
                }
                AppButton {
                    Layout.fillWidth: true
                    text: qsTr("Play")
                    iconName: "play"
                    variant: "primary"
                    available: !Launcher.gameRunning
                    reason: qsTr("Wait for the running game or preparation to finish.")
                    onClicked: Launcher.startGame(panel.entryId)
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    AppButton {
                        Layout.fillWidth: true
                        text: panel.entry.favorite ? qsTr("Favorited") : qsTr("Favorite")
                        iconName: "star"
                        compact: true
                        onClicked: Launcher.setFavorite(panel.entryId, !panel.entry.favorite)
                    }
                    AppButton {
                        Layout.fillWidth: true
                        text: qsTr("Show file")
                        iconName: "folder"
                        compact: true
                        available: panel.compat !== "missing"
                        reason: qsTr("The file is not accessible.")
                        onClicked: Launcher.openFolder(panel.entry.path)
                    }
                    IconButton {
                        iconName: "refresh"
                        tooltip: qsTr("Identify again")
                        onClicked: Launcher.reidentify(panel.entryId)
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("Serial"); value: panel.info.serial || ""; mono: true; copyable: true }
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("Region"); value: panel.info.region ? panel.info.region + " — " + Theme.regionName(panel.info.region) : "" }
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("Platform"); value: panel.info.platform || "" }
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("Video mode"); value: panel.info.video_mode || "" }
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("Disc version"); value: panel.info.disc_version || "" }
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("Boot file"); value: panel.info.boot || ""; mono: true }
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("Volume"); value: panel.info.volume_id || ""; mono: true }
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("Volume date"); value: panel.info.volume_date || "" }
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("Format"); value: panel.info.format || "" }
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("Size"); value: panel.entry.sizeText || "" }
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("Added"); value: panel.entry.added || "" }
                InfoRow { Layout.fillWidth: true; labelWidth: 108; label: qsTr("File"); value: panel.entry.path || ""; mono: true; copyable: true }
            }

            AppButton {
                Layout.fillWidth: true
                text: qsTr("Remove from library")
                iconName: "trash"
                variant: "danger"
                compact: true
                tooltip: qsTr("Forget this entry. The disc image itself is not deleted.")
                onClicked: Launcher.removeEntry(panel.entryId)
            }
        }
    }
}
