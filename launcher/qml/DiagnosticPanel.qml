import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

// Runs the existing ps2n-boot diagnostic on this BIOS and shows its output.
ColumnLayout {
    id: panel
    property string biosId: ""
    property string biosTitle: ""
    property bool biosValid: false
    property string consoleProfile: ""
    property string nvmStatus: ""

    readonly property var diag: Launcher.diagnostic
    readonly property bool mine: diag.biosId === biosId
    readonly property bool running: diag.running
    readonly property var summary: diag.summary
    readonly property var budgets: [1000000, 10000000, 50000000, 100000000, 500000000]
    spacing: 14

    function seconds(ms) { return (ms / 1000).toFixed(1) + " s" }
    function outcomeTitle(outcome) {
        if (outcome === "block point") return qsTr("Stopped at unmodeled behavior")
        if (outcome === "instruction budget reached") return qsTr("Instruction budget reached")
        if (outcome === "stop address reached") return qsTr("Stop address reached")
        return outcome
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save diagnostic log")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTr("Text files (*.txt)")]
        onAccepted: Launcher.diagnostic.saveLog(selectedFile)
    }
    Connections {
        target: Launcher.diagnostic
        function onSaved(ok, message) {
            Launcher.notify(ok ? "success" : "error", ok ? qsTr("Log saved") : qsTr("Log not saved"), message)
        }
    }

    // Configuration
    RowLayout {
        Layout.fillWidth: true
        spacing: 14
        Column {
            spacing: 4
            Text { text: qsTr("Instruction budget"); color: Theme.textFaint; font.pixelSize: Theme.captionSize }
            AppComboBox {
                width: 170
                enabled: !panel.running
                model: panel.budgets.map(b => qsTr("%1 instructions").arg(b >= 1000000 ? (b / 1000000) + "M" : b))
                currentIndex: Math.max(0, panel.budgets.indexOf(Launcher.diagnosticBudget))
                onActivated: index => Launcher.diagnosticBudget = panel.budgets[index]
            }
        }
        Column {
            spacing: 4
            Text { text: qsTr("Full trace"); color: Theme.textFaint; font.pixelSize: Theme.captionSize }
            Row {
                spacing: 8
                height: 38
                AppSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: !panel.running
                    checked: Launcher.diagnosticFullTrace
                    onToggled: Launcher.diagnosticFullTrace = checked
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("every instruction and bus access")
                    color: Theme.textFaint
                    font.pixelSize: Theme.captionSize
                }
            }
        }
        Item { Layout.fillWidth: true }
        AppButton {
            visible: !panel.running
            text: panel.mine && panel.diag.state !== "idle" ? qsTr("Run again") : qsTr("Start diagnostic")
            iconName: "play"
            variant: "primary"
            available: panel.biosValid && Launcher.bootToolAvailable && panel.nvmStatus !== "invalid" && panel.nvmStatus !== "pending"
            reason: !panel.biosValid ? qsTr("This BIOS was not verified.")
                  : !Launcher.bootToolAvailable ? qsTr("ps2n-boot was not found. Set its location in Settings.")
                  : qsTr("The attached NVM is not valid for the selected profile.")
            onClicked: Launcher.startDiagnostic(panel.biosId)
        }
        AppButton {
            visible: panel.running
            text: qsTr("Cancel")
            iconName: "stop"
            variant: "danger"
            onClicked: Launcher.diagnostic.cancel()
        }
    }

    // Run status
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: statusRow.implicitHeight + 24
        radius: Theme.radius
        color: Theme.surfaceSunken
        border.color: panel.mine && panel.diag.state === "failed" ? Theme.alpha(Theme.danger, 0.4) : Theme.border
        RowLayout {
            id: statusRow
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12
            Spinner { visible: panel.running && panel.mine; size: 18 }
            Icon {
                visible: !(panel.running && panel.mine)
                name: !panel.mine || panel.diag.state === "idle" ? "terminal" : panel.diag.state === "finished" ? "check-circle"
                    : panel.diag.state === "cancelled" ? "stop" : "x-circle"
                color: !panel.mine || panel.diag.state === "idle" ? Theme.textMuted : panel.diag.state === "finished" ? Theme.success
                     : panel.diag.state === "cancelled" ? Theme.warning : Theme.danger
                size: 18
            }
            Column {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    width: parent.width
                    text: panel.running && !panel.mine ? qsTr("A diagnostic is running for %1").arg(panel.diag.biosTitle)
                        : !panel.mine || panel.diag.state === "idle"
                          ? qsTr("Boots the EE from the reset vector of this ROM0 (interpreter, traced) with %1%2.")
                              .arg(panel.consoleProfile.length > 0 ? panel.consoleProfile : qsTr("no console profile"))
                              .arg(panel.nvmStatus === "valid" ? qsTr(" and its NVM") : "")
                        : panel.diag.state === "running" ? qsTr("Running · %1 · %2 lines").arg(panel.seconds(panel.diag.elapsedMs)).arg(Launcher.formatCount(panel.diag.lineCount))
                        : panel.diag.state === "finished" ? qsTr("Finished in %1 · %2 lines").arg(panel.seconds(panel.diag.elapsedMs)).arg(Launcher.formatCount(panel.diag.lineCount))
                        : panel.diag.errorText
                    color: Theme.text
                    font.pixelSize: Theme.captionSize + 1
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    visible: panel.mine && panel.diag.commandLine.length > 0
                    text: panel.diag.commandLine
                    color: Theme.textFaint
                    font.family: Launcher.monoFamily
                    font.pixelSize: Theme.captionSize
                    elide: Text.ElideMiddle
                }
            }
            IconButton {
                visible: panel.mine && panel.diag.commandLine.length > 0
                iconName: "copy"
                size: 30
                tooltip: qsTr("Copy command line")
                onClicked: Launcher.copyText(panel.diag.commandLine)
            }
        }
    }

    // Result
    Rectangle {
        Layout.fillWidth: true
        visible: panel.mine && panel.summary.finished === true
        implicitHeight: result.implicitHeight + 32
        radius: Theme.radius
        color: Theme.alpha(Theme.accent, 0.06)
        border.color: Theme.alpha(Theme.accent, 0.3)
        ColumnLayout {
            id: result
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: panel.outcomeTitle(panel.summary.outcome || "")
                    color: Theme.text
                    font.pixelSize: Theme.subtitleSize
                    font.weight: Font.Bold
                }
                Badge { text: panel.summary.engine || ""; tone: Theme.textMuted; small: true; visible: text.length > 0 }
            }
            Text {
                Layout.fillWidth: true
                visible: panel.summary.outcome === "block point"
                text: qsTr("Expected at this stage: a block point is the first hardware behavior ps2-native does not model yet. Execution stops there explicitly instead of inventing a value.")
                color: Theme.textMuted
                font.pixelSize: Theme.captionSize + 1
                wrapMode: Text.WordWrap
            }
            InfoRow { Layout.fillWidth: true; label: qsTr("Stopped at"); value: panel.summary.blockPc || ""; mono: true; copyable: true }
            InfoRow { Layout.fillWidth: true; label: qsTr("Category"); value: panel.summary.blockCategory || "" }
            InfoRow { Layout.fillWidth: true; label: qsTr("Reason"); value: panel.summary.blockReason || ""; mono: true }
            InfoRow { Layout.fillWidth: true; label: qsTr("EE instructions"); value: panel.summary.retired ? Launcher.formatCount(panel.summary.retired) : "" }
            InfoRow { Layout.fillWidth: true; label: qsTr("IOP"); value: panel.summary.iop || "" }
            InfoRow { Layout.fillWidth: true; label: qsTr("BIOS output"); value: (panel.summary.sio || []).join("\n"); mono: true }
        }
    }

    // Log
    RowLayout {
        Layout.fillWidth: true
        visible: panel.mine && panel.diag.log.count > 0
        Text {
            Layout.fillWidth: true
            text: qsTr("Output")
            color: Theme.textFaint
            font.pixelSize: Theme.captionSize
            font.weight: Font.Bold
        }
        AppSwitch {
            id: follow
            checked: true
        }
        Text { text: qsTr("Follow"); color: Theme.textFaint; font.pixelSize: Theme.captionSize }
        AppButton {
            text: qsTr("Save log…")
            iconName: "download"
            compact: true
            variant: "ghost"
            available: !panel.running
            reason: qsTr("Wait for the run to finish.")
            onClicked: saveDialog.open()
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 160
        radius: Theme.radius
        color: "#02040a"
        border.color: Theme.border
        visible: panel.mine && panel.diag.log.count > 0
        ListView {
            id: log
            anchors.fill: parent
            anchors.margins: 10
            clip: true
            model: panel.mine ? Launcher.diagnostic.log : null
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
            ScrollBar.horizontal: AppScrollBar {}
            contentWidth: Math.max(width, 1400)
            flickableDirection: Flickable.AutoFlickIfNeeded
            reuseItems: true
            delegate: Item {
                id: logLine
                required property string text
                required property int channel
                width: log.contentWidth
                height: 17
                Text {
                    anchors.fill: parent
                    text: logLine.text
                    color: logLine.channel === 2 ? Theme.danger : logLine.channel === 0 ? Theme.cyan
                         : logLine.text.startsWith("# block point") || logLine.text.startsWith("# result") ? Theme.warning
                         : logLine.text.startsWith("# |") ? Theme.success
                         : logLine.text.startsWith("#") ? Theme.textMuted : Theme.text
                    font.family: Launcher.monoFamily
                    font.pixelSize: 12
                    textFormat: Text.PlainText
                }
            }
            onCountChanged: if (follow.checked) Qt.callLater(positionViewAtEnd)
        }
    }
    EmptyState {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: !panel.mine || panel.diag.log.count === 0
        compact: true
        iconName: "terminal"
        title: qsTr("Boot diagnostic")
        message: qsTr("Runs ps2n-boot in a separate process and reports how far the original firmware gets before it reaches hardware that is not modeled. It does not start the Browser or a game.")
    }
}
