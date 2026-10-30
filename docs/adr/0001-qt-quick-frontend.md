# ADR 0001: Qt 6 Quick/QML frontend over Qt-free C++ core layers

- Status: Accepted; the Linux and macOS scope superseded by [ADR 0009](0009-windows-only.md)
- Date: 2026-10-29

## Context

The product is a compact, dense, three-column desktop application for Windows,
Linux and macOS (spec sections 1, 3 and 8). It needs virtualized lists for at
least 2,000 rows, drag-and-drop reordering, keyboard navigation, accessible
labels, high-DPI rendering and light/dark themes. The domain logic (playsets,
share codes, persistence, game adapters, Steam acquisition) must be testable
without starting a GUI, and must run on worker threads without blocking the UI.

## Decision

- The UI is Qt 6 Quick/QML with Qt Quick Controls (Basic style, customised
  through a `Theme` singleton). QML only presents and forwards user actions; it
  contains no domain logic. The C++/QML boundary is specified in
  [`docs/ui-contract.md`](../ui-contract.md).
- Everything below the application layer is plain C++20 with **no Qt
  dependency**: `src/core`, `src/platform`, `src/sharing`, `src/persistence`,
  `src/providers`, `src/games`, the Qt-free `src/services` layer and `tools/`.
  These libraries use the standard library, nlohmann/json, zlib and SQLite, and
  report failures through `pmm::Result`.
- `src/app` (Qt) composes the layers, adapts them to `QAbstractListModel` /
  `QObject` types for QML, moves long work to worker threads and delivers
  results on the GUI thread.
- Qt 6.5 is the minimum (`qt_standard_project_setup(REQUIRES 6.5)`, QML module
  APIs); 6.10.3 is the tested and CI-pinned version.

## Consequences

- Core logic is unit tested with Catch2 on every OS without a display server;
  the `core-debug` and `core-sanitize` presets build without Qt at all, which
  also keeps sanitizer runs free of Qt noise.
- Another front end could reuse the same services without Qt (none is
  planned, see the amendment).
- Some functionality exists twice in spirit (native OS paths, process start,
  file I/O instead of `QStandardPaths`, `QProcess`, `QSaveFile`), because the
  lower layers must not link Qt. Those primitives are implemented once in
  `src/platform` and tested there.
- Release packages carry the Qt runtime (LGPLv3, see ADR 0007).
- QML is not compiled into the core tests; UI behaviour needs separate Qt Quick
  tests in the application layer.

## Alternatives considered

- **Qt Widgets**: mature and dense, but custom-styled list rows with
  drag-and-drop, theming and high-DPI behaviour need more custom painting;
  the spec asks for Qt Quick unless investigation shows a compelling reason
  otherwise, and none was found.
- **Electron or a web view**: a browser runtime per installation, higher memory
  use, and a JavaScript/C++ bridge for all domain logic; "browser-style
  navigation" is explicitly unwanted.
- **Dear ImGui, wxWidgets, GTK**: weaker accessibility, theming or macOS
  integration, and none is a better fit for virtualized, accessible lists.
- **Qt throughout (QString, QSqlDatabase, QJsonDocument in the core)**: faster
  to start, but it couples every test to Qt, complicates sanitizer builds and
  ties the protocol implementation to Qt's JSON parser, whose duplicate-key and
  number handling would then define the share-code format.

## Amendment (2026-10-30): no command-line tool

The desktop application `pmm-app` is the only front end. The specification
does not require a command-line tool, and none will be built. Headless and
scripted checks use `pmm-app`'s hidden verification switches (`--profile`,
`--read-only`, `--self-test`, `--screenshot`; see [`app.md`](../app.md)),
which drive the real Backend without user input. The services stay Qt-free
for the reasons above.
