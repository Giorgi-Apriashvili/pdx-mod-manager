import QtQuick
import "FakeData.js" as FakeData

// Active playset entries in load order (stand-in for ModListModel).
ListModel {
    id: mods

    property FakeNotificationCenter notifications
    property FakeModFilterModel view
    property FakePlaysetsModel playsets
    property var catalog: []

    property int enabledCount: 0
    property int readyCount: 0
    property int missingCount: 0
    property int warningCount: 0
    property int blockingCount: 0
    property int undoDepth: 0
    property int redoDepth: 0
    readonly property bool canUndo: undoDepth > 0
    readonly property bool canRedo: redoDepth > 0
    property string undoText
    property string redoText

    property int playsetId: -1
    property var store: ({})
    property var undoStack: []
    property var redoStack: []

    signal contentChanged()
    signal dependencyFeedback(string text)

    readonly property var roleNames: ["position", "enabled", "name", "source", "sourceLabel", "providerId",
        "status", "statusText", "warnings", "hasWarnings", "progress", "blocking", "version",
        "supportedVersion", "tags"]

    function rowData(i: int): var {
        const m = get(i)
        const o = {}
        for (const r of roleNames)
            o[r] = m[r]
        return o
    }

    function snapshot(): var {
        const list = []
        for (let i = 0; i < count; ++i)
            list.push(rowData(i))
        return list
    }

    function restore(list: var) {
        clear()
        append(list)
        afterChange(true)
    }

    function load(id: int, size: int) {
        if (playsetId >= 0)
            store[playsetId] = snapshot()
        playsetId = id
        undoStack = []
        redoStack = []
        updateUndo()
        let list = store[id]
        if (!list) {
            list = []
            // Every eighth installed mod past the first rows stays out of the
            // playset so "Add mods" has something to offer.
            const offset = id === 3 ? 400 : id === 2 ? 1200 : 0
            for (let k = offset; list.length < size && k < catalog.length; ++k) {
                if (k >= 40 && k % 8 === 7)
                    continue
                const c = catalog[k]
                const o = Object.assign({}, c)
                o.position = list.length + 1
                o.blocking = o.enabled && o.status !== "ready"
                list.push(o)
            }
        }
        clear()
        append(list)
        afterChange(true)
    }

    function afterChange(structural: bool) {
        let en = 0, ready = 0, missing = 0, warn = 0, blocking = 0
        for (let i = 0; i < count; ++i) {
            const m = get(i)
            if (m.position !== i + 1)
                setProperty(i, "position", i + 1)
            const b = m.enabled && m.status !== "ready"
            if (m.blocking !== b)
                setProperty(i, "blocking", b)
            if (m.enabled) en++
            if (m.status === "ready") ready++
            if (m.status === "missing") missing++
            if (m.hasWarnings) warn++
            if (b) blocking++
        }
        enabledCount = en
        readyCount = ready
        missingCount = missing
        warningCount = warn
        blockingCount = blocking
        if (playsets)
            playsets.updateCounts(playsetId, count, en)
        if (view)
            view.sync(structural)
        contentChanged()
    }

    function pushUndo(text: string) {
        undoStack.push({ text: text, rows: snapshot() })
        redoStack = []
        updateUndo()
    }

    function updateUndo() {
        undoDepth = undoStack.length
        redoDepth = redoStack.length
        undoText = undoDepth > 0 ? "Undo " + undoStack[undoDepth - 1].text : ""
        redoText = redoDepth > 0 ? "Redo " + redoStack[redoDepth - 1].text : ""
    }

    function undo() {
        if (undoStack.length === 0)
            return
        const step = undoStack.pop()
        redoStack.push({ text: step.text, rows: snapshot() })
        restore(step.rows)
        updateUndo()
    }

    function redo() {
        if (redoStack.length === 0)
            return
        const step = redoStack.pop()
        undoStack.push({ text: step.text, rows: snapshot() })
        restore(step.rows)
        updateUndo()
    }

    function setEnabled(row: int, on: bool) {
        if (row < 0 || row >= count || get(row).enabled === on)
            return
        pushUndo((on ? "enable " : "disable ") + "“" + get(row).name + "”")
        setProperty(row, "enabled", on)
        afterChange(false)
    }

    function setEnabledRows(rows: list<int>, on: bool): string {
        if (rows.length === 0)
            return ""
        pushUndo((on ? "enable " : "disable ") + rows.length + " mods")
        let feedback = []
        for (const row of rows) {
            if (row < 0 || row >= count)
                continue
            setProperty(row, "enabled", on)
            const m = get(row)
            if (on && m.warnings.indexOf("Requires") >= 0)
                feedback.push("“" + m.name + "” requires “Compact Interface - Core Library”, which is not in this playset.")
            else if (on && m.status !== "ready")
                feedback.push("“" + m.name + "” is enabled but not installed (" + m.statusText.toLowerCase() + ").")
        }
        afterChange(false)
        if (feedback.length > 6)
            feedback = feedback.slice(0, 6).concat(["… and " + (feedback.length - 6) + " more."])
        // Like the C++ model: feedback arrives through the signal, the return value is empty.
        if (on && feedback.length > 0)
            dependencyFeedback(feedback.join("\n"))
        return ""
    }

    function refuseReorder(): bool {
        if (view && !view.reorderAllowed) {
            notifications.post("warning", "Reordering is disabled", view.reorderBlockedReason)
            return true
        }
        return false
    }

    function moveRow(from: int, to: int) {
        if (refuseReorder() || from === to || from < 0 || to < 0 || from >= count || to >= count)
            return
        pushUndo("move “" + get(from).name + "”")
        move(from, to, 1)
        if (view)
            view.move(from, to, 1)
        afterChange(false)
    }

    function moveUp(row: int) {
        moveRow(row, row - 1)
    }

    function moveDown(row: int) {
        moveRow(row, row + 1)
    }

    // One undo step: delta -1/+1 moves each selected row one step unless the
    // edge or a selected row that cannot move blocks it; larger deltas gather
    // the selection into a block whose first row lands at rows[0] + delta.
    function moveRows(rows: list<int>, delta: int) {
        const sorted = Array.prototype.slice.call(rows).filter(r => r >= 0 && r < count).sort((a, b) => a - b)
        if (refuseReorder() || sorted.length === 0 || delta === 0)
            return
        pushUndo("move " + sorted.length + (sorted.length === 1 ? " mod" : " mods"))
        if (delta === -1 || delta === 1) {
            const blocked = {}
            for (const r of delta < 0 ? sorted : sorted.slice().reverse()) {
                const to = r + delta
                if (to < 0 || to >= count || blocked[to]) {
                    blocked[r] = true
                    continue
                }
                move(r, to, 1)
                if (view)
                    view.move(r, to, 1)
            }
            afterChange(false)
            return
        }
        const items = sorted.map(r => rowData(r))
        for (const r of sorted.slice().reverse())
            remove(r, 1)
        const first = Math.max(0, Math.min(count, sorted[0] + delta))
        for (let i = 0; i < items.length; ++i)
            insert(first + i, items[i])
        afterChange(true)
    }

    function removeRows(rows: list<int>) {
        if (rows.length === 0)
            return
        pushUndo("remove " + rows.length + (rows.length === 1 ? " mod" : " mods"))
        const sorted = Array.prototype.slice.call(rows).sort((a, b) => b - a)
        for (const row of sorted) {
            if (row >= 0 && row < count)
                remove(row, 1)
        }
        afterChange(true)
        notifications.post("info", "Removed from playset",
                           sorted.length + (sorted.length === 1 ? " mod was" : " mods were")
                           + " removed. The mods stay installed; press Ctrl+Z to undo.")
    }

    function addMods(identityKeys: list<string>) {
        const present = {}
        for (let i = 0; i < count; ++i)
            present[get(i).source + ":" + get(i).providerId] = true
        const add = []
        for (const key of identityKeys) {
            if (present[key])
                continue
            const c = catalog.find(m => FakeData.identityKey(m) === key)
            if (c)
                add.push(Object.assign({}, c, { enabled: true, position: count + add.length + 1 }))
        }
        if (add.length === 0)
            return
        pushUndo("add " + add.length + (add.length === 1 ? " mod" : " mods"))
        append(add)
        afterChange(true)
    }
}
