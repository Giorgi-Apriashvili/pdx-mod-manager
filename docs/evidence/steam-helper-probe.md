# Evidence: Steam helper feasibility probe (Stellaris, Windows)

**Date:** 2026-10-29
**Question (spec section 6):** can a standalone third-party process initialise the Steam API in a
Paradox game's app context, using the game's own Steam API library, and read Workshop (UGC) item
state and install information?
**Result:** **Yes, for read-only operations.** Subscribe/download were deliberately not exercised
on the live account (see "Not verified").

## Environment

| Item | Value |
| --- | --- |
| OS | Windows 11 Enterprise 10.0.22631, x64 |
| Steam client | running, signed in, online; package version 1792618285 (`package/steam_client_win64.manifest`) |
| Game | Stellaris "Cygnus v4.5.2 (9776)", Steam app 281990, build id 25664273 |
| Game running? | No: checked with `tasklist` / `Get-Process stellaris` immediately before each run |
| Helper | `pmm-steam-helper.exe`, Debug build, MSVC 19.51. Run 2 used the code committed as `414b5c6` on branch `agent/providers`. Run 1 used an earlier build of the same code that lacked two changes irrelevant to this path (suppressing Windows error dialogs, renaming a local variable) |
| Working directory | a temp directory without `steam_appid.txt` (the app context came only from `SteamAppId`/`SteamGameId`, which the helper sets) |

### The game's Steam API library (read only, not copied)

| Property | Value |
| --- | --- |
| Path | `<SteamLibrary>\steamapps\common\Stellaris\steam_api64.dll` |
| Size | 298,384 bytes |
| Version resource | FileVersion 07.40.51.27, ProductVersion 01.00.00.01, ProductName "Steam Client API" (Valve Corporation) |
| SHA-256 | `1DB3FD414039D3E5815A5721925DD2E0A3A9F2549603C6CAB7C49B84966A1AF3` |
| Last write (UTC) | 2026-09-22 11:20:53 |
| Exports (dumpbin) | 1065 functions, including `SteamAPI_Init`, `SteamAPI_InitSafe`, `SteamAPI_SteamUGC_v016`, `SteamAPI_SteamUtils_v010`, `SteamAPI_SteamUser_v021`, `SteamAPI_SteamApps_v008`, `SteamInternal_FindOrCreateUserInterface`, `SteamAPI_ISteamUGC_{GetItemState,GetItemInstallInfo,GetItemDownloadInfo,SubscribeItem,DownloadItem,GetNumSubscribedItems}`, `SteamAPI_ISteamUtils_{IsAPICallCompleted,GetAPICallResult,GetAppID}`, `SteamAPI_ManualDispatch_*` |
| Not exported | `SteamAPI_InitFlat` (an SDK earlier than 1.58), so the helper fell back to `SteamAPI_InitSafe` as designed |

## Run 1: one-shot command script

Command (from `cmd.exe`; stdin from a file):

```text
pmm-steam-helper.exe --app-id 281990 --steam-api "<SteamLibrary>\steamapps\common\Stellaris\steam_api64.dll" < cmds.jsonl
```

`cmds.jsonl` (read-only commands only):

```json
{"protocol":1,"cmd":"ping"}
{"protocol":1,"cmd":"state","ids":["703156866","1623423360","2830996883","3090328185","1121692237"]}
{"protocol":1,"cmd":"ping"}
{"protocol":1,"cmd":"quit"}
```

Ids 703156866, 1623423360, 2830996883 and 3090328185 are folders under
`workshop/content/281990` (installed). 1121692237 is "Gigastructural Engineering & More (4.5)",
a public Stellaris item (confirmed through the anonymous Web API) that is **not** subscribed or
installed on this machine.

stdout, exactly as printed except that install paths are redacted to `<SteamLibrary>`:

```json
{"event":"ready","protocol":1,"ok":true,"loggedOn":true,"ownsApp":true,"appId":"281990","ugcInterface":"STEAMUGC_INTERFACE_VERSION016","init":"SteamAPI_InitSafe","subscribedCount":27}
{"event":"pong"}
{"event":"state","id":"703156866","flags":5,"subscribed":true,"installed":true,"downloading":false,"downloadPending":false,"needsUpdate":false,"legacy":false,"bytesDownloaded":0,"bytesTotal":0,"installPath":"<SteamLibrary>\\steamapps\\workshop\\content\\281990\\703156866","sizeOnDisk":429416,"timestamp":1782489192}
{"event":"state","id":"1623423360","flags":5,"subscribed":true,"installed":true,"downloading":false,"downloadPending":false,"needsUpdate":false,"legacy":false,"bytesDownloaded":0,"bytesTotal":0,"installPath":"<SteamLibrary>\\steamapps\\workshop\\content\\281990\\1623423360","sizeOnDisk":231762225,"timestamp":1791477347}
{"event":"state","id":"2830996883","flags":5,"subscribed":true,"installed":true,"downloading":false,"downloadPending":false,"needsUpdate":false,"legacy":false,"bytesDownloaded":0,"bytesTotal":0,"installPath":"<SteamLibrary>\\steamapps\\workshop\\content\\281990\\2830996883","sizeOnDisk":76147217,"timestamp":1781543136}
{"event":"state","id":"3090328185","flags":5,"subscribed":true,"installed":true,"downloading":false,"downloadPending":false,"needsUpdate":false,"legacy":false,"bytesDownloaded":0,"bytesTotal":0,"installPath":"<SteamLibrary>\\steamapps\\workshop\\content\\281990\\3090328185","sizeOnDisk":1093428,"timestamp":1782072748}
{"event":"state","id":"1121692237","flags":0,"subscribed":false,"installed":false,"downloading":false,"downloadPending":false,"needsUpdate":false,"legacy":false,"bytesDownloaded":0,"bytesTotal":0,"installPath":"","sizeOnDisk":0,"timestamp":0}
{"event":"pong"}
```

stderr (the Steam ID is redacted):

```text
Setting breakpad minidump AppID = 281990
SteamInternal_SetMinidumpSteamID:  Caching Steam ID:  <SteamID64> [API loaded no]
pmm-steam-helper: ready: app 281990, STEAMUGC_INTERFACE_VERSION016 via SteamAPI_InitSafe
pmm-steam-helper: exiting
```

Exit code 0; wall time about 0.11 s (15:56:30.56 to 15:56:30.67 local, UTC+4).

The first two stderr lines come from `steam_api64.dll` itself, which writes them to **stdout**.
The helper moves the process's stdout to stderr before loading the library, which kept them out
of the protocol stream. Without that, the very first protocol line would have been corrupted.

### Cross-check against Steam's own records

Values reported through `ISteamUGC` match `steamapps/workshop/appworkshop_281990.acf`, which the
Steam client maintains:

| Item | flags | sizeOnDisk (API) | size (acf) | timestamp (API) | timeupdated (acf) |
| --- | --- | --- | --- | --- | --- |
| 703156866 | 5 = subscribed\|installed | 429416 | 429416 | 1782489192 | 1782489192 |
| 1623423360 | 5 | 231762225 | 231762225 | 1791477347 | 1791477347 |
| 2830996883 | 5 | 76147217 | 76147217 | 1781543136 | 1781543136 |
| 3090328185 | 5 | 1093428 | 1093428 | 1782072748 | 1782072748 |
| 3243319558 (run 2) | 5 | 573715 | 573715 | 1781658607 | 1781658607 |
| 1121692237 | 0 = none | 0 | not listed | 0 | not listed |

`subscribedCount` (27) equals the number of item folders under `workshop/content/281990` (27).

## Run 2: sustained interactive session

Driven from PowerShell through redirected pipes, with pauses so that the callback pump runs
across several seconds between commands. Times are milliseconds since process start:

```text
   106 ms  <- {"event":"ready","protocol":1,"ok":true,"loggedOn":true,"ownsApp":true,"appId":"281990","ugcInterface":"STEAMUGC_INTERFACE_VERSION016","init":"SteamAPI_InitSafe","subscribedCount":27}
   111 ms  <- {"event":"pong"}
  2120 ms  <- {"event":"state","id":"3243319558","flags":5,...,"installPath":"<SteamLibrary>\\steamapps\\workshop\\content\\281990\\3243319558","sizeOnDisk":573715,"timestamp":1781658607}
  2120 ms  <- {"event":"state","id":"1121692237","flags":0,...,"installPath":"","sizeOnDisk":0,"timestamp":0}
  4129 ms  <- {"event":"error","code":"invalid_id","message":"Workshop item id must be a canonical decimal number.","cmd":"state"}
  4143 ms  exit code 0
```

The `invalid_id` line answers a deliberately malformed `state` request (`"ids":["not-a-number"]`),
which the helper rejected before calling Steam. The session ended at 2026-10-29T12:10:41Z.

## Side effects observed

- **Steam treats every helper session as a Stellaris launch.** `steamapps/appmanifest_281990.acf`
  `LastPlayed` changed to 1793274992 (2026-10-29 11:56:32 UTC) after run 1, and to 1793275842
  (12:10:42 UTC) after run 2. Expect the Steam UI to show the user as "Playing Stellaris" while a
  helper runs, and expect playtime to grow by the session length. The Steam UI was not watched
  during these runs.
- `appworkshop_281990.acf` `TimeLastAppRan` did not change. Its `TimeLastUpdated` changed once
  between my initial reads, before either run, so it was not caused by the helper.
- Nothing was written under the Steam folder or `Documents/Paradox Interactive` by the probe. The
  helper never writes files.
- No subscription, download, or other account change was requested. Neither probe sent a
  `subscribe` or `download` command.

## Conclusions

1. **Feasible:** a standalone third-party executable can load the game's own `steam_api64.dll`,
   initialise it in the game's app context (app 281990) with only `SteamAppId`/`SteamGameId` set
   in its environment, and obtain `ISteamUGC`, `ISteamUtils`, `ISteamUser` and `ISteamApps`. No
   Steamworks SDK files and no `steam_appid.txt` were needed. Initialisation took about 0.1 s.
2. **Read access works:** `GetItemState`, `GetItemInstallInfo` and `GetNumSubscribedItems`
   returned correct data for installed items and `flags=0` for an unsubscribed item. That is
   enough for "already installed" detection, reconciliation after a restart, and rescans after the
   manual fallback.
3. **Session facts are available:** `BLoggedOn` and `BIsSubscribedApp(281990)` reported true,
   which lets the application explain offline or ownership problems before it attempts a
   download.
4. **Protocol robustness:** the library's own stdout logging is real. The stdout redirection in
   the helper is necessary, and it worked.
5. **User-visible side effect:** each session counts as running Stellaris, which updates
   `LastPlayed` and probably shows the user as in-game. The application must disclose this, keep
   helper sessions short, and shut them down when idle.

## Not verified (requires explicit user approval or other machines)

- `SubscribeItem` / `DownloadItem` against the live account, including the call-result path
  (`IsAPICallCompleted` + `GetAPICallResult` for callback 1313 with the 16-byte struct). That
  path is covered only by tests against the project's fake Steam API library. A live test
  changes the user's subscriptions and needs their approval. A proposed procedure is in
  `docs/steam-workshop.md`.
- Behaviour while Stellaris itself is running, and while the user is in Steam offline mode.
- Linux (`libsteam_api.so`) and macOS (`libsteam_api.dylib`) builds of the helper. Both were
  written to compile there but were not compiled or run on this machine.
- Europa Universalis IV (app 236850) is not installed on this machine.
