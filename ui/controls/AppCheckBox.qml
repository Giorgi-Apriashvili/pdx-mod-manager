import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Compact check box; the check mark shape carries the state, not colour.
CheckBox {
    id: control

    property string toolTipText

    implicitHeight: Math.max(18, implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    padding: 2
    spacing: 6
    hoverEnabled: true
    font.pixelSize: Theme.fontSize

    Accessible.role: Accessible.CheckBox
    Accessible.name: text.length > 0 ? text : toolTipText
    Accessible.description: toolTipText

    ToolTip.visible: toolTipText.length > 0 && hovered
    ToolTip.text: toolTipText
    ToolTip.delay: 600

    indicator: Rectangle {
        implicitWidth: 14
        implicitHeight: 14
        x: control.text.length > 0 ? control.leftPadding
                                   : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 2
        color: control.checkState !== Qt.Unchecked
               ? (control.enabled ? Theme.accent : Theme.borderStrong)
               : Theme.input
        border.width: control.checkState !== Qt.Unchecked ? 0 : 1
        border.color: control.hovered ? Theme.textMuted : Theme.borderStrong

        Icon {
            anchors.centerIn: parent
            size: 12
            strokeWidth: 1.8
            name: control.checkState === Qt.Checked ? "check"
                : control.checkState === Qt.PartiallyChecked ? "minus" : ""
            color: Theme.accentText
        }

        FocusRing {
            visible: control.visualFocus
            baseRadius: 2
        }
    }

    contentItem: Text {
        leftPadding: control.text.length > 0 ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textFaint
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        visible: control.text.length > 0
    }
}
