import QtQuick
import Pmm.Ui

// Keyboard side of a column divider, after the WAI-ARIA window splitter: a
// focusable separator whose value is the width of the column it resizes.
// Left and Right move the divider (Shift: larger steps), Home and End give the
// column its smallest and largest width, Enter its default width. The mouse
// stays with the divider underneath (SplitView handle, table header edge); this
// item takes no mouse input. Widths are applied by the owner (resizeRequested).
Item {
    id: root

    // The column it resizes, for the accessible name ("Games column").
    property string columnName
    property real value
    property real minimum: 0
    property real maximum: 10000
    // +1: the column is left of the divider, so Right widens it; -1: right of it.
    property int side: 1
    property int step: 10
    property int largeStep: 50
    // Ctrl+Left and Ctrl+Right ask for the neighbouring divider (column edges).
    property bool hasNeighbors: false

    // A width within minimum..maximum; resetRequested() for the default width,
    // which the owner applies even where the window is too narrow to show it.
    signal resizeRequested(real width)
    signal resetRequested()
    signal neighborRequested(int delta)

    function resizeTo(width: real) {
        resizeRequested(Math.round(Math.max(minimum, Math.min(maximum, width))))
        // The new width is spoken once the layout has applied it.
        Qt.callLater(announceWidth)
    }

    function reset() {
        resetRequested()
        Qt.callLater(announceWidth)
    }

    function announceWidth() {
        Accessible.announce(qsTr("%1 pixels").arg(Math.round(value)), Accessible.Polite)
    }

    activeFocusOnTab: true
    Accessible.role: Accessible.Separator
    Accessible.name: qsTr("%1 width: %2 pixels").arg(columnName).arg(Math.round(value))
    Accessible.description: (hasNeighbors ? qsTr("Ctrl+Left and Ctrl+Right: other column edges. ") : "")
                            + qsTr("Left and Right move the divider, with Shift in larger steps. Home: narrowest, End: widest, Enter: default width.")
    Accessible.focusable: true

    Keys.onPressed: event => {
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const amount = (event.modifiers & Qt.ShiftModifier) !== 0 ? root.largeStep : root.step
        switch (event.key) {
        case Qt.Key_Left:
        case Qt.Key_Right: {
            const towardsRight = event.key === Qt.Key_Right
            if (ctrl) {
                if (!root.hasNeighbors)
                    return
                root.neighborRequested(towardsRight ? 1 : -1)
            } else {
                root.resizeTo(root.value + (towardsRight ? amount : -amount) * root.side)
            }
            break
        }
        case Qt.Key_Home:
            root.resizeTo(root.minimum)
            break
        case Qt.Key_End:
            root.resizeTo(root.maximum)
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.reset()
            break
        default:
            return
        }
        event.accepted = true
    }

    // Only keyboard focus reaches this item, so the ring follows activeFocus.
    FocusRing {
        visible: root.activeFocus
        inset: 0
        baseRadius: 2
    }
}
