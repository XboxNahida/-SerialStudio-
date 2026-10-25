import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components" as C
import "../components/CommandGuide.js" as Guide

Window {
    id: root
    objectName: "commandAssistantWindow"
    required property var controller
    property var repository: null
    property int selectedMode: 0
    property var entries: []
    property int selectedIndex: -1
    property int repositoryRevision: 0
    property int seenCatalogRevision: -1
    property string customError: ""
    property string editingId: ""
    property bool favoritesOnly: false
    property var guideParts: []
    property int guideSection: 0
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
        const catalogRevision=controller ? controller.catalogRevision : 0
        const result=controller ? controller.commands(system.currentIndex,"","").slice() : []
        if(repository)for(const saved of repository.entries(commandScope)) {
            if(saved.builtinId) {
                const index=result.findIndex(function(item){return item.id===saved.builtinId})
                if(index>=0)result[index]=Object.assign({},result[index],{favorite:!!saved.favorite,favoriteId:saved.id})
                continue
            }
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
        guideSection=0
        edit.text=index>=0 && index<entries.length ? entries[index].example : ""
        loadGuide(selectedEntry)
    }
    function loadGuide(entry) {guideParts=Guide.parts(entry)}
    function syncGuide(source) {
        // 手动编辑或参数补全后，参数表必须对应当前指令，不显示过期值。
        const entry=source || selectedEntry
        const words=Guide.tokens(edit.text), original=Guide.tokens(entry.example || "")
        const compatible=words.length===original.length && words[0]===original[0]
        guideParts=Guide.parts(compatible?Object.assign({},entry,{example:edit.text}):{example:edit.text,syntax:entry.syntax})
    }
    function useAdvanced(example) {
        edit.text=example.command;syncGuide();edit.forceActiveFocus()
    }
    function browseTask(index) {
        if(index<=0)return
        search.text=["","连接","下载","调试","应用","设置","文件","高级语法"][index]
        category.currentIndex=0;favoritesOnly=false;reload()
    }
    function setGuideValue(index,value) {
        if(index<0 || index>=guideParts.length)return
        guideParts[index].value=value
        edit.text=guideParts.map(function(part){return part.value}).filter(function(value){return value.length>0}).join(" ")
    }
    function chooseCandidate(entry) {
        search.text="";category.currentIndex=0;favoritesOnly=false;reload()
        select(entries.findIndex(function(item){return item.id===entry.id}))
    }
    function toggleFavorite() {
        if(!repository || selectedIndex<0)return false
        const item=selectedEntry
        let ok
        if(item.custom)ok=!!repository.put(commandScope,Object.assign({},item,{text:item.example,favorite:!item.favorite}))
        else if(item.favoriteId)ok=repository.remove(commandScope,item.favoriteId)
        else {
            const snapshot=Object.assign({},item,{builtinId:item.id,text:item.example,favorite:true,hex:false,newline:""})
            delete snapshot.id
            ok=!!repository.put(commandScope,snapshot)
        }
        customError=ok?"":repository.error || "收藏保存失败"
        if(ok){repositoryRevision++;reload()}
        return ok
    }
    function reload() {
        if(!system || !search || !category || !edit)return
        const previousId=selectedEntry.id, draft=edit.text, previousGuide=guideParts
        const revision=controller ? Number(controller.catalogRevision) || 0 : 0
        const catalogUpdated=seenCatalogRevision>=0 && revision!==seenCatalogRevision
        seenCatalogRevision=revision
        const needle=search.text.trim().toLowerCase()
        const chosen=category.currentText
        entries=allEntries().filter(function(item) {
            const haystack=[item.title,item.category,item.syntax,item.example,item.description,item.requires,item.location,item.usage,
                JSON.stringify(item.parameters || []),JSON.stringify(item.options || []),JSON.stringify(item.advanced || []),Array.isArray(item.tags) ? item.tags.join(" ") : ""].join(" ").toLowerCase()
            return (!root.favoritesOnly || item.favorite) && (!chosen || chosen==="全部" || chosen===item.category) && (!needle || haystack.indexOf(needle)>=0)
        }).sort(function(a,b) {return a.category===b.category ? 0 : a.category<b.category ? -1 : 1})
        const previousIndex=entries.findIndex(function(item){return item.id===previousId})
        select(previousIndex>=0?previousIndex:entries.length>0?0:-1)
        if(previousIndex>=0 || catalogUpdated){edit.text=draft;guideParts=previousGuide}
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
        target: root.controller; ignoreUnknownSignals: true
        function onCatalogChanged() {root.repositoryRevision++;root.reload()}
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
                model: ["Linux · 目标 Shell","Windows · PowerShell","Windows · CMD","Android · 设备 Shell"]
                currentIndex: root.selectedMode
                onCurrentIndexChanged: {category.currentIndex=0;customDialog.close();root.reload()}
            }
        }
        RowLayout {
            C.SelectBox {objectName:"commandTask";Layout.preferredWidth:145;model:["按用途找指令…","连接设备","下载与部署","调试与排错","启动应用 / 操作","修改系统设置","管理文件","复杂语法 / 进阶"];onActivated:function(index){root.browseTask(index)}}
            C.InputBox { id: search; objectName: "commandSearch"; Layout.fillWidth: true; placeholderText: "搜用途 / 参数 / 高级用法"; onTextChanged: root.reload() }
            C.SelectBox { id: category; objectName: "commandCategory"; Layout.preferredWidth: 120; model: root.categories(); onCurrentTextChanged: root.reload() }
        }
        Text {Layout.fillWidth:true;text:"先选用途或搜索：亮度、启动、下载、崩溃、Wi-Fi…  找到 "+root.entryCount+" 项；示例需替换地址和路径。";color:C.Theme.muted;font.pixelSize:11;wrapMode:Text.Wrap}
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
                                Text { Layout.fillWidth: true; text: (entry.modelData.favorite ? "★ " : "")+(entry.modelData.custom ? "我的指令" : entry.modelData.interaction==="editor-key" ? "编辑器内部按键" : "内置模板"); color: C.Theme.muted; font.pixelSize: 10 }
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
                RowLayout {
                    Layout.fillWidth:true
                    Text { Layout.fillWidth: true; text: root.selectedEntry.title || "选择左侧指令"; color: C.Theme.text; font.pixelSize: 16; elide: Text.ElideRight }
                    C.ActionButton {objectName:"toggleCommandFavorite";text:root.selectedEntry.favorite?"★ 已收藏":"☆ 收藏";implicitHeight:30;enabled:!!root.repository && root.selectedIndex>=0;onClicked:root.toggleFavorite()}
                }
                RowLayout {
                    visible:!root.editorKey;Layout.fillWidth:true;spacing:5
                    C.ActionButton {objectName:"guideParameters";text:"参数填空";Layout.fillWidth:true;implicitHeight:28;primary:root.guideSection===0;onClicked:root.guideSection=0}
                    C.ActionButton {objectName:"guideOptions";text:"选项 ("+(root.selectedEntry.options || []).length+")";Layout.fillWidth:true;implicitHeight:28;primary:root.guideSection===1;onClicked:root.guideSection=1}
                    C.ActionButton {objectName:"guideAdvanced";text:"高级 ("+(root.selectedEntry.advanced || []).length+")";Layout.fillWidth:true;implicitHeight:28;primary:root.guideSection===2;onClicked:root.guideSection=2}
                }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 9
                        Text { text: root.editorKey ? "编辑器内操作" : "常见用法"; color: C.Theme.muted; font.pixelSize: 11 }
                        TextEdit { Layout.fillWidth: true; text: root.selectedEntry.syntax || ""; color: C.Theme.blue; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; font.family: "Consolas"; font.pixelSize: 13 }
                        Text { visible:!root.editorKey; Layout.fillWidth:true; text:"格式读法：[参数] 可选；<参数> 必填；A | B 二选一；… 可重复。这些括号是说明符号，不直接输入。";color:C.Theme.muted;font.pixelSize:11;wrapMode:Text.Wrap;textFormat:Text.PlainText }
                        Text {objectName:"commandLocation";Layout.fillWidth:true;visible:!!root.selectedEntry.location;text:"执行位置："+(root.selectedEntry.location || "");color:C.Theme.accent;font.pixelSize:12;wrapMode:Text.Wrap;textFormat:Text.PlainText}
                        Text { Layout.fillWidth: true; text: root.selectedEntry.usage || root.selectedEntry.description || "通过分类或用途查找指令。"; color: C.Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; lineHeight: 1.3;textFormat:Text.PlainText }
                        Text {visible:!root.editorKey && root.guideSection===0 && root.guideParts.length>0;text:"参数填空 · 下方同步生成指令";color:C.Theme.accent;font.pixelSize:12}
                        Repeater {
                            model:root.guideParts
                            ColumnLayout {
                                id:guidePart
                                required property var modelData
                                required property int index
                                visible:root.guideSection===0 && modelData.editable
                                Layout.fillWidth:true;spacing:3
                                Text {text:modelData.label;color:C.Theme.muted;font.pixelSize:11;textFormat:Text.PlainText}
                                RowLayout {
                                    Layout.fillWidth:true
                                    C.CommandInput {id:parameterValue;Layout.fillWidth:true;Layout.minimumWidth:60;text:guidePart.modelData.value;readOnly:!guidePart.modelData.editable;font.family:"Consolas";maximumLength:4096;valueChoices:guidePart.modelData.choices || [];onTextEdited:root.setGuideValue(guidePart.index,text);onCompletionApplied:root.setGuideValue(guidePart.index,text)}
                                    C.SelectBox {
                                        objectName:"parameterChoice_"+guidePart.index
                                        visible:(guidePart.modelData.choices || []).length>0;Layout.preferredWidth:110
                                        model:guidePart.modelData.choices || [];textRole:"value";displayText:"可选值…"
                                        onActivated:function(choice){parameterValue.text=model[choice].value;root.setGuideValue(guidePart.index,parameterValue.text)}
                                    }
                                }
                                Text {Layout.fillWidth:true;text:modelData.hint;color:C.Theme.subtle;font.pixelSize:10;wrapMode:Text.Wrap;textFormat:Text.PlainText}
                            }
                        }
                        Text {visible:root.guideSection===1;text:(root.selectedEntry.options || []).length>0?"选项字典 · 在下方输入 - 可查看候选":"此模板没有独立选项表，请查看格式和参数说明。";Layout.fillWidth:true;wrapMode:Text.Wrap;color:C.Theme.accent;font.pixelSize:12}
                        Repeater {
                            model:root.selectedEntry.options || []
                            Text {required property var modelData;visible:root.guideSection===1;Layout.fillWidth:true;text:modelData.value+"  "+modelData.description;color:C.Theme.muted;font.pixelSize:11;wrapMode:Text.Wrap;textFormat:Text.PlainText}
                        }
                        Text {visible:root.guideSection===2;text:(root.selectedEntry.advanced || []).length>0?"高级用法 · 选择后仍只修改草稿":"更多组合用法请搜索“高级语法”或该工具名称。";Layout.fillWidth:true;wrapMode:Text.Wrap;color:C.Theme.accent;font.pixelSize:12}
                        Repeater {
                            model:root.selectedEntry.advanced || []
                            ColumnLayout {
                                required property var modelData
                                visible:root.guideSection===2
                                Layout.fillWidth:true;spacing:4
                                Text {Layout.fillWidth:true;text:modelData.title;color:C.Theme.text;font.pixelSize:12;wrapMode:Text.Wrap;textFormat:Text.PlainText}
                                TextEdit {Layout.fillWidth:true;text:modelData.command;readOnly:true;selectByMouse:true;wrapMode:TextEdit.WrapAnywhere;color:C.Theme.blue;font.family:"Consolas";font.pixelSize:12;textFormat:TextEdit.PlainText}
                                Text {Layout.fillWidth:true;text:modelData.description;color:C.Theme.muted;font.pixelSize:11;wrapMode:Text.Wrap;textFormat:Text.PlainText}
                                C.ActionButton {text:"采用此示例";implicitHeight:28;onClicked:root.useAdvanced(modelData)}
                            }
                        }
                        Text { Layout.fillWidth: true; text: root.selectedEntry.requires || ""; color: C.Theme.orange; font.pixelSize: 11; wrapMode: Text.Wrap }
                        Text { Layout.fillWidth: true; visible: root.editorKey; text: root.selectedEntry.delivery || ""; color: C.Theme.orange; font.pixelSize: 11; wrapMode: Text.Wrap }
                        Text { Layout.fillWidth: true; text: root.editorKey ? "请先确认编辑器处于前台；按键会直接影响编辑器状态，保存和覆盖提示由你确认。" : "核对路径、PID、网卡和权限；删除、安装、服务及联网操作会改变系统。填入只追加内容，不清空已有输入。"; color: C.Theme.orange; font.pixelSize: 11; wrapMode: Text.Wrap; lineHeight: 1.3 }
                    }
                }
                Text { text: root.editorKey ? "按键标识 · 按上方说明操作" : "示例 / 可编辑指令 · 单行"; color: C.Theme.muted; font.pixelSize: 11 }
                C.CommandInput { id: edit; objectName: "commandEdit"; Layout.fillWidth: true; font.family: "Consolas"; maximumLength: 4096; readOnly: root.editorKey; placeholderText: "输入指令或留空看推荐 · Tab 补全";candidateEntries:root.allEntries();onCandidateChosen:function(entry){root.chooseCandidate(entry)};onTextEdited:root.syncGuide();onCompletionApplied:function(candidate){root.syncGuide(candidate.entry)} }
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
