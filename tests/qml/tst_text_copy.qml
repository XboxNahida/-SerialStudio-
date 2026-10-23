import QtQuick
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    id: tests
    name: "连续文本复制入口"
    when: windowShown
    visible:true;width:800;height:700
    property var page
    QtObject {
        id:webBackend
        property bool busy:false
        property string status:"响应完成"
        property string error:""
        property int statusCode:200
        property int elapsed:1
        property var responseHeaders:[{name:"Content-Type",value:"text/plain"}]
        property string responseBody:"第一行\n第二行"
    }
    QtObject {
        id:mqttBackend
        property bool connected:false
        property bool busy:false
        property string status:"未连接"
        property var messages:[{topic:"test",payload:"第一行\n第二行",hex:"31 32",qos:0,retain:false,timestamp:"10:01"}]
        property var subscriptions:[]
    }
    Component {id:factory;App.InternetPage {width:800;height:700;web:webBackend;mqtt:mqttBackend;settings:null;quickCommands:null}}
    QtObject {
        id:boardBackend
        property bool busy:false
        property bool success:true
        property string output:"请求完成\n板端响应"
        property var terminal:null
    }
    QtObject {id:sessionBackend;property string status:"未连接"}
    Component {id:boardFactory;App.BoardPage {width:800;height:700;controller:boardBackend;serialBackend:sessionBackend;networkBackend:sessionBackend}}
    function field(name) {const item=findChild(page,name);verify(item!==null,name);return item}
    function init() {messageFixture.clearClipboard();page=factory.createObject(tests);verify(page!==null);wait(30)}
    function cleanup() {page.destroy();page=null;wait(10)}
    function test_httpCopiesSelectionAndAllPreservingSelection() {
        const response=field("httpResponse"), copy=field("copyHttpResponse")
        verify(copy.enabled)
        response.select(0,3)
        mouseClick(copy);compare(messageFixture.clipboard(),"第一行")
        response.deselect()
        mouseClick(copy);compare(messageFixture.clipboard(),"第一行\n第二行")
        compare(response.selectionStart,response.selectionEnd)
        webBackend.responseBody="";wait(10);verify(!copy.enabled)
        webBackend.responseBody="第一行\n第二行"
    }
    function test_mqttCopiesCurrentHexAndDetails() {
        field("internetTabs").currentIndex=1;wait(20)
        const log=field("mqttLog"), copy=field("copyMqttLog")
        mouseClick(copy);compare(messageFixture.clipboard(),"test  第一行\n第二行")
        field("mqttHex").checked=true;page.detailed=true
        mouseClick(copy);compare(messageFixture.clipboard(),"10:01 · QoS 0\ntest  31 32")
        log.select(0,5);mouseClick(copy);compare(messageFixture.clipboard(),"10:01")
    }
    function test_boardOutputHasCopyEntry() {
        page.destroy();page=boardFactory.createObject(tests);verify(page!==null);wait(20)
        field("boardBus").currentIndex=0;wait(20)
        mouseClick(field("copyBoardOutput"));compare(messageFixture.clipboard(),"请求完成\n板端响应")
        field("boardOutput").select(0,4)
        mouseClick(field("copyBoardOutput"));compare(messageFixture.clipboard(),"请求完成")
    }
}
