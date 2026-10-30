# ADR 0009: Windows only

- Status: Accepted
- Date: 2026-10-30

## Context

The specification asked for an application that runs on Windows, Linux and macOS,
and the code base carried all three: per-OS paths, process start and Steam API
loading, per-OS game traits, CI jobs and packages.

Only Windows was ever exercised for real. Every verified result in the
[support matrix](../support-matrix.md) comes from one Windows 11 machine: real game
scans, live launches, Workshop downloads, the ZIP and the installer. On Linux the
core libraries were built and tested in WSL, without Qt and without a Steam client,
and CI built the application. macOS was built by CI only. Neither system ran the
application against a real game or Steam client. The installer and the update button
exist only for Windows. No version has been published, so no installed copy depends
on the other systems.

The repository owner decided on 2026-10-30 to make the product Windows only.

## Decision

- The application is built, tested, packaged and supported on Windows only.
  Support for Linux and macOS is removed from the code base, not merely left
  unadvertised.
- Configuring on another host stops at once: `if(NOT WIN32) message(FATAL_ERROR ...)`
  at the top of the root `CMakeLists.txt`.
- [`spec.md`](../../spec.md) is amended to say so.

### What was removed

- Code: `OsPlatform`, `currentOsPlatform()`, `GameInstallation::platform`, the
  `DiscoveryEnvironment` fields used only off Windows, the per-OS `GameTraits` lists,
  the per-OS executable and Steam API library names, and the POSIX branches.
- Database: `game_installations.platform`, dropped by a forward migration that goes
  through the usual upgrade backup ([ADR 0008](0008-sqlite-migrations.md)). The old
  migration is unchanged.
- CI: the Linux GCC and macOS AppleClang build jobs and the Linux GCC ASan + UBSan job.
- Packaging: the `.dmg` and `.tar.gz` packages, `packaging/linux`, `packaging/macos`
  and the macOS signing options.

### What was kept

- Qt, the QML user interface, the services and the layering of
  [ADR 0001](0001-qt-quick-frontend.md). The lower layers stay Qt-free for the
  reasons given there, which never depended on portability alone.
- The share-code format: `tests/fixtures/sharing/v1` is frozen and the output for a
  given playset is unchanged ([ADR 0004](0004-share-code-format.md)). Codes carry no
  operating system and no paths.
- The Windows ZIP and installer, their asset names and the update path.
- clang-cl. `clangd` and `clang-tidy` parse this code as Clang in MSVC mode, so what
  `__clang__` needs stays, and a CI job builds the Qt-free libraries with clang-cl
  and warnings as errors and runs their tests under UBSan. It replaces the second and third compiler as the check on
  non-standard code.
- ASan, now with MSVC, as the replacement for the Linux sanitizer job.

## Consequences

- One platform to build, test and release, and every supported system is also the
  one the evidence comes from.
- **Linux players** with the native builds of the games have no version of this
  application. Neither have **Steam Deck** users, whose system is Linux.
  Running the Windows build through Proton or Wine has not been tried and is not
  supported.
- **Mac players** have no version of this application.
- A share code stays readable on any system with `scripts/share-code.py` and the
  [protocol](../share-code-protocol.md), but importing or playing one needs Windows.
- Lost coverage: ASan on Windows has no LeakSanitizer, so CI no longer checks for
  leaks, and GCC's warnings are gone; clang-cl and `/W4` remain. MSVC has no UBSan, so
  undefined behaviour is now checked on the Qt-free libraries only, through clang-cl.
- Some earlier records still describe three systems. They stay as history: the
  evidence files, [`feasibility.md`](../feasibility.md) and ADRs 0001, 0002, 0003 and
  0007, which are marked as superseded on this point.

## How to bring it back

The last cross-platform commit is `0ab0089`. It has the removed code, the CI jobs,
the packaging and the documentation of what had and had not been verified. Reverting
the removal commits, or reapplying from that commit, is the starting point. The
database needs its own step: migrations only go forward, so `platform` would return
as a new migration, not by undoing this one. Then treat the other systems as
unverified until they have evidence of their own: an application build, the
application tests, the read-only helper probe and the live steps in
[`validation.md`](../validation.md), and a signing and notarisation pass on macOS.

## Alternatives considered

- **Keep the code and stop advertising it**: the owner rejected this. Code that no
  one runs on a real machine keeps costing review and CI time, and reads as support
  that does not exist.
- **Keep CI on the other systems without packages**: green builds would say nothing
  about whether the application works there, which is what the support matrix
  already warned about.
