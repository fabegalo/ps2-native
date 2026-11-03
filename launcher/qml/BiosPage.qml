import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

Item {
    id: page
    property string selectedId: ""
    property alias detailsTab: details.tab
    signal requestImportBios()
    signal requestScanBios()

    function showDiagnostic() {
        if (selectedId.length === 0) selectedId = Launcher.activeBiosId
        details.tab = "diagnostic"
    }
    Connections {
        target: Launcher
        function onActiveBiosChanged() { if (page.selectedId.length === 0) page.selectedId = Launcher.activeBiosId }
        function onEntryRemoved(id) { if (id === page.selectedId) page.selectedId = Launcher.activeBiosId }
    }
    Connections {
        target: Launcher.bioses
        function onCountChanged() {
            if (page.selectedId.length === 0 && biosList.count > 0) page.selectedId = biosList.itemAtIndex(0) ? biosList.itemAtIndex(0).entryId : ""
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pagePadding
        spacing: 20

        PageHeader {
            Layout.fillWidth: true
            eyebrow: qsTr("System firmware")
            title: qsTr("BIOS & console")
            subtitle: qsTr("The firmware and NVM of your own console. Each BIOS runs only with the console profile and NVM that belong to it; ps2-native never bundles, downloads or mixes firmware.")
            AppButton {
                text: qsTr("Scan folder")
                iconName: "folder"
                available: Launcher.loaded
                tooltip: qsTr("Find ROM0 dumps (4 MiB) in a folder. Only valid ones are added.")
                onClicked: page.requestScanBios()
            }
            AppButton {
                text: qsTr("Import BIOS")
                iconName: "plus"
                variant: "primary"
                available: Launcher.loaded
                onClicked: page.requestImportBios()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.gap
            visible: Launcher.biosCount > 0

            ListView {
                id: biosList
                Layout.preferredWidth: page.width > 1250 ? 360 : 300
                Layout.fillHeight: true
                spacing: 10
                clip: true
                model: Launcher.bioses
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                delegate: Item {
                    id: biosItem
                    required property string entryId
                    required property string title
                    required property string romver
                    required property string biosVersion
                    required property string region
                    required property string consoleKind
                    required property string buildDate
                    required property string status
                    required property string error
                    required property bool active
                    width: biosList.width - 6
                    height: 92
                    BiosCard {
                        anchors.fill: parent
                        title: biosItem.title
                        romver: biosItem.romver
                        version: biosItem.biosVersion
                        region: biosItem.region
                        consoleKind: biosItem.consoleKind
                        buildDate: biosItem.buildDate
                        status: biosItem.status
                        error: biosItem.error
                        active: biosItem.active
                        selected: page.selectedId === biosItem.entryId
                        onActivated: page.selectedId = biosItem.entryId
                    }
                    Component.onCompleted: if (page.selectedId.length === 0) page.selectedId = entryId
                }
            }
            BiosDetails {
                id: details
                Layout.fillWidth: true
                Layout.fillHeight: true
                entryId: page.selectedId
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: Launcher.biosCount === 0
            Card {
                anchors.centerIn: parent
                width: Math.min(parent.width, 760)
                padding: 36
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 24
                    EmptyState {
                        Layout.fillWidth: true
                        iconName: "chip"
                        title: Launcher.loaded ? qsTr("No BIOS imported") : qsTr("Loading…")
                        message: qsTr("ps2-native runs the original firmware of your console. Dump the BIOS (ROM0, 4 MiB) and, optionally, the MECHACON NVM from your own PlayStation 2, then import them here.")
                        AppButton {
                            text: qsTr("Scan folder")
                            iconName: "folder"
                            available: Launcher.loaded
                            onClicked: page.requestScanBios()
                        }
                        AppButton {
                            text: qsTr("Import BIOS")
                            iconName: "plus"
                            variant: "primary"
                            available: Launcher.loaded
                            onClicked: page.requestImportBios()
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 16
                        Repeater {
                            model: [
                                { icon: "shield", title: qsTr("Verified"), text: qsTr("ROMDIR, EXTINFO and ROMVER are validated by the core parser; the SHA-256 identifies your dump.") },
                                { icon: "key", title: qsTr("Matched"), text: qsTr("A console profile and NVM are accepted only for the BIOS revision they belong to.") },
                                { icon: "lock", title: qsTr("Private"), text: qsTr("Files stay where they are. Nothing is copied, uploaded or redistributed.") }
                            ]
                            delegate: Column {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                spacing: 6
                                Icon { name: modelData.icon; color: Theme.cyan; size: 20 }
                                Text { text: modelData.title; color: Theme.text; font.pixelSize: Theme.bodySize; font.weight: Font.DemiBold }
                                Text { width: parent.width; text: modelData.text; color: Theme.textFaint; font.pixelSize: Theme.captionSize + 1; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }
            }
        }
    }
}
