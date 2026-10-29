# ADR 0002: Dependencies pinned with FetchContent; Qt as a toolchain prerequisite

- Status: Accepted
- Date: 2026-10-29

## Context

The spec requires reproducible dependency versions through one documented
package-management approach (section 8), on Windows (MSVC), Linux (GCC) and
macOS (AppleClang). The non-Qt dependencies are small: a JSON library, a
zlib-compatible compressor, SQLite and a test framework. Qt is large, ships
prebuilt per compiler, and is normally installed once per machine.

## Decision

- Non-Qt dependencies are declared in
  [`cmake/Dependencies.cmake`](../../cmake/Dependencies.cmake) and fetched with
  CMake's `FetchContent` from their upstream release archives, each pinned by
  **exact URL and SHA-256**:
  nlohmann/json 3.12.0, zlib 1.3.1, SQLite 3.50.4 (amalgamation), and
  Catch2 3.8.1 (only when tests are built).
- zlib and SQLite are compiled as private static libraries with project-chosen
  options. zlib uses `Z_PREFIX` so its symbols never clash with a system libz
  that Qt may load. SQLite is built with `SQLITE_DQS=0`,
  `SQLITE_DEFAULT_FOREIGN_KEYS=1`, `SQLITE_OMIT_LOAD_EXTENSION`, and other
  hardening options.
- Qt 6 is a toolchain prerequisite found through `CMAKE_PREFIX_PATH`
  (`QT_ROOT_DIR` in the presets). The minimum is 6.5; the tested version
  (6.10.3) is pinned in CI (`jurplel/install-qt-action`) and in the README.
- Updating a dependency is a reviewed change to `Dependencies.cmake` (new URL
  and hash), a changelog entry, and a full CI run. A zlib update must keep the
  share-code golden tests green or be called out (see ADR 0004).

## Consequences

- Every platform builds the same dependency versions from source; the hash
  check rejects a changed or tampered archive.
- The first configure needs network access (or a pre-populated
  `FETCHCONTENT_BASE_DIR` / `FETCHCONTENT_SOURCE_DIR_<NAME>` for offline
  builds). Downloads are a few megabytes.
- Builds do not use system copies of these libraries, which also means
  distribution packagers who must unbundle them need to patch
  `Dependencies.cmake`.
- Qt is not reproducible by the build itself; CI pins it and the README
  documents the installation (online installer or aqtinstall).

## Alternatives considered

- **vcpkg (manifest mode)**: good reproducibility via a baseline, but it adds a
  second tool and bootstrap step on every machine, builds Qt from source unless
  a binary cache is set up (hours in CI), and the four small dependencies do
  not need its features. Revisit if the dependency set grows (for example
  libraries with complex transitive dependencies).
- **Conan 2**: similar trade-off, plus a Python toolchain and profile
  management per compiler.
- **System packages / `find_package` only**: versions differ per OS and
  distribution, which would make the share-code encoder output and SQLite
  behaviour platform-dependent.
- **Vendoring sources into the repository**: reproducible, but bloats history
  and makes license and update review harder than a pinned URL + hash.
