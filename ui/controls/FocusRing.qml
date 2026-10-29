import QtQuick
import Pmm.Ui

// Keyboard focus indicator drawn just outside its parent's bounds.
Rectangle {
    property real inset: -2
    property real baseRadius: Theme.radius

    anchors.fill: parent
    anchors.margins: inset
    radius: baseRadius - inset
    color: "transparent"
    border.width: 2
    border.color: Theme.focus
    Accessible.ignored: true
}
