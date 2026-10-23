import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import "../components" as C

C.Panel {
    id: root
    required property var workspace
    property var settings: null
    property bool detailed: false
    property string scope: "serial"
    property alias showRules: rulesToggle.checked
    property string lastMedia: ""
    property string restoreError: ""
    property var configuredWorkspaces: []
    property string chartFieldName: ""
    readonly property string defaultSchema: '[\n {"name":"温度","offset":0,"type":"i16","endian":"le","scale":0.1},\n {"name":"电压","offset":2,"type":"u16","endian":"le","scale":0.001}\n]'
    readonly property var formats: ["auto","utf8","json","csv","binary","hex","image","image","image","image","audio","audio","audio","audio","video","video","video"]
    onScopeChanged: {
        if(!form || !format || !schema)return
        form.disconnectFields()
        format.currentIndex=0;schema.text=root.defaultSchema;live.checked=false
        fileFormat.currentIndex=0;output.currentIndex=0;field.currentIndex=0;root.chartFieldName="";rulesToggle.checked=false;root.restoreError=""
        Qt.callLater(function(){form.restore()})
    }
    function applyRules(remember) {
        if(!workspace || typeof workspace.configure!=="function")return false
        const selected=["utf8","json","csv","binary","hex"][format.currentIndex]
        if(!configureWorkspace(selected,schema.text,remember)) {restoreError=workspace.error || "数据规则无效";return false}
        workspace.enabled=live.checked;restoreError=""
        const key="data/"+scope+"/applied"
        if(settings && (remember || settings.value(key,null)===null))settings.setValue(key,{format:selected,schema:schema.text})
        return true
    }
    function configureWorkspace(selected,schemaText,force) {
        const signature=JSON.stringify([selected,schemaText])
        const previous=configuredWorkspaces.find(function(item){return item.target===root.workspace})
        // DataWorkspace.configure 会清空已有图表和媒体；切回相同来源不重复清空。
        if(!force && previous && previous.signature===signature)return true
        if(!workspace.configure(selected,schemaText))return false
        if(previous)previous.signature=signature
        else configuredWorkspaces.push({target:root.workspace,signature:signature})
        return true
    }
    function restoreChartField() {
        if(!workspace || !root.chartFieldName)return
        const index=workspace.fields.indexOf(root.chartFieldName)
        if(index>=0)field.currentIndex=index
    }
    C.PersistentForm {
        id:form;settings:root.settings;scope:"data/"+root.scope
        fields:[{key:"format",object:format,property:"currentIndex"},{key:"schema",object:schema,property:"text"},
            {key:"enabled",object:live,property:"checked"},{key:"fileFormat",object:fileFormat,property:"currentIndex"},
            {key:"output",object:output,property:"currentIndex"},
            {key:"rulesExpanded",object:rulesToggle,property:"checked"}]
        onRestored: {
            root.chartFieldName=root.settings.value(scope+"/fieldName","")
            const applied=root.settings.value(scope+"/applied",null)
            if(applied && root.workspace && typeof root.workspace.configure==="function") {
                if(!root.configureWorkspace(applied.format,applied.schema,false))root.restoreError=root.workspace.error || "已保存数据规则恢复失败"
                root.workspace.enabled=live.checked
            } else root.applyRules(false)
            const folder=root.settings.value(scope+"/openFolder","")
            const saveFolder=root.settings.value(scope+"/saveFolder","")
            if(folder)openDialog.currentFolder=folder
            if(saveFolder)saveDialog.currentFolder=saveFolder
            root.restoreChartField()
        }
    }
    FileDialog {
        id: openDialog; title: "载入数据或媒体文件"
        onCurrentFolderChanged:if(form.ready && !form.restoring && root.settings)root.settings.setValue("data/"+root.scope+"/openFolder",currentFolder.toString())
        nameFilters: ["常用数据与媒体 (*.txt *.json *.csv *.bin *.png *.jpg *.jpeg *.bmp *.gif *.wav *.mp3 *.flac *.ogg *.mp4 *.mkv *.avi)","所有文件 (*)"]
        onAccepted: if(root.workspace)root.workspace.openFile(selectedFile,root.formats[fileFormat.currentIndex])
    }
    FileDialog {
        id: saveDialog; title: "收集有效帧载荷到新文件"; fileMode: FileDialog.SaveFile
        onCurrentFolderChanged:if(form.ready && !form.restoring && root.settings)root.settings.setValue("data/"+root.scope+"/saveFolder",currentFolder.toString())
        nameFilters: ["二进制或媒体文件 (*)"]
        onAccepted: if(root.workspace)root.workspace.beginCapture(selectedFile)
    }
    Connections {
        target: root.workspace
        function onChanged() {
            root.restoreChartField()
            chart.requestPaint()
            if(root.workspace.mediaUrl.toString()!==root.lastMedia) {
                root.lastMedia=root.workspace.mediaUrl.toString()
                if(root.workspace.mediaKind==="image")output.currentIndex=2
                if(root.workspace.mediaKind==="video")output.currentIndex=3
                if(root.workspace.mediaKind==="audio")output.currentIndex=4
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 14; spacing: 9
        RowLayout {
            Text { text: "数据解释与输出"; color: C.Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            C.ActionButton {id:rulesToggle;objectName:"dataRulesToggle";checkable:true;checked:false; text: root.showRules ? "收起规则" : "数据规则"; implicitHeight: 28}
            C.ActionButton { text: "清空"; implicitHeight: 28; enabled: root.workspace!==null; onClicked: root.workspace.clear() }
        }
        RowLayout {
            visible: root.showRules
            C.SelectBox { id: format; objectName: "dataFormat"; model: ["UTF-8 文本","JSON 对象/数组","CSV 单行","自定义二进制字段","HEX 字节"]; implicitWidth: 165 }
            C.ActionButton {
                objectName: "applyDataButton"; text: "应用数据规则"; primary: true; implicitHeight: 36; enabled: root.workspace!==null
                onClicked: if(root.applyRules(true))root.showRules=false
            }
            CheckBox {id:live;objectName:"dataLive"; text: "实时解析"; checked: root.workspace ? root.workspace.enabled:false; enabled: root.workspace!==null; onToggled: root.workspace.enabled=checked;palette.windowText:C.Theme.text}
        }
        Text {visible:!root.showRules;Layout.fillWidth:true;color:C.Theme.muted;font.pixelSize:11;text:format.currentText+" · "+(live.checked?"实时解析":"解析暂停")}
        ScrollView {
            visible: root.showRules && format.currentIndex===3; Layout.fillWidth: true; Layout.preferredHeight: 105
            TextArea {
                id: schema; objectName: "dataSchema"; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                text:root.defaultSchema
                color: C.Theme.text; font.family: "Consolas"; font.pixelSize: 11
                background: Rectangle { color: C.Theme.consoleBg; radius: 6; border.color: C.Theme.line }
            }
        }
        Text {
            visible: root.showRules && root.detailed; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10; color: C.Theme.subtle
            text: "偏移从有效载荷第 0 字节起；自定义分帧去掉头尾。字段支持 u8/i8/u16/i16/u32/i32/f32/f64/utf8/hex，字符串需 length。"
        }
        RowLayout {
            C.SelectBox {
                id: fileFormat; objectName: "fileFormat"; Layout.fillWidth: true; implicitWidth: 150
                model: ["文件：自动识别","TXT · UTF-8","JSON","CSV","BIN · 当前字段规则","HEX · 原始文件","PNG 图片","JPEG 图片","BMP 图片","GIF 图片（首帧）","WAV 音频","MP3 音频","FLAC 音频","OGG 音频","MP4 视频","MKV 视频","AVI 视频"]
            }
            C.ActionButton { text: "载入文件"; implicitHeight: 34; enabled: root.workspace!==null; onClicked: openDialog.open() }
            C.ActionButton { text: root.workspace && root.workspace.capturing ? "完成收集" : "收集载荷"; implicitHeight: 34; enabled: root.workspace!==null; onClicked: root.workspace.capturing ? root.workspace.finishCapture() : saveDialog.open() }
            C.ActionButton { visible: root.workspace && root.workspace.capturing; text: "取消"; implicitHeight: 34; onClicked: root.workspace.cancelCapture() }
        }
        RowLayout {
            C.SelectBox { id: output; objectName: "dataOutput"; implicitWidth: 130; model: ["文字","图表","图片","视频","音乐 / 音频"]; onCurrentIndexChanged: chart.requestPaint() }
            C.CopyTextButton {objectName:"copyDataText";visible:output.currentIndex===0;editor:dataText}
            C.SelectBox {
                id: field; objectName: "chartField"; visible: output.currentIndex===1; Layout.fillWidth: true
                model: root.workspace ? root.workspace.fields:[]; onCurrentTextChanged: chart.requestPaint()
                onActivated: {
                    root.chartFieldName=currentText
                    if(root.settings && form.ready && !form.restoring)root.settings.setValue("data/"+root.scope+"/fieldName",currentText)
                }
            }
            Text {
                Layout.fillWidth: output.currentIndex!==1; elide: Text.ElideRight; font.pixelSize: 10; color: C.Theme.subtle
                text: root.workspace && root.workspace.capturing ? "正在收集 "+root.workspace.captured+" 字节" : "图表最多 256 样本；媒体需完整文件"
            }
        }
        Text { visible: !!text; text: root.restoreError || form.error || (root.workspace ? root.workspace.error:"数据工作区未加载"); color: C.Theme.red; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 100; radius: 8; color: C.Theme.consoleBg; border.color: C.Theme.line; clip: true
            ScrollView {
                anchors.fill: parent; anchors.margins: 10; visible: output.currentIndex===0
                TextArea { id:dataText;objectName:"dataText";text: root.workspace ? root.workspace.text:""; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; color: C.Theme.text; font.family: "Consolas"; font.pixelSize: 12; background: null }
            }
            Canvas {
                id: chart; anchors.fill: parent; anchors.margins: 10; visible: output.currentIndex===1
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onVisibleChanged: requestPaint()
                onPaint: {
                    const ctx=getContext("2d");ctx.clearRect(0,0,width,height)
                    const rows=root.workspace ? root.workspace.samples:[]
                    const key=field.currentText;const values=[]
                    for(let i=0;i<rows.length;++i) {const v=rows[i].values[key];values.push(typeof v==="number" && isFinite(v) ? v:null)}
                    const numbers=values.filter(v=>v!==null)
                    ctx.fillStyle=C.Theme.muted;ctx.font="12px sans-serif"
                    if(numbers.length===0) {ctx.fillText("等待可绘制的数值字段",18,30);return}
                    let low=Math.min.apply(null,numbers),high=Math.max.apply(null,numbers)
                    if(low===high) {low-=1;high+=1}
                    const left=58,top=20,w=Math.max(1,width-left-12),h=Math.max(1,height-top-28)
                    ctx.strokeStyle=C.Theme.line;ctx.beginPath();ctx.moveTo(left,top);ctx.lineTo(left,top+h);ctx.lineTo(left+w,top+h);ctx.stroke()
                    ctx.fillText(high.toPrecision(5),0,top+5);ctx.fillText(low.toPrecision(5),0,top+h)
                    ctx.fillText("样本 →  "+rows.length,left,top+h+20)
                    ctx.strokeStyle=C.Theme.accent;ctx.lineWidth=2;ctx.beginPath();let started=false
                    for(let j=0;j<values.length;++j) {
                        if(values[j]===null){started=false;continue}
                        const x=left+j*w/Math.max(1,values.length-1),y=top+h-(values[j]-low)*h/(high-low)
                        if(!started){ctx.moveTo(x,y);started=true}else ctx.lineTo(x,y)
                        if(numbers.length===1){ctx.moveTo(x-2,y);ctx.lineTo(x+2,y)}
                    }
                    ctx.stroke()
                }
            }
            Image {
                anchors.fill: parent; anchors.margins: 10; visible: output.currentIndex===2
                source: root.workspace && root.workspace.mediaKind==="image" ? root.workspace.mediaUrl:""
                sourceSize.width: 1600; sourceSize.height: 1200; fillMode: Image.PreserveAspectFit; asynchronous: true
                Text { anchors.centerIn: parent; visible: parent.status!==Image.Ready; text: parent.status===Image.Error ? "图片解码失败":"载入完整图片文件后预览"; color: C.Theme.muted; font.pixelSize: 12 }
            }
            Loader {
                anchors.fill: parent; active: output.currentIndex>=3; visible: active
                sourceComponent: Item {
                    id: media; onVisibleChanged: if(!visible)player.stop()
                    MediaPlayer {
                        id: player; source: root.workspace && (root.workspace.mediaKind==="video" || root.workspace.mediaKind==="audio") ? root.workspace.mediaUrl:""
                        audioOutput: AudioOutput { id: audio; volume: 0.35 }
                        videoOutput: video
                    }
                    VideoOutput { id: video; anchors.fill: parent; anchors.bottomMargin: 80; fillMode: VideoOutput.PreserveAspectFit }
                    ColumnLayout {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 10; spacing: 4
                        Text { Layout.fillWidth: true; elide: Text.ElideRight; text: player.error!==MediaPlayer.NoError ? player.errorString:root.workspace ? root.workspace.text:"载入媒体文件后播放"; color: player.error!==MediaPlayer.NoError ? C.Theme.red:C.Theme.muted; font.pixelSize: 10; maximumLineCount: 2 }
                        RowLayout {
                            C.ActionButton { text: player.playbackState===MediaPlayer.PlayingState ? "暂停":"播放"; implicitHeight: 30; enabled: player.source.toString()!==""; onClicked: player.playbackState===MediaPlayer.PlayingState ? player.pause():player.play() }
                            C.ActionButton { text: "停止"; implicitHeight: 30; onClicked: player.stop() }
                            Slider { Layout.fillWidth: true; from: 0; to: Math.max(1,player.duration); value: player.position; onMoved: player.setPosition(value) }
                            Text { text: Math.floor(player.position/1000)+" / "+Math.floor(player.duration/1000)+" s"; color: C.Theme.subtle; font.pixelSize: 10 }
                        }
                    }
                }
            }
        }
        Text {visible:root.detailed; text: "收集仅写入有效帧载荷（不含自定义头尾），最大 256 MiB；完成后载入文件预览。"; color: C.Theme.subtle; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.Wrap }
    }
}
