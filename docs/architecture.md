# Architecture

This document describes how Paradox Mod Manager is structured: the layers and
their responsibilities, how data flows through the main workflows, the
threading model, the error and logging policy, and where state is stored.
Decisions behind the structure are recorded in [`docs/adr/`](adr/README.md).

Implementation status: every layer below is implemented and tested. The
desktop application (`pmm-app`) is the only front end; there is no
command-line tool. The application layer is described in detail in
[`app.md`](app.md), the services in
[`architecture-services.md`](architecture-services.md), the QML interface in
[`ui-contract.md`](ui-contract.md). What has been verified against real games
is listed in the [support matrix](support-matrix.md).

## 1. Layers

Dependencies point downwards only. Everything below `app` is plain C++20
without Qt (ADR 0001), so it builds and tests with the `core-debug` preset.

```text
  ui/ (QML, Pmm.Ui)           presentation only; binds to Backend.* (docs/ui-contract.md)
        |
  src/app (Qt, pmm-app)       composition root (AppRuntime), one services worker thread,
        |                     Backend + models/controllers, QProcess helper host,
        |                     Web API host, clipboard, file dialogs, profile lock
        |
  src/services (Qt-free)      use cases: games and scans, playsets, export/import,
        |                     acquisition coordinator, launch and configuration recovery
        |
  +-----------+-------------+-------------+---------------+-------------+
  |           |             |             |               |             |
  games       providers     sharing       persistence     platform
  EU4 and     Workshop      share codes   SQLite state,   OS paths, files, processes,
  Stellaris   protocol,     (PMM1)        migrations      Steam discovery,
  adapters    state machine |             |               file transactions
  +-----------+-------------+-------------+---------------+
                            |
  core                      domain model, Result/Error, logging, catalog, SHA-256, UTF-8,
                            playset ops, readiness, dependencies, import planning

  tools/steam-helper (pmm-steam-helper) -> providers -> core
```

Within the middle row, adapters may use `platform`; `sharing`, `persistence`
and `providers` depend only on `core`.

Link dependencies as declared in CMake:

| Library | Target | Depends on | Third-party |
| --- | --- | --- | --- |
| core | `pmm_core` / `PMM::core` | nothing | none |
| platform | `pmm_platform` | core | nlohmann/json; Win32 shell32, ole32, uuid, advapi32 |
| sharing | `pmm_sharing` | core | nlohmann/json, zlib |
| persistence | `pmm_persistence` | core | SQLite, nlohmann/json |
| providers | `pmm_providers` | core | nlohmann/json |
| games | `pmm_games` | core, platform | nlohmann/json |
| services | `pmm_services` | core, platform, sharing, persistence, games, providers | none |
| application core | `pmm_appcore` | services | Qt Core, Network |
| application GUI | `pmm_appgui` | appcore | Qt Gui, Qml |
| QML module | `pmm_ui` (`Pmm.Ui`, static plugin `pmm_uiplugin`) | nothing in C++ | Qt Quick, Quick Controls 2 |
| desktop application | `pmm-app` | appgui, the QML plugin | Qt Quick, Quick Controls 2 |
| steam helper | `pmm-steam-helper` | providers | the game's Steam API library, loaded at run time |

## 2. Module responsibilities

| Module | Owns | Key headers |
| --- | --- | --- |
| `core` | Value types (`GameInstallation`, `ModIdentity`, `InstalledMod`, `Playset`, `PlaysetEntry`, `AcquisitionJob`, `LaunchPlan`, ...); `Result`/`Error`/`PMM_TRY`; the game catalog; pure playset editing and view rules (filters never change load order); launch readiness; name-based dependency checks; import planning and unique playset names; logging and redaction; SHA-256; the one UTF-8 implementation (validation, U+FFFD repair, boundary-safe prefixes, UTF-16 conversion) | `model.hpp`, `result.hpp`, `catalog.hpp`, `playset_ops.hpp`, `readiness.hpp`, `dependencies.hpp`, `import_plan.hpp`, `log.hpp`, `utf8.hpp` |
| `platform` | Standard per-user directories; durable atomic writes; process start without a shell and process listing; Steam root, library, app manifest and Workshop manifest discovery (read-only); journaled multi-file transactions | `paths.hpp`, `fs.hpp`, `process.hpp`, `steam.hpp`, `file_transaction.hpp` |
| `sharing` | Share-code protocol v1: strict manifest parsing and canonical serialization, bounded zlib/Base64URL codec, envelope and digest | `share_code.hpp`, `manifest.hpp`, `codec.hpp`, `utf8.hpp` |
| `persistence` | One SQLite database: migrations, upgrade backups, integrity checks, repositories for settings, installations, inventory, playsets (with undo history), acquisition jobs and launch history | `database.hpp`, `*_repository.hpp` ([details](persistence.md)) |
| `providers` | Steam Workshop: helper wire protocol, pure acquisition reducer, EResult classification, Web API metadata request/response, manual fallback links, local mod fingerprints | `helper_protocol.hpp`, `acquisition.hpp`, `workshop_item.hpp`, `web_metadata.hpp`, `fallback.hpp`, `local_fingerprint.hpp` ([details](steam-workshop.md)) |
| `games` | One adapter per game: discovery per OS/store, descriptor parsing, version and capability detection, load-order translation, generated configuration, executable/arguments/environment, process names, compatibility warnings. Adapters return plans; they never write files. EU4 and Stellaris share `DlcLoadJsonAdapter` with per-game traits ([Stellaris](games/stellaris.md), [EU4](games/eu4.md)) | `game_adapter.hpp`, `dlc_load_adapter.hpp`, `eu4.hpp`, `stellaris.hpp` |
| `services` | Qt-free use cases called by the application on one worker thread: game discovery and scans, playset editing with undo, export/import, the acquisition coordinator (helper sessions, Web API identity gate, ticks, persistence), the launch check, launch and configuration recovery, diagnostics, application folders ([details](architecture-services.md)) | `game_service.hpp`, `playset_service.hpp`, `share_service.hpp`, `acquisition_coordinator.hpp`, `launch_service.hpp`, `app_paths.hpp` |
| `app` | Qt composition root (`AppRuntime`), the services worker (`ServiceRunner`), `QProcess` host for the Steam helper, `QNetworkAccessManager` host for Web API metadata, the per-profile lock, GUI-ready snapshots; the `Backend` with the models and controllers of the UI contract, settings and window state, clipboard and file dialogs, the offline QML network factory, the verification switches ([details](app.md)) | `app_runtime.hpp`, `service_runner.hpp`, `session_host.hpp`, `metadata_host.hpp`, `profile_lock.hpp`, `backend.hpp` |
| `ui` | QML module `Pmm.Ui`: the three-column window, panes, dialogs and controls; presentation and interaction only, data shown as plain text ([contract](ui-contract.md), [accessibility](accessibility.md)) | `Main.qml`, `panes/`, `dialogs/`, `controls/` |
| `tools/steam-helper` | One process per game that loads the game's own Steam API library and answers JSON-lines commands | `main.cpp`, `steam_api_loader.hpp` |

Game-specific knowledge lives only in adapters; provider-specific knowledge
only in `providers`. A new game (for example Hearts of Iron IV, already in the
catalog with `adapterAvailable = false`) means a new adapter, fixtures and
validation; a new provider means a new provider module and a new `source` key
(which is part of the share-code value space).

## 3. Data flows

### Export

1. Load the active playset (`PlaysetRepository`).
2. `sharing::manifestFromPlayset(playset, installation.version)` builds and
   validates the manifest: every entry, disabled ones included, in order.
3. `sharing::encodeShareCode(manifest)` produces `PMM1.<payload>.<digest>`,
   deterministic for unchanged content.
4. The app copies it to the clipboard or writes it to a UTF-8 file.

### Import

1. Text from the clipboard or a file goes to `sharing::decodeShareCode()`,
   which runs every envelope, integrity, size and schema check
   ([protocol §1](share-code-protocol.md)). Nothing is persisted and nothing is
   downloaded before it succeeds.
2. `playsetEntriesFromManifest()` and `buildImportPlan()` match the entries
   against the installed inventory by identity and produce the preview:
   counts, missing enabled/disabled entries, unmapped local mods, warnings,
   a unique playset name.
3. On confirmation the exact playset is saved in one database transaction: a
   new playset by default, or an explicit replacement that first snapshots the
   previous state (`replaceWithSnapshot`, reason `import_replace`).
4. With Workshop downloads on, an acquisition job is created (or reused) for
   every missing Workshop entry, enabled or not. With downloads off
   (`--no-steam`, `--read-only`) the import withdraws the jobs it created in
   the same transaction, so the entries stay missing until the user chooses
   **Download missing**. Local entries are never downloaded; they need
   mapping or manual installation.
5. Readiness (`computePlaysetReadiness`) is recomputed as jobs and scans
   progress; failed or unavailable entries stay in the playset, visible.
   Workshop items that are registered with the game but have no content are
   not in the inventory, so their entries read as missing (and can be
   acquired), not as broken.

### Acquire (Steam Workshop)

1. The acquisition coordinator (`services`) asks the application's session
   host to start `pmm-steam-helper --app-id <id> --steam-api <library>` for the
   game, with the library found next to the game
   (`platform::steam::findSteamApiLibrary`).
2. `startAcquisition(job)` yields actions; `helperCommandFor()` turns them into
   protocol lines; helper events become observations through
   `observationFromHelperEvent()`; `reduceAcquisition()` returns the next state,
   further actions and whether to persist the job.
3. Before deciding anything, every run queries the item state, so installed
   items are verified instead of downloaded again and restored jobs are
   reconciled instead of re-subscribed.
4. `verifyWorkshopContent(installPath)` checks the content; only then does a
   job become `Installed`. A rescan updates the inventory and readiness.
5. Identity gate: Subscribe and download requests are sent only for items the
   Web API (`GetPublishedFileDetails`) confirmed for this game. Deleted,
   private or wrong-game items become `Unavailable`; no verdict in time means
   *needs user action*. The manual fallback opens the item in Steam and relies
   on a rescan.
6. After a restart, `AcquisitionJobRepository::listUnfinished()` feeds
   `startAcquisition()` again. The helper is stopped when idle and before the
   game is launched.

### Launch (Play)

1. `LaunchService::check()` collects every blocker without changing
   anything: `evaluateLaunch()` requires every enabled entry to be ready
   (disabled entries never block); the executable must exist; the user-data
   and mod folders must be writable (probed with a temporary file, skipped in
   read-only sessions); running processes (`platform::listProcesses()`) are
   compared with the adapter's `processNames()`, so a running game or a
   conflicting configuration writer (for example the Paradox launcher)
   blocks; an interrupted configuration transaction of this game blocks,
   interrupted transactions of other games are warnings. Blockers are shown
   next to Play.
2. `GameAdapter::buildLaunchPlan()` returns the executable, working directory,
   argument vector, environment additions and the minimal `ConfigWrite`s,
   generated from the existing files so unrelated settings survive. The plan
   is validated.
3. Only then is the game's Steam helper session closed (Steam counts it as the
   game running) and its acquisition jobs parked.
4. `FileTransactionManager::apply()` writes the configuration as one
   journaled unit with backups (ADR 0005).
5. `platform::startDetached()` starts the real game executable with
   `CreateProcessW` or `posix_spawn`, never through a shell or a store URL that
   would reopen the Paradox launcher. Failures carry the OS error in
   `Error::detail`; the configuration written for the attempt is restored and
   the parked jobs resume.
6. The outcome and transaction id are recorded in `launch_history`.

Recovery: on start-up `listIncomplete()` finds transactions interrupted by a
crash; the application shows a notice and offers **Recover** (`recover()`)
next to Play. "Restore pre-launch configuration" calls `restore(txid)` for the
last committed launch transaction.

## 4. Threading model

The application side is described in detail in [`app.md`](app.md)
("Processes and threads").

- **GUI thread**: QML, the `Backend`, models and controllers. Every property
  change and model update is emitted on the GUI thread (UI contract). The GUI
  thread never calls a service.
- **One services worker thread** (`ServiceRunner`, "pmm-services") owns the
  runtime, every service, the Steam helper host (`QProcess`), the Web API host
  (`QNetworkAccessManager`), the acquisition coordinator and its tick timer.
  Calls run in submission order; results return to the GUI thread as
  snapshots through queued calls. Scans, fingerprints, content verification
  and launches all run there.
- **Lower layers are synchronous and reentrant.** They hold no global mutable
  state except the logger (thread-safe sink replacement) and test hooks.
- **Database**: one `persistence::Database` (one SQLite connection), used by
  the worker thread; a recursive mutex serialises every call, and repository
  calls compose inside `Database::transaction()`.
- **One process per profile**: `pmm-app` holds a lock file
  (`<data folder>/pmm-app.lock`, `ProfileLock`) for its whole run. A second
  start on the same profile waits up to 2 s and then reports that the
  profile is in use; a lock left by a crashed process is taken over.
- **Helper processes**: one per game while it has active jobs; a reader thread
  inside the helper keeps the Steam callback pump from blocking on input.
- **File transactions**: `FileTransactionManager` is not thread-safe; there is
  one per journal root, used only by the worker thread, and the profile lock
  keeps other processes out of that journal.
- **Acquisition reducer**: pure and clock-free; the coordinator feeds `Tick`
  about once per second.

## 5. Errors, logging and redaction

- Expected failures return `pmm::Result<T>` / `Status` carrying an `Error`:
  a stable `ErrorCode` (machine name via `errorCodeName()`, e.g.
  `validation_failed`), an actionable user-facing `message`, and a separate
  technical `detail` (OS error text, offsets, paths). Exceptions are reserved
  for programming errors and never cross module boundaries.
- Messages never contain secrets. `detail` may contain personal paths and is
  redacted before it leaves the machine.
- Logging (`core/log.hpp`): records with level, component
  (`games.stellaris`, `providers.workshop`, ...) and key/value fields. The
  application installs a file sink in the log directory; tests install a
  capturing sink. Never log credentials; paths may be logged locally.
- Exported diagnostics go through `redact()`: the home directory becomes `~`,
  the user name `<user>`, and credential-like `key=value` pairs
  (`password`, `token`, `secret`, `api_key`, `auth`, `session`, ...) become
  `<redacted>`.
- The Steam library writes the user's SteamID64 to the helper's stderr. The
  application treats helper stderr as sensitive and must not export it
  unredacted.
- No telemetry. Network traffic is limited to the Steam client (through the
  helper) and key-less Steam Web API metadata requests that contain only
  published-file ids.

## 6. State and file locations

From `platform::standardDirectories()` and [`persistence.md`](persistence.md):

| What | Windows | Linux | macOS |
| --- | --- | --- | --- |
| Database `state.sqlite` (+ `-wal`, `-shm`, `.bak-v<N>`) | `%APPDATA%\ParadoxModManager\` | `$XDG_DATA_HOME/ParadoxModManager/` (default `~/.local/share/...`) | `~/Library/Application Support/ParadoxModManager/` |
| Configuration transaction journals | `transactions\` in the application data directory | `transactions/` in the data directory | `transactions/` in the data directory |
| Profile lock `pmm-app.lock` | application data directory | same | same |
| Logs | `%LOCALAPPDATA%\ParadoxModManager\logs\` | `$XDG_STATE_HOME/ParadoxModManager/logs/` (default `~/.local/state/...`) | `~/Library/Logs/ParadoxModManager/` |
| Cache | `%LOCALAPPDATA%\ParadoxModManager\cache\` | `$XDG_CACHE_HOME/ParadoxModManager/` (default `~/.cache/...`) | `~/Library/Caches/ParadoxModManager/` |
| Game user data (read on scan; written only on Play, through a transaction) | `Documents\Paradox Interactive\<Game>` (Known Folder, so OneDrive redirection is honoured) | `$XDG_DATA_HOME/Paradox Interactive/<Game>` | `~/Documents/Paradox Interactive/<Game>` |
| Steam (read-only) | registry `SteamPath` / `InstallPath`, default `C:\Program Files (x86)\Steam` | `~/.steam/steam`, `$XDG_DATA_HOME/Steam`, Flatpak and Snap paths | `~/Library/Application Support/Steam` |

With `pmm-app --profile <dir>` the database, journals, lock, logs and cache
all live in `<dir>` ([app.md](app.md#files-and-folders)).

The application never writes under a Steam folder, never reads or writes the
Paradox launcher's `launcher-v2.sqlite`, and never patches game binaries.

## 7. Trust boundaries

Untrusted inputs: share codes, mod descriptors, Steam manifests, helper
output and Web API responses. Each is parsed with explicit limits and
validated before use. Nothing from a share code selects an executable,
argument, path or URL; Workshop URLs are built only from validated canonical
ids. Processes are started with an argument vector, never a shell string.
The QML user interface shows every piece of data as plain text, and its
engine refuses network access except for local resources, so a playset or
mod name can never make the window fetch a URL ([app.md](app.md#start-up-sequence)).
