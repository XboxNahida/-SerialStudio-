import QtQuick
import QtQuick.Layouts

Rectangle {
    id:root
    property var manager:null
    property var tools:[]
    property var paths:({})
    property int revision:0
    readonly property var missing: {
        const dependency=revision
        return manager?manager.missingTools(tools,paths):[]
    }
    signal settingsRequested(string componentId)
    visible:missing.length>0
    implicitHeight:visible?column.implicitHeight+16:0
    implicitWidth:200
    color:Theme.raised;radius:6;border.color:Theme.orange
    Connections {target:root.manager;ignoreUnknownSignals:true;function onChanged(){root.revision++}}
    ColumnLayout {
        id:column;anchors {left:parent.left;right:parent.right;top:parent.top;margins:8}
        spacing:6
        Text {Layout.fillWidth:true;wrapMode:Text.Wrap;color:Theme.orange;font.pixelSize:11;text:"缺少工具："+root.missing.map(function(row){return row.tool}).join("、")+"。可下载补齐，或在高级路径选择已有工具。"}
        Repeater {
            model:root.missing
            ActionButton {required property var modelData;objectName:"missingTool-"+modelData.tool;text:"前往设置下载 · "+modelData.componentName;Layout.fillWidth:true;implicitHeight:28;onClicked:root.settingsRequested(modelData.componentId)}
        }
    }
}
