# Support matrix

What works for which game and store, and how we know. The application runs on
Windows only ([ADR 0009](adr/0009-windows-only.md)). Support for a combination
depends on the game's own executable and a validated integration (spec section 1).

Last reviewed: 2026-11-21 (Hearts of Iron IV added: read-only checks, then
its probe run with direct launches), after Victoria 3 (read-only checks on
2026-11-20, then its probe run with direct launches on 2026-11-21), the Crusader Kings III probe run of
2026-11-13 and the live validation of 2026-10-30; application 0.3.0.

## Status legend

| Status | Meaning |
| --- | --- |
| **Verified (date, version)** | Observed on a real installation; the evidence is linked. "Read-only" means files were read and plans computed, but nothing was written and no game was started. |
| **Implemented, unverified** | Code and automated tests exist, but the behaviour has not been observed against the real game or store. |
| **Unsupported** | Not available; the reason is given. |

Rules: a cell changes to *Verified* only with recorded evidence (date, game
version, OS, distribution, link to `docs/validation.md` results or
`docs/evidence/`). Untested combinations are **unverified, not supported**.
Nothing here is inferred from another game.

Verified so far on the development machine (Windows 11 Enterprise 23H2, Steam)
with the game versions installed at the time: Stellaris "Cygnus v4.5.2 (9776)"
build 25664273 and EU4 "EU4 v1.37.5.0 Inca (491d)" build 15918133. Stellaris 4.5.2
is the version tested on 2026-10-29 and 2026-10-30. Steam has since updated it on
that machine to "Cygnus v4.5.3 (4e91)" build 26325427 (read-only check of
`launcher-settings.json` and the app manifest, 2026-11-06), which has not been
tested. Results:

- Both games, 2026-10-29, read-only: installation, user data, executable,
  arguments, environment, mod inventory (36 and 13 mods), the current
  `dlc_load.json`, compatibility warnings, and the launch plan for the
  launcher's selection, which writes nothing; regenerated Workshop descriptors
  are byte-identical to the launcher's
  ([evidence](evidence/real-scan-2026-10-29.md)).
- Stellaris, 2026-10-29, read-only: `pmm-steam-helper` initialised the Steam
  API in the game's app context and read session facts and Workshop item
  state ([evidence](evidence/steam-helper-probe.md)).
- Both games, 2026-10-29 and 2026-10-30, read-only: the desktop application
  created a playset from the game's configuration, disabled and moved
  entries, exported it, and a clean profile imported it with identical
  identities, order and enabled flags
  ([evidence](evidence/app-round-trip-2026-10-30.md)).
- Windows ZIP package, 2026-10-29: built, inspected and run from an extracted
  copy without Qt or Visual Studio ([evidence](evidence/packaging-2026-10-29.md)).
- Windows installer, 2026-10-30: real silent installs, upgrades, failed upgrades
  and uninstalls in a scratch folder, including the update run's arguments and its
  wait for the real `pmm-app`, with a stand-in program for the relaunch
  ([evidence](evidence/installer-2026-10-30.md)). On 2026-11-06 the owner installed
  the published 0.1.0 with the wizard, and it worked.
- Both games, 2026-10-30, live (with the user's approval): direct launches
  without the Paradox launcher, enabled and disabled mods and the effective
  override order observed in the games' own logs with probe mods, restore of
  the previous configuration; for EU4 a real Workshop subscription and
  download through the application, restart persistence of a pending job, and
  the failure cases ([evidence](evidence/live-validation-2026-10-30.md)).
- Crusader Kings III "1.20.1.2 (Crozier)" build 26046173, 2026-11-13, read-only:
  installation, user data, executable, arguments, environment, mod inventory
  (11 mods), the current `dlc_load.json`, compatibility warnings, the launch
  plan for the launcher's selection, which writes nothing, and a share-code
  round trip between two new profiles of the application
  ([CK3 adapter](games/ck3.md#real-machine-evidence)). The game was not
  started for these checks.
- Crusader Kings III 1.20.1.2, 2026-11-13, live (with the owner's approval):
  two direct launches without the Paradox launcher, enabled and disabled
  probe mods and the effective override order observed in the game's own
  logs, restore of the previous configuration ([evidence](evidence/2026-11-13-ck3-load-order.md)).
- Victoria 3 "1.15.2 (Ivan-Chai)" build 26763837, 2026-11-20, read-only:
  installation, user data, executable, arguments, environment, mod inventory
  (9 Workshop mods and 1 local mod), the current `content_load.json`,
  compatibility warnings, the launch plan for the launcher's selection, which
  writes nothing, the download check over the 9 Workshop folders, and two
  share-code round trips between new profiles of the application
  ([Victoria 3 adapter](games/vic3.md#real-machine-evidence)). The game was
  not started for these checks.
- Victoria 3 1.15.2, 2026-11-21, live (with the owner's approval): two direct
  launches without the Paradox launcher, enabled and disabled probe mods,
  one installed Workshop item by its absolute path, the effective override
  order and the `supported_game_version` results observed in the game's own
  logs, restore of the previous configuration
  ([evidence](evidence/2026-11-21-vic3-load-order.md)).
- Hearts of Iron IV "Operation Postern v1.20.0.0.8873 (0c4f)" (`rawVersion`
  1.20.0.0) build 26733403, 2026-11-21, read-only: installation, user data
  (from `gameDataPath`), executable, arguments, environment, mod inventory
  (23 Workshop mods), the current `dlc_load.json` (21 enabled), compatibility
  warnings, the launch plan for the launcher's selection, which writes
  nothing although the launcher pretty-prints the file, the download check
  over the 23 Workshop folders, and a share-code round trip between two new
  profiles of the application
  ([Hearts of Iron IV adapter](games/hoi4.md#real-machine-evidence)). The game
  was not started for these checks.
- Hearts of Iron IV 1.20.0.0, 2026-11-21, live: four direct launches without
  the Paradox launcher, enabled and disabled probe mods, the compact
  `dlc_load.json` the application writes, the effective override order and
  the `supported_version` results observed in the game's own logs, restore of
  the previous configuration
  ([evidence](evidence/2026-11-21-hoi4-load-order.md)).

Not observed yet: other stores and Steam's own offline mode.

## Application build

| Platform | Compiler | Core libraries + unit tests | Desktop application + tests | Packages |
| --- | --- | --- | --- | --- |
| Windows 11 x64 | MSVC 19.51 and 19.52 (VS 2026), Qt 6.10.3 | **Verified** 2026-10-30: 402 tests pass, 0 warnings at `/W4`; 2026-11-20 with MSVC 19.52 (toolset 14.52.37025): all of `scripts/check.sh --full` passes, 0 warnings | **Verified** 2026-10-30: 65 tests pass; runs read-only and live against both games | ZIP **verified** 2026-10-29; installer **verified** 2026-10-30 ([evidence](evidence/installer-2026-10-30.md)) |
| Windows (CI, `windows-2025`) | MSVC from the runner image | **Verified** 2026-10-30, CI green | **Verified** 2026-10-30, CI green | Unverified |
| Windows (CI, `windows-2025`) | MSVC with ASan; clang-cl with UBSan (LLVM from the runner image) | **Verified** 2026-11-06, CI green | Not built in these jobs | Not built in these jobs |

## Installing and updating

| Capability | Windows 11 |
| --- | --- |
| Per-user installer: install, upgrade, failed upgrade, uninstall | **Verified** 2026-10-30, silent runs in a scratch folder ([evidence](evidence/installer-2026-10-30.md)); the owner installed the published 0.1.0 with the wizard on 2026-11-06, and it worked |
| Installer: no downgrade over a newer version, one Start Menu shortcut, no desktop shortcut brought back by an update run, the previous version started again after a failed update run, a folder with a non-ASCII name | **Verified** 2026-11-06 by the extended smoke test on a clean GitHub runner ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42164236292)) |
| "Check for updates" and the daily check (on by default, once a day, a banner for a new version) | Implemented; tested against a local stub. The real request by repository id on 2026-11-06: HTTP 404 without a redirect before 0.1.0 was published, HTTP 200 with `v0.1.0` after, and with `v0.1.1` after that was published on 2026-11-07, read by the real update client. **Verified** 2026-11-07: the owner's installed 0.1.0 found 0.1.1 and updated to it |
| "Update and restart" (download, verify, installer, relaunch) | **Verified** 2026-11-07: the owner's installed 0.1.0 updated itself to the published 0.1.1, and that copy later to 0.1.2. Before that, parts run: a real GitHub asset through the real client, the installer with the exact arguments waiting for the real `pmm-app`, a stand-in for the relaunch. The relaunch of the real `pmm-app.exe` after an update run passed in the smoke test on GitHub on 2026-11-06 ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42164236292)). Not available in `--profile` and `--no-steam` sessions |
| Update contract (the values every released copy relies on) | Checked from 2026-11-06 by `scripts/check-update-contract.sh` in CI and by the `Release` workflow ([contract](updates.md#compatibility-contract-frozen-since-010)); passed on GitHub on 2026-11-06 ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42164236292)) |
| `Release` workflow | **Verified** 2026-11-06: published 0.1.0 ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42197165030)) after the full CI, the package, the installer smoke test and the contract checks |

## Stellaris (Steam app 281990)

| Capability | Windows · Steam | Other stores (GOG, Paradox Store, Microsoft Store) |
| --- | --- | --- |
| Detect installation and user data | **Verified (2026-10-29, 4.5.2)** read-only | Implemented, unverified (manual location) |
| Mod inventory (descriptors, Workshop content) | **Verified (2026-10-29, 4.5.2)** read-only: 36 mods; registered Workshop items without content read as missing | Implemented, unverified (local mods only) |
| Playsets (create, order, enable, persist, undo) | **Verified (2026-10-30, 4.5.2)** read-only in the application (create from the game's configuration, disable, move, persist); undo unit tested | Implemented, unverified |
| Share codes (export, import preview, exact reconciliation) | **Verified (2026-10-30, 4.5.2)** read-only between two clean profiles on one machine | Implemented, unverified |
| Generated configuration (`dlc_load.json`, `mod/ugc_<id>.mod`) | **Verified (2026-10-29, 4.5.2)** computed, not written: identical to the launcher's | Implemented, unverified |
| Workshop: Steam session and item state | **Verified (2026-10-29, 4.5.2)** read-only probe | Unsupported: requires the game on a Steam account |
| Workshop: subscribe and download | **Verified (2026-10-30, 4.5.2)**: an item not subscribed before was subscribed, downloaded and verified through the application; the identity check rejected an item of another game without contacting Steam ([evidence](evidence/live-validation-2026-10-30.md)) | Unsupported: requires the game on a Steam account |
| Workshop: manual fallback (open in Steam, rescan) | Implemented, unverified | Unsupported |
| Direct launch without the Paradox launcher | **Verified (2026-10-30, 4.5.2)**: started directly, no launcher process; write failure and start failure handled and restored ([evidence](evidence/live-validation-2026-10-30.md)) | Implemented, unverified |
| Play only while the Steam client runs (from 0.1.1) | Implemented, unverified: tested with fakes only (2026-11-06); checks that Steam runs, not that it is signed in ([validation](validation.md) step 6, item 7) | Not required: a copy located by hand outside a Steam library starts without Steam (implemented, unverified) |
| In-game verification of enabled mods and order | **Verified (2026-10-30, 4.5.2)** with probe mods: enabled mods loaded, disabled not; the later mod in `enabled_mods` wins a shared file, and moving mods in the app moves the winner ([evidence](evidence/live-validation-2026-10-30.md)) | Unverified |

## Europa Universalis IV (Steam app 236850)

Differences from Stellaris: the install folder has no `steam_appid.txt` (a
direct start sets `SteamAppId`/`SteamGameId`; observed 2026-10-30: the game
kept running without restarting through Steam), `userdir.txt` can relocate
the user-data folder (implemented; empty on the development machine),
`exeArgs` contains `-enabletelemetry`, which direct launches drop, and **EU4
does not use the order of `enabled_mods` for file overrides**: the mod whose
name sorts first wins ([evidence](evidence/live-validation-2026-10-30.md); [EU4 adapter](games/eu4.md)).

| Capability | Windows · Steam | Other stores |
| --- | --- | --- |
| Detect installation and user data | **Verified (2026-10-29, 1.37.5)** read-only | Implemented, unverified (manual location) |
| Mod inventory | **Verified (2026-10-29, 1.37.5)** read-only: 13 mods; registered Workshop items without content read as missing (replay of the state after a reinstall) | Implemented, unverified (local mods only) |
| Playsets | **Verified (2026-10-30, 1.37.5)** read-only in the application; undo unit tested | Implemented, unverified |
| Share codes | **Verified (2026-10-30, 1.37.5)** read-only between two clean profiles on one machine | Implemented, unverified |
| Generated configuration | **Verified (2026-10-29, 1.37.5)** computed, not written: identical to the launcher's | Implemented, unverified |
| Workshop: Steam session and item state | **Verified (2026-10-30, 1.37.5)**: helper session in EU4's app context during the live acquisition | Unsupported: requires the game on a Steam account |
| Workshop: subscribe and download | **Verified (2026-10-30, 1.37.5)**: an item not subscribed before was subscribed, downloaded and verified through the application; a pending job survived a restart without a duplicate; with the Web API unreachable nothing was subscribed ([evidence](evidence/live-validation-2026-10-30.md)) | Unsupported: requires the game on a Steam account |
| Workshop: manual fallback | Implemented, unverified | Unsupported |
| Direct launch without the Paradox launcher | **Verified (2026-10-30, 1.37.5)**: started directly, no launcher process, no restart through Steam despite the missing `steam_appid.txt` ([evidence](evidence/live-validation-2026-10-30.md)) | Implemented, unverified |
| Play only while the Steam client runs (from 0.1.1) | Implemented, unverified: tested with fakes only (2026-11-06); checks that Steam runs, not that it is signed in ([validation](validation.md) step 6, item 7) | Not required: a copy located by hand outside a Steam library starts without Steam (implemented, unverified) |
| In-game verification of enabled mods and order | **Verified (2026-10-30, 1.37.5)** with probe mods: enabled mods loaded, disabled not; **the playset order does not decide file overrides** (the mod whose name sorts first wins) ([evidence](evidence/live-validation-2026-10-30.md)) | Unverified |

## Crusader Kings III (Steam app 1158310)

Differences from Stellaris and EU4: the executable is `binaries/ck3.exe` and
`launcher-settings.json` lies in `launcher/`; `steam_appid.txt` lies in
`binaries/`, where direct launches start; `userdir.txt` is read from
`<install>/game/` (documented by the CK3 wiki, unverified); only
`supported_version="1.*"` was reported invalid (1.19 build); one Workshop item
ships `<Name>.mod` instead of `descriptor.mod`; started directly, 1.20.1.2
reports nothing about descriptors outside `enabled_mods`. As in Stellaris,
**the later mod in `enabled_mods` wins a shared file**
([evidence](evidence/2026-11-13-ck3-load-order.md); [CK3 adapter](games/ck3.md)).

| Capability | Windows · Steam | Other stores |
| --- | --- | --- |
| Detect installation and user data | **Verified (2026-11-13, 1.20.1.2)** read-only; `game/userdir.txt` implemented, unverified | Implemented, unverified (manual location) |
| Mod inventory | **Verified (2026-11-13, 1.20.1.2)** read-only: 11 mods, including an item without `descriptor.mod`; registered Workshop items without content read as missing (replay) | Implemented, unverified (local mods only) |
| Playsets | **Verified (2026-11-13, 1.20.1.2)** read-only in the application (create from the game's configuration, disable, move); undo unit tested | Implemented, unverified |
| Share codes | **Verified (2026-11-13, 1.20.1.2)** read-only between two new profiles on one machine. Releases up to 0.1.2 reject codes for `ck3` as not supported | Implemented, unverified |
| Generated configuration | **Verified (2026-11-13, 1.20.1.2)** computed, not written: no write for the launcher's selection; 7 of 11 regenerated descriptors identical, 4 registrations older than their content | Implemented, unverified |
| Workshop: Steam session and item state | Implemented, unverified | Unsupported: requires the game on a Steam account |
| Workshop: subscribe and download | Implemented, unverified | Unsupported: requires the game on a Steam account |
| Workshop: manual fallback | Implemented, unverified | Unsupported |
| Direct launch without the Paradox launcher | **Verified (2026-11-13, 1.20.1.2)**: started directly from `binaries/ck3.exe` (working directory `binaries/`, as the launcher uses), no launcher process, the same process 26 s later (no restart through Steam); restored afterwards ([evidence](evidence/2026-11-13-ck3-load-order.md)). One of the two runs crashed while the game was being closed, cause unknown | Implemented, unverified |
| Play only while the Steam client runs | Implemented, unverified: tested with fakes only | Not required: a copy located by hand outside a Steam library starts without Steam (implemented, unverified) |
| In-game verification of enabled mods and order | **Verified (2026-11-13, 1.20.1.2)** with probe mods: enabled mods loaded, disabled not; the later mod in `enabled_mods` wins a shared file, and moving mods in the app moves the winner ([evidence](evidence/2026-11-13-ck3-load-order.md); [load order](load-order.md#crusader-kings-iii)) | Unverified |

## Hearts of Iron IV (Steam app 394360)

The `dlc_load.json` scheme of Stellaris, EU4 and CK3, with Stellaris's layout:
`hoi4.exe`, `launcher-settings.json` and `steam_appid.txt` in the installation
folder. Differences: `gameDataPath` in `launcher-settings.json` comes before a
`userdir.txt` in the game folder, which `hoi4.exe` calls "old style" (inferred
from its strings, untested); the launcher writes `dlc_load.json`
pretty-printed, and this application writes it compact when the selection
changes, which the game reads; only `supported_version="1.*.*"` is reported
invalid (1.19.5.0 and 1.20.0.0), and 1.20.0.0 reads every registration in
`mod/`, enabled or not. **Hearts of Iron IV does not use the order of
`enabled_mods` for file overrides: the mod whose registration file name
(`mod/<file>.mod`) sorts last wins**, as its wiki says; a note above a Hearts
of Iron IV playset says so, and the playset order is still written, as for
EU4 ([evidence](evidence/2026-11-21-hoi4-load-order.md);
[Hearts of Iron IV adapter](games/hoi4.md); [load order](load-order.md#hearts-of-iron-iv)).

| Capability | Windows · Steam | Other stores |
| --- | --- | --- |
| Detect installation and user data | **Verified (2026-11-21, 1.20.0.0)** read-only (user data from `gameDataPath`); the precedence over `userdir.txt` and an absolute `gameDataPath` implemented, unverified | Implemented, unverified (manual location) |
| Mod inventory | **Verified (2026-11-21, 1.20.0.0)** read-only: 23 Workshop mods, 0 problems, `dependencies` and repeated `replace_path` kept; registered Workshop items without content read as missing (replay) | Implemented, unverified (local mods only) |
| Playsets | **Verified (2026-11-21, 1.20.0.0)** read-only in the application (create from the game's configuration, disable, move); undo unit tested | Implemented, unverified |
| Share codes | **Verified (2026-11-21, 1.20.0.0)** read-only between two new profiles on one machine. Releases up to 0.2.0 reject codes for `hoi4` as not supported | Implemented, unverified |
| Generated configuration | **Verified (2026-11-21, 1.20.0.0)** computed, not written: no write for the launcher's selection (compared by meaning with the launcher's pretty-printed file); 21 of 23 regenerated descriptors identical, 2 registrations older than their content. **Verified (2026-11-21, 1.20.0.0)** written for the probe run: a changed selection is written as compact JSON, the game loaded exactly the listed mods and did not rewrite the file ([evidence](evidence/2026-11-21-hoi4-load-order.md)) | Implemented, unverified |
| Workshop: Steam session and item state | Implemented, unverified | Unsupported: requires the game on a Steam account |
| Workshop: subscribe and download | Implemented, unverified; the check that ends a download accepts the 23 installed items (read-only, 2026-11-21) | Unsupported: requires the game on a Steam account |
| Workshop: manual fallback | Implemented, unverified | Unsupported |
| Direct launch without the Paradox launcher | **Verified (2026-11-21, 1.20.0.0)**: started directly as `hoi4.exe -gdpr-compliant` in the installation folder (where `steam_appid.txt` lies) with `SteamAppId`/`SteamGameId`, no launcher process, the same process 25 s later (no restart through Steam), 43 DLC active; all four runs closed cleanly; restored afterwards ([evidence](evidence/2026-11-21-hoi4-load-order.md)) | Implemented, unverified |
| Play only while the Steam client runs | Implemented, unverified: tested with fakes only | Not required: a copy located by hand outside a Steam library starts without Steam (implemented, unverified) |
| `supported_version` warnings | **Verified (2026-11-21, 1.20.0.0)** with probe mods: a mismatch warns, and `"1.*.*"` as invalid, which the game's error.log reports (also on 1.19.5.0); the game does not report `"1.20"` or `"1.20.*"`. `"*"`, `"1.*"` and `"1.20.0"` untested ([evidence](evidence/2026-11-21-hoi4-load-order.md#supported_version)) | Same code |
| In-game verification of enabled mods and order | **Verified (2026-11-21, 1.20.0.0)** with probe mods: enabled mods loaded, disabled not; **the playset order does not decide file overrides: the mod whose registration file name (`mod/<file>.mod`) sorts last wins** (measured with local mods; for Workshop items, `mod/ugc_<id>.mod`, inferred) ([evidence](evidence/2026-11-21-hoi4-load-order.md); [load order](load-order.md#hearts-of-iron-iv)) | Unverified |

## Victoria 3 (Steam app 529340)

Differences from the other games: the enabled mods are in `content_load.json`
as absolute paths of the mod folders, and each mod is described by its own
`.metadata/metadata.json`; there are no `.mod` files, and Workshop folders are
used in place ([ADR 0010](adr/0010-content-load-adapter-family.md)). The
executable is `binaries/victoria3.exe` and `launcher-settings.json` lies in
`launcher/`, as for CK3; there is no `steam_appid.txt`, as for EU4. A local
mod's id is its folder's (or `.zip` file's) name in `mod/`. Since 1.12 many
databases keep the first definition of a key unless a mod uses `REPLACE:` or
`INJECT:`, so the file order says nothing about database entries
([Victoria 3 adapter](games/vic3.md); [load order](load-order.md#victoria-3)).
`supported_game_version` `"1.*"` is accepted (unlike Stellaris and CK3), but a
lone `"*"` matches no version. As in Stellaris and CK3, **the later mod in
`enabledMods` wins a shared file** ([evidence](evidence/2026-11-21-vic3-load-order.md)).

| Capability | Windows · Steam | Other stores |
| --- | --- | --- |
| Detect installation and user data | **Verified (2026-11-20, 1.15.2)** read-only; `game/userdir.txt` implemented, unverified (where the game reads it from is not known) | Implemented, unverified (manual location) |
| Mod inventory (`metadata.json`, Workshop content) | **Verified (2026-11-20, 1.15.2)** read-only: 9 Workshop mods and 1 local mod, 0 problems; zipped local mods listed without reading their metadata (fixture-tested; none on the machine) | Implemented, unverified (local mods only) |
| Playsets | **Verified (2026-11-20, 1.15.2)** read-only in the application (create from the game's configuration, disable, move); undo unit tested | Implemented, unverified |
| Share codes | **Verified (2026-11-20, 1.15.2)** read-only between new profiles on one machine (the game's configuration, and a 9-item playset from a Workshop ID list). Releases up to 0.2.0 reject codes for `vic3` as not supported | Implemented, unverified |
| Generated configuration (`content_load.json`) | **Verified (2026-11-20, 1.15.2)** computed, not written: no write for the launcher's selection; only `enabledMods` changes, the launcher's layout byte for byte (fixture-tested). **Verified (2026-11-21, 1.15.2)** written for the probe run: the game loaded exactly the listed mods, a Workshop item by its absolute path among them, and did not rewrite the file ([evidence](evidence/2026-11-21-vic3-load-order.md)) | Implemented, unverified |
| Workshop: Steam session and item state | Implemented, unverified | Unsupported: requires the game on a Steam account |
| Workshop: subscribe and download | Implemented, unverified; the check that ends a download accepts the 9 installed items (read-only, 2026-11-20) | Unsupported: requires the game on a Steam account |
| Workshop: manual fallback | Implemented, unverified | Unsupported |
| Direct launch without the Paradox launcher | **Verified (2026-11-21, 1.15.2)**: started directly from `binaries/victoria3.exe` in `binaries/` with `SteamAppId`/`SteamGameId` and no `steam_appid.txt`, no launcher process, the same process 27 s later (no restart through Steam), all 18 DLC loaded; both runs closed cleanly; restored afterwards ([evidence](evidence/2026-11-21-vic3-load-order.md)) | Implemented, unverified |
| Play only while the Steam client runs | Implemented, unverified: tested with fakes only | Not required: a copy located by hand outside a Steam library starts without Steam (implemented, unverified) |
| `supported_game_version` warnings | **Verified (2026-11-21, 1.15.2)** with probe mods: `"1.15.*"`, `"1.*"` and `"1.15"` match, and the game reports no pattern as invalid; a lone `"*"` does not match and warns; a mismatch warns, and the game still loads such a mod ([evidence](evidence/2026-11-21-vic3-load-order.md)) | Same code |
| In-game verification of enabled mods and order | **Verified (2026-11-21, 1.15.2)** with probe mods: enabled mods loaded, disabled not; the later mod in `enabledMods` wins a shared file, and moving mods in the app moves the winner. Whole files only: database entries follow the game's own rules. A mod without `game_custom_data` is left out of the game's mod list but still loaded ([evidence](evidence/2026-11-21-vic3-load-order.md); [load order](load-order.md#victoria-3)) | Unverified |

## Other combinations

| Combination | Status | Reason |
| --- | --- | --- |
| Victoria 3 and Hearts of Iron IV: Paradox Mods, the launcher's playsets, DLC toggling | Unsupported | As for every game: no Paradox Mods provider (Victoria 3 skips `pdx_*` folders, as its launcher does; in Hearts of Iron IV, as in the other `dlc_load.json` games, a `mod/pdx_<id>.mod` registration reads as a local mod), the launcher's database is never read, and `disabledDLC` (Victoria 3) or `disabled_dlcs` (Hearts of Iron IV) is kept as it is. |
| EU4 versions before the Paradox launcher v2 (mods in `settings.txt`) | Unsupported | Not detected as such; their mods would not be enabled ([EU4 adapter](games/eu4.md)). |
| Workshop acquisition with Steam offline or not signed in | Unsupported (by design) | Installed content is still accepted; network operations wait until Steam is online. |
| Mods from Paradox Mods (mods.paradoxplaza.com) or other non-Steam sources | Unsupported | No provider exists; such mods can be used as local mods. |
| 32-bit game builds | Unsupported | The helper is built 64-bit; none of the target games needs a 32-bit Steam API library. |

## Linux, macOS and Proton

Unsupported by decision ([ADR 0009](adr/0009-windows-only.md)). The application
does not build on Linux or macOS and no package exists for them. That includes the
Steam Deck. Running the Windows build, or the games it manages, through Proton or
Wine has not been tried and is not supported either.

## Changing this matrix

Run the procedure in [`docs/validation.md`](validation.md), add the results
row there (and evidence under `docs/evidence/` when it does not fit in a
table), then update the affected cells with the date and game version. Record
failures too: a failed check moves a cell to *Unsupported* with the reason, or
stays *unverified* with a link to the issue.
