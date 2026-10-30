# Paradox Mod Manager — Implementation Specification

> Amended 2026-10-30: the product is Windows only; the reason is in [docs/adr/0009-windows-only.md](docs/adr/0009-windows-only.md).

## Instructions to the implementation agent

Build a professional, compact Windows desktop mod manager for Paradox games. Treat this document as the product specification and implementation prompt. Deliver working software, tests, build configuration, and documentation. Do not substitute a visual prototype or mocked integrations for the required functionality.

Use a C++ backend, CMake, and Git. Use Qt 6 Quick/QML for the frontend unless a documented technical investigation establishes a compelling reason to change it. Keep domain logic in C++, with QML responsible for presentation and interaction.

Work incrementally. Investigate the real game and distribution-platform interfaces before committing to integration assumptions. Clearly distinguish implemented, experimentally verified, and unsupported capabilities. If a core requirement cannot be achieved through a supported integration, document the blocker and the available alternatives rather than silently reducing scope.

## 1. Product goal

Let players manage mods, share a mod selection and its exact order as a copyable code, reproduce that setup on another machine, and launch a game directly with those mods while bypassing the Paradox launcher.

The primary workflow is:

1. Select a game from the left sidebar.
2. Enable, disable, and reorder mods in the center list.
3. Export a share code from the right panel.
4. Another player imports that code, resolves and downloads missing mods, and obtains the same ordered selection.
5. Press **Play** to launch the selected game with the selected mods, without opening the Paradox launcher.

The application must run on Windows only. Support for a particular game depends on that game's available executable and validated integration; do not equate the application running with universal game compatibility.

## 2. Scope and priorities

### First release

- Fully implement Europa Universalis IV and Stellaris through separate game adapters.
- Discover installed games, Steam libraries, installed Workshop mods, and local mods.
- Support manually selected installation and user-data directories.
- Maintain multiple named local playsets per game.
- Enable, disable, search, filter, and reorder mods.
- Export and import portable share codes containing mod identities, enabled states, and ordering.
- Resolve missing mods and automate subscription/download where a validated integration permits it.
- Launch supported games directly with the active playset.
- Package the desktop application for Windows.

Design the adapter architecture to accommodate games such as Hearts of Iron IV, Crusader Kings III, and Victoria 3. Additional games are subsequent work, not placeholders advertised as supported.

### Outside the first release

- Mod authoring, publishing, or hosting mod files.
- Multiplayer matchmaking, social accounts, or cloud playset storage.
- Automatic conflict patch generation or guaranteed compatibility between arbitrary mods.
- Distribution of game files, DLC, or historical Workshop versions.
- Full Paradox launcher feature parity.
- Automatic application updates, unless added after the core workflow is complete.

## 3. Desktop layout and interaction

Use a compact, resizable three-column layout with clear typography, restrained colors, and high information density. Avoid oversized cards, unnecessary whitespace, and browser-style navigation.

```text
┌─────────────────┬─────────────────────────────────┬──────────────────────┐
│ Games           │ Active playset ▾    Search      │ Playset options      │
│                 ├─────────────────────────────────┤                      │
│ Europa          │ #   On   Mod name       Status  │ Import code          │
│ Universalis IV  │ 1   ☑    Mod A          Ready   │ Export code          │
│                 │ 2   ☐    Mod B          Ready   │                      │
│ Stellaris       │ 3   ☑    Mod C          Missing │ Selected mod details │
│                 │                                 │                      │
│ Other games     │                                 │ Validation / progress│
│                 │                                 │                      │
│ Settings        │ Mod count / enabled count       │        Play          │
└─────────────────┴─────────────────────────────────┴──────────────────────┘
```

### Left: games

- Show game names and installation status; icons may supplement labels.
- Selecting a game restores its last active playset and view state.
- Provide a clear path to locate a missing installation or rescan libraries.

### Center: mods

- Each row shows order, enabled checkbox, name, source, and readiness/warnings.
- Support drag-and-drop ordering and accessible Move Up / Move Down actions.
- Search by name and filter by enabled, disabled, missing, or warning state.
- Sorting for browsing must not silently change load order. Reordering while filtered must have defined behavior; disable it with an explanation if safe semantics are unavailable.
- Support bulk enabling/disabling, with dependency issues surfaced immediately.
- Persist playset edits and maintain an undo path for destructive playset changes.

### Right: actions and details

- Give **Import code** and **Export code** prominent placement near the top.
- Provide playset creation, duplication, renaming, and deletion.
- Show selected-mod details, dependency information, and source links.
- Show download progress and concrete recovery actions.
- Place a prominent **Play** action near the bottom. Explain blocking conditions beside it.

### Quality requirements

- Support keyboard navigation, visible focus, accessible labels, and high-DPI displays.
- Keep the interface responsive during scans, downloads, and metadata parsing.
- Use virtualized lists; validate responsiveness with at least 2,000 synthetic mod records.
- Provide readable light and dark themes and remember window size and column widths.
- Preserve useful state across restarts; do not rely on color alone for status.

## 4. Domain model and persistence

Keep installed mod inventory separate from playset membership. A mod can be installed without belonging to a playset.

Model at least:

- **GameDefinition:** stable game key, platform identity, adapter, supported capabilities.
- **GameInstallation:** installation path, user-data path, executable, platform, detected version.
- **ModIdentity:** source provider plus stable provider ID, scoped to its game.
- **InstalledMod:** identity, local location, descriptor metadata, installation/readiness state.
- **Playset:** local ID, game key, display name, ordered entries, timestamps.
- **PlaysetEntry:** mod identity, enabled state, optional informational version/fingerprint.
- **ImportPlan:** validated manifest, proposed changes, unresolved entries, acquisition jobs.
- **LaunchPlan:** validated executable, working directory, arguments, environment, configuration writes.

Use SQLite for application-owned persistent state, with schema migrations and transactional updates. Keep game-generated files separate from the application's database. Store application data in standard per-user OS locations, with logs and cache in their appropriate directories.

Do not use a display name or an absolute filesystem path as a portable mod identity. Store provider IDs as strings to avoid numeric precision loss.

## 5. Share-code protocol — core feature

### Required semantics

An export captures the active playset's entries, including disabled entries, in exact order. It does not include the user's entire installed inventory unless that inventory is explicitly part of the playset.

Importing preserves that ordered list and every enabled flag. Extra installed mods on the recipient's machine remain installed but are not activated by the imported playset.

The code is self-contained and requires no application-operated server to decode. It contains references to mods, not mod files, credentials, executable commands, or machine-specific paths.

### Version 1 encoding

Use the textual envelope `PMM1.<payload>.<digest>`:

- Payload: UTF-8 JSON, compressed with zlib, encoded as unpadded Base64URL.
- Digest: lowercase SHA-256 hex of the compressed bytes, for corruption detection only. It is not proof of authenticity.
- Include an explicit schema version inside the JSON as well as the envelope version.
- Export deterministically for unchanged playset content; omit volatile timestamps from the encoded payload.
- Provide clipboard copy/paste and file export/import using the same code format.

Illustrative decoded manifest; names and identifiers below are placeholders:

```json
{
  "schemaVersion": 1,
  "game": "stellaris",
  "playsetName": "Example playset",
  "gameVersionHint": "optional",
  "mods": [
    {
      "source": "steam-workshop",
      "id": "1234567890",
      "nameHint": "Example enabled mod",
      "enabled": true
    },
    {
      "source": "steam-workshop",
      "id": "1234567891",
      "nameHint": "Example disabled mod",
      "enabled": false
    }
  ]
}
```

The array position is the authoritative playset order. Each adapter translates that order into the game's verified load-order semantics. Document which end loads first and how precedence works for each supported game.

### Import workflow

1. Decode, check integrity, and validate the complete manifest before changing persistent state or starting downloads.
2. Identify the game and show an import preview: name, mod count, enabled count, missing content, and warnings.
3. Create a new playset by default. Replacing an existing playset must be an explicit user choice with a recoverable previous state.
4. Match entries against installed inventory by stable identity.
5. Acquire missing referenced mods, including disabled entries, where supported. Do not redownload installed, usable content unnecessarily.
6. Track each entry as ready, pending, downloading, failed, unavailable, or requiring user action.
7. Save the exact intended playset even when acquisition is incomplete, so the user can retry without losing the setup.
8. Allow Play only when all enabled entries are ready and required validation passes. Missing disabled entries may remain visibly unresolved without blocking launch.

Do not silently omit failed entries, change enabled flags, reorder content, or activate dependencies that were not in the imported setup. Offer explicit fixes and explain that accepting them changes the imported setup.

### Validation and portability

- Reject unknown protocol/schema versions with an actionable message.
- Reject invalid game/provider combinations, duplicate identities, invalid field types, and unreasonable field lengths.
- Set explicit limits: at most 2 MiB of encoded input, 8 MiB of decompressed JSON, and 10,000 entries. Enforce output limits during decompression, not afterward.
- Accept surrounding whitespace but reject malformed payloads and truncated digests.
- Never interpret imported content as a shell command or arbitrary download URL.
- Treat names as display hints only; verify authoritative identity and game association through the provider when possible.
- Local mods may be represented with a content fingerprint and display hint. Require explicit recipient-side mapping or installation; never imply they can be downloaded automatically.
- Explain that identical references, ordering, and enabled states do not guarantee identical bytes or multiplayer checksums: mods and games can update. Version/fingerprint mismatches should be visible when information is available.

## 6. Mod acquisition and provider integration

Implement a provider interface separate from game adapters. Steam Workshop is the initial acquisition target; local filesystem mods are also required. Leave room for other providers without claiming support before implementation.

First investigate whether a standalone third-party application can perform subscription and download operations for each target game under the intended distribution model. The existence of Steamworks methods alone does not establish that this application has the necessary access or app context.

For each supported acquisition path, verify authentication requirements, ownership requirements, subscription behavior, download completion, and access to the resulting files. Reuse a supported user session; do not collect or persist Steam passwords.

Distinguish **subscribed**, **download requested**, and **installed and ready**. A successful subscription request is not a completed download. Confirm completion and usable content before enabling launch.

Handle offline operation, pending downloads, revoked/deleted/private items, invalid IDs, unavailable providers, retries, cancellation, and interrupted application sessions. Persist enough state to resume or reconcile safely on restart.

If fully automated acquisition is unavailable, provide a clearly labeled fallback that opens the relevant provider page/client, then rescans for completion. This fallback is a useful limited capability, but does not satisfy the automatic-download requirement. Record the remaining blocker explicitly before claiming the release meets the full specification.

Do not automatically unsubscribe from or delete mods. Cancelling an import may stop application-managed jobs but must explain that already completed subscriptions/downloads can remain.

## 7. Game adapters and launcher bypass

Each game adapter must own:

- Installation and user-data discovery for each supported OS/store combination.
- Descriptor parsing and identity association.
- Version/capability detection and compatibility warnings.
- Load-order translation and generation of required game configuration.
- Executable discovery, launch arguments, working directory, and environment.
- Validation, backup/recovery rules, and integration fixtures.

Do not assume all Paradox games use the same configuration format, filename, launch flags, or precedence rules. Investigate EU4 and Stellaris separately against current installed versions.

### Play workflow

1. Validate the installation, active playset, enabled mod availability, and writable configuration locations.
2. Check for another running instance of the game or conflicting configuration writer.
3. Build a launch plan and preserve backups of files that will change.
4. Apply the minimum required configuration atomically where possible; preserve unrelated settings and DLC choices.
5. Launch the actual game executable through a validated OS-specific process API, bypassing the Paradox launcher.
6. Surface process-start failures and provide useful diagnostics.

Steam or another store client may still be required for ownership, DRM, or integration. Bypassing the Paradox launcher does not imply bypassing these requirements. Do not route through an unverified store launch path that opens the Paradox launcher again.

Never replace or patch game binaries. Treat imported paths and arguments as untrusted and do not launch through shell string concatenation. Custom executable selection, if offered, must be a local user action outside the share-code format.

Document when generated configuration remains in effect, how to restore backups, and how the application coexists with subsequent use of the official launcher. If configuration changes fail partway through, restore the prior state. On restart, detect interrupted transactions and offer recovery.

Validate launcher bypass and effective mod ordering in the running game, using observable test mods where necessary. A process starting successfully is insufficient evidence that mods loaded correctly.

## 8. Architecture and build system

- C++20 or newer, with a consistent standard across supported platforms.
- Qt 6 Quick/QML and Qt Quick Controls for UI; expose focused C++ models/services.
- Target-based CMake with CMakePresets.json and separate configure/build/test presets.
- SQLite persistence, a well-maintained JSON library, and a zlib-compatible compression library.
- Reproducible dependency versions through one documented package-management approach.
- Async workers for filesystem scans, provider operations, and expensive parsing; UI changes remain on the GUI thread.
- Structured errors and logs, with sensitive information and unnecessary personal paths redacted from exported diagnostics.
- No required backend server, telemetry, or application account for basic operation.

Suggested repository structure:

```text
CMakeLists.txt
CMakePresets.json
src/
  core/             # Domain models and playset logic
  persistence/      # Database and migrations
  sharing/          # Manifest validation and code codec
  providers/        # Workshop and local inventory
  games/            # EU4 and Stellaris adapters
  platform/         # OS paths, processes, file operations
  app/              # Composition and UI-facing models
ui/                 # QML components and assets
tests/              # Unit, integration, fixtures
docs/               # Architecture, support matrix, decisions
packaging/
```

Keep core playset and protocol logic testable without launching the GUI. Avoid a single controller that combines UI, downloads, persistence, and game-specific behavior.

Review dependency licenses and Qt distribution obligations before choosing the release packaging approach. Record the project's own license explicitly.

## 9. Professional engineering and Git workflow

- Inspect existing repository instructions and changes before editing. Preserve unrelated work.
- If starting in a genuinely new project directory without a repository, initialize Git there; do not accidentally initialize inside an unrelated parent repository.
- Make coherent commits at meaningful milestones, such as build scaffolding, codec implementation, an adapter, or a verified import workflow.
- Use concise imperative commit messages. Do not commit generated build outputs, credentials, downloaded mods, or game binaries.
- Do not push, publish releases, or rewrite existing history unless authorized.
- Provide a README with prerequisites, development/build/test commands, packaging steps, supported capabilities, and troubleshooting.
- Include formatting/static-analysis configuration, a suitable .gitignore, CI, a changelog, and short architecture decision records for material tradeoffs.
- Use semantic release versioning. Version the share-code protocol independently and maintain regression fixtures for backward compatibility.

## 10. Verification and acceptance criteria

### Automated checks

- Codec round-trip preserves game, entry order, identities, and enabled states exactly.
- Deterministic exports and compatibility fixtures protect the public protocol.
- Malformed, oversized, unsupported-version, duplicate, wrong-game, and decompression-limit cases fail safely without side effects.
- Import reconciliation handles already installed, missing, disabled, unavailable, and local-only mods.
- Failed/cancelled imports preserve existing playsets and do not report false readiness.
- Adapter fixtures verify descriptor parsing and configuration output, including paths containing spaces and Unicode.
- Configuration writes preserve unrelated fields and recover after simulated failures.
- Database migrations and recovery behavior are exercised.
- Launch-plan tests verify executable, working directory, arguments, and translated order without requiring proprietary game binaries in CI.
- Build and test in CI on Windows; apply platform-appropriate compiler warnings and sanitizers where practical.

### Manual end-to-end acceptance

For EU4 and Stellaris, record the tested game version, OS, distribution source, and result:

1. Detect a real installation and its installed mods.
2. Create a playset with at least three mods, including a disabled entry and deliberately non-default ordering.
3. Export its code and import it into a clean application profile on another machine or equivalent isolated environment.
4. Resolve at least one initially missing Workshop mod through the implemented acquisition flow.
5. Confirm exact identity/order/enabled-state equality after import, with no unintended active mods.
6. Launch without displaying the Paradox launcher and verify the enabled mods and effective order in-game.
7. Confirm that missing enabled content blocks launch, and that an unavailable disabled entry remains visible without changing the requested configuration.
8. Restart the manager and verify that playsets and pending acquisition state persist.
9. Exercise failed download, offline use, bad share code, configuration permission failure, and process-start failure.

Publish an explicit support matrix. Mark untested combinations as unverified, not supported. If the development environment lacks a required OS or game installation, provide the exact outstanding validation procedure and do not invent successful results.

## 11. Implementation sequence and deliverables

1. **Feasibility:** investigate Workshop acquisition and direct modded launch for EU4/Stellaris. Record evidence, constraints, supported platforms, and unresolved blockers.
2. **Foundation:** establish CMake, Qt shell, core models, persistence, CI, and initial game discovery.
3. **Playsets:** implement mod inventory, ordering, enabled state, and the compact three-column interface.
4. **Sharing:** implement versioned encoding, validation, preview, and exact import reconciliation with tests.
5. **Acquisition:** integrate real subscription/download behavior and visible recovery states.
6. **Launch:** implement and validate per-game configuration, direct launch, and backup/recovery.
7. **Release readiness:** complete accessibility, packaging, end-to-end validation, documentation, and support matrix.

The final handoff must include source code, meaningful Git history, reproducible build/test instructions, packaged builds where the environment permits, test evidence, known limitations, and a concise description of any requirements still blocked. Do not describe a mocked provider or an unverified direct-launch path as production-ready.

## 12. Research starting points

These sources guide investigation; they do not establish that every integration is available to a third-party app:

- [Steamworks ISteamUGC reference](https://partner.steamgames.com/doc/api/isteamugc): subscription, download, installation-state, and callback APIs. Validate access in this application's intended context.
- [Paradox launcher troubleshooting](https://support.paradoxplaza.com/hc/en-us/articles/360010684620-The-Paradox-Launcher-v2-doesn-t-work): identifies game-dependent launcher configuration files. It is not a documented universal direct-launch contract.

Recheck integration details at implementation time and retain evidence of the actual tested behavior.
