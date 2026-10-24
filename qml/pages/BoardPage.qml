import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import SerialStudio.Terminal 1.0
import "../components" as C

Item {
    id: root
    required property var controller
    required property var serialBackend
    required property var networkBackend
    property var settings: null
    property var quickCommands: null
    property var discovery: null
    property bool detailed: false
    property bool advanced: false
    property string pendingAdbSerial: ""
    property string selectedPort: ""
    readonly property bool interactive: bus.currentIndex===3
    readonly property var connectionOptions: terminalMode.currentIndex===0 ? [{label:"网络 · SSH",value:"ssh"},{label:"USB / 网络 · ADB",value:"adb"},{label:"有线 · 串口",value:"serial"}] : terminalMode.currentIndex===3 ? [{label:"USB / 网络 · ADB",value:"adb"},{label:"网络 · SSH",value:"ssh"},{label:"有线 · 串口",value:"serial"}] : [{label:"本机终端",value:"local"},{label:"网络 · SSH",value:"ssh"},{label:"有线 · 串口",value:"serial"}]
    readonly property string terminalTransport: connectionOptions[terminalConnection.currentIndex] ? connectionOptions[terminalConnection.currentIndex].value : ""
    readonly property bool showSshConfig: interactive ? terminalTransport==="ssh" && sessionEditable : source.currentIndex===2
    readonly property bool sessionEditable: !interactive || !root.terminal || !root.terminal.active
    function selectAdbSerial(serial,save) {
        pendingAdbSerial=serial || ""
        if(root.adb)root.adb.selectedSerial=pendingAdbSerial
        if(save && root.settings)root.settings.setValue("board/adbSerial",pendingAdbSerial)
    }
    function restoreAdbSelection() {
        if(root.adb)root.adb.executablePath=adbPath.text
        const serial=root.settings ? root.settings.value("board/adbSerial","") : pendingAdbSerial
        selectAdbSerial(serial,false)
    }
    function utf8Hex(value) {
        return encodeURIComponent(value).replace(/%([0-9A-F]{2})|([^%])/g,function(match,hex,ascii) {
            return (hex || ascii.charCodeAt(0).toString(16).padStart(2,"0")).toUpperCase()+" "
        }).trim()
    }
    function fillBusQuick(entry,panel) {
        try {
            let value
            if(entry.hex) {
                const compact=String(entry.text || "").replace(/\s/g,"")
                if(compact.length%2 || !/^[0-9a-fA-F]*$/.test(compact))throw new Error("HEX 内容必须为完整字节")
                value=(compact.match(/../g)||[]).join(" ").toUpperCase()
            } else value=utf8Hex(entry.text || "")
            const newline=utf8Hex(entry.newline || "")
            tx.text=[value,newline].filter(function(part){return part.length>0}).join(" ")
            panel.localError=""
        } catch(error) {panel.localError="无法填入："+error.message}
    }
    function fillTerminalQuick(entry,panel) {
        if(entry.interaction==="editor-key"){panel.localError="编辑器按键请在指令助手中发送，此处只填入普通文本。";return}
        if(entry.hex){panel.localError="终端不接受 HEX 快捷内容，请改用单行文本指令。";return}
        if(!root.terminal || root.terminal.mode!==terminalMode.currentIndex){panel.localError="请先打开所选系统的终端。";return}
        if(root.terminal.fill(entry.text || ""))panel.localError=entry.newline ? "已填入文本；换行未投递，请核对后自行按回车。" : ""
    }
    function useDevice(device) {
        bus.currentIndex=3
        if(device.type==="adb"){
            const config=device.config || device
            terminalMode.currentIndex=3
            terminalConnection.currentIndex=0
            if(config.adb){adbPath.text=config.adb;if(root.adb)root.adb.executablePath=config.adb}
            selectAdbSerial(config.serial || device.address,true)
        }
        else {const config=device.config||device;terminalMode.currentIndex=0;terminalConnection.currentIndex=0;sshHost.text=config.host||device.address;if(config.port)sshPort.text=String(config.port)}
    }
    C.PersistentForm {
        id: boardForm
        settings: root.settings; scope: "board"
        onRestored: {
            if(bus.currentIndex<0 || bus.currentIndex>=3)bus.currentIndex=3
            root.restoreAdbSelection()
            root.restoreTerminal()
        }
        fields:[{key:"source",object:source,property:"currentIndex"},{key:"bus",object:bus,property:"currentIndex"},
            {key:"i2c",object:i2cBus,property:"text"},{key:"address",object:address,property:"text"},
            {key:"spi",object:spiDevice,property:"text"},{key:"spiMode",object:spiMode,property:"currentIndex"},
            {key:"speed",object:speed,property:"text"},{key:"usbOp",object:usbOp,property:"currentIndex"},
            {key:"vid",object:vid,property:"text"},{key:"pid",object:pid,property:"text"},
            {key:"interface",object:usbInterface,property:"text"},{key:"endpoint",object:endpoint,property:"text"},
            {key:"usbBus",object:usbBus,property:"text"},{key:"usbAddress",object:usbAddress,property:"text"},
            {key:"tx",object:tx,property:"text"},{key:"read",object:readCount,property:"text"},
            {key:"terminal",object:terminalMode,property:"currentIndex"},{key:"presentation",object:presentation,property:"currentIndex"},
            {key:"connection",object:terminalConnection,property:"currentIndex"},
            {key:"serialPort",object:root,property:"selectedPort"},{key:"baud",object:terminalBaud,property:"text"},
            {key:"dataBits",object:terminalBits,property:"currentIndex"},{key:"parity",object:terminalParity,property:"currentIndex"},
            {key:"stopBits",object:terminalStop,property:"currentIndex"},{key:"flow",object:terminalFlow,property:"currentIndex"},
            {key:"host",object:sshHost,property:"text"},{key:"user",object:sshUser,property:"text"},
            {key:"port",object:sshPort,property:"text"},{key:"identityPath",object:sshKey,property:"text"},
            {key:"adbPath",object:adbPath,property:"text"},{key:"advanced",object:root,property:"advanced"}]
    }
    readonly property var terminal: controller ? controller.terminal : null
    function restoreTerminal() {
        if(terminal && terminal.mode>=0) {
            terminalMode.currentIndex=terminal.mode
            presentation.currentIndex=terminal.embedded?0:1
            const protocol=terminal.transport || (terminal.mode===0?"ssh":terminal.mode===3?"adb":"local")
            for(let i=0;i<connectionOptions.length;i++)if(connectionOptions[i].value===protocol)terminalConnection.currentIndex=i
        }
    }
    onTerminalChanged: restoreTerminal()
    onInteractiveChanged: if(interactive)restoreTerminal()
    onTerminalTransportChanged: if(terminalTransport==="serial")presentation.currentIndex=0
    readonly property bool canInteract: terminal && terminal.active && terminal.embedded===true
    readonly property var adb: terminal ? terminal.adb : null
    onAdbChanged: if(root.adb && pendingAdbSerial)root.adb.selectedSerial=pendingAdbSerial
    Connections {
        target: root.adb; ignoreUnknownSignals: true
        function onChanged() {
            if(boardForm.ready && !boardForm.restoring && root.adb.ready && !root.adb.scanning && root.adb.selectedSerial!==pendingAdbSerial)
                root.selectAdbSerial(root.adb.selectedSerial,true)
        }
    }
    Binding { target: root.adb; property: "enabled"; value: root.visible && root.interactive && root.terminalTransport==="adb"; when: root.adb!==null }
    Timer {interval:3000;repeat:true;running:root.visible && root.interactive && root.terminalTransport==="serial";onTriggered:root.serialBackend.refreshPorts()}
    FileDialog {
        id: adbDialog; title: "选择 Android platform-tools 中的 adb.exe"; nameFilters: ["ADB 工具 (adb.exe)"]
        onAccepted: if(root.adb) { root.adb.executablePath=selectedFile.toString(); adbPath.text=root.adb.executablePath }
    }
    function sshConfig() { return {host:sshHost.text, user:sshUser.text, port:Number(sshPort.text), key:sshKey.text,presentation:presentation.currentIndex===0?"qt":"computer"} }
    function openTerminal() {
        if(!root.controller)return
        if(!interactive || (terminalMode.currentIndex===0 && terminalTransport==="ssh")){root.controller.openTerminal(sshConfig());return}
        if(!terminal)return
        let config={transport:terminalTransport,presentation:presentation.currentIndex===0?"qt":"computer"}
        if(terminalTransport==="ssh")config=Object.assign(sshConfig(),config)
        if(terminalTransport==="adb"){
            if(!root.adb || !root.adb.ready || root.adb.scanning)return
            config.adb=root.adb.resolvedPath;config.serial=root.adb.selectedSerial
        }
        if(terminalTransport==="serial")Object.assign(config,{port:selectedPort,baud:Number(terminalBaud.text),dataBits:8-terminalBits.currentIndex,
            parity:[0,2,3,4,5][terminalParity.currentIndex],stopBits:[1,2,3][terminalStop.currentIndex],flow:terminalFlow.currentIndex,presentation:"qt"})
        terminal.open(terminalMode.currentIndex,config)
    }
    function fillSystemOverview() {
        if(root.terminal && root.terminal.active)root.terminal.fill([
            "uname -a; cat /etc/os-release; id; ip -br addr",
            "Get-ComputerInfo; Get-NetIPConfiguration",
            "ver & systeminfo & ipconfig",
            "getprop ro.product.model; uname -a; id; ip addr"][terminalMode.currentIndex])
    }
    CommandAssistant { id: assistant; controller: root.terminal; repository: root.quickCommands; selectedMode: terminalMode.currentIndex }
    function request() {
        if (bus.currentIndex === 0)
            return {bus:"i2c", op:"transfer", device:Number(i2cBus.text), address:parseInt(address.text,16), tx:tx.text, read:Number(readCount.text)}
        if (bus.currentIndex === 1)
            return {bus:"spi", op:"transfer", device:spiDevice.text, mode:spiMode.currentIndex, speed:Number(speed.text), tx:tx.text, read:0}
        if (bus.currentIndex === 2) {
            if (usbOp.currentIndex === 0) return {bus:"usb",op:"list"}
            return {bus:"usb", op:usbOp.currentIndex === 1 ? "read" : "write", vid:parseInt(vid.text,16), pid:parseInt(pid.text,16),
                    interface:Number(usbInterface.text), endpoint:parseInt(endpoint.text,16), tx:tx.text, read:Number(readCount.text), timeout:1000,
                    usb_bus:Number(usbBus.text), usb_address:Number(usbAddress.text)}
        }
        return {bus:"system",op:"info"}
    }
    Drawer {
        id:boardDrawer;objectName:"boardDrawer";parent:root
        width:Math.min(390,root.width-32);height:root.height;edge:Qt.LeftEdge
        background:Rectangle {color:C.Theme.panel;border.color:C.Theme.line}
        ScrollView {
        objectName:"boardDrawerScroll";anchors.fill:parent;contentWidth:availableWidth;clip:true
        ColumnLayout {
        width:parent.width;spacing:8
        C.Panel {
            Layout.fillWidth:true;implicitHeight:boardConfigColumn.implicitHeight+24
            Item {
                anchors.fill: parent; anchors.margins: 12
                ColumnLayout {
                    id:boardConfigColumn
                    width: parent.width; spacing: 9
                    RowLayout {
                        Text { text: "连接与板端配置"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Item {Layout.fillWidth:true}
                        C.ActionButton {text:"完成";implicitHeight:28;onClicked:boardDrawer.close()}
                    }
                    Text { text: "功能"; color: C.Theme.muted; font.pixelSize: 12 }
                    C.SelectBox { id: bus; objectName: "boardBus"; Layout.fillWidth: true; currentIndex: 3; model: ["I²C / IIC", "SPI", "USB", "系统交互终端"]; enabled: !root.controller || !root.controller.busy }
                    Text { visible: !root.interactive; text: "板端操作通过"; color: C.Theme.muted; font.pixelSize: 12 }
                    C.SelectBox { id: source; objectName: "boardSource"; visible: !root.interactive; Layout.fillWidth: true; currentIndex: 2; model: ["当前串口桥接", "当前网络桥接", "SSH · Linux 终端"]; enabled: !root.controller || !root.controller.busy }
                    Text {
                        visible: !root.interactive
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: C.Theme.subtle; font.pixelSize: 10
                        text: source.currentIndex === 0 ? root.serialBackend.status : source.currentIndex === 1 ? root.networkBackend.status : "板端需 Python 3；APP 操作使用 SSH 密钥认证"
                    }
                    Text { visible: root.interactive; Layout.fillWidth: true; wrapMode: Text.Wrap; text: "选择目标系统与连接方式。网线、Wi-Fi 和 USB 网卡均可通过 SSH 连接。"; color: C.Theme.muted; font.pixelSize: 11 }
                    ColumnLayout {
                        visible: bus.currentIndex === 0; Layout.fillWidth: true; spacing: 7
                        Text { text: "I²C 总线编号 / 7 位地址 HEX"; color: C.Theme.muted; font.pixelSize: 11 }
                        RowLayout {
                            C.InputBox { id: i2cBus; text: "1"; Layout.fillWidth: true; implicitWidth: 70; validator: IntValidator { bottom: 0; top: 255 } }
                            C.InputBox { id: address; text: "50"; Layout.fillWidth: true; implicitWidth: 70; validator: RegularExpressionValidator { regularExpression: /[0-7]?[0-9a-fA-F]/ } }
                        }
                        Text { text: "先写后读在同一次 I²C 事务中完成"; color: C.Theme.subtle; font.pixelSize: 10 }
                    }
                    ColumnLayout {
                        visible: bus.currentIndex === 1; Layout.fillWidth: true; spacing: 7
                        Text { text: "SPI 设备 / 模式 / 频率 Hz"; color: C.Theme.muted; font.pixelSize: 11 }
                        C.InputBox { id: spiDevice; text: "/dev/spidev0.0"; Layout.fillWidth: true }
                        RowLayout {
                            C.SelectBox { id: spiMode; model: ["Mode 0","Mode 1","Mode 2","Mode 3"]; implicitWidth: 95 }
                            C.InputBox { id: speed; text: "1000000"; Layout.fillWidth: true; implicitWidth: 100; validator: IntValidator { bottom: 1; top: 100000000 } }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "8 位全双工，收发等长；读取可发送 FF 产生时钟。"; color: C.Theme.subtle; font.pixelSize: 10 }
                    }
                    ColumnLayout {
                        visible: bus.currentIndex === 2; Layout.fillWidth: true; spacing: 7
                        C.SelectBox { id: usbOp; objectName: "usbOperation"; Layout.fillWidth: true; model: ["枚举板端 USB", "读取 IN 端点", "写入 OUT 端点"] }
                        GridLayout {
                            visible: usbOp.currentIndex > 0; Layout.fillWidth: true; columns: 2
                            Text { text: "VID / HEX"; color: C.Theme.muted; font.pixelSize: 11 }
                            Text { text: "PID / HEX"; color: C.Theme.muted; font.pixelSize: 11 }
                            C.InputBox { id: vid; text: "1234"; Layout.fillWidth: true; implicitWidth: 80 }
                            C.InputBox { id: pid; text: "5678"; Layout.fillWidth: true; implicitWidth: 80 }
                            Text { text: "接口编号"; color: C.Theme.muted; font.pixelSize: 11 }
                            Text { text: "端点 / HEX"; color: C.Theme.muted; font.pixelSize: 11 }
                            C.InputBox { id: usbInterface; text: "0"; Layout.fillWidth: true; implicitWidth: 80 }
                            C.InputBox { id: endpoint; text: usbOp.currentIndex === 1 ? "81" : "01"; Layout.fillWidth: true; implicitWidth: 80 }
                            Text { text: "总线（0 不限）"; color: C.Theme.muted; font.pixelSize: 11 }
                            Text { text: "地址（0 不限）"; color: C.Theme.muted; font.pixelSize: 11 }
                            C.InputBox { id: usbBus; text: "0"; Layout.fillWidth: true; implicitWidth: 80 }
                            C.InputBox { id: usbAddress; text: "0"; Layout.fillWidth: true; implicitWidth: 80 }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "枚举使用 sysfs；端点读写需 PyUSB/libusb 和设备权限，不自动解绑驱动。"; color: C.Theme.subtle; font.pixelSize: 10 }
                    }
                    Text { visible: bus.currentIndex < 2 || (bus.currentIndex === 2 && usbOp.currentIndex === 2); text: "写入 / MOSI 数据 · HEX"; color: C.Theme.muted; font.pixelSize: 11 }
                    C.InputBox { id: tx; objectName: "boardTx"; visible: bus.currentIndex < 2 || (bus.currentIndex === 2 && usbOp.currentIndex === 2); text: "00"; Layout.fillWidth: true; placeholderText: "可留空，只读取 I²C" }
                    C.QuickSendPanel { id:busQuick; objectName:"boardBusQuick"; Layout.fillWidth: true; visible: bus.currentIndex<3; repository:root.quickCommands; scope:["board-i2c","board-spi","board-usb"][Math.max(0,Math.min(bus.currentIndex,2))]; expanded:false; onChosen:function(text){root.fillBusQuick(selectedEntry,busQuick)} }
                    Text { visible: bus.currentIndex === 0 || (bus.currentIndex === 2 && usbOp.currentIndex === 1); text: "读取字节数 · 0–4096"; color: C.Theme.muted; font.pixelSize: 11 }
                    C.InputBox { id: readCount; visible: bus.currentIndex === 0 || (bus.currentIndex === 2 && usbOp.currentIndex === 1); text: "1"; Layout.fillWidth: true; validator: IntValidator { bottom: 0; top: 4096 } }
                }
            }
        }
            C.Panel {
                visible: root.interactive || root.showSshConfig
                Layout.fillWidth: true; implicitHeight:terminalConfigColumn.implicitHeight+24
                Item {
                    objectName:"boardTerminalConfig"
                    anchors.fill: parent; anchors.margins: 12
                ColumnLayout {
                    id:terminalConfigColumn
                    width: parent.width; spacing: 9
                    RowLayout {
                        Text { text: root.interactive ? "终端连接" : "SSH 板端操作连接"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        visible: root.interactive
                        C.SelectBox { id: terminalMode; objectName: "terminalMode"; enabled:root.sessionEditable; Layout.fillWidth: true; model: ["Linux","Windows · PowerShell","Windows · CMD","Android"]; onCurrentIndexChanged: if(root.sessionEditable)terminalConnection.currentIndex=0 }
                    }
                    C.SelectBox {id:terminalConnection;objectName:"terminalConnection";visible:root.interactive;Layout.fillWidth:true;model:root.connectionOptions;textRole:"label";enabled:root.sessionEditable}
                    RowLayout {
                        C.SelectBox { id:presentation; objectName:"terminalPresentation"; visible:root.interactive; Layout.fillWidth:true; model:["Qt 内置交互终端","电脑终端窗口"]; enabled:root.sessionEditable && root.terminalTransport!=="serial" }
                        C.ActionButton { objectName:"boardAdvancedButton";text:root.advanced?"收起高级":"高级"; implicitHeight:30; onClicked:root.advanced=!root.advanced }
                    }
                    Text { visible:root.interactive && !root.sessionEditable; Layout.fillWidth:true; text:"当前会话已连接。更改连接请先关闭会话，或点击主界面“新建终端”。"; wrapMode:Text.Wrap; color:C.Theme.accent; font.pixelSize:11 }
                    Text { objectName:"terminalConnectionSummary"; visible:root.interactive && !root.sessionEditable; Layout.fillWidth:true; text:"当前会话："+(root.terminal && root.terminal.connectionSummary || ""); wrapMode:Text.Wrap; textFormat:Text.PlainText; color:C.Theme.text; font.pixelSize:12 }
                    C.NetworkTargets {
                        visible:root.showSshConfig;Layout.fillWidth:true
                        enabled:root.sessionEditable
                        discovery:root.discovery;settings:root.settings;advanced:root.advanced;protocol:"ssh"
                        host:sshHost.text;port:Number(sshPort.text)
                        onTargetChosen:function(device){const config=device.config || device;sshHost.text=config.host || device.address;if(config.port)sshPort.text=String(config.port)}
                    }
                    RowLayout {
                        visible: root.showSshConfig; enabled:root.sessionEditable
                        C.InputBox { id: sshHost; objectName: "sshHost"; placeholderText: "板端 IP / 主机名"; Layout.fillWidth: true; implicitWidth: 160 }
                        C.InputBox { id: sshUser; text: "root"; placeholderText: "用户名"; Layout.preferredWidth: 105 }
                        C.InputBox { id: sshPort; visible:root.advanced; text: "22"; Layout.preferredWidth: 65; validator: IntValidator { bottom: 1; top: 65535 } }
                    }
                    C.InputBox { id: sshKey; enabled:root.sessionEditable; visible: root.advanced && root.showSshConfig; placeholderText: "SSH 私钥路径（可选；也可用默认密钥 / ssh-agent）"; Layout.fillWidth: true }
                    ColumnLayout {
                        visible:root.interactive && root.terminalTransport==="serial" && root.sessionEditable
                        Layout.fillWidth:true
                        RowLayout {
                            C.SelectBox {
                                objectName:"terminalSerialPort";Layout.fillWidth:true;model:root.serialBackend.ports;textRole:"label"
                                currentIndex:{for(let i=0;i<count;i++)if(model[i].name===root.selectedPort)return i;return -1}
                                displayText:currentIndex>=0?currentText:(root.selectedPort || "选择调试串口")
                                popup.onAboutToShow:root.serialBackend.refreshPorts()
                                onActivated:function(index){root.selectedPort=model[index].name}
                            }
                            C.ActionButton {text:"刷新";onClicked:root.serialBackend.refreshPorts()}
                        }
                        C.InputBox {id:terminalBaud;objectName:"terminalSerialBaud";Layout.fillWidth:true;text:"115200";placeholderText:"波特率";validator:IntValidator {bottom:1;top:2147483647}}
                        GridLayout {
                            visible:root.advanced;columns:2;Layout.fillWidth:true
                            C.SelectBox {id:terminalBits;Layout.fillWidth:true;model:["8 数据位","7 数据位","6 数据位","5 数据位"]}
                            C.SelectBox {id:terminalParity;Layout.fillWidth:true;model:["无校验","偶校验","奇校验","空校验","标记校验"]}
                            C.SelectBox {id:terminalStop;Layout.fillWidth:true;model:["1 停止位","2 停止位","1.5 停止位"]}
                            C.SelectBox {id:terminalFlow;Layout.fillWidth:true;model:["无流控","硬件流控","软件流控"]}
                        }
                        Text {Layout.fillWidth:true;wrapMode:Text.Wrap;color:C.Theme.subtle;font.pixelSize:10;text:"使用 Qt 交互终端，Enter 发送 CR。请先释放串口工作台占用的端口；设备须提供 Shell。"}
                    }
                    RowLayout {
                        visible: root.interactive && root.terminalTransport==="adb" && root.advanced && root.sessionEditable
                        C.InputBox {
                            id: adbPath; objectName: "adbPath"; Layout.fillWidth: true; implicitWidth: 160
                            text: root.adb ? root.adb.executablePath : ""; placeholderText: "ADB 工具路径（留空自动查找 SDK / PATH）"
                            onEditingFinished: if(root.adb)root.adb.executablePath=text
                        }
                        C.ActionButton { text: "选择工具"; implicitWidth: 90; onClicked: adbDialog.open() }
                    }
                    RowLayout {
                        visible: root.interactive && root.terminalTransport==="adb" && root.sessionEditable; Layout.fillWidth: true
                        C.SelectBox {
                            id: adbDevices; objectName: "adbDevices"; Layout.fillWidth: true; implicitWidth: 220
                            model: root.adb ? root.adb.devices : []; textRole: "label"
                            currentIndex: { for(let i=0;i<count;i++)if(model[i].serial===root.adb.selectedSerial)return i;return -1 }
                            displayText: currentIndex>=0 ? currentText : (count>0 ? "请选择 Android 设备" : "等待 USB / ADB 设备")
                            popup.onAboutToShow: if(root.adb && !root.adb.scanning)root.adb.refresh()
                            onActivated: function(index){root.selectAdbSerial(model[index].serial,true)}
                        }
                        C.ActionButton { objectName: "adbRefreshButton"; text: root.adb && root.adb.scanning ? "检测中…" : "刷新设备"; implicitWidth: 90; enabled: root.adb && !root.adb.scanning; onClicked: root.adb.refresh() }
                    }
                    Text { objectName: "adbStatus"; visible: root.interactive && root.terminalTransport==="adb" && root.sessionEditable; Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.adb ? root.adb.status : "ADB 检测未加载"; color: root.adb && root.adb.ready ? C.Theme.accent : C.Theme.muted; font.pixelSize: 11 }
                    C.AdbNetwork {visible:root.interactive && root.terminalTransport==="adb" && root.sessionEditable;Layout.fillWidth:true;adb:root.adb;settings:root.settings;scope:"board/adbNetwork";editable:root.sessionEditable}
                    Text { visible: root.interactive && root.terminalTransport==="adb" && root.sessionEditable && text.length>0; Layout.fillWidth: true; text: root.adb ? root.adb.resolvedPath : ""; elide: Text.ElideMiddle; color: C.Theme.subtle; font.pixelSize: 10; ToolTip.visible: pathMouse.containsMouse; ToolTip.text: text; MouseArea { id: pathMouse; anchors.fill: parent; hoverEnabled: true } }
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.terminalTransport==="ssh" ? "SSH 支持网线、Wi-Fi、USB 网络；目标需开启 SSH 服务。可交互认证，首次登录请核对指纹；总线操作另外需要密钥。" : root.terminalTransport==="adb" ? "每 3 秒检测 USB / 网络 ADB；保留所选设备，断线不切换到其他板。Android 需调试授权，Linux 需开放 adbd；无线调试先配对再连接。" : root.terminalTransport==="serial" ? "串口仅传输交互字节，系统选择决定指令库；波特率及格式须与设备一致。" : "在 Windows 本机运行所选 Shell；指令库分别匹配 PowerShell / CMD 语法。"; color: C.Theme.subtle; font.pixelSize: 10; lineHeight: 1.3 }
                    Flow {
                        visible: root.interactive && (root.advanced || root.detailed); Layout.fillWidth: true; spacing: 5
                        Repeater {
                            model: [{label:"回车",key:"enter"},{label:"Ctrl+C",key:"ctrlC"},{label:"Ctrl+D",key:"ctrlD"},{label:"Tab",key:"tab"},{label:"Esc",key:"escape"},{label:"↑",key:"up"},{label:"↓",key:"down"},{label:"←",key:"left"},{label:"→",key:"right"},{label:"退格",key:"backspace"}]
                            C.ActionButton {
                                required property var modelData
                                objectName: "terminalKey_"+modelData.key
                                text: modelData.label; implicitWidth: modelData.label.length>2?65:45; implicitHeight: 29; leftPadding: 8; rightPadding: 8
                                enabled: root.terminal && root.terminal.active && !root.terminal.inputBusy && root.terminal.mode===terminalMode.currentIndex
                                onClicked: root.terminal.sendKey(modelData.key)
                            }
                        }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.terminal ? root.terminal.status : "终端未加载"; color: C.Theme.muted; font.pixelSize: 10 }
                }
                }
            }
        }
        }
    }
    ColumnLayout {
        anchors.fill:parent;spacing:6
        RowLayout {
            Layout.fillWidth:true;spacing:5
            C.ActionButton {objectName:"boardDrawerButton";text:"配置";implicitHeight:30;onClicked:boardDrawer.open()}
            C.ActionButton {objectName:"boardExecuteButton";visible:!root.interactive;implicitHeight:30;primary:true;text:root.controller && root.controller.busy?"等待响应…":"执行板端操作";enabled:!root.interactive && root.controller!==null && !root.controller.busy;onClicked:root.controller.execute(source.currentIndex,root.sshConfig(),root.request())}
            C.ActionButton {text:"停止等待";visible:root.controller!==null && root.controller.busy;implicitHeight:30;onClicked:root.controller.cancel()}
            C.ActionButton {
                objectName:"sshTerminalButton";visible:root.interactive || root.showSshConfig;implicitHeight:30
                text:root.terminal && root.terminal.active ? (root.terminal.embedded?"关闭终端":"解除控制") : "打开终端"
                primary:!root.terminal || !root.terminal.active;danger:root.terminal && root.terminal.active
                enabled:root.controller !== null && (root.terminal && root.terminal.active || (!root.controller.busy && (!root.interactive || root.terminalTransport!=="adb" || (root.adb && root.adb.ready && !root.adb.scanning))))
                onClicked:root.terminal && root.terminal.active ? root.terminal.close() : root.openTerminal()
            }
            C.ActionButton {objectName:"newTerminalButton";text:"＋ 新建终端";implicitHeight:30;onClicked:{if(root.controller){root.controller.newTerminal();bus.currentIndex=3;boardDrawer.open()}}}
            Item {Layout.fillWidth:true}
            C.ActionButton {objectName:"commandAssistantButton";text:"指令辅助";implicitHeight:30;onClicked:assistant.showFor(terminalMode.currentIndex)}
        }
        ScrollView {
            objectName:"terminalTabsScroll";visible:root.interactive;Layout.fillWidth:true;Layout.preferredHeight:32
            contentHeight:availableHeight;clip:true
            Row {
                spacing:4
                Repeater {
                    model:root.controller && root.controller.terminalSessions ? root.controller.terminalSessions : []
                    Row {
                        required property var modelData
                        required property int index
                        C.ActionButton {text:modelData.title+(modelData.active?" ●":"");primary:root.controller.terminalIndex===parent.index;implicitHeight:28;onClicked:root.controller.selectTerminal(parent.index)}
                        C.ActionButton {text:"×";implicitWidth:28;implicitHeight:28;onClicked:root.controller.closeTerminal(parent.index)}
                    }
                }
            }
        }
            C.Panel {
                visible: !root.interactive
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "请求 / 响应"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        C.CopyTextButton {objectName:"copyBoardOutput";editor:boardOutput}
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "串口、网络沿用各自的 JSON 行或自定义帧头尾配置。板端应运行匹配的桥接程序；操作仅在点击后发送。"; color: C.Theme.muted; font.pixelSize: 11 }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        TextArea {
                            id: boardOutput; objectName: "boardOutput"
                            text: root.controller ? root.controller.output : "板端控制器未加载"
                            color: root.controller && !root.controller.success ? C.Theme.red : C.Theme.text
                            font.family: "Consolas"; font.pixelSize: 12; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                            background: Rectangle { color: C.Theme.consoleBg; radius: 7; border.color: C.Theme.line }
                        }
                    }
                }
            }
            C.Panel {
                visible: root.interactive; Layout.fillWidth:true; Layout.fillHeight:true
                ColumnLayout {
                    anchors.fill:parent; anchors.margins:10; spacing:6
                    RowLayout {
                        Text { text:root.terminal?root.terminal.status:"Qt 交互终端";elide:Text.ElideRight;Layout.fillWidth:true;color:C.Theme.text;font.pixelSize:11 }
                        Item {Layout.fillWidth:true}
                        C.ActionButton {objectName:"clearTerminalButton";text:"清空";implicitHeight:26;enabled:root.terminal!==null;onClicked:root.terminal.clearDisplay()}
                        C.ActionButton {objectName:"systemOverviewButton";text:"系统概览";implicitHeight:26;enabled:root.terminal && root.terminal.active;onClicked:root.fillSystemOverview()}
                        C.ActionButton {text:"复制选区";implicitHeight:26;onClicked:terminalView.copy()}
                        C.ActionButton {text:"粘贴";implicitHeight:26;enabled:root.canInteract;onClicked:terminalView.paste()}
                    }
                    TerminalView {id:terminalView;objectName:"embeddedTerminalView";Layout.fillWidth:true;Layout.fillHeight:true;enabled:root.canInteract;screen:root.terminal && root.terminal.screen?root.terminal.screen:null}
                    Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:"点击终端输入 · Ctrl+Shift+C / V 复制粘贴 · 滚轮查看历史。电脑终端模式在独立窗口操作。";color:C.Theme.subtle;font.pixelSize:10}
                }
            }
            C.QuickSendPanel {
                id: terminalQuick; objectName:"terminalQuick"
                visible:root.interactive;Layout.fillWidth:true;repository:root.quickCommands;expanded:false
                scope:["terminal-linux","terminal-powershell","terminal-cmd","terminal-android"][Math.max(0,terminalMode.currentIndex)]
                onChosen:function(text){root.fillTerminalQuick(selectedEntry,terminalQuick)}
            }
    }
}
