# Changelog

All notable changes to this project are documented in this file. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
application uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The share-code protocol is versioned independently of the application
(envelope `PMM1`, manifest schema 1; see
[`docs/share-code-protocol.md`](docs/share-code-protocol.md)). Entries that
change what codes look like or which codes are accepted say so explicitly.

## [Unreleased]

### Added

- Build system: target-based CMake with configure, build and test presets
  (`core-debug`, `core-sanitize`, `debug`, `release`, `ci`); dependencies
  pinned by URL and SHA-256 (nlohmann/json 3.12.0, zlib 1.3.1, SQLite 3.50.4,
  Catch2 3.8.1); strict warnings, optional sanitizers.
- Core domain model, game catalog (EU4 and Stellaris supported; HOI4, CK3 and
  Victoria 3 known but unsupported), structured `Result`/`Error` handling,
  structured logging with redaction for exported diagnostics.
- Playset editing operations, view filtering rules that never change load
  order, launch readiness, name-based dependency checks and import planning.
- Share-code protocol version 1 (`PMM1.<payload>.<digest>`): canonical,
  deterministic encoding; strict, bounded decoding; frozen regression fixtures
  in `tests/fixtures/sharing/v1`.
- SQLite persistence: schema version 1, ordered migrations with pre-upgrade
  backups, integrity checks, playset undo history, acquisition jobs, launch
  history.
- Platform layer: standard per-user directories, durable atomic file writes,
  detached process start without a shell, process listing, Steam root,
  library, app manifest and Workshop manifest discovery, and journaled
  multi-file transactions with rollback, crash recovery and restore.
- Steam Workshop provider: helper wire protocol, pure acquisition state
  machine (retry, cancel, restart reconciliation, stall detection), EResult
  classification, Web API metadata parsing, manual fallback links and local
  mod fingerprints.
- `pmm-steam-helper`, which loads the game's own Steam API library at run time
  (no Steamworks SDK files); read-only probe verified with Stellaris 4.5.2 on
  Windows 11.
- Observable probe mods (`tools/probe-mods`) for in-game load-order checks.
- Feasibility record: how the Paradox launcher hands mods to Stellaris,
  Workshop acquisition constraints and alternatives, and read-only
  observations of an EU4 1.37.5.0 installation.
- Repository infrastructure: README, contributing guide, architecture
  description, decision records, support matrix, manual validation procedure,
  troubleshooting guide, third-party notices, CI for Windows, Linux and macOS
  with sanitizers and a format check, CPack packaging, and an independent
  share-code tool (`scripts/share-code.py`).

## [0.1.0] - planned

First release. It will be tagged only when the following are done; until then
the entries above stay under Unreleased.

- Qt Quick desktop application (`pmm-app`) and command-line tool (`pmm-cli`).
- Europa Universalis IV and Stellaris adapters: discovery, mod inventory,
  load-order translation, direct launch without the Paradox launcher.
- Import workflow with Workshop acquisition and visible recovery states.
- Packages for Windows, Linux and macOS.
- The manual acceptance procedure in [`docs/validation.md`](docs/validation.md)
  executed and recorded, with the support matrix updated from the evidence.
  Combinations that could not be tested stay marked as unverified.
