import QtQuick
import "FakeData.js" as FakeData

// Installed mods of the current game (stand-in for InventoryModel).
ListModel {
    id: inventory

    property FakeModListModel mods
    property var catalog: []
    property string filter

    onFilterChanged: Qt.callLater(refresh)

    function refresh() {
        clear()
        if (!mods)
            return
        const present = {}
        for (let i = 0; i < mods.count; ++i)
            present[mods.get(i).source + ":" + mods.get(i).providerId] = true
        const needle = filter.toLowerCase()
        const rows = []
        for (const c of catalog) {
            if (c.status !== "ready" && c.status !== "broken")
                continue
            if (needle.length > 0 && c.name.toLowerCase().indexOf(needle) < 0 && c.providerId.indexOf(needle) < 0)
                continue
            const key = FakeData.identityKey(c)
            rows.push({
                identityKey: key,
                name: c.name,
                source: c.source,
                sourceLabel: c.sourceLabel,
                providerId: c.providerId,
                version: c.version,
                inPlayset: present[key] === true,
                stateText: c.status === "broken" ? "Installed · descriptor unreadable" : "Installed",
                problems: c.status === "broken" ? "descriptor.mod has no name; the game may skip this mod." : ""
            })
        }
        append(rows)
    }
}
