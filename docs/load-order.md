# Load order

How a playset's order becomes the order the game loads mods in, per game, and
what of that is verified. Status labels as in [`feasibility.md`](feasibility.md).

Convention used everywhere in this code base (`core/model.hpp`): **index 0 of
an ordered mod list is the first mod handed to the game.** Which end of the
list wins when two mods contain the same file is a property of the game, not
of this application, and is recorded below only as far as it is verified.

Code: `src/games/dlc_load_adapter.cpp` and, for Victoria 3,
`src/games/content_load_adapter.cpp` (`buildLaunchPlan`,
`readCurrentConfiguration`), `LoadOrderInfo` from `GameAdapter::loadOrderInfo()`.

## Summary

| | Stellaris | Europa Universalis IV | Crusader Kings III | Hearts of Iron IV | Victoria 3 |
| --- | --- | --- | --- | --- | --- |
| Configuration file | `<user data>/dlc_load.json`, key `enabled_mods` | same (**verified** read-only on v1.37.5.0) | same (**verified** read-only on 1.20.1.2) | same (**verified** on 1.20.0.0: the launcher writes it pretty-printed, and the game reads the compact form the application writes) | `<user data>/content_load.json`, key `enabledMods`, absolute paths (**verified** read-only on 1.15.2) |
| Playset order -> file | enabled entries in playset order, index 0 = array element 0 | same | same | same | same |
| Disabled entries | not written (the launcher writes only enabled ones) | same | same | same | same |
| Array order == launcher playset order | **Verified** read-only, 2026-10-29 (alphabetical playset, see below) | **Verified** read-only, 2026-10-29 (hand-made `custom` order, see below) | Not checked (the launcher's database is not read) | Not checked; the launcher turns its activation order off for this game (its log, 2026-11-18) | Not checked. The launcher's code writes every folder mod in playset order, then every zipped mod (inferred) |
| Game enables the listed mods only | **Verified** in-game, 2026-10-30: listed probes loaded, the unlisted one not; every descriptor in `mod/` is parsed | **Verified** in-game, 2026-10-30 (same) | **Verified** in-game, 2026-11-13 (1.20.1.2): listed probes loaded, the unlisted one not, mounted in `enabled_mods` order. Started directly, 1.20.1.2 lists only the `enabled_mods` entries in `debug.log`; a 1.19 build, apparently started through the launcher, had listed every descriptor as `Enabled` or `Disabled` | **Verified** in-game, 2026-11-21 (1.20.0.0): listed probes loaded, the unlisted one not; `system.log` lists the loaded mods as `Active Mod:` lines sorted by `name=`, not in load order. Started directly, 1.20.0.0 reads every descriptor in `mod/` (it reports invalid `supported_version` patterns outside `enabled_mods` too) | **Verified** in-game, 2026-11-21 (1.15.2): the listed probes and Workshop item loaded, the unlisted probe not, listed in the `Mod:` block and mounted after the DLC in `enabledMods` order. A mod without `game_custom_data` is left out of the `Mod:` block but still mounted and loaded |
| File override precedence (which mod wins) | **Verified** in-game, 2026-10-30 (4.5.2): **the later mod in `enabled_mods` wins**; moving mods in the app moves the winner | **Verified** in-game, 2026-10-30 (1.37.5): **the order of `enabled_mods` is ignored; the mod whose `name` sorts first wins** | **Verified** in-game, 2026-11-13 (1.20.1.2): **the later mod in `enabled_mods` wins**; moving mods in the app moves the winner | **Verified** in-game, 2026-11-21 (1.20.0.0): **the order of `enabled_mods` is ignored; the mod whose registration file name (`mod/<file>.mod`) sorts last wins**, as the HOI4 wiki says; `name=` does not decide either. Measured with local mods; for Workshop items (`mod/ugc_<id>.mod`) inferred (see [Hearts of Iron IV](#hearts-of-iron-iv)) | **Verified** in-game, 2026-11-21 (1.15.2): **the later mod in `enabledMods` wins** a whole file; moving mods in the app moves the winner. Database entries follow other rules since 1.12 (see [Victoria 3](#victoria-3)) |
| `LoadOrderInfo::verifiedInGame` | `true` | `true`, with a user note | `true` | `true`, with a user note | `true` |

`LoadOrderInfo::summary`, Stellaris and CK3: "dlc_load.json enabled_mods,
index 0 first; when two enabled mods contain the same file, the one later in
the list wins"; Victoria 3 the same with "content_load.json enabledMods".
EU4: "dlc_load.json enabled_mods lists the playset order, but EU4 does not
use it for file overrides: when two enabled mods contain the same file, the
mod whose name sorts first alphabetically wins". Hearts of Iron IV:
"dlc_load.json enabled_mods lists the playset order, but Hearts of Iron IV
does not use it for file overrides: when two enabled mods contain the same
file, the mod whose registration file name (mod/<file>.mod) sorts last
wins". Evidence: [live validation](evidence/live-validation-2026-10-30.md)
for Stellaris and EU4, [CK3 probe run](evidence/2026-11-13-ck3-load-order.md)
for CK3, [Victoria 3 probe run](evidence/2026-11-21-vic3-load-order.md) for
Victoria 3, [Hearts of Iron IV probe run](evidence/2026-11-21-hoi4-load-order.md)
for Hearts of Iron IV. The mods pane shows a game's user note above its
playsets. EU4 and Hearts of Iron IV have permanent ones, because these games
do not use the list order; Hearts of Iron IV's: "Hearts of Iron IV does not
use this order when two mods change the same file: the mod whose .mod file in
the game's mod folder comes last alphabetically wins (ugc_<ID>.mod for a
Workshop mod). The order is still saved, shared and written for the game."
The temporary "not yet verified" notes of CK3, Victoria 3 and Hearts of Iron
IV were removed or replaced after their probe runs.

## Mapping (the dlc_load.json games)

Stellaris, EU4, Crusader Kings III and Hearts of Iron IV.

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
  writes for Stellaris, EU4 and CK3 (for Hearts of Iron IV it writes the file
  pretty-printed; Hearts of Iron IV 1.20.0.0 reads the compact form, observed
  in its probe run). The file is only written when its content would change:
  an existing file with the same JSON in another layout (BOM, CRLF,
  indentation, `1.50` for `1.5`) is left as it is.
- `readCurrentConfiguration` is the inverse: array order becomes playset
  order, every entry enabled. A reference that matches no installed mod keeps
  the identity its registration implies: `mod/ugc_<id>.mod` (or a descriptor
  pointing into the Workshop content folder) stays the Workshop item `<id>`,
  even when its content is not downloaded, so it can be acquired. Anything
  else becomes a `local` entry named after the reference stem.

## Mapping (Victoria 3)

`buildLaunchPlan` receives the enabled entries in playset order and writes
`content_load.json` in the launcher's layout (two-space indent, LF, no BOM,
no trailing newline):

```json
{
  "enabledMods": [
    {
      "path": "C:\\Program Files (x86)\\Steam\\steamapps\\workshop\\content\\529340\\1000000001"
    },
    {
      "path": "C:\\Users\\Player\\Documents\\Paradox Interactive\\Victoria 3\\mod\\Local Tweaks"
    }
  ],
  "disabledDLC": [],
  "enabledUGC": []
}
```

- Each element is `{"path": ...}` with the absolute path of the mod's folder,
  or of its `.zip`. A path the file already holds for the same location is
  kept as written; new ones are absolute, with backslashes, an upper-case
  drive letter and no trailing separator, the form the launcher writes.
  Workshop items are used in place; nothing is generated or registered.
- Only `enabledMods` changes. `disabledDLC`, `enabledUGC`, `formatVersion`
  and unknown keys keep their values and positions; no key is added. A
  missing file becomes `{"enabledMods":[...],"disabledDLC":[],"enabledUGC":[]}`,
  a malformed one is rebuilt so, with a launch warning. The file is only
  written when its JSON would change.
- `readCurrentConfiguration` is the inverse: a path inside
  `.../workshop/content/529340/<id>` is that Workshop item, installed or not;
  any other path is the installed mod at that location, else a `local` entry
  named after the path's last component (the entry's name in `mod/`), which
  reads as not installed. A path outside `mod/` whose last component is the
  name of an installed mod at another location, and a path that names no
  folder, are left out and logged.

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

Victoria 3 1.15.2 (Steam, Windows 11), 2026-11-20 (details in
[`games/vic3.md`](games/vic3.md#real-machine-evidence)). The game had last
run as 1.15.1 on 2026-11-18:

1. **The game reads `content_load.json`.** `victoria3.exe` contains
   `content_load.json`, `enabledMods` and `disabledDLC`, and no
   `dlc_load.json`. The last session's `debug.log` lists the 18 DLC, then a
   `Mod:` block with the one enabled mod (`name|absolute path`), then mounts
   the DLC folders followed by the mod.
2. **This application reproduces the launcher's file**: the launch plan for
   the current `content_load.json` has zero writes.

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

For Hearts of Iron IV, Paradox's own sources describe a different rule from
the list order:

- A 2018 developer diary (the game director): "Mods will usually load in
  alphabetical order of their file names (so the mod last in the list will
  overwrite the earlier)", and a mod that needs a certain load order names
  the others under `dependencies`.
- The Paradox-hosted HOI4 wiki (Modding; the same wording in its 2023 and
  2026 captures): mods are "ordered by using filenames of user-specific
  descriptors", so `mod/xyz_mod.mod` beats `mod/abc_mod.mod` for the same
  file; `dependencies = { "Exact Name" }` makes a mod load after the named
  ones without making them required. The full order is the base game, the
  DLC by internal id, the user directory, then the mods. The later copy wins
  per relative file path and folders merge; `replace_path` unloads earlier
  files in one folder (not its subfolders) without changing the order and
  must be in both descriptors; of two mods with the same name only the
  first-loaded one counts; within a folder, files are read in ASCII
  file-name order across sources; `localisation/<language>/replace/` wins.
- A staff comment of 2021 in an EU4 thread ("It has worked for HoI 4 when
  we've had the same problem") is ambiguous.

Community claims are weaker and mostly older than the launcher v2: in 2016
the built-in launcher sorted by `name=`, the higher one winning; in 2018 "1.5.0
prioritizes Workshop mods over local mods no matter what"; in 2021 players
said that Hearts of Iron IV "still load[s] mods only alphabetically", unlike
EU4 and Stellaris. The mod manager Irony writes `enabled_mods` in its
collection order for Hearts of Iron IV, which describes a tool, not the game.
On the development machine the launcher turns its activation order and
activation sets off for `hoi4` only, `system.log` lists the loaded mods sorted
by name, and the owner's `enabled_mods` is in neither name nor id order (all
observed read-only). If the wiki is right, Workshop items order by the string
`ugc_<id>`, so a 9-digit id starting with 9 would beat a 10-digit id starting
with 1, and the order of `enabled_mods` would not matter (inferred; case and
byte order are not documented). The probe run of 2026-11-21 confirmed the
file-name rule on 1.20.0.0 for local mods (below); the Workshop case remains
an inference.

For Victoria 3, reverse engineering of 1.15.1 (v3x) reports that every
mounted mod is searched before the earlier ones, so a later entry in
`enabledMods` wins a shared file; the community gives the same advice, and the
third-party validator vic3-tiger models it. `game_custom_data.replace_paths`
hides the files of lower mods in exactly that folder. Since 1.12 (December
2025) many databases no longer let a later definition of the same key
replace an earlier one: the first one, in file-name order, is kept, unless
the mod uses `REPLACE:`, `INJECT:`, `TRY_*` or `*_OR_CREATE:`; events and GUI
types keep their first definition, and localization keys are replaced only
through `localization/<language>/replace/`. So the file rule says nothing
about which mod wins a database entry. All of this is documentation; the
probe run of 2026-11-21 confirmed the file rule on 1.15.2 (below), and the
database rules were not probed.

On 2026-10-29 these pages could not be retrieved from the development
environment (a browser-verification page and HTTP 500). The claims above come
from search-result excerpts of these threads and related Steam discussions,
so they cannot be attributed to individual posts. None of them is official
Paradox documentation. Hence the precedence rule is measured in-game, not
assumed.

## Dependencies

A `.mod` descriptor can name other mods, by their exact `name`, under
`dependencies = { "Exact Name" }` (Victoria 3: `metadata.json`
relationships of `rel_type` `"dependency"`). What that does is untested
in-game in every game; read-only research of 2026-11-21
([evidence](evidence/2026-11-21-descriptor-dependencies.md)) found:

- The executables of Stellaris, Europa Universalis IV, Crusader Kings III
  and Hearts of Iron IV read the key, and none of their messages about
  loading mods is about a missing or misplaced dependency. The Paradox
  launcher checks only Paradox Mods dependencies and Victoria 3's
  `metadata.json` relationships, not these names, and does not reorder a
  playset by them.
- **Hearts of Iron IV:** documented: the HOI4 wiki says they make a mod load
  after the named ones without making them required.
- **Stellaris:** a summary of the wiki says the same ("if they're
  installed"); unverified. The later mod in the list wins a shared file
  (measured).
- **Europa Universalis IV:** a summary of the wiki says a submod naming mods
  there overrides them; unverified. EU4 ignores the list order (measured).
- **Crusader Kings III:** no documentation found. The later mod in the list
  wins a shared file (measured).
- **Victoria 3:** a `"dependency"` relationship is a requirement in the
  launcher too.

So the application (the catalog's `DependencyMeaning`, owner's decision of
2026-11-21) treats them as required in Victoria 3 only. In Stellaris and
Crusader Kings III, where the list order decides, it keeps an order hint: a
named mod that is enabled but comes later in the list than the mod naming
it is a warning ("Lists "<name>" (#n) to load before this mod (#m), but that
mod comes later in this playset."); a missing, disabled or absent one is
not. In Europa Universalis IV and Hearts of Iron IV, which ignore the list
order, nothing is a warning. Nothing is ever enabled, added or reordered for
them, and the order written is the playset's.

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
where both files named the same winner). Victoria 3 needs `--game vic3`,
which writes probe folders with `.metadata/metadata.json` instead of `.mod`
files, and Victoria 3's events (`<ns>.1 = { type = country_event hidden =
yes ... }`) with a UTF-8 BOM and LF line ends, as the game's own script files
have; its `error.log` writes `Unknown trigger type:` and, like Stellaris and
EU4, reported only the event files (1.15.2 on 2026-11-21).
Hearts of Iron IV needs `--game hoi4`, which writes the Stellaris and EU4
event with `add_namespace = <ns>` instead of `namespace = <ns>`, in UTF-8
without a BOM, as all of the game's scripted-trigger files and a third of its
event files are, and with LF line ends, as all of them have; its `error.log`
reports each probe file in `common/scripted_triggers` and in `events` with two
lines, `Invalid trigger '<name>' in <file> line : <n>` and `Error: "Unknown
trigger-type: <name>, near line: <n>" in file: "<file>"`, which `analyze`
finds (1.20.0.0 on 2026-11-21, where both files named the same winner).

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
   (`last-enabled-wins` or `first-enabled-wins`: the winner's place in the
   list, which is the game's rule only where the list decides).
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
EU4's `LoadOrderInfo`. Untested: mods with `dependencies` (see
[Dependencies](#dependencies)), names that differ only in case or start with
non-ASCII characters.

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

### Victoria 3

Result, 2026-11-21, Victoria 3 1.15.2, Windows 11, Steam, started directly
by the application, approved by the owner (O1)
([evidence](evidence/2026-11-21-vic3-load-order.md)):

| Run | Probe order in `enabledMods` | Probe names | Loaded | Shared file won by |
| --- | --- | --- | --- | --- |
| 1 | c, w1, w2, w3, g, the Workshop item, a (b disabled) | C, ..., A | a, c, g, w1, w2, w3 and the Workshop item | a (`last-enabled-wins`) |
| 2 (a and c swapped in the app) | a, w1, w2, w3, g, the Workshop item, c (b disabled) | A, ..., C | the same | **c** (`last-enabled-wins`) |
| 3 (only if the winner did not move) | | **Z**, ..., C (probe a renamed) | not needed | not needed |

As in Stellaris and CK3, the playset order is the effective order for whole
files: for the same file the later mod in `enabledMods` wins, and moving a
mod in the app moves the winner. A sorts first by name in both runs, so
EU4's rule (name order) would have kept a. The `Mod:` block of `debug.log`
and the mounts follow `enabledMods`; the disabled probe b appears nowhere.
`src/games/vic3.cpp` states the rule with `loadOrderVerifiedInGame = true`
and no user note. Which definition of a database entry wins is a different
rule since 1.12 (above) and was not probed.

The same runs settled the rest of the procedure's questions:

- `supported_game_version`: `"1.15.*"`, `"1.*"` and `"1.15"` were logged as
  matching 1.15.2, `"*"` as **not** matching (and `dedicated_server.log`
  listed that probe as out of date at the title menu); no pattern was
  reported as invalid, and every version probe loaded. So
  `rejectsEarlyWildcards` stays `false`, and `loneWildcardMismatches = true`
  makes the application warn about `"*"`, which it used to count as a
  match.
- A `metadata.json` without `game_custom_data` (probe g): the game logs
  "Mod metadata read error" and leaves the mod out of its `Mod:` block, but
  still mounts its folder in playset position and loads its files. Such a
  mod stays Ready, with a problem that says so; it is not Broken.
- The Workshop item, written as
  `C:\Program Files (x86)\Steam\steamapps\workshop\content\529340\<id>`
  (the casing on disk and in `libraryfolders.vdf`; Steam's registry entry is
  lower-case and was not used), was listed in the `Mod:` block, mounted in
  its playset position and loaded.
- The direct start stayed up: one `victoria3.exe`, the same process 27 s
  later and until it was closed, no launcher process, no restart through
  Steam, all 18 DLC in the `DLC:` block.

Victoria 3 notes on the procedure above, as used for that run:

- Run the read-only `[.real]` scan first, as a baseline
  ([`games/vic3.md`](games/vic3.md#real-machine-evidence)), and back up
  `content_load.json` outside the game folders (with its SHA-256), together
  with a listing of `<userdata>/mod`.
- Step 1: close the Paradox launcher; it writes `content_load.json` from its
  own playset when it starts a game.
- Step 2: `python tools/probe-mods/probe_mods.py create "<Documents>/Paradox Interactive/Victoria 3/mod" --game vic3 --supported-version "1.15.*"`
  writes probes a, b and c, each a folder with `.metadata/metadata.json`
  and no `.mod` file; the output lists the absolute folder paths, exactly as
  `content_load.json` will hold them. Then the version probes, without the
  shared file so that they take no part in the precedence test:
  `create ... --game vic3 --names w1 --supported-version "1.*" --no-shared`,
  the same with `--names w2 --supported-version "*"` and with
  `--names w3 --supported-version "1.15"`. Optionally (step 6 of
  [`games/vic3.md`](games/vic3.md#outstanding-validation)):
  `create ... --game vic3 --names g --no-game-custom-data --no-shared --supported-version "1.15.*"`.
- Step 3: make a playset with **only** the probes and **one** installed
  Workshop item (for step 6; pick one without warnings in the application),
  from a share code built with `scripts/share-code.py encode` (game `vic3`;
  local ids are the folder names). In this order: `pmm_probe_c`,
  `pmm_probe_w1`, `pmm_probe_w2`, `pmm_probe_w3`, `pmm_probe_g` if made, the
  Workshop item (`steam-workshop` and its id), `pmm_probe_a`, all enabled,
  then `pmm_probe_b` disabled. The version probes and the Workshop item have
  no shared file, so they stay between c and a. Save the code to a file,
  then run `pmm-app --profile <scratch> --no-steam --live-test
  "game:vic3,import:<file>,blockers,play"` (the 2026-11-21 run added
  `dump:<file>` before `play`). Check the direct launch:
  `tasklist /FI "IMAGENAME eq victoria3.exe"` lists the game and still the
  same process about 20 s later (no restart through Steam), no
  `Paradox Launcher.exe` or `dowser.exe` process runs, and the `DLC:` block
  of `debug.log` lists all 18 DLC.
- Step 4: wait for the title menu, then for the logs to stay unchanged for
  60 s, then close the game with `CloseMainWindow` (never kill it while it
  loads). Victoria 3's counterpart of CK3's `Setup completion` line is
  `[pdx_transition_task.cpp:328]: Transition Empty->Game took: N seconds` in
  `debug.log`, but `debug.log` reaches the disk late: on 2026-11-21 that line
  and everything after it appeared only when the game closed. Wait instead
  for `logs/custom_automated_stats.log`'s
  `[AUTOMATION_SINGLE_STAT][time-to-reach-title-menu]:[N]`, written at once
  (97 to 107 s after the start on 1.15.2), and judge "unchanged" from copies
  of the files: sizes read from a folder listing lag behind. Closed this way,
  both runs quit within 3 s.
- Step 5: `python tools/probe-mods/probe_mods.py analyze "<Documents>/Paradox Interactive/Victoria 3/logs/error.log" --game vic3 --enabled c w1 w2 w3 a --disabled b`
  (add `g` after `w3` if it was made). Besides the `error.log` checks it
  reads `debug.log` and `debug.1.log` to `debug.5.log` (the log rotates
  within a session) and reports the latest session's `Mod:` block (the
  probes in it, in order, and how many other mods: the Workshop item), its
  `Mounted Data:` lines, the probes' "version ... matched / does not match"
  lines and every `Invalid supported_version` line, which settle
  `rejectsEarlyWildcards` for `1.*`, `*` and `1.15`. The version probes and
  `g` test what the game accepts: `g` is expected to be left out, and so may
  be a version probe whose pattern the game rejects. Then `loaded` and
  `modBlock.probes` lack it, `allEnabledLoaded` and `modBlockIsEnabledOrder`
  are false and the tool exits with 1; that is a result to record, not a
  failed run. The run itself is judged by a and c (both loaded, and in the
  `Mod:` block in playset order), by b (in neither) and by
  `sharedFileWinner`. If `error.log` names no unknown trigger, the `Mod:`
  block and the mount order still show what loaded, but not which mod won.
  On 2026-11-21 only g was left out of the `Mod:` block (so `ok` was false,
  as expected), the version probes all loaded, and the run 1 `Mod:` block
  was in `debug.1.log` (the log rotated about a minute after the start).
  Check each run's logs by their timestamps: the previous run's files are
  still there until the game rotates them.
- Step 6: repeat with the order **a, w1, w2, w3, (g,) the Workshop item,
  c**, moved in the application: `pmm-app --profile <scratch> --no-steam
  --live-test "game:vic3,select:<playset name>,move:5:0,move:1:5,dump:<file>,blockers,play"`
  (with g: `move:6:0,move:1:6`; rows count from 0, b included). The dump
  must list a, w1, w2, w3, (g,) the Workshop item, c and b (disabled). The
  Workshop item's `Mounted Data:` line in `debug.log` (in both runs) proves
  that the game takes the `...\steamapps\workshop\content\529340\<id>` path
  the application writes. Note whether that path has the casing on disk:
  the application upper-cases only the drive letter and keeps the casing of
  the Steam library path it read (`libraryfolders.vdf`, else Steam's
  registry entry, which can be lower-case), while a third-party tool writes
  the casing on disk because a wrongly cased first entry reportedly breaks
  the whole list. On 2026-11-21 the path had the casing on disk, and the
  game mounted it.
- Step 7: restore with the application once per launch, so twice:
  `pmm-app --profile <scratch> --live-test "game:vic3,restore"` restores the
  configuration from before the last launch it has not restored yet, so the
  first run undoes step 6 and the second step 3, as in the CK3 run. Then
  compare `content_load.json` with the backup (it must be byte-identical)
  and run
  `python tools/probe-mods/probe_mods.py remove "<Documents>/Paradox Interactive/Victoria 3/mod" --game vic3 --names a b c w1 w2 w3 g`
  (folders only; there are no `.mod` files).
- Step 8: record the evidence as `docs/evidence/<date>-vic3-load-order.md`
  and the result table here. Then set the traits in `src/games/vic3.cpp`
  (`loadOrderVerifiedInGame`, `loadOrderEvidence`, the user note and
  `rejectsEarlyWildcards`), their checks in
  `tests/unit/games/vic3_adapter_tests.cpp` and `registry_tests.cpp`, the
  Victoria 3 row of `ui/dev/Pmm/Backend/FakeGamesModel.qml`,
  [`games/vic3.md`](games/vic3.md), the [support matrix](support-matrix.md),
  `src/games/supported_version.hpp` and the CHANGELOG's load-order sentence.
  If the later mod wins, CK3's wording and no note; if the name that sorts
  first wins, EU4's wording and a note like EU4's; if neither, stop and
  report to the owner. Done on 2026-11-21 for the later mod winning, with
  `loneWildcardMismatches` for `"*"` and the corrected `game_custom_data`
  problem.
- The probe measures whole files. It says nothing about database entries,
  which follow the rules above since 1.12.

### Hearts of Iron IV

Result, 2026-11-21, Hearts of Iron IV 1.20.0.0, Windows 11, Steam, started
directly by the application, four runs
([evidence](evidence/2026-11-21-hoi4-load-order.md)):

| Run | `enabled_mods` | `name=` | Registration files | Loaded | Shared file won by |
| --- | --- | --- | --- | --- | --- |
| 1 | c, a (b disabled) | C, A | `pmm_probe_c.mod`, `pmm_probe_a.mod` | a, c | c |
| 2 (a moved to the top in the app) | a, c (b disabled) | A, C | the same | a, c | **c** (did not move) |
| 3 (a renamed) | a, c (b disabled) | **Z**, C | the same | a, c | **c** (did not move) |
| 4 (a's registration renamed) | a, c (b disabled) | A, C | **`pmm_probe_z.mod`** (`path=` and folder unchanged), `pmm_probe_c.mod` | a, c | **a** (moved) |

Hypothesis **H3** holds: when two enabled mods contain the same file, the mod
whose registration file name (the `mod/<file>.mod` named in `enabled_mods`)
sorts later wins. The list order does not decide (c won as the first and as
the last entry, runs 1 and 2), nor does `name=` (c won with the earlier and
with the later name, runs 2 and 3), nor the folder or `path=` (unchanged
between runs 2 and 4, while the winner moved). `analyze`'s `precedence` label
followed the list (`first-enabled-wins` in runs 1 and 4, `last-enabled-wins`
in runs 2 and 3) and is not the rule here. In every run the event file and
the scripted-trigger file at the shared path named the same winner. The
`Active Mod:` lines of `system.log` are sorted by `name=` (run 3: C, Z), a
display order. The game read the compact `dlc_load.json` the application
wrote and never rewrote it; `hoi4.exe` kept its process id with `pmm-app`
as its parent, without a launcher process or a restart through Steam.

As for EU4, the application keeps writing the playset order (it is what the
launcher writes, and share codes carry it) and does not rename registrations
to force an order; `src/games/hoi4.cpp` states the rule with
`loadOrderVerifiedInGame = true` and a permanent user note (see
[Summary](#summary)). Not tested, so not claimed: whether the comparison is
ordinal or ignores case, how `ugc_<id>.mod` names of 9- and 10-digit ids
compare (as strings, `ugc_9…` sorts after `ugc_1…`), non-ASCII file names,
`dependencies`, `replace_path`, per-object merging in database folders, and
a launcher-started game. With only two file names compared, another
deterministic order of file names (such as the order NTFS lists them in,
which is the same for these names) is not strictly excluded. For Workshop
items (`mod/ugc_<id>.mod`) the rule is inferred, not measured.

`dependencies` follow the wiki in the application, untested in-game: a mod's
`dependencies` are not checked as requirements in Hearts of Iron IV (no
warnings, nothing enabled, added or reordered for them, the playset order
written as it is), and the selected mod's details list them under "Loads
after" ([Hearts of Iron IV](games/hoi4.md#mods), [Dependencies](#dependencies)).
Whether the game loads a mod after the mods it names, and how that combines
with the file-name rule, is still untested.

`supported_version`: started directly, 1.20.0.0 reads every `mod/*.mod` at
start-up, enabled or not (unlike CK3 1.20.1.2). It reported the owner's four
`"1.*.*"` registrations as invalid, none of them enabled, and not probe b's
`"1.20"` (disabled in every run), nor `"1.20.*"`. So `rejectsEarlyWildcards`
stays `true`, and a two-part pattern without a wildcard is valid; whether the
game counts `"1.20"` as matching 1.20.0.0 is not shown (b was never loaded,
and the game logs no match lines).

The procedure as prepared, and used for that run. Four rules were possible:

| Hypothesis | Rule | Precedent |
| --- | --- | --- |
| H1 | The later entry in `enabled_mods` wins | Stellaris, CK3 (measured) |
| H2 | The `name=` order decides (direction to measure) | EU4: the name that sorts first wins (measured) |
| H3 | The registration file name (`mod/<file>.mod`) decides, the later name winning | the HOI4 wiki and the 2018 diary |
| H4 | The content folder or path order decides | none (a fallback) |

The probes align every key: probe a has the folder `pmm_probe_a`, the
registration `mod/pmm_probe_a.mod` and the name "PMM Probe A", and so on, so
each run changes one key. Probe b stays disabled throughout.

| Run | `enabled_mods` | `name=` | Registration files | Separates |
| --- | --- | --- | --- | --- |
| 1 | c, a | C, A | `pmm_probe_c.mod`, `pmm_probe_a.mod` | the baseline |
| 2 | a, c (a moved in the app) | A, C | the same | H1 from any sorted key |
| 3 (only if run 2 kept the winner) | a, c | **Z**, C (a renamed in both of its descriptors) | the same | H2 from H3 and H4 |
| 4 (only if run 3 kept the winner) | a, c | A, C (the name restored) | **`pmm_probe_z.mod`** (a's registration renamed; `path=` and folder unchanged) | H3 from H4 |

Reading the results: if the winner swaps between runs 1 and 2, the list
decides (H1, `last-enabled-wins` expected). If it stays, a sorted key decides:
a winning means "first wins" (EU4's direction), c winning "last wins" (the
wiki's). If run 3 moves the winner, `name=` decides; if run 4 does, the file
name decides; if neither, record that and claim no rule. In every run record
whether the event file and the scripted-trigger file at the shared path name
the same winner, the `system.log` `Active Mod:` lines, that the compact
`dlc_load.json` the application wrote was accepted (the probes loaded),
whether the game rewrote `dlc_load.json`, and that `hoi4.exe` kept one
process id with `pmm-app` as its parent (no restart through Steam or the
launcher, no launcher process). Untested by these probes, to be recorded as
such: `dependencies`, the order of 9- and 10-digit `ugc_` ids (if H3 holds,
state it as inferred), names or file names that differ only in case or start
with non-ASCII characters, and per-object merging in database folders.

Notes on the procedure above, for that run (`<userdata>` is
`<Documents>/Paradox Interactive/Hearts of Iron IV`):

- Preconditions: the application is built from the branch with Hearts of
  Iron IV support; the Steam client runs and is signed in; no
  `Paradox Launcher.exe`, `dowser.exe` or `bootstrapper-v2.exe` process runs.
  Record `buildid` from `appmanifest_394360.acf` (26733403 on 2026-11-21) at
  the start and again at the end, and stop if Steam starts updating the game.
- Run the read-only `[.real]` scan first, as a baseline
  ([`games/hoi4.md`](games/hoi4.md#real-machine-evidence)): 23 Workshop mods,
  21 enabled, 0 unresolved, **0 writes** for the current selection (this shows
  the compare by meaning against the launcher's pretty-printed file), working
  directory `<install>`, arguments `[-gdpr-compliant]`,
  `SteamAppId`/`SteamGameId` 394360, the user data from `gameDataPath`, and
  only `supported_version` warnings. Then back up, outside the game folders,
  `dlc_load.json` (677 bytes on 2026-11-21) with its SHA-256, the 23
  `mod/*.mod` files with their hashes, sizes and times, and a listing of
  `<userdata>`.
- Step 2:
  `python tools/probe-mods/probe_mods.py create "<userdata>/mod" --game hoi4 --supported-version "1.20.*"`.
  Do **not** use `"1.*.*"`: the error.log of 1.19.5.0 and of 1.20.0.0
  reports it as invalid.
  Optionally, rewrite probe b (disabled in every run) with a two-part pattern:
  `python tools/probe-mods/probe_mods.py create "<userdata>/mod" --game hoi4 --names b --supported-version "1.20"`.
  Unlike CK3, 1.20.0.0 started directly reads descriptors outside
  `enabled_mods`, so this tells whether `"1.20"` is reported as invalid
  (2026-11-21: it was not).
- Step 3: make a playset with **only** the probes, from a share code: write
  the manifest
  `{"schemaVersion":1,"game":"hoi4","playsetName":"PMM probes","mods":[{"source":"local","id":"pmm_probe_c","enabled":true},{"source":"local","id":"pmm_probe_a","enabled":true},{"source":"local","id":"pmm_probe_b","enabled":false}]}`
  to a file, encode it with
  `python scripts/share-code.py encode <manifest.json> > probes.code`, then
  run `pmm-app --profile <scratch> --no-steam --live-test "game:hoi4,import:probes.code,blockers,play"`
  (run 1: c, a). Snapshot the processes every 5 s while the game starts
  (`tasklist /FI "IMAGENAME eq hoi4.exe"`, and the launcher names above).
- Step 4: wait until `error.log` and `system.log` show the probe lines and
  the logs have been quiet for at least 60 s (the main menu), then close the
  game with `CloseMainWindow`; never force-kill it while it loads. If
  `CrashReporter.exe` appears, leave it open for the owner and record it.
  Copy `error.log`, `system.log`, `dlc_load.json` after Play and after the
  exit, and the transaction journal. Loading is complete at `setup.log`'s
  `[..][1936.01.01.12]: Startup time: Nms` (48 s on the first start of
  1.20.0.0, which rebuilt `naval_dist.cache`, then about 19 s); `game.log`'s
  `Executing History ... to 1936.1.1.12` comes 1 to 5 s earlier. On
  2026-11-21 no line reached the disk late (copies before the close and after
  the exit were identical), the game writes no quit line, and each close took
  about 3.3 s.
- Step 5:
  `python tools/probe-mods/probe_mods.py analyze "<userdata>/logs/error.log" --game hoi4 --enabled c a --disabled b`
  reports, besides the `error.log` checks, the probes among the `system.log`
  `Active Mod:` lines (by name) and every `Invalid supported_version` line.
  If `error.log` has no `pmm_probe_` line, the run says nothing about
  precedence (the `Active Mod:` lines still show what loaded): stop, restore
  and remove (below), record it, and keep the note. Its `precedence` label
  names the winner's place in `--enabled`; compare the winner across runs
  instead.
- Run 2: move a to the top in the application, then repeat steps 4 and 5
  with `--enabled a c`:
  `pmm-app --profile <scratch> --no-steam --live-test "game:hoi4,select:PMM probes,move:1:0,dump:run2.json,blockers,play"`
  (rows count from 0: c, a, b before the move; the dump must list a, c and b
  disabled).
- Run 3, only if run 2 kept the winner:
  `python tools/probe-mods/probe_mods.py rename "<userdata>/mod" --name a --display Z`
  (name="PMM Probe Z" in both of a's descriptors), then
  `pmm-app --profile <scratch> --no-steam --live-test "game:hoi4,rescan,select:PMM probes,blockers,play"`,
  steps 4 and 5 with `--enabled a c`, and afterwards
  `python tools/probe-mods/probe_mods.py rename "<userdata>/mod" --name a --display A`.
- Run 4, only if run 3 kept the winner:
  `python tools/probe-mods/probe_mods.py rename "<userdata>/mod" --name a --file z`
  moves a's registration to `mod/pmm_probe_z.mod` (`path=` unchanged), so the
  application sees a new local mod `pmm_probe_z`. Import a second manifest
  for it, as in step 3 (`"playsetName":"PMM probes z"`, the entries
  `pmm_probe_z` and `pmm_probe_c` enabled in that order, `pmm_probe_b`
  disabled), with
  `pmm-app --profile <scratch> --no-steam --live-test "game:hoi4,rescan,import:probes-z.code,blockers,play"`;
  steps 4 and 5 with `--enabled a c` (the trigger names inside the folder
  still say a). Afterwards
  `python tools/probe-mods/probe_mods.py rename "<userdata>/mod" --name a --file a`.
- Step 7: restore with the application once per launch that wrote the
  configuration:
  `pmm-app --profile <scratch> --no-steam --live-test "game:hoi4,restore"`
  restores the configuration from before the last launch it has not
  restored yet, the last launch first. Only a launch that writes something
  can be restored: runs 1, 2 and 4 write `dlc_load.json`, but run 3 has run
  2's `enabled_mods` and normally writes nothing, so it leaves nothing to
  restore. That is 2 restores after runs 1 to 3 and 3 after runs 1 to 4
  (2026-11-21: 3, and the file was byte-identical to the backup after them). Or
  repeat it until it fails with "there is no configuration to restore" (the
  scratch profile holds only this run's launches); that failure then only
  says that everything is restored. Then compare `dlc_load.json` with the
  backup (it must be byte-identical, the launcher's pretty layout back) and
  the 23 registrations with theirs, and run
  `python tools/probe-mods/probe_mods.py remove "<userdata>/mod"`, which also
  removes a registration left renamed by run 4. Record what the game itself
  changed in `<userdata>` (settings, `continue_game.json`, caches).
- Stop, restore and leave the rest to the owner if a launcher process
  appears, `hoi4.exe` restarts through Steam, the application writes anything
  besides `dlc_load.json` and `mod/pmm_probe_*`, or the restore does not match
  the backup.
- Step 8: record the evidence as `docs/evidence/<date>-hoi4-load-order.md`
  (the owner's mods by count only) and the result table here and in
  [`validation.md`](validation.md#6-results). Then set the traits in
  `src/games/hoi4.cpp` (`loadOrderSummary`, `loadOrderEvidence`,
  `loadOrderVerifiedInGame = true`), their checks in
  `tests/unit/games/hoi4_adapter_tests.cpp` and `registry_tests.cpp`, the
  Hearts of Iron IV row of `ui/dev/Pmm/Backend/FakeGamesModel.qml`,
  [`games/hoi4.md`](games/hoi4.md), the [support matrix](support-matrix.md),
  the development status and the CHANGELOG. If the later mod in the list
  wins (H1), CK3's wording and no note. If a sorted key decides (H2, H3 or
  H4), keep writing the playset order, as for EU4 (it is what the launcher
  writes, and share codes carry it), state the measured rule in the summary
  and in a note like EU4's, and do not rename registrations to force an
  order. If no rule shows, keep the "not verified" note and report to the
  owner. Done on 2026-11-21 for H3 (the later registration file name wins).
