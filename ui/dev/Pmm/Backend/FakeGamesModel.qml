import QtQuick

ListModel {
    id: games

    property FakeNotificationCenter notifications
    property string currentKey
    property string currentName
    property bool currentInstalled: false
    property bool currentSupported: false
    property string currentLoadOrderNote
    property bool scanning: false

    signal gameSelected(string key)
    signal configurationImported()

    ListElement {
        key: "stellaris"; name: "Stellaris"; supported: true; installed: true; manualLocation: false
        statusText: "Installed \u00b7 v4.5.2"; version: "4.5.2"
        installPath: "C:/Games/Steam/steamapps/common/Stellaris"
        userDataPath: "C:/Users/Player/Documents/Paradox Interactive/Stellaris"
        loadOrderNote: ""
    }
    ListElement {
        key: "eu4"; name: "Europa Universalis IV"; supported: true; installed: false; manualLocation: false
        statusText: "Not found"; version: ""
        installPath: ""
        userDataPath: "C:/Users/Player/Documents/Paradox Interactive/Europa Universalis IV"
        loadOrderNote: "Europa Universalis IV does not use this order when two mods change the same file: the mod whose name comes first alphabetically wins. The order is still saved, shared and written for the game."
    }
    ListElement {
        key: "hoi4"; name: "Hearts of Iron IV"; supported: false; installed: false; manualLocation: false
        statusText: "Not supported yet"; version: ""; installPath: ""; userDataPath: ""; loadOrderNote: ""
    }
    ListElement {
        key: "ck3"; name: "Crusader Kings III"; supported: false; installed: false; manualLocation: false
        statusText: "Not supported yet"; version: ""; installPath: ""; userDataPath: ""; loadOrderNote: ""
    }
    ListElement {
        key: "vic3"; name: "Victoria 3"; supported: false; installed: false; manualLocation: false
        statusText: "Not supported yet"; version: ""; installPath: ""; userDataPath: ""; loadOrderNote: ""
    }

    function indexOfKey(key: string): int {
        for (let i = 0; i < count; ++i) {
            if (get(i).key === key)
                return i
        }
        return -1
    }

    function select(key: string) {
        const row = indexOfKey(key)
        if (row < 0 || !get(row).supported || key === currentKey)
            return
        currentKey = key
        syncCurrent()
        gameSelected(key)
    }

    function syncCurrent() {
        const row = indexOfKey(currentKey)
        currentName = row >= 0 ? get(row).name : ""
        currentInstalled = row >= 0 && get(row).installed
        currentSupported = row >= 0 && get(row).supported
        currentLoadOrderNote = row >= 0 ? get(row).loadOrderNote : ""
    }

    function rescan() {
        scanning = true
        scanTimer.restart()
    }

    function locate(key: string, installDirUrl: url, userDataDirUrl: url): bool {
        const row = indexOfKey(key)
        const path = String(installDirUrl)
        if (row < 0 || path.length === 0) {
            notifications.post("error", "Installation not recognised",
                               "Choose the folder that contains the game executable.")
            return false
        }
        const display = path.replace(/^file:\/\/\/?/, "")
        setProperty(row, "installPath", display)
        if (String(userDataDirUrl).length > 0)
            setProperty(row, "userDataPath", String(userDataDirUrl).replace(/^file:\/\/\/?/, ""))
        setProperty(row, "installed", true)
        setProperty(row, "manualLocation", true)
        setProperty(row, "statusText", "Installed \u00b7 manual location")
        syncCurrent()
        if (key === currentKey)
            gameSelected(currentKey)
        notifications.post("info", "Installation located", get(row).name + " will use " + display + ".")
        return true
    }

    function clearManualLocation(key: string) {
        notifications.post("info", "Automatic detection restored", "The next scan uses the detected Steam libraries.")
        rescan()
    }

    function importCurrentConfiguration(): bool {
        configurationImported()
        return true
    }

    property Timer scanTimer: Timer {
        interval: 1500
        onTriggered: {
            games.scanning = false
            games.notifications.post("info", "Scan complete", "Found 2 games and 2,250 installed mods.")
        }
    }
}
