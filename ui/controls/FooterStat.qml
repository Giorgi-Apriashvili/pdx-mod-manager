import QtQuick
import Pmm.Ui

// Status bar item: optional glyph plus short text.
Row {
    id: root

    property string iconName
    property color iconColor: Theme.textMuted
    property string text
    property bool strong: false

    spacing: 4
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Icon {
        anchors.verticalCenter: parent.verticalCenter
        name: root.iconName
        size: 12
        color: root.iconColor
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        textFormat: Text.PlainText
        font.pixelSize: Theme.fontSizeSmall
        font.weight: root.strong ? Font.DemiBold : Font.Normal
        color: root.strong ? Theme.text : Theme.textMuted
    }
}
