import QtQuick
import Pmm.Ui

// Plain table cell used by read-only lists in dialogs.
Item {
    id: root

    property string text
    property bool header: false
    property bool muted: false
    property bool alignRight: false

    height: parent ? parent.height : 22
    clip: true

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 4
        anchors.rightMargin: root.alignRight ? 8 : 4
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        textFormat: Text.PlainText
        horizontalAlignment: root.alignRight ? Text.AlignRight : Text.AlignLeft
        font.pixelSize: root.header ? Theme.fontSizeSmall : Theme.fontSize
        font.weight: root.header ? Font.DemiBold : Font.Normal
        color: root.header || root.muted ? Theme.textMuted : Theme.text
        elide: Text.ElideRight
    }
}
