import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components" as C

Item {
    id: root
    objectName: networkMode ? "networkTerminalPage" : "terminalPage"
    required property var backend
    property bool networkMode: false
    property var settings: null
    property var quickCommands: null
    property var discovery: null
    property bool detailed: false
    property bool advanced: false
    property string selectedPort: ""
    function restorePort() {
        let index=-1
        for(let i=0;i<backend.ports.length;i++)if(backend.ports[i].name===selectedPort)index=i
        portBox.currentIndex=index
    }
    function refreshSerialPorts() {if(!networkMode && backend && !backend.busy)backend.refreshPorts()}
    onSelectedPortChanged: restorePort()
    onVisibleChanged: if(visible)refreshSerialPorts()
    Timer {interval:3000;repeat:true;running:root.visible && !root.networkMode;onTriggered:root.refreshSerialPorts()}
    Connections {target:root.backend;function onPortsChanged(){root.restorePort()}}
    C.PersistentForm {
        settings: root.settings; scope: root.networkMode ? "networkTerminal" : "serial"
        fields: [{key:"baud",object:baudInput,property:"text"},{key:"bits",object:bitsBox,property:"currentIndex"},
            {key:"parity",object:parityBox,property:"currentIndex"},{key:"stop",object:stopBox,property:"currentIndex"},
            {key:"flow",object:flowBox,property:"currentIndex"},{key:"draft",object:sendInput,property:"text"},
            {key:"sendMode",object:sendMode,property:"currentIndex"},{key:"newline",object:newline,property:"currentIndex"},
            {key:"interval",object:intervalInput,property:"text"},{key:"hexView",object:root,property:"hexView"},
            {key:"follow",object:root,property:"follow"},{key:"advanced",object:root,property:"advanced"},
            {key:"portName",object:root,property:"selectedPort"}]
        onRestored:root.restorePort()
    }
    signal saveRequested()
    function useDevice(device) {
        if(root.networkMode){if(networkLoader.item)networkLoader.item.applyDevice(device.config||device)}
        else {root.backend.refreshPorts();root.selectedPort=device.address;root.restorePort()}
    }
    property bool hexView: false
    property bool follow: true
    property var baudRates: ["300","600","1200","2400","4800","9600","14400","19200","38400","57600","115200","230400","460800","921600","1000000","1500000","2000000","3000000","4000000"]
    function connectPort() {
        if (backend.connected) { backend.closePort(); return }
        if(portBox.currentIndex<0)return
        backend.openPort({port: portBox.currentIndex >= 0 ? backend.ports[portBox.currentIndex].name : "",
                          baud: baudInput.text, dataBits: Number(bitsBox.currentText),
                          parity: [0,2,3,4,5][parityBox.currentIndex],
                          stopBits: [1,2,3][stopBox.currentIndex], flow: flowBox.currentIndex})
    }
    Component.onCompleted: {
        const config = backend.preferences.connection || {}
        if (config.baud) baudInput.text = String(config.baud)
        if (config.dataBits) bitsBox.currentIndex = ["5","6","7","8"].indexOf(String(config.dataBits))
        if (config.parity !== undefined) parityBox.currentIndex = [0,2,3,4,5].indexOf(config.parity)
        if (config.stopBits !== undefined) stopBox.currentIndex = [1,2,3].indexOf(config.stopBits)
        if (config.flow !== undefined) flowBox.currentIndex = config.flow
        if(!selectedPort)selectedPort=config.port || (backend.ports.length ? backend.ports[0].name : "")
        restorePort()
    }
    RowLayout {
        anchors.fill: parent
        spacing: 16
        C.Panel {
            visible: !root.networkMode
            Layout.preferredWidth: 248
            Layout.fillHeight: true
            ScrollView {
                anchors.fill: parent
                anchors.margins: 18
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "连接配置"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        Text { text: "UART"; color: C.Theme.subtle; font.family: "Consolas"; font.pixelSize: 11 }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line; Layout.topMargin: 3; Layout.bottomMargin: 3 }
                    Text { text: "串口设备"; color: C.Theme.muted; font.pixelSize: 12 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        C.SelectBox {
                            id: portBox; objectName:"serialPort";Layout.fillWidth: true; implicitWidth: 155
                            model: root.backend.ports; textRole: "label"
                            displayText: currentIndex>=0 ? currentText : root.selectedPort ? root.selectedPort+" · 未连接 / 离线" : "未发现串口"
                            enabled: !root.backend.connected && !root.backend.busy
                            popup.onAboutToShow: root.refreshSerialPorts()
                            onActivated:function(index){if(index>=0 && index<root.backend.ports.length)root.selectedPort=root.backend.ports[index].name}
                        }
                        C.ActionButton { text: "↻"; implicitWidth: 36; leftPadding: 5; rightPadding: 5; enabled: !root.backend.busy; onClicked: root.refreshSerialPorts(); ToolTip.visible: hovered; ToolTip.text: "刷新串口设备" }
                    }
                    Text { text: "波特率 / bps"; color: C.Theme.muted; font.pixelSize: 12 }
                    RowLayout {
                        spacing: 6
                        C.InputBox {
                            id: baudInput; objectName: "baudInput"; text: "115200"; Layout.fillWidth: true; implicitWidth: 110
                            validator: RegularExpressionValidator { regularExpression: /[0-9]{1,10}/ }
                            enabled: !root.backend.connected && !root.backend.busy
                            font.family: "Consolas"
                        }
                        C.SelectBox {
                            id: baudPreset; implicitWidth: 94; model: root.baudRates; currentIndex: 10
                            enabled: baudInput.enabled
                            onActivated: baudInput.text = currentText
                            ToolTip.visible: hovered; ToolTip.text: "常用值；左侧可输入自定义波特率"
                        }
                    }
                    C.ActionButton { text: root.advanced ? "收起高级" : "高级设置"; Layout.fillWidth: true; onClicked: root.advanced=!root.advanced }
                    RowLayout {
                        visible: root.advanced
                        Layout.fillWidth: true; spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 7
                            Text { text: "数据位"; color: C.Theme.muted; font.pixelSize: 12 }
                            C.SelectBox { id: bitsBox; Layout.fillWidth: true; implicitWidth: 80; model: ["5","6","7","8"]; currentIndex: 3; enabled: baudInput.enabled }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 7
                            Text { text: "停止位"; color: C.Theme.muted; font.pixelSize: 12 }
                            C.SelectBox { id: stopBox; Layout.fillWidth: true; implicitWidth: 80; model: ["1","2","1.5"]; enabled: baudInput.enabled }
                        }
                    }
                    Text { visible: root.advanced; text: "校验方式"; color: C.Theme.muted; font.pixelSize: 12 }
                    C.SelectBox { id: parityBox; visible: root.advanced; Layout.fillWidth: true; model: ["无校验 / None","偶校验 / Even","奇校验 / Odd","空校验 / Space","标记 / Mark"]; enabled: baudInput.enabled }
                    Text { visible: root.advanced; text: "流控"; color: C.Theme.muted; font.pixelSize: 12 }
                    C.SelectBox { id: flowBox; visible: root.advanced; Layout.fillWidth: true; model: ["无流控","硬件 RTS / CTS","软件 XON / XOFF"]; enabled: baudInput.enabled }
                    C.ActionButton {
                        objectName: "connectButton"
                        Layout.fillWidth: true; Layout.topMargin: 6; implicitHeight: 42
                        text: root.backend.busy ? "处理中…" : root.backend.connected ? "断开连接" : "连接串口"
                        primary: !root.backend.connected; danger: root.backend.connected
                        enabled: !root.backend.busy && (root.backend.connected || portBox.currentIndex>=0)
                        onClicked: root.connectPort()
                    }
                    RowLayout {
                        CheckBox { id: dtr; visible: root.advanced; text: "DTR"; palette.windowText: C.Theme.muted; enabled: root.backend.connected; onClicked: root.backend.setLines(checked, rts.checked) }
                        CheckBox { id: rts; visible: root.advanced; text: "RTS"; palette.windowText: C.Theme.muted; enabled: root.backend.connected && flowBox.currentIndex !== 1; onClicked: root.backend.setLines(dtr.checked, checked) }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line }
                    Text {
                        Layout.fillWidth: true
                        text: "支持自定义波特率\n实际速率由芯片与驱动决定"
                        color: C.Theme.subtle; font.pixelSize: 11; lineHeight: 1.5; wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "DTR / RTS 可能触发部分开发板复位，请按接线设置。"
                        color: C.Theme.subtle; font.pixelSize: 11; wrapMode: Text.Wrap
                    }
                    Item { Layout.preferredHeight: 4 }
                }
            }
        }
        Loader {
            id:networkLoader
            visible: root.networkMode; active: root.networkMode; Layout.preferredWidth: 248; Layout.fillHeight: true
            sourceComponent: Component { NetworkConfig { anchors.fill: parent; backend: root.backend; settings: root.settings; discovery: root.discovery; detailed: root.detailed } }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14
            C.Panel {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "收发终端"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Rectangle {
                            width: 50; height: 23; radius: 5; color: C.Theme.accentFill
                            Text { anchors.centerIn: parent; text: "LIVE"; color: C.Theme.accent; font.family: "Consolas"; font.pixelSize: 10 }
                        }
                        Item { Layout.fillWidth: true }
                        C.ActionButton { text: root.hexView ? "HEX" : "UTF-8"; implicitWidth: 66; implicitHeight: 30; onClicked: root.hexView = !root.hexView }
                        C.ActionButton { text: root.follow ? "自动滚动" : "暂停滚动"; implicitHeight: 30; onClicked: root.follow = !root.follow }
                        C.ActionButton { text: "保存"; implicitWidth: 58; implicitHeight: 30; onClicked: root.saveRequested() }
                        C.ActionButton { text: "清空"; implicitWidth: 58; implicitHeight: 30; onClicked: root.backend.clearTerminal() }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line }
                    C.MessageSelectionBar {
                        id: messageSelection; objectName: "terminalMessageSelection"
                        Layout.fillWidth: true; eventModel: root.backend.terminal
                        hex: root.hexView; detailed: root.detailed
                    }
                    Item {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        Column {
                            visible: root.backend.terminal.count === 0
                            anchors.centerIn: parent; spacing: 14
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 60; height: 60; radius: 18; color: C.Theme.raised
                                Text { anchors.centerIn: parent; text: ">_"; color: C.Theme.accent; font.family: "Consolas"; font.pixelSize: 25 }
                            }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "等待设备数据"; color: C.Theme.text; font.pixelSize: 17 }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.networkMode ? "打开网络开始调试，或加载离线演示" : "连接串口开始调试，或加载离线演示"; color: C.Theme.subtle; font.pixelSize: 12 }
                            C.ActionButton { objectName: "demoButton"; anchors.horizontalCenter: parent.horizontalCenter; text: "加载演示数据"; onClicked: root.backend.demo(); enabled: !root.backend.connected && !root.backend.busy }
                        }
                        ListView {
                            id: terminalList; anchors.fill: parent; clip: true; spacing: 4
                            objectName: "terminalMessageList"
                            Keys.onPressed: function(event) { messageSelection.handleCopy(event) }
                            visible: count > 0
                            model: root.backend.terminal
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar {}
                            delegate: Rectangle {
                                id: row
                                required property string stamp
                                required property string kind
                                required property string heading
                                required property string detail
                                required property string hexData
                                required property string textData
                                required property bool valid
                                required property string eventId
                                required property bool eventSelected
                                width: terminalList.width; height: entry.implicitHeight+16
                                color: row.eventSelected ? C.Theme.accentFill : row.kind === "DEMO" ? Qt.rgba(C.Theme.orange.r, C.Theme.orange.g, C.Theme.orange.b, 0.05) : "transparent"
                                radius: 6
                                RowLayout {
                                    id: entry; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 8; y: 8; spacing: 12
                                    CheckBox {
                                        objectName: "terminalSelect_" + row.eventId
                                        visible: messageSelection.active; checked: row.eventSelected
                                        Layout.alignment: Qt.AlignTop; implicitWidth: 28; implicitHeight: 28
                                        Accessible.name: "选择消息 " + row.stamp + " " + row.kind
                                        onClicked: root.backend.terminal.setEventSelected(row.eventId, checked)
                                    }
                                    Text { visible: root.detailed; text: row.stamp; color: C.Theme.subtle; font.family: "Consolas"; font.pixelSize: 11; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2 }
                                    Text { text: row.kind; color: row.kind === "TX" ? C.Theme.blue : row.kind === "DEMO" ? C.Theme.orange : C.Theme.accent; font.family: "Consolas"; font.pixelSize: 11; Layout.preferredWidth: 36; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2 }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 4
                                        Text { visible: root.detailed || (row.kind!=="RX" && row.kind!=="TX") || !row.valid; text: row.heading + (row.detail ? " · " + row.detail : ""); color: row.valid ? C.Theme.muted : C.Theme.red; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                        TextEdit {
                                            objectName: "terminalMessageText_" + row.eventId
                                            Layout.fillWidth: true
                                            visible: text.length > 0
                                            text: root.hexView ? row.hexData : row.textData
                                            color: C.Theme.text; font.family: "Consolas"; font.pixelSize: 12
                                            readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText
                                            wrapMode: TextEdit.WrapAnywhere; selectionColor: C.Theme.accentFill
                                            Keys.onPressed: function(event) { if (selectedText.length === 0) messageSelection.handleCopy(event) }
                                        }
                                    }
                                }
                            }
                            onCountChanged: if (root.follow) Qt.callLater(positionViewAtEnd)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "● RX"; color: C.Theme.accent; font.pixelSize: 10 }
                        Text { text: "● TX"; color: C.Theme.blue; font.pixelSize: 10 }
                        Text { text: "● DEMO"; color: C.Theme.orange; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: "保留 " + root.backend.terminal.count + " / 1000 条 · 已轮换 " + root.backend.terminal.dropped; color: C.Theme.subtle; font.pixelSize: 10 }
                    }
                }
            }
            C.Panel {
                Layout.fillWidth: true; Layout.preferredHeight: 174
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 9
                    RowLayout {
                        Text { text: "发送数据"; color: C.Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        C.SelectBox { id: sendMode; objectName: root.networkMode ? "networkSendMode" : "sendMode"; model: ["HEX","UTF-8 文本"]; currentIndex: 1; implicitWidth: 112; implicitHeight: 30 }
                        C.SelectBox { id: newline; model: ["不追加","LF / \\n","CRLF / \\r\\n","CR / \\r"]; implicitWidth: 130; implicitHeight: 30 }
                    }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        TextArea {
                            id: sendInput; objectName: root.networkMode ? "networkSendInput" : "sendInput"; text: ""
                            placeholderText: "输入要发送的数据…"
                            color: C.Theme.text; placeholderTextColor: C.Theme.subtle
                            font.family: "Consolas"; font.pixelSize: 13
                            selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                            background: Rectangle { color: C.Theme.consoleBg; radius: 7; border.color: C.Theme.line }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        C.InputBox {
                            id: intervalInput; text: "1000"; implicitWidth: 72; implicitHeight: 32
                            validator: IntValidator { bottom: 10; top: 3600000 }
                            enabled: !root.backend.periodic
                        }
                        Text { text: "ms"; color: C.Theme.subtle; font.pixelSize: 11 }
                        C.ActionButton {
                            text: root.backend.periodic ? "停止定时" : "定时发送"; implicitHeight: 32
                            enabled: root.backend.writable
                            onClicked: root.backend.periodic ? root.backend.stopPeriodic() : root.backend.startPeriodic(sendInput.text, sendMode.currentIndex === 0, newline.currentIndex, Number(intervalInput.text))
                        }
                        Item { Layout.fillWidth: true }
                        C.ActionButton { objectName: root.networkMode ? "networkSendButton" : "sendButton"; text: "发送数据  ↗"; primary: true; implicitHeight: 34; enabled: root.backend.writable; onClicked: root.backend.send(sendInput.text, sendMode.currentIndex === 0, newline.currentIndex) }
                    }
                }
            }
            C.QuickSendPanel {
                id: quickSend; Layout.fillWidth: true; repository: root.quickCommands
                scope: root.networkMode ? "network" : "serial"
                onChosen: function(text) { sendInput.text=text; sendMode.currentIndex=selectedEntry.hex?0:1; const endings=["","\n","\r\n","\r"]; const index=endings.indexOf(selectedEntry.newline); if(index>=0)newline.currentIndex=index }
            }
        }
    }
}
