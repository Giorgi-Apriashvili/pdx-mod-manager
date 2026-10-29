import QtQuick
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Bottom of the right column: recovery, blocking conditions, Play and diagnostics.
Rectangle {
    id: root

    readonly property var launcher: Backend.launcher
    readonly property bool hasGame: Backend.games.currentSupported && Backend.games.currentInstalled
    readonly property bool hasPlayset: Backend.playsets.activeId >= 0
    // The backend always explains why Play is unavailable (game not located,
    // no active playset, mods not ready, game or launcher running, ...).
    readonly property list<string> blockerLines: {
        const lines = Theme.lines(launcher.blockers)
        if (lines.length > 0 || Backend.games.currentName.length > 0)
            return lines
        return [qsTr("Select a supported game first.")]
    }
    readonly property bool playable: launcher.canPlay && !launcher.busy && hasGame && hasPlayset
    readonly property list<string> warningLines: Theme.lines(launcher.warnings)
    readonly property int maxBlockers: 4

    function stateText(state: string): string {
        switch (state) {
        case "validating": return qsTr("Checking installation and mods…")
        case "applying": return qsTr("Writing game configuration…")
        case "starting": return qsTr("Starting %1…").arg(Backend.games.currentName)
        case "started": return qsTr("%1 is running with this playset.").arg(Backend.games.currentName)
        case "failed": return qsTr("Launch failed.")
        default: return ""
        }
    }

    implicitHeight: layout.implicitHeight + 20
    color: Theme.pane

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.divider
    }

    ColumnLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        spacing: 6

        Banner {
            Layout.fillWidth: true
            visible: root.launcher.hasInterruptedTransaction
            level: "error"
            actionsBelow: true
            title: qsTr("Interrupted configuration change")
            text: qsTr("A previous launch stopped while writing game files. Recover to restore the files to their previous state.")
            AppButton {
                text: qsTr("Recover")
                iconName: "wrench"
                onClicked: root.launcher.recoverInterrupted()
            }
        }

        Banner {
            Layout.fillWidth: true
            visible: root.launcher.state === "failed" && root.launcher.lastError.length > 0
            level: "error"
            actionsBelow: true
            title: qsTr("Could not start the game")
            text: root.launcher.lastError
                  + (root.launcher.lastErrorDetail.length > 0 ? "\n" + root.launcher.lastErrorDetail : "")
            LinkButton {
                text: qsTr("Copy diagnostics")
                onClicked: root.launcher.copyDiagnostics()
            }
        }

        Repeater {
            model: root.blockerLines.slice(0, root.maxBlockers)
            delegate: MessageLine {
                required property string modelData
                Layout.fillWidth: true
                level: "error"
                iconName: "ban"
                text: modelData
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.blockerLines.length > root.maxBlockers
            text: qsTr("…and %1 more").arg(root.blockerLines.length - root.maxBlockers)
            leftPadding: 19
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textMuted
        }
        Repeater {
            model: root.blockerLines.length === 0 ? root.warningLines.slice(0, 2) : []
            delegate: MessageLine {
                required property string modelData
                Layout.fillWidth: true
                level: "warning"
                text: modelData
            }
        }

        AppButton {
            id: playButton
            objectName: "playButton"
            Layout.fillWidth: true
            implicitHeight: Theme.playHeight
            variant: "accent"
            large: true
            iconName: "play"
            text: Backend.games.currentName.length > 0 ? qsTr("Play %1").arg(Backend.games.currentName) : qsTr("Play")
            font.pixelSize: 14
            enabled: root.playable
            toolTipText: root.playable
                         ? qsTr("Apply this playset and start the game directly, without the Paradox launcher")
                         : root.blockerLines.length > 0 ? root.blockerLines.join("\n") : ""
            Accessible.description: root.playable ? qsTr("Starts the game with this playset")
                                                          : qsTr("Blocked: %1").arg(root.blockerLines.join("; "))
            onClicked: root.launcher.play()

            Spinner {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                running: root.launcher.busy
                color: Theme.accentText
                Accessible.ignored: true
            }
        }

        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.stateText(root.launcher.state)
            font.pixelSize: Theme.fontSizeSmall
            color: root.launcher.state === "failed" ? Theme.danger : Theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
            // Launch progress is spoken as it happens; failures interrupt.
            onTextChanged: {
                if (text.length > 0)
                    Accessible.announce(text, root.launcher.state === "failed" ? Accessible.Assertive
                                                                               : Accessible.Polite)
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 12

            LinkButton {
                visible: root.launcher.canRestoreBackup
                text: qsTr("Restore pre-launch configuration")
                font.pixelSize: Theme.fontSizeSmall
                toolTipText: qsTr("Put back the game configuration files as they were before the last launch from this app")
                onClicked: root.launcher.restoreLastBackup()
            }
            LinkButton {
                text: qsTr("Copy diagnostics")
                font.pixelSize: Theme.fontSizeSmall
                toolTipText: qsTr("Copy a redacted report (no user names or personal paths) to the clipboard")
                onClicked: root.launcher.copyDiagnostics()
            }
        }
    }
}
