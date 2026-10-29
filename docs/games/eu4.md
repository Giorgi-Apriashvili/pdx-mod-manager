# Europa Universalis IV adapter

Steam app 236850, catalog key `eu4`. Code: `src/games/eu4.{hpp,cpp}` (traits)
on top of the shared `DlcLoadJsonAdapter` (`src/games/dlc_load_adapter.{hpp,cpp}`).
Load order: [`../load-order.md`](../load-order.md).

## Status

Status labels as in [`../feasibility.md`](../feasibility.md). The development
machine runs Windows 11 with "EU4 v1.37.5.0 Inca (491d)" from Steam (build
15918133). It was first only read
([`../evidence/real-scan-2026-10-29.md`](../evidence/real-scan-2026-10-29.md));
on 2026-10-30 it was launched by the application with probe mods, with the
user's approval ([live validation](../evidence/live-validation-2026-10-30.md)).

| Capability | Windows / Steam | Linux, macOS, other stores |
| --- | --- | --- |
| Discover installation and user data (`appmanifest_236850.acf`, `launcher-settings.json`, `userdir.txt`) | **Verified** (read-only) | Implemented, unverified |
| Read `launcher-settings.json` (version, executable, arguments) | **Verified**; `-enabletelemetry` is dropped (see Launch) | Implemented, unverified |
| Scan Workshop and local mods, identity rules | **Verified**: 13 mods (12 Workshop, 1 local with a `remote_file_id`), 0 problems | Implemented, unverified |
| Registered Workshop items whose content is missing | **Verified** with a replay of the real descriptors: not installed, read as missing | Same code |
| Read current `dlc_load.json` | **Verified**: 9 of 9 resolved; equals the launcher's active (custom-ordered) playset | Implemented, unverified |
| Generate `dlc_load.json` and `mod/ugc_<id>.mod` | **Verified** byte-identical to the launcher's output (computed, not written): 0 writes, 12 of 12 descriptors identical | Implemented, unverified |
| Legacy zipped mods (`archive=`) | Implemented, fixture-tested; none on the machine | Implemented, unverified |
| `supported_version` warnings (no wildcard rule) | **Verified** against the installed descriptors | Same code |
| Direct launch (bypassing the launcher) | **Verified** 2026-10-30: started directly with `-gdpr-compliant`, no launcher process, no restart through Steam | Unverified |
| Workshop subscribe and download (helper in EU4's app context) | **Verified** 2026-10-30: one item subscribed, downloaded and verified through the app | Unverified |
| Effective order / precedence in game | **Verified** 2026-10-30: enabled mods load, disabled do not; **the order of `enabled_mods` does not decide file overrides: the mod whose `name` sorts first wins** ([load order](../load-order.md#europa-universalis-iv)) | Unverified |

Not supported: EU4 versions that predate the Paradox launcher v2 and listed
mods in `settings.txt` (`last_mods`) instead of `dlc_load.json`. They are not
detected as such; their mods would simply not be enabled.

## What differs from Stellaris, and what does not

Shared through `GameTraits`: the `dlc_load.json + mod/*.mod` scheme, the
identity rules, descriptor parsing and generation, Workshop scanning,
`userdir.txt` and the dropped `-enabletelemetry` argument. Separate per game:
app id, user-data folder, executable names, launcher-settings `gameId`, the
`supported_version` wildcard rule (`rejectsEarlyWildcards = false` for EU4),
and the load-order evidence. Observed differences on v1.37.5.0:

- **No `steam_appid.txt`** in the install folder (Stellaris has one). The
  launch plan's `SteamAppId`/`SteamGameId` environment is therefore required
  for the Steam API. Observed 2026-10-30: a direct start kept running (same
  process 20 s later) without restarting through Steam or the launcher.
- **Playset order does not decide overrides** (observed 2026-10-30): EU4
  picks the winner of a shared file by mod name; Stellaris uses the order of
  `enabled_mods`.
- **An empty `userdir.txt`** ships in the install folder (see User data).
- **`exeArgs` asks for `-enabletelemetry`** (see Launch).
- **Four-component versions**: `rawVersion` `v1.37.5.0`, descriptors with
  `supported_version="v1.37.*.*"` or `"v1.37.5.0"`.

EU4-specific behaviour handled since the first version:

- **Legacy zipped mods.** Old Workshop items and local mods can be a `.zip`
  referenced with `archive=`. A registered archive inside the Workshop
  folder becomes `contentPath`; a regenerated descriptor uses
  `archive="<absolute zip>"` instead of `path=`. A zipped Workshop item
  without any registration cannot be described (its descriptor is inside
  the archive) and is reported `Broken`.
- **Relative paths.** `path="mod/<folder>"` and `archive="mod/<file>.zip"`
  are resolved against the user-data folder.

## Installation discovery

- **Steam** (`discover`): Steam root and libraries as for Stellaris;
  `appmanifest_236850.acf` must report "fully installed" (`StateFlags` bit 4)
  and its folder must exist. `BytesToDownload`/`BytesDownloaded` describe the
  last update and are not used (both 530400080 on the machine). Install folder
  `steamapps/common/Europa Universalis IV`.
- **Manual** (`inspectInstallation`): as for Stellaris. A
  `launcher-settings.json` whose `gameId` is not `eu4` is rejected
  (`WrongGame`).
- **Paradox Store, GOG, Microsoft Store**: no automatic discovery; manual
  selection is untested.

## User data discovery

In this order:

1. A folder chosen by the user (manual location).
2. `<install>/userdir.txt`: `eu4.exe` references this file, and v1.37.5.0
   ships it empty. When its first non-empty line (surrounding whitespace, a
   UTF-8 BOM and one pair of quotes removed) is an **absolute** path, that
   folder is the user-data folder. Relative paths, invalid UTF-8 or control
   characters are ignored with a warning, because what a relative path is
   relative to is unverified. An empty file is ignored silently. Assumed, not
   observed: the path names the user directory itself, not a parent folder.
3. `launcher-settings.json` `gameDataPath`
   (`%USER_DOCUMENTS%/Paradox Interactive/Europa Universalis IV` on
   v1.37.5.0), mapped exactly as for Stellaris.
4. The OS default:

| OS | Default |
| --- | --- |
| Windows | `<Documents>/Paradox Interactive/Europa Universalis IV` (verified) |
| macOS | `~/Documents/Paradox Interactive/Europa Universalis IV` (unverified) |
| Linux | `$XDG_DATA_HOME/Paradox Interactive/Europa Universalis IV` (unverified) |

## launcher-settings.json

Used fields on v1.37.5.0 (validation as described in
[`stellaris.md`](stellaris.md#launcher-settingsjson)):

| Field | Value on v1.37.5.0 | Use |
| --- | --- | --- |
| `gameId` | `eu4` | wrong-game check |
| `rawVersion` | `v1.37.5.0` | `GameInstallation::version`, `supported_version` checks |
| `version` | `EU4 v1.37.5.0 Inca (491d)` | `versionLabel` |
| `gameDataPath` | `%USER_DOCUMENTS%/Paradox Interactive/Europa Universalis IV` | user data |
| `exePath` | `./eu4.exe` | executable |
| `exeArgs` | `["-enabletelemetry", "-gdpr-compliant"]` | default arguments, without `-enabletelemetry` |

## Files

| File | Read | Written (through `LaunchPlan::configWrites` only) |
| --- | --- | --- |
| `<install>/launcher-settings.json` (or `launcher/launcher-settings.json`) | yes | never |
| `<install>/userdir.txt` | yes | never |
| Steam `libraryfolders.vdf`, `appmanifest_236850.acf`, `appworkshop_236850.acf` | yes | never |
| `<library>/steamapps/workshop/content/236850/<id>/descriptor.mod` | yes | never |
| `<user data>/mod/*.mod` | yes | `mod/ugc_<id>.mod` only, when missing or pointing elsewhere, and never over another mod's registration |
| `<user data>/dlc_load.json` | yes | when its content would change; `disabled_dlcs` and unknown keys preserved |
| `<user data>/launcher-v2.sqlite` | **never** | **never** |

## Mods

Identity rules, states and descriptor generation are those of
[`stellaris.md`](stellaris.md#mods). Observed on v1.37.5.0: every
`mod/ugc_<id>.mod` the launcher wrote is `{eu4, steam-workshop, <id>}`, and
the one local mod (absolute `path` into `mod/`, with a `remote_file_id` of
its published copy) stays `{eu4, local, MGBR}`.

**Registered Workshop items without content.** EU4 was reinstalled on the
development machine. Right afterwards the user mod folder still held the 13
registrations and the `dlc_load.json` of the earlier installation, while
Steam had not downloaded any Workshop item yet (`appworkshop_236850.acf`:
`NeedsDownload 1`, nothing installed). Such a registration is not an installed
mod: `scanMods` leaves it out of the inventory and logs it ("A registered
Workshop item is not downloaded", with Steam's view of the item), so that
playsets show the item as missing and the acquisition flow can download it.
`readCurrentConfiguration` keeps the Workshop identity of these
`dlc_load.json` references (from the registration, or from the
`ugc_<id>.mod` name when no descriptor exists), so a playset imported from
the game references them as Workshop entries. A local mod whose folder is
missing stays `Broken`, because it cannot be acquired. Fixture:
`tests/fixtures/games/eu4-reinstalled`; the hidden test replays the state
with the real descriptors.

## Launch

Executable from `launcher-settings.json` `exePath` (`./eu4.exe`, validated as
for Stellaris), falling back to `eu4.exe` / `eu4` / `eu4.app/Contents/MacOS/eu4`
without arguments. The working directory is the installation folder. Steam
installations get `SteamAppId=236850` and `SteamGameId=236850`.

**Arguments**: `exeArgs` without `-enabletelemetry`. The manager never enables
the game's telemetry on the user's behalf; `eu4.exe` lists `enabletelemetry`
among its command-line switches, so leaving it out keeps telemetry at the
game's own default. The flag is matched ASCII case-insensitively and removed
both when the installation is read and when a launch plan is built (for
installations stored earlier). This is silent: no warning is shown. On
v1.37.5.0 the arguments are `["-gdpr-compliant"]`.

Running-instance check: game `eu4.exe` (Linux/macOS `eu4`). Conflicting
writers are the Paradox launcher processes as for Stellaris
(`Paradox Launcher.exe`, `dowser.exe`); `dowser.exe` ships in the EU4 install
folder too, the other names are unverified for EU4.

## Backups, recovery and coexistence

Identical to Stellaris (see [`stellaris.md`](stellaris.md#backups-and-recovery)):
writes go through `platform::FileTransactionManager` with backups, rollback,
crash recovery and `restore()`. The official launcher rewrote `dlc_load.json`
and all registered descriptors at its last start (all dated 2026-08-23
19:52), as it does for Stellaris. This application never touches
`launcher-v2.sqlite`.

## Real-machine evidence

`pmm_games_tests "[.real]" -s` on 2026-10-29 (excerpt, paths redacted; full
output and conclusions in
[`../evidence/real-scan-2026-10-29.md`](../evidence/real-scan-2026-10-29.md)):

```text
version:        v1.37.5.0 (EU4 v1.37.5.0 Inca (491d))
arguments:      1 [-gdpr-compliant]
userdir.txt:    empty
steam_appid.txt: absent
mods:           13
  source local: 1
  source steam-workshop: 12
  state ready: 13
  with scan problems: 0
  with compatibility warnings: 2
dlc_load.json enabled mods: 9
  unresolved: 0
launch plan for the current configuration: 0 write(s)
  environment: SteamAppId=236850
  environment: SteamGameId=236850
regenerated Workshop descriptors identical to the launcher's: 12 of 12
== Europa Universalis IV: replay without Workshop content
  inventory: 1
  dlc_load.json entries: 9
    local installed: 1
    steam-workshop not installed: 8
```

## Outstanding validation

1. Run the probe procedure in [`../load-order.md`](../load-order.md) with
   `--supported-version "v1.37.*.*"` (or the installed version), with the
   user's approval.
2. Launch from the app: confirm that the launcher does not appear, that the
   game does not restart through Steam (no `steam_appid.txt`), that the mods
   are active, and that a legacy zipped mod loads.
3. Observe how EU4 treats a non-empty `userdir.txt` (absolute and relative).
4. Repeat discovery, scan and launch on Linux and macOS.
