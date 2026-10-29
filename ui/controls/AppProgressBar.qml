import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Slim progress bar; indeterminate when the value is unknown.
ProgressBar {
    id: control

    property int barHeight: 4

    implicitWidth: 120
    implicitHeight: barHeight
    padding: 0
    Accessible.role: Accessible.ProgressBar

    background: Rectangle {
        implicitHeight: control.barHeight
        radius: control.barHeight / 2
        color: Theme.dark ? "#33363c" : "#e1e3e7"
    }

    contentItem: Item {
        implicitHeight: control.barHeight
        clip: true

        Rectangle {
            visible: !control.indeterminate
            width: control.visualPosition * parent.width
            height: parent.height
            radius: control.barHeight / 2
            color: Theme.accent
        }

        Rectangle {
            id: sweep
            visible: control.indeterminate
            width: parent.width * 0.3
            height: parent.height
            radius: control.barHeight / 2
            color: Theme.accent

            XAnimator on x {
                running: control.indeterminate && control.visible
                from: -sweep.width
                to: control.width
                duration: 1200
                loops: Animation.Infinite
            }
        }
    }
}
