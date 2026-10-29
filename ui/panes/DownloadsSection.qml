pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Workshop acquisition: provider status with recovery text, job list with
// progress and per-job recovery, and bulk retry/cancel.
ColumnLayout {
    id: root

    readonly property var acquisition: Backend.acquisition
    readonly property bool providerReady: acquisition.providerStatus === "ready"
    readonly property int jobCount: jobList.count

    spacing: 6

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
        font.pixelSize: Theme.fontSize
        color: Theme.text
        wrapMode: Text.Wrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    Text {
        Layout.fillWidth: true
        visible: root.jobCount === 0
        text: Backend.playsets.activeId < 0 ? qsTr("No downloads.")
            : Backend.mods.missingCount > 0
              ? qsTr("%1 referenced mods are not installed.").arg(Theme.formatCount(Backend.mods.missingCount))
              : qsTr("No downloads. Everything in this playset is installed.")
        font.pixelSize: Theme.fontSize
        color: Theme.textMuted
        wrapMode: Text.Wrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    ListView {
        id: jobList
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
        }
    }

    Flow {
        Layout.fillWidth: true
        visible: Backend.mods.missingCount > 0 || root.jobCount > 0 || root.acquisition.active
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

    MessageLine {
        Layout.fillWidth: true
        visible: root.acquisition.downloadsEnabled
        level: "info"
        textColor: Theme.textMuted
        text: qsTr("While downloads run, Steam shows you as playing %1.").arg(Backend.games.currentName)
    }
}
