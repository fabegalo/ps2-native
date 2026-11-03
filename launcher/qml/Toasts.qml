import QtQuick
import PS2Native
import PS2Native.Backend

// Notifications from the backend (imports, validation, errors), newest last.
Item {
    id: toasts
    width: 380
    height: column.height

    ListModel { id: messages }
    property int serial: 0

    function push(level, title, message) {
        messages.append({ uid: ++serial, level: level, title: title, message: message })
        if (messages.count > 4) messages.remove(0)
    }
    function clear() { messages.clear() }
    function remove(uid) {
        for (let i = 0; i < messages.count; ++i)
            if (messages.get(i).uid === uid) { messages.remove(i); return }
    }

    Connections {
        target: Launcher
        function onNotify(level, title, message) { toasts.push(level, title, message) }
    }

    Column {
        id: column
        width: parent.width
        spacing: 10
        anchors.bottom: parent.bottom
        move: Transition { NumberAnimation { properties: "y"; duration: Theme.normal; easing.type: Easing.OutCubic } }
        Repeater {
            model: messages
            delegate: Rectangle {
                id: toast
                required property int uid
                required property string level
                required property string title
                required property string message
                readonly property color tone: level === "error" ? Theme.danger : level === "warning" ? Theme.warning
                                            : level === "success" ? Theme.success : Theme.info
                width: column.width
                height: body.implicitHeight + 28
                radius: Theme.radius + 2
                color: Theme.surfaceHigh
                border.color: Theme.alpha(tone, 0.45)
                opacity: 0
                Component.onCompleted: opacity = 1
                Behavior on opacity { NumberAnimation { duration: Theme.normal } }
                Rectangle {
                    width: 3
                    radius: 2
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    color: toast.tone
                }
                Row {
                    id: body
                    x: 24
                    y: 14
                    spacing: 12
                    width: parent.width - 60
                    Icon {
                        name: toast.level === "error" ? "x-circle" : toast.level === "warning" ? "alert"
                            : toast.level === "success" ? "check-circle" : "info"
                        color: toast.tone
                        size: 20
                    }
                    Column {
                        width: parent.width - 32
                        spacing: 3
                        Text {
                            width: parent.width
                            text: toast.title
                            color: Theme.text
                            font.pixelSize: Theme.bodySize
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            width: parent.width
                            visible: text.length > 0
                            text: toast.message
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize + 1
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            maximumLineCount: 6
                            elide: Text.ElideRight
                        }
                    }
                }
                IconButton {
                    iconName: "x"
                    size: 26
                    tooltip: qsTr("Dismiss")
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8
                    onClicked: toasts.remove(toast.uid)
                }
                Timer {
                    interval: toast.level === "error" ? 12000 : 6000
                    running: !hover.hovered
                    onTriggered: toasts.remove(toast.uid)
                }
                HoverHandler { id: hover }
            }
        }
    }
}
