import QtQuick
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    id: tests;name:"解析与传输表单恢复";when:windowShown;visible:true;width:900;height:640
    property var page
    readonly property var testSettings:settings
    readonly property var serialBackend:serial
    readonly property var transferBackend:transfer
    QtObject {
        id:settings
        property string error:""
        property var saved:({})
        function value(key,fallback){return saved[key]===undefined?fallback:saved[key]}
        function setValue(key,value){saved[key]=value;return true}
    }
    component Analysis: QtObject {
        property bool enabled:false
        property string text:"已收到数据"
        property string error:""
        property var samples:[]
        property var fields:["temperature"]
        property url mediaUrl:""
        property string mediaKind:""
        property bool capturing:false
        property int captured:0
        property int configured:0
        property string format:""
        property string schema:""
        property int activity:0
        signal changed()
        function configure(selected,rule){format=selected;schema=rule;configured++;enabled=true;changed();return true}
        function openFile(url,format){activity++;return true}
        function beginCapture(url){activity++;return true}
        function clear(){}
        function cancelCapture(){}
        function finishCapture(){return true}
    }
    component Backend: QtObject {
        property QtObject analysis:Analysis{}
        property bool connected:false
        property bool busy:false
        property int parserMode:1
        property int configured:0
        property var last:({})
        property int activity:0
        property ListModel frames:ListModel{property int dropped:0}
        function configureParser(mode,head,tail){last={mode:mode,head:head,tail:tail};parserMode=mode;configured++;return true}
        function demo(){activity++}
        function clearFrames(){}
    }
    Backend{id:serial}
    Backend{id:network}
    QtObject {
        id:transfer
        property bool busy:false
        property bool success:true
        property int progress:0
        property string status:"尚未传输"
        property var volumes:[]
        property int activity:0
        function start(mode,from,to,options){activity++;return true}
        function localUrl(path){return path}
        function localPath(url){return url.toString()}
        function refreshVolumes(){}
        function cancel(){}
    }
    Component{id:protocolFactory;App.ProtocolPage{width:900;height:640;backend:tests.serialBackend;settings:tests.testSettings}}
    Component{id:transferFactory;App.TransferPage{width:900;height:640;controller:tests.transferBackend;settings:tests.testSettings}}
    function field(name){const item=findChild(page,name);verify(item!==null,name);return item}
    function init(){settings.saved=({});serial.configured=0;serial.activity=0;serial.parserMode=1;serial.analysis.activity=0;network.configured=0;network.activity=0;network.parserMode=1;transfer.activity=0}
    function cleanup(){if(page){page.destroy();page=null}wait(10)}
    function test_parserSourceIsolationAndSafeRestore(){
        settings.saved={"protocol/serial/mode":3,"protocol/serial/head":"AB CD","protocol/serial/tail":"EF", "protocol/network/mode":1}
        page=protocolFactory.createObject(tests);verify(page!==null);wait(40)
        compare(field("parserMode").currentIndex,3);compare(serial.last.head,"AB CD");compare(serial.activity,0)
        field("parserHead").text="AA BB"
        page.backend=network;page.scope="network";wait(40)
        compare(field("parserMode").currentIndex,1);compare(network.last.mode,1)
        page.backend=serial;page.scope="serial";wait(40)
        compare(field("parserHead").text,"AA BB");compare(serial.activity,0);compare(network.activity,0)
    }
    function test_dataRulesRestoreAndNoFileActivity(){
        settings.saved={"data/serial/format":3,"data/serial/schema":"[{\"name\":\"x\",\"offset\":0,\"type\":\"u8\"}]","data/serial/enabled":false}
        page=protocolFactory.createObject(tests);verify(page!==null);wait(60)
        compare(field("dataFormat").currentIndex,3);compare(serial.analysis.format,"binary")
        compare(serial.analysis.enabled,false);compare(serial.analysis.activity,0)
        field("dataSchema").text="[]";compare(settings.saved["data/serial/schema"],"[]")
    }
    function test_transferDraftAndOverwriteNeverRestored(){
        settings.saved={"transfer/mode":1,"transfer/file":"C:/data/source.bin","transfer/host":"192.168.2.3","transfer/remote":"/tmp/result.bin","transfer/overwrite":true}
        page=transferFactory.createObject(tests);verify(page!==null);wait(40)
        compare(field("transferFile").text,"C:/data/source.bin");compare(field("transferOverwrite").checked,false)
        compare(field("transferHost").text,"192.168.2.3");compare(transfer.activity,0)
        field("transferOverwrite").checked=true
        field("transferMode").currentIndex=2;compare(field("transferOverwrite").checked,false)
        field("transferMode").currentIndex=1
        field("transferRemote").text="/tmp/new.bin";compare(settings.saved["transfer/remote"],"/tmp/new.bin")
        compare(transfer.activity,0)
    }
    function test_discoveredVolumeReplacesOldTargetWithoutTransfer(){
        page=transferFactory.createObject(tests);wait(40)
        field("transferMode").currentIndex=1
        field("transferFolder").text="F:/"
        field("transferOverwrite").checked=true
        verify(page.useDevice({type:"volume",config:{path:"E:/"}}))
        compare(field("transferMode").currentIndex,0)
        compare(field("transferFolder").text,"E:/")
        compare(field("transferOverwrite").checked,false)
        compare(transfer.activity,0)
        transfer.busy=true
        verify(!page.useDevice({type:"volume",config:{path:"G:/"}}))
        compare(field("transferFolder").text,"E:/")
        transfer.busy=false
    }
    function test_unconfiguredSourceUsesIndependentDefaults(){
        settings.saved={"protocol/serial/mode":3,"protocol/serial/head":"12 34","data/serial/format":1,"data/serial/schema":"serial-draft"}
        page=protocolFactory.createObject(tests);wait(40)
        page.backend=network;page.scope="network";wait(40)
        compare(field("parserHead").text,"AA 55")
        compare(field("dataFormat").currentIndex,0)
        verify(field("dataSchema").text.indexOf("serial-draft")<0)
        page.backend=serial;page.scope="serial";wait(40)
        compare(field("parserHead").text,"12 34");compare(field("dataSchema").text,"serial-draft")
    }
    function test_appliedRulesAndDraftStaySeparate(){
        settings.saved={"protocol/serial/mode":3,"protocol/serial/head":"AA 55","protocol/serial/tail":"0D", "protocol/serial/applied":{mode:1,head:"",tail:""},
            "data/serial/format":3,"data/serial/schema":"unfinished draft","data/serial/applied":{format:"utf8",schema:""}}
        page=protocolFactory.createObject(tests);wait(50)
        compare(serial.last.mode,1);compare(field("parserMode").currentIndex,3)
        compare(serial.analysis.format,"utf8");compare(field("dataSchema").text,"unfinished draft")
        const count=serial.analysis.configured
        page.backend=network;page.scope="network";wait(40)
        page.backend=serial;page.scope="serial";wait(40)
        compare(serial.analysis.configured,count)
        compare(serial.activity,0);compare(serial.analysis.activity,0)
    }
    function test_chartFieldWaitsForDecodedFields(){
        settings.saved={"data/serial/fieldName":"voltage","data/serial/output":1}
        serial.analysis.fields=[]
        page=protocolFactory.createObject(tests);wait(40)
        serial.analysis.fields=["temperature","voltage"];serial.analysis.changed();wait(20)
        compare(field("chartField").currentText,"voltage")
        field("chartField").currentIndex=0;field("chartField").activated(0)
        compare(settings.saved["data/serial/fieldName"],"temperature")
        serial.analysis.fields=[];serial.analysis.changed();wait(20)
        compare(settings.saved["data/serial/fieldName"],"temperature")
        serial.analysis.fields=["temperature"]
    }
    function test_visualEvidenceAndCompactLayout(){
        page=protocolFactory.createObject(tests);page.width=800;page.height=540;wait(40)
        verify(field("applyParserButton").width>150)
        grabImage(page).save("forms-protocol-800x540.png")
        field("protocolView").currentIndex=1;field("protocolView").activated(1)
        field("dataRulesToggle").checked=true;field("dataFormat").currentIndex=3;wait(30)
        verify(field("applyDataButton").width>80)
        grabImage(page).save("forms-data-800x540.png")
        page.destroy();page=transferFactory.createObject(tests);page.width=800;page.height=540
        field("transferMode").currentIndex=1;field("transferAdvanced").checked=true;wait(40)
        verify(field("transferFile").width>100);verify(!field("transferStart").enabled)
        grabImage(page).save("forms-transfer-advanced-800x540.png")
        field("transferAdvanced").checked=false;wait(20)
        grabImage(page).save("forms-transfer-800x540.png")
    }
}
