import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components" as C

C.Panel {
    id: root
    required property var backend
    property var settings: null
    property var discovery: null
    property bool detailed: false
    property bool advanced: false
    function applyDevice(device) {
        if(root.backend.connected || root.backend.busy)return false
        const config=device.config || device
        mode.currentIndex=Number.isInteger(config.mode) && config.mode>=0 && config.mode<=2 ? config.mode : 0
        hostInput.text=config.host || device.address
        if(config.port)portInput.text=String(config.port)
        return true
    }
    C.PersistentForm {
        settings: root.settings; scope: "network"
        fields:[{key:"mode",object:mode,property:"currentIndex"},{key:"host",object:hostInput,property:"text"},
            {key:"port",object:portInput,property:"text"},{key:"bind",object:bind,property:"text"},
            {key:"localPort",object:localPort,property:"text"},{key:"link",object:link,property:"currentIndex"},
            {key:"advanced",object:root,property:"advanced"}]
    }
    Layout.preferredWidth: 248; Layout.fillHeight: true
    ScrollView {
        anchors.fill: parent; anchors.margins: 18; contentWidth: availableWidth; clip: true
        ColumnLayout {
            width: parent.width; spacing: 12
            Text { text: "网络连接"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
            C.SelectBox { id: link; Layout.fillWidth: true; model:["网线直连 / 有线 LAN","无线局域网","其他 TCP / UDP 网络"] }
            C.SelectBox {
                Layout.fillWidth: true; model: root.discovery ? root.discovery.interfaces : []; textRole: "address"
                displayText: count ? currentText : "等待本机网卡"
                onActivated: {if(mode.currentIndex===1)hostInput.text=model[currentIndex].address;else bind.text=model[currentIndex].address}
                ToolTip.visible: hovered; ToolTip.text: "选择本机 IP 用于监听或 UDP 绑定，不修改 Windows 网卡设置"
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line; Layout.topMargin: 8 }
            Text { text: "通信模式"; color: C.Theme.muted; font.pixelSize: 12 }
            C.SelectBox { id: mode; objectName: "networkMode"; model: ["TCP 客户端", "TCP 服务端", "UDP"]; Layout.fillWidth: true; enabled: !root.backend.connected && !root.backend.busy }
            C.NetworkTargets {
                visible:mode.currentIndex!==1;Layout.fillWidth:true;enabled:mode.enabled
                discovery:root.discovery;settings:root.settings;advanced:root.advanced
                host:hostInput.text;port:Number(portInput.text)
                onTargetChosen:function(device){root.applyDevice(device)}
            }
            Text { text: mode.currentIndex === 1 ? "本地监听 IP" : "目标主机 / IP"; color: C.Theme.muted; font.pixelSize: 12 }
            C.InputBox { id: hostInput; objectName: "networkHost"; text: "127.0.0.1"; Layout.fillWidth: true; enabled: mode.enabled }
            Text { text: mode.currentIndex === 1 ? "监听端口（0 为自动分配）" : "目标端口"; color: C.Theme.muted; font.pixelSize: 12 }
            C.InputBox { id: portInput; objectName: "networkPort"; text: "8765"; Layout.fillWidth: true; enabled: mode.enabled; validator: IntValidator { bottom: 0; top: 65535 } }
            C.ActionButton { text: root.advanced ? "收起高级" : "高级 · 扫描 / 本地绑定"; Layout.fillWidth: true; onClicked: root.advanced=!root.advanced }
            Text { visible: mode.currentIndex === 2 && root.advanced; text: "本地绑定 IP"; color: C.Theme.muted; font.pixelSize: 12 }
            C.InputBox { id: bind; visible: mode.currentIndex === 2 && root.advanced; text: "0.0.0.0"; Layout.fillWidth: true; enabled: mode.enabled }
            Text { visible: mode.currentIndex === 2 && root.advanced; text: "本地端口（0 为自动分配）"; color: C.Theme.muted; font.pixelSize: 12 }
            C.InputBox { id: localPort; visible: mode.currentIndex === 2 && root.advanced; text: "0"; Layout.fillWidth: true; enabled: mode.enabled; validator: IntValidator { bottom: 0; top: 65535 } }
            C.ActionButton {
                objectName: "networkConnectButton"; Layout.fillWidth: true; implicitHeight: 42; Layout.topMargin: 8
                text: root.backend.busy ? "取消连接" : root.backend.connected ? "关闭网络" : mode.currentIndex === 1 ? "开始监听" : "打开网络"
                primary: !root.backend.connected; danger: root.backend.connected
                onClicked: {
                    if (root.backend.connected || root.backend.busy) root.backend.closePort()
                    else root.backend.openNetwork({mode: mode.currentIndex, host: hostInput.text, port: Number(portInput.text), bind: bind.text, localPort: Number(localPort.text)})
                }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11; lineHeight: 1.5; color: C.Theme.subtle
                visible: root.detailed || root.advanced
                text: mode.currentIndex === 1 ? "一次接入一个对端。等待对端时不能发送；对端断开后继续监听。"
                    : mode.currentIndex === 2 ? "UDP 目标填写 IP。每个数据报独立解析；本地端口用于接收，来源不限于目标地址。UDP 不保证送达或顺序。"
                    : "客户端支持 IP 或主机名。连接超时 10 秒，可取消。"
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: C.Theme.muted; font.pixelSize: 11; text: link.currentIndex===0 ? "电脑和板卡网口接线后，双方 IP 需在同一网段。选择本机网卡查看地址，再填写板端 IP。" : link.currentIndex===1 ? "双方加入同一 Wi-Fi / 局域网，使用板端 IP。接入点的客户端隔离可能阻止互通。" : "HTTP / MQTT 跨网调试请使用顶部对应入口；目标需有可达服务。" }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: C.Theme.muted; font.pixelSize: 11; text: "开发板桥接时，在协议页选择网络来源，并应用 JSON 行或相同帧头尾配置。" }
        }
    }
}
