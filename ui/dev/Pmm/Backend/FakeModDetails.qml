import QtQuick
import "FakeData.js" as FakeData

// Details of the selected source row (stand-in for ModDetails).
QtObject {
    id: details

    property FakeModListModel mods
    property FakeNotificationCenter notifications
    property int row: -1
    // The selected game. The dependencies are listed as it treats them (core
    // DependencyMeaning): "vic3" requires them; "stellaris" and "ck3" do not
    // need them but warn when an enabled one comes later in the list; "eu4"
    // and "hoi4" never warn ("hoi4" lists four example mods of its own). The
    // preview has only Stellaris mods, so the preview tests set it while
    // Stellaris's list is shown.
    property string game: "stellaris"
    readonly property var gameNames: ({
        ck3: "Crusader Kings III", eu4: "Europa Universalis IV", hoi4: "Hearts of Iron IV",
        stellaris: "Stellaris", vic3: "Victoria 3"
    })

    property bool hasSelection: false
    property string name
    property string source
    property string sourceLabel
    property string providerId
    property string version
    property string supportedVersion
    property string statusText
    property list<string> tags
    property var dependencies: []
    property bool dependenciesLoadAfter: false
    property string dependenciesNote
    property list<string> warnings
    property list<string> problems
    property string workshopUrl
    property string contentPath
    property bool canOpenFolder: false

    onRowChanged: refresh()
    onGameChanged: refresh()
    property Connections modsConnection: Connections {
        target: details.mods
        function onContentChanged() { details.refresh() }
    }

    function refresh() {
        hasSelection = mods !== null && row >= 0 && row < mods.count
        if (!hasSelection) {
            name = ""
            return
        }
        const m = mods.get(row)
        name = m.name
        source = m.source
        sourceLabel = m.sourceLabel
        providerId = m.providerId
        version = m.status === "ready" || m.status === "broken" ? m.version : ""
        supportedVersion = m.supportedVersion
        statusText = m.statusText
        tags = m.tags.length > 0 ? m.tags.split(",") : []
        // Computed here: a binding on `game` could still hold the old game
        // while onGameChanged runs.
        const dependencyMeaning = game === "vic3" ? "required"
                : game === "stellaris" || game === "ck3" ? "loadAfterInList" : "loadAfter"
        // The fake list's dependency warnings are Stellaris's order hints:
        // reworded where the named mods are required, dropped where they never
        // cause a warning.
        const lines = m.warnings.length > 0 ? m.warnings.split("\n") : []
        const hint = lines.find(w => FakeData.isOrderHint(w)) || ""
        const required = dependencyMeaning === "required"
        const hintText = !hint || dependencyMeaning === "loadAfter" ? ""
                       : required ? FakeData.requiredOrderWarning(hint) : hint
        problems = m.status === "broken" ? ["descriptor.mod has no name; the game may skip this mod."]
                 : m.status === "failed" ? ["The last download attempt failed: Steam reported a timeout (k_EResultTimeout)."]
                 : []
        const installed = m.status === "ready" || m.status === "broken"
        workshopUrl = m.source === "steam-workshop"
                    ? "https://steamcommunity.com/sharedfiles/filedetails/?id=" + m.providerId : ""
        contentPath = installed ? (m.source === "local"
                                   ? "C:/Users/Player/Documents/Paradox Interactive/Stellaris/mod/" + m.providerId
                                   : "C:/Games/Steam/steamapps/workshop/content/281990/" + m.providerId)
                                : ""
        canOpenFolder = installed
        dependenciesLoadAfter = !required
        dependenciesNote = required ? ""
                : game === "hoi4" ? "Hearts of Iron IV loads this mod after these mods when they are enabled. It does not need them."
                : gameNames[game] + " does not need these mods. This mod names them to load before it when they are enabled."
        const deps = []
        const issues = []
        if (game === "hoi4") {
            if (row % 7 === 3 && installed) {
                deps.push({ name: "Example Overhaul", state: "load_after", text: "Enabled in this playset" })
                deps.push({ name: "Example Map Expansion", state: "load_after", text: "Disabled in this playset; not needed" })
                deps.push({ name: "Example Total Conversion", state: "load_after", text: "Installed, not in this playset; not needed" })
                deps.push({ name: "Example Alternate History", state: "load_after", text: "Not installed; not needed" })
            }
        } else if (installed) {
            if (row % 7 === 3)
                deps.push(required ? { name: "Unofficial Patch Collection", state: "ok", text: "Enabled in this playset and loads earlier" }
                                   : { name: "Unofficial Patch Collection", state: "load_after", text: "Enabled in this playset" })
            if (hint)
                deps.push(hintText ? { name: "Compact Interface - Core Library", state: "order", text: hintText }
                                   : { name: "Compact Interface - Core Library", state: "load_after", text: "Enabled in this playset" })
            if (row % 11 === 5) {
                const text = "Requires \"Expanded Tech Tree\", which is installed but not in this playset."
                deps.push(required ? { name: "Expanded Tech Tree", state: "not_in_playset", text: text }
                                   : { name: "Expanded Tech Tree", state: "load_after", text: "Installed, not in this playset; not needed" })
                if (required)
                    issues.push(text)
            }
            if (row % 13 === 6) {
                const text = "Requires \"Detailed Species Portraits\" (#" + (row + 9) + "), which is disabled in this playset."
                deps.push(required ? { name: "Detailed Species Portraits", state: "disabled", text: text }
                                   : { name: "Detailed Species Portraits", state: "load_after", text: "Disabled in this playset; not needed" })
                if (required)
                    issues.push(text)
            }
        }
        warnings = lines.filter(w => !FakeData.isOrderHint(w))
                        .concat(hintText ? [hintText] : [], issues)
        dependencies = deps
    }

    function openWorkshopPage() {
        notifications.post("info", "Workshop page", "Would open " + workshopUrl)
    }

    function openContentFolder() {
        notifications.post("info", "Open folder", "Would open " + contentPath)
    }
}
