pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Snapshots taken before destructive playset changes; each can be restored.
AppDialog {
    id: dialog

    property var entries: []

    title: qsTr("Playset history")
    subtitle: qsTr("Snapshots saved before playsets of %1 were deleted, replaced or had mods removed.")
              .arg(Backend.games.currentName)
    preferredWidth: 560

    onAboutToShow: entries = Backend.playsets.history()

    contentItem: Rectangle {
        implicitHeight: Math.max(120, Math.min(historyList.contentHeight + 2, 420))
        color: Theme.base
        border.color: Theme.border
        radius: Theme.radius
        clip: true

        ListView {
            id: historyList
            anchors.fill: parent
            anchors.margins: 1
            model: dialog.entries
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("History snapshots")

            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index
                width: historyList.width
                implicitHeight: 44
                color: index % 2 === 1 ? Theme.alternateBase : Theme.base
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.playsetName + ", " + modelData.reason + ", " + modelData.createdText

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    spacing: 10

                    Icon {
                        name: "history"
                        color: Theme.textMuted
                        size: 14
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            Layout.fillWidth: true
                            text: entry.modelData.playsetName
                            textFormat: Text.PlainText
                            font.pixelSize: Theme.fontSize
                            font.weight: Font.DemiBold
                            color: Theme.text
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: entry.modelData.reason + " · " + entry.modelData.createdText
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textMuted
                            elide: Text.ElideRight
                        }
                    }
                    AppButton {
                        text: qsTr("Restore")
                        toolTipText: qsTr("Restore this snapshot as a playset")
                        onClicked: {
                            if (Backend.playsets.restoreHistory(entry.modelData.historyId))
                                dialog.close()
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                visible: historyList.count === 0
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("No snapshots yet. They are created automatically before destructive changes.")
                font.pixelSize: Theme.fontSize
                color: Theme.textMuted
                wrapMode: Text.Wrap
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
