import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Compact radio button matching AppCheckBox.
RadioButton {
    id: control

    implicitHeight: Math.max(20, implicitContentHeight + topPadding + bottomPadding)
    padding: 2
    spacing: 6
    hoverEnabled: true
    font.pixelSize: Theme.fontSize

    Accessible.role: Accessible.RadioButton
    Accessible.name: text

    indicator: Rectangle {
        implicitWidth: 14
        implicitHeight: 14
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 7
        color: Theme.input
        border.width: control.checked ? 4 : 1
        border.color: control.checked ? Theme.accent
                    : control.hovered ? Theme.textMuted : Theme.borderStrong

        FocusRing {
            visible: control.visualFocus
            baseRadius: 7
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textFaint
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
