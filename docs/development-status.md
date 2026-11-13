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
| Build, presets, pinned dependencies, CI, format check | `CMakeLists.txt`, `cmake/`, `CMakePresets.json`, `.github/workflows/ci.yml` | done; CI green on `main` on 2026-10-30 with the four Windows-only checks. Since 2026-11-07 (owner's decision) CI runs only for a release and by hand; pushes are checked locally with `scripts/check.sh` (`--full` runs the same checks) | |
| Core model, catalog, logging and redaction, SHA-256, UTF-8 | `src/core` | done, tested | |
| Playset operations, readiness, dependencies, import plan | `src/core` | done, tested | |
| Share-code protocol v1 and regression fixtures | `src/sharing`, [`share-code-protocol.md`](share-code-protocol.md) | done, tested | |
| SQLite persistence and migrations | `src/persistence`, [`persistence.md`](persistence.md) | done, tested | |
| Platform: files, paths, processes, Steam discovery, file transactions | `src/platform` | done, tested | |
| Workshop provider and `pmm-steam-helper` | `src/providers`, `tools/steam-helper`, [`steam-workshop.md`](steam-workshop.md) | done; session and item state verified read-only; subscribe and download tested against the fake Steam API only | [probe](evidence/steam-helper-probe.md) |
| EU4 and Stellaris adapters | `src/games`, [`games/`](games/), [`load-order.md`](load-order.md) | done; detection, inventory, current configuration and launch plan verified read-only | [real scan](evidence/real-scan-2026-10-29.md) |
| Crusader Kings III adapter (for the next release) | `src/games/ck3.{hpp,cpp}`, [`games/ck3.md`](games/ck3.md), [`load-order.md`](load-order.md#crusader-kings-iii) | done 2026-11-13 on `agent/ck3`, merged into `main` (`5013efe`). Owner's decisions of 2026-11-12/13: the game's own Steam API library is loaded as for the other games, the catalog lists the supported games alphabetically (`ck3`, `eu4`, `stellaris`), the load order ships unverified (`loadOrderVerifiedInGame = false`, and a temporary note above a CK3 playset says so) until a probe run approved by the owner. Also for every game: launches start in the executable's folder (unchanged for Stellaris and EU4), and a Workshop folder without `descriptor.mod` is described by its only `.mod` file, also by the content check after a download. Detection, inventory, current configuration, launch plan and a share-code round trip verified read-only on 1.20.1.2. **Probe run done 2026-11-13** with the owner's approval (recorded on `agent/ck3-probe`): two direct launches without the launcher or a restart through Steam, and **the later mod in `enabled_mods` wins**, as in Stellaris; `loadOrderVerifiedInGame = true` and the temporary note removed. **Workshop acquisition not run**; `supported_version="1.20"` still undetermined | [CK3 adapter](games/ck3.md#real-machine-evidence), [CK3 probe run](evidence/2026-11-13-ck3-load-order.md) |
| Qt-free services (games, playsets, sharing, acquisition, launch) | `src/services`, [`architecture-services.md`](architecture-services.md) | done, tested | |
| Desktop application (`pmm-app`): runtime, services worker, Steam helper and Web API hosts, Backend and models, profile lock, offline QML engine | `src/app`, [`app.md`](app.md), [`ui-contract.md`](ui-contract.md) | done, tested; share-code round trip verified read-only for both games | [round trip](evidence/app-round-trip-2026-10-30.md) |
| Launch: wait for the game's Steam helper to exit before the game starts; refresh the downloads and mod rows after every launch outcome | `src/services/acquisition_coordinator`, `src/app/core/session_host`, `src/app/gui/launch_controller`, [`architecture-services.md`](architecture-services.md#data-flow-launch), [`app.md`](app.md#steam-helper-host) | done 2026-11-06, tested with the real `pmm-steam-helper` and the fake Steam API library; not run against a real game | |
| Play without a playset (for 0.1.1) | `src/services/launch_service`, `src/app/core/view_data`, `src/app/gui/launch_controller`, `ui/panes/PlaySection.qml`, [`architecture-services.md`](architecture-services.md#launchservice-launch_servicehpp), [`ui-contract.md`](ui-contract.md#launchcontroller-qobject) | done 2026-11-06 on `agent/vanilla-launch`. Owner's decision of 2026-11-06: spec section 7 says "validate the installation, active playset, ..."; no playset now means a launch without mods instead of a blocker. Every other check still runs; the empty mod list is written as one journaled transaction labelled `launch <game> without mods`, which Restore and Recover handle like any launch. Tested with fakes only; **not run against a real game** (a live run needs the owner's approval) | |
| Play requires the Steam client for Steam installations (for 0.1.1) | `src/services/launch_service`, `src/platform/steam`, `src/app/gui/backend`, `src/app/core/view_data`, [`architecture-services.md`](architecture-services.md#launchservice-launch_servicehpp), [`app.md`](app.md), [`ui-contract.md`](ui-contract.md#launchcontroller-qobject) | done 2026-11-06 on `agent/require-steam`. Owner's decision of 2026-11-06: games could be started directly with Steam closed; Play now requires the Steam client for Steam installations (`DistributionSource::Steam`: found through Steam's app manifest, or located by hand inside a Steam library). A copy from another store located by hand is not blocked. The check uses the process list the running-instance check already reads (`steam.exe`, or the pid in `HKCU\Software\Valve\Steam\ActiveProcess` without a readable name); an unreadable list is a warning. While Steam, the game or the Paradox launcher blocks, the Backend re-checks every 3 s without the write probes, so Play follows on its own. Tested with fakes only; **not run against a real game or Steam** (a live run needs the owner's approval). Limitations: whether Steam runs is detected, whether it is signed in is not; the process list covers every Windows session, so another signed-in user's Steam counts as running | |
| Discard an interrupted change that Recover cannot undo (for the next release) | `src/platform/file_transaction`, `src/services/launch_service`, `src/app/gui/launch_controller`, `ui/panes/PlaySection.qml`, `ui/dialogs/ConfirmDialog.qml`, [`troubleshooting.md`](troubleshooting.md#recover-reports-a-missing-or-damaged-backup), [ADR 0005](adr/0005-journaled-config-writes.md) | done 2026-11-07 on `agent/discard-interrupted`. Recovery skips the backups of targets already original (or absent and absent before); `abandon()` gives up an interrupted transaction (`abandoned`, no file changed, folder and backups kept, pruned like any finished one but counted from when it was abandoned); after a Recover that failed with `IntegrityCheckFailed` the banner offers "Discard this record", and Play is available afterwards when nothing else blocks. Tested with fakes only; **not run against a real game** | |
| QML user interface | `ui/`, [`ui/README.md`](../ui/README.md) | done; data shown as plain text (checked by a source test) | screenshots in `docs/screenshots/` |
| Accessibility and high-DPI | `ui/`, [`accessibility.md`](accessibility.md) | done except the gaps listed there; 2026-11-12 on `agent/accessibility-6`: the smallest window fits 1920x1080 at 200 %, keyboard resizing of the columns, Play focusable while blocked, preview tests in ctest | UI Automation inspection, contrast check, scale factors 1.5 and 2, captures of the smallest window at 100 % and 200 % |
| Windows package (ZIP) | `cmake/Packaging.cmake`, `packaging/`, [`packaging.md`](packaging.md) | verified from an extracted copy without Qt or Visual Studio | [packaging](evidence/packaging-2026-10-29.md) |
| Windows installer (Inno Setup, per-user) | `packaging/windows/`, [`packaging.md`](packaging.md#windows-installer) | verified with silent runs in a scratch folder, including failed upgrades; the owner installed the published 0.1.0 with the wizard on 2026-11-06, and it worked. Added 2026-11-06: no downgrade over a newer installed version, one Start Menu shortcut instead of a folder, no desktop shortcut brought back by an update run, the previous version started again after a failed update run, the uninstall message about the data folders; verified by the extended smoke test on GitHub on 2026-11-06 ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42164236292)) | [installer](evidence/installer-2026-10-30.md) |
| Update check, daily check and update button | `src/services/app_update`, `src/app/core/update_client`, `src/app/gui/update_controller`, [`updates.md`](updates.md) | done, tested against a local stub; **a real update verified** on 2026-11-07: the owner's installed 0.1.0 updated itself to [0.1.1](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/tag/v0.1.1). 2026-11-06 (the updater lockdown): the request by repository id, owner and repository taken from the checked reply, redirects only for downloads and only to `github.com` and `*.githubusercontent.com`, the daily check with its banner and setting, no in-app install in `--profile` and `--no-steam` sessions, retries of the final save, and "Open release page" built from the repository the reply named (the compiled slug's list of releases only while no reply has named one). Merged and verified together on `agent/updater-lockdown` | [installer](evidence/installer-2026-10-30.md#after-merging-the-update-check) |
| Update contract, frozen since 0.1.0 | [`packaging/update-contract.txt`](../packaging/update-contract.txt), `scripts/check-update-contract.sh`, `scripts/test-update-contract.sh`, [`updates.md`](updates.md#compatibility-contract-frozen-since-010) | done 2026-11-06: CI's "Format check" checks the sources and tests the check (80 cases); the Release workflow checks the version, the published releases, the repository id in `pmm-app.exe` and the assets before the draft. Passes on the merged tree, and a deliberate drift fails it; passed on GitHub in the dry run of 2026-11-06 ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42164236292)). Since 2026-11-07 the check reads the code of the sources, without their comments, and also checks the relaunch after an update run: 56 source checks, 112 test cases | |
| `Release` workflow | `.github/workflows/release.yml`, [`releasing.md`](releasing.md) | published 0.1.0 on 2026-11-06 ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42197165030)); dry runs passed on 2026-10-30 and 2026-11-06. The contract checks and the extended smoke test passed in the dry run of 2026-11-06 ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42164236292)): 57 of 57 installer checks, including the update run against a held mutex | [installer](evidence/installer-2026-10-30.md#release-workflow-what-could-be-checked) |
| README, ADRs, support matrix, validation procedure, troubleshooting, changelog | repository root, `docs/` | up to date with this handoff | |
| In-game probe mods | `tools/probe-mods/probe_mods.py` | done; used in the live validation of 2026-10-30 (Stellaris, EU4). `--game ck3` (CK3 event syntax, 2026-11-13) checked by creating the probes into a scratch folder, then used for the CK3 probe run of 2026-11-13 | [CK3 probe run](evidence/2026-11-13-ck3-load-order.md) |

## Test status (2026-11-13, Crusader Kings III probe run, `agent/ck3-probe`)

The live run itself is recorded in the
[CK3 probe run](evidence/2026-11-13-ck3-load-order.md): two direct launches of
1.20.1.2 by the debug build of `main` (`5013efe`) with a scratch profile,
`--live-test` and `--no-steam`, approved by the owner; the configuration
restored and the probes removed afterwards. The branch then records it:
CK3's traits (`loadOrderSummary` with Stellaris's wording, `loadOrderEvidence`,
`loadOrderVerifiedInGame = true`, no `loadOrderUserNote`), their checks in
`ck3_adapter_tests.cpp` and `registry_tests.cpp`, the CK3 row of the
preview's `FakeGamesModel.qml`, and the docs. No game was started and no game
file was written for these checks.

- `debug` preset: **0 warnings**, **565/565 tests pass**, by label: core 51,
  sharing 55, persistence 72, platform 59, providers 62, games 68, services
  91, application 111, ui-preview 1.
- `scripts/check.sh --full` on `c664177`: passed in 7 minutes (ASan 453/453,
  clang-cl with UBSan 453/453, clang-tidy on the 3 C++ files changed since
  `origin/main` without findings, the `ci` preset from a fresh checkout
  565/565).

## Test status (2026-11-13, Crusader Kings III, `agent/ck3`)

Local runs only, with fakes, fixtures and new scratch profiles; no game, no
launcher and no Steam client was started, nothing under Steam, the game
folders or `Documents/Paradox Interactive` was written (the CK3 `dlc_load.json`
and `mod/*.mod` kept their timestamps of 2026-07-06), and the launcher's
databases and `playsets_backup` were not opened.

- `debug` preset: **0 warnings**, **560/560 tests pass**, by label: core 51,
  sharing 55, persistence 72, platform 59, providers 61, games 66, services
  89, application 111, ui-preview 1.
- New: `ck3_adapter_tests.cpp` (11 cases on the fixture
  `tests/fixtures/games/ck3`: discovery through `launcher/` and
  `binaries/`, both wrong-game directions, the fallback executable,
  `game/userdir.txt` only, the identity rules, the current configuration,
  exact launch plans with the working directory `binaries/`, regenerated
  descriptors identical to the launcher's, items without `descriptor.mod`,
  the `supported_version` rule, process names and load-order information);
  Stellaris cases for the working directory and the sole `.mod` file; a CK3
  share-code round trip, the golden fixture `05-ck3-basic` (checked with
  Python's `zlib`, `base64` and `hashlib`), catalog order and registry
  checks. Changed: the unsupported example of the share-code tests is now
  `vic3`.
- Against the code without the two shared changes (working directory and
  sole `.mod` file), the new checks fail: 4 test cases (working directory,
  launch plan, items without `descriptor.mod`, the Stellaris sole `.mod`
  case), and the regeneration of a sole `.mod` in an author's key order.
- `pmm_games_tests`, `pmm_sharing_tests`, the catalog tests of
  `pmm_core_tests` and the preview tests passed 10 times each.
- `pmm_games_tests "[.real]"` (read-only): CK3 1.20.1.2 11 mods, 9 enabled,
  0 unresolved, 0 writes, 7 of 11 regenerated descriptors identical (the
  other 4 registrations are older than their content); the Stellaris and EU4
  output was identical, line for line, to a run of the code before these
  changes. Stellaris on the machine is now "Cygnus v4.5.4 (1b4f)", build
  26557690 (39 mods), and was not otherwise tested.
- The application read-only with new scratch profiles: `--game ck3
  --screenshot` (CK3 first in the games column, Play blocked by the read-only
  mode), and the scripted share-code round trip for CK3 with byte-identical
  dumps (9 entries).
- `tools/probe-mods/probe_mods.py create` into scratch folders: the default,
  `--game stellaris` and `--game eu4` output is byte-identical to the previous
  version; `--game ck3` writes CK3 events with a BOM; `analyze` on a
  synthetic CK3-style log.
- `scripts/check.sh --full` on the branch (`15517c0`): passed (ASan and
  clang-cl with UBSan 448/448 each, clang-tidy on the 12 changed C++ files
  after one finding was fixed, the `ci` preset from a fresh checkout
  560/560).
- Review fixes (`ffca05e`): the content check after a Workshop download
  accepts a sole `.mod` file (providers cases for none, one, two, empty,
  `descriptor.mod` first and `.zip`; a services case that downloads such an
  item to Ready; a services case that compares the check with the scan on
  8 folder layouts); a registered folder with a stray `.zip` stays the
  content (a CK3 and a Stellaris case); CK3's temporary load-order note; the
  hint for a CK3 subfolder chosen by hand. With the previous
  `acquisition.cpp` and `dlc_load_adapter.cpp`, the new checks fail (3
  games, 1 providers and 2 services test cases). `debug`:
  **0 warnings**, **565/565 tests pass**, by label: core 51, sharing 55,
  persistence 72, platform 59, providers 62, games 68, services 91,
  application 111, ui-preview 1. `pmm_games_tests "[.real]"` (read-only):
  the Stellaris, EU4 and CK3 output is identical, line for line, to a run
  of the code before the fixes. A scratch program ran the download's
  content check read-only over the 11 CK3 Workshop folders: 11 accepted
  (10 with the code before). The application read-only with a new scratch
  profile (`--game ck3 --self-test config --screenshot`) shows the CK3
  load-order note. `scripts/check.sh --full` on `ffca05e`: passed (ASan and
  clang-cl with UBSan 453/453 each, clang-tidy on the 17 C++ files changed
  since `origin/main` without findings, the `ci` preset from a fresh
  checkout 565/565).

## Test status (2026-11-12, accessibility item 6b-e, `agent/accessibility-6`)

Local runs only, with fakes, a hermetic profile and new scratch profiles; no
game was started, nothing under Steam, the game folders or
`Documents/Paradox Interactive` was written, and the real profile was not
used.

- `debug` preset: **0 warnings**, **544/544 tests pass**, by label (some
  tests carry two): core 50, sharing 54, persistence 72, platform 59,
  providers 61, games 53, services 89, application 110, ui-preview 1 (the 30
  fake-backend QML tests in one ctest test, new).
- New tests in `qml_tests.cpp`: a blocked Play reached with Tab (its name,
  description and tool tip; Enter, Space and a click post the reasons and
  start no process; the same key launches once Steam runs, and Play keeps the
  focus); both column dividers and the table's column edges resized from the
  keyboard (arrows, Shift, Home, End, Enter, Ctrl+Left/Right, the stored
  widths, roles and names); the smallest window, 900x440, with an interrupted
  change and the read-only blocker (columns side by side at their minimums,
  the header row inside the mods column, the actions column scrolling and
  Play scrolled into view by Tab); a window saved at 900x440 and 1000x520
  restored, at 899x440 and 900x439 ignored. Changed: the checks of a disabled
  Play now check that it is unavailable, and the Updates harness waits until
  the window's content has its new size (before, at 980x600, the Settings
  check passed without the dialog having been laid out at that size). New
  preview tests: a13 (blocked Play), a14 and a15 (column edges and dividers),
  b1 (the smallest window, a squeezed column's stored width), b2 (a dragged
  divider's stored width); test_8 checks the unavailable Play.
- Against the QML before these changes (the new tests and fakes kept), every
  new and changed test fails: the four new `qml_tests.cpp` cases, the two
  changed Play cases, and preview tests 8, a13, a14, a15, b1 and b2 (b2 only
  as a consequence of b1 leaving the window narrow; it guards the stored
  widths of a drag). `ctest -L ui-preview` reports the failing run as failed.
- Every `[qml]` case of `pmm_app_tests` (20, including the new and changed
  ones) and the 30 preview tests passed 10 times each (200 and 300 runs).
- `scripts/check-format.sh`: 238 files OK. QML lint
  (`ui/dev/preview.sh --lint`): clean. `scripts/check.sh --full` on the
  branch's last commit: passed (ASan, clang-cl with UBSan, clang-tidy on the
  changed C++ files, and the `ci` preset from a fresh checkout, which runs the
  preview tests through ctest too).
- Screenshots (scratch only; `docs/screenshots/*` unchanged, since the
  documented 1400x860 layout renders the same apart from animations): the
  preview at 900x440 at scale factors 1 and 2 in both themes, with Play
  blocked (also with Steam not running and an interrupted change, the actions
  column scrolling), with Play available (no playset), focus on the blocked
  Play, on both dividers and on a column edge, the update banner, and every
  dialog; the real application read-only with a new scratch profile
  (`--self-test config --select-row 2 --window-size 1x1`), light and dark, at
  900x440 and, with `QT_SCALE_FACTOR=2`, 1800x880. The minimum size and the
  `QT_SCALE_FACTOR` capture were not checked on a real 200 % display.

Review fixes (same day, same branch):

- Fixed: the dividers announced the old width (the announcement ran before
  the split view applied the new one; it now waits for the width, or speaks
  at once when a key changes nothing); a key or drag that could not widen a
  squeezed column stored the squeezed width (now nothing is stored, and
  Enter still stores the default); a column edge kept the focus when the
  header hid (Ctrl+N, no playset), and arrows resized hidden columns (the
  focus now moves to the list, or the column's first control); the footer's
  "Showing N of M" was pushed under the actions column in a narrow mods
  column (the counts now drop their words and give way first); the narrow
  header cut "With warnings" short with room to spare (the option lists are
  as wide as their longest option, up to a quarter of the row); a blocked
  Play's tool tip covered its reasons (it now lists only the ones behind
  "…and N more"); Play's name said "unavailable" during a launch it started
  ("starting" now); holding Enter repeated a button's action (auto-repeat is
  ignored); a long confirmation's scroll bar covered its text and the text
  could not be scrolled from the keyboard (room for the bar; Up, Down, PgUp,
  PgDn, Home and End scroll it); `scripts/check.sh` passed without the
  preview tests when `qmltestrunner` was missing (it now fails, and CMake
  only takes the kit's own `qmltestrunner`); the docs promised exact
  `QT_SCALE_FACTOR` captures of windows that do not fit the screen.
- `debug` preset: **545/545 tests pass** (application 111, ui-preview 1 with
  the 33 fake-backend QML tests).
- New tests: in `qml_tests.cpp`, the announcements of divider and column
  edge keys, recorded with `QAccessible::installUpdateHandler` after
  switching on the offscreen platform's accessibility (which needs
  `Qt6::GuiPrivate` in the test target): nothing is spoken before the
  column has its new width, then the width once; keys at a limit and on a
  squeezed column in the smallest window speak the current width and keep
  the stored one, which comes back in a wider window. Changed: the blocked
  Play case checks that held Enter posts nothing more and that the tool tip
  is empty; the smallest-window case checks the icon-only toolbar (and its
  labels at 1400 wide) and that "Showing 1 of 2" stays inside the mods
  column. Preview tests: a16 (a hidden column edge gives up the focus), b3
  (squeezed columns keep the chosen widths, keys and drags), b4 (a long
  confirmation scrolls from the keyboard and leaves room for its bar); a13
  checks the tool tip with two and with six reasons and the "starting" name;
  b1 checks the toolbar, both header option lists and the footer.
- Against the code before the fixes, the new checks fail: the announcement
  case (stale "300 pixels" and the like), the held Enter (6 posts instead of
  3), a13, a16, b1 (the footer), b3 (250 stored instead of 300 after Left on
  the squeezed actions divider; with only `Main.qml` reverted, 188 instead
  of 200 after a drag), b4.
- The changed `[qml]` cases and the preview tests passed 10 times each.
- Screenshots (scratch only; `docs/screenshots/*` unchanged, since the
  documented 1400x860 layout is unchanged): the preview at 900x440 at scale
  factors 1 (light, dark) and 2, at 911x512 at 1.5 (dark), 1120x700 and
  1400x860, with a filter and a scan running, a search, a blocked Play with
  the focus (two and five reasons), the actions divider focused, and the
  discard confirmation with 14 and 40 files, scrolled to its end; the real
  application read-only with new scratch profiles
  (`--self-test config --filter disabled --window-size 1x1`) at scale 1 and
  2. One of these real-application runs ended with a crash at shutdown after
  writing its capture, as noted under Open items; 13 more runs did not.

## Test status (2026-11-07, discard an unrecoverable change, `agent/discard-interrupted`)

Local runs only, with fakes and temporary folders; no game was started and no
real journal was touched.

- `debug` preset: **0 warnings**, **532/532 tests pass**, by label: core 50,
  sharing 54, persistence 71, platform 59, providers 61, games 52, services
  86, application 104.
- New tests: in `file_transaction_tests.cpp`, recovery with a missing or
  damaged backup of a target that is already original (a crash before the
  first write, a file put back by hand while a created file is still
  removed), recovery that still refuses with `IntegrityCheckFailed` and
  changes nothing when a needed backup is gone (and reports the read error
  while the target cannot be read), `restore()` without the backup of a
  target already original, `abandon()` on "applying" and "prepared" records
  (no target changed, folder and remaining backup kept, excluded from
  `listIncomplete()`, read back after a restart, refused again), its refusal
  of committed, restored, rolled-back and recovered records, `prune()`
  removing an abandoned record as finished, and the `abandoned` name. In
  `launch_service_tests.cpp`, `discardInterrupted()` unblocking this game's
  launch (and the next launch writing the configuration), leaving another
  game's interrupted change as a warning, refusing a recovered or committed
  record, and discarding another game's record. In `runner_tests.cpp`,
  Recover failing on a missing or damaged backup, the offer with the file and
  folder, the discard, Play available and starting, the journal `abandoned`
  with its reason; a Recover failing for another reason (a folder in place of
  the file) without the offer, then succeeding; read-only sessions refusing
  both. In `qml_tests.cpp`, the action appearing only after Recover failed,
  the confirmation naming the file and folder with Cancel focused, Enter and
  Escape changing nothing, and **Discard** hiding the banner and enabling
  Play. The relaxed recovery and restore tests fail against the code before
  the change. The 31 new and changed tests passed 10 times each, eight in
  parallel (310 runs).
- After the review: a backup that another program keeps open is reported
  with its read error (Recover can be retried, nothing is offered to discard)
  instead of `IntegrityCheckFailed`; `prune()` counts an abandoned record from
  when it was abandoned, so a freshly discarded record of an old launch keeps
  its remaining backups; focus leaves Recover or the discard link for the mod
  list when they go away. New tests: a backup held open without sharing
  (`IoError` with the "close it and try again" hint, nothing changed, then
  recovered once released), `prune()` keeping a record abandoned after newer
  launches, the offer ending when the game changes and a Recover answered
  after a switch offering nothing (a second fake game, not installed), and
  discarding from the keyboard leaving the focus on the mod list (in
  `qml_tests.cpp` and the preview tests). The held-backup, prune and focus
  tests fail against the code before these fixes; the changed tests passed 10
  times each, eight in parallel.
- Fake-backend QML tests (`ui/dev/preview.sh --test`, offscreen): 25/25.
  QML lint: clean. `scripts/check-format.sh`: 238 files OK. `clang-tidy` on
  the changed `.cpp` files: clean.

## Test status (2026-11-06, Play requires Steam, `agent/require-steam`)

Local runs only, with fakes; nothing of this has run on GitHub, and no game
was started and Steam was neither started nor stopped.

- `debug` preset: **0 warnings**, **517/517 tests pass**, by label: core 50,
  sharing 54, persistence 71, platform 51, providers 61, games 52, services
  84, application 99 (after the review fixes below).
- `core-clang-cl` (LLVM 22, UBSan, warnings as errors): 0 warnings, 418/418.
- Review fixes: the installation's blockers (folders, a running game or
  launcher, Steam) now come before the playset's one-per-mod blockers, so
  the Play section does not hide them behind "...and N more"; an unreadable
  process list gives one warning for both checks, first in the list, so it
  shows next to Play; a folder located by hand with a trailing separator is
  classified like one without (it was `Manual` inside a Steam library); the
  "started" state line and a refusal's "Could not start the game" banner go
  back to idle once the game has exited or the refusal is resolved. New
  tests: the blocker order and refusal codes, the single warning, the
  trailing separator, the state clearing (and a start failure that keeps its
  banner), and a playset chosen while a re-check waits on the worker. Each
  fails against the code before its fix, and the last one when a dropped
  re-check does not clear its in-flight flag; the eight new and changed
  tests passed 10 times each, eight in parallel.
- New tests: in `launch_service_tests.cpp`, a Steam installation blocked
  with the exact text and `steamNotRunning`, with and without a playset,
  `launch()` refusing with `ProviderUnavailable` without a plan, a
  transaction or a process, and going ahead once `steam.exe` is listed; the
  registered pid counting only for a process without a name; through the
  real Stellaris adapter, a copy located outside a Steam library launching
  with Steam closed (no Steam environment) and one located inside a library
  blocked; an unset or failing process lister giving a warning and no
  blocker. In `runtime_tests.cpp`, the flags in `LaunchData`; in
  `runner_tests.cpp`, Play becoming available on its own after `steam.exe`
  appears in the fake process list, the game exits, the Paradox launcher
  closes, or Steam starts while the game runs, with the re-checks stopping
  afterwards, and no re-checks in read-only sessions; in `qml_tests.cpp`,
  the Play section's blocker, tool tip and accessible description, and the
  button enabling on its own. The test fixtures list a running Steam client
  (the existing tests that expect Play keep passing for that reason, not
  through a weaker check). Disabling the Steam rule fails the services,
  runtime, runner and QML tests; turning the re-checks off fails the runner
  and QML tests; re-checks that never stop fail the runner test. The 13 new
  and changed tests passed 10 times each, eight in parallel (130 runs).
- Fake-backend QML tests (`ui/dev/preview.sh --test`, offscreen): 23/23.
- `scripts/check-format.sh`: 238 files OK. `clang-tidy` 22.1.8 on the five
  changed `.cpp` files and the four changed test files: no findings. QML lint
  (`ui/dev/preview.sh --lint`, and `qmllint` on the changed dev fake): clean.
  `scripts/check-update-contract.sh`: 54 source checks OK.
- Read-only `--screenshot` of Stellaris with a new scratch profile while
  Steam was running (pid 37408, the same as `ActiveProcess`): the Play
  section shows only the read-only blocker, so the production process list
  and registry reading find the real client. The case with Steam closed was
  not run (validation step 6, item 7, needs the owner).

## Test status (2026-11-06, Play without a playset, `agent/vanilla-launch`)

Local runs only; nothing of this has run on GitHub, and the application was not
started against the real games (in read-only verification mode Play is blocked
anyway).

- `debug` preset: **0 warnings**, **507/507 tests pass**, by label: core 50,
  sharing 54, persistence 71, platform 51, providers 61, games 52, services
  80, application 93.
- `core-clang-cl` (LLVM 22, UBSan, warnings as errors): 0 warnings, 414/414.
- New tests: in `launch_service_tests.cpp`, a launch without a playset through
  the real Stellaris adapter (one warning, `enabled_mods` emptied with
  `disabled_dlcs` and an unknown key kept, one transaction labelled
  `launch stellaris without mods`, a history row without a playset, Restore
  finding both label forms) and every other blocker still applying; in
  `runner_tests.cpp`, Play without a playset through the Backend; in
  `qml_tests.cpp`, the Play button enabled with its tool tip, accessible
  description and warning line. Matching the old label prefix in
  `lastRestorableTransaction()`, or the old `hasPlayset` condition in
  `PlaySection.qml`, makes them fail. The new and changed launch tests passed
  10 times each (90 runs).
- After the adversarial review: a selected playset whose mods cannot be
  loaded no longer turns into a launch without mods (the Backend and the
  launch check keep the selected id; `runtime_tests.cpp` and
  `runner_tests.cpp` damage an inventory row and check that Play stays
  blocked, also after later refreshes), and an active playset that cannot be
  read blocks instead of counting as none. Launch verdicts computed for
  another playset are dropped, the "started" text follows what the launch
  used (`LaunchController::startedWithPlayset`; `qml_tests.cpp` launches,
  then creates a playset), and an interrupted change is offered for recovery
  even when the check stops early (no adapter, installation unreadable).
  Reverting each fix makes its test fail, except the dropping of stale
  verdicts, which depends on reply timing and has no test; the new tests
  passed 10 times each (70 runs). The Play section's state line no longer uses the deprecated
  injected `text` parameter in `onTextChanged` (a QML warning on every launch,
  seen once the QML test pressed Play).
- Fake-backend QML tests (`ui/dev/preview.sh --test`, offscreen): 23/23.
- `scripts/check-format.sh`: 238 files OK. `clang-tidy` 22.1.8 on the nine
  changed `.cpp` files: no findings. QML lint (`ui/dev/preview.sh --lint`, and
  `qmllint` on the changed dev files): clean, apart from two earlier
  `missing-property` warnings in `tst_ui.qml` (`activeFocusItem.text`, lines this
  change does not touch).

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
  After 0.1.0 was published, the same request answers HTTP 200 with `v0.1.0`.

**Verified on GitHub (dry run of 2026-11-06, [run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42164236292)):** the installer changes
of 2026-11-06 and the extended smoke test (57 of 57 checks: downgrade guard, update
run waiting for the mutex and starting `bin\pmm-app.exe`, a non-ASCII folder, one
Start Menu shortcut and the earlier folder removed, the desktop shortcut rule),
the repository id in `pmm-app.exe`, and the version check.

**Published:** 0.1.0 on 2026-11-06 ([release](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/tag/v0.1.0), [run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42197165030)), checked afterwards
by the procedure in [`releasing.md`](releasing.md#verifying-a-release) and as an
installed copy reads it: the request by repository id answers HTTP 200 without a
redirect, with `v0.1.0` and this repository in `url` and `html_url`, and the real
update client reads it (one check, no download); both downloads redirect once, from
`github.com` to `release-assets.githubusercontent.com`, which the client's rule
accepts; the setup's SHA-256 equals its line in `SHA256SUMS.txt` (LF, no BOM); the
assets pass `check-update-contract.sh assets`; the tag points at `f13e899`; the
notes equal the changelog section plus the fixed note, and their links answer
HTTP 200.

**Verified 2026-11-07:** the first real update. The owner's installed 0.1.0 found
the published 0.1.1, **Update and restart** installed it, and the owner reported
that it worked.

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
9; Stellaris steps 1-7. Done on 2026-11-13: CK3 step 6, items 1-5, with the
probes only ([CK3 probe run](evidence/2026-11-13-ck3-load-order.md)). Still open, each needing the user's
approval:

| What | Why open |
| --- | --- |
| EU4 step 7 | run for Stellaris; same code path |
| Play with Steam closed (0.1.1; step 6, item 7) | new on 2026-11-06; tested with fakes only |
| Steam's own offline mode | the Web API was made unreachable instead |
| EU4 mods with `dependencies` and override precedence | not covered by the probes |
| CK3 step 4 (Workshop subscribe and download for app 1158310) | not run; ideally with an item that ships `<Name>.mod` |
| CK3 `supported_version="1.20"` and `"1.*"` on 1.20.1.2 | started directly, 1.20.1.2 did not read descriptors outside `enabled_mods`, so the disabled probe told nothing ([CK3 adapter](games/ck3.md#outstanding-validation)) |
| CK3 steps 7-9, and step 6 items 6-7 | same code paths as Stellaris and EU4; not run for CK3 |

**EU4 order:** EU4 does not use the playset order for file overrides. The
adapter's `LoadOrderInfo` says so (`verifiedInGame = true`), and the mods
pane shows its `userNote` as a closable info banner for EU4 playsets
(`GamesModel.currentLoadOrderNote`, [ui-contract](ui-contract.md)). CK3's
temporary "not yet verified" note was removed after its probe run of
2026-11-13 (the later mod wins, as in Stellaris). Not done (possible later):
a warning when two enabled EU4 mods contain the same file.

### 2. Open items from the last review wave

Services and application:

- Done 2026-11-07 on `agent/import-options` (owner's go-ahead of 2026-11-07),
  tested with fakes only: `ShareService::applyImport` takes `ImportOptions {
  bool queueAcquisition = true; }`, and `ShareController::confirmImport`
  passes `queueAcquisition = steam` instead of calling
  `applyImportWithoutDownloads`, which created the jobs and cancelled them in
  the same transaction. An import with downloads off now creates and reuses no
  job and leaves existing ones alone; its missing Workshop entries without a
  job read "Not installed. Download it from Steam Workshop." in the details
  panel instead of "The download was cancelled. Retry to acquire this mod.",
  and the others read as their mod's newest job says. The notification counts
  them the same way as the mod list: not queued (restart normally, then
  **Download missing**), still queued from before (resumes after a normal
  restart) and could not be downloaded before (restart normally, then
  **Retry**); the import dialog's confirm tooltip no longer says that missing
  mods are queued. Unlike an import with downloads on, which queues a fresh
  job, an entry whose mod has a failed, unavailable or needs-action job from
  the last 7 days keeps that state and is not taken by **Download missing**;
  this is deliberate (the import touches no job) and the notification says to
  use **Retry**. The jobs that such imports left in 0.1.0 and 0.1.1 are told
  apart reliably and removed at every start
  (`AcquisitionJobRepository::removeWithdrawnImportJobs()` in
  `AppRuntime::runMaintenance`, next to the 7-day retention): in every
  version, only that withdrawal and the acquisition reducer's Cancel wrote the
  Cancelled state, and Cancel always records the error code `cancelled` with a
  message, while a withdrawn job has an empty error code and message, no
  attempt and no byte. A test cancels a parked job that never ran through
  `cancelAll` and checks that the clean-up keeps it. Since nothing creates
  such jobs any more, the clean-up can be dropped in a later version.
- Done 2026-11-07 (owner's decision of 2026-11-07: before the next release),
  tested with fakes only: a configuration transaction whose **Recover** fails
  because of a missing or damaged backup no longer blocks that game for good.
  `recover()` and `restore()` need no backup for a target already back to its
  original contents; `FileTransactionManager::abandon(txid)` marks an
  interrupted transaction `abandoned` without touching a file;
  `LaunchService::discardInterrupted(txid)`; "Discard this record" next to
  Recover, offered only after Recover failed with `IntegrityCheckFailed`,
  behind a confirmation (Cancel focused) that names the files and the backup
  folder; a backup that is only in use gives its read error and offers
  Recover again only; `prune()` counts an abandoned record from when it was
  abandoned ([`troubleshooting.md`](troubleshooting.md#recover-reports-a-missing-or-damaged-backup),
  [ADR 0005 amendment](adr/0005-journaled-config-writes.md)).
- Done 2026-11-07 (owner's go-ahead of 2026-11-07), tested with temporary
  folders only: descriptor files that give a mod the same local id (on
  Windows, names that differ only in unpaired surrogates, which read as
  U+FFFD like a literal U+FFFD) become one `Broken` entry in the scan of
  `DlcLoadJsonAdapter`, with one problem that names every file and its mod
  name and asks to rename them, then rescan. No file stands for the entry and
  no other id is made up, because playsets and share codes refer to mods by
  id; a playset entry for it reads Broken and, while enabled, keeps Play
  unavailable, and `dlc_load.json` references to the files, also in an
  imported current configuration, resolve to it; the imported entry is named
  after the mod of the file the reference names, else the reference
  ([stellaris.md](games/stellaris.md#mods),
  [troubleshooting](troubleshooting.md#several-descriptor-files-give-a-mod-the-same-id)).
  The save did not actually fail: `GameService::normalizeInventory` kept
  whichever entry sorted first and dropped the others with only a log line.
  It stays as a safety net, as does `InventoryRepository::replaceForGame`,
  which already refused duplicate identities with `InvalidArgument` ("The
  same mod was found twice in one scan.") instead of a constraint error.
- Done 2026-11-07: `tools/steam-helper/main.cpp` reads its arguments with
  `providers::internal::displayPath` instead of `path::u8string()`, so an
  argument with unpaired surrogates gets an `invalid_arguments` ready event
  instead of ending the helper (end-to-end test), and
  `validateHelperArguments` shortens the app id in its error detail without
  cutting a U+FFFD in half. No other throwing conversion of a path
  (`u8string()`, `string()`, `generic_string()`, `u8path`, stream output) is
  left in `src/` or `tools/`; the tests use them on fixed names only.
- Done 2026-11-07: a mod list edit of several rows is all or nothing. When an
  undo or redo still in flight has taken one of its rows away, nothing changes
  and the warning says so; it used to apply to the rest without telling the
  user (owner's choice of the two options).
- On Clang, `PMM_TRY` names its variable by line number; two on one line
  would collide (no such use exists).
- Optional: bring the running window to the front on a second start
  (`QLocalServer`); one read-only verification run during the review fixes
  ended with a crash at shutdown that did not recur; if it does, enable WER
  local dumps for `pmm-app.exe`.

Accessibility ([`accessibility.md`](accessibility.md), "Known gaps"):

- Screen-reader listening pass with NVDA and Narrator (backlog item 6a, the
  owner's). It should also settle how Play's "unavailable" and "starting"
  names, its notification and the separators' widths (name change plus
  announcement; one of the two may have to go) sound.
- Done 2026-11-12 on `agent/accessibility-6` (owner's go-ahead of 2026-11-12,
  backlog item 6b-e; not merged): the minimum window size is 900x440 and fits
  a 1920x1080 screen at 200 % (it was 980x600); the column dividers and the
  table's column edges resize from the keyboard (WAI-ARIA window splitter,
  two Tab stops at the columns' edges and one for the four column edges);
  Play stays focusable while it is blocked (aria-disabled: its name, its
  description and a notification give the reasons); `--screenshot` keeps an
  explicit `QT_SCALE_FACTOR` and `--window-size` sets the window's size; the
  preview tests run in ctest (label `ui-preview`), so the release's CI runs
  them.

Packaging ([`packaging.md`](packaging.md)):

- 0.1.0, 0.1.1 and 0.1.2 are published. On 2026-11-07 the owner's installed 0.1.0
  updated itself to 0.1.1 with **Update and restart**, and that copy then updated
  itself to 0.1.2 (reported 2026-11-12). The
  owner installed 0.1.0 with the wizard on 2026-11-06, and it worked; on that
  computer neither the browser nor SmartScreen warned, which may come from its
  settings, so the documented warnings stay.
- The package job's check that `pmm-app.exe` contains the repository id needs the
  id compiled in as text (a string define, as `PMM_GITHUB_REPOSITORY_ID` is passed);
  an id formatted from a number at run time would fail it.
- The repository id 1439071956 is confirmed: `releases/latest` by that id answered
  with `v0.1.0` and `v0.1.1` once they were published, and the owner's installed
  0.1.0 updated itself through it. The `Release` workflow's validation still compares
  it with `GITHUB_REPOSITORY_ID` before anything is built.
- Fixed 2026-11-07 (owner's go-ahead of 2026-11-07; accepted for 0.1.0 after the
  lockdown review until then): `check-update-contract.sh` matched text, so a value
  that survived only in a comment still passed, for example `/releases/latest`
  removed from the code but left in the comment in `app_update.hpp`. It now reads a
  copy of each source without its comments, made by a string-aware POSIX awk
  stripper per syntax: C++ (char literals, digit separators, raw strings), Inno
  Setup with its Pascal code and preprocessor (a line that starts with `//` is
  dropped in every section, as ISPP drops it, seen with ISCC 6.7.3; `;` lines
  outside `[Code]`),
  PowerShell (here-strings), the workflows' YAML and their shell, CMake (bracket
  comments and arguments); every line keeps its number. A stripper that fails
  stops the check (exit 2) instead of leaving an empty copy to check.
  Documentation is read whole: `docs/updates.md` and the comment at the top of
  `installer.iss`, whose list of the update arguments was a check that only ever
  matched a comment. New checks cover the other example of the review, the
  relaunch's `Check:` flipped: an entry in `[Run]` that starts `{app}\{#AppExe}`
  must have `Check: IsPmmUpdate`, and no entry with that `Check:` may have
  `skipifsilent` or `postinstall`. 56 source checks pass; the 112 cases of
  `test-update-contract.sh` pass with gawk, with `gawk --posix` and with mawk
  1.3.4 20240123, the version on the Ubuntu 24.04 runners (WSL Ubuntu here), and
  include, for each syntax, a value left only in a comment (fails) and comment
  markers in strings (passes). Not handled: a quote within `$(...)` in a
  PowerShell `"..."` string, a C++ `//` comment continued by a trailing
  backslash, and a quoted YAML value that goes on to the next line (quotes are
  followed within a line).
- Accepted for 0.1.0 after the lockdown review: an update run that the downgrade
  guard refuses does not start the application again; that needs an installed
  version newer than the running one.
  "Open release page" falls back to the compiled slug's list of releases while no
  reply in the session has named the repository (no release yet, or no answer).
- `jurplel/install-qt-action` is the one third-party action left in the jobs that
  build the shipped installer; it is pinned to the commit of v4.5.0. Since 2026-11-07
  the MSVC environment comes from the repository's own
  `.github/actions/msvc-dev-env` instead of `ilammy/msvc-dev-cmd`, which still
  targets Node.js 20 and has had no release since January 2024.
- No code signing; no `longPathAware` manifest (the package folder path must
  stay below about 170 characters); not tried on a machine without the VC++
  Redistributable.
- The Qt style prune list assumes the Basic style chosen in `main()`.
- The `release` preset uses CMake's `RelWithDebInfo` flags (`/Ob1`).

## Next steps (in order)

1. Done 2026-10-30: the repository is on GitHub
   (https://github.com/Giorgi-Apriashvili/pdx-mod-manager, public, MIT), `main` is
   protected (no force pushes or deletion; CI checks were required for merges
   until 2026-11-07, when CI moved to releases only),
   and the code base is clang-tidy clean (`.clang-tidy`, `tests/.clang-tidy`).
2. Done 2026-10-30: the required checks were "Build and test (Windows MSVC)",
   "Format check", "Sanitizers (Windows MSVC, ASan, Qt-free libraries)" and
   "Warnings (Windows clang-cl, Qt-free libraries)". Removed 2026-11-07: CI no
   longer runs on pushes or pull requests, so they could never pass.
3. Optional: warn when two enabled EU4 mods contain the same file.
4. Implement the import options item above, with tests. Done 2026-11-06: the
   helper exit wait and the launch refresh. Done 2026-11-07: the discard flow
   for unrecoverable transactions (deferred on 2026-11-06, then scheduled
   before the next release by the owner on 2026-11-07).
5. With the user's approval, the remaining live pieces (table above). Record
   results in [`validation.md`](validation.md) and
   [`support-matrix.md`](support-matrix.md).
6. Done 2026-11-06: [0.1.0](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/tag/v0.1.0) released from `f13e899` ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42197165030)), with
   the notes of [`../CHANGELOG.md`](../CHANGELOG.md). The updater is locked down for
   it (owner's decision, 2026-11-06: unsigned, a daily check that is on by default,
   no rehearsal of an update between two releases): the values every copy relies on
   are frozen in [`../packaging/update-contract.txt`](../packaging/update-contract.txt)
   and checked in CI and in the workflow. Done 2026-11-07:
   [0.1.1](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/tag/v0.1.1) released from `4046b83` ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42313402701)) with Play without a
   playset and Play requiring the Steam client. The owner's installed 0.1.0
   updated itself to it on 2026-11-07, the first real update. Done 2026-11-07:
   [0.1.2](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/tag/v0.1.2) released from `2dadc9b` by pushing its tag ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42359177117))
   with backlog items 1-5: discarding an interrupted change that Recover cannot
   undo, imports with downloads off, all-or-nothing edits, descriptor files that
   share a mod id, and the update contract check matching code only.
7. Done 2026-11-12 on `agent/accessibility-6`, for the owner's review (not
   merged): backlog item 6, parts b-e (smallest window, keyboard resizing,
   Play focusable while blocked, screenshot and test tooling; see Open items >
   Accessibility). Part a, the listening pass with NVDA and Narrator, stays
   with the owner.
8. Done 2026-11-13 on `agent/ck3`, merged into `main` (`5013efe`):
   Crusader Kings III support, phase 1 (code, tests, fixtures, docs, the
   probe tool), verified read-only. Done 2026-11-13 with the owner's
   approval, recorded on `agent/ck3-probe` for the owner's review: the probe
   run and two direct launches on 1.20.1.2 ([CK3 probe run](evidence/2026-11-13-ck3-load-order.md)), then the
   traits (the later mod wins), the support matrix and the CHANGELOG's
   load-order sentence. Next, each with the owner's approval: a Workshop
   download for app 1158310 and the other open CK3 items
   ([CK3 adapter](games/ck3.md#outstanding-validation)). The release number
   (probably 0.2.0) is decided later.

## Working rules

- Before every push run `bash scripts/check.sh`, and `--full` for larger changes
  and before a release: GitHub runs CI only for releases and by hand. Releases
  start by pushing a tag `vX.Y.Z` ([`releasing.md`](releasing.md)), only with the
  owner's OK.
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
to the launcher's. Crusader Kings III "1.20.1.2 (Crozier)" (build 26046173,
11 Workshop mods) was read on 2026-11-13: the same holds, except for 4
registrations older than their Workshop content ([CK3](games/ck3.md)). The
same day, started directly by the application, it stayed up without the
launcher or a restart through Steam, loaded exactly the enabled probes, and
the later mod in `enabled_mods` won a shared file; it listed only the
`enabled_mods` descriptors in `debug.log`, and one of the two runs crashed
while being closed ([CK3 probe run](evidence/2026-11-13-ck3-load-order.md)).
