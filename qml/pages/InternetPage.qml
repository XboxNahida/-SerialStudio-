import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components" as C

Rectangle {
    id: root
    property var web
    property var mqtt
    property var settings
    property var quickCommands
    property bool detailed: false
    property bool restoringSecrets: false
    property string localError: ""
    property bool settingBody: false
    color: C.Theme.panel
    QtObject {id:httpDraft;property string text:""}
    QtObject {id:mqttDraft;property string text:""}
    function setBody(scope,text) {
        settingBody=true
        if(scope==="http")httpBody.text=text
        else mqttBody.text=text
        settingBody=false
        // TextArea 会规范化 CRLF/CR；保存原始字符串供发送与恢复。
        if(scope==="http")httpDraft.text=text
        else mqttDraft.text=text
    }
    function fillQuick(panel,text,scope) {
        const entry=panel.selectedEntry || ({})
        if(entry.hex) {
            localError=scope.toUpperCase()+" 正文仅支持 UTF-8 文本，无法填入 HEX 字节条目；请选择文本条目"
            return
        }
        localError=""
        setBody(scope,text+(entry.newline || ""))
    }
    function storeSecret(key,value) {
        if(restoringSecrets || !settings)return
        if(!settings.setSecret(key,value))localError=settings.error || "凭据保存失败"
    }
    function restoreSecrets() {
        restoringSecrets=true
        mqttPassword.text=settings && rememberPassword.checked ? settings.secret("internet/mqtt/password") : ""
        httpHeaders.text=settings && rememberHeaders.checked ? settings.secret("internet/http/headers") : ""
        if(settings)httpUrl.text=settings.secret("internet/http/url") || "http://127.0.0.1:8765/"
        restoringSecrets=false
    }
    function sendHttp() {
        localError=""
        const headers=[]
        for(const line of httpHeaders.text.split(/\r?\n/)) {
            if(!line.trim())continue
            const at=line.indexOf(":")
            if(at<1) {localError="请求头每行应为 名称: 值";return}
            headers.push({name:line.slice(0,at).trim(),value:line.slice(at+1).trim()})
        }
        if(web)web.request({method:httpMethod.currentText,url:httpUrl.text,headers:headers,
            body:httpDraft.text,contentType:httpContentType.text,timeout:Number(httpTimeout.text)})
    }
    C.PersistentForm {
        id: form;settings:root.settings;scope:"internet"
        fields: [{key:"tab",object:tabs,property:"currentIndex"},
            {key:"http/method",object:httpMethod,property:"currentIndex"},
            {key:"http/body",object:httpDraft,property:"text"},{key:"http/contentType",object:httpContentType,property:"text"},
            {key:"http/timeout",object:httpTimeout,property:"text"},{key:"http/rememberHeaders",object:rememberHeaders,property:"checked"},
            {key:"http/advanced",object:httpAdvancedToggle,property:"checked"},
            {key:"mqtt/host",object:mqttHost,property:"text"},{key:"mqtt/port",object:mqttPort,property:"text"},
            {key:"mqtt/clientId",object:mqttClientId,property:"text"},{key:"mqtt/user",object:mqttUser,property:"text"},
            {key:"mqtt/tls",object:mqttTls,property:"checked"},{key:"mqtt/caFile",object:mqttCaFile,property:"text"},
            {key:"mqtt/keepAlive",object:mqttKeepAlive,property:"text"},{key:"mqtt/cleanSession",object:cleanSession,property:"checked"},
            {key:"mqtt/rememberPassword",object:rememberPassword,property:"checked"},
            {key:"mqtt/advanced",object:mqttAdvancedToggle,property:"checked"},
            {key:"mqtt/filter",object:mqttFilter,property:"text"},{key:"mqtt/topic",object:mqttTopic,property:"text"},
            {key:"mqtt/body",object:mqttDraft,property:"text"},{key:"mqtt/qos",object:mqttQos,property:"currentIndex"},
            {key:"mqtt/retain",object:mqttRetain,property:"checked"},{key:"mqtt/hex",object:mqttHex,property:"checked"}]
        onRestored: {root.setBody("http",httpDraft.text);root.setBody("mqtt",mqttDraft.text);root.restoreSecrets()}
    }
    ColumnLayout {
        anchors.fill:parent;anchors.margins:12;spacing:8
        TabBar {
            id:tabs;objectName:"internetTabs";Layout.fillWidth:true
            background:Rectangle {color:C.Theme.consoleBg;radius:8}
            TabButton {
                id:httpTab;text:"HTTP / HTTPS"
                background:Rectangle {radius:8;color:httpTab.checked?C.Theme.accentFill:C.Theme.raised;border.color:httpTab.checked?C.Theme.accent:C.Theme.line}
                contentItem:Text {text:httpTab.text;color:httpTab.checked?C.Theme.accent:C.Theme.muted;font.pixelSize:13;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
            }
            TabButton {
                id:mqttTab;text:"MQTT / MQTTS"
                background:Rectangle {radius:8;color:mqttTab.checked?C.Theme.accentFill:C.Theme.raised;border.color:mqttTab.checked?C.Theme.accent:C.Theme.line}
                contentItem:Text {text:mqttTab.text;color:mqttTab.checked?C.Theme.accent:C.Theme.muted;font.pixelSize:13;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
            }
        }
        Text {
            visible:root.localError!=="" || form.error!=="";Layout.fillWidth:true;wrapMode:Text.Wrap
            text:root.localError || form.error;color:C.Theme.red;font.pixelSize:12
        }
        StackLayout {
            currentIndex:tabs.currentIndex;Layout.fillWidth:true;Layout.fillHeight:true
            ScrollView {
                contentWidth:availableWidth;clip:true
                ColumnLayout {
                    width:parent.width;spacing:7
                    RowLayout {
                        Layout.fillWidth:true
                        C.SelectBox {id:httpMethod;objectName:"httpMethod";model:["GET","POST","PUT","PATCH","DELETE","HEAD","OPTIONS"];Layout.preferredWidth:105}
                        C.InputBox {id:httpUrl;objectName:"httpUrl";Layout.fillWidth:true;text:"http://127.0.0.1:8765/";placeholderText:"完整 HTTP(S) URL";onTextChanged:if(form.ready && !form.restoring)root.storeSecret("internet/http/url",text)}
                        C.ActionButton {id:httpSend;objectName:"httpSend";text:root.web && root.web.busy?"取消":"发送";primary:!(root.web && root.web.busy);enabled:!!root.web;onClicked:root.web.busy?root.web.cancel():root.sendHttp()}
                    }
                    Text {objectName:"httpStatus";Layout.fillWidth:true;wrapMode:Text.Wrap;color:root.web && root.web.error?C.Theme.red:C.Theme.muted;font.pixelSize:12;text:root.web?(root.web.error || root.web.status):"HTTP 核心尚未接入"}
                    C.ActionButton {id:httpAdvancedToggle;objectName:"httpAdvancedToggle";checkable:true;text:(checked?"▾ ":"▸ ")+"高级 · 请求头、类型与超时"}
                    ColumnLayout {
                        id:httpAdvanced;visible:httpAdvancedToggle.checked;Layout.fillWidth:true;spacing:6
                        RowLayout {
                            Layout.fillWidth:true
                            C.InputBox {id:httpContentType;objectName:"httpContentType";Layout.fillWidth:true;text:"application/json";placeholderText:"Content-Type"}
                            C.InputBox {id:httpTimeout;objectName:"httpTimeout";Layout.preferredWidth:150;text:"15000";placeholderText:"超时（毫秒）";validator:IntValidator {bottom:1;top:120000}}
                        }
                        ScrollView {Layout.fillWidth:true;Layout.preferredHeight:74;clip:true;TextArea {id:httpHeaders;objectName:"httpHeaders";placeholderText:"请求头，每行 名称: 值";color:C.Theme.text;wrapMode:TextEdit.WrapAnywhere;selectByMouse:true;onTextChanged:if(rememberHeaders.checked)root.storeSecret("internet/http/headers",text)}}
                        CheckBox {id:rememberHeaders;objectName:"httpRememberHeaders";text:"加密记住请求头（可能包含认证信息）";palette.windowText:C.Theme.text;onCheckedChanged:if(form.ready && !form.restoring)root.storeSecret("internet/http/headers",checked?httpHeaders.text:"")}
                    }
                    Text {text:"请求正文";color:C.Theme.muted;font.pixelSize:12}
                    ScrollView {Layout.fillWidth:true;Layout.preferredHeight:86;clip:true;TextArea {id:httpBody;objectName:"httpBody";placeholderText:"UTF-8 文本或 JSON；快捷填入保留换行，编辑后按文本发送";color:C.Theme.text;wrapMode:TextEdit.WrapAnywhere;selectByMouse:true;onTextChanged:if(!root.settingBody)httpDraft.text=text;background:Rectangle {color:C.Theme.consoleBg;radius:6;border.color:C.Theme.line}}}
                    C.QuickSendPanel {id:httpQuick;objectName:"httpQuick";Layout.fillWidth:true;repository:root.quickCommands;scope:"http";expanded:false;detailed:root.detailed;onChosen:function(text){root.fillQuick(httpQuick,text,"http")}}
                    RowLayout {
                        Layout.fillWidth:true
                        Text {Layout.fillWidth:true;text:root.web?"响应 · HTTP "+root.web.statusCode+" · "+root.web.elapsed+" ms":"响应";color:C.Theme.muted;font.pixelSize:12}
                        C.CopyTextButton {objectName:"copyHttpResponseHeaders";visible:root.detailed;editor:httpResponseHeaders}
                        C.CopyTextButton {objectName:"copyHttpResponse";editor:httpResponse}
                    }
                    TextEdit {id:httpResponseHeaders;visible:root.detailed;Layout.fillWidth:true;readOnly:true;selectByMouse:true;textFormat:TextEdit.PlainText;wrapMode:TextEdit.WrapAnywhere;color:C.Theme.muted;font.pixelSize:11;text:root.web?root.web.responseHeaders.map(function(h){return h.name+": "+h.value}).join("\n"):""}
                    ScrollView {Layout.fillWidth:true;Layout.preferredHeight:160;clip:true;TextArea {id:httpResponse;objectName:"httpResponse";readOnly:true;text:root.web?root.web.responseBody:"";color:C.Theme.text;wrapMode:TextEdit.WrapAnywhere;selectByMouse:true;background:Rectangle {color:C.Theme.consoleBg;radius:6;border.color:C.Theme.line}}}
                }
            }
            ScrollView {
                contentWidth:availableWidth;clip:true
                ColumnLayout {
                    width:parent.width;spacing:7
                    RowLayout {
                        Layout.fillWidth:true
                        C.InputBox {id:mqttHost;objectName:"mqttHost";Layout.fillWidth:true;text:"127.0.0.1";placeholderText:"Broker 主机";enabled:!(root.mqtt && (root.mqtt.connected || root.mqtt.busy))}
                        C.InputBox {id:mqttPort;objectName:"mqttPort";Layout.preferredWidth:85;text:"1883";placeholderText:"端口";validator:IntValidator {bottom:1;top:65535}}
                        C.ActionButton {objectName:"mqttConnect";text:root.mqtt && (root.mqtt.connected || root.mqtt.busy)?"断开 / 取消":"连接";primary:!(root.mqtt && root.mqtt.connected);enabled:!!root.mqtt;onClicked:{
                            root.localError=""
                            if(root.mqtt.connected || root.mqtt.busy)root.mqtt.disconnectBroker()
                            else root.mqtt.connectBroker({host:mqttHost.text,port:Number(mqttPort.text),clientId:mqttClientId.text,user:mqttUser.text,password:mqttPassword.text,tls:mqttTls.checked,caFile:mqttCaFile.text,keepAlive:Number(mqttKeepAlive.text),cleanSession:cleanSession.checked})
                        }}
                    }
                    Text {Layout.fillWidth:true;wrapMode:Text.Wrap;color:root.mqtt && !root.mqtt.connected?C.Theme.orange:C.Theme.muted;font.pixelSize:12;text:root.mqtt?root.mqtt.status:"MQTT 核心尚未接入"}
                    C.ActionButton {id:mqttAdvancedToggle;objectName:"mqttAdvancedToggle";checkable:true;text:(checked?"▾ ":"▸ ")+"高级 · TLS、认证与会话"}
                    ColumnLayout {
                        id:mqttAdvanced;visible:mqttAdvancedToggle.checked;Layout.fillWidth:true;spacing:6
                        RowLayout {
                            Layout.fillWidth:true
                            C.InputBox {id:mqttClientId;objectName:"mqttClientId";Layout.fillWidth:true;text:"SerialStudio";placeholderText:"Client ID（必填）"}
                            C.InputBox {id:mqttKeepAlive;Layout.preferredWidth:130;text:"60";placeholderText:"保活（秒）";validator:IntValidator {bottom:0;top:65535}}
                        }
                        RowLayout {
                            Layout.fillWidth:true
                            C.InputBox {id:mqttUser;Layout.fillWidth:true;placeholderText:"用户名"}
                            C.InputBox {id:mqttPassword;objectName:"mqttPassword";Layout.fillWidth:true;placeholderText:"密码";echoMode:TextInput.Password;onTextChanged:if(rememberPassword.checked)root.storeSecret("internet/mqtt/password",text)}
                        }
                        RowLayout {
                            CheckBox {id:mqttTls;objectName:"mqttTls";text:"TLS（建议端口 8883）";palette.windowText:C.Theme.text}
                            CheckBox {id:cleanSession;checked:true;text:"清理会话";palette.windowText:C.Theme.text}
                            CheckBox {id:rememberPassword;objectName:"mqttRememberPassword";text:"记住密码";palette.windowText:C.Theme.text;onCheckedChanged:if(form.ready && !form.restoring)root.storeSecret("internet/mqtt/password",checked?mqttPassword.text:"")}
                        }
                        C.InputBox {id:mqttCaFile;Layout.fillWidth:true;placeholderText:"可选 PEM CA 文件绝对路径；保持证书与主机名验证"}
                    }
                    RowLayout {
                        Layout.fillWidth:true
                        C.InputBox {id:mqttFilter;objectName:"mqttFilter";Layout.fillWidth:true;text:"sensor/#";placeholderText:"订阅过滤器，支持 + 和 #"}
                        C.ActionButton {objectName:"mqttSubscribe";text:"订阅";enabled:!!root.mqtt && root.mqtt.connected;onClicked:root.mqtt.subscribe(mqttFilter.text,mqttQos.currentIndex)}
                        C.ActionButton {objectName:"mqttUnsubscribe";text:"退订";enabled:!!root.mqtt && root.mqtt.connected;onClicked:root.mqtt.unsubscribe(mqttFilter.text)}
                    }
                    Text {visible:root.detailed;Layout.fillWidth:true;wrapMode:Text.Wrap;color:C.Theme.muted;font.pixelSize:11;text:"已确认订阅："+(root.mqtt?root.mqtt.subscriptions.join("，"):"")}
                    RowLayout {
                        Layout.fillWidth:true
                        C.InputBox {id:mqttTopic;objectName:"mqttTopic";Layout.fillWidth:true;text:"sensor/test";placeholderText:"发布主题"}
                        C.SelectBox {id:mqttQos;objectName:"mqttQos";Layout.preferredWidth:95;model:["QoS 0","QoS 1"]}
                        CheckBox {id:mqttRetain;text:"retain";palette.windowText:C.Theme.text}
                        C.ActionButton {objectName:"mqttPublish";text:"发布";primary:true;enabled:!!root.mqtt && root.mqtt.connected;onClicked:root.mqtt.publish(mqttTopic.text,mqttDraft.text,mqttQos.currentIndex,mqttRetain.checked)}
                    }
                    ScrollView {Layout.fillWidth:true;Layout.preferredHeight:65;clip:true;TextArea {id:mqttBody;objectName:"mqttBody";placeholderText:"UTF-8 正文；快捷填入保留换行，编辑后按文本发送";color:C.Theme.text;wrapMode:TextEdit.WrapAnywhere;selectByMouse:true;onTextChanged:if(!root.settingBody)mqttDraft.text=text;background:Rectangle {color:C.Theme.consoleBg;radius:6;border.color:C.Theme.line}}}
                    C.QuickSendPanel {id:mqttQuick;objectName:"mqttQuick";Layout.fillWidth:true;repository:root.quickCommands;scope:"mqtt";expanded:false;detailed:root.detailed;onChosen:function(text){root.fillQuick(mqttQuick,text,"mqtt")}}
                    RowLayout {
                        Layout.fillWidth:true
                        Text {text:"收到的消息";color:C.Theme.muted;font.pixelSize:12;Layout.fillWidth:true}
                        CheckBox {id:mqttHex;objectName:"mqttHex";text:"HEX";palette.windowText:C.Theme.text}
                        C.CopyTextButton {objectName:"copyMqttLog";visible:!mqttSelection.active;editor:mqttLog}
                        C.ActionButton {objectName:"mqttClear";text:"清空";enabled:!!root.mqtt;onClicked:root.mqtt.clear()}
                    }
                    C.MessageSelectionBar {
                        id:mqttSelection;objectName:"mqttMessageSelection"
                        Layout.fillWidth:true;eventModel:root.mqtt;hex:mqttHex.checked;detailed:root.detailed
                    }
                    ScrollView {
                        visible:!mqttSelection.active;Layout.fillWidth:true;Layout.preferredHeight:145;clip:true
                        TextArea {
                            id:mqttLog;objectName:"mqttLog";readOnly:true;selectByMouse:true;color:C.Theme.text;wrapMode:TextEdit.WrapAnywhere
                            text:root.mqtt && !mqttSelection.active?root.mqtt.messages.map(function(m){return (root.detailed?m.timestamp+" · QoS "+m.qos+(m.retain?" · retain":"")+"\n":"")+m.topic+"  "+(mqttHex.checked?m.hex:m.payload)}).join("\n"):""
                            background:Rectangle {color:C.Theme.consoleBg;radius:6;border.color:C.Theme.line}
                        }
                    }
                    ListView {
                        id:mqttMessageList;objectName:"mqttMessageList"
                        visible:mqttSelection.active;Layout.fillWidth:true;Layout.preferredHeight:220
                        clip:true;spacing:5;model:root.mqtt?root.mqtt.messages:[]
                        boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar {}
                        Keys.onPressed:function(event){mqttSelection.handleCopy(event)}
                        delegate:Rectangle {
                            id:mqttMessage
                            required property var modelData
                            width:mqttMessageList.width;height:messageRow.implicitHeight+12;radius:6
                            color:modelData.eventSelected?C.Theme.accentFill:C.Theme.consoleBg
                            RowLayout {
                                id:messageRow;anchors.left:parent.left;anchors.right:parent.right;anchors.margins:6;y:6;spacing:8
                                CheckBox {
                                    objectName:"mqttSelect_"+mqttMessage.modelData.eventId
                                    checked:!!mqttMessage.modelData.eventSelected
                                    implicitWidth:28;implicitHeight:28;Layout.alignment:Qt.AlignTop
                                    Accessible.name:"选择 MQTT 消息 "+mqttMessage.modelData.topic
                                    onClicked:{mqttMessageList.forceActiveFocus();root.mqtt.setEventSelected(mqttMessage.modelData.eventId,checked)}
                                }
                                TextEdit {
                                    objectName:"mqttMessageText_"+mqttMessage.modelData.eventId
                                    Layout.fillWidth:true;readOnly:true;selectByMouse:true;textFormat:TextEdit.PlainText
                                    wrapMode:TextEdit.WrapAnywhere;color:C.Theme.text;font.family:"Consolas";font.pixelSize:12
                                    text:(root.detailed?mqttMessage.modelData.timestamp+" · QoS "+mqttMessage.modelData.qos+(mqttMessage.modelData.retain?" · retain":"")+"\n":"")+
                                        mqttMessage.modelData.topic+"  "+(mqttHex.checked?mqttMessage.modelData.hex:mqttMessage.modelData.payload)
                                    Keys.onPressed:function(event){if(selectedText.length===0)mqttSelection.handleCopy(event)}
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
