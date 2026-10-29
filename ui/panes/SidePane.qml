import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Right column: share-code actions, playset management, selected mod details,
// downloads, and Play with its blocking conditions at the bottom.
Rectangle {
    id: root

    signal importRequested()
    signal exportRequested()
    signal newPlaysetRequested()
    signal duplicateRequested()
    signal renameRequested()
    signal deleteRequested()
    signal historyRequested()

    readonly property bool hasGame: Backend.games.currentSupported && Backend.games.currentInstalled
    readonly property bool hasPlayset: Backend.playsets.activeId >= 0

    // F6 target: the first enabled control (Import code when a game is ready).
    function focusFirst() {
        const next = root.nextItemInFocusChain(true)
        if (next)
            next.forceActiveFocus(Qt.TabFocusReason)
    }

    color: Theme.pane

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Share codes
        RowLayout {
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
                toolTipText: qsTr("Import a playset from a share code (Ctrl+I)")
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
                toolTipText: qsTr("Create a share code for the active playset (Ctrl+E)")
                onClicked: root.exportRequested()
            }
        }

        PlaysetSection {
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
            Layout.fillWidth: true
        }
    }
}
