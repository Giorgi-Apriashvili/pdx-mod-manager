import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Thin scroll bar that widens slightly on hover.
ScrollBar {
    id: control

    padding: 2
    minimumSize: 0.04
    hoverEnabled: true

    contentItem: Rectangle {
        implicitWidth: control.horizontal ? 40 : (control.hovered || control.pressed ? 7 : 5)
        implicitHeight: control.horizontal ? (control.hovered || control.pressed ? 7 : 5) : 40
        radius: Math.min(width, height) / 2
        color: control.pressed ? Theme.textMuted : control.hovered ? Theme.textFaint : Theme.borderStrong
        opacity: control.policy === ScrollBar.AlwaysOn || control.size < 1.0 ? 1 : 0
    }

    background: Item {
        implicitWidth: Theme.scrollBarSize + 2
        implicitHeight: Theme.scrollBarSize + 2
    }
}
