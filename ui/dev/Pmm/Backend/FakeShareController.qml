import QtQuick
import "FakeData.js" as FakeData

// Stand-in for ShareController with a canned import preview. A Workshop ID
// list is previewed with its missing mods shown by ID; finishLookup() (or,
// with autoLookup, a timer) then adds their Workshop names to the same
// preview (same generation), as the Steam Web API answer does.
QtObject {
    id: sharing

    property FakeNotificationCenter notifications
    property FakeModListModel mods
    property FakePlaysetsModel playsets
    property string lastExportCode
    property var workshopIdList: ({})
    property var preview: ({})
    property bool hasPreview: false
    property bool busy: false
    property int generation: 0
    // Finish a list's lookup on its own after a moment (the preview app).
    property bool autoLookup: true
    readonly property string sampleIdList: FakeData.sampleIdList
    readonly property string portabilityNote: "A share code fixes which mods are used, their order and "
        + "which are enabled. It does not pin mod versions: mods and the game can update, so identical "
        + "codes do not guarantee identical files or multiplayer checksums."

    property Timer lookupTimer: Timer {
        interval: 1200
        onTriggered: sharing.finishLookup()
    }

    function plural(count: int, one: string, many: string): string {
        return count + " " + (count === 1 ? one : many)
    }

    function exportCode(): string {
        let body = ""
        const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
        let s = ((mods ? mods.count : 0) * 7919 + 17) >>> 0
        const length = Math.min(4000, 180 + (mods ? mods.count : 0) * 9)
        for (let i = 0; i < length; ++i) {
            s = (Math.imul(s, 1664525) + 1013904223) >>> 0
            body += alphabet[(s >>> 24) & 63]
        }
        lastExportCode = "PMM1.eNq" + body + ".3f9a1c27b84e5d6091fa2c3b7e8d4a5f60b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5"
        workshopIdList = makeWorkshopIdList()
        return lastExportCode
    }

    // The enabled Workshop mods of the active playset, as ShareController lists them.
    function makeWorkshopIdList(): var {
        const ids = []
        let local = 0
        let disabled = 0
        for (let i = 0; mods && i < mods.count; ++i) {
            const row = mods.get(i)
            if (row.source === "local")
                ++local
            else if (!row.enabled)
                ++disabled
            else
                ids.push(row.providerId)
        }
        const parts = []
        if (local > 0)
            parts.push(plural(local, "local mod", "local mods"))
        if (disabled > 0)
            parts.push(plural(disabled, "disabled mod", "disabled mods"))
        const leftOutText = parts.length === 0 ? ""
            : "Left out: " + parts.join(" and ") + ". The share code includes "
              + (local + disabled === 1 ? "it." : "them.")
        let note = ids.length === 0 ? "This playset has no enabled Steam Workshop mods, so the list is empty."
            : plural(ids.length, "enabled Workshop mod", "enabled Workshop mods")
              + " in load order. The list does not say which game it is for."
        if (leftOutText.length > 0)
            note += " " + leftOutText
        return {
            text: ids.join("|"),
            count: ids.length,
            leftOutLocal: local,
            leftOutDisabled: disabled,
            leftOutText: leftOutText,
            error: "",
            note: note
        }
    }

    function copyExportCode(): bool {
        notifications.post("info", "Share code copied", "Paste it anywhere to share this playset.")
        return true
    }

    function copyWorkshopIdList(): bool {
        if ((workshopIdList.text ?? "").length === 0)
            return false
        notifications.post("info", "Workshop ID list copied",
                           "It lists " + plural(workshopIdList.count, "Workshop mod", "Workshop mods")
                           + ". Whoever imports it selects the game first.")
        return true
    }

    function exportToFile(fileUrl: url): bool {
        notifications.post("info", "Share code saved", "Saved to " + String(fileUrl))
        return true
    }

    function clipboardText(): string {
        return FakeData.sampleCode
    }

    function setPreview(map: var) {
        lookupTimer.stop()
        preview = map
        hasPreview = true
    }

    function previewImport(text: string): bool {
        const t = text.trim()
        ++generation
        if (t.startsWith("PMM") && !t.startsWith("PMM1.")) {
            setPreview({
                ok: false,
                generation: generation,
                format: "code",
                errorTitle: "Unsupported share-code version",
                errorMessage: "This code was made by a newer version of Paradox Mod Manager (protocol "
                    + t.substring(3, t.indexOf(".")) + "). Update the application to import it."
            })
            return false
        }
        if (!t.startsWith("PMM1."))
            return previewIdList(t)
        const entries = []
        const cat = mods ? mods.catalog : []
        for (let i = 0; i < 42 && i < cat.length; ++i) {
            const c = cat[(i * 37) % cat.length]
            const missing = i === 3 || i === 9 || i === 17 || i === 30 || i === 31
            entries.push({
                position: i + 1,
                name: c.name,
                source: c.source,
                providerId: c.providerId,
                enabled: i % 5 !== 4,
                check: "",
                statusText: c.source === "local" ? "Local · needs mapping"
                          : missing ? "Missing · will download" : "Installed"
            })
        }
        setPreview({
            ok: true,
            generation: generation,
            format: "code",
            listNote: "",
            lookup: "off",
            lookupText: "",
            errorTitle: "",
            errorMessage: "",
            game: "stellaris",
            gameName: "Stellaris",
            playsetName: "Federation campaign",
            proposedName: "Federation campaign (imported)",
            total: 42,
            enabled: 34,
            installed: 36,
            missing: 5,
            missingEnabled: 4,
            missingDisabled: 1,
            localUnmapped: 1,
            downloads: true,
            warnings: [
                "1 local mod cannot be downloaded automatically; map it to an installed mod or install it manually.",
                "The code was exported with game version 4.4.1; you have 4.5.2."
            ],
            entries: entries
        })
        return true
    }

    // A list of ids separated by |, commas or white space; every third one is
    // installed, the others missing and shown by ID until finishLookup().
    function previewIdList(t: string): bool {
        const ids = t.split(/[|,\s]+/).filter(s => s.length > 0)
        const bad = ids.findIndex(s => !/^[1-9][0-9]{0,19}$/.test(s))
        if (ids.length === 0 || bad >= 0) {
            setPreview({
                ok: false,
                generation: generation,
                format: "idList",
                errorTitle: "Not a valid Workshop ID list",
                errorMessage: ids.length === 0 ? "The text contains no Workshop IDs."
                    : "Entry " + (bad + 1) + " of the Workshop ID list, “" + ids[bad]
                      + "”, is not a Workshop ID or a Steam Workshop link."
            })
            return false
        }
        const cat = mods ? mods.catalog.filter(c => c.source !== "local") : []
        const entries = []
        let installed = 0
        for (let i = 0; i < ids.length; ++i) {
            const isInstalled = i % 3 === 0 && cat.length > 0
            installed += isInstalled ? 1 : 0
            entries.push({
                position: i + 1,
                name: isInstalled ? cat[(i * 7) % cat.length].name : ids[i],
                source: "steam-workshop",
                providerId: ids[i],
                enabled: true,
                check: "",
                statusText: isInstalled ? "Installed" : "Missing · will download"
            })
        }
        const missing = ids.length - installed
        setPreview({
            ok: true,
            generation: generation,
            format: "idList",
            listNote: "An ID list does not say which game it is for. It is imported for Stellaris, the "
                + "selected game, with every mod enabled, in list order.",
            lookup: missing > 0 ? "pending" : "off",
            lookupText: missing > 0 ? "Looking up " + plural(missing, "missing mod", "missing mods")
                                      + " on the Steam Workshop…" : "",
            errorTitle: "",
            errorMessage: "",
            game: "stellaris",
            gameName: "Stellaris",
            playsetName: "Imported mod list",
            proposedName: "Imported mod list",
            total: ids.length,
            enabled: ids.length,
            installed: installed,
            missing: missing,
            missingEnabled: missing,
            missingDisabled: 0,
            localUnmapped: 0,
            downloads: true,
            warnings: [],
            entries: entries
        })
        if (missing > 0 && autoLookup)
            lookupTimer.start()
        return true
    }

    // The Steam Web API's answer for the current list preview: names for the
    // missing mods, and the last one not found. Same generation.
    function finishLookup() {
        if (!hasPreview || preview.lookup !== "pending")
            return
        const updated = Object.assign({}, preview)
        const entries = []
        let last = -1
        for (let i = 0; i < preview.entries.length; ++i) {
            if (preview.entries[i].statusText.startsWith("Missing"))
                last = i
        }
        for (let i = 0; i < preview.entries.length; ++i) {
            const e = Object.assign({}, preview.entries[i])
            if (e.statusText.startsWith("Missing")) {
                e.check = i === last ? "notFound" : "ok"
                if (i === last)
                    e.statusText = "Not found on Workshop"
                else
                    e.name = "Workshop mod " + e.providerId
            }
            entries.push(e)
        }
        updated.entries = entries
        updated.lookup = "done"
        updated.lookupText = ""
        updated.warnings = ["1 missing Workshop mod was not found on the Steam Workshop. It may have "
                            + "been deleted, or it is private or friends-only."]
        preview = updated
    }

    function importFromFile(fileUrl: url): bool {
        return previewImport(FakeData.sampleCode)
    }

    function confirmImport(mode: string, targetPlaysetId: int, name: string): bool {
        if (!preview.ok)
            return false
        if (mode === "replace")
            notifications.post("info", "Playset replaced", "The previous contents were saved to History.")
        else
            playsets.create(name)
        notifications.post("info", "Import complete", "5 missing mods were queued for download.")
        clearPreview()
        return true
    }

    function clearPreview() {
        lookupTimer.stop()
        ++generation
        preview = {}
        hasPreview = false
    }
}
