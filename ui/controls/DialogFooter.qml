import QtQuick
import QtQuick.Layouts
import Pmm.Ui

// Dialog button row: `leading` items on the left, default children right-aligned.
Rectangle {
    id: root

    default property alias buttons: trailingRow.data
    property alias leading: leadingRow.data

    implicitHeight: 46
    color: Theme.dark ? "#1f2024" : "#f0f1f3"
    radius: 6

    // Square off the top corners where the footer meets the dialog body.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.radius
        color: root.color
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.divider
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 8

        Row {
            id: leadingRow
            spacing: 8
            Layout.alignment: Qt.AlignVCenter
        }
        Item {
            Layout.fillWidth: true
        }
        Row {
            id: trailingRow
            spacing: 8
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
