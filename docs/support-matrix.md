# Support matrix

What works for which game, operating system and store, and how we know.
Application portability does not imply that every game works on every OS:
support for a combination depends on the game's own executable and a validated
integration (spec section 1).

Last reviewed: 2026-10-30 (after the live validation), application 0.1.0 (in development).

## Status legend

| Status | Meaning |
| --- | --- |
| **Verified (date, version)** | Observed on a real installation; the evidence is linked. "Read-only" means files were read and plans computed, but nothing was written and no game was started. |
| **Implemented, unverified** | Code and automated tests exist, but the behaviour has not been observed against the real game or store on that OS. |
| **Unsupported** | Not available; the reason is given. |

Rules: a cell changes to *Verified* only with recorded evidence (date, game
version, OS, distribution, link to `docs/validation.md` results or
`docs/evidence/`). Untested combinations are **unverified, not supported**.
Nothing here is inferred from another OS or another game.

Verified so far (development machine: Windows 11 Enterprise 23H2, Steam,
Stellaris "Cygnus v4.5.2 (9776)" build 25664273, EU4 "EU4 v1.37.5.0 Inca
(491d)" build 15918133):

- Both games, 2026-10-29, read-only: installation, user data, executable,
  arguments, environment, mod inventory (36 and 13 mods), the current
  `dlc_load.json`, compatibility warnings, and the launch plan for the
  launcher's selection, which writes nothing; regenerated Workshop descriptors
  are byte-identical to the launcher's
  ([evidence](evidence/real-scan-2026-10-29.md)).
- Stellaris, 2026-10-29, read-only: `pmm-steam-helper` initialised the Steam
  API in the game's app context and read session facts and Workshop item
  state ([evidence](evidence/steam-helper-probe.md)).
- Both games, 2026-10-29 and 2026-10-30, read-only: the desktop application
  created a playset from the game's configuration, disabled and moved
  entries, exported it, and a clean profile imported it with identical
  identities, order and enabled flags
  ([evidence](evidence/app-round-trip-2026-10-30.md)).
- Windows ZIP package, 2026-10-29: built, inspected and run from an extracted
  copy without Qt or Visual Studio ([evidence](evidence/packaging-2026-10-29.md)).
- Both games, 2026-10-30, live (with the user's approval): direct launches
  without the Paradox launcher, enabled and disabled mods and the effective
  override order observed in the games' own logs with probe mods, restore of
  the previous configuration; for EU4 a real Workshop subscription and
  download through the application, restart persistence of a pending job, and
  the failure cases ([evidence](evidence/live-validation-2026-10-30.md)).

Not observed yet: Linux and macOS, other stores, Steam's own offline mode.

## Application build

| Platform | Compiler | Core libraries + unit tests | Desktop application + tests | Packages |
| --- | --- | --- | --- | --- |
| Windows 11 x64 | MSVC 19.51 (VS 2026), Qt 6.10.3 | **Verified** 2026-10-30: 390 tests pass, 0 warnings at `/W4` | **Verified** 2026-10-30: 35 tests pass; runs read-only and live against both games | ZIP **verified** 2026-10-29; installer **verified** 2026-10-30 ([evidence](evidence/installer-2026-10-30.md)) |
| Linux x86_64 (Arch Linux in WSL) | GCC 16.2.1, no Qt | **Verified** 2026-10-30: 384 tests pass with warnings as errors, also with ASan + UBSan | Not built (no Qt in that environment) | TGZ of the core build only (2026-10-29) |
| Windows (CI, `windows-2025`) | MSVC from the runner image | Unverified until the first CI run | Unverified until the first CI run | Unverified |
| Linux (CI, `ubuntu-24.04`) | GCC 13 | Unverified until the first CI run | Unverified until the first CI run | Unverified |
| macOS (CI, `macos-15`) | AppleClang | Unverified until the first CI run | Unverified until the first CI run | Unverified |

## Stellaris (Steam app 281990)

| Capability | Windows · Steam | Linux · Steam (native) | macOS · Steam | Other stores (GOG, Paradox Store, Microsoft Store), any OS |
| --- | --- | --- | --- | --- |
| Detect installation and user data | **Verified (2026-10-29, 4.5.2)** read-only | Implemented, unverified | Implemented, unverified | Implemented, unverified (manual location) |
| Mod inventory (descriptors, Workshop content) | **Verified (2026-10-29, 4.5.2)** read-only: 36 mods; registered Workshop items without content read as missing | Implemented, unverified | Implemented, unverified | Implemented, unverified (local mods only) |
| Playsets (create, order, enable, persist, undo) | **Verified (2026-10-30, 4.5.2)** read-only in the application (create from the game's configuration, disable, move, persist); undo unit tested | Implemented, unverified | Implemented, unverified | Implemented, unverified |
| Share codes (export, import preview, exact reconciliation) | **Verified (2026-10-30, 4.5.2)** read-only between two clean profiles on one machine | Implemented, unverified | Implemented, unverified | Implemented, unverified |
| Generated configuration (`dlc_load.json`, `mod/ugc_<id>.mod`) | **Verified (2026-10-29, 4.5.2)** computed, not written: identical to the launcher's | Implemented, unverified | Implemented, unverified | Implemented, unverified |
| Workshop: Steam session and item state | **Verified (2026-10-29, 4.5.2)** read-only probe | Implemented, unverified (helper compiled with GCC and tested against the fake library, not run against Steam) | Implemented, unverified (not compiled) | Unsupported: requires the game on a Steam account |
| Workshop: subscribe and download | **Verified (2026-10-30, 4.5.2)**: an item not subscribed before was subscribed, downloaded and verified through the application; the identity check rejected an item of another game without contacting Steam ([evidence](evidence/live-validation-2026-10-30.md)) | Implemented, unverified | Implemented, unverified | Unsupported: requires the game on a Steam account |
| Workshop: manual fallback (open in Steam, rescan) | Implemented, unverified | Implemented, unverified | Implemented, unverified | Unsupported |
| Direct launch without the Paradox launcher | **Verified (2026-10-30, 4.5.2)**: started directly, no launcher process; write failure and start failure handled and restored ([evidence](evidence/live-validation-2026-10-30.md)) | Implemented, unverified | Implemented, unverified | Implemented, unverified |
| In-game verification of enabled mods and order | **Verified (2026-10-30, 4.5.2)** with probe mods: enabled mods loaded, disabled not; the later mod in `enabled_mods` wins a shared file, and moving mods in the app moves the winner ([evidence](evidence/live-validation-2026-10-30.md)) | Unverified | Unverified | Unverified |

## Europa Universalis IV (Steam app 236850)

Differences from Stellaris: the install folder has no `steam_appid.txt` (a
direct start sets `SteamAppId`/`SteamGameId`; observed 2026-10-30: the game
kept running without restarting through Steam), `userdir.txt` can relocate
the user-data folder (implemented; empty on the development machine),
`exeArgs` contains `-enabletelemetry`, which direct launches drop, and **EU4
does not use the order of `enabled_mods` for file overrides**: the mod whose
name sorts first wins ([evidence](evidence/live-validation-2026-10-30.md); [EU4 adapter](games/eu4.md)).

| Capability | Windows · Steam | Linux · Steam (native) | macOS · Steam | Other stores, any OS |
| --- | --- | --- | --- | --- |
| Detect installation and user data | **Verified (2026-10-29, 1.37.5)** read-only | Implemented, unverified | Implemented, unverified | Implemented, unverified (manual location) |
| Mod inventory | **Verified (2026-10-29, 1.37.5)** read-only: 13 mods; registered Workshop items without content read as missing (replay of the state after a reinstall) | Implemented, unverified | Implemented, unverified | Implemented, unverified (local mods only) |
| Playsets | **Verified (2026-10-30, 1.37.5)** read-only in the application; undo unit tested | Implemented, unverified | Implemented, unverified | Implemented, unverified |
| Share codes | **Verified (2026-10-30, 1.37.5)** read-only between two clean profiles on one machine | Implemented, unverified | Implemented, unverified | Implemented, unverified |
| Generated configuration | **Verified (2026-10-29, 1.37.5)** computed, not written: identical to the launcher's | Implemented, unverified | Implemented, unverified | Implemented, unverified |
| Workshop: Steam session and item state | **Verified (2026-10-30, 1.37.5)**: helper session in EU4's app context during the live acquisition | Implemented, unverified | Implemented, unverified | Unsupported: requires the game on a Steam account |
| Workshop: subscribe and download | **Verified (2026-10-30, 1.37.5)**: an item not subscribed before was subscribed, downloaded and verified through the application; a pending job survived a restart without a duplicate; with the Web API unreachable nothing was subscribed ([evidence](evidence/live-validation-2026-10-30.md)) | Implemented, unverified | Implemented, unverified | Unsupported: requires the game on a Steam account |
| Workshop: manual fallback | Implemented, unverified | Implemented, unverified | Implemented, unverified | Unsupported |
| Direct launch without the Paradox launcher | **Verified (2026-10-30, 1.37.5)**: started directly, no launcher process, no restart through Steam despite the missing `steam_appid.txt` ([evidence](evidence/live-validation-2026-10-30.md)) | Implemented, unverified | Implemented, unverified | Implemented, unverified |
| In-game verification of enabled mods and order | **Verified (2026-10-30, 1.37.5)** with probe mods: enabled mods loaded, disabled not; **the playset order does not decide file overrides** (the mod whose name sorts first wins) ([evidence](evidence/live-validation-2026-10-30.md)) | Unverified | Unverified | Unverified |

## Other combinations

| Combination | Status | Reason |
| --- | --- | --- |
| Hearts of Iron IV, Crusader Kings III, Victoria 3 | Unsupported | In the game catalog only (`adapterAvailable = false`); share codes for them are rejected with `UnsupportedGame`. The adapter interface is designed for them; no adapter exists. |
| EU4 versions before the Paradox launcher v2 (mods in `settings.txt`) | Unsupported | Not detected as such; their mods would not be enabled ([EU4 adapter](games/eu4.md)). |
| Any game run through Proton on Linux | Unsupported for Workshop acquisition | The game ships a Windows Steam API DLL that the native Linux helper cannot load; the manual fallback is the only path. Detection and launch of Proton installations are not designed. |
| Workshop acquisition with Steam offline or not signed in | Unsupported (by design) | Installed content is still accepted; network operations wait until Steam is online. |
| Mods from Paradox Mods (mods.paradoxplaza.com) or other non-Steam sources | Unsupported | No provider exists; such mods can be used as local mods. |
| 32-bit game builds | Unsupported | The helper is built 64-bit; none of the target games needs a 32-bit Steam API library. |
| Flatpak or Snap Steam on Linux | Unverified | Steam roots are searched; whether the helper can reach the client's IPC from outside the sandbox is untested. |

## Changing this matrix

Run the procedure in [`docs/validation.md`](validation.md), add the results
row there (and evidence under `docs/evidence/` when it does not fit in a
table), then update the affected cells with the date and game version. Record
failures too: a failed check moves a cell to *Unsupported* with the reason, or
stays *unverified* with a link to the issue.
