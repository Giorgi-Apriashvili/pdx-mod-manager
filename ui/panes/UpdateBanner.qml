pragma ComponentBehavior: Bound

import QtQuick
import Pmm.Ui
import Pmm.Backend

// The daily update check's offer above the columns (docs/updates.md). It never
// takes focus and blocks nothing; nothing is downloaded before its button is
// pressed. It then follows that update: progress, a held-back reason, errors.
// "Not now" hides it for the rest of the session.
Banner {
    id: root

    property bool dismissed: false
    readonly property string updateState: Backend.updates.state
    readonly property bool installs: Backend.updates.mode === "installer"

    // The focused button went away with the banner.
    signal focusReleased()

    function dismiss() {
        dismissed = true
        focusReleased()
    }

    visible: !dismissed && Backend.updates.checkedAutomatically
             && ["available", "downloading", "ready", "installing", "failed"].indexOf(updateState) >= 0
    level: updateState === "failed" ? "error" : "info"
    iconName: updateState === "failed" ? Theme.levelIcon("error") : "download"
    // Once per step, not per percent: the progress is not part of the text.
    announced: true
    title: qsTr("Paradox Mod Manager %1 is available.").arg(Backend.updates.latestVersion)
    text: {
        if (Backend.updates.errorMessage.length > 0)
            return Backend.updates.errorMessage
        switch (updateState) {
        case "downloading":
            return qsTr("Downloading and verifying the installer…")
        case "ready":
            return qsTr("The installer is downloaded and verified.")
        case "installing":
            return qsTr("Starting the installer. The application will close and restart.")
        default:
            return Backend.updates.installUnavailableReason
        }
    }

    readonly property string actionText: {
        switch (updateState) {
        case "available":
            return installs ? qsTr("Update and restart") : qsTr("Open release page")
        case "downloading":
            return qsTr("Cancel")
        case "ready":
            return qsTr("Install and restart")
        case "failed":
            return qsTr("Try again")
        default:
            return ""
        }
    }

    AppProgressBar {
        objectName: "updateBannerProgress"
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        visible: root.updateState === "downloading"
        from: 0
        to: 1
        value: Math.max(0, Backend.updates.progress)
        indeterminate: Backend.updates.progress < 0
        Accessible.name: qsTr("Download progress")
    }
    AppButton {
        objectName: "updateBannerAction"
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        visible: root.actionText.length > 0
        variant: root.updateState === "available" || root.updateState === "ready" ? "accent" : "normal"
        text: root.actionText
        onClicked: {
            switch (root.updateState) {
            case "available":
            case "ready":
                Backend.updates.startUpdate()
                break
            case "downloading":
                Backend.updates.cancel()
                break
            case "failed":
                Backend.updates.retry()
                break
            }
        }
    }
    LinkButton {
        objectName: "updateBannerDismiss"
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        visible: root.updateState === "available" || root.updateState === "ready"
                 || root.updateState === "failed"
        text: qsTr("Not now")
        toolTipText: qsTr("Hide this message. Settings → Updates still offers the update.")
        onClicked: root.dismiss()
    }
}
