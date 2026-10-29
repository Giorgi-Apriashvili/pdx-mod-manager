# Application services

`src/services/` (library `pmm_services`, namespace `pmm::services`) is the
Qt-free layer that turns the lower modules into the application's use cases:
discover games, edit playsets, export and import share codes, acquire missing
Workshop mods and launch a game. The Qt application (`src/app`) and a future
CLI call these services. The application turns their results into the QML
objects described in [`ui-contract.md`](ui-contract.md).

```text
  src/app (Qt: models, controllers, QProcess, QNetworkAccessManager)
        |  one worker thread, synchronous calls
  src/services  GameService  PlaysetService  ShareService  AcquisitionCoordinator
                LaunchService  playset_view  diagnostics  AppPaths
        |
  core  sharing  persistence  platform  providers  games (adapter interface)
```

The services hold no UI state and run no event loop or thread. They never block
on the network or the Steam helper. Each call reads what it needs from the
database and the filesystem, does its work, persists the result and returns a
value or a `pmm::Result` error with an actionable message.

## Dependency injection

Every service receives a `ServiceContext` (`service_context.hpp`), built by the
composition root (the Qt application, the CLI, or a test):

| Field | Production value | Purpose |
| --- | --- | --- |
| `database` | `Database::open(AppPaths::databaseFile)` | all application state |
| `transactions` | `FileTransactionManager(AppPaths::transactionsDir)` | crash-safe game configuration writes |
| `adapters` | `games::adapters()` (raw pointers, not owned) | game-specific behaviour |
| `environment` | `games::DiscoveryEnvironment::current()` | Steam root, Documents, XDG, home |
| `paths` | `AppPaths::standard()` (or `fromProfileDir()`) | data, journal, logs, cache folders |
| `clock` | `nowUnixMs` | timestamps and timeouts |
| `startProcess` | `platform::startDetached` | starting the game (argv vector, never a shell) |
| `listProcesses` | `platform::listProcesses` | running-instance checks |
| `fingerprint` | `providers::fingerprintDirectory` | local-mod fingerprints for share codes |

`useSystemDefaults(context)` fills in the last four. The services never call
`games::adapters()`, `DiscoveryEnvironment::current()`, the system clock or OS
process APIs themselves, so tests run them against temporary folders, an
in-memory database, fake adapters, a fake Steam helper and a fake process
starter. `AppPaths::fromProfileDir(dir)` puts every application file under one
folder. This gives a clean, isolated profile for tests and for the "import into
a clean application profile" acceptance procedure.

The acquisition coordinator also receives two host interfaces, which the Qt
layer implements:

- `WorkshopSessionHost`: `open(game, appId, steamApiLibrary)`, `send(game,
  HelperCommand)`, `close(game)`. It runs one `pmm-steam-helper` process per game
  (QProcess) and writes `encodeCommand()` lines to its stdin. Each decoded stdout
  line goes to `onHelperEvent()` and the process exit goes to `onHelperExited()`.
  No event may arrive after `close()` returns.
- `WorkshopMetadataHost`: `request(game, ids)` performs the key-less Web API
  `GetPublishedFileDetails` call (QNetworkAccessManager). It parses the reply with
  `providers::parsePublishedFileDetailsResponse()` and passes it to
  `onMetadata()`. A failed request (offline) may go unanswered.

`CoordinatorOptions::helperExecutable` names the helper binary shipped next to
the application. When that file is missing, jobs are told so instead of failing
obscurely.

## Threading model

```text
 GUI thread                         services worker thread (one QThread)
+-------------------+  queued      +---------------------------------------------+
| QML, models,      |  calls  ---> | GameService, PlaysetService, ShareService,   |
| controllers       |              | LaunchService, AcquisitionCoordinator        |
|                   | <--- results | (one instance each, owned by the worker)     |
+-------------------+  (signals)   +----------------------+----------------------+
                                      ^ onHelperEvent       | send / open / close
                                      | onHelperExited      v
                                   QProcess pmm-steam-helper (one per game)
                                      ^ onMetadata          | request
                                      |                     v
                                   QNetworkAccessManager (Web API)
                                   QTimer ~1 s -> coordinator.tick(nowMs)
```

- The application owns one `ServiceContext` and one instance of each service.
  It calls them **only from one dedicated worker thread**. Service instances
  are not thread-safe: `PlaysetService` keeps the undo stacks and
  `AcquisitionCoordinator` keeps the job contexts and helper sessions in memory.
- Every call is synchronous and short: a database transaction, a folder scan,
  a fingerprint, a file transaction. Waiting (helper start-up, Steam callbacks,
  downloads, HTTP) happens as events. The QProcess and network objects live on
  the worker thread, so their signals arrive there and become coordinator calls.
- `Database` is internally serialised and could be used from the GUI thread.
  The GUI still goes through the worker so that it never sees a half-finished
  service operation.
- Host callbacks made from inside `open/send/close` (a fake host or a
  synchronous implementation) are queued and processed before the outer call
  returns, so the coordinator is never re-entered.
- Results are plain values (`GameState`, `PlaysetView`, `ImportPreview`,
  `AcquisitionSnapshot`, `LaunchCheck`, ...). The app copies them to the GUI
  thread and turns them into model updates. Coordinator calls return a
  `CoordinatorUpdate` that says whether job state changed and which games were
  rescanned, so the app knows which models to refresh.

## Services

### GameService (`game_service.hpp`)

- `refreshAll()` / `refreshGame(key)`: for each catalog game with an injected
  adapter, refresh uses a manually configured installation stored in the
  database, re-validated with `inspectInstallation()`. Otherwise it uses the
  first result of `adapter.discover(env)`. The installation is persisted, mods
  are scanned, and the inventory cache is replaced atomically. Problems are
  reported per game in `GameState::scanError`. A failed scan keeps the cached
  inventory, a game that is no longer found drops its stale installation and
  inventory, and a manual folder that became invalid is reported rather than
  silently replaced.
- `rescanMods(key)` rescans the stored installation without rediscovery (used
  after a Workshop item is installed). A rescan drops cached local-mod
  fingerprints: editing a file inside a mod folder, or anywhere in its
  subfolders, leaves the folder's own modification time unchanged on NTFS,
  ext4 and APFS, so nothing cheap proves that a cached fingerprint still
  matches.
- `setManualLocation` / `clearManualLocation`. Nothing is stored when the
  adapter rejects the folders.
- `cachedStates()` / `cachedInventory()`: the last known state, read from the
  database without touching the filesystem, for a fast start-up.
- `importCurrentConfiguration(key)` creates a new active playset from
  `adapter.readCurrentConfiguration()` (for example `dlc_load.json`).

### PlaysetService (`playset_service.hpp`)

- Management: list, create, duplicate, rename (names unique per game, ASCII
  case-insensitive, at most 256 bytes so that they fit into share codes),
  soft delete with a snapshot, `restoreLastDeleted()`, `history()` and
  `restoreHistory()`. Where a name is made unique (create, duplicate, import,
  current configuration), a " (2)" suffix that would not fit shortens the
  name on a character boundary instead.
- The active playset per game is stored in settings
  (`game.<key>.activePlaysetId`), together with `app.lastGameKey`.
- Editing (core `playset_ops`): `setEnabled(indices, on)` returns the
  dependency issues the change introduces. `move`, `moveUp`/`moveDown`
  (selection), `moveBlock` (drag and drop), `addMods` (installed mods,
  appended) and `removeEntries` (snapshotted into the history first).
  Reordering takes the view's `ViewFilter` and is refused with
  `reorderPolicy()`'s explanation while the view is searched, filtered or
  browse-sorted.
- Every edit is saved immediately and pushed onto an in-memory undo stack per
  playset (100 steps) with redo. Undo and redo save too. If the stored entries
  no longer match what the stack expects (an import replaced the playset, a
  restore happened), undo refuses and clears the stack instead of overwriting
  that change. The undo history lasts for the session only; the playset history
  in the database persists.

### playset_view (`playset_view.hpp`)

`buildPlaysetView()` is a pure function that derives what the mod list shows.
Each row has a display name (installed descriptor name, else name hint, else
id), readiness (`core/readiness`: installed content wins over job state,
dependency warnings, plus the adapter's compatibility warnings) and the latest
job. The view also carries the footer counts and the `LaunchReadiness`
verdict. `loadPlaysetView()` reads everything from the database and overlays
the coordinator's live jobs, which carry fresher progress than the coalesced
database rows.

### ShareService (`share_service.hpp`)

- `exportCode(playsetId)`: every entry, disabled ones included, in exact order.
  Installed entries carry their descriptor name and version as hints, and
  installed local mods carry a content fingerprint. The fingerprint is
  recomputed with the injected function on every export (exports are rare and
  `fingerprintDirectory()` enforces size and file-count limits) and cached in
  the inventory for readiness comparisons until the next rescan. When it cannot
  be computed, the entry carries none. `gameVersionHint` is the installed game
  version. Hints are trimmed to what the manifest accepts.
  Identities, order and enabled flags are copied verbatim.
- `previewImport(text)`: decodes, validates and builds the `ImportPlan` against
  the cached inventory of the manifest's game. It is read-only: no database
  write, no job, no download.
- `applyImport(preview, mode, target, name)`: see the import flow below.

### AcquisitionCoordinator (`acquisition_coordinator.hpp`)

The coordinator drives the pure reducer `providers/acquisition.hpp` for the
persisted acquisition jobs. It keeps one `AcquisitionContext` per job and one
helper session per game. Its responsibilities:

- Map events with `observationFromHelperEvent()` and actions with
  `helperCommandFor()`, batching state queries into one command.
- Verify content (`verifyWorkshopContent()`).
- Persist jobs when the reducer asks, coalescing progress-only changes.
- Rescan the inventory when an item becomes Installed.
- Report provider status: `ready`, `steam_not_running`, `not_logged_on`,
  `not_owned`, `unavailable`, with recovery text.
- Offer the clearly labelled manual Workshop page fallback
  (`providers/fallback.hpp`) for jobs that need the user.

It never unsubscribes or deletes anything.

### LaunchService (`launch_service.hpp`)

- `check(game, playset)` collects blockers and warnings without changing
  anything:
  - the installation is present and its executable exists;
  - direct launch is available;
  - every enabled entry is Ready (disabled entries never block; they produce a
    warning);
  - a playset with no enabled mods is allowed, with a warning;
  - the user-data and mod folders are writable;
  - no game instance and no Paradox launcher is running;
  - no interrupted configuration transaction is waiting for recovery.
- `launch()`: see the launch flow below.
- Recovery functions: `interruptedTransactions()`, `recoverInterrupted(txid)`,
  `lastRestorableTransaction(game)` and `restoreTransaction(txid)` (the user's
  "restore pre-launch configuration").

### diagnostics (`diagnostics.hpp`)

`diagnosticsText()` builds the report behind "Copy diagnostics". It covers:

- app and protocol version, OS, database schema;
- interrupted transactions;
- per game: adapter, installation summary and version, capabilities, inventory
  counts, playsets, provider status, recent jobs with errors, recent launches.

The whole text passes through core `redact()`: the home directory becomes `~`,
the user name becomes `<user>`, and credential-like `key=value` pairs are
stripped.

## Data flow: import

```text
 text --previewImport--> decodeShareCode (all protocol checks)
                          -> adapter for the game injected?
                          -> buildImportPlan(entries, cached inventory, playset names)
                          => ImportPreview {manifest | error, plan, gameInstalled}
                             (nothing written)

 user confirms --applyImport(preview, New | Replace, target, name)-->
   validate: preview ok, validateManifest again, adapter, target exists and
             belongs to the game (Replace), name valid and unique
   rebuild the plan against the CURRENT inventory (a rescan may have installed
             something since the preview; installed content is never re-acquired)
   assert plan.toPlayset().entries == manifest entries (exact identity, order, flags)
   Database::transaction {
       New:     PlaysetRepository::create(exact entries)
       Replace: replaceWithSnapshot(target, exact entries, "import_replace")  (restorable)
       for id in plan.toAcquire (missing Workshop entries, disabled ones included):
           AcquisitionJobRepository::createOrReuse(id, playsetId)
       settings: game.<key>.activePlaysetId, app.lastGameKey
   }   any failure rolls back everything: no playset, no job, no selection change
 app: coordinator.start(game)
```

The imported entries are never reordered, re-flagged, filtered or extended.
Extra mods installed on this machine stay out of the playset. Local mods that
are not installed here have no job: they show "needs user action", meaning map
or install them manually. Until content is installed and seen by the inventory,
every missing entry reads Pending, Downloading, Failed, Unavailable or
NeedsUserAction, never Ready. Launching stays blocked while any of them is
enabled.

## Data flow: acquisition

```text
 start(game): listUnfinished() jobs of the game -> startAcquisition() each
              (restart reconciliation: the reducer queries Steam first, so nothing
               is re-subscribed blindly and installed items are verified)
              -> metadata host: request(names, wrong-game / deleted check)

 action QuerySession -> session open? ready event cached -> ProviderReady
                        no installation / no Steam app id / steam_api library missing
                        / helper missing / open() false -> NeedsUserAction (+ fallback)
                        else host.open(game, appId, findSteamApiLibrary(installPath))
 helper "ready" ok   -> provider status ready | not_logged_on | not_owned; jobs re-query
 helper "ready" fail -> provider status steam_not_running | not_logged_on | unavailable;
                        jobs NeedsUserAction (or Failed + automatic retry for crashes)
 actions QueryState / Subscribe / RequestDownload -> outbox -> host.send (state batched)
 helper events -> observationFromHelperEvent -> reduceAcquisition -> persist if asked
 action VerifyContent -> verifyWorkshopContent(installPath) -> ContentVerified
 job Installed -> GameService::rescanMods(game) -> CoordinatorUpdate::rescanned
 tick(nowMs)  -> timeouts, polling, automatic retries, coalesced progress writes
 idle (no job of the game waits for Steam) -> host.close(game)
```

User actions: `retry(job)`, `retryAllFailed(game)` (fresh retry cycles),
`cancelAll(game)` and `openFallback(job)`. `cancelAll` stops application-managed
work only and says that subscriptions and downloads Steam already completed
remain. `openFallback` returns the Steam client and web links; it is a limited,
clearly labelled fallback in which nothing is downloaded for the user, and a
later rescan picks the content up. `acquireMissing(playset)` creates jobs for a
playset's missing Workshop entries outside an import. `snapshot(game)` gives
the jobs (state text, progress, retry and fallback availability), the provider
status and a summary. Idle jobs whose mod a rescan has since found installed
are left out, because usable installed content wins over job state.

While a helper session is open, Steam treats the game as running. Before a
launch, `closeSessionsForLaunch(game)` therefore saves progress, closes the
session and parks the game's unfinished jobs. The next `start(game)` reconciles
them with Steam. `shutdown()` does the same for every game on exit.

## Data flow: launch

```text
 launch(game, playset)
   check() again                       -> blockers: Error (ConflictingProcess,
                                          Interrupted or ValidationFailed); nothing written
   coordinator.closeSessionsForLaunch(game)
   adapter.buildLaunchPlan(installation, enabled InstalledMods in playset order)
   validate plan (absolute existing executable, absolute config targets)
   FileTransactionManager::apply("launch <game> playset <id> (<name>)", configWrites)
       journal, durable backups, atomic replace, rollback on failure
       failure -> Error, game not started, previous configuration unchanged
                  (or, if even the rollback failed, recovery offered)
   startProcess(ProcessSpec{plan.executable, plan.workingDirectory,
                            plan.arguments (argv vector), plan.environment})
       failure -> FileTransactionManager::restore(txid): previous configuration back,
                  ProcessStartFailed with diagnostics (executable, folder, argv, env, OS error)
   LaunchHistoryRepository::record(outcome, pid, transaction id)
   prune old finished transactions (keep 20)
```

The configuration written for a launch stays in effect until the next launch or
until the user restores it with `restoreTransaction()`. The official launcher
overwrites it the next time it is used. After a crash, a transaction left
"prepared" or "applying" blocks launching until `recoverInterrupted()` puts the
backed-up files back. The application should offer this on start-up when
`interruptedTransactions()` is not empty.

## Errors and logging

Expected failures are `pmm::Result` / `Status` with a user-facing message and a
separate technical `detail`. No exception crosses a service boundary.
Repository and adapter errors are wrapped with context ("Scanning the mods of
Stellaris failed. ...", "The import was not saved and nothing was changed.
..."). Services log through core `log` with components such as
`services.games`, `services.share`, `services.acquisition` and
`services.launch`. Exported diagnostics are always redacted.

## Tests

`tests/unit/services` (`pmm_services_tests`, label `pmm_services_tests`) runs
every service against:

- an in-memory database on a fake clock;
- temporary folders whose names contain spaces and non-ASCII characters;
- a fake Stellaris adapter that scans plain folders;
- a simulated Steam helper that subscribes, downloads, fails, refuses sessions
  and crashes on demand;
- a fake process starter and lister.

Fixtures in `tests/fixtures/services` are sanitised descriptors and a
`dlc_load.json` with placeholder names. No test touches a real game, Steam
folder or process.
