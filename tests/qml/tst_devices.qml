import QtQuick
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    name: "统一设备页"
    when: windowShown
    width: 1100; height: 740; visible: true
    property var page
    property bool globalDetailed: false
    QtObject {
        id: discoveryStub
        property var devices: [
            {id:"serial:one",type:"serial",name:"测试串口",address:"COM7",state:"available",detail:"USB UART",actions:["serial"],config:{port:"COM7"}},
            {id:"adb:one",type:"adb",name:"RK3576",address:"board-one",state:"unauthorized",detail:"尚未授权",actions:[],config:{serial:"board-one"}},
            {id:"volume:one",type:"volume",name:"测试盘",address:"E:/",state:"offline",detail:"已拔出",actions:[],config:{path:"E:/"}}]
        property var interfaces: [{name:"Ethernet",id:"eth0",address:"192.168.8.20",prefixLength:24,type:"ethernet",up:true}]
        property bool scanning: false
        property string status: "测试枚举完成"
        property var saved: []
        property string lastCidr: ""
        property var lastPorts: []
        property int refreshes: 0
        signal changed()
        function refresh() {refreshes++}
        function setSavedDevices(items) {saved=items}
        function scanSubnet(cidr,ports) {lastCidr=cidr;lastPorts=ports;scanning=true;return true}
        function cancelScan() {scanning=false}
    }
    QtObject {
        id: settingsStub
        property var values: ({"devices/saved":[]})
        property string error: ""
        function value(key,fallback) {return values[key]===undefined ? fallback : values[key]}
        function setValue(key,value) {values=Object.assign({},values);values[key]=value;return true}
    }
    Component {id: factory;App.DevicesPage {discovery:discoveryStub;settings:settingsStub;detailed:testCase.globalDetailed}}
    SignalSpy {id: used;target:page;signalName:"useDevice"}
    function init() {
        settingsStub.values=({"devices/saved":[]});discoveryStub.saved=[];discoveryStub.scanning=false;discoveryStub.lastCidr="";globalDetailed=false;used.clear()
        page=factory.createObject(testCase,{width:1080,height:700});verify(page!==null);verify(waitForPolish(page));wait(100)
    }
    id: testCase
    function cleanup() {page.destroy();wait(20)}
    function test_filterAndSelectionNeverConnects() {
        compare(page.filteredDevices.length,3)
        findChild(page,"deviceSearch").text="COM7";tryCompare(page,"deviceCount",1)
        page.selectDevice(page.filteredDevices[0]);compare(used.count,0)
        mouseClick(findChild(page,"useSelectedDevice"));compare(used.count,1);compare(used.signalArguments[0][0].config.port,"COM7")
        findChild(page,"deviceSearch").text="";findChild(page,"deviceStateFilter").currentIndex=1;tryCompare(page,"deviceCount",1)
        findChild(page,"deviceStateFilter").currentIndex=0;page.selectDevice(discoveryStub.devices[1])
        verify(!findChild(page,"useSelectedDevice").enabled)
        verify(!page.useSelected());compare(used.count,1)
    }
    function test_explicitScanAndCancel() {
        compare(discoveryStub.lastCidr,"");compare(used.count,0)
        mouseClick(findChild(page,"scanDevices"));compare(discoveryStub.lastCidr,"192.168.8.20/24")
        compare(discoveryStub.lastPorts.length,5);verify(discoveryStub.scanning)
        mouseClick(findChild(page,"cancelDeviceScan"));verify(!discoveryStub.scanning)
        page.detailed=true;findChild(page,"scanCidr").text="192.168.8.10/32";findChild(page,"scanPorts").text="22,8765"
        mouseClick(findChild(page,"scanDevices"));compare(discoveryStub.lastPorts.length,2);compare(discoveryStub.lastPorts[1],8765)
    }
    function test_savedAddressesAndRestore() {
        verify(page.saveAddress("测试地址","192.168.8.10",8765))
        compare(settingsStub.values["devices/saved"].length,1);compare(discoveryStub.saved[0].state,"known")
        verify(!page.saveAddress("错误","bad host",70000));compare(settingsStub.values["devices/saved"].length,1)
        verify(page.removeSavedAddress("192.168.8.10:8765"));compare(discoveryStub.saved.length,0)
        verify(page.saveAddress("恢复地址","192.168.8.11",22));page.destroy()
        page=factory.createObject(testCase,{width:1080,height:700});wait(30)
        compare(discoveryStub.saved.length,1);compare(discoveryStub.lastCidr,"")
    }
    function test_compactLayout() {
        page.width=880;page.height=600;verify(waitForPolish(page));wait(50)
        const normal=grabImage(page);verify(normal.width>0);normal.save("devices-compact.png")
        page.detailed=true;verify(waitForPolish(page));wait(50)
        const advanced=grabImage(page);verify(advanced.width>0);advanced.save("devices-advanced.png")
        const history=findChild(page,"addSavedDevice")
        verify(history.mapToItem(page,0,0).y+history.height<=page.height)
    }
    function test_advancedKeepsGlobalDetailedBinding() {
        compare(page.detailed,false)
        mouseClick(findChild(page,"deviceDetails"))
        compare(page.detailed,false)
        compare(page.advanced,true)
        globalDetailed=true;tryCompare(page,"detailed",true)
        mouseClick(findChild(page,"deviceDetails"));compare(page.advanced,false)
        compare(findChild(page,"scanCidr").visible,true)
        globalDetailed=false;tryCompare(page,"detailed",false)
        compare(findChild(page,"scanCidr").visible,false)
    }
    function test_localOptionsRestoreWithoutActivity() {
        findChild(page,"deviceSearch").text="UART"
        findChild(page,"deviceTypeFilter").currentIndex=1
        findChild(page,"deviceStateFilter").currentIndex=1
        findChild(page,"deviceDetails").clicked()
        findChild(page,"scanCidr").text="192.168.8.10/32"
        findChild(page,"scanPorts").text="22,8765"
        findChild(page,"savedDeviceName").text="草稿名称"
        findChild(page,"savedDeviceHost").text="192.168.8.50"
        findChild(page,"savedDevicePort").text="1883"
        page.selectDevice(discoveryStub.devices[0]);wait(10)
        const refreshes=discoveryStub.refreshes
        page.destroy();page=factory.createObject(testCase,{width:1080,height:700});wait(100)
        compare(findChild(page,"deviceSearch").text,"UART")
        compare(findChild(page,"deviceTypeFilter").currentIndex,1)
        compare(findChild(page,"deviceStateFilter").currentIndex,1)
        compare(page.advanced,true);compare(page.detailed,false)
        compare(findChild(page,"scanCidr").text,"192.168.8.10/32")
        compare(findChild(page,"scanPorts").text,"22,8765")
        compare(findChild(page,"savedDeviceName").text,"草稿名称")
        compare(findChild(page,"savedDeviceHost").text,"192.168.8.50")
        compare(findChild(page,"savedDevicePort").text,"1883")
        compare(page.selectedId,"serial:one")
        compare(used.count,0);compare(discoveryStub.lastCidr,"");compare(discoveryStub.refreshes,refreshes)
        verify(!discoveryStub.scanning)
    }
    function test_emptyScanDraftRestoresWithoutReplacingIt() {
        findChild(page,"scanCidr").text=""
        page.destroy();page=factory.createObject(testCase,{width:1080,height:700});wait(50)
        compare(findChild(page,"scanCidr").text,"")
        compare(discoveryStub.lastCidr,"");compare(used.count,0)
    }
    function test_savedInterfaceRestoresByStableId() {
        const original=discoveryStub.interfaces
        try {
            discoveryStub.interfaces=original.concat([{name:"Wi-Fi",id:"wifi0",address:"192.168.9.20",prefixLength:24,type:"wifi",up:true}])
            const picker=findChild(page,"deviceInterface");picker.currentIndex=1;picker.activated(1)
            compare(settingsStub.values["devices/interfaceId"],"wifi0")
            discoveryStub.interfaces=[discoveryStub.interfaces[1],discoveryStub.interfaces[0]]
            page.destroy();page=factory.createObject(testCase,{width:1080,height:700});wait(50)
            compare(page.currentInterface.id,"wifi0")
            compare(discoveryStub.lastCidr,"");compare(used.count,0)
        } finally {discoveryStub.interfaces=original}
    }
}
