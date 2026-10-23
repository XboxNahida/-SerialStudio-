import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components" as C

Item {
    id: root
    required property var discovery
    property var settings: null
    property var terminal: null
    property bool detailed: false
    property bool advanced: false
    property bool restoring: true
    property string selectedId: ""
    property var savedDevices: []
    property string historyError: ""
    property bool historyValid: true
    readonly property var selectedDevice: discovery ? discovery.devices.find(function(x){return x.id===selectedId}) || ({}) : ({})
    readonly property var filteredDevices: {
        const needle=search.text.trim().toLowerCase()
        const type=["","serial","adb","usb","volume","network"][typeFilter.currentIndex]
        return discovery ? discovery.devices.filter(function(item) {
            const online=item.state==="available" || item.state==="connected"
            const stateOk=stateFilter.currentIndex===0 || (stateFilter.currentIndex===1 && online)
                || (stateFilter.currentIndex===2 && item.state==="offline")
                || (stateFilter.currentIndex===3 && item.state==="unauthorized")
                || (stateFilter.currentIndex===4 && item.state==="known")
            return (!type || item.type===type) && stateOk && (!needle || [item.name,item.address,item.detail].join(" ").toLowerCase().indexOf(needle)>=0)
        }) : []
    }
    readonly property int deviceCount: filteredDevices.length
    readonly property var scanInterfaces: discovery ? discovery.interfaces.filter(function(x){return x.up && x.address.indexOf("127.")!==0}) : []
    readonly property var currentInterface: interfacePicker.currentIndex>=0 ? scanInterfaces[interfacePicker.currentIndex] || ({}) : ({})
    readonly property string defaultCidr: currentInterface.address ? currentInterface.address+"/"+Math.max(24,Math.min(32,currentInterface.prefixLength)) : ""
    readonly property bool usable: !!selectedDevice.id && selectedDevice.state!=="unauthorized" && selectedDevice.state!=="offline" && (selectedDevice.actions || []).length>0
    readonly property bool selectedSaved: savedDevices.some(function(x){return x.type===selectedDevice.type && x.address===selectedDevice.address})
    signal useDevice(var device)
    Rectangle {anchors.fill:parent;color:C.Theme.background;z:-1}

    function stateLabel(state) {return ({available:"可用",connected:"已连接",offline:"离线",unauthorized:"未授权",known:"待确认"})[state] || "未知"}
    function typeLabel(type) {return ({serial:"串口",adb:"ADB",usb:"USB 设备",volume:"磁盘 / 卷",network:"网络目标"})[type] || type}
    function selectDevice(item) {selectedId=item.id || ""}
    function useSelected() {if(!usable)return false;useDevice(selectedDevice);return true}
    function saveOption(key,value) {
        if(restoring || !settings)return
        if(!settings.setValue("devices/"+key,value))historyError=settings.error || "设备页面选项保存失败"
    }
    function restoreOptions() {
        restoring=true
        loadSaved()
        if(settings) {
            search.text=String(settings.value("devices/search",""))
            typeFilter.currentIndex=Math.max(0,Math.min(typeFilter.count-1,Number(settings.value("devices/typeFilter",0)) || 0))
            stateFilter.currentIndex=Math.max(0,Math.min(stateFilter.count-1,Number(settings.value("devices/stateFilter",0)) || 0))
            advanced=!!settings.value("devices/advanced",false)
            selectedId=String(settings.value("devices/selectedId",""))
            scanCidr.text=String(settings.value("devices/scanCidr",defaultCidr))
            scanPorts.text=String(settings.value("devices/scanPorts","22,8765,1883,80,443"))
            savedName.text=String(settings.value("devices/savedName",""))
            savedHost.text=String(settings.value("devices/savedHost",""))
            savedPort.text=String(settings.value("devices/savedPort","8765"))
            restoreInterface()
        }
        restoring=false
    }
    function restoreInterface() {
        if(!settings)return
        const id=String(settings.value("devices/interfaceId",""))
        const index=scanInterfaces.findIndex(function(item){return (item.id || item.address)===id})
        if(index>=0)interfacePicker.currentIndex=index
    }
    function loadSaved() {
        const list=settings ? settings.value("devices/saved",[]) : []
        historyValid=Array.isArray(list)
        if(!historyValid) {historyError="历史设备格式错误，保留原数据；请检查配置。";return}
        savedDevices=list
        if(discovery)discovery.setSavedDevices(savedDevices)
    }
    function persistSaved(list) {
        if(!settings || !historyValid) {historyError="设备配置仓库不可用，未覆盖历史内容。";return false}
        if(!settings.setValue("devices/saved",list)) {historyError=settings.error || "历史设备保存失败";return false}
        savedDevices=list;historyError=""
        if(discovery)discovery.setSavedDevices(list)
        return true
    }
    function validHost(host) {
        if(!host || host.length>253 || !/^[A-Za-z0-9][A-Za-z0-9.-]*$/.test(host))return false
        if(/^[0-9.]+$/.test(host)) {
            const parts=host.split(".")
            return parts.length===4 && parts.every(function(x){return /^\d{1,3}$/.test(x) && Number(x)<=255})
        }
        return true
    }
    function saveAddress(name,hostValue,portValue) {
        const host=hostValue.trim(),port=Number(portValue)
        if(!validHost(host) || !Number.isInteger(port) || port<1 || port>65535) {historyError="请填写有效 IPv4 / 主机名和 1–65535 端口。";return false}
        const address=host+":"+port
        const item={type:"network",name:name.trim() || address,address:address,state:"known",
            detail:"历史保存地址；尚未确认在线",actions:["tcp"],config:{host:host,port:port,mode:0}}
        return persistSaved(savedDevices.filter(function(x){return x.type!=="network" || x.address!==address}).concat([item]))
    }
    function saveSelected() {
        if(!selectedDevice.id)return false
        const item=Object.assign({},selectedDevice,{state:"known"})
        return persistSaved(savedDevices.filter(function(x){return x.type!==item.type || x.address!==item.address}).concat([item]))
    }
    function removeSavedAddress(address,type) {
        return persistSaved(savedDevices.filter(function(x){return x.address!==address || (type && x.type!==type)}))
    }
    function scan() {
        if(!discovery || discovery.scanning)return false
        const cidr=detailed || advanced ? scanCidr.text.trim() : defaultCidr
        const text=detailed || advanced ? scanPorts.text.trim() : "22,8765,1883,80,443"
        const ports=text.split(/[\s,，;]+/).filter(function(x){return x.length>0}).map(Number)
        if(!cidr || !ports.length || ports.some(function(x){return !Number.isInteger(x) || x<1 || x>65535})) {historyError="请先选择网卡，或在高级中填写 CIDR 和有效端口。";return false}
        historyError=""
        if(settings) {settings.setValue("devices/scanCidr",cidr);settings.setValue("devices/scanPorts",text)}
        return discovery.scanSubnet(cidr,ports)
    }
    onSettingsChanged: restoreTimer.restart()
    onDiscoveryChanged: {if(discovery)discovery.setSavedDevices(savedDevices)}
    onSelectedIdChanged: saveOption("selectedId",selectedId)
    onAdvancedChanged: saveOption("advanced",advanced)
    onScanInterfacesChanged: restoreInterface()
    Component.onCompleted: restoreOptions()
    Timer {id:restoreTimer;interval:0;onTriggered:root.restoreOptions()}

    ColumnLayout {
        anchors.fill: parent; spacing: 12
        RowLayout {
            Text {text:"设备探索";font.pixelSize:22;font.weight:Font.DemiBold;color:C.Theme.text}
            Text {text:root.deviceCount+" 个结果";color:C.Theme.muted;font.pixelSize:12}
            Item {Layout.fillWidth:true}
            C.ActionButton {objectName:"deviceDetails";text:root.advanced ? "收起高级" : "高级选项";onClicked:root.advanced=!root.advanced}
            C.ActionButton {objectName:"refreshDevices";text:"刷新设备";enabled:!!root.discovery;onClicked:root.discovery.refresh()}
        }
        Text {Layout.fillWidth:true;text:"自动读取串口、ADB、USB、磁盘和网卡；网络目标来自服务声明、历史地址或你主动发起的扫描。";color:C.Theme.muted;font.pixelSize:12;wrapMode:Text.Wrap}
        C.Panel {
            Layout.fillWidth:true;Layout.preferredHeight:root.detailed || root.advanced ? 168 : 94
            ColumnLayout {
                anchors.fill:parent;anchors.margins:12;spacing:7
                RowLayout {
                    Text {text:"本机网卡 / IPv4";color:C.Theme.muted;font.pixelSize:12}
                    C.SelectBox {
                        id:interfacePicker;objectName:"deviceInterface";Layout.fillWidth:true
                        model:root.scanInterfaces.map(function(x){return x.name+" · "+x.address+"/"+x.prefixLength+" · "+(x.type==="wifi" ? "无线" : x.type==="ethernet" ? "有线" : "其他")})
                        onActivated: {const item=root.currentInterface;if(item.address)root.saveOption("interfaceId",item.id || item.address)}
                    }
                    C.ActionButton {objectName:"scanDevices";text:"扫描当前网段";enabled:!!root.discovery && !root.discovery.scanning && (root.detailed || root.advanced || !!root.defaultCidr);onClicked:root.scan()}
                    C.ActionButton {objectName:"cancelDeviceScan";text:"取消";enabled:!!root.discovery && root.discovery.scanning;onClicked:root.discovery.cancelScan()}
                }
                Text {Layout.fillWidth:true;text:root.scanInterfaces.length ? "选择网卡后主动扫描 /24–/32 私有网段；只探测 TCP 端口，不建立调试会话。静默服务不会被自动识别。" : "暂无启用的非回环 IPv4 网卡；高级中可指定私有或回环 CIDR。";color:C.Theme.muted;wrapMode:Text.Wrap;font.pixelSize:11}
                RowLayout {
                    visible:root.detailed || root.advanced
                    C.InputBox {id:scanCidr;objectName:"scanCidr";Layout.fillWidth:true;text:root.defaultCidr;placeholderText:"IPv4 CIDR，例如 192.168.1.0/24";onTextChanged:root.saveOption("scanCidr",text)}
                    C.InputBox {id:scanPorts;objectName:"scanPorts";Layout.fillWidth:true;text:"22,8765,1883,80,443";placeholderText:"TCP 端口，以逗号分隔";onTextChanged:root.saveOption("scanPorts",text)}
                }
                Text {visible:root.detailed || root.advanced;Layout.fillWidth:true;text:root.discovery ? root.discovery.interfaces.map(function(x){return x.name+" "+x.address+"/"+x.prefixLength+" "+(x.up ? "启用" : "停用")}).join("；") : "";color:C.Theme.muted;font.pixelSize:11;elide:Text.ElideRight}
            }
        }
        RowLayout {
            C.InputBox {id:search;objectName:"deviceSearch";Layout.fillWidth:true;placeholderText:"搜索设备名、COM、序列号或地址";onTextChanged:root.saveOption("search",text)}
            C.SelectBox {id:typeFilter;objectName:"deviceTypeFilter";model:["全部类型","串口","ADB","USB 设备","磁盘 / 卷","网络目标"];Layout.preferredWidth:140;onCurrentIndexChanged:root.saveOption("typeFilter",currentIndex)}
            C.SelectBox {id:stateFilter;objectName:"deviceStateFilter";model:["全部状态","在线可用","离线","未授权","待确认"];Layout.preferredWidth:130;onCurrentIndexChanged:root.saveOption("stateFilter",currentIndex)}
        }
        RowLayout {
            Layout.fillWidth:true;Layout.fillHeight:true;spacing:12
            C.Panel {
                Layout.fillWidth:true;Layout.fillHeight:true
                ListView {
                    id:list;objectName:"unifiedDeviceList";anchors.fill:parent;anchors.margins:8;clip:true;spacing:6;model:root.filteredDevices
                    ScrollBar.vertical:ScrollBar {}
                    delegate:Rectangle {
                        id:card;required property var modelData
                        width:list.width;height:root.detailed ? 92 : 72;radius:8
                        color:root.selectedId===modelData.id ? C.Theme.accentFill : C.Theme.raised
                        MouseArea {anchors.fill:parent;onClicked:root.selectDevice(card.modelData)}
                        ColumnLayout {
                            anchors.fill:parent;anchors.margins:10;spacing:5
                            RowLayout {
                                Text {Layout.fillWidth:true;text:card.modelData.name;color:C.Theme.text;font.pixelSize:13;elide:Text.ElideRight}
                                Text {text:root.stateLabel(card.modelData.state);color:card.modelData.state==="available" ? C.Theme.accent : C.Theme.orange;font.pixelSize:11}
                            }
                            Text {Layout.fillWidth:true;text:root.typeLabel(card.modelData.type)+" · "+card.modelData.address;color:C.Theme.muted;font.pixelSize:11;elide:Text.ElideMiddle}
                            Text {visible:root.detailed;Layout.fillWidth:true;text:card.modelData.detail || "";color:C.Theme.subtle;font.pixelSize:10;elide:Text.ElideRight}
                        }
                    }
                    Text {anchors.centerIn:parent;visible:root.deviceCount===0;text:"没有匹配的设备；可刷新或调整筛选";color:C.Theme.muted;font.pixelSize:12}
                }
            }
            C.Panel {
                Layout.preferredWidth:290;Layout.fillHeight:true
                ColumnLayout {
                    anchors.fill:parent;anchors.margins:14;spacing:10
                    Text {Layout.fillWidth:true;text:root.selectedDevice.name || "选择一个设备";color:C.Theme.text;font.pixelSize:16;wrapMode:Text.Wrap}
                    Text {Layout.fillWidth:true;text:root.selectedDevice.address || "点击左侧设备，使用已有参数进入对应页面。";color:C.Theme.blue;font.pixelSize:12;wrapMode:Text.WrapAnywhere}
                    Text {Layout.fillWidth:true;text:root.selectedDevice.id ? root.typeLabel(root.selectedDevice.type)+" · "+root.stateLabel(root.selectedDevice.state) : "";color:C.Theme.muted;font.pixelSize:12}
                    ScrollView {
                        Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
                        Text {width:parent.width;text:(root.selectedDevice.detail || "")+"\n\n选择仅填写目标和切换页面，不会自动连接、授权或发送。USB/PnP 可见不代表可读写；TCP 端口可达不代表服务身份已确认。";color:C.Theme.muted;font.pixelSize:12;wrapMode:Text.Wrap;lineHeight:1.3}
                    }
                    C.ActionButton {objectName:"useSelectedDevice";Layout.fillWidth:true;text:"使用所选设备";primary:true;enabled:root.usable;onClicked:root.useSelected()}
                    C.ActionButton {objectName:"saveSelectedDevice";Layout.fillWidth:true;text:root.selectedSaved ? "移除历史记录" : "保存到历史";enabled:!!root.settings && !!root.selectedDevice.id;onClicked:root.selectedSaved ? root.removeSavedAddress(root.selectedDevice.address,root.selectedDevice.type) : root.saveSelected()}
                }
            }
        }
        RowLayout {
            visible:root.detailed || root.advanced;spacing:8
            Text {text:"历史地址";color:C.Theme.muted;font.pixelSize:12}
            C.InputBox {id:savedName;objectName:"savedDeviceName";Layout.preferredWidth:130;placeholderText:"名称（可选）";onTextChanged:root.saveOption("savedName",text)}
            C.InputBox {id:savedHost;objectName:"savedDeviceHost";Layout.fillWidth:true;placeholderText:"IPv4 / 主机名";onTextChanged:root.saveOption("savedHost",text)}
            C.InputBox {id:savedPort;objectName:"savedDevicePort";Layout.preferredWidth:90;placeholderText:"端口";text:"8765";inputMethodHints:Qt.ImhDigitsOnly;onTextChanged:root.saveOption("savedPort",text)}
            C.ActionButton {objectName:"addSavedDevice";text:"保存地址";enabled:!!root.settings;onClicked:root.saveAddress(savedName.text,savedHost.text,savedPort.text)}
        }
        Text {Layout.fillWidth:true;text:root.historyError || (root.discovery ? root.discovery.status : "设备探索服务未加载");color:root.historyError ? C.Theme.red : C.Theme.muted;font.pixelSize:11;wrapMode:Text.Wrap}
    }
}
