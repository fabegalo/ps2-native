import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

Flickable {
    id: page
    signal navigate(int index)
    signal showGame(string id)
    signal requestDiagnostic()
    signal requestImportDiscs()
    signal requestImportBios()

    readonly property var bios: Launcher.activeBios
    readonly property bool biosReady: bios.status === "valid"
    readonly property bool wide: width > 1180
    // Below this width the hero actions move under the status text instead of squeezing it.
    readonly property bool stackHero: width < 1100

    contentWidth: width
    contentHeight: column.implicitHeight + Theme.pagePadding * 2
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: AppScrollBar {}

    ColumnLayout {
        id: column
        x: Theme.pagePadding
        y: Theme.pagePadding
        width: page.width - Theme.pagePadding * 2
        spacing: 24

        // Hero: a banner kept compact so the status cards stay in view, then the system state.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: heroImage.height + heroBar.implicitHeight + heroBar.anchors.topMargin + 26
            radius: Theme.radiusLarge + 2
            color: Theme.surface
            border.color: Theme.alpha(Theme.accent, 0.3)
            clip: true

            Image {
                id: heroImage
                width: parent.width - 2
                x: 1
                y: 1
                height: Math.round(Math.max(150, Math.min(width / 4.2, 280, page.height * 0.34)))
                source: "qrc:/ps2native/assets/banner.webp"
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 1600
                smooth: true
                // Without the image-format plugin the banner cannot be decoded: keep a designed fallback.
                Rectangle {
                    anchors.fill: parent
                    visible: heroImage.status !== Image.Ready
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "#04081a" }
                        GradientStop { position: 0.6; color: "#0b2a7a" }
                        GradientStop { position: 1; color: "#03050b" }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "ps2-native"
                        color: Theme.text
                        font.pixelSize: 56
                        font.weight: Font.Bold
                    }
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.height * 0.35
                    gradient: Gradient {
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 1; color: Theme.surface }
                    }
                }
            }
            GridLayout {
                id: heroBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: heroImage.bottom
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                anchors.topMargin: 6
                columns: page.stackHero ? 1 : 2
                columnSpacing: 24
                rowSpacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 8
                            Layout.preferredHeight: 8
                            radius: 4
                            color: page.biosReady ? Theme.success : Theme.warning
                        }
                        Text {
                            Layout.fillWidth: true
                            text: page.biosReady ? qsTr("System ready for diagnostics") : qsTr("Set up your system")
                            color: Theme.text
                            font.pixelSize: Theme.subtitleSize + 2
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: page.biosReady
                              ? qsTr("BIOS %1 (%2, %3). Run the boot diagnostic to see how far the original firmware gets. The Browser and games cannot be opened from the launcher yet.")
                                  .arg(page.bios.info.version).arg(page.bios.info.region).arg(page.bios.info.romver)
                              : qsTr("Import the BIOS dumped from your own console to run the boot diagnostic. ps2-native never bundles or downloads firmware.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.bodySize
                        lineHeight: 1.15
                        wrapMode: Text.WordWrap
                    }
                }
                Row {
                    Layout.alignment: page.stackHero ? Qt.AlignLeft : (Qt.AlignRight | Qt.AlignVCenter)
                    spacing: 10
                    AppButton {
                        text: qsTr("Enter PS2 Browser")
                        iconName: "globe"
                        available: false
                        reason: qsTr("The original firmware does not reach the Browser yet. Run the boot diagnostic to see where it stops.")
                        visible: page.wide
                    }
                    AppButton {
                        text: page.biosReady ? qsTr("Run boot diagnostic") : qsTr("Import BIOS")
                        iconName: page.biosReady ? "terminal" : "chip"
                        variant: page.biosReady ? "secondary" : "primary"
                        available: !page.biosReady || Launcher.bootToolAvailable
                        reason: qsTr("ps2n-boot was not found next to the launcher. Set its location in Settings.")
                        onClicked: page.biosReady ? page.requestDiagnostic() : page.requestImportBios()
                    }
                    AppButton {
                        text: qsTr("Open library")
                        iconName: "library"
                        variant: page.biosReady ? "primary" : "secondary"
                        onClicked: page.navigate(1)
                    }
                }
            }
        }

        // Status cards
        GridLayout {
            Layout.fillWidth: true
            columns: page.width > 1060 ? 3 : 1
            columnSpacing: Theme.gap
            rowSpacing: Theme.gap

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 14
                    SectionTitle {
                        Layout.fillWidth: true
                        text: qsTr("System")
                        iconName: "chip"
                        Badge {
                            text: page.biosReady ? qsTr("Verified") : qsTr("Not configured")
                            tone: page.biosReady ? Theme.success : Theme.warning
                            dot: true
                            small: true
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !page.biosReady
                        text: qsTr("No active BIOS. Import a ROM0 dump from your own console: the core verifies it by SHA-256 and ROMVER and references it where it is.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.bodySize - 1
                        lineHeight: 1.15
                        wrapMode: Text.WordWrap
                    }
                    InfoRow {
                        Layout.fillWidth: true
                        labelWidth: 96
                        label: qsTr("BIOS")
                        value: page.biosReady ? qsTr("%1 · version %2 · %3").arg(page.bios.title).arg(page.bios.info.version).arg(page.bios.info.console_kind) : ""
                    }
                    InfoRow {
                        Layout.fillWidth: true
                        labelWidth: 96
                        label: qsTr("ROMVER")
                        value: page.biosReady ? page.bios.info.romver : ""
                        mono: true
                    }
                    InfoRow {
                        Layout.fillWidth: true
                        labelWidth: 96
                        label: qsTr("Console")
                        value: page.biosReady ? (page.bios.consoleProfile || qsTr("No profile (board registers refuse reads)")) : ""
                    }
                    InfoRow {
                        Layout.fillWidth: true
                        labelWidth: 96
                        label: qsTr("NVM")
                        value: !page.biosReady ? "" : page.bios.nvmStatus === "valid" ? qsTr("%1 · validated").arg(page.bios.nvmInfo.model_name)
                             : page.bios.nvmStatus.length > 0 ? Theme.statusLabel(page.bios.nvmStatus) : qsTr("Not attached")
                        valueColor: page.bios.nvmStatus === "invalid" ? Theme.danger : Theme.text
                    }
                    Item { Layout.fillHeight: true }
                    Row {
                        spacing: 8
                        AppButton {
                            visible: !page.biosReady
                            text: qsTr("Import BIOS")
                            iconName: "plus"
                            compact: true
                            onClicked: page.requestImportBios()
                        }
                        AppButton {
                            text: qsTr("Manage BIOS")
                            iconName: "chevron-right"
                            variant: "ghost"
                            compact: true
                            onClicked: page.navigate(2)
                        }
                    }
                }
            }
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 14
                    SectionTitle {
                        Layout.fillWidth: true
                        text: qsTr("Library")
                        iconName: "library"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        StatTile {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 1
                            value: Launcher.formatCount(Launcher.gameCount)
                            label: qsTr("Disc images")
                        }
                        StatTile {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 1
                            value: Launcher.formatCount(Launcher.identifiedGameCount)
                            label: qsTr("Identified PS2")
                            tone: Theme.info
                        }
                        StatTile {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 1
                            value: Launcher.formatCount(Launcher.issueGameCount)
                            label: qsTr("Need attention")
                            tone: Launcher.issueGameCount > 0 ? Theme.warning : Theme.textFaint
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Import your discs, prepare verified native code, and select Play. Compatibility is incomplete; unsupported translations and services are reported explicitly.")
                        color: Theme.textFaint
                        font.pixelSize: Theme.captionSize + 1
                        wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                    Row {
                        spacing: 8
                        AppButton {
                            text: qsTr("Add discs")
                            iconName: "plus"
                            compact: true
                            onClicked: page.requestImportDiscs()
                        }
                        AppButton {
                            text: qsTr("Browse")
                            iconName: "chevron-right"
                            variant: "ghost"
                            compact: true
                            onClicked: page.navigate(1)
                        }
                    }
                }
            }
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    SectionTitle {
                        Layout.fillWidth: true
                        text: qsTr("Runtime")
                        iconName: "layers"
                        hint: qsTr("what works today")
                    }
                    Repeater {
                        model: Launcher.capabilities.slice(0, 5)
                        delegate: CapabilityRow {
                            required property var modelData
                            Layout.fillWidth: true
                            key: modelData.key
                            name: Theme.capabilityName(modelData)
                            reason: Theme.capabilityReason(modelData)
                            available: modelData.available
                            showReason: false
                        }
                    }
                    Item { Layout.fillHeight: true }
                    AppButton {
                        text: qsTr("Details")
                        iconName: "chevron-right"
                        variant: "ghost"
                        compact: true
                        onClicked: page.navigate(4)
                    }
                }
            }
        }

        // Recently added
        Card {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 16
                SectionTitle {
                    Layout.fillWidth: true
                    text: qsTr("Recently added")
                    iconName: "clock"
                    AppButton {
                        visible: Launcher.gameCount > 0
                        text: qsTr("View all")
                        variant: "ghost"
                        compact: true
                        iconName: "chevron-right"
                        onClicked: page.navigate(1)
                    }
                }
                ListView {
                    id: recent
                    visible: Launcher.gameCount > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 250
                    orientation: ListView.Horizontal
                    spacing: 18
                    clip: true
                    model: Launcher.recentGames
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.horizontal: AppScrollBar {}
                    delegate: Item {
                        id: recentItem
                        required property string entryId
                        required property string title
                        required property string serial
                        required property string region
                        required property string cover
                        required property string compatibility
                        required property bool favorite
                        width: 136
                        height: 250
                        GameCard {
                            y: 6
                            width: parent.width
                            height: parent.height - 6
                            entryId: recentItem.entryId
                            title: recentItem.title
                            serial: recentItem.serial
                            region: recentItem.region
                            cover: recentItem.cover
                            compatibility: recentItem.compatibility
                            favorite: recentItem.favorite
                            onActivated: page.showGame(recentItem.entryId)
                            onOpened: page.showGame(recentItem.entryId)
                        }
                    }
                }
                EmptyState {
                    visible: Launcher.gameCount === 0
                    Layout.fillWidth: true
                    compact: true
                    iconName: "disc"
                    title: Launcher.loaded ? qsTr("No discs yet") : qsTr("Loading your library…")
                    message: qsTr("Add disc images you own, or a folder of them. They are referenced in place, never copied.")
                    AppButton {
                        text: qsTr("Add discs")
                        iconName: "plus"
                        variant: "primary"
                        available: Launcher.loaded
                        onClicked: page.requestImportDiscs()
                    }
                }
            }
        }
    }
}
