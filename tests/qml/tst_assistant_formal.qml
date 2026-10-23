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
        function fill(text) { lastFill=text; return true }
        function applyCommand(entry) { applied=entry; return true }
        function commands(mode, search, category) {
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
        terminal.active=true;terminal.inputBusy=false;terminal.mode=0;terminal.lastFill="";terminal.applied=({})
        repo.saved=[];repo.lastScope="";repo.importUrl="";repo.exportUrl=""
        assistant=factory.createObject(null);verify(assistant!==null);assistant.showFor(0);wait(80)
    }
    function cleanup() {assistant.destroy();wait(20)}
    function test_windowAndGrouping() {
        compare(assistant.transientParent,null);compare(assistant.modality,Qt.NonModal)
        verify(!(assistant.flags & Qt.WindowStaysOnTopHint));compare(assistant.width,760);compare(assistant.height,580)
        compare(findChild(assistant,"commandList").section.property,"category")
        const screenshot=grabImage(assistant.contentItem);verify(screenshot.width>0)
        screenshot.save("assistant-formal.png")
        findChild(assistant,"commandSearch").text="Shell";tryCompare(assistant,"entryCount",1)
        findChild(assistant,"commandSearch").text="没有这个词";tryCompare(assistant,"entryCount",0)
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
}
