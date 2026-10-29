pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQml.Models
import Pmm.Ui
import Pmm.Backend

// Center column: playset selector, search/filter/sort, the virtualized mod
// table with selection, drag-and-drop reordering and bulk actions, and a
// footer with counts. Rows passed to Backend.mods are source rows.
Rectangle {
    id: root

    signal addModsRequested()
    signal importRequested()
    signal newPlaysetRequested()
    signal locateRequested()
    signal removeRequested(list<int> rows, string description)

    // Column widths: #, On, Name, Source, Status. Name takes the remaining width;
    // the stored value at index 2 is informational only.
    readonly property list<real> defaultColumns: [44, 30, 0, 90, 150]
    property list<real> columns: defaultColumns
    readonly property real gripWidth: 18
    readonly property real minNameWidth: 160
    readonly property real fixedWidth: gripWidth + columns[0] + columns[1] + columns[3] + columns[4]
    readonly property real nameWidth: Math.max(minNameWidth, modList.width - fixedWidth)
    readonly property list<real> effectiveWidths: [columns[0], columns[1], nameWidth, columns[3], columns[4]]
    readonly property var filterOptions: [
        { value: "all", text: qsTr("All mods") },
        { value: "enabled", text: qsTr("Enabled") },
        { value: "disabled", text: qsTr("Disabled") },
        { value: "missing", text: qsTr("Missing") },
        { value: "warning", text: qsTr("With warnings") }
    ]
    readonly property var sortOptions: [
        { value: "order", text: qsTr("Load order") },
        { value: "name", text: qsTr("Name") },
        { value: "source", text: qsTr("Source") },
        { value: "status", text: qsTr("Status") }
    ]

    property int selectionRevision: 0
    property int selectedCount: 0
    property int anchorRow: -1
    property bool bulkSelecting: false
    property string dependencyFeedback
    // Focus given programmatically (start-up, after dialogs) shows no list ring
    // until the user moves focus away and back.
    property bool quietListFocus: false

    // Drag-and-drop state (proxy rows; reordering only happens in the unfiltered load-order view).
    property int dragFrom: -1
    property int dropGap: -1
    property bool dragActive: false
    property real dragY: 0
    property string dragName

    readonly property bool hasGame: Backend.games.currentSupported && Backend.games.currentInstalled
    readonly property bool hasPlayset: Backend.playsets.activeId >= 0
    readonly property bool canEdit: hasGame && hasPlayset
    readonly property bool filtered: Backend.modView.search.length > 0 || Backend.modView.statusFilter !== "all"
    // Backend state observed through bindings (works with any NOTIFY signal).
    readonly property string viewSearch: Backend.modView.search
    readonly property bool reorderAllowed: Backend.modView.reorderAllowed
    readonly property string playsetKey: Backend.playsets.activeId + ":" + Backend.playsets.count

    onViewSearchChanged: {
        if (searchField.text !== viewSearch)
            searchField.text = viewSearch
    }
    onReorderAllowedChanged: updateMoveState()
    onPlaysetKeyChanged: Qt.callLater(syncPlaysetIndex)

    // Updated imperatively (updateMoveState) so bulk selection changes do not
    // re-evaluate a scan of the whole selection per selected row.
    property bool canMoveUp: false
    property bool canMoveDown: false
    readonly property bool hasTarget: modList.count > 0 && (selectedCount > 0 || modList.currentIndex >= 0)

    color: Theme.base

    function restoreColumns() {
        const saved = Backend.settings.columnWidths
        if (saved && saved.length === 5) {
            const c = []
            for (let i = 0; i < 5; ++i)
                c.push(i === 2 ? 0 : Math.max(24, Number(saved[i]) || defaultColumns[i]))
            columns = c
        }
    }

    function saveColumns() {
        const c = columns.map(w => Math.round(w))
        c[2] = Math.round(nameWidth)
        Backend.settings.columnWidths = c
    }

    function optionIndex(options: var, value: string): int {
        return options.findIndex(o => o.value === value)
    }

    function syncPlaysetIndex() {
        playsetCombo.currentIndex = playsetCombo.indexOfValue(Backend.playsets.activeId)
    }

    function focusSearch() {
        searchField.forceActiveFocus(Qt.ShortcutFocusReason)
        searchField.selectAll()
    }

    function focusList() {
        quietListFocus = !modList.activeFocus
        modList.forceActiveFocus(Qt.OtherFocusReason)
    }

    // F6 target: the mod list, else the first control of the pane.
    function focusFirst() {
        if (modList.visible) {
            quietListFocus = false
            modList.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        const next = root.nextItemInFocusChain(true)
        if (next)
            next.forceActiveFocus(Qt.TabFocusReason)
    }

    // Spoken by screen readers without moving focus (Qt 6.8+).
    function announce(text: string) {
        modList.Accessible.announce(text)
    }

    function popupContextMenu() {
        const item = modList.currentItem
        if (!item)
            return
        const p = item.mapToItem(listArea, 60, item.height)
        contextMenu.popup(listArea, p.x, p.y)
        contextMenu.currentIndex = 0
    }

    function showLoadOrder() {
        Backend.modView.search = ""
        Backend.modView.statusFilter = "all"
        Backend.modView.sortMode = "order"
    }

    // --- selection -------------------------------------------------------

    function modelIndex(row: int): var {
        return Backend.modView.index(row, 0)
    }

    function selectionUpdated() {
        if (bulkSelecting)
            return
        selectionRevision++
        selectedCount = selection.selectedIndexes.length
        updateMoveState()
    }

    function updateMoveState() {
        const rows = Backend.modView.reorderAllowed && modList.count > 0 ? selectedSourceRows() : []
        canMoveUp = rows.length > 0 && rows[0] > 0
        canMoveDown = rows.length > 0 && rows[rows.length - 1] < Backend.mods.count - 1
    }

    function isSelected(row: int): bool {
        return selection.isSelected(modelIndex(row))
    }

    function setCurrent(row: int) {
        if (row < 0 || row >= modList.count)
            return
        modList.currentIndex = row
        modList.positionViewAtIndex(row, ListView.Contain)
    }

    function selectOnly(row: int) {
        if (row < 0 || row >= modList.count)
            return
        selection.select(modelIndex(row), ItemSelectionModel.ClearAndSelect)
        anchorRow = row
        setCurrent(row)
    }

    function toggleRow(row: int) {
        selection.select(modelIndex(row), ItemSelectionModel.Toggle)
        anchorRow = row
        setCurrent(row)
    }

    function selectRows(rows: list<int>) {
        bulkSelecting = true
        selection.clearSelection()
        for (const r of rows) {
            if (r >= 0 && r < modList.count)
                selection.select(modelIndex(r), ItemSelectionModel.Select)
        }
        bulkSelecting = false
        selectionUpdated()
    }

    function selectRange(row: int) {
        const from = anchorRow >= 0 && anchorRow < modList.count ? anchorRow : row
        const rows = []
        for (let r = Math.min(from, row); r <= Math.max(from, row); ++r)
            rows.push(r)
        selectRows(rows)
        setCurrent(row)
    }

    function selectAll() {
        if (modList.count === 0)
            return
        const rows = []
        for (let r = 0; r < modList.count; ++r)
            rows.push(r)
        selectRows(rows)
        if (modList.currentIndex < 0)
            setCurrent(0)
        modList.forceActiveFocus(Qt.ShortcutFocusReason)
    }

    function navigate(row: int, extend: bool) {
        if (modList.count === 0)
            return
        row = Math.max(0, Math.min(modList.count - 1, row))
        if (extend) {
            if (anchorRow < 0)
                anchorRow = Math.max(0, modList.currentIndex)
            selectRange(row)
        } else {
            selectOnly(row)
        }
    }

    function rowPressed(row: int, modifiers: int, button: int) {
        modList.forceActiveFocus(Qt.MouseFocusReason)
        if (button === Qt.RightButton) {
            if (!isSelected(row))
                selectOnly(row)
            else
                setCurrent(row)
            return
        }
        if (modifiers & Qt.ShiftModifier)
            selectRange(row)
        else if (modifiers & Qt.ControlModifier)
            toggleRow(row)
        else
            selectOnly(row)
    }

    // Selected rows as sorted source rows; falls back to the current row.
    function selectedSourceRows(): list<int> {
        const indexes = selection.selectedIndexes
        const rows = []
        for (let i = 0; i < indexes.length; ++i)
            rows.push(Backend.modView.sourceRow(indexes[i].row))
        if (rows.length === 0 && modList.currentIndex >= 0)
            rows.push(Backend.modView.sourceRow(modList.currentIndex))
        return rows.filter(r => r >= 0).sort((a, b) => a - b)
    }

    // --- actions -----------------------------------------------------------

    function setEnabledSelected(on: bool) {
        const rows = selectedSourceRows()
        if (rows.length === 0)
            return
        // Dependency issues arrive through Backend.mods.dependencyFeedback.
        dependencyFeedback = ""
        Backend.mods.setEnabledRows(rows, on)
    }

    function toggleSelected() {
        const item = modList.currentItem as ModRow
        if (!item)
            return
        const on = !item.modEnabled
        if (selectedCount <= 1 && isSelected(modList.currentIndex) || selectedCount === 0) {
            Backend.mods.setEnabled(Backend.modView.sourceRow(modList.currentIndex), on)
            announce(on ? qsTr("Enabled %1").arg(item.model.name) : qsTr("Disabled %1").arg(item.model.name))
        } else {
            const n = selectedCount
            setEnabledSelected(on)
            announce(on ? qsTr("Enabled %1 mods").arg(n) : qsTr("Disabled %1 mods").arg(n))
        }
    }

    // Ctrl+Space: add the current row to the selection or take it out.
    function toggleCurrentSelected() {
        if (modList.currentIndex >= 0)
            toggleRow(modList.currentIndex)
    }

    function requestRemove() {
        const rows = selectedSourceRows()
        if (rows.length === 0)
            return
        const item = modList.currentItem as ModRow
        const description = rows.length === 1 && item
                ? qsTr("Remove “%1” from the playset?").arg(item.model.name)
                : qsTr("Remove %1 mods from the playset?").arg(rows.length)
        root.removeRequested(rows, description)
    }

    function afterRemove() {
        selection.clear()
        anchorRow = -1
    }

    // The whole selection moves as one undoable step. The backend applies the
    // move to the model right away (and persists it in the background), so the
    // moved rows can be re-selected immediately.
    function moveSelection(delta: int) {
        const rows = selectedSourceRows()
        if (rows.length === 0)
            return
        if (!Backend.modView.reorderAllowed) {
            // The backend refuses and explains why (notification).
            Backend.mods.moveRows(rows, delta)
            return
        }
        if (delta < 0 && rows[0] <= 0 || delta > 0 && rows[rows.length - 1] >= Backend.mods.count - 1)
            return
        const current = modList.currentIndex
        Backend.mods.moveRows(rows, delta)
        const moved = rows.map(r => Backend.modView.proxyRow(r + delta))
        selectRows(moved)
        setCurrent(current + delta)
        if (rows.length === 1) {
            const entry = Backend.mods.get(rows[0] + delta)
            announce(qsTr("%1 moved to position %2").arg(entry.name).arg(entry.position))
        } else {
            announce(delta < 0 ? qsTr("%1 mods moved up").arg(rows.length)
                               : qsTr("%1 mods moved down").arg(rows.length))
        }
    }

    // Drag of a multi-selection: the selection is gathered into one block whose
    // first row lands `delta` rows from where the first selected row was.
    function moveSelectionBlock(rows: list<int>, delta: int) {
        const first = Math.max(0, Math.min(Backend.mods.count - rows.length, rows[0] + delta))
        Backend.mods.moveRows(rows, delta)
        const moved = []
        for (let i = 0; i < rows.length; ++i)
            moved.push(Backend.modView.proxyRow(first + i))
        selectRows(moved)
        setCurrent(moved[0])
    }

    function moveCurrentTo(target: int) {
        const from = modList.currentIndex
        if (from < 0 || target === from)
            return
        const to = Backend.modView.sourceRow(target)
        Backend.mods.moveRow(Backend.modView.sourceRow(from), to)
        selectOnly(target)
        const entry = Backend.mods.get(to)
        announce(qsTr("%1 moved to position %2").arg(entry.name).arg(entry.position))
    }

    function afterModelUpdate() {
        Qt.callLater(selectionUpdated)
        Qt.callLater(updateDetailsRow)
    }

    function updateDetailsRow() {
        const row = modList.currentIndex >= 0 ? Backend.modView.sourceRow(modList.currentIndex) : -1
        if (Backend.details.row !== row)
            Backend.details.row = row
    }

    // --- drag and drop -------------------------------------------------------

    function gapAt(y: real): int {
        const contentPos = y + modList.contentY - modList.originY
        return Math.max(0, Math.min(modList.count, Math.round(contentPos / Theme.rowHeight)))
    }

    function updateDrag(y: real) {
        dragY = y
        dropGap = gapAt(y)
    }

    function finishDrag(commit: bool) {
        autoScroll.stop()
        const from = dragFrom
        const gap = dropGap
        const wasActive = dragActive
        dragFrom = -1
        dropGap = -1
        dragActive = false
        if (!commit || !wasActive || from < 0 || gap < 0)
            return
        const to = gap > from ? gap - 1 : gap
        if (to === from)
            return
        const rows = selectedSourceRows()
        if (rows.length > 1 && isSelected(from)) {
            moveSelectionBlock(rows, to - from)
            return
        }
        Backend.mods.moveRow(Backend.modView.sourceRow(from), Backend.modView.sourceRow(to))
        selectOnly(to)
    }

    Component.onCompleted: {
        restoreColumns()
        searchField.text = Backend.modView.search
        Qt.callLater(syncPlaysetIndex)
    }


    ItemSelectionModel {
        id: selection
        model: Backend.modView
        onSelectionChanged: root.selectionUpdated()
    }

    Connections {
        target: Backend.modView
        // The selection model clears itself silently on reset, so recount too.
        function onModelReset() { root.afterModelUpdate() }
        function onLayoutChanged() { root.afterModelUpdate() }
        function onRowsInserted() { root.afterModelUpdate() }
        function onRowsRemoved() { root.afterModelUpdate() }
        function onRowsMoved() { root.afterModelUpdate() }
    }

    Connections {
        target: Backend.mods
        function onDependencyFeedback(text: string) {
            root.dependencyFeedback = text
            if (text.length > 0)
                root.announce(qsTr("Dependency issues after enabling: %1").arg(text))
        }
    }

    Timer {
        id: searchDebounce
        interval: 160
        onTriggered: Backend.modView.search = searchField.text
    }

    Timer {
        id: autoScroll
        interval: 30
        repeat: true
        onTriggered: {
            const edge = 28
            let step = 0
            if (root.dragY < edge)
                step = -Math.ceil((edge - root.dragY) / 3)
            else if (root.dragY > modList.height - edge)
                step = Math.ceil((root.dragY - (modList.height - edge)) / 3)
            if (step === 0)
                return
            const maxY = modList.originY + Math.max(0, modList.contentHeight - modList.height)
            modList.contentY = Math.max(modList.originY, Math.min(maxY, modList.contentY + step))
            root.updateDrag(root.dragY)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: playset, search, filter, browse sort
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: Theme.pane

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6

                AppComboBox {
                    id: playsetCombo
                    objectName: "playsetSelector"
                    Layout.preferredWidth: 230
                    Layout.minimumWidth: 140
                    enabled: root.hasGame && Backend.playsets.count > 0
                    model: Backend.playsets
                    textRole: "name"
                    valueRole: "playsetId"
                    detailRole: "entryCount"
                    displayText: root.hasPlayset ? Backend.playsets.activeName : qsTr("No playset")
                    accessibleName: qsTr("Active playset")
                    toolTipText: qsTr("Active playset")
                    font.weight: Font.DemiBold
                    onActivated: index => Backend.playsets.activate(valueAt(index))
                }

                AppTextField {
                    id: searchField
                    objectName: "searchField"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 120
                    Layout.maximumWidth: 420
                    iconName: "search"
                    clearable: true
                    placeholderText: qsTr("Search name or ID (Ctrl+F)")
                    Accessible.name: qsTr("Search mods by name or ID")
                    Accessible.description: qsTr("Ctrl+F. Down arrow moves to the list, Escape clears the search.")
                    enabled: root.canEdit
                    onTextEdited: searchDebounce.restart()
                    onCleared: {
                        searchDebounce.stop()
                        Backend.modView.search = ""
                    }
                    Keys.onDownPressed: root.focusList()
                    Keys.onEscapePressed: {
                        if (text.length > 0) {
                            clear()
                            cleared()
                        } else {
                            root.focusList()
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }

                AppComboBox {
                    id: filterCombo
                    objectName: "statusFilter"
                    Layout.preferredWidth: 150
                    enabled: root.canEdit
                    textRole: "text"
                    valueRole: "value"
                    model: root.filterOptions
                    displayText: qsTr("Show: %1").arg(currentText)
                    currentIndex: root.optionIndex(root.filterOptions, Backend.modView.statusFilter)
                    accessibleName: qsTr("Status filter")
                    toolTipText: qsTr("Filter the list by state")
                    onActivated: Backend.modView.statusFilter = currentValue
                }

                AppComboBox {
                    id: sortCombo
                    objectName: "sortSelector"
                    Layout.preferredWidth: 150
                    enabled: root.canEdit
                    textRole: "text"
                    valueRole: "value"
                    model: root.sortOptions
                    displayText: qsTr("Sort: %1").arg(currentText)
                    currentIndex: root.optionIndex(root.sortOptions, Backend.modView.sortMode)
                    accessibleName: qsTr("Browse sort")
                    toolTipText: qsTr("Sort for browsing; the load order itself is not changed")
                    onActivated: Backend.modView.sortMode = currentValue
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.divider
            }
        }

        // Toolbar: add, bulk actions, move, undo/redo
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 34
            color: Theme.pane

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 2

                AppButton {
                    text: qsTr("Add mods…")
                    iconName: "plus"
                    variant: "flat"
                    enabled: root.canEdit
                    toolTipText: qsTr("Add installed mods to this playset (Ctrl+Shift+A)")
                    onClicked: root.addModsRequested()
                }
                ToolSeparator {}
                AppButton {
                    text: qsTr("Enable")
                    iconName: "check"
                    variant: "flat"
                    enabled: root.hasTarget
                    toolTipText: qsTr("Enable the selected mods (Space toggles)")
                    Accessible.name: qsTr("Enable selected mods")
                    onClicked: root.setEnabledSelected(true)
                }
                AppButton {
                    text: qsTr("Disable")
                    iconName: "minus"
                    variant: "flat"
                    enabled: root.hasTarget
                    toolTipText: qsTr("Disable the selected mods (Space toggles)")
                    Accessible.name: qsTr("Disable selected mods")
                    onClicked: root.setEnabledSelected(false)
                }
                AppButton {
                    text: qsTr("Remove")
                    iconName: "trash"
                    variant: "flat"
                    enabled: root.hasTarget
                    toolTipText: qsTr("Remove the selected mods from this playset (Delete). They stay installed.")
                    Accessible.name: qsTr("Remove selected mods from playset")
                    onClicked: root.requestRemove()
                }
                ToolSeparator {}
                AppButton {
                    text: qsTr("Move up")
                    iconName: "arrow-up"
                    variant: "flat"
                    enabled: root.canMoveUp
                    toolTipText: Backend.modView.reorderAllowed ? qsTr("Load earlier (Alt+Up or Ctrl+Up)")
                                                                : Backend.modView.reorderBlockedReason
                    Accessible.name: qsTr("Move up in load order")
                    onClicked: root.moveSelection(-1)
                }
                AppButton {
                    text: qsTr("Move down")
                    iconName: "arrow-down"
                    variant: "flat"
                    enabled: root.canMoveDown
                    toolTipText: Backend.modView.reorderAllowed ? qsTr("Load later (Alt+Down or Ctrl+Down)")
                                                                : Backend.modView.reorderBlockedReason
                    Accessible.name: qsTr("Move down in load order")
                    onClicked: root.moveSelection(1)
                }

                Item {
                    Layout.fillWidth: true
                }

                Text {
                    visible: root.selectedCount > 1
                    text: qsTr("%1 selected").arg(Theme.formatCount(root.selectedCount))
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                    textFormat: Text.PlainText
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textMuted
                    rightPadding: 6
                }
                AppButton {
                    iconName: "undo"
                    variant: "flat"
                    enabled: Backend.mods.canUndo
                    toolTipText: Backend.mods.canUndo ? qsTr("%1 (Ctrl+Z)").arg(Backend.mods.undoText) : qsTr("Nothing to undo")
                    Accessible.name: qsTr("Undo")
                    onClicked: Backend.mods.undo()
                }
                AppButton {
                    iconName: "redo"
                    variant: "flat"
                    enabled: Backend.mods.canRedo
                    toolTipText: Backend.mods.canRedo ? qsTr("%1 (Ctrl+Shift+Z)").arg(Backend.mods.redoText) : qsTr("Nothing to redo")
                    Accessible.name: qsTr("Redo")
                    onClicked: Backend.mods.redo()
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.divider
            }
        }

        // Slim banners
        Banner {
            Layout.fillWidth: true
            Layout.margins: 6
            Layout.bottomMargin: 0
            visible: root.canEdit && !Backend.modView.reorderAllowed && Backend.mods.count > 0
            level: "info"
            iconName: "info"
            text: Backend.modView.reorderBlockedReason
            LinkButton {
                text: qsTr("Show load order")
                toolTipText: qsTr("Clear search and filter and sort by load order")
                onClicked: root.showLoadOrder()
            }
        }
        Banner {
            Layout.fillWidth: true
            Layout.margins: 6
            Layout.bottomMargin: 0
            visible: root.dependencyFeedback.length > 0
            level: "warning"
            title: qsTr("Dependency issues after enabling")
            text: root.dependencyFeedback
            closable: true
            onClosed: root.dependencyFeedback = ""
        }

        ModColumnHeader {
            objectName: "columnHeader"
            Layout.fillWidth: true
            Layout.topMargin: visible && (root.dependencyFeedback.length > 0 || !Backend.modView.reorderAllowed) ? 6 : 0
            visible: root.canEdit && Backend.mods.count > 0
            gripWidth: root.gripWidth
            widths: root.effectiveWidths
            sortMode: Backend.modView.sortMode
            onSortRequested: mode => Backend.modView.sortMode = mode
            onResized: (column, width) => {
                const c = root.columns.slice()
                // Never squeeze the stretching Name column below its minimum.
                const others = root.fixedWidth - c[column]
                c[column] = Math.max(24, Math.min(width, modList.width - others - root.minNameWidth))
                root.columns = c
            }
            onResizeFinished: root.saveColumns()
        }

        // Table
        Item {
            id: listArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ListView {
                id: modList
                objectName: "modList"
                anchors.fill: parent
                visible: root.canEdit
                model: Backend.modView
                reuseItems: true
                cacheBuffer: Theme.rowHeight * 20
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: false
                highlightFollowsCurrentItem: false
                currentIndex: -1
                activeFocusOnTab: true
                focus: true
                ScrollBar.vertical: AppScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Mods in playset %1").arg(Backend.playsets.activeName)
                Accessible.description: qsTr("Space toggles, Alt+Up and Alt+Down move, Delete removes, "
                                             + "Shift+F10 opens the context menu")

                onActiveFocusChanged: {
                    if (!activeFocus)
                        root.quietListFocus = false
                }
                onCurrentIndexChanged: {
                    root.updateDetailsRow()
                    root.updateMoveState()
                }
                onCountChanged: {
                    if (currentIndex >= count)
                        currentIndex = count - 1
                }

                delegate: ModRow {
                    width: modList.width
                    gripWidth: root.gripWidth
                    widths: root.effectiveWidths
                    reorderAllowed: Backend.modView.reorderAllowed
                    listHasFocus: modList.activeFocus
                    isCurrent: ListView.isCurrentItem
                    selectionRevision: root.selectionRevision
                    selectionModel: selection
                    dragging: root.dragActive && root.dragFrom === index
                    onRowPressed: (row, modifiers, button) => root.rowPressed(row, modifiers, button)
                    onToggleRequested: row => {
                        root.rowPressed(row, 0, Qt.LeftButton)
                        Backend.mods.setEnabled(Backend.modView.sourceRow(row), !modEnabled)
                    }
                    onContextMenuRequested: (row, x, y) => {
                        const p = mapToItem(listArea, x, y)
                        contextMenu.popup(listArea, p.x, p.y)
                    }
                }

                // Alt or Ctrl with Up/Down moves the selection in the load order.
                Keys.onPressed: event => {
                    const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                    const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                    const move = ctrl || (event.modifiers & Qt.AltModifier) !== 0
                    const page = Math.max(1, Math.floor(height / Theme.rowHeight) - 1)
                    switch (event.key) {
                    case Qt.Key_Up:
                        if (move) root.moveSelection(-1)
                        else root.navigate(currentIndex - 1, shift)
                        break
                    case Qt.Key_Down:
                        if (move) root.moveSelection(1)
                        else root.navigate(currentIndex < 0 ? 0 : currentIndex + 1, shift)
                        break
                    case Qt.Key_PageUp:
                        root.navigate(currentIndex - page, shift)
                        break
                    case Qt.Key_PageDown:
                        root.navigate(currentIndex + page, shift)
                        break
                    case Qt.Key_Home:
                        root.navigate(0, shift)
                        break
                    case Qt.Key_End:
                        root.navigate(count - 1, shift)
                        break
                    case Qt.Key_Space:
                        if (ctrl) root.toggleCurrentSelected()
                        else root.toggleSelected()
                        break
                    case Qt.Key_Delete:
                        root.requestRemove()
                        break
                    case Qt.Key_Menu:
                        root.popupContextMenu()
                        break
                    case Qt.Key_F10:
                        if (!shift)
                            return
                        root.popupContextMenu()
                        break
                    default:
                        return
                    }
                    root.quietListFocus = false
                    event.accepted = true
                }
            }

            // Keyboard focus on the list before a row is current.
            FocusRing {
                anchors.fill: modList
                inset: 1
                baseRadius: 1
                visible: modList.visible && modList.activeFocus && modList.currentIndex < 0
                         && !root.quietListFocus
            }

            // Drag handle column: press on a grip and drag to reorder.
            MouseArea {
                id: dragArea
                objectName: "dragHandleArea"
                x: 0
                y: modList.y
                width: root.gripWidth
                height: Math.min(modList.height, modList.contentHeight - modList.contentY + modList.originY)
                visible: modList.visible
                enabled: Backend.modView.reorderAllowed && modList.count > 1
                hoverEnabled: true
                preventStealing: true
                cursorShape: root.dragActive ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property real pressY: 0

                onPressed: mouse => {
                    const row = modList.indexAt(4, mouse.y + modList.contentY)
                    if (row < 0)
                        return
                    root.rowPressed(row, mouse.modifiers, mouse.button)
                    root.dragFrom = row
                    root.dragName = (modList.itemAtIndex(row) as ModRow)?.model.name ?? ""
                    pressY = mouse.y
                }
                onPositionChanged: mouse => {
                    if (root.dragFrom < 0)
                        return
                    if (!root.dragActive) {
                        if (Math.abs(mouse.y - pressY) < 4)
                            return
                        root.dragActive = true
                        autoScroll.start()
                    }
                    root.updateDrag(mouse.y)
                }
                onReleased: root.finishDrag(true)
                onCanceled: root.finishDrag(false)

                ToolTip.visible: containsMouse && !pressed && !root.dragActive
                ToolTip.delay: 900
                ToolTip.text: qsTr("Drag to change the load order (keyboard: Alt+Up / Alt+Down)")
            }

            // Insertion marker
            Item {
                visible: root.dragActive && root.dropGap >= 0
                x: 2
                width: modList.width - 4
                height: 2
                y: root.dropGap * Theme.rowHeight + modList.originY - modList.contentY - 1
                z: 5

                Rectangle {
                    anchors.fill: parent
                    color: Theme.accent
                }
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.base
                    border.width: 2
                    border.color: Theme.accent
                }
            }

            // Drag ghost
            Rectangle {
                visible: root.dragActive
                z: 6
                x: root.gripWidth + 10
                y: Math.max(0, Math.min(listArea.height - height, root.dragY - height / 2))
                width: Math.min(360, ghostText.implicitWidth + 36)
                height: Theme.rowHeight
                radius: Theme.radius
                color: Theme.base
                border.color: Theme.accent
                opacity: 0.95

                Icon {
                    id: ghostIcon
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    name: "grip"
                    size: 12
                    color: Theme.accent
                }
                Text {
                    id: ghostText
                    anchors.left: ghostIcon.right
                    anchors.leftMargin: 6
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.dragName
                    textFormat: Text.PlainText
                    font.pixelSize: Theme.fontSize
                    color: Theme.text
                    elide: Text.ElideRight
                }
            }

            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 420)
                visible: !root.hasGame
                iconName: Backend.games.currentSupported ? "circle-alert" : "layers"
                title: Backend.games.currentSupported
                       ? qsTr("%1 was not found").arg(Backend.games.currentName)
                       : qsTr("Select a game")
                text: Backend.games.currentSupported
                      ? qsTr("Locate the installation folder, or rescan if you just installed it.")
                      : qsTr("Choose a supported game on the left to manage its mods.")
                AppButton {
                    visible: Backend.games.currentSupported
                    text: qsTr("Locate installation…")
                    iconName: "locate"
                    variant: "accent"
                    onClicked: root.locateRequested()
                }
                AppButton {
                    visible: Backend.games.currentSupported
                    text: qsTr("Rescan")
                    iconName: "refresh"
                    onClicked: Backend.games.rescan()
                }
            }

            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 440)
                visible: root.hasGame && !root.hasPlayset
                iconName: "layers"
                title: qsTr("No playset yet")
                text: qsTr("Create a playset, import a share code, or start from the mods the game currently has enabled.")
                AppButton {
                    text: qsTr("New playset")
                    iconName: "plus"
                    variant: "accent"
                    onClicked: root.newPlaysetRequested()
                }
                AppButton {
                    text: qsTr("Import code…")
                    iconName: "import"
                    onClicked: root.importRequested()
                }
                AppButton {
                    text: qsTr("Use game configuration")
                    toolTipText: qsTr("Create a playset from the mods currently enabled in the game's own configuration")
                    onClicked: Backend.games.importCurrentConfiguration()
                }
            }

            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 420)
                visible: root.canEdit && Backend.mods.count === 0
                iconName: "plus"
                title: qsTr("This playset is empty")
                text: qsTr("Add installed mods, or import a share code to fill it.")
                AppButton {
                    text: qsTr("Add mods…")
                    iconName: "plus"
                    variant: "accent"
                    onClicked: root.addModsRequested()
                }
                AppButton {
                    text: qsTr("Import code…")
                    iconName: "import"
                    onClicked: root.importRequested()
                }
            }

            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 420)
                visible: root.canEdit && Backend.mods.count > 0 && modList.count === 0
                iconName: "search"
                title: qsTr("No mods match")
                text: qsTr("Nothing in this playset matches the current search and filter.")
                AppButton {
                    text: qsTr("Clear search and filter")
                    onClicked: root.showLoadOrder()
                }
            }
        }

        // Footer
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 24
            color: Theme.pane

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 14
                visible: root.canEdit

                FooterStat {
                    text: qsTr("%1 mods").arg(Theme.formatCount(Backend.mods.count))
                    strong: true
                }
                FooterStat {
                    iconName: "check"
                    iconColor: Theme.accent
                    text: qsTr("%1 enabled").arg(Theme.formatCount(Backend.mods.enabledCount))
                }
                FooterStat {
                    iconName: "check-circle"
                    iconColor: Theme.success
                    text: qsTr("%1 ready").arg(Theme.formatCount(Backend.mods.readyCount))
                }
                FooterStat {
                    iconName: "circle-alert"
                    iconColor: Backend.mods.missingCount > 0 ? Theme.danger : Theme.textFaint
                    text: qsTr("%1 missing").arg(Theme.formatCount(Backend.mods.missingCount))
                }
                FooterStat {
                    iconName: "triangle-alert"
                    iconColor: Backend.mods.warningCount > 0 ? Theme.warning : Theme.textFaint
                    text: qsTr("%1 with warnings").arg(Theme.formatCount(Backend.mods.warningCount))
                }
                Item {
                    Layout.fillWidth: true
                }
                FooterStat {
                    visible: modList.count !== Backend.mods.count
                    text: qsTr("Showing %1 of %2").arg(Theme.formatCount(modList.count))
                                                   .arg(Theme.formatCount(Backend.mods.count))
                }
                FooterStat {
                    visible: Backend.games.scanning
                    text: qsTr("Scanning…")
                }
            }
        }
    }

    AppMenu {
        id: contextMenu

        AppMenuItem {
            text: qsTr("Enable")
            iconName: "check"
            shortcutText: qsTr("Space")
            onTriggered: root.setEnabledSelected(true)
        }
        AppMenuItem {
            text: qsTr("Disable")
            iconName: "minus"
            onTriggered: root.setEnabledSelected(false)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Move up")
            iconName: "arrow-up"
            shortcutText: qsTr("Alt+Up")
            enabled: root.canMoveUp
            onTriggered: root.moveSelection(-1)
        }
        AppMenuItem {
            text: qsTr("Move down")
            iconName: "arrow-down"
            shortcutText: qsTr("Alt+Down")
            enabled: root.canMoveDown
            onTriggered: root.moveSelection(1)
        }
        AppMenuItem {
            text: qsTr("Move to top")
            enabled: root.canMoveUp && root.selectedCount <= 1
            onTriggered: root.moveCurrentTo(0)
        }
        AppMenuItem {
            text: qsTr("Move to bottom")
            enabled: root.canMoveDown && root.selectedCount <= 1
            onTriggered: root.moveCurrentTo(modList.count - 1)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Open Workshop page")
            iconName: "external"
            enabled: Backend.details.workshopUrl.length > 0
            onTriggered: Backend.details.openWorkshopPage()
        }
        AppMenuItem {
            text: qsTr("Open folder")
            iconName: "folder"
            enabled: Backend.details.canOpenFolder
            onTriggered: Backend.details.openContentFolder()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Remove from playset…")
            iconName: "trash"
            shortcutText: qsTr("Del")
            danger: true
            onTriggered: root.requestRemove()
        }
    }
}
