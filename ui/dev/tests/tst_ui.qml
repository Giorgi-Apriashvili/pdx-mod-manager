import QtQuick
import QtTest
import Pmm.Ui
import Pmm.Backend

// Smoke tests of the QML layer against the fake backend: virtualization with
// 2,000 rows, keyboard editing with undo/redo, the reorder rules while
// searching, delete confirmation, drag-and-drop, column-width persistence,
// theme switching and the import flow. Run: ui/dev/preview.sh --test
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
        }

        function cleanupTestCase() {
            win.destroy()
        }

        function init() {
            win.closeDialogs()
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

        name: "PmmUi"
        when: windowShown
    }
}
