import QtQuick
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    id:tests;name:"设备文件管理";when:windowShown;visible:true;width:1000;height:650
    property var page:null
    property var api:files
    property var preferences:settings
    QtObject {
        id:files
        property bool busy:false
        property bool success:true
        property string path:""
        property string kind:""
        property string status:"等待操作"
        property int progress:0
        property var entries:[]
        property int visits:0
        property int transfers:0
        property string clipboardName:""
        property bool clipboardCut:false
        property int deletions:0
        property var last:({})
        property QtObject adb:QtObject {
            property bool enabled:false
            property string selectedSerial:"B"
            property string executablePath:""
            property string status:"两台设备"
            property var devices:[{serial:"A",label:"A"},{serial:"B",label:"B"}]
            function refresh(){}
        }
        signal changed()
        function browse(type,value,options){visits++;kind=type;busy=true;last={path:value,options:options};changed();return true}
        function complete(){path=last.path;entries=[{name:"data.bin",id:path+"/data.bin",directory:false,size:12}];busy=false;success=true;changed()}
        function reset(){if(busy)return;path="";entries=[];changed()}
        function upload(local,permission){last={local:local,permission:permission};transfers++;return true}
        function download(id,folder){last={id:id,folder:folder};transfers++;return true}
        function copyEntry(id,cut){clipboardName="data.bin";clipboardCut=!!cut;last={id:id,cut:!!cut};changed();return true}
        function paste(){last={paste:true};transfers++;changed();return true}
        function removeEntry(id,confirmed){last={id:id,confirmed:confirmed};deletions++;changed();return true}
        function clearClipboard(){clipboardName="";changed()}
        function cancel(){busy=false;changed()}
    }
    QtObject {id:settings;property var saved:({});function value(key,fallback){return saved[key]===undefined?fallback:saved[key]}function setValue(key,value){saved[key]=value;return true}}
    Component {id:factory;App.FileBrowser {width:1000;height:650;files:tests.api;settings:tests.preferences}}
    QtObject {
        id:legacyTransfer
        property bool busy:false
        property bool success:true
        property int progress:0
        property string status:"等待操作"
        property var volumes:[]
        function refreshVolumes(){}
    }
    property var legacyApi:legacyTransfer
    Component {id:transferFactory;App.TransferPage {width:1000;height:650;controller:tests.legacyApi;files:tests.api}}
    function field(name){const value=findChild(page,name);verify(value!==null,name);return value}
    function init(){files.busy=false;files.visits=0;files.transfers=0;files.deletions=0;files.clipboardName="";files.entries=[];files.path="";settings.saved=({});files.adb.selectedSerial="B";page=factory.createObject(tests);verify(page!==null);wait(30)}
    function cleanup(){page.destroy();page=null;wait(10)}
    function test_startAndRestoreDoNotReadFiles(){
        compare(files.visits,0);compare(files.transfers,0);compare(field("fileTransport").currentIndex,0)
        field("fileTransport").currentIndex=2;field("fileHost").text="board.local";field("browserPath").text="/home/user"
        field("browserAllowWrite").checked=true
        field("fileHost").text="other.local";compare(field("browserAllowWrite").checked,false)
        compare(files.visits,0);compare(files.transfers,0)
        page.destroy();page=factory.createObject(tests);wait(30)
        compare(field("fileTransport").currentIndex,2);compare(field("fileHost").text,"other.local")
        compare(field("browserPath").text,"/home/user");compare(field("browserAllowWrite").checked,false)
        compare(files.visits,0);compare(files.transfers,0)
    }
    function test_adbSelectionAndExplicitFileDownload(){
        field("fileAdbDevices").activated(1);compare(files.adb.selectedSerial,"B")
        mouseClick(field("browserOpen"));compare(files.visits,1);compare(files.last.options.serial,"B");compare(files.last.path,"/sdcard")
        files.complete();wait(20);compare(field("browserEntries").count,1)
        page.selected=files.entries[0];field("browserSaveDirectory").text="C:/downloads"
        mouseClick(field("browserDownload"));compare(files.transfers,1);compare(files.last.id,"/sdcard/data.bin")
        field("fileTransport").currentIndex=3;compare(page.selected,null);compare(files.entries.length,0)
        verify(!field("browserDownload").enabled)
    }
    function test_mtpNeedsNoDriveAndSftpWriteIsExplicit(){
        field("fileTransport").currentIndex=1;compare(field("browserPath").text,"")
        mouseClick(field("browserOpen"));compare(files.kind,"mtp");compare(files.last.path,"");files.complete()
        field("fileTransport").currentIndex=2;field("fileHost").text="linux-board";field("browserPath").text="/tmp"
        mouseClick(field("browserOpen"));files.complete();wait(20)
        field("browserUploadFile").text="C:/sample.bin";verify(!field("browserUpload").enabled)
        field("browserAllowWrite").checked=true;verify(field("browserUpload").enabled)
        mouseClick(field("browserUpload"));compare(files.transfers,1);compare(files.last.permission,true)
        field("browserPath").text="/different";compare(field("browserAllowWrite").checked,false)
        field("browserAllowWrite").checked=true;field("browserUploadFile").text="C:/other.bin"
        compare(field("browserAllowWrite").checked,false)
        field("browserAllowWrite").checked=true;page.open("/subdirectory",true)
        compare(field("browserAllowWrite").checked,false);files.complete()
    }
    function test_busyTransferKeepsCancellationPageReachable(){
        page.destroy();page=transferFactory.createObject(tests);wait(30)
        files.busy=true
        verify(!field("legacyTransferTab").enabled)
        files.busy=false;mouseClick(field("legacyTransferTab"));compare(page.legacy,true)
        legacyTransfer.busy=true
        verify(!field("fileBrowserTab").enabled)
        legacyTransfer.busy=false
    }
    function test_automaticAdbTargetChangeInvalidatesOldListing(){
        mouseClick(field("browserOpen"));files.complete();wait(20)
        page.selected=files.entries[0];files.adb.selectedSerial="A";wait(10)
        compare(files.entries.length,0);compare(page.selected,null)
        mouseClick(field("browserOpen"));compare(files.busy,true)
        files.adb.selectedSerial="B";files.complete();wait(20)
        compare(files.entries.length,0);verify(!field("browserDownload").enabled)
    }
    function test_clipboardButtonsAndDeleteConfirmation(){
        verify(!field("browserPaste").enabled);verify(!field("browserCut").enabled)
        mouseClick(field("browserOpen"));files.complete();wait(20);page.selected=files.entries[0]
        grabImage(page).save(quickEvidenceDirectory+"/file-actions-toolbar.png")
        mouseClick(field("browserCopy"));compare(files.clipboardCut,false);compare(files.last.id,"/sdcard/data.bin")
        mouseClick(field("browserCut"));compare(files.clipboardCut,true);compare(files.transfers,0)
        mouseClick(field("browserDelete"));wait(30);compare(files.deletions,0);verify(field("browserDeleteDialog").visible)
        grabImage(tests.Window.window.contentItem).save(quickEvidenceDirectory+"/file-actions-delete.png")
        field("browserDeleteDialog").reject();compare(files.deletions,0)
        mouseClick(field("browserDelete"));wait(30);mouseClick(field("browserConfirmDelete"));compare(files.deletions,1);compare(files.last.confirmed,true)
        field("fileTransport").currentIndex=3;compare(files.clipboardName,"data.bin")
        field("browserPath").text="C:/out"
        mouseClick(field("browserOpen"));files.complete();wait(20)
        verify(field("browserPaste").enabled);mouseClick(field("browserPaste"));compare(files.transfers,1)
        files.busy=true;verify(!field("browserDelete").enabled);verify(!field("browserPaste").enabled)
    }
}
