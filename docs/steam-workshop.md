# Steam Workshop acquisition

This document describes how Paradox Mod Manager acquires missing Steam Workshop mods
(spec section 6), and updates or downloads again installed ones when the user asks: the
per-game helper process, its wire protocol, the acquisition state machine, error mapping, the
manual fallback, the update check, and what is and is not verified.

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
| Updates of installed items: "Check for mod updates" (the Web API's `time_updated` against the acf's `timeupdated`), Steam's own record (`latest_manifest`), "Update mods" / "Update from Workshop" (`DownloadItem` on an installed item, done when the install time reaches the Workshop's) and `update_not_seen` | Implemented (2026-11-22), tested with the fake Steam library, the services fakes and the local Web API stub; **not verified live** (section 11, item 4) |
| "Re-download from scratch…" (`UnsubscribeItem` and call result 1315, then subscribe and download again) | Implemented (2026-11-22), tested with the fake Steam library and a hand-written transcript (`helper_transcript_update.jsonl`); **not verified live**: whether Steam removes the files while the helper session runs, and whether it downloads anything when they stay (section 11, item 4) |
| Manual fallback (open Workshop page, then rescan) | Implemented. **Limited**: does not satisfy automatic download |
| Web API metadata (names, sizes, wrong-game check) | Request builder and parser in `providers`; the app layer's `QtWorkshopMetadataHost` makes the call (tested against a local stub) |
| Identity gate (Subscribe/RequestDownload only for items the Web API confirmed) | Implemented in the services coordinator, unit tested (section 3, "Identity gate") |
| Europa Universalis IV | **Verified live** 2026-10-30 with EU4 1.37.5: the helper session in EU4's app context, with the `steam_api64.dll` from the game root, and subscribe and download (see the "Subscribe" row and the [evidence](evidence/live-validation-2026-10-30.md)) |
| Crusader Kings III | Implemented, **unverified**: the same mechanism, with the Steam API library from `<install>/binaries`, where CK3 keeps it next to `ck3.exe`; the helper has not been run for app 1158310 |
| Hearts of Iron IV | Implemented, **unverified**: the same mechanism, with the Steam API library from the installation folder, as for Stellaris; the helper has not been run for app 394360. The check that ends a download accepts the 23 items installed on the development machine (read-only, 2026-11-21) |
| Victoria 3 | Implemented, **unverified**: the same mechanism; the helper has not been run for app 529340. Its Workshop items have no `descriptor.mod`: the check that ends a download wants a `.metadata/metadata.json` that is a JSON object, and accepts the 9 items installed on the development machine (read-only, 2026-11-20) |

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
- **Small, safe surface.** The protocol can query state, subscribe, request a download, and
  unsubscribe one item. There is no command to delete anything or to pass paths or URLs, and
  every id is validated as a canonical decimal `uint64`. Unsubscribe exists only for the
  re-download from scratch that the user asks for per mod (section 3); Steam then removes the
  item's files itself.

### How the helper binds to the game's library

1. Validate arguments: `--app-id` must be canonical decimal digits (fits in `uint32`).
   `--steam-api` must name exactly `steam_api64.dll` or `steam_api.dll`, and must exist as a
   regular file.
2. Before loading anything: set `SteamAppId` and `SteamGameId` to the app id. Duplicate the
   original stdout for protocol output and point the process stdout (CRT descriptor and Win32 std
   handle) at stderr, because the library logs to stdout. Also disable system error
   dialogs (`SetErrorMode`).
3. Load the library: `LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)`.
4. Resolve flat exports. Function signatures are written by hand from the public documentation:
   the interface pointer is the first argument, and the calling convention is `__cdecl`.
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
   Pending subscribe and unsubscribe calls are polled with `IsAPICallCompleted` and read with
   `GetAPICallResult`: callback 1313, `RemoteStorageSubscribePublishedFileResult_t`, and
   callback 1315, `RemoteStorageUnsubscribePublishedFileResult_t`, both `{EResult; uint64}`:
   16 bytes with 8-byte callback packing. If a result doesn't arrive within 120 s, it is
   reported as EResult 16 (Timeout). `SteamAPI_ISteamUGC_UnsubscribeItem` is resolved but not
   required: with a library that lacks it, `unsubscribe` answers `ok:false, result:2` and
   everything else works.
7. On `quit` or end of input, the helper calls `SteamAPI_Shutdown` and exits 0.

Stellaris 4.5.2 ships an SDK build earlier than 1.58: it has no `SteamAPI_InitFlat`, and it
exports `SteamAPI_SteamUGC_v016`. The helper therefore used `SteamAPI_InitSafe` with
`STEAMUGC_INTERFACE_VERSION016`, which is also what the probe recorded.

## 2. Helper protocol (version 1)

UTF-8 JSON, one object per line, `\n` terminated, at most 256 KiB per line. The helper prints a
`ready` event first and then answers commands in order. Subscribe and unsubscribe results arrive
asynchronously. Encoding and decoding live in `providers/helper_protocol.hpp`, shared by both
sides.

### Commands (application to helper)

| Command | Reply |
| --- | --- |
| `{"protocol":1,"cmd":"ping"}` | `{"event":"pong"}` |
| `{"protocol":1,"cmd":"state","ids":["1200000019",...]}` (1 to 1000 ids) | one `state` event per id, in order |
| `{"protocol":1,"cmd":"subscribe","id":"..."}` | later: `subscribe_result` |
| `{"protocol":1,"cmd":"download","id":"...","highPriority":true}` | `download_requested` |
| `{"protocol":1,"cmd":"unsubscribe","id":"..."}` | later: `unsubscribe_result` |
| `{"protocol":1,"cmd":"quit"}` | none; the helper shuts Steam down and exits 0 |

`download` passes the id straight to `DownloadItem`, also for an installed item: an update job
uses it to ask for a newer version. `unsubscribe` calls `UnsubscribeItem` (Steamworks: "This
will result in the item being removed after the game quits"; the helper session counts as the
game). Only a reinstall job sends it (section 3).

`protocol` may be omitted, which is convenient for manual testing. Any other value is rejected.
Unknown fields are ignored. Unknown commands, non-canonical ids (`""`, `"0"`, `"012"`, signs,
spaces, more than `2^64-1`) and oversized lines get an `error` event, and the helper keeps running.

### Events (helper to application)

```json
{"event":"ready","protocol":1,"ok":true,"loggedOn":true,"ownsApp":true,"appId":"281990",
 "ugcInterface":"STEAMUGC_INTERFACE_VERSION016","init":"SteamAPI_InitSafe","subscribedCount":27}
{"event":"ready","protocol":1,"ok":false,"error":"steam_not_running","message":"..."}
{"event":"state","id":"1200000019","flags":5,"subscribed":true,"installed":true,"downloading":false,
 "downloadPending":false,"needsUpdate":false,"legacy":false,"bytesDownloaded":0,"bytesTotal":0,
 "installPath":"...","sizeOnDisk":300000,"timestamp":1708000000}
{"event":"subscribe_result","id":"1200000023","ok":true,"result":1}
{"event":"download_requested","id":"1200000023","accepted":true}
{"event":"unsubscribe_result","id":"1200000019","ok":true,"result":1}
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
provider as unavailable. `unsubscribe` and `unsubscribe_result` were added to version 1 without
a version change (0.3.1): they are additive, and a helper without them answers `unsubscribe`
with an `unknown_command` error, which ends the job with a response timeout instead of
unsubscribing. `tests/fixtures/providers/helper_transcript_update.jsonl` is a hand-written
transcript of an update and a re-download (none has been recorded live yet).

## 3. Acquisition state machine

`providers/acquisition.hpp` is a pure reducer:
`reduceAcquisition(context, observation, policy) -> {context', actions, persist, retryAfterMs}`.
It performs no I/O and never reads the clock. Time arrives as `Tick{nowMs}` (and as the `nowMs`
argument of `startAcquisition`). The persisted part of the context is the core
`AcquisitionJob`. The rest (what the job is waiting for, timers, session facts) is transient and
rebuilt by calling `startAcquisition` again after a restart.

A job has a kind (`AcquisitionKind`, stored since schema version 3):

- **acquire**: a missing mod. The diagram below.
- **update**: an installed, subscribed mod with a newer version on the Workshop (found by the
  update check or flagged in Steam's `appworkshop_<app>.acf`). It carries a target: the
  Workshop's `time_updated` (and `hcontent_file`, recorded only), or the acf's
  `latest_timeupdated` / `latest_manifest`.
- **reinstall**: "Re-download from scratch", which the user asks for per mod after a
  confirmation. Its `ReinstallStep` (start, unsubscribed, resubscribed) is stored, so that a
  retry or a restart carries on instead of unsubscribing twice or leaving the item unsubscribed.

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

Update and reinstall jobs:

```text
 update: [await State] --ItemState--> decide:
        |  not subscribed ------------------------------> NeedsUserAction ("not subscribed in
        |                                                  Steam"; an update never subscribes)
        |  downloading / pending -----------------------> [Progress]
        |  installed & current & target reached --------> VerifyContent(path)
        |  otherwise (needs online) --------------------> RequestDownload (*), also when Steam
        |                                                  calls the installed version current
  [Progress] ItemState installed & current:
        |     install timestamp >= target (no target: after this run's request) -> VerifyContent
        |     still older -> keep polling; updateRecheckMs (30 s) after the accepted request:
        |                    RequestDownload once more, then Failed (update_not_seen, no
        |                    automatic retry): "Steam has not picked up this update yet. ..."
        |  ItemState not subscribed -> NeedsUserAction

 reinstall: [await State] --ItemState--> decide by ReinstallStep:
        |  start, subscribed (needs online) ---> Unsubscribing --> [await UnsubscribeResult] (*)
        |  start, not subscribed --------------> step unsubscribed, as below
        |  unsubscribed -----------------------> Subscribing --> [await SubscribeResult] (*)
        |  resubscribed -----------------------> as an update (download even when current);
        |                                         its target, when it has one, as an update's
  [await UnsubscribeResult] --OK--> step unsubscribed (stored), [await Removal]: QueryState
        |                             every pollInterval until Steam no longer reports the
        |                             subscription, then up to removalGraceMs (10 s) for the
        |                             files to go, then Subscribe; unsubscribeTimeoutMs (60 s)
        |                             without that -> Failed
        |                   --error--> as a subscribe error; nothing changed in Steam
  [await SubscribeResult] --OK--> step resubscribed (stored), QueryState
  Every failure in step unsubscribed adds: "Workshop item <id> is now unsubscribed in Steam:
  Retry subscribes to it again and downloads it." Retry (and the automatic retries) subscribe
  again; they never unsubscribe a second time.

  (*) through the identity gate (below)
```

Invariants, all enforced by tests:

- `Installed` is reached only through `ContentVerified(ok)` for the path Steam reported.
  `Subscribed` and `DownloadRequested` never imply readiness.
- Subscribe, RequestDownload, Unsubscribe and VerifyContent are decided only from a fresh
  `ItemState`; the one exception is an update's second download request, which the clock
  decides. So for an acquire job, items that are already installed are verified instead of
  downloaded again (installed content is downloaded again only on an explicit update or
  re-download), and a restored job is reconciled instead of being subscribed again blindly.
- A job is terminal (core `isTerminal`) exactly when it is waiting for nothing. Each transition
  emits at most one action.
- The machine never deletes anything. Only a reinstall job unsubscribes, only in step start, and
  an update job never subscribes.

Default policy (`AcquisitionPolicy`): 3 automatic runs per retry cycle; backoff 5 s, doubling, capped
at 120 s; 30 s response timeout (60 s for subscribe and unsubscribe); 5 min stall timeout; state
polling every 1 s while downloading; high-priority downloads; an update asks once more after
30 s; a re-download waits up to 10 s for Steam to remove the files. A user `Retry` always starts
a fresh cycle.

What the services and the readiness rules make of the kinds:

- An unfinished update or reinstall job of an installed mod makes its playset entry Pending or
  Downloading, so Play waits; the launch check also waits while an update or re-download of a
  disabled mod (or of a mod of another playset) of the same game runs. A failed one leaves the
  entry Ready with a warning ("Update failed: ...").
- `AcquisitionCoordinator::acquireUpdates()`, `reinstall()` and `reinstallJob()`, and a Retry of
  an update or reinstall job, are refused (`ConflictingProcess`) while the game's process runs
  (the launch check's process list); `start()` leaves stored update and reinstall jobs parked
  meanwhile. A download Steam already accepted before an outside launch cannot be stopped.
- The Downloads list keeps failed and running update and reinstall jobs, also when the mod is
  installed and usable; a failed update offers "Re-download from scratch" (`canReinstall`).

### Identity gate (services)

Share codes are untrusted input, and Subscribe, RequestDownload and Unsubscribe act on the
user's Steam account. So when a Web API metadata host is configured (the app sets one up
whenever Workshop acquisition is enabled),
`services::AcquisitionCoordinator` sends these actions only for items the Web API has
confirmed: the item exists, is not banned, and its `consumer_app_id` is the game's app id
(`validateWorkshopItemForGame()`, section 7). The reducer above stays pure; the gate sits between
its transitions and the helper. One exception: the download of an update that Steam's own
`appworkshop_<app>.acf` flagged for an installed item passes without a verdict (an update never
subscribes, so nothing new reaches the account), and works while the Web API is unreachable. A
re-download needs a metadata host at all (`reinstall()` refuses without one): the Web API must
confirm the item still exists before it is unsubscribed.

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
                                                    ... so it did not subscribe to it" (update:
                                                    "did not ask Steam to download the update";
                                                    reinstall: "did not unsubscribe or download
                                                    it again", or, after the unsubscribe, "did
                                                    not subscribe to it again" with the note
                                                    that the item is now unsubscribed)
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
- Hold Subscribe, RequestDownload and Unsubscribe behind the identity gate (above).
- Run the game adapter's `verifyWorkshopContent()` on a worker thread, then feed
  `ContentVerified`. Each adapter family applies the rule of its own scan (since 2026-11-20;
  [ADR 0010](adr/0010-content-load-adapter-family.md)): for Stellaris, EU4 and CK3 a non-empty
  `descriptor.mod` in the folder, else exactly one other `.mod` file there, non-empty, and no
  `.zip` (`games::contentDescriptorFile()`; some items ship `<Name>.mod`); for Victoria 3 a
  `.metadata/metadata.json` that is a JSON object, as the scan reads it (anything else makes
  the scan list the mod as Broken). For every game a single file (a legacy item) must not be
  empty.
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
| Installed content unusable (no `descriptor.mod` and not exactly one other `.mod` file; Victoria 3: no `.metadata/metadata.json`) | Failed (validation_failed) | verify files in Steam or resubscribe there, retry |
| Unsubscribed in Steam during the download | NeedsUserAction | retry to subscribe again |
| Update: Steam does not fetch the newer version (it reports the item current, also after a second request 30 s later) | Failed (`update_not_seen`), no automatic retry | "Steam has not picked up this update yet. Steam checks for updates about every 45 minutes; restarting Steam makes it check right away." **Retry**, or **Re-download from scratch…** on the job. Nothing is deleted; the installed version stays and the mod stays Ready, with a warning |
| Update of an item that is not subscribed in Steam | NeedsUserAction | "Workshop item … is not subscribed in Steam, so Steam does not update it. Subscribe to it on its Workshop page, or use Re-download from scratch, then retry." An update never subscribes |
| Update or re-download while the game runs (the launch check's process list) | refused, no job (`ConflictingProcess`) | "Stellaris is running (…). Close it before updating mods: Steam would change the files of mods the game has loaded." Also for Retry of such a job; Play waits while one runs |
| Re-download fails after the unsubscribe (subscribe refused, timeout, cancelled) | Failed / NeedsUserAction / Cancelled | the message ends "Workshop item … is now unsubscribed in Steam: Retry subscribes to it again and downloads it." The mod stays in the playsets, missing until then |
| Re-download without the Web API (no metadata host, or no verdict within 15 s) | refused / NeedsUserAction (timeout) | nothing is unsubscribed: the Web API must confirm first that the item still exists |
| The update check finds the item removed, private, banned or another game's | no update for it | a warning on the row: the installed copy stays as it is; neither Update nor Re-download touches it |
| The update check cannot reach the Workshop (offline, blocked) | (no job) | the result line says so, with **Retry**; Steam's own record still counts |
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
cases where the helper cannot work (incompatible library) or the user prefers it.

## 7. Web API metadata

`providers/web_metadata.hpp` builds the form body for
`POST https://api.steampowered.com/ISteamRemoteStorage/GetPublishedFileDetails/v1/` (key-less;
`itemcount=N&publishedfileids%5B0%5D=...`, at most 100 ids) and parses the reply into
`WorkshopItemMetadata`. The reply observed on 2026-10-29 has `file_size` as a string and other
numbers as JSON numbers. Hidden or missing items come back as `{"publishedfileid":"…","result":9}`.
The parser accepts both numbers and numeric strings. `classifyWorkshopItemForGame()` gives the
verdict (not found, no details, banned, another game, ok), and `validateWorkshopItemForGame()`
builds the error from it: `ItemUnavailable` for result 9, other non-OK results and banned items,
and `WrongGame` when `consumer_app_id` differs from the game's app id. Feed failures to the job as
`ItemRejected`. The services coordinator does this and uses the verdicts as the identity gate
(section 3).

The import preview asks too, for the Workshop mods of a share code or a Workshop ID list that are
not installed: their titles replace the text's name hints, and not-found, banned and other
games' items are marked before anything is saved
([app](app.md#workshop-names-in-the-import-preview)). Only the item ids are sent, with fixed
headers (`Accept-Language: en`, not the system's language), at most 1,000
per preview, never in sessions without Steam (`--no-steam`, `--read-only`). These answers go to
the preview only, never to the coordinator: a job's identity gate always rests on its own request.

**The update check** ("Check for mod updates", `ServiceRunner::checkWorkshopUpdates()`) asks,
only when the user presses it, about the installed Workshop mods of the active playset, enabled
and disabled: item ids only, in batches of 100, leaving out items answered within the last five
minutes (`CoordinatorOptions::updateCheckMaxAgeMs`). It first rescans the game's inventory,
which re-reads `appworkshop_<app>.acf`. The coordinator keeps the answers for the session
(`recordUpdateCheck()`; they also serve as verdicts for the identity gate) and compares them on
demand with the installed versions (`workshopFindings()`, `providers::compareWithWorkshop()`):

- an update is available when `time_updated` is later than the acf's `timeupdated` of the
  installed version (`InstalledMod::contentUpdatedAt` when `contentTimeFromSteam`);
- an item whose installed time is only the folder's modification time (no acf record) is not
  compared: folder times are local download times;
- `hcontent_file` is parsed (`WorkshopItemMetadata::contentManifest`, empty for `"0"`) and
  recorded with the acf `manifest` (`InstalledMod::workshopManifest`), but not compared until a
  live test confirms they name the same thing;
- not found, banned and other-game items are refused: the entry gets a warning that the
  installed copy stays, and no update.

Independently of the check, Steam's own record always counts, without any network use: an acf
`latest_manifest` that is not empty, not `"0"` and differs from `manifest` makes the mod
`InstallState::NeedsUpdate` and the entry "Update available". Nothing is checked automatically:
not at start, not on a playset change, not before Play.

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
  Steam client for a game they own (view state, subscribe, download, and unsubscribe for a
  re-download the user confirmed).
- **Acting in the game's app context.** Steam's agreements do not explicitly address a
  third-party tool initialising the API with a game's app id. The tool is disclosed and
  user-initiated, but this remains a policy risk. Valve could restrict it, and a game could
  switch to an incompatible library. The manual fallback stays available.
- **Visible side effects to disclose in the UI:** while the helper runs, Steam treats the game as
  running. `LastPlayed` was observed to change, playtime may accrue, and friends may see "Playing
  Stellaris". Some games refuse to start while their app is already "running". Keep sessions
  short and quit the helper before launching the game.

## 10. Platform notes

The helper runs on Windows only ([ADR 0009](adr/0009-windows-only.md)). The 64-bit helper loads
`steam_api64.dll` from the game root, `bin`, `binaries` or `win64`, the first found (the root
verified for Stellaris and EU4; Crusader Kings III keeps it in `binaries`, unverified). A
32-bit `steam_api.dll` would need a 32-bit helper build; none of the target games needs it. CI
builds `pmm-steam-helper` and runs `pmm_providers_tests`, which include the end-to-end helper
tests against the fake library.

## 11. Not verified yet, and how to verify it

1. **Live subscribe and download** (requires the user's explicit approval, because it changes
   their subscriptions). Suggested procedure: pick a small public Stellaris item that is not
   subscribed (check with `state` first, expect `flags:0`). Send `subscribe` and expect
   `subscribe_result ok:true result:1` within a few seconds, which validates callback 1313 and the
   16-byte struct against the real client. Then send `state` and expect `subscribed:true`. Send
   `download` with `highPriority:true` and expect `accepted:true`. Poll `state` until
   `installed:true` with an install path that contains `descriptor.mod`. Record the transcript in
   `docs/evidence/`. Afterwards, the user may unsubscribe manually in Steam; the application
   unsubscribes only for a "Re-download from scratch…" the user confirmed.
2. Behaviour with the game running at the same time, in Steam offline mode, and with Steam not
   running (the `ready` failure path against the real client).
3. EU4 (app 236850): repeat the read-only probe (`ping`, `state`) on the development machine,
   where it is installed, with the user's approval.
4. **Updates and re-downloads of installed items** (implemented 2026-11-22; a live run needs
   the owner's approval, because it downloads into and, for a re-download, unsubscribes from the
   owner's account). Open questions, from the design of 2026-11-21:
   - L1: does `DownloadItem` on an installed item that Steam flagged update it through the
     state machine, in a Paradox game? Every real scan so far found no pending update.
   - L2 (decisive): does `DownloadItem` fetch a version Steam has **not** noticed yet, or is it
     a no-op until Steam's next check (about every 45 minutes)? Without it, "Update mods" ends
     in `update_not_seen` for updates only the Web API knows about.
   - L3: is the Web API's `time_updated` the acf's `timeupdated` of the same version, and
     `hcontent_file` the acf's `manifest`? Only `time_updated` decides today.
   - L4: does an edit of only the title or description move `time_updated` or
     `hcontent_file` (a false "Update available")?
   - L5: how long after an author's update does the Web API report it?
   - L6: which flags does Steam report while it updates an installed item, and does the acf
     stay "installed" meanwhile?
   - L7: does a helper session (Steam shows the game as running) hold back Steam's own
     background update for that game?
   - L8: what does a download request do for an installed item that was removed, made private
     or banned?
   - L9: how long does a high-priority request for several items take, and how does it affect
     Steam's other downloads?
   - Re-download: does `UnsubscribeItem` answer with callback 1315 as the struct expects; does
     Steam remove the files while the helper session runs (Steamworks: "after the game quits"),
     and when they stay, does subscribing again download anything?

   Suggested, opportunistic: when "Check for mod updates" reports an item Steam has not
   flagged, press **Update from Workshop** for it and read Steam's `workshop_log.txt`
   (read-only); that answers L1, L2, L3 and L5. A re-download is best tried on a small item of
   the owner's choice. Record the results in `docs/evidence/` and the
   [support matrix](support-matrix.md).
