# Crusader Kings III adapter

Steam app 1158310, catalog key `ck3`. Code: `src/games/ck3.{hpp,cpp}` (traits)
on top of the shared `DlcLoadJsonAdapter` (`src/games/dlc_load_adapter.{hpp,cpp}`).
Load order: [`../load-order.md`](../load-order.md#crusader-kings-iii).

## Status

Status labels as in [`../feasibility.md`](../feasibility.md). The development
machine runs Windows 11 with "1.20.1.2 (Crozier)" from Steam (build 26046173).
On 2026-11-13 it was first only read (the hidden real-machine test, see
[Real-machine evidence](#real-machine-evidence), and the application in
read-only mode). Then, with the owner's approval, the application started it
twice directly with probe mods only
([probe run](../evidence/2026-11-13-ck3-load-order.md)). The game logs quoted
below as those of a 1.19 build come from its last run before that, on
2026-07-06; the probe run overwrote them.

| Capability | Windows / Steam | Other stores |
| --- | --- | --- |
| Discover installation and user data (`appmanifest_1158310.acf`, `launcher/launcher-settings.json`) | **Verified** (read-only) | Not discovered; manual selection, unverified |
| Read `launcher-settings.json` (version, executable, arguments) | **Verified** (read-only): `binaries/ck3.exe`, `-gdpr-compliant` | Unverified |
| `game/userdir.txt` | Implemented, unverified: none ships, and whether the game honours it is not observed | Unverified |
| Scan Workshop and local mods, identity rules | **Verified** (read-only): 11 Workshop mods, 0 problems, including one item without `descriptor.mod` | Local mods only, unverified |
| Read current `dlc_load.json` | **Verified** (read-only): 9 of 9 resolved | Unverified |
| Generate `dlc_load.json` and `mod/ugc_<id>.mod` | **Verified** computed, not written: 0 writes for the launcher's selection; 7 of 11 regenerated descriptors identical to the launcher's, the other 4 differ only in metadata the Workshop changed since (see Mods) | Unverified |
| Playsets and share codes in the application | **Verified** (read-only): a playset from the game's configuration, disabled and moved entries, exported and imported into a new profile with byte-identical dumps | Unverified |
| `supported_version` warnings (CK3 wildcard rule) | Implemented: the rule comes from the error.log of a 1.19 build; unverified on 1.20.1.2, which, started directly, reported nothing about descriptors outside `enabled_mods` (`"1.20.*"` on the enabled probes drew no message) | Same code |
| Direct launch (bypassing the launcher) | **Verified** (2026-11-13, 1.20.1.2): `binaries/ck3.exe -gdpr-compliant` started by the application, no launcher process, the same process 26 s later (no restart through Steam), the written `dlc_load.json` used and not rewritten ([probe run](../evidence/2026-11-13-ck3-load-order.md)). With Steam closed, unverified | Unverified, no Steam environment |
| Workshop subscribe and download (helper in CK3's app context) | Implemented, unverified (the content check after a download accepts an item that ships `<Name>.mod`, fixture-tested) | Unsupported: requires the game on a Steam account |
| Effective order / precedence in game | **Verified** (2026-11-13, 1.20.1.2) with probe mods: enabled mods loaded, the disabled one not; **the later mod in `enabled_mods` wins a shared file**, and moving mods in the application moves the winner ([load order](../load-order.md#crusader-kings-iii)) | Unverified |

No claim is made about achievements or ironman with mods: the sources
disagree.

## What differs from Stellaris and EU4, and what does not

Shared through `GameTraits`: the `dlc_load.json + mod/*.mod` scheme (the
launcher v2 writes it for CK3 too: `dlc_load.json` with `enabled_mods` and
`disabled_dlcs`, and `mod/ugc_<id>.mod` registrations; `ck3.exe` contains the
strings `dlc_load.json`, `enabled_mods`, `disabled_dlcs` and `userdir.txt`),
the identity rules, descriptor parsing and generation, Workshop scanning and
the dropped `-enabletelemetry` argument. Observed differences on 1.20.1.2:

| Difference | Handling |
| --- | --- |
| The executable is `<install>/binaries/ck3.exe`; the installation folder has no `.exe` | Trait `executable = "binaries/ck3.exe"` (the fallback); `launcher-settings.json` names it |
| `launcher-settings.json` exists only in `<install>/launcher/`, with `"exePath": "../binaries/ck3.exe"` | Read from there; `exePath` is resolved against that folder and must stay inside the installation |
| `steam_appid.txt` (7 bytes, `1158310`, no newline) lies in `binaries/`, not in the installation folder | Launches start in the executable's folder, `binaries/`, as the launcher does (see Launch) |
| `rawVersion` `1.20.1.2`: no leading `v`, four parts; the label is `1.20.1.2 (Crozier)`, codename last | Same version parser (an optional `v`) |
| `userdir.txt`: none ships; the CK3 wiki documents `<install>/game/userdir.txt` | Trait `userDirFile = "game/userdir.txt"` (see User data) |
| Only `supported_version="1.*"` was reported as invalid (1.19 build) | `rejectsEarlyWildcards = true`, as for Stellaris |
| Started directly, 1.20.1.2 lists only the `enabled_mods` descriptors in `debug.log` and reports nothing about the others (Stellaris, EU4 and a 1.19 build parsed every descriptor in `mod/`) | Nothing to handle: warnings come from the descriptors themselves |
| A Workshop item ships `<Name>.mod` instead of `descriptor.mod` | The only `.mod` file describes the item, for every game (see Mods) |
| Not a difference from Stellaris: the later mod in `enabled_mods` wins a shared file (measured 2026-11-13) | `loadOrderVerifiedInGame = true` with Stellaris's wording; no user note (the temporary "not yet verified" note is gone) |

## Installation discovery

- **Steam** (`discover`): Steam root and libraries as for Stellaris;
  `appmanifest_1158310.acf` must report "fully installed" (`StateFlags`
  bit 4) and its folder must exist. Install folder
  `steamapps/common/Crusader Kings III` (holds `binaries`, `clausewitz`,
  `game`, `jomini`, `launcher` and branch files, no executable).
- **Manual** (`inspectInstallation`): as for Stellaris. A
  `launcher-settings.json` whose `gameId` is not `ck3` is rejected
  (`WrongGame`), and so is a CK3 folder chosen for Stellaris or EU4. Without
  usable launcher settings, the folder must contain `binaries/ck3.exe`; the
  error names that path. When `binaries/` or `launcher/` is chosen by
  mistake (the folder above it holds `binaries/ck3.exe`), the error also says
  to choose the folder above it.
- **Paradox Store, GOG, Microsoft Store**: no automatic discovery; manual
  selection is untested.

## User data discovery

In this order:

1. A folder chosen by the user (manual location).
2. `<install>/game/userdir.txt`, with the rules of
   [`eu4.md`](eu4.md#user-data-discovery) (first non-empty line, absolute
   paths only, a trailing separator allowed, anything else ignored with a
   warning). This is the location the CK3 wiki documents (its example is an
   absolute path ending in a slash); `ck3.exe` references the name
   `userdir.txt`. No such file ships with 1.20.1.2, and none exists at
   `<install>/userdir.txt` or `<install>/binaries/userdir.txt` either. The
   root location that Stellaris and EU4 use is **not** read for CK3.
   Whether and how the game honours the file is unverified.
3. `launcher-settings.json` `gameDataPath`
   (`%USER_DOCUMENTS%/Paradox Interactive/Crusader Kings III` on 1.20.1.2),
   mapped as for Stellaris.
4. The default: `<Documents>/Paradox Interactive/Crusader Kings III`
   (verified).

## launcher-settings.json

Read from `<install>/launcher/launcher-settings.json` (LF, no BOM). Validation
as described in [`stellaris.md`](stellaris.md#launcher-settingsjson). Used
fields on 1.20.1.2:

| Field | Value on 1.20.1.2 | Use |
| --- | --- | --- |
| `gameId` | `ck3` | wrong-game check |
| `rawVersion` | `1.20.1.2` | `GameInstallation::version`, `supported_version` checks |
| `version` | `1.20.1.2 (Crozier)` | `versionLabel` |
| `gameDataPath` | `%USER_DOCUMENTS%/Paradox Interactive/Crusader Kings III` | user data |
| `exePath` | `../binaries/ck3.exe` (relative to `launcher/`) | executable |
| `exeArgs` | `["-gdpr-compliant"]` | default arguments, minus `-enabletelemetry` if ever present |

Ignored: `formatVersion` (0), `modsCompatibilityVersion` (`"1"`),
`displayName`, `dlcPath` (`../game`), `browserModUrl` and the other launcher
keys, and `alternativeExecutables`, whose one entry starts the game with
`-debug_mode`. The manager never passes `-debug_mode`, nor the launcher's
own session arguments.

## Files

| File | Read | Written (through `LaunchPlan::configWrites` only) |
| --- | --- | --- |
| `<install>/launcher/launcher-settings.json` | yes | never |
| `<install>/game/userdir.txt` | yes, when present | never |
| Steam `libraryfolders.vdf`, `appmanifest_1158310.acf`, `appworkshop_1158310.acf` | yes | never |
| `<library>/steamapps/workshop/content/1158310/<id>/descriptor.mod`, else its only `*.mod` | yes | never |
| `<user data>/mod/*.mod` | yes | `mod/ugc_<id>.mod` only, when missing or pointing elsewhere, and never over another mod's registration |
| `<user data>/dlc_load.json` | yes | when its content would change; `disabled_dlcs` and unknown keys preserved |
| `<user data>/launcher-v2*.sqlite`, `<user data>/playsets_backup/` | **never** | **never** |

Observed on 1.20.1.2: `dlc_load.json` is compact JSON without a trailing
newline (`{"enabled_mods":["mod/ugc_<id>.mod",...],"disabled_dlcs":[]}`).
The format of a `disabled_dlcs` entry is unknown for CK3 (the file on the
development machine has an empty list); the adapter keeps whatever is there.
`mods_registry.json`, which older forum advice names, is not part of the
current layout.

## Mods

Identity rules, states and descriptor generation are those of
[`stellaris.md`](stellaris.md#mods). Observed on 1.20.1.2:

- **Registrations.** The launcher writes `mod/ugc_<id>.mod` in its usual
  style: `key="value"`, lists as `key={` and tab-indented quoted items, LF,
  no BOM, no trailing newline, an absolute `path=` with forward slashes, then
  `remote_file_id=` last. Keys seen: `version`, `tags`, `name`, `picture`,
  `supported_version`, `replace_path` (one descriptor repeats it 24 times;
  every repetition is kept, in order), `path` and `remote_file_id`. No
  `archive`, `dependencies` or `user_dir`.
- **Workshop content.** Plain folders under
  `steamapps/workshop/content/1158310/<id>`, no zips. Their `descriptor.mod`
  has no `path=`.
- **An item without `descriptor.mod`.** One Workshop item ships
  `<Name>.mod` instead. The launcher had registered it from that file; the
  regenerated registration is byte-identical to the launcher's. When a
  content folder has no `descriptor.mod` and **exactly one** `*.mod` file,
  that file describes the item, for every game: an unregistered item is
  `Ready`, and its `mod/ugc_<id>.mod` is generated from that file. The
  content check after a Workshop download applies the same rule, so the
  application can download such an item (on a new machine, for example
  after importing a share code that lists it). With no `.mod` file the item
  is `Broken` as before; with several it is `Broken` and the problem names
  them. A folder that holds a `.zip` stays a legacy zipped item, unless a
  registration's `path=` names the folder itself (then the folder stays the
  content and the registration is reused).
- **Registrations older than their content.** On the development machine 4
  of the 11 registrations carry the metadata of an older version of the item
  (its version, name or tags changed on the Workshop since the launcher last
  wrote them). Their `path=` still points at the content, so the existing
  behaviour applies, as for every game (owner's decision, 2026-11-13): the
  application shows the registration's name and version, and a launch reuses
  the registration without rewriting it. The launcher refreshes them the next
  time it starts. The real-machine test reports these 4 as "regenerated
  descriptor differs".
- **Paradox Mods items** (`mod/pdx_<id>.mod` by Irony's convention), zipped
  (`archive=`) mods, local mods and `dependencies`: none exist for CK3 on the
  development machine; they are covered by the shared code and its Stellaris
  and EU4 tests only.

**Compatibility warnings.** `supported_version` patterns (`1.20.*`,
`1.19.0.6`) are compared with `rawVersion`. The game's `logs/error.log` of
the last run (a 1.19 build) reports `Invalid supported_version in file:
mod/ugc_<id>.mod` for exactly one descriptor, the one with `"1.*"`, and not
for `"1.19.*"`, `"1.18.*"`, `"1.16.*"`, `"1.19.0.6"` or `"1.13.0.3"`; the
four-part form is valid. The adapter therefore reports a wildcard in the
major or minor position as a separate, non-blocking warning, as for
Stellaris. That `"*"` and `"1.*.*"` are rejected too is inferred from
Stellaris, and the rule is unverified on 1.20.1.2. That a two-part pattern
without a wildcard (`"1.20"`) is valid is assumed: no descriptor on the
machine has one, so the report for `"1.*"` cannot tell a wildcard before the
patch number from fewer than three parts. `debug.log` of the same run lists
every registered descriptor as `|Enabled` or `|Disabled` and mounts the
enabled mods in exactly the `enabled_mods` order.

The probe run on 1.20.1.2 (2026-11-13) could not check `"1.20"`: started
directly by the application, the game listed only the `enabled_mods` entries
in `debug.log` (no `|Disabled` line) and reported no `Invalid
supported_version` line at all, also not for the registered `"1.*"`
descriptor; the disabled probe with `"1.20"` was never read. `"1.20.*"` on
the two enabled probes drew no message. The 1.19 run was apparently started
through the Paradox launcher, so whether the version or the launch path makes
the difference is not known. The rule therefore stays as it is: unverified
on 1.20.1.2, and `"1.20"` assumed valid.

## Launch

`buildLaunchPlan` validates as for Stellaris and returns:

- executable: `<install>/binaries/ck3.exe` (from `exePath`), falling back to
  `binaries/ck3.exe` without arguments;
- arguments: `exeArgs` without `-enabletelemetry`; on 1.20.1.2
  `["-gdpr-compliant"]`. The launcher adds its own session arguments
  (`_commandline_options.info` describes `-pdx-launcher-session-token` as
  "Session token passed in from PDX launcher"); a direct start does not
  pass them, and none of them is needed to load mods;
- working directory: the executable's folder, `<install>/binaries`, where
  `steam_appid.txt` lies. A Paradox launcher log of 2024 shows the launcher
  doing the same (`Running ck3.exe in ...\Crusader Kings III\binaries`).
  Since 2026-11-13 every game starts in its executable's folder; for
  Stellaris and EU4 that is the installation folder, as before;
- environment: `SteamAppId=1158310` and `SteamGameId=1158310` for Steam
  installations. The Steam client must be running;
- config writes: generated `mod/ugc_<id>.mod` files first, then
  `dlc_load.json`, each only if its content changes.

Running-instance check (`processNames`): game `ck3.exe`. Conflicting
writers: `Paradox Launcher.exe` and `dowser.exe` (`dowser.exe` ships in
`<install>/launcher/`; the launcher itself lives in
`%LOCALAPPDATA%\Programs\Paradox Interactive\launcher`).

Observed on 2026-11-13 in two direct launches with the owner's approval
([probe run](../evidence/2026-11-13-ck3-load-order.md)): `ck3.exe` started
this way stays up, as the same process, without restarting through Steam or
opening the launcher; it reads the written `dlc_load.json` (the enabled
probes loaded, mounted in its order) and does not rewrite it; `debug.log`
mounted 27 DLC folders. The process list does not show the working
directory; the launch plan sets `binaries/`. One of the two runs crashed
while closing (an access violation in a worker thread, after
`CloseMainWindow` 43 s after the main menu and before the `debug.log` line
`Setup completion (history loaded)`); the other, closed after that line,
quit cleanly. The cause is not known. Still unverified: what the game does
without Steam, whether DLC stays enabled when the launcher is bypassed for a
long time (the game writes `dlc_signature` at start-up, as Stellaris and EU4
do), and multiplayer without the launcher's session arguments.

## Backups and coexistence

Identical to Stellaris (see [`stellaris.md`](stellaris.md#backups-and-recovery)):
writes go through `platform::FileTransactionManager` with backups, rollback,
crash recovery and `restore()`.

The Paradox launcher rewrites `dlc_load.json` and every registered
`mod/ugc_<id>.mod` when it starts (a 2024 launcher log: `Saving settings to
dlc_load.json`, about 2 s after start). On the development machine it wrote
them at 15:39:47 on 2026-07-06, and the game started 5 s later. Opening the
launcher therefore replaces the playset this application wrote, and refreshes
the registrations older than their content. Close the launcher before pressing
Play here; its processes are reported as conflicting writers. This
application never touches `launcher-v2*.sqlite` or `playsets_backup/`.

## Real-machine evidence

`pmm_games_tests "[.real]" -s` on 2026-11-13 (`Real CK3 installation
(read-only)`; excerpt, user paths redacted to `~`, the mods given as counts
only):

```text
== Crusader Kings III
discovered installations: 1
install:        C:/Program Files (x86)/Steam/steamapps/common/Crusader Kings III
user data:      ~/Documents/Paradox Interactive/Crusader Kings III
executable:     C:/Program Files (x86)/Steam/steamapps/common/Crusader Kings III/binaries/ck3.exe
arguments:      1 [-gdpr-compliant]
version:        1.20.1.2 (1.20.1.2 (Crozier))
distribution:   steam app 1158310
steam library:  C:/Program Files (x86)/Steam
app manifest:   StateFlags 4 (fully installed yes, update required no), build 26046173
workshop acf:   NeedsDownload 0, NeedsUpdate 0, items 11 (11 installed, 0 with a pending update)
userdir.txt:    absent (game/userdir.txt)
steam_appid.txt: present in the executable's folder (binaries)
capabilities:   directLaunch 1, workshopAcquisition 1, localMods 1
mods:           11
  source steam-workshop: 11
  state ready: 11
  local mods carrying a remote_file_id: 0
  unregistered Workshop folders: 0
  with scan problems: 0
  with compatibility warnings: 11
    supported_version "1.*" -> 1 warning(s): 1 mod(s)
    supported_version "1.13.0.3" -> 1 warning(s): 1 mod(s)
    supported_version "1.16.*" -> 1 warning(s): 1 mod(s)
    supported_version "1.18.*" -> 1 warning(s): 1 mod(s)
    supported_version "1.19.*" -> 1 warning(s): 4 mod(s)
    supported_version "1.19.0.6" -> 1 warning(s): 3 mod(s)
dlc_load.json enabled mods: 9
  ... (#1-#9: 9 Workshop, all resolved)
  unresolved: 0
launch plan for the current configuration: 0 write(s)
  executable:  C:/Program Files (x86)/Steam/steamapps/common/Crusader Kings III/binaries/ck3.exe
  working dir: C:/Program Files (x86)/Steam/steamapps/common/Crusader Kings III/binaries
  arguments:   [-gdpr-compliant]
  environment: SteamAppId=1158310
  environment: SteamGameId=1158310
  ... (4 "regenerated descriptor differs" lines)
regenerated Workshop descriptors identical to the launcher's: 7 of 11
== Crusader Kings III: replay without Workshop content
  inventory: 0
  dlc_load.json entries: 9
    steam-workshop not installed: 9
```

Reading it:

- "0 write(s)": the `dlc_load.json` computed for the launcher's current
  selection is the one the launcher wrote, and every enabled registration is
  reused as it is.
- The 11 warnings are 10 version mismatches (descriptors made for 1.19 and
  older) and the invalid-wildcard warning for `"1.*"`, which matches the
  installed version otherwise.
- 7 of 11: the item without `descriptor.mod` is among the 7 (regenerated
  from its `<Name>.mod`). The other 4 are the registrations older than their
  content (see Mods); for each, only `version`, `name` or `tags` differ.
- `workshopAcquisition 1` only says that the helper would find
  `binaries/steam_api64.dll`; Workshop acquisition for CK3 has not been run.
  The content check that ends a download (`verifyWorkshopContent()`), run
  read-only over the 11 content folders by a scratch program on the same
  day, accepts all 11; the code before the sole-`.mod` fix in that check
  rejected the item without `descriptor.mod`.
- The replay copies the registered descriptors with the Steam library
  replaced by an empty one: the 9 Workshop references keep their identity and
  read as not installed.
- In the same run the Stellaris and EU4 summaries were identical to a run of
  the code before the CK3 changes, line for line (including their working
  directory, the installation folder).

The application, read-only with new scratch profiles on the same day:
`pmm-app --profile <new> --read-only --game ck3 --screenshot <png>` shows
"Crusader Kings III, Installed · 1.20.1.2" first in the games column and Play
blocked by the read-only mode; with `--self-test config` added, the mods
pane showed the "not yet verified" load-order note above the 9 entries (removed
after the probe run); `--self-test
"config,disable:1,move:0:3,export:<code>,dump:a.json"` created a playset of 9
entries from the game's configuration, and a second new profile imported the
code (9 entries, 8 enabled, 9 installed, 0 missing) with a byte-identical
dump. No file under Steam, the game folder or `Documents/Paradox Interactive`
changed.

## Outstanding validation

Done on 2026-11-13 with the owner's approval
([probe run](../evidence/2026-11-13-ck3-load-order.md)): the probe procedure
of [`../load-order.md`](../load-order.md#crusader-kings-iii) (the later mod in
`enabled_mods` wins a shared file; the traits are set), and two direct
launches from the application (no launcher process, no restart through Steam,
the probes active as configured). Each of the following needs the owner's
approval, on 1.20.1.2:

1. Whether CK3 accepts `supported_version="1.20"` and still rejects `"1.*"`.
   Started directly, 1.20.1.2 did not read descriptors outside
   `enabled_mods`, so this needs a run in which such a descriptor is enabled,
   or one started through the launcher.
2. A direct launch with Steam closed; DLC over a long time without the
   launcher; multiplayer without the launcher's session arguments.
3. Workshop subscribe and download for app 1158310, ideally including an
   item that ships `<Name>.mod` instead of `descriptor.mod`.
4. How CK3 treats a `game/userdir.txt` (absolute and relative).
