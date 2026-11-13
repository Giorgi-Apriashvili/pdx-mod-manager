import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Right column: share-code actions, playset management, selected mod details,
// downloads, and Play with its blocking conditions at the bottom. When the
// window is too short for all of that, the whole column scrolls instead, so
// Play and its reasons are always reachable.
Rectangle {
    id: root

    signal importRequested()
    signal exportRequested()
    signal newPlaysetRequested()
    signal duplicateRequested()
    signal renameRequested()
    signal deleteRequested()
    signal historyRequested()
    signal discardInterruptedRequested()
    // A focused action of the Play section went away (Main.qml moves the focus on).
    signal focusReleased()
    // From the divider: a new width, or the default one (Main.qml applies it).
    signal widthRequested(real width)
    signal widthResetRequested()

    // The keyboard divider to the mods column (Main.qml sets its range).
    property alias divider: divider
    readonly property bool hasGame: Backend.games.currentSupported && Backend.games.currentInstalled
    readonly property bool hasPlayset: Backend.playsets.activeId >= 0
    // Smallest height of the details and downloads area (which scrolls on its
    // own). Below it, the column no longer fits the window and scrolls as a
    // whole, by as much as is missing.
    readonly property int minimumMiddleHeight: 90
    readonly property real fixedHeight: shareRow.implicitHeight + 18 + playsetSection.implicitHeight + 6
                                        + playSection.implicitHeight
    readonly property bool scrolling: height < fixedHeight + minimumMiddleHeight

    // F6 target: the first enabled control (Import code when a game is ready).
    function focusFirst() {
        const next = divider.nextItemInFocusChain(true)
        if (next)
            next.forceActiveFocus(Qt.TabFocusReason)
    }

    color: Theme.pane

    // First in the column's Tab order, on the divider to the mods column.
    ResizeHandle {
        id: divider
        objectName: "sideDivider"
        z: 1
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: 7
        columnName: qsTr("Actions column")
        value: root.width
        side: -1
        onResizeRequested: width => root.widthRequested(width)
        onResetRequested: root.widthResetRequested()
    }

    Flickable {
        id: page
        objectName: "sidePage"
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.height
        interactive: root.scrolling
        clip: root.scrolling
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: column
            width: page.width
            height: Math.max(page.height, root.fixedHeight + root.minimumMiddleHeight)
            spacing: 0

            // Share codes and Workshop ID lists
            RowLayout {
                id: shareRow
                Layout.fillWidth: true
                Layout.margins: 10
                Layout.bottomMargin: 8
                spacing: 6

                AppButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    large: true
                    variant: "accent"
                    text: qsTr("Import code")
                    iconName: "import"
                    enabled: root.hasGame
                    toolTipText: qsTr("Import a playset from a share code or Workshop ID list (Ctrl+I)")
                    onClicked: root.importRequested()
                }
                AppButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    large: true
                    text: qsTr("Export code")
                    iconName: "export"
                    iconColor: Theme.accent
                    enabled: root.hasPlayset && root.hasGame
                    toolTipText: qsTr("Create a share code and Workshop ID list for the active playset (Ctrl+E)")
                    onClicked: root.exportRequested()
                }
            }

            PlaysetSection {
                id: playsetSection
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                enabled: root.hasGame
                onNewRequested: root.newPlaysetRequested()
                onDuplicateRequested: root.duplicateRequested()
                onRenameRequested: root.renameRequested()
                onDeleteRequested: root.deleteRequested()
                onHistoryRequested: root.historyRequested()
            }

            Flickable {
                id: scroller
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: root.minimumMiddleHeight
                Layout.topMargin: 6
                contentHeight: middle.implicitHeight + 12
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}

                ColumnLayout {
                    id: middle
                    x: 10
                    width: scroller.width - 20
                    spacing: 12

                    DetailsSection {
                        Layout.fillWidth: true
                    }
                    DownloadsSection {
                        Layout.fillWidth: true
                    }
                }
            }

            PlaySection {
                id: playSection
                Layout.fillWidth: true
                onDiscardRequested: root.discardInterruptedRequested()
                onFocusReleased: root.focusReleased()
            }
        }
    }
}
