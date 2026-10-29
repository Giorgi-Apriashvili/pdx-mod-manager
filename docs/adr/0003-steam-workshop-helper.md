# ADR 0003: Workshop acquisition through a per-game helper using the game's own Steam API library

- Status: Accepted
- Date: 2026-10-29

## Context

Imports must acquire missing Steam Workshop mods automatically where a
validated integration permits it (spec section 6). Findings from the
feasibility investigation ([`docs/feasibility.md`](../feasibility.md) §4):

- The Steamworks API binds a process to one AppID for its lifetime, and
  `ISteamUGC` acts on behalf of that app and the signed-in user. A manager for
  several games therefore needs one Steam API session per game.
- The Steamworks SDK may not be redistributed by this project, but every
  Paradox game ships its own Steam API library (for example `steam_api64.dll`
  in the Stellaris folder) exporting the flat C API.
- A read-only probe on 2026-10-29 (Stellaris 4.5.2, Windows 11) showed that a
  separate executable can initialise that library in the game's app context
  with only `SteamAppId`/`SteamGameId` set, and read item state and install
  information ([evidence](../evidence/steam-helper-probe.md)).

## Decision

- `pmm-steam-helper` (`tools/steam-helper`) is a small executable started on
  demand, one process per game. It loads the **game's own** Steam API library
  at run time (`LoadLibraryExW` / `dlopen`), initialises it for the game's app
  id, and speaks a JSON-lines protocol on stdin/stdout
  (`providers/helper_protocol.hpp`).
- The protocol can only query state, subscribe and request a download for
  validated canonical published-file ids. It has no command to unsubscribe,
  delete, or pass paths or URLs.
- The application drives a pure acquisition state machine
  (`providers/acquisition.hpp`) that distinguishes *subscribed*, *download
  requested* and *installed and verified*, persists jobs, retries with backoff
  and reconciles after restarts.
- A clearly labelled manual fallback opens the item in the Steam client
  (`steam://url/CommunityFilePage/<id>`) and rescans. It is documented as not
  satisfying automatic download.
- Read-only metadata (names, sizes, game association) comes from the key-less
  Steam Web API `GetPublishedFileDetails`.

## Consequences

- No Valve code is built or shipped; packages never contain Steam binaries.
- Requirements for the automatic path: Steam running, user signed in, game
  owned on that account. No credentials are ever collected or stored.
- While a helper runs, Steam treats the game as running (observed: the app
  manifest's `LastPlayed` changed); the user may appear as playing it. The UI
  must disclose this, sessions are kept short, and the helper is stopped
  before launching the game.
- A crash or hang of the game's library is isolated in the helper process.
- Policy risk: Steam's agreements do not explicitly cover a third-party tool
  initialising the API in a game's app context, and a game could ship an
  incompatible library. The fallback remains available.
- Subscribe/download against a live account, EU4, Linux and macOS are
  **unverified** ([support matrix](../support-matrix.md)); the call-result
  struct layout is tested only against the project's fake library.
- On macOS a hardened, notarised helper needs the
  `com.apple.security.cs.disable-library-validation` entitlement to load a
  library signed by another team ([packaging](../packaging.md)).

## Alternatives considered

- **SteamCMD `workshop_download_item`**: needs a separate login (password or
  cached SteamCMD credentials), downloads outside the Steam client library so
  items are neither subscribed nor updated, and Paradox titles generally do not
  allow anonymous Workshop downloads.
- **Steam Web API**: provides metadata only; it cannot subscribe or download
  for a user without their credentials. Used for metadata.
- **`steam://` URLs only**: requires manual action per item; kept as the
  labelled fallback.
- **Initialising the Steam API inside the GUI process**: would bind the whole
  application to one game for its lifetime and expose it to the library's
  crashes and stdout logging.
- **Bundling the Steamworks SDK library**: not permitted for this project, and
  the game's library already matches what the game uses.
