import QtQuick
import QtQuick.Layouts
import Pmm.Ui

// Asks for a playset name (new, duplicate, rename).
AppDialog {
    id: dialog

    property string label: qsTr("Name")
    property string acceptText: qsTr("OK")
    property string initialName
    readonly property string name: field.text.trim()

    signal submitted(string name)

    preferredWidth: 380

    function ask(title: string, acceptText: string, initialName: string) {
        dialog.title = title
        dialog.acceptText = acceptText
        dialog.initialName = initialName
        open()
    }

    function submit() {
        if (name.length === 0)
            return
        submitted(name)
        close()
    }

    onAboutToShow: field.text = initialName
    onOpened: {
        field.forceActiveFocus()
        field.selectAll()
    }

    contentItem: ColumnLayout {
        spacing: 6

        Text {
            text: dialog.label
            font.pixelSize: Theme.fontSize
            color: Theme.textMuted
        }
        AppTextField {
            id: field
            Layout.fillWidth: true
            maximumLength: 120
            placeholderText: qsTr("Playset name")
            Accessible.name: qsTr("Playset name")
            onAccepted: dialog.submit()
        }
    }

    footer: DialogFooter {
        AppButton {
            text: qsTr("Cancel")
            onClicked: dialog.close()
        }
        AppButton {
            variant: "accent"
            text: dialog.acceptText
            enabled: dialog.name.length > 0
            onClicked: dialog.submit()
        }
    }
}
