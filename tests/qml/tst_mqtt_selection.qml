import QtQuick
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    id: tests
    name: "MQTT非连续消息多选"
    when: windowShown
    visible:true;width:900;height:900
    property var page
    QtObject {
        id:mqttBackend
        property bool connected:false
        property bool busy:false
        property string status:"未连接"
        property var subscriptions:[]
        property var messages:[]
        readonly property int count:messages.length
        readonly property int selectedCount:messages.filter(function(m){return m.eventSelected}).length
        property var lastFormat:[]
        signal modelReset()
        function reset() {messages=[{eventId:"a",eventSelected:false,topic:"first",payload:"一",hex:"e4b880",timestamp:"10:01",qos:0,retain:false},
            {eventId:"b",eventSelected:false,topic:"middle",payload:"二",hex:"e4ba8c",timestamp:"10:02",qos:0,retain:false},
            {eventId:"c",eventSelected:false,topic:"last",payload:"三",hex:"e4b889",timestamp:"10:03",qos:1,retain:true}];modelReset()}
        function setEventSelected(id,selected) {messages=messages.map(function(m){const entry=Object.assign({},m);if(m.eventId===id)entry.eventSelected=selected;return entry});return true}
        function selectAll() {messages=messages.map(function(m){return Object.assign({},m,{eventSelected:true})})}
        function clearSelection() {messages=messages.map(function(m){return Object.assign({},m,{eventSelected:false})})}
        function clear() {messages=[];modelReset()}
        function selectedText(hex,detailed,protocol) {
            lastFormat=[hex,detailed,protocol]
            return messages.filter(function(m){return m.eventSelected}).map(function(m){return (detailed?m.timestamp+" · QoS "+m.qos+(m.retain?" · retain":"")+"\n":"")+m.topic+"  "+(hex?m.hex:m.payload)}).join("\n")
        }
    }
    Component {id:factory;App.InternetPage {width:900;height:900;web:null;mqtt:mqttBackend;settings:null;quickCommands:null}}
    function field(name) {const item=findChild(page,name);verify(item!==null,name);return item}
    function init() {
        failOnWarning(/.*/)
        mqttBackend.reset();mqttBackend.lastFormat=[];messageFixture.clearClipboard()
        page=factory.createObject(tests);verify(page!==null);field("internetTabs").currentIndex=1;wait(40)
    }
    function cleanup() {page.destroy();page=null;wait(10)}
    function test_noncontiguousCheckboxesAndFormats() {
        mouseClick(field("messageSelectionToggle"));wait(30)
        mouseClick(field("mqttSelect_c"));wait(30)
        mouseClick(field("mqttSelect_a"));wait(30)
        compare(mqttBackend.selectedCount,2);verify(!field("mqttSelect_b").checked)
        keyClick(Qt.Key_C,Qt.ControlModifier);compare(messageFixture.clipboard(),"first  一\nlast  三")
        mouseClick(field("copySelectedMessages"));compare(messageFixture.clipboard(),"first  一\nlast  三")
        field("mqttHex").checked=true;page.detailed=true;wait(30)
        field("mqttMessageList").forceActiveFocus();keyClick(Qt.Key_C,Qt.ControlModifier)
        compare(mqttBackend.lastFormat,[true,true,false])
        compare(messageFixture.clipboard(),"10:01 · QoS 0\nfirst  e4b880\n10:03 · QoS 1 · retain\nlast  e4b889")
        const text=field("mqttMessageText_a");text.forceActiveFocus();text.select(0,5)
        keyClick(Qt.Key_C,Qt.ControlModifier);compare(messageFixture.clipboard(),"10:01")
        mouseClick(field("clearSelectedMessages"));compare(mqttBackend.selectedCount,0)
        mouseClick(field("selectAllMessages"));compare(mqttBackend.selectedCount,3)
    }
    function test_clearResetsModeAndNormalTextStillCopies() {
        mouseClick(field("messageSelectionToggle"));wait(30)
        mouseClick(field("selectAllMessages"));compare(mqttBackend.selectedCount,3)
        mouseClick(field("mqttClear"));compare(mqttBackend.selectedCount,0)
        verify(!field("copySelectedMessages").visible);verify(field("mqttLog").visible)
        mqttBackend.reset();wait(30)
        const log=field("mqttLog");log.forceActiveFocus();log.select(0,8)
        keyClick(Qt.Key_C,Qt.ControlModifier);compare(messageFixture.clipboard(),"first  一")
        mouseClick(field("messageSelectionToggle"));wait(30)
        verify(!field("mqttSelect_a").checked)
    }
}
