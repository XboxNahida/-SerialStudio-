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
        property int openCount: 0
        property string lastFill: ""
        function open(mode,config) {openCount++;return true}
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
    QtObject {id: backendStub;property string status:"测试"}
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
        settingsStub.values=values || ({});adbStub.executablePath="";adbStub.selectedSerial="";terminalStub.openCount=0
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
}
