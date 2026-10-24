import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components" as C

Item {
    id: root
    required property var files
    property var settings: null
    property var transfer: null
    property bool updating: false
    property bool awaitingBrowse: false
    property bool invalidateAfterBusy: false
    property bool advanced: false
    property var history: []
    property var selected: null
    property var pendingDelete: null
    property string deleteTarget: ""
    property string sortColumn: "name"
    property bool sortDescending: false
    function fileType(entry) {
        if(entry.link)return "符号链接"
        if(entry.directory)return "文件夹"
        const dot=entry.name.lastIndexOf(".")
        return dot>0 ? entry.name.slice(dot+1).toUpperCase()+" 文件" : "文件"
    }
    function fileSize(entry) {
        if(entry.directory || entry.size===undefined)return "—"
        const bytes=Number(entry.size)
        return bytes<1024 ? bytes+" B" : bytes<1048576 ? (bytes/1024).toFixed(1)+" KB" : (bytes/1048576).toFixed(1)+" MB"
    }
    function sortBy(column) {sortDescending=sortColumn===column ? !sortDescending : false;sortColumn=column}
    readonly property var sortedEntries: {
        const column=sortColumn, direction=sortDescending?-1:1
        return (files ? files.entries.slice() : []).sort(function(a,b) {
            if(!!a.directory!==!!b.directory)return a.directory?-1:1
            const x=column==="size"?Number(a.size || 0):column==="type"?fileType(a):a.name.toLowerCase()
            const y=column==="size"?Number(b.size || 0):column==="type"?fileType(b):b.name.toLowerCase()
            return (x<y?-1:x>y?1:0)*direction
        })
    }
    readonly property bool fileSelected: !!selected && !selected.directory && !selected.link
    function requestDelete() {
        if(!selected || busy)return
        pendingDelete=selected
        deleteTarget=kind+"|"+files.path+"|"+JSON.stringify(config())
        deleteDialog.open()
    }
    readonly property bool smb: transport.currentIndex===4
    readonly property string kind: ["adb","mtp","sftp","local","local"][transport.currentIndex] || "adb"
    property string pathError: ""
    function uncPath(value) {
        const path=value.replace(/\\/g,"/").replace(/\/+$/,"")
        if(!/^\/\/[^/]+\/[^/]+(?:\/.*)?$/.test(path))return ""
        const parts=path.slice(2).split("/")
        for(const part of parts)if(!part || part==="." || part===".." || /[\x00-\x1f:*?"<>|]/.test(part))return ""
        return path
    }
    function sftpDefaults() {
        if(kind!=="sftp" || !remotePath || (form && form.restoring))return
        updating=true
        remotePath.text=["/","/sdcard","/C:/Users","/"][sftpSystem.currentIndex] || "/"
        user.text=["root","shell","",""][sftpSystem.currentIndex] || ""
        updating=false;invalidate()
    }
    readonly property bool busy: !!files && files.busy
    readonly property var adb: files ? files.adb : null
    readonly property string targetSerial: adb ? adb.selectedSerial : ""
    onTargetSerialChanged: {
        if(form && form.ready && !form.restoring)selectedPort=targetSerial
        if(kind==="adb")invalidate()
    }
    function config() {return {adb:adbTool.text,serial:adb?adb.selectedSerial:"",host:host.text,user:user.text,port:Number(port.text),key:identity.text}}
    function invalidate() {
        if(updating)return
        pathError=""
        if(deleteDialog && deleteDialog.visible)deleteDialog.reject()
        selected=null;history=[];if(writePermission)writePermission.checked=false
        if(busy){invalidateAfterBusy=true;return}
        invalidateAfterBusy=false
        if(files)files.reset()
    }
    function open(path,push) {
        if(!files || busy)return false
        pathError=""
        if(smb) {
            path=uncPath(path)
            if(!path){pathError="请输入有效的UNC共享目录，例如 \\\\主机\\共享；不支持相对路径或上级跳转。";return false}
        }
        const previous=files.path
        selected=null
        writePermission.checked=false
        if(files.browse(kind,path,config())) {
            if(push)history=history.concat([previous])
            awaitingBrowse=true;return true
        }
        return false
    }
    function goParent() {
        if(smb) {
            const current=uncPath(remotePath.text)
            if(!current)return
            const parts=current.slice(2).split("/")
            if(parts.length<=2)return
            parts.pop();open("//"+parts.join("/"),false);return
        }
        if(history.length){const values=history.slice();const path=values.pop();history=values;open(path,false);return}
        if(kind==="mtp"){open("",false);return}
        const path=remotePath.text.replace(/\\/g,"/").replace(/\/+$/,"")
        const cut=path.lastIndexOf("/")
        open(cut<=0 ? (kind==="local"?"":"/") : path.slice(0,cut+1),false)
    }
    onKindChanged: {
        updating=true
        if(remotePath)remotePath.text=kind==="adb"?"/sdcard":kind==="sftp"?(["/","/sdcard","/C:/Users","/"][sftpSystem.currentIndex] || "/"):""
        updating=false;invalidate()
    }
    onSmbChanged: {
        if(!remotePath || (transport.currentIndex!==3 && transport.currentIndex!==4))return
        updating=true;remotePath.text="";updating=false;invalidate()
    }
    Binding {target:root.adb;property:"enabled";value:root.visible && root.kind==="adb";when:root.adb!==null}
    Connections {
        target:root.files
        function onChanged() {
            if(!root.files.busy && root.invalidateAfterBusy){root.awaitingBrowse=false;root.invalidate();return}
            if(!root.files.busy && root.awaitingBrowse) {
                root.awaitingBrowse=false
                if(root.files.success){root.updating=true;remotePath.text=root.files.path;root.updating=false}
            }
        }
    }
    C.PersistentForm {
        id:form;settings:root.settings;scope:"fileBrowser"
        fields:[{key:"transport",object:transport,property:"currentIndex"},{key:"sftpSystem",object:sftpSystem,property:"currentIndex"},{key:"path",object:remotePath,property:"text"},
            {key:"host",object:host,property:"text"},{key:"user",object:user,property:"text"},{key:"port",object:port,property:"text"},
            {key:"identity",object:identity,property:"text"},{key:"adbTool",object:adbTool,property:"text"},
            {key:"serial",object:root,property:"selectedPort"},{key:"source",object:localFile,property:"text"},
            {key:"saveDirectory",object:saveDirectory,property:"text"},{key:"advanced",object:root,property:"advanced"}]
        onRestored:{if(root.adb){root.adb.executablePath=adbTool.text;if(root.selectedPort)root.adb.selectedSerial=root.selectedPort;else root.selectedPort=root.adb.selectedSerial}writePermission.checked=false}
    }
    // 使用PersistentForm允许的选择字段，内容为ADB序列号或网络端点。
    property string selectedPort: ""
    Dialog {
        id:deleteDialog;objectName:"browserDeleteDialog";modal:true;title:"确认永久删除"
        x:Math.max(0,(root.width-width)/2);y:Math.max(0,(root.height-height)/2);width:Math.min(460,root.width-24)
        closePolicy:Popup.CloseOnEscape
        contentItem:ColumnLayout {
            spacing:16
            Label {Layout.fillWidth:true;wrapMode:Text.Wrap;textFormat:Text.PlainText;text:root.pendingDelete?"永久删除“"+root.pendingDelete.name+"”？"+(root.pendingDelete.directory?"目录中的文件和子目录也将删除。":"")+"\n此操作不进入回收站，不能撤回。":""}
            RowLayout {
                Layout.alignment:Qt.AlignRight
                C.ActionButton {text:"取消";onClicked:deleteDialog.reject()}
                C.ActionButton {objectName:"browserConfirmDelete";text:"永久删除";danger:true;onClicked:deleteDialog.accept()}
            }
        }
        onAccepted: {
            if(root.pendingDelete && root.files && root.deleteTarget===root.kind+"|"+root.files.path+"|"+JSON.stringify(root.config()))
                root.files.removeEntry(root.pendingDelete.id,true)
            root.selected=null;root.pendingDelete=null
        }
        onRejected:root.pendingDelete=null
    }
    FileDialog {id:sourceDialog;title:"选择要上传的文件";onAccepted:localFile.text=root.transfer.localPath(selectedFile)}
    FolderDialog {id:saveDialog;title:"选择下载保存目录";onAccepted:saveDirectory.text=root.transfer.localPath(selectedFolder)}
    RowLayout {
        anchors.fill:parent;spacing:14
        C.Panel {
            Layout.preferredWidth:310;Layout.fillHeight:true
            ScrollView {
                objectName:"browserSettingsScroll"
                anchors.fill:parent;anchors.margins:14;contentWidth:availableWidth;clip:true
                ColumnLayout {
                    width:parent.width;spacing:9
                    Text {text:"设备文件管理";color:C.Theme.text;font.pixelSize:16;font.weight:Font.DemiBold}
                    C.SelectBox {id:transport;objectName:"fileTransport";Layout.fillWidth:true;enabled:!root.busy;model:["USB / 网络 · ADB","USB · MTP 便携设备","网络 · SFTP（各系统）","本地 / USB磁盘","网络 · SMB Windows共享"]}
                    Text {Layout.fillWidth:true;wrapMode:Text.Wrap;font.pixelSize:11;color:C.Theme.muted;text:root.kind==="adb"?"USB需调试授权；网络ADB需设备已开放服务。仅在点击连接后接入，只访问有权限的目录。":root.kind==="mtp"?"无需盘符；USB选择“文件传输”。点击打开目录列出Windows可见的便携设备。":root.kind==="sftp"?"目标需启用SSH/SFTP，预先核对主机指纹并配置密钥或ssh-agent；此页不支持交互密码认证。":root.smb?"输入 \\\\主机\\共享目录；使用Windows当前已授权凭据，不收集密码。若拒绝访问，请先在Windows中授权该共享。":"输入本地或USB磁盘路径；留空可列出磁盘。原有UNC路径仍兼容。"}
                    C.SelectBox {
                        objectName:"fileAdbDevices";visible:root.kind==="adb";Layout.fillWidth:true;enabled:!root.busy
                        model:root.adb?root.adb.devices:[];textRole:"label"
                        currentIndex:{for(let i=0;i<count;i++)if(model[i].serial===root.adb.selectedSerial)return i;return -1}
                        displayText:currentIndex>=0?currentText:"等待已授权的USB / 网络ADB设备"
                        onActivated:function(index){root.adb.selectedSerial=model[index].serial;root.selectedPort=model[index].serial;root.invalidate()}
                        popup.onAboutToShow:if(root.adb)root.adb.refresh()
                    }
                    Text {visible:root.kind==="adb";Layout.fillWidth:true;wrapMode:Text.Wrap;text:root.adb?root.adb.status:"ADB未加载";font.pixelSize:10;color:C.Theme.muted}
                    C.AdbNetwork {visible:root.kind==="adb";Layout.fillWidth:true;adb:root.adb;settings:root.settings;scope:"fileAdbNetwork";editable:!root.busy}
                    C.SelectBox {id:sftpSystem;objectName:"fileSftpSystem";visible:root.kind==="sftp";Layout.fillWidth:true;enabled:!root.busy;model:["Linux","Android（SSH服务）","Windows（OpenSSH）","通用 SFTP"];onCurrentIndexChanged:root.sftpDefaults()}
                    Text {visible:root.kind==="sftp";Layout.fillWidth:true;wrapMode:Text.Wrap;font.pixelSize:10;color:C.Theme.muted;text:sftpSystem.currentIndex===2?"Windows OpenSSH目录示例 /C:/Users；填写远端Windows账户。受服务端根目录/权限限制，请按实际路径修改。":sftpSystem.currentIndex===1?"Android需额外启用SSH/SFTP服务；账户及目录以该服务为准，USB调试不提供SFTP。":"填写SSH服务端账户及绝对目录；系统选项只提供默认提示，不会自动访问。"}
                    C.InputBox {id:host;objectName:"fileHost";visible:root.kind==="sftp";Layout.fillWidth:true;placeholderText:"SSH / SFTP 主机或IP";enabled:!root.busy;onTextChanged:root.invalidate()}
                    RowLayout {
                        visible:root.kind==="sftp";Layout.fillWidth:true
                        C.InputBox {id:user;objectName:"fileUser";Layout.fillWidth:true;text:"root";placeholderText:sftpSystem.currentIndex===2?"远端Windows账户":sftpSystem.currentIndex===1?"SSH服务账户":"SSH用户名";enabled:!root.busy;onTextChanged:root.invalidate()}
                        C.InputBox {id:port;Layout.preferredWidth:70;text:"22";validator:IntValidator {bottom:1;top:65535} enabled:!root.busy;onTextChanged:root.invalidate()}
                    }
                    C.ActionButton {text:root.advanced?"收起高级":"高级 · 工具和私钥";Layout.fillWidth:true;onClicked:root.advanced=!root.advanced}
                    C.InputBox {id:identity;visible:root.advanced && root.kind==="sftp";Layout.fillWidth:true;placeholderText:"SSH私钥路径（可选）";enabled:!root.busy;onTextChanged:root.invalidate()}
                    C.InputBox {id:adbTool;visible:root.advanced && root.kind==="adb";Layout.fillWidth:true;placeholderText:"adb.exe路径（留空自动寻找）";enabled:!root.busy;onEditingFinished:{if(root.adb)root.adb.executablePath=text;root.invalidate()}}
                    Rectangle {Layout.fillWidth:true;height:1;color:C.Theme.line}
                    Text {text:"上传到当前目录";color:C.Theme.text;font.pixelSize:13}
                    C.InputBox {id:localFile;objectName:"browserUploadFile";Layout.fillWidth:true;placeholderText:"本地来源文件";enabled:!root.busy;onTextChanged:writePermission.checked=false}
                    C.ActionButton {text:"选择本地文件";Layout.fillWidth:true;enabled:!!root.transfer && !root.busy;onClicked:sourceDialog.open()}
                    CheckBox {id:writePermission;objectName:"browserAllowWrite";visible:root.kind==="sftp";text:"允许写入 / 覆盖远端同名文件";palette.windowText:C.Theme.text;enabled:!root.busy}
                    C.ActionButton {objectName:"browserUpload";text:"上传文件";Layout.fillWidth:true;enabled:!!root.files && !root.busy && root.files.path!=="" && localFile.text!=="" && (root.kind!=="sftp" || writePermission.checked);onClicked:root.files.upload(localFile.text,writePermission.checked)}
                    Rectangle {Layout.fillWidth:true;height:1;color:C.Theme.line}
                    Text {text:"下载选中文件";color:C.Theme.text;font.pixelSize:13}
                    C.InputBox {id:saveDirectory;objectName:"browserSaveDirectory";Layout.fillWidth:true;placeholderText:"本地保存目录";enabled:!root.busy}
                    C.ActionButton {text:"选择保存目录";Layout.fillWidth:true;enabled:!!root.transfer && !root.busy;onClicked:saveDialog.open()}
                    C.ActionButton {objectName:"browserDownload";text:"下载选中文件";Layout.fillWidth:true;primary:true;enabled:!!root.files && !root.busy && !!root.selected && !root.selected.directory && !root.selected.link && saveDirectory.text!=="";onClicked:root.files.download(root.selected.id,saveDirectory.text)}
                    Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:"本地、ADB、MTP拒绝同名覆盖；SFTP上传需明确允许写入。仅传单文件，不自动递归。断线/取消可能留下部分文件。";color:C.Theme.subtle;font.pixelSize:10}
                }
            }
        }
        C.Panel {
            Layout.fillWidth:true;Layout.fillHeight:true
            ColumnLayout {
                anchors.fill:parent;anchors.margins:14;spacing:9
                RowLayout {
                    Layout.fillWidth:true
                    C.ActionButton {objectName:"browserParent";text:"上级";implicitWidth:52;enabled:!root.busy;onClicked:root.goParent()}
                    C.InputBox {id:remotePath;objectName:"browserPath";Layout.fillWidth:true;text:"/sdcard";placeholderText:root.smb?"\\\\主机\\共享\\目录":root.kind==="mtp"?"留空列出便携设备；也可粘贴Shell目录ID":"目录路径（共享目录可填UNC）";enabled:!root.busy;onTextChanged:root.invalidate();onAccepted:root.open(text,false)}
                    C.ActionButton {objectName:"browserOpen";text:root.busy?"读取中…":"打开目录";primary:true;implicitWidth:86;enabled:!!root.files && !root.busy;onClicked:root.open(remotePath.text,false)}
                }
                RowLayout {
                    Layout.fillWidth:true;spacing:6
                    C.ActionButton {objectName:"browserCopy";text:"复制";implicitWidth:64;enabled:!root.busy && root.fileSelected;onClicked:root.files.copyEntry(root.selected.id,false)}
                    C.ActionButton {objectName:"browserCut";text:"剪切";implicitWidth:64;enabled:!root.busy && root.fileSelected && root.selected.deletable!==false;onClicked:root.files.copyEntry(root.selected.id,true)}
                    C.ActionButton {objectName:"browserPaste";text:"粘贴";implicitWidth:64;enabled:!!root.files && !root.busy && !!root.files.clipboardName && root.files.path!=="";onClicked:root.files.paste()}
                    Item {Layout.fillWidth:true}
                    C.ActionButton {objectName:"browserDelete";text:"删除…";danger:true;implicitWidth:70;enabled:!!root.files && !root.busy && !!root.selected && root.files.path!=="" && root.selected.deletable!==false;onClicked:root.requestDelete()}
                }
                RowLayout {
                    Layout.fillWidth:true;visible:!!root.files && !!root.files.clipboardName
                    Text {Layout.fillWidth:true;elide:Text.ElideMiddle;color:C.Theme.accent;font.pixelSize:11;textFormat:Text.PlainText;text:root.files?(root.files.clipboardCut?"待剪切：":"待复制：")+root.files.clipboardName+" · 打开目标目录后粘贴":""}
                    C.ActionButton {text:"清空";implicitHeight:26;implicitWidth:52;enabled:!root.busy;onClicked:root.files.clearClipboard()}
                }
                Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:root.selected?(root.selected.directory?"目录：":"文件：")+root.selected.name:"单击选择文件，双击进入目录";color:C.Theme.muted;font.pixelSize:11}
                Rectangle {
                    objectName:"browserDetailsHeader";Layout.fillWidth:true;height:30;color:C.Theme.raised
                    RowLayout {
                        anchors.fill:parent;anchors.leftMargin:8;anchors.rightMargin:16;spacing:8
                        Repeater {
                            model:[{key:"name",label:"名称"},{key:"type",label:"类型"},{key:"size",label:"大小"}]
                            Text {
                                required property var modelData
                                Layout.fillWidth:modelData.key==="name";Layout.preferredWidth:modelData.key==="name"?160:modelData.key==="type"?90:76
                                text:modelData.label+(root.sortColumn===modelData.key?(root.sortDescending?" ↓":" ↑"):"")
                                color:C.Theme.muted;font.pixelSize:11
                                MouseArea {anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:root.sortBy(parent.modelData.key)}
                            }
                        }
                    }
                }
                ListView {
                    id:listing;objectName:"browserEntries";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:0
                    Keys.onPressed:function(event) {
                        if(root.busy)return
                        if(event.matches(StandardKey.Copy) && root.fileSelected){root.files.copyEntry(root.selected.id,false);event.accepted=true}
                        else if(event.matches(StandardKey.Cut) && root.fileSelected){root.files.copyEntry(root.selected.id,true);event.accepted=true}
                        else if(event.matches(StandardKey.Paste) && root.files.clipboardName){root.files.paste();event.accepted=true}
                        else if(event.key===Qt.Key_Delete && root.selected){root.requestDelete();event.accepted=true}
                    }
                    model:root.sortedEntries;ScrollBar.vertical:ScrollBar{}
                    delegate:Rectangle {
                        required property var modelData
                        required property int index
                        width:listing.width;height:32;radius:2;color:root.selected && root.selected.id===modelData.id?C.Theme.accentFill:mouse.containsMouse?C.Theme.raised:"transparent"
                        border.color:root.selected && root.selected.id===modelData.id?C.Theme.accent:"transparent"
                        RowLayout {
                            anchors.fill:parent;anchors.leftMargin:8;anchors.rightMargin:16;spacing:8
                            RowLayout {
                                Layout.fillWidth:true;spacing:7
                                Item {
                                    Layout.preferredWidth:18;Layout.preferredHeight:18
                                    Rectangle {x:1;y:2;width:8;height:4;radius:1;color:"#d9a441";visible:modelData.directory}
                                    Rectangle {x:1;y:5;width:modelData.directory?17:13;height:modelData.directory?11:12;radius:1;color:modelData.directory?"#efc365":"#b6cce3";border.color:modelData.directory?"#d9a441":"#7590ab"}
                                }
                                Text {text:modelData.name;Layout.fillWidth:true;elide:Text.ElideMiddle;color:C.Theme.text;font.pixelSize:12;textFormat:Text.PlainText}
                            }
                            Text {text:root.fileType(modelData);Layout.preferredWidth:90;elide:Text.ElideRight;color:C.Theme.muted;font.pixelSize:11}
                            Text {text:root.fileSize(modelData);Layout.preferredWidth:76;horizontalAlignment:Text.AlignRight;color:C.Theme.muted;font.pixelSize:11}
                        }
                        MouseArea {id:mouse;anchors.fill:parent;hoverEnabled:true;onClicked:{listing.forceActiveFocus();root.selected=modelData}onDoubleClicked:if(modelData.directory)root.open(modelData.id,true)}
                    }
                    Text {anchors.centerIn:parent;visible:listing.count===0;text:root.busy?"正在访问设备…":"尚未读取目录，或当前目录为空";color:C.Theme.subtle;font.pixelSize:12}
                }
                ProgressBar {Layout.fillWidth:true;from:0;to:100;value:root.files?root.files.progress:0;indeterminate:root.busy}
                RowLayout {
                    Layout.fillWidth:true
                    TextArea {id:statusText;objectName:"browserStatus";Layout.fillWidth:true;Layout.maximumHeight:85;readOnly:true;selectByMouse:true;wrapMode:TextEdit.WrapAnywhere;text:root.pathError || (root.files?root.files.status:"文件管理未加载");color:root.pathError || (root.files && !root.files.success)?C.Theme.red:C.Theme.muted;font.pixelSize:11;background:null}
                    C.CopyTextButton {editor:statusText}
                    C.ActionButton {text:"取消";visible:root.busy;onClicked:root.files.cancel()}
                }
            }
        }
    }
}
