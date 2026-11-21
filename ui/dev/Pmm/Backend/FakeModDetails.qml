import QtQuick

// Details of the selected source row (stand-in for ModDetails).
QtObject {
    id: details

    property FakeModListModel mods
    property FakeNotificationCenter notifications
    property int row: -1
    // The selected game. "hoi4" lists the dependencies as Hearts of Iron IV
    // treats them: it loads the mod after them when they are enabled and does
    // not need them. The preview has no Hearts of Iron IV mods, so the
    // preview tests set it while Stellaris's list is shown.
    property string game: "stellaris"

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
        // Hearts of Iron IV never warns about dependencies (the fake list's
        // "Requires …" warnings stand for Stellaris's).
        warnings = (m.warnings.length > 0 ? m.warnings.split("\n") : [])
                   .filter(w => game !== "hoi4" || !w.startsWith("Requires"))
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
        dependenciesLoadAfter = game === "hoi4"
        dependenciesNote = dependenciesLoadAfter
                ? "Hearts of Iron IV loads this mod after these mods when they are enabled. It does not need them."
                : ""
        const deps = []
        if (dependenciesLoadAfter) {
            if (row % 7 === 3 && installed) {
                deps.push({ name: "Example Overhaul", state: "load_after", text: "Enabled in this playset" })
                deps.push({ name: "Example Map Expansion", state: "load_after", text: "Disabled in this playset; not needed" })
                deps.push({ name: "Example Total Conversion", state: "load_after", text: "Installed, not in this playset; not needed" })
                deps.push({ name: "Example Alternate History", state: "load_after", text: "Not installed; not needed" })
            }
            dependencies = deps
            return
        }
        if (row % 7 === 3)
            deps.push({ name: "Unofficial Patch Collection", state: "ok", text: "In playset, loads earlier" })
        if (m.warnings.indexOf("Requires") >= 0)
            deps.push({ name: "Compact Interface - Core Library", state: "not_in_playset", text: "Not in this playset" })
        if (row % 11 === 5)
            deps.push({ name: "Expanded Tech Tree", state: "order", text: "Must load before this mod" })
        if (row % 13 === 6)
            deps.push({ name: "Detailed Species Portraits", state: "disabled", text: "In playset but disabled" })
        dependencies = deps
    }

    function openWorkshopPage() {
        notifications.post("info", "Workshop page", "Would open " + workshopUrl)
    }

    function openContentFolder() {
        notifications.post("info", "Open folder", "Would open " + contentPath)
    }
}
