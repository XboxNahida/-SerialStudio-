import QtQuick
import QtQuick.Controls
import QtTest
import SerialWorkbench 1.0 as App

TestCase {
    id: testCase
    name: "工作台交互"
    when: windowShown
    property var appWindow
    component AnalysisStub: QtObject {
        property bool enabled: false
        property string text: "测试字段数据"
        property string error: ""
        property var samples: [{sample:1,values:{temperature:20}},{sample:2,values:{temperature:25}}]
        property var fields: ["temperature"]
        property url mediaUrl: ""
        property string mediaKind: ""
        property bool capturing: false
        property int captured: 0
        property string format: ""
        property string schema: ""
        signal changed()
        function configure(selected,rule) {format=selected;schema=rule;enabled=true;changed();return true}
        function clear() {}
        function openFile(url,format) {return true}
        function beginCapture(url) {capturing=true;changed();return true}
        function finishCapture() {capturing=false;changed();return true}
        function cancelCapture() {capturing=false;changed()}
    }
    component Stub: QtObject {
        property QtObject analysis: AnalysisStub {}
        property var ports: [{name:"TEST",label:"TEST · 测试用设备"}]
        property bool connected: false
        property bool busy: false
        property bool writable: connected && !busy
        property string portName: "TEST"
        property string status: "未连接"
        property string lastError: ""
        property int rxBytes: 0
        property int txBytes: 0
        property double rxRate: 0
        property int frameCount: 0
        property int errorCount: 0
        property bool periodic: false
        property int parserMode: 1
        property var preferences: ({dark:true,connection:{}})
        property var lastConfig: ({})
        property string lastSend: ""
        property int demoCount: 0
        property int applyCount: 0
        property int refreshCount: 0
        property ListModel terminal: ListModel { property int dropped: 0 }
        property ListModel frames: ListModel { property int dropped: 0 }
        signal notify(string message)
        function refreshPorts() {refreshCount++}
        function openPort(config) { lastConfig = config; connected = true; return true }
        function openNetwork(config) { lastConfig = config; connected = true; return true }
        function closePort() { connected = false }
        function savePreference(key, value) {}
        function setLines(dtr, rts) {}
        function clearTerminal() { terminal.clear() }
        function clearFrames() { frames.clear() }
        function resetStats() {}
        function demo() { demoCount++ }
        function send(text, hex, newline) { lastSend = text; return true }
        function configureParser(mode, head, tail) { parserMode = mode; applyCount++; return true }
        function startPeriodic(text, hex, newline, interval) { periodic = true; return true }
        function stopPeriodic() { periodic = false }
    }
    Stub { id: stub }
    Stub { id: networkStub }
    QtObject {
        id: migrationSettings
        property var values: ({})
        property string error: ""
        function value(key,fallback){return values[key]===undefined ? fallback : values[key]}
        function setValue(key,value){const next=Object.assign({},values);next[key]=value;values=next;return true}
        function secret(key){return ""}
        function setSecret(key,value){return true}
    }
    QtObject {
        id: inlineDiscovery
        property var devices: [{type:"network",address:"192.168.8.7:2222",name:"广播板卡",state:"available",config:{host:"192.168.8.7",port:2222}}]
        property var interfaces: []
        property bool scanning: false
        property string status: "广播发现"
        function refresh() {}
        function setSavedDevices(value) {}
        function scanSubnet(cidr,ports) {scanning=true;return true}
        function cancelScan() {scanning=false}
    }
    QtObject {
        id: boardStub
        property QtObject terminal: QtObject {
            property QtObject adb: QtObject {
                property string executablePath: ""
                property string resolvedPath: "C:/Android/platform-tools/adb.exe"
                property var devices: [{serial:"boardA",state:"device",label:"RK3576 · boardA · 已连接"}]
                property string selectedSerial: "boardA"
                property bool ready: true
                property bool enabled: false
                property bool scanning: false
                property string status: "检测到 1 台设备，1 台可连接"
                property int refreshCount: 0
                function refresh(){refreshCount++}
            }
            property bool active: false
            property bool inputBusy: false
            property int mode: -1
        property int clearCount:0
            property string status: "尚未打开交互终端"
            property string lastFill: ""
            property string lastKey: ""
            property var lastConfig: ({})
            function open(selected,config) {mode=selected;lastConfig=config;active=true;return true}
            function close() {active=false;mode=-1}
        function clearDisplay(){clearCount++}
            function fill(text) {lastFill=text;return true}
            function sendKey(key) {lastKey=key;return true}
            function commands(selected,search,category) {
                if(search && search.indexOf("不存在")>=0)return []
                return [{system:selected===0?"linux":"cmd",category:"系统",title:"测试命令",syntax:"格式",example:selected===0?"pwd":"whoami",description:"测试用说明"}]
            }
        }
        property bool busy: false
        property bool success: true
        property string output: "测试响应"
        property int source: -1
        property var request: ({})
        property var ssh: ({})
        property int terminalCount: 0
        property var terminalSessions: [{title:"终端 1",active:false}]
        property int terminalIndex:0
        function newTerminal(){terminalSessions=terminalSessions.concat([{title:"终端 "+(terminalSessions.length+1),active:false}]);terminalIndex=terminalSessions.length-1}
        function selectTerminal(index){if(index>=0&&index<terminalSessions.length)terminalIndex=index}
        function closeTerminal(index){if(index>=0&&index<terminalSessions.length&&terminalSessions.length>1){let next=terminalSessions.slice();next.splice(index,1);terminalSessions=next;terminalIndex=Math.min(terminalIndex,next.length-1)}}
        function execute(selected, config, payload) { source = selected; ssh = config; request = payload; return true }
        function openTerminal(config) { ssh = config; terminalCount++; return terminal.open(0,config) }
        function cancel() {}
    }
    QtObject {
        id: transferStub
        property bool busy: false
        property bool success: true
        property string status: "等待文件传输"
        property int progress: 0
        property var volumes: [{label:"TEST 磁盘",path:"E:/"}]
        property int selectedMode: -1
        property string source: ""
        property string destination: ""
        property var config: ({})
        function refreshVolumes() {}
        function localUrl(path) {return path}
        function localPath(url) {return url.toString()}
        function start(mode,from,to,options) {selectedMode=mode;source=from;destination=to;config=options;return true}
        function cancel() {}
    }
    QtObject {
        id: updaterStub
        property bool busy: false
        property bool updateAvailable: true
        property bool canDownload: true
        property bool downloadReady: false
        property bool uninstallAvailable: false
        property real progress: 0
        property string status: "发现测试版"
        property int checked: 0
        property int downloaded: 0
        property int installed: 0
        function check(manual) {if(manual)checked++}
        function download() {downloaded++}
        function install() {installed++}
        function ignore() {}
        function openRelease() {}
        function uninstall() {}
    }
    Component { id: windowComponent; App.Main { backend: stub; networkBackend: networkStub; board: boardStub; transfer: transferStub; updater: updaterStub; appVersion: "1.0.0" } }
    function initTestCase() {
        appWindow = windowComponent.createObject(null)
        verify(appWindow !== null)
        wait(250)
        verify(appWindow.width < 1120, "新版初始窗口仍未缩小")
        verify(appWindow.height < 760, "新版初始窗口仍未缩小")
        compare(findChild(appWindow,"boardSource").currentIndex,2)
        compare(findChild(appWindow,"boardBus").currentIndex,3)
    }
    function cleanupTestCase() { appWindow.destroy() }
    function navigateTo(page) {appWindow.pageIndex=page}
    function test_primaryConnectionsOutsideDrawers() {
        appWindow.width=760;appWindow.height=500
        const page=findChild(appWindow,"terminalPage")
        const drawer=findChild(page,"connectionDrawer")
        drawer.close();wait(200)
        const button=findChild(page,"connectButton")
        verify(button.visible,"连接串口必须在抽屉关闭时可见")
        mouseClick(button);compare(stub.connected,true)
        mouseClick(button);compare(stub.connected,false)
        appWindow.pageIndex=3;wait(100)
        const net=findChild(appWindow,"networkConnectButton")
        verify(net.visible,"网络连接必须在主界面")
        mouseClick(net);compare(networkStub.connected,true)
        mouseClick(net);compare(networkStub.connected,false)
        appWindow.pageIndex=4;wait(100)
        findChild(appWindow,"boardBus").currentIndex=3
        const opener=findChild(appWindow,"sshTerminalButton")
        verify(opener.visible,"打开终端必须在主界面")
        mouseClick(opener);compare(boardStub.terminal.active,true)
        mouseClick(opener);compare(boardStub.terminal.active,false)
        grabImage(appWindow.contentItem).save(quickEvidenceDirectory+"/primary-terminal-small.png")
        appWindow.pageIndex=0
        grabImage(appWindow.contentItem).save(quickEvidenceDirectory+"/primary-serial-small.png")
        appWindow.width=860;appWindow.height=570
    }
    function test_compactWorkspaceAndDrawerPreservesDraft() {
        appWindow.width=860;appWindow.height=570
        appWindow.pageIndex=0
        const page=findChild(appWindow,"terminalPage")
        verify(waitForPolish(page))
        const receive=findChild(page,"receivePanel")
        verify(receive!==null,"缺少可验证的主体接收区")
        verify(receive.width>appWindow.width*0.85,"侧栏仍挤占主体宽度")
        verify(receive.height>appWindow.height*0.45,"接收区高度不足")
        const draft=findChild(page,"sendInput")
        draft.text="抽屉关闭后保留草稿"
        const opener=findChild(page,"connectionDrawerButton")
        verify(opener!==null)
        mouseClick(opener);tryCompare(findChild(page,"connectionDrawer"),"opened",true)
        const drawer=findChild(page,"connectionDrawer")
        findChild(page,"baudInput").text="460800"
        drawer.close();wait(250)
        compare(draft.text,"抽屉关闭后保留草稿")
        compare(findChild(page,"baudInput").text,"460800")
    }
    function init() {
        appWindow.pageIndex = 0; appWindow.parserSource = 0
        appWindow.visible=true
        wait(30)
        const connection=findChild(appWindow,"connectionDrawer");if(connection)connection.close()
        const boardDrawer=findChild(appWindow,"boardDrawer");if(boardDrawer)boardDrawer.close()
        const navigation=findChild(appWindow,"navigationDrawer");if(navigation)navigation.close()
        const assistant=findChild(appWindow,"commandAssistantWindow");if(assistant)assistant.hide()
        wait(250)
        appWindow.discovery=null
        appWindow.settings=null
        stub.connected = false; stub.demoCount = 0; stub.applyCount = 0
        networkStub.connected = false; networkStub.applyCount = 0
        boardStub.terminal.active=false;boardStub.terminal.mode=-1;boardStub.terminalCount=0
        boardStub.terminal.adb.ready=true
        findChild(appWindow,"boardBus").currentIndex=3
        findChild(appWindow,"terminalMode").currentIndex=0
        findChild(appWindow,"terminalConnection").currentIndex=0
        wait(30)
    }
    function visualChild(item, name) {
        if (item.objectName === name) return item
        const children = item.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = visualChild(children[i], name)
            if (found) return found
        }
        return null
    }
    function test_navigate() {
        navigateTo(1)
        compare(appWindow.pageIndex, 1)
        findChild(appWindow, "applyParserButton").clicked()
        compare(stub.applyCount, 1)
        navigateTo(2)
        compare(appWindow.pageIndex, 2)
    }
    function test_removedDevicesNavigationAndMigration() {
        compare(findChild(appWindow,"nav6"),null)
        appWindow.pageIndex=6
        compare(appWindow.pageIndex,0)
        migrationSettings.values=({"window/page":6})
        appWindow.settings=migrationSettings
        tryVerify(function(){return migrationSettings.values["window/page"]===0})
        compare(appWindow.pageIndex,0)
    }
    function test_startOnSerialAndRelevantBoardConfig() {
        migrationSettings.values=({"window/page":4})
        appWindow.settings=migrationSettings
        wait(30)
        compare(appWindow.pageIndex,0)
        appWindow.pageIndex=4
        boardStub.terminal.active=false
        findChild(appWindow,"boardDrawer").open();wait(80)
        const bus=findChild(appWindow,"boardBus")
        const source=findChild(appWindow,"boardSource")
        bus.currentIndex=3
        verify(!source.visible,"交互终端不应显示无效的桥接来源")
        appWindow.width=860;appWindow.height=570
        tryCompare(findChild(appWindow,"boardDrawer"),"position",1)
        const advanced=findChild(appWindow,"boardAdvancedButton")
        if(advanced.text==="收起高级")advanced.clicked()
        findChild(appWindow,"boardDrawerScroll").contentItem.contentY=0
        wait(50);grabImage(appWindow.contentItem).save(quickEvidenceDirectory+"/v110-board-config.png")
        const mode=findChild(appWindow,"terminalMode")
        mode.currentIndex=0;boardStub.terminal.active=true
        verify(!mode.enabled,"运行中的会话不可改为其他系统")
        boardStub.terminal.active=false
        bus.currentIndex=0;source.currentIndex=0
        verify(!findChild(appWindow,"sshHost").visible,"串口桥接不应展示无效SSH地址")
        source.currentIndex=2
        verify(findChild(appWindow,"sshHost").visible)
    }
    function test_serialRefreshAndStableSelection() {
        const box=findChild(appWindow,"serialPort")
        verify(box!==null)
        const page=findChild(appWindow,"terminalPage")
        mouseClick(findChild(page,"connectionDrawerButton"));wait(80)
        const original=stub.ports
        page.selectedPort="TEST";page.restorePort()
        stub.ports=[{name:"OTHER",label:"其他板"}]
        wait(10)
        compare(page.selectedPort,"TEST")
        compare(box.currentIndex,-1)
        verify(!findChild(appWindow,"connectButton").enabled)
        stub.ports=[{name:"OTHER",label:"其他板"},original[0]]
        wait(10);compare(box.currentIndex,1)
        const before=stub.refreshCount
        box.popup.open();wait(20);box.popup.close()
        verify(stub.refreshCount>before)
        compare(stub.connected,false)
        const polling=stub.refreshCount
        wait(3100);verify(stub.refreshCount>polling)
        appWindow.pageIndex=2
        const hidden=stub.refreshCount
        wait(3100);compare(stub.refreshCount,hidden)
        stub.ports=original;page.selectedPort="TEST";page.restorePort()
    }
    function test_inlineTargetsFillNetworkAndSshWithoutConnecting() {
        appWindow.discovery=inlineDiscovery
        appWindow.pageIndex=3;wait(30)
        const network=findChild(appWindow,"networkTerminalPage")
        const target=findChild(network,"networkTarget")
        verify(target!==null);compare(target.count,1)
        target.activated(0)
        compare(findChild(appWindow,"networkHost").text,"192.168.8.7")
        compare(findChild(appWindow,"networkPort").text,"2222")
        compare(networkStub.connected,false)
        appWindow.pageIndex=4;boardStub.terminal.active=false
        findChild(appWindow,"terminalMode").currentIndex=0;wait(30)
        const host=findChild(appWindow,"sshHost")
        const sshPicker=findChild(host.parent.parent,"networkTarget")
        verify(sshPicker!==null)
        const count=boardStub.terminalCount
        sshPicker.activated(0)
        compare(host.text,"192.168.8.7");compare(boardStub.terminalCount,count)
        compare(boardStub.terminal.active,false)
    }
    function test_updateActions() {
        appWindow.pageIndex=2
        appWindow.width=1120;appWindow.height=760
        wait(120)
        const check=findChild(appWindow,"checkUpdates")
        const download=findChild(appWindow,"downloadUpdate")
        const install=findChild(appWindow,"installUpdate")
        verify(check.enabled);verify(download.enabled);verify(!install.enabled)
        verify(!findChild(appWindow,"uninstallApp").enabled)
        const before=updaterStub.checked;mouseClick(check);compare(updaterStub.checked,before+1)
        mouseClick(download);compare(updaterStub.downloaded,1)
        updaterStub.busy=true;verify(!check.enabled);verify(!download.enabled)
        updaterStub.busy=false;updaterStub.downloadReady=true
        verify(install.enabled);verify(!download.enabled)
        mouseClick(install);compare(updaterStub.installed,1)
        updaterStub.downloadReady=false
        appWindow.width=1200;appWindow.height=800
    }
    function test_connectAndSend() {
        findChild(appWindow,"connectionDrawer").open()
        const input = findChild(appWindow, "baudInput")
        input.text = "921600"
        const button = findChild(appWindow, "connectButton")
        verify(button.enabled)
        button.clicked()
        compare(stub.lastConfig.baud, "921600")
        compare(stub.lastConfig.dataBits, 8)
        compare(stub.lastConfig.port, "TEST")
        const send = findChild(appWindow, "sendButton")
        verify(send.enabled)
        compare(findChild(appWindow, "sendMode").currentIndex, 1)
        findChild(appWindow, "sendInput").text = "你好，开发板"
        send.clicked()
        compare(stub.lastSend, "你好，开发板")
        findChild(appWindow,"connectionDrawer").open();button.clicked()
        compare(stub.connected, false)
        verify(!send.enabled)
    }
    function test_themeAndDemo() {
        const themeButton = findChild(appWindow, "themeButton")
        const before = themeButton.text
        mouseClick(themeButton)
        verify(themeButton.text !== before)
        compare(stub.demoCount, 0)
        mouseClick(findChild(appWindow, "demoButton"))
        compare(stub.demoCount, 1)
    }
    function test_discoveredTcpResetsOldMode() {
        appWindow.pageIndex=3;wait(50)
        const mode=findChild(appWindow,"networkMode")
        mode.currentIndex=2
        findChild(appWindow,"networkTerminalPage").useDevice({type:"network",address:"192.0.2.50:8765",config:{host:"192.0.2.50",port:8765,mode:0}})
        compare(mode.currentIndex,0)
        compare(findChild(appWindow,"networkHost").text,"192.0.2.50")
        compare(findChild(appWindow,"networkPort").text,"8765")
        compare(networkStub.connected,false)
    }
    function test_networkAndParserSource() {
        navigateTo(3)
        compare(appWindow.pageIndex, 3)
        findChild(appWindow,"connectionDrawer").open()
        verify(waitForPolish(appWindow.contentItem))
        const host = findChild(appWindow, "networkHost")
        verify(host !== null)
        host.text = "192.0.2.10"
        findChild(appWindow, "networkPort").text = "8765"
        const connect = findChild(appWindow, "networkConnectButton")
        wait(100)
        connect.clicked()
        compare(networkStub.lastConfig.host, "192.0.2.10")
        compare(networkStub.lastConfig.port, 8765)
        compare(stub.connected, false)
        compare(findChild(appWindow, "networkSendMode").currentIndex, 1)
        findChild(appWindow, "networkSendInput").text = "网络中文"
        findChild(appWindow, "networkSendButton").clicked()
        compare(networkStub.lastSend, "网络中文")
        findChild(appWindow,"connectionDrawer").open();connect.clicked()
        compare(networkStub.connected, false)
        navigateTo(1)
        verify(waitForPolish(appWindow.contentItem))
        const parserSource = findChild(appWindow, "parserSource")
        parserSource.currentIndex = 1
        parserSource.activated(1)
        findChild(appWindow, "applyParserButton").clicked()
        compare(networkStub.applyCount, 1)
        compare(stub.applyCount, 0)
    }
    function test_boardSourcesAndTerminal() {
        navigateTo(4)
        compare(appWindow.pageIndex, 4)
        verify(waitForPolish(appWindow.contentItem))
        findChild(appWindow,"boardDrawer").open()
        const source = findChild(appWindow, "boardSource")
        const bus = findChild(appWindow, "boardBus")
        const execute = findChild(appWindow, "boardExecuteButton")
        wait(100)
        bus.currentIndex = 0
        findChild(appWindow, "boardTx").text = "0010"
        for (let selected = 0; selected < 3; ++selected) {
            source.currentIndex = selected
            execute.clicked()
            compare(boardStub.source, selected)
            compare(boardStub.request.bus, "i2c")
            compare(boardStub.request.address, 80)
            compare(boardStub.request.tx, "0010")
        }
        bus.currentIndex = 1
        verify(waitForPolish(appWindow.contentItem))
        execute.clicked()
        compare(boardStub.request.bus, "spi")
        compare(boardStub.request.device, "/dev/spidev0.0")
        bus.currentIndex = 2
        verify(waitForPolish(appWindow.contentItem))
        execute.clicked()
        compare(boardStub.request.bus, "usb")
        compare(boardStub.request.op, "list")
        bus.currentIndex = 0
        verify(waitForPolish(appWindow.contentItem))
        findChild(appWindow, "sshHost").text = "192.0.2.20"
        const scroll=findChild(appWindow,"boardDrawerScroll")
        scroll.contentItem.contentY=Math.max(0,scroll.contentItem.contentHeight-scroll.availableHeight);wait(100)
        wait(100)
        const open=findChild(appWindow, "sshTerminalButton");verify(open.enabled);open.clicked()
        compare(boardStub.terminalCount, 1)
        compare(boardStub.ssh.host, "192.0.2.20")
        boardStub.terminal.active=false;boardStub.terminal.mode=-1
    }
    function test_terminalDrawerAndSessionActions() {
        appWindow.width=860;appWindow.height=570;appWindow.pageIndex=4
        findChild(appWindow,"boardBus").currentIndex=3
        verify(waitForPolish(appWindow.contentItem))
        const panel=findChild(appWindow,"embeddedTerminalView")
        verify(panel!==null)
        verify(panel.height>appWindow.height*0.50,"终端画面未取得主要空间")
        const clear=findChild(appWindow,"clearTerminalButton")
        verify(clear.enabled)
        verify(clear.visible)
        clear.clicked()
        tryCompare(boardStub.terminal,"clearCount",1)
        const before=boardStub.terminalSessions.length
        findChild(appWindow,"newTerminalButton").clicked()
        compare(boardStub.terminalSessions.length,before+1)
        tryCompare(findChild(appWindow,"boardDrawer"),"opened",true)
        findChild(appWindow,"boardDrawer").close()
        tryCompare(findChild(appWindow,"boardDrawer"),"opened",false)
        compare(boardStub.terminalSessions.length,before+1)
    }
    function test_adbDiscoveryControls() {
        appWindow.pageIndex=4
        findChild(appWindow,"boardDrawer").open()
        const mode=findChild(appWindow,"terminalMode")
        mode.currentIndex=3
        wait(100)
        const adb=boardStub.terminal.adb
        tryCompare(adb,"enabled",true)
        const devices=findChild(appWindow,"adbDevices")
        verify(devices!==null,"Android 页缺少设备选择列表")
        compare(devices.currentIndex,0)
        const initialRefreshes=boardStub.terminal.adb.refreshCount
        devices.popup.open();wait(20);devices.popup.close()
        compare(boardStub.terminal.adb.refreshCount,initialRefreshes+1)
        const refresh=findChild(appWindow,"adbRefreshButton")
        const before=adb.refreshCount;refresh.clicked();compare(adb.refreshCount,before+1)
        const open=findChild(appWindow,"sshTerminalButton")
        adb.ready=false;tryCompare(open,"enabled",false)
        tryCompare(findChild(appWindow,"boardExecuteButton"),"enabled",false)
        adb.ready=true;tryCompare(open,"enabled",true);open.clicked()
        compare(boardStub.terminal.lastConfig.serial,"boardA")
        compare(boardStub.terminal.lastConfig.adb,adb.resolvedPath)
        boardStub.terminal.active=false;boardStub.terminal.mode=-1
        mode.currentIndex=0;tryCompare(adb,"enabled",false)
    }
    function test_commandAssistant() {
        appWindow.pageIndex=4
        findChild(appWindow,"boardBus").currentIndex=3
        verify(waitForPolish(appWindow.contentItem))
        findChild(appWindow,"boardDrawer").open()
        const mode=findChild(appWindow,"terminalMode")
        verify(mode!==null)
        mode.currentIndex=2
        const scroll=findChild(appWindow,"boardDrawerScroll")
        scroll.contentItem.contentY=Math.max(0,scroll.contentItem.contentHeight-scroll.availableHeight)
        wait(100)
        findChild(appWindow,"sshTerminalButton").clicked()
        compare(boardStub.terminal.mode,2)
        findChild(appWindow,"commandAssistantButton").clicked()
        const assistant=findChild(appWindow,"commandAssistantWindow")
        verify(assistant!==null);tryCompare(assistant,"visible",true)
        compare(assistant.width,760);compare(assistant.height,580)
        const edit=findChild(assistant,"commandEdit")
        edit.text="echo 可编辑"
        findChild(assistant,"applyCommandButton").clicked()
        compare(boardStub.terminal.lastFill,"echo 可编辑")
        compare(boardStub.terminal.lastKey,"")
        const system=findChild(assistant,"assistantSystem")
        system.currentIndex=0
        tryCompare(findChild(assistant,"applyCommandButton"),"enabled",false)
        const search=findChild(assistant,"commandSearch")
        search.text="不存在";tryCompare(assistant,"entryCount",0)
        search.text="";tryCompare(assistant,"entryCount",1)
        assistant.close()
        wait(80)
        findChild(appWindow,"boardAdvancedButton").clicked();wait(100)
        const terminalScroll=findChild(appWindow,"boardDrawerScroll")
        terminalScroll.contentItem.contentY=Math.max(0,terminalScroll.contentItem.contentHeight-terminalScroll.availableHeight)
        wait(50)
        visualChild(findChild(appWindow,"boardDrawer").contentItem,"terminalKey_enter").clicked()
        compare(boardStub.terminal.lastKey,"enter")
        boardStub.terminal.lastKey=""
        boardStub.terminal.active=false;boardStub.terminal.mode=-1;mode.currentIndex=0
    }
    function test_compactUsbLayout() {
        appWindow.width = 1120; appWindow.height = 760; appWindow.pageIndex = 4
        findChild(appWindow,"boardDrawer").open()
        findChild(appWindow, "boardBus").currentIndex = 2
        findChild(appWindow, "usbOperation").currentIndex = 1
        verify(waitForPolish(appWindow.contentItem))
        wait(100)
        const screenshot = grabImage(appWindow.contentItem)
        verify(screenshot.width > 0)
        screenshot.save("usb-compact.png")
        const viewport = findChild(appWindow, "boardDrawerScroll").contentItem
        viewport.contentY = Math.max(0, viewport.contentHeight - viewport.height)
        wait(100)
        findChild(appWindow, "boardExecuteButton").clicked()
        compare(boardStub.request.bus, "usb")
        compare(boardStub.request.op, "read")
        compare(boardStub.request.endpoint, 129)
        findChild(appWindow, "usbOperation").currentIndex = 0
        findChild(appWindow, "boardBus").currentIndex = 0
        viewport.contentY = 0
        appWindow.width = 1200; appWindow.height = 800
    }
    function test_dataRulesAndChart() {
        appWindow.pageIndex=1
        const view=findChild(appWindow,"protocolView")
        view.currentIndex=1;view.activated(1)
        verify(waitForPolish(appWindow.contentItem));wait(100)
        findChild(appWindow,"dataFormat").currentIndex=3
        findChild(appWindow,"dataSchema").text='[{"name":"x","offset":0,"type":"u16"}]'
        verify(waitForPolish(appWindow.contentItem));wait(100)
        findChild(appWindow,"dataRulesToggle").checked=true;wait(50)
        mouseClick(findChild(appWindow,"applyDataButton"))
        compare(stub.analysis.format,"binary")
        compare(JSON.parse(stub.analysis.schema)[0].name,"x")
        findChild(appWindow,"dataOutput").currentIndex=1
        verify(waitForPolish(appWindow.contentItem));wait(100)
        const chartImage=grabImage(appWindow.contentItem);verify(chartImage.width>0);chartImage.save("chart-layout.png")
        compare(findChild(appWindow,"chartField").currentText,"temperature")
        compare(findChild(appWindow,"fileFormat").count,17)
        view.currentIndex=0;view.activated(0)
    }
    function test_dataCompactRulesAndMedia() {
        appWindow.width=1120;appWindow.height=760;appWindow.pageIndex=1
        const view=findChild(appWindow,"protocolView")
        view.currentIndex=1;view.activated(1)
        const panel=findChild(appWindow,"dataFormat").parent.parent.parent
        panel.showRules=true
        findChild(appWindow,"dataFormat").currentIndex=3
        findChild(appWindow,"dataOutput").currentIndex=0
        verify(waitForPolish(appWindow.contentItem));wait(100)
        const rulesImage=grabImage(appWindow.contentItem);verify(rulesImage.width>0);rulesImage.save("data-compact.png")
        // 实例化播放器控件并加载本地视频，不调用播放。
        stub.analysis.mediaKind="video"
        stub.analysis.mediaUrl=Qt.resolvedUrl("../fixtures/sample.mp4")
        stub.analysis.changed()
        verify(waitForPolish(appWindow.contentItem));wait(250)
        compare(findChild(appWindow,"dataOutput").currentIndex,3)
        const mediaImage=grabImage(appWindow.contentItem);verify(mediaImage.width>0);mediaImage.save("media-compact.png")
        stub.analysis.mediaUrl="";stub.analysis.mediaKind="";stub.analysis.changed()
        findChild(appWindow,"dataOutput").currentIndex=0
        view.currentIndex=0;view.activated(0)
        appWindow.width=1200;appWindow.height=800
    }
    function test_fileTransferSelection() {
        appWindow.pageIndex=5;verify(waitForPolish(appWindow.contentItem));wait(100)
        const mode=findChild(appWindow,"transferMode"),start=findChild(appWindow,"transferStart")
        mode.currentIndex=0
        findChild(appWindow,"transferFile").text="C:/test.bin"
        findChild(appWindow,"transferFolder").text="E:/"
        mouseClick(start)
        compare(transferStub.selectedMode,0);compare(transferStub.destination,"E:/")
        mode.currentIndex=1;verify(waitForPolish(appWindow.contentItem));wait(100)
        verify(!start.enabled)
        findChild(appWindow,"transferHost").text="192.0.2.30"
        findChild(appWindow,"transferRemote").text="/tmp/test.bin"
        findChild(appWindow,"transferOverwrite").checked=true
        verify(start.enabled)
        mouseClick(start)
        compare(transferStub.selectedMode,1);compare(transferStub.config.remote,"/tmp/test.bin")
        const image=grabImage(appWindow.contentItem);verify(image.width>0);image.save("transfer-layout.png")
        mode.currentIndex=2;verify(waitForPolish(appWindow.contentItem));wait(100)
        mouseClick(start);compare(transferStub.selectedMode,2);compare(transferStub.destination,"C:/test.bin")
        appWindow.width=1120;appWindow.height=760
        verify(waitForPolish(appWindow.contentItem));wait(100)
        const compactImage=grabImage(appWindow.contentItem);verify(compactImage.width>0);compactImage.save("transfer-compact.png")
        appWindow.width=1200;appWindow.height=800
        mode.currentIndex=0
    }
}
