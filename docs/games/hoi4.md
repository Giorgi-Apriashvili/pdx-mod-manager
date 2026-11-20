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
It was only read: on 2026-11-20 by a read-only inspection, and on 2026-11-21
by the hidden real-machine test (see
[Real-machine evidence](#real-machine-evidence)) and the application in
read-only mode. The application has not started it. The game logs quoted below
come from its last run, a 1.19.5.0 build started through the Paradox launcher
on 2026-11-18.

| Capability | Windows / Steam | Other stores |
| --- | --- | --- |
| Discover installation and user data (`appmanifest_394360.acf`, `launcher-settings.json`) | **Verified** (read-only); the user data from `gameDataPath` | Not discovered; manual selection, unverified |
| Read `launcher-settings.json` (version, executable, arguments) | **Verified** (read-only): `hoi4.exe`, `-gdpr-compliant` | Unverified |
| `gameDataPath` before `userdir.txt` | Implemented, inferred from `hoi4.exe`'s strings: none ships, and how the game treats a `userdir.txt` is not observed | Unverified |
| Scan Workshop and local mods, identity rules | **Verified** (read-only): 23 Workshop mods, 0 problems | Local mods only, unverified |
| Read current `dlc_load.json` | **Verified** (read-only): 21 of 21 resolved, from the launcher's pretty-printed file | Unverified |
| Generate `dlc_load.json` and `mod/ugc_<id>.mod` | **Verified** computed, not written: 0 writes for the launcher's selection; 21 of 23 regenerated descriptors identical to the launcher's, the other 2 differ only in `supported_version` (see Mods). A changed selection is written as compact JSON; that the game reads it is unverified | Unverified |
| Playsets and share codes in the application | **Verified** (read-only): a playset from the game's configuration, a disabled and a moved entry, exported and imported into a new profile with byte-identical dumps | Unverified |
| `supported_version` warnings | Implemented: `"1.*.*"` is reported as invalid, as the error.log of 1.19.5.0 does; `"*"`, `"1.*"` and `"1.20"` untested | Same code |
| Direct launch (bypassing the launcher) | Implemented, unverified | Unverified, no Steam environment |
| Workshop subscribe and download (helper in Hearts of Iron IV's app context) | Implemented, unverified (the content check after a download accepts the 23 installed items, read-only) | Unsupported: requires the game on a Steam account |
| Effective order / precedence in game | **Unverified.** The list order is written as for every game; the HOI4 wiki says the names of the `mod/*.mod` files decide, and the launcher turns its own load-order controls off for this game. A note above a Hearts of Iron IV playset says so. The probe run is prepared ([load order](../load-order.md#hearts-of-iron-iv)) | Unverified |

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
| Only `supported_version="1.*.*"` was reported as invalid (1.19.5.0) | `rejectsEarlyWildcards = true`, as for Stellaris and CK3 |
| Event files declare their namespace with `add_namespace = <ns>` | Only the probe tool cares: `probe_mods.py --game hoi4` |
| `error.log` lines carry no `[file.cpp:N]` tag; there is no `debug.log` and no mount lines; `system.log` lists the loaded mods as `Active Mod:` lines, sorted by name | Only the probe procedure cares (`probe_mods.py analyze --game hoi4` reads `system.log`) |
| The launcher turns off its activation order and activation sets for `hoi4` (true for `ck3` and `victoria3`), and the HOI4 wiki orders mods by the names of their `.mod` files | `loadOrderVerifiedInGame = false` and a note above each Hearts of Iron IV playset (see Load order) |
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
   mapped as for Stellaris, when it resolves. An unsupported form is ignored
   with a warning.
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
  without making them required; this application does not use them for the
  order, and that is untested.
- **Registrations older than their content.** On 2026-11-21, 2 of the 23
  registrations differ from what their Workshop content would give, only in
  `supported_version`: the launcher wrote them on 2026-11-18, and Steam updated
  the items on 2026-11-19 and 2026-11-21. Their `path=` still points at the
  content, so, as for every game, the application shows the registration's
  values and a launch reuses it without rewriting it; the launcher refreshes
  it the next time it starts. The real-machine test reports these 2 as
  "regenerated descriptor differs". The read-only inspection of 2026-11-20
  found 22 of 23 identical; the second item was updated after it.
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
`"1.17.*"`, `"1.19.4.*"` and the three- and four-part forms without a
wildcard. The adapter therefore reports a wildcard in the major or minor
position as a separate, non-blocking warning, as for Stellaris and CK3. That
`"*"` and `"1.*"` are rejected too, and that `"1.20"` is accepted, is untested
for Hearts of Iron IV and follows the assumption made for Stellaris and CK3.
The rule was observed on 1.19.5.0 and has not been rechecked on 1.20.0.0.

## Load order

Unverified for Hearts of Iron IV. The application writes the playset order
into `enabled_mods`, index 0 first, as the launcher does and as share codes
carry it. Whether the game applies that order when two mods contain the same
file is not known, and the sources point elsewhere:

- the HOI4 wiki (2023 and 2026) and a 2018 developer diary say mods are
  ordered by the names of their registration files (`mod/<file>.mod`), the
  later name winning, and that `dependencies` load a mod after the named
  ones;
- the launcher v2 turns its activation order and activation sets off for
  `hoi4` only;
- `system.log` lists the loaded mods sorted by name, not in `enabled_mods`
  order (possibly a display order only).

So `loadOrderVerifiedInGame = false`, and a note above a Hearts of Iron IV
playset says: "Hearts of Iron IV may not follow this order when two mods
change the same file; this is not verified yet." A probe run with the owner's
approval is to decide between the list order, the `name=` order, the file
names and the folder order ([load order](../load-order.md#hearts-of-iron-iv)).
If the game ignores the list order, the application keeps writing the playset
order (as for EU4) and states the measured rule; it does not rename
registrations to force an order.

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
  so a direct start is expected not to restart through Steam, as for
  Stellaris and CK3 (unverified for Hearts of Iron IV);
- environment: `SteamAppId=394360` and `SteamGameId=394360` for Steam
  installations. The Steam client must be running;
- config writes: generated `mod/ugc_<id>.mod` files first, then
  `dlc_load.json`, each only if its content changes.

Running-instance check (`processNames`): game `hoi4.exe`. Conflicting
writers: `Paradox Launcher.exe` and `dowser.exe` (`dowser.exe` ships in the
installation folder). Since 2021 Steam's Play button offers the Paradox
launcher or "run with last settings"; Paradox staff confirm that starting
`hoi4.exe` directly skips the launcher. Unverified: the direct start itself,
what the game does without Steam, DLC over a long time without the launcher,
and multiplayer without the launcher's session arguments.

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
  Mods); for each, only `supported_version` differs.
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
note "Hearts of Iron IV may not follow this order when two mods change the
same file; this is not verified yet." above the 21 entries;
`--self-test "config,disable:1,move:0:3,export:<code>,dump:a.json"` created a
playset of 21 entries from the game's configuration, and a second new profile
imported the code (21 entries, 20 enabled, 21 installed, 0 missing) with a
byte-identical dump; `scripts/share-code.py decode` read the same code (game
`hoi4`, version hint `1.20.0.0`). No file under the game folder or
`Documents/Paradox Interactive/Hearts of Iron IV` changed.

## Outstanding validation

Each of the following needs the owner's approval, on 1.20.0.0:

1. The probe procedure of
   [`../load-order.md`](../load-order.md#hearts-of-iron-iv): which mod wins a
   shared file, whether the game reads the compact `dlc_load.json` the
   application writes, and whether a direct start stays up without the
   launcher or a restart through Steam. Then the traits
   (`loadOrderSummary`, `loadOrderEvidence`, `loadOrderVerifiedInGame`) and
   the note.
2. Whether 1.20.0.0 still reports `"1.*.*"` and accepts `"1.20"`.
3. A direct launch with Steam closed; DLC over a long time without the
   launcher; multiplayer without the launcher's session arguments.
4. Workshop subscribe and download for app 394360.
5. How the game treats a `userdir.txt` in its folder, with and without
   `gameDataPath` (needs a write to the installation folder).
6. `dependencies`, the order of 9- and 10-digit Workshop ids if the file
   names decide (`ugc_9…` sorts after `ugc_1…`), names that differ only in
   case or start with non-ASCII characters, and how database folders merge
   entries per object: not covered by the probes.
