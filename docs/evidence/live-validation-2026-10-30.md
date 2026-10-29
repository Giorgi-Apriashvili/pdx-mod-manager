# Live validation, 2026-10-30

The live part of the acceptance procedure ([validation.md](../validation.md)):
direct launches with probe mods, a real Workshop download, restart
persistence and the failure cases. Run with the user's explicit approval on
the development machine. Every change to the real game folders was reverted
and checked afterwards (see [Clean-up](#clean-up)).

| | |
| --- | --- |
| OS | Windows 11 Enterprise 23H2 (10.0.22631), x64 |
| Distribution | Steam (client running and signed in) |
| Europa Universalis IV | `EU4 v1.37.5.0 Inca (491d)`, Steam build 15918133 |
| Stellaris | `Cygnus v4.5.2 (9776)`, Steam build 25664273 |
| Application | 0.1.0-dev debug build: commit `91b973e` for the EU4 acquisition and launch runs, `19e4705` (with the blocker fix below) for the Stellaris runs and the failure cases |
| Driver | `pmm-app --live-test` ([app.md](../app.md#hidden-verification-switches)): the real Backend, controllers and services, the same calls as the buttons; one isolated `--profile` per role |
| Observation | the games' own `logs/error.log` (probe mods, [ADR 0006](../adr/0006-probe-mod-verification.md)), `dlc_load.json`, the process list (`Win32_Process`), the Steam API item state |

Paths below: `<scratch>` is a temporary folder, `~` the user's home. The
SteamID printed by the Steam API library is omitted.

## Summary

| Step (spec §10) | EU4 | Stellaris |
| --- | --- | --- |
| 1 Detect installation and mods | pass (also [real scan](real-scan-2026-10-29.md)) | pass |
| 2 Playset with ≥ 3 mods, a disabled entry, non-default order | pass: 6 entries, 2 disabled, probes C before A, local and Workshop mixed | pass (same shape) |
| 3 Export, import into a clean profile | pass: profile A to profile B, same machine | pass (code imported into a clean profile) |
| 4 Acquire an initially missing Workshop mod | **pass**: Larger Monuments Interface (2471388429), not subscribed before; subscribe, download, verify through the app | **pass** (rerun the same night): UI Overhaul Dynamic - Extended Topbar (1780481482), not subscribed before |
| 5 Exact equality after import | pass: `share-code.py compare` equal, dumps byte-identical | pass: `compare` equal |
| 6 Launch without the Paradox launcher; enabled mods and order in-game | **pass**: no launcher process, same game process after 20 s (no restart through Steam), probes loaded / not loaded as configured. **Order: EU4 ignores the `enabled_mods` order for file overrides** (below) | **pass**: no launcher process; probes as configured; the later mod in `enabled_mods` wins a shared file |
| 7 Missing enabled content blocks; unavailable disabled entry stays visible | (covered by step 9 "offline") | pass: enabled wrong-game item blocks Play with the reason; disabled one stays listed as unavailable, Play allowed, not written to `dlc_load.json`, re-export equal |
| 8 Restart keeps playsets and pending acquisition | **pass**: job pending at quit, resumed after restart, one job, one attempt | – |
| 9 Failed download, offline, bad code, permission failure, process-start failure | pass (all five, below) | – |

## Steps 2–5 and 8: EU4 import, acquisition and restart

The validation playset (`PMM validation`), built as a manifest, imported into
profile A read-only and exported by the app as `validation.code`:

| # | Mod | Source | Enabled |
| --- | --- | --- | --- |
| 1 | PMM Probe C | local `pmm_probe_c` | yes |
| 2 | MRBM Font | Workshop 2506263674 | yes |
| 3 | PMM Probe A | local `pmm_probe_a` | yes |
| 4 | Larger Monuments Interface | Workshop 2471388429 (**not subscribed**: Steam API item state 0) | yes |
| 5 | PMM Probe B | local `pmm_probe_b` | no |
| 6 | Colored Mapmodes | Workshop 3390117335 | no |

Profile B, run 1, with the Web API endpoint pointed at a local server that
accepts connections and never answers (`PMM_WORKSHOP_METADATA_URL`), quit
after 5 s:

```text
01:13:05.359 preview: 6 entries, 4 enabled, 5 installed, 1 missing
01:13:05.378 notification info: Import complete - "PMM validation" was created with 6 entries. 1 missing Workshop mod queued for download.
01:13:05.382 job 1 2471388429 "Larger Monuments Interface": pending - Queued
01:13:05.612 provider: ready - Steam is running and signed in.
01:13:10.599   job 1 2471388429 "Larger Monuments Interface": pending - Checking the item with Steam
```

Profile B, run 2 (restart, real Web API): the stored job resumed by itself.

```text
01:13:30.625 job 1 2471388429 "Larger Monuments Interface": subscribing - Subscribing
01:13:30.709 job 1 2471388429 "Larger Monuments Interface": download_requested - Download requested
01:13:32.335 job 1 2471388429 "Larger Monuments Interface": downloading 0% - Downloading 0 %
01:13:33.468 job 1 2471388429 "Larger Monuments Interface": installed 100% - Installed
01:13:33.469 every enabled entry is ready; Play is available
```

- The entry became Ready only after the content was verified on disk.
- Profile B's database afterwards holds one job for the item (`installed`,
  1 attempt, 748,816 bytes), so the restart created no duplicate. The Steam
  API reported the item as subscribed and installed (state 5).
- `share-code.py compare validation.code b.code`: `equal: game 'eu4', 6
  entries, same order and enabled flags`; the A and B dumps are
  byte-identical.

### Stellaris acquisition (step 4)

Run later the same night (build `f809e91`) with a separate approval. A clean
profile imported a code with Nullcore Nomad Fixes (installed, enabled),
**UI Overhaul Dynamic - Extended Topbar** (1780481482, 1.0 MB, Steam API
item state 0 before the run, enabled) and Relic Trade (installed, disabled):

```text
03:30:57.901 preview: 3 entries, 2 enabled, 2 installed, 1 missing
03:30:58.129 job 1 1780481482 "UI Overhaul Dynamic - Extended Topbar": pending - Queued
03:30:58.674 job 1 1780481482 "UI Overhaul Dynamic - Extended Topbar": subscribing - Subscribing
03:30:59.051 job 1 1780481482 "UI Overhaul Dynamic - Extended Topbar": subscribed - Subscribed, waiting for the download
03:30:59.059 job 1 1780481482 "UI Overhaul Dynamic - Extended Topbar": download_requested - Download requested
03:31:00.567 job 1 1780481482 "UI Overhaul Dynamic - Extended Topbar": downloading 0% - Downloading 0 %
03:31:01.571 job 1 1780481482 "UI Overhaul Dynamic - Extended Topbar": downloading 100% - Downloading 100 %
03:31:03.726 job 1 1780481482 "UI Overhaul Dynamic - Extended Topbar": installed 100% - Installed
03:31:03.729 every enabled entry is ready; Play is available
03:31:03.742 canPlay=true blockers=[] warnings=[1 enabled mod has warnings (versions or dependencies); see the mod list.]
```

The re-export compared `equal` with the imported code. The game was not
started for this step (launching was verified with the probe playset). The
item was unsubscribed again afterwards (state 5 → 4); Stellaris's
`dlc_load.json` and `mod/` folder were unchanged.

## Step 6: direct launch and in-game verification

### EU4

```text
01:13:33.558 pressing Play for playset 1
01:13:33.560 launch state: validating
01:13:33.583 launch state: applying
01:13:33.612 launch state: starting
01:13:33.795 launch state: started
01:13:33.801 game started, pid 8404
```

Processes at start and 20 s later (no `Paradox Launcher`, `bootstrapper-v2`
or `dowser` process at either time; the same pid, so EU4 did not restart
itself through Steam although it ships no `steam_appid.txt`):

```text
[01:13:34] pid=8404 eu4.exe started=01:13:33 cmd="C:\Program Files (x86)\Steam\steamapps\common\Europa Universalis IV\eu4.exe" -gdpr-compliant
[01:14:04] pid=8404 eu4.exe started=01:13:33 cmd="C:\Program Files (x86)\Steam\steamapps\common\Europa Universalis IV\eu4.exe" -gdpr-compliant
```

Written configuration: `{"enabled_mods":["mod/pmm_probe_c.mod","mod/ugc_2506263674.mod","mod/pmm_probe_a.mod","mod/ugc_2471388429.mod"],"disabled_dlcs":[]}`
(the four enabled entries in playset order; `disabled_dlcs` unchanged), plus
the registration `mod/ugc_2471388429.mod` for the new item.

The probes as first created (unknown triggers inside scripted triggers)
produced no lines in EU4's `error.log`: EU4 1.37 does not check scripted
triggers that nothing uses. `tools/probe-mods` now also writes event files,
which EU4 parses at start-up. With those:

```text
[persistent.cpp:88]: Parsing Errors. File: "events/pmm_probe_only_a.txt", Error:
  [0]: Unknown trigger type. Key: pmm_probe_loaded_a: pmm_probe_loaded_a, near line: 9
[persistent.cpp:88]: Parsing Errors. File: "events/pmm_probe_only_c.txt", Error:
  [0]: Unknown trigger type. Key: pmm_probe_loaded_c: pmm_probe_loaded_c, near line: 9
[persistent.cpp:88]: Parsing Errors. File: "events/pmm_probe_shared.txt", Error:
  [0]: Unknown trigger type. Key: pmm_probe_shared_from_a: pmm_probe_shared_from_a, near line: 9
```

| Run | `enabled_mods` order of the probes | Probe names | Loaded | Shared file won by |
| --- | --- | --- | --- | --- |
| 1 | C, A | "PMM Probe C", "PMM Probe A" | A, C (B disabled: not loaded) | A |
| 2 (A moved to the top in the app) | A, C | "PMM Probe A", "PMM Probe C" | A, C | **A** |
| 3 (A's `name` changed to "PMM Probe Z" in both descriptors) | A, C | "PMM Probe Z", "PMM Probe C" | A, C | **C** |

**Conclusion (EU4 1.37.5):** enabled and disabled mods are honoured exactly,
but the order of `enabled_mods` does not decide file overrides. When two
enabled mods contain the same file, the mod whose `name` sorts first wins
(A over C, then C over Z). The official launcher writes the same file, so it
has the same limitation. The app keeps writing the playset order (it is what
the game's launcher writes, and it travels in share codes), and the EU4
adapter's `LoadOrderInfo` now states this rule with `verifiedInGame = true`.
Not tested: mods that declare `dependencies`, and names that differ only in
case or start with non-ASCII characters.

### Stellaris

Playset `PMM validation (disabled unavailable)`: Probe C, Nullcore Nomad
Fixes (3745972069), Probe A, Relic Trade (3476347069), all enabled; Probe B
disabled; and EU4's Workshop item 3390117335 disabled (an item of another
game, as the unavailable entry U).

```text
01:28:43.218 job 1 3390117335 "Colored Mapmodes": unavailable - Unavailable [Workshop item 3390117335 (Colored Mapmodes) belongs to a different game (Steam app 236850).]
01:28:45.960 every enabled entry is ready; Play is available
01:28:46.021 canPlay=true blockers=[] warnings=[1 disabled entry is not available; disabled entries do not block launching.]
01:28:46.170 launch state: started
01:28:46.176 game started, pid 10516
[01:28:46] pid=10516 stellaris.exe cmd="C:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe" -gdpr-compliant
```

`dlc_load.json`: `["mod/pmm_probe_c.mod","mod/ugc_3745972069.mod","mod/pmm_probe_a.mod","mod/ugc_3476347069.mod"]`,
no entry for the disabled unavailable item. Re-export compared `equal` with
the imported code.

Stellaris also reported the probes only through the event files:

```text
[trigger_impl.cpp:1706]: Scripted Trigger pmm_probe_shared_from_a is invalid at  file: events/pmm_probe_shared.txt line: 9
[trigger_impl.cpp:1706]: Scripted Trigger pmm_probe_loaded_c is invalid at  file: events/pmm_probe_only_c.txt line: 9
[trigger_impl.cpp:1706]: Scripted Trigger pmm_probe_loaded_a is invalid at  file: events/pmm_probe_only_a.txt line: 9
```

| Run | Probe order | Loaded | Shared file won by |
| --- | --- | --- | --- |
| 1 | C, A | A, C (B disabled: not loaded) | A |
| 2 (A moved to the top in the app) | A, C | A, C | **C** |

**Conclusion (Stellaris 4.5.2):** the order of `enabled_mods` is the
effective order; for the same file the later mod wins. `verifiedInGame` is
now set for Stellaris.

## Step 7: blocking and unavailable entries (Stellaris)

The same playset with the wrong-game item **enabled**, in a clean profile:

```text
01:28:08.351 job 1 3390117335 "Colored Mapmodes": unavailable - Unavailable [Workshop item 3390117335 (Colored Mapmodes) belongs to a different game (Steam app 236850).]
01:28:15.984 canPlay=false blockers=[#6 an EU4 item (wrong game): unavailable - Workshop item 3390117335 (Colored Mapmodes) belongs to a different game (Steam app 236850).]
01:28:22.240 Play is blocked, as expected
```

The identity check rejected the item 0.6 s after the import; nothing was
sent to Steam for it (the job never entered `subscribing`).

**Defect found and fixed.** The first run (build `91b973e`) showed the same
job as unavailable, but Play's blocker still said "pending - Queued for
download" 15 s later: launch data is refreshed with acquisition updates only
once per 5 s, and the job's last transition fell inside that window with no
later event. Fixed in `19e4705` (a trailing refresh after a throttled
update), with a regression test that fails without the fix
(`tests/app/session_host_tests.cpp`, "Play's blockers follow a download
that ends unavailable"). The run above is the rerun with the fix.

## Step 9: failure cases

| Case | How | Observed | Result |
| --- | --- | --- | --- |
| Offline (Web API unreachable) | `PMM_WORKSHOP_METADATA_URL=http://127.0.0.1:9/`, import `validation.code` (EU4) | job `pending` then, after the 15 s identity timeout, `needs_user_action`: "could not confirm with Steam that Workshop item 2471388429 exists and belongs to Europa Universalis IV, so it did not subscribe to it. Retry when this computer is online, or open the item's Workshop page manually."; Play blocked naming the entry; the Steam API still reported the item as not subscribed | pass |
| Failed download | enabled wrong-game item (step 7) | `unavailable` with the reason; entry stays in the playset; Play blocked | pass |
| Bad share code | `--self-test import:` of four damaged copies of `validation.code` | (a) last character removed: "The code is damaged … its checksum is incomplete. Copy the complete code again." (b) `PMM2.`: "Unsupported share-code version … update the app and try again." (c) plain text: "Not a valid share code … Share codes start with "PMM1."" (d) space inserted: "The share code is malformed: it contains a character that is not allowed." A dump afterwards: 0 entries, no playset created | pass |
| Configuration permission failure | a scratch copy of EU4's user-data folder (descriptors and `dlc_load.json`, path with a space and "Ü"), its `dlc_load.json` made read-only; the real EU4 installation located with that copy as user-data folder; Play | `launch state: failed` before any start: "Writing the game configuration failed, so the game was not started; the previous configuration is unchanged. Could not replace "dlc_load.json". Check that the location is not read-only and that you have permission to change it." Detail: `…\dlc_load.json: Access is denied. (Windows error 5)`. Hash unchanged; the descriptor written earlier in the same transaction was rolled back; no game process; no interrupted transaction afterwards | pass |
| Process-start failure | a scratch "installation" (EU4's `launcher-settings.json`, `eu4.exe` = a small test executable with execute permission denied to the user), scratch user-data folder; Play | "Europa Universalis IV could not be started. The previous game configuration was restored. Could not start "eu4.exe"." Detail lists executable, working directory, arguments and `Access is denied. (Windows error 5)`. `dlc_load.json` hash back to the previous value, generated descriptor removed, nothing running, no system dialog | pass |

A first attempt at the permission case failed in the test setup: the
`attrib +R` command did not apply to the path, so the copy stayed writable,
and the app correctly started EU4 (writing its configuration into the
scratch copy only; the real `dlc_load.json` was unaffected, hash checked).
The game was closed and the case repeated with the read-only flag verified
first.

## Restore

The app's **Restore** (the `restore` step, i.e. `restoreLastBackup`) undid
the launches one transaction at a time. After two restores per game the real
`dlc_load.json` files were byte-identical to the backups taken before the
session (SHA-256 `b59815f2…cfb157` for EU4, `26795db1…435e53` for
Stellaris), and the registration descriptor created for the new Workshop
item was removed again.

## Clean-up

- Probe mods removed from both games (`probe_mods.py remove`); both `mod/`
  listings identical to before the session.
- Larger Monuments Interface unsubscribed again through the Steam API in
  EU4's app context (state 5 → 4; Steam deleted the content folder shortly
  after). pmm-steam-helper deliberately has no unsubscribe command; a one-off
  script outside the repository was used.
- No game, launcher or pmm process left running.

## Limitations

- One machine and one Steam account: profiles A and B are isolated
  application profiles, not separate machines.
- The Steam client's own UI was not observed; subscription and installation
  were read from the Steam API item state and the content folder.
- The games were observed through their logs and the process list, not
  screenshots (the desktop showed the user's own windows).
- Launches were checked until the probes were reported (EU4: menu session
  started; Stellaris: event loading), not into a running campaign.
- Not covered: Linux and macOS; Steam's offline mode (the Web API was made
  unreachable instead); EU4 mods with `dependencies`.
