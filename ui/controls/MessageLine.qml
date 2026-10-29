import QtQuick
import QtQuick.Layouts
import Pmm.Ui

// Glyph + wrapped text line for warnings, problems and blockers.
RowLayout {
    id: root

    property string level: "warning"
    property string text
    property string iconName: Theme.levelIcon(level)
    property color textColor: Theme.text

    spacing: 6
    Accessible.role: Accessible.StaticText
    Accessible.name: (level === "error" ? qsTr("Problem: ") : level === "warning" ? qsTr("Warning: ") : "") + text

    Icon {
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: 1
        name: root.iconName
        color: Theme.levelColor(root.level)
        size: 13
    }
    Text {
        Layout.fillWidth: true
        text: root.text
        textFormat: Text.PlainText
        font.pixelSize: Theme.fontSize
        color: root.textColor
        wrapMode: Text.Wrap
    }
}
