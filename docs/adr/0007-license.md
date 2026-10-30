# ADR 0007: MIT license; Qt under LGPLv3 with dynamic linking

- Status: Accepted; the Linux and macOS deployment superseded by [ADR 0009](0009-windows-only.md)
- Date: 2026-10-29

## Context

The spec asks for the project's own license to be recorded explicitly and for
dependency licenses and Qt distribution obligations to be reviewed before the
release packaging approach is chosen (section 8). Dependencies: nlohmann/json
(MIT), zlib (zlib License), SQLite (public domain), Catch2 (BSL-1.0, tests
only) and Qt 6, which is available under commercial terms or under the LGPLv3
(the modules used here: Core, Gui, Qml, Quick, Quick Controls 2, Network,
Concurrent). The Steam integration must not redistribute Valve code.

## Decision

- The project is licensed under the **MIT License** ([`LICENSE`](../../LICENSE)).
- Qt is used under the **LGPLv3** (open-source option). Release builds link
  the unmodified Qt shared libraries dynamically; **Qt is never linked
  statically into release builds**, and no GPL-only Qt modules or tools are
  linked into the application.
- Packages that contain Qt include `THIRD_PARTY_NOTICES.md`, the LGPLv3 and
  GPLv3 texts (`packaging/licenses/`, installed by `cmake/Packaging.cmake`),
  and state where the corresponding Qt source is available.
- Users must be able to replace the Qt libraries: they ship as separate files
  (DLLs, `.so` files, frameworks) deployed by `windeployqt`, `macdeployqt` or
  Qt's Linux deployment script, and the application source is public under MIT.
- No Steamworks SDK files or Steam binaries are distributed; the helper loads
  the game's own library at run time (ADR 0003).
- Dependency additions require a license review recorded in
  `THIRD_PARTY_NOTICES.md` in the same change.

## Consequences

- MIT is compatible with all dependencies; LGPLv3 obligations apply only to
  the Qt libraries, which stay separate and replaceable.
- Static Qt builds (for single-file distribution or smaller packages) are ruled
  out unless the project obtains a commercial Qt license or provides relinkable
  object files; this decision would need a new ADR.
- On macOS, replacing a Qt framework invalidates the bundle's code signature;
  users re-sign locally (an ad hoc signature suffices). This does not restrict
  replacement.
- Not legal advice. Before the first public release, review the notices in a
  built package against this record.

## Alternatives considered

- **GPLv3 for the project**: would also satisfy Qt's terms, but restricts reuse
  of the protocol and core libraries more than necessary.
- **Apache-2.0**: equally suitable; MIT was chosen for brevity and because the
  repository already used it.
- **Commercial Qt license**: would allow static linking but adds cost and
  per-developer licensing for an open-source tool.
