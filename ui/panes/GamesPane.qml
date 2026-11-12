pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Left column: games with installation status, rescan/locate and settings.
Rectangle {
    id: root

    property int otherGameCount: 0
    // The keyboard divider to the mods column (Main.qml sets its range).
    property alias divider: divider

    signal locateRequested()
    signal settingsRequested()
    // From the divider: a new width, or the default one (Main.qml applies it).
    signal widthRequested(real width)
    signal widthResetRequested()

    function countOtherGames() {
        let n = 0
        for (let i = 0; i < otherRepeater.count; ++i) {
            const item = otherRepeater.itemAt(i) as GameDelegate
            if (item && !item.supported)
                ++n
        }
        otherGameCount = n
    }

    // F6 target: the selected game, else the first focusable control.
    function focusFirst() {
        for (let i = 0; i < gameRepeater.count; ++i) {
            const item = gameRepeater.itemAt(i) as GameDelegate
            if (item && item.visible && item.current) {
                item.forceActiveFocus(Qt.TabFocusReason)
                return
            }
        }
        const next = root.nextItemInFocusChain(true)
        if (next)
            next.forceActiveFocus(Qt.TabFocusReason)
    }

    color: Theme.pane

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 0

        SectionHeader {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 4
            text: qsTr("Games")
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: gameColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            Column {
                id: gameColumn
                width: parent.width
                spacing: 1
                leftPadding: 4
                rightPadding: 4

                Repeater {
                    id: gameRepeater
                    model: Backend.games
                    delegate: GameDelegate {
                        width: gameColumn.width - 8
                        visible: model.supported
                    }
                }

                Item {
                    width: 1
                    height: 8
                    visible: root.otherGameCount > 0
                }
                SectionHeader {
                    x: 6
                    width: gameColumn.width - 16
                    visible: root.otherGameCount > 0
                    text: qsTr("Other games")
                }
                Text {
                    x: 6
                    width: gameColumn.width - 16
                    visible: root.otherGameCount > 0
                    topPadding: 2
                    bottomPadding: 2
                    text: qsTr("Planned; not supported by this version.")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textFaint
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Repeater {
                    id: otherRepeater
                    model: Backend.games
                    delegate: GameDelegate {
                        width: gameColumn.width - 8
                        visible: !model.supported
                    }
                    onItemAdded: Qt.callLater(root.countOtherGames)
                    onItemRemoved: Qt.callLater(root.countOtherGames)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.divider
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 8
            spacing: 4

            Text {
                Layout.fillWidth: true
                visible: Backend.games.currentSupported && !Backend.games.currentInstalled
                text: qsTr("%1 was not found. Locate the installation folder or rescan your Steam libraries.")
                      .arg(Backend.games.currentName)
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.warning
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            AppButton {
                Layout.fillWidth: true
                text: Backend.games.scanning ? qsTr("Scanning…") : qsTr("Rescan")
                iconName: "refresh"
                enabled: !Backend.games.scanning
                toolTipText: qsTr("Rediscover game installations and installed mods (F5)")
                onClicked: Backend.games.rescan()

                Spinner {
                    anchors.right: parent.right
                    anchors.rightMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    running: Backend.games.scanning
                    size: 12
                    Accessible.ignored: true
                }
            }
            AppButton {
                Layout.fillWidth: true
                text: qsTr("Locate installation…")
                iconName: "locate"
                enabled: Backend.games.currentSupported
                toolTipText: qsTr("Choose the installation and user-data folders of %1 manually")
                             .arg(Backend.games.currentName)
                onClicked: root.locateRequested()
            }
            AppButton {
                objectName: "settingsButton"
                Layout.fillWidth: true
                Layout.topMargin: 4
                text: qsTr("Settings")
                iconName: "settings"
                variant: "flat"
                toolTipText: qsTr("Theme and version information (Ctrl+,)")
                onClicked: root.settingsRequested()
            }
        }
    }

    // Last in the column's Tab order, on the divider to the mods column.
    ResizeHandle {
        id: divider
        objectName: "gamesDivider"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 7
        columnName: qsTr("Games column")
        value: root.width
        side: 1
        onResizeRequested: width => root.widthRequested(width)
        onResetRequested: root.widthResetRequested()
    }
}
