import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components" as C

ScrollView {
    id: root
    required property var backend
    property var updater: null
    property string appVersion: ""
    property var settings: null
    property var systemProxy: null
    property var toolManager: null
    property bool toolAdvanced: false
    property string selectedTool: ""
    property var pendingTool: ({})
    property bool detailed: false
    signal themeToggleRequested()
    clip: true
    contentWidth: availableWidth
    FileDialog {
        id: toolPicker; title: "选择工具可执行文件"; nameFilters: ["可执行程序 (*.exe)"]
        onAccepted: if(root.toolManager)root.toolManager.setPath(root.selectedTool,selectedFile.toString())
    }
    Dialog {
        id: toolConfirm; title: "下载工具 · " + (root.pendingTool.name || "")
        anchors.centerIn: Overlay.overlay
        width: Math.min(480,root.width-32); modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Text {
            wrapMode: Text.Wrap; color: C.Theme.text
            text: (root.pendingTool.version || "") + "\n下载约 " + ((root.pendingTool.bytes || 0)/1048576).toFixed(1) + " MiB；解压需额外空间。\n" + (root.pendingTool.license || "") + "\n来源：" + (root.pendingTool.url || "") + "\n确认后按系统代理下载并校验，不安装系统驱动。"
        }
        onAccepted: if(root.toolManager)root.toolManager.install(root.pendingTool.id)
    }
    ColumnLayout {
        width: root.availableWidth; spacing: 16
        C.Panel {
            Layout.fillWidth: true; implicitHeight: toolColumn.implicitHeight+32
            ColumnLayout {
                id: toolColumn
                anchors {left:parent.left;right:parent.right;top:parent.top;margins:16}
                spacing: 10
                RowLayout {
                    Layout.fillWidth:true
                    Text {Layout.fillWidth:true;text:"开发工具";color:C.Theme.text;font.pixelSize:18}
                    C.ActionButton {text:root.toolAdvanced?"收起高级路径":"高级路径";onClicked:root.toolAdvanced=!root.toolAdvanced}
                    C.ActionButton {text:"刷新";onClicked:if(root.toolManager)root.toolManager.refresh()}
                }
                Text {Layout.fillWidth:true;wrapMode:Text.Wrap;color:C.Theme.muted;text:root.toolManager?root.toolManager.status:"工具管理未启用"}
                Text {Layout.fillWidth:true;wrapMode:Text.Wrap;color:C.Theme.subtle;font.pixelSize:11;text:"自选路径 → 受管工具 → 系统 PATH；不修改系统环境。目录："+(root.toolManager?root.toolManager.directory:"")}
                ProgressBar {Layout.fillWidth:true;visible:root.toolManager && root.toolManager.busy;value:root.toolManager?root.toolManager.progress:0}
                C.ActionButton {text:"取消下载/解压";visible:root.toolManager && root.toolManager.busy;onClicked:root.toolManager.cancel()}
                Repeater {
                    model:root.toolManager?root.toolManager.components:[]
                    delegate: ColumnLayout {
                        id: toolRow
                        required property var modelData
                        Layout.fillWidth:true;spacing:6
                        RowLayout {
                            Layout.fillWidth:true
                            Text {Layout.fillWidth:true;wrapMode:Text.Wrap;color:C.Theme.text;text:toolRow.modelData.name+" · "+(toolRow.modelData.available?"可用":"未找到")+(toolRow.modelData.version?"\n提供版本："+toolRow.modelData.version:"")}
                            C.ActionButton {text:toolRow.modelData.installed?"已部署":toolRow.modelData.required?"下载补齐":"下载";visible:!!toolRow.modelData.url;enabled:root.toolManager && !root.toolManager.busy && !toolRow.modelData.installed;onClicked:{root.pendingTool=toolRow.modelData;toolConfirm.open()}}
                            C.ActionButton {objectName:"toolOfficialButton";text:"官方说明";onClicked:root.toolManager.openOfficial(toolRow.modelData.id)}
                        }
                        Repeater {
                            model:toolRow.modelData.paths
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth:true;visible:root.toolAdvanced
                                Text {Layout.fillWidth:true;wrapMode:Text.Wrap;color:C.Theme.subtle;font.pixelSize:11;text:modelData.name+"："+(modelData.resolved || "不可用，请选择路径或安装工具")}
                                RowLayout {
                                    Layout.fillWidth:true
                                    C.InputBox {Layout.fillWidth:true;implicitWidth:100;text:modelData.custom;placeholderText:"留空自动查找";onEditingFinished:root.toolManager.setPath(modelData.name,text)}
                                    C.ActionButton {text:"选择";enabled:!root.toolManager.busy;onClicked:{root.selectedTool=modelData.name;toolPicker.open()}}
                                    C.ActionButton {text:"自动";enabled:!root.toolManager.busy;onClicked:root.toolManager.setPath(modelData.name,"")}
                                }
                            }
                        }
                        Rectangle {Layout.fillWidth:true;height:1;color:C.Theme.line}
                    }
                }
            }
        }
        C.Panel {
            Layout.fillWidth: true; implicitHeight: proxyColumn.implicitHeight+32
            ColumnLayout {
                id: proxyColumn
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                Text { text: "网络 · 自动跟随系统"; color: C.Theme.text; font.pixelSize: 18 }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: C.Theme.muted; text: root.systemProxy ? root.systemProxy.status : "代理服务未启用" }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: C.Theme.subtle; font.pixelSize: 12; text: "更新、工具下载、HTTP 和 MQTT 使用系统代理规则；失败时不擅自改为直连。SSH/SFTP、网络 ADB 使用系统路由和各自连接设置，不自动使用 HTTP 代理。USB ADB 为本机连接。代理变更对新请求/重连生效。" }
                C.ActionButton { text: "刷新代理状态"; onClicked: if(root.systemProxy) root.systemProxy.refresh() }
            }
        }
        C.Panel {
            Layout.fillWidth: true; implicitHeight: updateColumn.implicitHeight + 40
            ColumnLayout {
                id: updateColumn
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 20; spacing: 12
                Text { text: "版本与安装管理  ·  " + root.appVersion; color: C.Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.updater ? root.updater.status : "更新组件未启用"; color: root.updater && root.updater.updateAvailable ? C.Theme.accent : C.Theme.muted; font.pixelSize: 12 }
                ProgressBar { Layout.fillWidth: true; visible: root.updater && (root.updater.busy || root.updater.downloadReady); value: root.updater ? root.updater.progress : 0 }
                Flow {
                    Layout.fillWidth: true; spacing: 8
                    C.ActionButton { objectName: "checkUpdates"; text: "检查更新"; enabled: root.updater && !root.updater.busy; onClicked: root.updater.check(true) }
                    C.ActionButton { objectName: "downloadUpdate"; text: "下载安装包"; enabled: root.updater && root.updater.canDownload && !root.updater.busy && !root.updater.downloadReady; onClicked: root.updater.download() }
                    C.ActionButton { objectName: "installUpdate"; text: "退出并安装"; enabled: root.updater && root.updater.downloadReady && !root.updater.busy; onClicked: root.updater.install() }
                    C.ActionButton { text: "忽略此版本"; enabled: root.updater && root.updater.updateAvailable && !root.updater.busy; onClicked: root.updater.ignore() }
                    C.ActionButton { text: "打开 Release"; enabled: root.updater !== null; onClicked: root.updater.openRelease() }
                    C.ActionButton { objectName: "uninstallApp"; text: "卸载应用…"; enabled: root.updater && root.updater.uninstallAvailable && !root.updater.busy; onClicked: root.updater.uninstall() }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "启动后后台检查，不自动安装。安装时请保持原安装目录，新版将完整替换旧程序文件。\n卸载按钮打开系统卸载向导；便携运行时不可用。测试版接收测试和正式更新，正式版只接收正式更新。"; color: C.Theme.subtle; font.pixelSize: 11 }
            }
        }
        C.Panel {
            Layout.fillWidth: true; implicitHeight: 120
            RowLayout {
                anchors.fill: parent; anchors.margins: 24; spacing: 18
                ColumnLayout {
                    Text { text: "外观与偏好"; color: C.Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Text { text: "表单、草稿、快捷内容和显示偏好自动保存；启动时保持断开。"; color: C.Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
                Item { Layout.fillWidth: true }
                C.ActionButton { text: C.Theme.dark ? "切换浅色主题" : "切换深色主题"; onClicked: root.themeToggleRequested() }
            }
        }
        C.Panel {
            visible:root.detailed;Layout.fillWidth: true; implicitHeight: 270
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 24; spacing: 14
                Text { text: "扩展与协议接入"; color: C.Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.7
                    text: "支持串口、TCP / UDP，开发板默认 SSH Linux 终端；I²C、SPI、USB 可选来源。\n终端扩展本机 PowerShell / CMD、Android ADB Shell，指令辅助只填入不回车。\n分帧后可配置自定义字段，输出文字和图表，载入完整图片、视频和音频文件。\n文件传输支持 Windows 可见的 USB 磁盘复制和独立 SFTP 上传/下载。\n桥接沿用文本 / JSON 或自定义帧头尾，板端参考随运行包提供。"
                    color: C.Theme.muted; font.pixelSize: 13
                }
                Text { text: "首版采用源码扩展，暂不支持动态加载外部插件。"; color: C.Theme.subtle; font.pixelSize: 11 }
                Item { Layout.fillHeight: true }
            }
        }
        C.Panel {
            Layout.fillWidth: true; implicitHeight: 230
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 24; spacing: 14
                Text { text: "运行与数据说明"; color: C.Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.8
                    text: "• 串口与网络在独立工作线程运行；Qt 交互终端通过 ConPTY / VT 屏幕运行。\n• 终端保留 1000 条、报文保留 500 条；单条字节显示最多 4096 字节。\n• 快捷内容不设条数限制；日志与终端滚动历史有内存上限。\n• 51、STM32、RK3588 的实际波特率由设备与驱动决定。\n• 配置目录："+(root.settings?root.settings.directory:"未启用")
                    color: C.Theme.muted; font.pixelSize: 12
                }
                Item { Layout.fillHeight: true }
            }
        }
        Text { text: "SERIAL STUDIO  /  " + root.appVersion + "    ·    Qt Quick + C++    ·    界面风格参考 EvolveUI"; color: C.Theme.subtle; font.pixelSize: 11; Layout.topMargin: 6 }
    }
}
