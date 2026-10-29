# Persistence

Paradox Mod Manager keeps its own state in one SQLite database. This document
describes where it lives, what it contains, how it is upgraded and backed up,
and how to recover it. The implementation is in `src/persistence/`
(namespace `pmm::persistence`); it uses the SQLite C API directly and has no Qt
dependency.

## What is stored, and what is not

The database holds **application-owned state only**:

| Data | Tables | Can be rebuilt? |
| --- | --- | --- |
| Settings (theme, window geometry, last active playset, ...) | `settings` | No (user preferences) |
| Detected or manually configured game installations | `game_installations` | Mostly: rescan; manual paths must be re-entered |
| Installed-mod inventory cache | `installed_mods` | Yes: rescan the game |
| Playsets, their ordered entries and undo history | `playsets`, `playset_entries`, `playset_history` | No (user data; share codes are a portable export) |
| Acquisition (subscribe/download) jobs | `acquisition_jobs` | Partly: reconcile with the provider |
| Launch history | `launch_history` | No (diagnostics only) |

Game-generated files (descriptors, `dlc_load.json`, launcher databases, saves)
are never copied into this database; game adapters read and write them in the
game's own user-data folder. Logs and caches live in their own OS directories
(`platform::StandardDirectories::logs` / `::cache`), not in the database.

## Location policy

The persistence layer never chooses a location itself: the application passes
the database path to `Database::open()`. The application uses the standard
per-user data directory, i.e.
`platform::standardDirectories().appData / Database::kDefaultFileName`:

| OS | Database file |
| --- | --- |
| Windows | `%APPDATA%\ParadoxModManager\state.sqlite` |
| Linux | `$XDG_DATA_HOME/ParadoxModManager/state.sqlite` (default `~/.local/share/...`) |
| macOS | `~/Library/Application Support/ParadoxModManager/state.sqlite` |

`Database::open()` creates missing parent directories. Paths are handed to
SQLite as UTF-8 (SQLite's Windows VFS converts them to UTF-16), so folders with
spaces and non-ASCII characters work on every OS. A Windows path that is not
valid UTF-16 (see Conventions) is refused with `ErrorCode::InvalidArgument`
rather than opened under an altered name. While the application runs,
SQLite keeps `state.sqlite-wal` and `state.sqlite-shm` next to the file; they
are folded back into the main file when the last connection closes.
Upgrade backups are written next to the database (see below).

Tests use `Database::openInMemory()` or a unique temporary directory.

## Connection and threading model

* One `Database` object owns one SQLite connection, configured on open with
  `PRAGMA foreign_keys = ON`, `PRAGMA journal_mode = WAL` (file databases),
  the build default `synchronous = NORMAL` for WAL, and a 5 s busy timeout so
  another connection to the same file waits instead of failing immediately.
  (`pmm-app` itself keeps a second instance off a profile with a lock file,
  see [`app.md`](app.md).)
* Any thread may use that object; in `pmm-app` only the services worker
  thread does. Access is serialised by
  an internal **recursive mutex**: every public call that touches the
  connection, on `Database` and on each repository (`SettingsRepository`, `InstallationRepository`,
  `InventoryRepository`, `PlaysetRepository`, `AcquisitionJobRepository`,
  `LaunchHistoryRepository`, all constructed from a `Database&`) holds it for
  its whole duration. The mutex is recursive so repository calls can be
  composed inside `Database::transaction()`.
* Keep transaction bodies short: while one runs, every other thread that
  touches the database blocks.
* `Database::access()` returns a guard that exposes the raw `Connection`; it is
  meant for repositories and tests, not for application code.

## Transactions

* `Transaction` is an RAII guard: `BEGIN IMMEDIATE` (or `BEGIN` for read
  snapshots), rolled back on scope exit unless `commit()` succeeded. When the
  connection is already in a transaction, the guard uses a uniquely named
  `SAVEPOINT`, so guards nest.
* `Database::transaction(body)` runs `body` in such a guard and commits only if
  it returns success. Repository operations that touch several rows
  (inventory replacement, saving playset entries, delete/restore, job
  create-or-reuse, history purge) each run in one transaction; called inside an
  outer `transaction()`, they become savepoints and commit or roll back with
  the outer body. A failure part-way through leaves the previous state intact;
  the tests inject failures with temporary triggers and failing bodies.

## Conventions

* Timestamps are `INTEGER` unix **milliseconds**. `InstalledMod::contentUpdatedAt`
  (seconds in the domain model) is stored in milliseconds and converted back.
* Provider ids are always `TEXT` (a Workshop id such as
  `18446744073709551615` or `000123` round-trips exactly); empty ids are
  rejected by `CHECK` constraints. Playset entries additionally require the
  provider and provider id to be **valid UTF-8**, because history snapshots
  are JSON (see Undo history). On Linux a local mod's id is its descriptor
  file stem as raw bytes, so a descriptor whose name is Latin-1 encoded
  (`café.mod` as bytes `63 61 66 E9 ...`) cannot be added to a playset:
  `ErrorCode::InvalidArgument`, "rename the file, then rescan". The
  inventory stores ids as they are.
* Paths are `TEXT` in native format: the native bytes on POSIX (any bytes,
  stored exactly), UTF-8 on Windows. Path conversion never throws. NTFS
  allows names that are not valid UTF-16 (an unpaired surrogate, usually
  created by a tool); they have no UTF-8 form, so the inventory, installation
  and database-open paths reject them with `ErrorCode::InvalidArgument`
  instead of storing an altered path. Every stored path therefore
  round-trips exactly; ill-formed UTF-8 found in a stored Windows path
  (damaged or foreign data) is read with U+FFFD instead of failing.
* Enumerations are stored as their stable `toString()` names (`steam`,
  `windows`, `ready`, `download_requested`, ...). Unknown stored values are
  reported as damaged data rather than guessed. Adding an enumeration value
  that is persisted requires a schema version bump, so older builds refuse the
  database instead of misreading it.
* Booleans are `INTEGER` 0/1. Lists are JSON arrays (`*_json` columns).
* Lookups by key (`settings`, installation by game, inventory `find`) return
  `std::optional`; lookups by local id (playset, job, history entry) return
  `ErrorCode::NotFound` for unknown or deleted rows.

## Schema version 1

```text
schema_migrations(version INTEGER PRIMARY KEY, name TEXT, applied_at INTEGER)

settings(key TEXT PRIMARY KEY, value TEXT NOT NULL)

game_installations(game_key TEXT PRIMARY KEY, install_path, user_data_path, executable_path,
    default_args_json, distribution, platform, steam_app_id, steam_library_path,
    version, version_label, manually_configured 0/1, updated_at)

installed_mods(game_key, source, provider_id, name, version, supported_version, picture,
    declared_path, archive, remote_file_id, tags_json, dependencies_json, extra_json,
    content_path, descriptor_path, descriptor_ref, state, problems_json,
    content_updated_at, fingerprint, scanned_at,
    PRIMARY KEY (game_key, source, provider_id))

playsets(id INTEGER PRIMARY KEY AUTOINCREMENT, game_key, name, created_at, updated_at,
    deleted_at NULL)

playset_entries(playset_id REFERENCES playsets ON DELETE CASCADE, position >= 0, source,
    provider_id, enabled 0/1, name_hint, version_hint, fingerprint,
    PRIMARY KEY (playset_id, position), UNIQUE (playset_id, source, provider_id))

playset_history(id INTEGER PRIMARY KEY AUTOINCREMENT, playset_id, game_key, reason,
    snapshot_json, created_at)

acquisition_jobs(id INTEGER PRIMARY KEY AUTOINCREMENT, game_key, source, provider_id,
    playset_id NULL REFERENCES playsets ON DELETE SET NULL, state, error_code,
    error_message, attempts, bytes_downloaded, bytes_total, created_at, updated_at)
  UNIQUE INDEX acquisition_jobs_one_unfinished (game_key, source, provider_id)
    WHERE state IN ('pending', 'subscribing', 'subscribed', 'download_requested',
                    'downloading')

launch_history(id INTEGER PRIMARY KEY AUTOINCREMENT, game_key,
    playset_id NULL REFERENCES playsets ON DELETE SET NULL, started_at, pid NULL,
    outcome, detail, transaction_id)
```

The authoritative DDL is the embedded migration in
`src/persistence/migrations.cpp`. Notes:

* **Inventory vs. playsets.** `installed_mods` is a cache that
  `InventoryRepository::replaceForGame()` replaces atomically per game after
  each scan. It is independent of playset membership; replacing it never
  touches playsets. The descriptor's `path=` value is kept as `declared_path`
  (it may be stale or from another machine); unknown descriptor keys are kept
  in `extra_json` as `[key, value]` pairs in file order.
* **Playset order.** Entries are stored with positions `0..N-1` in exactly the
  given order (index 0 is the first mod handed to the game). Identities are
  unique per playset, must belong to the playset's game and must be valid
  UTF-8; `create`, `saveEntries`, `replaceWithSnapshot` and `duplicate`
  enforce this.
* **Undo history.** `remove()` writes a snapshot (`reason = "deleted"`) and
  soft-deletes the playset (`deleted_at`). `replaceWithSnapshot()` snapshots
  the current state with a caller-supplied reason (`import_replace`,
  `bulk_edit`, ...) before replacing the entries. `restore(historyId)` brings
  back the exact name and entries: it undeletes the playset, or recreates it
  under a new id if the row was purged; restoring over a live playset first
  snapshots it (`before_restore`), so restores are undoable too. Ordinary
  edits (`saveEntries`, `rename`) do not create history.
  `snapshot_json` format (version 1):
  `{"format":1,"name":..,"createdAt":..,"updatedAt":..,"entries":[{"source":..,"id":..,"enabled":..,"nameHint":..,"versionHint":..,"fingerprint":..}]}`.
  Invalid UTF-8 in names (playset name, name/version hints, fingerprint) is
  replaced with U+FFFD in snapshots. Identities (`source`, `id`) are never
  altered: a playset whose stored entries include an id that is not valid
  UTF-8 (rows written before ids were validated) cannot be snapshotted, so
  `remove()`, `replaceWithSnapshot()` and a restore over it fail with
  `ErrorCode::InvalidArgument` and leave it unchanged until that entry is
  removed. `restore()` validates the snapshot's entries first, so a snapshot
  from an older build whose replaced ids collide is reported instead of
  failing on the unique constraint.
  `playset_history` has no foreign key so history survives purged playsets.
* **Acquisition jobs.** At most one unfinished job per mod identity, enforced
  by the partial unique index; `createOrReuse()` returns the existing job.
  The index's state list must equal the non-terminal `AcquisitionState`
  values (`isTerminal()`); a unit test checks this. Changing which states are
  terminal requires a migration that recreates the index.
  `listUnfinished()` is the input for restart reconciliation.
* **Launch history.** `outcome` is `"started"` or the `errorCodeName()` of the
  failure; `transaction_id` links the launch to the configuration-write
  transaction of the game adapter.

### Retention

The persistence layer never deletes user data on its own. The application
decides retention and calls, typically at startup:

* `PlaysetRepository::purgeHistoryOlderThan(cutoff, keepAtLeast)` removes
  history older than `cutoff` while keeping the `keepAtLeast` newest entries
  of each playset; soft-deleted playsets left without history are then
  removed for good (their entries cascade). Recommended: 30 days, keep 10.
* `AcquisitionJobRepository::removeFinishedOlderThan(cutoff)` removes terminal
  jobs. Recommended: 7 days.

## Migrations

* Migrations are an ordered list of embedded SQL scripts
  (`builtinMigrations()`), versions `1, 2, 3, ...` without gaps.
* On open, the current version is `MAX(version)` in `schema_migrations`
  (also mirrored in `PRAGMA user_version` for external tools). Each pending
  migration runs in **its own** `BEGIN IMMEDIATE` transaction together with
  its `schema_migrations` row; a failure rolls that migration back, leaves the
  database at the previous version and returns `ErrorCode::MigrationFailed`.
* Inside the transaction the version is re-read, so two application
  instances starting at the same time do not apply a migration twice.
* A database whose version is **newer** than the build knows is refused with
  `ErrorCode::UnsupportedVersion` ("update the application") before anything
  is written: no WAL switch, no backup, no migration. The file is untouched.
* A file that is not a SQLite database, or a SQLite database without
  `schema_migrations` that contains other tables, is refused with
  `ErrorCode::DatabaseError`.

Rules for adding a migration:

1. Never edit a released migration. Append `{N + 1, "description", sql}` to
   `builtinMigrations()` and document the change here.
2. Keep each migration self-contained SQL. SQLite cannot drop or alter most
   constraints in place; use SQLite's documented table rebuild (create the new
   table, copy rows, drop the old table, rename, recreate indexes) and set
   `Migration::foreignKeysOff = true`. With enforcement on, dropping a parent
   table such as `playsets` would cascade-delete its child rows. Because the
   pragma cannot change inside a transaction, the migration runner turns
   enforcement off around that migration's transaction, runs
   `PRAGMA foreign_key_check` before committing (any violation aborts the
   migration) and turns enforcement back on afterwards.
3. Add a `tests/fixtures/persistence/v<N>_sample.sql` fixture and keep the
   older fixtures: `fixture_tests.cpp` loads each old fixture into an old
   schema and opens it with the current build.

## Backups

Before migrating an **existing, non-empty** database to a newer version,
`Database::open()` copies it to `<file>.bak-v<old version>` (for example
`state.sqlite.bak-v1`), using SQLite's online backup API so the copy includes
any content still in the WAL. The copy is written to `<backup>.tmp` and then
renamed, so a failed copy never replaces an older backup; an existing backup
of the same version is replaced. If the backup cannot be written the upgrade
is not attempted and the database is left unchanged.
`Database::upgradeBackupPath()` reports the backup made by the current open,
so the application can mention it. Backups are never deleted automatically.
New databases and opens that need no migration create no backup.

## Integrity checks and recovery

`Database::open()` runs `PRAGMA quick_check` before using the file. If the
check fails, or SQLite reports the file as corrupt or not a database, open
returns `ErrorCode::DatabaseError` with a recovery hint and the SQLite
diagnostics in `Error::detail`. Damaged values inside otherwise healthy rows
(for example unreadable JSON) are reported the same way when read.

Recovery procedure for users (also shown in the error message):

1. Close Paradox Mod Manager.
2. In the application data folder, rename the damaged `state.sqlite` (for
   example to `state.sqlite.damaged`) and delete any leftover
   `state.sqlite-wal` / `state.sqlite-shm` files that belong to it.
3. Either rename the most recent `state.sqlite.bak-v<N>` to `state.sqlite`
   (it will be upgraded again on the next start), or start without a database:
   a new empty one is created.
4. Rescan games to rebuild installations and the inventory. Playsets that
   were exported as share codes can be imported again.

## Test hooks

`persistence/testing.hpp` (tests only) provides `openWithMigrations()`,
`openWithExtraMigrations()` and `openInMemoryWithMigrations()` to create
old-version databases or simulate a future migration, and `setClock()` to make
stored timestamps deterministic.
