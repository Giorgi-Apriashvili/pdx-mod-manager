# Hearts of Iron IV adapter

Steam app 394360, catalog key `hoi4`. Code: `src/games/hoi4.{hpp,cpp}` (traits)
on top of the shared `DlcLoadJsonAdapter` (`src/games/dlc_load_adapter.{hpp,cpp}`),
which shares discovery and launch with the other games through
`LauncherGameAdapter` (`src/games/launcher_adapter.{hpp,cpp}`).
Load order: [`../load-order.md`](../load-order.md#hearts-of-iron-iv).

## Status

Status labels as in [`../feasibility.md`](../feasibility.md). The development
machine runs Windows 11 with "Operation Postern v1.20.0.0.8873 (0c4f)"
(`rawVersion` 1.20.0.0) from Steam (build 26733403, updated on 2026-11-19).
It was read on 2026-11-20 by a read-only inspection, and on 2026-11-21 by the
hidden real-machine test (see [Real-machine evidence](#real-machine-evidence))
and the application in read-only mode. Later on 2026-11-21 the application
started it directly four times for the probe run
([evidence](../evidence/2026-11-21-hoi4-load-order.md)). The game logs quoted
below, unless they are dated 2026-11-21, come from the run before, a 1.19.5.0
build started through the Paradox launcher on 2026-11-18; the probe run's
starts have since replaced those logs on the machine.

| Capability | Windows / Steam | Other stores |
| --- | --- | --- |
| Discover installation and user data (`appmanifest_394360.acf`, `launcher-settings.json`) | **Verified** (read-only); the user data from `gameDataPath` | Not discovered; manual selection, unverified |
| Read `launcher-settings.json` (version, executable, arguments) | **Verified** (read-only): `hoi4.exe`, `-gdpr-compliant` | Unverified |
| `gameDataPath` before `userdir.txt`, also as an absolute folder | Implemented, inferred from `hoi4.exe`'s strings: none ships, and how the game treats a `userdir.txt` or an absolute `gameDataPath` is not observed | Unverified |
| Scan Workshop and local mods, identity rules | **Verified** (read-only): 23 Workshop mods, 0 problems | Local mods only, unverified |
| Read current `dlc_load.json` | **Verified** (read-only): 21 of 21 resolved, from the launcher's pretty-printed file | Unverified |
| Generate `dlc_load.json` and `mod/ugc_<id>.mod` | **Verified** computed, not written: 0 writes for the launcher's selection; 21 of 23 regenerated descriptors identical to the launcher's, the other 2 differ in `supported_version`, one of them also in `version` (see Mods). A changed selection is written as compact JSON. **Verified** written (2026-11-21, probe run): the game read the compact file and did not rewrite it | Unverified |
| Playsets and share codes in the application | **Verified** (read-only): a playset from the game's configuration, a disabled and a moved entry, exported and imported into a new profile with byte-identical dumps | Unverified |
| `supported_version` warnings | **Verified** (2026-11-21, 1.20.0.0, probe run): `"1.*.*"` is reported as invalid, as the error.log of 1.19.5.0 and 1.20.0.0 does; `"1.20"` and `"1.20.*"` are not. `"*"`, `"1.*"` and `"1.20.0"` untested | Same code |
| Direct launch (bypassing the launcher) | **Verified** (2026-11-21, 1.20.0.0): `hoi4.exe -gdpr-compliant` in the installation folder, no launcher process, the same process 25 s later (no restart through Steam), 43 DLC active; four runs, all closed cleanly ([evidence](../evidence/2026-11-21-hoi4-load-order.md)) | Unverified, no Steam environment |
| Workshop subscribe and download (helper in Hearts of Iron IV's app context) | Implemented, unverified (the content check after a download accepts the 23 installed items, read-only) | Unsupported: requires the game on a Steam account |
| Effective order / precedence in game | **Verified** (2026-11-21, 1.20.0.0) with probe mods: enabled mods loaded, disabled not; **the order of `enabled_mods` is ignored: the mod whose registration file name (`mod/<file>.mod`) sorts last wins a shared file**, as the HOI4 wiki says; `name=` does not decide either. The list order is still written; a note above a Hearts of Iron IV playset states the rule ([evidence](../evidence/2026-11-21-hoi4-load-order.md); [load order](../load-order.md#hearts-of-iron-iv)) | Unverified |

No claim is made about achievements or ironman with mods.

## What differs from the other games, and what does not

Shared through `GameTraits`: the `dlc_load.json + mod/*.mod` scheme
(`dlc_load.json` with `enabled_mods` and `disabled_dlcs`, and
`mod/ugc_<id>.mod` registrations with an absolute `path=` and
`remote_file_id`; `hoi4.exe` contains the strings `dlc_load.json`,
`enabled_mods`, `disabled_dlcs` and `userdir.txt`), the identity rules,
descriptor parsing and generation, Workshop scanning and the dropped
`-enabletelemetry` argument. Observed differences on 1.20.0.0:

| Difference | Handling |
| --- | --- |
| The layout is Stellaris's, not CK3's: `hoi4.exe`, `launcher-settings.json`, `dowser.exe` and `steam_appid.txt` in the installation folder; no `launcher/` or `binaries/` folder | Trait `executable = "hoi4.exe"`; launches start in the installation folder, the executable's folder |
| `rawVersion` `1.20.0.0`: no leading `v`, four parts, as for CK3; the label is `Operation Postern v1.20.0.0.8873 (0c4f)` | Same version parser; `rawVersion` is used, so the label's fifth part does not matter |
| `hoi4.exe` calls a `userdir.txt` in the game folder "old style", "Overridden by the gameDataPath field in launcher-settings.json" | Trait `gameDataPathOverridesUserDirFile` (Hearts of Iron IV only): `gameDataPath` first (see User data) |
| The launcher writes `dlc_load.json` pretty-printed (2-space indent), not compact | Compared by meaning: the launcher's selection needs no write. A changed selection is written compact, as for every game |
| Only `supported_version="1.*.*"` was reported as invalid (1.19.5.0, and 1.20.0.0, which reads every registration at start-up, enabled or not) | `rejectsEarlyWildcards = true`, as for Stellaris and CK3 |
| Event files declare their namespace with `add_namespace = <ns>` | Only the probe tool cares: `probe_mods.py --game hoi4` |
| `error.log` lines carry no `[file.cpp:N]` tag; there is no `debug.log` and no mount lines; `system.log` lists the loaded mods as `Active Mod:` lines, sorted by name (a display order, not the load order) | Only the probe procedure cares (`probe_mods.py analyze --game hoi4` reads `system.log`) |
| The game ignores the order of `enabled_mods` for file overrides: the registration file names decide (measured on 1.20.0.0, as the HOI4 wiki says); the launcher turns off its activation order and activation sets for `hoi4` (true for `ck3` and `victoria3`) | The playset order is still written; `loadOrderVerifiedInGame = true` and a permanent note above each Hearts of Iron IV playset, as for EU4 (see Load order) |
| `launcher-settings.json` lists `gameCachePaths`, among them `"mod/"` | Not handled (see Backups and coexistence) |

## Installation discovery

- **Steam** (`discover`): Steam root and libraries as for Stellaris;
  `appmanifest_394360.acf` must report "fully installed" (`StateFlags`
  bit 4) and its folder must exist. Install folder
  `steamapps/common/Hearts of Iron IV`.
- **Manual** (`inspectInstallation`): as for Stellaris. A
  `launcher-settings.json` whose `gameId` is not `hoi4` is rejected
  (`WrongGame`), and so is a Hearts of Iron IV folder chosen for another game.
  Without usable launcher settings, the folder must contain `hoi4.exe`; when a
  subfolder is chosen by mistake, the error says to choose the folder above
  it.
- **Other stores**: no automatic discovery; manual selection is untested.

## User data discovery

In this order (Hearts of Iron IV only; the other games read `userdir.txt`
before `gameDataPath`):

1. A folder chosen by the user (manual location).
2. `launcher-settings.json` `gameDataPath`
   (`%USER_DOCUMENTS%/Paradox Interactive/Hearts of Iron IV` on 1.20.0.0),
   mapped as for Stellaris, or an absolute folder below a drive letter
   (`D:/Games/HOI4 data`, normalized; no `..` parts), which the other games
   ignore: `hoi4.exe` tells users to "Update launcher-settings.json to point
   to the intended custom user directory". Any other form is ignored with a
   warning, and `userdir.txt` is read instead.
3. `<install>/userdir.txt`, with the rules of
   [`eu4.md`](eu4.md#user-data-discovery) (first non-empty line, absolute
   paths only, a trailing separator allowed, anything else ignored with a
   warning). `hoi4.exe` contains the messages "Custom user directory
   configured in the old style with the userdir.txt file in the game folder."
   and "Overridden by the gameDataPath field in launcher-settings.json in the
   same folder.", and advises adding `gameDataPath` when it is missing;
   `stellaris.exe`, `eu4.exe` and `ck3.exe` contain no `gameDataPath` string.
   Hence the order, which is **inferred** from these strings: no
   `userdir.txt` ships with 1.20.0.0, and none was tried in the game (writing
   one would change the installation). A `userdir.txt` that is overridden is
   logged.
4. The default: `<Documents>/Paradox Interactive/Hearts of Iron IV`
   (verified; it is also what `gameDataPath` names).

## launcher-settings.json

Read from `<install>/launcher-settings.json` (tab-indented, LF, no BOM, a
trailing newline). Validation as described in
[`stellaris.md`](stellaris.md#launcher-settingsjson). Used fields on
1.20.0.0:

| Field | Value on 1.20.0.0 | Use |
| --- | --- | --- |
| `gameId` | `hoi4` | wrong-game check |
| `rawVersion` | `1.20.0.0` | `GameInstallation::version`, `supported_version` checks |
| `version` | `Operation Postern v1.20.0.0.8873 (0c4f)` | `versionLabel` |
| `gameDataPath` | `%USER_DOCUMENTS%/Paradox Interactive/Hearts of Iron IV` | user data |
| `exePath` | `./hoi4.exe` | executable |
| `exeArgs` | `["-gdpr-compliant"]` | default arguments, minus `-enabletelemetry` if ever present (`hoi4.exe` has no such switch) |

Ignored: `displayName`, `distPlatform`, `dlcPath` (`.`), `ingameSettingsPath`
and `gameCachePaths` (keys no other game's file has), `browserModUrl` (empty)
and the other launcher keys, and `alternativeExecutables`, whose one entry
starts the game with `-debug_mode`. There is no `formatVersion`,
`modsCompatibilityVersion` or `modLoadOrderDisabled` (copies of the file from
1.9 and 1.10 had `"modLoadOrderDisabled": true`). The manager never passes
`-debug_mode`, nor the launcher's own session arguments
(`--pdx-launcher-session-token`, `--paradox-account-userid`).

## Files

| File | Read | Written (through `LaunchPlan::configWrites` only) |
| --- | --- | --- |
| `<install>/launcher-settings.json` | yes | never |
| `<install>/userdir.txt` | yes, when present and `gameDataPath` does not resolve | never |
| Steam `libraryfolders.vdf`, `appmanifest_394360.acf`, `appworkshop_394360.acf` | yes | never |
| `<library>/steamapps/workshop/content/394360/<id>/descriptor.mod`, else its only `*.mod` | yes | never |
| `<user data>/mod/*.mod` | yes | `mod/ugc_<id>.mod` only, when missing or pointing elsewhere, and never over another mod's registration |
| `<user data>/dlc_load.json` | yes | when its content would change; `disabled_dlcs` and unknown keys preserved |
| `<user data>/launcher-v2*.sqlite`, `<user data>/playsets_backup/` | **never** | **never** |

Observed on 1.20.0.0: `dlc_load.json` is pretty-printed with a 2-space
indent, LF, no BOM and no trailing newline
(`{\n  "enabled_mods": [\n    "mod/ugc_<id>.mod",\n ...  ],\n  "disabled_dlcs": []\n}`),
keys in that order. The adapter compares by meaning, so the launcher's file is
left as it is for the same selection; any write is compact, as for the other
games. `hoi4.exe` builds the reference as `"%s/ugc_%llu.mod"`, limits the
file's size and expects only strings in the lists. If `enabled_mods` appears
twice, the first one counts (HOI4 wiki). The user-data folder also holds
`dlc_signature`, `settings.txt` (without `last_mods`), `pdx_settings.txt`,
`game_data.json`, `continue_game.json` and the launcher's databases; there is
no `mods_registry.json`.

## Mods

Identity rules, states and descriptor generation are those of
[`stellaris.md`](stellaris.md#mods). Observed on 1.20.0.0 (the owner's mods,
by count only):

- **Registrations.** 23 `mod/ugc_<id>.mod` in the launcher's style:
  `key="value"`, lists as `key={` and tab-indented quoted items, LF, no BOM,
  no trailing newline, the author's key order kept, an absolute `path=` with
  forward slashes, then `remote_file_id=` last (equal to the id in 23 of 23).
  Keys seen: `version` (22), `tags`, `name`, `picture` (16),
  `supported_version`, `dependencies` (7 registrations), `replace_path`
  (2 registrations, 3 lines: one repeats it), `path` and `remote_file_id`.
  No `archive` and no `user_dir`. 21 are enabled, 2 registered but not
  enabled; none points at missing content.
- **Workshop content.** 23 plain folders under
  `steamapps/workshop/content/394360/<id>`, each with `descriptor.mod` as its
  only `.mod` file, no zips (before 1.9.0, items were `.bin` zips; documented,
  legacy). One `descriptor.mod` carries its author's own, stale `path=`; it is
  dropped when the registration is generated, as the launcher does.
- **`dependencies`.** Names, kept in the descriptor and written back as they
  are. The HOI4 wiki says a dependency makes a mod load after the named ones
  without making them required, and popular mods name big total conversions
  they do not need. So the application does not check them as requirements
  (the catalog's `dependencyMeaning` is `LoadAfter`, as for Europa
  Universalis IV since 2026-11-21; see
  [load order](../load-order.md#dependencies)): no
  warning on the row, nothing counted under "with warnings" or in the launch
  check (whose line about them reads "N enabled mods have warnings; see the
  mod list.", without "(for example about dependencies)"), no feedback when
  mods are enabled or disabled. The selected mod's details list them under
  "Loads after", each mod once, with what each is in the playset (enabled,
  disabled, installed but not in the playset, not installed) and the note
  that the game does not need them (the only note that says the game loads
  the mod after them, because only Hearts of Iron IV documents it); a mod
  that names none shows "Dependencies" and "None declared.". Nothing is
  enabled, added or reordered for them, and the written order stays the
  playset's (the game applies dependencies itself, according to the wiki).
  Whether the game really orders mods by them is untested in-game. Before
  this change, 4 of the 21 enabled mods on the development machine had 20
  warnings about dependencies that were not installed (2026-11-21,
  read-only; counts only).
- **Registrations older than their content.** On 2026-11-21, 2 of the 23
  registrations differ from what their Workshop content would give, in the
  values the Workshop updates changed: `supported_version` in both, and
  `version` in the one updated on 2026-11-21. The launcher wrote them on
  2026-11-18, and Steam updated the items on 2026-11-19 and 2026-11-21. Their
  `path=` still points at the content, so, as for every game, the application
  shows the registration's values and a launch reuses it without rewriting it;
  the launcher refreshes it the next time it starts. The real-machine test
  reports these 2 as "regenerated descriptor differs". The read-only
  inspection of 2026-11-20 found 22 of 23 identical; the second item was
  updated after it.
- **Paradox Mods items, zipped (`archive=`) mods and local mods**: none exist
  for Hearts of Iron IV on the development machine; they are covered by the
  shared code and the fixture tests only.

**Compatibility warnings.** `supported_version` patterns (`1.19.*`,
`1.19.4.2`) are compared with `rawVersion`. The values on the machine:
`"1.19.*"` 11 times, `"1.*.*"` 4 times, `"1.19.4.2"` twice, and once each
`"1.17.*"`, `"1.11.*"`, `"1.19.4.1"`, `"1.17.3.0"`, `"1.19.1.0"` and
`"1.19.4.*"`; none matches 1.20.0.0 except the `"1.*.*"` ones. The
`logs/error.log` of the 1.19.5.0 run reports `Invalid supported_version in
file: mod/ugc_<id>.mod line: <n>` (with two spaces before `file:`) for exactly
the four `"1.*.*"` registrations, all enabled, each at its
`supported_version=` line, and for none of the others, including `"1.19.*"`,
`"1.17.*"`, `"1.19.4.*"` and the four-part forms without a wildcard. The
adapter therefore reports a wildcard in the major or minor position as a
separate, non-blocking warning, as for Stellaris and CK3.

The probe run of 2026-11-21 rechecked the rule on 1.20.0.0
([evidence](../evidence/2026-11-21-hoi4-load-order.md#supported_version)).
Started directly, 1.20.0.0 reads every registration in `mod/` at start-up,
enabled or not (CK3 1.20.1.2 did not), and reported exactly the same four
`"1.*.*"` registrations, although none of them was enabled, and none of the
other 19. It did not report probe b's two-part `"1.20"` (disabled) or the
probes' `"1.20.*"`. So a wildcard before the patch number is still invalid,
and a two-part pattern without a wildcard is valid, as the adapter assumed;
whether the game counts `"1.20"` as matching 1.20.0.0 is not shown (b was
never loaded, and the game logs no match lines). That `"*"` and `"1.*"` are
rejected too, and that a three-part form without a wildcard such as
`"1.20.0"` is accepted, is still untested for Hearts of Iron IV and follows
the assumption made for Stellaris and CK3.

## Load order

**Verified** on 1.20.0.0 (2026-11-21, four direct launches with probe mods;
[evidence](../evidence/2026-11-21-hoi4-load-order.md),
[load order](../load-order.md#hearts-of-iron-iv)): Hearts of Iron IV loads
the mods `enabled_mods` lists, and no others, but **does not use their order
for file overrides. When two enabled mods contain the same file, the mod
whose registration file name (`mod/<file>.mod`) sorts last wins.** The
winner did not move when the list order changed (run 2) or when the mod's
`name=` changed (run 3); it moved when only the registration file was renamed
(run 4), with the content folder and `path=` unchanged. This is what the
HOI4 wiki (2023 and 2026) and a 2018 developer diary describe. The launcher
v2 turns its activation order and activation sets off for `hoi4` only, which
fits. `system.log` lists the loaded mods sorted by `name=`: a display order,
not the load order.

The application keeps writing the playset order into `enabled_mods`, index 0
first, as the launcher does and as share codes carry it, and states the
rule: `loadOrderVerifiedInGame = true`, the summary "dlc_load.json
enabled_mods lists the playset order, but Hearts of Iron IV does not use it
for file overrides: when two enabled mods contain the same file, the mod
whose registration file name (mod/<file>.mod) sorts last wins", and a
permanent note above a Hearts of Iron IV playset, as for EU4: "Hearts of
Iron IV does not use this order when two mods change the same file: the mod
whose .mod file in the game's mod folder comes last alphabetically wins
(ugc_<ID>.mod for a Workshop mod). The order is still saved, shared and
written for the game." It does not rename registrations to force an order:
the launcher rewrites every `mod/ugc_<id>.mod` under its own name, and other
tools and share codes identify Workshop mods by those names.

Measured with two local mods and two file-name comparisons. Inferred, not
measured: that Workshop items, registered as `mod/ugc_<id>.mod`, follow the
same rule, so that ids compare as strings (`ugc_9…` after `ugc_1…`, whatever
their length). Untested: whether the comparison is ordinal or ignores case,
non-ASCII file names, `dependencies` (the wiki says they load a mod after
the named ones), `replace_path`, folders that merge per object, and a
launcher-started game.

## Launch

`buildLaunchPlan` validates as for Stellaris and returns:

- executable: `<install>/hoi4.exe` (from `exePath`), falling back to
  `hoi4.exe` without arguments;
- arguments: `exeArgs` without `-enabletelemetry`; on 1.20.0.0
  `["-gdpr-compliant"]`. The launcher adds its own session arguments
  (`--pdx-launcher-session-token`, `--paradox-account-userid`; its log of
  2026-11-18 shows `Running hoi4.exe in ...\Hearts of Iron IV`); a direct
  start does not pass them;
- working directory: the executable's folder, the installation folder, as the
  launcher does. `steam_appid.txt` (6 bytes, `394360`, no newline) lies there,
  and a direct start does not restart through Steam, as for Stellaris and CK3
  (verified on 1.20.0.0, 2026-11-21: the same `hoi4.exe` process 25 s later,
  with the application as its parent);
- environment: `SteamAppId=394360` and `SteamGameId=394360` for Steam
  installations. The Steam client must be running;
- config writes: generated `mod/ugc_<id>.mod` files first, then
  `dlc_load.json`, each only if its content changes.

Running-instance check (`processNames`): game `hoi4.exe`. Conflicting
writers: `Paradox Launcher.exe` and `dowser.exe` (`dowser.exe` ships in the
installation folder). Since 2021 Steam's Play button offers the Paradox
launcher or "run with last settings"; Paradox staff confirm that starting
`hoi4.exe` directly skips the launcher. Verified on 1.20.0.0 (2026-11-21,
four runs): no launcher process appeared, the game stayed up until it was
closed, `system.log` listed 43 active DLC (as in the launcher-started
1.19.5.0 session), and `CloseMainWindow` closed it cleanly in about 3.3 s.
Unverified: what the game does without Steam, DLC over a long time without
the launcher, and multiplayer without the launcher's session arguments.

## Backups and coexistence

Identical to Stellaris (see [`stellaris.md`](stellaris.md#backups-and-recovery)):
writes go through `platform::FileTransactionManager` with backups, rollback,
crash recovery and `restore()`.

The Paradox launcher rewrites `dlc_load.json` (pretty-printed) and every
registered `mod/ugc_<id>.mod` before it starts the game, so opening it
replaces the playset this application wrote. Close the launcher before
pressing Play here; its processes are reported as conflicting writers. Its
`launcher-settings.json` lists `"mod/"` among `gameCachePaths`; whether the
launcher's "clear cache" removes the registrations is not known. The
application writes missing Workshop registrations again at Play. This
application never touches `launcher-v2*.sqlite` or `playsets_backup/`.

## Real-machine evidence

`pmm_games_tests "[.real]" -s` on 2026-11-21 (`Real HOI4 installation
(read-only)`; excerpt, user paths redacted to `~`, the mods given as counts
only):

```text
== Hearts of Iron IV
discovered installations: 1
install:        C:/Program Files (x86)/Steam/steamapps/common/Hearts of Iron IV
user data:      ~/Documents/Paradox Interactive/Hearts of Iron IV
executable:     C:/Program Files (x86)/Steam/steamapps/common/Hearts of Iron IV/hoi4.exe
arguments:      1 [-gdpr-compliant]
version:        1.20.0.0 (Operation Postern v1.20.0.0.8873 (0c4f))
distribution:   steam app 394360
steam library:  C:/Program Files (x86)/Steam
app manifest:   StateFlags 4 (fully installed yes, update required no), build 26733403
workshop acf:   NeedsDownload 0, NeedsUpdate 0, items 23 (23 installed, 0 with a pending update)
userdir.txt:    absent
user data from: gameDataPath (launcher-settings.json; before userdir.txt)
steam_appid.txt: present
capabilities:   directLaunch 1, workshopAcquisition 1, localMods 1
mods:           23
  source steam-workshop: 23
  state ready: 23
  local mods carrying a remote_file_id: 0
  unregistered Workshop folders: 0
  with scan problems: 0
  with compatibility warnings: 23
    supported_version "1.*.*" -> 1 warning(s): 4 mod(s)
    supported_version "1.11.*" -> 1 warning(s): 1 mod(s)
    supported_version "1.17.*" -> 1 warning(s): 1 mod(s)
    supported_version "1.17.3.0" -> 1 warning(s): 1 mod(s)
    supported_version "1.19.*" -> 1 warning(s): 11 mod(s)
    supported_version "1.19.1.0" -> 1 warning(s): 1 mod(s)
    supported_version "1.19.4.*" -> 1 warning(s): 1 mod(s)
    supported_version "1.19.4.1" -> 1 warning(s): 1 mod(s)
    supported_version "1.19.4.2" -> 1 warning(s): 2 mod(s)
dlc_load.json enabled mods: 21
  ... (#1-#21: 21 Workshop, all resolved)
  unresolved: 0
launch plan for the current configuration: 0 write(s)
  executable:  C:/Program Files (x86)/Steam/steamapps/common/Hearts of Iron IV/hoi4.exe
  working dir: C:/Program Files (x86)/Steam/steamapps/common/Hearts of Iron IV
  arguments:   [-gdpr-compliant]
  environment: SteamAppId=394360
  environment: SteamGameId=394360
  ... (2 "regenerated descriptor differs" lines)
regenerated Workshop descriptors identical to the launcher's: 21 of 23
download check accepts 23 of 23 Workshop folders
== Hearts of Iron IV: replay without Workshop content
  inventory: 0
  dlc_load.json entries: 21
    steam-workshop not installed: 21
```

Reading it:

- "user data from: gameDataPath": the folder is the one `gameDataPath` names,
  which Hearts of Iron IV reads before a `userdir.txt` (there is none).
- "0 write(s)": the `dlc_load.json` for the launcher's current selection is,
  by meaning, the one the launcher wrote (pretty-printed; the application would
  write it compact), and every enabled registration is reused as it is.
- The 23 warnings are 19 version mismatches (descriptors made for 1.19 and
  older) and the invalid-wildcard warning for the four `"1.*.*"`
  registrations, which match the installed version otherwise.
- 21 of 23: the other 2 are the registrations older than their content (see
  Mods); they differ in `supported_version`, and one also in `version`.
- `workshopAcquisition 1` and the download check say what the application
  would accept; Workshop acquisition for Hearts of Iron IV has not been run.
- The replay copies the registered descriptors with the Steam library
  replaced by an empty one: the 21 Workshop references keep their identity and
  read as not installed.
- In the same run the Stellaris, EU4 and CK3 summaries were identical, line
  for line, to a run of `main` before the Hearts of Iron IV changes.

The application, read-only with new scratch profiles on the same day:
`pmm-app --profile <new> --read-only --game hoi4 --screenshot <png>` shows
"Hearts of Iron IV, Installed · 1.20.0.0" third in the games column, no
"Other games" section (every catalog game is supported), and Play blocked by
the read-only mode; with `--self-test config` added, the mods pane shows the
note of that build, "Hearts of Iron IV may not follow this order when two
mods change the same file; this is not verified yet." (replaced after the
probe run, see Load order), above the 21 entries;
`--self-test "config,disable:1,move:0:3,export:<code>,dump:a.json"` created a
playset of 21 entries from the game's configuration, and a second new profile
imported the code (21 entries, 20 enabled, 21 installed, 0 missing) with a
byte-identical dump; `scripts/share-code.py decode` read the same code (game
`hoi4`, version hint `1.20.0.0`). No file under the game folder or
`Documents/Paradox Interactive/Hearts of Iron IV` changed.

`dependencies`, read-only on the same day after the owner's report (branch
`agent/hoi4-dependencies`): the real-machine test now prints what the
enabled mods list and where the names lead, by count only:

```text
dependencies:   listed by 7 of 23 installed mod(s); 6 enabled mod(s) list 24 name(s); dependency warnings: 0 in 0 enabled mod(s)
  the names lead to: enabled earlier 4, enabled later 0, disabled 0, not in the playset 0, not installed 20
```

With `main`'s rules (Hearts of Iron IV's dependencies checked as required)
the same configuration gave `dependency warnings: 20 in 4 enabled mod(s)`,
all of them `not_installed`. In the application (`--self-test config`, then
`--screenshot` with `--filter warning` and with `--select-row`, new scratch
profiles), the debug build of `main` showed such a mod's names in red as
"Requires …, which is not installed." and the branch shows them under "Loads
after" as "Not installed; not needed"; the footer says "18 with warnings" in
both, because those 4 mods also carry a `supported_version` warning.

The probe run of the same day, with the application of `main` (`11b73aa`),
started the game directly four times; it is recorded in the
[Hearts of Iron IV probe run](../evidence/2026-11-21-hoi4-load-order.md)
(load order, `supported_version`, direct launch, restore, and what the game
changed in its user folder).

## Outstanding validation

Done on 2026-11-21 ([probe run](../evidence/2026-11-21-hoi4-load-order.md)):
which mod wins a shared file (the registration file names decide), that the
game reads the compact `dlc_load.json` the application writes, that a direct
start stays up without the launcher or a restart through Steam, and that
1.20.0.0 still reports `"1.*.*"` and not `"1.20"`. Each of the following
needs the owner's approval, on 1.20.0.0:

1. A direct launch with Steam closed; DLC over a long time without the
   launcher; multiplayer without the launcher's session arguments.
2. Workshop subscribe and download for app 394360.
3. How the game treats a `userdir.txt` in its folder, with and without
   `gameDataPath`, and an absolute `gameDataPath` (needs a write to the
   installation folder).
4. The file-name rule beyond two local mods: the order of 9- and 10-digit
   Workshop ids (`ugc_9…` sorts after `ugc_1…` if the names compare as
   strings), file names that differ only in case or start with non-ASCII
   characters, `dependencies`, `replace_path`, how database folders merge
   entries per object, and a game started by the launcher: not covered by
   the probes.
5. `"*"`, `"1.*"` and a three-part `supported_version` without a wildcard
   (`"1.20.0"`).
