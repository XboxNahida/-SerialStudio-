import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components" as C

Item {
    id: root
    required property var controller
    required property var serialBackend
    required property var networkBackend
    readonly property var terminal: controller ? controller.terminal : null
    readonly property var adb: terminal ? terminal.adb : null
    Binding { target: root.adb; property: "enabled"; value: root.visible && terminalMode.currentIndex===3; when: root.adb!==null }
    FileDialog {
        id: adbDialog; title: "选择 Android platform-tools 中的 adb.exe"; nameFilters: ["ADB 工具 (adb.exe)"]
        onAccepted: if(root.adb) { root.adb.executablePath=selectedFile.toString(); adbPath.text=root.adb.executablePath }
    }
    function sshConfig() { return {host:sshHost.text, user:sshUser.text, port:Number(sshPort.text), key:sshKey.text} }
    function openTerminal() {
        if(!root.controller)return
        if(terminalMode.currentIndex===0)root.controller.openTerminal(sshConfig())
        else if(terminal) {
            if(terminalMode.currentIndex===3) {
                if(root.adb && root.adb.ready && !root.adb.scanning)terminal.open(3,{adb:root.adb.resolvedPath,serial:root.adb.selectedSerial})
            } else terminal.open(terminalMode.currentIndex,{})
        }
    }
    CommandAssistant { id: assistant; controller: root.terminal; selectedMode: terminalMode.currentIndex }
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
                Layout.fillWidth: true; Layout.preferredHeight: terminalMode.currentIndex===3 ? 438 : 346
                ScrollView {
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
                        visible: terminalMode.currentIndex===0 || bus.currentIndex!==4
                        C.InputBox { id: sshHost; objectName: "sshHost"; placeholderText: "板端 IP / 主机名"; Layout.fillWidth: true; implicitWidth: 160 }
                        C.InputBox { id: sshUser; text: "root"; placeholderText: "用户名"; Layout.preferredWidth: 105 }
                        C.InputBox { id: sshPort; text: "22"; Layout.preferredWidth: 65; validator: IntValidator { bottom: 1; top: 65535 } }
                    }
                    C.InputBox { id: sshKey; visible: terminalMode.currentIndex===0 || bus.currentIndex!==4; placeholderText: "SSH 私钥路径（可选；也可用默认密钥 / ssh-agent）"; Layout.fillWidth: true }
                    RowLayout {
                        visible: terminalMode.currentIndex===3
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
                            onActivated: if(root.adb)root.adb.selectedSerial=model[index].serial
                        }
                        C.ActionButton { objectName: "adbRefreshButton"; text: root.adb && root.adb.scanning ? "检测中…" : "刷新设备"; implicitWidth: 90; enabled: root.adb && !root.adb.scanning; onClicked: root.adb.refresh() }
                    }
                    Text { objectName: "adbStatus"; visible: terminalMode.currentIndex===3; Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.adb ? root.adb.status : "ADB 检测未加载"; color: root.adb && root.adb.ready ? C.Theme.accent : C.Theme.muted; font.pixelSize: 11 }
                    Text { visible: terminalMode.currentIndex===3 && text.length>0; Layout.fillWidth: true; text: root.adb ? root.adb.resolvedPath : ""; elide: Text.ElideMiddle; color: C.Theme.subtle; font.pixelSize: 10; ToolTip.visible: pathMouse.containsMouse; ToolTip.text: text; MouseArea { id: pathMouse; anchors.fill: parent; hoverEnabled: true } }
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: terminalMode.currentIndex===0 ? "独立 SSH 窗口支持密码、vim/top。APP 板端操作使用密钥；首次登录请确认指纹。" : terminalMode.currentIndex===3 ? "每 3 秒自动检测；单设备自动选择，多设备请选目标。未授权时在板端确认 USB 调试；无线 ADB 先配对 / connect。" : "在本机独立控制台运行；指令库分别匹配 PowerShell / CMD 语法。"; color: C.Theme.subtle; font.pixelSize: 10; lineHeight: 1.3 }
                    Flow {
                        Layout.fillWidth: true; spacing: 5
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
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 10
                    Text { text: "请求 / 响应"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "串口、网络沿用各自的 JSON 行或自定义帧头尾配置。板端应运行匹配的桥接程序；操作仅在点击后发送。"; color: C.Theme.muted; font.pixelSize: 11 }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        TextArea {
                            text: root.controller ? root.controller.output : "板端控制器未加载"
                            color: root.controller && !root.controller.success ? C.Theme.red : C.Theme.text
                            font.family: "Consolas"; font.pixelSize: 12; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                            background: Rectangle { color: C.Theme.consoleBg; radius: 7; border.color: C.Theme.line }
                        }
                    }
                }
            }
        }
    }
}
