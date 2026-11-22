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
| Workshop ID lists and Workshop names in the import preview (for 0.2.0) | `src/sharing/workshop_id_list`, `src/services/share_service`, `src/app/core/metadata_host`, `src/app/gui/share_controller`, `ui/dialogs`, [`share-code-protocol.md`](share-code-protocol.md#8-workshop-id-lists), [`app.md`](app.md#workshop-names-in-the-import-preview) | done 2026-11-13 on `workshop-id-lists`, merged into `main` (`abcb775`). Owner's decisions of 2026-11-13 with the lead's defaults D1-D10. The Web API lookup is tested against a local stub only; a real lookup needs a session with Steam (not read-only) and is left to the owner's first use | |
| SQLite persistence and migrations | `src/persistence`, [`persistence.md`](persistence.md) | done, tested | |
| Platform: files, paths, processes, Steam discovery, file transactions | `src/platform` | done, tested | |
| Workshop provider and `pmm-steam-helper` | `src/providers`, `tools/steam-helper`, [`steam-workshop.md`](steam-workshop.md) | done; session and item state verified read-only; subscribe and download tested against the fake Steam API only | [probe](evidence/steam-helper-probe.md) |
| EU4 and Stellaris adapters | `src/games`, [`games/`](games/), [`load-order.md`](load-order.md) | done; detection, inventory, current configuration and launch plan verified read-only | [real scan](evidence/real-scan-2026-10-29.md) |
| Crusader Kings III adapter (for 0.2.0) | `src/games/ck3.{hpp,cpp}`, [`games/ck3.md`](games/ck3.md), [`load-order.md`](load-order.md#crusader-kings-iii) | done 2026-11-13 on `agent/ck3`, merged into `main` (`5013efe`). Owner's decisions of 2026-11-12/13: the game's own Steam API library is loaded as for the other games, the catalog lists the supported games alphabetically (`ck3`, `eu4`, `stellaris`), the load order ships unverified (`loadOrderVerifiedInGame = false`, and a temporary note above a CK3 playset says so) until a probe run approved by the owner. Also for every game: launches start in the executable's folder (unchanged for Stellaris and EU4), and a Workshop folder without `descriptor.mod` is described by its only `.mod` file, also by the content check after a download. Detection, inventory, current configuration, launch plan and a share-code round trip verified read-only on 1.20.1.2. **Probe run done 2026-11-13** with the owner's approval (recorded on `agent/ck3-probe`, merged `ab265ea`): two direct launches without the launcher or a restart through Steam, and **the later mod in `enabled_mods` wins**, as in Stellaris; `loadOrderVerifiedInGame = true` and the temporary note removed. **Workshop acquisition not run**; `supported_version="1.20"` still undetermined | [CK3 adapter](games/ck3.md#real-machine-evidence), [CK3 probe run](evidence/2026-11-13-ck3-load-order.md) |
| Victoria 3 adapter (for 0.3.0) | `src/games/{launcher_adapter,content_load_adapter,mod_metadata,vic3}.{hpp,cpp}`, [`games/vic3.md`](games/vic3.md), [ADR 0010](adr/0010-content-load-adapter-family.md), [`load-order.md`](load-order.md#victoria-3) | done 2026-11-20 on `agent/vic3`, merged into `main` (`8225f35`). Owner's decisions of 2026-11-20 (the lead's recommendations L1-L11, O2/O3 skipped, O4 no release yet, O5 the brief's scope): a second adapter family (`ContentLoadJsonAdapter`, `content_load.json` and `.metadata/metadata.json`) on a base extracted from `DlcLoadJsonAdapter` (`LauncherGameAdapter`; a commit of its own, without a change in behaviour); the Workshop content check per game (`GameAdapter::verifyWorkshopContent`); the catalog lists `vic3` as supported (`ck3`, `eu4`, `stellaris`, `vic3`, then `hoi4`); the load order ships unverified with a temporary note. Detection, inventory, current configuration, launch plan, the download check and share-code round trips verified read-only on 1.15.2. **Probe run done 2026-11-21** with the owner's approval (O1; recorded on `agent/vic3-probe`): two direct launches without the launcher or a restart through Steam, and **the later mod in `enabledMods` wins** a whole file, as in Stellaris and CK3; `loadOrderVerifiedInGame = true` and the temporary note removed. The same run showed that 1.15.2 accepts `"1.*"` and `"1.15"` but counts a lone `"*"` as not matching (the application now warns about it: `loneWildcardMismatches`), and that a mod without `game_custom_data` is left out of the game's mod list but still loaded (it stays Ready; its problem text now says so). **Workshop acquisition not run** | [Victoria 3 adapter](games/vic3.md#real-machine-evidence), [Victoria 3 probe run](evidence/2026-11-21-vic3-load-order.md) |
| Hearts of Iron IV adapter (for 0.3.0) | `src/games/hoi4.{hpp,cpp}`, `src/core/catalog_testing.hpp`, [`games/hoi4.md`](games/hoi4.md), [`load-order.md`](load-order.md#hearts-of-iron-iv) | done 2026-11-21 on `agent/hoi4`, rebased onto `main` (`a294448`) and merged (`11b73aa`). Owner's decisions of 2026-11-20/21 (the lead's recommendations, pre-authorized): no real unsupported title is added to the catalog (O1): the paths for a known game without an adapter stay in the code and are tested through a test-only catalog (`pmm::testing::ScopedGameCatalog`, a commit of its own); merged with `loadOrderVerifiedInGame = false` and the temporary note "Hearts of Iron IV may not follow this order when two mods change the same file; this is not verified yet." (O2, T8); if the game ignores the list order, the playset order is still written, as for EU4 (O3); the probe run is a separate, later step (O4, O5); the preview and the screenshots lose the "Other games" section (O6). `Hoi4Adapter` on `DlcLoadJsonAdapter`; one new trait, `gameDataPathOverridesUserDirFile` (Hearts of Iron IV only: `gameDataPath`, also as an absolute folder, before `userdir.txt`, inferred from `hoi4.exe`); the catalog lists `ck3`, `eu4`, `hoi4`, `stellaris`, `vic3`, all supported. Detection, inventory, current configuration (the launcher's pretty-printed `dlc_load.json`: 0 writes), launch plan, the download check and a share-code round trip verified read-only on 1.20.0.0. Probe run done 2026-11-21 (four direct launches, recorded on `agent/hoi4-probe`): **the registration file names decide, the later one winning**, so the temporary note became a permanent one like EU4's and `loadOrderVerifiedInGame = true` (O3: the playset order is still written); the direct launch and the compact `dlc_load.json` verified. Workshop acquisition not run | [Hearts of Iron IV adapter](games/hoi4.md#real-machine-evidence), [Hearts of Iron IV probe run](evidence/2026-11-21-hoi4-load-order.md) |
| Qt-free services (games, playsets, sharing, acquisition, launch) | `src/services`, [`architecture-services.md`](architecture-services.md) | done, tested | |
| Desktop application (`pmm-app`): runtime, services worker, Steam helper and Web API hosts, Backend and models, profile lock, offline QML engine | `src/app`, [`app.md`](app.md), [`ui-contract.md`](ui-contract.md) | done, tested; share-code round trip verified read-only for both games | [round trip](evidence/app-round-trip-2026-10-30.md) |
| Launch: wait for the game's Steam helper to exit before the game starts; refresh the downloads and mod rows after every launch outcome | `src/services/acquisition_coordinator`, `src/app/core/session_host`, `src/app/gui/launch_controller`, [`architecture-services.md`](architecture-services.md#data-flow-launch), [`app.md`](app.md#steam-helper-host) | done 2026-11-06, tested with the real `pmm-steam-helper` and the fake Steam API library; not run against a real game | |
| Play without a playset (for 0.1.1) | `src/services/launch_service`, `src/app/core/view_data`, `src/app/gui/launch_controller`, `ui/panes/PlaySection.qml`, [`architecture-services.md`](architecture-services.md#launchservice-launch_servicehpp), [`ui-contract.md`](ui-contract.md#launchcontroller-qobject) | done 2026-11-06 on `agent/vanilla-launch`. Owner's decision of 2026-11-06: spec section 7 says "validate the installation, active playset, ..."; no playset now means a launch without mods instead of a blocker. Every other check still runs; the empty mod list is written as one journaled transaction labelled `launch <game> without mods`, which Restore and Recover handle like any launch. Tested with fakes only; **not run against a real game** (a live run needs the owner's approval) | |
| Play requires the Steam client for Steam installations (for 0.1.1) | `src/services/launch_service`, `src/platform/steam`, `src/app/gui/backend`, `src/app/core/view_data`, [`architecture-services.md`](architecture-services.md#launchservice-launch_servicehpp), [`app.md`](app.md), [`ui-contract.md`](ui-contract.md#launchcontroller-qobject) | done 2026-11-06 on `agent/require-steam`. Owner's decision of 2026-11-06: games could be started directly with Steam closed; Play now requires the Steam client for Steam installations (`DistributionSource::Steam`: found through Steam's app manifest, or located by hand inside a Steam library). A copy from another store located by hand is not blocked. The check uses the process list the running-instance check already reads (`steam.exe`, or the pid in `HKCU\Software\Valve\Steam\ActiveProcess` without a readable name); an unreadable list is a warning. While Steam, the game or the Paradox launcher blocks, the Backend re-checks every 3 s without the write probes, so Play follows on its own. Tested with fakes only; **not run against a real game or Steam** (a live run needs the owner's approval). Limitations: whether Steam runs is detected, whether it is signed in is not; the process list covers every Windows session, so another signed-in user's Steam counts as running | |
| Discard an interrupted change that Recover cannot undo (for 0.1.2) | `src/platform/file_transaction`, `src/services/launch_service`, `src/app/gui/launch_controller`, `ui/panes/PlaySection.qml`, `ui/dialogs/ConfirmDialog.qml`, [`troubleshooting.md`](troubleshooting.md#recover-reports-a-missing-or-damaged-backup), [ADR 0005](adr/0005-journaled-config-writes.md) | done 2026-11-07 on `agent/discard-interrupted`. Recovery skips the backups of targets already original (or absent and absent before); `abandon()` gives up an interrupted transaction (`abandoned`, no file changed, folder and backups kept, pruned like any finished one but counted from when it was abandoned); after a Recover that failed with `IntegrityCheckFailed` the banner offers "Discard this record", and Play is available afterwards when nothing else blocks. Tested with fakes only; **not run against a real game** | |
| Updates of installed Workshop mods (for 0.3.1) | `src/providers/{acquisition,web_metadata,helper_protocol}`, `tools/steam-helper`, `src/services/acquisition_coordinator`, `src/persistence` (schema 3), `src/app/core/{service_runner,view_data}`, `src/app/gui/{acquisition_controller,mod_list_model,mod_filter_model,jobs_model,mod_details}`, `ui/panes/{DownloadsSection,JobDelegate,ModRow,ModsPane}.qml`, [`steam-workshop.md`](steam-workshop.md), [`app.md`](app.md#checking-installed-workshop-mods-for-updates), [`ui-contract.md`](ui-contract.md) | done 2026-11-22 on `agent/workshop-updates`, not merged (item 17). The owner's request and decisions of 2026-11-21: **Check for mod updates** asks the Steam Web API (ids only, on request) and Steam's own `appworkshop_<app>.acf` counts without any network; **Update mods** and **Update from Workshop** in the Downloads area and the right-click menu; Play warns about updates and waits only while one runs; **Re-download from scratch…** unsubscribes, subscribes again and downloads after a confirmation. Tested with fakes, the fake Steam library and the local Web API stub only; **live verification pending the owner's approval** | [steam-workshop.md, section 11, item 4](steam-workshop.md#11-not-verified-yet-and-how-to-verify-it) |
| QML user interface | `ui/`, [`ui/README.md`](../ui/README.md) | done; data shown as plain text (checked by a source test) | screenshots in `docs/screenshots/` |
| Accessibility and high-DPI | `ui/`, [`accessibility.md`](accessibility.md) | done except the gaps listed there; 2026-11-12 on `agent/accessibility-6`, merged into `main` (`8e4c7da`) for 0.2.0: the smallest window fits 1920x1080 at 200 %, keyboard resizing of the columns, Play focusable while blocked, preview tests in ctest | UI Automation inspection, contrast check, scale factors 1.5 and 2, captures of the smallest window at 100 % and 200 % |
| Windows package (ZIP) | `cmake/Packaging.cmake`, `packaging/`, [`packaging.md`](packaging.md) | verified from an extracted copy without Qt or Visual Studio | [packaging](evidence/packaging-2026-10-29.md) |
| Windows installer (Inno Setup, per-user) | `packaging/windows/`, [`packaging.md`](packaging.md#windows-installer) | verified with silent runs in a scratch folder, including failed upgrades; the owner installed the published 0.1.0 with the wizard on 2026-11-06, and it worked. Added 2026-11-06: no downgrade over a newer installed version, one Start Menu shortcut instead of a folder, no desktop shortcut brought back by an update run, the previous version started again after a failed update run, the uninstall message about the data folders; verified by the extended smoke test on GitHub on 2026-11-06 ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42164236292)) | [installer](evidence/installer-2026-10-30.md) |
| Update check, daily check and update button | `src/services/app_update`, `src/app/core/update_client`, `src/app/gui/update_controller`, [`updates.md`](updates.md) | done, tested against a local stub; **a real update verified** on 2026-11-07: the owner's installed 0.1.0 updated itself to [0.1.1](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/tag/v0.1.1). 2026-11-06 (the updater lockdown): the request by repository id, owner and repository taken from the checked reply, redirects only for downloads and only to `github.com` and `*.githubusercontent.com`, the daily check with its banner and setting, no in-app install in `--profile` and `--no-steam` sessions, retries of the final save, and "Open release page" built from the repository the reply named (the compiled slug's list of releases only while no reply has named one). Merged and verified together on `agent/updater-lockdown` | [installer](evidence/installer-2026-10-30.md#after-merging-the-update-check) |
| Update contract, frozen since 0.1.0 | [`packaging/update-contract.txt`](../packaging/update-contract.txt), `scripts/check-update-contract.sh`, `scripts/test-update-contract.sh`, [`updates.md`](updates.md#compatibility-contract-frozen-since-010) | done 2026-11-06: CI's "Format check" checks the sources and tests the check (80 cases); the Release workflow checks the version, the published releases, the repository id in `pmm-app.exe` and the assets before the draft. Passes on the merged tree, and a deliberate drift fails it; passed on GitHub in the dry run of 2026-11-06 ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42164236292)). Since 2026-11-07 the check reads the code of the sources, without their comments, and also checks the relaunch after an update run: 56 source checks, 112 test cases | |
| `Release` workflow | `.github/workflows/release.yml`, [`releasing.md`](releasing.md) | published 0.1.0 on 2026-11-06 ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42197165030)); dry runs passed on 2026-10-30 and 2026-11-06. The contract checks and the extended smoke test passed in the dry run of 2026-11-06 ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42164236292)): 57 of 57 installer checks, including the update run against a held mutex | [installer](evidence/installer-2026-10-30.md#release-workflow-what-could-be-checked) |
| README, ADRs, support matrix, validation procedure, troubleshooting, changelog | repository root, `docs/` | up to date with this handoff | |
| In-game probe mods | `tools/probe-mods/probe_mods.py` | done; used in the live validation of 2026-10-30 (Stellaris, EU4). `--game ck3` (CK3 event syntax, 2026-11-13) checked by creating the probes into a scratch folder, then used for the CK3 probe run of 2026-11-13. `--game vic3` (2026-11-20: probe folders with `metadata.json`, `--no-shared`, `--no-game-custom-data`, `analyze` of the rotating debug logs) checked in scratch folders, then used for the Victoria 3 probe run of 2026-11-21. `--game hoi4` (2026-11-21: `add_namespace`, no BOM, LF; `rename` for the runs 3 and 4 of its procedure; `analyze --game hoi4` reads `system.log`) checked in scratch folders, then used for the Hearts of Iron IV probe run of 2026-11-21 (all four runs; its `precedence` label names the winner's place in the list, which is not Hearts of Iron IV's rule) | [CK3 probe run](evidence/2026-11-13-ck3-load-order.md), [Victoria 3 probe run](evidence/2026-11-21-vic3-load-order.md), [Hearts of Iron IV probe run](evidence/2026-11-21-hoi4-load-order.md) |

## Test status (2026-11-22, updates of installed Workshop mods, `agent/workshop-updates`)

Local runs only, with fakes, the fake Steam library, the local Web API stub
and new scratch folders; no game, no launcher and no Steam client was
started, `pmm-steam-helper` never ran against the real Steam client, nothing
was subscribed, unsubscribed or downloaded, the real Web API was not asked,
and nothing under Steam, the game folders or `Documents/Paradox Interactive`
was written. Item 17 below.

- `debug` preset: **699/699 tests pass** (656 on `main`), by label:
  pmm_core_tests 64, pmm_persistence_tests 75, pmm_providers_tests 75,
  pmm_services_tests 117, pmm_app_tests 133, the others as before
  (pmm_sharing_tests 70, pmm_platform_tests 59, pmm_games_tests 110), and
  ui-preview 1 (44 test functions, 38 on `main`).
- The window's part (`b039536` to `6763714`): 2 models tests (the row
  role, the counts, the "updates" filter and status sort, the details'
  offers; the job kind, error code and re-download offer), 3
  `[qml][updates]` cases (Check for mod updates against the local stub,
  with its result line, announcement, the rows, counts, empty-state text,
  Play's warning, the context menu and the confirmation, and a repeat that
  sends nothing; a check that cannot reach the Workshop, with Retry and its
  focus; a session without Steam, where everything is disabled and nothing
  is sent while Steam's own record still shows "Update available"), a
  source test that keeps "Check for updates" (Settings) apart from "Check
  for mod updates" and the update actions, and 6 preview tests
  (`test_c1` to `test_c6`).
- Without the change: against `main`'s production QML the 3
  `[qml][updates]` cases and the label test fail, and so do the 6 preview
  tests (the other 38 pass); against `main`'s preview
  fakes the 6 preview tests fail; with `main`'s `mod_filter_model.cpp` and
  `jobs_model.cpp` both models tests fail.
- Screenshots: the preview's captures differ from `main`'s only by the two
  rows with "Update available", the failed update job ("2 failed") and
  the job list's scroll bar, so the twelve captures that show them were
  replaced (`game-not-found.png` kept); a second run differed from the
  first only by scattered pixels and a hover state. The real
  application's captures (`scripts/app-screenshots.py`) differ only by the
  disabled **Check for mod updates** of a read-only session and were
  replaced (`app-import-preview.png` and `app-settings-updates.png` kept);
  a second run matched them except for a text cursor.
- `scripts/check.sh --full` on `ccb6ea0`: passed in 12 min 49 s (ASan and
  clang-cl with UBSan 565/565 each, the update contract's 112 checks,
  clang-tidy on the 43 C++ files changed since `origin/main` without
  findings, the `ci` preset from a fresh checkout 699/699). The commit
  after it changes this file only.

## Test status (2026-11-21, dependencies not required, `agent/dependencies-optional`)

Local runs only, with fakes, fixtures and new scratch folders; no game, no
launcher and no Steam client was started, nothing under Steam, the game
folders or `Documents/Paradox Interactive` was written, and the launcher's
databases and `playsets_backup` were not opened. Item 15 below.

- `debug` preset: **655/655 tests pass** (652 on `main`, as recorded below), by
  label: pmm_core_tests 60, pmm_services_tests 102, the others as before
  (pmm_sharing_tests 70, pmm_persistence_tests 72, pmm_platform_tests 59,
  pmm_providers_tests 61, pmm_games_tests 110, pmm_app_tests 125,
  ui-preview 1 with the new `test_9g_dependenciesPerGame`).
- Without the change: with the catalog's `ck3`, `eu4` and `stellaris` set
  back to `Required`, 12 tests fail (the catalog test, 6 core dependency and
  readiness tests, 4 services tests including the launch check, the app
  details test); with the launch check's and the details' conditions of
  `main` (`== Required`, `== LoadAfter`) the launch-check and app details
  tests fail; against `main`'s `DetailsSection.qml`
  `test_9g_dependenciesPerGame` fails (the heading of a mod that names
  none), and against `main`'s preview fakes `test_4b`, `test_9f` and
  `test_9g` fail. The tests of every kind of issue (now Victoria 3 and the
  test-only game without an adapter) and of the resolver pass either way.
- The first code commit alone (`98c4898`, with `main`'s `ui/`): 655/655.
- Preview screenshots: a capture of `main`'s `ui/` matches the committed
  PNGs except for scattered single pixels (and, in some runs, the grey
  selection of an inactive window); seven captures that show the selected
  mod's details were replaced, differing from the committed ones only there
  and by such pixels, and the six others kept. The real application's captures were
  not retaken: their selected Stellaris and EU4 mods list no dependencies,
  so they show "Dependencies" and "None declared." as before (captures
  replaced on 2026-11-21, item 16).
- `scripts/check.sh --full` on `18197c4`: passed in 8 min 51 s (ASan and
  clang-cl with UBSan 529/529 each, the update contract's 112 checks,
  clang-tidy on the 12 C++ files changed since `origin/main` without
  findings, the `ci` preset from a fresh checkout 655/655). The commit after
  it changes this file only.
- Read-only against the real installations (`pmm_games_tests "[.real]"`,
  counts only, [evidence](evidence/2026-11-21-descriptor-dependencies.md)):
  the current configurations give 0 dependency warnings in every game with
  `main`'s rules and with the new ones (no enabled mod of Stellaris, EU4,
  CK3 or Victoria 3 lists dependencies). A playset of every installed mod,
  enabled, in identity order (new line of the test, so `main`'s rules there
  are the branch's test with the catalog's `ck3`, `eu4` and `stellaris` set
  back to `Required`): Stellaris 3 warnings in 2 mods with `main`'s rules (2
  not installed, 1 ordered after), 1 in 1 mod after (ordered after);
  Victoria 3 1 (not installed) either way; EU4, CK3 and Hearts of Iron IV 0.
- Review fixes (`b48a6f7` to `53f8213`: the app details test now has a
  disabled mod naming an enabled mod after it, which stays a neutral
  "load_after" row; a core test pins that an enabled mod of the name earlier
  in the list satisfies it in Stellaris and CK3; the preview's fake order
  hints are placed for each mod's place in its playset, never past its end;
  made-up names in two older test files; the changelog, CK3, Stellaris,
  accessibility and evidence wording): `debug` **656/656** (pmm_core_tests
  61, the others as above; ui-preview with the new
  `test_9h_fakeOrderHintsFitThePlayset`). The app details test fails with a
  details rule that marks a disabled mod's later names as "order" (the
  reviewer's mutation), and `test_9h` fails against the earlier fakes (a
  hint naming #2016 in the 2,000-mod playset). A capture of the preview
  matches the committed PNGs except for scattered pixels and the run-to-run
  difference of `a11y-shortcuts-dark.png`, so none was replaced.
  `scripts/check.sh --full` on `53f8213`: passed in 8 min 39 s (ASan and
  clang-cl with UBSan 530/530 each, the update contract's 112 checks,
  clang-tidy on the 13 C++ files changed since `origin/main` without
  findings, the `ci` preset from a fresh checkout 656/656). The commit after
  it changes this file only. Read-only again (counts only): the same counts
  as above, with the branch's rules and with `main`'s.

## Test status (2026-11-21, Hearts of Iron IV dependencies, `agent/hoi4-dependencies`)

Local runs only, with fakes, fixtures and new scratch profiles; no game, no
launcher and no Steam client was started, nothing under Steam, the game
folders or `Documents/Paradox Interactive` was written, and the launcher's
databases and `playsets_backup` were not opened.

- Before any change, the `debug` build of `main` (`810843d`) passed 642/642
  tests.
- `debug` preset: **650/650 tests pass**, by label: pmm_core_tests 57,
  pmm_sharing_tests 70, pmm_persistence_tests 72, pmm_platform_tests 59,
  pmm_providers_tests 61, pmm_games_tests 110, pmm_services_tests 100,
  pmm_app_tests 125, ui-preview 1 (with the new `test_9f_loadAfterDependencies`).
- Without the change: with `hoi4` set back to `Required` in the catalog, the
  new catalog, core, readiness, services and app tests fail (5), and
  `test_9f_loadAfterDependencies` fails against `main`'s `DetailsSection.qml`.
  The resolver tests and the test of the CK3, EU4, Stellaris and Victoria 3
  messages pass either way (the latter also on `main`'s code).
- The first commit alone (`5cf2ab7`, the shared resolver without a change in
  behaviour), exported and built with `core-debug`: 520/520.
- `scripts/check.sh --full` on `229ce20`: passed in 8 min 46 s (ASan and
  clang-cl with UBSan 524/524 each, the update contract's 112 test cases,
  clang-tidy on the 11 C++ files changed since `origin/main` without
  findings, the `ci` preset with warnings as errors from a fresh checkout
  650/650). The commit after it changes this file only.
- Read-only against the real installations (`pmm_games_tests "[.real]"`,
  counts only): Hearts of Iron IV 20 dependency warnings in 4 of 21 enabled
  mods with `main`'s rules, none after; Stellaris, EU4, CK3 and Victoria 3
  unchanged (no enabled mod lists dependencies). The application's captures
  of the owner's Hearts of Iron IV mods stayed in the scratch folder.
- Review fixes (`66c918a` to `bd7655e`: each mod once in the "Loads after"
  list, "Enabled in this playset" without "the game loads it first", the
  launch check's line without "(versions or dependencies)" where they only
  order the load, no fake "Requires" warnings in the preview's Hearts of Iron
  IV details, no name index built for a playset without a game that requires
  dependencies): `debug` **652/652** (pmm_core_tests 58, pmm_services_tests
  101, the others as above). Without the fixes the 5 tests that cover them
  fail (two core resolver tests, the launch-check test, the app details test,
  `test_9f_loadAfterDependencies`). `scripts/check.sh --full` on `bd7655e`:
  passed in 8 min 24 s (ASan and clang-cl with UBSan 526/526 each, the update
  contract's 112 checks, clang-tidy on the 13 C++ files changed since
  `origin/main` without findings, the `ci` preset from a fresh checkout
  652/652). Read-only again (counts only): Hearts of Iron IV 24 names in 6
  enabled mods, 0 dependency warnings, none spelled twice; on the whole
  inventories, Stellaris 3 of 39 installed mods list 6 names (4 distinct, 2
  of them not installed) and Victoria 3 1 of 10 lists 1 (not installed),
  none of those mods enabled; EU4 and CK3 none.

## Test status (2026-11-21, Hearts of Iron IV probe run, `agent/hoi4-probe`)

The live run itself is recorded in the
[Hearts of Iron IV probe run](evidence/2026-11-21-hoi4-load-order.md): four
direct launches of 1.20.0.0 by the debug build of `main` (`11b73aa`) with a
scratch profile, `--live-test` and `--no-steam` (run 2 moved a probe in the
application, run 3 renamed a probe, run 4 renamed its registration file);
the configuration restored three times and the probes removed afterwards.
Result: hypothesis H3, the mod whose registration file name sorts later wins
a shared file; neither the order of `enabled_mods` nor `name=` decides. The
branch then records it: Hearts of Iron IV's traits (`loadOrderSummary` with
the measured rule, `loadOrderEvidence`, `loadOrderVerifiedInGame = true`,
and a permanent `loadOrderUserNote` like EU4's in place of the temporary
one; `rejectsEarlyWildcards` stays, now confirmed on 1.20.0.0, with `"1.20"`
not reported), their checks in `hoi4_adapter_tests.cpp` and
`registry_tests.cpp` (every game's load order is now verified in the game,
and only EU4 and Hearts of Iron IV have a note), the Hearts of Iron IV row
of the preview's `FakeGamesModel.qml`, the wording of the Workshop ID list's
`;N` message ("Paradox Mod Manager writes the mods in list order, the order
most Paradox games load them in", in place of "Paradox games load mods in
list order", which EU4 and Hearts of Iron IV contradict; the parser is
unchanged) with its tests, the probe tool's notes, and the docs. No game was
started, and no game file was read or written for these checks (`[.real]`
was not run).

- `debug` preset: **0 warnings** (a full build of the worktree), **642/642
  tests pass**, by label: pmm_core_tests 51, pmm_sharing_tests 70,
  pmm_persistence_tests 72, pmm_platform_tests 59, pmm_providers_tests 61,
  pmm_games_tests 110, pmm_services_tests 99, pmm_app_tests 124, ui-preview
  1 (no new test cases; changed checks only).
- `scripts/check.sh --full` on `f21cdcf`: passed in 7 minutes (ASan and
  clang-cl with UBSan 517/517 each, the update contract's 112 test cases,
  clang-tidy on the 6 C++ files changed since `origin/main` without
  findings, the `ci` preset from a fresh checkout 642/642). The commit after
  it changes docs only (the close times of the runs, a rewording).

## Test status (2026-11-21, Hearts of Iron IV, `agent/hoi4`)

Local runs only, with fakes, fixtures and new scratch profiles; no game, no
launcher and no Steam client was started, nothing under Steam, the game
folders or `Documents/Paradox Interactive` was written, and the launcher's
databases and `playsets_backup` were not opened. A Victoria 3 probe run was
under way on the machine at the same time, so the Victoria 3 `[.real]` case
was not run and served as no gate. MSVC 19.52 (toolset 14.52.37025).

- Before any change, the `debug` build of `main` (`8225f35`) passed 623/623
  tests, and `pmm_games_tests "[.real]"` gave the Stellaris, EU4 and CK3
  baseline.
- Step 1, the test-only catalog (no change in behaviour): 627/627.
  `core/catalog_testing.hpp` swaps the catalog for a test
  (`ScopedGameCatalog`, `catalogWithUnsupportedGame()` adds
  `pmm-test-unsupported`); every test that used `hoi4` as the unsupported
  example uses it, and new cases cover the catalog's `adapterAvailable` on
  its own (an adapter injected under the synthetic key), the share-code form
  of "cannot manage" (refused at preview and import, nothing saved), the
  Backend (the row cannot be selected) and the preview's "Other games"
  section (`test_9e_otherGamesSection`).
- After the review, the branch was rebased onto `main` (`a294448`, the
  Victoria 3 probe run). The conflicts were resolved by keeping `main`'s
  Victoria 3 results (verified load order, no note, `loneWildcardMismatches`)
  and adding the Hearts of Iron IV parts; two comments that still named
  Victoria 3 as unverified were corrected, and the registry test checks that
  `loneWildcardMismatches` is off for Hearts of Iron IV (a lone `"*"` is
  untested there). Review fixes on top: an absolute `gameDataPath` with a
  drive letter is taken for Hearts of Iron IV (`resolveAbsoluteGameDataPath`;
  the other games still take only the `%USER_DOCUMENTS%` form), step 7 of the
  probe procedure restores once per launch that wrote (run 3 normally writes
  nothing) and passes `--no-steam`, the registry's check that a known game
  without an adapter has none runs on the test-only catalog, and wording and
  layout in the docs and comments are corrected. Before the rebase, `[.real]`
  on `main` (`a294448`) gave a new Stellaris, EU4 and CK3 baseline, identical
  to the one of `8225f35`.
- `debug` preset: **0 warnings**, **642/642 tests pass**, by label: core 51,
  sharing 70, persistence 72, platform 59, providers 61, games 110, services
  99, application 124, ui-preview 1. New: `hoi4_adapter_tests.cpp` (12 cases
  on the fixture `tests/fixtures/games/hoi4`, ids 6000000001 up: discovery,
  the games told apart both ways, the fallback executable, `gameDataPath`
  before `userdir.txt`, also absolute, and Stellaris's order and form
  unchanged, the identity rules, no write for the launcher's pretty-printed
  `dlc_load.json`, the compact write and the missing registration, the
  launcher's registrations regenerated byte for byte, the warnings, the
  download check, process names and load-order information), the absolute
  `gameDataPath` forms, a Hearts of Iron IV share-code round trip, the golden
  fixture `07-hoi4-basic` (checked with `scripts/share-code.py` and with
  Python's `zlib`, `base64` and `hashlib`), catalog and registry checks.
- The catalog tests of `pmm_core_tests`, `[hoi4]`, `[registry]` and
  `[content-check]` of `pmm_games_tests`, `pmm_sharing_tests`, the
  diagnostics, games and share tests of `pmm_services_tests`, the two
  changed application cases and the preview tests passed 10 times each
  before the review; after it, the `[hoi4]`, `[launcher_settings]`,
  `[registry]` and `[version]` cases and the catalog tests passed 10 times
  each.
- `pmm_games_tests "[.real]"` (read-only), last on the rebased branch with
  the review fixes: Hearts of Iron IV 1.20.0.0, 23 Workshop mods, 0
  problems, 23 with warnings (19 version mismatches, 4 `"1.*.*"` reported as
  invalid), 21 enabled, 0 unresolved, **0 writes** for the launcher's
  selection, the user data from `gameDataPath`, working directory the
  installation folder, arguments `[-gdpr-compliant]`,
  `SteamAppId`/`SteamGameId` 394360, 21 of 23 regenerated descriptors
  identical (2 registrations older than their content, differing in
  `supported_version`, one also in `version`; one item was updated by Steam
  after the inspection of 2026-11-20), the download check accepts 23 of 23
  (the same summary as before the review); the Stellaris, EU4 and CK3 output
  identical, line for line, to the baseline of `main` (Catch2's seed line
  aside; with `-s`, also apart from the line numbers of
  `real_machine_tests.cpp`).
- The application read-only with new scratch profiles (before the review):
  `--game hoi4 --screenshot` (Hearts of Iron IV third in the games column,
  Installed · 1.20.0.0, no "Other games" section, Play blocked by the
  read-only mode); with `--self-test config` the note above the 21 entries;
  the scripted share-code round trip from the game's configuration (21
  entries, 20 enabled, 21 installed, 0 missing) with byte-identical dumps.
  No file under the Hearts of Iron IV user data or installation changed.
- `tools/probe-mods/probe_mods.py` into scratch folders (before the review):
  `create` (default, `--game stellaris`, `eu4`, `ck3`, `vic3` and their
  options), `remove` and `analyze` print and write exactly what the version
  of `main` did; `--game hoi4` writes `add_namespace` events without a BOM
  and with LF line ends, `rename --display` and `--file` and their reversal
  restore every file byte for byte, `remove` after a rename leaves nothing
  behind and keeps unrelated `.mod` files, and `analyze --game hoi4` reads
  synthetic logs in the observed formats.
- `scripts/check.sh --full` on `50cd716`, the rebased branch with the review
  fixes: passed (ASan 517/517 and clang-cl with UBSan 517/517, the update
  contract's 112 test cases, clang-tidy on the 18 C++ files changed since
  `origin/main` without findings, the `ci` preset from a fresh checkout
  642/642).

## Test status (2026-11-21, Victoria 3 probe run, `agent/vic3-probe`)

The live run itself is recorded in the
[Victoria 3 probe run](evidence/2026-11-21-vic3-load-order.md): two direct
launches of 1.15.2 by the debug build of `main` (`8225f35`) with a scratch
profile, `--live-test` and `--no-steam`, approved by the owner (O1); the
configuration restored and the probes removed afterwards. The branch then
records it: Victoria 3's traits (`loadOrderEvidence`,
`loadOrderVerifiedInGame = true`, no `loadOrderUserNote`, and the new
`loneWildcardMismatches = true` with `SupportedVersionCheck::loneWildcard`),
the corrected `game_custom_data` problem, their checks in
`vic3_adapter_tests.cpp`, `registry_tests.cpp` and
`supported_version_tests.cpp`, the Victoria 3 row of the preview's
`FakeGamesModel.qml`, and the docs. No game was started, and no game file was
read or written for these checks (`[.real]` was not run).

- `debug` preset: **0 warnings** (a full build of the worktree), **624/624
  tests pass**, by label: core 51, sharing 69, persistence 72, platform 59,
  providers 61, games 97, services 96, application 123, ui-preview 1. New:
  a lone `"*"` (also `" * "` and `"v*"`) is flagged by
  `checkSupportedVersion` and warned about for Victoria 3, not when the
  installed version is unknown, and not for `"1.15"`, `"1.*"` or `"1.*.*"`.
  The `[vic3]`, `[registry]` and `[version]` cases passed 10 times each.
- `scripts/check.sh --full` on `95d2efe`: passed in 8 minutes (ASan and
  clang-cl with UBSan 500/500 each, the update contract's 112 test cases,
  clang-tidy on the 7 C++ files changed since `origin/main` without
  findings, the `ci` preset from a fresh checkout 624/624).

## Test status (2026-11-20, Victoria 3, `agent/vic3`)

Local runs only, with fakes, fixtures and new scratch profiles; no game, no
launcher and no Steam client was started, nothing under Steam, the game
folders or `Documents/Paradox Interactive` was written, and the launcher's
databases and `playsets_backup` were not opened. MSVC 19.52 (toolset
14.52.37025).

- Step 1, the shared base without a change in behaviour (`1e646e3`): before
  any change, the `debug` build of `main` (`cd1b2fc`) passed 596/596 tests
  and `pmm_games_tests "[.real]"` gave the baseline; after the extraction,
  596/596 again and the Stellaris, EU4 and CK3 output identical to it, line
  for line (Catch2's seed line aside).
- `debug` preset: **0 warnings**, **621/621 tests pass**, by label: core 51,
  sharing 69, persistence 72, platform 59, providers 61, games 95, services
  95, application 123, ui-preview 1. New: `mod_metadata_tests.cpp` (6
  cases), `vic3_adapter_tests.cpp` (15 cases on the fixture
  `tests/fixtures/games/vic3`: discovery, both wrong-game directions, the
  fallback executable, `game/userdir.txt`, the identity rules, Downloading
  and NeedsUpdate, folder names that read the same, the current
  configuration, exact `content_load.json` plans byte for byte in the
  launcher's layout, every other key kept, path strings reused, refusals,
  the warnings with `supported_game_version`, process names with the console
  wrapper, load-order information), `content_check_tests.cpp` (the content
  check of both families; the providers cases moved here, the services case
  that compared two copies of the rule is gone), a Victoria 3 share-code
  round trip, the golden fixture `06-vic3-basic` (checked with Python's
  `zlib`, `base64` and `hashlib`), catalog and registry checks. Changed: the
  unsupported example of the share-code tests is now `hoi4`.
- `pmm_games_tests`, `pmm_sharing_tests`, `pmm_providers_tests`, the
  catalog tests of `pmm_core_tests`, the acquisition tests of
  `pmm_services_tests`, the helper tests of `pmm_app_tests` (they change the
  content check hook) and the preview tests passed 10 times each.
- `pmm_games_tests "[.real]"` (read-only): Victoria 3 1.15.2, 10 mods (9
  Workshop, 1 local), 0 problems, 1 version warning (`1.7.*`), 1 enabled mod,
  0 unresolved, 0 writes, the download check accepts 9 of 9 Workshop folders
  (the previous rule in providers would have rejected all 9: none has a
  `.mod` file); the Stellaris, EU4 and CK3 output identical, line for line,
  to the baseline of `main`.
- The application read-only with new scratch profiles: `--game vic3
  --screenshot` (Victoria 3 fourth in the games column, Installed · 1.15.2,
  Hearts of Iron IV alone under "Other games", Play blocked by the read-only
  mode); the scripted share-code round trip from the game's configuration (1
  entry, disabled) and from a Workshop ID list of the 9 installed items (9
  entries, 8 enabled, 2 moved), each with byte-identical dumps; the
  load-order note above the 9-entry playset.
- `tools/probe-mods/probe_mods.py` into scratch folders: `create` (default,
  `--game stellaris`, `--game eu4`, `--game ck3`), `remove` and `analyze`
  print and write exactly what the version of `main` does; `--game vic3`
  writes probe folders with `metadata.json` (and the `--no-shared` and
  `--no-game-custom-data` variants), and `analyze --game vic3` on synthetic
  rotating debug logs picks the latest session.
- Review fixes (`99c286d` to `a10c917`): a `content_load.json` path outside
  `mod/` whose name is an installed mod's id, and which is not that folder
  (`fs::equivalent`), is left out and logged instead of becoming that mod,
  as is a path that names no folder (a games case with a folder of the
  same name elsewhere, a missing one, `C:\`, an empty path and the installed
  folder in the `\\?\` form); the download check reads `metadata.json` as
  the scan does, so whitespace, a trailing comma and a JSON array are
  rejected (games cases, and two more layouts in the comparison with the
  scan); a services case shows that the check takes the adapter of the
  job's game (Victoria 3's adapter listed first; with the coordinator
  changed to take the first adapter, the case fails); the `[.real]` summary
  counts only local `.zip` mods; the Victoria 3 probe scripts have LF line
  ends (as all 133 event and 33 scripted-trigger files of 1.15.2 have; the
  Stellaris, EU4 and CK3 probes unchanged byte for byte); the probe
  procedure (the Workshop item in the step-3 code, `move` steps for run 2,
  one `restore` per launch, the probes the game may leave out) and the
  wording of the approval. `debug`: **0 warnings**, **623/623 tests pass**,
  by label: core 51, sharing 69, persistence 72, platform 59, providers 61,
  games 96, services 96, application 123, ui-preview 1; the new cases
  passed 10 times each. `pmm_games_tests "[.real]"` (read-only): the
  Stellaris, EU4 and CK3 output identical, line for line, to the baseline
  of `main`; the Victoria 3 output unchanged (9 of 9 Workshop folders
  accepted). `scripts/check.sh --full` on `a10c917`: passed (ASan and
  clang-cl with UBSan 499/499 each, clang-tidy on the 22 C++ files changed
  since `origin/main` without findings, the `ci` preset from a fresh
  checkout 623/623).

## Test status (2026-11-13, Workshop ID lists, `workshop-id-lists`)

Owner's decisions of 2026-11-13 (plan "Workshop ID lists for sharing
playsets", defaults D1-D10): a plain Workshop ID list next to the unchanged
share code, exported as the enabled Workshop mods in load order separated by
`|` ([protocol section 8](share-code-protocol.md#8-workshop-id-lists)), and
the names of missing Workshop mods in the import preview from the Steam Web
API ([app](app.md#workshop-names-in-the-import-preview)). Local runs only,
with fakes, fixtures, a local HTTP stub for the Web API and new scratch
profiles; no request reached the real Web API, no game, launcher or Steam
client was started, nothing under Steam, the game folders or
`Documents/Paradox Interactive` was written, and the launcher's database was
not opened. `git diff tests/fixtures` against `main` is empty, and
`share_code.*` and `codec.*` are unchanged.

- `debug` preset: **0 warnings**, **594/594 tests pass**, by label: core 51,
  sharing 67, persistence 72, platform 59, providers 63, games 68, services
  96, application 122, ui-preview 1.
- New: `workshop_id_list_tests.cpp` (11 cases: the owner's example, every
  separator including CRLF and the Unicode spaces, every link form, refusals
  by entry number including the `;N` suffix and foreign links, duplicates as
  id and link, the 10,000-entry and 2 MiB limits with the size checked first,
  the export form, manifests for the three games, the playset export's
  counts, the detection rule); `isValidWorkshopId` boundaries; each verdict of
  the Web API fixture with the old messages unchanged; 5 share-service cases
  (list preview and import, format detection and errors, titles replacing the
  code's hints and stored on import, the warnings and the other-game hint,
  the list export); `lookup()` against the stub (answered once, never from
  inside the call, never through the sink, on success, HTTP 500, a malformed
  reply, a timeout and invalid ids); the runner's batches; 6 Backend cases
  against the stub (the exact request body for the missing ids, names and
  "Not found on Workshop" in the preview and the stored names, a stale answer
  dropped, the cache, 150 ids in 2 requests, 1,001 ids capped at 1,000, no
  request with `--no-steam` or read-only, a stub that never answers ends
  "failed" and the import still works, the list export and the clipboard);
  2 QML cases against the real backend (a hostile Workshop title stays plain
  text without warnings or fetches; at 900x440 the export dialog scrolls and
  Tab reaches **Copy Workshop ID list**); 3 QML preview tests (`tst_ui.qml`
  `test_9b`-`test_9d`). The preview test
  of a typed playset name that survives the late names update fails on the
  previous `syncFromPreview()`.
- The new and changed tests passed 10 times each (the sharing, providers and
  services cases, the metadata host, runner and Backend cases, every `[qml]`
  case and the 36 QML preview tests).
- The application read-only with new scratch profiles against the real
  Stellaris installation: `--self-test "config,ids:…,export:…,dump:…"` wrote
  `1200000004|1200000005|…` (7 enabled Workshop mods, 7 local left out),
  identical to `scripts/share-code.py ids` on the exported code; importing the
  list into a new profile gave exactly those 7 entries, enabled, in order.
  Captures of the import dialog with the list (header "Workshop ID list", the
  list note, "not looked up … in read-only mode") and of the export dialog at
  1400x860 (light and dark) and at 900x440 (it scrolls).
- `scripts/check.sh --full` on `b74796e`: passed in 11 minutes (ASan and
  clang-cl with UBSan 471/471 each, clang-tidy on the 16 C++ files changed
  since `origin/main` without findings after 3 were fixed, the `ci` preset
  from a fresh checkout 594/594).

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
probes only ([CK3 probe run](evidence/2026-11-13-ck3-load-order.md)). Done on
2026-11-21: Victoria 3 step 6, items 1-5, with the probes and one installed
Workshop item ([Victoria 3 probe run](evidence/2026-11-21-vic3-load-order.md)),
and Hearts of Iron IV step 6, items 1-5, with the probes only, in four runs
([Hearts of Iron IV probe run](evidence/2026-11-21-hoi4-load-order.md)).
Still open, each needing the user's approval:

| What | Why open |
| --- | --- |
| EU4 step 7 | run for Stellaris; same code path |
| Play with Steam closed (0.1.1; step 6, item 7) | new on 2026-11-06; tested with fakes only |
| Steam's own offline mode | the Web API was made unreachable instead |
| EU4 mods with `dependencies` and override precedence | not covered by the probes |
| `dependencies` in Stellaris, EU4, CK3 and Hearts of Iron IV | untested in-game: whether a game loads a mod after the mods it names, needs them or skips a mod whose named mods are missing; the application treats them as not required since 2026-11-21 ([evidence](evidence/2026-11-21-descriptor-dependencies.md)). A probe that names one missing and one present probe would settle it |
| CK3 step 4 (Workshop subscribe and download for app 1158310) | not run; ideally with an item that ships `<Name>.mod` |
| CK3 `supported_version="1.20"` and `"1.*"` on 1.20.1.2 | started directly, 1.20.1.2 did not read descriptors outside `enabled_mods`, so the disabled probe told nothing ([CK3 adapter](games/ck3.md#outstanding-validation)) |
| CK3 steps 7-9, and step 6 items 6-7 | same code paths as Stellaris and EU4; not run for CK3 |
| Victoria 3 database entries | the probe run measured whole files; which mod's definition of a database entry wins (since 1.12 the first, unless `REPLACE:` or `INJECT:`) was not probed |
| Victoria 3 `supported_game_version` patterns other than `1.15.*`, `1.*`, `1.15` and `*` | not tried (e.g. `1.*.*`, `*.*`, `1`) |
| Victoria 3 steps 4 and 6-9 (Workshop download for app 529340: O2, skipped) | not run; the download check is the only Victoria 3 specific part, checked read-only |
| Hearts of Iron IV: the file-name rule beyond two local mods | measured with two local mods and two file-name comparisons; Workshop items (`mod/ugc_<id>.mod`, so 9- and 10-digit ids compared as strings) inferred; case, non-ASCII names, `dependencies`, `replace_path`, per-object merging and a launcher-started game not tested ([Hearts of Iron IV adapter](games/hoi4.md#outstanding-validation)) |
| Hearts of Iron IV `supported_version` `"*"`, `"1.*"` and `"1.20.0"` | not tried; `"1.*.*"` (invalid) and `"1.20"` (not reported) are settled for 1.20.0.0 |
| Hearts of Iron IV steps 4 and 6-9 (Workshop subscribe and download for app 394360), and `userdir.txt` or an absolute `gameDataPath` in the game | not run; the latter needs a write to the installation folder |
| Updates and re-downloads of installed Workshop items (since 2026-11-22) | tested with fakes, the fake Steam library and a local Web API stub only: whether `DownloadItem` fetches a version Steam has not noticed yet, whether the Web API's `time_updated` matches the acf, and whether Steam removes an unsubscribed item while the helper session runs are open ([steam-workshop.md](steam-workshop.md#11-not-verified-yet-and-how-to-verify-it), item 4). It downloads into, and a re-download unsubscribes from, the owner's account |

**EU4 and Hearts of Iron IV order:** these two games do not use the playset
order for file overrides (EU4: the mod whose name sorts first wins; Hearts
of Iron IV: the mod whose registration file name sorts last wins). Their
adapters' `LoadOrderInfo` say so (`verifiedInGame = true`), and the mods
pane shows their `userNote` as a closable info banner for their playsets
(`GamesModel.currentLoadOrderNote`, [ui-contract](ui-contract.md)). The
temporary "not yet verified" notes were removed or replaced after the probe
runs, CK3's on 2026-11-13, Victoria 3's on 2026-11-21 (the later mod wins,
as in Stellaris) and Hearts of Iron IV's on 2026-11-21 (replaced by the
permanent note). Not done (possible later): a warning when two enabled EU4
or Hearts of Iron IV mods contain the same file.

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
  backlog item 6b-e; merged into `main` as `8e4c7da`): the minimum window
  size is 900x440 and fits a 1920x1080 screen at 200 % (it was 980x600); the
  column dividers and the table's column edges resize from the keyboard
  (WAI-ARIA window splitter, two Tab stops at the columns' edges and one for
  the four column edges);
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
7. Done 2026-11-12 on `agent/accessibility-6`, merged into `main`
   (`8e4c7da`): backlog item 6, parts b-e (smallest window, keyboard resizing,
   Play focusable while blocked, screenshot and test tooling; see Open items >
   Accessibility). Part a, the listening pass with NVDA and Narrator, stays
   with the owner.
8. Done 2026-11-13 on `agent/ck3`, merged into `main` (`5013efe`):
   Crusader Kings III support, phase 1 (code, tests, fixtures, docs, the
   probe tool), verified read-only. Done 2026-11-13 with the owner's
   approval, recorded on `agent/ck3-probe` and merged (`ab265ea`): the probe
   run and two direct launches on 1.20.1.2 ([CK3 probe run](evidence/2026-11-13-ck3-load-order.md)), then the
   traits (the later mod wins), the support matrix and the CHANGELOG's
   load-order sentence. Next, each with the owner's approval: a Workshop
   download for app 1158310 and the other open CK3 items
   ([CK3 adapter](games/ck3.md#outstanding-validation)). In 0.2.0 (item
   10).
9. Done 2026-11-13 on `workshop-id-lists`, merged into `main` (`abcb775`):
   Workshop ID lists next to the share code, and the names of missing
   Workshop mods in the import preview (owner's decisions of 2026-11-13).
   In 0.2.0 (item 10).
10. 0.2.0 (owner's approval of 2026-11-13), prepared on `release-0.2.0`
    (version, notes, docs and screenshots) and merged into `main`: Crusader Kings III (item 8), Workshop ID lists
    and the Workshop names in the import preview (item 9), backlog item 6b-e
    (item 7) and the fallback to a Workshop item's only `.mod` file, with the
    notes of [`../CHANGELOG.md`](../CHANGELOG.md). Published 2026-11-13 from
    `b3437aa` by pushing its tag ([release](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/tag/v0.2.0), [run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/43448512713)); checked
    afterwards as an installed copy reads it (the request by repository id
    answers `v0.2.0` without a redirect, the real update client reads it, the
    assets match `SHA256SUMS.txt` and pass `check-update-contract.sh assets`).
11. Done 2026-11-20 on `agent/vic3`, merged into `main` (`8225f35`):
    Victoria 3 support, phase 1 (code, tests, fixtures, docs, the probe
    tool), verified read-only on 1.15.2
    ([Victoria 3 adapter](games/vic3.md)). Done 2026-11-21 with the owner's
    approval (O1), recorded on `agent/vic3-probe`: the probe run and two
    direct launches on 1.15.2
    ([Victoria 3 probe run](evidence/2026-11-21-vic3-load-order.md)), then
    the traits (the later mod wins), the warning for a lone `"*"`, the
    `game_custom_data` problem text, the support matrix and the CHANGELOG's
    load-order sentence. In 0.3.0 (item 13). Next: a Workshop download for
    app 529340 needs the owner's approval (O2 skipped it).
12. Done 2026-11-21 on `agent/hoi4` (rebased onto `main` `a294448`, merged
    as `11b73aa`): Hearts of Iron IV support, phase 1 (the test-only catalog
    for the unsupported-game paths, code, tests, fixtures, the golden
    share-code fixture `07-hoi4-basic`, docs, the probe tool with `rename`),
    verified read-only on 1.20.0.0 ([Hearts of Iron IV adapter](games/hoi4.md)).
    Done 2026-11-21, recorded on `agent/hoi4-probe`: the probe run of
    [`load-order.md`](load-order.md#hearts-of-iron-iv), all four runs, and
    four direct launches on 1.20.0.0
    ([Hearts of Iron IV probe run](evidence/2026-11-21-hoi4-load-order.md)):
    the registration file names decide (H3), so the traits state that rule
    and a permanent note like EU4's replaces the temporary one (O3: the
    playset order is still written); the Workshop ID list's `;N` message no
    longer says that every Paradox game loads mods in list order. In 0.3.0
    (item 13). Next: a Workshop download for app 394360 needs the owner's
    approval.
13. 0.3.0 (owner's approval of 2026-11-21), prepared on `release-0.3.0`
    (version, notes, docs and screenshots) and merged into `main`
    (`bb081fd`): Victoria 3 (item 11), Hearts of Iron IV (item 12) and the
    Workshop ID list's `;N` message, with the notes of
    [`../CHANGELOG.md`](../CHANGELOG.md). The screenshots were refreshed on
    2026-11-21: every game in the games column is supported, and there is no
    "Other games" section any more. Nothing in the updater, the installer or
    the packaging changed since 0.2.0, so no dry run was needed
    ([`releasing.md`](releasing.md), step 4). The same branch fixed a race
    in two runner tests of Recover, which had once timed out under load.
    The first release run failed in CI's clang-cl job: the runner image of
    2026-11-11 carries MSVC 14.52, whose standard library refuses clang-cl
    older than Clang 22, next to LLVM 20.1.8. `ci.yml` now installs the
    pinned LLVM 22.1.8 for that job (`6bf6e4f`, checked by a manual CI run),
    and with the owner's OK the tag, which had no release, was moved there.
    Published 2026-11-21 from `6bf6e4f` ([release](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/tag/v0.3.0), [run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/44891023186)); checked
    afterwards as an installed copy reads it (the request by repository id
    answers `v0.3.0` without a redirect, the real update client reads it, the
    assets match `SHA256SUMS.txt` and pass `check-update-contract.sh assets`).
    The owner's installed copy updated itself to 0.3.0 the same day
    (reported by the owner: "update went very well"). Also noted on
    2026-11-21: Steam updated Crusader Kings III on this machine to
    1.21.0.3, which is untested (1.20.1.2 is the verified version).
14. Done 2026-11-21 on `agent/hoi4-dependencies` (the owner's report of
    2026-11-21: a popular Hearts of Iron IV mod lists about a dozen total
    conversions under `dependencies`, and each showed as a missing
    requirement with a warning): the catalog says per game what dependencies
    mean (`DependencyMeaning`: `Required`, or `LoadAfter` for `hoi4` only,
    after the HOI4 wiki); for Hearts of Iron IV there are no dependency
    warnings or feedback, the launch check's line about mods with warnings
    does not mention dependencies, and the details list the names under
    "Loads after", each mod once, with a neutral state each (core
    `resolveDependencies()`, shared with the checks). Read-only on 1.20.0.0: 20 warnings in 4 of the 21 enabled mods
    before, none after ([Hearts of Iron IV](games/hoi4.md#real-machine-evidence)).
    Whether the game orders mods by `dependencies` is untested in-game. The
    other games keep `Required`: on this machine none of their enabled mods
    lists dependencies (Stellaris 3 of 39 installed mods do, Victoria 3 1
    of 10, EU4 and CK3 none); whether they are requirements there is not
    established either. Merged into `main` (`57a6b5e`); item 15 changed the
    other games.
15. Done 2026-11-21 on `agent/dependencies-optional` (the owner's decision
    of 2026-11-21, "not required; keep the order hint", after read-only
    research: none of the four games' mod-loader messages is about a
    missing or misplaced dependency, the Paradox launcher checks only Paradox
    Mods and `metadata.json` dependencies, and only Hearts of Iron IV
    documents what the names do;
    [evidence](evidence/2026-11-21-descriptor-dependencies.md)): a third
    `DependencyMeaning`, `LoadAfterInList`, for `stellaris` and `ck3`, where
    the later mod in the list wins a shared file: only a named mod that is
    enabled but comes later in the list than the mod naming it is a warning,
    once per mod, "Lists "<name>" (#n) to load before this mod (#m), but
    that mod comes later in this playset." (never "Requires"); a missing,
    disabled or absent one is not. `eu4` joins `hoi4` as `LoadAfter` (EU4
    ignores the list order, so no warning at all); `vic3` stays `Required`.
    The launch check keeps "(versions or dependencies)" except in EU4 and
    Hearts of Iron IV. The details list the names of every game but
    Victoria 3 under "Loads after", with the order warning where it applies
    and a note that claims only what is established (Hearts of Iron IV keeps
    its documented one, the catalog's `dependencyLoadAfterDocumented`); a
    mod that names none shows "Dependencies" and "None declared." in every
    game, Hearts of Iron IV included (before, "Loads after" without its
    note). Seven preview screenshots regenerated; the real application's
    captures are unchanged (their selected mods list no dependencies;
    captures replaced on 2026-11-21, item 16).
    Whether any game loads a mod after the mods it names is untested
    in-game. Not merged.
16. Done 2026-11-21 on `agent/privacy-scrub` (the owner's decision of
    2026-11-21: the current files of the public repository must not contain
    the owner's Workshop item ids, mod names or local mod folder names; the
    git history stays as it is, with no rewrite and no force push). Every
    tracked file uses one mapping of made-up values: Workshop ids from one
    made-up range (`12000000NN`) in no particular order, invented item sizes
    and update times (only "API value = acf value" and their order kept), a
    synthetic first item in the Web API fixture, and topic-free names
    (`Example Mod D` to `X`) for the names that had kept a word of a real
    title, also in the 2026-10-29 fixtures. The evidence notes say what
    was replaced or left out (two item sizes and the SHA-256 values of real
    files in the 2026-10-30 notes). The real application's captures
    (`app-*`, `a11y-scale-*`) come from `scripts/app-screenshots.py`: a
    made-up installation of all five games, whose selected Stellaris mod
    has a "Loads after" row; the script checks the profiles against the
    catalog and the made-up data, refuses a `--root` that would show the
    Windows user and cleans up after a failed run. The working rules and
    `CONTRIBUTING.md` now forbid real Workshop data in fixtures, evidence,
    examples and screenshots. Left for the owner: the changelog's 0.2.0
    section now has the new example ids, but the published v0.2.0 release
    notes on GitHub, the import box of the shipped 0.2.0 and 0.3.0 and the
    old tags still show the earlier ones (the next release ships the new
    placeholder); the later CK3, Hearts of Iron IV and Victoria 3 evidence
    notes keep truncated SHA-256 values of the real configuration files.
    Status: `debug` 656/656; scans of every tracked text file against the
    owner's ids, names, local names, SteamIDs and item details (exact,
    case-insensitive, across line breaks, truncated or extended ids, grouped
    digits, slug, joined and acronym forms of the names) find nothing; the
    capture script ran once into a scratch folder with its new checks passing
    (the committed captures kept: the new ones differ only by scattered
    pixels and a hover state); `scripts/check.sh --full` on `3d2fe55`: passed
    in 8 min 15 s (ASan and clang-cl with UBSan 530/530 each, the update
    contract's 112 checks, clang-tidy on the 12 C++ files changed since
    `origin/main` without findings, the `ci` preset from a fresh checkout
    656/656). The commit after it changes this file only. Not merged.
17. Done 2026-11-22 on `agent/workshop-updates` (the owner's request "We
    need a button for re-downloading mods that have been updated on
    Workshop." and decisions of 2026-11-21; a 0.3.1 release follows and is
    not part of this item). **Check for mod updates** in the Downloads area
    asks the Steam Web API about the active playset's installed Workshop
    mods, enabled and disabled (item ids only, batches of 100, only when
    pressed, answers kept for the session and not asked again for 5
    minutes), after a rescan that re-reads Steam's own
    `appworkshop_<app>.acf`, which also counts without any network (with a
    guard for a `latest_manifest` of "0"); only `time_updated` against the
    acf's `timeupdated` decides, and mods whose time came from the folder
    are not compared. A Ready mod with an update stays Ready and
    launchable, says "Update available" with a glyph of its own, has a
    filter ("With updates") and is counted in Play's warnings. **Update
    mods** (every mod with an update, enabled and disabled) and **Update
    from Workshop** (right-click) queue update jobs: they never subscribe,
    request the download also when Steam calls the item current, end only
    when the install time reaches the Workshop's, ask once more after 30 s
    and then fail with `update_not_seen` ("Steam has not picked up this
    update yet. ..."), with Retry. **Re-download from scratch…** (right-click
    on an installed Workshop mod, and on a failed update job), after a
    confirmation, unsubscribes through the helper's new `unsubscribe`
    command (`UnsubscribeItem`, call result 1315), waits until Steam drops
    the subscription, subscribes again, downloads and verifies; a failure
    after the unsubscribe says that the mod is now (or may be) unsubscribed,
    and Retry subscribes again. It never adds a subscription the account did
    not have, stores its step before each request, and says so when Steam
    kept the installed files instead of reporting "Downloaded again".
    Updates and re-downloads are refused while the game runs, wait (and
    resume by themselves) while it runs, and Play waits while one of the
    game runs; a failed update that Steam completes by itself later is no
    longer reported. Jobs keep their kind,
    target and re-download step (schema 3, `v3_sample.sql`). Sessions
    without Steam (`--no-steam`, read-only) disable the actions and send
    nothing; Steam's own record still shows. Documented in
    [`steam-workshop.md`](steam-workshop.md), [`app.md`](app.md#checking-installed-workshop-mods-for-updates),
    [`ui-contract.md`](ui-contract.md), [`accessibility.md`](accessibility.md),
    [`troubleshooting.md`](troubleshooting.md#steam-has-not-picked-up-the-update),
    the support matrix, the README and the changelog. Tested with fakes,
    the fake Steam library, a hand-written helper transcript and the local
    Web API stub only (test status above). **Live verification pending the
    owner's approval** (the open questions L1-L9 and the re-download's in
    [`steam-workshop.md`](steam-workshop.md#11-not-verified-yet-and-how-to-verify-it),
    item 4). Not merged.

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
  Fixtures, evidence, examples and screenshots use only invented Workshop
  ids, mod names, local mod folder names and item details (sizes, times, Web
  API fields), never values from the development machine, not even rounded
  or with a word of the real name kept; the real application's screenshots
  come from `scripts/app-screenshots.py` (a made-up installation).

## Facts established on the development machine

See [`feasibility.md`](feasibility.md) and the evidence files: Windows 11,
Steam, Stellaris 4.5.2 (36 mods) and EU4 1.37.5 (13 mods), the versions tested on
2026-10-29 and 2026-10-30. Stellaris 4.5.4 ("Cygnus v4.5.4 (1b4f)", build
26557690) is installed now (read-only scan, 2026-11-13) and has not been
tested in the game. EU4 has no
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
Victoria 3 "1.15.2 (Ivan-Chai)" (build 26763837, 9 Workshop mods and 1 local
mod) was read on 2026-11-20 and not started: it keeps the enabled mods in
`content_load.json` as absolute paths, has no `.mod` files, no
`dlc_load.json` and no `steam_appid.txt`, and every mod carries a
`.metadata/metadata.json`; the launch plan for the launcher's selection
writes nothing ([Victoria 3](games/vic3.md)). On 2026-11-21, started
directly by the application, it stayed up without the launcher or a
restart through Steam, loaded exactly the enabled probes and a Workshop item
by its absolute path, and the later mod in `enabledMods` won a shared file;
it counted `supported_game_version` `"*"` as not matching 1.15.2, and left a
mod without `game_custom_data` out of its mod list while still loading its
files ([Victoria 3 probe run](evidence/2026-11-21-vic3-load-order.md)).
Hearts of Iron IV "Operation Postern v1.20.0.0.8873 (0c4f)" (build 26733403,
23 Workshop mods, 21 enabled) was read on 2026-11-20 and 2026-11-21: CK3's
`dlc_load.json` scheme in Stellaris's layout, a `dlc_load.json` the launcher
pretty-prints (the launch plan for its selection writes nothing all the
same), `gameDataPath` before `userdir.txt` according to `hoi4.exe`, and a
launcher that turns its own load-order controls off for this game
([Hearts of Iron IV](games/hoi4.md)). On 2026-11-21, started directly by the
application four times, it stayed up without the launcher or a restart
through Steam, read the compact `dlc_load.json` the application wrote and
loaded exactly the enabled probes; the order of `enabled_mods` and `name=`
did not decide which mod won a shared file, the registration file names did
(the later name wins). It reads every `mod/*.mod` at start-up and reported
`supported_version="1.*.*"` as invalid but not `"1.20"`
([Hearts of Iron IV probe run](evidence/2026-11-21-hoi4-load-order.md)).
