import QtQuick

ListModel {
    id: playsets

    property FakeNotificationCenter notifications
    property int activeId: -1
    property string activeName
    property bool canRestore: false
    property string gameKey

    property int nextId: 100
    property var byGame: ({
        "stellaris": [
            { playsetId: 1, name: "Grand campaign", entryCount: 2000, enabledCount: 0, updatedText: "Today 14:02", size: 2000 },
            { playsetId: 2, name: "Vanilla+ quality of life", entryCount: 24, enabledCount: 0, updatedText: "Yesterday", size: 24 },
            { playsetId: 3, name: "Multiplayer with friends", entryCount: 120, enabledCount: 0, updatedText: "12 Oct 2026", size: 120 }
        ],
        "eu4": []
    })
    property var removed: []
    property var historyEntries: [
        { historyId: 7, reason: "Replaced by import", playsetName: "Multiplayer with friends", createdText: "Yesterday 21:14" },
        { historyId: 6, reason: "Deleted", playsetName: "Test playset", createdText: "20 Oct 2026" },
        { historyId: 5, reason: "Mods removed", playsetName: "Grand campaign", createdText: "18 Oct 2026" }
    ]

    signal activated(int playsetId, int size)

    function load(key: string) {
        gameKey = key
        clear()
        const list = byGame[key] || []
        for (const p of list)
            append(p)
        activate(list.length > 0 ? list[0].playsetId : -1)
    }

    function rowOf(id: int): int {
        for (let i = 0; i < count; ++i) {
            if (get(i).playsetId === id)
                return i
        }
        return -1
    }

    function activate(id: int) {
        const row = rowOf(id)
        activeId = row >= 0 ? id : -1
        activeName = row >= 0 ? get(row).name : ""
        activated(activeId, row >= 0 ? get(row).size : 0)
    }

    function updateCounts(id: int, entries: int, enabled: int) {
        const row = rowOf(id)
        if (row >= 0) {
            setProperty(row, "entryCount", entries)
            setProperty(row, "enabledCount", enabled)
        }
    }

    function store() {
        const list = []
        for (let i = 0; i < count; ++i) {
            const p = get(i)
            list.push({ playsetId: p.playsetId, name: p.name, entryCount: p.entryCount,
                        enabledCount: p.enabledCount, updatedText: p.updatedText, size: p.size })
        }
        byGame[gameKey] = list
    }

    function create(name: string): int {
        const id = nextId++
        append({ playsetId: id, name: name, entryCount: 0, enabledCount: 0, updatedText: "Just now", size: 0 })
        store()
        activate(id)
        return id
    }

    function duplicate(id: int, name: string): int {
        const row = rowOf(id)
        if (row < 0)
            return -1
        const src = get(row)
        const nid = nextId++
        append({ playsetId: nid, name: name, entryCount: src.entryCount, enabledCount: src.enabledCount,
                 updatedText: "Just now", size: src.size })
        store()
        activate(nid)
        return nid
    }

    function rename(id: int, name: string): bool {
        const row = rowOf(id)
        if (row < 0 || name.trim().length === 0)
            return false
        setProperty(row, "name", name.trim())
        if (id === activeId)
            activeName = name.trim()
        store()
        return true
    }

    function remove(id: int): bool {
        const row = rowOf(id)
        if (row < 0)
            return false
        const src = get(row)
        const p = { playsetId: src.playsetId, name: src.name, entryCount: src.entryCount,
                    enabledCount: src.enabledCount, updatedText: src.updatedText, size: src.size }
        removed.push(p)
        // remove(id) shadows ListModel.remove(index), so rebuild without the row.
        store()
        byGame[gameKey].splice(row, 1)
        clear()
        for (const q of byGame[gameKey])
            append(q)
        canRestore = true
        notifications.post("info", "Playset deleted", "“" + p.name + "” was deleted. Use Restore to bring it back.")
        if (id === activeId)
            activate(count > 0 ? get(0).playsetId : -1)
        return true
    }

    function restoreLast(): bool {
        if (removed.length === 0)
            return false
        const p = removed.pop()
        append(p)
        store()
        canRestore = removed.length > 0
        activate(p.playsetId)
        notifications.post("info", "Playset restored", "“" + p.name + "” is back.")
        return true
    }

    function history(): var {
        return historyEntries
    }

    function restoreHistory(historyId: int): bool {
        notifications.post("info", "Restored from history", "Snapshot " + historyId + " was restored as a new playset.")
        return true
    }
}
