import QtQuick
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// One acquisition job: state glyph and text, progress, error and recovery.
Rectangle {
    id: job

    required property var model

    readonly property string jobState: model.state
    readonly property real progress: model.progress
    readonly property bool active: jobState === "downloading" || jobState === "subscribing"
                                   || jobState === "subscribed" || jobState === "download_requested"
                                   || jobState === "pending"
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
            Layout.row: 0
            Layout.column: 2
            Layout.alignment: Qt.AlignTop
            visible: job.model.canRetry
            text: qsTr("Retry")
            implicitHeight: 22
            toolTipText: qsTr("Try this download again")
            Accessible.name: qsTr("Retry download of %1").arg(job.model.name)
            onClicked: Backend.acquisition.retry(job.model.jobId)
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
    }
}
