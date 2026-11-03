import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

Flickable {
    id: page
    contentWidth: width
    contentHeight: column.implicitHeight + Theme.pagePadding * 2
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: AppScrollBar {}
    readonly property var build: Launcher.buildInfo

    ColumnLayout {
        id: column
        x: Theme.pagePadding
        y: Theme.pagePadding
        width: Math.min(page.width - Theme.pagePadding * 2, 1080)
        spacing: Theme.gap + 4

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(width / 3.0)
            radius: Theme.radiusLarge + 2
            color: Theme.surface
            border.color: Theme.alpha(Theme.accent, 0.3)
            clip: true
            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: "qrc:/ps2native/assets/banner.webp"
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 1600
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: page.width > 1100 ? 2 : 1
            columnSpacing: Theme.gap
            rowSpacing: Theme.gap

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 14
                    SectionTitle { Layout.fillWidth: true; text: qsTr("ps2-native"); iconName: "info" }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Runs the original PlayStation 2 system and games through static AOT recompilation of MIPS R5900 code to native C++, with an explicit, traced interpreter for code that is not proven statically. Fidelity first; performance without trading correctness.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.bodySize - 1
                        wrapMode: Text.WordWrap
                        lineHeight: 1.2
                    }
                    InfoRow { Layout.fillWidth: true; label: qsTr("Version"); value: qsTr("%1 (developer preview)").arg(page.build.version) }
                    InfoRow { Layout.fillWidth: true; label: qsTr("Qt"); value: qsTr("%1 runtime · built with %2").arg(page.build.qtRuntime).arg(page.build.qtBuild) }
                    InfoRow { Layout.fillWidth: true; label: qsTr("Compiler"); value: page.build.compiler }
                    InfoRow { Layout.fillWidth: true; label: qsTr("Architecture"); value: page.build.cpu }
                    InfoRow { Layout.fillWidth: true; label: qsTr("System"); value: page.build.os }
                    InfoRow { Layout.fillWidth: true; label: qsTr("Window system"); value: page.build.platform }
                    Item { Layout.fillHeight: true }
                    AppButton {
                        text: qsTr("Project on GitHub")
                        iconName: "external"
                        compact: true
                        onClicked: Launcher.openUrl("https://github.com/fabegalo/ps2-native")
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
                    SectionTitle { Layout.fillWidth: true; text: qsTr("Runtime status"); iconName: "layers" }
                    Repeater {
                        model: Launcher.capabilities
                        delegate: CapabilityRow {
                            required property var modelData
                            Layout.fillWidth: true
                            key: modelData.key
                            name: modelData.name
                            reason: modelData.reason
                            available: modelData.available
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
                    SectionTitle { Layout.fillWidth: true; text: qsTr("Principles"); iconName: "shield" }
                    Repeater {
                        model: [
                            qsTr("The launcher manages the original system. It never replaces the PS2 Browser, System Configuration or any firmware screen."),
                            qsTr("No BIOS, NVM, game, cover or other proprietary content is bundled or downloaded. Your files are referenced where they are."),
                            qsTr("Each BIOS revision runs only with the console profile and NVM that belong to it."),
                            qsTr("Unmodeled behavior stops explicitly with its cause; nothing is reported as working without evidence.")
                        ]
                        delegate: RowLayout {
                            required property string modelData
                            Layout.fillWidth: true
                            spacing: 10
                            Icon { name: "check"; size: 16; color: Theme.cyan; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2 }
                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                color: Theme.textMuted
                                font.pixelSize: Theme.captionSize + 1
                                wrapMode: Text.WordWrap
                            }
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
                    SectionTitle { Layout.fillWidth: true; text: qsTr("Licenses"); iconName: "file" }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("ps2-native is BSD-3-Clause. The launcher uses the Qt framework (Qt Core, Gui, Qml, Quick, Quick Controls, Svg and image-format plugins) under the GNU LGPL version 3, linked dynamically, so the Qt libraries can be replaced by the user. Qt is a trademark of The Qt Company.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize + 1
                        wrapMode: Text.WordWrap
                        lineHeight: 1.2
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("PlayStation and PlayStation 2 are trademarks of Sony Interactive Entertainment. ps2-native is not affiliated with or endorsed by Sony.")
                        color: Theme.textFaint
                        font.pixelSize: Theme.captionSize
                        wrapMode: Text.WordWrap
                    }
                    Row {
                        spacing: 8
                        AppButton { text: qsTr("Qt licensing"); iconName: "external"; compact: true; variant: "ghost"; onClicked: Launcher.openUrl("https://www.qt.io/licensing/open-source-lgpl-obligations") }
                        AppButton { text: qsTr("Project license"); iconName: "external"; compact: true; variant: "ghost"; onClicked: Launcher.openUrl("https://github.com/fabegalo/ps2-native/blob/main/LICENSE") }
                    }
                }
            }
        }
        Item { height: 8 }
    }
}
