import QtQuick

// Filtered/sorted copy of the mod list (stand-in for the ModFilterModel proxy).
ListModel {
    id: view

    property var source // FakeModListModel (untyped to avoid a type cycle)
    property string search
    property string statusFilter: "all"
    property string sortMode: "order"
    readonly property bool reorderAllowed: search.length === 0 && statusFilter === "all" && sortMode === "order"
    readonly property string reorderBlockedReason: reorderAllowed ? ""
        : sortMode !== "order"
          ? "Reordering is off while the list is sorted by " + sortMode + ". Sort by load order to drag or move mods."
          : "Reordering is off while a search or filter hides part of the load order. Clear them to drag or move mods."

    property var map: []

    onSearchChanged: Qt.callLater(rebuild)
    onStatusFilterChanged: Qt.callLater(rebuild)
    onSortModeChanged: Qt.callLater(rebuild)

    function sourceRow(proxyRow: int): int {
        return proxyRow >= 0 && proxyRow < map.length ? map[proxyRow] : -1
    }

    function proxyRow(sourceRow: int): int {
        return map.indexOf(sourceRow)
    }

    function accepts(m: var): bool {
        if (search.length > 0 && m.name.toLowerCase().indexOf(search.toLowerCase()) < 0
                && m.providerId.indexOf(search) < 0)
            return false
        switch (statusFilter) {
        case "enabled": return m.enabled
        case "disabled": return !m.enabled
        case "missing": return m.status !== "ready"
        case "warning": return m.hasWarnings
        default: return true
        }
    }

    function rebuild() {
        if (!source)
            return
        const rows = []
        for (let i = 0; i < source.count; ++i) {
            if (accepts(source.get(i)))
                rows.push(i)
        }
        if (sortMode !== "order") {
            const key = sortMode === "status" ? "statusText" : sortMode === "source" ? "sourceLabel" : "name"
            rows.sort((a, b) => {
                const x = String(source.get(a)[key]), y = String(source.get(b)[key])
                return x < y ? -1 : x > y ? 1 : a - b
            })
        }
        map = rows
        clear()
        append(rows.map(r => source.rowData(r)))
    }

    function sync(structural: bool) {
        if (structural || !reorderAllowed && map.length === 0) {
            rebuild()
            return
        }
        if (reorderAllowed && map.length !== source.count) {
            rebuild()
            return
        }
        for (let i = 0; i < count; ++i) {
            const s = map[i]
            const src = source.get(s)
            if (!accepts(src)) {
                Qt.callLater(rebuild)
                return
            }
            const v = get(i)
            for (const r of ["position", "enabled", "blocking", "name", "status", "statusText", "progress"]) {
                if (v[r] !== src[r])
                    setProperty(i, r, src[r])
            }
        }
    }
}
