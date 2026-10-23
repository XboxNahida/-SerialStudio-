import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root
    property var eventModel
    property bool hex: false
    property bool detailed: false
    property bool protocol: false
    property bool active: false
    readonly property bool available: !!eventModel && typeof eventModel.selectedText === "function"
    readonly property bool hasSelection: available && eventModel.selectedCount > 0
    spacing: 8
    onActiveChanged: if (!active && available) eventModel.clearSelection()
    onEventModelChanged: active = false
    function copySelected() {
        if (!hasSelection) return false
        copyBuffer.text = eventModel.selectedText(hex, detailed, protocol)
        copyBuffer.selectAll(); copyBuffer.copy(); copyBuffer.deselect()
        copyBuffer.text = ""
        return true
    }
    function handleCopy(event) {
        if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier) && hasSelection) {
            event.accepted = copySelected()
        }
    }
    Keys.onPressed: function(event) { handleCopy(event) }
    Connections {
        target: root.eventModel
        ignoreUnknownSignals: true
        function onModelReset() { root.active = false }
    }
    TextEdit {id: copyBuffer;visible:false;textFormat:TextEdit.PlainText}
    ActionButton {
        objectName: "messageSelectionToggle"
        text: root.active ? "结束多选" : "多选复制"
        checkable: true; checked: root.active; enabled: root.available
        implicitHeight: 30
        onClicked: root.active = checked
    }
    ActionButton {
        objectName: "selectAllMessages"; visible: root.active; text: "全选"; implicitHeight: 30
        enabled: root.available && root.eventModel.count > 0
        onClicked: root.eventModel.selectAll()
    }
    ActionButton {
        objectName: "clearSelectedMessages"; visible: root.active; text: "清选"; implicitHeight: 30
        enabled: root.hasSelection; onClicked: root.eventModel.clearSelection()
    }
    ActionButton {
        objectName: "copySelectedMessages"; visible: root.active
        text: "复制选中（" + (root.available ? root.eventModel.selectedCount : 0) + "）"
        primary: true; implicitHeight: 30; enabled: root.hasSelection
        onClicked: root.copySelected()
    }
    Item {Layout.fillWidth:true}
    Text {visible:root.active;text:"Ctrl+C";font.pixelSize:10;color:Theme.subtle}
}
