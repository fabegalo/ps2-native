import QtQuick
import PS2Native

Badge {
    property string compatibility: "pending"
    text: Theme.compatLabel(compatibility)
    tone: Theme.compatColor(compatibility)
    iconName: Theme.compatIcon(compatibility)
}
