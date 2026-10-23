import QtQuick
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    id: testCase
    name: "页面内网络目标"
    when: windowShown
    width: 420; height: 700; visible: true
    property var picker: null
    QtObject {
        id: discoveryStub
        property var devices: [{type:"network",address:"192.168.8.3:22",name:"广播板卡",state:"available",config:{host:"192.168.8.3",port:22}},
            {type:"serial",address:"COM8",name:"串口",config:{}}]
        property var interfaces: [{name:"以太网",address:"192.168.8.2",prefixLength:24,up:true}]
        property bool scanning: false
        property string status: "已发现广播目标"
        property int refreshes: 0
        property int scans: 0
        property string cidr: ""
        property var ports: []
        property var saved: []
        function refresh(){refreshes++}
        function setSavedDevices(value){saved=value}
        function scanSubnet(value,items){cidr=value;ports=items;scans++;scanning=true;return true}
        function cancelScan(){scanning=false}
    }
    QtObject {
        id: settingsStub
        property var values: ({})
        property string error: ""
        function value(key,fallback){return values[key]===undefined?fallback:values[key]}
        function setValue(key,value){const next=Object.assign({},values);next[key]=value;values=next;return true}
    }
    Component {id: factory; App.NetworkTargets {discovery:discoveryStub;settings:settingsStub;host:"192.168.8.9";port:2222;protocol:"ssh"}}
    SignalSpy {id: chosen;target:picker;signalName:"targetChosen"}
    function init(){
        discoveryStub.refreshes=0;discoveryStub.scans=0;discoveryStub.scanning=false;chosen.clear()
        discoveryStub.devices=[{type:"network",address:"192.168.8.3:22",name:"广播板卡",state:"available",config:{host:"192.168.8.3",port:22}},{type:"serial",address:"COM8",name:"串口",config:{}}]
        settingsStub.values=({"devices/saved":[{type:"network",address:"192.168.8.4:2022",name:"历史板卡",state:"known",config:{host:"192.168.8.4",port:2022}}]})
        picker=factory.createObject(testCase,{width:400});verify(picker!==null);wait(30)
    }
    function cleanup(){picker.destroy();picker=null;wait(10)}
    function test_broadcastHistoryAndExplicitSelection(){
        compare(picker.targets.length,2);compare(discoveryStub.scans,0);compare(chosen.count,0)
        const box=findChild(picker,"networkTarget")
        box.popup.open();wait(20);box.popup.close();verify(discoveryStub.refreshes>0)
        compare(chosen.count,0);compare(discoveryStub.scans,0)
        box.activated(1);compare(chosen.count,1)
        compare(chosen.signalArguments[0][0].config.host,"192.168.8.4")
        discoveryStub.devices=[];wait(10);compare(chosen.count,1)
        compare(picker.selectedAddress,"192.168.8.4:2022")
    }
    function test_saveAndManualScanOnly(){
        verify(picker.saveCurrent());compare(settingsStub.values["devices/saved"].length,2)
        compare(discoveryStub.saved.length,2);compare(chosen.count,0);compare(discoveryStub.scans,0)
        picker.advanced=true;wait(20)
        findChild(picker,"inlineScanCidr").text="127.0.0.1/32"
        findChild(picker,"inlineScanPorts").text="22,2022"
        verify(picker.scan());compare(discoveryStub.scans,1);compare(discoveryStub.cidr,"127.0.0.1/32")
        compare(discoveryStub.ports.length,2);compare(chosen.count,0)
        discoveryStub.cancelScan();findChild(picker,"inlineScanPorts").text="70000"
        verify(!picker.scan());compare(discoveryStub.scans,1)
    }
    function test_badHistoryAndNullBackend(){
        settingsStub.values=({"devices/saved":"损坏数据"});picker.restoreHistory()
        verify(!picker.saveCurrent());compare(settingsStub.values["devices/saved"],"损坏数据")
        picker.discovery=null;verify(!picker.scan());wait(10)
    }
}
