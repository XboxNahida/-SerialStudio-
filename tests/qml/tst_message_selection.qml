import QtQuick
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    id: tests
    name: "消息多选复制"
    when: windowShown
    visible: true; width: 1000; height: 700
    property var page
    QtObject {
        id: sessionStub
        property var terminal: messageFixture.model
        property var frames: messageFixture.model
        property var preferences: ({connection:{}})
        property var ports: []
        property bool connected: false
        property bool busy: false
        property bool writable: false
        property bool periodic: false
        property int parserMode: 1
        property var analysis: null
        function clearTerminal() {messageFixture.clear()}
        function clearFrames() {messageFixture.clear()}
        function refreshPorts() {}
        function configureParser(mode,head,tail) {return true}
    }
    Component {id: terminalFactory;App.TerminalPage {width:1000;height:700;backend:sessionStub}}
    Component {id: protocolFactory;App.ProtocolPage {width:1000;height:700;backend:sessionStub}}
    function field(name) {
        // 切换行高度并轮换模型后，ListView 会异步重建可见委托。
        tryVerify(function(){return findChild(page,name)!==null},2000,name)
        return findChild(page,name)
    }
    function open(factory) {page=factory.createObject(tests);verify(page!==null);wait(40)}
    function init() {messageFixture.reset();messageFixture.clearClipboard()}
    function cleanup() {if(page){page.destroy();page=null}wait(10)}
    function test_toolbarCheckboxClipboardAndRollover() {
        open(terminalFactory)
        const toggle=field("messageSelectionToggle")
        mouseClick(toggle)
        const first=messageFixture.idAt(0), second=messageFixture.idAt(1)
        mouseClick(field("terminalSelect_"+second));mouseClick(field("terminalSelect_"+first))
        compare(messageFixture.model.selectedCount,2)
        mouseClick(field("copySelectedMessages"))
        compare(messageFixture.clipboard(),"RX\n一\nTX\n二")
        page.hexView=true
        mouseClick(field("copySelectedMessages"))
        compare(messageFixture.clipboard(),"RX\nE4 B8 80\nTX\nE4 BA 8C")
        page.detailed=true
        messageFixture.append();wait(20)
        compare(messageFixture.model.selectedCount,1)
        verify(field("terminalSelect_"+second).checked)
        verify(!field("terminalSelect_"+messageFixture.idAt(2)).checked)
        field("terminalMessageList").forceActiveFocus()
        keyClick(Qt.Key_C,Qt.ControlModifier)
        compare(messageFixture.clipboard(),"10:02 TX\n发送 · 第二条\nE4 BA 8C")
        mouseClick(field("clearSelectedMessages"));compare(messageFixture.model.selectedCount,0)
        verify(!field("copySelectedMessages").enabled)
    }
    function test_compactContinuousTextSelection() {
        open(terminalFactory)
        const editor=field("continuousReceiveText")
        verify(editor.visible)
        verify(editor.text.indexOf("RX\n一\nTX\n二")===0)
        editor.forceActiveFocus();editor.select(0,9);keyClick(Qt.Key_C,Qt.ControlModifier)
        compare(messageFixture.clipboard(),"RX\n一\nTX\n二")
        const selected=editor.selectedText
        page.follow=false;messageFixture.append();wait(40)
        // 轮换丢弃的内容不能被错误选中为新的消息。
        verify(editor.selectedText===selected || editor.selectedText==="")
    }
    function test_singleMessageTextSelectionKeepsCtrlC() {
        open(terminalFactory)
        mouseClick(field("messageSelectionToggle"));mouseClick(field("selectAllMessages"))
        const text=field("terminalMessageText_"+messageFixture.idAt(1))
        text.forceActiveFocus();text.selectAll();keyClick(Qt.Key_C,Qt.ControlModifier)
        compare(messageFixture.clipboard(),"二")
    }
    function test_compactSelectionSurvivesRollover() {
        open(terminalFactory)
        const editor=field("continuousReceiveText")
        const start=editor.text.indexOf("TX\n二")
        editor.forceActiveFocus();editor.select(start+4,start)
        compare(editor.selectedText,"TX\n二")
        page.follow=false;messageFixture.append();wait(40)
        compare(editor.selectedText,"TX\n二")
        compare(editor.cursorPosition,0)
        keyClick(Qt.Key_C,Qt.ControlModifier)
        compare(messageFixture.clipboard(),"TX\n二")
    }
    function test_protocolCurrentFormatAndSelectNone() {
        open(protocolFactory)
        mouseClick(field("messageSelectionToggle"));mouseClick(field("selectAllMessages"))
        compare(messageFixture.model.selectedCount,3)
        mouseClick(field("copySelectedMessages"))
        compare(messageFixture.clipboard(),"接收\n首条\n发送\n第二条\n示例 · 离线演示\n离线")
        page.detailed=true
        field("protocolMessageList").forceActiveFocus();keyClick(Qt.Key_C,Qt.ControlModifier)
        verify(messageFixture.clipboard().indexOf("10:01 接收\n首条\nE4 B8 80")===0)
        wait(40)
        mouseClick(field("messageSelectionToggle"));compare(messageFixture.model.selectedCount,0)
        verify(!field("copySelectedMessages").visible)
    }
    function test_networkSharesSelectionAndClearResetsMode() {
        open(terminalFactory)
        page.networkMode=true;wait(30)
        mouseClick(field("messageSelectionToggle"));mouseClick(field("selectAllMessages"))
        compare(messageFixture.model.selectedCount,3)
        mouseClick(field("copySelectedMessages"));verify(messageFixture.clipboard().indexOf("RX\n一\nTX\n二")===0)
        sessionStub.clearTerminal();wait(20)
        compare(messageFixture.model.count,0);compare(messageFixture.model.selectedCount,0)
        verify(!field("copySelectedMessages").visible)
        messageFixture.append();wait(20)
        compare(messageFixture.model.selectedCount,0)
        mouseClick(field("messageSelectionToggle"))
        wait(40)
        verify(!field("terminalSelect_"+messageFixture.idAt(0)).checked)
    }
}
