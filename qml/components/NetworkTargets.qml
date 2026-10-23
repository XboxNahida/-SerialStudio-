import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var discovery: null
    property var settings: null
    property bool advanced: false
    property string protocol: "tcp"
    property string host: ""
    property int port: 8765
    property string selectedAddress: ""
    property var saved: []
    property bool historyValid: true
    property string error: ""
    readonly property var interfaces: discovery ? discovery.interfaces.filter(function(item){return item.up && item.address.indexOf("127.")!==0}) : []
    readonly property var targets: {
        const merged=[],seen={}
        const list=(discovery ? discovery.devices : []).concat(saved)
        for(const device of list) {
            if(!device || device.type!=="network" || !device.config || !device.config.host || !device.config.port)continue
            const address=device.config.host+":"+device.config.port
            if(seen[address])continue
            seen[address]=true
            merged.push(Object.assign({},device,{address:address,label:(device.name || address)+" · "+address+(device.state==="offline" ? " · 离线" : device.state==="known" ? " · 历史" : "")}))
        }
        return merged
    }
    signal targetChosen(var device)
    spacing: 7
    function restoreHistory() {
        const value=settings ? settings.value("devices/saved",[]) : []
        historyValid=Array.isArray(value)
        if(!historyValid){error="历史目标格式错误，保留原数据。";return}
        saved=value
        if(discovery)discovery.setSavedDevices(saved)
        error=""
    }
    function saveCurrent() {
        const value=settings ? settings.value("devices/saved",[]) : []
        if(!settings || !historyValid || !Array.isArray(value)){error="历史目标不可保存，原数据未覆盖。";return false}
        const address=host.trim()
        if(!address || /[\s/@\\]/.test(address) || !Number.isInteger(port) || port<1 || port>65535){error="请先填写有效主机和端口。";return false}
        const target={type:"network",name:address+":"+port,address:address+":"+port,state:"known",config:{host:address,port:port,mode:0,protocol:protocol}}
        const next=value.filter(function(item){return !item || item.type!=="network" || item.address!==target.address}).concat([target])
        if(!settings.setValue("devices/saved",next)){error=settings.error || "历史目标保存失败。";return false}
        saved=next;if(discovery)discovery.setSavedDevices(next);error="";return true
    }
    function scan() {
        if(!discovery || discovery.scanning)return false
        const ports=scanPorts.text.split(/[\s,，;]+/).filter(function(value){return value.length>0}).map(Number)
        if(!scanCidr.text.trim() || !ports.length || ports.some(function(value){return !Number.isInteger(value) || value<1 || value>65535})) {
            error="请填写 CIDR 网段和 1–65535 的 TCP 端口。";return false
        }
        error=""
        return discovery.scanSubnet(scanCidr.text.trim(),ports)
    }
    function refreshSelection() {targetBox.currentIndex=targets.findIndex(function(item){return item.address===selectedAddress})}
    onSelectedAddressChanged: refreshSelection()
    onTargetsChanged: refreshSelection()
    onSettingsChanged: restoreTimer.restart()
    onDiscoveryChanged: restoreTimer.restart()
    Component.onCompleted: restoreHistory()
    Timer {id:restoreTimer;interval:0;onTriggered:root.restoreHistory()}
    Text {text:"发现 / 历史网络目标";color:Theme.muted;font.pixelSize:12}
    SelectBox {
        id: targetBox;objectName:"networkTarget";Layout.fillWidth:true
        model:root.targets;textRole:"label";currentIndex:-1
        displayText:currentIndex>=0 ? currentText : root.selectedAddress || "选择广播或历史目标…"
        popup.onAboutToShow: {root.restoreHistory();if(root.discovery)root.discovery.refresh()}
        onActivated: function(index) {
            if(index<0 || index>=root.targets.length)return
            const device=root.targets[index]
            root.selectedAddress=device.address
            root.targetChosen(device)
        }
    }
    Text {Layout.fillWidth:true;text:"选择只填入地址；连接由你确认。";color:Theme.subtle;font.pixelSize:10;wrapMode:Text.Wrap}
    ColumnLayout {
        visible:root.advanced;Layout.fillWidth:true;spacing:7
        ActionButton {text:"保存当前目标";Layout.fillWidth:true;enabled:!!root.settings;onClicked:root.saveCurrent()}
        Text {text:"主动扫描 TCP 服务";color:Theme.muted;font.pixelSize:12}
        SelectBox {
            id:interfaceBox;objectName:"inlineScanInterface";Layout.fillWidth:true
            model:root.interfaces.map(function(item){return item.name+" · "+item.address})
            displayText:count ? currentText : "无可用网卡，可手填 CIDR"
        }
        InputBox {
            id:scanCidr;objectName:"inlineScanCidr";Layout.fillWidth:true
            text:interfaceBox.currentIndex>=0 && root.interfaces[interfaceBox.currentIndex] ? root.interfaces[interfaceBox.currentIndex].address+"/"+Math.max(24,Math.min(32,root.interfaces[interfaceBox.currentIndex].prefixLength)) : ""
            placeholderText:"例如 192.168.1.0/24"
        }
        InputBox {id:scanPorts;objectName:"inlineScanPorts";Layout.fillWidth:true;text:root.protocol==="ssh" ? "22" : "8765";placeholderText:"TCP 端口，逗号分隔"}
        RowLayout {
            Layout.fillWidth:true
            ActionButton {objectName:"inlineScanButton";text:"扫描网段";Layout.fillWidth:true;enabled:!!root.discovery && !root.discovery.scanning;onClicked:root.scan()}
            ActionButton {text:"取消";enabled:!!root.discovery && root.discovery.scanning;onClicked:root.discovery.cancelScan()}
        }
        Text {Layout.fillWidth:true;text:"只在点击后探测 /24–/32 私有或回环网段，不打开调试连接。";color:Theme.subtle;font.pixelSize:10;wrapMode:Text.Wrap}
        Text {Layout.fillWidth:true;text:root.discovery ? root.discovery.status : "发现服务未加载";color:Theme.muted;font.pixelSize:10;wrapMode:Text.Wrap}
    }
    Text {visible:text.length>0;Layout.fillWidth:true;text:root.error;color:Theme.red;font.pixelSize:11;wrapMode:Text.Wrap}
}
