import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Compact context / overflow menu.
Menu {
    id: control

    padding: 3
    implicitWidth: Math.max(200, contentItem.implicitWidth + leftPadding + rightPadding)

    delegate: AppMenuItem {}

    background: Rectangle {
        implicitWidth: 200
        color: Theme.base
        border.color: Theme.border
        radius: Theme.radius
    }
}
