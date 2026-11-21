# Descriptor dependencies, 2026-11-21

What the `dependencies = { "Exact Name" }` key of a `.mod` descriptor (and
Victoria 3's `metadata.json` relationships of `rel_type` `"dependency"`)
means in each supported game, from read-only research on the development
machine and documentation, and what the application does with it since
then ([load order](../load-order.md#dependencies)). **Nothing here was
tested in a game:** no game, launcher or Steam client was started, and no
probe mod lists dependencies.

| | |
| --- | --- |
| OS | Windows 11 Enterprise 23H2 (10.0.22631), x64 |
| Games installed | Stellaris 4.5.4, Europa Universalis IV 1.37.5.0, Crusader Kings III 1.21.0.3, Hearts of Iron IV 1.20.0.0, Victoria 3 1.15.2 (Steam) |
| Paradox launcher | 2026.14, installed |
| Application | debug builds of `main` (`57a6b5e`) and of `agent/dependencies-optional` |

The owner's own mods appear as counts only.

## Sources

Read, without changing anything:

- **The game executables** of Stellaris, Europa Universalis IV, Crusader
  Kings III and Hearts of Iron IV (their text): each reads `dependencies`
  from the table of descriptor keys these games share, next to `name`,
  `path`, `supported_version` and the others. None of their messages about
  loading mods is about a dependency that is missing or in the wrong place
  in the list.
- **The installed Paradox launcher** (2026.14, its application code): it
  checks two kinds of dependency only, those of Paradox Mods (the
  platform's own items) and the `metadata.json` relationships Victoria 3
  uses, never the names a `.mod` descriptor lists. It does not reorder a
  playset by them; it writes the playset's order.
- **Irony Mod Manager** (its public source code): it puts the mods a mod
  names first when it merges mods, and ignores a name that matches no mod.
  That describes a tool, not a game.
- **Documentation:**
  - Hearts of Iron IV: the Paradox-hosted HOI4 wiki (Modding, its 2023 and
    2026 captures; [load order](../load-order.md#community-claims-about-precedence-conflicting))
    says that `dependencies` make a mod load after the named ones without
    making them required. **Documented.**
  - Stellaris: a summary of the wiki's modding documentation says the same,
    that a mod loads after the named mods "if they're installed".
    **Unverified** (not checked against the page).
  - Europa Universalis IV: a summary of the wiki's modding documentation
    says that a submod naming mods there overrides them. **Unverified.**
  - Crusader Kings III: no documentation of the key was found.
  - Victoria 3: a relationship of `rel_type` `"dependency"` is a
    requirement in the Paradox launcher too (above).

Measured earlier, and relevant here
([load order](../load-order.md#summary)): the later mod in the list wins a
shared file in Stellaris, Crusader Kings III and Victoria 3; Europa
Universalis IV ignores the list order (the mod whose name sorts first wins);
Hearts of Iron IV ignores it too (the registration file name decides).

## On the development machine

Counts only, from `pmm_games_tests "[.real]"` (read-only), whose output now
also has a line for a playset of every installed mod, enabled, in the order
of their identities (game, source, id), since no current configuration
enables a mod of Stellaris, EU4, CK3 or Victoria 3 that lists dependencies:

| | Installed mods listing dependencies | Enabled mods listing names (current configuration) | Where the names of the enabled mods lead |
| --- | --- | --- | --- |
| Stellaris | 3 of 39 (6 names, 4 distinct, 2 of them not installed) | 0 of 14 | - |
| Europa Universalis IV | 0 of 13 | 0 of 3 | - |
| Crusader Kings III | 0 of 13 | 0 of 0 | - |
| Hearts of Iron IV | 7 of 23 | 6 of 21, 24 names | 4 enabled earlier, 20 not installed |
| Victoria 3 | 1 of 10 (1 name, not installed) | 0 of 1 | - |

Dependency warnings (`checkDependencies()`), with the rules of `main`
(`57a6b5e`: Required everywhere but Hearts of Iron IV) and of
`agent/dependencies-optional`:

| | Current configuration, `main` | Current configuration, branch | Every installed mod enabled, `main` | Every installed mod enabled, branch |
| --- | --- | --- | --- | --- |
| Stellaris | 0 | 0 | 3 in 2 mods (2 not installed, 1 ordered after) | 1 in 1 mod (ordered after) |
| Europa Universalis IV | 0 | 0 | 0 | 0 |
| Crusader Kings III | 0 | 0 | 0 | 0 |
| Hearts of Iron IV | 0 | 0 | 0 | 0 |
| Victoria 3 | 0 | 0 | 1 in 1 mod (not installed) | 1 in 1 mod (not installed) |

Before `agent/hoi4-dependencies` (merged into `main` as `57a6b5e`), Hearts
of Iron IV's current configuration had 20 warnings in 4 enabled mods, all
for names that are not installed ([Hearts of Iron IV](../games/hoi4.md#real-machine-evidence)).

## What the application does since

The owner's decision of 2026-11-21 ("not required; keep the order hint"):

| Game | `DependencyMeaning` | A named mod that is missing, disabled or not in the playset | A named mod that is enabled but later in the list than the mod naming it |
| --- | --- | --- | --- |
| Stellaris, Crusader Kings III | `LoadAfterInList` | no warning | a warning: `Lists "<name>" (#<n>) to load before this mod (#<m>), but that mod comes later in this playset.` |
| Europa Universalis IV, Hearts of Iron IV | `LoadAfter` | no warning | no warning (both games ignore the list order for this) |
| Victoria 3 | `Required` | a warning ("Requires …"), as before | a warning ("Requires …"), as before |

The details of a mod that names mods list them under "Loads after" in every
game but Victoria 3, with a note: Hearts of Iron IV's says what its wiki
documents; the others say only that the game does not need the mods and
that the mod names them to load before it. Nothing is enabled, added or
reordered for them.

## Not verified

- Whether any of the four games loads a mod after the mods it names, needs
  them in any way, or skips a mod whose named mods are missing: no probe mod
  has listed dependencies yet ([validation](../validation.md)).
- Whether the order hint matters in Stellaris and Crusader Kings III beyond
  the measured file rule (later wins), for example whether the game itself
  moves a mod after the mods it names.
- The two wiki summaries (Stellaris, Europa Universalis IV).
