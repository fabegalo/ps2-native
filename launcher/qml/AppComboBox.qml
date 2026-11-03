import QtQuick
import QtQuick.Controls.Basic
import PS2Native

ComboBox {
    id: control
    implicitHeight: 38
    implicitWidth: 220
    font.pixelSize: Theme.bodySize - 1
    leftPadding: 12
    rightPadding: 34

    contentItem: Text {
        text: control.displayText
        color: control.enabled ? Theme.text : Theme.textFaint
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        name: "chevron-down"
        size: 16
        color: Theme.textMuted
        x: control.width - width - 12
        y: (control.height - height) / 2
    }
    background: Rectangle {
        radius: Theme.radius
        color: control.hovered && control.enabled ? Theme.surfaceHover : Theme.surfaceHigh
        border.color: control.activeFocus || control.popup.visible ? Theme.alpha(Theme.cyan, 0.6) : Theme.border
        opacity: control.enabled ? 1 : 0.5
    }
    delegate: ItemDelegate {
        id: item
        required property var modelData
        required property int index
        width: control.width - 8
        x: 4
        height: 36
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: typeof item.modelData === "object" && item.modelData !== null
                  ? item.modelData[control.textRole] : item.modelData
            color: control.currentIndex === item.index ? Theme.cyan : Theme.text
            font.pixelSize: Theme.bodySize - 1
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: item.highlighted ? Theme.surfaceHover : "transparent"
        }
    }
    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.surfaceHigh
            border.color: Theme.borderStrong
        }
    }
}
