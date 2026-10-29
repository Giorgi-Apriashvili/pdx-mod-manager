import QtQuick
import "FakeData.js" as FakeData

// Stand-in for ShareController with a canned import preview.
QtObject {
    id: sharing

    property FakeNotificationCenter notifications
    property FakeModListModel mods
    property FakePlaysetsModel playsets
    property string lastExportCode
    property var preview: ({})
    property bool hasPreview: false
    property bool busy: false
    readonly property string portabilityNote: "A share code fixes which mods are used, their order and "
        + "which are enabled. It does not pin mod versions: mods and the game can update, so identical "
        + "codes do not guarantee identical files or multiplayer checksums."

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
        return lastExportCode
    }

    function copyExportCode(): bool {
        notifications.post("info", "Share code copied", "Paste it anywhere to share this playset.")
        return true
    }

    function exportToFile(fileUrl: url): bool {
        notifications.post("info", "Share code saved", "Saved to " + String(fileUrl))
        return true
    }

    function clipboardText(): string {
        return FakeData.sampleCode
    }

    function previewImport(text: string): bool {
        const t = text.trim()
        if (!t.startsWith("PMM1.")) {
            preview = {
                ok: false,
                errorTitle: t.startsWith("PMM") ? "Unsupported share-code version" : "Not a share code",
                errorMessage: t.startsWith("PMM")
                    ? "This code was made by a newer version of Paradox Mod Manager (protocol "
                      + t.substring(3, t.indexOf(".")) + "). Update the application to import it."
                    : "Share codes start with “PMM1.”. Copy the complete code, including the part after the last dot."
            }
            hasPreview = true
            return false
        }
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
                statusText: c.source === "local" ? "Local · needs mapping"
                          : missing ? "Missing · will download" : "Installed"
            })
        }
        preview = {
            ok: true,
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
            warnings: [
                "1 local mod cannot be downloaded automatically; map it to an installed mod or install it manually.",
                "The code was exported with game version 4.4.1; you have 4.5.2."
            ],
            entries: entries
        }
        hasPreview = true
        return true
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
        preview = {}
        hasPreview = false
    }
}
