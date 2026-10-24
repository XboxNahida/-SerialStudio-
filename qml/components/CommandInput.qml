import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "CommandGuide.js" as Guide

InputBox {
    id: control
    property var candidateEntries: []
    property var completionMatches: []
    property int completionIndex: 0
    property var valueChoices: []
    signal candidateChosen(var entry)
    signal completionApplied(var candidate)
    function refreshCandidates() {
        completionMatches=readOnly?[]:valueChoices.length?valueChoices.filter(function(v){return v.value.toLowerCase().indexOf(text.toLowerCase())===0}).map(function(v){return {example:v.value,result:v.value,title:"参数值",syntax:v.description,kind:"参数值"}}):Guide.complete(text,cursorPosition,candidateEntries)
        completionIndex=0
        if(activeFocus && completionMatches.length)suggestions.open()
        else suggestions.close()
    }
    function acceptCandidate(index) {
        if(index<0 || index>=completionMatches.length)return false
        const entry=completionMatches[index]
        const previous=text, previousCursor=cursorPosition
        if(entry.kind==="整条示例")candidateChosen(entry.entry)
        text=entry.result
        cursorPosition=entry.cursor!==undefined?entry.cursor:entry.kind==="整条示例"?text.length:Math.max(0,previousCursor+text.length-previous.length)
        suggestions.close();forceActiveFocus()
        completionApplied(entry)
        return true
    }
    function moveCandidate(delta) {
        completionIndex=(completionIndex+delta+completionMatches.length)%completionMatches.length
        candidates.positionViewAtIndex(completionIndex,ListView.Contain)
    }
    onTextEdited: refreshCandidates()
    onActiveFocusChanged: if(!activeFocus)suggestions.close();else refreshCandidates()
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function(event) {
        if(event.key===Qt.Key_Escape && suggestions.visible){suggestions.close();event.accepted=true}
        else if(suggestions.visible && completionMatches.length) {
            if(event.key===Qt.Key_Tab){acceptCandidate(completionIndex);event.accepted=true}
            else if(event.key===Qt.Key_Down || event.key===Qt.Key_Up){moveCandidate(event.key===Qt.Key_Down?1:-1);event.accepted=true}
        }
    }
    Popup {
        id: suggestions; objectName:"commandCandidates"
        parent:control;y:control.mapToItem(null,0,0).y>height+8?-height-5:control.height+5;width:control.width;margins:8
        height:Math.min(280,completionMatches.length*44+90)
        padding:5;focus:false;modal:false
        closePolicy:Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background:Rectangle {color:Theme.panel;border.color:Theme.accent;radius:6}
        contentItem:ColumnLayout {
            spacing:2
            Text {text:"↑↓ / 滚轮选择 · Tab 补全 · Esc 关闭";color:Theme.muted;font.pixelSize:10;Layout.fillWidth:true;elide:Text.ElideRight}
            ListView {
                id:candidates;objectName:"commandCandidateList";Layout.fillWidth:true;Layout.fillHeight:true;clip:true
                model:control.completionMatches;ScrollBar.vertical:ScrollBar{}
                delegate:Rectangle {
                    required property var modelData
                    required property int index
                    width:candidates.width;height:44;radius:4
                    color:index===control.completionIndex?Theme.accentFill:"transparent"
                    Column {
                        anchors.fill:parent;anchors.margins:5
                        Text {width:parent.width;text:modelData.example || modelData.text || "";color:Theme.text;font.family:"Consolas";font.pixelSize:12;elide:Text.ElideRight;textFormat:Text.PlainText}
                        Text {width:parent.width;text:modelData.kind+" · "+modelData.title+" · "+(modelData.syntax || "");color:Theme.muted;font.pixelSize:10;elide:Text.ElideRight;textFormat:Text.PlainText}
                    }
                    MouseArea {anchors.fill:parent;onClicked:control.acceptCandidate(parent.index)}
                }
                MouseArea {
                    anchors.fill:parent;acceptedButtons:Qt.NoButton
                    onWheel:function(wheel){if(wheel.angleDelta.y!==0)control.moveCandidate(wheel.angleDelta.y>0?-1:1);wheel.accepted=true}
                }
            }
            Text {
                Layout.fillWidth:true;Layout.maximumHeight:46
                text:control.completionMatches.length ? control.completionMatches[control.completionIndex].syntax || "" : ""
                color:Theme.text;font.pixelSize:11;wrapMode:Text.Wrap;maximumLineCount:3;elide:Text.ElideRight;textFormat:Text.PlainText
            }
        }
    }
}
