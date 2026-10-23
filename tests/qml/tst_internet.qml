import QtQuick
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    id: tests
    name: "HTTP与MQTT页面"
    when: windowShown
    visible: true; width: 800; height: 540
    property var page
    readonly property var httpBackend: web
    readonly property var mqttBackend: mqtt
    readonly property var settingsBackend: settings
    QtObject {
        id: web
        property bool busy: false
        property string status: "尚未发送"
        property string error: ""
        property int statusCode: 200
        property var responseHeaders: [{name:"Content-Type",value:"text/plain"}]
        property string responseBody: "回环响应"
        property int elapsed: 3
        property int calls: 0
        property var last: ({})
        function request(config) {calls++;last=config;return true}
        function cancel() {calls++;busy=false}
    }
    QtObject {
        id: mqtt
        property bool connected: false
        property bool busy: false
        property string status: "MQTT 未连接"
        property var messages: [{topic:"sensor/temp",payload:"温度",hex:"e6b8a9e5baa6",qos:1,retain:false,timestamp:"2026-10-23"}]
        property var subscriptions: []
        property int calls: 0
        property var last: ({})
        function connectBroker(config) {calls++;last=config;return true}
        function disconnectBroker() {calls++;connected=false;busy=false}
        function subscribe(topic,qos) {calls++;last={topic:topic,qos:qos};return true}
        function unsubscribe(topic) {calls++;last={topic:topic};return true}
        function publish(topic,text,qos,retain) {calls++;last={topic:topic,text:text,qos:qos,retain:retain};return true}
        function clear() {messages=[]}
    }
    QtObject {
        id: settings
        property string error: ""
        property var values: ({})
        property var secrets: ({})
        property var plainWrites: []
        function value(key,fallback) {return values[key]===undefined?fallback:values[key]}
        function setValue(key,value) {values[key]=value;plainWrites.push(key);return true}
        function secret(key) {return secrets[key] || ""}
        function setSecret(key,value) {secrets[key]=value;return true}
    }
    QtObject {
        id: repository
        property string error: ""
        signal changed()
        function entries(scope) {return [{id:scope+"-sample",title:"示例",text:scope+"草稿"}]}
    }
    Component {id: factory; App.InternetPage {width:800;height:540;web:tests.httpBackend;mqtt:tests.mqttBackend;settings:tests.settingsBackend;quickCommands:repository}}
    function field(name) {const item=findChild(page,name);verify(item!==null,name);return item}
    function init() {
        web.calls=0;mqtt.calls=0;mqtt.connected=false;mqtt.busy=false;web.busy=false;web.error=""
        settings.values=({});settings.secrets=({});settings.plainWrites=[]
        page=factory.createObject(tests);verify(page!==null);wait(40)
    }
    function cleanup() {page.destroy();wait(10)}
    function test_restoreDoesNotConnectAndActionsExplicit() {
        compare(web.calls,0);compare(mqtt.calls,0)
        field("httpUrl").text="http://127.0.0.1:8765/test"
        field("httpBody").text="修改正文"
        mouseClick(field("httpSend"));compare(web.calls,1);compare(web.last.body,"修改正文")
        field("internetTabs").currentIndex=1;wait(20)
        mouseClick(field("mqttConnect"));compare(mqtt.calls,1);compare(mqtt.last.host,"127.0.0.1")
    }
    function test_quickOnlyFills() {
        field("httpQuick").chosen("HTTP填入")
        compare(field("httpBody").text,"HTTP填入");compare(web.calls,0)
        field("internetTabs").currentIndex=1;wait(20)
        field("mqttQuick").chosen("MQTT填入")
        compare(field("mqttBody").text,"MQTT填入");compare(mqtt.calls,0)
    }
    function test_quickTextKeepsAllNewlineKinds() {
        for(const scope of ["http","mqtt"]) {
            const panel=field(scope+"Quick")
            for(const suffix of ["","\n","\r\n","\r"]) {
                const webCalls=web.calls,mqttCalls=mqtt.calls
                panel.selection={text:"正文",hex:false,newline:suffix};panel.chosen("正文")
                compare(web.calls,webCalls);compare(mqtt.calls,mqttCalls)
                if(scope==="http") {field("httpSend").clicked();compare(web.last.body,"正文"+suffix)}
                else {mqtt.connected=true;field("mqttPublish").clicked();compare(mqtt.last.text,"正文"+suffix)}
                compare(page.localError,"")
            }
        }
    }
    function test_quickHexIsExplicitlyRejected() {
        for(const scope of ["http","mqtt"]) {
            field(scope+"Body").text="原正文"
            const panel=field(scope+"Quick")
            panel.selection={hex:true,newline:"\n",text:"41 42"};panel.chosen("41 42")
            compare(field(scope+"Body").text,"原正文")
            verify(page.localError.indexOf("HEX")>=0)
            compare(web.calls,0);compare(mqtt.calls,0)
            panel.selection={hex:false,newline:"",text:"新正文"};panel.chosen("新正文")
            compare(field(scope+"Body").text,"新正文");compare(page.localError,"")
        }
    }
    function test_exactQuickBodyPersistsAndManualEditReplacesIt() {
        for(const scope of ["http","mqtt"]) {
            const panel=field(scope+"Quick")
            panel.selection={hex:false,newline:"\r\n",text:"原文"};panel.chosen("原文")
            compare(settings.values["internet/"+scope+"/body"],"原文\r\n")
        }
        page.destroy();page=factory.createObject(tests);wait(40)
        compare(web.calls,0);compare(mqtt.calls,0)
        field("httpSend").clicked();compare(web.last.body,"原文\r\n")
        mqtt.connected=true;field("mqttPublish").clicked();compare(mqtt.last.text,"原文\r\n")
        field("httpBody").text="编辑正文";field("httpSend").clicked()
        compare(web.last.body,"编辑正文");compare(settings.values["internet/http/body"],"编辑正文")
        field("mqttBody").text="编辑消息";field("mqttPublish").clicked()
        compare(mqtt.last.text,"编辑消息");compare(settings.values["internet/mqtt/body"],"编辑消息")
    }
    function test_credentialsNeverPlaintextAndNoAutomaticActivity() {
        field("internetTabs").currentIndex=1;field("mqttAdvancedToggle").clicked()
        field("mqttPassword").text="测试口令"
        compare(settings.secret("internet/mqtt/password"),"")
        field("mqttRememberPassword").checked=true
        compare(settings.secret("internet/mqtt/password"),"测试口令")
        verify(settings.plainWrites.indexOf("internet/mqtt/password")<0)
        field("mqttRememberPassword").checked=false;compare(settings.secret("internet/mqtt/password"),"")
        compare(web.calls,0);compare(mqtt.calls,0)
    }
    function test_httpHeaderSecretAndRestore() {
        field("httpUrl").text="http://user:password@127.0.0.1/path?token=test"
        compare(settings.secret("internet/http/url"),"http://user:password@127.0.0.1/path?token=test")
        verify(settings.plainWrites.indexOf("internet/http/url")<0)
        field("httpHeaders").text="Authorization: Bearer 示例令牌"
        field("httpRememberHeaders").checked=true
        compare(settings.secret("internet/http/headers"),"Authorization: Bearer 示例令牌")
        verify(settings.plainWrites.indexOf("internet/http/headers")<0)
        page.destroy();page=factory.createObject(tests);wait(40)
        compare(field("httpUrl").text,"http://user:password@127.0.0.1/path?token=test")
        compare(field("httpHeaders").text,"Authorization: Bearer 示例令牌")
        compare(web.calls,0);compare(mqtt.calls,0)
        field("httpRememberHeaders").checked=false
        compare(settings.secret("internet/http/headers"),"")
    }
    function test_layoutErrorsAndHex() {
        verify(field("httpSend").width>50);verify(field("httpUrl").width>200)
        web.error="证书校验失败";tryVerify(function(){return field("httpStatus").text.indexOf("证书校验失败")>=0})
        field("internetTabs").currentIndex=1;wait(20)
        field("mqttHex").checked=true
        tryVerify(function(){return field("mqttLog").text.indexOf("e6b8a9e5baa6")>=0})
        page.detailed=true;wait(20)
        verify(field("mqttLog").text.indexOf("sensor/temp")>=0)
        grabImage(page).save("internet-800x540.png")
    }
}
