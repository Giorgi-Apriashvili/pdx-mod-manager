# Steam Workshop acquisition

This document describes how Paradox Mod Manager acquires missing Steam Workshop mods
(spec section 6): the per-game helper process, its wire protocol, the acquisition state
machine, error mapping, the manual fallback, and what is and is not verified.

Code: `src/providers/` (library `pmm_providers`), `tools/steam-helper/` (`pmm-steam-helper`).
Evidence: [`docs/evidence/steam-helper-probe.md`](evidence/steam-helper-probe.md).

## Status

| Capability | Status |
| --- | --- |
| Start a Steam API session in the game's app context from our own process (Windows, Stellaris) | **Experimentally verified** 2026-10-29 |
| Read item state, install path, size and timestamp (`GetItemState`, `GetItemInstallInfo`) | **Experimentally verified** 2026-10-29 |
| Read session facts (`BLoggedOn`, `BIsSubscribedApp`, subscribed count) | **Experimentally verified** 2026-10-29 |
| Subscribe (`SubscribeItem` + call result 1313) and download (`DownloadItem`) | **Verified live** 2026-10-30 on Windows 11 for EU4 1.37.5: an item not subscribed before went subscribing → download requested → downloading → installed in about 3 s through the application; a job pending at quit resumed after a restart without a duplicate; an item of another game was rejected by the identity gate without contacting Steam. The same for Stellaris 4.5.2 (one item, about 5.6 s) ([evidence](evidence/live-validation-2026-10-30.md)) |
| Acquisition state machine (retry, cancel, restart reconciliation, stall detection) | Implemented, unit tested (pure logic) |
| Manual fallback (open Workshop page, then rescan) | Implemented. **Limited**: does not satisfy automatic download |
| Web API metadata (names, sizes, wrong-game check) | Request builder and parser in `providers`; the app layer's `QtWorkshopMetadataHost` makes the call (tested against a local stub) |
| Identity gate (Subscribe/RequestDownload only for items the Web API confirmed) | Implemented in the services coordinator, unit tested (section 3, "Identity gate") |
| Linux helper | Compiled with GCC 16.2 (Arch Linux in WSL, 2026-10-30); the end-to-end tests against the fake library pass, also under ASan + UBSan. **Not run against a real Steam client** |
| macOS helper | Written, **not compiled or run** (no Mac available) |
| Europa Universalis IV | Same mechanism expected (the game ships `steam_api64.dll`). EU4 1.37.5 is installed on the development machine, but the helper has **not been run** for it yet |

## 1. Architecture

```text
 GUI thread               worker thread (app layer)              helper process, one per game
+------------+  status  +---------------------------+  JSON lines  +-----------------------------+
| QML models | <------- | driver: one Acquisition-  | -----------> | pmm-steam-helper            |
|            |          | Context per job,          |  stdin       |  --app-id 281990            |
|            | -------> | reduceAcquisition(),      | <----------- |  --steam-api <game>/        |
+------------+  user    | persistence, VerifyContent|  stdout      |      steam_api64.dll        |
               actions  +---------------------------+              +--------------+--------------+
                                                                                  | Steam IPC
                                                                         +--------v--------+
                                                                         | Steam client    |
                                                                         +-----------------+
```

### Why a helper process per game

- **One AppID per process.** `SteamAPI_Init` binds the calling process to one app for its whole
  lifetime, taken from `SteamAppId`/`steam_appid.txt`. Managing EU4 (236850) and Stellaris
  (281990) therefore needs separate processes.
- **Isolation from the GUI.** The game's Steam library brings its own C runtime and threads, logs
  to stdout, and can crash or hang. In a separate process, a crash becomes
  `ProviderUnavailable(HelperFailed)` and an automatic retry; the GUI is unaffected.
- **Session lifetime is visible to the user.** While the API is initialised, Steam treats the
  process as the game running: `LastPlayed` changes and the user probably shows as "Playing
  Stellaris" (see the evidence). A separate process can be started on demand and stopped as
  soon as acquisition is idle.
- **No SDK redistribution.** The helper loads the `steam_api` library that the user's game
  installation already contains, at run time. No Steamworks SDK headers, import libraries or
  binaries are needed to build, and none are in the repository or the packages.
- **Small, safe surface.** The protocol can query state, subscribe, and request a download.
  There is no command to unsubscribe, delete, or pass paths or URLs, and every id is validated as
  a canonical decimal `uint64`.

### How the helper binds to the game's library

1. Validate arguments: `--app-id` must be canonical decimal digits (fits in `uint32`).
   `--steam-api` must name exactly `steam_api64.dll` or `steam_api.dll` (Windows),
   `libsteam_api.so` (Linux), or `libsteam_api.dylib` (macOS), and must exist as a regular file.
2. Before loading anything: set `SteamAppId` and `SteamGameId` to the app id. Duplicate the
   original stdout for protocol output and point the process stdout (CRT descriptor and Win32 std
   handle) at stderr, because the library logs to stdout. On Windows, also disable system error
   dialogs (`SetErrorMode`).
3. Load the library: `LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)` on Windows,
   `dlopen(RTLD_NOW | RTLD_LOCAL)` elsewhere.
4. Resolve flat exports. Function signatures are written by hand from the public documentation:
   the interface pointer is the first argument, and the calling convention is `__cdecl` on Windows.
   Interface pointers must match the vtable layout the library's flat functions were compiled for,
   so they come from:
   - the library's own versioned accessor, probing from the highest version down:
     `SteamAPI_SteamUGC_v021`…`v010`, `SteamAPI_SteamUtils_v012`…`v007`,
     `SteamAPI_SteamUser_v025`…`v017`, `SteamAPI_SteamApps_v010`…`v006`;
   - or, for libraries without accessors, `SteamInternal_FindOrCreateUserInterface(
     SteamAPI_GetHSteamUser(), "<VERSION>")`, but only with a version string that appears
     exactly once in the library image. A guessed version could make a flat call land on an
     unrelated vtable slot (for example `UnsubscribeItem`), so guessing is refused and reported as
     `symbols_missing`.
5. Initialise with `SteamAPI_InitFlat(SteamErrMsg*)` (SDK 1.58+), else `SteamAPI_InitSafe()`,
   else `SteamAPI_Init()`. A failed init is classified with `SteamAPI_IsSteamRunning`. After init
   the helper checks that `ISteamUtils::GetAppID` equals the requested app, which catches a stray
   `steam_appid.txt`.
6. Main loop: pump `SteamAPI_RunCallbacks` every 50 ms (sooner when a command arrives). A reader
   thread performs raw OS reads from stdin into a queue, so the pump never blocks on input.
   Pending subscribe calls are polled with `IsAPICallCompleted` and read with `GetAPICallResult`
   (callback 1313, `RemoteStorageSubscribePublishedFileResult_t {EResult; uint64}`: 16 bytes with
   Windows 8-byte callback packing, 12 bytes with 4-byte packing on Linux/macOS). If a result
   doesn't arrive within 120 s, it is reported as EResult 16 (Timeout).
7. On `quit` or end of input, the helper calls `SteamAPI_Shutdown` and exits 0.

Stellaris 4.5.2 ships an SDK build earlier than 1.58: it has no `SteamAPI_InitFlat`, and it
exports `SteamAPI_SteamUGC_v016`. The helper therefore used `SteamAPI_InitSafe` with
`STEAMUGC_INTERFACE_VERSION016`, which is also what the probe recorded.

## 2. Helper protocol (version 1)

UTF-8 JSON, one object per line, `\n` terminated, at most 256 KiB per line. The helper prints a
`ready` event first and then answers commands in order. Subscribe results arrive asynchronously.
Encoding and decoding live in `providers/helper_protocol.hpp`, shared by both sides.

### Commands (application to helper)

| Command | Reply |
| --- | --- |
| `{"protocol":1,"cmd":"ping"}` | `{"event":"pong"}` |
| `{"protocol":1,"cmd":"state","ids":["703156866",...]}` (1 to 1000 ids) | one `state` event per id, in order |
| `{"protocol":1,"cmd":"subscribe","id":"..."}` | later: `subscribe_result` |
| `{"protocol":1,"cmd":"download","id":"...","highPriority":true}` | `download_requested` |
| `{"protocol":1,"cmd":"quit"}` | none; the helper shuts Steam down and exits 0 |

`protocol` may be omitted, which is convenient for manual testing. Any other value is rejected.
Unknown fields are ignored. Unknown commands, non-canonical ids (`""`, `"0"`, `"012"`, signs,
spaces, more than `2^64-1`) and oversized lines get an `error` event, and the helper keeps running.

### Events (helper to application)

```json
{"event":"ready","protocol":1,"ok":true,"loggedOn":true,"ownsApp":true,"appId":"281990",
 "ugcInterface":"STEAMUGC_INTERFACE_VERSION016","init":"SteamAPI_InitSafe","subscribedCount":27}
{"event":"ready","protocol":1,"ok":false,"error":"steam_not_running","message":"..."}
{"event":"state","id":"703156866","flags":5,"subscribed":true,"installed":true,"downloading":false,
 "downloadPending":false,"needsUpdate":false,"legacy":false,"bytesDownloaded":0,"bytesTotal":0,
 "installPath":"...","sizeOnDisk":429416,"timestamp":1782489192}
{"event":"subscribe_result","id":"1121692237","ok":true,"result":1}
{"event":"download_requested","id":"1121692237","accepted":true}
{"event":"pong"}
{"event":"error","code":"invalid_id","message":"...","cmd":"subscribe"}
```

(Lines are wrapped here for readability; on the wire each event is one line.)

- In `state` events, `flags` (EItemState) is authoritative. The booleans are derived from it for
  readability. `installPath` is whatever Steam reports (UTF-8, native separators), or empty.
- If `ownsApp` cannot be queried, it reports whether init succeeded, because Steam only grants
  an API session for apps the account may run.
- `loggedOn:false` with `ok:true` is Steam's offline mode: state queries work, but network
  operations will not.
- `error` codes: `bad_request`, `invalid_id`, `unknown_command`, `unsupported_protocol`.

### Ready failures and exit codes

| `error` | Exit code | Meaning |
| --- | --- | --- |
| `invalid_arguments` | 2 | bad `--app-id`, unexpected library file name, unknown argument |
| `library_load_failed` | 3 | file missing, wrong architecture, not a library |
| `symbols_missing` | 4 | the library lacks required exports or a safe interface accessor |
| `steam_not_running` | 5 | Steam client not running (`InitFlat` NoSteamClient, or `IsSteamRunning` false) |
| `init_failed` | 6 | Steam refused the session (ownership, a stray `steam_appid.txt`, client still starting, ...) |
| `not_logged_on` | 7 | init succeeded but there is no user session (`GetHSteamUser() == 0`) |
| (none) | 8 | the protocol output pipe failed |

The helper writes human-readable diagnostics to stderr. These include the Steam library's own
logging, which can contain the user's SteamID64, so the app must treat stderr as sensitive and
redact it before exporting diagnostics.

Protocol versioning: the helper and the application ship together. A `ready` event with another
`protocol` value fails to decode with `UnsupportedVersion`, and the app should then treat the
provider as unavailable.

## 3. Acquisition state machine

`providers/acquisition.hpp` is a pure reducer:
`reduceAcquisition(context, observation, policy) -> {context', actions, persist, retryAfterMs}`.
It performs no I/O and never reads the clock. Time arrives as `Tick{nowMs}` (and as the `nowMs`
argument of `startAcquisition`). The persisted part of the context is the core
`AcquisitionJob`. The rest (what the job is waiting for, timers, session facts) is transient and
rebuilt by calling `startAcquisition` again after a restart.

```text
 startAcquisition (new job, or any non-terminal job loaded after a restart)
        |
        v   QuerySession
  [await Session] --ProviderUnavailable--> NeedsUserAction / Failed(HelperFailed, retryable)
        | ProviderReady{loggedOn, ownsApp}
        v   QueryState
  [await State] --ItemState--> decide:
        |  installed & current & subscribed & path ---------------------> VerifyContent(path)
        |  not subscribed (needs loggedOn & ownsApp) --> Subscribing --> [await SubscribeResult] (*)
        |  subscribed, downloading/pending ---------------------------> [Progress]
        |  subscribed, not installed / needs update (needs online) ---> [await DownloadAccepted] (*)
        |
        |  (*) the Subscribe / RequestDownload action first passes the identity gate (below)
        |
  [await SubscribeResult] --OK--> Subscribed, QueryState --> [await State]
        |                  --error--> Unavailable | NeedsUserAction | Failed (see section 4)
  [await DownloadAccepted] --true--> DownloadRequested --> [Progress]
        |                   --false--> Failed (retryable)
  [Progress]  Tick: QueryState every pollInterval; no byte/flag change for stallTimeout -> Failed
        |     ItemState downloading -> Downloading (bytes recorded), pending -> DownloadRequested
        |     ItemState not subscribed and not downloading -> NeedsUserAction
        |     ItemState installed & current & path -> VerifyContent(path)
  [await Verification] --ContentVerified(true)--> Installed
                       --ContentVerified(false)--> Failed (validation_failed, no auto retry)

 Any non-terminal step: Cancel -> Cancelled (message says what remains in Steam)
                        response timeout -> Failed (retryable)
                        ProviderReady (helper restarted) -> QueryState
 Failed + retry scheduled: Tick >= retryAt -> attempts+1, Pending, QueryState
 Failed / NeedsUserAction / Unavailable / Cancelled: Retry -> attempts=1, Pending, query again
 Failed / NeedsUserAction / Unavailable + ItemState (rescan): subscribed & installed -> verify;
                                                              subscribed & downloading -> Progress
 ManualFallback -> OpenFallbackPage, NeedsUserAction ("subscribe there, then rescan")
 ItemRejected (Web API: not found / banned / wrong game) -> Unavailable
```

Invariants, all enforced by tests:

- `Installed` is reached only through `ContentVerified(ok)` for the path Steam reported.
  `Subscribed` and `DownloadRequested` never imply readiness.
- Subscribe, RequestDownload and VerifyContent are decided only from a fresh `ItemState`. So
  items that are already installed are verified instead of downloaded again, and a restored job
  is reconciled instead of being subscribed again blindly.
- A job is terminal (core `isTerminal`) exactly when it is waiting for nothing. Each transition
  emits at most one action.
- The machine never asks to unsubscribe or delete anything.

Default policy (`AcquisitionPolicy`): 3 automatic runs per retry cycle; backoff 5 s, doubling, capped
at 120 s; 30 s response timeout (60 s for subscribe); 5 min stall timeout; state polling every 1 s
while downloading; high-priority downloads. A user `Retry` always starts a fresh cycle.

### Identity gate (services)

Share codes are untrusted input, and Subscribe and RequestDownload act on the user's Steam
account. So when a Web API metadata host is configured (the app sets one up whenever Workshop
acquisition is enabled),
`services::AcquisitionCoordinator` sends these two actions only for items the Web API has
confirmed: the item exists, is not banned, and its `consumer_app_id` is the game's app id
(`validateWorkshopItemForGame()`, section 7). The reducer above stays pure; the gate sits between
its transitions and the helper.

```text
 job starts or resumes --> Web API request for its item (batched per game, before any session opens)

 reducer emits Subscribe or RequestDownload:
        |-- item confirmed --------------------> action goes to the helper as usual
        |-- item rejected ---------------------> ItemRejected -> Unavailable
        |                                         (item_unavailable / wrong_game)
        '-- no verdict yet --> [held] the transition is dropped; Steam timeouts do not run
                 |-- verdict: confirmed ----------> fresh run: startAcquisition -> QueryState ->
                 |                                  decide again from a current ItemState
                 |-- verdict: rejected -----------> ItemRejected -> Unavailable
                 '-- no verdict within identityTimeoutMs (15 s) -->
                                                    NeedsUserAction (timeout): "could not confirm
                                                    ... so it did not subscribe to it"
```

- QuerySession, QueryState and the verification of content Steam already installed are never
  held, so installed items still reach `Installed` while the Web API is unreachable.
- A rejection stops every job of that item at whatever step it is in, not only held ones.
- Verdicts are kept per item for the session. `Retry` (and "retry all") asks the Web API again
  unless the item was already confirmed, because a rejection may no longer hold and an unanswered
  request may have been lost.
- Without a metadata host (some tests), nothing is held.

### Driver responsibilities (application layer)

- Keep one helper per game while jobs for that game are active, and send `quit` when idle.
  Answer `QuerySession` from the cached `ready` event, starting the helper if needed. When the
  helper exits unexpectedly, feed `ProviderUnavailable{HelperFailed}` to its jobs.
- Map helper events with `observationFromHelperEvent()` and actions with `helperCommandFor()`.
  Batch `QueryState` actions from several jobs into one `state` command.
- Hold Subscribe and RequestDownload behind the identity gate (above).
- Run `verifyWorkshopContent()` (a non-empty `descriptor.mod` in the folder, or a non-empty
  single file for legacy items) on a worker thread, then feed `ContentVerified`.
- Feed `Tick` about once per second. Save `context.job` when `persist` is set; progress-only
  changes may be coalesced.
- On start-up, call `startAcquisition()` for every stored non-terminal job.

## 4. EResult mapping

`classifySteamResult()` in `providers/workshop_item.cpp`. "Retryable" errors become `Failed`
with automatic retries (capped). "User action" errors become `NeedsUserAction`. "Unavailable"
errors become `Unavailable`. Values not in the table are treated as retryable provider errors.

| EResult | Name | ErrorCode | Disposition |
| --- | --- | --- | --- |
| 1 | OK | none | success |
| 2 | Fail | provider_unavailable | retryable |
| 3 | NoConnection | offline | retryable |
| 8 | InvalidParam | item_unavailable | unavailable |
| 9 | FileNotFound | item_unavailable | unavailable (deleted, or hidden from this account) |
| 10 | Busy | busy | retryable |
| 11 | InvalidState | provider_unavailable | retryable |
| 15 | AccessDenied | access_denied | unavailable (private / friends-only) |
| 16 | Timeout | timeout | retryable |
| 17 | Banned | access_denied | unavailable |
| 20 | ServiceUnavailable | provider_unavailable | retryable |
| 21 | NotLoggedOn | provider_unavailable | user action |
| 22 | Pending | busy | retryable |
| 24 | InsufficientPrivilege | access_denied | user action (limited account, missing licence) |
| 25 | LimitExceeded | limit_exceeded | user action (for example too many subscriptions) |
| 26 | Revoked | item_unavailable | unavailable |
| 27 | Expired | item_unavailable | unavailable |
| 29 | DuplicateRequest | busy | retryable |
| 35 | ConnectFailed | offline | retryable |
| 37 | IOFailure | io_error | retryable |
| 38 | RemoteDisconnect | offline | retryable |
| 40 | Blocked | access_denied | unavailable |
| 44 | ServiceReadOnly | provider_unavailable | retryable |
| 48 | TryAnotherCM | offline | retryable |
| 52 | Cancelled | cancelled | retryable |
| 53 | DataCorruption | integrity_check_failed | retryable |
| 54 | DiskFull | io_error | user action |
| 55 | RemoteCallFailed | provider_unavailable | retryable |
| 69 | ParentalControlRestricted | access_denied | user action |
| 78 | ValueOutOfRange | item_unavailable | unavailable |
| 79 | UnexpectedError | provider_unavailable | retryable |
| 80 | Disabled | provider_unavailable | user action |
| 83 | RegionLocked | access_denied | unavailable |
| 84 | RateLimitExceeded | busy | retryable |
| 86 | ItemDeleted | item_unavailable | unavailable |
| 104 | InvalidItemType | item_unavailable | unavailable |
| 128 | NotSupported | not_implemented | unavailable |

When the helper cannot obtain a call result, it reports 2 (Fail). An invalid call handle is also
reported as 2. A result that takes longer than 120 s is reported as 16 (Timeout).

## 5. Failure handling

| Situation | Job state | What the user is told / can do |
| --- | --- | --- |
| Steam not running | NeedsUserAction | "Start Steam, sign in, and retry." |
| Not signed in / offline mode (needs network) | NeedsUserAction | go online in Steam, retry. Installed content is still accepted offline |
| Account does not own the game | NeedsUserAction (access_denied) | sign in with the owning account |
| Steam refused the session | NeedsUserAction | check account, game ownership, Steam start-up |
| Library missing / not loadable | NeedsUserAction | verify the game files in Steam, or use the manual fallback |
| Library lacks the API (`symbols_missing`) | NeedsUserAction | manual fallback, then rescan |
| Helper crash / no answer | Failed, retried automatically | after 3 runs: Retry or manual fallback |
| Deleted / private / banned / invalid / wrong game (Steam or the Web API says so) | Unavailable | entry stays in the playset, visibly unresolved |
| Web API gives no verdict within 15 s (offline, blocked) | NeedsUserAction (timeout) | nothing was subscribed; Retry asks the Web API again, or use the manual fallback |
| Offline / timeouts / Steam busy | Failed, retried with backoff | after 3 runs: "Automatic retries are exhausted" |
| Download stalls (no progress for 5 min) | Failed (timeout) | check Steam's Downloads page (paused, disk space), retry |
| Installed content unusable (no `descriptor.mod`) | Failed (validation_failed) | verify files in Steam or resubscribe there, retry |
| Unsubscribed in Steam during the download | NeedsUserAction | retry to subscribe again |
| Cancel | Cancelled | the message says whether a subscription or download already happened and remains in Steam |
| Application restarted mid-job | resumes in the stored state, re-queries Steam first | no blind re-subscribe |

## 6. Manual fallback (limited capability)

`providers/fallback.hpp` builds `steam://url/CommunityFilePage/<id>` (opens the item page inside
the Steam client) and `https://steamcommunity.com/sharedfiles/filedetails/?id=<id>` (browser),
only from validated canonical ids. The UI label is "Open in Steam (manual - subscribe there, then
rescan)". After the user subscribes in Steam, a rescan (`QueryState`, or inventory scanning)
picks the item up. The state machine accepts a subscribed and installed item from any
idle failure state, and tracks a download that Steam is already running.

**The fallback does not satisfy the spec's automatic-download requirement.** It exists for
cases where the helper cannot work (incompatible library, platform without a helper) or the user
prefers it.

## 7. Web API metadata

`providers/web_metadata.hpp` builds the form body for
`POST https://api.steampowered.com/ISteamRemoteStorage/GetPublishedFileDetails/v1/` (key-less;
`itemcount=N&publishedfileids%5B0%5D=...`, at most 100 ids) and parses the reply into
`WorkshopItemMetadata`. The reply observed on 2026-10-29 has `file_size` as a string and other
numbers as JSON numbers. Hidden or missing items come back as `{"publishedfileid":"…","result":9}`.
The parser accepts both numbers and numeric strings. `validateWorkshopItemForGame()` returns
`ItemUnavailable` for result 9, other non-OK results and banned items, and `WrongGame` when
`consumer_app_id` differs from the game's app id. Feed failures to the job as `ItemRejected`.
The services coordinator does this and uses the verdicts as the identity gate (section 3).

## 8. Local mod fingerprints

`fingerprintDirectory()` (`providers/local_fingerprint.hpp`) produces `sha256:<hex>` over the
files sorted by '/'-separated UTF-8 relative path. Each file contributes
`path \0 size \0 sha256(file) \n`. `.git/`, `__MACOSX/`, `.DS_Store`, `Thumbs.db`,
`desktop.ini` and `._*` are skipped. The function supports cancellation and size/file-count
limits. The definition is pinned by a golden value computed with an independent Python
implementation. File names are not Unicode-normalised: a copy whose names were converted to NFD
by an old macOS filesystem fingerprints differently. Local mods are never downloaded
automatically; the fingerprint is an informational match hint.

## 9. Terms of service and licensing considerations

Not legal advice. These points need review before release.

- **No Valve code is distributed.** The repository contains no Steamworks SDK files. The flat API
  declarations are minimal, written from public documentation. The helper loads the
  `steam_api` library that the user's own game installation contains. Packages must never
  include a Steam binary (`.gitignore` blocks `steam_api*.dll` and `libsteam_api.*`).
- **The game's library is used as shipped.** It is not modified, copied or patched, and no game
  binary is patched (spec section 7).
- **The user's own session.** The helper uses the Steam client the user is already signed in to.
  It never sees or stores credentials, and it only performs actions the user could take in the
  Steam client for a game they own (view state, subscribe, download).
- **Acting in the game's app context.** Steam's agreements do not explicitly address a
  third-party tool initialising the API with a game's app id. The tool is disclosed and
  user-initiated, but this remains a policy risk. Valve could restrict it, and a game could
  switch to an incompatible library. The manual fallback stays available.
- **Visible side effects to disclose in the UI:** while the helper runs, Steam treats the game as
  running. `LastPlayed` was observed to change, playtime may accrue, and friends may see "Playing
  Stellaris". Some games refuse to start while their app is already "running". Keep sessions
  short and quit the helper before launching the game.

## 10. Platform notes

- **Windows:** the 64-bit helper loads `steam_api64.dll` from the game root (verified for
  Stellaris). A 32-bit `steam_api.dll` would need a 32-bit helper build; none of the target games
  needs it.
- **Linux (unverified):** native builds ship `libsteam_api.so` (expected in the game root). It
  connects to the running client through `~/.steam/sdk64/steamclient.so`. The helper must match
  the library's architecture (x86-64). Flatpak or Snap Steam may need the helper to run where the
  client's IPC is reachable. Games run through Proton ship a Windows DLL that a native helper
  cannot load, so that setup is unsupported and should fall back to the manual path.
- **macOS (unverified):** look for `libsteam_api.dylib` inside the app bundle (`Contents/MacOS`
  or `Contents/Frameworks`). A notarised helper with hardened runtime needs the
  `com.apple.security.cs.disable-library-validation` entitlement to load a library signed by
  another team. Callback structs use 4-byte packing (12-byte subscribe result) on both Linux and
  macOS.
- POSIX branches (`dlopen`, `dup2`, `posix_spawn` in tests) compile only on those systems. On
  Linux they were built and tested with GCC 16.2 in WSL (no real Steam client there); macOS has
  not been built. CI on Linux and macOS builds `pmm-steam-helper` and runs
  `pmm_providers_tests`, which include the end-to-end helper tests against the fake library.

## 11. Not verified yet, and how to verify it

1. **Live subscribe and download** (requires the user's explicit approval, because it changes
   their subscriptions). Suggested procedure: pick a small public Stellaris item that is not
   subscribed (check with `state` first, expect `flags:0`). Send `subscribe` and expect
   `subscribe_result ok:true result:1` within a few seconds, which validates callback 1313 and the
   16-byte struct against the real client. Then send `state` and expect `subscribed:true`. Send
   `download` with `highPriority:true` and expect `accepted:true`. Poll `state` until
   `installed:true` with an install path that contains `descriptor.mod`. Record the transcript in
   `docs/evidence/`. Afterwards, the user may unsubscribe manually in Steam; the application never
   does.
2. Behaviour with the game running at the same time, in Steam offline mode, and with Steam not
   running (the `ready` failure path against the real client).
3. EU4 (app 236850): repeat the read-only probe (`ping`, `state`) on the development machine,
   where it is installed, with the user's approval.
4. Linux and macOS: compile and run the unit and end-to-end tests on macOS (Linux done in WSL),
   then repeat the read-only probe with the native library on machines with the games.
