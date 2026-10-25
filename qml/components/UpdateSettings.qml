import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

Panel {
    id: root
    property var updater: null
    property var catalog: null
    property var catalogUpdater: null
    property string appVersion: ""
    implicitHeight: column.implicitHeight+32
    FileDialog {
        id: importDialog; title: "导入指令库 JSON"; nameFilters: ["JSON 指令库 (*.json)"]
        onAccepted: if(root.catalog)root.catalog.importFile(selectedFile)
    }
    FileDialog {
        id: exportDialog; title: "导出当前指令库"; fileMode: FileDialog.SaveFile
        nameFilters: ["JSON 指令库 (*.json)"]; defaultSuffix: "json"
        onAccepted: if(root.catalog)root.catalog.exportFile(selectedFile)
    }
    ColumnLayout {
        id: column
        anchors {left:parent.left;right:parent.right;top:parent.top;margins:16}
        spacing:10
        Text {text:"更新与指令库";color:Theme.text;font.pixelSize:18;font.weight:Font.DemiBold}
        Text {text:"APP · "+root.appVersion+(root.updater && root.updater.updateAvailable?"   ● 新版本 "+root.updater.latestVersion:"");color:root.updater && root.updater.updateAvailable?Theme.red:Theme.text;Layout.fillWidth:true;wrapMode:Text.Wrap}
        Text {text:root.updater?root.updater.status:"APP 更新未启用";color:Theme.muted;font.pixelSize:12;Layout.fillWidth:true;wrapMode:Text.Wrap}
        ProgressBar {Layout.fillWidth:true;visible:root.updater && (root.updater.busy || root.updater.downloadReady);value:root.updater?root.updater.progress:0}
        Flow {
            Layout.fillWidth:true;spacing:8
            ActionButton {objectName:"checkUpdates";text:"检查 APP 更新";enabled:root.updater && !root.updater.busy;onClicked:root.updater.check(true)}
            ActionButton {objectName:"downloadUpdate";text:"下载安装包";enabled:root.updater && root.updater.canDownload && !root.updater.busy && !root.updater.downloadReady;onClicked:root.updater.download()}
            ActionButton {objectName:"installUpdate";text:"退出并安装";enabled:root.updater && root.updater.downloadReady && !root.updater.busy;onClicked:root.updater.install()}
            ActionButton {objectName:"ignoreAppUpdate";text:"忽略 APP 版本";enabled:root.updater && root.updater.updateAvailable && !root.updater.busy;onClicked:root.updater.ignore()}
            ActionButton {text:"打开 Release";enabled:root.updater!==null;onClicked:root.updater.openRelease()}
            ActionButton {objectName:"uninstallApp";text:"卸载应用…";enabled:root.updater && root.updater.uninstallAvailable && !root.updater.busy;onClicked:root.updater.uninstall()}
        }
        Rectangle {Layout.fillWidth:true;height:1;color:Theme.line}
        Text {text:"指令库 · "+(root.catalog?root.catalog.version || "未加载":"未启用")+(root.catalogUpdater && root.catalogUpdater.updateAvailable?"   ● 新版本 "+root.catalogUpdater.latestVersion:"");color:root.catalogUpdater && root.catalogUpdater.updateAvailable?Theme.red:Theme.text;Layout.fillWidth:true;wrapMode:Text.Wrap}
        Text {text:root.catalog?(root.catalog.count+" 条 · "+root.catalog.source+"\n"+root.catalog.status):"";color:Theme.muted;Layout.fillWidth:true;wrapMode:Text.Wrap;font.pixelSize:12}
        Text {text:root.catalogUpdater?root.catalogUpdater.status+(root.catalogUpdater.lastChecked?"\n上次检查："+root.catalogUpdater.lastChecked:""):"";color:Theme.muted;Layout.fillWidth:true;wrapMode:Text.Wrap;font.pixelSize:12}
        ProgressBar {Layout.fillWidth:true;visible:root.catalogUpdater && root.catalogUpdater.busy;value:root.catalogUpdater?root.catalogUpdater.progress:0}
        Flow {
            Layout.fillWidth:true;spacing:8
            ActionButton {objectName:"checkCatalogUpdates";text:"检查指令库更新";enabled:root.catalogUpdater && !root.catalogUpdater.busy;onClicked:root.catalogUpdater.check(true)}
            ActionButton {objectName:"downloadCatalogUpdate";text:"下载并应用指令库";enabled:root.catalogUpdater && root.catalogUpdater.canDownload && !root.catalogUpdater.busy;onClicked:root.catalogUpdater.download()}
            ActionButton {objectName:"ignoreCatalogUpdate";text:"忽略指令库版本";enabled:root.catalogUpdater && root.catalogUpdater.updateAvailable && !root.catalogUpdater.busy;onClicked:root.catalogUpdater.ignore()}
            ActionButton {objectName:"importCatalog";text:"导入 JSON";enabled:root.catalog && (!root.catalogUpdater || !root.catalogUpdater.busy);onClicked:importDialog.open()}
            ActionButton {objectName:"exportCatalog";text:"导出当前库";enabled:root.catalog && root.catalog.count>0;onClicked:exportDialog.open()}
            ActionButton {objectName:"rollbackCatalog";text:"恢复上一版";enabled:root.catalog && root.catalog.hasPrevious && (!root.catalogUpdater || !root.catalogUpdater.busy);onClicked:root.catalog.rollback()}
            ActionButton {text:"打开库目录";enabled:root.catalog!==null;onClicked:root.catalog.openDirectory()}
        }
        Text {Layout.fillWidth:true;wrapMode:Text.Wrap;color:Theme.subtle;font.pixelSize:11;text:"APP 与指令库独立检查、独立忽略。启动后台检查；下载/安装由你点击。安装 APP 时保持原目录，完整替换程序文件；用户指令、收藏及草稿保留。"}
    }
}
