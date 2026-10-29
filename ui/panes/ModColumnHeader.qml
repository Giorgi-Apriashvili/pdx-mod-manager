pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Table header for the mod list. Column edges can be dragged to resize; Name
// stretches, so the edges to its right resize Source and Status instead.
// Clicking a title switches the browse sort (never the load order).
Rectangle {
    id: root

    property real gripWidth: 18
    property list<real> widths: [44, 30, 360, 90, 150]
    property string sortMode: "order"

    signal resized(int column, real width)
    signal resizeFinished()
    signal sortRequested(string mode)

    readonly property var titles: [qsTr("#"), qsTr("On"), qsTr("Name"), qsTr("Source"), qsTr("Status")]
    readonly property var sortModes: ["order", "", "name", "source", "status"]
    readonly property var minimums: [32, 28, 120, 60, 110]

    implicitHeight: 24
    color: Theme.header

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.divider
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: root.gripWidth

        Repeater {
            model: 5

            delegate: Item {
                id: cell

                required property int index
                readonly property string mode: root.sortModes[index]
                readonly property bool active: mode.length > 0 && root.sortMode === mode

                width: root.widths[index]
                height: root.height
                // Earlier cells stack above later ones so each resize handle,
                // which overhangs into the next column, stays grabbable.
                z: root.titles.length - index

                AbstractButton {
                    id: titleButton
                    anchors.fill: parent
                    anchors.rightMargin: 4
                    enabled: cell.mode.length > 0
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    Accessible.role: Accessible.ColumnHeader
                    Accessible.name: root.titles[cell.index]
                    Accessible.description: cell.mode.length > 0 ? qsTr("Sort the view by this column") : ""
                    ToolTip.visible: hovered && cell.mode.length > 0
                    ToolTip.delay: 700
                    ToolTip.text: cell.mode === "order" ? qsTr("Show in load order")
                                                        : qsTr("Sort the view by %1 (load order is unchanged)")
                                                          .arg(root.titles[cell.index].toLowerCase())
                    onClicked: root.sortRequested(cell.mode)

                    background: Rectangle {
                        color: titleButton.hovered ? Theme.hover : "transparent"
                    }
                    contentItem: Row {
                        spacing: 3
                        leftPadding: cell.index === 0 ? 0 : 6
                        layoutDirection: cell.index === 0 ? Qt.RightToLeft : Qt.LeftToRight

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.titles[cell.index]
                            textFormat: Text.PlainText
                            font.pixelSize: Theme.fontSizeSmall
                            font.weight: cell.active ? Font.Bold : Font.DemiBold
                            color: cell.active ? Theme.text : Theme.textMuted
                        }
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: cell.active && cell.mode !== "order"
                            name: "chevron-down"
                            size: 10
                            color: Theme.textMuted
                        }
                    }
                }

                // Resize handle on the right edge (none after the last column).
                MouseArea {
                    id: handle

                    // Left of Name an edge resizes its own column; right of Name it
                    // resizes the following column (dragging right makes it narrower).
                    readonly property int target: cell.index < 2 ? cell.index : cell.index + 1
                    readonly property real direction: cell.index < 2 ? 1 : -1
                    property real startX: 0
                    property real startWidth: 0

                    visible: cell.index < 4
                    width: 8
                    height: parent.height
                    x: parent.width - 4
                    z: 2
                    hoverEnabled: true
                    cursorShape: Qt.SplitHCursor
                    preventStealing: true
                    onPressed: mouse => {
                        startX = mapToItem(root, mouse.x, 0).x
                        startWidth = root.widths[target]
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return
                        const dx = mapToItem(root, mouse.x, 0).x - startX
                        root.resized(target, Math.max(root.minimums[target], startWidth + direction * dx))
                    }
                    onReleased: root.resizeFinished()

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 1
                        height: parent.height - 8
                        anchors.verticalCenter: parent.verticalCenter
                        color: handle.containsMouse || handle.pressed ? Theme.accent : Theme.divider
                    }
                }
            }
        }
    }
}
