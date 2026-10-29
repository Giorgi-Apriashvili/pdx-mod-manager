import QtQuick

// Stand-in for LaunchController; blockers follow the fake mod list.
QtObject {
    id: launcher

    property FakeNotificationCenter notifications
    property FakeModListModel mods
    readonly property bool canPlay: blockers.length === 0 && !busy
    readonly property list<string> blockers: mods && mods.blockingCount > 0
        ? [mods.blockingCount + (mods.blockingCount === 1 ? " enabled mod is" : " enabled mods are")
           + " not ready (missing, downloading or broken). Disable them or wait for downloads."]
        : []
    readonly property list<string> warnings: mods && mods.warningCount > 0
        ? [mods.warningCount + " mods report compatibility warnings."] : []
    property bool busy: false
    property string state: "idle"
    property string lastError
    property string lastErrorDetail
    property bool hasInterruptedTransaction: false
    property bool canRestoreBackup: true

    signal launched(int pid)
    signal launchFailed(string message)

    function play() {
        if (!canPlay)
            return
        busy = true
        state = "validating"
        stepTimer.restart()
    }

    function recoverInterrupted(): bool {
        hasInterruptedTransaction = false
        notifications.post("info", "Configuration recovered", "The game configuration was restored to its state before the interrupted launch.")
        return true
    }

    function restoreLastBackup(): bool {
        notifications.post("info", "Configuration restored", "dlc_load.json was restored from the pre-launch backup.")
        return true
    }

    function diagnosticsText(): string {
        return "Paradox Mod Manager diagnostics (preview)\nGame: stellaris 4.5.2\nPlayset entries: " + (mods ? mods.count : 0)
    }

    function copyDiagnostics() {
        notifications.post("info", "Diagnostics copied", "Paths and user names are redacted.")
    }

    property Timer stepTimer: Timer {
        interval: 700
        repeat: true
        onTriggered: {
            if (launcher.state === "validating") {
                launcher.state = "applying"
            } else if (launcher.state === "applying") {
                launcher.state = "starting"
            } else {
                launcher.state = "started"
                launcher.busy = false
                stop()
                launcher.launched(4242)
            }
        }
    }
}
