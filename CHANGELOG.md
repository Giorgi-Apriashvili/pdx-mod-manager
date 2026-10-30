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

- Windows installer (Inno Setup): per-user, no administrator rights, default folder
  `%LOCALAPPDATA%\Programs\ParadoxModManager`. It replaces the earlier NSIS
  configuration, which was never shipped. A failed or cancelled upgrade leaves the
  previous version working, and a first install leaves an existing folder's own
  files alone. Verified with real silent runs in a scratch folder
  ([evidence](docs/evidence/installer-2026-10-30.md)); the wizard was not clicked
  through.
- Settings > Updates: "Check for updates" asks GitHub for the latest release, only
  when pressed. An installed copy then downloads the installer, verifies
  it against the release's `SHA256SUMS.txt`, starts it and quits; the installer
  starts the new version. ZIP copies and development builds get the release page. See
  [`docs/updates.md`](docs/updates.md). No update between two published releases
  has happened yet.
- `Release` workflow: full CI, the Windows ZIP and installer, smoke test of the
  installer, then a GitHub release that stays a draft until complete; a dry run
  publishes nothing ([`docs/releasing.md`](docs/releasing.md)). It has never run.
- Live acceptance run on Windows 11 / Steam with the user's approval
  ([evidence](docs/evidence/live-validation-2026-10-30.md)): direct launches
  of Stellaris 4.5.2 and EU4 1.37.5 with probe mods, a real EU4 Workshop
  subscription and download, restart persistence, and the failure cases.
- EU4 playsets show a closable note above the mod list: EU4 does not use
  the playset order when two mods change the same file (the mod whose name
  comes first alphabetically wins). `GamesModel` gains the `loadOrderNote`
  role and `currentLoadOrderNote`; adapters expose it as
  `LoadOrderInfo::userNote`.
- `pmm-app --live-test <steps>` (hidden, requires `--profile`): the
  self-test steps plus `play`, `play-fails`, `wait-ready`, `restore`,
  `select`, `locate`, `blockers`, `jobs`, with timestamped job, launch and
  notification transitions, for recorded acceptance runs.
- Build system: target-based CMake with configure, build and test presets
  (`core-debug`, `core-sanitize`, `core-clang-cl`, `debug`, `release`, `ci`); dependencies
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
- Europa Universalis IV and Stellaris adapters on a shared `dlc_load.json`
  implementation: installation and user-data discovery (including
  `userdir.txt`), descriptors, `supported_version` warnings, the current
  configuration, generated `dlc_load.json` and Workshop descriptors, and launch
  plans. Verified read-only against Stellaris 4.5.2 and EU4 1.37.5 on
  Windows 11 (`docs/evidence/real-scan-2026-10-29.md`); hidden real-data tests
  `pmm_games_tests "[.real]"`.
- Qt-free application services (`src/services`): game discovery and scans,
  playset editing with undo, export and import with exact reconciliation, the
  acquisition coordinator (one helper session per game, Workshop identity gate
  through the Web API, at most one job per mod, retries parked across launches
  and restarts), the launch check, direct launch through one journaled
  configuration transaction, recovery and restore, redacted diagnostics.
- Desktop application `pmm-app` (`src/app`, QML module `Pmm.Ui` in `ui/`): the
  three-column window of the UI contract, one services worker thread, Steam
  helper host (`QProcess`), Web API host, settings and window state, start-up
  notices and a fatal error window. Options `--profile`, `--game`,
  `--no-steam`, `--read-only`, `--verbose`; hidden verification switches
  `--self-test` and `--screenshot` for headless checks. One instance per
  profile (lock file in the data folder).
- Accessibility: complete keyboard operation (Tab order across the columns,
  F6, shortcuts, an F1 shortcut list, keyboard context menu, Alt/Ctrl+Up/Down
  reordering), 2 px focus rings, accessible names, descriptions and
  announcements, WCAG 2.x AA contrast in both themes (`ui/dev/check_contrast.py`),
  checks at scale factors 1.5 and 2; see `docs/accessibility.md`.
- Application tests (`tests/app`): models, the services worker, Backend flows
  in a temporary profile, a 2,000-entry performance budget, the real QML module
  against the real Backend, keyboard paths with real key events, a source
  check that data is shown as plain text, the Steam helper host with the fake
  Steam API library and the Web API host against a local stub.
- Windows packaging: only the Qt files the application loads, the C++
  runtime app-local, version resources and release link options for both
  executables (`packaging/windows/`); the ZIP was verified from an extracted
  copy (`docs/evidence/packaging-2026-10-29.md`).
- Observable probe mods (`tools/probe-mods`) for in-game load-order checks.
- Feasibility record: how the Paradox launcher hands mods to Stellaris,
  Workshop acquisition constraints and alternatives, and read-only
  observations of an EU4 1.37.5.0 installation.
- Repository infrastructure: README, contributing guide, architecture
  description, decision records, support matrix, manual validation procedure,
  troubleshooting guide, third-party notices, CI on Windows with MSVC
  (with the application tests), ASan and clang-cl jobs, a clang-format 22 check
  and a contrast check, CPack packaging, and an independent share-code tool
  (`scripts/share-code.py`).

### Changed

- Packaging moved from `ci.yml` to the `Release` workflow, which runs it on tags,
  on request, and for pull requests that touch the packaging.
- EU4's `LoadOrderInfo` states the observed rule: the order of
  `enabled_mods` does not decide file overrides, the mod whose name sorts
  first wins. Stellaris: the later mod in the list wins. Both are now
  `verifiedInGame`.
- `tools/probe-mods` also writes event files: neither game reports unknown
  triggers in scripted triggers that nothing uses.
- Direct launches never pass `-enabletelemetry`, even when the game's launcher
  settings ask for it.
- Workshop items that are registered with a game but have no content are
  treated as not installed: playset entries show them as Missing and can
  acquire them (before: Broken, not acquired).
- Mods whose content or descriptor path has no UTF-8 form stay in the
  inventory as Broken, with the name to rename, instead of failing the scan.
- UTF-8 handling is one implementation in `core/utf8`. Invalid text is
  repaired with one U+FFFD per ill-formed sequence (before: one per byte), so
  descriptor text and configuration-change labels with broken UTF-8 can read
  slightly differently. Share codes are not affected.
- Proposed playset names (imports, copies, de-duplication) stay within the
  256-byte limit, cut on a character boundary before the suffix.
- An interrupted configuration change blocks launching only its own game;
  those of other games are warnings.
- A launch closes the game's Steam helper session only once the launch plan
  is valid, and resumes the parked downloads when no game was started.
- Imports with Workshop downloads off (`--no-steam`, `--read-only`) queue no
  downloads; missing entries wait for **Download missing**.
- Read-only sessions run the full launch check except the folder write
  probes, so they report the same blockers as a normal session.
- Share codes: a code from a newer schema version is reported as "created by
  a newer version" even when it also exceeds a version 1 structural limit.
  Which codes are accepted is unchanged.

### Removed

- Linux and macOS support, by decision: the application is Windows only
  ([ADR 0009](docs/adr/0009-windows-only.md)). Configuring on another system stops
  with an error. Gone: the non-Windows code paths, the GCC and AppleClang CI jobs and
  the GCC UBSan job (ASan on MSVC and a clang-cl job with UBSan and warnings replace
  them), the
  `.dmg` and `.tar.gz` packages, the macOS signing options and entitlements, and the
  Linux desktop entry. Existing databases keep opening: a forward migration drops
  `game_installations.platform`. Share codes are unchanged.

### Fixed

- Play's blockers could keep showing an acquisition job as queued after it
  had ended (for example Unavailable) when the last update fell inside the
  5 s launch-refresh throttle; a trailing refresh now follows every
  throttled update.
- Mod list edits made while an undo or redo was in flight could apply to the
  wrong rows; edits now carry the identities of their entries.
- Replies superseded by later requests could leave the window on a stale
  playset; reloads follow the stored active playset.
- Undo and the launch verdict now stay current while edits are being saved;
  the outcome of a launch is shown even after switching games.
- Workshop subscriptions are held until the Web API confirms the item, and a
  mod never has two acquisition jobs at once.
- Local-mod fingerprints are recomputed on every export.
- Path conversions in persistence and providers no longer throw on Windows
  names with unpaired surrogates; transaction targets without an exact UTF-8
  path are refused; playset provider ids that are not valid UTF-8 are
  rejected.
- Launch plans never overwrite another mod's descriptor, require descriptors
  directly in `mod/`, do not rewrite a `dlc_load.json` that only differs in
  layout, and match `mod/` prefixes case-insensitively.
- Nested `PMM_TRY` expansions no longer shadow each other (`-Wshadow`).

### Security

- The user interface shows every piece of data (playset and mod names, tags,
  paths, messages) as plain text, so a crafted share code or descriptor can no
  longer make the window load a remote image. The QML engine's network access
  refuses every URL except local resources.

## [0.1.0] - planned

First release. It will be tagged only when the following are done; until then
the entries above stay under Unreleased.

- The live steps of the manual acceptance procedure in
  [`docs/validation.md`](docs/validation.md) (Workshop download, direct launch
  without the Paradox launcher, in-game order, blocking and failure cases)
  executed and recorded for EU4 and Stellaris, with the support matrix updated
  from the evidence. Combinations that could not be tested stay marked as
  unverified.
- CI passing.
- The Windows ZIP and installer built by CI and checked against
  [`docs/packaging.md`](docs/packaging.md).
- The open launch-related items in
  [`docs/development-status.md`](docs/development-status.md) resolved.
