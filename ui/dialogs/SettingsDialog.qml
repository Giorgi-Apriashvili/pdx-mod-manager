pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Appearance, updates and version information.
AppDialog {
    id: dialog

    title: qsTr("Settings")
    preferredWidth: 440
    height: Math.min(implicitHeight, parent ? parent.height - 32 : implicitHeight)

    signal shortcutsRequested()

    readonly property string updateState: Backend.updates.state
    readonly property string updateMode: Backend.updates.mode

    // One line for the screen and for screen readers.
    readonly property string updateStatusText: {
        switch (updateState) {
        case "checking":
            return qsTr("Checking GitHub for a new version…")
        case "upToDate":
            return qsTr("Paradox Mod Manager is up to date.")
        case "noRelease":
            return qsTr("No release has been published yet.")
        case "available":
            return qsTr("Version %1 is available.").arg(Backend.updates.latestVersion)
        case "downloading":
            return Backend.updates.progress < 0
                ? qsTr("Downloading version %1…").arg(Backend.updates.latestVersion)
                : qsTr("Downloading version %1… %2 %").arg(Backend.updates.latestVersion)
                                                           .arg(Math.round(Backend.updates.progress * 100))
        case "ready":
            return qsTr("Version %1 is downloaded and verified.").arg(Backend.updates.latestVersion)
        case "installing":
            return qsTr("Starting the installer. The application will close and restart.")
        default:
            return ""
        }
    }

    // The section's main button; it has none in the other states.
    readonly property string updateActionText: {
        switch (updateState) {
        case "checking":
        case "downloading":
            return qsTr("Cancel")
        case "available":
            return updateMode === "installer" ? qsTr("Update and restart")
                                              : qsTr("Open release page")
        case "ready":
            return qsTr("Install and restart")
        case "failed":
            return qsTr("Try again")
        default:
            return ""
        }
    }

    // Announced per state, not per percent of the download.
    Connections {
        target: Backend.updates
        function onStateChanged() {
            Qt.callLater(dialog.keepUpdateFocus)
            Qt.callLater(dialog.announceUpdate)
        }
    }

    function announceUpdate() {
        const text = Backend.updates.errorMessage.length > 0 ? Backend.updates.errorMessage
                                                             : updateStatusText
        if (opened && text.length > 0)
            updateStatus.Accessible.announce(text)
    }

    // The update button that was just used may be gone or disabled now: keep
    // the keyboard in the section unless focus already is on another control.
    function keepUpdateFocus() {
        if (!opened)
            return
        const focused = ApplicationWindow.window?.activeFocusItem ?? null
        if (focused && focused.activeFocusOnTab && focused.visible && focused.enabled)
            return
        if (updateAction.visible && updateAction.enabled)
            updateAction.forceActiveFocus(Qt.TabFocusReason)
        else if (checkButton.enabled)
            checkButton.forceActiveFocus(Qt.TabFocusReason)
    }

    // Keyboard focus starts on the selected theme option.
    onOpened: {
        for (let i = 0; i < themeRepeater.count; ++i) {
            const radio = themeRepeater.itemAt(i) as AppRadioButton
            if (radio && radio.checked)
                radio.forceActiveFocus(Qt.TabFocusReason)
        }
    }

    // Scrolls when the section's messages make it taller than a small window.
    contentItem: Flickable {
        id: scroller
        implicitHeight: content.implicitHeight
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: content
            x: 3 // room for the focus rings, and for the scroll bar on the other side
            width: scroller.width - 11
            spacing: 8

                SectionHeader {
                    Layout.fillWidth: true
                    text: qsTr("Appearance")
                }

                ButtonGroup {
                    id: themeGroup
                }
                Repeater {
                    id: themeRepeater
                    model: [
                        { value: "system", text: qsTr("Follow system") },
                        { value: "light", text: qsTr("Light") },
                        { value: "dark", text: qsTr("Dark") }
                    ]
                    delegate: AppRadioButton {
                        required property var modelData
                        text: modelData.text
                        ButtonGroup.group: themeGroup
                        checked: Backend.settings.theme === modelData.value
                        onClicked: Backend.settings.theme = modelData.value
                    }
                }

                SectionHeader {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    text: qsTr("Updates")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Current version: %1").arg(Backend.updates.currentVersion)
                        textFormat: Text.PlainText
                        font.pixelSize: Theme.fontSize
                        color: Theme.text
                        elide: Text.ElideRight
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                    AppButton {
                        id: checkButton
                        objectName: "updateCheckButton"
                        text: qsTr("Check for updates")
                        enabled: Backend.updates.canCheck
                        onClicked: Backend.updates.check()
                    }
                }
                MessageLine {
                    Layout.fillWidth: true
                    objectName: "updateDisabledReason"
                    visible: Backend.updates.disabledReason.length > 0
                    level: "info"
                    text: Backend.updates.disabledReason
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: dialog.updateStatusText.length > 0
                    spacing: 6

                    Spinner {
                        running: dialog.updateState === "checking" || dialog.updateState === "installing"
                        size: 12
                        Accessible.ignored: true
                    }
                    Text {
                        id: updateStatus
                        objectName: "updateStatus"
                        Layout.fillWidth: true
                        text: dialog.updateStatusText
                        textFormat: Text.PlainText
                        font.pixelSize: Theme.fontSize
                        color: Theme.text
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                }
                AppProgressBar {
                    Layout.fillWidth: true
                    objectName: "updateProgress"
                    visible: dialog.updateState === "downloading"
                    from: 0
                    to: 1
                    value: Math.max(0, Backend.updates.progress)
                    indeterminate: Backend.updates.progress < 0
                    Accessible.name: qsTr("Download progress")
                }
                MessageLine {
                    Layout.fillWidth: true
                    objectName: "updateError"
                    visible: Backend.updates.errorMessage.length > 0
                    level: dialog.updateState === "failed" ? "error" : "warning"
                    text: Backend.updates.errorMessage
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: dialog.updateActionText.length > 0
                    spacing: 8

                    AppButton {
                        id: updateAction
                        objectName: "updateAction"
                        variant: dialog.updateState === "available" || dialog.updateState === "ready"
                                 ? "accent" : "normal"
                        text: dialog.updateActionText
                        onClicked: {
                            switch (dialog.updateState) {
                            case "checking":
                            case "downloading":
                                Backend.updates.cancel()
                                break
                            case "available":
                            case "ready":
                                Backend.updates.startUpdate()
                                break
                            case "failed":
                                Backend.updates.retry()
                                break
                            }
                        }
                    }
                    LinkButton {
                        id: releasePageLink
                        objectName: "updateReleasePage"
                        // After a failure the page is the way around it.
                        visible: dialog.updateState === "failed"
                        text: qsTr("Open release page")
                        onClicked: Backend.updates.openReleasePage()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Checking contacts GitHub only when you ask, and sends nothing about you. "
                               + "The installer is verified against the checksum published with the "
                               + "release before it runs.")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                SectionHeader {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    text: qsTr("About")
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 3

                    DetailLabel { id: appKey; text: qsTr("Application") }
                    DetailValue { labelText: appKey.text; text: qsTr("Paradox Mod Manager %1").arg(Backend.appVersion) }
                    DetailLabel { id: protocolKey; text: qsTr("Share-code protocol") }
                    DetailValue {
                        labelText: protocolKey.text
                        text: qsTr("Version %1 (PMM%1 codes)").arg(Backend.protocolVersion)
                    }
                    DetailLabel { id: platformKey; text: qsTr("Platform") }
                    DetailValue {
                        labelText: platformKey.text
                        text: Qt.platform.os === "windows" ? "Windows" : Qt.platform.os === "osx" ? "macOS"
                            : Qt.platform.os === "linux" ? "Linux" : Qt.platform.os
                    }
                    DetailLabel { id: dataKey; text: qsTr("Data folder") }
                    DetailValue {
                        labelText: dataKey.text
                        text: Backend.dataDir
                        selectable: true
                        elideMode: Text.ElideMiddle
                        toolTipText: Backend.dataDir
                    }
                    DetailLabel { id: logsKey; text: qsTr("Logs") }
                    DetailValue {
                        labelText: logsKey.text
                        text: Backend.logsDir
                        selectable: true
                        elideMode: Text.ElideMiddle
                        toolTipText: Backend.logsDir
                    }
                }
                LinkButton {
                    text: qsTr("Open logs folder")
                    onClicked: Backend.openLogsFolder()
                }
                LinkButton {
                    text: qsTr("Keyboard shortcuts (F1)")
                    onClicked: {
                        dialog.close()
                        dialog.shortcutsRequested()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: qsTr("No account, server or telemetry. Playsets are stored locally on this computer.")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
        }
    }

    footer: DialogFooter {
        leading: [
            LinkButton {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: qsTr("Copy diagnostics")
                toolTipText: qsTr("Copy a redacted report to the clipboard")
                onClicked: Backend.launcher.copyDiagnostics()
            }
        ]
        AppButton {
            variant: "accent"
            text: qsTr("Close")
            onClicked: {
                Backend.settings.save()
                dialog.close()
            }
        }
    }
}
