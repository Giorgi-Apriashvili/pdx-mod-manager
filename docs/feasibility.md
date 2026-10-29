# Feasibility investigation

Status labels used throughout the project documentation:

- **Verified**: observed on a real installation, with the evidence recorded.
- **Implemented, unverified**: code and automated tests exist, but the
  behaviour has not been observed against the real game or store.
- **Unsupported**: not available; the blocker and alternatives are listed.

Investigation environment (2026-10-29): Windows 11 Enterprise 23H2
(10.0.22631), Steam client running, Visual Studio 2026 (MSVC 19.51), Qt 6.10.3.

## 1. Installed games on the development machine

| Game | Steam app | State on this machine | Notes |
|---|---|---|---|
| Stellaris | 281990 | Installed via Steam, build 25664273, `launcher-settings.json` reports "Cygnus v4.5.2 (9776)" | 27 Workshop items installed, 10 local mods, 2 launcher playsets |
| Europa Universalis IV | 236850 | **Not installed** (no `appmanifest_236850.acf`; only leftover user data in `Documents/Paradox Interactive/Europa Universalis IV`) | EU4 behaviour cannot be verified on this machine |

Consequence: every EU4-specific claim in this project is at best
*implemented, unverified* until someone runs the validation procedure in
[`docs/validation.md`](validation.md) on a machine with EU4 installed.

## 2. How the Paradox launcher hands mods to the game (Stellaris, observed)

Observed files in `Documents/Paradox Interactive/Stellaris/`:

- `launcher-v2.sqlite`: the launcher's own database (tables `playsets`,
  `playsets_mods(playsetId, modId, enabled, position)`, `mods`). It is the
  launcher's private state. **This application never reads or writes it.**
- `dlc_load.json`: `{"enabled_mods":["mod/ugc_1880071041.mod", ...],"disabled_dlcs":[]}`.
  For the active launcher playset, the order of `enabled_mods` matches
  `playsets_mods.position` ascending (position 0 first), and only enabled
  entries are listed.
- `mod/*.mod`: descriptor files. Workshop items are registered as
  `mod/ugc_<id>.mod` containing `path="<Steam library>/steamapps/workshop/content/281990/<id>"`
  and `remote_file_id="<id>"`; local mods as `mod/<name>.mod` pointing at a
  folder under `mod/`.
- `logs/error.log` from the last game start contains lines such as
  `[dlc.cpp:346]: Invalid supported_version in file: mod/ugc_703156866.mod`
  for descriptors that are **not** enabled in `dlc_load.json`: the game parses
  every descriptor in `mod/` at startup and enables those listed in
  `dlc_load.json`.
- The Workshop copy of a descriptor (`<content>/<id>/descriptor.mod`) may
  carry a stale `path` from the author's machine; the registered
  `mod/ugc_<id>.mod` is what carries the correct local path.
- `<install>/launcher-settings.json` declares `"exePath": "./stellaris.exe"`
  and `"exeArgs": ["-gdpr-compliant"]`; the install folder contains
  `steam_appid.txt`, which lets the Steam API initialise when the executable
  is started directly while Steam runs.

Direct launch approach: write `dlc_load.json` (and any missing
`mod/ugc_<id>.mod` descriptors), then start `<install>/stellaris.exe` with
the arguments from `launcher-settings.json`, working directory = install
folder, through `CreateProcessW` (no shell). Status: **implemented, see
validation record** (`docs/validation.md`).

## 3. Load-order semantics

Community sources disagree about override precedence (see
[`docs/load-order.md`](load-order.md) for the sources). The one consistent
observation is that the game reads `enabled_mods` in array order. Precedence
for identical file paths is therefore verified in-game with observable probe
mods (`tools/probe-mods`) rather than assumed.

## 4. Workshop acquisition from a third-party application

Constraints:

- The Steamworks API binds a process to one AppID for its lifetime
  (`SteamAppId` environment variable or `steam_appid.txt`), and ISteamUGC
  calls act on behalf of that app and the logged-in user. A game-independent
  manager therefore needs one Steam API session per game.
- The Steamworks SDK may not be redistributed by this project. Every
  Paradox game ships its own Steam API library: Stellaris has
  `steam_api64.dll` in its install folder exporting the flat C API
  (`SteamAPI_ISteamUGC_SubscribeItem`, `..._DownloadItem`,
  `..._GetItemState`, `..._GetItemInstallInfo`, `..._GetItemDownloadInfo`,
  `SteamAPI_SteamUGC_v016`, `SteamAPI_ManualDispatch_*`, ...).

Design: a small helper process (`pmm-steam-helper`) loads the **game's own**
Steam API library with `SteamAppId=<game app id>`, and the GUI talks to it
over a JSON-lines pipe. Read-only probe results and the remaining
verification steps are recorded in [`docs/evidence/steam-helper-probe.md`](evidence/steam-helper-probe.md)
and [`docs/steam-workshop.md`](steam-workshop.md).

Probe result (2026-10-29, Stellaris 4.5.2, Windows 11, read-only commands
only), status **verified for session and item-state access**:

- The helper, a separate unsigned executable outside the game folder,
  initialised the Steam API through the game's `steam_api64.dll`
  (`SteamAPI_InitSafe`, interface `STEAMUGC_INTERFACE_VERSION016`) and
  reported `loggedOn: true`, `ownsApp: true`, 27 subscribed items.
- `GetItemState`/`GetItemInstallInfo` returned flags 5 (subscribed +
  installed), install folders, sizes and timestamps that match Steam's own
  `appworkshop_281990.acf` exactly; an unsubscribed item returned flags 0.
- Side effect: each helper session counts as a Stellaris launch for Steam
  (the app manifest's LastPlayed changed); Steam may show the user as
  playing the game while the helper runs.
- The Steam library writes the user's SteamID to the helper's stderr; that
  stream is treated as sensitive and redacted in diagnostics.

Still **unverified** on a live account: `SubscribeItem` / `DownloadItem` and
the subscribe call result. Running them changes the user's Workshop
subscriptions, so they are executed only with the user's explicit approval
(procedure in `docs/steam-workshop.md`).

Requirements for this path: Steam client running, user logged in, user owns
the game (checked with `ISteamApps::BIsSubscribedApp`). No credentials are
collected or stored. While a helper session is open Steam may show the user
as playing the game.

Fallback (clearly labelled, does not satisfy automatic download): open
`steam://url/CommunityFilePage/<id>` in the Steam client and rescan until the
item appears in the Workshop content folder.

Alternatives considered and rejected:

- **SteamCMD `workshop_download_item`**: needs a separate login (password or
  cached credentials for SteamCMD), downloads outside the Steam client's
  library so items are not subscribed or updated, and Paradox titles generally
  do not allow anonymous Workshop downloads.
- **Steam Web API**: read-only metadata (`ISteamRemoteStorage/GetPublishedFileDetails`,
  no key required) is used for identity/game verification and names; it
  cannot subscribe or download for a user without their credentials.
- **Opening `steam://` URLs only**: kept as the manual fallback.

## 5. Platform notes (not verifiable on this machine)

- Linux: user data under `~/.local/share/Paradox Interactive/<Game>`; native
  executables `stellaris` / `eu4`; Steam API library `libsteam_api.so` in the
  game folder; Steam root `~/.steam/steam`, `~/.local/share/Steam` or the
  Flatpak path.
- macOS: user data under `~/Documents/Paradox Interactive/<Game>`; app bundles
  `stellaris.app` / `eu4.app` with `Contents/MacOS/<exe>`; `libsteam_api.dylib`
  inside the bundle; Steam root `~/Library/Application Support/Steam`.

These are implemented from documentation and community knowledge and are
**unverified** until run on those systems (see the support matrix).
