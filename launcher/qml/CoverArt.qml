import QtQuick
import PS2Native
import PS2Native.Backend

// A user-supplied cover image when one is present next to the disc or in the
// covers folder; otherwise generated artwork from the disc's own metadata.
Item {
    id: art
    property string cover: ""
    property string title: ""
    property string serial: ""
    property string region: ""
    property string media: ""
    property bool dimmed: false
    property real radius: Theme.radius

    readonly property bool hasImage: cover.length > 0 && image.status === Image.Ready
    readonly property var coverPalette: [
        ["#1d47c9", "#081a52"], ["#0f6cb8", "#062448"], ["#3b33c4", "#120d4a"],
        ["#127a9e", "#06283a"], ["#2552b8", "#0a1840"], ["#5130b0", "#170b3f"]
    ]
    function hash(text) {
        let h = 0
        for (let i = 0; i < text.length; ++i) h = (h * 31 + text.charCodeAt(i)) | 0
        return Math.abs(h)
    }
    readonly property var colors: coverPalette[hash(serial + title) % coverPalette.length]

    Rectangle {
        id: generated
        anchors.fill: parent
        visible: !art.hasImage
        radius: art.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: art.colors[0] }
            GradientStop { position: 1.0; color: art.colors[1] }
        }
        border.color: Theme.alpha("#ffffff", 0.08)

        // Case spine
        Rectangle {
            x: 0
            width: Math.max(4, parent.width * 0.045)
            height: parent.height
            radius: art.radius
            color: Theme.alpha("#000000", 0.25)
        }
        // Disc emblem
        Item {
            width: parent.width * 0.62
            height: width
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.horizontalCenterOffset: parent.width * 0.04
            y: parent.height * 0.22
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.alpha("#ffffff", 0.05)
                border.color: Theme.alpha("#9fe7ff", 0.30)
            }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.62
                height: width
                radius: width / 2
                color: "transparent"
                border.color: Theme.alpha("#9fe7ff", 0.16)
            }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.2
                height: width
                radius: width / 2
                color: Theme.alpha("#000000", 0.35)
                border.color: Theme.alpha("#9fe7ff", 0.45)
            }
        }
        Text {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: parent.height * 0.05
            anchors.leftMargin: parent.width * 0.1
            text: art.serial.length > 0 ? art.serial : art.media
            color: Theme.alpha("#ffffff", 0.72)
            font.family: Launcher.monoFamily
            font.pixelSize: Math.max(9, parent.width * 0.065)
            font.weight: Font.DemiBold
        }
        Text {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: parent.height * 0.05
            anchors.rightMargin: parent.width * 0.07
            text: art.region
            color: Theme.alpha("#ffffff", 0.6)
            font.pixelSize: Math.max(9, parent.width * 0.06)
            font.weight: Font.Bold
        }
        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: parent.width * 0.1
            anchors.rightMargin: parent.width * 0.08
            anchors.bottomMargin: parent.height * 0.06
            text: art.title
            color: "#ffffff"
            font.pixelSize: Math.max(11, parent.width * 0.085)
            font.weight: Font.Bold
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            lineHeight: 1.05
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: art.hasImage
        radius: art.radius
        color: Theme.surfaceSunken
        border.color: Theme.alpha("#ffffff", 0.08)
    }
    Image {
        id: image
        anchors.fill: parent
        anchors.margins: 1
        visible: art.hasImage
        source: art.cover
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        smooth: true
        mipmap: true
    }
    Rectangle {
        anchors.fill: parent
        radius: art.radius
        visible: art.dimmed
        color: Theme.alpha("#000000", 0.45)
    }
}
