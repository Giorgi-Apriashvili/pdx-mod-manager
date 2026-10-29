import QtQuick
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Playset management: create, duplicate, rename, delete (undoable), history.
ColumnLayout {
    id: root

    signal newRequested()
    signal duplicateRequested()
    signal renameRequested()
    signal deleteRequested()
    signal historyRequested()

    readonly property bool hasPlayset: Backend.playsets.activeId >= 0

    spacing: 5

    SectionHeader {
        Layout.fillWidth: true
        text: qsTr("Playset")

        AppButton {
            variant: "flat"
            iconName: "history"
            implicitWidth: 22
            implicitHeight: 20
            toolTipText: qsTr("History: restore deleted or replaced playsets")
            Accessible.name: qsTr("Playset history")
            onClicked: root.historyRequested()
        }
        AppButton {
            id: moreButton
            variant: "flat"
            iconName: "more"
            implicitWidth: 22
            implicitHeight: 20
            toolTipText: qsTr("More playset actions")
            Accessible.name: qsTr("More playset actions")
            onClicked: moreMenu.popup(moreButton, 0, moreButton.height + 2)
        }
    }

    Text {
        Layout.fillWidth: true
        text: root.hasPlayset ? Backend.playsets.activeName : qsTr("No playset selected")
        font.pixelSize: Theme.fontSizeLarge
        font.weight: Font.DemiBold
        color: root.hasPlayset ? Theme.text : Theme.textMuted
        elide: Text.ElideRight
    }
    Text {
        Layout.fillWidth: true
        visible: root.hasPlayset
        text: qsTr("%1 · %2 enabled · %3 for %4")
              .arg(Theme.plural(Backend.mods.count, qsTr("mod"), qsTr("mods")))
              .arg(Theme.formatCount(Backend.mods.enabledCount))
              .arg(Theme.plural(Backend.playsets.count, qsTr("playset"), qsTr("playsets")))
              .arg(Backend.games.currentName)
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.textMuted
        elide: Text.ElideRight
    }

    // Text-only so all four fit on one row in the narrowest right column.
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 2
        spacing: 4

        AppButton {
            Layout.fillWidth: true
            leftPadding: 8
            rightPadding: 8
            text: qsTr("New…")
            toolTipText: qsTr("Create an empty playset (Ctrl+N)")
            onClicked: root.newRequested()
        }
        AppButton {
            Layout.fillWidth: true
            leftPadding: 8
            rightPadding: 8
            text: qsTr("Duplicate…")
            enabled: root.hasPlayset
            toolTipText: qsTr("Copy the active playset under a new name")
            onClicked: root.duplicateRequested()
        }
        AppButton {
            Layout.fillWidth: true
            leftPadding: 8
            rightPadding: 8
            text: qsTr("Rename…")
            enabled: root.hasPlayset
            toolTipText: qsTr("Rename the active playset")
            onClicked: root.renameRequested()
        }
        AppButton {
            Layout.fillWidth: true
            leftPadding: 8
            rightPadding: 8
            text: qsTr("Delete")
            enabled: root.hasPlayset
            toolTipText: qsTr("Delete the active playset. It is kept in History and can be restored.")
            onClicked: root.deleteRequested()
        }
    }

    Banner {
        Layout.fillWidth: true
        Layout.topMargin: 2
        visible: Backend.playsets.canRestore
        level: "info"
        iconName: "history"
        text: qsTr("The deleted playset can be restored.")
        LinkButton {
            text: qsTr("Restore")
            toolTipText: qsTr("Restore the most recently deleted playset (replaced ones are in History)")
            onClicked: Backend.playsets.restoreLast()
        }
    }

    AppMenu {
        id: moreMenu

        AppMenuItem {
            text: qsTr("New from game configuration")
            iconName: "import"
            onTriggered: Backend.games.importCurrentConfiguration()
        }
        AppMenuItem {
            text: qsTr("History…")
            iconName: "history"
            onTriggered: root.historyRequested()
        }
    }
}
