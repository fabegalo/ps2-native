import QtQuick
import PS2Native

// Line icon from the launcher's icon provider, tinted and sized in logical pixels.
Image {
    id: icon
    property string name: ""
    property color color: Theme.text
    property real size: 20
    property real stroke: 1.75

    width: size
    height: size
    visible: name.length > 0
    fillMode: Image.PreserveAspectFit
    smooth: true
    cache: true
    sourceSize: Qt.size(Math.ceil(size * Screen.devicePixelRatio), Math.ceil(size * Screen.devicePixelRatio))
    source: name.length > 0 ? "image://icon/" + name + "/" + Theme.hex(color) + "/" + stroke : ""
}
