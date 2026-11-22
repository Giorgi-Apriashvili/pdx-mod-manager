import QtQuick
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// One acquisition job: state glyph and text, progress, error and recovery.
// `kind` says what it does: "acquire" (a missing mod), "update" or
// "reinstall" (a re-download from scratch of an installed mod).
Rectangle {
    id: job

    required property var model

    // "Re-download from scratch…" on a failed update: Main.qml asks first.
    signal reinstallRequested(int jobId, string name)
    // Retry or "Re-download from scratch…" went away with the focus (the job
    // moved on): DownloadsSection moves the focus.
    signal actionHidden()

    readonly property string jobState: model.state
    readonly property string kind: model.kind ?? "acquire"
    readonly property real progress: model.progress
    readonly property bool active: jobState === "downloading" || jobState === "subscribing"
                                   || jobState === "subscribed" || jobState === "download_requested"
                                   || jobState === "pending" || jobState === "unsubscribing"
    readonly property string glyph: {
        switch (jobState) {
        case "installed": return "check-circle"
        case "downloading": return "download"
        case "failed": return "circle-x"
        case "unavailable": return "ban"
        case "needs_user_action": return "circle-help"
        case "cancelled": return "circle-minus"
        default: return "clock"
        }
    }
    readonly property color glyphColor: {
        switch (jobState) {
        case "installed": return Theme.success
        case "downloading": return Theme.info
        case "failed":
        case "unavailable": return Theme.danger
        case "needs_user_action": return Theme.warning
        default: return Theme.textMuted
        }
    }

    implicitHeight: layout.implicitHeight + 12
    radius: Theme.radius
    color: Theme.base
    border.color: Theme.divider

    Accessible.role: Accessible.ListItem
    Accessible.name: model.name + ", " + model.stateText
    Accessible.description: model.errorMessage

    // Also when the focus comes back to an action already hidden (a dialog
    // that asked about it closes).
    function checkAction(action: var) {
        if (action.visible) {
            action.focusHeld = action.activeFocus
        } else if (action.activeFocus || action.focusHeld) {
            action.focusHeld = false
            job.actionHidden()
        }
    }

    // The first visible action of this job takes the focus; false when it has none.
    function focusFirstAction(): bool {
        for (const action of [retryButton, fallbackLink, reinstallLink]) {
            if (action.visible && action.enabled) {
                action.forceActiveFocus(Qt.TabFocusReason)
                return true
            }
        }
        return false
    }

    GridLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 7
        anchors.rightMargin: 7
        columns: 3
        columnSpacing: 6
        rowSpacing: 3

        Icon {
            Layout.row: 0
            Layout.column: 0
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            name: job.glyph
            color: job.glyphColor
            size: 13
        }
        ColumnLayout {
            Layout.row: 0
            Layout.column: 1
            Layout.fillWidth: true
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: job.model.name
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSize
                color: Theme.text
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: job.model.stateText
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSizeSmall
                color: job.jobState === "failed" || job.jobState === "unavailable" ? Theme.danger : Theme.textMuted
                elide: Text.ElideRight
            }
        }
        AppButton {
            id: retryButton
            objectName: "jobRetryButton"
            property bool focusHeld: false
            Layout.row: 0
            Layout.column: 2
            Layout.alignment: Qt.AlignTop
            visible: job.model.canRetry
            text: qsTr("Retry")
            implicitHeight: 22
            toolTipText: job.kind === "update" ? qsTr("Ask Steam for this update again")
                       : job.kind === "reinstall" ? qsTr("Try this re-download again")
                       : qsTr("Try this download again")
            Accessible.name: job.kind === "update" ? qsTr("Retry update of %1").arg(job.model.name)
                           : job.kind === "reinstall" ? qsTr("Retry re-download of %1").arg(job.model.name)
                           : qsTr("Retry download of %1").arg(job.model.name)
            onClicked: Backend.acquisition.retry(job.model.jobId)
            onActiveFocusChanged: job.checkAction(retryButton)
            onVisibleChanged: job.checkAction(retryButton)
        }

        AppProgressBar {
            Layout.row: 1
            Layout.column: 1
            Layout.columnSpan: 2
            Layout.fillWidth: true
            visible: job.active
            indeterminate: job.progress < 0
            value: Math.max(0, job.progress)
            barHeight: 4
            Accessible.name: qsTr("%1: %2").arg(job.model.name).arg(job.model.stateText)
        }

        Text {
            Layout.row: 2
            Layout.column: 1
            Layout.columnSpan: 2
            Layout.fillWidth: true
            visible: job.model.errorMessage.length > 0
            text: job.model.errorMessage
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.text
            wrapMode: Text.Wrap
        }

        LinkButton {
            id: fallbackLink
            Layout.row: 3
            Layout.column: 1
            Layout.columnSpan: 2
            visible: job.model.canOpenFallback
            text: qsTr("Open in Steam (manual fallback)")
            Accessible.name: qsTr("Open %1 in Steam (manual fallback)").arg(job.model.name)
            font.pixelSize: Theme.fontSizeSmall
            toolTipText: qsTr("Manual step: opens the Workshop page in the Steam client so you can subscribe yourself. "
                              + "The manager detects the download when Steam finishes it.")
            onClicked: Backend.acquisition.openFallback(job.model.jobId)
        }

        LinkButton {
            id: reinstallLink
            objectName: "jobReinstallLink"
            property bool focusHeld: false
            Layout.row: 4
            Layout.column: 1
            Layout.columnSpan: 2
            visible: job.model.canReinstall ?? false
            enabled: Backend.acquisition.downloadsEnabled
            text: qsTr("Re-download from scratch…")
            Accessible.name: qsTr("Re-download %1 from scratch…").arg(job.model.name)
            font.pixelSize: Theme.fontSizeSmall
            toolTipText: qsTr("Steam unsubscribes the mod, which removes its files, then it is subscribed again and downloaded. Asks first.")
            onClicked: job.reinstallRequested(job.model.jobId, job.model.name)
            onActiveFocusChanged: job.checkAction(reinstallLink)
            onVisibleChanged: job.checkAction(reinstallLink)
        }
    }
}
