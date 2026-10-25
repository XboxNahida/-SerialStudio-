import QtQuick
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    name: "正式指令助手"
    when: windowShown
    property var assistant
    QtObject {
        id: terminal
        property bool active: true
        property bool inputBusy: false
        property int mode: 0
        property string status: "测试终端"
        property string lastFill: ""
        property var applied: ({})
        property bool realCatalog: false
        property int catalogRevision: 0
        property var catalogState: ({replaced:false})
        signal catalogChanged()
        function fill(text) { lastFill=text; return true }
        function applyCommand(entry) { applied=entry; return true }
        function commands(mode, search, category) {
            if(catalogState.replaced)return [{id:"new-entry",category:"新分类",title:"新指令",syntax:"whoami",example:"whoami",description:"只查询",interaction:"shell"}]
            if(realCatalog)return dictionaryEntries.filter(function(e){return e.system===["linux","powershell","cmd","android"][mode]})
            return [{id:"sample",category:"文件",title:"当前目录",syntax:"pwd",example:"pwd",description:"只查询",requires:"Linux Shell",interaction:"shell"},
                    {id:"key",category:"编辑器按键",title:"保存退出",syntax:":wq",example:":wq",description:"编辑器内部",requires:"Vi 编辑器",interaction:"editor-key",key:":wq",delivery:"先 Esc 再输入，最后手动 Enter"}]
        }
    }
    QtObject {
        id: repo
        property string error: ""
        property var saved: []
        property string lastScope: ""
        property string importUrl: ""
        property string exportUrl: ""
        signal changed()
        function entries(scope) { return saved.filter(function(x) {return x.scope===scope}) }
        function put(scope, entry) {
            lastScope=scope
            const next=Object.assign({},entry,{scope:scope,id:entry.id || "custom-test"})
            saved=saved.filter(function(x) {return x.id!==next.id}).concat([next]);changed();return next.id
        }
        function remove(scope,id) {lastScope=scope;saved=saved.filter(function(x){return x.id!==id});changed();return true}
        function importFile(scope,url) {lastScope=scope;importUrl=url.toString();return true}
        function exportFile(scope,url) {lastScope=scope;exportUrl=url.toString();return true}
    }
    Component { id: factory; App.CommandAssistant {controller:terminal;repository:repo} }
    function init() {
        terminal.active=true;terminal.inputBusy=false;terminal.mode=0;terminal.lastFill="";terminal.applied=({});terminal.realCatalog=false;terminal.catalogState.replaced=false
        repo.saved=[];repo.lastScope="";repo.importUrl="";repo.exportUrl=""
        assistant=factory.createObject(null);verify(assistant!==null);assistant.showFor(0);wait(80)
    }
    function cleanup() {assistant.destroy();wait(20)}
    function test_catalogUpdatePreservesDraftWhenEntryRemoved() {
        const edit=findChild(assistant,"commandEdit")
        edit.text="echo my-unfinished-input"
        terminal.catalogState.replaced=true;terminal.catalogRevision++;terminal.catalogChanged();wait(100)
        compare(edit.text,"echo my-unfinished-input")
        verify(assistant.entries.some(function(e){return e.id==="new-entry"}))
        compare(terminal.lastFill,"")
    }
    function test_catalogUpdatePreservesDraftWhenSearchBecomesEmpty() {
        findChild(assistant,"commandSearch").text="pwd";wait(30)
        const edit=findChild(assistant,"commandEdit");edit.text="pwd -P unfinished"
        terminal.catalogState.replaced=true;terminal.catalogRevision++;terminal.catalogChanged();wait(100)
        compare(assistant.entryCount,0);compare(edit.text,"pwd -P unfinished")
    }
    function test_catalogUpdatePreservesDraftWhenCategoryRemoved() {
        const category=findChild(assistant,"commandCategory")
        category.currentIndex=category.model.indexOf("文件");wait(30)
        const edit=findChild(assistant,"commandEdit");edit.text="echo my category draft"
        terminal.catalogState.replaced=true;terminal.catalogRevision++;terminal.catalogChanged();wait(100)
        compare(edit.text,"echo my category draft")
    }
    function test_windowAndGrouping() {
        compare(assistant.transientParent,null);compare(assistant.modality,Qt.NonModal)
        verify(!(assistant.flags & Qt.WindowStaysOnTopHint));compare(assistant.width,760);compare(assistant.height,580)
        compare(findChild(assistant,"commandList").section.property,"category")
        const screenshot=grabImage(assistant.contentItem);verify(screenshot.width>0)
        screenshot.save("assistant-formal.png")
        findChild(assistant,"commandSearch").text="Shell";tryCompare(assistant,"entryCount",1)
        findChild(assistant,"commandSearch").text="没有这个词";tryCompare(assistant,"entryCount",0)
    }
    function test_dictionaryHintsAndParameterCompletion() {
        verify(findChild(assistant,"commandTask")!==null,"新手需从用途查找")
        const edit=findChild(assistant,"commandEdit")
        edit.candidateEntries=[{id:"sample",title:"目录",example:"ls -l /tmp",syntax:"ls [选项] [目录]",interaction:"shell",
            options:[{value:"-a",description:"显示隐藏文件"},{value:"-l",description:"权限、大小和时间"}],
            parameters:[{index:2,label:"目录路径",hint:"填写要查看的目录",choices:[{value:"/tmp",description:"临时目录"}]}]}]
        edit.forceActiveFocus();edit.text="";edit.refreshCandidates();verify(edit.completionMatches.length>0)
        edit.text="ls - /home";edit.cursorPosition=4;edit.refreshCandidates()
        const index=edit.completionMatches.findIndex(function(c){return c.insert==="-a"})
        verify(index>=0);edit.acceptCandidate(index)
        compare(edit.text,"ls -a /home");compare(terminal.lastFill,"")
        assistant.loadGuide({example:"demo --mode safe",parameters:[{index:2,label:"模式",hint:"safe 为保守模式",choices:[{value:"safe",description:"保守"}]}]})
        compare(assistant.guideParts[2].hint,"safe 为保守模式")
        compare(assistant.guideParts[2].choices.length,1)
        assistant.guideSection=0;wait(30)
        function visualFind(item,name) {
            if(item.objectName===name)return item
            for(const child of item.children || []){const match=visualFind(child,name);if(match)return match}
            return null
        }
        const choice=visualFind(assistant.contentItem,"parameterChoice_2")
        verify(choice!==null,"找不到参数候选下拉框；字段="+JSON.stringify(assistant.guideParts))
        verify(choice.visible,"参数候选被隐藏："+JSON.stringify(assistant.guideParts))
        choice.activated(0)
        compare(findChild(assistant,"commandEdit").text,"demo --mode safe")
        compare(terminal.lastFill,"")
    }
    function test_realDictionarySearchAndAdvancedExamples() {
        terminal.realCatalog=true;assistant.showFor(0)
        verify(assistant.entryCount>=150)
        assistant.browseTask(2);verify(assistant.entryCount>5)
        findChild(assistant,"commandSearch").text="ptrace";verify(assistant.entryCount>=2)
        findChild(assistant,"commandSearch").text="";assistant.reload()
        const gdbIndex=assistant.entries.findIndex(function(e){return e.id==="linux-debug-gdb"})
        verify(gdbIndex>=0)
        assistant.select(gdbIndex)
        verify(assistant.selectedEntry.advanced.length>0)
        assistant.useAdvanced(assistant.selectedEntry.advanced[0]);compare(terminal.lastFill,"")
        verify(findChild(assistant,"commandEdit").text.indexOf("gdb -ex")===0)
        assistant.showFor(3)
        findChild(assistant,"commandSearch").text="亮度";verify(assistant.entryCount>=3)
        const image=grabImage(assistant.contentItem);verify(image.width>0)
        image.save(quickEvidenceDirectory+"/dictionary-android-settings.png")
        assistant.showFor(1)
        findChild(assistant,"commandSearch").text="网络 ADB";verify(assistant.entryCount>=3)
        verify(assistant.entries.every(function(e){return e.location.indexOf("电脑")>=0}))
        grabImage(assistant.contentItem).save(quickEvidenceDirectory+"/dictionary-host-adb.png")
        compare(terminal.lastFill,"")
    }
    function test_shellFillAndEditorRouting() {
        assistant.select(0);findChild(assistant,"commandEdit").text="pwd -P"
        mouseClick(findChild(assistant,"applyCommandButton"));compare(terminal.lastFill,"pwd -P")
        compare(Object.keys(terminal.applied).length,0)
        assistant.select(1);mouseClick(findChild(assistant,"applyCommandButton"))
        compare(terminal.applied.interaction,"editor-key");compare(terminal.applied.key,":wq")
        compare(terminal.lastFill,"pwd -P")
        terminal.inputBusy=true;tryCompare(findChild(assistant,"applyCommandButton"),"enabled",false)
        terminal.inputBusy=false;terminal.mode=2;tryCompare(findChild(assistant,"applyCommandButton"),"enabled",false)
    }
    function test_customCrudAndSystemScope() {
        assistant.beginCustom(false)
        findChild(assistant,"customCommandTitle").text="测试自定义"
        findChild(assistant,"customCommandText").text="echo 测试"
        findChild(assistant,"customCommandFavorite").checked=true
        verify(assistant.saveCustom());compare(repo.lastScope,"terminal-linux")
        compare(repo.saved.length,1);compare(repo.saved[0].text,"echo 测试")
        assistant.favoritesOnly=true;assistant.reload();compare(assistant.entryCount,1)
        assistant.favoritesOnly=false;assistant.reload()
        assistant.reload();assistant.select(assistant.entries.findIndex(function(x){return x.custom}))
        assistant.beginCustom(true);findChild(assistant,"customCommandText").text="echo 修改"
        verify(assistant.saveCustom());compare(repo.saved.length,1);compare(repo.saved[0].text,"echo 修改")
        assistant.reload();assistant.select(assistant.entries.findIndex(function(x){return x.custom}))
        verify(assistant.deleteSelectedCustom());compare(repo.saved.length,0)
        assistant.showFor(3);compare(assistant.commandScope,"terminal-android")
        verify(assistant.importCustom("file:///C:/test.json"));compare(repo.lastScope,"terminal-android")
        verify(assistant.exportCustom("file:///C:/out.json"));compare(repo.exportUrl,"file:///C:/out.json")
        assistant.beginCustom(false);findChild(assistant,"customCommandTitle").text="拒绝多行"
        findChild(assistant,"customCommandText").text="pwd\nwhoami";verify(!assistant.saveCustom());compare(repo.saved.length,0)
    }
    function test_importedHexNeverBecomesShellText() {
        repo.saved=[{scope:"terminal-linux",id:"imported-hex",title:"HEX导入",text:"65 78 69 74",hex:true,newline:""}]
        repo.changed();assistant.reload();assistant.select(assistant.entries.findIndex(function(x){return x.id==="imported-hex"}))
        verify(!assistant.applySelected());compare(terminal.lastFill,"");verify(assistant.customError.indexOf("HEX")>=0)
    }
    function test_builtinFavoritesPersistWithoutDuplicatesOrChangingDraft() {
        assistant.select(0)
        findChild(assistant,"commandEdit").text="pwd -P"
        verify(assistant.toggleFavorite())
        compare(repo.saved.length,1);compare(repo.saved[0].builtinId,"sample")
        compare(assistant.entryCount,2);compare(assistant.selectedEntry.favorite,true)
        compare(findChild(assistant,"commandEdit").text,"pwd -P")
        assistant.favoritesOnly=true;assistant.reload();compare(assistant.entryCount,1)
        assistant.destroy();assistant=factory.createObject(null);assistant.showFor(0);wait(30)
        assistant.select(assistant.entries.findIndex(function(x){return x.id==="sample"}))
        verify(assistant.selectedEntry.favorite)
        verify(assistant.toggleFavorite());compare(repo.saved.length,0)
        assistant.select(assistant.entries.findIndex(function(x){return x.id==="key"}))
        verify(assistant.toggleFavorite());assistant.reload()
        assistant.select(assistant.entries.findIndex(function(x){return x.id==="key"}))
        compare(assistant.selectedEntry.interaction,"editor-key")
    }
    function test_candidatesTabAndEditableTemplate() {
        const edit=findChild(assistant,"commandEdit")
        edit.forceActiveFocus();edit.text="pw";edit.refreshCandidates()
        verify(edit.completionMatches.length>0)
        keyClick(Qt.Key_Tab);compare(edit.text,"pwd")
        compare(terminal.lastFill,"")
        assistant.loadGuide({example:"cp /tmp/a.txt '/tmp/a copy.txt'",syntax:"cp [选项] <源文件> <目标文件>"})
        compare(assistant.guideParts.length,3)
        assistant.setGuideValue(2,"'/tmp/新 文件.txt'")
        compare(edit.text,"cp /tmp/a.txt '/tmp/新 文件.txt'")
        compare(terminal.lastFill,"")
        edit.candidateEntries=[{id:"sample",title:"物理路径",example:"pwd -P",interaction:"shell"},{id:"sample",title:"逻辑路径",example:"pwd -L",interaction:"shell"}]
        edit.forceActiveFocus();edit.text="pw";edit.refreshCandidates();wait(30)
        const candidates=findChild(assistant,"commandCandidateList")
        mouseWheel(candidates,20,20,0,-120)
        compare(edit.completionIndex,1)
        keyClick(Qt.Key_Tab);compare(edit.text,"pwd -L")
        compare(terminal.lastFill,"")
    }
    function test_realRepositoryAcceptsBuiltinFavorite() {
        assistant.repository=commandRepository;assistant.reload()
        assistant.select(assistant.entries.findIndex(function(x){return x.id==="sample"}))
        verify(assistant.toggleFavorite())
        tryVerify(function(){return !!assistant.selectedEntry.favorite})
        verify(formSettings.flush())
        const saved=commandRepository.entries("terminal-linux").find(function(x){return x.builtinId==="sample"})
        verify(!!saved);verify(saved.id!=="sample");verify(saved.favorite)
        verify(assistant.toggleFavorite())
        tryVerify(function(){return !commandRepository.entries("terminal-linux").some(function(x){return x.builtinId==="sample"})})
    }
}
