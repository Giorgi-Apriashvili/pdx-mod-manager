# Evidence: share-code round trip through the application, both games

**Date:** 2026-10-30, 00:20-00:50 local time (UTC+4)
**Question:** after the integration of the accessibility, review-fix, follow-up and packaging
branches, does the real application (`pmm-app`) still create a playset from each game's current
configuration, edit it, export it, and reproduce exactly the same identities, order and enabled
flags in a clean profile?
**Result:** **Yes, read-only, on Windows 11 for Stellaris 4.5.2 and EU4 1.37.5.** The dumps of
the source and the clean profile are byte-identical for both games. This covers acceptance steps
1, 2, 3 and 5 of [`../validation.md`](../validation.md) in their read-only form (one machine,
isolated profiles, no Workshop download, no launch).

Every run used a new scratch profile (`--profile`) and a verification switch, which implies
`--read-only` and `--no-steam`: no game or launcher was started, the Steam helper never ran,
nothing was sent to Steam, and nothing was written under Steam, the game folders or
`Documents/Paradox Interactive`. Afterwards `dlc_load.json` was still dated 2026-10-27 15:05
(Stellaris) and 2026-08-23 19:52 (EU4). `<scratch>` stands for the private scratch folder.

## Environment

| Item | Value |
| --- | --- |
| OS | Windows 11 Enterprise 10.0.22631, x64 |
| Build | `debug` preset, MSVC 19.51, Qt 6.10.3, branch `agent/integrate` (fresh configure, clean build, 0 warnings, 424/424 tests) |
| Stellaris | "Cygnus v4.5.2 (9776)", Steam app 281990, 36 installed mods |
| EU4 | "EU4 v1.37.5.0 Inca (491d)", Steam app 236850, 13 installed mods |

## Commands

From Git Bash with Qt's `bin` folder on `PATH`, for `<game>` = `stellaris` and `eu4`:

```sh
pmm-app --profile <scratch>/<game>/A --game <game> \
        --self-test "config,disable:1,move:0:3,export:<scratch>/<game>/code.txt,dump:<scratch>/<game>/a.json"
pmm-app --profile <scratch>/<game>/B --game <game> \
        --self-test "import:<scratch>/<game>/code.txt,dump:<scratch>/<game>/b.json"
cmp <scratch>/<game>/a.json <scratch>/<game>/b.json
python scripts/share-code.py decode <scratch>/<game>/code.txt
```

`config` creates a playset from the game's own `dlc_load.json`; `disable:1` disables the second
entry and `move:0:3` moves the first entry to the fourth place, so the order is no longer the
game's. Profile B was empty before the import.

## Results

| Game | Output of A | Preview in B | Code | Dumps (`a.json` = `b.json`) |
| --- | --- | --- | --- | --- |
| Stellaris | playset with 17 entries (17 enabled), then disable and move saved | 17 entries, 16 enabled, 17 installed, 0 missing | 1,524 characters | identical, 2,150 bytes, SHA-256 `c421c6d2…8c3cd2`; 17 entries, Workshop and local mods |
| EU4 | playset with 9 entries (9 enabled), then disable and move saved | 9 entries, 8 enabled, 9 installed, 0 missing | 689 characters | identical, 1,120 bytes, SHA-256 `d7b95da0…68ea89`; 9 entries, Workshop and local mods |

The independent decoder read schema 1, the game key and the same counts (Stellaris 17/16,
EU4 9/8). In the EU4 code the disabled entry (`3390117335`, Colored Mapmodes) comes first and
`2506263674` (MRBM Font) fourth, as the edits require. All four runs exited with code 0.

In the same session, with the same build:

- `pmm_games_tests "[.real]" -s`: all tests passed (187 assertions in 2 test cases), the same
  counts as in [`real-scan-2026-10-29.md`](real-scan-2026-10-29.md).
- `--screenshot` for each game against profile A, and the refreshed
  `docs/screenshots/app-*.png` (see [`../app.md`](../app.md)); no personal paths are visible in
  them.

## Not covered

- Workshop acquisition of a missing item (step 4), launching and the in-game order (step 6),
  blocking and unavailable entries in the live game (step 7), restart with a pending download
  (step 8) and the live failure cases (step 9). They need the user's approval.
- A second machine: B was an isolated profile on the same machine, sharing its game folders and
  Steam account.
