import QtQuick
import QtQuick.Controls.Basic
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    id:testCase;name:"独立指令库更新交互";when:windowShown;visible:true;width:860;height:570
    QtObject {
        id:appUpdate
        property bool updateAvailable:false
        property bool busy:false
        property bool canDownload:true
        property bool downloadReady:false
        property bool uninstallAvailable:false
        property string latestVersion:"1.3.0"
        property string status:"应用状态"
        property real progress:0
        property int checks:0
        signal changed()
        function check(manual){checks++;updateAvailable=true;changed()}
        function ignore(){updateAvailable=false;changed()}
        function download(){}
        function install(){}
        function uninstall(){}
        function openRelease(){}
    }
    QtObject {
        id:catalogUpdate
        property bool updateAvailable:false
        property bool busy:false
        property bool canDownload:true
        property string latestVersion:"1.1.0"
        property string status:"库状态"
        property string lastChecked:"2026-10-25"
        property real progress:0
        property int checks:0
        signal changed()
        function check(manual){checks++;updateAvailable=true;changed()}
        function ignore(){updateAvailable=false;changed()}
        function download(){}
    }
    QtObject {id:catalogStore;property string version:"1.0.0";property string source:"初始库";property string status:"加载成功";property int count:589;property bool hasPrevious:true;function rollback(){} function openDirectory(){}}
    Component {id:noticeComponent;App.UpdateNotice {updater:appUpdate;catalogUpdater:catalogUpdate}}
    Component {id:settingsComponent;App.SettingsPage {width:840;height:550;backend:null;updater:appUpdate;catalog:catalogStore;catalogUpdater:catalogUpdate;appVersion:"1.2.0"}}
    function init(){appUpdate.updateAvailable=false;catalogUpdate.updateAvailable=false;appUpdate.checks=0;catalogUpdate.checks=0}
    function test_双更新可分别忽略() {
        const notice=createTemporaryObject(noticeComponent,testCase);verify(notice)
        appUpdate.updateAvailable=true;catalogUpdate.updateAvailable=true;appUpdate.changed();catalogUpdate.changed()
        const dialog=findChild(notice,"updateNoticeDialog");verify(dialog);tryCompare(dialog,"visible",true)
        wait(200)
        compare(dialog.modal,false)
        const ignoreApp=findChild(notice,"noticeIgnoreApp");verify(ignoreApp);mouseClick(ignoreApp)
        compare(appUpdate.updateAvailable,false);compare(catalogUpdate.updateAvailable,true)
        mouseClick(findChild(notice,"noticeIgnoreCatalog"));tryCompare(dialog,"visible",false)
    }
    function test_稍后保留更新状态但不重复弹出() {
        const notice=createTemporaryObject(noticeComponent,testCase);verify(notice)
        appUpdate.updateAvailable=true;appUpdate.changed()
        const dialog=findChild(notice,"updateNoticeDialog");tryCompare(dialog,"visible",true)
        mouseClick(findChild(notice,"noticeLater"));tryCompare(dialog,"visible",false)
        compare(appUpdate.updateAvailable,true);appUpdate.changed();wait(350);compare(dialog.visible,false)
        appUpdate.latestVersion="1.4.0";appUpdate.changed();tryCompare(dialog,"visible",true);appUpdate.latestVersion="1.3.0"
    }
    function test_设置检查置顶且两种按钮独立() {
        const page=createTemporaryObject(settingsComponent,testCase);verify(page);wait(50)
        const panel=findChild(page,"updateSettingsPanel");verify(panel);compare(panel.y,0)
        const appButton=findChild(page,"checkUpdates"),libButton=findChild(page,"checkCatalogUpdates")
        verify(appButton);verify(libButton);verify(appButton.visible);verify(libButton.visible)
        appButton.clicked();compare(appUpdate.checks,1);compare(catalogUpdate.checks,0)
        libButton.clicked();compare(catalogUpdate.checks,1)
        verify(findChild(page,"importCatalog").enabled);verify(findChild(page,"rollbackCatalog").enabled)
    }
}
