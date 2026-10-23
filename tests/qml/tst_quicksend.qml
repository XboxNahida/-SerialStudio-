import QtQuick
import QtQuick.Controls.Basic
import QtTest
import SerialWorkbench 1.0 as C

TestCase {
    id: tests
    name: "快捷发送与配置绑定"
    when: windowShown
    visible: true
    width: 800; height: 600
    C.QuickSendPanel { id: panel; width: 760; repository: commandRepository; scope: "serial" }
    SignalSpy { id: chosen; target: panel; signalName: "chosen" }
    TextField {id: draft;visible: false;text: "新草稿"}
    CheckBox {id: flag;visible: false;checked: false}
    TextField {id: password;visible: false;echoMode: TextInput.Password;text: "只在内存"}
    TextField {id: accessToken;visible: false;text: "令牌不保存"}
    C.PersistentForm {
        id: form;settings: formSettings;scope: "form-test";enabled: false
        fields: [{key:"draft",object:draft,property:"text"},{key:"checked",object:flag,property:"checked"},
                 {key:"masked",object:password,property:"text"},{key:"accessToken",object:accessToken,property:"text"}]
    }
    function field(name) { const found=findChild(panel,name);verify(found!==null,name);return found }
    function init() {
        for(const scope of ["serial","network"])
            for(const entry of commandRepository.entries(scope))commandRepository.remove(scope,entry.id)
        panel.scope="serial";panel.expanded=true;panel.detailed=false;chosen.clear()
        panel.reload();wait(1)
    }
    function cleanup() {form.enabled=false}
    function test_chooseOnlyFillsAndPreservesMetadata() {
        commandRepository.put("serial",{title:"读取温度",text:"AA 01",category:"板端",hex:true,newline:"\r\n"})
        tryCompare(panel,"filteredEntries",commandRepository.entries("serial"))
        const list=field("quickList");tryCompare(list,"count",1)
        list.positionViewAtIndex(0,ListView.Beginning);wait(30)
        mouseClick(findChild(list,"quickChoose"))
        compare(chosen.count,1);compare(chosen.signalArguments[0][0],"AA 01")
        compare(panel.selectedEntry.hex,true);compare(panel.selectedEntry.newline,"\r\n")
    }
    function test_editorAddsUpdatesDeletes() {
        mouseClick(field("quickAdd"));wait(30)
        const title=field("quickTitle");const text=field("quickText")
        title.text="中文指令";text.text="echo hello"
        mouseClick(field("quickSave"));tryCompare(field("quickList"),"count",1)
        const entry=commandRepository.entries("serial")[0];verify(entry.id.length>20)
        panel.editEntry(entry);wait(20);title.text="编辑后"
        mouseClick(field("quickSave"));tryCompare(field("quickList"),"count",1)
        compare(commandRepository.entries("serial")[0].id,entry.id)
        compare(commandRepository.entries("serial")[0].title,"编辑后")
        mouseClick(findChild(field("quickList"),"quickDelete"));tryCompare(field("quickList"),"count",0)
        compare(chosen.count,0)
    }
    function test_filterCollapseAndTheme() {
        commandRepository.put("serial",{title:"系统信息",text:"uname",category:"系统"})
        commandRepository.put("serial",{title:"温度",text:"temp",category:"板端"})
        tryCompare(field("quickList"),"count",2)
        field("quickSearch").text="uname";tryCompare(field("quickList"),"count",1)
        field("quickSearch").text="";field("quickCategory").currentIndex=1
        tryCompare(field("quickList"),"count",1)
        mouseClick(field("quickToggle"));compare(panel.expanded,false);verify(panel.implicitHeight<60)
        mouseClick(field("quickToggle"));verify(panel.implicitHeight>=140)
        C.Theme.dark=false;wait(10);C.Theme.dark=true
        panel.scope="network";tryCompare(field("quickList"),"count",0)
    }
    function test_fileRoundTripAndScopeSwitch() {
        commandRepository.put("serial",{title:"导出",text:"echo 中文"});wait(10)
        verify(panel.exportTo(quickExportUrl));commandRepository.remove("serial",commandRepository.entries("serial")[0].id)
        verify(panel.importFrom(quickExportUrl));tryCompare(field("quickList"),"count",1)
        panel.editEntry(commandRepository.entries("serial")[0]);wait(10);panel.scope="network"
        verify(!field("quickEditor").opened)
        verify(!panel.importFrom(quickExportUrl));compare(commandRepository.entries("network").length,0)
    }
    function test_largeListIsVirtualized() {
        panel.scope="terminal-powershell"
        for(let i=0;i<10000;++i)commandRepository.put(panel.scope,{title:"指令 "+i,text:"echo "+i})
        tryCompare(field("quickList"),"count",10000,5000)
        const list=field("quickList");list.positionViewAtEnd();wait(50)
        verify(list.contentItem.children.length<60)
        compare(chosen.count,0)
    }
    function test_persistentFormRestoresAndSavesExplicitFields() {
        formSettings.setValue("form-test/draft","历史中文");formSettings.setValue("form-test/checked",true)
        form.enabled=true;tryCompare(form,"ready",true)
        compare(draft.text,"历史中文");compare(flag.checked,true)
        draft.text="修改草稿";flag.checked=false
        compare(formSettings.value("form-test/draft"),"修改草稿");compare(formSettings.value("form-test/checked"),false)
        form.scope="form-other";tryCompare(form,"ready",true);wait(10)
        draft.text="其他用途"
        compare(formSettings.value("form-other/draft"),"其他用途")
        form.scope="form-test";wait(10);compare(draft.text,"修改草稿")
        form.enabled=false;draft.text="不应保存"
        compare(formSettings.value("form-test/draft"),"修改草稿")
    }
    function test_persistentFormSkipsPasswordsAndTokens() {
        form.scope="sensitive-test";form.enabled=true;tryCompare(form,"ready",true)
        password.text="新的口令";accessToken.text="新的令牌"
        compare(formSettings.value("sensitive-test/masked","absent"),"absent")
        compare(formSettings.value("sensitive-test/accessToken","absent"),"absent")
        formSettings.setValue("sensitive-test/masked","不得恢复的旧值")
        form.restore();compare(password.text,"新的口令")
    }
    function test_qmlObjectsAndArraysPersistAsJson() {
        const value={mode:3,fields:[{name:"温度",offset:0}],enabled:true}
        verify(formSettings.setValue("qml-boundary/object",value),formSettings.error)
        compare(formSettings.value("qml-boundary/object").fields[0].name,"温度")
        verify(formSettings.setValue("qml-boundary/list",[{host:"192.0.2.1",port:22}]),formSettings.error)
        compare(formSettings.value("qml-boundary/list")[0].port,22)
        verify(formSettings.flush(),formSettings.error)
    }
    function test_visualEvidence() {
        commandRepository.put("serial",{title:"读取温度",text:"TEMP?",category:"板端常用"})
        commandRepository.put("serial",{title:"协议读取示例",text:"AA 01 00 FF",category:"板端常用",hex:true,newline:"\r\n"})
        tryCompare(field("quickList"),"count",2);wait(100)
        grabImage(panel).save(quickEvidenceDirectory+"/quick-panel-dark.png")
        C.Theme.dark=false;panel.width=350;wait(250)
        grabImage(panel).save(quickEvidenceDirectory+"/quick-panel-light-350.png")
        panel.editEntry(commandRepository.entries("serial")[1]);wait(100)
        grabImage(tests).save(quickEvidenceDirectory+"/quick-editor-light.png")
        field("quickEditor").close();panel.width=760;C.Theme.dark=true
    }
}
