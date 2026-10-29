import QtQuick
import QtQuick.Layouts
import Pmm.Ui

// Generic confirmation; `onConfirmed` runs the action.
AppDialog {
    id: dialog

    property string message
    property string detail
    property string confirmText: qsTr("OK")
    property bool destructive: false
    property var action: null

    preferredWidth: 420

    function ask(title: string, message: string, detail: string, confirmText: string,
                 destructive: bool, action: var) {
        dialog.title = title
        dialog.message = message
        dialog.detail = detail
        dialog.confirmText = confirmText
        dialog.destructive = destructive
        dialog.action = action
        open()
    }

    onOpened: confirmButton.forceActiveFocus()
    onClosed: action = null

    contentItem: ColumnLayout {
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: dialog.message
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSize
            color: Theme.text
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            visible: dialog.detail.length > 0
            text: dialog.detail
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSize
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }

    footer: DialogFooter {
        AppButton {
            text: qsTr("Cancel")
            onClicked: dialog.close()
        }
        AppButton {
            id: confirmButton
            objectName: "confirmButton"
            variant: dialog.destructive ? "danger" : "accent"
            text: dialog.confirmText
            // The question is read with the focused button.
            Accessible.description: dialog.message
            onClicked: {
                const run = dialog.action
                dialog.close()
                if (typeof run === "function")
                    run()
            }
        }
    }
}
