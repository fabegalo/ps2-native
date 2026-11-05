import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

Flickable {
    id: page
    signal requestAddFolder()
    contentWidth: width
    contentHeight: column.implicitHeight + Theme.pagePadding * 2
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: AppScrollBar {}

    readonly property var budgets: [1000000, 10000000, 50000000, 100000000, 500000000]

    FolderDialog {
        id: coversDialog
        title: qsTr("Folder with cover images")
        onAccepted: Launcher.setCoversFolder(selectedFolder)
    }
    FileDialog {
        id: toolDialog
        title: qsTr("Locate ps2n-boot")
        onAccepted: Launcher.setBootToolOverride(selectedFile)
    }

    ColumnLayout {
        id: column
        x: Theme.pagePadding
        y: Theme.pagePadding
        width: Math.min(page.width - Theme.pagePadding * 2, 980)
        spacing: Theme.gap + 4

        PageHeader {
            Layout.fillWidth: true
            eyebrow: qsTr("Preferences")
            title: qsTr("Settings")
            subtitle: qsTr("Saved automatically. Options for components that are not implemented yet are shown, and locked, so nothing pretends to work.")
        }

        Card {
            Layout.fillWidth: true
            SettingRow {
                anchors.fill: parent
                title: qsTr("Language")
                description: qsTr("Use English or Brazilian Portuguese. Changes apply immediately.")
                iconName: "settings"
                AppComboBox {
                    model: [qsTr("Automatic"), "English", "Português (Brasil)"]
                    currentIndex: Launcher.language === "en" ? 1 : Launcher.language === "pt-BR" ? 2 : 0
                    onActivated: Launcher.language = ["auto", "en", "pt-BR"][currentIndex]
                }
            }
        }
        // Library
        Card {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 18
                SectionTitle { Layout.fillWidth: true; text: qsTr("Library"); iconName: "library" }
                SettingRow {
                    Layout.fillWidth: true
                    title: qsTr("Game folders")
                    description: qsTr("Disc images in these folders (and subfolders) are added to the library. Files stay where they are.")
                    iconName: "folder"
                    AppButton { text: qsTr("Add folder"); iconName: "plus"; compact: true; onClicked: page.requestAddFolder() }
                }
                Repeater {
                    model: Launcher.gameFolders
                    delegate: Rectangle {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: 36
                        implicitHeight: 44
                        radius: Theme.radius
                        color: Theme.surfaceSunken
                        border.color: Theme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 6
                            Icon { name: "folder"; size: 16; color: Theme.textMuted }
                            Text {
                                Layout.fillWidth: true
                                text: parent.parent.modelData
                                color: Theme.text
                                font.family: Launcher.monoFamily
                                font.pixelSize: Theme.captionSize + 1
                                elide: Text.ElideMiddle
                            }
                            IconButton { iconName: "external"; size: 30; tooltip: qsTr("Open folder"); onClicked: Launcher.openFolder(parent.parent.modelData) }
                            IconButton { iconName: "x"; size: 30; tooltip: qsTr("Stop watching (entries stay in the library)"); onClicked: Launcher.removeGameFolder(parent.parent.modelData) }
                        }
                    }
                }
                Text {
                    Layout.leftMargin: 36
                    visible: Launcher.gameFolders.length === 0
                    text: qsTr("No folders yet.")
                    color: Theme.textFaint
                    font.pixelSize: Theme.captionSize + 1
                }
                SettingRow {
                    Layout.fillWidth: true
                    title: qsTr("Scan folders on start")
                    description: qsTr("Look for new disc images in the game folders every time the launcher opens.")
                    iconName: "refresh"
                    AppSwitch { checked: Launcher.scanOnStart; onToggled: Launcher.scanOnStart = checked }
                }
                SettingRow {
                    Layout.fillWidth: true
                    title: qsTr("Cover images")
                    description: Launcher.coversFolder.length > 0
                                 ? qsTr("Covers named after the serial (SLUS-20946.png) are read from %1. An image next to a disc with the same name is also used.").arg(Launcher.coversFolder)
                                 : qsTr("Use your own images: put one next to the disc with the same name, or choose a folder of images named after the serial. Nothing is downloaded.")
                    iconName: "eye"
                    Row {
                        spacing: 8
                        AppButton { visible: Launcher.coversFolder.length > 0; text: qsTr("Clear"); compact: true; variant: "ghost"; onClicked: Launcher.clearCoversFolder() }
                        AppButton { text: qsTr("Choose folder"); compact: true; onClicked: coversDialog.open() }
                    }
                }
                SettingRow {
                    Layout.fillWidth: true
                    title: qsTr("Check the library now")
                    description: qsTr("Identify changed or missing files again and scan the game folders.")
                    iconName: "shield"
                    AppButton { text: qsTr("Rescan"); iconName: "refresh"; compact: true; busy: Launcher.pendingTasks > 0; onClicked: Launcher.rescan() }
                }
            }
        }

        // Diagnostic
        Card {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 18
                SectionTitle { Layout.fillWidth: true; text: qsTr("Boot diagnostic"); iconName: "terminal" }
                SettingRow {
                    Layout.fillWidth: true
                    title: qsTr("ps2n-boot")
                    description: Launcher.bootTool
                    iconName: "file"
                    Row {
                        spacing: 8
                        Badge {
                            text: Launcher.bootToolAvailable ? qsTr("Found") : qsTr("Not found")
                            tone: Launcher.bootToolAvailable ? Theme.success : Theme.danger
                            dot: true
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        AppButton { visible: Launcher.bootToolOverride.length > 0; text: qsTr("Automatic"); compact: true; variant: "ghost"; onClicked: Launcher.clearBootToolOverride() }
                        AppButton { text: qsTr("Locate…"); compact: true; onClicked: toolDialog.open() }
                    }
                }
                SettingRow {
                    Layout.fillWidth: true
                    title: qsTr("Default instruction budget")
                    description: qsTr("How many EE instructions a diagnostic may retire before it stops.")
                    iconName: "clock"
                    AppComboBox {
                        model: page.budgets.map(b => Launcher.formatCount(b))
                        currentIndex: Math.max(0, page.budgets.indexOf(Launcher.diagnosticBudget))
                        onActivated: index => Launcher.diagnosticBudget = page.budgets[index]
                    }
                }
                SettingRow {
                    Layout.fillWidth: true
                    title: qsTr("Full trace")
                    description: qsTr("Record every instruction and data access. Much larger output.")
                    iconName: "list"
                    AppSwitch { checked: Launcher.diagnosticFullTrace; onToggled: Launcher.diagnosticFullTrace = checked }
                }
            }
        }

        // Video
        Card {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 18
                SectionTitle {
                    Layout.fillWidth: true
                    text: qsTr("Video")
                    iconName: "monitor"
                    Badge { text: qsTr("Arrives with the runtime"); tone: Theme.textFaint; iconName: "lock"; small: true }
                }
                SettingRow {
                    Layout.fillWidth: true
                    available: false
                    title: qsTr("Renderer")
                    description: qsTr("GS faithful is the reference for correctness; Native enhances output without changing game logic or effects. Your choice is kept for when rendering exists.")
                    iconName: "layers"
                    ChipGroup {
                        enabled: false
                        opacity: 0.55
                        current: Launcher.renderer
                        model: [{ key: "gs-faithful", label: qsTr("GS faithful") }, { key: "native", label: qsTr("Native") }]
                    }
                }
                SettingRow {
                    Layout.fillWidth: true
                    available: false
                    title: qsTr("Internal resolution, aspect ratio, V-Sync")
                    description: qsTr("Not available: the experimental GS model runs only in diagnostics and is not a validated renderer yet.")
                    iconName: "monitor"
                    Icon { name: "lock"; size: 18; color: Theme.textFaint }
                }
            }
        }

        // Input and audio
        Card {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 18
                SectionTitle {
                    Layout.fillWidth: true
                    text: qsTr("Input, audio & memory cards")
                    iconName: "gamepad"
                    Badge { text: qsTr("Arrives with the runtime"); tone: Theme.textFaint; iconName: "lock"; small: true }
                }
                Repeater {
                    model: Launcher.capabilities.filter(c => c.key === "controller" || c.key === "audio" || c.key === "memory-cards")
                    delegate: SettingRow {
                        required property var modelData
                        Layout.fillWidth: true
                        available: modelData.available
                        title: Theme.capabilityName(modelData)
                        description: Theme.capabilityReason(modelData)
                        iconName: modelData.key === "controller" ? "gamepad" : modelData.key === "audio" ? "speaker" : "memory"
                        Icon { name: "lock"; size: 18; color: Theme.textFaint }
                    }
                }
            }
        }

        // Interface and data
        Card {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 18
                SectionTitle { Layout.fillWidth: true; text: qsTr("Interface & data"); iconName: "settings" }
                SettingRow {
                    Layout.fillWidth: true
                    title: qsTr("Reduce motion")
                    description: qsTr("Turn off hover lifts, spinners and transitions.")
                    iconName: "sparkles"
                    AppSwitch { checked: Launcher.reduceMotion; onToggled: Launcher.reduceMotion = checked }
                }
                SettingRow {
                    Layout.fillWidth: true
                    title: qsTr("Data folder")
                    description: Launcher.dataDir
                    iconName: "folder"
                    AppButton { text: qsTr("Open"); iconName: "external"; compact: true; onClicked: Launcher.openFolder(Launcher.dataDir) }
                }
                InfoRow { Layout.fillWidth: true; Layout.leftMargin: 36; label: qsTr("Library file"); value: Launcher.libraryFile; mono: true }
                InfoRow { Layout.fillWidth: true; Layout.leftMargin: 36; label: qsTr("Settings file"); value: Launcher.settingsFile; mono: true }
            }
        }
        Item { height: 8 }
    }
}
