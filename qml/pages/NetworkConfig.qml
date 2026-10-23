import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components" as C

C.Panel {
    id: root
    required property var backend
    Layout.preferredWidth: 248; Layout.fillHeight: true
    ScrollView {
        anchors.fill: parent; anchors.margins: 18; contentWidth: availableWidth; clip: true
        ColumnLayout {
            width: parent.width; spacing: 12
            Text { text: "网络连接"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
            Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line; Layout.topMargin: 8 }
            Text { text: "通信模式"; color: C.Theme.muted; font.pixelSize: 12 }
            C.SelectBox { id: mode; objectName: "networkMode"; model: ["TCP 客户端", "TCP 服务端", "UDP"]; Layout.fillWidth: true; enabled: !root.backend.connected && !root.backend.busy }
            Text { text: mode.currentIndex === 1 ? "本地监听 IP" : "目标主机 / IP"; color: C.Theme.muted; font.pixelSize: 12 }
            C.InputBox { id: host; objectName: "networkHost"; text: "127.0.0.1"; Layout.fillWidth: true; enabled: mode.enabled }
            Text { text: mode.currentIndex === 1 ? "监听端口（0 为自动分配）" : "目标端口"; color: C.Theme.muted; font.pixelSize: 12 }
            C.InputBox { id: port; objectName: "networkPort"; text: "8765"; Layout.fillWidth: true; enabled: mode.enabled; validator: IntValidator { bottom: 0; top: 65535 } }
            Text { visible: mode.currentIndex === 2; text: "本地绑定 IP"; color: C.Theme.muted; font.pixelSize: 12 }
            C.InputBox { id: bind; visible: mode.currentIndex === 2; text: "0.0.0.0"; Layout.fillWidth: true; enabled: mode.enabled }
            Text { visible: mode.currentIndex === 2; text: "本地端口（0 为自动分配）"; color: C.Theme.muted; font.pixelSize: 12 }
            C.InputBox { id: localPort; visible: mode.currentIndex === 2; text: "0"; Layout.fillWidth: true; enabled: mode.enabled; validator: IntValidator { bottom: 0; top: 65535 } }
            C.ActionButton {
                objectName: "networkConnectButton"; Layout.fillWidth: true; implicitHeight: 42; Layout.topMargin: 8
                text: root.backend.busy ? "取消连接" : root.backend.connected ? "关闭网络" : mode.currentIndex === 1 ? "开始监听" : "打开网络"
                primary: !root.backend.connected; danger: root.backend.connected
                onClicked: {
                    if (root.backend.connected || root.backend.busy) root.backend.closePort()
                    else root.backend.openNetwork({mode: mode.currentIndex, host: host.text, port: Number(port.text), bind: bind.text, localPort: Number(localPort.text)})
                }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11; lineHeight: 1.5; color: C.Theme.subtle
                text: mode.currentIndex === 1 ? "一次接入一个对端。等待对端时不能发送；对端断开后继续监听。"
                    : mode.currentIndex === 2 ? "UDP 目标填写 IP。每个数据报独立解析；本地端口用于接收，来源不限于目标地址。UDP 不保证送达或顺序。"
                    : "客户端支持 IP 或主机名。连接超时 10 秒，可取消。"
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: C.Theme.muted; font.pixelSize: 11; text: "开发板桥接时，在协议页选择网络来源，并应用 JSON 行或相同帧头尾配置。" }
        }
    }
}
