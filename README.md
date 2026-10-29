# Paradox Mod Manager

A compact, cross-platform desktop mod manager for Paradox games. It keeps named
playsets per game, shares a playset's exact mod list, order and enabled flags as
a copyable code (`PMM1.…`), acquires missing Steam Workshop mods through the
user's own Steam client, and starts the game directly with the selected mods,
without the Paradox launcher.

The first release targets **Stellaris** and **Europa Universalis IV** on Windows,
Linux and macOS. It needs no server, account or telemetry.

> **Status: pre-release (0.1.0 in development).** The core libraries below are
> implemented and covered by automated tests. The Qt desktop application
> (`pmm-app`), the command-line tool (`pmm-cli`), the EU4 and Stellaris game
> adapters and the application services are being implemented. No end-to-end
> validation has been recorded yet; see the [support matrix](docs/support-matrix.md).

## Feature status

Status labels follow [`docs/feasibility.md`](docs/feasibility.md): **verified**
means observed on a real installation with recorded evidence; **implemented**
means code and automated tests exist but the behaviour has not been observed
against the real game or store; **in progress** means not available yet;
**unsupported** means not available, with the reason documented.

| Capability | Status | Notes |
| --- | --- | --- |
| Share codes: `PMM1` encoding, strict validation, size limits, frozen regression fixtures | Implemented | [Protocol](docs/share-code-protocol.md), versioned independently (protocol 1, schema 1) |
| Playset editing, filtering rules, launch readiness, dependency checks, import planning | Implemented | `src/core` |
| Application database: migrations, upgrade backups, playset undo history | Implemented | [Persistence](docs/persistence.md) |
| Steam root, library, app manifest and Workshop manifest discovery | Implemented | Parsers tested against sanitized copies of real Steam files |
| Journaled game-config writes with backups, rollback and crash recovery | Implemented | `src/platform/file_transaction.hpp` |
| Workshop helper: Steam session and item state in the game's app context | **Verified** read-only, 2026-10-29, Stellaris 4.5.2, Windows 11 | [Probe evidence](docs/evidence/steam-helper-probe.md) |
| Workshop subscribe and download | Implemented, unverified | Tested only against a fake Steam API library |
| Manual Workshop fallback (open the item in Steam, then rescan) | Implemented, unverified | Limited: does not satisfy automatic download |
| Stellaris adapter: detection, mod inventory, launch configuration | In progress | Game files and `dlc_load.json` format observed read-only on Windows |
| Europa Universalis IV adapter | In progress, entirely unverified | Installation (1.37.5.0) inspected read-only; no EU4 capability has been exercised yet |
| Desktop application (Qt Quick) and `pmm-cli` | In progress | [UI contract](docs/ui-contract.md) |
| Direct launch bypassing the Paradox launcher | In progress, unverified | |
| In-game verification of loaded mods and order | Not yet performed | Procedure: [validation](docs/validation.md) |
| Linux and macOS | Unverified | Code paths written; not yet built or run on those systems |
| Hearts of Iron IV, Crusader Kings III, Victoria 3 | Unsupported | Known to the catalog only; their share codes are rejected as "not supported by this version" |

## Prerequisites

All platforms:

- CMake 3.25 or newer and Ninja.
- A C++20 compiler (see below).
- Network access on the first configure: CMake downloads the pinned
  dependencies (nlohmann/json, zlib, SQLite, Catch2) and verifies their SHA-256
  ([ADR 0002](docs/adr/0002-dependencies.md)).
- Qt 6.5 or newer for the application presets (`debug`, `release`, `ci`);
  Qt 6.10.3 is the tested version. Required modules: Core, Gui, Qml, Quick,
  Quick Controls 2, Network, Concurrent (all part of the default Qt desktop
  installation). The `core-debug` and `core-sanitize` presets need no Qt.
- Python 3 only for [`tools/probe-mods`](tools/probe-mods/probe_mods.py) and
  the helper scripts in `scripts/`.

Install Qt with the [online installer](https://www.qt.io/download-qt-installer-oss)
or with [aqtinstall](https://github.com/miurahr/aqtinstall):

```sh
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O C:/Qt   # Windows
python -m aqt install-qt linux desktop 6.10.3 linux_gcc_64 -O ~/Qt           # Linux
python -m aqt install-qt mac desktop 6.10.3 clang_64 -O ~/Qt                 # macOS
```

Then point `QT_ROOT_DIR` at the kit, for example `C:/Qt/6.10.3/msvc2022_64`,
`~/Qt/6.10.3/gcc_64` or `~/Qt/6.10.3/macos`. When neither `QT_ROOT_DIR`,
`CMAKE_PREFIX_PATH` nor `Qt6_DIR` is set, configure uses the newest kit found
under `C:/Qt` or `~/Qt` and prints which one.

VS Code (CMake Tools): open the folder and select a configure preset. On
Windows the presets declare the x64 MSVC toolset, so CMake Tools loads the
Visual Studio developer environment for the Ninja generator. A machine-specific
`CMakeUserPresets.json` (gitignored) can pin the Qt kit and compiler, e.g. a
preset inheriting `debug` with `"environment": {"QT_ROOT_DIR": "..."}` and
`"cacheVariables": {"CMAKE_CXX_COMPILER": "cl", "CMAKE_C_COMPILER": "cl"}`.
In a plain terminal on Windows, load the environment first (Developer
PowerShell, or `source scripts/msvc-env.sh` in Git Bash); otherwise configure
stops with "cl ... was not found in the PATH".

| OS | Compiler | Tested | Additional packages |
| --- | --- | --- | --- |
| Windows 10/11 x64 | MSVC with the "Desktop development with C++" workload | MSVC 19.51 (Visual Studio 2026) | Ninja (bundled with Visual Studio's CMake component, or `winget install Ninja-build.Ninja`) |
| Linux | GCC 13 or newer | Not yet (CI) | Ubuntu 24.04: `sudo apt install build-essential cmake ninja-build libgl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 libxcb-cursor0 libfontconfig1` |
| macOS | AppleClang 15 or newer (Xcode command line tools) | Not yet (CI) | `brew install cmake ninja` |

## Build and test

Configure, build and test presets share the same names
([`CMakePresets.json`](CMakePresets.json)); build trees go to `build/<preset>`.

| Preset | Contents | Qt |
| --- | --- | --- |
| `core-debug` | Libraries, Steam helper and unit tests, Debug | No |
| `core-sanitize` | Same with AddressSanitizer + UBSan and warnings as errors (Linux/macOS) | No |
| `debug` | Everything including the desktop application, Debug | Yes |
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
export QT_ROOT_DIR=~/Qt/6.10.3/gcc_64             # PowerShell: $env:QT_ROOT_DIR = "C:/Qt/6.10.3/msvc2022_64"
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

On Windows the compiler must be on `PATH`. Either open a *Developer PowerShell
for VS* / *x64 Native Tools Command Prompt*, or, in Git Bash, import the
Visual Studio environment into the current shell first:

```sh
source scripts/msvc-env.sh
```

Formatting and static analysis: [`CONTRIBUTING.md`](CONTRIBUTING.md).

## Running

The desktop application target `pmm-app` and the command-line tool `pmm-cli`
are in progress. Once they land, build them with
`cmake --build --preset debug --target pmm-app` (or `pmm-cli`) and start them
from the build tree. `pmm-steam-helper` is started by the application on
demand; it is not meant to be run by hand except for diagnostics
([Troubleshooting](docs/troubleshooting.md)).

Real game data is only read during scans. Configuration is written only when
you press **Play**, through a journaled transaction with backups
([ADR 0005](docs/adr/0005-journaled-config-writes.md)).

## Packaging

```sh
cmake --preset release -DPMM_BUILD_TESTS=OFF
cmake --build --preset release
cpack --config build/release/CPackConfig.cmake -B dist
```

This produces `ParadoxModManager-<version>-<os>-<arch>` packages: ZIP (plus an
NSIS installer when `makensis` is available) on Windows, a `.dmg` on macOS and
a `.tar.gz` on Linux. Qt deployment, code signing and LGPL compliance are
described in [`docs/packaging.md`](docs/packaging.md).

## Project layout

```text
CMakeLists.txt, CMakePresets.json   build entry points
cmake/            warnings, pinned dependencies, packaging
src/core/         domain model, playset logic, readiness, import planning, logging
src/platform/     OS paths, filesystem, processes, Steam discovery, file transactions
src/sharing/      share-code protocol (manifest, codec, envelope)
src/persistence/  SQLite database, migrations, repositories
src/providers/    Workshop helper protocol, acquisition state machine, metadata, fingerprints
src/games/        game adapter interface (EU4 and Stellaris adapters in progress)
src/app/          Qt application layer (in progress)
ui/               QML module (in progress)
tools/steam-helper/  pmm-steam-helper (loads the game's own Steam API library)
tools/probe-mods/    observable probe mods for in-game load-order checks
tests/            Catch2 unit tests and fixtures
docs/             architecture, decisions, protocol, support matrix, validation
packaging/        license texts, macOS entitlements, Linux desktop entry
scripts/          developer scripts (formatting, MSVC environment, share-code tool)
```

## Documentation

- [Architecture](docs/architecture.md) and [decision records](docs/adr/README.md)
- [Support matrix](docs/support-matrix.md) and [feasibility record](docs/feasibility.md)
- [Share-code protocol](docs/share-code-protocol.md)
- [Steam Workshop acquisition](docs/steam-workshop.md) and [probe evidence](docs/evidence/steam-helper-probe.md)
- [Persistence](docs/persistence.md)
- [UI contract](docs/ui-contract.md)
- [Manual validation procedure](docs/validation.md)
- [Packaging](docs/packaging.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Changelog](CHANGELOG.md), [contributing](CONTRIBUTING.md), [third-party notices](THIRD_PARTY_NOTICES.md)

## Troubleshooting

Common problems (Steam not running, private or deleted Workshop items,
configuration permission errors, interrupted launches, a database from a newer
version) and the locations of logs and diagnostics are covered in
[`docs/troubleshooting.md`](docs/troubleshooting.md).

## License

MIT, see [`LICENSE`](LICENSE). Third-party components and the Qt LGPLv3
obligations are listed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
This project is not affiliated with Paradox Interactive or Valve.
