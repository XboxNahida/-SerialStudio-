import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    property bool primary: false
    property bool danger: false
    implicitHeight: 38
    implicitWidth: Math.max(76, contentItem.implicitWidth + 30)
    hoverEnabled: true
    font.pixelSize: 13
    leftPadding: 15
    rightPadding: 15
    opacity: enabled ? 1 : 0.4
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.primary ? "#092a28" : (control.danger ? Theme.red : Theme.text)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 9
        color: control.primary ? (control.hovered ? Qt.lighter(Theme.accent, 1.12) : Theme.accent)
                               : (control.hovered ? Theme.line : Theme.raised)
        border.color: control.primary ? "transparent" : Theme.line
        scale: control.down ? 0.98 : 1
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
