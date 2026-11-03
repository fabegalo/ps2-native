import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

Rectangle {
    id: panel
    property string entryId: ""
    property string tab: "overview"
    property var entry: ({})
    property var profiles: []

    function refresh() {
        entry = entryId.length > 0 ? Launcher.entry(entryId) : ({})
        profiles = entryId.length > 0 ? Launcher.profilesFor(entryId) : []
    }
    onEntryIdChanged: refresh()
    Connections {
        target: Launcher
        function onEntryChanged(id) { if (id === panel.entryId) panel.refresh() }
        function onActiveBiosChanged() { panel.refresh() }
    }

    readonly property var info: entry.info || ({})
    readonly property var nvmInfo: entry.nvmInfo || ({})
    readonly property bool valid: entry.status === "valid"
    readonly property bool isActive: entry.active === true
    readonly property var selectedProfile: {
        for (let i = 0; i < profiles.length; ++i)
            if (profiles[i].id === entry.consoleProfile) return profiles[i]
        return null
    }

    color: Theme.surface
    radius: Theme.radiusLarge
    border.color: Theme.border

    FileDialog {
        id: nvmDialog
        title: qsTr("Attach the NVM of this console")
        nameFilters: [qsTr("NVM images (*.nvm *.bin)"), qsTr("All files (*)")]
        onAccepted: Launcher.attachNvm(panel.entryId, selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 18
        visible: panel.entryId.length > 0

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                radius: Theme.radius + 4
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.alpha(Theme.accent, 0.45) }
                    GradientStop { position: 1; color: Theme.alpha(Theme.accentDeep, 0.25) }
                }
                border.color: Theme.alpha(Theme.cyan, 0.4)
                Icon { anchors.centerIn: parent; name: "chip"; size: 28; color: Theme.text }
            }
            Column {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    width: parent.width
                    text: panel.entry.title || ""
                    color: Theme.text
                    font.pixelSize: Theme.titleSize
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    Badge { text: Theme.statusLabel(panel.entry.status || "pending"); tone: Theme.statusColor(panel.entry.status || "pending"); dot: true }
                    Badge { visible: panel.valid; text: qsTr("Version %1").arg(panel.info.version); tone: Theme.textMuted }
                    Badge { visible: panel.valid; text: panel.info.region || ""; tone: Theme.textMuted }
                    Badge { visible: panel.valid; text: panel.info.console_kind || ""; tone: Theme.textMuted }
                    Badge { visible: panel.isActive; text: qsTr("Active BIOS"); tone: Theme.success; iconName: "check" }
                }
            }
            Row {
                spacing: 8
                Layout.alignment: Qt.AlignTop
                AppButton {
                    visible: !panel.isActive
                    text: qsTr("Set as active")
                    iconName: "check"
                    available: panel.valid
                    reason: qsTr("Only a verified BIOS can be active.")
                    onClicked: Launcher.setActiveBios(panel.entryId)
                }
                AppButton {
                    text: qsTr("Boot diagnostic")
                    iconName: "terminal"
                    variant: "primary"
                    available: panel.valid && Launcher.bootToolAvailable
                    reason: !panel.valid ? qsTr("This BIOS was not verified.") : qsTr("ps2n-boot was not found. Set its location in Settings.")
                    onClicked: panel.tab = "diagnostic"
                }
            }
        }

        ChipGroup {
            current: panel.tab
            model: [
                { key: "overview", label: qsTr("Overview") },
                { key: "console", label: qsTr("Console & NVM") },
                { key: "romdir", label: qsTr("ROMDIR"), count: (panel.entry.details || []).length },
                { key: "diagnostic", label: qsTr("Diagnostic") }
            ]
            onPicked: key => panel.tab = key
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: ["overview", "console", "romdir", "diagnostic"].indexOf(panel.tab)

            // Overview
            Flickable {
                contentHeight: overview.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                ColumnLayout {
                    id: overview
                    width: parent.width - 12
                    spacing: 16
                    Rectangle {
                        Layout.fillWidth: true
                        visible: (panel.entry.error || "").length > 0
                        implicitHeight: errorText.implicitHeight + 28
                        radius: Theme.radius
                        color: Theme.alpha(Theme.danger, 0.08)
                        border.color: Theme.alpha(Theme.danger, 0.35)
                        Row {
                            id: errorText
                            x: 14; y: 14
                            width: parent.width - 28
                            spacing: 10
                            Icon { name: "alert"; color: Theme.danger; size: 18 }
                            Text {
                                width: parent.width - 30
                                text: qsTr("Not a valid ROM0 dump: %1").arg(panel.entry.error || "")
                                color: Theme.text
                                font.pixelSize: Theme.captionSize + 1
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        visible: (panel.info.warning || "").length > 0
                        implicitHeight: warning.implicitHeight + 28
                        radius: Theme.radius
                        color: Theme.alpha(Theme.warning, 0.08)
                        border.color: Theme.alpha(Theme.warning, 0.3)
                        Row {
                            id: warning
                            x: 14; y: 14
                            width: parent.width - 28
                            spacing: 10
                            Icon { name: "alert"; color: Theme.warning; size: 18 }
                            Text {
                                width: parent.width - 30
                                text: panel.info.warning || ""
                                color: Theme.text
                                font.pixelSize: Theme.captionSize + 1
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                    SectionTitle { Layout.fillWidth: true; text: qsTr("Identity"); iconName: "shield" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        InfoRow { Layout.fillWidth: true; label: qsTr("ROMVER"); value: panel.info.romver || ""; mono: true; copyable: true }
                        InfoRow { Layout.fillWidth: true; label: qsTr("Version"); value: panel.info.version || "" }
                        InfoRow { Layout.fillWidth: true; label: qsTr("Region"); value: panel.info.region ? panel.info.region + " (" + panel.info.region_code + ")" : "" }
                        InfoRow { Layout.fillWidth: true; label: qsTr("Console kind"); value: panel.info.console_kind || "" }
                        InfoRow { Layout.fillWidth: true; label: qsTr("Build date"); value: panel.info.build_date || "" }
                        InfoRow { Layout.fillWidth: true; label: qsTr("ROMDIR files"); value: panel.info.romdir_files || "" }
                        InfoRow { Layout.fillWidth: true; label: qsTr("SHA-256"); value: panel.info.sha256 || ""; mono: true; copyable: true }
                        InfoRow { Layout.fillWidth: true; label: qsTr("Size"); value: panel.entry.sizeText || "" }
                        InfoRow { Layout.fillWidth: true; label: qsTr("File"); value: panel.entry.path || ""; mono: true; copyable: true }
                        InfoRow { Layout.fillWidth: true; label: qsTr("Added"); value: panel.entry.added || "" }
                    }
                    SectionTitle { Layout.fillWidth: true; text: qsTr("Original system"); iconName: "globe" }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("The launcher manages your firmware; it does not replace it. The PS2 Browser, System Configuration and disc boot are the firmware's own screens and appear only once the runtime can execute them.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize + 1
                        wrapMode: Text.WordWrap
                    }
                    Row {
                        spacing: 8
                        AppButton {
                            text: qsTr("Enter PS2 Browser")
                            iconName: "globe"
                            available: false
                            compact: true
                            reason: qsTr("Not available yet: the firmware stops at unmodeled hardware before the Browser.")
                        }
                        AppButton {
                            text: qsTr("Show file")
                            iconName: "folder"
                            compact: true
                            variant: "ghost"
                            onClicked: Launcher.openFolder(panel.entry.path)
                        }
                        AppButton {
                            text: qsTr("Remove")
                            iconName: "trash"
                            compact: true
                            variant: "danger"
                            tooltip: qsTr("Forget this BIOS. The dump itself is not deleted.")
                            onClicked: Launcher.removeEntry(panel.entryId)
                        }
                    }
                    Item { height: 8 }
                }
            }

            // Console & NVM
            Flickable {
                contentHeight: consoleColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                ColumnLayout {
                    id: consoleColumn
                    width: parent.width - 12
                    spacing: 16
                    SectionTitle { Layout.fillWidth: true; text: qsTr("Console profile"); iconName: "chip" }
                    Text {
                        Layout.fillWidth: true
                        text: panel.profiles.length > 0
                              ? qsTr("Board values the firmware reads from hardware (D_ENABLER, IOP revision, MECHACON IDs). Only the profiles shipped with ROMVER %1 are offered.").arg(panel.info.romver || "")
                              : qsTr("The core database has no console profile for ROMVER %1. The diagnostic runs without one: board registers and NVM reads are refused, never invented.").arg(panel.info.romver || "")
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize + 1
                        wrapMode: Text.WordWrap
                    }
                    AppComboBox {
                        id: profileBox
                        Layout.preferredWidth: 320
                        enabled: panel.valid && panel.profiles.length > 0
                        textRole: "label"
                        model: [{ id: "", label: qsTr("None") }].concat(panel.profiles.map(p => ({ id: p.id, label: p.model + " · " + p.mainboard })))
                        currentIndex: {
                            const items = model
                            for (let i = 0; i < items.length; ++i) if (items[i].id === (panel.entry.consoleProfile || "")) return i
                            return 0
                        }
                        onActivated: index => Launcher.setConsoleProfile(panel.entryId, model[index].id)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panel.selectedProfile !== null
                        spacing: 10
                        InfoRow { Layout.fillWidth: true; label: qsTr("Evidence"); value: panel.selectedProfile ? panel.selectedProfile.evidence : "" }
                        InfoRow { Layout.fillWidth: true; label: qsTr("D_ENABLER"); value: panel.selectedProfile ? panel.selectedProfile.dEnabler : ""; mono: true }
                        InfoRow { Layout.fillWidth: true; label: qsTr("Measured on"); value: panel.selectedProfile ? panel.selectedProfile.source : "" }
                        InfoRow { Layout.fillWidth: true; label: qsTr("IOP revision"); value: panel.selectedProfile ? panel.selectedProfile.iopRevision : ""; mono: true }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                    SectionTitle {
                        Layout.fillWidth: true
                        text: qsTr("MECHACON NVM")
                        iconName: "key"
                        Badge {
                            visible: (panel.entry.nvmStatus || "").length > 0
                            text: Theme.statusLabel(panel.entry.nvmStatus || "")
                            tone: Theme.statusColor(panel.entry.nvmStatus || "")
                            dot: true
                            small: true
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("The 1 KiB EEPROM of your console (configuration, model name, IDs). It is console-specific user data: dump it from the same console as the BIOS. It is validated against the selected profile.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize + 1
                        wrapMode: Text.WordWrap
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: (panel.entry.nvmPath || "").length > 0
                        spacing: 10
                        InfoRow { Layout.fillWidth: true; label: qsTr("File"); value: panel.entry.nvmPath || ""; mono: true }
                        InfoRow { Layout.fillWidth: true; label: qsTr("Model name"); value: panel.nvmInfo.model_name || "" }
                        InfoRow { Layout.fillWidth: true; label: qsTr("Layout"); value: panel.nvmInfo.layout || "" }
                        InfoRow { Layout.fillWidth: true; label: qsTr("Config checksums"); value: panel.nvmInfo.checksums || ""; mono: true }
                        InfoRow { Layout.fillWidth: true; label: qsTr("SHA-256"); value: panel.nvmInfo.sha256 || ""; mono: true; copyable: true }
                        InfoRow {
                            Layout.fillWidth: true
                            label: panel.entry.nvmStatus === "pending" ? qsTr("Waiting") : qsTr("Problem")
                            value: panel.entry.nvmError || ""
                            valueColor: panel.entry.nvmStatus === "pending" ? Theme.warning : Theme.danger
                        }
                    }
                    Row {
                        spacing: 8
                        AppButton {
                            text: (panel.entry.nvmPath || "").length > 0 ? qsTr("Replace NVM") : qsTr("Attach NVM")
                            iconName: "key"
                            compact: true
                            available: panel.valid
                            reason: qsTr("Verify the BIOS first.")
                            onClicked: nvmDialog.open()
                        }
                        AppButton {
                            visible: (panel.entry.nvmPath || "").length > 0
                            text: qsTr("Detach")
                            iconName: "x"
                            compact: true
                            variant: "ghost"
                            onClicked: Launcher.detachNvm(panel.entryId)
                        }
                    }
                    Item { height: 8 }
                }
            }

            // ROMDIR
            ColumnLayout {
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Files of this ROM0 in ROMDIR order, as validated by the core (offset, size, EXTINFO version and comment).")
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize + 1
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radius
                    color: Theme.surfaceSunken
                    border.color: Theme.border
                    ListView {
                        id: romdir
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        model: panel.entry.details || []
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: AppScrollBar {}
                        header: RowLayout {
                            width: romdir.width
                            height: 30
                            spacing: 12
                            Repeater {
                                model: [[qsTr("File"), 120], [qsTr("Offset"), 100], [qsTr("Size"), 90], [qsTr("Version"), 70], [qsTr("Comment"), -1]]
                                delegate: Text {
                                    required property var modelData
                                    Layout.preferredWidth: modelData[1] > 0 ? modelData[1] : -1
                                    Layout.fillWidth: modelData[1] < 0
                                    Layout.leftMargin: 6
                                    text: modelData[0]
                                    color: Theme.textFaint
                                    font.pixelSize: Theme.captionSize
                                    font.weight: Font.Bold
                                }
                            }
                        }
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            readonly property var cells: modelData.split("\t")
                            width: romdir.width
                            height: 26
                            radius: 4
                            color: index % 2 === 0 ? Theme.alpha(Theme.surfaceHigh, 0.6) : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                spacing: 12
                                Text { Layout.preferredWidth: 120; Layout.leftMargin: 6; text: parent.parent.cells[0] || ""; color: Theme.text; font.family: Launcher.monoFamily; font.pixelSize: Theme.captionSize + 1 }
                                Text { Layout.preferredWidth: 100; Layout.leftMargin: 6; text: parent.parent.cells[1] || ""; color: Theme.textMuted; font.family: Launcher.monoFamily; font.pixelSize: Theme.captionSize + 1 }
                                Text { Layout.preferredWidth: 90; Layout.leftMargin: 6; text: Launcher.formatCount(Number(parent.parent.cells[2] || 0)); color: Theme.textMuted; font.family: Launcher.monoFamily; font.pixelSize: Theme.captionSize + 1 }
                                Text { Layout.preferredWidth: 70; Layout.leftMargin: 6; text: parent.parent.cells[3] || ""; color: Theme.textMuted; font.family: Launcher.monoFamily; font.pixelSize: Theme.captionSize + 1 }
                                Text { Layout.fillWidth: true; Layout.leftMargin: 6; text: parent.parent.cells[4] || ""; color: Theme.textFaint; font.pixelSize: Theme.captionSize + 1; elide: Text.ElideRight }
                            }
                        }
                    }
                }
            }

            // Diagnostic: fills the tab; scrolls when its result and log need more room.
            Flickable {
                id: diagnosticScroll
                contentHeight: Math.max(height, diagnostic.implicitHeight + 8)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                DiagnosticPanel {
                    id: diagnostic
                    width: diagnosticScroll.width - 12
                    height: diagnosticScroll.contentHeight - 8
                    biosId: panel.entryId
                    biosValid: panel.valid
                    biosTitle: panel.entry.title || ""
                    consoleProfile: panel.entry.consoleProfile || ""
                    nvmStatus: panel.entry.nvmStatus || ""
                }
            }
        }
    }
}
