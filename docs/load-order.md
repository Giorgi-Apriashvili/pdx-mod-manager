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

| | Stellaris | Europa Universalis IV |
| --- | --- | --- |
| Configuration file | `<user data>/dlc_load.json`, key `enabled_mods` | same (**verified** read-only on v1.37.5.0) |
| Playset order -> file | enabled entries in playset order, index 0 = array element 0 | same |
| Disabled entries | not written (the launcher writes only enabled ones) | same |
| Array order == launcher playset order | **Verified** read-only, 2026-10-29 (alphabetical playset, see below) | **Verified** read-only, 2026-10-29 (hand-made `custom` order, see below) |
| Game reads every descriptor, enables the listed ones | **Verified** read-only (error.log) | Unverified |
| File override precedence (which mod wins) | **Unverified**: Result: pending in-game verification | **Unverified** |
| `LoadOrderInfo::verifiedInGame` | `false` | `false` |

`LoadOrderInfo::summary` for both games: "dlc_load.json enabled_mods, index 0
first; file override precedence: see docs/load-order.md".

## Mapping (both games)

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
mods whose effect shows up in `logs/error.log`: every probe has a unique
scripted-trigger file that proves it loaded, and a file at the same relative
path in every probe (`common/scripted_triggers/pmm_probe_shared.txt`) whose
error names the copy that won.

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
6. Repeat with the order **a, c**. The winner must swap, which also rules out
   name-based sorting.
7. Restore the previous configuration (transaction restore), then
   `probe_mods.py remove ...`.
8. Record the game version, OS, store and the JSON output here. Then set
   `verifiedInGame = true` and the observed rule in the game's traits
   (`loadOrderSummary`, `loadOrderEvidence`).

Scope: the probe measures full-file override in `common/scripted_triggers`.
Other folders may merge per object; the community claims say exactly that,
so do not generalise without probing those too.

### Stellaris

Result: pending in-game verification.

### Europa Universalis IV

Result: pending in-game verification. EU4 1.37.5.0 is installed on the
development machine; the run needs the user's approval.
