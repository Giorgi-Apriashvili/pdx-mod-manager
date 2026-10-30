<img src="packaging/icons/app-128.png" alt="" width="64" height="64">

# Paradox Mod Manager

A compact desktop mod manager for Paradox games, for Windows only. It keeps named
playsets per game, shares a playset's exact mod list, order and enabled flags as
a copyable code (`PMM1.…`), acquires missing Steam Workshop mods through the
user's own Steam client, and starts the game directly with the selected mods,
without the Paradox launcher.

The first release targets **Stellaris** and **Europa Universalis IV** on Windows
10 and 11. Linux, macOS, the Steam Deck and Proton are not supported
([ADR 0009](docs/adr/0009-windows-only.md)). It needs no server, account or
telemetry.

> **Status: pre-release (0.1.0 in development).** The desktop application
> (`pmm-app`), the EU4 and Stellaris adapters and every layer below them are
> implemented and covered by 467 automated tests. On Windows 11 with Steam,
> both games were started directly by the application with the selected mods
> (checked in the games' own logs), a missing Workshop mod was subscribed and
> downloaded live for each game, and the failure cases behaved as specified
> ([live validation](docs/evidence/live-validation-2026-10-30.md)). **EU4
> ignores the mod order for file overrides** (the mod whose name sorts first
> wins); Stellaris follows it. See the
> [support matrix](docs/support-matrix.md) and the
> [development status](docs/development-status.md). **Nothing has been published
> yet**: a dry run of the release workflow has passed, but its publish step has not
> run, and an update between two published releases has not happened.

![Main window with a real Stellaris installation, read-only verification mode](docs/screenshots/app-main-light.png)

## Feature status

Status labels follow [`docs/feasibility.md`](docs/feasibility.md): **verified**
means observed on a real installation with recorded evidence; **implemented**
means code and automated tests exist but the behaviour has not been observed
against the real game or store; **unsupported** means not available, with the
reason documented.

| Capability | Status | Notes |
| --- | --- | --- |
| Desktop application (Qt Quick): games, playsets, search and filters, drag-and-drop and keyboard reordering, undo, import and export, downloads, Play with its blockers | Implemented; run read-only against the real games on Windows 11 | [Application](docs/app.md), [UI contract](docs/ui-contract.md) |
| Accessibility: keyboard navigation, visible focus, accessible names, WCAG AA contrast, high-DPI | Implemented; checked with UI Automation and at scale factors 1.5 and 2 | [Accessibility](docs/accessibility.md); no screen-reader listening test yet |
| Stellaris and EU4: installation and user-data detection, mod inventory, current configuration, launch plan (executable, arguments, environment, configuration writes) | **Verified** read-only, 2026-10-29, Stellaris 4.5.2 and EU4 1.37.5, Steam, Windows 11 | [Real-data evidence](docs/evidence/real-scan-2026-10-29.md); registered Workshop items without content show as Missing |
| Share codes: `PMM1` encoding, strict validation, size limits, frozen regression fixtures | Implemented | [Protocol](docs/share-code-protocol.md), versioned independently (protocol 1, schema 1) |
| Share-code round trip between two clean profiles (identities, order, enabled flags) | **Verified** read-only, 2026-10-29 and 2026-10-30, both games, Windows 11 | [Round-trip evidence](docs/evidence/app-round-trip-2026-10-30.md) |
| Playset editing, filtering rules, launch readiness, dependency checks, import planning | Implemented | `src/core`, `src/services` |
| Application database: migrations, upgrade backups, playset undo history | Implemented | [Persistence](docs/persistence.md) |
| Journaled game-config writes with backups, rollback and crash recovery | Implemented | [ADR 0005](docs/adr/0005-journaled-config-writes.md) |
| Workshop helper: Steam session and item state in the game's app context | **Verified** read-only, 2026-10-29, Stellaris 4.5.2, Windows 11 | [Probe evidence](docs/evidence/steam-helper-probe.md) |
| Workshop subscribe and download | **Verified** 2026-10-30, EU4 1.37.5 and Stellaris 4.5.2, Windows 11 (one item each, live) | [Live validation](docs/evidence/live-validation-2026-10-30.md): pending job survives a restart; nothing is subscribed until the Web API confirms the item's game |
| Manual Workshop fallback (open the item in Steam, then rescan) | Implemented, unverified | Limited: does not satisfy automatic download |
| Direct launch bypassing the Paradox launcher | **Verified** 2026-10-30, Stellaris 4.5.2 and EU4 1.37.5, Windows 11 | No launcher process; EU4 runs without `steam_appid.txt`; write and start failures restore the previous configuration |
| In-game verification of loaded mods and order | **Verified** 2026-10-30 with probe mods, both games | Stellaris: the later mod in the list wins a shared file. **EU4: the list order is ignored; the mod whose name sorts first wins** ([load order](docs/load-order.md)) |
| Windows package (ZIP with Qt and the C++ runtime) | **Verified** 2026-10-29 | [Packaging evidence](docs/evidence/packaging-2026-10-29.md) |
| Windows installer: per-user, no administrator rights, upgrades in place, a failed upgrade keeps the previous version | **Verified** 2026-10-30 with silent runs in a scratch folder | [Installer evidence](docs/evidence/installer-2026-10-30.md); the wizard was not clicked through; not code signed |
| Update button: check GitHub on request, verify the installer's SHA-256, install and restart | Implemented, unverified end to end | [Updates](docs/updates.md): no published release exists yet. The installer's update run was tested with the exact arguments and the real application's mutex, with a stand-in for the relaunch |
| GitHub releases from a workflow | Dry run passed 2026-10-30; publishing not run yet | [Releasing](docs/releasing.md) |
| Hearts of Iron IV, Crusader Kings III, Victoria 3 | Unsupported | Known to the catalog only; their share codes are rejected as "not supported by this version" |

## Prerequisites

- Windows 10 or 11, x64.
- CMake 3.25 or newer and Ninja.
- A C++20 compiler (see below).
- Network access on the first configure: CMake downloads the pinned
  dependencies (nlohmann/json, zlib, SQLite, Catch2) and verifies their SHA-256
  ([ADR 0002](docs/adr/0002-dependencies.md)).
- Qt 6.5 or newer for the application presets (`debug`, `release`, `ci`);
  Qt 6.10.3 is the tested version. Required modules: Core, Gui, Qml, Quick,
  Quick Controls 2, Network, Concurrent (all part of the default Qt desktop
  installation). The `core-debug`, `core-sanitize` and `core-clang-cl` presets
  need no Qt.
- Python 3 only for [`tools/probe-mods`](tools/probe-mods/probe_mods.py) and
  the helper scripts in `scripts/` and `ui/dev/`.

Install Qt with the [online installer](https://www.qt.io/download-qt-installer-oss)
or with [aqtinstall](https://github.com/miurahr/aqtinstall):

```sh
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O C:/Qt
```

Then point `QT_ROOT_DIR` at the kit, for example `C:/Qt/6.10.3/msvc2022_64`.
When neither `QT_ROOT_DIR`, `CMAKE_PREFIX_PATH` nor `Qt6_DIR` is set, configure
uses the newest kit found under `C:/Qt` and prints which one.

VS Code (CMake Tools): open the folder and select a configure preset. The
presets declare the x64 MSVC toolset, so CMake Tools loads the
Visual Studio developer environment for the Ninja generator. A machine-specific
`CMakeUserPresets.json` (gitignored) can pin the Qt kit and compiler, e.g. a
preset inheriting `debug` with `"environment": {"QT_ROOT_DIR": "..."}` and
`"cacheVariables": {"CMAKE_CXX_COMPILER": "cl", "CMAKE_C_COMPILER": "cl"}`.
In a plain terminal, load the environment first (Developer
PowerShell, or `source scripts/msvc-env.sh` in Git Bash); otherwise configure
stops with "cl ... was not found in the PATH".

| Compiler | Tested | Additional packages |
| --- | --- | --- |
| MSVC with the "Desktop development with C++" workload | MSVC 19.51 (Visual Studio 2026) | Ninja (bundled with Visual Studio's CMake component, or `winget install Ninja-build.Ninja`) |
| clang-cl, only for the `core-clang-cl` preset | LLVM 22 | LLVM (`winget install LLVM.LLVM`), and the MSVC workload above for its headers and libraries |

## Build and test

Configure, build and test presets share the same names
([`CMakePresets.json`](CMakePresets.json)); build trees go to `build/<preset>`.

Build from a folder whose path has only ASCII characters: Qt's `qmlimportscanner`
fails during configure otherwise. This only affects building; the installed
application runs from any folder.

| Preset | Contents | Qt |
| --- | --- | --- |
| `core-debug` | Libraries, Steam helper and unit tests, Debug | No |
| `core-sanitize` | Same with AddressSanitizer (MSVC) and warnings as errors | No |
| `core-clang-cl` | Same with clang-cl, UBSan and warnings as errors | No |
| `debug` | Everything including the desktop application and its tests, Debug | Yes |
| `release` | Everything, RelWithDebInfo | Yes |
| `ci` | Everything, RelWithDebInfo, warnings as errors | Yes |

Core libraries only:

```sh
cmake --preset core-debug
cmake --build --preset core-debug
ctest --preset core-debug                         # all tests
ctest --preset core-debug -L pmm_sharing_tests    # one suite (label = test executable)
```

With the application (Qt):

```sh
export QT_ROOT_DIR=C:/Qt/6.10.3/msvc2022_64       # PowerShell: $env:QT_ROOT_DIR = "C:/Qt/6.10.3/msvc2022_64"
cmake --preset debug
cmake --build --preset debug
ctest --preset debug                              # 467 tests, 65 of them pmm_app_tests
```

The application tests (`-L pmm_app_tests`) run on Qt's offscreen platform;
the test properties set `QT_QPA_PLATFORM=offscreen` and put Qt's `bin` folder on
`PATH`.

The compiler must be on `PATH`. Either open a *Developer PowerShell
for VS* / *x64 Native Tools Command Prompt*, or, in Git Bash, import the
Visual Studio environment into the current shell first:

```sh
source scripts/msvc-env.sh
```

Formatting and static analysis: [`CONTRIBUTING.md`](CONTRIBUTING.md).

## Installing

Once a version is published, download
`ParadoxModManager-<version>-windows-x86_64-setup.exe` from
[Releases](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases) and run
it. It needs no administrator rights and installs to
`%LOCALAPPDATA%\Programs\ParadoxModManager`. The setup is not code signed yet, so
SmartScreen may warn (*More info > Run anyway*). Your playsets and settings are kept
outside that folder, so upgrading or uninstalling leaves them alone.

To update, open **Settings > Updates > Check for updates**. An installed copy can
then download a new version, verify it, install it and restart. The application
only contacts GitHub when you press the button ([what it contacts and
verifies](docs/updates.md)). A ZIP copy gets a link to the release page instead.

## Running

From the build tree: `pmm-app` is written to `build/<preset>/bin/`, next to
`pmm-steam-helper`. Qt's DLLs must be on `PATH`:

```sh
export PATH="/c/Qt/6.10.3/msvc2022_64/bin:$PATH"   # PowerShell: $env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
build/debug/bin/pmm-app.exe                         # normal profile
build/debug/bin/pmm-app.exe --profile "<empty folder>" --read-only
```

From a package: extract the ZIP and run `bin\pmm-app.exe` (keep the path of the
folder that contains `bin\` below about 170 characters;
[packaging](docs/packaging.md)).

Options: `--profile <dir>` keeps every application file (database, journal,
logs, cache) in `<dir>`; `--game <key>` selects `stellaris` or `eu4`;
`--no-steam` never starts the Steam helper; `--read-only` never writes game
files or launches a game; `--verbose` writes debug logs. One instance runs per
profile. Hidden verification switches (`--self-test`, `--screenshot`) drive
the real application without input for headless checks and imply
`--read-only` ([application](docs/app.md#command-line)).

`pmm-steam-helper` is started by the application on demand; it is not meant
to be run by hand except for diagnostics
([troubleshooting](docs/troubleshooting.md)).

Real game data is only read during scans. Configuration is written only when
you press **Play**, through a journaled transaction with backups
([ADR 0005](docs/adr/0005-journaled-config-writes.md)). Keyboard use, the
shortcut list (F1) and the accessibility checks are described in
[`docs/accessibility.md`](docs/accessibility.md).

## Packaging

```sh
cmake --preset release -DPMM_BUILD_TESTS=OFF
cmake --build --preset release
cpack --config build/release/CPackConfig.cmake -B build/release/dist
cmake --build --preset release --target installer   # needs Inno Setup 6
```

This produces `ParadoxModManager-<version>-windows-x86_64.zip` and a per-user
installer, `ParadoxModManager-<version>-windows-x86_64-setup.exe`, in
`build/release/dist`. Run it from an x64 Visual Studio developer
environment. Qt deployment, package contents, code signing and LGPL compliance are
described in [`docs/packaging.md`](docs/packaging.md); the ZIP was verified
on 2026-10-29 ([evidence](docs/evidence/packaging-2026-10-29.md)) and the installer
on 2026-10-30 ([evidence](docs/evidence/installer-2026-10-30.md)). Publishing to
GitHub Releases: [`docs/releasing.md`](docs/releasing.md).

## Project layout

```text
CMakeLists.txt, CMakePresets.json   build entry points
cmake/            warnings, pinned dependencies, packaging
src/core/         domain model, playset logic, readiness, import planning, UTF-8, logging
src/platform/     OS paths, filesystem, processes, Steam discovery, file transactions
src/sharing/      share-code protocol (manifest, codec, envelope)
src/persistence/  SQLite database, migrations, repositories
src/providers/    Workshop helper protocol, acquisition state machine, metadata, fingerprints
src/games/        game adapters: Stellaris and EU4
src/services/     Qt-free use cases: games, playsets, sharing, acquisition, launch
src/app/          Qt application (pmm-app): runtime, services worker, Backend and models
ui/               QML module Pmm.Ui (presentation only) and its preview tools
tools/steam-helper/  pmm-steam-helper (loads the game's own Steam API library)
tools/probe-mods/    observable probe mods for in-game load-order checks
tools/icon-gen/      regenerates the application icon (optional, docs/packaging.md)
tests/            unit tests and fixtures (tests/unit), application tests (tests/app)
docs/             architecture, decisions, protocol, support matrix, validation, evidence
packaging/        license texts, Windows version resource, Qt deployment prune
                  script, installer and its test
scripts/          developer scripts (formatting, MSVC environment, share-code tool)
```

## Documentation

- [Development status and next steps](docs/development-status.md)
- [Architecture](docs/architecture.md), [application services](docs/architecture-services.md) and [decision records](docs/adr/README.md)
- [Desktop application](docs/app.md), [UI contract](docs/ui-contract.md) and [accessibility](docs/accessibility.md)
- [Support matrix](docs/support-matrix.md) and [feasibility record](docs/feasibility.md)
- Game adapters: [Stellaris](docs/games/stellaris.md), [Europa Universalis IV](docs/games/eu4.md), [load order](docs/load-order.md)
- [Share-code protocol](docs/share-code-protocol.md)
- [Steam Workshop acquisition](docs/steam-workshop.md)
- [Persistence](docs/persistence.md)
- [Manual validation procedure](docs/validation.md)
- [Packaging](docs/packaging.md), [releasing](docs/releasing.md) and [updates](docs/updates.md)
- [Troubleshooting](docs/troubleshooting.md)
- Evidence: [Steam helper probe](docs/evidence/steam-helper-probe.md), [real EU4 and Stellaris data](docs/evidence/real-scan-2026-10-29.md), [Windows package](docs/evidence/packaging-2026-10-29.md), [Windows installer](docs/evidence/installer-2026-10-30.md), [application round trip](docs/evidence/app-round-trip-2026-10-30.md)
- [Changelog](CHANGELOG.md), [contributing](CONTRIBUTING.md), [third-party notices](THIRD_PARTY_NOTICES.md)

## Troubleshooting

Common problems (Steam not running, private or deleted Workshop items,
configuration permission errors, interrupted launches, a profile that is
already in use, a database from a newer version) and the locations of logs
and diagnostics are covered in
[`docs/troubleshooting.md`](docs/troubleshooting.md).

## License

MIT, see [`LICENSE`](LICENSE). Third-party components and the Qt LGPLv3
obligations are listed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
This project is not affiliated with Paradox Interactive or Valve.
