# UI contract (QML ↔ C++)

This is the boundary between the C++ application layer (`src/app`, see
[`app.md`](app.md)) and the QML presentation (`ui/`). QML contains no domain
logic. It binds to the objects below and calls their invokables.

All objects live on the GUI thread, and every property change and model
update is emitted there. Service work runs on the services worker thread.
Invokables that need it start the work and return at once; the outcome
arrives through the properties, signals and notifications described below.
A boolean returned by such an invokable only says whether the request was
accepted.

## Module layout

- `ui/CMakeLists.txt` builds a static QML module `pmm_ui` with URI `Pmm.Ui`
  (`qt_add_qml_module(pmm_ui URI Pmm.Ui VERSION 1.0 ...)`). Its entry component
  is `Main.qml` (an `ApplicationWindow`).
- The executable `pmm-app` (`src/app/gui/main.cpp`) links `pmm_uiplugin`,
  uses `Q_IMPORT_QML_PLUGIN(Pmm_UiPlugin)`, registers the backend and calls
  `engine.loadFromModule("Pmm.Ui", "Main")`.
- The backend is registered before loading with
  `qmlRegisterSingletonInstance("Pmm.Backend", 1, 0, "Backend", backend)`. Its
  object types are registered as uncreatable types of `Pmm.Backend`. QML
  uses `import Pmm.Backend` and accesses `Backend.<object>`.
- Qt Quick Controls style: `Basic`, customised through the `Theme` QML
  singleton in `Pmm.Ui` (colours, spacing, fonts). There are light and dark
  palettes; `Theme.dark` follows `Backend.settings.effectiveDark`.
- Strings from this contract are data, not markup: names and hints come from
  share codes, Workshop descriptors and folder names. Every `Text`, `Label`
  or text edit whose text is not a fixed literal sets
  `textFormat: Text.PlainText` (`Text.AutoText` would render a string that
  starts with a tag as rich text and load its images), and the shared tool
  tip shows plain text. `tests/app/ui_text_tests.cpp` checks the sources.
  The engine itself has no network access (`app.md`).
- `ui/dev/Pmm/Backend` is a QML-only stand-in that implements this contract
  for previews, screenshots and QML tests without the C++ build.

## `Backend` (singleton, all properties CONSTANT)

| Property | Type | Purpose |
|---|---|---|
| `games` | `GamesModel` | left sidebar |
| `playsets` | `PlaysetsModel` | playsets of the current game |
| `mods` | `ModListModel` | active playset entries in load order (source model) |
| `modView` | `ModFilterModel` | proxy over `mods` used by the center ListView |
| `inventory` | `InventoryModel` | installed mods of the current game ("Add mods" dialog) |
| `details` | `ModDetails` | selected mod details (right panel) |
| `sharing` | `ShareController` | export/import share codes |
| `acquisition` | `AcquisitionController` | Workshop downloads |
| `launcher` | `LaunchController` | Play |
| `settings` | `AppSettings` | theme, geometry, column widths |
| `notifications` | `NotificationCenter` | transient messages |
| `appVersion` | `string` | semantic version |
| `protocolVersion` | `int` | share-code protocol version (1) |
| `readOnly` | `bool` | read-only verification session (no game-folder writes, no launch, no Steam) |
| `dataDir`, `logsDir` | `string` | folders of the running profile (display) |

Invokable: `openLogsFolder()`.

Row indices passed to `mods` invokables are **source rows** of `mods`
(0-based, load order). The view converts with `modView.sourceRow()`.

Selecting a game restores its last active playset (stored by the services)
and the view state it had in this session (search, status filter, sort).

## `GamesModel` (QAbstractListModel)

Roles:

- `key` (string), `name`;
- `supported` (bool: adapter implemented), `installed` (bool);
- `statusText` ("Installed · v4.5.2", "Installed · v4.5.2 · manual location",
  "Not found", "Not supported yet"), `version`;
- `installPath`, `userDataPath` (display only);
- `manualLocation` (bool: the installation was located manually).

Properties: `currentKey` (string), `currentName`, `currentInstalled` (bool),
`currentSupported` (bool), `scanning` (bool), `count`.

Invokables:
- `select(key)`: switches to a supported game, restoring its last active
  playset and view state.
- `rescan()`: rediscovers installations and rescans mods (`scanning` while it
  runs). Ends with a summary notification; scan problems become warnings.
- `locate(key, installDirUrl, userDataDirUrl) -> bool`: manual location (URLs
  from FolderDialog; userData may be empty for the OS default). Returns false
  when no local install folder was given. The adapter validates the folders
  asynchronously; success and errors go to `notifications`. Nothing is stored
  when the folders are rejected.
- `clearManualLocation(key)`: back to automatic detection.
- `importCurrentConfiguration() -> bool`: creates and activates a new playset
  from the game's current mod configuration (e.g. `dlc_load.json`).

## `PlaysetsModel` (QAbstractListModel)

Roles: `playsetId` (int), `name`, `entryCount`, `enabledCount`, `updatedText`.

Properties:

- `activeId` (int, -1 none), `activeName`, `count`;
- `canRestore` (bool): a playset of the current game deleted in this session
  can be brought back with `restoreLast()`. Replaced playsets are restored
  from the history.

Invokables:

- `activate(id)`;
- `create(name) -> int`, `duplicate(id, name) -> int`: return -1 at once. The
  new playset becomes active when it has been created; names are made
  unique;
- `rename(id, name) -> bool`;
- `remove(id) -> bool`: snapshotted, undoable with `restoreLast()`;
- `restoreLast() -> bool`;
- `history() -> list<{historyId, reason, playsetName, createdText}>`:
  snapshots of the current game, newest first, refreshed with the playset
  list;
- `restoreHistory(historyId) -> bool`.

Failures go to `notifications`.

## `ModListModel` (QAbstractListModel, source model)

Roles:

- `position` (1-based), `enabled` (bool);
- `name` (installed descriptor name, else name hint, else id);
- `source` ("steam-workshop" | "local"), `sourceLabel` ("Workshop" | "Local"),
  `providerId`, `identityKey` ("source:id");
- `status` ("ready" | "missing" | "pending" | "downloading" | "failed" |
  "unavailable" | "needs_user_action" | "broken");
- `statusText` ("Ready", "Missing", "Queued", "Waiting for Steam",
  "Downloading 45 %", "Verifying", "Failed", "Action needed", ...);
- `warnings` (string list), `hasWarnings` (bool);
- `progress` (real, -1 when not downloading);
- `blocking` (bool: enabled and not ready);
- `version`.

Properties: `count`, `enabledCount`, `readyCount`, `missingCount`,
`warningCount`, `blockingCount`, `canUndo`, `canRedo`, `undoText`,
`redoText`, `busy` (edits are being saved).

Invokables. Every edit is persisted and undoable. Edits apply to the model
immediately, then the saved state is reconciled once no other edit is in
flight:
- `setEnabled(row, on)`
- `setEnabledRows(rows, on) -> string`: returns "". Dependency issues that
  enabling introduces arrive through the `dependencyFeedback(text)` signal
  (one issue per line; empty when none) and show as row warnings.
- `moveRow(from, to)`, `moveUp(row)`, `moveDown(row)`.
- `moveRows(rows, delta)`: one atomic, single-undo move of a multi-row
  selection. With `delta` -1/+1, every selected entry moves one step unless
  the list edge or a selected entry that cannot move blocks it (keyboard).
  With larger deltas, the selection is gathered into one block whose first
  entry lands at `rows[0] + delta`, clamped (drag and drop).
- All moves are refused (no-op + notification) when `modView.reorderAllowed`
  is false.
- `removeRows(rows)`: snapshotted first. `addMods(identityKeys)`: keys from
  `InventoryModel.identityKey`, appended at the end, enabled. Adding mods
  already in the playset is refused.
- `undo()`, `redo()`: not applied up front; the model follows when the
  saved state arrives.
- `get(row) -> map` (role name → value; for tests and scripting).

Row arguments name the rows the list shows when the invokable is called. The
services receive the identities of those entries (and, for a move, the entry
it lands in front of), so an edit made while an undo or redo is still in
flight changes the entries the user saw. An edit whose entries that undo or
redo removed is refused with a notification.

Signals: `dependencyFeedback(text)`, `edited()`.

Updates are incremental: `dataChanged` for changed rows, and
`rowsInserted`/`rowsRemoved` for added and removed entries. Only showing
another playset resets the model. **Reorders are reported as `dataChanged`
over the affected range, not as `rowsMoved`.** `QSortFilterProxyModel` turns
source moves into a layout change without a hint, which makes a `ListView`
rebuild its delegates and scroll to the top. Because edits apply
synchronously, the QML re-selects moved rows right after calling a move
invokable.

## `ModFilterModel` (QSortFilterProxyModel over `mods`)

Properties: `search` (string; name or provider id, case-insensitive),
`statusFilter` ("all" | "enabled" | "disabled" | "missing" | "warning"),
`sortMode` ("order" | "name" | "source" | "status"), `reorderAllowed` (bool,
read-only: true only for the unfiltered load-order view),
`reorderBlockedReason` (string). Both reorder properties come from core
`reorderPolicy()`, the rule `PlaysetService` enforces.

Invokables: `sourceRow(proxyRow) -> int`, `proxyRow(sourceRow) -> int`, and
the inherited `index(row, 0)` for `ItemSelectionModel`. Browsing sorts never
change the load order.

## `InventoryModel` (QAbstractListModel)

Roles: `identityKey` ("source:id"), `name`, `source`, `sourceLabel`,
`providerId`, `version`, `inPlayset` (bool), `stateText` ("Installed",
"Installed · update pending", "Updating", "Broken"), `problems` (string).
Rows are sorted by name.

Properties: `count` (rows matching `filter`), `filter` (string). Invokables:
`refresh()` reloads the installed mods.

## `ModDetails` (QObject)

Property `row` (int, source row in `mods`, -1 none; writable). It follows
the row's data.

Read-only properties:

- `hasSelection`, `name`, `source`, `sourceLabel`, `providerId`;
- `version`, `supportedVersion`, `statusText`, `tags` (string list);
- `dependencies`: list of `{name, state, text}`, where state is "ok" |
  "missing" | "not_in_playset" | "disabled" | "order" | "unchecked"
  ("unchecked": the mod is disabled, so its dependencies are not checked);
- `warnings`, `problems` (scan problems, and for entries that are not
  usable, what to do);
- `workshopUrl` (an `https://steamcommunity.com/` page or empty);
- `contentPath` (display), `canOpenFolder`.

Invokables: `openWorkshopPage()`, `openContentFolder()`. They open only the
Workshop page URLs the app builds from validated ids, and existing local
folders (an archive's containing folder).

## `ShareController` (QObject)

Properties:

- `lastExportCode` (string);
- `preview` (map, see below), `hasPreview` (bool);
- `busy` (bool: export, preview or import running);
- `portabilityNote` (string: identical references and order do not
  guarantee identical bytes or multiplayer checksums).

Invokables:
- `exportCode() -> string`: starts exporting the active playset and returns
  the current `lastExportCode`, which is cleared first. The code arrives in
  `lastExportCode` (signal `exported(code)`; empty on failure, with a
  notification).
- `copyExportCode() -> bool`: copies `lastExportCode`. While the export is
  still running, it copies as soon as the code is ready and returns false.
- `exportToFile(url) -> bool`: writes the code (UTF-8, one line).
- `clipboardText() -> string`
- `previewImport(text) -> bool`: decodes and validates without side effects
  (`busy` while running) and then sets `preview`. Returns whether a preview
  was started.
- `importFromFile(url) -> bool`: reads the file (at most 4 MiB) and calls
  `previewImport`.
- `confirmImport(mode, targetPlaysetId, name) -> bool`:
  - `mode` is "new" (default) or "replace" (explicit; the previous state is
    snapshotted and restorable from the history).
  - It persists the exact playset, queues acquisition of missing Workshop
    entries (disabled ones included) and activates the playset. With Steam
    off nothing is queued, not even for a later session: the entries stay
    "Missing" until `acquireMissing()`. Signal `imported(playsetId)`, -1 on
    failure, with a notification.
  - A code for another supported game is imported as a new playset of that
    game (replace is refused), and the window switches to that game.
- `clearPreview()`

`preview` map:

- `ok` (bool), `errorTitle`, `errorMessage`;
- `game`, `gameName`, `gameInstalled` (bool);
- `playsetName`, `proposedName`;
- `total`, `enabled`, `installed`, `missing`, `missingEnabled`,
  `missingDisabled`, `localUnmapped`;
- `warnings` (list; the portability note is not repeated here);
- `entries` (list of `{position, name, source, providerId, enabled,
  statusText}`).

## `AcquisitionController` (QObject)

Properties:

- `jobs` (`JobsModel`), `active` (bool), `summary` (string);
- `providerStatus` ("ready" | "steam_not_running" | "not_logged_on" |
  "not_owned" | "unavailable" | "unknown"; "unknown" until a Steam session
  was opened), `providerMessage` (recovery text);
- `downloadsEnabled` (bool, constant: false when the session runs with
  `--no-steam` or read-only).

`JobsModel` roles: `jobId`, `name`, `providerId`, `state` (AcquisitionState
name), `stateText`, `progress` (real, -1 unknown), `errorMessage`, `canRetry`,
`canOpenFallback`. The model is updated in place by job id.

Invokables: `acquireMissing()`, `retry(jobId)`, `retryAllFailed()`,
`cancelAll()` (explains that completed subscriptions/downloads remain),
`openFallback(jobId)` (the clearly labelled manual Steam page fallback: the
`steam://` page when a handler is registered, else the web page),
`rescanNow()`. With downloads off, they explain that and do nothing.

## `LaunchController` (QObject)

Properties:

- `canPlay` (bool);
- `blockers` (string list, shown next to Play). It always says why Play is
  unavailable: game not installed or not located, no active playset,
  enabled entries not ready, executable missing, folders not writable, a
  running game or Paradox launcher, an interrupted configuration change,
  read-only session;
- `warnings` (string list);
- `busy` (bool), `state` ("idle" | "validating" | "applying" | "starting" |
  "started" | "failed");
- `lastError`, `lastErrorDetail`;
- `hasInterruptedTransaction` (bool), `canRestoreBackup` (bool).

The verdict is refreshed whenever the playset, the inventory or the
downloads change, after a launch, and when the window is activated.

Invokables:

- `play()`;
- `recoverInterrupted() -> bool`, `restoreLastBackup() -> bool`;
- `diagnosticsText() -> string`: the last generated report;
- `copyDiagnostics()`: regenerates the redacted report and copies it.

Signals: `launched(pid)`, `launchFailed(message)`, `diagnosticsReady(text)`.

## `AppSettings` (QObject)

Properties, persisted in the application database's settings table under
`ui.*`:

- `theme` ("system" | "light" | "dark");
- `effectiveDark` (bool, read-only; follows `QStyleHints::colorScheme` while
  the theme is "system");
- `windowX`, `windowY`, `windowWidth`, `windowHeight`, `windowMaximized`;
- `leftPaneWidth`, `rightPaneWidth`;
- `columnWidths` (list of int);
- `lastGameKey` (read-only, set by selecting a game).

They are restored before the window is created. Invokable: `save()` (also
called when the application quits).

## `NotificationCenter` (QObject)

Signal `posted(level, title, text)` where level is "info" | "warning" |
"error". Property `latest` (map `{level, title, text}`). Invokables
`post(level, title, text)` and `dismiss()`. Warnings and errors are also
written to the log.
