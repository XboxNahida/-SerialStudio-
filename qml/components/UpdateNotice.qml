import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    property var updater: null
    property var catalogUpdater: null
    property string shownApp: ""
    property string shownCatalog: ""
    readonly property bool appAvailable: !!updater && updater.updateAvailable
    readonly property bool catalogAvailable: !!catalogUpdater && catalogUpdater.updateAvailable
    signal openUpdates()
    function refresh() {
        if(!appAvailable && !catalogAvailable){notice.close();return}
        if((appAvailable && !updater.busy && updater.latestVersion!==shownApp)
                || (catalogAvailable && !catalogUpdater.busy && catalogUpdater.latestVersion!==shownCatalog)) coalesce.restart()
    }
    function showPending() {
        if(appAvailable && !updater.busy)shownApp=updater.latestVersion
        if(catalogAvailable && !catalogUpdater.busy)shownCatalog=catalogUpdater.latestVersion
        if(appAvailable || catalogAvailable)notice.open()
    }
    onUpdaterChanged: Qt.callLater(refresh)
    onCatalogUpdaterChanged: Qt.callLater(refresh)
    Connections {target:root.updater;ignoreUnknownSignals:true;function onChanged(){root.refresh()}}
    Connections {target:root.catalogUpdater;ignoreUnknownSignals:true;function onChanged(){root.refresh()}}
    Timer {id:coalesce;interval:250;onTriggered:root.showPending()}
    Dialog {
        id:notice;objectName:"updateNoticeDialog";title:"发现可用更新";modal:false
        parent:Overlay.overlay
        anchors.centerIn:parent
        width:Math.min(460,parent?parent.width-32:460)
        closePolicy:Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background:Rectangle {color:Theme.panel;border.color:Theme.line;radius:10}
        contentItem:ColumnLayout {
            spacing:10
            Text {text:"可先继续使用，稍后到设置处理。";color:Theme.muted;wrapMode:Text.Wrap;Layout.fillWidth:true}
            RowLayout {
                visible:root.appAvailable;Layout.fillWidth:true
                Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:"APP · "+(root.updater?root.updater.latestVersion:"");color:Theme.text}
                ActionButton {objectName:"noticeIgnoreApp";text:"忽略此版";enabled:root.updater && !root.updater.busy;onClicked:root.updater.ignore()}
            }
            RowLayout {
                visible:root.catalogAvailable;Layout.fillWidth:true
                Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:"指令库 · "+(root.catalogUpdater?root.catalogUpdater.latestVersion:"");color:Theme.text}
                ActionButton {objectName:"noticeIgnoreCatalog";text:"忽略此版";enabled:root.catalogUpdater && !root.catalogUpdater.busy;onClicked:root.catalogUpdater.ignore()}
            }
            RowLayout {
                Layout.fillWidth:true
                Item {Layout.fillWidth:true}
                ActionButton {objectName:"noticeLater";text:"稍后提醒";onClicked:notice.close()}
                ActionButton {objectName:"noticeOpenUpdates";text:"立即处理";primary:true;onClicked:{notice.close();root.openUpdates()}}
            }
        }
    }
}
