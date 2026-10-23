import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components" as C

Item {
    id: root
    required property var backend
    property var settings: null
    property bool detailed: false
    property string scope: "serial"
    property bool follow: true
    property bool showData: false
    property string restoreError: ""
    onScopeChanged: {
        if(!form || !mode || !head)return
        form.disconnectFields()
        mode.currentIndex=backend && backend.parserMode!==undefined?backend.parserMode:1
        head.text="AA 55";tail.text="0D 0A";view.currentIndex=0;root.showData=false
        followButton.checked=true;root.follow=true;advanced.checked=false;root.restoreError=""
        Qt.callLater(function(){form.restore()})
    }
    function applyRules(remember) {
        if(!backend || typeof backend.configureParser!=="function")return false
        const ok=backend.configureParser(mode.currentIndex,head.text,tail.text)
        if(!ok) {restoreError=backend.lastError || "解析规则无效，请检查帧头和帧尾";return false}
        restoreError=""
        const key="protocol/"+scope+"/applied"
        if(settings && (remember || settings.value(key,null)===null))settings.setValue(key,{mode:mode.currentIndex,head:head.text,tail:tail.text})
        return true
    }
    C.PersistentForm {
        id:form;settings:root.settings;scope:"protocol/"+root.scope
        fields:[{key:"mode",object:mode,property:"currentIndex"},{key:"head",object:head,property:"text"},
            {key:"tail",object:tail,property:"text"},{key:"view",object:view,property:"currentIndex"},
            {key:"follow",object:followButton,property:"checked"},{key:"advanced",object:advanced,property:"checked"}]
        onRestored: {
            root.showData=view.currentIndex===1;root.follow=followButton.checked
            const applied=root.settings.value(scope+"/applied",null)
            if(applied && root.backend && typeof root.backend.configureParser==="function") {
                if(!root.backend.configureParser(applied.mode,applied.head,applied.tail))root.restoreError=root.backend.lastError || "已保存解析规则恢复失败"
            } else root.applyRules(false)
        }
    }
    RowLayout {
        anchors.fill: parent; spacing: 16
        C.Panel {
            Layout.preferredWidth: 248; Layout.fillHeight: true
            ScrollView {
                anchors.fill:parent;anchors.margins:18;contentWidth:availableWidth;clip:true
            ColumnLayout {
                width:parent.width;spacing:14
                Text { text: "解析配置"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line }
                Text { text: "协议类型"; color: C.Theme.muted; font.pixelSize: 12 }
                C.SelectBox {
                    id: mode;objectName:"parserMode"; Layout.fillWidth: true
                    model: ["原始字节流","文本行 / JSON","Modbus RTU","自定义帧头 / 帧尾"]
                    currentIndex: root.backend.parserMode
                }
                Text { visible: mode.currentIndex === 3; text: "帧头 / HEX"; color: C.Theme.muted; font.pixelSize: 12 }
                C.InputBox { id: head;objectName:"parserHead"; visible: mode.currentIndex === 3; Layout.fillWidth: true; text: "AA 55"; font.family: "Consolas" }
                Text { visible: mode.currentIndex === 3; text: "帧尾 / HEX"; color: C.Theme.muted; font.pixelSize: 12 }
                C.InputBox { id: tail;objectName:"parserTail"; visible: mode.currentIndex === 3; Layout.fillWidth: true; text: "0D 0A"; font.family: "Consolas" }
                C.ActionButton {
                    objectName: "applyParserButton"
                    text: "应用解析配置"; primary: true; Layout.fillWidth: true
                    onClicked: if (root.applyRules(true)) applied.restart()
                }
                Text {visible:text!=="";text:root.restoreError || form.error;Layout.fillWidth:true;wrapMode:Text.Wrap;color:C.Theme.red;font.pixelSize:11}
                Text { id: appliedText; text: "已应用，残留半帧已清除"; visible: applied.running; color: C.Theme.accent; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Timer { id: applied; interval: 2400 }
                Text { text: "解析输出"; color: C.Theme.muted; font.pixelSize: 12 }
                C.SelectBox {id:view; objectName: "protocolView"; Layout.fillWidth: true; model: ["原始报文列表","数据 / 图表 / 媒体"]; currentIndex: root.showData ? 1:0; onActivated: root.showData=currentIndex===1 }
                C.ActionButton {id:advanced;checkable:true;text:checked?"收起详细说明":"详细说明与离线演示";Layout.fillWidth:true}
                Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line }
                Text {
                    visible:root.detailed || advanced.checked
                    Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.65
                    text: mode.currentIndex === 0 ? "保留原始字节内容，适合直接观察未知协议。显示不改变实际字节。"
                        : mode.currentIndex === 1 ? "以 LF 分帧，兼容 CRLF。完整行是 JSON 对象或数组时自动格式化，其他内容按 UTF-8 文本显示。"
                        : mode.currentIndex === 2 ? "支持功能码 01 / 02 / 03 / 04 / 05 / 06 / 0F / 10 及异常响应。自动检查 CRC16，展示从站、地址、数量与寄存器值。"
                        : "连续缓存字节流，以帧头和帧尾提取完整帧。适合简单二进制协议；暂不处理转义或长度字段。"
                    color: C.Theme.muted; font.pixelSize: 12
                }
                Text {
                    visible: mode.currentIndex === 2 && (root.detailed || advanced.checked)
                    Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.5
                    text: "Windows 空闲定时具有精度限制。此功能用于报文观察，不替代严格的 Modbus 总线时序检测。"
                    color: C.Theme.orange; font.pixelSize: 11
                }
                Item { Layout.fillHeight: true }
                C.ActionButton {visible:root.detailed || advanced.checked; text: "加载本协议演示"; Layout.fillWidth: true; enabled: !root.backend.connected && !root.backend.busy; onClicked: { if (root.applyRules(true)) root.backend.demo() } }
                Text {visible:root.detailed || advanced.checked; text: "演示数据不计入串口 RX / TX"; color: C.Theme.subtle; font.pixelSize: 10 }
            }
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
                    C.ActionButton {id:followButton;checkable:true;checked:true; text: root.follow ? "自动滚动" : "暂停滚动"; implicitHeight: 30; onToggled:root.follow=checked }
                    C.ActionButton { text: "清空报文"; implicitHeight: 30; onClicked: root.backend.clearFrames() }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: C.Theme.line }
                C.MessageSelectionBar {
                    id: messageSelection; objectName: "protocolMessageSelection"
                    Layout.fillWidth: true; eventModel: root.backend.frames
                    detailed: root.detailed; protocol: true
                }
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
                        objectName: "protocolMessageList"
                        Keys.onPressed: function(event) { messageSelection.handleCopy(event) }
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
                            required property string eventId
                            required property bool eventSelected
                            width: list.width; height: content.implicitHeight + 28
                            color: frame.eventSelected ? C.Theme.accentFill : C.Theme.consoleBg; radius: 10; border.color: C.Theme.line
                            ColumnLayout {
                                id: content; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 14; y: 14; spacing: 9
                                RowLayout {
                                    CheckBox {
                                        objectName: "protocolSelect_" + frame.eventId
                                        visible: messageSelection.active; checked: frame.eventSelected
                                        implicitWidth: 28; implicitHeight: 28
                                        Accessible.name: "选择报文 " + frame.heading
                                        onClicked: root.backend.frames.setEventSelected(frame.eventId, checked)
                                    }
                                    Rectangle { width: 6; height: 6; radius: 3; color: frame.valid ? C.Theme.accent : C.Theme.red }
                                    Text { text: frame.heading; color: C.Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                                    Text { visible: frame.kind === "DEMO"; text: "离线演示"; color: C.Theme.orange; font.pixelSize: 10 }
                                    Item { Layout.fillWidth: true }
                                    Text {visible:root.detailed; text: frame.stamp; color: C.Theme.subtle; font.family: "Consolas"; font.pixelSize: 11 }
                                }
                                TextEdit {
                                    Layout.fillWidth: true; text: frame.detail; readOnly: true; selectByMouse: true
                                    textFormat: TextEdit.PlainText; wrapMode: TextEdit.WrapAnywhere
                                    color: frame.valid ? C.Theme.muted : C.Theme.red; font.family: "Microsoft YaHei UI"; font.pixelSize: 12
                                    Keys.onPressed: function(event) { if (selectedText.length === 0) messageSelection.handleCopy(event) }
                                }
                                TextEdit {
                                    visible:root.detailed
                                    Layout.fillWidth: true; text: frame.hexData; readOnly: true; selectByMouse: true
                                    textFormat: TextEdit.PlainText; wrapMode: TextEdit.WrapAnywhere
                                    color: C.Theme.subtle; font.family: "Consolas"; font.pixelSize: 11
                                    Keys.onPressed: function(event) { if (selectedText.length === 0) messageSelection.handleCopy(event) }
                                }
                            }
                        }
                    }
                }
                Text { text: "保留最近 " + root.backend.frames.count + " / 500 条 · 已轮换 " + root.backend.frames.dropped + " 条"; color: C.Theme.subtle; font.pixelSize: 10 }
            }
        }
        DataPanel { visible: root.showData; Layout.fillWidth: true; Layout.fillHeight: true; workspace: root.backend.analysis || null;settings:root.settings;detailed:root.detailed;scope:root.scope }
    }
}
