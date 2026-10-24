import QtQuick
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    id: testCase
    name: "开发板目标恢复"
    when: windowShown
    width: 1100; height: 740; visible: true
    property var page: null
    QtObject {
        id: adbStub
        property string executablePath: ""
        property string resolvedPath: executablePath
        property string selectedSerial: ""
        property var devices: [{serial:"boardA",label:"板 A",state:"device"},{serial:"boardB",label:"板 B",state:"device"}]
        property bool enabled: false
        property bool scanning: false
        property bool ready: selectedSerial!==""
        property string status: "测试枚举"
        signal changed()
        function refresh() {}
    }
    QtObject {
        id: terminalStub
        property var adb: adbStub
        property bool active: false
        property bool embedded: true
        property bool inputBusy: false
        property int mode: -1
        property string status: "尚未打开"
        property string connectionSummary: "root@board-a:22"
        property string transport: "ssh"
        property int openCount: 0
        property var lastConfig: ({})
        property int lastMode: -1
        property string lastFill: ""
        function open(mode,config) {openCount++;lastMode=mode;lastConfig=config;return true}
        function commands() {return []}
        function fill(text) {lastFill=text;return true}
    }
    QtObject {
        id: boardStub
        property var terminal: terminalStub
        property bool busy: false
        property bool success: true
        property string output: ""
        function openTerminal(config) {terminalStub.openCount++}
    }
    QtObject {id: backendStub;property string status:"测试";property var ports:[];function refreshPorts(){}}
    QtObject {
        id: settingsStub
        property var values: ({})
        property string error: ""
        function value(key,fallback) {return values[key]===undefined ? fallback : values[key]}
        function setValue(key,value) {const next=Object.assign({},values);next[key]=value;values=next;return true}
    }
    Component {id: factory;App.BoardPage {controller:boardStub;serialBackend:backendStub;networkBackend:backendStub;settings:settingsStub}}
    function create(values) {
        if(page){page.destroy();wait(10)}
        settingsStub.values=values || ({});adbStub.executablePath="";adbStub.selectedSerial="";terminalStub.openCount=0;terminalStub.active=false;terminalStub.mode=-1
        page=factory.createObject(testCase,{width:1080,height:700});verify(page!==null);verify(waitForPolish(page));wait(50)
    }
    function init() {create({})}
    function cleanup() {if(page){page.destroy();page=null;wait(10)}}
    function test_deviceSelectionCarriesToolAndPersistsWithoutOpening() {
        page.useDevice({type:"adb",address:"boardB",config:{serial:"boardB",adb:"C:/Custom/adb.exe"}})
        compare(adbStub.executablePath,"C:/Custom/adb.exe")
        compare(adbStub.selectedSerial,"boardB");compare(settingsStub.values["board/adbSerial"],"boardB")
        compare(terminalStub.openCount,0)
    }
    function test_restoreAndUserSelectionDoNotConnect() {
        create({"board/terminal":3,"board/adbPath":"C:/Restored/adb.exe","board/adbSerial":"boardB"})
        compare(adbStub.executablePath,"C:/Restored/adb.exe");compare(adbStub.selectedSerial,"boardB")
        const devices=findChild(page,"adbDevices");devices.activated(0)
        compare(adbStub.selectedSerial,"boardA");compare(settingsStub.values["board/adbSerial"],"boardA")
        adbStub.devices=adbStub.devices.slice().reverse();wait(20);compare(adbStub.selectedSerial,"boardA")
        compare(terminalStub.openCount,0)
        const saved=settingsStub.values;create(saved);compare(adbStub.selectedSerial,"boardA");compare(terminalStub.openCount,0)
    }
    function test_automaticSelectionIsSavedOnceAndRefreshKeepsUserChoice() {
        adbStub.selectedSerial="boardB";adbStub.changed()
        compare(settingsStub.values["board/adbSerial"],"boardB")
        findChild(page,"adbDevices").activated(0)
        adbStub.changed();adbStub.changed()
        compare(adbStub.selectedSerial,"boardA");compare(settingsStub.values["board/adbSerial"],"boardA")
        compare(terminalStub.openCount,0)
    }
    function test_busQuickConvertsUtf8AndNewlinesToHex() {
        const panel=findChild(page,"boardBusQuick");verify(panel!==null)
        panel.selection={text:"中",hex:false,newline:"\r\n"};panel.chosen("中")
        compare(findChild(page,"boardTx").text,"E4 B8 AD 0D 0A")
        panel.selection={text:"01 ff",hex:true,newline:"\n"};panel.chosen("01 ff")
        compare(findChild(page,"boardTx").text,"01 FF 0A")
        panel.selection={text:"GG",hex:true,newline:""};panel.chosen("GG")
        compare(findChild(page,"boardTx").text,"01 FF 0A");verify(panel.localError.length>0)
    }
    function test_terminalQuickRejectsHexAndNeverAppendsNewline() {
        findChild(page,"terminalMode").currentIndex=2;terminalStub.mode=2;terminalStub.active=true;terminalStub.lastFill=""
        const panel=findChild(page,"terminalQuick");verify(panel!==null)
        panel.selection={text:"65 78 69 74",hex:true,newline:""};panel.chosen("65 78 69 74")
        compare(terminalStub.lastFill,"");verify(panel.localError.indexOf("HEX")>=0)
        panel.selection={text:"whoami",hex:false,newline:"\r\n"};panel.chosen("whoami")
        compare(terminalStub.lastFill,"whoami");verify(panel.localError.indexOf("回车")>=0)
        terminalStub.active=false
    }
    function test_terminalQuickRejectsEditorKeys() {
        findChild(page,"terminalMode").currentIndex=2;terminalStub.mode=2;terminalStub.active=true;terminalStub.lastFill=""
        const panel=findChild(page,"terminalQuick")
        panel.selection={text:"Ctrl+O",interaction:"editor-key",hex:false};panel.chosen("Ctrl+O")
        compare(terminalStub.lastFill,"");verify(panel.localError.indexOf("指令助手")>=0)
    }
    function test_activeSessionDoesNotDisplayBusAddressDraft() {
        const drawer=findChild(page,"boardDrawer");drawer.open();tryCompare(drawer,"position",1)
        const bus=findChild(page,"boardBus"), source=findChild(page,"boardSource")
        terminalStub.mode=0;terminalStub.active=true
        bus.currentIndex=0;source.currentIndex=2
        const host=findChild(page,"sshHost");host.text="board-b"
        verify(host.visible)
        bus.currentIndex=3;wait(20)
        verify(!host.visible)
        const summary=findChild(page,"terminalConnectionSummary")
        verify(summary!==null);verify(summary.visible)
        compare(summary.text,"当前会话：root@board-a:22")
        bus.currentIndex=0;wait(20)
        verify(host.visible);compare(host.text,"board-b")
        verify(!summary.visible)
    }
    function test_unifiedSystemsAndConnectionChoices() {
        const bus=findChild(page,"boardBus"), mode=findChild(page,"terminalMode")
        compare(bus.count,4);compare(bus.currentText,"系统交互终端")
        const connection=findChild(page,"terminalConnection")
        verify(connection!==null)
        mode.currentIndex=1
        compare(page.terminalTransport,"local")
        connection.currentIndex=1
        compare(page.terminalTransport,"ssh")
        verify(page.showSshConfig)
        mode.currentIndex=3
        compare(page.terminalTransport,"adb")
        connection.currentIndex=2
        compare(page.terminalTransport,"serial")
        compare(findChild(page,"terminalPresentation").currentIndex,0)
        verify(!findChild(page,"terminalPresentation").enabled)
        compare(terminalStub.openCount,0)
    }
    function test_legacyInfoAndTerminalMerge() {
        create({"board/bus":3});compare(findChild(page,"boardBus").currentIndex,3);verify(page.interactive)
        create({"board/bus":4});compare(findChild(page,"boardBus").currentIndex,3);verify(page.interactive)
    }
    function test_crossSystemConfigAndSerialPersistence() {
        const mode=findChild(page,"terminalMode"), connection=findChild(page,"terminalConnection")
        mode.currentIndex=1;connection.currentIndex=1
        findChild(page,"sshHost").text="192.0.2.50"
        page.openTerminal()
        compare(terminalStub.lastMode,1);compare(terminalStub.lastConfig.transport,"ssh")
        compare(terminalStub.lastConfig.host,"192.0.2.50")
        mode.currentIndex=3;connection.currentIndex=2
        page.selectedPort="TEST-SERIAL";findChild(page,"terminalSerialBaud").text="921600"
        page.openTerminal()
        compare(terminalStub.lastMode,3);compare(terminalStub.lastConfig.transport,"serial")
        compare(terminalStub.lastConfig.port,"TEST-SERIAL");compare(terminalStub.lastConfig.baud,921600)
        compare(terminalStub.lastConfig.presentation,"qt")
        const saved=settingsStub.values;create(saved)
        compare(page.terminalTransport,"serial");compare(page.selectedPort,"TEST-SERIAL")
        compare(findChild(page,"terminalSerialBaud").text,"921600");compare(terminalStub.openCount,0)
    }
    function test_overviewFillsWithoutEnter() {
        findChild(page,"terminalMode").currentIndex=1
        terminalStub.active=true;terminalStub.lastFill=""
        page.fillSystemOverview()
        compare(terminalStub.lastFill,"Get-ComputerInfo; Get-NetIPConfiguration")
        verify(terminalStub.lastFill.indexOf("\n")<0)
    }
    function test_smallConnectionDrawer() {
        page.width=620;page.height=430
        const drawer=findChild(page,"boardDrawer")
        drawer.open();tryCompare(drawer,"position",1)
        for(const option of [{mode:0,connection:0,name:"ssh"},{mode:3,connection:0,name:"adb"},{mode:2,connection:2,name:"serial"}]){
            findChild(page,"terminalMode").currentIndex=option.mode
            findChild(page,"terminalConnection").currentIndex=option.connection
            findChild(page,"boardDrawerScroll").contentItem.contentY=0
            verify(waitForPolish(page));wait(40)
            verify(drawer.width<page.width)
            grabImage(page).save(quickEvidenceDirectory+"/cross-terminal-"+option.name+".png")
        }
    }
}
