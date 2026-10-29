# Development status (paused 2026-10-29)

## Done and merged on `main`

| Area | Location | State |
|---|---|---|
| Build scaffolding, presets, pinned deps | `CMakeLists.txt`, `cmake/`, `CMakePresets.json` | done |
| Core model, catalog, logging/redaction, SHA-256 | `src/core` | done, tested |
| Playset ops, readiness, dependencies, import plan | `src/core` | done, tested |
| Share-code protocol v1 + regression fixtures | `src/sharing`, `docs/share-code-protocol.md` | done, tested |
| SQLite persistence + migrations | `src/persistence`, `docs/persistence.md` | done, tested |
| Platform: fs, paths, process, Steam discovery, file transactions | `src/platform` | done, tested (Windows; Linux build checked in WSL) |
| Workshop provider + `pmm-steam-helper` | `src/providers`, `tools/steam-helper`, `docs/steam-workshop.md` | done, tested; read-only live probe verified |
| EU4 + Stellaris adapters | `src/games`, `docs/games/`, `docs/load-order.md` | done, fixture-tested |
| Qt-free services (import, launch, acquisition, playsets) | `src/services`, `docs/architecture-services.md` | done, tested |
| QML UI (against `docs/ui-contract.md`, fake backend preview) | `ui/`, `docs/screenshots/` | done, not yet wired to C++ |
| README, ADRs, CI, packaging, support matrix, validation procedure | repo root, `docs/`, `.github/`, `packaging/` | done |
| In-game probe mods | `tools/probe-mods/probe_mods.py` | done |

Test suite: 341/341 passing (`ctest --preset core-debug`), MSVC /W4 clean.

## Next steps (in order)

1. **Qt app layer** (`src/app`): implement `docs/ui-contract.md` on top of
   `src/services` (one worker QThread; QProcess `WorkshopSessionHost`;
   QNetworkAccessManager `WorkshopMetadataHost`; `main.cpp` registering
   `Backend`, `Q_IMPORT_QML_PLUGIN(Pmm_UiPlugin)`, process-wide
   `SetErrorMode`), plus `pmm-cli` with `--profile <dir>`. Integration notes:
   follow-ups from the services, ui and docs agents (install rules,
   `qt_generate_deploy_qml_app_script`, helper path lookup, contract
   additions such as `ModListModel.moveRows`).
2. **EU4 real-data fixes** (EU4 was installed after the adapter was written):
   honour `userdir.txt`; drop `-enabletelemetry` from launch arguments by
   default; run the hidden `pmm_games_tests "[.real]" -s` against both games.
3. Run `scripts/format.sh` once (46 files would change) so the CI
   format-check passes; consider disabling AUTOMOC for Qt-free targets.
4. Review pass (adversarial, by dimension) and fixes.
5. **Live validation, needs the user's explicit approval each time:**
   - in-game load order/launcher bypass with `tools/probe-mods` (backs up
     and restores `dlc_load.json`),
   - Workshop download: preferably `DownloadItem` for EU4 items the user is
     already subscribed to (no subscription change); otherwise subscribe to
     one new item.
   Record results in `docs/validation.md` and `docs/support-matrix.md`.

## Facts established on the development machine

See `docs/feasibility.md`: Stellaris 4.5.2 and EU4 1.37.5 (Steam, Windows
11); EU4 has no `steam_appid.txt`; the helper initialised the Steam API in
Stellaris's app context and read item state correctly.
