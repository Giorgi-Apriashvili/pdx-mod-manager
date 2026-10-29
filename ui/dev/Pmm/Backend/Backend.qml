pragma Singleton
import QtQuick
import "FakeData.js" as FakeData

// Development stand-in for the C++ `Backend` singleton (docs/ui-contract.md).
// Implements the whole contract surface with synthetic data so the QML can be
// previewed, linted and screenshotted without the application layer.
QtObject {
    id: backend

    readonly property string appVersion: "0.1.0-dev (preview backend)"
    readonly property int protocolVersion: 1
    readonly property bool readOnly: false
    readonly property string dataDir: "C:/Users/Player/AppData/Roaming/ParadoxModManager"
    readonly property string logsDir: "C:/Users/Player/AppData/Local/ParadoxModManager/logs"

    function openLogsFolder() {
        notifications.post("info", "Logs folder", "Would open " + logsDir)
    }

    readonly property var catalog: FakeData.makeCatalog(281990, 2300)

    readonly property FakeAppSettings settings: FakeAppSettings {}
    readonly property FakeNotificationCenter notifications: FakeNotificationCenter {}
    readonly property FakeGamesModel games: FakeGamesModel {
        notifications: backend.notifications
        onGameSelected: key => {
            const cat = key === "stellaris" ? backend.catalog : []
            backend.mods.catalog = cat
            backend.inventory.catalog = cat
            backend.acquisition.gameKey = key
            backend.playsets.load(key)
        }
        onConfigurationImported: backend.playsets.create("Imported from game configuration")
    }
    readonly property FakePlaysetsModel playsets: FakePlaysetsModel {
        notifications: backend.notifications
        onActivated: (playsetId, size) => {
            backend.mods.load(playsetId, size)
            backend.inventory.refresh()
        }
    }
    readonly property FakeModListModel mods: FakeModListModel {
        notifications: backend.notifications
        view: backend.modView
        playsets: backend.playsets
    }
    readonly property FakeModFilterModel modView: FakeModFilterModel {
        source: backend.mods
    }
    readonly property FakeInventoryModel inventory: FakeInventoryModel {
        mods: backend.mods
    }
    readonly property FakeModDetails details: FakeModDetails {
        mods: backend.mods
        notifications: backend.notifications
    }
    readonly property FakeShareController sharing: FakeShareController {
        notifications: backend.notifications
        mods: backend.mods
        playsets: backend.playsets
    }
    readonly property FakeAcquisitionController acquisition: FakeAcquisitionController {
        notifications: backend.notifications
    }
    readonly property FakeLaunchController launcher: FakeLaunchController {
        notifications: backend.notifications
        mods: backend.mods
    }

    Component.onCompleted: games.select(settings.lastGameKey)
}
