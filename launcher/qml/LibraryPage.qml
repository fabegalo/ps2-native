import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PS2Native
import PS2Native.Backend

Item {
    id: page
    property string selectedId: ""
    signal requestImportDiscs()
    signal requestImportFolder()

    readonly property bool narrow: width < 1240
    readonly property bool showDetails: selectedId.length > 0
    readonly property bool gridView: Launcher.libraryView === "grid"
    readonly property var model: Launcher.games

    function select(id) { selectedId = id }

    Connections {
        target: Launcher
        function onEntryRemoved(id) { if (id === page.selectedId) page.selectedId = "" }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pagePadding
        anchors.rightMargin: page.showDetails && !page.narrow ? details.width + Theme.pagePadding + Theme.gap : Theme.pagePadding
        spacing: 20

        PageHeader {
            Layout.fillWidth: true
            eyebrow: qsTr("Your discs")
            title: qsTr("Library")
            subtitle: Launcher.gameCount === 0 ? qsTr("Disc images you own, referenced where they are.")
                      : [(Launcher.gameCount === 1 ? qsTr("1 disc image") : qsTr("%1 disc images").arg(Launcher.formatCount(Launcher.gameCount))),
                         qsTr("%1 identified as PlayStation 2").arg(Launcher.formatCount(Launcher.identifiedGameCount)),
                         (Launcher.issueGameCount === 1 ? qsTr("1 needs attention") : qsTr("%1 need attention").arg(Launcher.formatCount(Launcher.issueGameCount)))].join(" · ")
            AppButton {
                text: qsTr("Add folder")
                iconName: "folder"
                available: Launcher.loaded
                tooltip: qsTr("Watch a folder: its disc images are added now and on every start.")
                onClicked: page.requestImportFolder()
            }
            AppButton {
                text: qsTr("Add discs")
                iconName: "plus"
                variant: "primary"
                available: Launcher.loaded
                onClicked: page.requestImportDiscs()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: Launcher.gameCount > 0
            spacing: 12
            SearchField {
                id: search
                Layout.fillWidth: true
                Layout.maximumWidth: 420
                placeholderText: qsTr("Search title, serial, region…")
                onTextChanged: Launcher.games.search = text
            }
            ChipGroup {
                current: Launcher.games.filter
                model: [
                    { key: "all", label: qsTr("All"), count: Launcher.gameCount },
                    { key: "ps2", label: qsTr("PS2"), count: Launcher.identifiedGameCount },
                    { key: "issues", label: qsTr("Issues"), count: Launcher.issueGameCount },
                    { key: "favorites", label: qsTr("Favorites") }
                ]
                onPicked: key => Launcher.games.filter = key
            }
            Item { Layout.fillWidth: true }
            AppComboBox {
                implicitWidth: 170
                model: [qsTr("Sort: Title"), qsTr("Sort: Recently added"), qsTr("Sort: Region")]
                readonly property var keys: ["title", "recent", "region"]
                currentIndex: keys.indexOf(Launcher.games.sortMode)
                onActivated: index => Launcher.games.sortMode = keys[index]
            }
            Row {
                spacing: 2
                IconButton {
                    iconName: "grid"
                    tooltip: qsTr("Cover grid")
                    checked: page.gridView
                    onClicked: Launcher.libraryView = "grid"
                }
                IconButton {
                    iconName: "list"
                    tooltip: qsTr("Detailed list")
                    checked: !page.gridView
                    onClicked: Launcher.libraryView = "list"
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: grid
                anchors.fill: parent
                visible: page.gridView && Launcher.games.count > 0
                clip: true
                readonly property int columns: Math.max(2, Math.floor((width + 4) / 184))
                cellWidth: Math.floor(width / columns)
                cellHeight: Math.round((cellWidth - 28) * 1.38) + 96
                model: page.gridView ? Launcher.games : null
                boundsBehavior: Flickable.StopAtBounds
                topMargin: 6
                ScrollBar.vertical: AppScrollBar {}
                reuseItems: true
                delegate: Item {
                    id: cell
                    required property string entryId
                    required property string title
                    required property string serial
                    required property string region
                    required property string cover
                    required property string compatibility
                    required property bool favorite
                    width: grid.cellWidth
                    height: grid.cellHeight
                    GameCard {
                        x: 4
                        width: parent.width - 28
                        height: parent.height - 12
                        entryId: cell.entryId
                        title: cell.title
                        serial: cell.serial
                        region: cell.region
                        cover: cell.cover
                        compatibility: cell.compatibility
                        favorite: cell.favorite
                        selected: page.selectedId === cell.entryId
                        onActivated: page.select(cell.entryId)
                        onOpened: page.select(cell.entryId)
                    }
                }
            }

            ListView {
                id: list
                anchors.fill: parent
                visible: !page.gridView && Launcher.games.count > 0
                clip: true
                spacing: 2
                model: page.gridView ? null : Launcher.games
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                reuseItems: true
                delegate: Item {
                    id: rowItem
                    required property string entryId
                    required property string title
                    required property string serial
                    required property string region
                    required property string cover
                    required property string compatibility
                    required property bool favorite
                    required property string fileName
                    required property string videoMode
                    required property string sizeText
                    width: list.width - 8
                    height: 64
                    GameRow {
                        anchors.fill: parent
                        title: rowItem.title
                        serial: rowItem.serial
                        region: rowItem.region
                        cover: rowItem.cover
                        compatibility: rowItem.compatibility
                        favorite: rowItem.favorite
                        fileName: rowItem.fileName
                        videoMode: rowItem.videoMode
                        sizeText: rowItem.sizeText
                        selected: page.selectedId === rowItem.entryId
                        onActivated: page.select(rowItem.entryId)
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                width: parent.width
                visible: Launcher.loaded && Launcher.gameCount === 0
                iconName: "disc"
                title: qsTr("Your library is empty")
                message: qsTr("Add PlayStation 2 disc images you own (.iso or raw .bin), or a folder that contains them. ps2-native reads the disc's identity and leaves the files where they are.")
                AppButton {
                    text: qsTr("Add folder")
                    iconName: "folder"
                    onClicked: page.requestImportFolder()
                }
                AppButton {
                    text: qsTr("Add discs")
                    iconName: "plus"
                    variant: "primary"
                    onClicked: page.requestImportDiscs()
                }
            }
            EmptyState {
                anchors.centerIn: parent
                width: parent.width
                visible: Launcher.gameCount > 0 && Launcher.games.count === 0
                compact: true
                iconName: "search"
                title: qsTr("No matches")
                message: qsTr("Nothing in your library matches the current search and filter.")
                AppButton {
                    text: qsTr("Clear filters")
                    onClicked: { search.clear(); Launcher.games.filter = "all" }
                }
            }
            Column {
                anchors.centerIn: parent
                visible: !Launcher.loaded
                spacing: 12
                Spinner { size: 32; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: qsTr("Loading library…"); color: Theme.textMuted; font.pixelSize: Theme.bodySize }
            }
        }
    }

    // Details: docked on wide windows, overlay on narrow ones.
    Rectangle {
        anchors.fill: parent
        color: Theme.alpha(Theme.bg, 0.6)
        visible: page.showDetails && page.narrow
        MouseArea { anchors.fill: parent; onClicked: page.selectedId = "" }
    }
    GameDetails {
        id: details
        width: Math.min(400, page.width - 48)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: page.narrow ? 16 : Theme.pagePadding
        visible: page.showDetails
        entryId: page.selectedId
        onClosed: page.selectedId = ""
    }
}
