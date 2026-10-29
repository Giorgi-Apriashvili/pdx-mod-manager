import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Menu row: optional icon, label and right-aligned shortcut hint.
MenuItem {
    id: control

    property string iconName
    property string shortcutText
    property bool danger: false

    implicitWidth: Math.max(200, row.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Theme.rowHeight
    padding: 0
    leftPadding: 8
    rightPadding: 10
    font.pixelSize: Theme.fontSize
    hoverEnabled: true

    Accessible.name: text

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: Theme.rowHeight

        Row {
            id: row
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7
            opacity: control.enabled ? 1 : 0.45

            Item {
                width: 14
                height: 14
                anchors.verticalCenter: parent.verticalCenter
                Icon {
                    anchors.centerIn: parent
                    name: control.iconName
                    size: 13
                    color: control.highlighted ? Theme.accentText
                         : control.danger ? Theme.danger : Theme.textMuted
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                textFormat: Text.PlainText
                font: control.font
                color: control.highlighted ? Theme.accentText
                     : control.danger ? Theme.danger : Theme.text
            }
            Item {
                width: 24
                height: 1
            }
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: control.shortcutText
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSizeSmall
            color: control.highlighted ? Theme.accentText : Theme.textFaint
            opacity: control.enabled ? 1 : 0.45
        }
    }

    indicator: Item {}
    arrow: Item {}

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: Theme.rowHeight
        radius: 2
        color: control.highlighted ? Theme.accent : "transparent"
    }
}
