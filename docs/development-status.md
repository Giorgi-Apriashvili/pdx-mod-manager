# Development status (handoff, 2026-10-30)

Paradox Mod Manager is a Windows-only application
([ADR 0009](adr/0009-windows-only.md)). Version 0.1.0 is feature-complete for the
first release: every layer of [`spec.md`](../spec.md) section 8 exists, the Windows
package works, and on 2026-10-30 the live acceptance steps were run with the
user's approval ([live validation](evidence/live-validation-2026-10-30.md)):
direct launches of both games with probe mods, a real EU4 Workshop download,
restart persistence and the failure cases, all passing. The run found one
defect (a stale Play blocker, fixed) and one game fact the product must
respect: **EU4 ignores the `enabled_mods` order for file overrides** (the mod
whose name sorts first wins). What is missing: the open items below.

There is no command-line tool, and none is planned: the spec does not ask for
one. Headless checks use the hidden verification switches of `pmm-app`
(`--profile`, `--read-only`, `--self-test`, `--screenshot`, and
`--live-test` for recorded live runs; see [`app.md`](app.md#command-line)).

## Done

| Area | Location | State | Evidence |
| --- | --- | --- | --- |
| Build, presets, pinned dependencies, CI, format check | `CMakeLists.txt`, `cmake/`, `CMakePresets.json`, `.github/workflows/ci.yml` | done; CI last green on 2026-10-30, before the installer and update work and before the Windows-only jobs | |
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
| Windows installer (Inno Setup, per-user) | `packaging/windows/`, [`packaging.md`](packaging.md#windows-installer) | verified with silent runs in a scratch folder, including failed upgrades; wizard not clicked through | [installer](evidence/installer-2026-10-30.md) |
| Update check and update button | `src/services/app_update`, `src/app/core/update_client`, `src/app/gui/update_controller`, [`updates.md`](updates.md) | done, tested against a local stub; **no real update has happened**: no published release exists | [installer](evidence/installer-2026-10-30.md#after-merging-the-update-check) |
| `Release` workflow | `.github/workflows/release.yml`, [`releasing.md`](releasing.md) | written and linted, **has never run** on GitHub; its steps were run locally against a stub `gh` | [installer](evidence/installer-2026-10-30.md#release-workflow-what-could-be-checked) |
| README, ADRs, support matrix, validation procedure, troubleshooting, changelog | repository root, `docs/` | up to date with this handoff | |
| In-game probe mods | `tools/probe-mods/probe_mods.py` | done; not yet used against a running game | |

## Test status (2026-10-30, `main`)

- GitHub Actions CI: last green on 2026-10-30, up to the commit before the
  installer and update work, with the old Linux and macOS jobs. **The Windows-only
  `ci.yml` and `release.yml` have never run on GitHub.** The jobs have only been
  reproduced locally: their presets, not the workflows themselves, on MSVC 14.51 and
  LLVM 22. The image's toolset and LLVM version (20.1.8 when last looked up) are
  untried.

- Windows 11, MSVC 19.51, Qt 6.10.3, `debug` preset, fresh configure and clean
  build: **0 warnings** at `/W4`, **467/467 tests pass** (`ctest --preset
  debug`), by label (some UTF-8 tests carry two): core 50, sharing 54, persistence 71, platform 51, providers 61,
  games 52, services 68, application 65.
- Local runs of the other CI presets, 0 warnings: `core-sanitize` (MSVC, ASan)
  402/402, `core-clang-cl` (LLVM 22, UBSan) 402/402.
- Removing Linux and macOS took the suite from 469 to 467: 4 tests that only
  exercised other systems were deleted, 4 were renamed, and 2 migration tests were
  added.
- `scripts/check-format.sh` with clang-format 22.1.8: 236 files OK. `actionlint`
  1.7.12 on both workflows and `clang-tidy` on the changed C++ files: no findings.
- Hidden real-data tests, read-only: `pmm_games_tests "[.real]" -s` passes
  (187 assertions, both games).
- Read-only application smoke test: share-code round trip between two clean
  profiles with byte-identical dumps for Stellaris and EU4, and a screenshot
  per game ([evidence](evidence/app-round-trip-2026-10-30.md)).
- Not run: the 21 fake-backend QML tests (`ui/dev/preview.sh --test`, need an
  active window; the real-backend keyboard test in ctest covers the main paths).

## What remains

### 1. Live validation: remaining pieces

Done on 2026-10-30 ([results](validation.md#6-results)): EU4 steps 1-6, 8,
9; Stellaris steps 1-7. Still open, each needing the user's approval:

| What | Why open |
| --- | --- |
| EU4 step 7 | run for Stellaris; same code path |
| Steam's own offline mode | the Web API was made unreachable instead |
| EU4 mods with `dependencies` and override precedence | not covered by the probes |

**EU4 order:** EU4 does not use the playset order for file overrides. The
adapter's `LoadOrderInfo` says so (`verifiedInGame = true`), and the mods
pane shows its `userNote` as a closable info banner for EU4 playsets
(`GamesModel.currentLoadOrderNote`, [ui-contract](ui-contract.md)). Not
done (possible later): a warning when two enabled EU4 mods contain the same
file.

### 2. Open items from the last review wave

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

- Screen-reader listening pass with NVDA and Narrator.
- The minimum window size (980x600) does not fit a 1920x1080 screen at 200 %.
- Keyboard resizing of the column dividers and table columns; a focusable
  summary of the Play blockers.
- `--screenshot` forces scale factor 1; keeping an explicit
  `QT_SCALE_FACTOR` would make high-DPI captures reproducible.
- `ui/dev/preview.sh --test` is not part of ctest or CI.

Packaging ([`packaging.md`](packaging.md)):

- The `Release` workflow has not run yet (it cannot before it is on GitHub), and
  no update between two published releases has happened; the update button in a real
  installed copy and the real application's relaunch have not been run. The
  installer's wizard has not been clicked through.
- `ilammy/msvc-dev-cmd` and `jurplel/install-qt-action` are pinned to mutable tags in
  the job that builds the shipped, unsigned installer; pinning them to commit SHAs
  would narrow a supply-chain path to every user who updates.
- No code signing; no application icon; no `longPathAware` manifest (the package folder path must
  stay below about 170 characters); not tried on a machine without the VC++
  Redistributable.
- The Qt style prune list assumes the Basic style chosen in `main()`.
- The `release` preset uses CMake's `RelWithDebInfo` flags (`/Ob1`).

## Next steps (in order)

1. Done 2026-10-30: the repository is on GitHub
   (https://github.com/Giorgi-Apriashvili/pdx-mod-manager, public, MIT), `main` is
   protected (CI checks required for merges, no force pushes or deletion),
   and the code base is clang-tidy clean (`.clang-tidy`, `tests/.clang-tidy`).
2. Before the Windows-only CI reaches `main`: the repository owner changes the
   required checks to "Build and test (Windows MSVC)", "Format check",
   "Sanitizers (Windows MSVC, ASan, Qt-free libraries)" and
   "Warnings (Windows clang-cl, Qt-free libraries)". The old names would otherwise
   wait forever.
3. Optional: warn when two enabled EU4 mods contain the same file.
4. Implement the services and application items above (import options,
   helper exit wait, launch refresh), then the discard flow for unrecoverable
   transactions, each with tests.
5. With the user's approval, the remaining live pieces (table above). Record
   results in [`validation.md`](validation.md) and
   [`support-matrix.md`](support-matrix.md).
6. Release checklist in [`../CHANGELOG.md`](../CHANGELOG.md) ("0.1.0 -
   planned"), then the procedure in [`releasing.md`](releasing.md): a dry run of
   the `Release` workflow, click through the wizard once, the first release, and
   a second release to try the update button for real.

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
