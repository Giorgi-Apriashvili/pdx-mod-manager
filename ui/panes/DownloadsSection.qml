pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Workshop acquisition: provider status with recovery text, job list with
// progress and per-job recovery, and bulk retry/cancel; "Check for mod
// updates" (the Steam Web API, only on request) with its result line, and
// "Update mods" while mods of the playset have an update.
ColumnLayout {
    id: root

    // "Re-download from scratch…" on a failed update: Main.qml asks first.
    signal reinstallJobRequested(int jobId, string name)

    readonly property var acquisition: Backend.acquisition
    readonly property bool providerReady: acquisition.providerStatus === "ready"
    readonly property int jobCount: jobList.count
    readonly property int updateCount: Backend.mods.updateCount
    readonly property string checkText: acquisition.checkingUpdates
        ? qsTr("Asking the Steam Workshop about this playset's Workshop mods…")
        : acquisition.updateCheckSummary
    // The update action that had the focus last (null: none).
    property Item focusedAction: null

    spacing: 6

    // The result of a check is scrolled into view, and screen readers hear it
    // without a focus change.
    function announceCheck() {
        if (checkLine.visible && checkText.length > 0) {
            Theme.reveal(checkRow)
            checkLine.Accessible.announce(checkLine.Accessible.name,
                                          acquisition.updateCheckFailed ? Accessible.Assertive : Accessible.Polite)
        }
        keepFocus()
    }

    // "Update mods" goes once its updates are queued, and Retry while a check
    // runs: the focus they had moves to "Check for mod updates".
    function keepFocus() {
        if (focusedAction === null || focusedAction.visible)
            return
        focusedAction = null
        if (checkButton.visible && checkButton.enabled)
            checkButton.forceActiveFocus(Qt.TabFocusReason)
    }

    // Remembers an update action that has the focus until the focus moves on
    // (not when the action is hidden, which takes the focus too).
    function trackFocus(action: Item) {
        if (action.activeFocus)
            focusedAction = action
        else if (action.visible && focusedAction === action)
            focusedAction = null
    }

    Connections {
        target: root.acquisition
        function onUpdateCheckFinished() { Qt.callLater(root.announceCheck) }
    }

    function providerText(status: string): string {
        switch (status) {
        case "ready": return qsTr("Steam is running and signed in")
        case "steam_not_running": return qsTr("Steam is not running")
        case "not_logged_on": return qsTr("Steam is not signed in")
        case "not_owned": return qsTr("This Steam account does not own %1").arg(Backend.games.currentName)
        case "unavailable": return qsTr("Automatic downloads are unavailable")
        default: return qsTr("Steam status unknown")
        }
    }

    SectionHeader {
        Layout.fillWidth: true
        text: qsTr("Downloads")

        Spinner {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            running: root.acquisition.active
            size: 12
            Accessible.name: qsTr("Downloads in progress")
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        visible: root.providerReady || root.acquisition.providerStatus === "unknown"
        Accessible.role: Accessible.StaticText
        Accessible.name: providerLine.text

        Icon {
            name: root.providerReady ? "check-circle" : "circle-help"
            color: root.providerReady ? Theme.success : Theme.textMuted
            size: 13
        }
        Text {
            id: providerLine
            Layout.fillWidth: true
            text: root.providerText(root.acquisition.providerStatus)
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSize
            color: Theme.textMuted
            elide: Text.ElideRight
        }
    }

    Banner {
        Layout.fillWidth: true
        visible: !root.providerReady && root.acquisition.providerStatus !== "unknown"
        level: "warning"
        title: root.providerText(root.acquisition.providerStatus)
        text: root.acquisition.providerMessage
    }

    // Progress summary; screen readers hear it when it changes.
    Text {
        Layout.fillWidth: true
        visible: root.acquisition.summary.length > 0
        text: root.acquisition.summary
        textFormat: Text.PlainText
        font.pixelSize: Theme.fontSize
        color: Theme.text
        wrapMode: Text.Wrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    Text {
        objectName: "downloadsEmptyText"
        Layout.fillWidth: true
        visible: root.jobCount === 0
        text: {
            if (Backend.playsets.activeId < 0)
                return qsTr("No downloads.")
            const updates = root.updateCount === 0 ? ""
                : root.updateCount === 1 ? qsTr("1 mod has an update on the Workshop.")
                : qsTr("%1 mods have an update on the Workshop.").arg(Theme.formatCount(root.updateCount))
            if (Backend.mods.missingCount > 0)
                return qsTr("%1 referenced mods are not installed.").arg(Theme.formatCount(Backend.mods.missingCount))
                       + (updates.length > 0 ? " " + updates : "")
            return updates.length > 0 ? updates : qsTr("No downloads. Everything in this playset is installed.")
        }
        textFormat: Text.PlainText
        font.pixelSize: Theme.fontSize
        color: Theme.textMuted
        wrapMode: Text.Wrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    ListView {
        id: jobList
        objectName: "jobList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 250)
        visible: count > 0
        model: root.acquisition.jobs
        clip: true
        spacing: 4
        reuseItems: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Download jobs")

        delegate: JobDelegate {
            width: jobList.width - (jobList.contentHeight > jobList.height ? 10 : 0)
            onReinstallRequested: (jobId, name) => root.reinstallJobRequested(jobId, name)
        }
    }

    Flow {
        Layout.fillWidth: true
        visible: Backend.mods.missingCount > 0 || root.jobCount > 0 || root.acquisition.active
                 || Backend.mods.installedWorkshopCount > 0
        spacing: 4

        AppButton {
            text: qsTr("Download missing")
            iconName: "download"
            visible: Backend.mods.missingCount > 0
            // Steam's state is only known once a session was opened, which this starts.
            enabled: root.acquisition.downloadsEnabled
            toolTipText: qsTr("Subscribe to and download every missing Workshop mod of this playset, including disabled ones")
            onClicked: root.acquisition.acquireMissing()
        }
        // Not "Check for updates": that is the application's own update check in Settings.
        AppButton {
            id: checkButton
            objectName: "checkModUpdatesButton"
            text: qsTr("Check for mod updates")
            iconName: "search"
            visible: Backend.mods.installedWorkshopCount > 0
            // Off without Steam (--no-steam, read-only): nothing is sent then.
            enabled: root.acquisition.downloadsEnabled
            toolTipText: qsTr("Ask the Steam Workshop whether the installed Workshop mods of this playset, including disabled ones, have newer versions. Sends only their Workshop IDs.")
            onClicked: root.acquisition.checkForUpdates()
        }
        AppButton {
            id: updateButton
            objectName: "updateModsButton"
            text: qsTr("Update mods")
            iconName: "circle-arrow-up"
            visible: root.updateCount > 0
            enabled: root.acquisition.downloadsEnabled
            toolTipText: root.updateCount === 1
                ? qsTr("Ask Steam to download the new version of the mod with a Workshop update, also if it is disabled")
                : qsTr("Ask Steam to download the new versions of the %1 mods with a Workshop update, including disabled ones")
                  .arg(Theme.formatCount(root.updateCount))
            onClicked: root.acquisition.updateMods()
            onActiveFocusChanged: root.trackFocus(updateButton)
            onVisibleChanged: if (!visible) Qt.callLater(root.keepFocus)
        }
        AppButton {
            text: qsTr("Retry all failed")
            iconName: "refresh"
            visible: root.jobCount > 0
            toolTipText: qsTr("Queue every failed download again")
            onClicked: root.acquisition.retryAllFailed()
        }
        AppButton {
            text: qsTr("Cancel")
            iconName: "x"
            visible: root.acquisition.active
            toolTipText: qsTr("Stop queued downloads. Subscriptions and downloads that already completed stay in Steam.")
            onClicked: root.acquisition.cancelAll()
        }
        AppButton {
            text: qsTr("Rescan")
            iconName: "search"
            visible: root.jobCount > 0
            toolTipText: qsTr("Check the Workshop folder now for downloads finished in Steam")
            onClicked: root.acquisition.rescanNow()
        }
    }

    // What "Check for mod updates" is doing, or what it found.
    RowLayout {
        id: checkRow
        Layout.fillWidth: true
        visible: root.checkText.length > 0
        spacing: 6
        onVisibleChanged: if (visible && root.acquisition.checkingUpdates) Qt.callLater(Theme.reveal, checkRow)

        MessageLine {
            id: checkLine
            objectName: "updateCheckLine"
            Layout.fillWidth: true
            level: root.acquisition.updateCheckFailed && !root.acquisition.checkingUpdates ? "warning" : "info"
            iconName: root.acquisition.checkingUpdates ? "clock" : Theme.levelIcon(level)
            text: root.checkText
        }
        LinkButton {
            id: retryCheck
            objectName: "updateCheckRetry"
            Layout.alignment: Qt.AlignTop
            visible: root.acquisition.updateCheckFailed && !root.acquisition.checkingUpdates
            enabled: root.acquisition.downloadsEnabled
            text: qsTr("Retry")
            Accessible.name: qsTr("Check for mod updates again")
            toolTipText: qsTr("Ask the Steam Workshop again about the mods it did not answer for")
            onClicked: root.acquisition.checkForUpdates()
            onActiveFocusChanged: root.trackFocus(retryCheck)
            onVisibleChanged: if (!visible) Qt.callLater(root.keepFocus)
        }
    }

    MessageLine {
        Layout.fillWidth: true
        visible: root.acquisition.downloadsEnabled
        level: "info"
        textColor: Theme.textMuted
        text: qsTr("While downloads run, Steam shows you as playing %1.").arg(Backend.games.currentName)
    }
}
