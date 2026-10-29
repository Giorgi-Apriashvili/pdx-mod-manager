import QtQuick

// Details of the selected source row (stand-in for ModDetails).
QtObject {
    id: details

    property FakeModListModel mods
    property FakeNotificationCenter notifications
    property int row: -1

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
    property list<string> warnings
    property list<string> problems
    property string workshopUrl
    property string contentPath
    property bool canOpenFolder: false

    onRowChanged: refresh()
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
        warnings = m.warnings.length > 0 ? m.warnings.split("\n") : []
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
        const deps = []
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
