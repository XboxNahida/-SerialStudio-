import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components" as C

Item {
    id: root
    required property var controller
    function options() {return {host:host.text,user:user.text,port:Number(port.text),key:key.text,remote:remote.text,overwrite:overwrite.checked}}
    FileDialog { id: fileDialog; title: "选择本地文件"; onAccepted: file.text=root.controller.localPath(selectedFile) }
    FileDialog { id: downloadDialog; title: "下载保存到新文件"; fileMode: FileDialog.SaveFile; onAccepted: file.text=root.controller.localPath(selectedFile) }
    FolderDialog { id: folderDialog; title: "选择开发板磁盘或本地目标目录"; onAccepted: folder.text=root.controller.localPath(selectedFolder) }
    RowLayout {
        anchors.fill: parent; spacing: 16
        C.Panel {
            Layout.preferredWidth: 360; Layout.fillHeight: true
            ScrollView {
                anchors.fill: parent; anchors.margins: 16; contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: parent.width; spacing: 9
                    Text { text: "文件与通信方式"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                    C.SelectBox { id: mode; objectName: "transferMode"; Layout.fillWidth: true; model: ["USB / 本地磁盘复制","网络 · SFTP 上传","网络 · SFTP 下载"]; enabled: !root.controller || !root.controller.busy }
                    Text { text: mode.currentIndex===2 ? "本地保存文件（不可已存在）":"来源文件（任意格式）"; color: C.Theme.muted; font.pixelSize: 11 }
                    RowLayout {
                        C.InputBox { id: file; objectName: "transferFile"; Layout.fillWidth: true; implicitWidth: 150; placeholderText: "完整本地路径"; enabled: mode.enabled }
                        C.ActionButton { text: "选择"; implicitWidth: 54; enabled: root.controller!==null && mode.enabled; onClicked: mode.currentIndex===2 ? downloadDialog.open():fileDialog.open() }
                    }
                    ColumnLayout {
                        visible: mode.currentIndex===0; Layout.fillWidth: true; spacing: 9
                        Text { text: "选择 Windows 可见磁盘"; color: C.Theme.muted; font.pixelSize: 11 }
                        RowLayout {
                            C.SelectBox {
                                id: volume; objectName: "transferVolume"; Layout.fillWidth: true; implicitWidth: 150
                                model: root.controller ? root.controller.volumes:[]; textRole: "label"
                                onActivated: folder.text=model[currentIndex].path
                                enabled: mode.enabled
                            }
                            C.ActionButton { text: "↻"; implicitWidth: 36; enabled: root.controller!==null && mode.enabled; onClicked: root.controller.refreshVolumes() }
                        }
                        RowLayout {
                            C.InputBox { id: folder; objectName: "transferFolder"; Layout.fillWidth: true; implicitWidth: 150; placeholderText: "目标目录，如 E:/"; enabled: mode.enabled }
                            C.ActionButton { text: "目录"; implicitWidth: 54; enabled: root.controller!==null && mode.enabled; onClicked: folderDialog.open() }
                        }
                        Text { Layout.fillWidth: true; text: "磁盘列表包含本机可写卷，请选择开发板实际盘符。反向复制时，把开发板文件选作来源、电脑目录选作目标。拒绝同名覆盖，完成后 SHA256 核对。"; color: C.Theme.subtle; font.pixelSize: 10; wrapMode: Text.Wrap }
                    }
                    ColumnLayout {
                        visible: mode.currentIndex>0; Layout.fillWidth: true; spacing: 8
                        Text { text: "SSH / SFTP 主机"; color: C.Theme.muted; font.pixelSize: 11 }
                        C.InputBox { id: host; objectName: "transferHost"; Layout.fillWidth: true; placeholderText: "板端 IP / 主机名"; enabled: mode.enabled }
                        RowLayout {
                            C.InputBox { id: user; text: "root"; Layout.fillWidth: true; implicitWidth: 120; enabled: mode.enabled }
                            C.InputBox { id: port; text: "22"; Layout.preferredWidth: 70; enabled: mode.enabled; validator: IntValidator { bottom: 1; top: 65535 } }
                        }
                        C.InputBox { id: key; Layout.fillWidth: true; placeholderText: "私钥路径（可选）"; enabled: mode.enabled }
                        Text { text: "远端完整文件路径"; color: C.Theme.muted; font.pixelSize: 11 }
                        C.InputBox { id: remote; objectName: "transferRemote"; Layout.fillWidth: true; placeholderText: "/home/user/data.bin"; enabled: mode.enabled }
                        CheckBox { id: overwrite; objectName: "transferOverwrite"; visible: mode.currentIndex===1; text: "允许写入 / 覆盖指定远端文件"; enabled: mode.enabled }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "先在开发板终端核对主机指纹，并配置 SSH 密钥。网络传输独立于 TCP/UDP 调试会话；服务端需启用 SFTP。"; color: C.Theme.subtle; font.pixelSize: 10 }
                    }
                    C.ActionButton {
                        objectName: "transferStart"; text: "开始传输"; primary: true; Layout.fillWidth: true; Layout.topMargin: 8
                        enabled: root.controller!==null && !root.controller.busy && (mode.currentIndex!==1 || overwrite.checked)
                        onClicked: root.controller.start(mode.currentIndex,root.controller.localUrl(file.text),root.controller.localUrl(mode.currentIndex===2 ? file.text:folder.text),root.options())
                    }
                    C.ActionButton { text: "取消传输"; Layout.fillWidth: true; visible: root.controller && root.controller.busy; onClicked: root.controller.cancel() }
                }
            }
        }
        C.Panel {
            Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 14
                Text { text: "传输状态"; color: C.Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                ProgressBar { Layout.fillWidth: true; from: 0; to: 100; value: root.controller ? root.controller.progress:0; indeterminate: root.controller && root.controller.busy && mode.currentIndex>0 }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    TextArea { text: root.controller ? root.controller.status:"文件传输未加载"; color: root.controller && !root.controller.success ? C.Theme.red:C.Theme.text; font.pixelSize: 12; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; textFormat: TextEdit.PlainText; background: Rectangle { color: C.Theme.consoleBg; radius: 8; border.color: C.Theme.line } }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "USB 磁盘需先由 Windows 挂载。复制完成后使用系统安全弹出。SFTP 上传取消可能留下远端 .part 文件；网络结果未另做远端 SHA256 比对。"; color: C.Theme.subtle; font.pixelSize: 11; lineHeight: 1.5 }
            }
        }
    }
}
