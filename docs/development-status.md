# Development status (handoff, 2026-10-30)

Paradox Mod Manager 0.1.0 is feature-complete for the first release on
Windows: every layer of [`spec.md`](../spec.md) section 8 exists, the Windows
package works, and on 2026-10-30 the live acceptance steps were run with the
user's approval ([live validation](evidence/live-validation-2026-10-30.md)):
direct launches of both games with probe mods, a real EU4 Workshop download,
restart persistence and the failure cases, all passing. The run found one
defect (a stale Play blocker, fixed) and one game fact the product must
respect: **EU4 ignores the `enabled_mods` order for file overrides** (the mod
whose name sorts first wins). What is missing: builds of the application on
Linux and macOS, a Workshop acquisition for Stellaris, and the open items
below.

There is no command-line tool, and none is planned: the spec does not ask for
one. Headless checks use the hidden verification switches of `pmm-app`
(`--profile`, `--read-only`, `--self-test`, `--screenshot`, and
`--live-test` for recorded live runs; see [`app.md`](app.md#command-line)).

## Done

| Area | Location | State | Evidence |
| --- | --- | --- | --- |
| Build, presets, pinned dependencies, CI, format check | `CMakeLists.txt`, `cmake/`, `CMakePresets.json`, `.github/workflows/ci.yml` | done; CI has not run yet (nothing pushed) | |
| Core model, catalog, logging and redaction, SHA-256, UTF-8 | `src/core` | done, tested | |
| Playset operations, readiness, dependencies, import plan | `src/core` | done, tested | |
| Share-code protocol v1 and regression fixtures | `src/sharing`, [`share-code-protocol.md`](share-code-protocol.md) | done, tested | |
| SQLite persistence and migrations | `src/persistence`, [`persistence.md`](persistence.md) | done, tested | |
| Platform: files, paths, processes, Steam discovery, file transactions | `src/platform` | done, tested | |
| Workshop provider and `pmm-steam-helper` | `src/providers`, `tools/steam-helper`, [`steam-workshop.md`](steam-workshop.md) | done; session and item state verified read-only; subscribe and download tested against the fake Steam API only | [probe](evidence/steam-helper-probe.md) |
| EU4 and Stellaris adapters | `src/games`, [`games/`](games/), [`load-order.md`](load-order.md) | done; detection, inventory, current configuration and launch plan verified read-only | [real scan](evidence/real-scan-2026-10-29.md) |
| Qt-free services (games, playsets, sharing, acquisition, launch) | `src/services`, [`architecture-services.md`](architecture-services.md) | done, tested | |
| Desktop application (`pmm-app`): runtime, services worker, Steam helper and Web API hosts, Backend and models, profile lock, offline QML engine | `src/app`, [`app.md`](app.md), [`ui-contract.md`](ui-contract.md) | done, tested; share-code round trip verified read-only for both games | [round trip](evidence/app-round-trip-2026-10-30.md) |
| QML user interface | `ui/`, [`ui/README.md`](../ui/README.md) | done; data shown as plain text (checked by a source test) | screenshots in `docs/screenshots/` |
| Accessibility and high-DPI | `ui/`, [`accessibility.md`](accessibility.md) | done except the gaps listed there | UI Automation inspection, contrast check, scale factors 1.5 and 2 |
| Windows package (ZIP) | `cmake/Packaging.cmake`, `packaging/`, [`packaging.md`](packaging.md) | verified from an extracted copy without Qt or Visual Studio | [packaging](evidence/packaging-2026-10-29.md) |
| README, ADRs, support matrix, validation procedure, troubleshooting, changelog | repository root, `docs/` | up to date with this handoff | |
| In-game probe mods | `tools/probe-mods/probe_mods.py` | done; not yet used against a running game | |

## Test status (2026-10-30, `main`)

- Windows 11, MSVC 19.51, Qt 6.10.3, `debug` preset, fresh configure and clean
  build: **0 warnings** at `/W4`, **425/425 tests pass** (`ctest --preset
  debug`): core 50, sharing 54, persistence 69, platform 54, providers 61,
  games 53, services 54, application 35.
- `scripts/check-format.sh` with clang-format 22.1.8: 225 files OK.
- Hidden real-data tests, read-only: `pmm_games_tests "[.real]" -s` passes
  (187 assertions, both games).
- Read-only application smoke test: share-code round trip between two clean
  profiles with byte-identical dumps for Stellaris and EU4, and a screenshot
  per game ([evidence](evidence/app-round-trip-2026-10-30.md)).
- Linux (Arch Linux in WSL, GCC 16.2.1, no Qt): core build with warnings as
  errors, 384/384 tests pass (two permission tests skip themselves because
  the WSL user is root); the same with AddressSanitizer and UBSan, 384/384.
- Not run: the 21 fake-backend QML tests (`ui/dev/preview.sh --test`, need an
  active window on Windows; the real-backend keyboard test in ctest covers the
  main paths), any build on macOS, any build of the application on Linux.

## What remains

### 1. Live validation: remaining pieces

Done on 2026-10-30 ([results](validation.md#6-results)): EU4 steps 1-6, 8,
9; Stellaris steps 1-3, 5-7. Still open, each needing the user's approval:

| What | Why open |
| --- | --- |
| Stellaris step 4 (a Workshop acquisition) | only one new subscription (EU4) was approved |
| EU4 step 7 | run for Stellaris; same code path |
| Steam's own offline mode | the Web API was made unreachable instead |
| EU4 mods with `dependencies` and override precedence | not covered by the probes |

**Product decision needed (EU4 order):** the app lets users reorder EU4
playsets, but EU4 does not use that order for file overrides. The adapter's
`LoadOrderInfo` now says so (`verifiedInGame = true`), but nothing in the UI
shows it yet. Options: show the rule as a note above the mod list for games
whose order is not effective; and/or warn when two enabled EU4 mods contain
the same file. Not decided.

### 2. Other platforms

- Linux and macOS: build the application, run `pmm_app_tests`, check the
  CI `package` job output against the layouts in [`packaging.md`](packaging.md),
  repeat the read-only helper probe and real-data tests on a machine with the
  games, then the live steps.
- macOS signing and notarisation (`packaging/macos/`) are unverified.

### 3. Open items from the last review wave

Services and application:

- `ShareService::applyImport` should take `ImportOptions { bool
  queueAcquisition = true; }`; then replace `applyImportWithoutDownloads` in
  `src/app/gui/share_controller.cpp` with a call that passes
  `queueAcquisition = steam`. Today an import with downloads off cancels the
  jobs it created in the same transaction: the cancelled records stay until
  the 7-day retention removes them, the details panel shows the "cancelled"
  text instead of "Not installed", and jobs left by earlier `--no-steam`
  imports are not cleaned up.
- Wait for the helper to exit before starting the game: add
  `WorkshopSessionHost::waitUntilClosed()`, override it in
  `QtWorkshopSessionHost` (`waitForRetired()`), call it in
  `AcquisitionCoordinator::closeSessionsForLaunch` (launch path only), and let
  the fake Steam in the services tests record the order. Whether a still-exiting
  helper affects EU4 or Stellaris is unproven.
- `LaunchController::launch` should report `LaunchOutcome::acquisition` on
  success, so the downloads view refreshes at once.
- A configuration transaction whose **Recover** fails (missing or damaged
  backup, `IntegrityCheckFailed`) keeps blocking that game's launches; other
  games only show a warning. Planned: `recover()` skips targets already back
  to their original contents; `FileTransactionManager::abandon(txid)` with an
  `Abandoned` state; `LaunchService::discardInterrupted(txid)`; a "Discard
  this record" action next to Play, offered only after Recover failed, behind
  a confirmation that names the files; then [`troubleshooting.md`](troubleshooting.md).
- On Windows, two descriptor files whose names differ only in unpaired
  surrogates map to the same local mod id and make the inventory save fail;
  the adapter scan should merge them or mark them Broken.
- `tools/steam-helper/main.cpp` converts paths with `path::u8string()` for
  diagnostics, which can throw on Windows for unpaired surrogates; use the
  non-throwing conversion of `src/providers/internal/path_text`.
- A mod list edit whose entries are partly gone applies to the rest without
  telling the user (it is refused only when none are left).
- On Clang, `PMM_TRY` names its variable by line number; two on one line
  would collide (no such use exists).
- Optional: bring the running window to the front on a second start
  (`QLocalServer`); one read-only verification run during the review fixes
  ended with a crash at shutdown that did not recur; if it does, enable WER
  local dumps for `pmm-app.exe`.

Accessibility ([`accessibility.md`](accessibility.md), "Known gaps"):

- Screen-reader listening pass with NVDA and Narrator, VoiceOver and Orca
  where possible.
- The minimum window size (980x600) does not fit a 1920x1080 screen at 200 %.
- Keyboard resizing of the column dividers and table columns; a focusable
  summary of the Play blockers.
- `--screenshot` forces scale factor 1; keeping an explicit
  `QT_SCALE_FACTOR` would make high-DPI captures reproducible.
- `ui/dev/preview.sh --test` is not part of ctest or CI.

Packaging ([`packaging.md`](packaging.md)):

- NSIS installer not built (`makensis` missing); no code signing; no
  application icon; no `longPathAware` manifest (the package folder path must
  stay below about 170 characters); not tried on a machine without the VC++
  Redistributable.
- The Qt style prune list assumes the Basic style chosen in `main()`.
- The `release` preset uses CMake's `RelWithDebInfo` flags (`/Ob1`).

## Next steps (in order)

1. Decide how the UI presents EU4's override rule (see above) and implement
   it with tests.
2. With the user's permission, push so CI runs once on Windows, Linux and
   macOS (first run of the application tests on Linux and macOS); fix what it
   finds.
3. Implement the services and application items above (import options,
   helper exit wait, launch refresh), then the discard flow for unrecoverable
   transactions, each with tests.
4. With the user's approval, the remaining live pieces (table above). Record
   results in [`validation.md`](validation.md) and
   [`support-matrix.md`](support-matrix.md).
5. Linux and macOS builds and validation as far as machines allow.
6. Release checklist in [`../CHANGELOG.md`](../CHANGELOG.md) ("0.1.0 -
   planned").

## Working rules

- Real game data is read-only unless the user approves a live step: never
  write, rename or delete under Steam, the game folders or
  `Documents/Paradox Interactive`, never start a game or press Play, never
  send subscribe or download requests, never change Steam subscriptions, never
  touch `launcher-v2.sqlite`.
- Run `pmm-app` only with `--profile <scratch folder>` and `--read-only` or a
  verification switch, which imply read-only and no Steam helper;
  `--live-test` only for an approved live run.
- Fixtures and evidence are sanitized (placeholder user names and paths).

## Facts established on the development machine

See [`feasibility.md`](feasibility.md) and the evidence files: Windows 11,
Steam, Stellaris 4.5.2 (36 mods) and EU4 1.37.5 (13 mods). EU4 has no
`steam_appid.txt`, so a direct start sets `SteamAppId`/`SteamGameId`; its
launcher arguments include `-enabletelemetry`, which direct launches drop.
The helper initialised the Steam API in Stellaris's app context and read item
state correctly. For both games the launch plan for the launcher's current
selection writes nothing, and regenerated Workshop descriptors are identical
to the launcher's.
