import QtQuick
import Pmm.Ui

// Small rounded label (tags, "in playset" markers).
Rectangle {
    id: root

    property string text
    property color textColor: Theme.textMuted

    implicitWidth: label.implicitWidth + 10
    implicitHeight: 17
    radius: 8
    color: Theme.dark ? "#2c2f35" : "#eceef1"
    border.width: 1
    border.color: Theme.dark ? "#3a3d44" : "#dfe1e5"
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        textFormat: Text.PlainText
        font.pixelSize: Theme.fontSizeSmall
        color: root.textColor
    }
}
