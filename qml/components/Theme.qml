pragma Singleton
import QtQuick

QtObject {
    property bool dark: true
    readonly property color background: dark ? "#0c121c" : "#f0f4f8"
    readonly property color sidebar: dark ? "#101925" : "#ffffff"
    readonly property color panel: dark ? "#141f2d" : "#ffffff"
    readonly property color raised: dark ? "#1a2838" : "#f4f7fa"
    readonly property color consoleBg: dark ? "#0d1723" : "#f7fafc"
    readonly property color line: dark ? "#263547" : "#dde5ee"
    readonly property color text: dark ? "#e6edf5" : "#1c2b40"
    readonly property color muted: dark ? "#8d9db0" : "#60738b"
    readonly property color subtle: dark ? "#556a82" : "#8b9caf"
    readonly property color accent: dark ? "#51dbc3" : "#008873"
    readonly property color accentFill: dark ? "#193e40" : "#e0f5ef"
    readonly property color blue: dark ? "#79afff" : "#3e6bce"
    readonly property color orange: dark ? "#eab777" : "#ae6918"
    readonly property color red: dark ? "#fb8c9a" : "#c54a5e"
}
