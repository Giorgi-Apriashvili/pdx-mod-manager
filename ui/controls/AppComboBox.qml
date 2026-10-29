pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Compact drop-down. `detailRole` optionally shows a muted right-aligned value
// (e.g. entry counts) in the popup rows.
ComboBox {
    id: control

    property string detailRole
    property string toolTipText
    property string accessibleName: toolTipText

    implicitWidth: 150
    implicitHeight: Theme.controlHeight
    leftPadding: 8
    rightPadding: 24
    font.pixelSize: Theme.fontSize
    hoverEnabled: true

    Accessible.role: Accessible.ComboBox
    Accessible.name: accessibleName
    Accessible.description: displayText

    ToolTip.visible: toolTipText.length > 0 && hovered && !popup.visible
    ToolTip.text: toolTipText
    ToolTip.delay: 600

    delegate: ItemDelegate {
        id: item

        required property var model
        required property int index

        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: Theme.rowHeight
        padding: 0
        leftPadding: 8
        rightPadding: 8
        highlighted: control.highlightedIndex === index
        hoverEnabled: true

        Accessible.name: label.text

        contentItem: Item {
            implicitHeight: label.implicitHeight
            Text {
                id: label
                anchors.left: parent.left
                anchors.right: detail.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    const role = control.textRole
                    if (role.length === 0)
                        return item.model.modelData !== undefined ? String(item.model.modelData) : ""
                    const value = item.model[role]
                    return value === undefined ? String(item.model.modelData?.[role] ?? "") : String(value)
                }
                font.pixelSize: Theme.fontSize
                font.weight: control.currentIndex === item.index ? Font.DemiBold : Font.Normal
                color: item.highlighted ? Theme.accentText : Theme.text
                elide: Text.ElideRight
            }
            Text {
                id: detail
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (control.detailRole.length === 0)
                        return ""
                    const value = item.model[control.detailRole]
                    return value === undefined ? "" : String(value)
                }
                font.pixelSize: Theme.fontSizeSmall
                color: item.highlighted ? Theme.accentText : Theme.textMuted
            }
        }
        background: Rectangle {
            color: item.highlighted ? Theme.accent : "transparent"
        }
    }

    indicator: Icon {
        x: control.width - width - 7
        y: (control.height - height) / 2
        name: "chevron-down"
        size: 12
        color: Theme.textMuted
    }

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.text : Theme.textFaint
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: control.down ? Theme.buttonPressed : control.hovered ? Theme.buttonHover : Theme.button
        border.width: 1
        border.color: control.popup.visible ? Theme.focus : Theme.border

        FocusRing {
            visible: control.visualFocus
        }
    }

    popup: Popup {
        y: control.height + 2
        width: Math.max(control.width, 180)
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 360)
        padding: 3

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
        }

        background: Rectangle {
            color: Theme.base
            border.color: Theme.border
            radius: Theme.radius
        }
    }
}
