import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: control
    implicitHeight: 38
    implicitWidth: 160
    font.pixelSize: 13
    leftPadding: 12
    rightPadding: 28
    opacity: enabled ? 1 : 0.45
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text { x: control.width-23; y: 10; text: "⌄"; color: Theme.muted; font.pixelSize: 16 }
    background: Rectangle { radius: 8; color: Theme.consoleBg; border.color: control.activeFocus ? Theme.accent : Theme.line }
    delegate: ItemDelegate {
        required property int index
        required property var modelData
        width: control.width
        implicitHeight: 36
        contentItem: Text {
            text: control.textRole ? modelData[control.textRole] : modelData
            color: index === control.currentIndex ? Theme.accent : Theme.text
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle { color: parent.highlighted ? Theme.raised : Theme.panel }
        highlighted: control.highlightedIndex === index
    }
    popup: Popup {
        y: control.height + 5
        width: control.width
        padding: 5
        implicitHeight: Math.min(contentItem.implicitHeight + 10, 300)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle { radius: 9; color: Theme.panel; border.color: Theme.line }
    }
}
