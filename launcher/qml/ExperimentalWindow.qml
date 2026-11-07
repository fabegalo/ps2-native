import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import PS2Native.Backend

Window {
    id: panel
    title: Experimental.originalBoot ? qsTr("ps2-native — Game session") : "ps2-native — Modo de Teste Experimental"
    width: 1100; height: 760
    minimumWidth: 800; minimumHeight: 620
    color: "#101621"
    onClosing: Experimental.stop()
    FileDialog {
        id: manifestDialog
        title: "Manifesto experimental local"
        nameFilters: ["Manifestos (*.json)"]
        onAccepted: Experimental.selectManifest(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: "Novo checkpoint seguro (não sobrescreve arquivos)"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Checkpoint (*.ps2ncp)"]
        onAccepted: Experimental.save(selectedFile)
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 14; spacing: 8
        Label {
            Layout.fillWidth: true
            text: Experimental.originalBoot ? qsTr("Original BIOS boot — reference interpreter, software GS; compatibility incomplete") : "Modo de Teste Experimental — checkpoint verificado, separado do boot original"
            color: "#f4c36a"; font.bold: true; wrapMode: Text.Wrap
        }
        RowLayout {
            visible: !Experimental.originalBoot
            Layout.fillWidth: true
            TextField {
                Layout.fillWidth: true; enabled: !Experimental.active
                text: Experimental.manifest
                placeholderText: "Caminho do manifesto JSON local"
                onEditingFinished: Experimental.manifest = text
            }
            Button { text: "Abrir…"; enabled: !Experimental.active; onClicked: manifestDialog.open() }
        }
        CheckBox {
            id: reviewed
            visible: !Experimental.originalBoot
            text: "Autorizo a troca revisada de runtime, preservando o cenário e as dependências do checkpoint"
            enabled: !Experimental.active
            palette.windowText: "#dfe8f7"
        }
        RowLayout {
            Button { text: qsTr("Open checkpoint manifest…"); visible: Experimental.originalBoot; enabled: !Experimental.active; onClicked: manifestDialog.open() }
            Button { text: "Iniciar"; visible: !Experimental.originalBoot; enabled: !Experimental.active; onClicked: { Experimental.begin(reviewed.checked); video.forceActiveFocus() } }
            Button { text: "Pausar"; enabled: Experimental.active && !Experimental.paused; onClicked: Experimental.pause() }
            Button { text: "Continuar"; enabled: Experimental.paused; onClicked: { Experimental.resume(); video.forceActiveFocus() } }
            Button { text: "Parar"; enabled: Experimental.active; onClicked: Experimental.stop() }
            Button { text: "Salvar checkpoint…"; visible: !Experimental.originalBoot; enabled: Experimental.paused; onClicked: saveDialog.open() }
            Label { text: "Setas • Enter = Start • Z = Cross • X = Circle • Backspace = Select"; color: "#dfe8f7"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
        ComboBox {
            id: aspect
            model: ["Tela 4:3", "Pixels GS (campo nativo)"]
            onActivated: video.forceActiveFocus()
        }
        GameView {
            id: video
            Layout.fillWidth: true; Layout.fillHeight: true
            session: Experimental
            nativePixels: aspect.currentIndex === 1
            focus: true
            MouseArea { anchors.fill: parent; onPressed: video.forceActiveFocus() }
        }
        Label {
            text: "Campos GS reais na proporção selecionada, sem filtro ou mistura entre campos. Clique na imagem para controlar."
            color: "#aab8cd"; Layout.fillWidth: true; wrapMode: Text.Wrap
        }
        Label { text: Experimental.metrics; color: "#a8d6ff"; font.family: "monospace"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
        ScrollView {
            id: statusScroll
            Layout.fillWidth: true; Layout.preferredHeight: 95; clip: true
            contentWidth: availableWidth
            TextArea { width: statusScroll.availableWidth; text: Experimental.status; readOnly: true; wrapMode: Text.Wrap; color: "#f4c36a"; font.pixelSize: 12; background: Rectangle { color: "#182130" } }
        }
    }
}
