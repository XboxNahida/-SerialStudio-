import QtQuick

ActionButton {
    id: root
    property var editor
    text: editor && editor.selectedText.length > 0 ? "复制选区" : "复制全部"
    enabled: !!editor && editor.text.length > 0
    implicitHeight: 30
    focusPolicy: Qt.NoFocus
    onClicked: {
        if (!editor) return
        if (editor.selectedText.length > 0) editor.copy()
        else {
            const position = editor.cursorPosition
            editor.selectAll(); editor.copy(); editor.deselect()
            editor.cursorPosition = position
        }
    }
}
