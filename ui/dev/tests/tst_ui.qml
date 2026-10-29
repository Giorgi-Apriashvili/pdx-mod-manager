import QtQuick
import QtTest
import Pmm.Ui
import Pmm.Backend

// Smoke tests of the QML layer against the fake backend: virtualization with
// 2,000 rows, keyboard editing with undo/redo, the reorder rules while
// searching, delete confirmation, drag-and-drop, column-width persistence,
// theme switching, the import flow, and keyboard accessibility (Tab order, F6,
// Escape and focus return, Alt+Up/Down, context menu, accessible names).
// Run: ui/dev/preview.sh --test
Item {
    width: 120
    height: 80

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        id: tc

        property Main win: null
        property int firstPlaysetId: -1

        function named(name: string): var {
            return findChild(win.contentItem, name)
        }

        // Key events go to the focused window, so keyboard tests need Main to be
        // active. Windows may refuse to activate it while another application
        // holds the foreground; those tests are then skipped, not failed.
        function activate() {
            for (let i = 0; i < 30 && !win.active; ++i) {
                win.raise()
                win.requestActivate()
                wait(100)
            }
            if (!win.active)
                skip("the test window could not be activated (another application holds the foreground)")
        }

        function requireFocus() {
            activate()
            win.selectMod(named("modList").currentIndex)
        }

        // keyClick() targets whichever window has focus; re-activate Main first
        // in case another application took the foreground mid-test.
        function press(key, modifiers) {
            activate()
            keyClick(key, modifiers ?? Qt.NoModifier)
        }

        function initTestCase() {
            win = mainComponent.createObject(null) as Main
            verify(win !== null)
            tryCompare(Backend.mods, "count", 2000)
            firstPlaysetId = Backend.playsets.activeId
        }

        function cleanupTestCase() {
            win.destroy()
        }

        function init() {
            win.closeDialogs()
            // test_9 activates the imported playset; later tests use the first one.
            if (Backend.playsets.activeId !== firstPlaysetId) {
                Backend.playsets.activate(firstPlaysetId)
                tryCompare(Backend.mods, "count", 2000)
            }
            Backend.modView.search = ""
            Backend.modView.statusFilter = "all"
            Backend.modView.sortMode = "order"
            tryCompare(named("modList"), "count", Backend.mods.count)
            win.selectMod(0)
        }

        function test_1_listIsVirtualized() {
            const list = named("modList")
            compare(list.count, 2000)
            verify(list.contentItem.children.length < 160, "too many delegates: " + list.contentItem.children.length)
            list.positionViewAtEnd()
            wait(50)
            verify(list.contentItem.children.length < 160, "too many delegates after scrolling")
            list.positionViewAtBeginning()
        }

        function test_2_keyboardToggleUndoRedo() {
            requireFocus()
            const list = named("modList")
            press(Qt.Key_Down)
            compare(list.currentIndex, 1)
            compare(Backend.details.row, 1)
            const before = Backend.mods.get(1).enabled
            press(Qt.Key_Space)
            compare(Backend.mods.get(1).enabled, !before)
            press(Qt.Key_Z, Qt.ControlModifier)
            compare(Backend.mods.get(1).enabled, before)
            press(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
            compare(Backend.mods.get(1).enabled, !before)
            press(Qt.Key_Z, Qt.ControlModifier)
            compare(Backend.mods.get(1).enabled, before)
        }

        function test_3_keyboardMove() {
            requireFocus()
            win.selectMod(2)
            const name = Backend.mods.get(2).name
            press(Qt.Key_Up, Qt.ControlModifier)
            compare(Backend.mods.get(1).name, name)
            compare(named("modList").currentIndex, 1)
            press(Qt.Key_Z, Qt.ControlModifier)
            compare(Backend.mods.get(2).name, name)
        }

        function test_4_searchDisablesReorder() {
            requireFocus()
            press(Qt.Key_F, Qt.ControlModifier)
            verify(named("searchField").activeFocus)
            for (const ch of "ship")
                keyClick(ch)
            tryCompare(Backend.modView, "search", "ship", 2000)
            verify(!Backend.modView.reorderAllowed)
            verify(!named("dragHandleArea").enabled)
            verify(named("modList").count < Backend.mods.count)
            press(Qt.Key_Escape)
            tryCompare(Backend.modView, "search", "")
        }

        function test_4b_multiSelectAndBulkEnable() {
            requireFocus()
            const pane = named("modsPane")
            const list = named("modList")
            // Shift+click extends the selection from row 0 to row 19.
            mouseClick(list.itemAtIndex(19), 100, Theme.rowHeight / 2, Qt.LeftButton, Qt.ShiftModifier)
            compare(pane.selectedCount, 20)
            // Row 18 of the fake data needs a dependency that is not in the playset.
            pane.setEnabledSelected(true)
            verify(pane.dependencyFeedback.length > 0)
            for (let r = 0; r < 20; ++r)
                verify(Backend.mods.get(r).enabled)
            press(Qt.Key_Z, Qt.ControlModifier)
            const started = Date.now()
            press(Qt.Key_A, Qt.ControlModifier)
            compare(pane.selectedCount, Backend.mods.count)
            console.log("select all of", pane.selectedCount, "rows took", Date.now() - started, "ms")
            pane.dependencyFeedback = ""
        }

        function test_5_deleteAsksForConfirmation() {
            requireFocus()
            const count = Backend.mods.count
            const name = Backend.mods.get(0).name
            press(Qt.Key_Delete)
            const dialog = named("confirmDialog")
            tryVerify(() => dialog.opened)
            compare(Backend.mods.count, count)
            press(Qt.Key_Return)
            tryVerify(() => !dialog.visible)
            compare(Backend.mods.count, count - 1)
            press(Qt.Key_Z, Qt.ControlModifier)
            compare(Backend.mods.count, count)
            compare(Backend.mods.get(0).name, name)
        }

        function test_6_dragReorder() {
            requireFocus()
            const area = named("dragHandleArea")
            verify(area.enabled)
            const name = Backend.mods.get(0).name
            // From the middle of row 0 down three and a half rows: drops before row 4.
            mouseDrag(area, 9, Theme.rowHeight / 2, 0, Theme.rowHeight * 3)
            compare(Backend.mods.get(3).name, name)
            press(Qt.Key_Z, Qt.ControlModifier)
            compare(Backend.mods.get(0).name, name)
        }

        function test_7_columnWidthPersists() {
            const header = named("columnHeader")
            const w = header.widths
            const statusBefore = w[4]
            // Dragging the Source|Status edge left widens Status.
            const edge = header.gripWidth + w[0] + w[1] + w[2] + w[3]
            mouseDrag(header, edge, header.height / 2, -40, 0)
            compare(Backend.settings.columnWidths.length, 5)
            compare(Backend.settings.columnWidths[4], Math.round(statusBefore + 40))
        }

        function test_8_themeAndPlayGate() {
            Backend.settings.theme = "dark"
            verify(Theme.dark)
            Backend.settings.theme = "light"
            verify(!Theme.dark)
            // The fake playset has enabled mods that are not ready.
            verify(Backend.launcher.blockers.length > 0)
            verify(!named("playButton").enabled)
        }

        function test_9_importCreatesPlayset() {
            const count = Backend.playsets.count
            win.openImport(Backend.sharing.clipboardText())
            const dialog = named("importDialog")
            tryVerify(() => dialog.opened)
            const nameField = findChild(dialog.contentItem, "importNameField")
            tryCompare(nameField, "text", "Federation campaign (imported)")
            const confirm = findChild(dialog.footer, "importConfirmButton")
            verify(confirm.enabled)
            mouseClick(confirm)
            tryVerify(() => !dialog.visible)
            compare(Backend.playsets.count, count + 1)
            compare(Backend.playsets.activeName, "Federation campaign (imported)")
        }

        // --- keyboard and accessibility (docs/accessibility.md) ---------------

        // The column (games, mods, side) that holds the keyboard focus, or "".
        function focusedPane(): string {
            for (let item = win.activeFocusItem; item; item = item.parent) {
                if (["gamesPane", "modsPane", "sidePane"].includes(item.objectName))
                    return item.objectName
            }
            return ""
        }

        function test_a1_tabVisitsEveryColumnInOrder() {
            requireFocus()
            const list = named("modList")
            verify(list.activeFocus)
            const visited = []
            let back = false
            for (let i = 0; i < 80 && !back; ++i) {
                press(Qt.Key_Tab)
                const pane = focusedPane()
                if (pane.length > 0 && visited[visited.length - 1] !== pane)
                    visited.push(pane)
                back = list.activeFocus
            }
            verify(back, "Tab never returned to the mod list; visited " + visited.join(" > "))
            // From the list: actions column, then (wrapping) games, then the mods header.
            compare(visited.join(" > "), "sidePane > gamesPane > modsPane")
            press(Qt.Key_Tab, Qt.ShiftModifier)
            verify(!list.activeFocus)
            compare(focusedPane(), "modsPane")
        }

        function test_a2_f6CyclesColumns() {
            requireFocus()
            press(Qt.Key_F6)
            compare(focusedPane(), "sidePane")
            press(Qt.Key_F6)
            compare(focusedPane(), "gamesPane")
            compare(win.activeFocusItem.Accessible.name.indexOf("Stellaris"), 0)
            press(Qt.Key_F6)
            verify(named("modList").activeFocus)
            press(Qt.Key_F6, Qt.ShiftModifier)
            compare(focusedPane(), "gamesPane")
        }

        function test_a3_enterOpensDialogAndEscapeReturnsFocus() {
            requireFocus()
            const button = named("settingsButton")
            button.forceActiveFocus(Qt.TabFocusReason)
            verify(button.visualFocus)
            press(Qt.Key_Return)
            const dialog = named("settingsDialog")
            tryVerify(() => dialog.opened)
            // Focus starts inside the dialog and Tab stays there.
            for (let i = 0; i < 12; ++i) {
                press(Qt.Key_Tab)
                verify(focusedPane() === "", "Tab left the modal dialog")
            }
            press(Qt.Key_Escape)
            tryVerify(() => !dialog.visible)
            tryVerify(() => button.activeFocus)
        }

        function test_a4_deleteDialogEscapeKeepsRows() {
            requireFocus()
            const count = Backend.mods.count
            press(Qt.Key_Delete)
            const dialog = named("confirmDialog")
            tryVerify(() => dialog.opened)
            press(Qt.Key_Escape)
            tryVerify(() => !dialog.visible)
            compare(Backend.mods.count, count)
            tryVerify(() => named("modList").activeFocus)
        }

        function test_a5_altArrowsMoveInLoadOrder() {
            requireFocus()
            win.selectMod(4)
            const name = Backend.mods.get(4).name
            press(Qt.Key_Down, Qt.AltModifier)
            compare(Backend.mods.get(5).name, name)
            compare(named("modList").currentIndex, 5)
            press(Qt.Key_Up, Qt.AltModifier)
            press(Qt.Key_Up, Qt.AltModifier)
            compare(Backend.mods.get(3).name, name)
            press(Qt.Key_Z, Qt.ControlModifier)
            press(Qt.Key_Z, Qt.ControlModifier)
            press(Qt.Key_Z, Qt.ControlModifier)
            compare(Backend.mods.get(4).name, name)
        }

        function test_a6_contextMenuFromKeyboard() {
            requireFocus()
            win.selectMod(2)
            press(Qt.Key_F10, Qt.ShiftModifier)
            tryVerify(() => win.activeFocusItem && win.activeFocusItem.text === qsTr("Enable"))
            press(Qt.Key_Down)
            compare(win.activeFocusItem.text, qsTr("Disable"))
            press(Qt.Key_Escape)
            tryVerify(() => named("modList").activeFocus)
            compare(named("modList").currentIndex, 2)
        }

        function test_a7_ctrlSpaceTogglesSelection() {
            requireFocus()
            const pane = named("modsPane")
            win.selectMod(0)
            press(Qt.Key_Down, Qt.ShiftModifier)
            press(Qt.Key_Down, Qt.ShiftModifier)
            compare(pane.selectedCount, 3)
            press(Qt.Key_Space, Qt.ControlModifier)
            compare(pane.selectedCount, 2)
            verify(!pane.isSelected(2))
            press(Qt.Key_Space, Qt.ControlModifier)
            compare(pane.selectedCount, 3)
        }

        function test_a8_shortcutsDialog() {
            requireFocus()
            press(Qt.Key_F1)
            const dialog = named("shortcutsDialog")
            tryVerify(() => dialog.opened)
            const list = findChild(dialog.contentItem, "shortcutList")
            tryVerify(() => list.activeFocus)
            verify(list.count > 15)
            press(Qt.Key_End)
            compare(list.currentIndex, list.count - 1)
            press(Qt.Key_Escape)
            tryVerify(() => !dialog.visible)
            tryVerify(() => named("modList").activeFocus)
        }

        function test_a9_accessibleNames() {
            requireFocus()
            win.selectMod(1)
            const row = named("modList").currentItem
            const entry = Backend.mods.get(1)
            verify(row.Accessible.name.startsWith("2. " + entry.name + ", "), row.Accessible.name)
            verify(row.Accessible.name.endsWith(entry.statusText), row.Accessible.name)
            compare(row.Accessible.role, Accessible.ListItem)
            // The current row carries the keyboard focus, so screen readers read it.
            verify(row.activeFocus)
        }

        name: "PmmUi"
        when: windowShown
    }
}
