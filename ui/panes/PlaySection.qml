import QtQuick
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Bottom of the right column: recovery, blocking conditions, Play and diagnostics.
Rectangle {
    id: root

    // "Discard this record": Main.qml asks for confirmation first.
    signal discardRequested()
    // Recover or Discard went away while it had the focus (with the banner, or
    // Discard once pressed); like the other banners, focus goes to the mod list.
    signal focusReleased()

    readonly property var launcher: Backend.launcher
    readonly property bool hasGame: Backend.games.currentSupported && Backend.games.currentInstalled
    // Without a playset Play starts the game without mods; a warning says so.
    readonly property bool hasPlayset: Backend.playsets.activeId >= 0
    // The backend always explains why Play is unavailable (game not located,
    // mods not ready, game or launcher running, Steam not running, ...).
    readonly property list<string> blockerLines: {
        const lines = Theme.lines(launcher.blockers)
        if (lines.length > 0 || Backend.games.currentName.length > 0)
            return lines
        return [qsTr("Select a supported game first.")]
    }
    readonly property bool playable: launcher.canPlay && !launcher.busy && hasGame
    readonly property list<string> warningLines: Theme.lines(launcher.warnings)
    readonly property int maxBlockers: 4

    // Enter, Space or a click on Play while it cannot start the game: the
    // reasons as a notification, which screen readers announce. Never launches.
    function explainBlocked() {
        if (launcher.busy)
            return // the state line below says what is happening
        Backend.notifications.post("warning", qsTr("The game cannot be started yet"), blockerLines.join("\n"))
    }

    function checkBannerFocus() {
        if ((recoverButton.activeFocus && !recoverButton.visible)
                || (discardButton.activeFocus && !discardButton.visible))
            root.focusReleased()
    }

    function stateText(state: string): string {
        switch (state) {
        case "validating": return qsTr("Checking installation and mods…")
        case "applying": return qsTr("Writing game configuration…")
        case "starting": return qsTr("Starting %1…").arg(Backend.games.currentName)
        // What the launch used, not the current selection.
        case "started": return root.launcher.startedWithPlayset ? qsTr("%1 is running with this playset.").arg(Backend.games.currentName)
                                                                 : qsTr("%1 is running without mods.").arg(Backend.games.currentName)
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
            objectName: "interruptedBanner"
            Layout.fillWidth: true
            visible: root.launcher.hasInterruptedTransaction
            level: "error"
            actionsBelow: true
            title: qsTr("Interrupted configuration change")
            // Discarding is offered once Recover has failed for good.
            text: root.launcher.canDiscardInterrupted
                  ? qsTr("A previous launch stopped while writing game files, and Recover cannot undo it because a backup is missing or damaged. Discard its record to launch with the files as they are now.")
                  : qsTr("A previous launch stopped while writing game files. Recover to restore the files to their previous state.")
            AppButton {
                id: recoverButton
                text: qsTr("Recover")
                iconName: "wrench"
                onClicked: root.launcher.recoverInterrupted()
                onVisibleChanged: Qt.callLater(root.checkBannerFocus)
            }
            LinkButton {
                id: discardButton
                objectName: "discardInterruptedButton"
                anchors.verticalCenter: parent.verticalCenter
                visible: root.launcher.canDiscardInterrupted
                text: qsTr("Discard this record")
                toolTipText: qsTr("Stop recovering this change and leave its files as they are now (asks first)")
                onClicked: root.discardRequested()
                onVisibleChanged: Qt.callLater(root.checkBannerFocus)
                // The confirmation gives the focus back to it, maybe once it is hidden.
                onActiveFocusChanged: Qt.callLater(root.checkBannerFocus)
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
            textFormat: Text.PlainText
            leftPadding: 19
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textMuted
            Accessible.role: Accessible.StaticText
            Accessible.name: text
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
            // Stays focusable while it cannot start the game (aria-disabled):
            // it looks disabled, its name and description say why, and Enter,
            // Space or a click show the reasons instead of launching. It also
            // keeps the focus while a launch it started runs.
            available: root.playable
            // While blocked, the reasons are on screen just above, where a tool
            // tip would cover them: it lists only those behind "…and N more".
            toolTipText: root.launcher.busy ? ""
                         : !root.playable ? root.blockerLines.slice(root.maxBlockers).join("\n")
                         : root.hasPlayset ? qsTr("Apply this playset and start the game directly, without the Paradox launcher")
                                           : qsTr("Start the game directly, without mods and without the Paradox launcher")
            // A launch it started keeps the focus here: "starting", not
            // "unavailable", is what a screen reader says after the key press.
            Accessible.name: root.playable ? text
                             : root.launcher.busy ? qsTr("%1, starting").arg(text)
                             : qsTr("%1, unavailable").arg(text)
            Accessible.description: root.launcher.busy ? (root.stateText(root.launcher.state) || qsTr("A launch is in progress."))
                                    : !root.playable ? qsTr("Blocked: %1").arg(root.blockerLines.join("; "))
                                    : root.hasPlayset ? qsTr("Starts the game with this playset")
                                                      : qsTr("Starts the game without mods")
            onClicked: {
                if (root.playable)
                    root.launcher.play()
                else
                    root.explainBlocked()
            }

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
            id: stateLine
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.stateText(root.launcher.state)
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSizeSmall
            color: root.launcher.state === "failed" ? Theme.danger : Theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
            // Launch progress is spoken as it happens; failures interrupt.
            onTextChanged: {
                if (stateLine.text.length > 0)
                    Accessible.announce(stateLine.text, root.launcher.state === "failed" ? Accessible.Assertive
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
