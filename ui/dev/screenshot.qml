import QtQuick
import Pmm.Ui
import Pmm.Backend

// Renders the main window and key dialogs to PNG files, then quits.
// Run through `ui/dev/preview.sh --screenshot [DIR]` (see ui/README.md).
Main {
    id: shot

    readonly property string outDir: {
        const args = Qt.application.arguments
        for (let i = 0; i < args.length; ++i) {
            if (args[i].startsWith("--out="))
                return args[i].substring(6)
        }
        return "."
    }
    property int step: -1

    // Each step puts the UI into a state; it is grabbed once layout settles.
    readonly property var steps: [
        { name: "main-light", run: () => {
            Backend.settings.theme = "light"
            shot.selectMod(17)
        } },
        { name: "main-dark", run: () => {
            Backend.settings.theme = "dark"
        } },
        { name: "import-preview", run: () => {
            Backend.settings.theme = "light"
            shot.openImport(Backend.sharing.clipboardText())
        } },
        { name: "import-preview-dark", run: () => {
            Backend.settings.theme = "dark"
        } },
        { name: "add-mods", run: () => {
            shot.closeDialogs()
            Backend.settings.theme = "dark"
            Backend.inventory.filter = "Ship"
            shot.openAddMods()
        } },
        { name: "main-filtered", run: () => {
            shot.closeDialogs()
            Backend.settings.theme = "light"
            Backend.modView.statusFilter = "missing"
            shot.selectMod(1)
        } },
        { name: "settings", run: () => {
            shot.openSettings()
        } },
        { name: "export-code", run: () => {
            shot.closeDialogs()
            Backend.settings.theme = "light"
            shot.openExport()
            Backend.sharing.copyExportCode()
        } },
        { name: "playset-deleted", run: () => {
            shot.closeDialogs()
            Backend.modView.statusFilter = "all"
            Backend.playsets.remove(Backend.playsets.activeId)
        } },
        { name: "history", run: () => {
            Backend.settings.theme = "dark"
            shot.openHistory()
        } },
        { name: "game-not-found", run: () => {
            shot.closeDialogs()
            Backend.settings.theme = "dark"
            Backend.games.select("eu4")
        } }
    ]

    function rootItem(): Item {
        let item = shot.contentItem
        while (item.parent)
            item = item.parent
        return item
    }

    function next() {
        ++step
        if (step >= steps.length) {
            console.log("screenshots written to", outDir)
            Qt.quit()
            return
        }
        steps[step].run()
        settle.restart()
    }

    width: 1400
    height: 860
    x: 40
    y: 40

    Component.onCompleted: start.start()

    Timer {
        id: start
        interval: 1200
        onTriggered: shot.next()
    }

    Timer {
        id: settle
        interval: 900
        onTriggered: {
            const name = shot.steps[shot.step].name
            shot.rootItem().grabToImage(result => {
                const path = shot.outDir + "/" + name + ".png"
                if (!result.saveToFile(path))
                    console.warn("could not write", path)
                shot.next()
            })
        }
    }
}
