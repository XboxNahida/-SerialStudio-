import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components" as C

Item {
    id: root
    required property var backend
    property bool follow: true
    property bool showData: false
    RowLayout {
        anchors.fill: parent; spacing: 16
        C.Panel {
            Layout.preferredWidth: 248; Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 14
                Text { text: "解析配置"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line }
                Text { text: "协议类型"; color: C.Theme.muted; font.pixelSize: 12 }
                C.SelectBox {
                    id: mode; Layout.fillWidth: true
                    model: ["原始字节流","文本行 / JSON","Modbus RTU","自定义帧头 / 帧尾"]
                    currentIndex: root.backend.parserMode
                }
                Text { visible: mode.currentIndex === 3; text: "帧头 / HEX"; color: C.Theme.muted; font.pixelSize: 12 }
                C.InputBox { id: head; visible: mode.currentIndex === 3; Layout.fillWidth: true; text: "AA 55"; font.family: "Consolas" }
                Text { visible: mode.currentIndex === 3; text: "帧尾 / HEX"; color: C.Theme.muted; font.pixelSize: 12 }
                C.InputBox { id: tail; visible: mode.currentIndex === 3; Layout.fillWidth: true; text: "0D 0A"; font.family: "Consolas" }
                C.ActionButton {
                    objectName: "applyParserButton"
                    text: "应用解析配置"; primary: true; Layout.fillWidth: true
                    onClicked: if (root.backend.configureParser(mode.currentIndex, head.text, tail.text)) applied.restart()
                }
                Text { id: appliedText; text: "已应用，残留半帧已清除"; visible: applied.running; color: C.Theme.accent; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Timer { id: applied; interval: 2400 }
                Text { text: "解析输出"; color: C.Theme.muted; font.pixelSize: 12 }
                C.SelectBox { objectName: "protocolView"; Layout.fillWidth: true; model: ["原始报文列表","数据 / 图表 / 媒体"]; currentIndex: root.showData ? 1:0; onActivated: root.showData=currentIndex===1 }
                Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.65
                    text: mode.currentIndex === 0 ? "保留原始字节内容，适合直接观察未知协议。显示不改变实际字节。"
                        : mode.currentIndex === 1 ? "以 LF 分帧，兼容 CRLF。完整行是 JSON 对象或数组时自动格式化，其他内容按 UTF-8 文本显示。"
                        : mode.currentIndex === 2 ? "支持功能码 01 / 02 / 03 / 04 / 05 / 06 / 0F / 10 及异常响应。自动检查 CRC16，展示从站、地址、数量与寄存器值。"
                        : "连续缓存字节流，以帧头和帧尾提取完整帧。适合简单二进制协议；暂不处理转义或长度字段。"
                    color: C.Theme.muted; font.pixelSize: 12
                }
                Text {
                    visible: mode.currentIndex === 2
                    Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.5
                    text: "Windows 空闲定时具有精度限制。此功能用于报文观察，不替代严格的 Modbus 总线时序检测。"
                    color: C.Theme.orange; font.pixelSize: 11
                }
                Item { Layout.fillHeight: true }
                C.ActionButton { text: "加载本协议演示"; Layout.fillWidth: true; enabled: !root.backend.connected && !root.backend.busy; onClicked: { if (root.backend.configureParser(mode.currentIndex, head.text, tail.text)) root.backend.demo() } }
                Text { text: "演示数据不计入串口 RX / TX"; color: C.Theme.subtle; font.pixelSize: 10 }
            }
        }
        C.Panel {
            visible: !root.showData
            Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 14
                RowLayout {
                    Text { text: "结构化报文"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    C.ActionButton { text: root.follow ? "自动滚动" : "暂停滚动"; implicitHeight: 30; onClicked: root.follow = !root.follow }
                    C.ActionButton { text: "清空报文"; implicitHeight: 30; onClicked: root.backend.clearFrames() }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Column {
                        anchors.centerIn: parent; spacing: 14; visible: root.backend.frames.count === 0
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "{ }"; color: C.Theme.accent; font.family: "Consolas"; font.pixelSize: 38 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "让字节变成可读信息"; color: C.Theme.text; font.pixelSize: 18 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "选择解析方式并应用，等待完整报文"; color: C.Theme.subtle; font.pixelSize: 12 }
                    }
                    ListView {
                        id: list; anchors.fill: parent; clip: true; spacing: 10
                        model: root.backend.frames
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        onCountChanged: if (root.follow) Qt.callLater(positionViewAtEnd)
                        delegate: Rectangle {
                            id: frame
                            required property string stamp
                            required property string kind
                            required property string heading
                            required property string detail
                            required property string hexData
                            required property bool valid
                            width: list.width; height: content.implicitHeight + 28
                            color: C.Theme.consoleBg; radius: 10; border.color: C.Theme.line
                            ColumnLayout {
                                id: content; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 14; y: 14; spacing: 9
                                RowLayout {
                                    Rectangle { width: 6; height: 6; radius: 3; color: frame.valid ? C.Theme.accent : C.Theme.red }
                                    Text { text: frame.heading; color: C.Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                                    Text { visible: frame.kind === "DEMO"; text: "离线演示"; color: C.Theme.orange; font.pixelSize: 10 }
                                    Item { Layout.fillWidth: true }
                                    Text { text: frame.stamp; color: C.Theme.subtle; font.family: "Consolas"; font.pixelSize: 11 }
                                }
                                TextEdit {
                                    Layout.fillWidth: true; text: frame.detail; readOnly: true; selectByMouse: true
                                    textFormat: TextEdit.PlainText; wrapMode: TextEdit.WrapAnywhere
                                    color: frame.valid ? C.Theme.muted : C.Theme.red; font.family: "Microsoft YaHei UI"; font.pixelSize: 12
                                }
                                TextEdit {
                                    Layout.fillWidth: true; text: frame.hexData; readOnly: true; selectByMouse: true
                                    textFormat: TextEdit.PlainText; wrapMode: TextEdit.WrapAnywhere
                                    color: C.Theme.subtle; font.family: "Consolas"; font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
                Text { text: "保留最近 " + root.backend.frames.count + " / 500 条 · 已轮换 " + root.backend.frames.dropped + " 条"; color: C.Theme.subtle; font.pixelSize: 10 }
            }
        }
        DataPanel { visible: root.showData; Layout.fillWidth: true; Layout.fillHeight: true; workspace: root.backend.analysis || null }
    }
}
