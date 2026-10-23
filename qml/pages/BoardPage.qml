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
        if(entry.hex){panel.localError="终端不接受 HEX 快捷内容，请改用单行文本指令。";return}
        if(!root.terminal || root.terminal.mode!==terminalMode.currentIndex){panel.localError="请先打开所选系统的终端。";return}
        if(root.terminal.fill(entry.text || ""))panel.localError=entry.newline ? "已填入文本；换行未投递，请核对后自行按回车。" : ""
    }
    function useDevice(device) {
        bus.currentIndex=4
        if(device.type==="adb"){
            const config=device.config || device
            terminalMode.currentIndex=3
            if(config.adb){adbPath.text=config.adb;if(root.adb)root.adb.executablePath=config.adb}
            selectAdbSerial(config.serial || device.address,true)
        }
        else {const config=device.config||device;terminalMode.currentIndex=0;sshHost.text=config.host||device.address;if(config.port)sshPort.text=String(config.port)}
    }
    C.PersistentForm {
        id: boardForm
        settings: root.settings; scope: "board"
        onRestored: root.restoreAdbSelection()
        fields:[{key:"source",object:source,property:"currentIndex"},{key:"bus",object:bus,property:"currentIndex"},
            {key:"i2c",object:i2cBus,property:"text"},{key:"address",object:address,property:"text"},
            {key:"spi",object:spiDevice,property:"text"},{key:"spiMode",object:spiMode,property:"currentIndex"},
            {key:"speed",object:speed,property:"text"},{key:"usbOp",object:usbOp,property:"currentIndex"},
            {key:"vid",object:vid,property:"text"},{key:"pid",object:pid,property:"text"},
            {key:"interface",object:usbInterface,property:"text"},{key:"endpoint",object:endpoint,property:"text"},
            {key:"usbBus",object:usbBus,property:"text"},{key:"usbAddress",object:usbAddress,property:"text"},
            {key:"tx",object:tx,property:"text"},{key:"read",object:readCount,property:"text"},
            {key:"terminal",object:terminalMode,property:"currentIndex"},{key:"presentation",object:presentation,property:"currentIndex"},
            {key:"host",object:sshHost,property:"text"},{key:"user",object:sshUser,property:"text"},
            {key:"port",object:sshPort,property:"text"},{key:"identityPath",object:sshKey,property:"text"},
            {key:"adbPath",object:adbPath,property:"text"},{key:"advanced",object:root,property:"advanced"}]
    }
    readonly property var terminal: controller ? controller.terminal : null
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
    Binding { target: root.adb; property: "enabled"; value: root.visible && terminalMode.currentIndex===3; when: root.adb!==null }
    FileDialog {
        id: adbDialog; title: "选择 Android platform-tools 中的 adb.exe"; nameFilters: ["ADB 工具 (adb.exe)"]
        onAccepted: if(root.adb) { root.adb.executablePath=selectedFile.toString(); adbPath.text=root.adb.executablePath }
    }
    function sshConfig() { return {host:sshHost.text, user:sshUser.text, port:Number(sshPort.text), key:sshKey.text,presentation:presentation.currentIndex===0?"qt":"computer"} }
    function openTerminal() {
        if(!root.controller)return
        if(terminalMode.currentIndex===0)root.controller.openTerminal(sshConfig())
        else if(terminal) {
            if(terminalMode.currentIndex===3) {
                if(root.adb && root.adb.ready && !root.adb.scanning)terminal.open(3,{adb:root.adb.resolvedPath,serial:root.adb.selectedSerial,presentation:presentation.currentIndex===0?"qt":"computer"})
            } else terminal.open(terminalMode.currentIndex,{presentation:presentation.currentIndex===0?"qt":"computer"})
        }
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
    RowLayout {
        anchors.fill: parent; spacing: 16
        C.Panel {
            Layout.preferredWidth: 292; Layout.fillHeight: true
            ScrollView {
                objectName: "boardConfigScroll"
                anchors.fill: parent; anchors.margins: 16; contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: parent.width; spacing: 9
                    Text { text: "开发板操作"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text { text: "数据来源"; color: C.Theme.muted; font.pixelSize: 12 }
                    C.SelectBox { id: source; objectName: "boardSource"; Layout.fillWidth: true; currentIndex: 2; model: ["当前串口桥接", "当前网络桥接", "SSH · Linux 终端"]; enabled: !root.controller || !root.controller.busy }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: C.Theme.subtle; font.pixelSize: 10
                        text: source.currentIndex === 0 ? root.serialBackend.status : source.currentIndex === 1 ? root.networkBackend.status : "板端需 Python 3；APP 操作使用 SSH 密钥认证"
                    }
                    Text { text: "总线 / 功能"; color: C.Theme.muted; font.pixelSize: 12 }
                    C.SelectBox { id: bus; objectName: "boardBus"; Layout.fillWidth: true; currentIndex: 4; model: ["I²C / IIC", "SPI", "USB", "Linux 系统信息", "系统交互终端"]; enabled: source.enabled }
                    Text { visible: bus.currentIndex===4; Layout.fillWidth: true; wrapMode: Text.Wrap; text: "右侧选择 Linux、Windows 或 Android 终端。指令辅助只填入，修改后自行回车执行。串口登录可在串口页使用 UTF-8 收发。"; color: C.Theme.muted; font.pixelSize: 11 }
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
                    C.ActionButton {
                        objectName: "boardExecuteButton"; Layout.fillWidth: true; Layout.topMargin: 6; implicitHeight: 40
                        primary: true; text: root.controller && root.controller.busy ? "等待响应…" : bus.currentIndex===4 ? "打开所选终端" : "执行板端操作"
                        enabled: root.controller !== null && !root.controller.busy && (bus.currentIndex!==4 || ((!root.terminal || !root.terminal.active) && (terminalMode.currentIndex!==3 || (root.adb && root.adb.ready && !root.adb.scanning))))
                        onClicked: bus.currentIndex===4 ? root.openTerminal() : root.controller.execute(source.currentIndex, root.sshConfig(), root.request())
                    }
                    C.ActionButton { text: "停止等待"; visible: root.controller !== null && root.controller.busy; Layout.fillWidth: true; onClicked: root.controller.cancel() }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14
            C.Panel {
                Layout.fillWidth: true; Layout.preferredHeight: root.advanced ? 340 : terminalMode.currentIndex===3 ? 265 : terminalMode.currentIndex===0 ? 310 : 212
                ScrollView {
                    objectName:"boardTerminalScroll"
                    anchors.fill: parent; anchors.margins: 16; contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: parent.width; spacing: 9
                    RowLayout {
                        Text { text: "系统交互终端"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        C.ActionButton { objectName: "commandAssistantButton"; text: "指令辅助"; implicitHeight: 32; onClicked: assistant.showFor(terminalMode.currentIndex) }
                    }
                    RowLayout {
                        C.SelectBox { id: terminalMode; objectName: "terminalMode"; Layout.fillWidth: true; model: ["Linux · SSH","Windows · PowerShell","Windows · CMD","Android · ADB Shell"] }
                        C.ActionButton { objectName: "sshTerminalButton"; text: "打开终端 ↗"; implicitHeight: 34; enabled: root.controller !== null && !root.controller.busy && (!root.terminal || !root.terminal.active) && (terminalMode.currentIndex!==3 || (root.adb && root.adb.ready && !root.adb.scanning)); onClicked: root.openTerminal() }
                    }
                    RowLayout {
                        C.SelectBox { id:presentation; objectName:"terminalPresentation"; Layout.fillWidth:true; model:["Qt 内置交互终端","电脑终端窗口"]; enabled:!root.terminal || !root.terminal.active }
                        C.ActionButton { objectName:"boardAdvancedButton";text:root.advanced?"收起高级":"高级"; implicitHeight:30; onClicked:root.advanced=!root.advanced }
                        C.ActionButton { text:root.terminal && root.terminal.embedded?"关闭会话":"解除控制"; visible:root.terminal && root.terminal.active; implicitHeight:30; onClicked:root.terminal.close() }
                    }
                    C.NetworkTargets {
                        visible:terminalMode.currentIndex===0 || bus.currentIndex!==4;Layout.fillWidth:true
                        enabled:!root.terminal || !root.terminal.active
                        discovery:root.discovery;settings:root.settings;advanced:root.advanced;protocol:"ssh"
                        host:sshHost.text;port:Number(sshPort.text)
                        onTargetChosen:function(device){const config=device.config || device;sshHost.text=config.host || device.address;if(config.port)sshPort.text=String(config.port)}
                    }
                    RowLayout {
                        visible: terminalMode.currentIndex===0 || bus.currentIndex!==4
                        C.InputBox { id: sshHost; objectName: "sshHost"; placeholderText: "板端 IP / 主机名"; Layout.fillWidth: true; implicitWidth: 160 }
                        C.InputBox { id: sshUser; text: "root"; placeholderText: "用户名"; Layout.preferredWidth: 105 }
                        C.InputBox { id: sshPort; visible:root.advanced; text: "22"; Layout.preferredWidth: 65; validator: IntValidator { bottom: 1; top: 65535 } }
                    }
                    C.InputBox { id: sshKey; visible: root.advanced && (terminalMode.currentIndex===0 || bus.currentIndex!==4); placeholderText: "SSH 私钥路径（可选；也可用默认密钥 / ssh-agent）"; Layout.fillWidth: true }
                    RowLayout {
                        visible: terminalMode.currentIndex===3 && root.advanced
                        C.InputBox {
                            id: adbPath; objectName: "adbPath"; Layout.fillWidth: true; implicitWidth: 160
                            text: root.adb ? root.adb.executablePath : ""; placeholderText: "ADB 工具路径（留空自动查找 SDK / PATH）"
                            onEditingFinished: if(root.adb)root.adb.executablePath=text
                        }
                        C.ActionButton { text: "选择工具"; implicitWidth: 90; onClicked: adbDialog.open() }
                    }
                    RowLayout {
                        visible: terminalMode.currentIndex===3; Layout.fillWidth: true
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
                    Text { objectName: "adbStatus"; visible: terminalMode.currentIndex===3; Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.adb ? root.adb.status : "ADB 检测未加载"; color: root.adb && root.adb.ready ? C.Theme.accent : C.Theme.muted; font.pixelSize: 11 }
                    Text { visible: terminalMode.currentIndex===3 && text.length>0; Layout.fillWidth: true; text: root.adb ? root.adb.resolvedPath : ""; elide: Text.ElideMiddle; color: C.Theme.subtle; font.pixelSize: 10; ToolTip.visible: pathMouse.containsMouse; ToolTip.text: text; MouseArea { id: pathMouse; anchors.fill: parent; hoverEnabled: true } }
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: terminalMode.currentIndex===0 ? "SSH 可在 Qt 内置终端或电脑终端窗口交互，支持密码、vim/top；APP 板端操作使用密钥，首次登录请确认指纹。" : terminalMode.currentIndex===3 ? "每 3 秒自动检测；保留所选设备，断线不切换到其他板。未授权时在板端确认 USB 调试；无线 ADB 先配对 / connect。" : "按上方选择在 Qt 内置终端或电脑终端窗口运行；指令库分别匹配 PowerShell / CMD 语法。"; color: C.Theme.subtle; font.pixelSize: 10; lineHeight: 1.3 }
                    Flow {
                        visible: root.advanced || root.detailed; Layout.fillWidth: true; spacing: 5
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
            C.Panel {
                visible: bus.currentIndex!==4
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
                visible: bus.currentIndex===4; Layout.fillWidth:true; Layout.fillHeight:true
                ColumnLayout {
                    anchors.fill:parent; anchors.margins:10; spacing:6
                    RowLayout {
                        Text { text:"Qt 交互终端"; color:C.Theme.text; font.pixelSize:13 }
                        Item {Layout.fillWidth:true}
                        C.ActionButton {text:"复制选区";implicitHeight:26;onClicked:terminalView.copy()}
                        C.ActionButton {text:"粘贴";implicitHeight:26;enabled:root.canInteract;onClicked:terminalView.paste()}
                    }
                    TerminalView {id:terminalView;objectName:"embeddedTerminalView";Layout.fillWidth:true;Layout.fillHeight:true;enabled:root.canInteract;screen:root.terminal && root.terminal.screen?root.terminal.screen:null}
                    Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:"点击终端输入 · Ctrl+Shift+C / V 复制粘贴 · 滚轮查看历史。电脑终端模式在独立窗口操作。";color:C.Theme.subtle;font.pixelSize:10}
                }
            }
            C.QuickSendPanel {
                id: terminalQuick; objectName:"terminalQuick"
                visible:bus.currentIndex===4;Layout.fillWidth:true;repository:root.quickCommands;expanded:false
                scope:["terminal-linux","terminal-powershell","terminal-cmd","terminal-android"][Math.max(0,terminalMode.currentIndex)]
                onChosen:function(text){root.fillTerminalQuick(selectedEntry,terminalQuick)}
            }
        }
    }
}
