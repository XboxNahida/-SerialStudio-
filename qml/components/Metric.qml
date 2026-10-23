import QtQuick
import QtQuick.Layouts

Panel {
    id: root
    property string caption
    property string value
    property string unit
    property color tint: Theme.accent
    implicitHeight: 86
    RowLayout {
        anchors.fill: parent
        anchors.margins: 17
        ColumnLayout {
            spacing: 7
            Text { text: root.caption; color: Theme.muted; font.pixelSize: 12 }
            RowLayout {
                spacing: 8
                Text { text: root.value; color: Theme.text; font.pixelSize: 25; font.family: "Consolas"; font.weight: Font.DemiBold }
                Text { text: root.unit; color: Theme.subtle; font.pixelSize: 11; Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: 4 }
            }
        }
        Item { Layout.fillWidth: true }
        Rectangle {
            width: 34; height: 34; radius: 10
            color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.12)
            Text { anchors.centerIn: parent; text: root.caption.indexOf("接收") >= 0 ? "↓" : root.caption.indexOf("发送") >= 0 ? "↑" : "↗"; color: root.tint; font.pixelSize: 21 }
        }
    }
}
