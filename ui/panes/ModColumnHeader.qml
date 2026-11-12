pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Table header for the mod list. Column edges can be dragged to resize; Name
// stretches, so the edges to its right resize Source and Status instead.
// Clicking a title switches the browse sort (never the load order).
// From the keyboard the four edges are one Tab stop: the current edge is a
// focusable separator (ResizeHandle), and Ctrl+Left / Ctrl+Right move the
// focus to the next edge (roving focus).
Rectangle {
    id: root

    property real gripWidth: 18
    property list<real> widths: [44, 30, 360, 90, 150]
    // Widest each column may get (the mods pane keeps Name at its minimum).
    property list<real> maximums: [400, 400, 400, 400, 400]
    property string sortMode: "order"
    // The edge that is in the Tab order; the last one used from the keyboard.
    property int currentEdge: 0

    signal resized(int column, real width)
    signal resizeFinished()
    signal resetRequested(int column)
    signal sortRequested(string mode)

    readonly property var titles: [qsTr("#"), qsTr("On"), qsTr("Name"), qsTr("Source"), qsTr("Status")]
    // What screen readers call the columns.
    readonly property var spokenTitles: [qsTr("Position"), qsTr("Enabled"), qsTr("Name"), qsTr("Source"), qsTr("Status")]
    readonly property var sortModes: ["order", "", "name", "source", "status"]
    readonly property var minimums: [32, 28, 120, 60, 110]
    // Left of Name an edge resizes its own column; right of Name it resizes
    // the following column.
    function targetOf(edge: int): int {
        return edge < 2 ? edge : edge + 1
    }

    // The edge takes over the Tab stop once it has the focus (onActiveFocusChanged).
    function focusEdge(edge: int) {
        if (edge < 0 || edge > 3)
            return
        const cell = cells.itemAt(edge) as HeaderCell
        if (cell)
            cell.keyHandle.forceActiveFocus(Qt.TabFocusReason)
    }

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
            id: cells
            model: 5

            delegate: HeaderCell {}
        }
    }

    component HeaderCell: Item {
        id: cell

        required property int index
        property alias keyHandle: keyHandle
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
            readonly property int target: root.targetOf(cell.index)
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
                color: handle.containsMouse || handle.pressed || keyHandle.activeFocus ? Theme.accent
                                                                                        : Theme.divider
            }

            ResizeHandle {
                id: keyHandle
                anchors.fill: parent
                activeFocusOnTab: cell.index === root.currentEdge
                columnName: qsTr("%1 column").arg(root.spokenTitles[handle.target])
                // The last cell has no edge (and no target column).
                value: root.widths[handle.target] ?? 0
                minimum: root.minimums[handle.target] ?? 0
                maximum: Math.max(minimum, root.maximums[handle.target] ?? 0)
                side: handle.direction
                hasNeighbors: true
                onActiveFocusChanged: {
                    if (activeFocus)
                        root.currentEdge = cell.index
                }
                onResizeRequested: width => {
                    root.resized(handle.target, width)
                    root.resizeFinished()
                }
                onResetRequested: root.resetRequested(handle.target)
                onNeighborRequested: delta => root.focusEdge(cell.index + delta)
            }
        }
    }
}
