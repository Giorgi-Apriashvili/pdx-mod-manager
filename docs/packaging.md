# Packaging

How the Windows release packages are built (the application is Windows only,
[ADR 0009](adr/0009-windows-only.md)), what they contain, how Qt is deployed, and how packages meet the Qt LGPLv3 obligations
([ADR 0007](adr/0007-license.md)). The configuration is
[`cmake/Packaging.cmake`](../cmake/Packaging.cmake) (CPack), included at the end
of the top-level `CMakeLists.txt`.

Status (2026-10-30):

| Package | Status |
| --- | --- |
| ZIP with `pmm-app`, the helper, Qt and the C++ runtime | **Verified**: built with the `release` preset, inspected, and run from an extracted copy without Qt or Visual Studio on `PATH` against the real Stellaris and EU4 installations ([evidence](evidence/packaging-2026-10-29.md)) |
| Per-user installer (Inno Setup) | **Verified** with real installs in a scratch folder: fresh install, update run, running-application handling, uninstall ([evidence](evidence/installer-2026-10-30.md)); the interactive wizard pages have not been looked at |

The [`Release` workflow](../.github/workflows/release.yml) is unverified: it cannot
run before it is on GitHub. Publishing is described in [`releasing.md`](releasing.md).

## Building packages

```sh
cmake --preset release -DPMM_BUILD_TESTS=OFF     # needs QT_ROOT_DIR, see README
cmake --build --preset release
cpack --config build/release/CPackConfig.cmake -B build/release/dist
cmake --build --preset release --target installer   # needs Inno Setup 6
```

Run this from an x64 Visual Studio developer environment. `-B <dir>`
after the preset puts the build tree elsewhere; pass the matching
`CPackConfig.cmake` to `cpack`. The `release` preset builds `RelWithDebInfo`:
the PDBs stay in the build tree (keep them for crash analysis of that
release), only the executables are packaged.

The [`Release` workflow](../.github/workflows/release.yml) runs the same
commands; see [`releasing.md`](releasing.md).

The CPack generator is `ZIP`, which writes
`ParadoxModManager-<version>-windows-x86_64.zip`. The installer is a separate
target, see [below](#windows-installer), which writes
`ParadoxModManager-<version>-windows-x86_64-setup.exe`.

The CPack package gets a `.sha256` file next to it (`CPACK_PACKAGE_CHECKSUM`);
releases carry one `SHA256SUMS.txt` instead. The
architecture comes from the MSVC target architecture or
`CMAKE_SYSTEM_PROCESSOR`. Source archives are made with `git archive`, not CPack.

## Layout

ZIP root or installation folder (the installer adds `unins000.exe` and
`unins000.dat` at the root):

```text
ParadoxModManager-<version>-windows-x86_64/
  bin/pmm-app.exe
  bin/pmm-steam-helper.exe
  bin/Qt6*.dll (22), bin/qt.conf             deployed by windeployqt
  bin/concrt140.dll, msvcp140*.dll,          Microsoft C++ runtime (app-local)
      vcruntime140*.dll
  plugins/platforms/qwindows.dll             platform plugin
  plugins/imageformats/, iconengines/,       image formats, SVG icons, TLS
      tls/, networkinformation/              (Schannel), network information
  qml/QtQml/, qml/QtQuick/ (Controls with    QML modules the UI imports
      the Basic style only), qml/Qt/labs/folderlistmodel/
  LICENSE  README.md  CHANGELOG.md  THIRD_PARTY_NOTICES.md  licenses/
```

The Qt folders are chosen by Qt's deployment tools.

Rules implemented by `cmake/Packaging.cmake`:

- **The Steam helper sits next to the application executable** in `bin/`. The
  application looks for it there. Without a `pmm-app` target (for example the
  `core-debug` preset), the helper still goes to `bin/`.
- `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README.md` and `CHANGELOG.md` go to
  the package root. When the application is packaged, `licenses/LGPL-3.0.txt`
  and `licenses/GPL-3.0.txt` are added.
- The Microsoft C++ runtime DLLs are installed app-locally into
  `bin/` (`InstallRequiredSystemLibraries`, from the compiler's `VC/Redist`
  folder), so the ZIP works without a separate VC++ Redistributable; the
  loader takes them from the application folder before `System32`. The
  Universal CRT is part of Windows 10+.
- Both executables carry a version resource
  ([`packaging/windows/version.rc.in`](../packaging/windows/version.rc.in):
  product, file description, version, copyright). MSVC links them with
  `/PDBALTPATH:%_PDB%`, so they contain the PDB's file name instead of an
  absolute build-tree path, and the `RelWithDebInfo` build is linked with
  `/INCREMENTAL:NO /OPT:REF /OPT:ICF` (about half the size). There is no
  application icon yet; it comes with the UI assets.
- Install rules for targets are guarded with `if(TARGET ...)`, so the
  configuration works whether or not the application target exists.
- Never packaged: Steam binaries or Steamworks SDK files, game files, test
  executables, the fake Steam API test library, Catch2.

The application target itself is installed by `src/app/CMakeLists.txt`
(`install(TARGETS pmm-app RUNTIME DESTINATION bin)`),
together with Qt's deployment script (next section).

## Qt deployment

`src/app` uses Qt's CMake deployment API:

```cmake
qt_generate_deploy_qml_app_script(TARGET pmm-app OUTPUT_SCRIPT pmm_deploy_script
  NO_UNSUPPORTED_PLATFORM_ERROR NO_COMPILER_RUNTIME NO_TRANSLATIONS
  EXCLUDE_PLUGIN_TYPES qmltooling generic
  DEPLOY_TOOL_OPTIONS --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw
  DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM)
install(SCRIPT "${pmm_deploy_script}")
install(SCRIPT packaging/windows/prune-qt-deployment.cmake)
```

The script runs during `cmake --install` and therefore inside `cpack`, after
the application has been installed into the staging directory. It copies the QML
modules the application imports (found by scanning the QML sources) to `qml/`, then
runs `windeployqt`, which adds the Qt DLLs to `bin/`, the plugins to `plugins/` and
a `qt.conf`. Because `Packaging.cmake` installs the MSVC runtime DLLs, the script
passes `NO_COMPILER_RUNTIME` to avoid also copying the redistributable installer.

Debug builds deploy debug Qt libraries; package only `release` builds.

Deployed by default but **not shipped**, because the application never loads
them (checked by running the package, see the evidence):

| Left out | Why |
| --- | --- |
| Qt translations (`translations/qt_*.qm`, 7 MB) | The application installs no translator; the UI is English only. `NO_TRANSLATIONS`. |
| `qmltooling` plugins (QML debugger, profiler) and `generic` plugins (TUIO touch) | Only used for QML debugging or embedded input. `EXCLUDE_PLUGIN_TYPES`. |
| `opengl32sw.dll` (Mesa software OpenGL, 20 MB) | Qt Quick renders with Direct3D 11. Without a usable GPU driver Qt falls back to WARP, the software rasterizer built into Windows (exercised with `QSG_RHI_PREFER_SOFTWARE_RENDERER=1`). |
| `d3dcompiler_47.dll`, `dxcompiler.dll`, `dxil.dll` | `D3DCompiler_47.dll` is part of Windows 10+ and was not loaded during the runs; DXC is only for Direct3D 12. Without these options windeployqt copies them from the Windows SDK when the Visual Studio environment is loaded, so the package depended on the build shell. |
| `icuuc.dll` | Qt's Windows builds use the ICU that is part of Windows 10 1703+; windeployqt copies `System32\icuuc.dll`, a Windows system file. Removed by [`prune-qt-deployment.cmake`](../packaging/windows/prune-qt-deployment.cmake). |
| Quick Controls styles other than Basic (Fusion, Imagine, Material, Universal, FluentWinUI3, Windows, `QtQuick/NativeStyle` and their `Qt6QuickControls2*.dll`) | `main()` selects the Basic style; the others are deployed only because Qt Quick Dialogs imports `QtQuick.Controls`, whose `qmldir` lists every style as an optional import. Removed by the same script, which fails the installation if the Basic style is missing. |

Together this took the ZIP from 47.8 MB (165 files) to 21.9 MB
(80 files) on 2026-10-29.

## Windows

- Unsigned executables trigger SmartScreen warnings. Sign `pmm-app.exe`,
  `pmm-steam-helper.exe` and the installer with `signtool` (Authenticode,
  timestamped) before release; signing is not automated yet (see
  [`releasing.md`](releasing.md#code-signing)).
- The ZIP is portable: extract it and run `bin\pmm-app.exe`. Keep the
  extraction folder's path short: without long-path support, Windows refused
  to load a QML plugin whose full path had 254 characters ("The filename or
  extension is too long"; the UI then does not load), while 247 worked. The
  deepest file is 72 characters below the package folder
  (`qml\QtQuick\Controls\Basic\impl\...plugin.dll`), so the path of the
  folder that contains `bin\` must stay below about 170 characters. Typical
  locations (`Downloads`, `Program Files`, `C:\Games`) are far below that.
- The application writes nothing into its own folder: its state lives in the
  per-user folders listed in [`app.md`](app.md) ("Files and folders"). After
  the verification runs the extracted folder still held exactly the 80
  packaged files.

### Windows installer

[`packaging/windows/installer.iss`](../packaging/windows/installer.iss), compiled
by Inno Setup 6 from the staged output of `cmake --install`, so it holds exactly
what the ZIP holds. It replaces the earlier per-machine NSIS configuration, so
there is one installer.

```sh
cmake --build <build dir> --target installer
```

builds what is installed, stages it to `<build dir>/installer-stage`, and writes
`<build dir>/dist/ParadoxModManager-<version>-windows-x86_64-setup.exe`
([`build-installer.cmake`](../packaging/windows/build-installer.cmake)). `ISCC.exe`
comes from the `ISCC` environment variable, `PATH` or Inno Setup's standard
folders. The compiler command it runs is

```text
ISCC /Q /DAppVersion=<x.y.z> /DStageDir=<stage> /DLicensePath=<license-page.rtf> /O<output> packaging\windows\installer.iss
```

- **Per-user, no administrator rights**: `PrivilegesRequired=lowest`, never a UAC
  prompt, default folder `%LOCALAPPDATA%\Programs\ParadoxModManager`, uninstall
  entry in `HKCU`. Start Menu shortcut, optional desktop shortcut (a task, off by
  default), [license page](#license-page), the usual "launch" checkbox on the last page.
- **Upgrades** install over the existing folder. `AppId` is fixed and must never
  change; it is what links a new version to the installed one. The folder page is
  skipped and earlier choices (folder, tasks) are kept. `bin\`, `plugins\` and
  `qml\` are replaced whole, so files a new Qt no longer ships do not stay behind:
  just before copying they are moved to `pmm-upgrade-backup\` in the install folder,
  which is deleted when the upgrade has finished. If Setup fails or is cancelled
  after that, its rollback runs and they are moved back, so the previous version
  keeps working. If they cannot be moved because something has a file open, a
  Retry/Cancel message says so and nothing has changed.
- **Only a real upgrade touches those folders**: the folder must have
  `unins000.exe`, and the `InstallLocation` of this `AppId`'s `HKCU` uninstall entry
  must be that folder. A first install into an existing folder, chosen in the wizard
  or with `/DIR=`, leaves its own `bin`, `plugins` and `qml` alone, and so does the
  uninstaller.
- **User data** lives outside the install folder (see [`app.md`](app.md), "Files and
  folders"). Neither an upgrade nor the uninstaller touches it.
- **A running application** is detected through the mutex
  `Local\ParadoxModManagerRunning` that `pmm-app` holds (`AppMutex`). Interactive
  runs and the uninstaller show Inno Setup's "close it, then OK" prompt.
  `CloseApplications` handles anything else that holds a file, such as the Steam
  helper: a prompt when interactive, closed automatically when silent.
  `RestartApplications=no`: the installer starts nothing it closed itself.
- **Update run**: the application starts the verified installer with exactly
  `/SILENT /SP- /NORESTART /PMMUPDATE=1` and quits. `InitializeSetup` then waits
  about 60 s for the mutex to go away; Inno Setup runs it before its own `AppMutex`
  check, so no prompt appears in the normal case. After the timeout a Retry/Cancel
  message says what happened and that the current version is unchanged. The
  installer shows only its progress window and starts `bin\pmm-app.exe` when it is
  done, which only an update run does. If the update fails after the program files
  were moved, a message says the update was not installed and the current version
  is still in place; the application is not started in that case. An installed copy
  is recognised by `..\unins000.exe` next to the folder of `pmm-app.exe`.
- Every run writes a log to `%TEMP%\Setup Log <date> #<n>.txt` (`SetupLogging`),
  the first place to look when an update did not arrive.

[`smoke-test-installer.ps1`](../packaging/windows/smoke-test-installer.ps1) is the
check the release workflow runs on a built setup, also for pull requests that touch
the packaging. It covers the first install, a first install into a folder with
another program's files, two failed upgrades (a file locked outside and inside the
replaced folders), a working upgrade and the uninstall. It never starts the
application, so the update run itself is not part of it. It refuses to run if the
application is installed, because the test would take over the installation's
uninstall registration.

### License page

[`license-page.rtf`](../packaging/windows/license-page.rtf) is written by hand, in
ASCII, with one paragraph per line so that the wizard reflows it to the box: the MIT
License, the third-party software the installer ships, and the non-affiliation
statement. Change the third-party part together with
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

`build-installer.cmake` fails the build when the text between the page's
`{\*\pmmmitlicensebegin}` and `{\*\pmmmitlicenseend}` markers (which an RTF reader
skips) does not match `LICENSE` word for word, ignoring RTF markup and how the
lines are wrapped.

## Qt LGPLv3 compliance in packages

Checklist for every release package that contains Qt:

- [ ] Qt is linked dynamically and shipped as separate, unmodified shared
      libraries (no static Qt in release builds).
- [ ] `THIRD_PARTY_NOTICES.md`, `licenses/LGPL-3.0.txt` and
      `licenses/GPL-3.0.txt` are in the package.
- [ ] The notices name the Qt version and where its source is available
      (download.qt.io / code.qt.io); if Qt was modified, the modified source is
      published with the release.
- [ ] Users can replace the Qt libraries by replacing the files.
- [ ] No GPL-only Qt modules or tools are linked into the application.

The ZIP of 2026-10-29 meets every item; the check is recorded in the
[evidence](evidence/packaging-2026-10-29.md#qt-lgplv3-checklist).

## Local verification

**Windows, 2026-10-29** (Windows 11 23H2, MSVC 19.51, CMake 4.4, Qt 6.10.3):
the `release` preset with `-DPMM_BUILD_TESTS=OFF` was configured, built from
scratch without warnings and packaged with `cpack`:
`ParadoxModManager-0.1.0-windows-x86_64.zip`, 21.9 MB, 80 files (54 MB
unpacked). An extracted copy ran with only the Windows folders on `PATH`
(no Qt, no Visual Studio): `--screenshot` and `--self-test "config,dump:..."`
for Stellaris 4.5.2 and EU4 1.37.5, read-only, all exit code 0; the dumps were
identical to the build-tree application's. Qt and the C++ runtime were loaded
from the package, Direct3D 11 rendered on the GPU and with WARP, and the
packaged `pmm-steam-helper.exe` started next to `pmm-app.exe` (without
arguments, so it never loaded a Steam library). Commands, file list, sizes and
results: [evidence/packaging-2026-10-29.md](evidence/packaging-2026-10-29.md).
Not covered: a machine without the VC++ Redistributable installed system-wide,
code signing.

**Windows installer, 2026-10-30** (Inno Setup 6.7.3): built with the `installer`
target from the same tree as the ZIP (80 files, 16.6 MB against the ZIP's 22.1 MB)
and exercised with real installs in a scratch folder, including the update run,
the wait for a held mutex (also the real `pmm-app`'s), the timeout, failed upgrades
and uninstall, with a stand-in program for every relaunch:
[evidence/installer-2026-10-30.md](evidence/installer-2026-10-30.md). The `Release`
workflow's dry run has passed on GitHub, including the smoke test on a runner; its
publish step has not run, and no update between two published releases has happened.
