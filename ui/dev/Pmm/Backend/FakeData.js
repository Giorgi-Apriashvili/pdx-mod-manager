.pragma library

// Deterministic synthetic data for the dev preview backend. Names and ids are
// invented; they do not refer to real Workshop items.

const adjectives = ["Expanded", "Improved", "Dynamic", "Realistic", "Advanced", "Extended",
    "Immersive", "Classic", "Galactic", "Ancient", "Stellar", "Glorious", "Compact",
    "Unofficial", "Plentiful", "Diverse", "Detailed", "Balanced", "Streamlined", "Hidden"];
const subjects = ["Ship Designs", "Traditions", "Planet Modifiers", "Leader Traits",
    "Megastructures", "Species Portraits", "Event Chains", "Diplomacy", "Trade Routes",
    "Starbase Modules", "Origins", "Civics", "Tech Tree", "Ascension Perks", "Music Pack",
    "Interface", "Map Colours", "Fleet Manager", "Pop Growth", "Archaeology Sites",
    "Fallen Empires", "Precursors", "Anomalies", "Ethics", "Relics", "Edicts", "Buildings"];
const suffixes = ["", "", "", "", " 2.0", " Redux", " (Compatibility Patch)", " Lite",
    " Reborn", " for 4.x", " and More", " [Legacy]", " Plus", " - Core Library"];
const tagPool = ["Gameplay", "Balance", "Graphics", "Interface", "Species", "Events",
    "Technologies", "Overhaul", "Fixes", "Sound", "Economy", "Military", "Diplomacy"];

const statusText = {
    "ready": "Ready",
    "missing": "Missing",
    "pending": "Queued",
    "downloading": "Downloading",
    "failed": "Download failed",
    "unavailable": "Unavailable",
    "needs_user_action": "Needs action",
    "broken": "Broken"
};

function rng(seed) {
    let s = seed >>> 0;
    return function () {
        s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
        return s / 4294967296;
    };
}

function pick(r, list) {
    return list[Math.floor(r() * list.length)];
}

function textFor(status, progress) {
    if (status === "downloading")
        return progress >= 0 ? "Downloading " + Math.round(progress * 100) + " %" : "Downloading";
    return statusText[status] || status;
}

// Forced states for the first rows so previews/screenshots show every state.
const forced = {
    2: { status: "missing", enabled: true },
    5: { status: "downloading", progress: 0.45, enabled: true },
    8: { warning: "version" },
    10: { status: "failed", enabled: true },
    13: { status: "pending", enabled: true },
    15: { status: "missing", enabled: false },
    17: { warning: "dependency" },
    19: { status: "needs_user_action", local: true, enabled: false },
    22: { status: "unavailable", enabled: false },
    24: { status: "broken", enabled: true, warning: "version" }
};

// Catalog of installed + referenced mods for one game.
function makeCatalog(seed, count) {
    const r = rng(seed);
    const used = {};
    const mods = [];
    for (let i = 0; i < count; ++i) {
        let name;
        do {
            name = pick(r, adjectives) + " " + pick(r, subjects) + pick(r, suffixes);
            if (used[name])
                name += " " + (2 + Math.floor(r() * 8));
        } while (used[name]);
        used[name] = true;

        const f = forced[i] || {};
        const local = f.local !== undefined ? f.local : r() < 0.08;
        const x = r();
        let status = x < 0.88 ? "ready" : x < 0.915 ? "missing" : x < 0.93 ? "pending"
                   : x < 0.94 ? "downloading" : x < 0.952 ? "failed" : x < 0.96 ? "unavailable"
                   : x < 0.97 ? "broken" : "ready";
        if (local && status !== "ready")
            status = r() < 0.5 ? "ready" : "needs_user_action";
        if (f.status)
            status = f.status;
        const progress = status === "downloading" ? (f.progress !== undefined ? f.progress
                                                                               : Math.round(r() * 90) / 100)
                                                   : -1;
        const id = local ? name.toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "")
                         : String(1000000000 + Math.floor(r() * 2999999999));
        const warnings = [];
        const w = f.warning || (r() < 0.05 ? "version" : r() < 0.02 ? "dependency" : "");
        if (w === "version")
            warnings.push("Declares supported version 3.14.* but the game is 4.5.2.");
        if (w === "dependency")
            warnings.push("Requires “Compact Interface - Core Library”, which is not in this playset.");
        const tags = [];
        const tagCount = 1 + Math.floor(r() * 3);
        for (let t = 0; t < tagCount; ++t) {
            const tag = pick(r, tagPool);
            if (tags.indexOf(tag) < 0)
                tags.push(tag);
        }
        mods.push({
            name: name,
            source: local ? "local" : "steam-workshop",
            sourceLabel: local ? "Local" : "Workshop",
            providerId: id,
            status: status,
            statusText: textFor(status, progress),
            progress: progress,
            warnings: warnings.join("\n"),
            hasWarnings: warnings.length > 0,
            enabled: f.enabled !== undefined ? f.enabled : r() < 0.72,
            version: (1 + Math.floor(r() * 4)) + "." + Math.floor(r() * 12) + "." + Math.floor(r() * 9),
            supportedVersion: w === "version" ? "3.14.*" : "4.5.*",
            tags: tags.join(",")
        });
    }
    return mods;
}

function identityKey(m) {
    return m.source + ":" + m.providerId;
}

const sampleCode = "PMM1.eNqtVk1v2zAM_SuGz2nTtGu7uYdh2GHAgB12GoYdaJm2hUiiJ1FO0qL_fZQ_8tGmW4dhF0Ok"
    + "SD3yPYrUXZ5r4PKDr0kDtfCbGZpd3iTrqv8PbPHdT6xxuw23gTz6cr4CSY7htqbclXMDbRslVsYw5zSxDTsSNcw9h"
    + "Oyzac8yoqMSV66JD2xZ7KwI-FDkjKn7TtSR-4_hk3t2FGx5vTCdxtwNeOGkvnnufHfZ7WyGbXbN8ZWyaw4Zt6TJH"
    + "f2GrRUzNWgtSIvhCoF7JwXmrjfEi8Ex_UKSNMXqy56tuGt5cnFxhy6D6p0x5r0YTNyPWZTufPZ5n4jcdKcS9e-H8"
    + "ozwftx2Bk6Hn0AzEpE0PENk1AG7Mpm1Bvg3hn9UOf-ZtGmc6iWH2_0Z8d4HZdJpyfx8pdWh0wJJBEQgsAANiUo5AM"
    + "cEc6Pvzx7Ir2G9lA.6f1c5e0a9d24b7c3e18f2a6b0d97c4e5f3a2b1c0d9e8f7a6b5c4d3e2f1a0b9c8";
