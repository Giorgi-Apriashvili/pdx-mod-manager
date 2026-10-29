# Third-party notices

Paradox Mod Manager is licensed under the MIT License (see [`LICENSE`](LICENSE)).
It uses the third-party components listed below. Versions are pinned in
[`cmake/Dependencies.cmake`](cmake/Dependencies.cmake) (URL + SHA-256); Qt is a
toolchain prerequisite whose tested version is pinned in CI.

| Component | Version | License | Use | Shipped in packages |
| --- | --- | --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | JSON parsing and writing (share codes, helper protocol, journals, persistence) | Compiled into the binaries (header-only) |
| [zlib](https://zlib.net/) | 1.3.1 | zlib License | Share-code compression | Statically linked, symbols prefixed (`Z_PREFIX`) |
| [SQLite](https://sqlite.org/) | 3.50.4 | Public domain | Application database | Statically linked (amalgamation) |
| [Catch2](https://github.com/catchorg/Catch2) | 3.8.1 | BSL-1.0 | Unit tests | No (test-only) |
| [Qt](https://www.qt.io/) | 6.10.3 tested, 6.5 minimum | LGPL-3.0-only (open-source option) | Desktop application (`pmm-app`) | Yes, as shared libraries next to the application |
| Microsoft Visual C++ runtime | matches the compiler | Visual Studio redistribution terms | C++ runtime of the Windows binaries | Windows packages only, as the redistributable DLLs |

No other code is vendored. `tools/probe-mods/probe_mods.py` and the scripts in
`scripts/` use only the Python standard library. The GitHub Actions used in CI
are not distributed.

## Steam and game files

**No Steamworks SDK files and no Steam or game binaries are distributed with
this project, in source or in packages.** The Steam Workshop helper
(`pmm-steam-helper`) contains its own minimal declarations, written from
Valve's public documentation, and loads the Steam API library that ships with
the user's own game installation (for example `steam_api64.dll` in the
Stellaris folder) at run time. Nothing from a game installation is copied,
modified or redistributed. `.gitignore` blocks `steam_api*.dll` and
`libsteam_api.*`, and the test suite uses a fake library built from this
repository's own code (`tests/unit/providers/fake_steam_api.cpp`).

Steam and Steamworks are trademarks of Valve Corporation. Paradox Interactive,
Stellaris, Europa Universalis, Hearts of Iron, Crusader Kings and Victoria are
trademarks of Paradox Interactive AB. This project is not affiliated with or
endorsed by Valve or Paradox Interactive.

## Qt (LGPLv3)

The desktop application uses these Qt 6 modules: Qt Core, Qt Gui, Qt Network,
Qt Qml, Qt Quick, Qt Quick Controls 2 (Basic style), Qt Quick Layouts,
Qt Quick Shapes and Qt Quick Dialogs. Packages also contain the Qt libraries
and plugins these modules load: Qt OpenGL, Qt QML Models, Qt QML WorkerScript,
Qt QML Meta, Qt Quick Templates 2, Qt Quick Effects, Qt Svg, Qt Labs
FolderListModel, and the platform, image format, icon engine, TLS and network
information plugins. All of them are used
under the GNU Lesser General Public License version 3
([`packaging/licenses/LGPL-3.0.txt`](packaging/licenses/LGPL-3.0.txt)), which
incorporates the GNU General Public License version 3
([`packaging/licenses/GPL-3.0.txt`](packaging/licenses/GPL-3.0.txt)). Qt is
Copyright (C) The Qt Company Ltd. and other contributors. Qt itself contains
third-party components under their own licenses (for example FreeType,
HarfBuzz, libpng, PCRE2 and zlib); see
[Licenses Used in Qt](https://doc.qt.io/qt-6/licenses-used-in-qt.html).

How this project meets the LGPLv3 obligations (details in
[`docs/adr/0007-license.md`](docs/adr/0007-license.md) and
[`docs/packaging.md`](docs/packaging.md)):

- **Dynamic linking only.** Release packages link the unmodified Qt shared
  libraries (DLLs, `.so` files, frameworks) deployed by `windeployqt`,
  `macdeployqt` or Qt's Linux deployment script. Qt is never linked statically
  into release builds.
- **Replaceability (relinking).** Users can replace the Qt libraries in a
  package with their own compatible build. The application's source code and
  build files are available under the MIT License, so it can also be rebuilt
  against a modified Qt. On macOS, replacing a framework invalidates the code
  signature; re-sign the bundle (an ad hoc signature is sufficient locally).
- **Notices.** Packages that contain Qt include this file and the LGPLv3 and
  GPLv3 texts (`licenses/`), installed by `cmake/Packaging.cmake`.
- **Source availability.** The Qt source code for the version in a package is
  available from <https://download.qt.io/official_releases/qt/> (for 6.10.3:
  `qt/6.10/6.10.3/single/`) and <https://code.qt.io/>. Qt is used unmodified;
  if a release ever ships a modified Qt, its changes will be published with
  that release.
- No terms of this project restrict reverse engineering for debugging
  modifications of the Qt libraries.

## License texts

### nlohmann/json 3.12.0

nlohmann/json embeds code by Florian Loitsch (Grisu2, 2009), Björn Hoehrmann
(UTF-8 decoder, 2008-2009), the Abseil Authors (2018) and Evan Nemerson
(Hedley, 2016-2021), all under the MIT License.

```text
MIT License

Copyright (c) 2013-2025 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### zlib 1.3.1

```text
Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

Jean-loup Gailly        Mark Adler
jloup@gzip.org          madler@alumni.caltech.edu
```

### SQLite 3.50.4

SQLite is in the public domain: "The author disclaims copyright to this source
code." See <https://sqlite.org/copyright.html>.

### Catch2 3.8.1 (tests only, not distributed)

Boost Software License 1.0, <https://www.boost.org/LICENSE_1_0.txt>.

### Microsoft Visual C++ runtime (Windows packages)

The runtime DLLs (`concrt140.dll`, `msvcp140*.dll`, `vcruntime140*.dll`) are
redistributed app-locally under the Microsoft Visual Studio license terms for
distributable code (`redist.txt`). No Windows system files are redistributed:
the Universal C runtime, ICU (`icu.dll`, `icuuc.dll`) and the Direct3D shader
compiler (`D3DCompiler_47.dll`) are used from Windows itself.

### Qt

GNU LGPL version 3: [`packaging/licenses/LGPL-3.0.txt`](packaging/licenses/LGPL-3.0.txt).
GNU GPL version 3: [`packaging/licenses/GPL-3.0.txt`](packaging/licenses/GPL-3.0.txt).
