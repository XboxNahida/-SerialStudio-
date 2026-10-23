import QtQuick
QtObject {
    id: root
    property var settings
    property string scope: ""
    // 每项为 {key, object, property}；只绑定显式提供的表单字段。
    property var fields: []
    property bool enabled: true
    readonly property bool ready: bound
    readonly property bool restoring: loading
    readonly property string error: failure
    property bool bound: false
    property bool loading: false
    property string failure: ""
    property var subscriptions: []
    signal restored()
    property Timer deferred: Timer {interval: 0;onTriggered: root.restore()}
    function disconnectFields() {
        for(const item of subscriptions) {
            try {item.signal.disconnect(item.callback)} catch(error) {}
        }
        subscriptions=[];bound=false
    }
    function safeField(field) {
        if(!field || !field.object || !field.key || !field.property || field.secret || field.sensitive)return false
        if(/password|passphrase|secret|token|credential|private.?key/i.test(field.key))return false
        if(field.property==="text" && field.object.echoMode!==undefined && field.object.echoMode!==TextInput.Normal)return false
        return ["text","checked","value","currentIndex","currentText","position","width","height","x","y",
                "hexView","follow","advanced","detailed","pageIndex","parserSource","showData","showRules","lightMode","selectedPort"].indexOf(field.property)>=0
    }
    function restore() {
        deferred.stop();disconnectFields();failure=""
        if(!enabled || !settings || !scope)return
        loading=true
        const connected=[]
        for(const field of fields) {
            if(!safeField(field))continue
            const object=field.object
            const name=field.property
            const key=scope+"/"+field.key
            try {
                if(object[name]===undefined)throw new Error("控件没有属性 "+name)
                object[name]=settings.value(key,object[name])
                const changed=object[name+"Changed"]
                if(!changed || !changed.connect)throw new Error("控件属性不支持变更通知："+name)
                const callback=function() {
                    if(!root.enabled || root.loading || !root.settings)return
                    if(!root.safeField(field))return
                    if(!root.settings.setValue(key,object[name]))root.failure=root.settings.error || "配置保存失败"
                }
                changed.connect(callback);connected.push({signal:changed,callback:callback})
            } catch(problem) {failure="配置字段 "+field.key+"："+problem.message}
        }
        subscriptions=connected;loading=false;bound=true;restored()
    }
    onSettingsChanged: {disconnectFields();deferred.restart()}
    onScopeChanged: {disconnectFields();deferred.restart()}
    onFieldsChanged: {disconnectFields();deferred.restart()}
    onEnabledChanged: {
        disconnectFields()
        if(enabled)deferred.restart()
        else deferred.stop()
    }
    Component.onCompleted: deferred.restart()
    Component.onDestruction: disconnectFields()
}
