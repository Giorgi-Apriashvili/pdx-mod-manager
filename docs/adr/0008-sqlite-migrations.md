# ADR 0008: SQLite schema and migration policy

- Status: Accepted
- Date: 2026-10-29

## Context

Application-owned state (settings, installations, inventory cache, playsets
with undo history, acquisition jobs, launch history) must persist in SQLite
with schema migrations and transactional updates, separate from game files
(spec section 4). Users upgrade, sometimes downgrade, and occasionally run two
instances; a failed upgrade must never lose playsets.

## Decision

- One database file per user, `state.sqlite` in the standard application data
  directory; the location is chosen by the application, not the persistence
  layer. Details: [`docs/persistence.md`](../persistence.md).
- Migrations are an ordered list of embedded SQL scripts, versions `1, 2, 3, …`
  without gaps. **A released migration is never edited**; changes are appended
  as a new version. The version is recorded in `schema_migrations` and
  mirrored in `PRAGMA user_version`.
- Each pending migration runs in its own `BEGIN IMMEDIATE` transaction together
  with its `schema_migrations` row, re-reading the version inside the
  transaction so concurrent starts do not apply it twice. A failure rolls that
  migration back and reports `MigrationFailed`.
- Before migrating an existing, non-empty database, a consistent copy is made
  with SQLite's online backup API to `state.sqlite.bak-v<old>` (written to a
  temporary file, then renamed). If the backup fails, the upgrade is not
  attempted. Backups are never deleted automatically.
- A database with a newer schema version than the build knows is refused with
  `UnsupportedVersion` before anything is written.
- Integrity: `PRAGMA quick_check` on open; corruption is reported with a
  recovery procedure rather than repaired silently.
- Table rebuilds follow SQLite's documented procedure with foreign-key
  enforcement turned off around that migration and `PRAGMA foreign_key_check`
  before commit.
- Persisted enumerations use stable string names; adding a persisted value
  requires a schema version bump so older builds refuse the file instead of
  misreading it.
- Every schema version keeps a fixture (`tests/fixtures/persistence/v<N>_sample.sql`)
  that is loaded into the old schema and opened with the current build.

## Consequences

- Upgrades are atomic per step and recoverable from the pre-upgrade backup;
  downgrades are refused instead of corrupting data.
- Schema changes cost a migration, a fixture and documentation, which is the
  intended friction.
- WAL mode leaves `-wal`/`-shm` files next to the database while the
  application runs; recovery instructions mention them.

## Alternatives considered

- **Qt SQL / an ORM**: would pull Qt into the persistence layer (ADR 0001) and
  hide the transaction boundaries that recovery relies on.
- **JSON files**: no transactions across records, no constraints, and partial
  writes are harder to recover.
- **Automatic down-migrations**: rarely tested and risky; refusing newer
  databases is safer.
- **Editing migrations in place before 1.0**: tempting during development but
  breaks databases created by earlier builds; the append-only rule applies
  from schema version 1.
