import QtQuick
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    id:testCase
    name:"工具管理窄窗口";when:windowShown;visible:true;width:740;height:440
    QtObject {
        id:tools
        property string status:"必备工具随安装包提供"
        property string directory:"C:/Users/一个很长的用户名/AppData/Local/SerialStudio/tools"
        property bool busy:false
        property real progress:0
        property bool missing:true
        property int downloads:0
        signal changed()
        function missingTools(names,paths){return missing && names.indexOf("ssh")>=0?[{tool:"ssh",componentId:"openssh",componentName:"OpenSSH"}]:[]}
        function install(id){downloads++}
        property var components:[{id:"openssh",name:"OpenSSH Client",version:"10.4.0.0p1-Preview",available:false,installed:false,url:"https://github.com/example",required:true,paths:[{name:"ssh",custom:"",resolved:"C:/Users/很长很长很长很长很长的用户名/AppData/Local/SerialStudio/tools/openssh/10.4.0.0p1-Preview/OpenSSH-Win64/ssh.exe"}]}]
        function setPath(name,path){return true}
        function refresh(){}
    }
    Component {id:page;App.SettingsPage {backend:null;toolManager:tools;width:740;height:440}}
    property var toolService:tools
    Component {id:hint;App.ToolHint {width:600;manager:testCase.toolService;tools:["ssh"]}}
    SignalSpy {id:requested;signalName:"settingsRequested"}
    SignalSpy {id:returned;signalName:"returnRequested"}
    function test_missingToolNavigationWithoutDownload(){
        tools.missing=true;tools.downloads=0
        const h=createTemporaryObject(hint,testCase);verify(h);requested.target=h;requested.clear()
        verify(h.visible);mouseClick(findChild(h,"missingTool-ssh"))
        compare(requested.count,1);compare(requested.signalArguments[0][0],"openssh");compare(tools.downloads,0)
        const p=createTemporaryObject(page,testCase);p.canReturn=true;returned.target=p;returned.clear()
        p.showTools("openssh");wait(100);compare(p.focusedTool,"openssh");verify(p.contentItem.contentY>0)
        mouseClick(findChild(p,"returnFromTools"));compare(returned.count,1)
        p.showUpdates();compare(p.contentItem.contentY,0)
        tools.missing=false;tools.changed();tryCompare(h,"visible",false)
        h.tools=[];tools.missing=true;tools.changed();compare(h.visible,false)
    }
    function test_buttonsFitBothModes(){
        const p=createTemporaryObject(page,this)
        verify(p!==null);wait(100)
        for(const advanced of [false,true]){
            p.toolAdvanced=advanced;wait(100)
            const button=findChild(p,"toolOfficialButton")
            verify(button!==null)
            const point=button.mapToItem(p,button.width,0)
            verify(point.x<=p.width,"工具操作超出窗口："+point.x)
        }
    }
}
