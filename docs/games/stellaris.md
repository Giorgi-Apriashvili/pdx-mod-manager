# Stellaris adapter

Steam app 281990, catalog key `stellaris`. Code: `src/games/stellaris.{hpp,cpp}`
(traits) on top of the shared `DlcLoadJsonAdapter`
(`src/games/dlc_load_adapter.{hpp,cpp}`), plus `descriptor`,
`launcher_settings` and `supported_version` in the same folder. Load order:
[`../load-order.md`](../load-order.md).

## Status

Status labels as in [`../feasibility.md`](../feasibility.md). The development
machine runs Windows 11 with Stellaris "Cygnus v4.5.2 (9776)" from Steam. It
was only read; the game was never started by this work. Latest read-only run:
[`../evidence/real-scan-2026-10-29.md`](../evidence/real-scan-2026-10-29.md).

| Capability | Windows / Steam | Linux / Steam | macOS / Steam | Other stores |
| --- | --- | --- | --- | --- |
| Discover installation and user data | **Verified** (read-only) | Implemented, unverified | Implemented, unverified | Not discovered; manual selection, unverified |
| Read `launcher-settings.json` (version, executable, arguments) | **Verified** | Implemented, unverified | Implemented, unverified | Unverified |
| Scan Workshop and local mods, identity rules | **Verified**: 36 mods, 0 problems | Implemented, unverified | Implemented, unverified | Local mods only, unverified |
| Registered Workshop items whose content is missing | **Verified** with a replay of the real descriptors: not installed, read as missing | Same code | Same code | n/a |
| Read current `dlc_load.json` | **Verified**: 17 of 17 resolved | Implemented, unverified | Implemented, unverified | Unverified |
| Generate `dlc_load.json` and `mod/ugc_<id>.mod` | **Verified** byte-identical to the launcher's output (computed, not written) | Implemented, unverified | Implemented, unverified | Unverified |
| Direct launch (bypassing the launcher) | **Verified** 2026-10-30: started directly with `-gdpr-compliant`, no launcher process ([live validation](../evidence/live-validation-2026-10-30.md)) | Implemented, unverified | Implemented, unverified | Unverified, no Steam environment |
| Effective order / precedence in game | **Verified** 2026-10-30: enabled mods load, disabled do not; the later mod in `enabled_mods` wins a shared file ([load order](../load-order.md#stellaris)) | Unverified | Unverified | Unverified |
| `supported_version` warnings incl. the Stellaris wildcard rule | **Verified** against `logs/error.log` | Same code | Same code | Same code |

## Installation discovery

- **Steam** (`discover`): the Steam root comes from `DiscoveryEnvironment`
  (`platform::steam::findSteamRoot()`: the registry on Windows,
  `~/.steam/steam`, `~/.local/share/Steam`, Flatpak and Snap on Linux,
  `~/Library/Application Support/Steam` on macOS). Libraries are read from
  `steamapps/libraryfolders.vdf`; `appmanifest_281990.acf` must report
  "fully installed" (StateFlags bit 4) and its folder must exist. Result:
  `distribution = Steam`, `steamAppId = "281990"`, `steamLibraryPath` = the
  library containing the game.
- **Manual** (`inspectInstallation`): any folder the user selects, validated
  the same way. A folder of the form `<library>/steamapps/common/<name>`
  counts as a Steam installation; anything else is `Manual`. A
  `launcher-settings.json` with another `gameId` is rejected (`WrongGame`),
  and so is a folder with neither usable launcher settings nor the default
  executable.
- **Paradox Store, GOG, Microsoft Store / Xbox app**: no automatic discovery.
  Manual selection should work wherever the folder has the normal layout,
  but it is untested, and there is no Workshop integration for those builds.
  The Xbox app's protected `WindowsApps` folder is not supported.

## User data discovery

| OS | Default | Source |
| --- | --- | --- |
| Windows | `<Documents>/Paradox Interactive/Stellaris` | Known Folder `FOLDERID_Documents` (honours OneDrive redirection); verified |
| macOS | `~/Documents/Paradox Interactive/Stellaris` | unverified |
| Linux | `$XDG_DATA_HOME/Paradox Interactive/Stellaris` (default `~/.local/share/...`) | unverified |

`launcher-settings.json` declares `"gameDataPath":
"%USER_DOCUMENTS%/Paradox Interactive/Stellaris"`. `%USER_DOCUMENTS%` is mapped
to the documents folder (Windows, macOS) or to `$XDG_DATA_HOME` (Linux). Any
other form, `..` components or drive letters are ignored with a warning and
the default is used. A user-chosen folder always wins.

`<install>/userdir.txt` comes before `gameDataPath`, with the same rules as
for EU4 (see [`eu4.md`](eu4.md#user-data-discovery): first non-empty line,
absolute paths only, anything else ignored with a warning). No such file
ships with Stellaris; the basis for honouring it is that `stellaris.exe`
v4.5.2 references `userdir.txt` among its start-up strings, as `eu4.exe` does.
How Stellaris interprets the file is unverified.

## launcher-settings.json

Read from `<install>/launcher-settings.json`, falling back to
`<install>/launcher/launcher-settings.json` (older layout). Used fields on
v4.5.2:

| Field | Value on v4.5.2 | Use |
| --- | --- | --- |
| `gameId` | `stellaris` | wrong-game check |
| `rawVersion` | `v4.5.2` | `GameInstallation::version`, `supported_version` checks |
| `version` | `Cygnus v4.5.2 (9776)` | `versionLabel` |
| `gameDataPath` | `%USER_DOCUMENTS%/Paradox Interactive/Stellaris` | user data |
| `exePath` | `./stellaris.exe` | executable |
| `exeArgs` | `["-gdpr-compliant"]` | default arguments, minus `-enabletelemetry` if ever present |

Validation: wrong types fail the read. `exeArgs` must be a list of at most 64
strings of at most 4096 bytes, without NUL. `exePath` must resolve inside the
installation folder (`platform::isWithin`) to a regular file. On Windows it
must end in `.exe`, so the app never asks Windows to start a script or
document. It must not name the Paradox launcher (`dowser.exe`,
`Paradox Launcher.exe`). An app bundle (`x.app`) resolves to
`x.app/Contents/MacOS/x`. `alternativeExecutables` (the `-nakama`
cross-store multiplayer variant) is ignored.

If the file is missing, the adapter falls back to `stellaris.exe` /
`stellaris` / `stellaris.app/Contents/MacOS/stellaris` with no arguments and
logs a warning. If it is unreadable or names an unusable executable,
discovery does the same, while manual inspection reports the error.

## Files

| File | Read | Written (through `LaunchPlan::configWrites` only) |
| --- | --- | --- |
| `<install>/launcher-settings.json`, `<install>/userdir.txt` | yes | never |
| `<steam>/steamapps/libraryfolders.vdf`, `appmanifest_281990.acf`, `workshop/appworkshop_281990.acf` | yes | never |
| `<library>/steamapps/workshop/content/281990/<id>/descriptor.mod` | yes | never |
| `<user data>/mod/*.mod` | yes | `mod/ugc_<id>.mod` only, when missing or pointing at another folder, and never over another mod's registration |
| `<user data>/dlc_load.json` | yes | when its content would change (the same JSON in another layout is left as is) |
| `<user data>/launcher-v2.sqlite` | **never** | **never** |

Nothing is ever written below a Steam folder or the game installation.

## Mods

**Identity** (`scanMods`):

- A registered `mod/<file>.mod` whose `path` (or `archive`) lies inside any
  `.../steamapps/workshop/content/281990/<id>`, or a file named
  `ugc_<id>.mod` whose `remote_file_id` equals `<id>`, is
  `{stellaris, steam-workshop, <id>}`.
- Any other registered descriptor is `{stellaris, local, <file stem>}`. The
  path decides: all nine local mods on the development machine carry a
  `remote_file_id` (developer copies of published mods) and are correctly
  local.
- A Workshop content folder without a registration is added with an empty
  `descriptorPath` and `descriptorRef = "mod/ugc_<id>.mod"`; that descriptor
  is generated at launch.
- Several registrations of one Workshop item: `ugc_<id>.mod` is used, the
  others are reported as problems.

**Not installed**: a registered Workshop item whose content exists in no Steam
library (for example right after the game was reinstalled, before Steam has
downloaded it) is left out of the inventory and logged for diagnostics
("A registered Workshop item is not downloaded", with Steam's view: download
pending, not subscribed, unknown to Steam). Playsets then show it as missing
and the acquisition flow can download it. `readCurrentConfiguration` still
maps its `dlc_load.json` reference to `{stellaris, steam-workshop, <id>}`
(from the registration, or from the `ugc_<id>.mod` name when no descriptor
exists).

**States**: `Broken` when the Workshop content folder has no usable descriptor,
the registered descriptor cannot be parsed and no usable copy exists, or a
local mod's folder/archive does not exist (local mods cannot be acquired).
`NeedsUpdate` when `appworkshop_281990.acf` lists a newer manifest than the
installed one. `Downloading` when the content is on disk but Steam lists the
subscribed item as not installed (an update in progress). A registration that
points at a stale folder while the content exists in a library stays `Ready`
with a problem note, because the launch rewrites it.
`contentUpdatedAt` comes from the Workshop manifest's `timeupdated`, else the
folder or file modification time. Results are sorted by identity, and one bad
mod never fails the scan.

**Descriptor format** (`games/descriptor.hpp`): `key="value"`, `key=value`,
`key={ "a" "b" }`, `#` comments, BOM, CRLF, `\"` and `\\` escapes. Unknown
keys are kept in file order (unknown lists as `"<key>{}"` entries in
`ModDescriptor::extra`). Invalid UTF-8 becomes U+FFFD. Generated
`ugc_<id>.mod` files keep the Workshop descriptor's keys in the author's
order, drop its stale `path`/`archive`/`remote_file_id` and append
`path="<generic absolute content path>"` and `remote_file_id="<id>"`,
LF-separated, without a trailing newline. That is exactly what the launcher
writes (27 of 27 identical on the development machine). All descriptors there
are LF; whether the launcher normalises CRLF or invalid UTF-8 the same way is
not observed.

**Compatibility warnings**: `supported_version` patterns (`v4.5.*`, `4.4.*`,
`v4.5.2`) are compared with `rawVersion`. Stellaris also logs
`Invalid supported_version` for a wildcard in the major or minor position.
On the development machine that happened for exactly the `*`, `v4.*` and
`v4.*.*` descriptors, and not for `v4.5.*`, `4.0.*`, `3.10.*` or `v4.1.7`.
The adapter reports that as a separate, non-blocking warning.

## Launch

`buildLaunchPlan` validates that the executable exists, that every enabled
mod's content exists, and that local mods have a registered descriptor in
`mod/` (otherwise it fails and names the mod). It then returns:

- executable: `<install>/stellaris.exe` (from `exePath`);
- arguments: `defaultArguments` (`-gdpr-compliant`), passed as an argv vector
  by `platform::startDetached`, never through a shell. Nothing from a share
  code ever reaches the arguments. `-enabletelemetry` (requested by EU4's
  launcher settings, not by Stellaris's) is always removed, silently, so the
  manager never enables the game's telemetry on the user's behalf;
- working directory: the installation folder;
- environment: `SteamAppId=281990` and `SteamGameId=281990` for Steam
  installations. The install folder also contains `steam_appid.txt`. The
  Steam client must be running: bypassing the Paradox launcher does not
  bypass Steam;
- config writes: generated `mod/ugc_<id>.mod` files first, then
  `dlc_load.json`, each only if its content changes. A `mod/ugc_<id>.mod`
  that belongs to another mod (a local mod using that name, or the
  registration of a different item) is never overwritten: the plan fails and
  names the file. Descriptors must lie directly in `mod/`, because
  `dlc_load.json` names the file only.

Running-instance check (`processNames`): game `stellaris.exe` (Linux
`stellaris`); conflicting writers `Paradox Launcher.exe` and `dowser.exe`
(Linux `dowser`, `paradox-launcher`; macOS `Paradox Launcher`). `dowser.exe`
ships in the install folder; the other names are unverified.

## Backups and recovery

Adapters never write. The application applies `configWrites` with
`platform::FileTransactionManager` (`platform/file_transaction.hpp`):

1. a journal records the targets and the hashes of the new contents;
2. existing files (`dlc_load.json`, any `ugc_<id>.mod` being replaced) are
   copied into the transaction folder;
3. targets are replaced atomically. If one write fails, all touched files are
   restored, and files that did not exist are deleted;
4. at the next start, `listIncomplete()` finds a transaction a crash left
   behind and `recover()` undoes it;
5. `restore(txid)` puts back the configuration from before a launch, for
   example to return to the official launcher's last state.

The generated configuration stays in effect until something rewrites
`dlc_load.json`. The game only reads it: on the development machine the
launcher wrote `dlc_load.json` and all 36 `mod/*.mod` files at 15:05:55,
the game logged its start at 15:06:38, and none of them changed afterwards.

## Coexistence with the official launcher

- The launcher keeps its playsets in `launcher-v2.sqlite`, which this
  application never touches. Playsets made here do not appear in the
  launcher, and the reverse holds too (use `readCurrentConfiguration` to
  import the launcher's current selection).
- When the user presses Play in the launcher, it rewrites `dlc_load.json` and
  every registered descriptor from its active playset (observed, see above).
  This application's next launch writes its own order back, touching only
  what differs.
- Generated `ugc_<id>.mod` files are byte-identical to the launcher's own, so
  they never confuse it. `disabled_dlcs` and unknown keys in `dlc_load.json`
  are preserved, so DLC choices made in the launcher survive.
- Close the launcher before pressing Play here; the launcher and `dowser`
  processes are reported as conflicting writers.

## Real-machine evidence

Hidden test `tests/unit/games/real_machine_tests.cpp`, run read-only on
2026-10-29 with `pmm_games_tests "[.real]" -s` (excerpt, paths redacted; the
full output of both games and the conclusions are in
[`../evidence/real-scan-2026-10-29.md`](../evidence/real-scan-2026-10-29.md)):

```text
discovered installations: 1
install:        C:/Program Files (x86)/Steam/steamapps/common/Stellaris
user data:      ~/Documents/Paradox Interactive/Stellaris
executable:     C:/Program Files (x86)/Steam/steamapps/common/Stellaris/stellaris.exe
arguments:      1 [-gdpr-compliant]
version:        v4.5.2 (Cygnus v4.5.2 (9776))
distribution:   steam app 281990
steam library:  C:/Program Files (x86)/Steam
app manifest:   StateFlags 4 (fully installed yes, update required no), build 25664273
workshop acf:   NeedsDownload 0, NeedsUpdate 0, items 27 (27 installed, 0 with a pending update)
userdir.txt:    absent
steam_appid.txt: present
mods:           36
  source local: 9
  source steam-workshop: 27
  state ready: 36
  local mods carrying a remote_file_id: 9
  unregistered Workshop folders: 0
  with scan problems: 0
  with compatibility warnings: 23
dlc_load.json enabled mods: 17
  #1 stellaris:steam-workshop:1880071041 "ASB Ironman"
  #2 stellaris:local:behemoth_transference_delay "Behemoth Transference - Quiet Delay"
  #3 stellaris:steam-workshop:3711052920 "Better Worlds (Gaia and Hydrocentric Jobs)"
  ...
  unresolved: 0
launch plan for the current configuration: 0 write(s)
  arguments:   [-gdpr-compliant]
  environment: SteamAppId=281990
  environment: SteamGameId=281990
regenerated Workshop descriptors identical to the launcher's: 27 of 27
== Stellaris: replay without Workshop content
  inventory: 9
  dlc_load.json entries: 17
    local installed: 7
    steam-workshop not installed: 10
```

"0 write(s)" means the `dlc_load.json` computed for the launcher's current
selection is identical to the one the launcher wrote. The replay copies the
registered descriptors with the Steam library replaced by an empty one: the
Workshop references keep their identity and read as not installed.

## Outstanding validation

1. Run the probe procedure in [`../load-order.md`](../load-order.md) with the
   user's approval and record the precedence result.
2. Launch from the app and confirm that the launcher does not appear and that
   the mods are active in game (spec section 10, step 6).
3. Repeat discovery, scan and launch on Linux and macOS.
