pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Keyboard reference (F1). The list is focusable so each shortcut can be read
// row by row with the arrow keys; docs/accessibility.md has the same table.
AppDialog {
    id: dialog

    readonly property var groups: [
        { title: qsTr("Anywhere"), rows: [
            [qsTr("Tab, Shift+Tab"), qsTr("Next / previous control")],
            [qsTr("F6, Shift+F6"), qsTr("Next / previous column (games, mods, actions)")],
            [qsTr("Ctrl+F"), qsTr("Search the mod list")],
            [qsTr("Ctrl+Z"), qsTr("Undo the last playset edit")],
            [qsTr("Ctrl+Y, Ctrl+Shift+Z"), qsTr("Redo")],
            [qsTr("Ctrl+I, Ctrl+E"), qsTr("Import / export a share code")],
            [qsTr("Ctrl+N"), qsTr("New playset")],
            [qsTr("Ctrl+Shift+A"), qsTr("Add installed mods")],
            [qsTr("F5"), qsTr("Rescan games and mods")],
            [qsTr("Ctrl+,"), qsTr("Settings")],
            [qsTr("F1"), qsTr("This list")]
        ] },
        { title: qsTr("Mod list"), rows: [
            [qsTr("Up, Down, PgUp, PgDn, Home, End"), qsTr("Move to another row")],
            [qsTr("Shift + the keys above"), qsTr("Extend the selection")],
            [qsTr("Ctrl+Space"), qsTr("Add the row to the selection or take it out")],
            [qsTr("Ctrl+A"), qsTr("Select all visible rows")],
            [qsTr("Space"), qsTr("Enable or disable the row or the selection")],
            [qsTr("Alt+Up, Alt+Down"), qsTr("Move the selection earlier / later in the load order (also Ctrl+Up, Ctrl+Down)")],
            [qsTr("Delete"), qsTr("Remove the selection from the playset (asks first)")],
            [qsTr("Shift+F10, Menu key"), qsTr("Context menu of the row")],
            [qsTr("Esc in the search field"), qsTr("Clear the search; again: back to the list")]
        ] },
        { title: qsTr("Games, dialogs and menus"), rows: [
            [qsTr("Up, Down"), qsTr("Next / previous game, list row or menu item")],
            [qsTr("Enter, Space"), qsTr("Select the game, activate the button or menu item")],
            [qsTr("Esc"), qsTr("Close the dialog or menu; focus returns where it was")]
        ] }
    ]

    title: qsTr("Keyboard shortcuts")
    preferredWidth: 600
    preferredHeight: 640

    onAboutToShow: {
        rows.clear()
        for (const group of groups) {
            for (const row of group.rows)
                rows.append({ group: group.title, keys: row[0], action: row[1] })
        }
        shortcutList.currentIndex = 0
    }
    onOpened: shortcutList.forceActiveFocus(Qt.TabFocusReason)

    ListModel {
        id: rows
    }

    contentItem: Rectangle {
        color: Theme.base
        border.color: Theme.border
        radius: Theme.radius
        clip: true

        ListView {
            id: shortcutList
            objectName: "shortcutList"
            anchors.fill: parent
            anchors.margins: 1
            model: rows
            clip: true
            keyNavigationEnabled: true
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: dialog.title
            Keys.onPressed: event => event.accepted = Theme.pageList(shortcutList, event.key)

            section.property: "group"
            section.delegate: Rectangle {
                id: header
                required property string section
                width: shortcutList.width
                height: 26
                color: Theme.header
                Accessible.role: Accessible.Heading
                Accessible.name: section

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: header.section
                    font.pixelSize: Theme.fontSize
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
            }

            delegate: Rectangle {
                id: row
                required property string keys
                required property string action
                required property int index
                width: shortcutList.width
                implicitHeight: Math.max(Theme.rowHeight, Math.max(keysText.implicitHeight, actionText.implicitHeight) + 8)
                color: index % 2 === 1 ? Theme.alternateBase : Theme.base
                Accessible.role: Accessible.ListItem
                Accessible.name: keys + ": " + action

                Rectangle {
                    anchors.fill: parent
                    z: 1
                    visible: row.ListView.isCurrentItem && shortcutList.activeFocus
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.focus
                    radius: 2
                }

                // Fixed columns (no layout): the row height follows the wrapped texts.
                Item {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10

                    Text {
                        id: keysText
                        width: 200
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.keys
                        font.pixelSize: Theme.fontSize
                        font.family: Theme.monoFamily
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                    Text {
                        id: actionText
                        x: keysText.width + 12
                        width: parent.width - x
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.action
                        font.pixelSize: Theme.fontSize
                        color: Theme.textMuted
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    footer: DialogFooter {
        AppButton {
            text: qsTr("Close")
            onClicked: dialog.close()
        }
    }
}
