pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Pmm.Ui
import Pmm.Backend

// Import a share code or a Workshop ID list: paste or open a file, preview
// without side effects, then create a new playset (default) or explicitly
// replace an existing one. A list is for the selected game. The names of
// missing Workshop mods may arrive in a later update of the same preview
// (same generation), which keeps the typed name and the list's position.
AppDialog {
    id: dialog

    readonly property var sharing: Backend.sharing
    readonly property var preview: sharing.preview
    readonly property bool previewOk: sharing.hasPreview && preview.ok === true
    readonly property bool sameGame: previewOk && preview.game === Backend.games.currentKey
    property string mode: "new"
    property string loadedFile
    property bool settingText: false
    // The preview the name field and the entry list were last filled from.
    property real syncedGeneration: -1

    title: qsTr("Import share code or Workshop ID list")
    subtitle: qsTr("Nothing changes until you confirm. Codes and lists contain mod references and order only, never files or paths.")
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
        const replace = mode === "replace"
        const targetId = replace ? targetCombo.currentValue : -1
        // A replaced playset keeps its name; the field is the new playset's.
        if (sharing.confirmImport(mode, targetId ?? -1, replace ? "" : nameField.text.trim()))
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
    // A new preview proposes its name; an update of the same one (the names
    // of missing mods) keeps a typed name, the current entry and the scroll
    // position.
    function syncFromPreview() {
        const update = previewOk && preview.generation === syncedGeneration
        const index = entryList.currentIndex
        const contentY = entryList.contentY
        entryList.model = previewOk ? preview.entries : []
        if (update) {
            entryList.currentIndex = Math.min(index, entryList.count - 1)
            entryList.contentY = contentY
        } else {
            syncedGeneration = previewOk ? preview.generation : -1
            if (previewOk)
                nameField.text = preview.proposedName || preview.playsetName || ""
        }
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
        syncedGeneration = -1
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
        title: qsTr("Open share code or Workshop ID list")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Share codes and ID lists (*.txt *.pmm)"), qsTr("All files (*)")]
        onAccepted: {
            previewTimer.stop()
            dialog.setCode("")
            dialog.sharing.importFromFile(selectedFile)
            dialog.loadedFile = Theme.urlToPath(selectedFile)
        }
    }

    // Fills the dialog; in a short window the entries keep their minimum height
    // and the whole preview scrolls.
    contentItem: Flickable {
        id: page
        contentWidth: width
        contentHeight: content.height
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: content
            // Room for the scroll bar while there is one.
            width: page.width - (page.interactive ? 12 : 0)
            height: Math.max(page.height, implicitHeight)
            spacing: 8

            // Input
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: dialog.loadedFile.length > 0 ? qsTr("Loaded from %1").arg(dialog.loadedFile) : qsTr("Share code or Workshop ID list")
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
                    toolTipText: qsTr("Read a share code or Workshop ID list from a text file")
                    onClicked: openDialog.open()
                }
            }

            CodeArea {
                id: codeArea
                Layout.fillWidth: true
                Layout.preferredHeight: dialog.sharing.hasPreview ? 64 : 160
                placeholderText: qsTr("Paste a share code (PMM1.…) or Workshop IDs, e.g. 3818297502|3149445431")
                Accessible.name: qsTr("Share code or Workshop ID list text")
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
                text: qsTr("A preview of the playset appears here as soon as a valid share code or Workshop ID list is pasted. "
                           + "Workshop IDs and Steam Workshop links are imported for the game selected in the window.")
                textFormat: Text.PlainText
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
                        objectName: "importPreviewTitle"
                        Layout.fillWidth: true
                        // A list has no name of its own.
                        text: dialog.preview.format === "idList" ? qsTr("Workshop ID list")
                                                                 : qsTr("“%1”").arg(dialog.preview.playsetName ?? "")
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
                    objectName: "importListNote"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    level: "info"
                    text: dialog.preview.listNote ?? ""
                }
                MessageLine {
                    objectName: "importLookupLine"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    level: dialog.preview.lookup === "failed" ? "warning" : "info"
                    text: dialog.preview.lookupText ?? ""
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
                            objectName: "importEntryList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            reuseItems: true
                            boundsBehavior: Flickable.StopAtBounds
                            // Set by syncFromPreview(), which keeps the position
                            // across an update of the same preview.
                            model: []
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
                                // The name comes from the text or from Steam: substituted
                                // last, so a "%3" in it stays as it is.
                                Accessible.name: qsTr("%1. %5, %2, %3, %4").arg(entry.modelData.position)
                                                 .arg(entry.modelData.enabled ? qsTr("enabled") : qsTr("disabled"))
                                                 .arg(entry.modelData.source === "local" ? qsTr("Local") : qsTr("Workshop"))
                                                 .arg(entry.modelData.statusText)
                                                 .arg(entry.modelData.name)

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
            toolTipText: (dialog.preview.missing ?? 0) === 0 ? ""
                         : dialog.preview.downloads
                           ? qsTr("Missing Workshop mods (including disabled ones) are queued for download after import")
                           : qsTr("Downloads are off in this session, so missing Workshop mods are not queued")
            onClicked: dialog.confirm()
        }
    }
}
