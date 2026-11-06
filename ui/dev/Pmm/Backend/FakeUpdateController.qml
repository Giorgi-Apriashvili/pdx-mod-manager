import QtQuick

// Stand-in for UpdateController: pretends a newer version exists, downloads it
// on a timer and stops at "installing". It never touches the network.
// checkDaily() plays the daily check, which the main window's banner offers.
QtObject {
    id: updates

    property FakeNotificationCenter notifications
    property string state: "idle"
    readonly property string currentVersion: "0.1.0"
    property string latestVersion: ""
    property real progress: 0
    property string errorMessage: ""
    property string mode: "installer"
    readonly property bool canCheck: disabledReason.length === 0 && state !== "checking"
                                     && state !== "downloading" && state !== "installing"
    property string disabledReason: ""
    property string installUnavailableReason: ""
    property bool checkedAutomatically: false

    function check() {
        if (!canCheck)
            return
        checkedAutomatically = false
        latestVersion = ""
        errorMessage = ""
        state = "checking"
        checkTimer.restart()
    }

    function checkDaily() {
        check()
        checkedAutomatically = true
    }

    function startUpdate() {
        if (mode !== "installer") {
            openReleasePage()
            return
        }
        errorMessage = ""
        progress = 0
        state = "downloading"
        downloadTimer.restart()
    }

    function cancel() {
        checkTimer.stop()
        downloadTimer.stop()
        progress = 0
        state = state === "downloading" ? "available" : "idle"
    }

    function openReleasePage() {
        notifications.post("info", "Release page", "Would open the release page of version " + latestVersion)
    }

    function retry() {
        if (latestVersion.length > 0)
            startUpdate()
        else
            check()
    }

    property Timer checkTimer: Timer {
        interval: 600
        onTriggered: {
            updates.latestVersion = "0.2.0"
            updates.state = "available"
        }
    }

    property Timer downloadTimer: Timer {
        interval: 100
        repeat: true
        onTriggered: {
            updates.progress = Math.min(1, updates.progress + 0.1)
            if (updates.progress >= 1) {
                stop()
                updates.state = "installing"
            }
        }
    }
}
