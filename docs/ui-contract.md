# UI contract (QML ↔ C++)

This is the boundary between the C++ application layer (`src/app`) and the QML
presentation (`ui/`). QML contains no domain logic: it binds to the objects
below and calls their invokables. C++ guarantees that every property change and
model update is emitted on the GUI thread; long work runs on worker threads and
reports back through these objects.

## Module layout

- `ui/CMakeLists.txt` builds a static QML module `pmm_ui` with URI `Pmm.Ui`
  (`qt_add_qml_module(pmm_ui URI Pmm.Ui VERSION 1.0 ...)`). Its entry component
  is `Main.qml` (an `ApplicationWindow`).
- The executable `pmm-app` (in `src/app`) links `pmm_uiplugin`, uses
  `Q_IMPORT_QML_PLUGIN(Pmm_UiPlugin)`, registers the backend and loads
  `engine.loadFromModule("Pmm.Ui", "Main")`.
- The backend is registered before loading with
  `qmlRegisterSingletonInstance("Pmm.Backend", 1, 0, "Backend", backend)`.
  QML uses `import Pmm.Backend` and accesses `Backend.<object>`.
- Qt Quick Controls style: `Basic`, customised through the `Theme` QML
  singleton in `Pmm.Ui` (colours, spacing, fonts). Light and dark palettes;
  `Theme.dark` follows `Backend.settings.effectiveDark`.

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

Row indices passed to `mods` invokables are **source rows** of `mods`
(0-based, load order). The view converts with `modView.sourceRow()`.

## `GamesModel` (QAbstractListModel)

Roles: `key` (string), `name`, `supported` (bool: adapter implemented),
`installed` (bool), `statusText` ("Installed · v4.5.2", "Not found",
"Not supported yet"), `version`, `installPath`, `userDataPath` (display only).

Properties: `currentKey` (string), `currentName`, `currentInstalled` (bool),
`currentSupported` (bool), `scanning` (bool).

Invokables:
- `select(key)`: switches game; restores its last active playset and view state.
- `rescan()`: rediscovers installations and rescans mods (async; `scanning`).
- `locate(key, installDirUrl, userDataDirUrl) -> bool`: manual location (URLs
  from FolderDialog; userData may be empty for the OS default). Errors go to
  `notifications`.
- `clearManualLocation(key)`.
- `importCurrentConfiguration() -> bool`: creates a new playset from the game's
  current mod configuration (e.g. `dlc_load.json`).

## `PlaysetsModel` (QAbstractListModel)

Roles: `playsetId` (int), `name`, `entryCount`, `enabledCount`, `updatedText`.

Properties: `activeId` (int, -1 none), `activeName`, `count`, `canRestore`
(bool: a deleted/replaced playset can be restored).

Invokables: `activate(id)`, `create(name) -> int`, `duplicate(id, name) -> int`,
`rename(id, name) -> bool`, `remove(id) -> bool` (snapshotted, undoable),
`restoreLast() -> bool`, `history() -> list<{historyId, reason, playsetName, createdText}>`,
`restoreHistory(historyId) -> bool`.

## `ModListModel` (QAbstractListModel, source model)

Roles: `position` (1-based), `enabled` (bool), `name` (installed descriptor
name, else name hint, else id), `source` ("steam-workshop" | "local"),
`sourceLabel` ("Workshop" | "Local"), `providerId`, `status`
("ready" | "missing" | "pending" | "downloading" | "failed" | "unavailable" |
"needs_user_action" | "broken"), `statusText` ("Ready", "Missing",
"Downloading 45 %", ...), `warnings` (string list), `hasWarnings` (bool),
`progress` (real, -1 when not downloading), `blocking` (bool: enabled and not
ready).

Properties: `count`, `enabledCount`, `readyCount`, `missingCount`,
`warningCount`, `canUndo`, `canRedo`, `undoText`, `redoText`.

Invokables (all persist immediately and are undoable):
- `setEnabled(row, on)`
- `setEnabledRows(rows, on) -> string` returns dependency feedback for bulk
  enable (empty when none) and also surfaces it via warnings.
- `moveRow(from, to)`, `moveUp(row)`, `moveDown(row)`: refused (no-op +
  notification) when `modView.reorderAllowed` is false.
- `removeRows(rows)` (snapshot first), `addMods(identityKeys)` (keys from
  `InventoryModel.identityKey`, appended at the end, disabled duplicates refused).
- `undo()`, `redo()`.

## `ModFilterModel` (QSortFilterProxyModel over `mods`)

Properties: `search` (string), `statusFilter` ("all" | "enabled" | "disabled" |
"missing" | "warning"), `sortMode` ("order" | "name" | "source" | "status"),
`reorderAllowed` (bool, read-only: true only for unfiltered load-order view),
`reorderBlockedReason` (string).

Invokables: `sourceRow(proxyRow) -> int`, `proxyRow(sourceRow) -> int`.
Browsing sorts never change load order.

## `InventoryModel` (QAbstractListModel)

Roles: `identityKey` ("source:id"), `name`, `source`, `sourceLabel`,
`providerId`, `version`, `inPlayset` (bool), `stateText`, `problems` (string).

Properties: `count`, `filter` (string). Invokables: `refresh()`.

## `ModDetails` (QObject)

Property `row` (int, source row in `mods`, -1 none; writable).
Read-only: `hasSelection`, `name`, `source`, `sourceLabel`, `providerId`,
`version`, `supportedVersion`, `statusText`, `tags` (string list),
`dependencies` (list of `{name, state, text}` where state is "ok" | "missing" |
"not_in_playset" | "disabled" | "order"), `warnings`, `problems`, `workshopUrl`
(https URL or empty), `contentPath` (display), `canOpenFolder`.

Invokables: `openWorkshopPage()`, `openContentFolder()`.

## `ShareController` (QObject)

Properties: `lastExportCode` (string), `preview` (map, see below),
`hasPreview` (bool), `busy` (bool), `portabilityNote` (string: identical
references/order do not guarantee identical bytes or multiplayer checksums).

Invokables:
- `exportCode() -> string` (active playset; also sets `lastExportCode`)
- `copyExportCode() -> bool`, `exportToFile(url) -> bool`
- `clipboardText() -> string`
- `previewImport(text) -> bool`: decodes and validates without side effects;
  sets `preview`.
- `importFromFile(url) -> bool`: reads the file and calls `previewImport`.
- `confirmImport(mode, targetPlaysetId, name) -> bool`: `mode` is "new"
  (default) or "replace" (explicit; previous state snapshotted). Persists the
  exact playset, queues acquisition of missing Workshop entries (including
  disabled ones), and activates the playset.
- `clearPreview()`

`preview` map: `ok` (bool), `errorTitle`, `errorMessage`, `game`, `gameName`,
`playsetName`, `proposedName`, `total`, `enabled`, `installed`, `missing`,
`missingEnabled`, `missingDisabled`, `localUnmapped`, `warnings` (list),
`entries` (list of `{position, name, source, providerId, enabled, statusText}`).

## `AcquisitionController` (QObject)

Properties: `jobs` (`JobsModel`), `active` (bool), `summary` (string),
`providerStatus` ("ready" | "steam_not_running" | "not_logged_on" |
"not_owned" | "unavailable" | "unknown"), `providerMessage` (recovery text).

`JobsModel` roles: `jobId`, `name`, `providerId`, `state` (AcquisitionState
name), `stateText`, `progress` (real, -1 unknown), `errorMessage`, `canRetry`,
`canOpenFallback`.

Invokables: `acquireMissing()`, `retry(jobId)`, `retryAllFailed()`,
`cancelAll()` (explains that completed subscriptions/downloads remain),
`openFallback(jobId)` (clearly labelled manual Steam page fallback),
`rescanNow()`.

## `LaunchController` (QObject)

Properties: `canPlay` (bool), `blockers` (string list, shown next to Play),
`warnings` (string list), `busy` (bool), `state` ("idle" | "validating" |
"applying" | "starting" | "started" | "failed"), `lastError`,
`lastErrorDetail`, `hasInterruptedTransaction` (bool), `canRestoreBackup` (bool).

Invokables: `play()`, `recoverInterrupted() -> bool`,
`restoreLastBackup() -> bool`, `diagnosticsText() -> string` (redacted),
`copyDiagnostics()`.

Signals: `launched(pid)`, `launchFailed(message)`.

## `AppSettings` (QObject)

Properties (persisted in the application database): `theme` ("system" |
"light" | "dark"), `effectiveDark` (bool, read-only), `windowX`, `windowY`,
`windowWidth`, `windowHeight`, `windowMaximized`, `leftPaneWidth`,
`rightPaneWidth`, `columnWidths` (list of int), `lastGameKey` (read-only).

Invokables: `save()` (also saved on quit).

## `NotificationCenter` (QObject)

Signal `posted(level, title, text)` where level is "info" | "warning" | "error".
Property `latest` (map `{level, title, text}`). Invokable `dismiss()`.
