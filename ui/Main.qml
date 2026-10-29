pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Pmm.Ui
import Pmm.Backend

// Application window: three resizable columns (games | mods | actions), the
// dialogs, global shortcuts and notifications. Window geometry and pane widths
// are restored from and written back to Backend.settings.
ApplicationWindow {
    id: window

    readonly property int defaultWidth: 1400
    readonly property int defaultHeight: 860
    readonly property int defaultLeftWidth: 200
    readonly property int defaultRightWidth: 300
    property bool geometryRestored: false
    property string nameMode

    // Hooks for the dev screenshot harness and the application shell.
    function openImport(text: string) {
        importDialog.openWith(text)
    }
    function openExport() {
        if (Backend.playsets.activeId >= 0)
            exportDialog.open()
    }
    function openAddMods() {
        if (Backend.playsets.activeId >= 0)
            addModsDialog.open()
    }
    function openSettings() {
        settingsDialog.open()
    }
    function openHistory() {
        historyDialog.open()
    }
    function openLocate() {
        locateDialog.open()
    }
    function openShortcuts() {
        shortcutsDialog.open()
    }
    function selectMod(proxyRow: int) {
        modsPane.selectOnly(proxyRow)
        modsPane.focusList()
    }
    function closeDialogs() {
        for (const d of [importDialog, exportDialog, addModsDialog, historyDialog, locateDialog,
                         settingsDialog, confirmDialog, nameDialog, shortcutsDialog])
            d.close()
    }

    // F6 / Shift+F6: move keyboard focus to the next / previous column.
    function focusPane(step: int) {
        const panes = [gamesPane, modsPane, sidePane]
        let current = -1
        for (let item = activeFocusItem; item && current < 0; item = item.parent)
            current = panes.indexOf(item)
        panes[(current + step + panes.length) % panes.length].focusFirst()
    }

    function askName(mode: string) {
        nameMode = mode
        const active = Backend.playsets.activeName
        if (mode === "new")
            nameDialog.ask(qsTr("New playset"), qsTr("Create"), qsTr("New playset"))
        else if (mode === "duplicate")
            nameDialog.ask(qsTr("Duplicate playset"), qsTr("Duplicate"), qsTr("%1 (copy)").arg(active))
        else
            nameDialog.ask(qsTr("Rename playset"), qsTr("Rename"), active)
    }

    function applyName(name: string) {
        const id = Backend.playsets.activeId
        if (nameMode === "new")
            Backend.playsets.create(name)
        else if (nameMode === "duplicate" && id >= 0)
            Backend.playsets.duplicate(id, name)
        else if (nameMode === "rename" && id >= 0)
            Backend.playsets.rename(id, name)
    }

    function deleteActivePlayset() {
        // Deletion is snapshotted by the backend; the Restore banner undoes it.
        const id = Backend.playsets.activeId
        if (id >= 0)
            Backend.playsets.remove(id)
    }

    // True when a window at (x, y) keeps its title bar on one of the screens.
    function onSomeScreen(x: int, y: int, w: int): bool {
        const screens = Qt.application.screens // qmllint disable missing-property
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i]
            const visibleX = Math.min(x + w, s.virtualX + s.width) - Math.max(x, s.virtualX)
            if (visibleX >= 120 && y >= s.virtualY - 8 && y + 40 <= s.virtualY + s.height)
                return true
        }
        return false
    }

    function restoreGeometry() {
        const s = Backend.settings
        if (s.windowWidth >= minimumWidth && s.windowHeight >= minimumHeight) {
            width = s.windowWidth
            height = s.windowHeight
            if (onSomeScreen(s.windowX, s.windowY, width)) {
                x = s.windowX
                y = s.windowY
            }
        }
        if (s.windowMaximized)
            visibility = Window.Maximized
        geometryRestored = true
    }

    function storeGeometry() {
        if (!geometryRestored)
            return
        const s = Backend.settings
        const maximized = visibility === Window.Maximized
        if (visibility === Window.Windowed) {
            s.windowX = x
            s.windowY = y
            s.windowWidth = width
            s.windowHeight = height
        }
        if (visibility === Window.Windowed || maximized)
            s.windowMaximized = maximized
    }

    function storePaneWidths() {
        Backend.settings.leftPaneWidth = Math.round(gamesPane.width)
        Backend.settings.rightPaneWidth = Math.round(sidePane.width)
    }

    width: defaultWidth
    height: defaultHeight
    minimumWidth: 980
    minimumHeight: 600
    visible: true
    title: Backend.games.currentName.length > 0
           ? qsTr("%1 – Paradox Mod Manager").arg(Backend.games.currentName)
           : qsTr("Paradox Mod Manager")
    color: Theme.window
    font.pixelSize: Theme.fontSize

    // Palette for the few Basic-style parts that are not restyled (tooltips,
    // separators, text selection).
    palette.window: Theme.window
    palette.windowText: Theme.text
    palette.base: Theme.base
    palette.alternateBase: Theme.alternateBase
    palette.text: Theme.text
    palette.button: Theme.button
    palette.buttonText: Theme.text
    palette.brightText: Theme.accentText
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.placeholderText: Theme.textFaint
    palette.light: Theme.buttonHover
    palette.midlight: Theme.divider
    palette.mid: Theme.border
    palette.dark: Theme.borderStrong
    palette.shadow: Theme.scrim
    palette.toolTipBase: Theme.tooltip
    palette.toolTipText: Theme.tooltipText
    palette.link: Theme.link

    Component.onCompleted: {
        // Tool tips show model data (warnings, paths). The shared Basic tool
        // tip would render text that starts with a tag as rich text.
        split.ToolTip.toolTip.contentItem.textFormat = Text.PlainText // qmllint disable missing-property
        restoreGeometry()
        modsPane.focusList()
    }

    onClosing: {
        storeGeometry()
        storePaneWidths()
        Backend.settings.save()
    }

    // Tabbing into a scrolled column scrolls the focused control into view.
    onActiveFocusItemChanged: Qt.callLater(Theme.reveal, activeFocusItem)

    onXChanged: geometryTimer.restart()
    onYChanged: geometryTimer.restart()
    onWidthChanged: geometryTimer.restart()
    onHeightChanged: geometryTimer.restart()
    onVisibilityChanged: geometryTimer.restart()

    // Geometry is sampled after moves settle so maximise/restore transitions
    // never overwrite the normal (windowed) geometry.
    Timer {
        id: geometryTimer
        interval: 400
        onTriggered: window.storeGeometry()
    }

    SplitView {
        id: split
        anchors.fill: parent
        orientation: Qt.Horizontal

        onResizingChanged: {
            if (!resizing)
                window.storePaneWidths()
        }

        handle: Rectangle {
            id: splitHandle
            implicitWidth: 1
            implicitHeight: 1
            color: SplitHandle.pressed ? Theme.accent
                 : SplitHandle.hovered ? Theme.borderStrong : Theme.divider
            Accessible.role: Accessible.Separator
            Accessible.name: qsTr("Resize columns")

            // 1 px divider with a wider grab area.
            containmentMask: Item {
                x: (splitHandle.width - width) / 2
                width: 9
                height: split.height
            }
        }

        GamesPane {
            id: gamesPane
            objectName: "gamesPane"
            SplitView.preferredWidth: Backend.settings.leftPaneWidth > 0 ? Backend.settings.leftPaneWidth
                                                                         : window.defaultLeftWidth
            SplitView.minimumWidth: 160
            SplitView.maximumWidth: 360
            onLocateRequested: window.openLocate()
            onSettingsRequested: window.openSettings()
        }

        ModsPane {
            id: modsPane
            objectName: "modsPane"
            SplitView.fillWidth: true
            SplitView.minimumWidth: 460
            onAddModsRequested: window.openAddMods()
            onImportRequested: window.openImport("")
            onNewPlaysetRequested: window.askName("new")
            onLocateRequested: window.openLocate()
            onRemoveRequested: (rows, description) => {
                confirmDialog.ask(qsTr("Remove from playset"), description,
                                  qsTr("The mods stay installed and can be added again. Ctrl+Z undoes the removal."),
                                  qsTr("Remove"), true, () => {
                                      Backend.mods.removeRows(rows)
                                      modsPane.afterRemove()
                                  })
            }
        }

        SidePane {
            id: sidePane
            objectName: "sidePane"
            SplitView.preferredWidth: Backend.settings.rightPaneWidth > 0 ? Backend.settings.rightPaneWidth
                                                                          : window.defaultRightWidth
            SplitView.minimumWidth: 270
            SplitView.maximumWidth: 480
            onImportRequested: window.openImport("")
            onExportRequested: window.openExport()
            onNewPlaysetRequested: window.askName("new")
            onDuplicateRequested: window.askName("duplicate")
            onRenameRequested: window.askName("rename")
            onDeleteRequested: window.deleteActivePlayset()
            onHistoryRequested: window.openHistory()
        }
    }

    // Notifications float over the bottom-right of the mod table, above dialogs.
    ToastHost {
        parent: Overlay.overlay
        z: 1000
        onFocusReleased: modsPane.focusList()
        width: Math.min(380, modsPane.width - 24)
        x: (parent ? parent.width : window.width) - sidePane.width - width - 12
        y: (parent ? parent.height : window.height) - height - 34
    }

    // --- dialogs ---------------------------------------------------------

    ImportDialog {
        id: importDialog
        objectName: "importDialog"
    }
    ExportDialog {
        id: exportDialog
    }
    AddModsDialog {
        id: addModsDialog
    }
    HistoryDialog {
        id: historyDialog
    }
    LocateDialog {
        id: locateDialog
    }
    SettingsDialog {
        id: settingsDialog
        objectName: "settingsDialog"
        onShortcutsRequested: window.openShortcuts()
    }
    ConfirmDialog {
        id: confirmDialog
        objectName: "confirmDialog"
    }
    NameDialog {
        id: nameDialog
        onSubmitted: name => window.applyName(name)
    }
    ShortcutsDialog {
        id: shortcutsDialog
        objectName: "shortcutsDialog"
    }

    // --- shortcuts (blocked automatically while a modal dialog is open) -----

    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: modsPane.focusSearch()
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: Backend.mods.canUndo
        onActivated: Backend.mods.undo()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        enabled: Backend.mods.canRedo
        onActivated: Backend.mods.redo()
    }
    Shortcut {
        sequences: [StandardKey.SelectAll]
        onActivated: modsPane.selectAll()
    }
    Shortcut {
        sequence: "Ctrl+I"
        enabled: Backend.games.currentSupported && Backend.games.currentInstalled
        onActivated: window.openImport("")
    }
    Shortcut {
        sequence: "Ctrl+E"
        onActivated: window.openExport()
    }
    Shortcut {
        sequence: "Ctrl+N"
        enabled: Backend.games.currentSupported && Backend.games.currentInstalled
        onActivated: window.askName("new")
    }
    Shortcut {
        sequence: "Ctrl+Shift+A"
        onActivated: window.openAddMods()
    }
    Shortcut {
        sequence: "F5"
        enabled: !Backend.games.scanning
        onActivated: Backend.games.rescan()
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: window.openSettings()
    }
    Shortcut {
        sequences: ["F6"]
        onActivated: window.focusPane(1)
    }
    Shortcut {
        sequences: ["Shift+F6"]
        onActivated: window.focusPane(-1)
    }
    Shortcut {
        // F1 everywhere, plus Ctrl+? on macOS (HelpContents is F1 elsewhere, and
        // a duplicate sequence would make the shortcut ambiguous).
        sequences: Qt.platform.os === "osx" ? ["F1", StandardKey.HelpContents] : ["F1"]
        onActivated: window.openShortcuts()
    }
}
