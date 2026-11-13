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
| Game enables the listed mods only | **Verified** in-game, 2026-10-30: listed probes loaded, the unlisted one not; every descriptor in `mod/` is parsed | **Verified** in-game, 2026-10-30 (same) | **Verified** in-game, 2026-11-13 (1.20.1.2): listed probes loaded, the unlisted one not, mounted in `enabled_mods` order. Started directly, 1.20.1.2 lists only the `enabled_mods` entries in `debug.log`; a 1.19 build, apparently started through the launcher, had listed every descriptor as `Enabled` or `Disabled` |
| File override precedence (which mod wins) | **Verified** in-game, 2026-10-30 (4.5.2): **the later mod in `enabled_mods` wins**; moving mods in the app moves the winner | **Verified** in-game, 2026-10-30 (1.37.5): **the order of `enabled_mods` is ignored; the mod whose `name` sorts first wins** | **Verified** in-game, 2026-11-13 (1.20.1.2): **the later mod in `enabled_mods` wins**; moving mods in the app moves the winner |
| `LoadOrderInfo::verifiedInGame` | `true` | `true` | `true` |

`LoadOrderInfo::summary`, Stellaris and CK3: "dlc_load.json enabled_mods,
index 0 first; when two enabled mods contain the same file, the one later in
the list wins". EU4: "dlc_load.json enabled_mods lists the playset order, but
EU4 does not use it for file overrides: when two enabled mods contain the
same file, the mod whose name sorts first alphabetically wins". Evidence:
[live validation](evidence/live-validation-2026-10-30.md) for Stellaris and
EU4, [CK3 probe run](evidence/2026-11-13-ck3-load-order.md) for CK3. Only
EU4 has a user note, which the mods pane shows above an EU4 playset; CK3's
temporary "not yet verified" note was removed after its probe run.

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
(details in [`games/ck3.md`](games/ck3.md#real-machine-evidence)). At the
time of this read-only check 1.20.1.2 had not run on the machine; the logs
were those of the last run, a 1.19 build, on 2026-07-06 (the probe run below
has since overwritten them):

1. **The 1.19 build parsed every descriptor in `mod/` and enabled the listed
   ones.** `logs/debug.log` lists the 11 registered descriptors as
   `|Enabled` (9) or `|Disabled` (the 2 not in `enabled_mods`), and
   `logs/error.log` reports `Invalid supported_version` for one descriptor.
   1.20.1.2, started directly by the probe run, listed only the enabled
   mods and reported nothing about the others.
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
1.37.5 turned out to ignore the order (below). For CK3 the probe run of
2026-11-13 confirmed the file rule on 1.20.1.2 (below).

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
with a UTF-8 BOM; its error.log reports unknown triggers in
`common/scripted_triggers` too (a 1.19 build, and 1.20.1.2 on 2026-11-13,
where both files named the same winner).

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

Result, 2026-11-13, Crusader Kings III 1.20.1.2, Windows 11, Steam, started
directly by the application ([evidence](evidence/2026-11-13-ck3-load-order.md)):

| Run | Probe order in `enabled_mods` | Probe names | Loaded | Shared file won by |
| --- | --- | --- | --- | --- |
| 1 | c, a (b disabled) | C, A | a, c | a (`last-enabled-wins`) |
| 2 (a moved to the top in the app) | a, c (b disabled) | A, C | a, c | **c** (`last-enabled-wins`) |
| 3 (only if the winner did not move) | a, c (b disabled) | **Z**, C (probe a renamed) | not needed | not needed |

As in Stellaris, the playset order is the effective order: for the same file
the later mod in `enabled_mods` wins, and moving a mod in the app moves the
winner. A sorts first by name in both runs, so EU4's rule (name order) would
have kept a. In both runs the event file and the scripted-trigger file at
the shared path named the same winner. `src/games/ck3.cpp` states the rule
with `loadOrderVerifiedInGame = true` and no user note.

Probe b's `supported_version="1.20"` could not be judged: started directly,
1.20.1.2 listed only the `enabled_mods` entries in `debug.log` (no
`|Disabled` line) and reported no `Invalid supported_version` line at all,
also not for the registered `"1.*"` descriptor that a 1.19 build had
reported. Whether the game version or the launch path (the 1.19 run was
apparently started through the launcher) makes that difference is not known.

CK3 notes on the procedure above, as used for that run:

- Run the read-only `[.real]` scan first, as a baseline
  ([`games/ck3.md`](games/ck3.md#real-machine-evidence)).
- Step 2: `python tools/probe-mods/probe_mods.py create "<Documents>/Paradox Interactive/Crusader Kings III/mod" --game ck3 --supported-version "1.20.*"`.
  Do **not** use `"1.*"`: a 1.19 build reported it as invalid. Then
  `python tools/probe-mods/probe_mods.py create "<Documents>/Paradox Interactive/Crusader Kings III/mod" --game ck3 --names b --supported-version "1.20"`
  rewrites probe b (disabled in every run) with a two-part pattern without
  a wildcard, which no descriptor on the machine has: whether CK3 accepts
  it is not known (the adapter assumes it does), and the 2026-11-13 run
  could not tell (above).
- Step 3: make a playset with **only** the probes (c, a, b disabled), so that
  the other mods' errors do not fill `error.log` and the start is faster
  (the 2026-11-13 run imported a share code built from a manifest with
  `scripts/share-code.py encode`). The same run checks the direct launch:
  `tasklist /FI "IMAGENAME eq ck3.exe"` lists the game, and no
  `Paradox Launcher.exe`, `dowser.exe` or `bootstrapper-v2.exe` process runs;
  note whether the game stays up and whether it restarts through Steam
  (2026-11-13: it stayed up, with the same process, and did not restart).
- Step 4: wait for the main menu **and** for the `debug.log` line
  `Setup completion (history loaded)` before closing the game. On 1.20.1.2
  the probe errors came 8 to 24 s after the start and the main menu after 15
  to 53 s (about 4 minutes on the 1.19 run). Run 1 of 2026-11-13, closed
  43 s after its main-menu line without that line, crashed while closing.
- Step 5: `python tools/probe-mods/probe_mods.py analyze "<Documents>/Paradox Interactive/Crusader Kings III/logs/error.log" --enabled c a --disabled b`;
  also check that `error.log` has no `Invalid supported_version` line for
  probes a and c. A `|Disabled` line for probe b in `debug.log`, or an
  `Invalid supported_version` line for `mod/pmm_probe_b.mod` (`"1.20"`), is
  not to be expected from 1.20.1.2 started directly (above); if a later
  version reports the latter, `"1.20"` is invalid for CK3 and the
  `supported_version` rule needs a change.
- Step 8: record the evidence as `docs/evidence/<date>-ck3-load-order.md` and
  the result table here. Then set the traits in `src/games/ck3.cpp`: if
  **the later mod wins**, the Stellaris wording, `loadOrderVerifiedInGame =
  true` and no user note; if **the name that sorts first wins**, the EU4
  wording and a `loadOrderUserNote` like EU4's; if neither, stop and report
  to the owner. Update the load-order checks in
  `tests/unit/games/ck3_adapter_tests.cpp` and the CK3 row of
  `ui/dev/Pmm/Backend/FakeGamesModel.qml` with it. Done on 2026-11-13 for
  the later mod winning.
