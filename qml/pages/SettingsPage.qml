import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components" as C

ScrollView {
    id: root
    required property var backend
    property var updater: null
    property string appVersion: ""
    property var settings: null
    property bool detailed: false
    signal themeToggleRequested()
    clip: true
    contentWidth: availableWidth
    ColumnLayout {
        width: root.availableWidth; spacing: 16
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
