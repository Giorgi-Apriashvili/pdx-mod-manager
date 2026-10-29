import QtQuick
import QtQuick.Layouts
import Pmm.Ui

// Centered placeholder with an explanation and next actions (children).
ColumnLayout {
    id: root

    property string iconName
    property string title
    property string text
    default property alias actions: actionRow.data

    spacing: 6

    Icon {
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: 2
        name: root.iconName
        size: 28
        strokeWidth: 1.4
        color: Theme.textFaint
    }
    Text {
        Layout.fillWidth: true
        text: root.title
        textFormat: Text.PlainText
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSizeTitle
        font.weight: Font.DemiBold
        color: Theme.text
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }
    Text {
        Layout.fillWidth: true
        visible: root.text.length > 0
        text: root.text
        textFormat: Text.PlainText
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSize
        color: Theme.textMuted
        wrapMode: Text.Wrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
    Row {
        id: actionRow
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 6
        spacing: 8
    }
}
