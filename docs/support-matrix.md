# Support matrix

What works for which game, operating system and store, and how we know.
Application portability does not imply that every game works on every OS:
support for a combination depends on the game's own executable and a validated
integration (spec section 1).

Last reviewed: 2026-10-29, application 0.1.0 (in development).

## Status legend

| Status | Meaning |
| --- | --- |
| **Verified (date, version)** | Observed on a real installation; the evidence is linked. |
| **Implemented, unverified** | Code and automated tests exist, but the behaviour has not been observed against the real game or store on that OS. |
| **In progress** | Being implemented; not available yet. Treat as unverified. |
| **Unsupported** | Not available; the reason is given. |

Rules: a cell changes to *Verified* only with recorded evidence (date, game
version, OS, distribution, link to `docs/validation.md` results or
`docs/evidence/`). Untested combinations are **unverified, not supported**.
Nothing here is inferred from another OS or another game.

Verified so far (development machine: Windows 11 Enterprise 23H2, Steam):

- Stellaris "Cygnus v4.5.2 (9776)", Steam build 25664273: installation, Steam
  library, user-data folder, `dlc_load.json` format, mod descriptors and
  launcher settings inspected read-only on 2026-10-29
  ([feasibility](feasibility.md) §1-2).
- The same installation: `pmm-steam-helper` initialised the Steam API in the
  game's app context and read session facts and Workshop item state on
  2026-10-29, read-only ([evidence](evidence/steam-helper-probe.md)).

Observed, but not a verified capability: the Europa Universalis IV
installation ("EU4 v1.37.5.0 Inca (491d)", Steam build 15918133) was inspected
read-only on 2026-10-29 (`launcher-settings.json`, `dlc_load.json`,
descriptors; [feasibility](feasibility.md) §1). That records the file layout
only; no EU4 capability has been exercised, so every EU4 cell below is
unverified.

Nothing else has been observed. Linux and macOS were not available.

## Application build

| Platform | Compiler | Core libraries + unit tests | Desktop application | Packages |
| --- | --- | --- | --- | --- |
| Windows 11 x64 | MSVC 19.51 (VS 2026) | 256 tests pass locally with `core-debug` and `ci` (Qt 6.10.3, warnings as errors), 2026-10-29 | In progress | ZIP config validated with the helper only (2026-10-29) |
| Windows (CI, `windows-2025`) | MSVC from the runner image | Unverified until the first CI run | In progress | Unverified |
| Linux (CI, `ubuntu-24.04`) | GCC 13 | Unverified until the first CI run | In progress | Unverified |
| macOS (CI, `macos-15`) | AppleClang | Unverified until the first CI run | In progress | Unverified |

## Stellaris (Steam app 281990)

| Capability | Windows · Steam | Linux · Steam (native) | macOS · Steam | Other stores (GOG, Paradox Store, Microsoft Store), any OS |
| --- | --- | --- | --- | --- |
| Detect installation and user data | Layout **Verified (2026-10-29, 4.5.2)** read-only; adapter in progress | In progress, unverified | In progress, unverified | In progress, unverified (manual location) |
| Mod inventory (descriptors, Workshop content) | Layout **Verified (2026-10-29, 4.5.2)** read-only; adapter in progress | In progress, unverified | In progress, unverified | In progress, unverified (local mods only) |
| Playsets (create, order, enable, persist, undo) | In progress (core logic and persistence implemented and unit tested) | In progress | In progress | In progress |
| Share codes (export, import preview, exact reconciliation) | Implemented, unverified (protocol and import planning unit tested; UI in progress) | Implemented, unverified | Implemented, unverified | Implemented, unverified |
| Workshop: Steam session and item state | **Verified (2026-10-29, 4.5.2)** read-only probe | Implemented, unverified (not yet compiled) | Implemented, unverified (not yet compiled) | Unsupported: requires the game on a Steam account |
| Workshop: subscribe and download | Implemented, unverified (fake library tests only) | Implemented, unverified | Implemented, unverified | Unsupported: requires the game on a Steam account |
| Workshop: manual fallback (open in Steam, rescan) | Implemented, unverified | Implemented, unverified | Implemented, unverified | Unsupported |
| Direct launch without the Paradox launcher | In progress (approach documented in [feasibility](feasibility.md) §2) | In progress, unverified | In progress, unverified | In progress, unverified |
| In-game verification of enabled mods and order | Unverified (procedure in [validation](validation.md)) | Unverified | Unverified | Unverified |

## Europa Universalis IV (Steam app 236850)

EU4 1.37.5.0 is installed on the development machine, but it was only
inspected read-only: no detection by the adapter, Workshop helper session or
launch has been run against it. Every EU4 cell is therefore at best
implemented, unverified. Known differences from Stellaris that the validation
must cover: the install folder has no `steam_appid.txt` (a direct start needs
`SteamAppId`/`SteamGameId` in the environment), `userdir.txt` can relocate the
user-data folder, and `exeArgs` contains `-enabletelemetry`, which the
direct launch is designed to drop by default ([feasibility](feasibility.md) §1).

| Capability | Windows · Steam | Linux · Steam (native) | macOS · Steam | Other stores, any OS |
| --- | --- | --- | --- | --- |
| Detect installation and user data | In progress, unverified | In progress, unverified | In progress, unverified | In progress, unverified (manual location) |
| Mod inventory | In progress, unverified | In progress, unverified | In progress, unverified | In progress, unverified (local mods only) |
| Playsets | In progress (core logic and persistence implemented and unit tested) | In progress | In progress | In progress |
| Share codes | Implemented, unverified | Implemented, unverified | Implemented, unverified | Implemented, unverified |
| Workshop: Steam session and item state | Implemented, unverified | Implemented, unverified | Implemented, unverified | Unsupported: requires the game on a Steam account |
| Workshop: subscribe and download | Implemented, unverified | Implemented, unverified | Implemented, unverified | Unsupported: requires the game on a Steam account |
| Workshop: manual fallback | Implemented, unverified | Implemented, unverified | Implemented, unverified | Unsupported |
| Direct launch without the Paradox launcher | In progress, unverified | In progress, unverified | In progress, unverified | In progress, unverified |
| In-game verification of enabled mods and order | Unverified | Unverified | Unverified | Unverified |

## Other combinations

| Combination | Status | Reason |
| --- | --- | --- |
| Hearts of Iron IV, Crusader Kings III, Victoria 3 | Unsupported | In the game catalog only (`adapterAvailable = false`); share codes for them are rejected with `UnsupportedGame`. The adapter interface is designed for them; no adapter exists. |
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
