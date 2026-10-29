import QtQuick

// Stand-in for AcquisitionController with a static job list.
QtObject {
    id: acquisition

    property FakeNotificationCenter notifications
    property bool active: true
    property string summary
    property string providerStatus: "ready"
    property string providerMessage
    readonly property bool downloadsEnabled: true
    property string gameKey: "stellaris"
    property ListModel jobs: ListModel {}

    readonly property var sampleJobs: [
        { jobId: 11, name: "Stellar Ship Designs Redux", providerId: "1834502911", state: "downloading",
          stateText: "Downloading 45 % (12.4 of 27.6 MB)", progress: 0.45, errorMessage: "",
          canRetry: false, canOpenFallback: false },
        { jobId: 12, name: "Diverse Event Chains", providerId: "2208311470", state: "download_requested",
          stateText: "Waiting for Steam", progress: -1, errorMessage: "", canRetry: false, canOpenFallback: false },
        { jobId: 13, name: "Hidden Relics Plus", providerId: "1990047312", state: "failed",
          stateText: "Failed after 3 attempts", progress: -1,
          errorMessage: "Steam reported a timeout. Check your connection and retry.",
          canRetry: true, canOpenFallback: true },
        { jobId: 14, name: "Ancient Civics [Legacy]", providerId: "1688204417", state: "unavailable",
          stateText: "Unavailable", progress: -1,
          errorMessage: "The item was removed from the Workshop or is private.",
          canRetry: false, canOpenFallback: true }
    ]

    // Only the Stellaris preview has queued downloads.
    function reload() {
        jobs.clear()
        const sample = gameKey === "stellaris"
        if (sample)
            jobs.append(sampleJobs)
        active = sample
        summary = sample ? "1 downloading · 1 queued · 1 failed" : ""
    }

    onGameKeyChanged: reload()
    Component.onCompleted: reload()

    function acquireMissing() {
        notifications.post("info", "Downloads queued", "4 missing mods were queued.")
    }

    function retry(jobId: int) {
        notifications.post("info", "Retrying", "Job " + jobId + " was queued again.")
    }

    function retryAllFailed() {
        notifications.post("info", "Retrying failed downloads", "1 job was queued again.")
    }

    function cancelAll() {
        active = false
        notifications.post("warning", "Downloads cancelled",
                           "Queued downloads were stopped. Mods that already finished subscribing or downloading stay in Steam.")
    }

    function openFallback(jobId: int) {
        notifications.post("info", "Opened in Steam", "Subscribe on the Workshop page; the manager picks it up when the download completes.")
    }

    function rescanNow() {
        notifications.post("info", "Rescanning", "Checking the Workshop folder for finished downloads.")
    }
}
