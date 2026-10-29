# Evidence: Windows release package

**Date:** 2026-10-29, 23:10-23:45 local time (UTC+4)
**Question:** does `cpack` produce a Windows package that contains what
[`../packaging.md`](../packaging.md) promises (and nothing else), and does the
packaged application run from an extracted copy on a machine path without Qt
or Visual Studio?
**Result:** **Yes**, after the fixes below. Final package:
`ParadoxModManager-0.1.0-windows-x86_64.zip`, 21,943,565 bytes, SHA-256
`8f3a23ac92de4a382d81a2233fae34fe3c00e2d84ac4b306a38da44f41617f42`, at
`C:/Users/<user>/AppData/Local/Temp/claude/E--Documents-Projects-pdxModManager/7e18f5ad-6f5d-4814-aa1a-1240e1a02124/scratchpad/release/dist/`
(a temporary folder; packages are never committed).

Every run used a scratch profile and a verification switch (`--screenshot`,
`--self-test`), which imply `--read-only` and `--no-steam`. No game was
started, nothing was written to the game or Steam folders (`dlc_load.json` of
both games still dated 2026-10-27 and 2026-08-23 afterwards), and the Steam
helper was never started against Steam. Paths are shortened: `<scratch>` is
the private scratch folder above (`.../scratchpad/release`), `<user>` the
Windows user name.

## Environment

| Item | Value |
| --- | --- |
| OS | Windows 11 Enterprise 23H2 (10.0.22631), x64 |
| Compiler | MSVC 19.51 (Visual Studio 18 Enterprise, tools 14.51.36231), x64 developer shell (`Enter-VsDevShell`) |
| CMake / generator | 4.4.4 / Ninja |
| Qt | 6.10.3 `msvc2022_64` (found automatically at `C:/Qt/6.10.3/msvc2022_64`) |
| MSVC runtime | `VC/Redist/MSVC/14.51.36231`, DLL file versions 14.51.36247.0 |
| NSIS | not installed: ZIP only |
| Code | branch `agent/release` (base `d6d4201`) |
| Games (read-only) | Stellaris 4.5.2, EU4 1.37.5 (Steam) |

## Commands

From the worktree, in the x64 developer shell:

```sh
cmake --preset release -DPMM_BUILD_TESTS=OFF -B <scratch>/build
cmake --build <scratch>/build
cpack --config <scratch>/build/CPackConfig.cmake -B <scratch>/dist
```

Final run from an empty build folder: configure, 243 build steps and `cpack`
in 42 s, **0 warnings** in the configure and build logs (`/W4`). `cpack`
installs into its staging folder, which runs Qt's deployment script
(`windeployqt` with the options in [`../packaging.md`](../packaging.md#qt-deployment))
and then `packaging/windows/prune-qt-deployment.cmake`.

The ZIP was extracted with `unzip` into `<scratch>/x/` and run from Git Bash
with a clean `PATH` (only the Windows folders; the user's normal `PATH`
contains other applications that ship Qt DLLs):

```sh
APP=<scratch>/x/ParadoxModManager-0.1.0-windows-x86_64/bin/pmm-app.exe
export PATH="/c/Windows/System32:/c/Windows:/c/Windows/System32/Wbem"
QSG_INFO=1 $APP --profile <scratch>/profile --verbose --game stellaris --screenshot 1-stellaris-clean.png
$APP --profile <scratch>/profile --game stellaris --self-test "config,dump:stellaris-dump.json"
$APP --profile <scratch>/profile --game eu4 --self-test "config,dump:eu4-dump.json"
$APP --profile <scratch>/profile --game stellaris --select-row 2 --screenshot 4-stellaris-playset.png
$APP --profile <scratch>/profile --game eu4 --theme dark --open export --screenshot 5-eu4-export-dark.png
QSG_INFO=1 QSG_RHI_PREFER_SOFTWARE_RENDERER=1 $APP --profile <scratch>/profile-warp --verbose --game eu4 --screenshot 6-eu4-warp.png
<scratch>/x/ParadoxModManager-0.1.0-windows-x86_64/bin/pmm-steam-helper.exe < /dev/null
```

The loaded modules were listed from PowerShell with the same `PATH`
(`Start-Process ... --screenshot-delay-ms 6000`, then `$p.Modules` after 4 s).
For comparison the build-tree Debug application (`build/debug/bin/pmm-app.exe`,
Qt's `bin` on `PATH`) ran the same two `--self-test` commands in another
scratch profile.

## Package contents

80 files (107 ZIP entries with folders), 55,954,364 bytes unpacked.

| Folder | Bytes | Files |
| --- | ---: | --- |
| `bin/` | 53,158,070 | `pmm-app.exe` (5,548,544), `pmm-steam-helper.exe` (214,016), 22 `Qt6*.dll`, `qt.conf` (`Prefix = ..`), `concrt140.dll`, `msvcp140.dll`, `msvcp140_1.dll`, `msvcp140_2.dll`, `msvcp140_atomic_wait.dll`, `msvcp140_codecvt_ids.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` |
| `plugins/` | 2,217,208 | `platforms/qwindows`, `imageformats/{qgif,qico,qjpeg,qsvg}`, `iconengines/qsvgicon`, `tls/{qschannelbackend,qcertonlybackend}`, `networkinformation/qnetworklistmanager` |
| `qml/` | 512,863 | `QtQml` (+ `Models`, `WorkerScript`), `QtQuick` (+ `Controls` with `impl` and `Basic` only, `Templates`, `Layouts`, `Shapes`, `Dialogs` + `quickimpl`, `Effects`, `Window`), `Qt/labs/folderlistmodel` |
| `licenses/` | 42,801 | `LGPL-3.0.txt`, `GPL-3.0.txt` |
| root | | `LICENSE`, `README.md`, `CHANGELOG.md`, `THIRD_PARTY_NOTICES.md` |

The 22 Qt libraries: Core, Gui, Network, OpenGL, Qml, QmlMeta, QmlModels,
QmlWorkerScript, Quick, QuickControls2, QuickControls2Basic,
QuickControls2BasicStyleImpl, QuickControls2Impl, QuickDialogs2,
QuickDialogs2QuickImpl, QuickDialogs2Utils, QuickEffects, QuickLayouts,
QuickShapes, QuickTemplates2, Svg, LabsFolderListModel (all 6.10.3).

Checks on the extracted tree:

- `pmm-app.exe` and `pmm-steam-helper.exe` are side by side in `bin/`.
- No test executables, no `fake_steam_api`/`steam_api*` library, no Catch2, no
  PDBs, no translations, no `opengl32sw.dll`, no `d3dcompiler_47.dll`,
  `dxcompiler.dll`, `dxil.dll` or `icuuc.dll`.
- No build-tree or user paths: `grep -ralE "<user>|scratchpad|worktree"` over
  all 80 files finds nothing; the executables contain only `pmm-app.pdb` and
  `pmm-steam-helper.pdb` as PDB names.
- Version resources (PowerShell `VersionInfo`): `pmm-app.exe` "Paradox Mod
  Manager", `pmm-steam-helper.exe` "Paradox Mod Manager Steam Workshop
  helper"; both FileVersion/ProductVersion 0.1.0 (binary 0.1.0.0), product
  "Paradox Mod Manager", copyright "Copyright (c) 2026 Giorgi Apriashvili. MIT
  License.", correct `OriginalFilename`. There is no application icon in the
  repository yet.
- The longest path below the package folder has 72 characters
  (`qml/QtQuick/Controls/Basic/impl/qtquickcontrols2basicstyleimplplugin.dll`).

## Findings and fixes

The first package (same commands, before the fixes) was 47,783,373 bytes with
165 files (about 121 MB unpacked).

| # | Finding | Fix | Effect |
| --- | --- | --- | --- |
| 1 | `bin/icuuc.dll` was a copy of `C:\Windows\System32\icuuc.dll` (identical MD5): windeployqt deploys the system ICU that `Qt6Core.dll` imports. Redistributing a Windows system file is wrong; Windows 10 1703+ has it. | Removed after deployment by `packaging/windows/prune-qt-deployment.cmake` (unversioned `icuuc.dll`/`icuin.dll` only). | At run time `icuuc.dll` and `icu.dll` load from `System32`. |
| 2 | `d3dcompiler_47.dll` (4.7 MB), `dxcompiler.dll` and `dxil.dll` (15.8 MB) were copied from the Windows SDK's `Redist/D3D/x64` folder (identical sizes), which windeployqt finds through the Visual Studio environment, so the package depended on the build shell. | `--no-system-d3d-compiler --no-system-dxc-compiler` | Not loaded at run time (D3D11 with Qt's shaders; `D3DCompiler_47.dll` is part of Windows). |
| 3 | `opengl32sw.dll` (20.6 MB) shipped although Qt Quick uses Direct3D 11. | `--no-opengl-sw` | WARP run below renders without a GPU driver. |
| 4 | 32 Qt translation files (7.1 MB) although the application installs no translator. | `NO_TRANSLATIONS` | |
| 5 | 11 `qmltooling` plugins (QML debugger and profiler hooks) and the TUIO `generic` plugin. | `EXCLUDE_PLUGIN_TYPES qmltooling generic` | |
| 6 | Six unused Quick Controls styles plus `NativeStyle` (QML modules and 10 `Qt6QuickControls2*` libraries), deployed through the optional imports of `QtQuick.Controls`; the application selects Basic. They also held the deepest paths (86 characters below the package folder, 72 now). | Removed by the prune script, which fails if Basic is missing. | Package 23.4 MB after fixes 1-6. |
| 7 | The executables embedded the absolute PDB path (`C:\Users\<user>\...\build\bin\pmm-app.pdb`). | `/PDBALTPATH:%_PDB%` for the shipped executables (`cmake/Packaging.cmake`). | Only the file name remains. |
| 8 | `RelWithDebInfo` links with `/INCREMENTAL` (CMake default): `pmm-app.exe` 11.75 MB, helper 0.93 MB. | `/INCREMENTAL:NO /OPT:REF /OPT:ICF` for the shipped executables in `RelWithDebInfo`. | 5.55 MB and 0.21 MB; package 21.9 MB. No `LNK4075`. |
| 9 | No version resource. | `packaging/windows/version.rc.in`, configured per executable. | See above. |
| 10 | CMake warning CMP0177 (`DESTINATION ./licenses` not normalized). | Licenses destination computed without `./`. | Configure log clean. |
| 11 | `THIRD_PARTY_NOTICES.md` listed Qt Concurrent (not linked or shipped) and not the Qt libraries the package does contain. | Notices list the shipped Qt modules, libraries and plugins, and state that no Windows system files are redistributed. | |
| 12 | Extracted under `<scratch>/extract1/` the UI did not load (exit code 3): "Cannot load library ...\qml\QtQuick\Controls\Basic\impl\qtquickcontrols2basicstyleimplplugin.dll: The filename or extension is too long." The full path had 254 characters; extracted under `<scratch>/x/` (247 characters) it worked. | Documented in [`../packaging.md`](../packaging.md#windows) (keep the folder containing `bin\` below about 170 characters); fix 6 removed the deeper style paths. | Long-path support was not enabled on this machine; not changed. |

## Run results (final package, clean `PATH`)

| Run | Exit | Result |
| --- | --- | --- |
| Stellaris `--screenshot`, empty profile | 0 | Main window: both games detected (Stellaris v4.5.2, EU4 v1.37.5.0), "No playset yet", Workshop downloads off, Play disabled by the read-only blocker. Log: `Creating QRhi with backend D3D11`, hardware adapter (NVIDIA) used. |
| Stellaris `--self-test "config,dump:..."` | 0 | `playset 1 has 17 entries (17 enabled)`, 17 entries written. |
| EU4 `--self-test "config,dump:..."` | 0 | `playset 2 has 9 entries (9 enabled)`, 9 entries written. |
| Stellaris `--select-row 2 --screenshot` | 0 | Playset "Current game configuration", 17 mods (Workshop and local), 9 with version warnings, details of the selected Workshop mod. |
| EU4 `--theme dark --open export --screenshot` | 0 | Dark theme, export dialog with a 685-character share code (Qt Quick Dialogs `FileDialog` type loaded). |
| EU4 `--screenshot` with `QSG_RHI_PREFER_SOFTWARE_RENDERER=1` | 0 | Adapter "Microsoft Basic Render Driver" (WARP) used; window rendered correctly. |
| Loaded modules during a screenshot run | 0 | 129 modules, 34 of them from the package: all Qt libraries and plugins, `MSVCP140.dll`, `MSVCP140_1.dll`, `MSVCP140_2.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll` from `bin/`; `ucrtbase.dll`, `icuuc.dll`, `icu.dll`, `d3d11.dll`, `dxgi.dll` from `System32`; no `d3dcompiler_47.dll`, no `opengl32.dll`. |
| `pmm-steam-helper.exe` without arguments | 2 | `{"event":"ready","protocol":1,"ok":false,"error":"invalid_arguments",...}`: the packaged helper starts with the app-local runtime and loads no Steam library without `--steam-api`. |
| Dumps vs. build-tree Debug application | | `stellaris-dump.json` and `eu4-dump.json` byte-identical to the build tree's (`cmp`). |
| Profile logs | | 0 `WARN`/`ERROR` records (no QML warnings). |
| Extracted folder after all runs | | Still exactly the 80 packaged files. |

All screenshots were inspected; they are not committed (they show local
paths).

**Helper lookup.** `QtWorkshopSessionHost::defaultHelperPath()` returns
`PMM_STEAM_HELPER` when set, otherwise `pmm-steam-helper.exe` in
`QCoreApplication::applicationDirPath()`, which for the package is `bin/`,
where the helper is. The verification switches imply `--no-steam`, so the
application did not start the helper in these runs, by design.

## Qt LGPLv3 checklist

| Item ([`../packaging.md`](../packaging.md#qt-lgplv3-compliance-in-packages)) | Windows ZIP 2026-10-29 |
| --- | --- |
| Qt linked dynamically, unmodified shared libraries | Yes: 22 `Qt6*.dll` from the official 6.10.3 build, imported by `pmm-app.exe`. |
| Notices and license texts in the package | Yes: `THIRD_PARTY_NOTICES.md`, `licenses/LGPL-3.0.txt`, `licenses/GPL-3.0.txt`. |
| Qt version and source location named | Yes: 6.10.3, download.qt.io and code.qt.io (`THIRD_PARTY_NOTICES.md`). |
| Users can replace the Qt libraries | Yes: separate files in `bin/`, `plugins/` and `qml/`. |
| No GPL-only Qt modules | Yes: only modules from qtbase, qtdeclarative and qtsvg (LGPLv3). |

## Other platforms and tests

- Linux (WSL Arch Linux, GCC 16.2.1, CMake 4.4, no Qt; built under `/tmp`):
  core build with `PMM_BUILD_APP=OFF`, `cpack` produced
  `ParadoxModManager-0.1.0-linux-x86_64.tar.gz` with `bin/pmm-steam-helper`
  and `share/doc/ParadoxModManager/{CHANGELOG.md,LICENSE,README.md,THIRD_PARTY_NOTICES.md}`.
  No package with the application was built.
- macOS: not built.
- `cmake --preset debug && cmake --build --preset debug && ctest --preset debug -j8`
  in the worktree: 0 warnings, 400 of 400 tests passed.

## Not verified

- The NSIS installer (`makensis` not installed).
- A machine without the VC++ Redistributable installed system-wide (the
  module list shows the app-local runtime being used on this one).
- Code signing and SmartScreen behaviour.
- Starting the helper against Steam from the package (excluded on purpose).
