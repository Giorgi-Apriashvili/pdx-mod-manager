pragma Singleton
import QtQuick
import Pmm.Backend

// Colours, metrics and small presentation helpers shared by every view.
// Light and dark palettes; `dark` follows the persisted theme setting.
// Contrast (WCAG AA, docs/accessibility.md): text colours reach 4.5:1 on the
// surfaces they are used on; focus, controlBorder and status glyphs reach 3:1.
QtObject {
    readonly property bool dark: Backend.settings.effectiveDark

    // Surfaces
    readonly property color window: dark ? "#1e1f22" : "#f3f3f5"
    readonly property color pane: dark ? "#222327" : "#f8f8fa"
    readonly property color base: dark ? "#18191b" : "#ffffff"
    readonly property color alternateBase: dark ? "#1c1d20" : "#f9f9fb"
    readonly property color header: dark ? "#25262a" : "#eeeff2"
    readonly property color divider: dark ? "#2e3035" : "#dcdde1"
    readonly property color border: dark ? "#383a40" : "#cfd1d6"
    readonly property color borderStrong: dark ? "#4b4e56" : "#b4b7be"
    // Outline of unchecked check boxes, radio buttons and text inputs.
    readonly property color controlBorder: dark ? "#80848c" : "#7d828a"
    // Fill of checked check boxes and radio buttons (graphics: 3:1, not 4.5:1).
    readonly property color controlChecked: dark ? "#3d7bf0" : "#2563eb"

    // Text
    readonly property color text: dark ? "#e3e5e8" : "#1f2328"
    readonly property color textMuted: dark ? "#a0a4ab" : "#5c6066"
    readonly property color textFaint: dark ? "#8a8e95" : "#6e7278"

    // Interaction
    readonly property color accent: dark ? "#2f6ce4" : "#2563eb"
    readonly property color accentHover: dark ? "#2a63d4" : "#1d4ed8"
    readonly property color accentPressed: dark ? "#2558bf" : "#1e40af"
    readonly property color accentText: "#ffffff"
    readonly property color link: dark ? "#79a8ff" : "#1d58d8"
    readonly property color focus: dark ? "#79a8ff" : "#2563eb"
    readonly property color selection: dark ? "#203556" : "#d9e5fd"
    readonly property color selectionInactive: dark ? "#2a2d33" : "#e6e8ec"
    readonly property color hover: dark ? "#26282d" : "#eceef1"
    readonly property color button: dark ? "#2a2c31" : "#ffffff"
    readonly property color buttonHover: dark ? "#32353b" : "#f1f2f4"
    readonly property color buttonPressed: dark ? "#3a3d44" : "#e4e6e9"
    readonly property color input: dark ? "#141517" : "#ffffff"
    readonly property color tooltip: dark ? "#3a3d44" : "#2b2e33"
    readonly property color tooltipText: "#f1f2f4"
    readonly property color scrim: dark ? "#99000000" : "#59000000"

    // Status (always paired with a glyph and text, never used alone)
    readonly property color success: dark ? "#3fb950" : "#1a7f37"
    readonly property color warning: dark ? "#d8a23a" : "#8a5c00"
    readonly property color danger: dark ? "#fa7d76" : "#bd1f27"
    readonly property color info: dark ? "#79a8ff" : "#1d58d8"
    readonly property color successBg: dark ? "#15291c" : "#e3f6e8"
    readonly property color warningBg: dark ? "#2f2713" : "#fff6d6"
    readonly property color dangerBg: dark ? "#3a1e1f" : "#ffebe9"
    readonly property color infoBg: dark ? "#162641" : "#e5effe"

    // Typography (system font family, logical pixel sizes)
    readonly property int fontSize: 12
    readonly property int fontSizeSmall: 11
    readonly property int fontSizeLarge: 13
    readonly property int fontSizeTitle: 15
    readonly property string uiFamily: Qt.application.font.family // qmllint disable missing-property
    readonly property string monoFamily: Qt.platform.os === "windows" ? "Consolas"
                                        : Qt.platform.os === "osx" ? "Menlo" : "monospace"

    // Metrics
    readonly property int rowHeight: 24
    readonly property int controlHeight: 24
    readonly property int controlHeightLarge: 30
    readonly property int playHeight: 40
    readonly property int spacing: 6
    readonly property int spacingSmall: 3
    readonly property int spacingLarge: 10
    readonly property int padding: 8
    readonly property int radius: 3
    readonly property int iconSize: 14
    readonly property int scrollBarSize: 8

    readonly property var statusIcons: ({
        "ready": "check",
        "missing": "circle-alert",
        "pending": "clock",
        "downloading": "download",
        "failed": "circle-x",
        "unavailable": "ban",
        "needs_user_action": "circle-help",
        "broken": "triangle-alert"
    })

    function statusIcon(status: string): string {
        return statusIcons[status] ?? "circle-help"
    }

    function statusColor(status: string): color {
        switch (status) {
        case "ready": return success
        case "downloading": return info
        case "pending": return textMuted
        case "needs_user_action": return warning
        default: return danger
        }
    }

    function levelColor(level: string): color {
        return level === "error" ? danger : level === "warning" ? warning
             : level === "success" ? success : info
    }

    function levelBackground(level: string): color {
        return level === "error" ? dangerBg : level === "warning" ? warningBg
             : level === "success" ? successBg : infoBg
    }

    function levelIcon(level: string): string {
        return level === "error" ? "circle-x" : level === "warning" ? "triangle-alert"
             : level === "success" ? "check-circle" : "info"
    }

    // Normalises a string list from C++ (JS array / sequence) or a newline
    // separated string into a plain JS array.
    function lines(value): list<string> {
        if (value === undefined || value === null)
            return []
        if (typeof value === "string")
            return value.length > 0 ? value.split("\n") : []
        if (value.length !== undefined)
            return Array.prototype.slice.call(value).map(v => String(v))
        return []
    }

    // Display form of a file:// URL from a file or folder dialog.
    function urlToPath(url): string {
        const s = String(url ?? "")
        if (/^file:\/\/\/[A-Za-z]:/.test(s))
            return decodeURIComponent(s.substring(8))
        if (s.startsWith("file://"))
            return decodeURIComponent(s.substring(7))
        return s
    }

    // Scrolls every Flickable around `item`, innermost first, so that keyboard
    // focus never lands on a control outside the visible area. A ListView's
    // current item is left to the view.
    function reveal(item: Item) {
        for (let content = item ? item.parent : null; content; content = content.parent) {
            const flick = content.parent as Flickable
            if (!flick || flick.contentItem !== content)
                continue
            if ((flick as ListView)?.currentItem === item)
                continue
            const top = item.mapToItem(content, 0, 0).y - 4
            const bottom = top + item.height + 8
            const maxY = flick.originY + Math.max(0, flick.contentHeight - flick.height)
            if (top < flick.contentY)
                flick.contentY = Math.max(flick.originY, top)
            else if (bottom > flick.contentY + flick.height)
                flick.contentY = Math.min(maxY, bottom - flick.height)
        }
    }

    // Home/End/PageUp/PageDown for ListViews that use the built-in arrow key
    // navigation. Returns whether the key was handled.
    function pageList(list: ListView, key: int): bool {
        if (list.count === 0)
            return false
        const page = Math.max(1, Math.floor(list.height / rowHeight) - 1)
        let row = list.currentIndex
        switch (key) {
        case Qt.Key_Home: row = 0; break
        case Qt.Key_End: row = list.count - 1; break
        case Qt.Key_PageUp: row = Math.max(0, row - page); break
        case Qt.Key_PageDown: row = Math.min(list.count - 1, row + page); break
        default: return false
        }
        list.currentIndex = row
        list.positionViewAtIndex(row, ListView.Contain)
        return true
    }

    function formatCount(n: int): string {
        return Number(n).toLocaleString(Qt.locale(), "f", 0)
    }

    function plural(n: int, one: string, many: string): string {
        return formatCount(n) + " " + (n === 1 ? one : many)
    }
}
