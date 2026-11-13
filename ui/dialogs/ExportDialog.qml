import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Pmm.Ui
import Pmm.Backend

// Shows the share code of the active playset with copy and save actions, and
// below it the playset's Workshop ID list (enabled Workshop mods only).
AppDialog {
    id: dialog

    // exportCode() starts the export; the code arrives in lastExportCode and
    // the list in workshopIdList.
    readonly property string code: Backend.sharing.lastExportCode
    readonly property var list: Backend.sharing.workshopIdList
    readonly property string listText: list.text ?? ""

    title: qsTr("Export share code")
    subtitle: qsTr("“%1” · %2 · %3 enabled")
              .arg(Backend.playsets.activeName)
              .arg(Theme.plural(Backend.mods.count, qsTr("mod"), qsTr("mods")))
              .arg(Theme.formatCount(Backend.mods.enabledCount))
    preferredWidth: 640

    onAboutToShow: Backend.sharing.exportCode()
    onOpened: {
        page.contentY = 0
        copyButton.forceActiveFocus()
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save share code")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTr("Text files (*.txt)"), qsTr("All files (*)")]
        onAccepted: Backend.sharing.exportToFile(selectedFile)
    }

    // In a short window the whole content scrolls; Tab reveals the focused
    // control (Main.qml).
    contentItem: Flickable {
        id: page
        objectName: "exportScroller"
        implicitHeight: content.implicitHeight
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: content
            // Room for the scroll bar while there is one.
            width: page.width - (page.interactive ? 12 : 0)
            spacing: 8

            CodeArea {
                id: codeArea
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                readOnly: true
                text: dialog.code
                placeholderText: Backend.sharing.busy ? qsTr("Preparing the share code…") : ""
                Accessible.name: qsTr("Share code")
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("The code lists every entry of the playset, including disabled ones, in load order. "
                           + "It contains mod references only: no files, paths or account data.")
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSize
                color: Theme.textMuted
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            MessageLine {
                Layout.fillWidth: true
                level: "info"
                textColor: Theme.textMuted
                text: Backend.sharing.portabilityNote
            }

            // Workshop ID list
            SectionHeader {
                Layout.fillWidth: true
                Layout.topMargin: 6
                text: qsTr("Workshop ID list")
            }
            CodeArea {
                id: listArea
                objectName: "workshopIdListField"
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                readOnly: true
                text: dialog.listText
                placeholderText: Backend.sharing.busy ? qsTr("Preparing the list…") : ""
                Accessible.name: qsTr("Workshop ID list")
                Accessible.description: dialog.list.note ?? ""
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    objectName: "workshopIdListNote"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    text: dialog.list.note ?? ""
                    textFormat: Text.PlainText
                    font.pixelSize: Theme.fontSize
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                AppButton {
                    objectName: "copyWorkshopIdListButton"
                    Layout.alignment: Qt.AlignTop
                    text: qsTr("Copy Workshop ID list")
                    iconName: "copy"
                    enabled: dialog.listText.length > 0
                    toolTipText: qsTr("Only the enabled Steam Workshop mods, in load order, separated by |")
                    onClicked: Backend.sharing.copyWorkshopIdList()
                }
            }
        }
    }

    footer: DialogFooter {
        leading: [
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: qsTr("%1 characters").arg(Theme.formatCount(dialog.code.length))
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textMuted
                Accessible.role: Accessible.StaticText
                Accessible.name: text
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
            objectName: "copyShareCodeButton"
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
