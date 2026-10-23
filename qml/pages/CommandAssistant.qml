import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components" as C

Window {
    id: root
    objectName: "commandAssistantWindow"
    required property var controller
    property var repository: null
    property int selectedMode: 0
    property var entries: []
    property int selectedIndex: -1
    property int repositoryRevision: 0
    property string customError: ""
    property string editingId: ""
    property bool favoritesOnly: false
    readonly property int entryCount: entries.length
    readonly property string commandScope: "terminal-" + ["linux","powershell","cmd","android"][Math.max(0,system.currentIndex)]
    readonly property var selectedEntry: selectedIndex>=0 && selectedIndex<entries.length ? entries[selectedIndex] : ({})
    readonly property bool canFill: controller && controller.active && !controller.inputBusy && controller.mode===system.currentIndex
    readonly property bool editorKey: selectedEntry.interaction==="editor-key"
    width: 760; height: 580; minimumWidth: 760; minimumHeight: 580
    transientParent: null
    flags: Qt.Window
    modality: Qt.NonModal
    title: "指令辅助 · 填入后自行确认"
    color: C.Theme.background
    onRepositoryChanged: {repositoryRevision++;reload()}

    function validSingleLine(text) {
        return text.trim().length>0 && text.length<=4096 && !/[\u0000-\u001f\u007f\u2028\u2029]/.test(text)
    }
    function allEntries() {
        const revision=repositoryRevision
        const result=controller ? controller.commands(system.currentIndex,"","").slice() : []
        if(repository)for(const saved of repository.entries(commandScope)) {
            result.push(Object.assign({},saved,{custom:true,category:saved.category || "自定义",
                example:saved.text,syntax:saved.syntax || saved.text,interaction:"shell",
                description:saved.description || "用户保存的指令；执行前核对目标与参数。",
                requires:saved.requires || "由用户确认工具、权限及目标系统。"}))
        }
        return result
    }
    function categories() {
        const result=["全部"]
        for(const item of allEntries())if(result.indexOf(item.category)<0)result.push(item.category)
        return result
    }
    function select(index) {
        selectedIndex=index
        edit.text=index>=0 && index<entries.length ? entries[index].example : ""
    }
    function reload() {
        if(!system || !search || !category || !edit)return
        const needle=search.text.trim().toLowerCase()
        const chosen=category.currentText
        entries=allEntries().filter(function(item) {
            const haystack=[item.title,item.syntax,item.example,item.description,item.requires,Array.isArray(item.tags) ? item.tags.join(" ") : ""].join(" ").toLowerCase()
            return (!root.favoritesOnly || item.favorite) && (!chosen || chosen==="全部" || chosen===item.category) && (!needle || haystack.indexOf(needle)>=0)
        }).sort(function(a,b) {return a.category===b.category ? 0 : a.category<b.category ? -1 : 1})
        select(entries.length>0?0:-1)
    }
    function showFor(mode) {
        system.currentIndex=mode;search.text="";category.currentIndex=0;reload()
        show();raise();requestActivate()
    }
    function applySelected() {
        if(!canFill || selectedIndex<0)return false
        if(selectedEntry.hex){customError="终端指令不能使用 HEX 内容，请将此项改为单行文本。";return false}
        if(editorKey) {
            if(typeof controller.applyCommand!=="function") {customError="当前终端尚不支持编辑器按键投递。";return false}
            return controller.applyCommand(selectedEntry)
        }
        if(!validSingleLine(edit.text)) {customError="只允许 1–4096 字符的单行指令，不得包含控制字符。";return false}
        return controller.fill(edit.text)
    }
    function beginCustom(existing) {
        if(!repository)return
        const item=selectedEntry
        editingId=existing && item.custom ? item.id : ""
        customTitle.text=existing ? (item.title || "") : ""
        customText.text=existing ? (item.example || "") : edit.text
        customCategory.text=existing ? (item.category || "自定义") : "自定义"
        customDescription.text=existing ? (item.description || "") : ""
        customFavorite.checked=existing && !!item.favorite
        customError="";customDialog.open()
    }
    function saveCustom() {
        if(!repository || !customTitle.text.trim() || !validSingleLine(customText.text)) {
            customError="请填写名称及不含换行、控制符的单行指令。";return false
        }
        const item={title:customTitle.text.trim(),text:customText.text,category:customCategory.text.trim() || "自定义",
            description:customDescription.text,favorite:customFavorite.checked,interaction:"shell",hex:false,newline:""}
        if(editingId)item.id=editingId
        const id=repository.put(commandScope,item)
        if(!id) {customError=repository.error || "保存失败";return false}
        customError="";customDialog.close();search.text="";category.currentIndex=0;favoritesOnly=false;reload()
        select(entries.findIndex(function(x){return x.custom && x.id===id}))
        return true
    }
    function deleteSelectedCustom() {
        if(!repository || !selectedEntry.custom)return false
        const ok=repository.remove(commandScope,selectedEntry.id)
        if(ok) {customError="";reload()} else customError=repository.error || "删除失败"
        return ok
    }
    function importCustom(url) {
        if(!repository)return false
        const ok=repository.importFile(commandScope,url)
        if(ok) {customError="";repositoryRevision++;reload()} else customError=repository.error || "导入失败"
        return ok
    }
    function exportCustom(url) {
        if(!repository)return false
        const ok=repository.exportFile(commandScope,url)
        customError=ok ? "" : repository.error || "导出失败"
        return ok
    }
    Connections {
        target: root.repository
        function onChanged() {root.repositoryRevision++;root.reload()}
    }
    FileDialog {
        id: importDialog; title: "导入当前系统的自定义指令"; nameFilters: ["JSON 指令文件 (*.json)"]
        onAccepted: root.importCustom(selectedFile)
    }
    FileDialog {
        id: exportDialog; title: "导出当前系统的自定义指令"; fileMode: FileDialog.SaveFile
        defaultSuffix: "json"; nameFilters: ["JSON 指令文件 (*.json)"]
        onAccepted: root.exportCustom(selectedFile)
    }
    Dialog {
        id: deleteDialog; parent: root.contentItem; anchors.centerIn: parent
        width: Math.min(440,root.width-40); implicitWidth: 440
        title: "删除自定义指令"; modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.deleteSelectedCustom()
        contentItem: Text { text: "删除“" + (root.selectedEntry.title || "") + "”？内置模板不受影响。"; color: C.Theme.text; wrapMode: Text.Wrap }
    }
    Dialog {
        id: customDialog; objectName: "customCommandDialog"; parent: root.contentItem
        anchors.centerIn: parent; width: Math.min(480,root.width-40); modal: true
        title: root.editingId ? "编辑自定义指令" : "新增自定义指令"
        ColumnLayout {
            width: parent.width; spacing: 10
            C.InputBox { id: customTitle; objectName: "customCommandTitle"; Layout.fillWidth: true; placeholderText: "名称"; maximumLength: 100 }
            C.InputBox { id: customCategory; objectName: "customCommandCategory"; Layout.fillWidth: true; placeholderText: "分类"; maximumLength: 50 }
            C.InputBox { id: customText; objectName: "customCommandText"; Layout.fillWidth: true; placeholderText: "指令内容 · 单行"; maximumLength: 4096; font.family: "Consolas" }
            C.InputBox { id: customDescription; objectName: "customCommandDescription"; Layout.fillWidth: true; placeholderText: "用途、权限及风险说明"; maximumLength: 1000 }
            CheckBox { id: customFavorite; objectName: "customCommandFavorite"; text: "收藏这条指令"; palette.windowText: C.Theme.text }
            Text { Layout.fillWidth: true; text: root.customError || "按当前系统保存；仅填入终端，不附加回车。"; color: root.customError ? C.Theme.red : C.Theme.muted; wrapMode: Text.Wrap; font.pixelSize: 12 }
            RowLayout {
                Item { Layout.fillWidth: true }
                C.ActionButton { text: "取消"; onClicked: customDialog.close() }
                C.ActionButton { objectName: "saveCustomCommand"; text: "保存"; primary: true; onClicked: root.saveCustom() }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 10
        RowLayout {
            Text { text: "指令辅助"; color: C.Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            C.SelectBox {
                id: system; objectName: "assistantSystem"; Layout.preferredWidth: 200
                model: ["Linux · SSH","Windows · PowerShell","Windows · CMD","Android · ADB Shell"]
                currentIndex: root.selectedMode
                onCurrentIndexChanged: {category.currentIndex=0;customDialog.close();root.reload()}
            }
        }
        RowLayout {
            C.InputBox { id: search; objectName: "commandSearch"; Layout.fillWidth: true; placeholderText: "搜索名称、标签、工具或说明"; onTextChanged: root.reload() }
            C.SelectBox { id: category; objectName: "commandCategory"; Layout.preferredWidth: 120; model: root.categories(); onCurrentTextChanged: root.reload() }
        }
        RowLayout {
            spacing: 6
            C.ActionButton { objectName: "newCustomCommand"; text: "新增"; implicitHeight: 32; enabled: !!root.repository; onClicked: root.beginCustom(false) }
            C.ActionButton { objectName: "editCustomCommand"; text: root.selectedEntry.custom ? "编辑" : "另存自定义"; implicitHeight: 32; enabled: !!root.repository && root.selectedIndex>=0 && !root.editorKey; onClicked: root.beginCustom(true) }
            C.ActionButton { objectName: "deleteCustomCommand"; text: "删除"; implicitHeight: 32; danger: true; enabled: !!root.repository && !!root.selectedEntry.custom; onClicked: deleteDialog.open() }
            C.ActionButton { objectName: "filterFavoriteCommands"; text: root.favoritesOnly ? "显示全部" : "仅看收藏"; implicitHeight: 32; enabled: !!root.repository; onClicked: {root.favoritesOnly=!root.favoritesOnly;root.reload()} }
            Item { Layout.fillWidth: true }
            C.ActionButton { objectName: "importCustomCommands"; text: "导入"; implicitHeight: 32; enabled: !!root.repository; onClicked: importDialog.open() }
            C.ActionButton { objectName: "exportCustomCommands"; text: "导出"; implicitHeight: 32; enabled: !!root.repository; onClicked: exportDialog.open() }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
            C.Panel {
                Layout.preferredWidth: 250; Layout.fillHeight: true
                ListView {
                    id: list; objectName: "commandList"
                    anchors.fill: parent; anchors.margins: 8; clip: true; spacing: 4
                    model: root.entries
                    section.property: "category"
                    section.delegate: Text { required property string section; width: list.width; height: 28; text: section; color: C.Theme.muted; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: entry
                        required property var modelData
                        required property int index
                        width: ListView.view.width; height: 60; radius: 7
                        color: root.selectedIndex===index ? C.Theme.accentFill : C.Theme.raised
                        MouseArea { anchors.fill: parent; onClicked: root.select(entry.index) }
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 9; spacing: 5
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 5
                                Text { Layout.fillWidth: true; text: entry.modelData.title; color: C.Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                                Text { Layout.fillWidth: true; text: entry.modelData.custom ? (entry.modelData.favorite ? "★ 收藏指令" : "我的指令") : entry.modelData.interaction==="editor-key" ? "编辑器内部按键" : "内置模板"; color: C.Theme.muted; font.pixelSize: 10 }
                            }
                            C.ActionButton {
                                text: entry.modelData.interaction==="editor-key" ? "按键" : "填入"
                                implicitWidth: 48; implicitHeight: 30; leftPadding: 6; rightPadding: 6; enabled: root.canFill
                                onClicked: {root.select(entry.index);root.applySelected()}
                            }
                        }
                    }
                    Text { anchors.centerIn: parent; visible: root.entryCount===0; text: "没有匹配的指令"; color: C.Theme.muted; font.pixelSize: 12 }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
                Text { Layout.fillWidth: true; text: root.selectedEntry.title || "选择左侧指令"; color: C.Theme.text; font.pixelSize: 16; elide: Text.ElideRight }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 9
                        Text { text: root.editorKey ? "编辑器内操作" : "常见用法"; color: C.Theme.muted; font.pixelSize: 11 }
                        TextEdit { Layout.fillWidth: true; text: root.selectedEntry.syntax || ""; color: C.Theme.blue; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; font.family: "Consolas"; font.pixelSize: 13 }
                        Text { Layout.fillWidth: true; text: root.selectedEntry.description || "通过分类或关键词查找指令。"; color: C.Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; lineHeight: 1.3 }
                        Text { Layout.fillWidth: true; text: root.selectedEntry.requires || ""; color: C.Theme.orange; font.pixelSize: 11; wrapMode: Text.Wrap }
                        Text { Layout.fillWidth: true; visible: root.editorKey; text: root.selectedEntry.delivery || ""; color: C.Theme.orange; font.pixelSize: 11; wrapMode: Text.Wrap }
                        Text { Layout.fillWidth: true; text: root.editorKey ? "请先确认编辑器处于前台；按键会直接影响编辑器状态，保存和覆盖提示由你确认。" : "核对路径、PID、网卡和权限；删除、安装、服务及联网操作会改变系统。填入只追加内容，不清空已有输入。"; color: C.Theme.orange; font.pixelSize: 11; wrapMode: Text.Wrap; lineHeight: 1.3 }
                    }
                }
                Text { text: root.editorKey ? "按键标识 · 按上方说明操作" : "示例 / 可编辑指令 · 单行"; color: C.Theme.muted; font.pixelSize: 11 }
                C.InputBox { id: edit; objectName: "commandEdit"; Layout.fillWidth: true; font.family: "Consolas"; maximumLength: 4096; readOnly: root.editorKey; placeholderText: "选择模板后修改参数" }
                C.ActionButton {
                    objectName: "applyCommandButton"; Layout.fillWidth: true
                    text: root.editorKey ? "发送编辑器按键" : "填入终端 · 不回车"; primary: true
                    enabled: root.canFill && root.selectedIndex>=0 && (root.editorKey || root.validSingleLine(edit.text))
                    onClicked: root.applySelected()
                }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11
            color: root.customError || (root.repository && root.repository.error) ? C.Theme.red : C.Theme.muted
            text: root.customError || (root.repository ? root.repository.error : "") || (!root.controller ? "终端控制器未加载" : root.controller.active && root.controller.mode!==system.currentIndex ? "当前打开的是其他系统终端；请选择相同系统后填入。" : root.controller.status)
        }
    }
    Component.onCompleted: reload()
}
