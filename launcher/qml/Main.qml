import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

ApplicationWindow {
    id: window
    width: initialWindowSize.width
    height: initialWindowSize.height
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: "ps2-native"
    color: Theme.bg
    font.pixelSize: Theme.bodySize

    property int currentPage: 0
    readonly property var pageNames: ["home", "library", "bios", "settings", "about"]
    readonly property string currentPageName: pageNames[currentPage]

    function navigate(index) { currentPage = index }
    function showGame(id) { currentPage = 1; libraryPage.select(id) }

    // Automation hook (tests and screenshots): shows a page, or a page in a given state.
    function automationShow(name) {
        if (name !== "home") toasts.clear()
        switch (name) {
        case "home": currentPage = 0; return true
        case "library": libraryPage.selectedId = ""; currentPage = 1; return true
        case "library-detail": {
            currentPage = 1
            const id = Launcher.games.idAt(0)
            libraryPage.selectedId = id
            return id.length > 0
        }
        case "bios": currentPage = 2; biosPage.detailsTab = "overview"; return true
        case "bios-detail": currentPage = 2; biosPage.detailsTab = "console"; return true
        case "settings": currentPage = 3; return true
        case "about": currentPage = 4; return true
        }
        return false
    }

    FileDialog {
        id: discDialog
        title: qsTr("Add disc images")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Disc images (*.iso *.bin *.img *.cso *.zso *.chd)"), qsTr("All files (*)")]
        onAccepted: Launcher.importUrls(selectedFiles, "disc")
    }
    FolderDialog {
        id: discFolderDialog
        title: qsTr("Add a folder of disc images")
        onAccepted: Launcher.importFolder(selectedFolder, "disc")
    }
    FileDialog {
        id: biosDialog
        title: qsTr("Import BIOS (ROM0 dump)")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("BIOS dumps (*.bin *.rom0 *.rom *.bios)"), qsTr("All files (*)")]
        onAccepted: Launcher.importUrls(selectedFiles, "bios")
    }
    FolderDialog {
        id: biosFolderDialog
        title: qsTr("Scan a folder for BIOS dumps")
        onAccepted: Launcher.importFolder(selectedFolder, "bios")
    }

    // Backdrop: soft blue light behind the content.
    Canvas {
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            let g = ctx.createRadialGradient(width * 0.85, -height * 0.05, 0, width * 0.85, -height * 0.05, Math.max(width, height) * 0.75)
            g.addColorStop(0, Qt.rgba(0.18, 0.42, 1.0, 0.16))
            g.addColorStop(0.5, Qt.rgba(0.10, 0.25, 0.80, 0.05))
            g.addColorStop(1, Qt.rgba(0, 0, 0, 0))
            ctx.fillStyle = g
            ctx.fillRect(0, 0, width, height)
            g = ctx.createRadialGradient(width * 0.25, height * 1.1, 0, width * 0.25, height * 1.1, Math.max(width, height) * 0.6)
            g.addColorStop(0, Qt.rgba(0.22, 0.84, 1.0, 0.06))
            g.addColorStop(1, Qt.rgba(0, 0, 0, 0))
            ctx.fillStyle = g
            ctx.fillRect(0, 0, width, height)
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            currentIndex: window.currentPage
            collapsed: window.width < 1120
            onNavigate: index => window.navigate(index)
        }

        StackLayout {
            id: pages
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: window.currentPage

            HomePage {
                onNavigate: index => window.navigate(index)
                onShowGame: id => window.showGame(id)
                onRequestImportDiscs: discDialog.open()
                onRequestImportBios: biosDialog.open()
            }
            LibraryPage {
                id: libraryPage
                onRequestImportDiscs: discDialog.open()
                onRequestImportFolder: discFolderDialog.open()
            }
            BiosPage {
                id: biosPage
                onRequestImportBios: biosDialog.open()
                onRequestScanBios: biosFolderDialog.open()
            }
            SettingsPage {
                onRequestAddFolder: discFolderDialog.open()
            }
            AboutPage {}
        }
    }

    Toasts {
        id: toasts
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        z: 10
    }

    DropOverlay {
        anchors.fill: parent
        active: drop.containsDrag
        z: 20
    }
    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: event => {
            if (event.hasUrls) {
                Launcher.importUrls(event.urls, "auto")
                event.acceptProposedAction()
            }
        }
    }

    Shortcut { sequence: "Ctrl+1"; onActivated: window.navigate(0) }
    Shortcut { sequence: "Ctrl+2"; onActivated: window.navigate(1) }
    Shortcut { sequence: "Ctrl+3"; onActivated: window.navigate(2) }
    Shortcut { sequence: "Ctrl+4"; onActivated: window.navigate(3) }
    Shortcut { sequence: "Ctrl+5"; onActivated: window.navigate(4) }
    Shortcut { sequences: [StandardKey.Open]; onActivated: discDialog.open() }
}
