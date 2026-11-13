# Crusader Kings III load order, 2026-11-13

The in-game probe run for Crusader Kings III
([load order](../load-order.md#crusader-kings-iii), [validation](../validation.md)
section 3 and step 6): two direct launches by the application with probe mods
only, on 1.20.1.2, run with the owner's approval on the development machine.
**Result: when two enabled mods contain the same file, the mod later in
`enabled_mods` wins**, as in Stellaris. The configuration was restored
afterwards and checked (see [Restore](#restore-and-clean-up)); what the game
itself changed in its user-data folder is listed under
[Changes made by the game](#changes-made-by-the-game).

| | |
| --- | --- |
| OS | Windows 11 Enterprise 23H2 (10.0.22631), x64 |
| Distribution | Steam (client running and signed in; the same `steam.exe` process before, during and after the runs) |
| Crusader Kings III | `1.20.1.2 (Crozier)`, Steam build 26046173, `binaries/ck3.exe` |
| Paradox launcher | not running at any time (no `Paradox Launcher.exe`, `dowser.exe` or `bootstrapper-v2.exe` process) |
| Application | 0.1.2 with the unreleased Crusader Kings III support: debug build of `main` at `5013efe` |
| Driver | `pmm-app --profile <scratch>/profile --no-steam --live-test "<steps>"` ([app.md](../app.md#hidden-verification-switches)): the real Backend, the same calls as the buttons; one scratch profile for every session; Workshop downloads off |
| Observation | the game's own `logs/error.log` and `logs/debug.log` (probe mods, [ADR 0006](../adr/0006-probe-mod-verification.md)), `dlc_load.json` after Play and after the game exited, `Win32_Process` snapshots every 5 s, the application's transaction journals |

Paths below: `<install>` is `C:\Program Files (x86)\Steam\steamapps\common\Crusader Kings III`,
`<userdata>` is `~\Documents\Paradox Interactive\Crusader Kings III`, and
`<scratch>` is a temporary folder. Times are the machine's local time; the
transaction ids are UTC. The owner's own mods appear as counts only.

## Summary

| Check | Result |
| --- | --- |
| Direct launch without the Paradox launcher | **pass**: the application started `<install>\binaries\ck3.exe -gdpr-compliant`; no launcher process at any snapshot; the same game process 26 s later in both runs, so no restart through Steam |
| `dlc_load.json` written | **pass**: exactly the enabled probes, in playset order, `disabled_dlcs` kept; the game did not rewrite the file |
| Enabled and disabled mods in the game | **pass**: probes a and c loaded, b (disabled) not, in both runs |
| Which mod wins a shared file | **the later mod in `enabled_mods`**: a with the order c, a; c with the order a, c (moved in the application) |
| Restore | **pass**: `dlc_load.json` byte-identical to the backup, `mod/*.mod` identical to before, probes removed |
| `supported_version="1.20"` (two parts, no wildcard) | **undetermined**: 1.20.1.2 did not report on descriptors outside `enabled_mods` at all |
| Shutdown | run 1 crashed while closing, after the probe evidence had been written; run 2 quit cleanly |

## Preparation

1. Baseline: the read-only `pmm_games_tests "[.real]" -s` gave the summary
   recorded in [`../games/ck3.md`](../games/ck3.md#real-machine-evidence):
   11 Workshop mods, 9 enabled, 0 unresolved, a launch plan with 0 writes,
   working directory `<install>/binaries`, arguments `[-gdpr-compliant]`,
   `SteamAppId` and `SteamGameId` 1158310.
2. Backup outside the game folders: `dlc_load.json` (262 bytes, SHA-256
   `bafe377ee8d98566648f422a9afbe650b56b2f81004e2b7cd24cd19be2db8abd`), the
   11 `mod/*.mod` files with their hashes, sizes and timestamps, and a
   metadata listing (names, sizes, timestamps) of `<userdata>`.
3. Probes, with the CK3 event syntax:

   ```sh
   python tools/probe-mods/probe_mods.py create "<userdata>/mod" --game ck3 --supported-version "1.20.*"
   python tools/probe-mods/probe_mods.py create "<userdata>/mod" --game ck3 --names b --supported-version "1.20"
   ```

   Probes a and c declare `supported_version="1.20.*"`, probe b `"1.20"`, in
   both their `mod/pmm_probe_<x>.mod` and their `descriptor.mod`.
4. The probe-only playset: a manifest (schema 1, game `ck3`, playset
   "PMM CK3 probes": local `pmm_probe_c` enabled, local `pmm_probe_a`
   enabled, local `pmm_probe_b` disabled), encoded with
   `scripts/share-code.py encode` and imported into the scratch profile with
   the `import` step. The scan found 14 CK3 mods (the 11 Workshop items and
   the 3 probes):

   ```text
   08:35:54.381 game ck3 selected
   08:35:54.443 preview: 3 entries, 2 enabled, 3 installed, 0 missing
   08:35:54.566 imported as playset 1 with 3 entries
   08:35:54.588 canPlay=true blockers=[] warnings=[]
   ```

   The dump of the imported playset: `pmm_probe_c` (enabled),
   `pmm_probe_a` (enabled), `pmm_probe_b` (disabled).

## Run 1: order c, a (b disabled)

```text
08:37:32.730 playset "PMM CK3 probes" active with 3 entries (2 enabled)
08:37:32.733 canPlay=true blockers=[] warnings=[]
08:37:32.792 pressing Play for playset 1
08:37:32.794 launch state: validating
08:37:32.812 launch state: applying
08:37:32.832 launch state: starting
08:37:33.360 launch state: started
08:37:33.374 notification info: Crusader Kings III started - The game was started directly with this playset, without the Paradox launcher.
08:37:33.374 game started, pid 35044
```

Processes at the start and 26 s later (`Win32_Process`; the parent of
`ck3.exe` was the `pmm-app` process):

```text
[08:37:33.851] pid=35044 ck3.exe started=08:37:33.337 cmd="C:\Program Files (x86)\Steam\steamapps\common\Crusader Kings III\binaries\ck3.exe" -gdpr-compliant
[08:37:59.468] pid=35044 ck3.exe started=08:37:33.337 cmd="C:\Program Files (x86)\Steam\steamapps\common\Crusader Kings III\binaries\ck3.exe" -gdpr-compliant
```

Written configuration (transaction `20261113T043732823Z-0000`, SHA-256
`81c6013a56fdc4e4ea28ee0ad39fbbc2fd378d09fa36e30fd952e49f0345325b`; the same
bytes after the game had exited):

```json
{"enabled_mods":["mod/pmm_probe_c.mod","mod/pmm_probe_a.mod"],"disabled_dlcs":[]}
```

`debug.log`: the mod list, then the probes mounted after the 27 DLC folders,
in `enabled_mods` order (the list itself is printed alphabetically):

```text
Mod:
PMM Probe A|mod/pmm_probe_a.mod|Enabled
PMM Probe C|mod/pmm_probe_c.mod|Enabled
[08:37:35][D][virtualfilesystem_physfs.cpp:814]: Mounted Data: ~/Documents/Paradox Interactive/Crusader Kings III/mod/pmm_probe_c
[08:37:35][D][virtualfilesystem_physfs.cpp:814]: Mounted Data: ~/Documents/Paradox Interactive/Crusader Kings III/mod/pmm_probe_a
```

`error.log` (the probe lines; the other lines refer to content that was not
loaded, such as saved game rules and coat-of-arms textures, and are not
quoted):

```text
[08:37:57][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_loaded_a, near line: 7" in file: "events/pmm_probe_only_a.txt" near line: 8
[08:37:57][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_loaded_c, near line: 7" in file: "events/pmm_probe_only_c.txt" near line: 8
[08:37:57][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_shared_from_a, near line: 7" in file: "events/pmm_probe_shared.txt" near line: 8
[08:38:01][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_loaded_a, near line: 2" in file: "common/scripted_triggers/pmm_probe_only_a.txt" near line: 2
[08:38:01][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_loaded_c, near line: 2" in file: "common/scripted_triggers/pmm_probe_only_c.txt" near line: 2
[08:38:01][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_shared_from_a, near line: 2" in file: "common/scripted_triggers/pmm_probe_shared.txt" near line: 2
[08:38:14][E][jomini_eventmanager.cpp:368]: Event pmm_probe_a.1 is orphaned
[08:38:14][E][jomini_eventmanager.cpp:368]: Event pmm_probe_c.1 is orphaned
[08:38:14][E][jomini_eventmanager.cpp:368]: Event pmm_probe_shared.1 is orphaned
```

`python tools/probe-mods/probe_mods.py analyze <copy of error.log> --enabled c a --disabled b`:

```json
{"enabledOrder":["c","a"],"disabled":["b"],"loaded":["a","c"],"sharedFileWinner":["a"],
 "checks":{"allEnabledLoaded":true,"noDisabledLoaded":true,"singleSharedWinner":true,"precedence":"last-enabled-wins"},"ok":true}
```

The probes were reported 24 s after the start, and `debug.log` reached the
main menu at 08:38:26 (`Held the main menu 4421 ms for the characters`),
53 s after the start; the 1.19 run had needed about 4 minutes. The game
was closed with `CloseMainWindow` at 08:39:09.990 and **crashed while
closing** (see [Run 1 crash](#run-1-crash-on-exit)).

## Run 2: order a, c (b disabled), probe a moved to the top in the application

```text
08:40:08.892 playset "PMM CK3 probes" active with 3 entries (2 enabled)
08:40:08.970 move 1:0 saved
08:40:08.972 canPlay=true blockers=[] warnings=[]
08:40:09.032 pressing Play for playset 1
08:40:09.034 launch state: validating
08:40:09.056 launch state: applying
08:40:09.069 launch state: starting
08:40:09.092 launch state: started
08:40:09.099 game started, pid 35076
```

The dump before Play: `pmm_probe_a`, `pmm_probe_c`, `pmm_probe_b`
(disabled).

```text
[08:40:09.577] pid=35076 ck3.exe started=08:40:09.067 cmd="C:\Program Files (x86)\Steam\steamapps\common\Crusader Kings III\binaries\ck3.exe" -gdpr-compliant
[08:40:35.111] pid=35076 ck3.exe started=08:40:09.067 cmd="C:\Program Files (x86)\Steam\steamapps\common\Crusader Kings III\binaries\ck3.exe" -gdpr-compliant
```

Written configuration (transaction `20261113T044009060Z-0000`, SHA-256
`ae8acec537be8604e2b7a52703075210c2d3317a50adbf069f2f3b14eb93c7b1`; the same
after the game had exited):

```json
{"enabled_mods":["mod/pmm_probe_a.mod","mod/pmm_probe_c.mod"],"disabled_dlcs":[]}
```

`debug.log` lists the same two mods as `|Enabled` and mounts
`mod/pmm_probe_a`, then `mod/pmm_probe_c`. `error.log`:

```text
[08:40:17][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_loaded_a, near line: 7" in file: "events/pmm_probe_only_a.txt" near line: 8
[08:40:17][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_loaded_c, near line: 7" in file: "events/pmm_probe_only_c.txt" near line: 8
[08:40:17][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_shared_from_c, near line: 7" in file: "events/pmm_probe_shared.txt" near line: 8
[08:40:20][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_loaded_a, near line: 2" in file: "common/scripted_triggers/pmm_probe_only_a.txt" near line: 2
[08:40:20][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_loaded_c, near line: 2" in file: "common/scripted_triggers/pmm_probe_only_c.txt" near line: 2
[08:40:20][E][pdx_persistent_reader.cpp:216]: Error: "Unknown trigger: pmm_probe_shared_from_c, near line: 2" in file: "common/scripted_triggers/pmm_probe_shared.txt" near line: 2
```

`analyze ... --enabled a c --disabled b`:

```json
{"enabledOrder":["a","c"],"disabled":["b"],"loaded":["a","c"],"sharedFileWinner":["c"],
 "checks":{"allEnabledLoaded":true,"noDisabledLoaded":true,"singleSharedWinner":true,"precedence":"last-enabled-wins"},"ok":true}
```

Main menu at 08:40:24, `Setup completion (history loaded)` at 08:40:28.
`CloseMainWindow` at 08:41:15.795; the game logged `Quit: Quit event from
os` and exited at 08:41:17.720, without a crash.

## Result

| Run | Probe order in `enabled_mods` | Probe names | Loaded | Shared file won by |
| --- | --- | --- | --- | --- |
| 1 | c, a (b disabled) | C, A | a, c | a (`last-enabled-wins`) |
| 2 (a moved to the top in the application) | a, c (b disabled) | A, C | a, c | **c** (`last-enabled-wins`) |

**Conclusion (Crusader Kings III 1.20.1.2):** the order of `enabled_mods` is
the effective order; when two enabled mods contain the same file, the later
one wins. The winner moved with the order, and against name order (A sorts
first in both runs, so EU4's rule would have kept a), so the third run of
the procedure (renaming a probe) was not needed. In both runs the event file
and the scripted-trigger file at the same path named the same winner. The
CK3 adapter's `LoadOrderInfo` now states this rule with
`verifiedInGame = true`, and the temporary note above a CK3 playset is
gone. Scope as in [load order](../load-order.md#in-game-verification-procedure):
full-file override of these two folders only.

## Direct launch: processes, working directory, Steam

- Every 5 s snapshot from shortly before Play until the run's logs had
  settled (run 1: 08:37:13 to 08:38:59; run 2: 08:39:54 to 08:40:55), and
  the process check after each close, show no `Paradox Launcher.exe`,
  `dowser.exe` or `bootstrapper-v2.exe` process.
- `ck3.exe` kept its process id from the start until it was closed, with the
  `pmm-app` process as its parent. It did **not** quit and restart through
  Steam. `steam.exe` kept its process id (started before the run) and was
  not touched.
- Working directory: `Win32_Process` does not report it. The application's
  launch plan sets `<install>/binaries` (where `steam_appid.txt` lies; the
  baseline above prints it) and the environment `SteamAppId` and
  `SteamGameId`; with that, the game started and stayed up.
- The game received only `-gdpr-compliant`, none of the launcher's session
  arguments. Both runs mounted 27 DLC folders from `<install>/game/dlc`
  before the mods; the main menu itself was not looked at.

## The `"1.20"` pattern and descriptors outside `enabled_mods`

Expected from the 1.19 logs (see [`../games/ck3.md`](../games/ck3.md#mods)):
`debug.log` lists every registered descriptor as `|Enabled` or `|Disabled`,
and `error.log` reports `Invalid supported_version` for a rejected pattern,
also for descriptors that are not enabled. Observed on 1.20.1.2, started
directly, in both runs:

- `debug.log`'s `Mod:` block lists only the two `enabled_mods` entries. Probe
  b and the 11 registered Workshop descriptors are not listed; there is no
  `|Disabled` line.
- `error.log` has no `Invalid supported_version` line and no line about
  probe b at all, also not for the registered Workshop descriptor with
  `"1.*"` that the 1.19 run reported as invalid.
- Probes a and c (`"1.20.*"`), which the game did read, loaded without a
  `supported_version` message.

So whether 1.20.1.2 accepts the two-part pattern `"1.20"` is **still
unknown**: probe b, disabled in every run, was never read. The adapter keeps
its assumption that `"1.20"` is valid (no warning for it). The 1.19 run
cited in the docs seems to have been started through the Paradox launcher
(its `dlc_load.json` and the launcher's database were written 5 s before its
logs began), while these runs were direct starts, so this run does not tell
whether the game version or the launch path made the difference. Measuring
it would need probe b enabled, which the probe procedure avoids.

## `dlc_load.json` and the transactions

| When | `enabled_mods` | SHA-256 |
| --- | --- | --- |
| Before the session (written by the launcher on 2026-07-06) | 9 Workshop items | `bafe377e…e2db8abd` |
| Run 1, after Play and after the game exited | `mod/pmm_probe_c.mod`, `mod/pmm_probe_a.mod` | `81c6013a…0345325b` |
| Run 2, after Play and after the game exited | `mod/pmm_probe_a.mod`, `mod/pmm_probe_c.mod` | `ae8acec5…eb93c7b1` |
| After the two restores | 9 Workshop items, byte-identical to the backup | `bafe377e…e2db8abd` |

The scratch profile holds the two transactions of the launches, each with
one entry (`dlc_load.json`), whose backups hash to the recorded
`backupSha256`. No `mod/ugc_<id>.mod` was written: the probe playset has no
Workshop item.

## Restore and clean-up

- The application's restore (`--live-test "game:ck3,restore"`, run twice)
  undid the launches in reverse order: `20261113T044009060Z-0000` at
  08:41:53, then `20261113T043732823Z-0000` at 08:42:03. Both journals now
  read `restored`. `dlc_load.json` is byte-identical to the backup (262
  bytes); only its timestamp changed. No manual copy was needed.
- `probe_mods.py remove "<userdata>/mod"` removed the three probe folders and
  descriptors. `mod/` again holds exactly the 11 `ugc_*.mod` files, with the
  names, SHA-256, sizes, timestamps and attributes of the backup; only the
  folder's own timestamp changed.
- Afterwards no `ck3.exe`, launcher or `pmm-app` process was running. Run 1's
  crash reporter (below) was still open and was left for the owner to close.
- The application log of the scratch profile has no warning or error line.

## Run 1 crash on exit

`exception.txt` of the crash folder the game wrote:

```text
Application: Crusader Kings III
Version: 1.20.1.2
Date/Time: 2026-11-13 08:39:12

Unhandled Exception C0000005 (EXCEPTION_ACCESS_VIOLATION) at address 0x00007FF7D6C39EBB
```

The stack has only unnamed `ck3.exe` frames on a worker thread
(`BaseThreadInitThunk`). The sequence: `CloseMainWindow` at 08:39:09.990,
the exception at 08:39:12, `CrashReporter.exe` (child of `ck3.exe`) from
08:39:13.649, `ck3.exe` gone at 08:39:14.443. Run 1's `debug.log` ends at
08:38:26 with the main-menu line: it has no `Setup completion (history
loaded)` line and no `Quit` line, while run 2 logged `history loaded` 4 s
after its main-menu line and `Quit` within the second of its close. Whether
run 1 was closed while the game was still loading its history, or had
stalled, is not established. The probe lines had been written about a
minute before the crash, and the evidence copies of run 1's logs are
byte-identical to the copies the game's crash handler saved.

For the procedure: before closing CK3, wait for the `debug.log` line
`Setup completion (history loaded)`, not only for the main menu and quiet
logs ([load order](../load-order.md#crusader-kings-iii), step 4).

## Changes made by the game

Comparing metadata listings of `<userdata>` before and after (names, sizes,
timestamps; the contents of private files were not read):

- **New:** `crashes/ck3_20261113_083912/` from the run 1 crash, about
  258 MB: `exception.txt`, `meta.yml`, `minidump.dmp` (54 MB), a copy of
  `last_save.ck3` (204 MB), `pdx_settings.txt` and copies of `debug.log`,
  `error.log` and `game.log`. Left in place, as the game's own output.
- **Rewritten by the game at start-up:** `logs/*`. The logs of the last 1.19
  run (2026-07-06) that [`../games/ck3.md`](../games/ck3.md) quotes are
  therefore gone from the machine; the observations quoted there stand as
  recorded.
- **Changed:** `pdx_settings.txt` (6873 to 7113 bytes), `dlc_signature`
  (same size, new timestamp), `account/PDX/SDK/ck3/account.json` and
  `eTags.json` (same sizes, new timestamps), about 2550 new or changed
  `shadercache` entries.
- **Only timestamps:** `dlc_load.json` (restored to identical bytes) and the
  `mod` folder.
- **Unchanged** by size and timestamp: `launcher-v2.sqlite`,
  `launcher-v2_backup.sqlite`, `launcher-v2_openbeta.sqlite` and
  `playsets_backup/` (never opened), the save games and `last_save.ck3`, and
  `mod/*.mod`.

## Deviations

- Run 1's crash was noticed only when run 2 had already been started (the
  monitor reported it in the output that also confirmed run 2's start), so
  one launch followed the crash. No further launch was made: run 3 was not
  needed, and the configuration was restored right after run 2.
- Run 2's log monitor matched run 1's old `error.log` (08:39:54) before the
  new game rewrote it. Run 2's own logs were checked by their timestamps
  before the game was closed.
- The first start of the background monitor was aborted and restarted, before
  any launch, because its list of enabled probes arrived as one string.

## Limitations

- One machine, one Steam account, one game version, started directly only.
- The probe measures full-file override of `events/` and
  `common/scripted_triggers/` files; folders that merge per object
  (`LIOS`/`FIOS`, appended `on_actions`) were not probed.
- Not covered: Workshop subscribe and download for app 1158310, a start with
  Steam closed, how long DLC stays enabled when the launcher is bypassed
  (`dlc_signature`), multiplayer without the launcher's session arguments,
  `game/userdir.txt`, mods with `dependencies`, and names that differ only in
  case or start with non-ASCII characters.
