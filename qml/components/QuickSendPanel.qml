import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root
    property var repository
    property string scope: "serial"
    property bool detailed: false
    property bool expanded: true
    readonly property var selectedEntry: selection
    property var selection: ({})
    property var allEntries: []
    property var editingEntry: ({})
    property string editingScope: ""
    property string localError: ""
    readonly property var categories: {
        const values=["全部分类"]
        for(const entry of allEntries)if(entry.category && values.indexOf(entry.category)<0)values.push(entry.category)
        return values
    }
    readonly property var filteredEntries: {
        const query=search.text.toLocaleLowerCase().trim()
        const group=category.currentIndex>0 ? category.currentText : ""
        return allEntries.filter(function(entry) {
            return (!group || entry.category===group) && (!query ||
                [entry.title,entry.text,entry.category].join(" ").toLocaleLowerCase().indexOf(query)>=0)
        })
    }
    signal chosen(string text)
    function reload() {
        // 内置收藏是助手的元数据，不作为可编辑的普通发送片段。
        allEntries=repository ? repository.entries(scope).filter(function(entry){return !entry.builtinId && entry.interaction!=="editor-key"}) : []
    }
    function editEntry(entry) {
        editingEntry=entry || ({});editingScope=scope
        titleInput.text=editingEntry.title || "";textInput.text=editingEntry.text || ""
        categoryInput.text=editingEntry.category || "";hexInput.checked=!!editingEntry.hex
        newline.currentIndex=Math.max(0,["","\n","\r\n","\r"].indexOf(editingEntry.newline || ""))
        localError="";editor.open();titleInput.forceActiveFocus()
    }
    function saveEntry() {
        if(!repository || editingScope!==scope)return
        const entry=Object.assign({},editingEntry,{title:titleInput.text,text:textInput.text,
            category:categoryInput.text,hex:hexInput.checked,newline:["","\n","\r\n","\r"][newline.currentIndex]})
        if(repository.put(scope,entry)) {localError="";editor.close();reload()}
        else localError=repository.error || "保存失败，请检查内容"
    }
    function importFrom(url) {
        if(!repository)return false
        const ok=repository.importFile(scope,url);localError=ok ? "" : repository.error
        if(ok)reload()
        return ok
    }
    function exportTo(url) {
        if(!repository)return false
        const ok=repository.exportFile(scope,url);localError=ok ? "" : repository.error;return ok
    }
    onRepositoryChanged: reload()
    onScopeChanged: {
        selection=({});localError=""
        if(search)search.text=""
        if(category)category.currentIndex=0
        if(editor)editor.close()
        reload()
    }
    Component.onCompleted: reload()
    implicitHeight: expanded ? (detailed ? 176 : 140) : 38
    color: Theme.consoleBg;radius: 8;border.color: Theme.line
    Connections { target: root.repository;ignoreUnknownSignals: true;function onChanged() {root.reload()} }
    ColumnLayout {
        anchors.fill: parent;anchors.margins: 6;spacing: 4
        RowLayout {
            Layout.fillWidth: true;spacing: 5
            ActionButton {
                objectName: "quickToggle";text: (root.expanded ? "▾ " : "▸ ")+"快捷内容"
                implicitWidth: 104;implicitHeight: 26;leftPadding: 8;rightPadding: 8
                onClicked: root.expanded=!root.expanded
            }
            Text {
                Layout.fillWidth: true;elide: Text.ElideRight
                text: root.localError || (root.repository && root.repository.error ? root.repository.error : "仅填入 · "+root.allEntries.length+" 条")
                color: root.localError ? Theme.red : Theme.muted;font.pixelSize: 11
            }
            ActionButton {
                objectName: "quickAdd";text: "新增";implicitWidth: 48;implicitHeight: 26
                leftPadding: 7;rightPadding: 7;enabled: !!root.repository
                onClicked: root.editEntry({})
            }
            ActionButton {
                objectName: "quickManage";text: "⋯";implicitWidth: 30;implicitHeight: 26
                leftPadding: 5;rightPadding: 5;enabled: !!root.repository;onClicked: menu.popup()
                Menu {
                    id: menu
                    MenuItem {text: "导入 JSON…";onTriggered: importDialog.open()}
                    MenuItem {text: "导出当前用途…";onTriggered: exportDialog.open()}
                }
            }
        }
        RowLayout {
            visible: root.expanded;Layout.fillWidth: true;spacing: 5
            InputBox {id: search;objectName: "quickSearch";Layout.fillWidth: true;implicitHeight: 28;placeholderText: "搜索名称、内容或分类"}
            SelectBox {id: category;objectName: "quickCategory";Layout.preferredWidth: 115;implicitHeight: 28;model: root.categories}
        }
        ListView {
            id: list;objectName: "quickList";visible: root.expanded
            Layout.fillWidth: true;Layout.fillHeight: true;clip: true;spacing: 3
            model: root.filteredEntries;cacheBuffer: 0;reuseItems: true
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width;height: root.detailed ? 45 : 29;radius: 5
                color: Theme.raised
                RowLayout {
                    anchors.fill: parent;anchors.leftMargin: 8;anchors.rightMargin: 7;spacing: 4
                    ColumnLayout {
                        Layout.fillWidth: true;spacing: 1
                        Text {Layout.fillWidth: true;text: row.modelData.title;color: Theme.text;font.pixelSize: 12;elide: Text.ElideRight}
                        Text {
                            visible: root.detailed;Layout.fillWidth: true;font.pixelSize: 10;color: Theme.muted;elide: Text.ElideRight
                            text: (row.modelData.category || "未分类")+" · "+(row.modelData.hex ? "HEX" : "文本")+" · "+row.modelData.text
                        }
                    }
                    Text {visible: !!row.modelData.hex;text: "HEX";color: Theme.orange;font.pixelSize: 10}
                    ActionButton {
                        objectName: "quickChoose";text: "填入";implicitWidth: 45;implicitHeight: 25;leftPadding: 5;rightPadding: 5
                        onClicked: {root.selection=row.modelData;root.chosen(row.modelData.text)}
                    }
                    ActionButton {
                        objectName: "quickEdit";text: "编辑";implicitWidth: 45;implicitHeight: 25;leftPadding: 5;rightPadding: 5
                        onClicked: root.editEntry(row.modelData)
                    }
                    ActionButton {
                        objectName: "quickDelete";text: "删除";danger: true;implicitWidth: 45;implicitHeight: 25;leftPadding: 5;rightPadding: 5
                        onClicked: {
                            if(root.repository.remove(root.scope,row.modelData.id)) {root.localError="";root.reload()}
                            else root.localError=root.repository.error
                        }
                    }
                }
            }
            Text {anchors.centerIn: parent;visible: list.count===0;text: root.allEntries.length ? "没有匹配内容" : "保存常用内容，点击填入后再发送";color: Theme.muted;font.pixelSize: 11}
        }
    }
    Dialog {
        id: editor;objectName: "quickEditor";parent: Overlay.overlay
        title: root.editingEntry.id ? "编辑快捷内容" : "新增快捷内容";modal: true
        width: Math.min(540,parent ? parent.width-32 : 540)
        x: parent ? (parent.width-width)/2 : 0;y: parent ? Math.max(8,(parent.height-height)/2) : 0
        padding: 16;closePolicy: Popup.CloseOnEscape
        background: Rectangle {radius: 10;color: Theme.panel;border.color: Theme.line}
        header: Text {text: editor.title;color: Theme.text;font.pixelSize: 17;padding: 16}
        contentItem: ColumnLayout {
            spacing: 8
            InputBox {id: titleInput;objectName: "quickTitle";Layout.fillWidth: true;placeholderText: "名称（必填）"}
            InputBox {id: categoryInput;objectName: "quickGroup";Layout.fillWidth: true;placeholderText: "分类（可选）"}
            ScrollView {
                Layout.fillWidth: true;Layout.preferredHeight: 120;clip: true
                TextArea {id: textInput;objectName: "quickText";placeholderText: "常用指令或发送内容";color: Theme.text;wrapMode: TextEdit.WrapAnywhere;selectByMouse: true;background: Rectangle {color: Theme.consoleBg;radius: 6;border.color: Theme.line}}
            }
            RowLayout {
                CheckBox {id: hexInput;objectName: "quickHex";text: "HEX 内容";palette.windowText: Theme.text}
                Item {Layout.fillWidth: true}
                SelectBox {id: newline;objectName: "quickNewline";model: ["不追加换行","LF","CRLF","CR"]}
            }
            Text {visible: root.localError!=="";text: root.localError;Layout.fillWidth: true;color: Theme.red;wrapMode: Text.Wrap;font.pixelSize: 12}
            Text {text: "这里只保存内容；填入不会连接设备或执行命令。";color: Theme.muted;font.pixelSize: 11}
            RowLayout {
                Item {Layout.fillWidth: true}
                ActionButton {text: "取消";onClicked: editor.close()}
                ActionButton {objectName: "quickSave";text: "保存";primary: true;enabled: titleInput.text.trim().length>0;onClicked: root.saveEntry()}
            }
        }
    }
    FileDialog {id: importDialog;title: "导入快捷内容";nameFilters: ["JSON 文件 (*.json)"];onAccepted: root.importFrom(selectedFile)}
    FileDialog {id: exportDialog;title: "导出当前用途的快捷内容";fileMode: FileDialog.SaveFile;defaultSuffix: "json";nameFilters: ["JSON 文件 (*.json)"];onAccepted: root.exportTo(selectedFile)}
}
