pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Pmm.Ui
import Pmm.Backend

// Import a share code: paste or open a file, preview without side effects,
// then create a new playset (default) or explicitly replace an existing one.
AppDialog {
    id: dialog

    readonly property var sharing: Backend.sharing
    readonly property var preview: sharing.preview
    readonly property bool previewOk: sharing.hasPreview && preview.ok === true
    readonly property bool sameGame: previewOk && preview.game === Backend.games.currentKey
    property string mode: "new"
    property string loadedFile
    property bool settingText: false

    title: qsTr("Import share code")
    subtitle: qsTr("Nothing changes until you confirm. Codes contain mod references and order only, never files or paths.")
    preferredWidth: 780
    preferredHeight: 700

    function setCode(text: string) {
        settingText = true
        codeArea.text = text
        settingText = false
    }

    function openWith(text: string) {
        setCode(text)
        open()
        if (text.length > 0)
            runPreview()
    }

    function runPreview() {
        previewTimer.stop()
        const text = codeArea.text.trim()
        if (text.length === 0) {
            sharing.clearPreview()
            return
        }
        loadedFile = ""
        sharing.previewImport(text)
    }

    function confirm() {
        if (!canConfirm())
            return
        const targetId = mode === "replace" ? targetCombo.currentValue : -1
        if (sharing.confirmImport(mode, targetId ?? -1, nameField.text.trim()))
            close()
    }

    function canConfirm(): bool {
        if (!previewOk || sharing.busy)
            return false
        if (mode === "replace")
            return sameGame && targetCombo.currentIndex >= 0
        return nameField.text.trim().length > 0
    }

    // `preview` and `hasPreview` change separately; sync once both settled.
    function syncFromPreview() {
        if (previewOk)
            nameField.text = preview.proposedName || preview.playsetName || ""
        if (!sameGame && mode === "replace")
            mode = "new"
    }

    onAboutToShow: {
        mode = "new"
        targetCombo.currentIndex = targetCombo.indexOfValue(Backend.playsets.activeId)
    }
    onOpened: codeArea.forceActiveFocus()
    onClosed: {
        sharing.clearPreview()
        setCode("")
        loadedFile = ""
    }

    onPreviewChanged: Qt.callLater(syncFromPreview)
    onPreviewOkChanged: Qt.callLater(syncFromPreview)

    Timer {
        id: previewTimer
        interval: 350
        onTriggered: dialog.runPreview()
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open share code file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Share code files (*.txt *.pmm)"), qsTr("All files (*)")]
        onAccepted: {
            previewTimer.stop()
            dialog.setCode("")
            dialog.sharing.importFromFile(selectedFile)
            dialog.loadedFile = Theme.urlToPath(selectedFile)
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        // Input
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: dialog.loadedFile.length > 0 ? qsTr("Loaded from %1").arg(dialog.loadedFile) : qsTr("Share code")
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSize
                font.weight: Font.DemiBold
                color: Theme.text
                elide: Text.ElideMiddle
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            AppButton {
                text: qsTr("Paste from clipboard")
                iconName: "clipboard"
                toolTipText: qsTr("Replace the text below with the clipboard contents")
                onClicked: {
                    dialog.setCode(dialog.sharing.clipboardText())
                    dialog.runPreview()
                }
            }
            AppButton {
                text: qsTr("Open file…")
                iconName: "file"
                toolTipText: qsTr("Read a share code from a text file")
                onClicked: openDialog.open()
            }
        }

        CodeArea {
            id: codeArea
            Layout.fillWidth: true
            Layout.preferredHeight: dialog.sharing.hasPreview ? 64 : 160
            placeholderText: qsTr("Paste a code starting with PMM1. …")
            Accessible.name: qsTr("Share code text")
            onTextChanged: {
                if (!dialog.settingText)
                    previewTimer.restart()
            }
        }

        // Error
        Banner {
            Layout.fillWidth: true
            visible: dialog.sharing.hasPreview && !dialog.previewOk
            level: "error"
            title: dialog.preview.errorTitle ?? ""
            text: dialog.preview.errorMessage ?? ""
        }

        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !dialog.sharing.hasPreview
            verticalAlignment: Text.AlignTop
            text: qsTr("A preview of the playset appears here as soon as a valid code is pasted.")
            font.pixelSize: Theme.fontSize
            color: Theme.textMuted
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // Preview
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: dialog.previewOk
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: qsTr("“%1”").arg(dialog.preview.playsetName ?? "")
                    textFormat: Text.PlainText
                    font.pixelSize: Theme.fontSizeTitle
                    font.weight: Font.DemiBold
                    color: Theme.text
                    elide: Text.ElideRight
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                Chip {
                    text: dialog.preview.gameName ?? ""
                    textColor: Theme.text
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 6

                StatTile { label: qsTr("Total"); value: dialog.preview.total ?? 0 }
                StatTile { label: qsTr("Enabled"); value: dialog.preview.enabled ?? 0 }
                StatTile { label: qsTr("Installed"); value: dialog.preview.installed ?? 0; iconName: "check-circle"; iconColor: Theme.success }
                StatTile {
                    label: qsTr("Missing")
                    value: dialog.preview.missing ?? 0
                    iconName: "download"
                    iconColor: (dialog.preview.missing ?? 0) > 0 ? Theme.info : Theme.textFaint
                }
                StatTile {
                    label: qsTr("Missing enabled")
                    value: dialog.preview.missingEnabled ?? 0
                    iconName: "circle-alert"
                    iconColor: (dialog.preview.missingEnabled ?? 0) > 0 ? Theme.danger : Theme.textFaint
                }
                StatTile {
                    label: qsTr("Missing disabled")
                    value: dialog.preview.missingDisabled ?? 0
                    iconName: "circle-minus"
                    iconColor: Theme.textMuted
                }
                StatTile {
                    label: qsTr("Local, unmapped")
                    value: dialog.preview.localUnmapped ?? 0
                    iconName: "circle-help"
                    iconColor: (dialog.preview.localUnmapped ?? 0) > 0 ? Theme.warning : Theme.textFaint
                }
            }

            Repeater {
                model: Theme.lines(dialog.preview.warnings)
                delegate: MessageLine {
                    required property string modelData
                    Layout.fillWidth: true
                    level: "warning"
                    text: modelData
                }
            }
            MessageLine {
                Layout.fillWidth: true
                level: "info"
                textColor: Theme.textMuted
                text: dialog.sharing.portabilityNote
            }

            // Entries in the imported order
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 90
                color: Theme.base
                border.color: Theme.border
                radius: Theme.radius
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 22
                        color: Theme.header
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            PreviewCell { width: 40; text: qsTr("#"); header: true; alignRight: true }
                            PreviewCell { width: 34; text: qsTr("On"); header: true }
                            PreviewCell { width: entryList.width - 40 - 34 - 90 - 170 - 6; text: qsTr("Name"); header: true }
                            PreviewCell { width: 90; text: qsTr("Source"); header: true }
                            PreviewCell { width: 170; text: qsTr("On this computer"); header: true }
                        }
                    }

                    ListView {
                        id: entryList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        reuseItems: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: dialog.previewOk ? dialog.preview.entries : []
                        ScrollBar.vertical: AppScrollBar {}
                        Accessible.role: Accessible.List
                        Accessible.name: qsTr("Entries in load order")
                        // Read-only, but focusable so the entries can be read and
                        // scrolled with the keyboard.
                        activeFocusOnTab: true
                        keyNavigationEnabled: true
                        currentIndex: -1
                        onActiveFocusChanged: {
                            if (activeFocus && currentIndex < 0 && count > 0)
                                currentIndex = 0
                        }
                        Keys.onPressed: event => event.accepted = Theme.pageList(entryList, event.key)

                        delegate: Rectangle {
                            id: entry
                            required property var modelData
                            required property int index
                            width: entryList.width
                            height: 22
                            color: index % 2 === 1 ? Theme.alternateBase : Theme.base
                            Accessible.role: Accessible.ListItem
                            Accessible.name: qsTr("%1. %2, %3, %4, %5").arg(entry.modelData.position)
                                             .arg(entry.modelData.name)
                                             .arg(entry.modelData.enabled ? qsTr("enabled") : qsTr("disabled"))
                                             .arg(entry.modelData.source === "local" ? qsTr("Local") : qsTr("Workshop"))
                                             .arg(entry.modelData.statusText)

                            Rectangle {
                                anchors.fill: parent
                                z: 1
                                visible: entry.ListView.isCurrentItem && entryList.activeFocus
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.focus
                                radius: 2
                            }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                PreviewCell { width: 40; text: entry.modelData.position; alignRight: true; muted: true }
                                Item {
                                    width: 34
                                    height: parent.height
                                    Icon {
                                        x: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: entry.modelData.enabled ? "check" : "minus"
                                        color: entry.modelData.enabled ? Theme.accent : Theme.textFaint
                                        size: 12
                                    }
                                }
                                PreviewCell {
                                    width: entryList.width - 40 - 34 - 90 - 170 - 6
                                    text: entry.modelData.name
                                    muted: !entry.modelData.enabled
                                }
                                PreviewCell {
                                    width: 90
                                    text: entry.modelData.source === "local" ? qsTr("Local") : qsTr("Workshop")
                                    muted: true
                                }
                                PreviewCell {
                                    width: 170
                                    text: entry.modelData.statusText
                                }
                            }
                        }
                    }
                }
            }

            // Destination
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: destination.implicitHeight + 16
                color: Theme.base
                border.color: Theme.border
                radius: Theme.radius

                GridLayout {
                    id: destination
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 10
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 6

                    AppRadioButton {
                        id: newRadio
                        text: qsTr("Create new playset")
                        checked: dialog.mode === "new"
                        onClicked: dialog.mode = "new"
                    }
                    AppTextField {
                        id: nameField
                        objectName: "importNameField"
                        Layout.fillWidth: true
                        enabled: dialog.mode === "new"
                        placeholderText: qsTr("Playset name")
                        Accessible.name: qsTr("New playset name")
                        onAccepted: dialog.confirm()
                    }

                    AppRadioButton {
                        id: replaceRadio
                        text: qsTr("Replace existing playset")
                        enabled: dialog.sameGame && Backend.playsets.count > 0
                        checked: dialog.mode === "replace"
                        onClicked: dialog.mode = "replace"
                    }
                    AppComboBox {
                        id: targetCombo
                        Layout.fillWidth: true
                        enabled: dialog.mode === "replace"
                        model: Backend.playsets
                        textRole: "name"
                        valueRole: "playsetId"
                        detailRole: "entryCount"
                        accessibleName: qsTr("Playset to replace")
                    }

                    Item {
                        implicitWidth: 1
                        implicitHeight: 1
                        visible: replaceNote.visible
                    }
                    MessageLine {
                        id: replaceNote
                        Layout.fillWidth: true
                        visible: dialog.mode === "replace" || !dialog.sameGame
                        level: dialog.sameGame ? "warning" : "info"
                        text: dialog.sameGame
                              ? qsTr("The entries of “%1” will be replaced. Its previous state is kept and can be restored from History.")
                                .arg(targetCombo.currentText)
                              : qsTr("This code is for %1, so it is imported as a new %1 playset.")
                                .arg(dialog.preview.gameName ?? "")
                    }
                }
            }
        }
    }

    footer: DialogFooter {
        leading: [
            Spinner {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                running: dialog.sharing.busy
            }
        ]
        AppButton {
            text: qsTr("Cancel")
            onClicked: dialog.close()
        }
        AppButton {
            objectName: "importConfirmButton"
            variant: "accent"
            text: dialog.mode === "replace" ? qsTr("Replace playset") : qsTr("Import as new playset")
            enabled: dialog.canConfirm()
            toolTipText: (dialog.preview.missing ?? 0) > 0
                         ? qsTr("Missing Workshop mods (including disabled ones) are queued for download after import")
                         : ""
            onClicked: dialog.confirm()
        }
    }
}
