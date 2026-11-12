import QtQuick
import QtTest
import Pmm.Ui
import Pmm.Backend

// Smoke tests of the QML layer against the fake backend: virtualization with
// 2,000 rows, keyboard editing with undo/redo, the reorder rules while
// searching, delete confirmation, drag-and-drop, column-width persistence,
// theme switching, the import flow, discarding an interrupted change that
// Recover cannot undo, keyboard accessibility (Tab order, F6,
// Escape and focus return, Alt+Up/Down, context menu, accessible names, the
// Updates section, the update banner, the discard action, a blocked Play and
// column widths from the keyboard), and the smallest window.
// Run: ui/dev/preview.sh --test, or ctest -L ui-preview
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
            // The fake playset has enabled mods that are not ready: Play is
            // unavailable, but stays focusable so the keyboard reaches the reasons.
            const play = named("playButton")
            verify(Backend.launcher.blockers.length > 0)
            verify(!play.available)
            verify(play.enabled)
            verify(play.Accessible.name.endsWith(", unavailable"), play.Accessible.name)
            // Without a playset the game starts without mods: Play is not blocked.
            Backend.playsets.activate(-1)
            tryCompare(Backend.mods, "count", 0)
            compare(Backend.launcher.blockers.length, 0)
            compare(Backend.launcher.warnings[0], "No playset is selected, so the game starts without mods.")
            verify(play.available)
            compare(play.Accessible.name, play.text)
        }

        function test_8b_discardInterruptedAfterRecoverFails() {
            const launcher = Backend.launcher
            launcher.hasInterruptedTransaction = true
            launcher.backupMissing = true
            const banner = named("interruptedBanner")
            const discard = named("discardInterruptedButton")
            tryVerify(() => banner.visible)
            verify(!discard.visible)
            launcher.recoverInterrupted()
            verify(launcher.canDiscardInterrupted)
            tryVerify(() => discard.visible)
            verify(banner.text.indexOf("missing or damaged") >= 0)

            // The confirmation names the file; Cancel is the default and does nothing.
            const dialog = named("confirmDialog")
            // Placed after Recover once the row has laid it out.
            tryVerify(() => discard.x > 0)
            mouseClick(discard)
            tryVerify(() => dialog.opened)
            verify(dialog.cancelIsDefault)
            verify(dialog.message.indexOf("dlc_load.json") >= 0)
            verify(dialog.detail.indexOf("20261107T101500000Z-0000") >= 0)
            mouseClick(findChild(dialog.footer, "cancelButton"))
            tryVerify(() => !dialog.visible)
            verify(launcher.hasInterruptedTransaction)
            verify(launcher.canDiscardInterrupted)

            mouseClick(discard)
            tryVerify(() => dialog.opened)
            mouseClick(findChild(dialog.footer, "confirmButton"))
            tryVerify(() => !dialog.visible)
            verify(!launcher.hasInterruptedTransaction)
            verify(!launcher.canDiscardInterrupted)
            tryVerify(() => !banner.visible)
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

        function test_a10_settingsUpdates() {
            requireFocus()
            win.openSettings()
            const dialog = named("settingsDialog")
            tryVerify(() => dialog.opened)
            const check = findChild(dialog.contentItem, "updateCheckButton")
            const action = findChild(dialog.contentItem, "updateAction")
            verify(check.enabled)
            verify(!action.visible)
            compare(check.Accessible.name, "Check for updates")

            // Reachable with Tab, and the keyboard stays in the section.
            for (let i = 0; i < 12 && !check.activeFocus; ++i)
                press(Qt.Key_Tab)
            verify(check.activeFocus)
            press(Qt.Key_Return)
            compare(Backend.updates.state, "checking")
            tryVerify(() => action.activeFocus)
            compare(action.text, "Cancel")
            tryCompare(Backend.updates, "state", "available")
            compare(action.text, "Update and restart")
            verify(action.activeFocus)

            // Cancelling a download goes back to the offer; then it runs through.
            press(Qt.Key_Return)
            compare(Backend.updates.state, "downloading")
            compare(action.text, "Cancel")
            press(Qt.Key_Return)
            compare(Backend.updates.state, "available")
            press(Qt.Key_Return)
            tryCompare(Backend.updates, "state", "installing")
            verify(!check.enabled)

            Backend.updates.state = "idle"
            press(Qt.Key_Escape)
            tryVerify(() => !dialog.visible)
        }

        function test_a11_updateBanner() {
            requireFocus()
            const list = named("modList")
            const banner = named("updateBanner")
            verify(!banner.visible)
            Backend.updates.checkDaily()
            tryCompare(Backend.updates, "state", "available")
            verify(banner.visible)
            compare(banner.Accessible.name, "Paradox Mod Manager 0.2.0 is available.")
            // Non-modal: the list keeps the keyboard. Tab reaches the banner's buttons.
            verify(list.activeFocus)
            const action = findChild(banner, "updateBannerAction")
            const dismiss = findChild(banner, "updateBannerDismiss")
            compare(action.text, "Update and restart")
            compare(dismiss.Accessible.name, "Not now")
            for (let i = 0; i < 80 && !dismiss.activeFocus; ++i)
                press(Qt.Key_Tab)
            verify(dismiss.activeFocus)
            press(Qt.Key_Return)
            verify(!banner.visible)
            tryVerify(() => list.activeFocus)
            compare(Backend.updates.state, "available") // Settings still offers it

            Backend.updates.state = "idle"
            Backend.updates.checkedAutomatically = false
            banner.dismissed = false
        }

        function test_a12_discardFromKeyboardMovesFocusOn() {
            requireFocus()
            const launcher = Backend.launcher
            launcher.hasInterruptedTransaction = true
            launcher.backupMissing = true
            launcher.recoverInterrupted()
            const banner = named("interruptedBanner")
            const discard = named("discardInterruptedButton")
            tryVerify(() => discard.visible)
            discard.forceActiveFocus(Qt.TabFocusReason)
            press(Qt.Key_Return)
            const dialog = named("confirmDialog")
            tryVerify(() => dialog.opened)
            tryVerify(() => findChild(dialog.footer, "cancelButton").activeFocus)
            press(Qt.Key_Tab)
            tryVerify(() => findChild(dialog.footer, "confirmButton").activeFocus)
            press(Qt.Key_Return)
            tryVerify(() => !banner.visible)
            // The focused action went away with the banner: the mod list has the focus.
            tryVerify(() => named("modList").activeFocus)
            verify(!discard.activeFocus)
        }

        // Tab until `item` has the focus; false if it never gets it.
        function tabTo(item: Item, limit: int): bool {
            for (let i = 0; i < limit && !item.activeFocus; ++i)
                press(Qt.Key_Tab)
            return item.activeFocus
        }

        function test_a13_blockedPlayIsReachableAndSaysWhy() {
            requireFocus()
            const launcher = Backend.launcher
            const play = named("playButton")
            const blocker = launcher.blockers[0]
            verify(blocker.length > 0)
            verify(!play.available)
            verify(tabTo(play, 80), "Tab never reached the blocked Play button")
            compare(play.Accessible.name, play.text + ", unavailable")
            verify(play.Accessible.description.startsWith("Blocked: "), play.Accessible.description)
            verify(play.Accessible.description.indexOf(blocker) >= 0, play.Accessible.description)
            // The reasons are listed just above Play, so no tool tip covers them;
            // past four, it lists the ones behind "…and N more".
            compare(play.toolTipText, "")
            launcher.extraBlockers = ["Reason 2.", "Reason 3.", "Reason 4.", "Reason 5.", "Reason 6."]
            compare(play.toolTipText, "Reason 5.\nReason 6.")
            verify(play.Accessible.description.indexOf("Reason 6.") >= 0, play.Accessible.description)
            launcher.extraBlockers = []
            compare(play.toolTipText, "")

            // Enter, Space and a click explain instead of launching.
            const launches = launcher.playCount
            notificationSpy.clear()
            press(Qt.Key_Return)
            press(Qt.Key_Space)
            mouseClick(play)
            compare(launcher.playCount, launches)
            compare(launcher.state, "idle")
            compare(notificationSpy.count, 3)
            compare(notificationSpy.signalArguments[0][0], "warning")
            compare(notificationSpy.signalArguments[0][1], "The game cannot be started yet")
            verify(notificationSpy.signalArguments[0][2].indexOf(blocker) >= 0)
            verify(play.activeFocus)

            // Once nothing blocks (no playset), the same key starts the game, and
            // the focus stays on Play while the launch runs.
            Backend.playsets.activate(-1)
            tryVerify(() => play.available)
            verify(play.activeFocus)
            notificationSpy.clear()
            press(Qt.Key_Return)
            compare(launcher.playCount, launches + 1)
            compare(launcher.state, "validating")
            verify(play.activeFocus)
            verify(!play.available)
            // What a screen reader says next for the focused button.
            compare(play.Accessible.name, play.text + ", starting")
            press(Qt.Key_Return) // busy: nothing happens, nothing is posted
            compare(launcher.playCount, launches + 1)
            compare(notificationSpy.count, 0)
            tryCompare(launcher, "state", "started", 5000)
            launcher.state = "idle"
        }

        function test_a14_keyboardResizesTableColumns() {
            requireFocus()
            const header = named("columnHeader")
            const defaults = named("modsPane").defaultColumns
            // The column edges are one Tab stop, right before the list.
            press(Qt.Key_Tab, Qt.ShiftModifier)
            let edge = win.activeFocusItem
            compare(edge.Accessible.role, Accessible.Separator)
            compare(edge.Accessible.name, "Position column width: " + header.widths[0] + " pixels")
            const width = header.widths[0]
            press(Qt.Key_Right)
            compare(header.widths[0], width + 10)
            compare(Backend.settings.columnWidths[0], width + 10)
            press(Qt.Key_Right, Qt.ShiftModifier)
            compare(Backend.settings.columnWidths[0], width + 60)
            press(Qt.Key_Home)
            compare(Backend.settings.columnWidths[0], header.minimums[0])
            press(Qt.Key_Return)
            compare(Backend.settings.columnWidths[0], defaults[0])

            // Ctrl+Right moves to the next edges; right of Name an edge resizes the
            // column after it, so Left widens Status.
            press(Qt.Key_Right, Qt.ControlModifier)
            press(Qt.Key_Right, Qt.ControlModifier)
            press(Qt.Key_Right, Qt.ControlModifier)
            edge = win.activeFocusItem
            verify(edge.Accessible.name.startsWith("Status column width: "), edge.Accessible.name)
            compare(header.currentEdge, 3)
            const status = header.widths[4]
            press(Qt.Key_Left)
            compare(Backend.settings.columnWidths[4], status + 10)
            press(Qt.Key_End)
            verify(header.widths[4] > status + 10)
            compare(named("modsPane").nameWidth, named("modsPane").minNameWidth)
            press(Qt.Key_Return)
            compare(Backend.settings.columnWidths[4], defaults[4])

            // The header stays one stop: Tab leaves it for the list, and Shift+Tab
            // comes back to the edge used last.
            press(Qt.Key_Tab)
            verify(named("modList").activeFocus)
            press(Qt.Key_Tab, Qt.ShiftModifier)
            compare(win.activeFocusItem, edge)
        }

        function test_a15_keyboardResizesPanes() {
            requireFocus()
            const side = named("sidePane")
            const games = named("gamesPane")
            // After the list, Tab reaches the divider at the actions column.
            press(Qt.Key_Tab)
            const sideDivider = named("sideDivider")
            verify(sideDivider.activeFocus)
            compare(sideDivider.Accessible.role, Accessible.Separator)
            const width = side.width
            compare(sideDivider.Accessible.name, "Actions column width: " + width + " pixels")
            // The divider moves left, so the actions column gets wider.
            press(Qt.Key_Left)
            tryCompare(side, "width", width + 10)
            compare(Backend.settings.rightPaneWidth, width + 10)
            press(Qt.Key_Right, Qt.ShiftModifier)
            tryCompare(side, "width", width - 40)
            compare(Backend.settings.rightPaneWidth, width - 40)
            press(Qt.Key_Home)
            tryCompare(side, "width", win.sideMinimumWidth)
            compare(Backend.settings.rightPaneWidth, win.sideMinimumWidth)
            press(Qt.Key_End)
            tryCompare(side, "width", sideDivider.maximum)
            verify(sideDivider.maximum > width + 10)
            press(Qt.Key_Return)
            tryCompare(side, "width", win.defaultRightWidth)
            compare(Backend.settings.rightPaneWidth, win.defaultRightWidth)

            // The games column's divider is the last stop of that column.
            const gamesDivider = named("gamesDivider")
            press(Qt.Key_F6) // the selected game
            verify(tabTo(gamesDivider, 10), "Tab never reached the games column's divider")
            const gamesWidth = games.width
            press(Qt.Key_Right)
            tryCompare(games, "width", gamesWidth + 10)
            compare(Backend.settings.leftPaneWidth, gamesWidth + 10)
            press(Qt.Key_Return)
            tryCompare(games, "width", win.defaultLeftWidth)
            compare(Backend.settings.leftPaneWidth, win.defaultLeftWidth)
            press(Qt.Key_Tab)
            compare(focusedPane(), "modsPane")
        }

        // The table header hides (an empty playset, or none) while one of its
        // column edges has the focus: the focus moves on, so arrow keys never
        // resize columns that are not shown.
        function test_a16_hiddenColumnEdgesGiveUpTheFocus() {
            requireFocus()
            const header = named("columnHeader")
            const list = named("modList")
            press(Qt.Key_Tab, Qt.ShiftModifier)
            const edge = win.activeFocusItem
            compare(edge.Accessible.role, Accessible.Separator)
            const widths = JSON.stringify(Backend.settings.columnWidths)

            // Ctrl+N and Enter: a new, empty playset; the dialog gives the focus
            // back to the edge, which is hidden by then.
            const count = Backend.playsets.count
            press(Qt.Key_N, Qt.ControlModifier)
            tryVerify(() => win.activeFocusItem !== edge)
            press(Qt.Key_Return)
            tryCompare(Backend.playsets, "count", count + 1)
            tryVerify(() => !header.visible)
            tryVerify(() => list.activeFocus, 2000, "the focus stayed on a hidden column edge")
            press(Qt.Key_Right)
            compare(JSON.stringify(Backend.settings.columnWidths), widths)

            // No playset: no list either, so the column's first control.
            Backend.playsets.activate(firstPlaysetId)
            tryCompare(Backend.mods, "count", 2000)
            tryVerify(() => header.visible)
            win.selectMod(0)
            press(Qt.Key_Tab, Qt.ShiftModifier)
            compare(win.activeFocusItem.Accessible.role, Accessible.Separator)
            Backend.playsets.activate(-1)
            tryVerify(() => !header.visible)
            tryVerify(() => win.activeFocusItem && win.activeFocusItem.Accessible.role !== Accessible.Separator,
                      2000, "the focus stayed on a hidden column edge")
            verify(win.activeFocusItem.visible)
            // The empty playset stays: deleting it would show the Restore banner.
        }

        // The smallest window: every column fits, the toolbar shows icons, and
        // Play stays reachable while blockers and a banner make the actions
        // column taller than the window.
        function test_b1_minimumWindow() {
            requireFocus()
            const launcher = Backend.launcher
            win.width = win.minimumWidth
            win.height = win.minimumHeight
            tryCompare(win, "width", 900)
            tryCompare(win, "height", 440)
            const games = named("gamesPane")
            const mods = named("modsPane")
            const side = named("sidePane")
            tryVerify(() => side.x + side.width <= win.width + 0.5)
            verify(games.width >= win.gamesMinimumWidth)
            verify(mods.width >= win.modsMinimumWidth)
            verify(side.width >= win.sideMinimumWidth)
            // Squeezed, but the chosen width is what is stored.
            verify(side.width < win.defaultRightWidth)
            win.storePaneWidths()
            compare(Backend.settings.rightPaneWidth, win.defaultRightWidth)
            for (const name of ["playsetSelector", "searchField", "statusFilter", "sortSelector"]) {
                const item = named(name)
                const x = item.mapToItem(mods, 0, 0).x
                verify(x >= 0 && x + item.width <= mods.width, name + " outside the mods column")
            }
            // The toolbar shows icons only, none squeezed below its own width.
            const toolbar = named("modsToolbar")
            verify(toolbar.compact, "the toolbar keeps its labels at the smallest width")
            for (const button of toolbarButtons(toolbar))
                verify(button.width >= button.implicitWidth - 0.5, button.Accessible.name + " is squeezed")
            const play = named("playButton")
            const page = named("sidePage")
            verify(play.mapToItem(side, 0, 0).y + play.height <= side.height, "Play is not visible")
            // The footer shows its counts without words, so "Showing N of M"
            // and "Scanning…" stay inside the column.
            const footer = named("modsFooter")
            Backend.modView.statusFilter = "warning"
            Backend.games.scanning = true
            tryVerify(() => named("showingStat").visible)
            verify(footer.compact)
            verifyInside(footer)
            Backend.games.scanning = false
            Backend.modView.statusFilter = "all"

            launcher.steamRunning = false
            launcher.hasInterruptedTransaction = true
            tryVerify(() => page.interactive, 2000, "the actions column did not start scrolling")
            verify(tabTo(play, 80), "Tab never reached Play")
            // Revealed: inside the visible part of the scrolled column.
            tryVerify(() => {
                const y = play.mapToItem(side, 0, 0).y
                return y >= 0 && y + play.height <= side.height
            }, 2000, "Play was not scrolled into view")
            verify(play.Accessible.description.indexOf("Steam is not running") >= 0)

            launcher.steamRunning = true
            launcher.hasInterruptedTransaction = false
            win.width = 1400
            win.height = 860
            tryVerify(() => !page.interactive)
            // With room, the labels and words come back.
            tryVerify(() => !toolbar.compact)
            verify(!footer.compact)
            for (const button of toolbarButtons(toolbar))
                verify(button.width >= button.implicitWidth - 0.5, button.Accessible.name + " is squeezed")
        }

        // The visible buttons of a toolbar row.
        function toolbarButtons(row: Item): list<Item> {
            const buttons = []
            for (const child of row.children) {
                if (child.visible && child.labelledWidth !== undefined)
                    buttons.push(child)
            }
            verify(buttons.length >= 6, "toolbar buttons not found")
            return buttons
        }

        // Every visible item of a row lies inside it.
        function verifyInside(row: Item) {
            for (const child of row.children) {
                if (child.visible && child.width > 0)
                    verify(child.x >= -0.5 && child.x + child.width <= row.width + 0.5,
                           (child.text ?? child.objectName) + " runs out of its row: " + child.x + " + "
                           + child.width + " > " + row.width)
            }
        }

        // In a window that squeezes the outer columns, a divider key or drag
        // that cannot widen a column changes nothing, and the stored widths
        // stay the chosen ones, which come back in a wider window.
        function test_b3_squeezedColumnsKeepTheChosenWidths() {
            requireFocus()
            const games = named("gamesPane")
            const side = named("sidePane")
            const sideDivider = named("sideDivider")
            const gamesDivider = named("gamesDivider")
            // From the default widths (Enter on each divider).
            press(Qt.Key_Tab)
            verify(sideDivider.activeFocus)
            press(Qt.Key_Return)
            press(Qt.Key_F6)
            verify(tabTo(gamesDivider, 10), "Tab never reached the games column's divider")
            press(Qt.Key_Return)
            tryCompare(games, "width", win.defaultLeftWidth)
            tryCompare(side, "width", win.defaultRightWidth)
            win.width = win.minimumWidth
            win.height = win.minimumHeight
            tryCompare(side, "width", win.sideMinimumWidth)
            tryVerify(() => games.width < win.defaultLeftWidth)
            const squeezed = games.width

            // The actions column: Left would widen it, End too.
            win.selectMod(0)
            press(Qt.Key_Tab)
            verify(sideDivider.activeFocus)
            press(Qt.Key_Left)
            press(Qt.Key_End)
            press(Qt.Key_Left, Qt.ShiftModifier)
            wait(50)
            compare(side.width, win.sideMinimumWidth)
            compare(Backend.settings.rightPaneWidth, win.defaultRightWidth)

            // The games column: Right and End would widen it; Left narrows it
            // for real, and Enter brings back the default.
            press(Qt.Key_F6)
            verify(tabTo(gamesDivider, 10), "Tab never reached the games column's divider")
            press(Qt.Key_Right)
            press(Qt.Key_End)
            wait(50)
            compare(games.width, squeezed)
            compare(Backend.settings.leftPaneWidth, win.defaultLeftWidth)
            press(Qt.Key_Left)
            tryCompare(games, "width", squeezed - 10)
            compare(Backend.settings.leftPaneWidth, squeezed - 10)
            press(Qt.Key_Return)
            tryCompare(games, "width", squeezed)
            compare(Backend.settings.leftPaneWidth, win.defaultLeftWidth)

            // Dragging either divider towards the squeezed column's limit.
            const gamesHandle = games.mapToItem(win.contentItem, games.width, games.height / 2)
            mouseDrag(win.contentItem, gamesHandle.x, gamesHandle.y, 30, 0)
            const sideHandle = side.mapToItem(win.contentItem, -1, side.height / 2)
            mouseDrag(win.contentItem, sideHandle.x, sideHandle.y, -30, 0)
            wait(50)
            compare(games.width, squeezed)
            compare(side.width, win.sideMinimumWidth)
            compare(Backend.settings.leftPaneWidth, win.defaultLeftWidth)
            compare(Backend.settings.rightPaneWidth, win.defaultRightWidth)

            win.width = 1400
            win.height = 860
            tryCompare(games, "width", win.defaultLeftWidth)
            tryCompare(side, "width", win.defaultRightWidth)
        }

        // A confirmation taller than the smallest window scrolls, from the
        // keyboard as well (the focus is on its buttons), and its text leaves
        // room for the scroll bar.
        function test_b4_longConfirmationScrolls() {
            requireFocus()
            win.width = win.minimumWidth
            win.height = win.minimumHeight
            tryCompare(win.contentItem, "height", 440)
            const dialog = named("confirmDialog")
            const files = []
            for (let i = 1; i <= 60; ++i)
                files.push("C:\\Users\\Player\\Documents\\Paradox Interactive\\Stellaris\\file" + i + ".txt")
            dialog.ask("Discard", "These files stay as they are:\n" + files.join("\n"), "Details.", "Discard",
                       true, true, null)
            tryVerify(() => dialog.opened)
            const scroller = findChild(dialog.contentItem, "confirmScroller") ?? dialog.contentItem
            compare(scroller.objectName, "confirmScroller")
            tryVerify(() => scroller.interactive, 2000, "the long confirmation does not scroll")
            const content = scroller.contentItem.children[0]
            verify(content.width <= scroller.width - 12, "the text runs under the scroll bar")
            tryVerify(() => findChild(dialog.footer, "cancelButton").activeFocus)
            const bottom = scroller.contentHeight - scroller.height
            compare(scroller.contentY, 0)
            press(Qt.Key_Down)
            verify(scroller.contentY > 0)
            press(Qt.Key_End)
            compare(scroller.contentY, bottom)
            press(Qt.Key_PageUp)
            verify(scroller.contentY < bottom)
            press(Qt.Key_Home)
            compare(scroller.contentY, 0)
            press(Qt.Key_PageDown)
            verify(scroller.contentY > 0)
            // The buttons still get their keys: Tab, then Escape closes.
            press(Qt.Key_Tab)
            verify(findChild(dialog.footer, "confirmButton").activeFocus)
            press(Qt.Key_Escape)
            tryVerify(() => !dialog.visible)
            win.width = 1400
            win.height = 860
        }

        // A dragged divider stores the width it was dragged to (the stored
        // widths are the chosen ones, SplitView.preferredWidth).
        function test_b2_draggedDividerWidthPersists() {
            const games = named("gamesPane")
            const width = games.width
            const handle = games.mapToItem(win.contentItem, games.width, games.height / 2)
            mouseDrag(win.contentItem, handle.x, handle.y, 40, 0)
            tryCompare(games, "width", width + 40)
            compare(Backend.settings.leftPaneWidth, width + 40)
            mouseDrag(win.contentItem, handle.x + 40, handle.y, -40, 0)
            tryCompare(games, "width", width)
            compare(Backend.settings.leftPaneWidth, width)
        }

        SignalSpy {
            id: notificationSpy
            target: Backend.notifications
            signalName: "posted"
        }

        name: "PmmUi"
        when: windowShown
    }
}
