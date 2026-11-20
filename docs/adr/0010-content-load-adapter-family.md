# ADR 0010: A second adapter family for content_load.json games

- Status: Accepted
- Date: 2026-11-20

## Context

Stellaris, EU4 and Crusader Kings III share one adapter implementation,
`DlcLoadJsonAdapter`, with a `GameTraits` value per game. They store their
enabled mods the same way: `dlc_load.json` lists `mod/<file>.mod`
registrations, and every mod is described by a Clausewitz `descriptor.mod`.

Victoria 3 (checked read-only on 1.15.2, 2026-11-20; [adapter](../games/vic3.md))
does not:

- the enabled mods are in `content_load.json`, as absolute paths of the mod
  folders (or `.zip` files), with `disabledDLC` and `enabledUGC` next to them;
  `victoria3.exe` does not even contain the string `dlc_load.json`;
- there are no `.mod` files and no registration step: Workshop folders are
  used in place;
- each mod describes itself in `.metadata/metadata.json` (JSON, with
  `supported_game_version` and object-form `relationships`).

Three of the `GameAdapter` methods in the existing family (`scanMods`,
`readCurrentConfiguration`, `buildLaunchPlan`) depend on the files Victoria 3
lacks, so new trait values cannot describe it. The rest, discovery through
`launcher-settings.json`, the wrong-game check, the user-data rules, the
launch in the executable's folder with `SteamAppId`/`SteamGameId`, process
names, load-order information and the supported-version warnings, is the same
for all four games.

A fourth dependency sat outside `games`: `providers::verifyWorkshopContent`,
the check that ends a Workshop download, repeated the `descriptor.mod` rule
(providers does not link games) and would have rejected every Victoria 3
download.

## Decision

- **A shared abstract base.** `LauncherGameAdapter`
  (`src/games/launcher_adapter.hpp`) holds `GameTraits` and everything the
  families share, including `beginLaunchPlan()`, the generic head of a launch
  plan. The Steam and Workshop helpers move to
  `src/games/workshop_content.hpp`. This extraction was its own commit with no
  change in behaviour: the full test suite, and the read-only real-machine
  scans of Stellaris, EU4 and CK3, identical line for line before and after.
- **Two families on top.** `DlcLoadJsonAdapter` keeps the `.mod` and
  `dlc_load.json` parts (Stellaris, EU4, CK3). `ContentLoadJsonAdapter`
  (`src/games/content_load_adapter.hpp`) reads `metadata.json`, scans the
  folders and `.zip` files of `mod/` and the Workshop folders, and writes only
  `content_load.json`, replacing `enabledMods` and keeping every other key
  verbatim. Each game still has its own class and traits (`Vic3Adapter`).
- **Two trait fields**, whose defaults keep the existing games as they were:
  `supportedVersionKey` (the key the warnings name) and `extraGameProcesses`
  (processes that count as the running game, such as Victoria 3's console
  wrapper).
- **The Workshop content check is a `GameAdapter` method**,
  `verifyWorkshopContent(installPath)`. Each family applies the rule of its own
  scan, so there is one copy of each rule; the services' acquisition
  coordinator asks the game's adapter. The copy in providers and the test that
  compared the two copies are gone.
- **A Victoria 3 local mod's id is its entry's name in `mod/`** (the folder's
  name, or the file name with `.zip`), not the metadata `id`, which authors
  choose freely and mostly leave empty. A Workshop mod's id stays the
  published file id. Share codes carry these ids without a format change.

## Consequences

- A further game that stores its mods like Victoria 3 is new traits on
  `ContentLoadJsonAdapter`; one like Stellaris is new traits on
  `DlcLoadJsonAdapter`. A game that fits neither still gets a family of its
  own on the shared base.
- Every `GameAdapter` implementation, the test fakes included, implements
  `verifyWorkshopContent`.
- The two families share discovery and launch code, so a change there affects
  all four games; the real-machine scans of each game are the regression gate.
- Zipped local Victoria 3 mods are listed and launched, but their metadata
  (inside the archive) is not read. Relationships other than `dependency`
  (`load_before`, `load_after`, `incompatible_with`) are not checked yet.
- The load order of Victoria 3 ships as not yet verified in the game, with a
  note in the application, until a probe run
  ([ADR 0006](0006-probe-mod-verification.md)) measures it.

## Alternatives considered

- **New traits for `DlcLoadJsonAdapter`**: not possible; the three methods
  that read and write the configuration depend on `.mod` files and
  `dlc_load.json`, which Victoria 3 does not have. Flags in the traits to
  switch those methods would have mixed two file formats in one class.
- **A separate Victoria 3 adapter that copies the shared parts**: two copies
  of discovery, the launch head and the Workshop helpers, which would drift.
- **A layout parameter on `providers::verifyWorkshopContent`**: keeps a second
  copy of each game's rule in providers and the "keep in step" test that
  guarded it; the adapter method has one copy per family.
- **The metadata `id` as the local identity**: empty in most files, and not
  unique by construction; the entry's name in `mod/` is what the game and the
  launcher locate a mod by.
