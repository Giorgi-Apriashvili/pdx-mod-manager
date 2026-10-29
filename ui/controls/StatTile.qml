import QtQuick
import Pmm.Ui

// Compact number + label tile for summaries (import preview counts).
Rectangle {
    id: root

    property string label
    property int value: 0
    property string iconName
    property color iconColor: Theme.textMuted

    implicitWidth: Math.max(78, content.implicitWidth + 16)
    implicitHeight: 40
    radius: Theme.radius
    color: Theme.base
    border.color: Theme.border
    Accessible.role: Accessible.StaticText
    Accessible.name: label + ": " + value

    Column {
        id: content
        anchors.verticalCenter: parent.verticalCenter
        x: 8
        spacing: 0

        Row {
            spacing: 4
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.iconName
                color: root.iconColor
                size: 12
            }
            Text {
                text: Theme.formatCount(root.value)
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Font.DemiBold
                font.features: { "tnum": 1 }
                color: Theme.text
            }
        }
        Text {
            text: root.label
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textMuted
        }
    }
}
