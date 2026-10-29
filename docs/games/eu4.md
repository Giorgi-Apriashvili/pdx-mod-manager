# Europa Universalis IV adapter

Steam app 236850, catalog key `eu4`. Code: `src/games/eu4.{hpp,cpp}` (traits)
on top of the shared `DlcLoadJsonAdapter` (`src/games/dlc_load_adapter.{hpp,cpp}`).
Load order: [`../load-order.md`](../load-order.md).

## Status

**EU4 is not installed on the development machine.** The only first-hand
evidence is leftover output of the Paradox launcher v2 for EU4 v1.37.5.0 in
`Documents/Paradox Interactive/Europa Universalis IV`. That data was read,
never written. Everything else is implemented by analogy with Stellaris and
is **unverified** until the validation below runs on a machine with EU4.

| Capability | Status |
| --- | --- |
| `dlc_load.json` structure (`enabled_mods` as `mod/<file>.mod`, `disabled_dlcs`) | Matches the leftover launcher output (v1.37.5.0); writing it is unverified |
| `mod/ugc_<id>.mod` format (descriptor keys, then `path` and `remote_file_id`, LF, no trailing newline) | Matches the leftover output. Whether the author's key order is kept cannot be checked: the Workshop content is gone |
| Local mod carrying a `remote_file_id` | Present in the leftover data (a developer copy); identity rule as for Stellaris |
| Steam discovery (`appmanifest_236850.acf`) | Implemented, unverified |
| `launcher-settings.json` for EU4 (`gameId "eu4"`, `exePath`, `exeArgs`) | Implemented, unverified: field values are assumed, not observed |
| Executable names (`eu4.exe`, `eu4`, `eu4.app/Contents/MacOS/eu4`) | Unverified |
| Legacy zipped mods (`archive=`) | Implemented and tested with fixtures; unverified in game |
| Direct launch, effective order, precedence | Unverified |
| `supported_version` warnings | Implemented, without the Stellaris wildcard rule. No evidence of such a rule: three leftover EU4 `error.log` files contain no `Invalid supported_version` lines, while the registered descriptors include `v1.37.*` and `v1.37.*.*` |
| Linux / macOS | Unverified |

Not supported: EU4 versions that predate the Paradox launcher v2 and listed
mods in `settings.txt` (`last_mods`) instead of `dlc_load.json`. They are not
detected as such; their mods would simply not be enabled.

## What differs from Stellaris, and what does not

Shared through `GameTraits`: the `dlc_load.json + mod/*.mod` scheme, the
identity rules, descriptor parsing and generation, and Workshop scanning.
Separate per game: app id, user-data folder, executable names,
launcher-settings `gameId`, the `supported_version` wildcard rule
(`rejectsEarlyWildcards = false` for EU4), and the load-order evidence. Only
what the leftover data shows is shared; once EU4 can be tested, any
difference belongs in `eu4.cpp` or in a dedicated adapter class.

EU4-specific behaviour already handled:

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
  `appmanifest_236850.acf` must report "fully installed"; install folder
  `steamapps/common/Europa Universalis IV`.
- **Manual** (`inspectInstallation`): as for Stellaris. A
  `launcher-settings.json` whose `gameId` is not `eu4` is rejected
  (`WrongGame`).
- **Paradox Store, GOG, Microsoft Store**: no automatic discovery; manual
  selection is untested.

## User data discovery

| OS | Default |
| --- | --- |
| Windows | `<Documents>/Paradox Interactive/Europa Universalis IV` (location of the leftover data) |
| macOS | `~/Documents/Paradox Interactive/Europa Universalis IV` (unverified) |
| Linux | `$XDG_DATA_HOME/Paradox Interactive/Europa Universalis IV` (unverified) |

A `gameDataPath` of the form `%USER_DOCUMENTS%/...` in `launcher-settings.json`
is honoured exactly as for Stellaris.

## Files

| File | Read | Written (through `LaunchPlan::configWrites` only) |
| --- | --- | --- |
| `<install>/launcher-settings.json` (or `launcher/launcher-settings.json`) | yes | never |
| Steam `libraryfolders.vdf`, `appmanifest_236850.acf`, `appworkshop_236850.acf` | yes | never |
| `<library>/steamapps/workshop/content/236850/<id>/descriptor.mod` | yes | never |
| `<user data>/mod/*.mod` | yes | `mod/ugc_<id>.mod` only, when missing or pointing elsewhere |
| `<user data>/dlc_load.json` | yes | when its bytes would change; `disabled_dlcs` and unknown keys preserved |
| `<user data>/launcher-v2.sqlite` | **never** | **never** |

## Launch

Executable from `launcher-settings.json` `exePath` (validated as for
Stellaris), falling back to `eu4.exe` / `eu4` / `eu4.app/Contents/MacOS/eu4`
without arguments. Arguments come from `exeArgs` only. The working directory
is the installation folder. Steam installations get `SteamAppId=236850` and
`SteamGameId=236850`. Running-instance check: game `eu4.exe` (Linux/macOS
`eu4`). Conflicting writers are assumed to be the same Paradox launcher
processes as for Stellaris (`Paradox Launcher.exe`, `dowser.exe`), which is
unverified for EU4.

## Backups, recovery and coexistence

Identical to Stellaris (see [`stellaris.md`](stellaris.md#backups-and-recovery)):
writes go through `platform::FileTransactionManager` with backups, rollback,
crash recovery and `restore()`. The official launcher is expected to rewrite
`dlc_load.json` and the registered descriptors when it starts the game, as it
does for Stellaris (unverified for EU4). This application never touches
`launcher-v2.sqlite`.

## Validation procedure (machine with EU4 installed)

1. Run the hidden test, adapted to EU4 (`Eu4Adapter` in place of
   `StellarisAdapter` in `tests/unit/games/real_machine_tests.cpp`), and
   check: one installation, a version from `launcher-settings.json`, every
   `dlc_load.json` entry resolved, a launch plan with 0 writes for the
   launcher's current selection, and regenerated `ugc_<id>.mod` files
   identical to the launcher's.
2. Record the real `launcher-settings.json` fields (`exePath`, `exeArgs`) here
   and adjust the traits if they differ.
3. Run the probe procedure in [`../load-order.md`](../load-order.md) with
   `--supported-version "v1.37.*"` (or the installed version).
4. Launch from the app: confirm that the launcher does not appear, the mods
   are active, and a legacy zipped mod loads.
5. Update the status table with the game version, OS, store and results.
