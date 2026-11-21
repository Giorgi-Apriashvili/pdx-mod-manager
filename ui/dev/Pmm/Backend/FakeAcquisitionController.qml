import QtQuick

// Stand-in for AcquisitionController with a static job list, and the updates
// of installed Workshop mods: "Check for mod updates" answers after a short
// delay from the fake mod list (set workshopReachable to false to preview a
// check that could not reach the Workshop), and "Update mods", "Update from
// Workshop" and "Re-download from scratch" turn rows and jobs into updates.
// Set downloadsEnabled to false to preview a session without Steam.
QtObject {
    id: acquisition

    property FakeNotificationCenter notifications
    property FakeModListModel mods
    property FakePlaysetsModel playsets
    property bool active: true
    property string summary
    property string providerStatus: "ready"
    property string providerMessage
    property bool downloadsEnabled: true
    property string gameKey: "stellaris"
    property ListModel jobs: ListModel {}

    // "Check for mod updates": the playset being checked (-1: none) and the
    // results by playset id.
    property int checkingPlayset: -1
    property var checks: ({})
    property bool workshopReachable: true
    readonly property int activePlayset: playsets ? playsets.activeId : -1
    readonly property bool checkingUpdates: checkingPlayset >= 0 && checkingPlayset === activePlayset
    readonly property var activeCheck: checks[activePlayset] ?? null
    readonly property string updateCheckSummary: activeCheck ? activeCheck.summary : ""
    readonly property bool updateCheckFailed: activeCheck ? activeCheck.failed : false
    // Checks answered (tests).
    property int checkCount: 0

    signal updateCheckFinished()

    readonly property string offTitle: "Workshop downloads are off"
    readonly property string offText: "This session was started with --no-steam or in read-only mode, so nothing is subscribed or downloaded. Restart Paradox Mod Manager normally to download mods."

    readonly property var sampleJobs: [
        { jobId: 11, name: "Stellar Ship Designs Redux", providerId: "2900000011", kind: "acquire", state: "downloading",
          stateText: "Downloading 45 % (12.4 of 27.6 MB)", progress: 0.45, errorMessage: "", errorCode: "",
          canRetry: false, canOpenFallback: false, canReinstall: false },
        { jobId: 12, name: "Diverse Event Chains", providerId: "2900000012", kind: "acquire", state: "download_requested",
          stateText: "Waiting for Steam", progress: -1, errorMessage: "", errorCode: "",
          canRetry: false, canOpenFallback: false, canReinstall: false },
        { jobId: 13, name: "Hidden Relics Plus", providerId: "2900000013", kind: "acquire", state: "failed",
          stateText: "Failed after 3 attempts", progress: -1,
          errorMessage: "Steam reported a timeout. Check your connection and retry.", errorCode: "timeout",
          canRetry: true, canOpenFallback: true, canReinstall: false },
        { jobId: 14, name: "Ancient Civics [Legacy]", providerId: "2900000014", kind: "acquire", state: "unavailable",
          stateText: "Unavailable", progress: -1,
          errorMessage: "The item was removed from the Workshop or is private.", errorCode: "item_unavailable",
          canRetry: false, canOpenFallback: true, canReinstall: false },
        { jobId: 15, name: "Glorious Edicts Lite", providerId: "2900000015", kind: "update", state: "failed",
          stateText: "Failed", progress: -1,
          errorMessage: "Steam has not picked up this update yet. Steam checks for updates about every 45 minutes; restarting Steam makes it check right away.",
          errorCode: "update_not_seen", canRetry: true, canOpenFallback: false, canReinstall: true }
    ]

    // Only the Stellaris preview has queued downloads.
    function reload() {
        jobs.clear()
        const sample = gameKey === "stellaris"
        if (sample)
            jobs.append(sampleJobs)
        active = sample
        summary = sample ? "1 downloading · 1 queued · 2 failed" : ""
    }

    onGameKeyChanged: reload()
    Component.onCompleted: reload()

    function refuseWithoutSteam(): bool {
        if (downloadsEnabled)
            return false
        notifications.post("warning", offTitle, offText)
        return true
    }

    function acquireMissing() {
        if (refuseWithoutSteam())
            return
        notifications.post("info", "Downloads queued", "4 missing mods were queued.")
    }

    function retry(jobId: int) {
        if (refuseWithoutSteam())
            return
        notifications.post("info", "Retrying", "Job " + jobId + " was queued again.")
    }

    function retryAllFailed() {
        if (refuseWithoutSteam())
            return
        notifications.post("info", "Retrying failed downloads", "2 jobs were queued again.")
    }

    function cancelAll() {
        if (refuseWithoutSteam())
            return
        active = false
        notifications.post("warning", "Downloads cancelled",
                           "Queued downloads were stopped. Mods that already finished subscribing or downloading stay in Steam.")
    }

    function openFallback(jobId: int) {
        if (refuseWithoutSteam())
            return
        notifications.post("info", "Opened in Steam", "Subscribe on the Workshop page; the manager picks it up when the download completes.")
    }

    function rescanNow() {
        notifications.post("info", "Rescanning", "Checking the Workshop folder for finished downloads.")
    }

    // --- Updates of installed Workshop mods ------------------------------------

    function checkForUpdates() {
        if (refuseWithoutSteam() || !playsets || playsets.activeId < 0 || checkingPlayset >= 0)
            return
        checkingPlayset = playsets.activeId
        checkTimer.restart()
    }

    function finishCheck() {
        const id = checkingPlayset
        checkingPlayset = -1
        const n = mods ? mods.installedWorkshopCount : 0
        const k = mods ? mods.updateCount : 0
        const result = {}
        if (!workshopReachable) {
            result.summary = "The Steam Workshop could not be reached, so " + n
                    + " Workshop mods were not checked. Check the internet connection and retry."
            result.failed = true
        } else {
            result.summary = n === 0 ? "This playset has no installed Workshop mods to check."
                    : k === 0 ? (n === 1 ? "The Workshop mod is up to date." : "All " + n + " Workshop mods are up to date.")
                    : "Checked " + n + " Workshop mods: " + k + (k === 1 ? " has an update." : " have an update.")
            result.failed = false
        }
        const next = Object.assign({}, checks)
        next[id] = result
        checks = next
        ++checkCount
        updateCheckFinished()
    }

    property Timer checkTimer: Timer {
        interval: 400
        onTriggered: acquisition.finishCheck()
    }

    function addJob(row: var, kind: string, state: string, stateText: string) {
        jobs.append({ jobId: 100 + jobs.count, name: row.name, providerId: row.providerId, kind: kind, state: state,
                      stateText: stateText, progress: -1, errorMessage: "", errorCode: "",
                      canRetry: false, canOpenFallback: false, canReinstall: false })
        active = true
    }

    // Rows of `providerIds` (all rows with an update when empty) become
    // "Updating", as the real rows do while their update job runs.
    function startUpdates(providerIds: var): int {
        let started = 0
        for (let i = 0; i < mods.count; ++i) {
            const m = mods.get(i)
            if (!m.updateAvailable || (providerIds.length > 0 && providerIds.indexOf(m.providerId) < 0))
                continue
            mods.setProperty(i, "updateAvailable", false)
            mods.setProperty(i, "status", "pending")
            mods.setProperty(i, "statusText", "Updating")
            addJob(m, "update", "download_requested", "Update requested")
            ++started
        }
        mods.afterChange(false)
        return started
    }

    function updateMods() {
        if (refuseWithoutSteam() || !mods)
            return
        const n = startUpdates([])
        if (n === 0)
            notifications.post("info", "Nothing to update", "No mod of this playset has a known update.")
        else
            notifications.post("info", "Updates requested",
                               n === 1 ? "Updating 1 Workshop mod through Steam." : "Updating " + n + " Workshop mods through Steam.")
    }

    function updateMod(providerId: string) {
        if (refuseWithoutSteam() || !mods)
            return
        let name = providerId
        for (let i = 0; i < mods.count; ++i) {
            if (mods.get(i).providerId === providerId)
                name = mods.get(i).name
        }
        if (startUpdates([providerId]) === 0)
            notifications.post("warning", "Nothing was updated",
                               "Nothing was updated. Not updated: “" + name + "”. No newer version is known. Use Check for mod updates first.")
        else
            notifications.post("info", "Update requested", "Updating “" + name + "” through Steam.")
    }

    function reinstallMod(providerId: string) {
        if (refuseWithoutSteam() || !mods)
            return
        for (let i = 0; i < mods.count; ++i) {
            const m = mods.get(i)
            if (m.providerId !== providerId)
                continue
            mods.setProperty(i, "updateAvailable", false)
            mods.setProperty(i, "status", "pending")
            mods.setProperty(i, "statusText", "Unsubscribing")
            addJob(m, "reinstall", "unsubscribing", "Unsubscribing")
            mods.afterChange(false)
            notifications.post("info", "Downloading again", "Downloading “" + m.name
                               + "” again: Steam unsubscribes it, then subscribes again and downloads it.")
            return
        }
    }

    function reinstallJob(jobId: int) {
        if (refuseWithoutSteam())
            return
        for (let i = 0; i < jobs.count; ++i) {
            const job = jobs.get(i)
            if (job.jobId !== jobId)
                continue
            jobs.set(i, { kind: "reinstall", state: "unsubscribing", stateText: "Unsubscribing", errorMessage: "",
                          errorCode: "", canRetry: false, canReinstall: false })
            notifications.post("info", "Downloading again", "Downloading “" + job.name
                               + "” again: Steam unsubscribes it, then subscribes again and downloads it.")
            return
        }
    }
}
