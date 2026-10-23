import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: control
    implicitHeight: 38
    leftPadding: 12
    rightPadding: 12
    color: Theme.text
    placeholderTextColor: Theme.subtle
    selectionColor: Theme.accentFill
    selectedTextColor: Theme.text
    font.pixelSize: 13
    selectByMouse: true
    opacity: enabled ? 1 : 0.45
    background: Rectangle { radius: 8; color: Theme.consoleBg; border.color: control.activeFocus ? Theme.accent : Theme.line }
}
