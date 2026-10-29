# Packaging

How release packages are built for Windows, macOS and Linux, what they
contain, how Qt is deployed, and how packages meet the Qt LGPLv3 obligations
([ADR 0007](adr/0007-license.md)). The configuration is
[`cmake/Packaging.cmake`](../cmake/Packaging.cmake) (CPack), included at the end
of the top-level `CMakeLists.txt`.

Status: the CPack configuration is validated locally on Windows with the
`core-debug` preset, which packages the Steam helper and the documents
(2026-10-29, see [below](#local-verification)). Packages containing the
desktop application need the `pmm-app` target, which is in progress; macOS and
Linux packages are unverified until the CI `package` job has run.

## Building packages

```sh
cmake --preset release -DPMM_BUILD_TESTS=OFF     # needs QT_ROOT_DIR, see README
cmake --build --preset release
cpack --config build/release/CPackConfig.cmake -B dist
```

CI does the same on each OS for tags `v*` (job `package` in
[`.github/workflows/ci.yml`](../.github/workflows/ci.yml)) and uploads the
packages as workflow artifacts. Nothing is published automatically.

| OS | Generators | File name |
| --- | --- | --- |
| Windows | `ZIP`; `NSIS` in addition when `makensis` is found at configure time | `ParadoxModManager-<version>-windows-x86_64.zip` / `.exe` |
| macOS | `DragNDrop` | `ParadoxModManager-<version>-macos-<arm64\|x86_64\|universal>.dmg` |
| Linux | `TGZ` | `ParadoxModManager-<version>-linux-x86_64.tar.gz` |

Each package gets a `.sha256` file next to it (`CPACK_PACKAGE_CHECKSUM`). The
architecture comes from the MSVC target architecture, `CMAKE_OSX_ARCHITECTURES`
(several architectures give `universal`) or `CMAKE_SYSTEM_PROCESSOR`. Source
archives are made with `git archive`, not CPack.

## Layout

Windows (ZIP root or NSIS installation folder):

```text
bin/pmm-app.exe
bin/pmm-steam-helper.exe
bin/Qt6*.dll, bin/qt.conf                    deployed by windeployqt
plugins/, qml/                               Qt plugins and QML modules (windeployqt)
bin/msvcp140*.dll, bin/vcruntime140*.dll     Microsoft C++ runtime
LICENSE  README.md  CHANGELOG.md  THIRD_PARTY_NOTICES.md  licenses/
```

macOS (`.dmg`):

```text
pmm-app.app/Contents/MacOS/pmm-app
pmm-app.app/Contents/MacOS/pmm-steam-helper
pmm-app.app/Contents/Frameworks/Qt*.framework   deployed by macdeployqt
pmm-app.app/Contents/PlugIns/, Contents/Resources/qml/
pmm-app.app/Contents/Resources/doc/             notices and licenses/
LICENSE  README.md  CHANGELOG.md  THIRD_PARTY_NOTICES.md  licenses/
```

Linux (`.tar.gz`):

```text
bin/pmm-app
bin/pmm-steam-helper
lib/libQt6*.so*, plugins/, qml/                  deployed by Qt's deployment script
share/applications/paradox-mod-manager.desktop
share/doc/ParadoxModManager/                     LICENSE, README.md, CHANGELOG.md,
                                                 THIRD_PARTY_NOTICES.md, licenses/
```

The Qt parts appear once `pmm-app` exists; their exact folders are chosen by
Qt's deployment tools.

Rules implemented by `cmake/Packaging.cmake`:

- **The Steam helper sits next to the application executable**: `bin/` on
  Windows and Linux, inside the bundle at `<bundle>.app/Contents/MacOS/` on
  macOS (the bundle name follows the `pmm-app` target's `OUTPUT_NAME`). The
  application looks for it there. Without a `pmm-app` target (for example the
  `core-debug` preset), the helper goes to `bin/` on every OS.
- `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README.md` and `CHANGELOG.md` go to
  the package root on Windows and macOS and to `share/doc/ParadoxModManager/`
  on Linux. When the application is packaged, `licenses/LGPL-3.0.txt` and
  `licenses/GPL-3.0.txt` are added, and on macOS the notices are also copied
  into `Contents/Resources/doc/` because users usually copy only the bundle
  out of the disk image.
- On Windows the Microsoft C++ runtime DLLs are installed app-locally into
  `bin/` (`InstallRequiredSystemLibraries`), so the ZIP works without a
  separate VC++ Redistributable. The Universal CRT is part of Windows 10+.
- Install rules for targets are guarded with `if(TARGET ...)`, so the
  configuration works whether or not the application target exists.
- Never packaged: Steam binaries or Steamworks SDK files, game files, test
  executables, the fake Steam API test library, Catch2.

The application target itself is installed by `src/app/CMakeLists.txt`
(`install(TARGETS pmm-app BUNDLE DESTINATION . RUNTIME DESTINATION bin)`),
together with Qt's deployment script (next section).

## Qt deployment

`src/app` uses Qt's CMake deployment API:

```cmake
qt_generate_deploy_qml_app_script(TARGET pmm-app OUTPUT_SCRIPT deploy_script
                                  NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT "${deploy_script}")
```

The script runs during `cmake --install` and therefore inside `cpack`, after
the application has been installed into the staging directory:

- **Windows**: runs `windeployqt` on `bin/pmm-app.exe`. It copies the Qt DLLs,
  the platform and style plugins, and the QML modules the application imports
  (found by scanning the QML sources) next to the executable. Because
  `Packaging.cmake` installs the MSVC runtime DLLs, the script can pass
  `NO_COMPILER_RUNTIME` to avoid also copying the redistributable installer.
- **macOS**: runs `macdeployqt` on the bundle: Qt frameworks into
  `Contents/Frameworks`, plugins into `Contents/PlugIns`, QML modules into
  `Contents/Resources/qml`, and a `qt.conf`. The helper does not link Qt and
  needs no deployment.
- **Linux**: Qt 6.5+ deploys natively (no external tool): it copies the Qt
  libraries it resolves into `lib/`, plugins and QML modules, and writes a
  `qt.conf`; installed binaries find them through the `$ORIGIN`-relative
  install RPATH that `qt_standard_project_setup()` sets up. System
  libraries (glibc, libstdc++, X11/Wayland, OpenGL, fontconfig) are expected on
  the target system; the package is built on Ubuntu 24.04, so it needs a
  distribution with a glibc at least as new.

Debug builds deploy debug Qt libraries; package only `release` builds.

## Windows

- Unsigned executables trigger SmartScreen warnings. Sign `pmm-app.exe`,
  `pmm-steam-helper.exe` and the installer with `signtool` (Authenticode,
  timestamped) before release; signing is not automated yet.
- NSIS installer: per-machine installation into
  `Program Files\ParadoxModManager`, Start-menu shortcut to `pmm-app`,
  uninstall entry, and the license page shows `LICENSE`. The application keeps
  its data in `%APPDATA%` / `%LOCALAPPDATA%`, which the uninstaller does not
  remove.
- The ZIP is portable: extract anywhere and run `bin\pmm-app.exe`.

## macOS

The `.dmg` contains the bundle and the notices. Gatekeeper blocks unsigned,
un-notarised applications downloaded from the internet, so releases must be
signed and notarised:

1. **Sign** while packaging: configure with
   `-DPMM_MACOS_CODESIGN_IDENTITY="Developer ID Application: <Name> (<TEAMID>)"`
   (or `-` for an ad hoc signature for local testing). `cpack` then runs
   [`packaging/macos/codesign.cmake`](../packaging/macos/codesign.cmake) on the
   staged bundle before building the disk image (`CPACK_PRE_BUILD_SCRIPTS`):
   - all nested code with the hardened runtime (`--options runtime`,
     `--timestamp`);
   - `pmm-steam-helper` again with
     [`helper.entitlements`](../packaging/macos/helper.entitlements):
     `com.apple.security.cs.disable-library-validation`. **Required**: the
     helper loads the game's `libsteam_api.dylib` (and Steam's own client
     library), which are signed by other teams; with the hardened runtime and
     library validation on, `dlopen` would refuse them;
   - the bundle with [`app.entitlements`](../packaging/macos/app.entitlements):
     `com.apple.security.cs.allow-jit` for the QML JavaScript JIT;
   - `codesign --verify --deep --strict`.
2. **Notarise** the `.dmg` and staple the ticket:

   ```sh
   xcrun notarytool submit dist/ParadoxModManager-<version>-macos-<arch>.dmg \
       --keychain-profile <profile> --wait
   xcrun stapler staple dist/ParadoxModManager-<version>-macos-<arch>.dmg
   ```

The application is not sandboxed (no App Store distribution): it must read
Steam and game folders and start the game. On first access to
`~/Documents/Paradox Interactive`, macOS asks the user for permission.

The signing script and entitlements are written from Apple's and Qt's
documentation and are **unverified** until run on a Mac.

## Linux

- **TGZ**: extract and run `bin/pmm-app`. The desktop entry
  `share/applications/paradox-mod-manager.desktop` can be copied to
  `~/.local/share/applications/`; its `Exec=pmm-app` expects the binary on
  `PATH`, so either link `bin/pmm-app` into `~/.local/bin` or edit `Exec` to
  the absolute path. An application icon is still to be added with the UI
  assets.
- **AppImage (optional)**: build an AppImage from the installed tree with
  [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) and its
  [Qt plugin](https://github.com/linuxdeploy/linuxdeploy-plugin-qt):

  ```sh
  cmake --install build/release --prefix AppDir/usr
  export QMAKE="$QT_ROOT_DIR/bin/qmake" QML_SOURCES_PATHS="$PWD/ui"
  linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt --output appimage \
      --desktop-file AppDir/usr/share/applications/paradox-mod-manager.desktop \
      --icon-file <256x256 icon>.png
  ```

  Keep `pmm-steam-helper` in `AppDir/usr/bin` next to `pmm-app`. AppImage
  packaging is not automated.
- Distribution packages (deb, rpm) and Flatpak are not provided. A Flatpak
  sandbox would need explicit access to the Steam and game folders and to the
  Steam client's IPC, which is untested.

## Qt LGPLv3 compliance in packages

Checklist for every release package that contains Qt:

- [ ] Qt is linked dynamically and shipped as separate, unmodified shared
      libraries (no static Qt in release builds).
- [ ] `THIRD_PARTY_NOTICES.md`, `licenses/LGPL-3.0.txt` and
      `licenses/GPL-3.0.txt` are in the package (and inside the macOS bundle).
- [ ] The notices name the Qt version and where its source is available
      (download.qt.io / code.qt.io); if Qt was modified, the modified source is
      published with the release.
- [ ] Users can replace the Qt libraries: on Windows and Linux by replacing
      the files; on macOS by replacing frameworks and re-signing the bundle
      (ad hoc signing is enough locally).
- [ ] No GPL-only Qt modules or tools are linked into the application.

## Local verification

2026-10-29, Windows 11, MSVC 19.51, CMake 4.4, preset `core-debug` (no Qt,
so no application target):

```text
cpack --config build/core-debug/CPackConfig.cmake -G ZIP
ParadoxModManager-0.1.0-windows-x86_64.zip (+ .sha256)
  CHANGELOG.md, LICENSE, README.md, THIRD_PARTY_NOTICES.md
  bin/pmm-steam-helper.exe
  bin/concrt140.dll, msvcp140*.dll, vcruntime140*.dll
```

The same day the `ci` preset (Qt 6.10.3 found, RelWithDebInfo, warnings as
errors) was configured, built from scratch, tested (256 tests passed) and
packaged with `-G ZIP`, with the same contents because `pmm-app` does not exist
yet. `makensis` was not installed, so no NSIS installer was produced. The
`codesign.cmake` control flow was exercised on Windows with a stand-in for
`codesign` (argument order, bundle discovery, missing-helper warning), not with
Apple's tool.
