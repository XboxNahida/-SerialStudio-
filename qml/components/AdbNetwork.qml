import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var adb
    property var settings: null
    property string scope: "adbNetwork"
    property bool editable: true
    property bool expanded: false
    readonly property bool busy: !!adb && !!adb.connectionBusy
    spacing: 6

    PersistentForm {
        settings: root.settings
        scope: root.scope
        fields: [
            {key:"endpoint",object: endpoint,property:"text"},
            {key:"pairEndpoint",object: pairEndpoint,property:"text"}
        ]
    }
    InputBox {
        id: endpoint
        objectName: "adbNetworkEndpoint"
        Layout.fillWidth: true
        enabled: root.editable && !root.busy
        placeholderText: "网络 ADB：192.168.1.8:5555"
    }
    RowLayout {
        Layout.fillWidth: true
        ActionButton {
            objectName: "adbNetworkConnect"
            text: "连接"
            Layout.fillWidth: true
            enabled: root.editable && !!root.adb && !root.busy
            onClicked: root.adb.connectDevice(endpoint.text.trim())
        }
        ActionButton {
            objectName: "adbNetworkDisconnect"
            text: "断开"
            Layout.fillWidth: true
            enabled: root.editable && !!root.adb && !root.busy
            onClicked: root.adb.disconnectDevice(endpoint.text.trim())
        }
        ActionButton {
            objectName: "adbNetworkExpandPair"
            text: root.expanded ? "收起配对" : "无线配对"
            enabled: root.editable && !root.busy
            onClicked: {root.expanded=!root.expanded;pairCode.clear()}
        }
    }
    ColumnLayout {
        visible: root.expanded
        Layout.fillWidth: true
        spacing: 6
        InputBox {
            id: pairEndpoint
            objectName: "adbNetworkPairEndpoint"
            Layout.fillWidth: true
            enabled: root.editable && !root.busy
            placeholderText: "配对地址:端口（可能与连接端口不同）"
        }
        RowLayout {
            Layout.fillWidth: true
            InputBox {
                id: pairCode
                objectName: "adbNetworkPairCode"
                Layout.fillWidth: true
                enabled: root.editable && !root.busy
                placeholderText: "设备显示的六位配对码"
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                maximumLength: 6
            }
            ActionButton {
                objectName: "adbNetworkPair"
                text: "配对"
                enabled: root.editable && !!root.adb && !root.busy
                onClicked: {
                    const code=pairCode.text
                    pairCode.clear()
                    root.adb.pairDevice(pairEndpoint.text.trim(),code)
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: "在设备中开启无线调试。配对成功后，用调试连接端口连接；配对码不保存。"
            color: Theme.subtle
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }
    }
    Text {
        objectName: "adbNetworkStatus"
        Layout.fillWidth: true
        text: root.adb ? (root.adb.connectionStatus || "网络 ADB：填写地址和端口后连接") : "ADB 服务未加载"
        color: Theme.muted
        font.pixelSize: 11
        wrapMode: Text.Wrap
    }
    onEditableChanged: {if(!editable)pairCode.clear()}
    onExpandedChanged: {if(!expanded)pairCode.clear()}
    onVisibleChanged: {if(!visible)pairCode.clear()}
}
