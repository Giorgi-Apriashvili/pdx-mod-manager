# Architecture

This document describes how Paradox Mod Manager is structured: the layers and
their responsibilities, how data flows through the main workflows, the
threading model, the error and logging policy, and where state is stored.
Decisions behind the structure are recorded in [`docs/adr/`](adr/README.md).

Implementation status: `core`, `platform`, `sharing`, `persistence`,
`providers` and the Steam helper are implemented and unit tested. The game
adapters (`games`), the Qt-free use-case layer (`services`), the Qt application
(`app`, `ui`) and the command-line tool `pmm-cli` are **in progress**; where
this document describes them it states the intended design, which follows the
interfaces that already exist (`games/game_adapter.hpp`,
[`docs/ui-contract.md`](ui-contract.md)).

## 1. Layers

Dependencies point downwards only. Everything below `app` is plain C++20
without Qt (ADR 0001), so it builds and tests with the `core-debug` preset.

```text
  ui/ (QML, Pmm.Ui)           presentation only; binds to Backend.* (docs/ui-contract.md)
        |
  src/app (Qt, pmm-app)       composition root, QML models/controllers, worker threads,
        |                     clipboard, file dialogs, HTTP           pmm-cli (in progress)
        |                                                                   |
  src/services (Qt-free)      use cases: scan, export/import, acquisition driver,
        |                     launch, configuration recovery          (in progress)
        |
  +-----------+-------------+-------------+---------------+-------------+
  |           |             |             |               |             |
  games       providers     sharing       persistence     platform
  adapters    Workshop      share codes   SQLite state,   OS paths, files, processes,
  (in prog.)  protocol,     (PMM1)        migrations      Steam discovery,
  |           state machine |             |               file transactions
  +-----------+-------------+-------------+---------------+
                            |
  core                      domain model, Result/Error, logging, catalog, SHA-256,
                            playset ops, readiness, dependencies, import planning

  tools/steam-helper (pmm-steam-helper) -> providers -> core
```

Within the middle row, adapters may use `platform`; `sharing`, `persistence`
and `providers` depend only on `core`.

Link dependencies as declared in CMake today:

| Library | Target | Depends on | Third-party |
| --- | --- | --- | --- |
| core | `pmm_core` / `PMM::core` | nothing | none |
| platform | `pmm_platform` | core | nlohmann/json; Win32 shell32, ole32, uuid, advapi32 |
| sharing | `pmm_sharing` | core | nlohmann/json, zlib |
| persistence | `pmm_persistence` | core | SQLite, nlohmann/json |
| providers | `pmm_providers` | core | nlohmann/json |
| games | `pmm_games` | core (interface only so far) | none |
| steam helper | `pmm-steam-helper` | providers | the game's Steam API library, loaded at run time |

## 2. Module responsibilities

| Module | Owns | Key headers |
| --- | --- | --- |
| `core` | Value types (`GameInstallation`, `ModIdentity`, `InstalledMod`, `Playset`, `PlaysetEntry`, `AcquisitionJob`, `LaunchPlan`, ...); `Result`/`Error`/`PMM_TRY`; the game catalog; pure playset editing and view rules (filters never change load order); launch readiness; name-based dependency checks; import planning; logging and redaction; SHA-256 | `model.hpp`, `result.hpp`, `catalog.hpp`, `playset_ops.hpp`, `readiness.hpp`, `dependencies.hpp`, `import_plan.hpp`, `log.hpp` |
| `platform` | Standard per-user directories; durable atomic writes; process start without a shell and process listing; Steam root, library, app manifest and Workshop manifest discovery (read-only); journaled multi-file transactions | `paths.hpp`, `fs.hpp`, `process.hpp`, `steam.hpp`, `file_transaction.hpp` |
| `sharing` | Share-code protocol v1: strict manifest parsing and canonical serialization, bounded zlib/Base64URL codec, envelope and digest | `share_code.hpp`, `manifest.hpp`, `codec.hpp`, `utf8.hpp` |
| `persistence` | One SQLite database: migrations, upgrade backups, integrity checks, repositories for settings, installations, inventory, playsets (with undo history), acquisition jobs and launch history | `database.hpp`, `*_repository.hpp` ([details](persistence.md)) |
| `providers` | Steam Workshop: helper wire protocol, pure acquisition reducer, EResult classification, Web API metadata request/response, manual fallback links, local mod fingerprints | `helper_protocol.hpp`, `acquisition.hpp`, `workshop_item.hpp`, `web_metadata.hpp`, `fallback.hpp`, `local_fingerprint.hpp` ([details](steam-workshop.md)) |
| `games` | One adapter per game: discovery per OS/store, descriptor parsing, version and capability detection, load-order translation, generated configuration, executable/arguments/environment, process names, compatibility warnings. Adapters return plans; they never write files | `game_adapter.hpp` |
| `services` (in progress) | Qt-free use cases shared by `pmm-app` and `pmm-cli`: scanning, export/import, the acquisition driver (helper processes, ticks, persistence), launch and configuration recovery | |
| `app` + `ui` (in progress) | Qt composition root, QML-facing models and controllers, worker threads, clipboard and file dialogs, HTTP for Web API metadata, settings and window state | [`ui-contract.md`](ui-contract.md) |
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
4. An acquisition job is created (or reused) for every missing Workshop entry,
   enabled or not. Local entries are never downloaded; they need mapping or
   manual installation.
5. Readiness (`computePlaysetReadiness`) is recomputed as jobs and scans
   progress; failed or unavailable entries stay in the playset, visible.

### Acquire (Steam Workshop)

1. The driver starts `pmm-steam-helper --app-id <id> --steam-api <library>`
   for the game, with the library found next to the game
   (`platform::steam::findSteamApiLibrary`).
2. `startAcquisition(job)` yields actions; `helperCommandFor()` turns them into
   protocol lines; helper events become observations through
   `observationFromHelperEvent()`; `reduceAcquisition()` returns the next state,
   further actions and whether to persist the job.
3. Before deciding anything, every run queries the item state, so installed
   items are verified instead of downloaded again and restored jobs are
   reconciled instead of re-subscribed.
4. `verifyWorkshopContent(installPath)` runs on a worker; only then does a job
   become `Installed`. A rescan updates the inventory and readiness.
5. Web API metadata (`GetPublishedFileDetails`) rejects deleted, private or
   wrong-game items (`Unavailable`). The manual fallback opens the item in
   Steam and relies on a rescan.
6. After a restart, `AcquisitionJobRepository::listUnfinished()` feeds
   `startAcquisition()` again. The helper is stopped when idle and before the
   game is launched.

### Launch (Play)

1. `evaluateLaunch()` requires every enabled entry to be ready; blockers are
   shown next to Play. Disabled entries never block.
2. Running processes (`platform::listProcesses()`) are compared with the
   adapter's `processNames()`: a running game or a conflicting configuration
   writer (for example the Paradox launcher) blocks the launch.
3. `GameAdapter::buildLaunchPlan()` returns the executable, working directory,
   argument vector, environment additions and the minimal `ConfigWrite`s,
   generated from the existing files so unrelated settings survive.
4. `FileTransactionManager::apply()` writes them as one journaled unit with
   backups (ADR 0005).
5. `platform::startDetached()` starts the real game executable with
   `CreateProcessW` or `posix_spawn`, never through a shell or a store URL that
   would reopen the Paradox launcher. Failures carry the OS error in
   `Error::detail`.
6. The outcome and transaction id are recorded in `launch_history`.

Recovery: on start-up `listIncomplete()` finds transactions interrupted by a
crash and the user is offered `recover()`. "Restore pre-launch configuration"
calls `restore(txid)` for the last committed launch transaction.

## 4. Threading model

- **GUI thread**: QML, models and controllers. Every property change and model
  update is emitted on the GUI thread (UI contract).
- **Worker threads** (application layer): installation discovery, mod scans,
  fingerprints, content verification, Web API requests and parsing, helper
  I/O. Results return to the GUI thread through queued signals. Long scans
  support cancellation (`fingerprintDirectory` takes a cancellation flag).
- **Lower layers are synchronous and reentrant.** They hold no global mutable
  state except the logger (thread-safe sink replacement) and test hooks.
- **Database**: one `persistence::Database` (one SQLite connection) is shared
  by all threads; a recursive mutex serialises every call, and repository calls
  compose inside `Database::transaction()`. Transaction bodies are kept short
  because they block other threads. A second application instance waits up to
  5 s (busy timeout).
- **Helper processes**: one per game while it has active jobs; a reader thread
  inside the helper keeps the Steam callback pump from blocking on input.
- **File transactions**: `FileTransactionManager` is not thread-safe; the
  application uses one per journal root and serialises launches.
- **Acquisition reducer**: pure and clock-free; the driver feeds `Tick`
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
| Configuration transaction journals | a folder inside the application data directory (chosen by the app layer) | same | same |
| Logs | `%LOCALAPPDATA%\ParadoxModManager\logs\` | `$XDG_STATE_HOME/ParadoxModManager/logs/` (default `~/.local/state/...`) | `~/Library/Logs/ParadoxModManager/` |
| Cache | `%LOCALAPPDATA%\ParadoxModManager\cache\` | `$XDG_CACHE_HOME/ParadoxModManager/` (default `~/.cache/...`) | `~/Library/Caches/ParadoxModManager/` |
| Game user data (read on scan; written only on Play, through a transaction) | `Documents\Paradox Interactive\<Game>` (Known Folder, so OneDrive redirection is honoured) | `$XDG_DATA_HOME/Paradox Interactive/<Game>` | `~/Documents/Paradox Interactive/<Game>` |
| Steam (read-only) | registry `SteamPath` / `InstallPath`, default `C:\Program Files (x86)\Steam` | `~/.steam/steam`, `$XDG_DATA_HOME/Steam`, Flatpak and Snap paths | `~/Library/Application Support/Steam` |

The application never writes under a Steam folder, never reads or writes the
Paradox launcher's `launcher-v2.sqlite`, and never patches game binaries.

## 7. Trust boundaries

Untrusted inputs: share codes, mod descriptors, Steam manifests, helper
output and Web API responses. Each is parsed with explicit limits and
validated before use. Nothing from a share code selects an executable,
argument, path or URL; Workshop URLs are built only from validated canonical
ids. Processes are started with an argument vector, never a shell string.
