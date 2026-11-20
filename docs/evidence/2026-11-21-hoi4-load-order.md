# Hearts of Iron IV load order, 2026-11-21

The in-game probe run for Hearts of Iron IV
([load order](../load-order.md#hearts-of-iron-iv), [validation](../validation.md)
section 3 and step 6): four direct launches by the application with probe mods
only, on 1.20.0.0, on the development machine. **Result: when two enabled
mods contain the same file, the mod whose registration file name
(`mod/<file>.mod`) sorts later wins; the order of `enabled_mods` and the
mods' `name=` do not matter.** That is hypothesis H3 of the procedure and
the rule the HOI4 wiki and a 2018 developer diary describe; it differs from
every other game measured so far (the list order decides in Stellaris,
Crusader Kings III and Victoria 3, `name=` in Europa Universalis IV). The same runs showed that the
game reads the compact `dlc_load.json` the application writes, that a direct
start stays up without the launcher or a restart through Steam, and that
1.20.0.0 still reports `supported_version="1.*.*"` as invalid but not `"1.20"`.
The configuration was restored afterwards and checked (see
[Restore](#restore-and-clean-up)); what the game itself changed is listed
under [Changes made by the game](#changes-made-by-the-game).

| | |
| --- | --- |
| OS | Windows 11 Enterprise 23H2 (10.0.22631), x64 |
| Distribution | Steam (client running and signed in; the same `steam.exe` process before, during and after the runs) |
| Hearts of Iron IV | `Operation Postern v1.20.0.0.8873 (0c4f)` (`rawVersion` 1.20.0.0), Steam build 26733403 (`StateFlags` 4 at the start and at the end), `hoi4.exe` in the installation folder |
| Paradox launcher | not running at any time (no `Paradox Launcher.exe`, `dowser.exe` or `bootstrapper-v2.exe` process) |
| Application | 0.2.0 with the unreleased Victoria 3 and Hearts of Iron IV support: debug build of `main` at `11b73aa` |
| Driver | `pmm-app --profile <scratch>/profile --no-steam --live-test "<steps>"` ([app.md](../app.md#hidden-verification-switches)): the real Backend, the same calls as the buttons; one scratch profile for every launch and restore; Workshop downloads off |
| Observation | the game's own `logs/error.log`, `system.log`, `setup.log` and `game.log` (probe mods, [ADR 0006](../adr/0006-probe-mod-verification.md)); `dlc_load.json` after Play and after the game exited; `Win32_Process` snapshots every 5 s; metadata snapshots of `<userdata>`; the application's transaction journals |

Paths below: `<install>` is `C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV`,
`<userdata>` is `~\Documents\Paradox Interactive\Hearts of Iron IV` (written in
full, `C:\Users\<user>\...`, in the files), and `<scratch>` is a temporary
folder. Times are the machine's local time; the transaction ids are UTC. The
owner's own mods appear as counts only; `<id>` stands for one of their
Workshop ids.

## Summary

| Check | Result |
| --- | --- |
| Direct launch without the Paradox launcher | **pass**: the application started `<install>\hoi4.exe -gdpr-compliant`; no launcher process at any snapshot; the same game process 25 s later in all four runs, with the `pmm-app` process as its parent, so no restart through Steam; 43 active DLC, as in the last launcher-started session |
| `dlc_load.json` written | **pass**: compact JSON with exactly the enabled probes, in playset order, `disabled_dlcs` kept; the game read it (the probes loaded) and did not rewrite it. Run 3 had run 2's selection and wrote nothing |
| Enabled and disabled mods in the game | **pass**: probes a and c loaded, b (disabled) not, in all four runs; `system.log` reports 2 active mods |
| Which mod wins a shared file | **the mod whose registration file name sorts later**: c (`pmm_probe_c.mod` against `pmm_probe_a.mod`) with the order c, a, with a, c, and with a named "PMM Probe Z"; a once its registration was `pmm_probe_z.mod` |
| `supported_version` | `"1.20.*"` accepted; `"1.20"` (probe b, disabled) not reported; `"1.*.*"` reported as invalid for the owner's 4 such registrations, none of them enabled: 1.20.0.0 reads every `mod/*.mod` at start-up |
| Restore | **pass**: `dlc_load.json` byte-identical to the backup (the launcher's pretty layout back), `mod/` identical to before, probes removed |
| Shutdown | all four runs quit within about 3.3 s of `CloseMainWindow`, without a crash (exit code 0 where it was captured) |

## Preparation

1. Baseline: the read-only `pmm_games_tests "[.real]" -s` gave the summary
   recorded in [`../games/hoi4.md`](../games/hoi4.md#real-machine-evidence):
   23 Workshop mods (all Ready, 0 scan problems, 23 with version warnings),
   21 enabled, 0 unresolved, a launch plan with 0 writes, working directory
   `<install>`, arguments `[-gdpr-compliant]`, `SteamAppId` and `SteamGameId`
   394360, 21 of 23 regenerated descriptors identical, the download check
   accepting 23 of 23 Workshop folders.
2. Backup outside the game folders: `dlc_load.json` (677 bytes, the
   launcher's pretty-printed layout, SHA-256
   `296d3fad34d99b9d683ae528ce1d9e7d1d35b8e0ea6751f33fbd8f52b79d20ef`, last
   written 2026-11-18 17:22:21), copies of the 23 `mod/*.mod` files with
   their names, sizes, SHA-256, timestamps and attributes, a copy of `logs/`
   (the 1.19.5.0 session of 2026-11-18), and a metadata snapshot of
   `<userdata>` (names, sizes and timestamps; hashes only of `dlc_load.json`
   and `mod/*.mod`; `playsets_backup/` was not entered).
3. Probes, with Hearts of Iron IV's event syntax (`add_namespace`, UTF-8
   without a BOM, LF line ends):

   ```sh
   python tools/probe-mods/probe_mods.py create "<userdata>/mod" --game hoi4 --supported-version "1.20.*"
   python tools/probe-mods/probe_mods.py create "<userdata>/mod" --game hoi4 --names b --supported-version "1.20"
   ```

   Each probe is a folder `mod/pmm_probe_<x>` (`descriptor.mod`,
   `common/scripted_triggers/pmm_probe_only_<x>.txt` and
   `pmm_probe_shared.txt`, `events/pmm_probe_only_<x>.txt` and
   `pmm_probe_shared.txt`) and a registration `mod/pmm_probe_<x>.mod`. The
   keys line up: probe a has the folder `pmm_probe_a`, the registration
   `pmm_probe_a.mod` and the name "PMM Probe A", and so on. Probes a and c
   declare `supported_version="1.20.*"`, probe b `"1.20"`. Probe a's
   registration:

   ```text
   version="1.0"
   name="PMM Probe A"
   tags={
   	"Utilities"
   }
   supported_version="1.20.*"
   path="C:/Users/<user>/Documents/Paradox Interactive/Hearts of Iron IV/mod/pmm_probe_a"
   ```

   Its shared files name it: `pmm_probe_shared_from_a = yes` in the trigger
   of event `pmm_probe_shared.1` and in the scripted trigger
   `pmm_probe_shared_trigger`.
4. The probe-only playset: the manifest
   `{"schemaVersion":1,"game":"hoi4","playsetName":"PMM probes","mods":[{"source":"local","id":"pmm_probe_c","enabled":true},{"source":"local","id":"pmm_probe_a","enabled":true},{"source":"local","id":"pmm_probe_b","enabled":false}]}`,
   encoded with `python scripts/share-code.py encode <manifest> > probes.code`.

## Run 1: order c, a (b disabled)

`--live-test "game:hoi4,import:<scratch>\probes.code,blockers,dump:<scratch>\run1\dump.json,play"`
into a new scratch profile:

```text
03:15:52.226 game hoi4 selected
03:15:52.304 preview: 3 entries, 2 enabled, 3 installed, 0 missing
03:15:52.314 notification info: Import complete - "PMM probes" was created with 3 entries.
03:15:52.443 imported as playset 1 with 3 entries
03:15:52.462 canPlay=true blockers=[] warnings=[]
03:15:52.466 wrote 3 entries to <scratch>\run1\dump.json
03:15:52.538 pressing Play for playset 1
03:15:52.539 launch state: validating
03:15:52.554 launch state: applying
03:15:52.585 launch state: starting
03:15:52.804 launch state: started
03:15:52.810 notification info: Hearts of Iron IV started - The game was started directly with this playset, without the Paradox launcher.
03:15:52.810 game started, pid 41580
```

The dump equals the manifest (c, a enabled, b disabled). Processes at the
start and 25 s later (`Win32_Process`; parent 48732 is the `pmm-app`
process):

```text
[03:15:53.292] pid=41580 parent=48732 hoi4.exe started=03:15:52.775 cmd="C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe" -gdpr-compliant
[03:16:18.107] pid=41580 parent=48732 hoi4.exe started=03:15:52.775 cmd="C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe" -gdpr-compliant
```

Written configuration (transaction `20261120T231552568Z-0000` at
03:15:52.571; 81 bytes, compact, no trailing newline, SHA-256
`81c6013a56fdc4e4ea28ee0ad39fbbc2fd378d09fa36e30fd952e49f0345325b`; the same
bytes and timestamp after the game had exited):

```json
{"enabled_mods":["mod/pmm_probe_c.mod","mod/pmm_probe_a.mod"],"disabled_dlcs":[]}
```

`error.log` (17 lines; besides those quoted, one start-up line about the DLC
that the launcher-started 1.19.5.0 session had too):

```text
[03:15:56][no_game_date]: Invalid supported_version in  file: mod/ugc_<id>.mod line: 8
[03:15:56][no_game_date]: Invalid supported_version in  file: mod/ugc_<id>.mod line: 9
[03:15:56][no_game_date]: Invalid supported_version in  file: mod/ugc_<id>.mod line: 7
[03:15:56][no_game_date]: Invalid supported_version in  file: mod/ugc_<id>.mod line: 7
[03:16:27][no_game_date]: Invalid trigger 'pmm_probe_loaded_a' in common/scripted_triggers/pmm_probe_only_a.txt line : 2
[03:16:27][no_game_date]: Error: "Unknown trigger-type: pmm_probe_loaded_a, near line: 2" in file: "common/scripted_triggers/pmm_probe_only_a.txt" near line: 3
[03:16:27][no_game_date]: Invalid trigger 'pmm_probe_loaded_c' in common/scripted_triggers/pmm_probe_only_c.txt line : 2
[03:16:27][no_game_date]: Error: "Unknown trigger-type: pmm_probe_loaded_c, near line: 2" in file: "common/scripted_triggers/pmm_probe_only_c.txt" near line: 3
[03:16:27][no_game_date]: Invalid trigger 'pmm_probe_shared_from_c' in common/scripted_triggers/pmm_probe_shared.txt line : 2
[03:16:27][no_game_date]: Error: "Unknown trigger-type: pmm_probe_shared_from_c, near line: 2" in file: "common/scripted_triggers/pmm_probe_shared.txt" near line: 3
[03:16:29][no_game_date]: Invalid trigger 'pmm_probe_loaded_a' in events/pmm_probe_only_a.txt line : 9
[03:16:29][no_game_date]: Error: "Unknown trigger-type: pmm_probe_loaded_a, near line: 9" in file: "events/pmm_probe_only_a.txt" near line: 10
[03:16:29][no_game_date]: Invalid trigger 'pmm_probe_loaded_c' in events/pmm_probe_only_c.txt line : 9
[03:16:29][no_game_date]: Error: "Unknown trigger-type: pmm_probe_loaded_c, near line: 9" in file: "events/pmm_probe_only_c.txt" near line: 10
[03:16:29][no_game_date]: Invalid trigger 'pmm_probe_shared_from_c' in events/pmm_probe_shared.txt line : 9
[03:16:29][no_game_date]: Error: "Unknown trigger-type: pmm_probe_shared_from_c, near line: 9" in file: "events/pmm_probe_shared.txt" near line: 10
```

The four `Invalid supported_version` lines name exactly the owner's four
registrations with `supported_version="1.*.*"`, at their
`supported_version=` lines; none of them was in `enabled_mods`. Nothing names
`pmm_probe_b`.

`system.log` (it starts at 03:15:53, this session's) and `setup.log`:

```text
[03:16:34][no_game_date]: Version: Operation Postern v1.20.0.0.8873 (a5e0) build time: Nov 17 2026 14:21:50
[03:16:34][no_game_date]: Active DLC Count: 43
[03:16:34][no_game_date]: Active Mod Count: 2
...
[03:16:34][no_game_date]: Active Mod: PMM Probe A
[03:16:34][no_game_date]: Active Mod: PMM Probe C
```

```text
[03:16:29][no_game_date]: Events loaded events/pmm_probe_only_a.txt' #1
[03:16:29][no_game_date]: Events loaded events/pmm_probe_only_c.txt' #1
[03:16:29][no_game_date]: Events loaded events/pmm_probe_shared.txt' #1
...
[03:16:41][1936.01.01.12]: Startup time: 48318ms
```

`python tools/probe-mods/probe_mods.py analyze "<userdata>/logs/error.log" --game hoi4 --enabled c a --disabled b`
(exit code 0):

```json
{"enabledOrder":["c","a"],"disabled":["b"],"loaded":["a","c"],"sharedFileWinner":["c"],
 "checks":{"allEnabledLoaded":true,"noDisabledLoaded":true,"singleSharedWinner":true,"precedence":"first-enabled-wins"},"ok":true,
 "systemLog":"system.log","activeMods":{"count":2,"listed":2,"probes":["PMM Probe A","PMM Probe C"],"others":0},
 "invalidSupportedVersion":["Invalid supported_version in  file: mod/ugc_<id>.mod line: 8","... line: 9","... line: 7","... line: 7"]}
```

c won, although it was first in the list. This was the first start of
1.20.0.0 on the machine: it also rebuilt `naval_dist.cache` (03:16:40),
which is why `Startup time` was 48 s here and about 19 s in the later runs.
`game.log` wrote `Executing History from 2.1.1.1 to 1936.1.1.12` at 03:16:36.
After the `Startup time` line the logs stayed unchanged for 60 s (compared
by hash, on copies of the files) and the game used about 0.01 CPU cores.
`CloseMainWindow` at 03:18:30.940; the game had exited by 03:18:34.240,
without a crash. The log copies taken before the close and after the exit
were identical.

## Run 2: order a, c (b disabled), probe a moved to the top in the application

`--live-test "game:hoi4,select:PMM probes,move:1:0,dump:<scratch>\run2\dump.json,blockers,play"`
with the same scratch profile:

```text
03:19:32.361 game hoi4 selected
03:19:32.423 playset "PMM probes" active with 3 entries (2 enabled)
03:19:32.485 move 1:0 saved
03:19:32.487 wrote 3 entries to <scratch>\run2\dump.json
03:19:32.487 canPlay=true blockers=[] warnings=[]
03:19:32.547 pressing Play for playset 1
03:19:32.548 launch state: validating
03:19:32.565 launch state: applying
03:19:32.588 launch state: starting
03:19:32.658 launch state: started
03:19:32.666 notification info: Hearts of Iron IV started - The game was started directly with this playset, without the Paradox launcher.
03:19:32.666 game started, pid 36604
```

The dump lists `pmm_probe_a` (enabled), `pmm_probe_c` (enabled),
`pmm_probe_b` (disabled).

```text
[03:19:33.146] pid=36604 parent=8292 hoi4.exe started=03:19:32.587 cmd="C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe" -gdpr-compliant
[03:19:57.100] pid=36604 parent=8292 hoi4.exe started=03:19:32.587 cmd="C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe" -gdpr-compliant
```

Written configuration (transaction `20261120T231932580Z-0000` at
03:19:32.583; 81 bytes, SHA-256
`ae8acec537be8604e2b7a52703075210c2d3317a50adbf069f2f3b14eb93c7b1`; unchanged
after the exit):

```json
{"enabled_mods":["mod/pmm_probe_a.mod","mod/pmm_probe_c.mod"],"disabled_dlcs":[]}
```

`error.log` has the lines of run 1 with this session's times: the four
`Invalid supported_version` lines at 03:19:35, the probe lines from
`common/scripted_triggers` at 03:19:44 and from `events` at 03:19:45, again
with `pmm_probe_shared_from_c` in both shared files. `system.log` (from
03:19:32) lists `Active Mod: PMM Probe A` and `Active Mod: PMM Probe C` at
03:19:48, with the same `(a5e0)` checksum in its `Version` line; `setup.log`:
`Startup time: 19707ms` at 03:19:52. `analyze ... --game hoi4 --enabled a c
--disabled b` (exit code 0):

```json
{"enabledOrder":["a","c"],"disabled":["b"],"loaded":["a","c"],"sharedFileWinner":["c"],
 "checks":{"allEnabledLoaded":true,"noDisabledLoaded":true,"singleSharedWinner":true,"precedence":"last-enabled-wins"},"ok":true,
 "systemLog":"system.log","activeMods":{"count":2,"listed":2,"probes":["PMM Probe A","PMM Probe C"],"others":0},
 "invalidSupportedVersion":[the same 4 lines as run 1]}
```

**c won again**: the winner did not move with the list, so the list order
does not decide (H1 is refuted: c won as the first and as the last entry).
Quiet for 60 s, idle; `CloseMainWindow` at 03:21:28.240; exited at
03:21:31.432 with exit code 0; the log copies before the close and after the
exit were identical.

## Run 3: order a, c, probe a renamed "PMM Probe Z"

```sh
python tools/probe-mods/probe_mods.py rename "<userdata>/mod" --name a --display Z
```

set `name="PMM Probe Z"` in `pmm_probe_a/descriptor.mod` and in
`mod/pmm_probe_a.mod` (file name and `path=` unchanged). Then
`--live-test "game:hoi4,rescan,select:PMM probes,blockers,play"`:

```text
03:22:06.698 game hoi4 selected
03:22:06.824 notification info: Scan complete - Found 5 installed games with 101 installed mods.
03:22:07.273 rescan finished
03:22:07.335 playset "PMM probes" active with 3 entries (2 enabled)
03:22:07.335 canPlay=true blockers=[] warnings=[]
03:22:07.398 pressing Play for playset 1
...
03:22:07.488 launch state: started
03:22:07.494 game started, pid 48880
```

```text
[03:22:07.966] pid=48880 parent=31552 hoi4.exe started=03:22:07.431 cmd="C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe" -gdpr-compliant
[03:22:32.107] pid=48880 parent=31552 hoi4.exe started=03:22:07.431 cmd="C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe" -gdpr-compliant
```

`dlc_load.json` was not written: run 3 has run 2's `enabled_mods`, so the
file kept run 2's bytes (`ae8acec5…`, timestamp 03:19:32.583) throughout, and
no transaction was created. `error.log`: the probe lines of both folders at
03:22:19, with `pmm_probe_shared_from_c` in both shared files.
`system.log` lists the probes by their new names, sorted:

```text
[03:22:23][no_game_date]: Active Mod Count: 2
...
[03:22:23][no_game_date]: Active Mod: PMM Probe C
[03:22:23][no_game_date]: Active Mod: PMM Probe Z
```

`Startup time: 19340ms` at 03:22:26. `analyze ... --game hoi4 --enabled a c
--disabled b` (exit code 0): `"sharedFileWinner":["c"]`,
`"precedence":"last-enabled-wins"`, `"activeMods":{"count":2,"listed":2,"probes":["PMM Probe C","PMM Probe Z"],"others":0}`,
the same 4 `Invalid supported_version` lines.

**c won again**, although a's name now sorts after C: `name=` does not
decide either (H2 is refuted: c won with the earlier name in runs 1 and 2
and with the later one here). The `Active Mod:` lines follow `name=` (C, Z),
so they show a display order, not the load order. Quiet for 60 s;
`CloseMainWindow` at 03:24:02.886; exited at 03:24:06.124 with exit code 0.
Afterwards `rename ... --name a --display A` set both descriptors back to
`name="PMM Probe A"`; `descriptor.mod` hashed as before the run.

## Run 4: probe a registered as `mod/pmm_probe_z.mod`

```sh
python tools/probe-mods/probe_mods.py rename "<userdata>/mod" --name a --file z
```

moved a's registration `mod/pmm_probe_a.mod` to `mod/pmm_probe_z.mod`; its
`path=` still names the folder `pmm_probe_a`, and its name is "PMM Probe A"
again (the file's SHA-256 is that of the original `pmm_probe_a.mod`). The
application sees it as a new local mod `pmm_probe_z`, so a second manifest
(`"playsetName":"PMM probes z"`: `pmm_probe_z` and `pmm_probe_c` enabled, in
that order, `pmm_probe_b` disabled) was encoded and imported with
`--live-test "game:hoi4,rescan,import:<scratch>\probes-z.code,blockers,play"`:

```text
03:24:52.078 rescan finished
03:24:52.140 preview: 3 entries, 2 enabled, 3 installed, 0 missing
03:24:52.279 imported as playset 2 with 3 entries
03:24:52.295 canPlay=true blockers=[] warnings=[]
03:24:52.358 pressing Play for playset 2
...
03:24:52.428 launch state: started
03:24:52.434 game started, pid 40064
```

```text
[03:24:52.924] pid=40064 parent=41356 hoi4.exe started=03:24:52.399 cmd="C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe" -gdpr-compliant
[03:25:17.114] pid=40064 parent=41356 hoi4.exe started=03:24:52.399 cmd="C:\Program Files (x86)\Steam\steamapps\common\Hearts of Iron IV\hoi4.exe" -gdpr-compliant
```

Written configuration (transaction `20261120T232452392Z-0000` at
03:24:52.396; 81 bytes, SHA-256
`3a1b73d053c2250daa92ecc2d643c96a2537f957da7d93dc1a47b802d0433bbb`; unchanged
after the exit). The list order is run 2's (a first, then c):

```json
{"enabled_mods":["mod/pmm_probe_z.mod","mod/pmm_probe_c.mod"],"disabled_dlcs":[]}
```

`error.log` (the four `Invalid supported_version` lines at 03:24:55, then):

```text
[03:25:04][no_game_date]: Invalid trigger 'pmm_probe_shared_from_a' in common/scripted_triggers/pmm_probe_shared.txt line : 2
[03:25:04][no_game_date]: Error: "Unknown trigger-type: pmm_probe_shared_from_a, near line: 2" in file: "common/scripted_triggers/pmm_probe_shared.txt" near line: 3
[03:25:04][no_game_date]: Invalid trigger 'pmm_probe_shared_from_a' in events/pmm_probe_shared.txt line : 9
[03:25:04][no_game_date]: Error: "Unknown trigger-type: pmm_probe_shared_from_a, near line: 9" in file: "events/pmm_probe_shared.txt" near line: 10
```

(with the `pmm_probe_loaded_a` and `pmm_probe_loaded_c` lines of both folders,
as before). `system.log` lists `Active Mod: PMM Probe A` and `Active Mod:
PMM Probe C` at 03:25:07; its `Version` line now ends in `(269a)` instead of
the `(a5e0)` of runs 1 to 3, which matches a change in the effective files
(a's shared files instead of c's). `Startup time: 18958ms` at 03:25:11.
`analyze ... --game hoi4 --enabled a c --disabled b` (the trigger names in
the folder still say a; exit code 0):

```json
{"enabledOrder":["a","c"],"disabled":["b"],"loaded":["a","c"],"sharedFileWinner":["a"],
 "checks":{"allEnabledLoaded":true,"noDisabledLoaded":true,"singleSharedWinner":true,"precedence":"first-enabled-wins"},"ok":true,
 "systemLog":"system.log","activeMods":{"count":2,"listed":2,"probes":["PMM Probe A","PMM Probe C"],"others":0},
 "invalidSupportedVersion":[the same 4 lines as run 1]}
```

**a won**: compared with run 2, only the registration file name changed
(the list order, `name=`, the folder and `path=` were the same), and the
winner moved. Quiet for 60 s; `CloseMainWindow` at 03:26:43.189; exited at
03:26:46.483 with exit code 0; the log copies before the close and after the
exit were identical. Afterwards `rename ... --name a --file a` moved the
registration back to `mod/pmm_probe_a.mod`.

## Result

| Run | `enabled_mods` | `name=` | Registration files | Loaded | Shared file won by |
| --- | --- | --- | --- | --- | --- |
| 1 | c, a (b disabled) | C, A | `pmm_probe_c.mod`, `pmm_probe_a.mod` | a, c | c |
| 2 (a moved to the top in the application) | a, c (b disabled) | A, C | the same | a, c | **c** (did not move) |
| 3 (a renamed) | a, c (b disabled) | **Z**, C | the same | a, c | **c** (did not move) |
| 4 (a's registration renamed) | a, c (b disabled) | A, C | **`pmm_probe_z.mod`**, `pmm_probe_c.mod` | a, c | **a** (moved) |

`analyze`'s `precedence` label (`first-enabled-wins` in runs 1 and 4,
`last-enabled-wins` in runs 2 and 3) names the winner's place in the list
only; here the list does not decide, so the label is not the rule.

**Conclusion (Hearts of Iron IV 1.20.0.0, started directly):** when two
enabled mods contain the same file, the mod whose registration file name
(the `mod/<file>.mod` named in `enabled_mods`) sorts later wins. The order
of `enabled_mods` does not decide (runs 1 and 2), nor does `name=` (runs 2
and 3), nor the content folder or `path=` (unchanged between runs 2 and 4,
while the winner moved). The direction follows from two comparisons:
`pmm_probe_a.mod` < `pmm_probe_c.mod` gave c, and `pmm_probe_z.mod` >
`pmm_probe_c.mod` gave a (as z). An order by the registrations' timestamps
is ruled out too: in run 3 a's registration had been rewritten after c's
and c still won; in run 4 the move kept a's creation time and a won. In
every run the event file and the scripted-trigger file at the shared path
named the same winner. This is hypothesis H3 of the procedure and what the
HOI4 wiki and the 2018 developer diary describe
([load order](../load-order.md#community-claims-about-precedence-conflicting)).

The Hearts of Iron IV adapter keeps writing the playset order (it is what the
launcher writes, and share codes carry it), as for EU4, and does not rename
registrations to force an order. Its `LoadOrderInfo` now states the measured
rule with `verifiedInGame = true`, and the temporary note above a Hearts of
Iron IV playset is replaced by a permanent one: "Hearts of Iron IV does not
use this order when two mods change the same file: the mod whose .mod file in
the game's mod folder comes last alphabetically wins (ugc_<ID>.mod for a
Workshop mod). The order is still saved, shared and written for the game."

Not settled by these runs (see [Limitations](#limitations)): only two file
names were compared, so another deterministic order of file names cannot be
fully excluded (the order in which NTFS lists these names is the same as the
sorted one); whether the comparison is ordinal or ignores case; how
`ugc_<id>.mod` names of 9- and 10-digit ids compare (as strings,
`ugc_9…` sorts after `ugc_1…`); non-ASCII file names; `dependencies`; and
which mod wins a database entry defined in different files. For Workshop
items, whose registrations are `mod/ugc_<id>.mod`, the rule is inferred,
not measured.

## `supported_version`

| Probe | Pattern | Reported in `error.log` | Loaded |
| --- | --- | --- | --- |
| a, c | `1.20.*` | nothing | yes, in all four runs |
| b (disabled in every run) | `1.20` (two parts, no wildcard) | nothing | no (disabled) |
| the owner's 4 registrations with `1.*.*` (none enabled) | `1.*.*` | `Invalid supported_version in  file: mod/ugc_<id>.mod line: N`, at each one's `supported_version=` line, at every start | no (not enabled) |
| the owner's other 19 registrations (none enabled) | `1.19.*` (11), `1.19.4.2` (2), `1.17.*`, `1.11.*`, `1.19.4.1`, `1.17.3.0`, `1.19.1.0`, `1.19.4.*` | nothing | no (not enabled) |

Started directly, 1.20.0.0 reads every registration in `mod/` at start-up
(the `Invalid supported_version` lines come at 03:15:56, before anything is
loaded), not only those in `enabled_mods`; Crusader Kings III 1.20.1.2,
started the same way, did not. So the silence about probe b means something:
1.20.0.0 does **not** report `"1.20"` as invalid, and the adapter's
assumption (a two-part pattern without a wildcard is valid) holds. Whether
the game counts `"1.20"` as matching 1.20.0.0 is not shown: b was never
loaded, and Hearts of Iron IV logs no match lines. `"1.*.*"` is still
reported as invalid, as by 1.19.5.0, so `rejectsEarlyWildcards = true` stays.
Not tested: `"*"`, `"1.*"` and three-part forms without a wildcard such as
`"1.20.0"`.

## Logs, loading and closing

- Hearts of Iron IV has no `debug.log` and logs no mount lines. Its
  `error.log` lines carry no `[file.cpp:N]` tag. An unknown trigger is
  reported with two lines, `Invalid trigger '<name>' in <file> line : N` and
  `Error: "Unknown trigger-type: <name>, near line: N" in file: "<file>" near
  line: M`, for the probe files in `common/scripted_triggers` and in
  `events` alike (Stellaris, EU4 and Victoria 3 reported only the event
  files). The scripted triggers came first, up to 2 s before the events.
- `system.log` lists `Active Mod Count: N`, then the active DLC and one
  `Active Mod: <name>` line per loaded mod, sorted by `name=`. Its `Version`
  line ends in a checksum in parentheses that changed when the effective
  files changed (run 4).
- Loading is complete at `setup.log`'s `[..][1936.01.01.12]: Startup time:
  Nms` (48.3 s on the first start of 1.20.0.0, then 19.7, 19.3 and 19.0 s);
  `game.log`'s `Executing History ... to 1936.1.1.12` comes 1 to 5 s earlier.
  The probe lines came about 12 s after the start (35 s on the first start).
- Each run waited for the `Startup time` line, then for 60 s without a
  change in the logs, judged by hashing copies of the files, with the game
  at 0.00 to 0.02 CPU cores, then closed it with `CloseMainWindow`. No log
  line reached the disk late: the copies taken before the close and after the
  exit were identical in every run. Hearts of Iron IV writes no quit line.
  Each close took about 3.2 to 3.3 s (the process's exit time; run 1's was
  not captured, and it had exited 3.3 s after the close).

## Direct launch: processes, Steam

- Every 5 s snapshot from shortly before Play until after the game had
  exited (run 1: 03:15:35 to 03:18:49, 38 snapshots, 31 with `hoi4.exe`;
  runs 2 to 4: 28 snapshots each, 24, 23 and 22 with `hoi4.exe`) shows at
  most one `hoi4.exe`, the one the application started, and never a
  `Paradox Launcher.exe`, `dowser.exe`, `bootstrapper-v2.exe`,
  `CrashReporter.exe` or `WerFault.exe` process.
- `hoi4.exe` kept its process id from the start until it was closed, with
  the `pmm-app` process as its parent: it did **not** quit and restart
  through Steam. `steam.exe` kept its process id (started before the
  session) and was not touched.
- The game received only `-gdpr-compliant`, none of the launcher's session
  arguments, and ran in `<install>`, where its `steam_appid.txt` lies, with
  `SteamAppId` and `SteamGameId` set by the application. `system.log`
  reported 43 active DLC in every run, as in the launcher-started 1.19.5.0
  session of 2026-11-18.
- Steam tracked the direct launches: it rewrote
  `steamapps\appmanifest_394360.acf` at 03:26:49 (`LastPlayed`; `buildid`
  still 26733403).

## `dlc_load.json` and the transactions

| When | `enabled_mods` | Size | SHA-256 |
| --- | --- | --- | --- |
| Before the session (written by the launcher, 2026-11-18 17:22) | the owner's 21 Workshop mods, pretty-printed | 677 bytes | `296d3fad…b79d20ef` |
| Run 1, after Play and after the game exited | `pmm_probe_c`, `pmm_probe_a` | 81 bytes | `81c6013a…0345325b` |
| Runs 2 and 3, after Play and after the game exited | `pmm_probe_a`, `pmm_probe_c` | 81 bytes | `ae8acec5…eb93c7b1` |
| Run 4, after Play and after the game exited | `pmm_probe_z`, `pmm_probe_c` | 81 bytes | `3a1b73d0…d0433bbb` |
| After the three restores | the owner's 21, byte-identical to the backup | 677 bytes | `296d3fad…b79d20ef` |

In every run the file's hash and timestamp stayed constant from Play until
after the exit: the game read the compact file the application wrote (the
probes loaded) and never rewrote it. The scratch profile holds three
transactions, one per launch that wrote (runs 1, 2 and 4), each with one
entry (`dlc_load.json`), whose backup hashes to the recorded `backupSha256`;
the chain is `296d…` -> `81c6…` -> `ae8a…` -> `3a1b…`. Run 3 created none
(the application log records an empty transaction id for it).

## Restore and clean-up

- The application's restore (`pmm-app --profile <scratch>/profile --no-steam
  --live-test "game:hoi4,restore"`, run three times, once per launch that
  wrote) undid the launches newest first: `20261120T232452392Z-0000` (run 4)
  at 03:27:19, after which `dlc_load.json` held run 2's bytes, then
  `20261120T231932580Z-0000` (run 2) at 03:27:22 (run 1's bytes), then
  `20261120T231552568Z-0000` (run 1) at 03:27:25. Each printed
  "Configuration restored" and exited with 0; all three journals now read
  `restored`. `dlc_load.json` is byte-identical to the backup: 677 bytes, the
  launcher's pretty layout, SHA-256 `296d3fad…`; only its timestamp changed.
  No manual copy was needed.
- Probe renames were undone after runs 3 and 4 (above).
  `python tools/probe-mods/probe_mods.py remove "<userdata>/mod"` removed
  the folders and registrations of probes a, b and c. `mod/` again holds
  exactly the 23 registrations of the backup, with the same names, sizes,
  SHA-256, timestamps and attributes; only the folder's own timestamp
  changed. Nothing named `pmm_probe*` remains in `<userdata>`.
- At the end no `hoi4.exe`, launcher, `dowser.exe`, `bootstrapper-v2.exe`,
  `CrashReporter.exe`, `WerFault.exe` or `pmm-app` process was running;
  `steam.exe` was still the same process; `buildid` was still 26733403
  (`StateFlags` 4).
- The application log of the scratch profile has INFO lines only (212 lines,
  no warning or error). The application wrote nothing besides
  `dlc_load.json` and its write-access check in `mod/`.
- A separate check of the result repeated the process, file, `mod/` and
  transaction checks with its own scripts, ran an identical copy of
  `probe_mods.py` (`python -I -B`, from a scratch folder) as `analyze --game
  hoi4` on the copied logs of every run, before the close and after the exit
  (the same JSON as above in all 8 cases), and read the logs itself: the same
  results, apart from the timing of the `settings.txt` writes (below).

## Changes made by the game

Comparing metadata snapshots of `<userdata>` before and after (names, sizes,
timestamps; the contents of private files were not read, and
`playsets_backup/` was not entered):

- **Rewritten by the game:** all 20 files in `logs/`, at every start. The
  2026-11-18 session logs (1.19.5.0, quoted in
  [`../games/hoi4.md`](../games/hoi4.md)) are gone from the machine; the
  observations quoted there stand as recorded.
- **Changed:** `settings.txt` (3345 -> 3015 bytes during the first 1.20.0.0
  session; written at every close, as `pdx_settings.txt` is, which kept its
  size); `naval_dist.cache` rebuilt in run 1 (32,381,790 -> 30,200,520 bytes,
  03:16:40) and `naval_dist_checksum.cache` (a new timestamp); new
  timestamps with the same sizes for `career_profile_<id>_v3.pr`,
  `dlc_signature` and `account/PDX/SDK/hoi4/account.json`.
- **Only timestamps:** `dlc_load.json` (restored to identical bytes) and
  `mod/` (the probes added and removed, and the application's write-access
  check at Play).
- **Unchanged** by size and timestamp: `launcher-v2.sqlite`,
  `launcher-v2_backup.sqlite`, `launcher-v2_openbeta.sqlite` (never
  opened), `playsets_backup/`, `continue_game.json`, `save games/`,
  `gameplaysettings.txt`, `game_data.json`, `shadercache/`, `crashes/`
  (empty; no crash folder was created), `dumps/` and `exceptions/`.
- No file was added to or removed from `<userdata>`.
- **Outside `<userdata>`:** Steam's `appmanifest_394360.acf` (above).

## Deviations

- Run 1 had a `dump` step before `play`, which the procedure lists for run 2
  only; it wrote a scratch file.
- Run 1's exit time and exit code were not captured (the process handle was
  opened after the exit); the exit lies between 03:18:30.940 and
  03:18:34.240.
- The `settings.txt` change could not be compared by content: no copy was
  taken before the run.
- The separate check wrote a list of hashes to a temporary file outside its
  scratch folder by mistake and deleted it; nothing under Steam, the game
  folder or `<userdata>` was touched by it.

## Limitations

- One machine, one Steam account, one game version (1.20.0.0), started
  directly only. Whether a launcher-started game applies the same rule was
  not tested (the launcher turns its own load-order controls off for this
  game).
- The probe measures full-file override of `events/` and
  `common/scripted_triggers/` files, with two mods and two file-name
  comparisons. Not tested: case (ordinal or case-insensitive comparison),
  `ugc_` ids of different lengths, non-ASCII file names, `dependencies`,
  `replace_path`, and folders that merge per object. The rule for Workshop
  items (`mod/ugc_<id>.mod`) is inferred from it.
- `supported_version`: only `1.20.*`, `1.20` (unloaded) and the owner's
  registrations' patterns were seen; `*`, `1.*` and `1.20.0` were not
  tried.
- Not covered: Workshop subscribe and download for app 394360, a start with
  Steam closed, DLC over a long time without the launcher, multiplayer
  without the launcher's session arguments, `userdir.txt` and an absolute
  `gameDataPath`, and local mods outside `mod/`.
