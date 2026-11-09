import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PS2Native.Backend

Window {
    id: panel
    title: qsTr("ps2-native — Game session")
    width: 1100; height: 760
    minimumWidth: 800; minimumHeight: 620
    color: "#101621"
    onClosing: Experimental.stop()
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 14; spacing: 8
        Label {
            Layout.fillWidth: true
            text: qsTr("Native AOT session with selective HLE — compatibility incomplete")
            color: "#f4c36a"; font.bold: true; wrapMode: Text.Wrap
        }
        RowLayout {
            Button { text: "Pausar"; enabled: Experimental.active && !Experimental.paused; onClicked: Experimental.pause() }
            Button { text: "Continuar"; enabled: Experimental.paused; onClicked: { Experimental.resume(); video.forceActiveFocus() } }
            Button { text: "Parar"; enabled: Experimental.active; onClicked: Experimental.stop() }
            Label { text: "WASD / IJKL = analógicos • Setas = direcional • Enter = Start • Z/X/C/V = Cross/Circle/Square/Triangle • Q/E/1/3 = L1/R1/L2/R2"; color: "#dfe8f7"; wrapMode: Text.Wrap; Layout.fillWidth: true }
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
