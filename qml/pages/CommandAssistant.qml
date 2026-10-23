import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components" as C

Window {
    id: root
    objectName: "commandAssistantWindow"
    required property var controller
    property int selectedMode: 0
    property var entries: []
    property int selectedIndex: -1
    readonly property int entryCount: entries.length
    readonly property var selectedEntry: selectedIndex>=0 && selectedIndex<entries.length ? entries[selectedIndex] : ({})
    readonly property bool canFill: controller && controller.active && !controller.inputBusy && controller.mode===system.currentIndex
    width: 760; height: 580; minimumWidth: 700; minimumHeight: 540
    title: "指令辅助 · 填入后自行回车执行"
    color: C.Theme.background
    function categories() {
        const result=["全部"]
        if(controller)for(const item of controller.commands(system.currentIndex,"",""))
            if(result.indexOf(item.category)<0)result.push(item.category)
        return result
    }
    function select(index) {
        selectedIndex=index
        edit.text=index>=0 && index<entries.length ? entries[index].example : ""
    }
    function reload() {
        if(!system || !search || !category || !edit)return
        entries=controller ? controller.commands(system.currentIndex,search.text,category.currentText) : []
        select(entries.length>0?0:-1)
    }
    function showFor(mode) {
        system.currentIndex=mode;search.text="";category.currentIndex=0;reload()
        show();raise();requestActivate()
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 10
        RowLayout {
            Text { text: "指令辅助"; color: C.Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            C.SelectBox { id: system; objectName: "assistantSystem"; Layout.preferredWidth: 200; model: ["Linux · SSH","Windows · PowerShell","Windows · CMD","Android · ADB Shell"]; currentIndex: root.selectedMode; onCurrentIndexChanged: { category.currentIndex=0;root.reload() } }
        }
        RowLayout {
            C.InputBox { id: search; objectName: "commandSearch"; Layout.fillWidth: true; placeholderText: "搜索名称、格式、示例或说明"; onTextChanged: root.reload() }
            C.SelectBox { id: category; objectName: "commandCategory"; Layout.preferredWidth: 110; model: root.categories(); onCurrentTextChanged: root.reload() }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
            C.Panel {
                Layout.preferredWidth: 270; Layout.fillHeight: true
                ListView {
                    id: list; objectName: "commandList"
                    anchors.fill: parent; anchors.margins: 8; clip: true; spacing: 4
                    model: root.entries
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: entry
                        required property var modelData
                        required property int index
                        width: ListView.view.width; height: 64; radius: 7
                        color: root.selectedIndex===index ? C.Theme.accentFill : C.Theme.raised
                        MouseArea { anchors.fill: parent; onClicked: root.select(entry.index) }
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 9; spacing: 5
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 5
                                Text { Layout.fillWidth: true; text: entry.modelData.title; color: C.Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                                Text { Layout.fillWidth: true; text: entry.modelData.category; color: C.Theme.muted; font.pixelSize: 10; elide: Text.ElideRight }
                            }
                            C.ActionButton { text: "填入"; implicitWidth: 55; implicitHeight: 30; leftPadding: 8; rightPadding: 8; enabled: root.canFill; onClicked: { root.select(entry.index);root.controller.fill(edit.text) } }
                        }
                    }
                    Text { anchors.centerIn: parent; visible: root.entryCount===0; text: "没有匹配的指令"; color: C.Theme.muted; font.pixelSize: 12 }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 9
                Text { Layout.fillWidth: true; text: root.selectedEntry.title || "选择左侧指令"; color: C.Theme.text; font.pixelSize: 16; elide: Text.ElideRight }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 10
                        Text { text: "输入格式"; color: C.Theme.muted; font.pixelSize: 11 }
                        TextEdit { Layout.fillWidth: true; text: root.selectedEntry.syntax || ""; color: C.Theme.blue; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; font.family: "Consolas"; font.pixelSize: 13 }
                        Text { Layout.fillWidth: true; text: root.selectedEntry.description || "可通过系统、分类或关键词查找指令。"; color: C.Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; lineHeight: 1.4 }
                        Text { Layout.fillWidth: true; text: system.currentIndex===3 ? "这些命令在设备 Shell 内使用。请先完成 ADB 授权并进入 shell。" : "请确认终端已登录并处于命令提示符；输入会追加到现有内容。"; color: C.Theme.orange; font.pixelSize: 11; wrapMode: Text.Wrap; lineHeight: 1.3 }
                    }
                }
                Text { text: "示例 / 可编辑指令 · 单行"; color: C.Theme.muted; font.pixelSize: 11 }
                C.InputBox { id: edit; objectName: "commandEdit"; Layout.fillWidth: true; font.family: "Consolas"; maximumLength: 4096; placeholderText: "选择模板后修改参数" }
                C.ActionButton { objectName: "applyCommandButton"; Layout.fillWidth: true; text: "填入终端 · 不回车"; primary: true; enabled: root.canFill && edit.text.trim().length>0; onClicked: root.controller.fill(edit.text) }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11; color: C.Theme.muted
            text: !root.controller ? "终端控制器未加载" : root.controller.active && root.controller.mode!==system.currentIndex ? "当前打开的是其他系统终端；切回相同系统可填入，或关闭原终端再重新打开。" : root.controller.status
        }
    }
    Component.onCompleted: reload()
}
