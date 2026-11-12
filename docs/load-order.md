# Load order

How a playset's order becomes the order the game loads mods in, per game, and
what of that is verified. Status labels as in [`feasibility.md`](feasibility.md).

Convention used everywhere in this code base (`core/model.hpp`): **index 0 of
an ordered mod list is the first mod handed to the game.** Which end of the
list wins when two mods contain the same file is a property of the game, not
of this application, and is recorded below only as far as it is verified.

Code: `src/games/dlc_load_adapter.cpp` (`buildLaunchPlan`,
`readCurrentConfiguration`), `LoadOrderInfo` from `GameAdapter::loadOrderInfo()`.

## Summary

| | Stellaris | Europa Universalis IV | Crusader Kings III |
| --- | --- | --- | --- |
| Configuration file | `<user data>/dlc_load.json`, key `enabled_mods` | same (**verified** read-only on v1.37.5.0) | same (**verified** read-only on 1.20.1.2) |
| Playset order -> file | enabled entries in playset order, index 0 = array element 0 | same | same |
| Disabled entries | not written (the launcher writes only enabled ones) | same | same |
| Array order == launcher playset order | **Verified** read-only, 2026-10-29 (alphabetical playset, see below) | **Verified** read-only, 2026-10-29 (hand-made `custom` order, see below) | Not checked (the launcher's database is not read) |
| Game reads every descriptor, enables the listed ones | **Verified** in-game, 2026-10-30: listed probes loaded, the unlisted one not | **Verified** in-game, 2026-10-30 (same) | Seen in the logs of a 1.19 build (`debug.log`: every descriptor `Enabled` or `Disabled`, the enabled ones mounted in `enabled_mods` order); unverified on 1.20.1.2 |
| File override precedence (which mod wins) | **Verified** in-game, 2026-10-30 (4.5.2): **the later mod in `enabled_mods` wins**; moving mods in the app moves the winner | **Verified** in-game, 2026-10-30 (1.37.5): **the order of `enabled_mods` is ignored; the mod whose `name` sorts first wins** | **Not verified**: the CK3 wiki says the lower mod wins, as advice for EU4 did too (see below) |
| `LoadOrderInfo::verifiedInGame` | `true` | `true` | `false` |

`LoadOrderInfo::summary`, Stellaris: "dlc_load.json enabled_mods, index 0
first; when two enabled mods contain the same file, the one later in the list
wins". EU4: "dlc_load.json enabled_mods lists the playset order, but EU4 does
not use it for file overrides: when two enabled mods contain the same file,
the mod whose name sorts first alphabetically wins". Evidence: [live validation](evidence/live-validation-2026-10-30.md).
CK3: "dlc_load.json enabled_mods, index 0 first; which mod wins when two
enabled mods contain the same file is not verified for Crusader Kings III",
with no user note, until the probe run described under
[Crusader Kings III](#crusader-kings-iii) below.

## Mapping (all three games)

`buildLaunchPlan(installation, orderedEnabledMods)` receives only the enabled
entries, in playset order, and writes:

```json
{"enabled_mods":["mod/ugc_1000000001.mod","mod/local_tweaks.mod"],"disabled_dlcs":[]}
```

- Each element is the mod's `descriptorRef`: `mod/<file>.mod` relative to the
  user-data folder. Workshop items use `mod/ugc_<id>.mod`; that descriptor is
  generated when it is missing or points at another folder.
- Every other key of an existing `dlc_load.json` (`disabled_dlcs` and anything
  unknown) is kept with its value and position. A malformed file is rebuilt
  as `{"enabled_mods":[...],"disabled_dlcs":[]}` with a launch warning.
- Output is compact JSON without a trailing newline, the form the launcher
  writes. The file is only written when its content would change: an
  existing file with the same JSON in another layout (BOM, CRLF, indentation,
  `1.50` for `1.5`) is left as it is.
- `readCurrentConfiguration` is the inverse: array order becomes playset
  order, every entry enabled. A reference that matches no installed mod keeps
  the identity its registration implies: `mod/ugc_<id>.mod` (or a descriptor
  pointing into the Workshop content folder) stays the Workshop item `<id>`,
  even when its content is not downloaded, so it can be acquired. Anything
  else becomes a `local` entry named after the reference stem.

## Verified on the development machine (read-only)

Stellaris v4.5.2 (Steam, Windows 11), 2026-10-29. Nothing was written and the
game was not started.

1. **Launcher playset order equals the `dlc_load.json` order.** The launcher's
   private database was opened strictly read-only (SQLite URI
   `mode=ro&immutable=1`, so no lock or journal files). For the active
   playset, the 17 enabled `playsets_mods` rows sorted by `position` (0..16,
   contiguous) have exactly the `mods.gameRegistryId` sequence of
   `enabled_mods` in `dlc_load.json`. Caveat: that playset uses the
   launcher's `loadOrder = 'alphabetical'` mode, so its positions are
   alphabetical by display name. The second playset (`custom` order) was not
   active, so how the launcher writes a hand-made order was not observed
   directly. The application never reads or writes `launcher-v2.sqlite`; this
   was a one-off investigation.
2. **The game parses every descriptor in `mod/` and enables the listed ones.**
   `logs/error.log` of the last game start contains `[dlc.cpp:346]: Invalid
   supported_version in file: mod/ugc_703156866.mod line: 8` and eight similar
   lines, including descriptors that are not in `enabled_mods`.
3. **This application reproduces the launcher's files byte for byte.** The
   hidden test `pmm_games_tests "[.real]"` (see
   [`games/stellaris.md`](games/stellaris.md#real-machine-evidence)) computes
   a launch plan for the current `dlc_load.json`: zero writes, i.e. identical
   output. Regenerating each of the 27 launcher-written `mod/ugc_<id>.mod`
   files from the Workshop `descriptor.mod` gives identical bytes for 27 of 27.

EU4 v1.37.5.0 (Steam, Windows 11), 2026-10-29, same rules (details in
[`evidence/real-scan-2026-10-29.md`](evidence/real-scan-2026-10-29.md)):

1. **Launcher playset order equals the `dlc_load.json` order, for a hand-made
   order too.** `launcher-v2.sqlite`, opened once the same way, has one
   active playset with `loadOrder = 'custom'`; its 9 enabled rows at positions
   0..8 have exactly the sequence of `enabled_mods` (8 Workshop items and one
   local mod, not alphabetical). This closes the Stellaris caveat above for
   the launcher as such: a custom order is written position 0 first.
2. **This application reproduces the launcher's files byte for byte**: the
   launch plan for the current `dlc_load.json` has zero writes, and 12 of 12
   regenerated `mod/ugc_<id>.mod` files are identical.

Crusader Kings III 1.20.1.2 (Steam, Windows 11), 2026-11-13, same rules
(details in [`games/ck3.md`](games/ck3.md#real-machine-evidence)). 1.20.1.2
has not run on the machine; the logs are those of the last run, a 1.19 build,
on 2026-07-06:

1. **The game parses every descriptor in `mod/` and enables the listed ones.**
   `logs/debug.log` lists the 11 registered descriptors as `|Enabled` (9) or
   `|Disabled` (the 2 not in `enabled_mods`), and `logs/error.log` reports
   `Invalid supported_version` for one descriptor.
2. **It mounts the enabled mods in `enabled_mods` order**: the 9 `Mounted
   Data: .../1158310/<id>` lines of `debug.log` follow the array exactly.
   Which mount wins a shared path is not visible there.
3. **This application reproduces the launcher's files**: the launch plan for
   the current `dlc_load.json` has zero writes. 7 of 11 regenerated
   `mod/ugc_<id>.mod` files are identical; the other 4 registrations are
   older than their Workshop content (only `version`, `name` or `tags`
   differ), and the launcher rewrites them at its next start.

## Community claims about precedence (conflicting)

Community sources disagree about which mod wins when several mods contain
the same file, and several of them predate the current launcher:

- "Order follows the `enabled_mods` array, and a mod lower in the list
  overrides the ones above it". This is common modder advice ("put patches
  below the mods they patch").
- "A given relative path is loaded once, from the first mod that defines it",
  and "whether the first or the last mod wins depends on the folder or
  database type" (full-file replacement versus per-object merging).
- Older Stellaris launchers (before 2.4 and the launcher v2) reportedly sorted
  mods by name, reverse alphanumerically, regardless of the user's list order.

Sources:

- https://forum.paradoxplaza.com/forum/threads/files-and-mod-load-order.1145871
- https://admin-forum.paradoxplaza.com/forum/threads/launcher-mod-manager-and-mod-load-order.1183040
- https://admin-forum.paradoxplaza.com/forum/threads/stellaris-mod-load-order.946110

For Crusader Kings III the Paradox-hosted CK3 wiki
(https://ck3.paradoxwikis.com/Modding, read 2026-11-13) states: "Mods are
loaded in order from top to bottom of the playset." and "The mod lower in the
playset will overwrite identical files from above." For one object defined in
several files it describes "Last in Only Served, or LIOS", while "Types in UI
files use FIOS: First In Only Served", and "`events`, `random_events` and
`on_actions` are appended". The third-party validator ck3-tiger models the
same rule (a later mod wins a shared path). This is documentation, not a
measurement: the same "lower overrides" advice circulated for EU4, and EU4
1.37.5 turned out to ignore the order (below).

On 2026-10-29 these pages could not be retrieved from the development
environment (a browser-verification page and HTTP 500). The claims above come
from search-result excerpts of these threads and related Steam discussions,
so they cannot be attributed to individual posts. None of them is official
Paradox documentation. Hence the precedence rule is measured in-game, not
assumed.

## In-game verification procedure

Only run this with the user's explicit approval: it writes probe mods into
the real user mod folder and starts the game.
[`tools/probe-mods/probe_mods.py`](../tools/probe-mods/probe_mods.py) creates
mods whose effect shows up in `logs/error.log`: every probe has a unique file
that proves it loaded, and a file at the same relative path in every probe
(`events/pmm_probe_shared.txt`, and the same under
`common/scripted_triggers`) whose error names the copy that won. Stellaris
and EU4 reported only the event files: unused scripted triggers are not
checked at start-up (observed 2026-10-30). CK3 needs `--game ck3`, which
writes CK3 events (`<ns>.1 = { type = character_event hidden = yes ... }`)
with a UTF-8 BOM; its error.log reported unknown triggers in
`common/scripted_triggers` on a 1.19 build, so either file may name the
winner.

Stellaris (EU4 the same, with its own folder and a `supported_version` such
as `v1.37.*`):

1. Close the Paradox launcher. Note the current `dlc_load.json` (the app's
   transaction backup also keeps it).
2. `python tools/probe-mods/probe_mods.py create "<Documents>/Paradox Interactive/Stellaris/mod" --supported-version "v4.5.*"`
   (do not use `v4.*`: Stellaris rejects wildcards before the patch number).
3. Rescan in the app. Create a playset with probes in the order **c, a** and
   **b disabled**, then press Play. The game is started directly, without the
   launcher.
4. Wait for the main menu, then quit the game.
5. `python tools/probe-mods/probe_mods.py analyze "<Documents>/Paradox Interactive/Stellaris/logs/error.log" --enabled c a --disabled b`
   checks that c and a loaded, b did not, and which probe won the shared file
   (`last-enabled-wins` or `first-enabled-wins`).
6. Repeat with the order **a, c**. If the winner swaps, the list order
   decides; if it does not (EU4), rename one probe (`name=` in both of its
   descriptors) so the names sort the other way round and repeat, to tell
   name order from file or path order.
7. Restore the previous configuration (transaction restore), then
   `probe_mods.py remove ...`.
8. Record the game version, OS, store and the JSON output here. Then set
   `verifiedInGame = true` and the observed rule in the game's traits
   (`loadOrderSummary`, `loadOrderEvidence`).

The app's `--live-test` switch runs steps 3, 5 and 7 through the real
Backend (`select`, `move`, `play`, `restore`; [app.md](app.md)).

Scope: the probe measures full-file override of an event file.
Other folders may merge per object; the community claims say exactly that,
so do not generalise without probing those too.

### Stellaris

Result, 2026-10-30, Stellaris 4.5.2, Windows 11, Steam ([live validation](evidence/live-validation-2026-10-30.md)):

| Run | Probe order in `enabled_mods` | Loaded | Shared file won by |
| --- | --- | --- | --- |
| 1 | c, a (b disabled) | a, c | a (`last-enabled-wins`) |
| 2 | a, c (b disabled) | a, c | c (`last-enabled-wins`) |

The playset order is the effective order: for the same file the later mod in
`enabled_mods` wins.

### Europa Universalis IV

Result, 2026-10-30, EU4 1.37.5.0, Windows 11, Steam ([live validation](evidence/live-validation-2026-10-30.md)):

| Run | Probe order in `enabled_mods` | Probe names | Loaded | Shared file won by |
| --- | --- | --- | --- | --- |
| 1 | c, a (b disabled) | C, A | a, c | a |
| 2 | a, c (b disabled) | A, C | a, c | a |
| 3 | a, c (b disabled) | **Z**, C (probe a renamed) | a, c | c |

EU4 honours which mods are enabled, but not their order: the mod whose `name`
sorts first wins a shared file, whatever its position in `enabled_mods`. The
official launcher writes the same file, so reordering a playset there has the
same (lack of) effect. This application still writes the playset order (it
is what the launcher writes, and share codes carry it) and states the rule in
EU4's `LoadOrderInfo`. Untested: mods with `dependencies`, names that differ
only in case or start with non-ASCII characters.

### Crusader Kings III

**Not run yet** (needs the owner's approval, on 1.20.1.2). Prepared: the
probe tool's `--game ck3`, the steps in [`validation.md`](validation.md)
section 3, and the traits to set afterwards. CK3 notes on the procedure
above:

- Run the read-only `[.real]` scan first, as a baseline
  ([`games/ck3.md`](games/ck3.md#real-machine-evidence)).
- Step 2: `python tools/probe-mods/probe_mods.py create "<Documents>/Paradox Interactive/Crusader Kings III/mod" --game ck3 --supported-version "1.20.*"`.
  Do **not** use `"1.*"`: CK3 reports it as invalid.
- Step 3: make a playset with **only** the probes (c, a, b disabled), so that
  the other mods' errors do not fill `error.log` and the start is faster.
  The same run checks the direct launch: `tasklist /FI "IMAGENAME eq ck3.exe"`
  lists the game, and no `Paradox Launcher.exe`, `dowser.exe` or
  `bootstrapper-v2.exe` process runs; note whether the game stays up with the
  working directory `binaries/` and whether it restarts through Steam.
- Step 4: on the 1.19 run, the trigger errors and `database_conflicts.log`
  came about 4 minutes after the start; wait for the main menu.
- Step 5: `python tools/probe-mods/probe_mods.py analyze "<Documents>/Paradox Interactive/Crusader Kings III/logs/error.log" --enabled c a --disabled b`;
  also check that `error.log` has no `Invalid supported_version` line for a
  probe and that `debug.log` lists probe b as `|Disabled`.
- Step 8: record the evidence under `docs/evidence/<date>-ck3/` and the result
  table here. Then set the traits in `src/games/ck3.cpp`: if **the later mod
  wins**, the Stellaris wording, `loadOrderVerifiedInGame = true` and no user
  note; if **the name that sorts first wins**, the EU4 wording and a
  `loadOrderUserNote`; if neither, stop and report to the owner.

| Run | Probe order in `enabled_mods` | Probe names | Loaded | Shared file won by |
| --- | --- | --- | --- | --- |
| 1 | c, a (b disabled) | C, A | not run | not run |
| 2 | a, c (b disabled) | A, C | not run | not run |
| 3 (only if the winner did not move) | a, c (b disabled) | **Z**, C (probe a renamed) | not run | not run |
