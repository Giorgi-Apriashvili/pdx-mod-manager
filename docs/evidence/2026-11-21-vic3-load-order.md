# Victoria 3 load order, 2026-11-21

The in-game probe run for Victoria 3
([load order](../load-order.md#victoria-3), [validation](../validation.md)
section 3 and step 6): two direct launches by the application with probe mods
and one installed Workshop item, on 1.15.2, run with the owner's approval
(O1) on the development machine. **Result: when two enabled mods contain the
same file, the mod later in `enabledMods` wins**, as in Stellaris and
Crusader Kings III. The same runs showed that 1.15.2 counts
`supported_game_version` `"*"` as not matching, that a mod without
`game_custom_data` is left out of the game's mod list but still loaded, and
that the absolute Workshop path the application writes is mounted. The
configuration was restored afterwards and checked (see
[Restore](#restore-and-clean-up)); what the game itself changed is listed
under [Changes made by the game](#changes-made-by-the-game).

| | |
| --- | --- |
| OS | Windows 11 Enterprise 23H2 (10.0.22631), x64 |
| Distribution | Steam (client running and signed in; the same `steam.exe` process before, during and after the runs) |
| Victoria 3 | `1.15.2 (Ivan-Chai)`, Steam build 26763837, `binaries/victoria3.exe` |
| Paradox launcher | not running at any time (no `Paradox Launcher.exe`, `dowser.exe` or `bootstrapper-v2.exe` process) |
| Application | 0.2.0 with the unreleased Victoria 3 support: debug build of `main` at `8225f35` |
| Driver | `pmm-app --profile <scratch>/profile --no-steam --live-test "<steps>"` ([app.md](../app.md#hidden-verification-switches)): the real Backend, the same calls as the buttons; one scratch profile for both launches and both restores; Workshop downloads off |
| Observation | the game's own `logs/error.log`, `logs/debug.log` and `debug.1.log` (the log rotates within a session), `dedicated_server.log` and `custom_automated_stats.log` (probe mods, [ADR 0006](../adr/0006-probe-mod-verification.md)); `content_load.json` after Play and after the game exited; `Win32_Process` snapshots every 5 s; the application's transaction journals |

Paths below: `<install>` is `C:\Program Files (x86)\Steam\steamapps\common\Victoria 3`,
`<userdata>` is `~\Documents\Paradox Interactive\Victoria 3` (written in full,
`C:\Users\<user>\...`, in the files), `<scratch>` is a temporary folder and
`<id>` the Workshop id of the one installed Workshop item used. Times are the
machine's local time; the transaction ids are UTC. The owner's own mods
appear as counts only, and the Workshop item is not named.

## Summary

| Check | Result |
| --- | --- |
| Direct launch without the Paradox launcher | **pass**: the application started `<install>\binaries\victoria3.exe -gdpr-compliant`; no launcher process at any snapshot; the same game process 27 s later in both runs, so no restart through Steam; all 18 DLC in the `DLC:` block |
| `content_load.json` written | **pass**: exactly the 7 enabled entries by absolute path, in playset order, the Workshop item as `C:\Program Files (x86)\Steam\steamapps\workshop\content\529340\<id>`; `disabledDLC` and `enabledUGC` kept; no file written in `mod/`; the game did not rewrite the file |
| Enabled and disabled mods in the game | **pass**: the enabled probes and the Workshop item mounted in playset order and their files loaded; b (disabled) in no log line, in both runs |
| Which mod wins a shared file | **the later mod in `enabledMods`**: a with the order c, ..., a; c with the order a, ..., c (moved in the application) |
| `supported_game_version` | `"1.15.*"`, `"1.*"` and `"1.15"` matched 1.15.2; **`"*"` did not match** (and the game listed that mod as out of date); no `Invalid supported_version` line; every version probe loaded |
| `metadata.json` without `game_custom_data` (probe g) | a "Mod metadata read error"; **left out of the `Mod:` block, but mounted in playset position and its files loaded** |
| A Workshop item by its absolute path | **pass**: in the `Mod:` block, mounted in playset position, its version line and its GUI file loaded |
| Restore | **pass**: `content_load.json` byte-identical to the backup, `mod/` identical to before, probes removed |
| Shutdown | both runs quit cleanly about 3 s after `CloseMainWindow`, without a crash |

## Preparation

1. Baseline: the read-only `pmm_games_tests "[.real]" -s` gave the summary
   recorded in [`../games/vic3.md`](../games/vic3.md#real-machine-evidence):
   10 mods (9 Workshop, 1 local), 0 problems, 1 version warning, 1 enabled
   mod (the owner's local mod), 0 unresolved, a launch plan with 0 writes,
   working directory `<install>/binaries`, arguments `[-gdpr-compliant]`,
   `SteamAppId` and `SteamGameId` 529340, the download check accepting 9 of 9
   Workshop folders.
2. Backup outside the game folders: `content_load.json` (183 bytes, SHA-256
   `6e6bf00398581e5b4cd894c6a75faa9f86d01cb17c27e410abfac156b5372697`), a
   listing of `<userdata>/mod` with names, sizes, SHA-256, timestamps and
   attributes (147 entries: the owner's local mod), and metadata listings
   (names, sizes, timestamps) of `<userdata>`.
3. Probes, with Victoria 3's event syntax:

   ```sh
   python tools/probe-mods/probe_mods.py create "<userdata>/mod" --game vic3 --supported-version "1.15.*"
   python tools/probe-mods/probe_mods.py create "<userdata>/mod" --game vic3 --names w1 --supported-version "1.*" --no-shared
   python tools/probe-mods/probe_mods.py create "<userdata>/mod" --game vic3 --names w2 --supported-version "*" --no-shared
   python tools/probe-mods/probe_mods.py create "<userdata>/mod" --game vic3 --names w3 --supported-version "1.15" --no-shared
   python tools/probe-mods/probe_mods.py create "<userdata>/mod" --game vic3 --names g --no-game-custom-data --no-shared --supported-version "1.15.*"
   ```

   Probes a, b and c carry the shared file, the others do not; g's
   `metadata.json` has no `game_custom_data`. Each probe is a folder with
   `.metadata/metadata.json`; there are no `.mod` files.
4. The Workshop item, picked as the procedure says: one installed item
   without warnings in the application, with `supported_game_version`
   `"1.15.*"`, no relationships and `game_custom_data`, holding only
   `metadata.json`, two GUI files and a thumbnail.
5. The probe playset: a manifest (schema 1, game `vic3`, playset "PMM Vic3
   probes": local `pmm_probe_c`, `pmm_probe_w1`, `pmm_probe_w2`,
   `pmm_probe_w3`, `pmm_probe_g`, the Workshop item, local `pmm_probe_a`, all
   enabled, then local `pmm_probe_b` disabled), encoded with
   `scripts/share-code.py encode` into a file. Before run 1 the code was
   imported once, without Play, into a second, throwaway scratch profile (see
   [Deviations](#deviations)): 8 entries, 7 enabled, 8 installed, 0 missing,
   one warning.

## Run 1: order c, w1, w2, w3, g, the Workshop item, a (b disabled)

`--live-test "game:vic3,import:<code file>,blockers,dump:<file>,play"`:

```text
01:08:55.047 game vic3 selected
01:08:55.108 preview: 8 entries, 7 enabled, 8 installed, 0 missing
01:08:55.356 imported as playset 1 with 8 entries
01:08:55.397 canPlay=true blockers=[] warnings=[1 enabled mod has warnings (versions or dependencies); see the mod list.]
01:08:55.399 wrote 8 entries to <scratch>\run1-dump.json
01:08:55.448 pressing Play for playset 1 (warnings: 1 enabled mod has warnings (versions or dependencies); see the mod list.)
01:08:55.450 launch state: validating
01:08:55.493 launch state: applying
01:08:55.527 launch state: starting
01:08:55.650 launch state: started
01:08:55.660 notification info: Victoria 3 started - 1 enabled mod has warnings (versions or dependencies); see the mod list.
01:08:55.660 game started, pid 16936
```

The dump equals the manifest. The one warning is probe g's scan problem (no
`game_custom_data`): the output gives only the count, but in the scratch
profile's database `pmm_probe_g` is the only Victoria 3 mod with a stored
problem, and the application of `8225f35` gave `"1.*"`, `"*"` and `"1.15"` no
warning (see [below](#supported_game_version)).

Processes at the start (snapshot at 01:09:03) and 27 s after the start
(`Win32_Process`; the parent of `victoria3.exe`, pid 42520, was the `pmm-app`
process):

```text
pid=16936 parent=42520 victoria3.exe started=01:08:55.608 cmd="C:\Program Files (x86)\Steam\steamapps\common\Victoria 3\binaries\victoria3.exe" -gdpr-compliant
[01:09:22.820] pid=16936 parent=42520 victoria3.exe started=01:08:55.608 cmd="C:\Program Files (x86)\Steam\steamapps\common\Victoria 3\binaries\victoria3.exe" -gdpr-compliant
```

Written configuration (transaction `20261120T210855503Z-0000` at
01:08:55.508; 833 bytes, SHA-256
`9726da441360e885cc0151493dddb72d4f0bf7ff4fbf19fbb87c4aae05f651e6`; the same
bytes after the game had exited). Two-space indent, LF, no BOM, no trailing
newline; `disabledDLC` and `enabledUGC` empty, as before:

```json
{
  "enabledMods": [
    {
      "path": "<userdata>\\mod\\pmm_probe_c"
    },
    {
      "path": "<userdata>\\mod\\pmm_probe_w1"
    },
    {
      "path": "<userdata>\\mod\\pmm_probe_w2"
    },
    {
      "path": "<userdata>\\mod\\pmm_probe_w3"
    },
    {
      "path": "<userdata>\\mod\\pmm_probe_g"
    },
    {
      "path": "C:\\Program Files (x86)\\Steam\\steamapps\\workshop\\content\\529340\\<id>"
    },
    {
      "path": "<userdata>\\mod\\pmm_probe_a"
    }
  ],
  "disabledDLC": [],
  "enabledUGC": []
}
```

`debug.log` rotated within the session at 01:09:56, so the session's first
part is in `debug.1.log`: the metadata error of probe g, the `DLC:` block (18
DLC), the `Mod:` block, the mounts (after the 18 DLC folders) and the version
lines. The `Mod:` block lists the playset in order without g (and without b);
the mounts include g in its playset position:

```text
[01:08:57][pdx_mod_metadata.cpp:168]: Mod metadata read error. Error: Expected member (game_custom_data), near: {"name":"PMM Probe G","id":"","version":"1.0","supported_game_version":"1.15.*","short_description":"","tags":[],"relationships":[]}
Mod:
PMM Probe C|~\Documents\Paradox Interactive\Victoria 3\mod\pmm_probe_c
PMM Probe W1|~\Documents\Paradox Interactive\Victoria 3\mod\pmm_probe_w1
PMM Probe W2|~\Documents\Paradox Interactive\Victoria 3\mod\pmm_probe_w2
PMM Probe W3|~\Documents\Paradox Interactive\Victoria 3\mod\pmm_probe_w3
<Workshop item>|C:\Program Files (x86)\Steam\steamapps\workshop\content\529340\<id>
PMM Probe A|~\Documents\Paradox Interactive\Victoria 3\mod\pmm_probe_a
[01:08:57][virtualfilesystem_physfs.cpp:460]: Mounted Data: ~/Documents/Paradox Interactive/Victoria 3/mod/pmm_probe_c
[01:08:57][virtualfilesystem_physfs.cpp:460]: Mounted Data: ~/Documents/Paradox Interactive/Victoria 3/mod/pmm_probe_w1
[01:08:57][virtualfilesystem_physfs.cpp:460]: Mounted Data: ~/Documents/Paradox Interactive/Victoria 3/mod/pmm_probe_w2
[01:08:57][virtualfilesystem_physfs.cpp:460]: Mounted Data: ~/Documents/Paradox Interactive/Victoria 3/mod/pmm_probe_w3
[01:08:57][virtualfilesystem_physfs.cpp:460]: Mounted Data: ~/Documents/Paradox Interactive/Victoria 3/mod/pmm_probe_g
[01:08:57][virtualfilesystem_physfs.cpp:460]: Mounted Data: C:/Program Files (x86)/Steam/steamapps/workshop/content/529340/<id>
[01:08:57][virtualfilesystem_physfs.cpp:460]: Mounted Data: ~/Documents/Paradox Interactive/Victoria 3/mod/pmm_probe_a
[01:08:57][dlc.cpp:748]: Mod PMM Probe C () version 1.15.* successfully matched game version 1.15.2.
[01:08:57][dlc.cpp:748]: Mod PMM Probe W1 () version 1.* successfully matched game version 1.15.2.
[01:08:57][dlc.cpp:753]: Mod PMM Probe W2 () version * does not match game version 1.15.2.
[01:08:57][dlc.cpp:748]: Mod PMM Probe W3 () version 1.15 successfully matched game version 1.15.2.
[01:08:57][dlc.cpp:748]: Mod <Workshop item> () version 1.15.* successfully matched game version 1.15.2.
[01:08:57][dlc.cpp:748]: Mod PMM Probe A () version 1.15.* successfully matched game version 1.15.2.
```

`error.log` (the probe lines; besides them it has the metadata error of g, the
`* does not match` line of w2, six "Unlocalized text" lines from the Workshop
item's GUI file at 01:09:09, and one line about a port in the base game's
map, which are not quoted):

```text
[01:10:16][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_a, near line: 7" in file: "events/pmm_probe_only_a.txt" near line: 8
[01:10:16][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_c, near line: 7" in file: "events/pmm_probe_only_c.txt" near line: 8
[01:10:16][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_g, near line: 7" in file: "events/pmm_probe_only_g.txt" near line: 8
[01:10:16][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_w1, near line: 7" in file: "events/pmm_probe_only_w1.txt" near line: 8
[01:10:16][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_w2, near line: 7" in file: "events/pmm_probe_only_w2.txt" near line: 8
[01:10:16][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_w3, near line: 7" in file: "events/pmm_probe_only_w3.txt" near line: 8
[01:10:16][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_shared_from_a, near line: 7" in file: "events/pmm_probe_shared.txt" near line: 8
[01:10:38][jomini_eventmanager.cpp:376]: Event pmm_probe_a.1 is orphaned
[01:10:38][jomini_eventmanager.cpp:376]: Event pmm_probe_c.1 is orphaned
[01:10:38][jomini_eventmanager.cpp:376]: Event pmm_probe_g.1 is orphaned
[01:10:38][jomini_eventmanager.cpp:376]: Event pmm_probe_w1.1 is orphaned
[01:10:38][jomini_eventmanager.cpp:376]: Event pmm_probe_w2.1 is orphaned
[01:10:38][jomini_eventmanager.cpp:376]: Event pmm_probe_w3.1 is orphaned
[01:10:38][jomini_eventmanager.cpp:376]: Event pmm_probe_shared.1 is orphaned
```

`python tools/probe-mods/probe_mods.py analyze "<userdata>/logs/error.log" --game vic3 --enabled c w1 w2 w3 g a --disabled b`
(exit code 1, see [Result](#result)):

```json
{"enabledOrder":["c","w1","w2","w3","g","a"],"disabled":["b"],"loaded":["a","c","g","w1","w2","w3"],"sharedFileWinner":["a"],
 "checks":{"allEnabledLoaded":true,"noDisabledLoaded":true,"singleSharedWinner":true,"precedence":"last-enabled-wins","modBlockIsEnabledOrder":false,"noDisabledInModBlock":true},"ok":false,
 "debugLogs":["debug.5.log","debug.4.log","debug.3.log","debug.2.log","debug.1.log","debug.log"],
 "modBlock":{"probes":["c","w1","w2","w3","a"],"others":1},"mounted":{"probes":["c","w1","w2","w3","g","a"],"others":19},
 "supportedVersionLines":["Mod PMM Probe C () version 1.15.* successfully matched game version 1.15.2.","Mod PMM Probe W1 () version 1.* successfully matched game version 1.15.2.","Mod PMM Probe W2 () version * does not match game version 1.15.2.","Mod PMM Probe W3 () version 1.15 successfully matched game version 1.15.2.","Mod PMM Probe A () version 1.15.* successfully matched game version 1.15.2."],
 "invalidSupportedVersion":[]}
```

`modBlock.others` is the Workshop item; `mounted.others` the 18 DLC folders
and the Workshop item. The probes were reported about 80 s after the start;
the title menu was reached at 01:10:43 (`custom_automated_stats.log`:
`[AUTOMATION_SINGLE_STAT][time-to-reach-title-menu]:[106.821]`;
`debug.log`: `Transition Empty->Game took: 106.57982 seconds`, a line that
reached the file only when the game closed, see
[Loading and closing](#loading-and-closing)). The game was closed with
`CloseMainWindow` at 01:14:16.250; it logged `Quit: Quit event from OS` at
01:14:16 and had exited at 01:14:19.201, without a crash.

## Run 2: order a, w1, w2, w3, g, the Workshop item, c (b disabled), moved in the application

`--live-test "game:vic3,select:PMM Vic3 probes,move:6:0,move:1:6,dump:<file>,blockers,play"`
with the same scratch profile (rows count from 0, b included):

```text
01:15:35.161 game vic3 selected
01:15:35.223 playset "PMM Vic3 probes" active with 8 entries (7 enabled)
01:15:35.301 move 6:0 saved
01:15:35.377 move 1:6 saved
01:15:35.380 wrote 8 entries to <scratch>\run2-dump.json
01:15:35.380 canPlay=true blockers=[] warnings=[1 enabled mod has warnings (versions or dependencies); see the mod list.]
01:15:35.439 pressing Play for playset 1 (warnings: 1 enabled mod has warnings (versions or dependencies); see the mod list.)
01:15:35.441 launch state: validating
01:15:35.458 launch state: applying
01:15:35.480 launch state: starting
01:15:35.553 launch state: started
01:15:35.564 notification info: Victoria 3 started - 1 enabled mod has warnings (versions or dependencies); see the mod list.
01:15:35.564 game started, pid 48004
```

The dump before Play: `pmm_probe_a`, `pmm_probe_w1`, `pmm_probe_w2`,
`pmm_probe_w3`, `pmm_probe_g`, the Workshop item, `pmm_probe_c` (all
enabled), `pmm_probe_b` (disabled).

```text
[01:15:43.226] pid=48004 parent=30888 victoria3.exe started=01:15:35.478 cmd="C:\Program Files (x86)\Steam\steamapps\common\Victoria 3\binaries\victoria3.exe" -gdpr-compliant
[01:16:02.915] pid=48004 parent=30888 victoria3.exe started=01:15:35.478 cmd="C:\Program Files (x86)\Steam\steamapps\common\Victoria 3\binaries\victoria3.exe" -gdpr-compliant
```

Written configuration (transaction `20261120T211535471Z-0000` at
01:15:35.475; 833 bytes, SHA-256
`ba1a356d43d9c8b21fb586189775fc6ae33c576516d9e923afde4ec9239a5758`; the same
after the game had exited): the layout of run 1 with the paths of
`pmm_probe_a`, `_w1`, `_w2`, `_w3`, `_g`, the Workshop item and `pmm_probe_c`,
in that order.

`debug.log` (no rotation within this session) lists PMM Probe A, W1, W2, W3,
the Workshop item and PMM Probe C in its `Mod:` block (g and b absent),
mounts `pmm_probe_a`, `_w1`, `_w2`, `_w3`, `_g`, the Workshop folder and
`pmm_probe_c` after the 18 DLC folders, and repeats run 1's version lines
(w2: `version * does not match game version 1.15.2.`) and g's metadata error
(01:15:36). `error.log`:

```text
[01:16:47][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_a, near line: 7" in file: "events/pmm_probe_only_a.txt" near line: 8
[01:16:47][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_c, near line: 7" in file: "events/pmm_probe_only_c.txt" near line: 8
[01:16:47][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_g, near line: 7" in file: "events/pmm_probe_only_g.txt" near line: 8
[01:16:47][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_w1, near line: 7" in file: "events/pmm_probe_only_w1.txt" near line: 8
[01:16:47][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_w2, near line: 7" in file: "events/pmm_probe_only_w2.txt" near line: 8
[01:16:47][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_loaded_w3, near line: 7" in file: "events/pmm_probe_only_w3.txt" near line: 8
[01:16:47][pdx_persistent_reader.cpp:268]: Error: "Unknown trigger type: pmm_probe_shared_from_c, near line: 7" in file: "events/pmm_probe_shared.txt" near line: 8
```

followed at 01:17:10 by the seven `Event pmm_probe_<x>.1 is orphaned` lines
of run 1. `analyze ... --game vic3 --enabled a w1 w2 w3 g c --disabled b`
(exit code 1):

```json
{"enabledOrder":["a","w1","w2","w3","g","c"],"disabled":["b"],"loaded":["a","c","g","w1","w2","w3"],"sharedFileWinner":["c"],
 "checks":{"allEnabledLoaded":true,"noDisabledLoaded":true,"singleSharedWinner":true,"precedence":"last-enabled-wins","modBlockIsEnabledOrder":false,"noDisabledInModBlock":true},"ok":false,
 "modBlock":{"probes":["a","w1","w2","w3","c"],"others":1},"mounted":{"probes":["a","w1","w2","w3","g","c"],"others":19},
 "invalidSupportedVersion":[]}
```

(`debugLogs` and `supportedVersionLines` as in run 1, with A and C swapped.)
Title menu at 01:17:13 (`time-to-reach-title-menu` 97.362 s; `Transition
Empty->Game took: 97.21 seconds`). `debug.log` had the same size, 245,223
bytes, at 01:17:10 and in the copy taken at 01:18:33, and the game was idle
at 01:18:25. `CloseMainWindow` at 01:18:33.322; the game logged `Quit: Quit
event from OS` at 01:18:33 and had exited at 01:18:36.174, without a crash.
Run 2's logs were checked by their timestamps (01:15:35 onwards), not by
name.

## Result

| Run | Probe order in `enabledMods` | Probe names | Loaded | Shared file won by |
| --- | --- | --- | --- | --- |
| 1 | c, w1, w2, w3, g, the Workshop item, a (b disabled) | C, ..., A | a, c, g, w1, w2, w3, and the Workshop item | a (`last-enabled-wins`) |
| 2 (a and c swapped in the application) | a, w1, w2, w3, g, the Workshop item, c (b disabled) | A, ..., C | the same | **c** (`last-enabled-wins`) |

**Conclusion (Victoria 3 1.15.2):** the order of `enabledMods` is the
effective order for whole files; when two enabled mods contain the same
file, the later one wins. The winner moved with the order, and against name
order (A sorts first in both runs, so EU4's rule would have kept a); the
folder paths were the same in both runs, so path order is ruled out too, and
the third run of the procedure (renaming a probe) was not needed. The
`Mod:` block and the mounts follow the playset order. The Victoria 3
adapter's `LoadOrderInfo` now states this rule with `verifiedInGame = true`,
and the temporary note above a Victoria 3 playset is gone.

`analyze` exited with 1 (`ok: false`) in both runs only because
`modBlockIsEnabledOrder` is false: probe g is left out of the `Mod:` block
(below). The procedure expects that; the run is judged by a and c (loaded,
and in the `Mod:` block in playset order), by b (in neither) and by
`sharedFileWinner`, and all of these pass.

Scope as in [load order](../load-order.md#in-game-verification-procedure):
full-file override of `events/` only. Victoria 3 reported only the event
files (as Stellaris and EU4 did): the `common/scripted_triggers` probe
files produced no error line. Its wording is `Unknown trigger type:`, where
CK3 writes `Unknown trigger:`. Which definition of a database entry wins is
a different rule since 1.12 ([Victoria 3 adapter](../games/vic3.md#load-order))
and was not probed.

## `supported_game_version`

| Probe | Pattern | Version line in `debug.log` | Loaded |
| --- | --- | --- | --- |
| a, c, the Workshop item | `1.15.*` | `successfully matched game version 1.15.2.` (`dlc.cpp:748`) | yes |
| w1 | `1.*` | `successfully matched game version 1.15.2.` | yes |
| w3 | `1.15` (two parts, no wildcard) | `successfully matched game version 1.15.2.` | yes |
| w2 | `*` | `does not match game version 1.15.2.` (`dlc.cpp:753`, also in `error.log`) | yes: in the `Mod:` block, mounted, its event file parsed |
| g | `1.15.*` | none (the metadata read error comes first) | its files, yes (below) |
| b (disabled) | `1.15.*` | none | no |

At the title menu, in both runs (01:10:43 and 01:17:13),
`dedicated_server.log` reports w2:

```text
[01:10:43][jominiapplication.cpp:539]: ================ Unsupported Mods Detected ================
[01:10:43][jominiapplication.cpp:539]: Some of your mods are for a different version of Victoria 3. These mods may not work correctly and could cause problems with your game. For stability, we recommend disabling these mods until they are updated.
The following mods are out of date:
• PMM Probe W2 (supports *)
[01:10:43][jominiapplication.cpp:539]: ===========================================================
```

That is presumably the notice the game shows in its main menu; the main menu
was not looked at (no screenshots). No `Invalid supported_version` line
appeared in `error.log` or any debug log in either run.

So Victoria 3 1.15.2, started directly, has **no early-wildcard rule**: it
accepts `"1.*"` (unlike Stellaris and CK3) and the two-part `"1.15"`, and
reports no pattern as invalid; `rejectsEarlyWildcards = false` is right. But
it does **not** count a lone `"*"` as a match, while the application of
`8225f35` did (no warning). The adapter now warns about `"*"` for Victoria 3
(`GameTraits::loneWildcardMismatches`): "Victoria 3 does not count
supported_game_version "*" as matching the installed version 1.15.2 and
lists the mod as out of date." Not tested: `"1.*.*"`, `"*.*"`, `"1"`, and
what the Paradox launcher shows for these patterns.

## A mod without `game_custom_data`

Probe g (`metadata.json` without `game_custom_data`, enabled in both runs):

- At start-up, in `debug.log` and `error.log`:
  `[pdx_mod_metadata.cpp:168]: Mod metadata read error. Error: Expected member (game_custom_data), near: {"name":"PMM Probe G",...}`
  (01:08:57 and 01:15:36).
- g is left out of the `Mod:` block and gets no version line.
- But its folder is still mounted in its playset position (`Mounted Data:
  <userdata>/mod/pmm_probe_g`, between w3 and the Workshop item), and its
  event file was parsed (`Unknown trigger type: pmm_probe_loaded_g` in
  `events/pmm_probe_only_g.txt`, `Event pmm_probe_g.1 is orphaned`).

So the game drops such a mod from its mod list, but mounts and loads its
files. The application showed g as Ready with the problem "Victoria 3
reportedly leaves such a mod out", which was only partly right. It stays
Ready (the game does load it; making it Broken would refuse a mod the game
uses), and its problem now says what the game does: "... has no
"game_custom_data" entry; Victoria 3 reports a read error and leaves the mod
out of its mod list, but still loads its files."

## The Workshop entry

The application wrote the Workshop item as
`C:\Program Files (x86)\Steam\steamapps\workshop\content\529340\<id>`: the
casing on disk and in `libraryfolders.vdf`. Steam's registry `SteamPath` is
lower-case (`c:/program files (x86)/steam`) and was not used. In both runs
the game listed the item in the `Mod:` block as
`<Workshop item>|C:\Program Files (x86)\Steam\steamapps\workshop\content\529340\<id>`,
mounted `C:/Program Files (x86)/Steam/steamapps/workshop/content/529340/<id>`
in its playset position (after g, before a or c), logged
`version 1.15.* successfully matched game version 1.15.2.` for it, and
loaded its GUI file (six "Unlocalized text" lines from it in `error.log`).
The absolute Workshop path the application writes works; no file in `mod/`
was written for the item, and the application gave it no warning.

## Direct launch: processes, working directory, Steam

- Every 5 s snapshot from shortly before Play until after the game had
  exited (run 1: 01:08:40 to 01:14:26, 69 snapshots; run 2: 01:15:25 to
  01:18:44, 40 snapshots) shows at most one `victoria3.exe`, the one the
  application started, and no `Paradox Launcher.exe`, `dowser.exe`,
  `bootstrapper-v2.exe`, `CrashReporter.exe` or `WerFault.exe` process. `content_load.json`
  changed only at the application's writes.
- `victoria3.exe` kept its process id from the start until it was closed,
  with the `pmm-app` process as its parent. It did **not** quit and restart
  through Steam. `steam.exe` kept its process id (started before the
  session) and was not touched. The game had one window (`SDL_app`, titled
  "Victoria 3") in the foreground and no dialog window.
- Working directory: `Win32_Process` does not report it. The application's
  launch plan sets `<install>/binaries`; the game mounted
  `<install>/clausewitz`, `jomini`, `platform_specific_game_data` and `game`
  as its first `debug.log` lines, so it found its folders from there. It ran
  without a `steam_appid.txt` (there is none), with `SteamAppId` and
  `SteamGameId` set by the application.
- The game received only `-gdpr-compliant`, none of the launcher's session
  arguments. Both runs list 18 DLC in the `DLC:` block and mount 18 DLC
  folders before the mods.
- Steam tracked the direct launches: it rewrote
  `steamapps\appmanifest_529340.acf` at 01:18:38 (`LastPlayed` is run 2's
  exit; `buildid` still 26763837).

## Loading and closing

CK3's `Setup completion (history loaded)` line has a counterpart in
Victoria 3's `debug.log`: `[pdx_transition_task.cpp:328]: Transition
Empty->Game took: N seconds`, logged when the title menu is reached. But
`debug.log` reaches the disk late: the copies taken before closing (run 1 at
01:13:59, run 2 at 01:18:33.297) do not have that line or anything after
it; it appears only in the copies taken after the game had exited. Written
at once, in the same second, are `custom_automated_stats.log`
(`[AUTOMATION_SINGLE_STAT][time-to-reach-title-menu]:[N]`) and
`dedicated_server.log`. Run 2 therefore waited for the
`custom_automated_stats.log` line and then for 60 s without a change in the
logs, judged from copies of the files: the monitor's sizes, read from the
folder listing, lagged (it showed `debug.log` as 0 bytes at 01:17:02 and
01:17:17). Closed this way, both runs quit within 3 s.

## `content_load.json` and the transactions

| When | `enabledMods` | Size | SHA-256 |
| --- | --- | --- | --- |
| Before the session (last written 2026-11-18 18:16) | the owner's local mod | 183 bytes | `6e6bf003…b5372697` |
| Run 1, after Play and after the game exited | c, w1, w2, w3, g, the Workshop item, a | 833 bytes | `9726da44…05f651e6` |
| Run 2, after Play and after the game exited | a, w1, w2, w3, g, the Workshop item, c | 833 bytes | `ba1a356d…239a5758` |
| After the two restores | the owner's local mod, byte-identical to the backup | 183 bytes | `6e6bf003…b5372697` |

The scratch profile holds the two transactions of the launches, each with
one entry (`content_load.json`), whose backups hash to the recorded
`backupSha256` and whose `newSha256` is the written file of the run.

## Restore and clean-up

- The application's restore (`pmm-app --profile <scratch>/profile --no-steam
  --live-test "game:vic3,restore"`, run twice) undid the launches in reverse
  order: `20261120T211535471Z-0000` (run 2) at 01:19:11, after which
  `content_load.json` held run 1's bytes, then `20261120T210855503Z-0000`
  (run 1) at 01:19:21. Both journals now read `restored`.
  `content_load.json` is byte-identical to the backup (183 bytes); only its
  timestamp changed. No manual copy was needed.
- `python tools/probe-mods/probe_mods.py remove "<userdata>/mod" --game vic3 --names a b c w1 w2 w3 g`
  removed the seven probe folders. `mod/` again holds exactly the 147
  entries of the backup listing, with the same names, sizes, SHA-256,
  timestamps and attributes; only the folder's own timestamp changed.
- At 01:20:05 no `victoria3.exe`, launcher, `dowser.exe`,
  `bootstrapper-v2.exe`, `CrashReporter.exe` or `pmm-app` process was
  running; `steam.exe` was still the same process.
- The application log of the scratch profile has INFO lines only. The
  throwaway preview profile has no transaction.
- A separate check of the result (01:22 to 01:29) repeated the process,
  file, `mod/` and transaction checks with its own scripts, ran `analyze`
  again on copies of the logs (the same JSON, apart from the list of debug
  logs found next to the copy) and read the logs itself: the same results.

## Changes made by the game

Comparing metadata listings of `<userdata>` before and after (names, sizes,
timestamps; the contents of private files were not read):

- **Rewritten by the game:** `logs/` (57 entries changed). Each log family
  rotated at each start, and `debug.log` also within run 1. The oldest logs
  rotated out and are gone from the machine: those of 2026-11-07 to
  2026-11-09, and the debug logs of the 2026-11-18 sessions (17:13 and
  18:16) that [`../games/vic3.md`](../games/vic3.md) quotes; the
  observations quoted there stand as recorded.
- **Changed:** `pdx_settings.json` (rewritten at each start, the same size,
  2775 bytes, a new timestamp), six new files in `shadercache/dx11` (two
  pairs in `ps_5_0` and one in `vs_5_0`, written 01:10:40 to 01:10:42) and
  those two folders' timestamps.
- **Only timestamps:** `content_load.json` (restored to identical bytes),
  `mod/` (the probes added and removed, and the application's write-access
  check at Play, which creates and deletes a temporary file in `mod/` and in
  `<userdata>`), `logs/` and `<userdata>` itself.
- **Unchanged** by size and timestamp: `launcher-v2.sqlite`,
  `launcher-v2_backup.sqlite`, `launcher-v2_openbeta.sqlite` (never
  opened), `playsets_backup/`, `save games/`, `continue_game.json`,
  `dlc_signature` (unlike CK3, not touched), `game_data.json`, `crashes/`
  (empty; no crash folder was created), `exceptions/` and `dumps/`.
- **Outside `<userdata>`:** Steam's `appmanifest_529340.acf` (above).

## Deviations

- Before run 1 the share code was imported once more, without Play, into a
  separate throwaway scratch profile (`--no-steam`, `--live-test
  "game:vic3,import:<code file>,blockers,dump:<file>"`), to confirm that
  the chosen Workshop item had no warning. It wrote no game file.
- Run 1 had a `dump` step before `play`, which the procedure does not list;
  the restores also ran with `--no-steam`.
- The metadata listing of `<userdata>` taken before the run went one level
  into `playsets_backup/` (the folder and one file's name, size and
  timestamp; no contents were read), although the launcher's files are not
  to be opened.
- The background monitor's "logs quiet" judgement used folder listings,
  whose sizes lag (above); run 2's close was decided from file copies.
- The separate check loaded `probe_mods.py` as a Python module, which wrote
  a `__pycache__` folder into the repository's `tools/probe-mods`; it was
  deleted right away. Nothing under Steam, the game folder or `<userdata>`
  was touched by that check.

## Limitations

- One machine, one Steam account, one game version (1.15.2), started
  directly only.
- The probe measures full-file override of `events/` files. Database
  entries, which since 1.12 keep their first definition unless a mod uses
  `REPLACE:` or `INJECT:`, `game_custom_data.replace_paths`, and folders
  that merge per object were not probed.
- `supported_game_version` patterns other than `1.15.*`, `1.*`, `1.15` and
  `*` were not tried; the in-game "Unsupported Mods Detected" notice was seen
  in `dedicated_server.log` only.
- Not covered: Workshop subscribe and download for app 529340, a start with
  Steam closed, DLC over a long time without the launcher, multiplayer
  without the launcher's session arguments, `game/userdir.txt`, zipped
  local mods, mods with `relationships`, paths with non-ASCII characters,
  and names that differ only in case.
