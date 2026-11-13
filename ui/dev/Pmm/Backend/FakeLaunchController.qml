import QtQuick

// Stand-in for LaunchController; blockers follow the selected game (not
// found comes first, as in the real one), steamRunning (set it to false to
// preview the Steam blocker) and the fake mod list. Without a playset nothing else blocks: the game
// starts without mods. Set hasInterruptedTransaction to preview the Recover
// banner, and backupMissing as well to make Recover fail and offer Discard.
QtObject {
    id: launcher

    property FakeNotificationCenter notifications
    property FakeModListModel mods
    property FakePlaysetsModel playsets
    property FakeGamesModel games
    property bool steamRunning: true
    // More reasons after these, e.g. more than the Play section lists.
    property list<string> extraBlockers: []
    readonly property bool canPlay: blockers.length === 0 && !busy
    readonly property list<string> blockers: (games && games.currentName.length > 0 && !games.currentInstalled
            ? [games.currentName + " has not been found. Rescan, or locate the game folder."]
            : [])
        .concat(steamRunning ? [] : ["Steam is not running. Start Steam and sign in, then launch Stellaris."])
        .concat(mods && mods.blockingCount > 0
            ? [mods.blockingCount + (mods.blockingCount === 1 ? " enabled mod is" : " enabled mods are")
               + " not ready (missing, downloading or broken). Disable them or wait for downloads."]
            : [])
        .concat(extraBlockers)
    readonly property list<string> warnings: playsets && playsets.activeId < 0
        ? ["No playset is selected, so the game starts without mods."]
        : mods && mods.warningCount > 0 ? [mods.warningCount + " mods report compatibility warnings."] : []
    property bool busy: false
    property string state: "idle"
    property string lastError
    property string lastErrorDetail
    property bool startedWithPlayset: false
    property bool hasInterruptedTransaction: false
    property bool backupMissing: false
    property bool canRestoreBackup: true
    property list<string> discardableFiles: []
    property list<string> discardFolders: []
    readonly property bool canDiscardInterrupted: discardableFiles.length > 0

    // Launches started (tests check that a blocked Play starts none).
    property int playCount: 0

    signal launched(int pid)
    signal launchFailed(string message)

    function play() {
        if (busy)
            return
        // As the real one: never launches while blocked, and says why.
        if (!canPlay) {
            notifications.post("warning", "The game cannot be started yet", blockers.join("\n"))
            return
        }
        ++playCount
        busy = true
        // What the launch uses, read while "started" is shown.
        startedWithPlayset = !!playsets && playsets.activeId >= 0
        state = "validating"
        stepTimer.restart()
    }

    function recoverInterrupted(): bool {
        if (backupMissing) {
            discardableFiles = ["C:\\Users\\Player\\Documents\\Paradox Interactive\\Stellaris\\dlc_load.json"]
            discardFolders = ["C:\\Users\\Player\\AppData\\Roaming\\ParadoxModManager\\transactions\\20261107T101500000Z-0000"]
            notifications.post("error", "Recovery incomplete", "The backup of \"dlc_load.json\" is missing, so nothing was restored.")
            return true
        }
        hasInterruptedTransaction = false
        notifications.post("info", "Configuration recovered", "The game configuration was restored to its state before the interrupted launch.")
        return true
    }

    function discardInterrupted(): bool {
        if (!canDiscardInterrupted)
            return false
        notifications.post("info", "Interrupted change discarded", "Its files were left as they are now. Its record and remaining backups stay in " + discardFolders.join(", ") + " until later launches make it old enough to be cleaned up.")
        hasInterruptedTransaction = false
        backupMissing = false
        discardableFiles = []
        discardFolders = []
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
