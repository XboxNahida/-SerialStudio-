import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "components" as C
import "pages" as Pages

ApplicationWindow {
    id: root
    required property var backend
    property var networkBackend: backend
    property var board: null
    property var transfer: null
    property var files: null
    property var updater: null
    property var settings: null
    property var quickCommands: null
    property var discovery: null
    property var web: null
    property var mqtt: null
    property bool detailed: false
    property string appVersion: ""
    property int parserSource: 0
    readonly property var activeBackend: pageIndex === 3 || (pageIndex === 1 && parserSource === 1) ? networkBackend : backend
    property int pageIndex: 0
    onPageIndexChanged: if(pageIndex===6)pageIndex=0
    property bool lightMode: false
    C.PersistentForm {
        settings:root.settings;scope:"window"
        onRestored: if(root.settings && Number(root.settings.value("window/page",0))===6)root.settings.setValue("window/page",0)
        fields:[{key:"parserSource",object:root,property:"parserSource"},
            {key:"lightMode",object:root,property:"lightMode"},{key:"detailed",object:root,property:"detailed"}]
    }
    C.PersistentForm {
        settings:root.settings;scope:"windowV110"
        fields:[{key:"width",object:root,property:"width"},{key:"height",object:root,property:"height"}]
        onRestored: {
            root.width=Math.max(root.minimumWidth,Math.min(root.width,Screen.desktopAvailableWidth-40))
            root.height=Math.max(root.minimumHeight,Math.min(root.height,Screen.desktopAvailableHeight-60))
        }
    }
    visible: true
    width: 860
    height: 570
    minimumWidth: 760
    minimumHeight: 500
    title: "串口工作台 · Serial Studio " + appVersion
    color: C.Theme.background
    font.family: "Microsoft YaHei UI"
    font.pixelSize: 13
    palette.window: C.Theme.panel
    palette.windowText: C.Theme.text
    palette.text: C.Theme.text
    palette.base: C.Theme.consoleBg
    palette.highlight: C.Theme.accent
    palette.highlightedText: C.Theme.background
    function formatBytes(value) {
        return value >= 1000000 ? (value/1000000).toFixed(2) + " M" : value >= 1000 ? (value/1000).toFixed(1) + " K" : String(value)
    }
    Component.onCompleted: C.Theme.dark = lightMode ? false : backend.preferences.dark !== false
    onLightModeChanged: C.Theme.dark = !lightMode
    FileDialog {
        id: saveDialog; title: "保存当前终端记录"; fileMode: FileDialog.SaveFile
        nameFilters: ["JSON 行日志 (*.jsonl)", "所有文件 (*)"]
        defaultSuffix: "jsonl"
        onAccepted: root.activeBackend.saveLog(selectedFile)
    }
    Connections {
        target: root.backend
        function onNotify(message) { toastLabel.text = message; toastTimer.restart() }
    }
    Connections {
        target: root.networkBackend === root.backend ? null : root.networkBackend
        function onNotify(message) { toastLabel.text = message; toastTimer.restart() }
    }
    Drawer {
        id:navigation;objectName:"navigationDrawer"
        width:224;height:root.height;edge:Qt.LeftEdge
        background:Rectangle {color:C.Theme.sidebar;border.color:C.Theme.line}
        Rectangle {
            anchors.fill:parent;color:C.Theme.sidebar
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: C.Theme.line }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 8
                RowLayout {
                    Layout.topMargin: 13; spacing: 11
                    Rectangle {
                        width: 37; height: 37; radius: 11; color: C.Theme.accent
                        Text { anchors.centerIn: parent; text: "S"; color: "#0c3430"; font.family: "Consolas"; font.pixelSize: 25; font.weight: Font.Bold }
                    }
                    Column {
                        spacing: 2
                        Text { text: "SERIAL"; color: C.Theme.text; font.family: "Consolas"; font.pixelSize: 18; font.weight: Font.Bold; font.letterSpacing: 2 }
                        Text { text: "S T U D I O"; color: C.Theme.subtle; font.family: "Consolas"; font.pixelSize: 10 }
                    }
                }
                Text { text: "开发工具"; color: C.Theme.subtle; font.pixelSize: 10; Layout.topMargin: 6; Layout.leftMargin: 12; Layout.bottomMargin: 3 }
                Repeater {
                    model: [{page:0,label:"串口终端",icon:">_",sub:"TERMINAL"},{page:3,label:"网络调试",icon:"↔",sub:"NETWORK"},
                            {page:1,label:"协议解析",icon:"{}",sub:"PROTOCOL"},{page:4,label:"开发板总线",icon:"⌘",sub:"BOARD / SSH"},
                            {page:5,label:"文件传输",icon:"⇄",sub:"FILES / SFTP"},
                            {page:7,label:"HTTP / MQTT",icon:"☁",sub:"INTERNET"},{page:2,label:"设置与扩展",icon:"≡",sub:"SETTINGS"}]
                    delegate: Rectangle {
                        id: nav
                        objectName: "nav" + modelData.page
                        required property int index
                        required property var modelData
                        readonly property int page: modelData.page
                        Layout.fillWidth: true; height: 42; radius: 10
                        color: root.pageIndex === page ? C.Theme.accentFill : navMouse.containsMouse ? C.Theme.raised : "transparent"
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 12; spacing: 12
                            Text { text: nav.modelData.icon; font.family: "Consolas"; font.pixelSize: 18; color: root.pageIndex === nav.page ? C.Theme.accent : C.Theme.muted; Layout.preferredWidth: 24 }
                            Column {
                                spacing: 3
                                Text { text: nav.modelData.label; color: root.pageIndex === nav.page ? C.Theme.accent : C.Theme.muted; font.pixelSize: 13; font.weight: root.pageIndex === nav.page ? Font.DemiBold : Font.Normal }
                                Text { text: nav.modelData.sub; color: C.Theme.subtle; font.family: "Consolas"; font.pixelSize: 8; font.letterSpacing: 1 }
                            }
                            Item { Layout.fillWidth: true }
                            Rectangle { visible: (nav.page === 2 && root.updater && root.updater.updateAvailable) || root.pageIndex === nav.page; width: 3; height: 18; radius: 1; color: nav.page === 2 && root.updater && root.updater.updateAvailable ? C.Theme.red : C.Theme.accent; Layout.rightMargin: 8 }
                        }
                        MouseArea { id: navMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: {root.pageIndex = nav.page;navigation.close()} }
                    }
                }
                Item { Layout.fillHeight: true }
                Rectangle {
                    visible:root.height>650;Layout.fillWidth: true; height: 108; radius: 12; color: C.Theme.consoleBg; border.color: C.Theme.line
                    Column {
                        anchors.fill: parent; anchors.margins: 14; spacing: 10
                        Text { text: "当前会话"; color: C.Theme.subtle; font.pixelSize: 10 }
                        Row {
                            spacing: 7
                            Rectangle { width: 6; height: 6; radius: 3; y: 5; color: root.activeBackend.connected ? C.Theme.accent : C.Theme.subtle }
                            Text { width: 115; elide: Text.ElideRight; text: root.activeBackend.connected ? root.activeBackend.portName : "设备未连接"; color: C.Theme.text; font.pixelSize: 12 }
                        }
                        Text { text: root.activeBackend.connected ? "通信来源已就绪" : "准备好后，连接你的开发板"; color: C.Theme.subtle; font.pixelSize: 10 }
                    }
                }
                C.ActionButton {
                    objectName: "drawerThemeButton"
                    Layout.fillWidth: true; Layout.topMargin: 10
                    text: C.Theme.dark ? "☼   浅色外观" : "☾   深色外观"
                    onClicked: { root.lightMode=C.Theme.dark; C.Theme.dark=!root.lightMode; root.backend.savePreference("dark", C.Theme.dark) }
                }
                Text { text: "v" + root.appVersion + " / WINDOWS"; color: C.Theme.subtle; font.family: "Consolas"; font.pixelSize: 9; Layout.topMargin: 12; Layout.bottomMargin: 4; Layout.leftMargin: 5 }
            }
        }
    }
        ColumnLayout {
            anchors.fill:parent;anchors.margins:10;spacing:8
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 34;spacing:8
                C.ActionButton {objectName:"navigationButton";text:"☰";implicitWidth:36;implicitHeight:32;onClicked:navigation.open();ToolTip.visible:hovered;ToolTip.text:"切换页面"}
                Column {
                    spacing: 2
                    Text { text: ["串口工作台","协议解析","设置与扩展","网络调试","开发板终端","文件传输","","HTTP / MQTT"][root.pageIndex]; color: C.Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Text {visible:root.detailed && root.height>650;text:"Serial Studio · v"+root.appVersion;color:C.Theme.subtle;font.pixelSize:10}
                }
                Item { Layout.fillWidth: true }
                C.ActionButton {objectName:"themeButton";text:C.Theme.dark?"☼":"☾";implicitWidth:32;implicitHeight:30;onClicked:{root.lightMode=C.Theme.dark;root.backend.savePreference("dark",!root.lightMode)}}
                C.ActionButton {objectName:"displayStyleButton";text:root.detailed?"详细显示":"简约显示";implicitHeight:32;onClicked:root.detailed=!root.detailed}
                C.SelectBox {
                    visible: root.pageIndex === 1; objectName: "parserSource"; implicitWidth: 130
                    model: ["串口来源", "网络来源"]; currentIndex: root.parserSource
                    onActivated: root.parserSource = currentIndex
                }
                Rectangle {
                    width: 108; height: 30; radius: 15; color: root.activeBackend.connected ? C.Theme.accentFill : C.Theme.raised; border.color: C.Theme.line
                    Row {
                        anchors.centerIn: parent; spacing: 7
                        Rectangle { width: 6; height: 6; radius: 3; y: 4; color: root.activeBackend.connected ? C.Theme.accent : C.Theme.subtle }
                        Text { text: root.activeBackend.connected ? "通信已就绪" : "未连接设备"; color: root.activeBackend.connected ? C.Theme.accent : C.Theme.muted; font.pixelSize: 11 }
                    }
                }
            }
            Rectangle {
                visible: root.detailed && root.pageIndex === 0 && root.height >= 880
                Layout.fillWidth: true; Layout.preferredHeight: 100; radius: 14
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: C.Theme.dark ? "#173a3d" : "#dbf3ed" }
                    GradientStop { position: 1; color: C.Theme.dark ? "#19283c" : "#e8eefb" }
                }
                border.color: C.Theme.dark ? "#294747" : "#cce6df"
                RowLayout {
                    anchors.fill: parent; anchors.margins: 21; spacing: 20
                    ColumnLayout {
                        spacing: 7
                        Text { text: "从 51 到 RK3588，自由连接。"; color: C.Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                        Text { text: "自定义波特率  /  HEX 与文本  /  可扩展协议解析"; color: C.Theme.muted; font.pixelSize: 11 }
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: "01 03\nC5 CD"; color: C.Theme.dark ? "#3e6f78" : "#a2c6c4"; font.family: "Consolas"; font.pixelSize: 23; font.letterSpacing: 4; lineHeight: 1.3; Layout.rightMargin: 6 }
                }
            }
            RowLayout {
                visible: root.detailed && root.height>=650 && (root.pageIndex === 0 || root.pageIndex === 3 || (root.pageIndex===1 && !protocolPage.showData))
                Layout.fillWidth: true; spacing: 12
                C.Metric { Layout.fillWidth: true; caption: "接收数据"; value: root.formatBytes(root.activeBackend.rxBytes); unit: "B" }
                C.Metric { Layout.fillWidth: true; caption: "发送数据"; value: root.formatBytes(root.activeBackend.txBytes); unit: "B"; tint: C.Theme.blue }
                C.Metric { Layout.fillWidth: true; caption: "接收速率"; value: root.formatBytes(root.activeBackend.rxRate); unit: "B/s"; tint: C.Theme.orange }
                C.Metric { Layout.fillWidth: true; caption: "解析 / 错误"; value: root.activeBackend.frameCount + " / " + root.activeBackend.errorCount; unit: "帧 / 次"; tint: C.Theme.red }
            }
            StackLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: root.pageIndex
                Pages.TerminalPage { id:serialPage;backend: root.backend; settings:root.settings;quickCommands:root.quickCommands;discovery:root.discovery;detailed:root.detailed;onSaveRequested: saveDialog.open() }
                Pages.ProtocolPage { id: protocolPage; backend: root.activeBackend;settings:root.settings;detailed:root.detailed;scope:root.parserSource===1?"network":"serial" }
                Pages.SettingsPage { backend: root.backend; updater: root.updater;settings:root.settings;detailed:root.detailed; appVersion: root.appVersion;onThemeToggleRequested:{root.lightMode=C.Theme.dark;C.Theme.dark=!root.lightMode;root.backend.savePreference("dark",C.Theme.dark)} }
                Pages.TerminalPage {id:networkPage; backend: root.networkBackend; settings:root.settings;quickCommands:root.quickCommands;discovery:root.discovery;detailed:root.detailed;networkMode: true; onSaveRequested: saveDialog.open() }
                Pages.BoardPage {id:boardPage; controller: root.board;discovery:root.discovery;settings:root.settings;quickCommands:root.quickCommands;detailed:root.detailed; serialBackend: root.backend; networkBackend: root.networkBackend }
                Pages.TransferPage { id:transferPage;controller: root.transfer;files:root.files;settings:root.settings;detailed:root.detailed }
                Item {}
                Pages.InternetPage {web:root.web;mqtt:root.mqtt;settings:root.settings;quickCommands:root.quickCommands;detailed:root.detailed}
            }
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 14
                Text { text: root.settings && root.settings.error ? root.settings.error : root.activeBackend.lastError ? root.activeBackend.lastError : root.activeBackend.status; color: root.settings && root.settings.error || root.activeBackend.lastError ? C.Theme.red : C.Theme.subtle; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: "UTF-8  ·  ASYNC I/O"; color: C.Theme.subtle; font.family: "Consolas"; font.pixelSize: 9 }
                C.ActionButton { text: "重置统计"; implicitHeight: 24; implicitWidth: 68; font.pixelSize: 10; onClicked: root.activeBackend.resetStats() }
            }
        }
    Rectangle {
        visible: toastTimer.running
        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 42
        width: Math.min(root.width-80, toastLabel.implicitWidth+36); height: toastLabel.implicitHeight+24
        radius: 10; color: C.Theme.raised; border.color: C.Theme.accent
        Text { id: toastLabel; anchors.centerIn: parent; width: Math.min(implicitWidth, root.width-120); wrapMode: Text.Wrap; color: C.Theme.text; font.pixelSize: 12 }
        Timer { id: toastTimer; interval: 3500 }
    }
}
