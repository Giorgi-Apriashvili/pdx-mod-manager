# ADR 0005: Game configuration written through a journaled file transaction

- Status: Accepted
- Date: 2026-10-29

## Context

Launching with a playset requires writing game-owned files before the game
starts: for Stellaris, `dlc_load.json` (enabled mods in order) and any missing
`mod/ugc_<id>.mod` descriptors ([feasibility](../feasibility.md) §2). These
files belong to the game and the Paradox launcher. The spec requires backups of
files that change, preserving unrelated settings, atomic application where
possible, restoring the prior state when a write fails part-way, and detecting
interrupted transactions on restart (section 7). Several files may change for
one launch, and a crash or power loss can happen between any two writes.

## Decision

- Game adapters never write files. `GameAdapter::buildLaunchPlan()` returns a
  `LaunchPlan` whose `ConfigWrite`s contain the complete new contents,
  generated from the existing files so unrelated fields (for example disabled
  DLC choices) are preserved.
- `platform::FileTransactionManager` applies them as one unit, journaled on
  disk without a database (`<journalRoot>/<txid>/journal.json`):
  1. journal `prepared` (targets, whether each existed, hash of new contents);
  2. existing targets copied into the transaction folder durably;
  3. journal `applying` (with backup hashes), then each target replaced with
     `writeFileAtomic()` (temporary file, flush, atomic rename);
  4. journal `committed`.
- A failure in step 3 restores every touched target (or deletes files that did
  not exist before) and marks the journal `rolled_back`. A crash leaves
  `prepared` or `applying` behind; on start-up `listIncomplete()` finds it and
  `recover()` undoes it. Backups are verified against their recorded SHA-256
  before any restore.
- `restore(txid)` puts back the configuration from before a launch (the
  user-facing "restore pre-launch configuration"); `prune(keepLast)` deletes
  old finished transactions, never interrupted ones.
- Launch history rows reference the transaction id.

## Consequences

- The game's files are either entirely old or entirely new after any failure,
  and the previous configuration can be restored later, including after the
  official launcher has been used in between (restore puts back the exact
  bytes that were saved).
- Generated configuration stays in effect until the user or the Paradox
  launcher changes it; the launcher may rewrite `dlc_load.json` from its own
  database the next time it is used. This is documented to users.
- The application chooses the journal root (a folder in its per-user data
  directory, outside the game folders). Journals contain copies of game
  configuration files, nothing else.
- The manager is single-instance per journal root and not thread-safe; the
  application serialises launches.
- Fault injection hooks (`FileTransactionFaults`) let tests simulate a failing
  write and a crash after N writes.

## Alternatives considered

- **Write files directly with ad hoc `.bak` copies**: no record of which files
  were half-written after a crash, and no reliable automatic recovery.
- **Store backups in the SQLite database**: couples game-file recovery to the
  application database; a damaged database would also lose the backups, and
  the spec keeps game-generated files separate from application state.
- **Editing the Paradox launcher's `launcher-v2.sqlite`**: private launcher
  state with an undocumented schema; this application never reads or writes it.
- **Launching through the Paradox launcher or `steam://run`**: reopens the
  launcher, which the product must bypass.

## Amendment (2026-10-30): single instance enforced, interrupted changes per game

- "Single-instance per journal root" is now enforced: `pmm-app` holds a lock
  file (`ProfileLock`, `<data folder>/pmm-app.lock`) from before the services
  start until they have shut down, and the journal root lies in the same data
  folder. A second start on the same profile reports that the profile is in
  use; a lock left by a crashed process is taken over ([`app.md`](../app.md),
  start-up sequence).
- An interrupted transaction blocks launching only the game it belongs to
  (its launch label, or a target in that game's user-data folder). Those of
  other games are shown as warnings, so a transaction that `recover()` cannot
  undo does not keep every game from launching.

## Amendment (2026-11-07): recovery without unneeded backups, abandoned changes

- `recover()` and `restore()` read and verify only the backups they still
  need: a target that already holds its original contents (a crash before its
  write, or a file put back since) needs none, and neither does one that did
  not exist before and is still absent. Every needed backup is still verified
  before any target changes. `IntegrityCheckFailed` means a needed backup is
  missing or damaged (wrong hash, or above the size limit). A backup that
  exists but cannot be read right now (in use by another program, access
  denied) is reported with its read error, and so is a bad backup whose target
  cannot be read at all: a retry may succeed.
- A transaction that still cannot be recovered can be given up with
  `abandon(txid)`: the journal says `abandoned` (with the reason in
  `lastError`), no target is changed, and the folder keeps its backups until
  `prune()` removes it like any finished transaction; `prune()` counts it from
  when it was abandoned (`updatedUnixMs`), not from its creation, so the
  remaining backups of an old crashed launch are not deleted at the next
  start. `listIncomplete()` no longer returns it, so it stops blocking its
  game. The application offers this as **Discard this record** after Recover
  failed with `IntegrityCheckFailed`, behind a confirmation that names the
  files.
- The journal format stays 1. A version without the new state cannot read an
  abandoned journal and skips that folder, so there it neither blocks nor is
  pruned.
