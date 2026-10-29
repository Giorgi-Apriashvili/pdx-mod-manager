import QtQuick
import QtQuick.Layouts
import Pmm.Ui

// Small uppercase section label with a hairline; children go on the right.
Item {
    id: root

    property string text
    default property alias actions: actionRow.data

    implicitHeight: Math.max(20, actionRow.implicitHeight)
    Accessible.role: Accessible.Heading
    Accessible.name: text

    RowLayout {
        anchors.fill: parent
        spacing: 6

        Text {
            text: root.text.toUpperCase()
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSizeSmall - 1
            font.weight: Font.DemiBold
            font.letterSpacing: 0.6
            color: Theme.textMuted
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            implicitHeight: 1
            color: Theme.divider
        }
        Row {
            id: actionRow
            spacing: 2
            visible: children.length > 0
        }
    }
}
