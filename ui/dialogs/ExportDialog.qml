import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import Pmm.Ui
import Pmm.Backend

// Shows the share code of the active playset with copy and save actions.
AppDialog {
    id: dialog

    property string code

    title: qsTr("Export share code")
    subtitle: qsTr("“%1” · %2 · %3 enabled")
              .arg(Backend.playsets.activeName)
              .arg(Theme.plural(Backend.mods.count, qsTr("mod"), qsTr("mods")))
              .arg(Theme.formatCount(Backend.mods.enabledCount))
    preferredWidth: 640

    onAboutToShow: code = Backend.sharing.exportCode()
    onOpened: copyButton.forceActiveFocus()

    FileDialog {
        id: saveDialog
        title: qsTr("Save share code")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTr("Text files (*.txt)"), qsTr("All files (*)")]
        onAccepted: Backend.sharing.exportToFile(selectedFile)
    }

    contentItem: ColumnLayout {
        spacing: 8

        CodeArea {
            id: codeArea
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            readOnly: true
            text: dialog.code
            Accessible.name: qsTr("Share code")
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("The code lists every entry of the playset, including disabled ones, in load order. "
                       + "It contains mod references only: no files, paths or account data.")
            font.pixelSize: Theme.fontSize
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
        MessageLine {
            Layout.fillWidth: true
            level: "info"
            textColor: Theme.textMuted
            text: Backend.sharing.portabilityNote
        }
    }

    footer: DialogFooter {
        leading: [
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: qsTr("%1 characters").arg(Theme.formatCount(dialog.code.length))
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textMuted
            }
        ]
        AppButton {
            text: qsTr("Save to file…")
            iconName: "save"
            enabled: dialog.code.length > 0
            onClicked: saveDialog.open()
        }
        AppButton {
            id: copyButton
            variant: "accent"
            text: qsTr("Copy")
            iconName: "copy"
            enabled: dialog.code.length > 0
            onClicked: Backend.sharing.copyExportCode()
        }
        AppButton {
            text: qsTr("Close")
            onClicked: dialog.close()
        }
    }
}
