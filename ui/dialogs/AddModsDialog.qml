pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Pick installed mods (Backend.inventory) to append to the active playset.
AppDialog {
    id: dialog

    objectName: "addModsDialog"

    property var picked: ({})
    property int pickedCount: 0

    title: qsTr("Add mods")
    subtitle: qsTr("Installed mods of %1. Added mods go to the end of the load order, enabled.")
              .arg(Backend.games.currentName)
    preferredWidth: 680
    preferredHeight: 600

    function toggle(key: string, inPlayset: bool) {
        if (inPlayset)
            return
        // A new object: the rows' bindings only update when the property changes.
        const p = Object.assign({}, picked)
        if (p[key])
            delete p[key]
        else
            p[key] = true
        picked = p
        pickedCount = Object.keys(p).length
    }

    function submit() {
        const keys = Object.keys(picked)
        if (keys.length === 0)
            return
        Backend.mods.addMods(keys)
        close()
    }

    onAboutToShow: {
        picked = {}
        pickedCount = 0
        filterField.text = Backend.inventory.filter
        Backend.inventory.refresh()
    }
    onOpened: filterField.forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            AppTextField {
                id: filterField
                Layout.fillWidth: true
                iconName: "search"
                clearable: true
                placeholderText: qsTr("Filter by name or ID")
                Accessible.name: qsTr("Filter installed mods by name or ID")
                onTextEdited: filterDebounce.restart()
                onCleared: Backend.inventory.filter = ""
                Keys.onDownPressed: inventoryList.forceActiveFocus(Qt.TabFocusReason)
                Keys.onReturnPressed: inventoryList.forceActiveFocus(Qt.TabFocusReason)
                Keys.onEnterPressed: inventoryList.forceActiveFocus(Qt.TabFocusReason)
            }
            AppButton {
                iconName: "refresh"
                toolTipText: qsTr("Rescan installed mods")
                Accessible.name: qsTr("Refresh")
                onClicked: Backend.inventory.refresh()
            }

            Timer {
                id: filterDebounce
                interval: 160
                onTriggered: Backend.inventory.filter = filterField.text
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.base
            border.color: Theme.border
            radius: Theme.radius
            clip: true

            // Column titles aligned with InventoryRow.
            Rectangle {
                id: inventoryHeader
                x: 1
                y: 1
                width: parent.width - 2
                height: 22
                color: Theme.header

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 30
                    anchors.rightMargin: 10
                    spacing: 8

                    Repeater {
                        model: [
                            { title: qsTr("Name"), width: -1 },
                            { title: qsTr("Version"), width: 60 },
                            { title: qsTr("Source"), width: 64 },
                            { title: qsTr("State"), width: 110 }
                        ]
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: modelData.width < 0
                            Layout.preferredWidth: modelData.width < 0 ? -1 : modelData.width
                            text: modelData.title
                            textFormat: Text.PlainText
                            font.pixelSize: Theme.fontSizeSmall
                            font.weight: Font.DemiBold
                            color: Theme.textMuted
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            ListView {
                id: inventoryList
                anchors.fill: parent
                anchors.topMargin: inventoryHeader.height + 1
                anchors.leftMargin: 1
                anchors.rightMargin: 1
                anchors.bottomMargin: 1
                model: Backend.inventory
                reuseItems: true
                clip: true
                currentIndex: -1
                keyNavigationEnabled: true
                activeFocusOnTab: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                objectName: "inventoryList"
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Installed mods")
                Accessible.description: qsTr("Space selects or deselects the current mod")

                onActiveFocusChanged: {
                    if (activeFocus && currentIndex < 0 && count > 0)
                        currentIndex = 0
                }
                Keys.onSpacePressed: {
                    const item = currentItem as InventoryRow
                    if (item)
                        dialog.toggle(item.model.identityKey, item.model.inPlayset)
                }
                Keys.onPressed: event => event.accepted = Theme.pageList(inventoryList, event.key)

                delegate: InventoryRow {
                    width: inventoryList.width
                    checked: dialog.picked[model.identityKey] === true
                    isCurrent: ListView.isCurrentItem && inventoryList.activeFocus
                    onToggleRequested: {
                        inventoryList.currentIndex = index
                        dialog.toggle(model.identityKey, model.inPlayset)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: inventoryList.count === 0
                    text: Backend.inventory.filter.length > 0 ? qsTr("No installed mod matches the filter.")
                                                              : qsTr("No installed mods were found. Try Rescan.")
                    textFormat: Text.PlainText
                    font.pixelSize: Theme.fontSize
                    color: Theme.textMuted
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
        }
    }

    footer: DialogFooter {
        leading: [
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: qsTr("%1 installed · %2 selected").arg(Theme.formatCount(Backend.inventory.count))
                                                             .arg(Theme.formatCount(dialog.pickedCount))
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textMuted
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        ]
        AppButton {
            text: qsTr("Cancel")
            onClicked: dialog.close()
        }
        AppButton {
            variant: "accent"
            text: dialog.pickedCount > 0 ? qsTr("Add %1").arg(Theme.plural(dialog.pickedCount, qsTr("mod"), qsTr("mods")))
                                         : qsTr("Add")
            enabled: dialog.pickedCount > 0
            onClicked: dialog.submit()
        }
    }
}
