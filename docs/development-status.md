# Development status (handoff, 2026-11-06)

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
| Build, presets, pinned dependencies, CI, format check | `CMakeLists.txt`, `cmake/`, `CMakePresets.json`, `.github/workflows/ci.yml` | done; CI green on `main` on 2026-10-30 with the four Windows-only checks | |
| Core model, catalog, logging and redaction, SHA-256, UTF-8 | `src/core` | done, tested | |
| Playset operations, readiness, dependencies, import plan | `src/core` | done, tested | |
| Share-code protocol v1 and regression fixtures | `src/sharing`, [`share-code-protocol.md`](share-code-protocol.md) | done, tested | |
| SQLite persistence and migrations | `src/persistence`, [`persistence.md`](persistence.md) | done, tested | |
| Platform: files, paths, processes, Steam discovery, file transactions | `src/platform` | done, tested | |
| Workshop provider and `pmm-steam-helper` | `src/providers`, `tools/steam-helper`, [`steam-workshop.md`](steam-workshop.md) | done; session and item state verified read-only; subscribe and download tested against the fake Steam API only | [probe](evidence/steam-helper-probe.md) |
| EU4 and Stellaris adapters | `src/games`, [`games/`](games/), [`load-order.md`](load-order.md) | done; detection, inventory, current configuration and launch plan verified read-only | [real scan](evidence/real-scan-2026-10-29.md) |
| Qt-free services (games, playsets, sharing, acquisition, launch) | `src/services`, [`architecture-services.md`](architecture-services.md) | done, tested | |
| Desktop application (`pmm-app`): runtime, services worker, Steam helper and Web API hosts, Backend and models, profile lock, offline QML engine | `src/app`, [`app.md`](app.md), [`ui-contract.md`](ui-contract.md) | done, tested; share-code round trip verified read-only for both games | [round trip](evidence/app-round-trip-2026-10-30.md) |
| Launch: wait for the game's Steam helper to exit before the game starts; refresh the downloads and mod rows after every launch outcome | `src/services/acquisition_coordinator`, `src/app/core/session_host`, `src/app/gui/launch_controller`, [`architecture-services.md`](architecture-services.md#data-flow-launch), [`app.md`](app.md#steam-helper-host) | done 2026-11-06, tested with the real `pmm-steam-helper` and the fake Steam API library; not run against a real game | |
| QML user interface | `ui/`, [`ui/README.md`](../ui/README.md) | done; data shown as plain text (checked by a source test) | screenshots in `docs/screenshots/` |
| Accessibility and high-DPI | `ui/`, [`accessibility.md`](accessibility.md) | done except the gaps listed there | UI Automation inspection, contrast check, scale factors 1.5 and 2 |
| Windows package (ZIP) | `cmake/Packaging.cmake`, `packaging/`, [`packaging.md`](packaging.md) | verified from an extracted copy without Qt or Visual Studio | [packaging](evidence/packaging-2026-10-29.md) |
| Windows installer (Inno Setup, per-user) | `packaging/windows/`, [`packaging.md`](packaging.md#windows-installer) | verified with silent runs in a scratch folder, including failed upgrades; wizard not clicked through. Added 2026-11-06: no downgrade over a newer installed version, one Start Menu shortcut instead of a folder, no desktop shortcut brought back by an update run, the previous version started again after a failed update run, the uninstall message about the data folders; compiled without warnings, **not run** (the smoke test runs them on GitHub) | [installer](evidence/installer-2026-10-30.md) |
| Update check, daily check and update button | `src/services/app_update`, `src/app/core/update_client`, `src/app/gui/update_controller`, [`updates.md`](updates.md) | done, tested against a local stub; **no real update has happened**: no published release exists. 2026-11-06 (the updater lockdown): the request by repository id, owner and repository taken from the checked reply, redirects only for downloads and only to `github.com` and `*.githubusercontent.com`, the daily check with its banner and setting, no in-app install in `--profile` and `--no-steam` sessions, retries of the final save, and "Open release page" built from the repository the reply named (the compiled slug's list of releases only while no reply has named one). Merged and verified together on `agent/updater-lockdown` | [installer](evidence/installer-2026-10-30.md#after-merging-the-update-check) |
| Update contract, frozen since 0.1.0 | [`packaging/update-contract.txt`](../packaging/update-contract.txt), `scripts/check-update-contract.sh`, `scripts/test-update-contract.sh`, [`updates.md`](updates.md#compatibility-contract-frozen-since-010) | done 2026-11-06: CI's "Format check" checks the sources and tests the check (80 cases); the Release workflow checks the version, the published releases, the repository id in `pmm-app.exe` and the assets before the draft. Passes on the merged tree, and a deliberate drift fails it; **not run on GitHub yet** | |
| `Release` workflow | `.github/workflows/release.yml`, [`releasing.md`](releasing.md) | dry run passed on GitHub on 2026-10-30 (validation, full CI, package, installer smoke test); **the publish step has not run**, only locally against a stub `gh`. The contract checks and the extended smoke test of 2026-11-06 have not run on GitHub | [installer](evidence/installer-2026-10-30.md#release-workflow-what-could-be-checked) |
| README, ADRs, support matrix, validation procedure, troubleshooting, changelog | repository root, `docs/` | up to date with this handoff | |
| In-game probe mods | `tools/probe-mods/probe_mods.py` | done; not yet used against a running game | |

## Test status (2026-11-06, updater lockdown merged)

The updater's three branches (`agent/updater-core`, `agent/update-daily`,
`agent/update-contract`) merged without conflicts on `agent/updater-lockdown`, in
that order: `agent/update-contract` alone fails its own contract check, because the
repository id comes with `agent/updater-core`, so the three land together. A review of
the merge found that "Open release page" still used the compiled slug instead of the
repository the reply named (reproduced, fixed, regression tests in
`update_tests.cpp` and `qml_tests.cpp`). Local runs on the merged tree; nothing of this
has run on GitHub yet.

- `debug` preset, fresh configure and clean build: **0 warnings**, **500/500 tests
  pass**, by label (some tests carry two): core 50, sharing 54, persistence 71,
  platform 51, providers 61, games 52, services 77, application 89. The new
  release-page tests fail with the previous `openReleasePage()` and pass with the
  fix.
- `core-sanitize` (MSVC, ASan) 411/411 and `core-clang-cl` (LLVM 22, UBSan)
  411/411, 0 warnings.
- The 71 update test cases (`[update]`, application and services) passed 10 times
  each, eight in parallel: 710 runs, no failure.
- `scripts/check-format.sh`: 238 files OK. `clang-tidy` 22.1.8 on the ten changed
  `.cpp` files (and the headers they include): no findings. QML lint
  (`ui/dev/preview.sh --lint`): clean. `actionlint` 1.7.12 on both workflows: no
  findings.
- `scripts/check-update-contract.sh`: 54 source checks OK; a changed setup argument
  in `app_update.cpp` and a changed mutex name in `docs/updates.md` each fail it
  (reverted). `scripts/test-update-contract.sh`: 80 cases pass.
- `release` preset without tests, `cpack` and the `installer` target: 0 warnings;
  the ZIP has 80 files, the same list as a package built the same way from
  `07f8cbf`, and the installer stage has the same 80 files, including
  `plugins/tls/qschannelbackend.dll`. Both `pmm-app.exe` files contain the id
  `1439071956` as the workflow's `grep -a` expects.
- `installer.iss` compiles with Inno Setup 6.7.3 without warnings from the real
  stage (also without `/Q`); no setup was run on the development machine.
- The Release workflow's asset steps on the real packages: `SHA256SUMS.txt` written
  as the workflow writes it passes `check-update-contract.sh assets`; CRLF, a BOM,
  the binary marker, uppercase digests, an unlisted file, a name with spaces, a
  name listed twice, a blank line, a changed file, a missing installer, a leading
  zero and an `-rc1` suffix are refused.
- `smoke-test-installer.ps1`: PowerShell's parser finds no errors in the merged
  script; the mutex helper and the process lookup were exercised on their own with
  another mutex name (on `agent/update-contract`). The script itself was not run (it
  starts setups and the application).
- `prune-qt-deployment.cmake` fails on a fake deployment without
  `plugins/tls/qschannelbackend.dll` and passes with it (on `agent/update-contract`).
- Read-only `--screenshot` of Stellaris with a new scratch profile, also with
  Settings open: the window renders as before, the Updates section shows the
  daily-check setting, off in a verification run, and the log says
  `update mode mode=releasePage installed=false dailyCheck=false`. The capture
  replaced `screenshots/app-settings-updates.png`.
- One unauthenticated `GET https://api.github.com/repositories/1439071956/releases/latest`
  (by the review): HTTP 404 without a redirect, as expected while nothing is
  published. A wrong id would also answer 404, so this does not prove the id; the
  Release workflow compares it with `GITHUB_REPOSITORY_ID` before anything is built.

**Not verified:** the installer changes of 2026-11-06 and the new smoke-test cases
run only on the GitHub runner, and have not run there yet; the contract checks of
the workflows have not run on GitHub; a real update between two published releases
has never run, and neither has the update button in a real installed copy.

## Test status (2026-11-06, after the launch fixes)

Local runs, and GitHub Actions CI: green on `main` at `d427f3c` on 2026-11-06, all
four required jobs.

- `debug` preset, fresh configure and clean build: **0 warnings**, **473/473
  tests pass**, by label: core 50, sharing 54, persistence 71, platform 51,
  providers 61, games 52, services 70, application 69.
- `core-sanitize` (MSVC, ASan) 404/404 and `core-clang-cl` (LLVM 22, UBSan)
  404/404, 0 warnings.
- `scripts/check-format.sh`: 237 files OK. `clang-tidy` on the changed C++
  files: no findings.
- The new and changed launch tests passed 20 times in a row each. Removing the
  coordinator's wait, the host's wait or the refresh after a launch makes them
  fail.
- Read-only `--screenshot` of Stellaris with a new scratch profile: the window
  renders as before (no playset, downloads off, Play blocked by read-only mode).

## Test status (2026-10-30, `main`)

- GitHub Actions CI: green on `main` on 2026-10-30 with the Windows-only jobs: MSVC
  467/467, ASan 402/402, clang-cl with UBSan on LLVM 20.1.8 402/402, format check. The
  `Release` workflow's dry run for 0.1.0 passed on the same day, calling that CI
  suite, then building and smoke testing the package on a runner.

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
- Windows-only tree, in scratch folders: fresh-profile dumps and share codes and
  migrated copies of two schema 1 profiles are byte-identical to the last
  cross-platform build, also from the extracted ZIP; ZIP and installer have the same 80
  files; `smoke-test-installer.ps1` and the update run's arguments with a stand-in
  relaunch pass, leaving nothing behind.
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
- A configuration transaction whose **Recover** fails (missing or damaged
  backup, `IntegrityCheckFailed`) keeps blocking that game's launches; other
  games only show a warning. Planned: `recover()` skips targets already back
  to their original contents; `FileTransactionManager::abandon(txid)` with an
  `Abandoned` state; `LaunchService::discardInterrupted(txid)`; a "Discard
  this record" action next to Play, offered only after Recover failed, behind
  a confirmation that names the files; then [`troubleshooting.md`](troubleshooting.md).
  Deferred to after 0.1.0 by the owner's decision of 2026-11-06.
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

- The `Release` workflow's publish step has not run, and no update between two
  published releases has happened; the update button in a real
  installed copy has not been run. The real application's relaunch after an update
  run, the installer changes of 2026-11-06 and the contract checks of the workflow
  run for the first time on GitHub with the next dry run. The
  installer's wizard has not been clicked through, so its license page and header
  image have only been checked outside it.
- The package job's check that `pmm-app.exe` contains the repository id needs the
  id compiled in as text (a string define, as `PMM_GITHUB_REPOSITORY_ID` is passed);
  an id formatted from a number at run time would fail it.
- The repository id 1439071956 has not been confirmed against GitHub from the
  development machine (a 404 from `releases/latest` does not tell a wrong id from an
  empty repository). The `Release` workflow's validation compares it with
  `GITHUB_REPOSITORY_ID` before anything is built, so a wrong id cannot be released.
- Accepted for 0.1.0 after the lockdown review: `check-update-contract.sh` matches
  text, so a value that survives only in a comment still passes (for example
  `/releases/latest` removed from the code, or the relaunch's `Check:` flipped); the
  unit test that pins `latestReleaseApiUrl()` and the smoke test on the runner catch
  those. An update run that the downgrade guard refuses does not start the
  application again; that needs an installed version newer than the running one.
  "Open release page" falls back to the compiled slug's list of releases while no
  reply in the session has named the repository (no release yet, or no answer).
- `ilammy/msvc-dev-cmd` and `jurplel/install-qt-action` are pinned to mutable tags in
  the job that builds the shipped, unsigned installer; pinning them to commit SHAs
  would narrow a supply-chain path to every user who updates.
- No code signing; no `longPathAware` manifest (the package folder path must
  stay below about 170 characters); not tried on a machine without the VC++
  Redistributable.
- The Qt style prune list assumes the Basic style chosen in `main()`.
- The `release` preset uses CMake's `RelWithDebInfo` flags (`/Ob1`).

## Next steps (in order)

1. Done 2026-10-30: the repository is on GitHub
   (https://github.com/Giorgi-Apriashvili/pdx-mod-manager, public, MIT), `main` is
   protected (CI checks required for merges, no force pushes or deletion),
   and the code base is clang-tidy clean (`.clang-tidy`, `tests/.clang-tidy`).
2. Done 2026-10-30: the required checks are "Build and test (Windows MSVC)",
   "Format check", "Sanitizers (Windows MSVC, ASan, Qt-free libraries)" and
   "Warnings (Windows clang-cl, Qt-free libraries)".
3. Optional: warn when two enabled EU4 mods contain the same file.
4. Implement the import options item above, with tests. Done 2026-11-06: the
   helper exit wait and the launch refresh. The discard flow for unrecoverable
   transactions is deferred to after 0.1.0 (owner's decision, 2026-11-06).
5. With the user's approval, the remaining live pieces (table above). Record
   results in [`validation.md`](validation.md) and
   [`support-matrix.md`](support-matrix.md).
6. Release 0.1.0. Its notes are drafted in [`../CHANGELOG.md`](../CHANGELOG.md)
   (`## [0.1.0] - unreleased`; set the date when releasing). The checklist they
   replace is met: the live validation is recorded, CI passed on `main` at
   `d427f3c` on 2026-11-06, the 2026-10-30 dry run built and smoke tested both
   packages (before the launch fixes, so the procedure in
   [`releasing.md`](releasing.md) runs a new one), and the launch items are done or
   deferred. The updater is locked down for 0.1.0 (owner's decision, 2026-11-06:
   unsigned, a daily check that is on by default, no rehearsal of an update between
   two releases): the values every copy relies on are frozen in
   [`../packaging/update-contract.txt`](../packaging/update-contract.txt) and checked in
   CI and in the workflow; the three updater branches land together through
   `agent/updater-lockdown`, never one by one. The next dry run is the first run of
   the extended smoke test and of the contract checks on GitHub. Click through the
   installer wizard once, then the first release, and a second release to try the
   update for real.

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
Steam, Stellaris 4.5.2 (36 mods) and EU4 1.37.5 (13 mods), the versions tested on
2026-10-29 and 2026-10-30. Stellaris 4.5.3 ("Cygnus v4.5.3 (4e91)", build
26325427) is installed now (read-only check, 2026-11-06) and has not been
tested. EU4 has no
`steam_appid.txt`, so a direct start sets `SteamAppId`/`SteamGameId`; its
launcher arguments include `-enabletelemetry`, which direct launches drop.
The helper initialised the Steam API in Stellaris's app context and read item
state correctly. For both games the launch plan for the launcher's current
selection writes nothing, and regenerated Workshop descriptors are identical
to the launcher's.
