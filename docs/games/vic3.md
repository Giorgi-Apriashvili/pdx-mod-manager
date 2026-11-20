# Victoria 3 adapter

Steam app 529340, catalog key `vic3`. Code: `src/games/vic3.{hpp,cpp}` (traits)
on top of `ContentLoadJsonAdapter` (`src/games/content_load_adapter.{hpp,cpp}`),
which shares discovery and launch with the other games through
`LauncherGameAdapter` (`src/games/launcher_adapter.{hpp,cpp}`;
[ADR 0010](../adr/0010-content-load-adapter-family.md)). Mod metadata:
`src/games/mod_metadata.{hpp,cpp}`.
Load order: [`../load-order.md`](../load-order.md#victoria-3).

## Status

Status labels as in [`../feasibility.md`](../feasibility.md). The development
machine runs Windows 11 with "1.15.2 (Ivan-Chai)" from Steam (build
26763837). On 2026-11-20 it was only read: the installation, the user data,
the game's executable (its strings) and its logs, the Paradox launcher's
code, the hidden real-machine test (see
[Real-machine evidence](#real-machine-evidence)) and the application in
read-only mode. Then, on 2026-11-21 and with the owner's approval (O1), the
application started it twice directly with probe mods and one installed
Workshop item ([probe run](../evidence/2026-11-21-vic3-load-order.md)). The
game logs quoted below as those of the last run come from a 1.15.1 session
on 2026-11-18; the probe run rotated them out.

| Capability | Windows / Steam | Other stores |
| --- | --- | --- |
| Discover installation and user data (`appmanifest_529340.acf`, `launcher/launcher-settings.json`) | **Verified** (read-only) | Not discovered; manual selection, unverified |
| Read `launcher-settings.json` (version, executable, arguments) | **Verified** (read-only): `binaries/victoria3.exe`, `-gdpr-compliant` | Unverified |
| `game/userdir.txt` | Implemented, unverified: none ships, and where the game reads it from is not known (chosen by analogy with Crusader Kings III) | Unverified |
| Scan Workshop and local mods, identity rules | **Verified** (read-only): 9 Workshop mods and 1 local mod, 0 problems | Local mods only, unverified |
| Read current `content_load.json` | **Verified** (read-only): 1 of 1 resolved | Unverified |
| Generate `content_load.json` | **Verified**: 0 writes for the launcher's selection (computed, read-only, 2026-11-20); byte-identical to the launcher's layout in the fixture tests; written for the probe run (2026-11-21) with 7 entries, a Workshop item among them, and the game loaded exactly those, without rewriting the file | Unverified |
| Playsets and share codes in the application | **Verified** (read-only): a playset from the game's configuration, and one made from a Workshop ID list of the 9 Workshop items, with a disabled entry and moved entries, each exported and imported into new profiles with byte-identical dumps | Unverified |
| `supported_game_version` warnings | **Verified** (2026-11-21, 1.15.2) with probe mods: a mismatch is a warning (the game logs it and still loads the mod); `"1.*"` and `"1.15"` match and no pattern is reported as invalid, so no wildcard rule; a lone `"*"` does not match and gets a warning | Same code |
| Direct launch (bypassing the launcher) | **Verified** (2026-11-21, 1.15.2): `binaries/victoria3.exe -gdpr-compliant` started by the application, no launcher process, the same process 27 s later (no restart through Steam) without a `steam_appid.txt`, all 18 DLC, the written `content_load.json` used and not rewritten ([probe run](../evidence/2026-11-21-vic3-load-order.md)). With Steam closed, unverified | Unverified, no Steam environment |
| Workshop subscribe and download (helper in Victoria 3's app context) | Implemented, unverified. The content check that ends a download accepts all 9 installed items read-only (the previous check, which wanted `descriptor.mod`, would have rejected every one) | Unsupported: requires the game on a Steam account |
| Effective order / precedence in game | **Verified** (2026-11-21, 1.15.2) with probe mods: enabled mods loaded, the disabled one not; **the later mod in `enabledMods` wins a shared file**, and moving mods in the application moves the winner ([load order](../load-order.md#victoria-3)). Whole files only: database entries follow the game's own rules (see [Load order](#load-order)) | Unverified |

No claim is made about achievements or ironman with mods.

## What differs from the other games

Victoria 3 needs its own adapter family; a new set of traits for the
`dlc_load.json` family would not do. Shared with Crusader Kings III through
`LauncherGameAdapter`: discovery, `launcher-settings.json` in `launcher/`,
the executable in `binaries/` and the launch in that folder, the
`SteamAppId`/`SteamGameId` environment, the dropped `-enabletelemetry`
argument, the Workshop scan and its Steam states. Different:

| | Stellaris, EU4, CK3 | Victoria 3 |
| --- | --- | --- |
| Enabled-mod file | `dlc_load.json` `{"enabled_mods":["mod/x.mod"],"disabled_dlcs":[...]}` | `content_load.json` `{"enabledMods":[{"path":"<absolute folder or .zip>"}],"disabledDLC":[{"paradoxAppId":...}],"enabledUGC":[]}` |
| Mod reference | `mod/<file>.mod`, relative to the user data | the absolute Windows path of the mod's folder (or `.zip`) |
| Registration | `mod/<name>.mod`, and `mod/ugc_<id>.mod` generated for Workshop items | none: Workshop folders are used in place |
| Descriptor | Clausewitz `descriptor.mod` (`name=`, `supported_version=`, `dependencies=`) | JSON `<mod>/.metadata/metadata.json` (`supported_game_version`, `relationships`, `game_custom_data`) |
| Local mod identity | the `.mod` file's stem | the entry's name in `mod/`: the folder's name, or the file name with `.zip` |
| What Play writes | `dlc_load.json`, and `mod/ugc_<id>.mod` where needed | `content_load.json` only |
| `launcher-settings.json` `gameId` | the catalog key | `victoria3`, not `vic3` |
| Version key | `supported_version` | `supported_game_version` (`1.15.*`, `""`) |
| Dependencies | display names | objects with `rel_type` `dependency`, matched by `display_name`, else `id`; older mods write a plain list of names |
| `steam_appid.txt` | Stellaris: the installation folder; CK3: `binaries/`; EU4: none | none anywhere, as for EU4 |
| Game processes | the executable | `victoria3.exe`, and `victoria3_win_console.exe`, a console wrapper that starts it |
| Same object in two mods | mostly, the last definition wins | since 1.12, many databases keep the first definition unless the mod writes `REPLACE:` or `INJECT:` (see [Load order](#load-order)) |
| Version wildcards | Stellaris and CK3 report `*` before the patch number as invalid | `1.*` matches; a lone `*` matches no version (1.15.2) |

## Installation discovery

- **Steam** (`discover`): Steam root and libraries as for Stellaris;
  `appmanifest_529340.acf` must report "fully installed" (`StateFlags`
  bit 4) and its folder must exist. Install folder
  `steamapps/common/Victoria 3` (holds `binaries`, `clausewitz`, `game`,
  `jomini`, `launcher` and `platform_specific_game_data`, no executable).
- **Manual** (`inspectInstallation`): as for Stellaris. A
  `launcher-settings.json` whose `gameId` is not `victoria3` is rejected
  (`WrongGame`), and so is a Victoria 3 folder chosen for another game.
  Without usable launcher settings, the folder must contain
  `binaries/victoria3.exe`; the error names that path, and says to choose
  the folder above when `binaries/` or `launcher/` was chosen by mistake.
- **Paradox Store, GOG, Microsoft Store**: no automatic discovery; manual
  selection is untested.

## User data discovery

In this order:

1. A folder chosen by the user (manual location).
2. `<install>/game/userdir.txt`, with the rules of
   [`eu4.md`](eu4.md#user-data-discovery) (first non-empty line, absolute
   paths only, a trailing separator allowed, anything else ignored with a
   warning). None ships with 1.15.2 (not in the installation folder,
   `binaries/` or `game/`). `victoria3.exe` mentions `userdir.txt` and a
   `userdir` option ("Specify user directory") without saying where it reads
   the file from; `game/` is the location Crusader Kings III, on the same
   engine, documents. **Unverified**.
3. `launcher-settings.json` `gameDataPath`
   (`%USER_DOCUMENTS%/Paradox Interactive/Victoria 3` on 1.15.2), mapped as
   for Stellaris.
4. The default: `<Documents>/Paradox Interactive/Victoria 3` (verified).

## launcher-settings.json

Read from `<install>/launcher/launcher-settings.json` (1388 bytes, LF, no
BOM). Validation as described in
[`stellaris.md`](stellaris.md#launcher-settingsjson). Used fields on 1.15.2:

| Field | Value on 1.15.2 | Use |
| --- | --- | --- |
| `gameId` | `victoria3` | wrong-game check |
| `rawVersion` | `1.15.2` | `GameInstallation::version`, `supported_game_version` checks |
| `version` | `1.15.2 (Ivan-Chai)` | `versionLabel` |
| `gameDataPath` | `%USER_DOCUMENTS%/Paradox Interactive/Victoria 3` | user data |
| `exePath` | `../binaries/victoria3.exe` (relative to `launcher/`) | executable |
| `exeArgs` | `["-gdpr-compliant"]` | default arguments, minus `-enabletelemetry` if ever present |

Ignored: `formatVersion` (the number `1.1`), `displayName`, `modPath`
(`mod`), `modExtension` (`zip`), `isLooseFilesModSupported`, `modStubberPath`
and `modStubberData`, `dlcPath` and the other launcher keys, and
`alternativeExecutables`, whose one entry adds `-debug_mode`. The manager
never passes `-debug_mode`, nor the launcher's own session arguments
(`--pdx-launcher-session-token`, `--paradox-account-userid`, which the
launcher's log shows on every start).

## Files

| File | Read | Written (through `LaunchPlan::configWrites` only) |
| --- | --- | --- |
| `<install>/launcher/launcher-settings.json` | yes | never |
| `<install>/game/userdir.txt` | yes, when present | never |
| Steam `libraryfolders.vdf`, `appmanifest_529340.acf`, `appworkshop_529340.acf` | yes | never |
| `<library>/steamapps/workshop/content/529340/<id>/.metadata/metadata.json` | yes | never |
| `<user data>/mod/<folder>/.metadata/metadata.json` | yes | never |
| `<user data>/content_load.json` | yes | when its content would change: `enabledMods` only |
| `launcher-v2*.sqlite`, `playsets_backup/`, `dlc_signature`, `.launcher-cache/`, `pdx_settings.json`, `game_data.json`, `continue_game.json`, `mods_registry.json` | **never** | **never** |

**content_load.json.** The executable reads `content_load.json`,
`enabledMods` and `disabledDLC` (it calls the file "launcher-generated format
v1/1.1") and contains no `dlc_load.json`. The launcher's JSON schema allows
exactly four keys: `formatVersion` (optional), `disabledDLC`
(`[{"paradoxAppId": ...}]`), `enabledMods` (`[{"path": ...}]`) and
`enabledUGC` (`[{"path": ...}]`); only enabled mods are listed, and the array
order is the load order. The file on the development machine, written by the
launcher: keys in the order `enabledMods`, `disabledDLC`, `enabledUGC`, a
two-space indent, LF, no BOM, **no trailing newline**, backslashes escaped,
no `formatVersion`. The adapter:

- replaces `enabledMods` only and keeps every other key as it is, in its
  place: `disabledDLC` (DLC toggling is out of scope; for all 18 installed DLC
  the launcher's id equals the DLC folder's name), `enabledUGC` (the game does
  not read it: `victoria3.exe` has no such string; the launcher fills it only
  from a `ugcPath` setting that Victoria 3 lacks), `formatVersion` and unknown
  keys. It never adds a key: not `formatVersion`, and not `orderedListMods`,
  a key the executable knows but the launcher never writes;
- writes a new file as `{"enabledMods": [...], "disabledDLC": [],
  "enabledUGC": []}`, and rebuilds a file that is not JSON with a warning;
- reuses the path string the file already holds for a mod (any spelling of
  the same location), and writes new ones in the launcher's form: absolute,
  backslashes, an upper-case drive letter, no trailing separator. A Workshop
  item is `<library>\steamapps\workshop\content\529340\<id>`, as the launcher
  writes it (its code, and a real 1.15 file published by a third-party tool);
- writes the launcher's layout (`dump(2)`: two-space indent, LF, no trailing
  newline), and nothing when the parsed file already says the same.

The launcher's own writer puts every enabled folder mod first, in playset
order, then every zipped mod; the adapter reads the file's order, which is
what the game applies, and writes the playset order.

## Mods

**Inventory** (`scanMods`):

- every folder directly in `<user data>/mod/` is a local mod with the
  folder's name as its id, described by its `.metadata/metadata.json`. A
  folder without that file, or with one that is not valid JSON, is `Broken`:
  the game would leave it out. Folders named `pdx_<n>` or `pdx_<n>_<n>`
  (Paradox Mods installs) are skipped, as the launcher skips them;
- every `*.zip` directly in `<user data>/mod/` is a local mod with the file
  name, `.zip` included, as its id, so that a folder and a zip never share
  one. It is `Ready` and named after the file; its metadata, inside the
  archive, is not read (none exists on the development machine);
- every `<library>/steamapps/workshop/content/529340/<id>` is a Workshop mod,
  `Downloading` or `NeedsUpdate` as `appworkshop_529340.acf` says. All 9 on
  the development machine are plain folders with `.metadata/metadata.json`:
  no `descriptor.mod`, no `*.mod`, no `.zip`.

The metadata `id` is not used as an identity: it is empty in 7 of the 9
Workshop items and in the local mod, and authors choose it freely. Folder
names that read the same (unpaired surrogates read as U+FFFD) become one
`Broken` entry that names them all, as for the other games.

**metadata.json** (`src/games/mod_metadata.hpp`): strict JSON (no comments,
no trailing commas), at most 1 MiB, an optional BOM, LF or CRLF, tabs or
spaces; ill-formed UTF-8 reads as U+FFFD. On the development machine all 9
Workshop files parse as strict JSON without a BOM; indentation and line ends
vary (CRLF in 2). `name`, `version`, `supported_game_version`, `tags` and
`picture` are read; `id`, `game_id`, `short_description` and other top-level
values are kept as extra keys. `relationships` gives the dependencies in two
forms: objects with `rel_type` `dependency` (by `display_name`, else `id`;
`load_before`, `load_after` and `incompatible_with` are not checked yet),
and the older plain list of names. A dependency is matched by name, as for
the other games; on the development machine one Workshop item depends on a
mod that is not installed, and the application says so.

**`game_custom_data`.** All 10 metadata files on the machine have it. Reverse
engineering of 1.15.1 reports that the game logs "Mod metadata read error"
and leaves out a mod without it, even when `content_load.json` lists it. The
probe run on 1.15.2 (2026-11-21) showed half of that: the game logs
`Mod metadata read error. Error: Expected member (game_custom_data)` and
leaves the mod out of its `Mod:` block, but still mounts its folder in its
playset position and loads its files (the probe's event file was parsed).
Such a mod therefore stays `Ready`, not `Broken`, with the problem "Its
.metadata/metadata.json has no "game_custom_data" entry; Victoria 3 reports
a read error and leaves the mod out of its mod list, but still loads its
files."

**Compatibility warnings.** `supported_game_version` (`1.15.*` in 4 Workshop
items and the local mod, `1.7.*` in 1, empty in 4) is compared with
`rawVersion` `1.15.2`. The game itself logs "Mod X (id) version 1.15.*
successfully matched game version 1.15.1" (`debug.log`) or "does not match"
(also in `error.log`), and a mismatch does not stop the mod loading; at the
title menu `dedicated_server.log` lists such mods under "Unsupported Mods
Detected ... The following mods are out of date". The executable also has
"Invalid supported_version in {}". The probe run on 1.15.2 (2026-11-21)
tried `1.15.*`, `1.*`, `1.15` and `*`: the first three matched 1.15.2, `*`
did not match it (and was listed as out of date), no line reported a
pattern as invalid, and all four probes loaded. So `rejectsEarlyWildcards`
stays off (unlike Stellaris and CK3, `1.*` is fine), and
`loneWildcardMismatches` gives a lone `*` the warning `Victoria 3 does not
count supported_game_version "*" as matching the installed version 1.15.2
and lists the mod as out of date.` Other warnings name the key too: `The
supported_game_version "x" is not a version pattern.` Not tried: `1.*.*`,
`*.*`, `1`.

## Load order

The game reads `content_load.json`'s `enabledMods` in order, index 0 first,
as the other games read `enabled_mods`: its `Mod:` block and its mounts
follow that order. **Verified on 1.15.2** with probe mods on 2026-11-21
([probe run](../evidence/2026-11-21-vic3-load-order.md)): when two enabled
mods contain the **same file**, the later one in the list wins, and moving a
mod in the application moves the winner. That is what reverse engineering
of 1.15.1 reported (every mounted mod is searched before the earlier ones),
and the community rule and the file set of the third-party validator
vic3-tiger say the same. `game_custom_data.replace_paths` hides the files of
lower mods in exactly that folder (not probed).

That rule is about **whole files**. Since 1.12 (December 2025) many of the
game's databases no longer let a later definition of the same key replace an
earlier one: the first definition, in file-name order, is kept ("Duplicated
key X will not be created"), unless the mod uses `REPLACE:`, `INJECT:`,
`TRY_*` or `*_OR_CREATE:`. Events and GUI types keep their first definition,
and localization keys are replaced only through
`localization/<language>/replace/`. Moving a mod down a playset therefore
does not make it override a database entry of a mod above it, unless that
mod is written to.

`debug.log` of the last run on the machine before the probe run (1.15.1,
2026-11-18) listed the 18 DLC, then a `Mod:` block (the one enabled mod,
`name|absolute path`), then mounted the DLC folders followed by the mod. The
probe run showed the same layout with seven mods, in `enabledMods` order.
`loadOrderVerifiedInGame` is true, with the wording of Stellaris and CK3,
and no note is shown above a Victoria 3 playset any more (until the probe
run one said: "It is not yet verified that Victoria 3 follows this order
when two mods change the same file."). The rule is about whole files; the
playset order does not decide which mod's database entry wins.

## Launch

`buildLaunchPlan` validates as for Stellaris and returns:

- executable: `<install>/binaries/victoria3.exe` (from `exePath`), falling
  back to `binaries/victoria3.exe` without arguments;
- arguments: `exeArgs` without `-enabletelemetry`; on 1.15.2
  `["-gdpr-compliant"]`. The launcher's session arguments are not passed;
  for CK3 and EU4 they were not needed to load mods;
- working directory: `<install>/binaries`. The engine finds `../game`,
  `../jomini` and `../clausewitz` from there (a wrong working directory logs
  "No subdirs mounted for game dir"), and the launcher's log shows "Running
  victoria3.exe in ...\Victoria 3\binaries";
- environment: `SteamAppId=529340` and `SteamGameId=529340` for Steam
  installations. There is no `steam_appid.txt` anywhere in the installation,
  as for EU4; started this way, 1.15.2 stayed up (2026-11-21). The Steam
  client must be running;
- config writes: `content_load.json` only, when its content changes. No
  descriptor or registration file is written.

The game has no command-line option for mods; third-party tools write
`content_load.json` and start `binaries\victoria3.exe` directly on 1.15, and
the game prints the list it used in its `Mod:` block.

Observed on 2026-11-21 in two direct launches with the owner's approval
([probe run](../evidence/2026-11-21-vic3-load-order.md)): `victoria3.exe`
started this way stays up, as the same process, without restarting through
Steam or opening the launcher; it reads the written `content_load.json` (the
enabled mods listed in the `Mod:` block and mounted in its order, the
Workshop item by its absolute path) and does not rewrite it; the `DLC:` block
lists all 18 DLC. Steam records the start (it rewrote
`appmanifest_529340.acf` with a new `LastPlayed`). The title menu came 97 to
107 s after the start; closed with `CloseMainWindow` afterwards, the game
quit within 3 s both times. `debug.log` reaches the disk late (its
title-menu line `Transition Empty->Game took` appeared only when the game
closed), while `custom_automated_stats.log` logs
`time-to-reach-title-menu` at once. The process list does not show the
working directory; the launch plan sets `binaries/`. Still unverified: what
the game does without Steam, whether DLC stays enabled when the launcher is
bypassed for a long time, and multiplayer without the launcher's session
arguments.

Running-instance check (`processNames`): `victoria3.exe` and
`victoria3_win_console.exe` (a 167 KB wrapper in `binaries/` that starts
`victoria3.exe` with its output piped). Conflicting writers: `Paradox
Launcher.exe` and `dowser.exe` (shipped in `<install>/launcher/`).
`bootstrapper-v2.exe` and `cpatch.exe` only install and patch the launcher.

## Backups and coexistence

Identical to Stellaris (see [`stellaris.md`](stellaris.md#backups-and-recovery)):
the write goes through `platform::FileTransactionManager` with a backup,
rollback, crash recovery and `restore()`.

The Paradox launcher writes `content_load.json` from its own playset when it
starts a game, so opening it replaces the playset this application wrote.
Close the launcher before pressing Play here; its processes are reported as
conflicting writers. This application never touches `launcher-v2*.sqlite`,
`playsets_backup/` or the other launcher files listed above.

Paradox Mods (`pdx_*` folders) are not supported, as for every game. Mods in
other locations than `mod/` and the Workshop that a `content_load.json`
lists are read as local mods that are not installed, named after their
folder. Such an entry is left out (and logged) when an installed mod in
`mod/` already has that name and the path is not that mod's folder, so that
an import never puts a different mod in its place; so is a path that names
no folder (empty, a drive root). Only other tools write such paths; the
launcher writes `mod/` and Workshop paths.

## Real-machine evidence

`pmm_games_tests "[.real]" -s` on 2026-11-20 (`Real Victoria 3 installation
(read-only)`; excerpt, user paths redacted to `~`, the mods given as counts
only):

```text
== Victoria 3
discovered installations: 1
install:        C:/Program Files (x86)/Steam/steamapps/common/Victoria 3
user data:      ~/Documents/Paradox Interactive/Victoria 3
executable:     C:/Program Files (x86)/Steam/steamapps/common/Victoria 3/binaries/victoria3.exe
arguments:      1 [-gdpr-compliant]
version:        1.15.2 (1.15.2 (Ivan-Chai))
distribution:   steam app 529340
steam library:  C:/Program Files (x86)/Steam
app manifest:   StateFlags 4 (fully installed yes, update required no), build 26763837
workshop acf:   NeedsDownload 0, NeedsUpdate 0, items 9 (9 installed, 0 with a pending update)
userdir.txt:    absent (game/userdir.txt)
steam_appid.txt: absent
capabilities:   directLaunch 1, workshopAcquisition 1, localMods 1
process names:  [victoria3.exe victoria3_win_console.exe], writers [Paradox Launcher.exe dowser.exe]
mods:           10
  source local: 1
  source steam-workshop: 9
  state ready: 10
  local .zip mods: 0
  metadata.json without game_custom_data: 0
  with scan problems: 0
  with compatibility warnings: 1
    supported_game_version "1.7.*" -> 1 warning(s): 1 mod(s)
content_load.json enabled mods: 1
  ... (#1: the local mod, resolved)
  unresolved: 0
launch plan for the current configuration: 0 write(s)
  executable:  C:/Program Files (x86)/Steam/steamapps/common/Victoria 3/binaries/victoria3.exe
  working dir: C:/Program Files (x86)/Steam/steamapps/common/Victoria 3/binaries
  arguments:   [-gdpr-compliant]
  environment: SteamAppId=529340
  environment: SteamGameId=529340
download check accepts 9 of 9 Workshop folders
== Victoria 3: replay without mods or Workshop content
  inventory: 0
  content_load.json entries: 1
    local not installed: 1
```

Reading it:

- "0 write(s)": the `content_load.json` computed for the launcher's current
  selection is the one the launcher wrote.
- The one warning is the item made for `1.7.*`.
- `workshopAcquisition 1` only says that the helper's prerequisites are in
  place; Workshop acquisition for Victoria 3 has not been run. The download
  check is the one the application runs when Steam reports an item
  installed: it accepts all 9 folders. The check before this change wanted
  `descriptor.mod` or a sole `.mod` file and would have rejected all 9.
- The replay copies only `content_load.json`, with an empty Steam library and
  no `mod/` folder: the entry keeps its identity and reads as not installed.
- In the same run the Stellaris, EU4 and CK3 summaries were identical, line
  for line, to a run of the code before the Victoria 3 changes (`main` at
  `cd1b2fc`).

The application, read-only with new scratch profiles on the same day:
`pmm-app --profile <new> --read-only --game vic3 --screenshot <png>` shows
"Victoria 3, Installed · 1.15.2" fourth in the games column, Hearts of Iron
IV alone under "Other games", and Play blocked by the read-only mode.
`--self-test "config,disable:0,export:<code>,dump:a.json"` made a playset of
the game's configuration (1 local mod), and a second new profile imported
the code with a byte-identical dump. A Workshop ID list of the 9 installed
items, imported into a third new profile with `--self-test
"import:<ids>,disable:2,move:0:5,move:8:1,export:<code>,dump:a.json"`, gave
a code that a fourth new profile imported with a byte-identical dump (9
entries, 8 enabled, 9 installed, 0 missing); its window showed the "not yet
verified" note above the list (removed after the probe run), and the
dependency warning of the item whose dependency is not installed.

## Outstanding validation

Done on 2026-11-21, on 1.15.2, with the owner's approval (O1)
([probe run](../evidence/2026-11-21-vic3-load-order.md)): the probe
procedure of [`../load-order.md`](../load-order.md#victoria-3), which
settled the former items 1 to 6:

1. A direct start from the application: the same process until it was
   closed, no launcher, no restart through Steam, all 18 DLC in the `DLC:`
   block.
2. The game loads exactly the written `content_load.json` (its `Mod:` block
   and mounts), and not a disabled mod.
3. Precedence: the later mod in `enabledMods` wins a shared file, in both
   orders; the traits are set (`loadOrderVerifiedInGame = true`, the
   evidence, no user note).
4. A Workshop item listed by its absolute
   `steamapps\workshop\content\529340\<id>` path, as the application writes
   it, is mounted and loaded.
5. `supported_game_version`: `1.*` and `1.15` match, `*` does not, none is
   reported as invalid, and all load; `rejectsEarlyWildcards` stays off and
   `loneWildcardMismatches` is on.
6. A mod without `game_custom_data` is left out of the `Mod:` block but
   still loaded, so it stays `Ready` with a problem that says so.

Each of the following needs the owner's approval: a Workshop subscribe and
download for app 529340, a direct launch with Steam closed, DLC over a long
time without the launcher, multiplayer without the launcher's session
arguments, `game/userdir.txt`, which definition wins a database entry when
two mods define it, and paths with non-ASCII characters (reportedly shown by
the launcher but not loaded by the game).
