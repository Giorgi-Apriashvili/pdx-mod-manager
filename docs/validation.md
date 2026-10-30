# Manual end-to-end validation

This is the acceptance procedure from spec section 10 ("Manual end-to-end
acceptance") for **Stellaris** and **Europa Universalis IV**. Automated tests
cannot cover it: it needs a real game installation, a Steam account that owns
the game, and observation of the running game.

**Only read-only partial runs have been recorded so far** (steps 1, 2, 3
and 5 on one machine with isolated profiles; see the
[results table](#6-results)). Steps 4 and 6 to 9 write game files, start the
game or talk to Steam and have not been run; they need the explicit approval
of the machine's owner. Record only what you observed; a failed or skipped
step is a valid result and must be written down as such. The
[support matrix](support-matrix.md) is updated only from rows in that table.

The procedure uses the desktop application `pmm-app` and the UI described in
[`ui-contract.md`](ui-contract.md) (Import code, Export code, Play, ...).
There is no command-line tool; where a step can be scripted, it uses
`pmm-app`'s verification switches ([app.md](app.md#command-line)), which
never write game files. Where the exact menu name differs in the build you
test, follow the build and note the difference in the results.

## 1. Before you start

### Environments

- **A (source)** and **B (recipient)**. B should be another machine or a VM
  with a Steam account that owns the game. If only one machine is available,
  use B = the same machine with an **isolated application profile** (below);
  the game folders and the Steam account are then shared, so one Workshop item
  must be made "missing" by hand (step 4).
- On each environment: Steam running and signed in, the game installed and
  started at least once through Steam (so its user-data folder exists), the
  game not running, the Paradox launcher closed.

### Isolated application profile

`pmm-app --profile <dir>` keeps the application database, configuration
journals, profile lock, logs and cache inside `<dir>` instead of the standard
per-user folders ([app.md](app.md#files-and-folders)). Use a new, empty
directory, preferably with a space and non-ASCII characters in its path, for
example `C:\pmm validation\Prüfung B`. The game's own folders are still the
real ones. Only one `pmm-app` can run per profile; A and B can run at the
same time because they use different profiles.

Add `--read-only` for any step that must not write game files or launch the
game (it also turns Workshop downloads off), and `--no-steam` to keep the
Steam helper from starting while Steam downloads should stay untouched.

### Tools

- A build of `pmm-app` from a known commit (from the build tree with Qt's
  `bin` folder on `PATH`, or an extracted package). Record the version and
  `git rev-parse --short HEAD`.
- Python 3 for `tools/probe-mods/probe_mods.py` (observable probe mods,
  [ADR 0006](adr/0006-probe-mod-verification.md)) and
  `scripts/share-code.py` (independent share-code decoder).
- Optional: `pmm-app --profile <dir> --live-test "<steps>"` drives the same
  Backend calls as the buttons (`import`, `select`, `move`, `wait-ready`,
  `play`, `play-fails`, `restore`, ...) and prints timestamped job, launch
  and notification transitions, which makes a run reproducible and its log
  usable as evidence ([app.md](app.md#hidden-verification-switches); the
  2026-10-30 run used it, see
  [live validation](evidence/live-validation-2026-10-30.md)).

### Choose test content

| Name | What | Requirement |
| --- | --- | --- |
| W1, W2 | Small public Workshop mods for the game | Compatible with the installed game version |
| W3 | Small public Workshop mod | Subscribed on A, **not** subscribed on B (the "initially missing" mod) |
| U | A Workshop id that cannot be acquired | Deleted, private, or belonging to another game (for example an EU4 item id in a Stellaris playset) |
| Probes A, B, C | Local probe mods created by `probe_mods.py` | Created on both A and B |

Subscribing to W3 on B changes that Steam account's subscriptions. Do it only
on an account where that is acceptable. The manager never unsubscribes; remove
subscriptions afterwards in the Steam client if wanted.

### Paths used below

| Placeholder | Stellaris | Europa Universalis IV |
| --- | --- | --- |
| `<appid>` | `281990` | `236850` |
| `<install>` | `<Steam library>/steamapps/common/Stellaris` | `<Steam library>/steamapps/common/Europa Universalis IV` |
| `<exe>` | `stellaris.exe` | `eu4.exe` (observed) |
| `<userdata>` | `Documents\Paradox Interactive\Stellaris` | `Documents\Paradox Interactive\Europa Universalis IV`, unless `<install>/userdir.txt` names another folder |
| Version source | `<install>/launcher-settings.json` (`version`, `rawVersion`) and `buildid` in `steamapps/appmanifest_<appid>.acf` | same files (observed: `rawVersion` `v1.37.5.0`) |
| Arguments the game's launcher passes (`exeArgs`) | `-gdpr-compliant` | `-enabletelemetry -gdpr-compliant`; direct launches are designed to drop `-enabletelemetry` |
| `steam_appid.txt` in `<install>` | present | **absent**: a direct start relies on `SteamAppId`/`SteamGameId` set by the manager |
| Load-order file | `<userdata>/dlc_load.json`, `enabled_mods` (observed) | `<userdata>/dlc_load.json`, `enabled_mods` (observed read-only; not yet with a game started by the manager) |
| `supported_version` format for probes | `v4.5.*` (match `rawVersion`) | `v1.37.*.*` (four components, as in installed descriptors) |
| Paradox launcher processes | `Paradox Launcher.exe`, `bootstrapper-v2.exe`, and `dowser.exe` (in the game folder) | same |

## 2. Record the environment

Capture these for the results row:

```sh
# OS
Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version   # PowerShell
# Game version and Steam build
python -c "import json,sys; d=json.load(open(sys.argv[1],encoding='utf-8')); print(d.get('version'), d.get('rawVersion'), d.get('distPlatform'))" "<install>/launcher-settings.json"
grep buildid "<Steam library>/steamapps/appmanifest_<appid>.acf"
```

## 3. Prepare probe mods (A and B)

```sh
python tools/probe-mods/probe_mods.py create "<userdata>/mod" --supported-version "v4.5.*"     # Stellaris 4.5.x
python tools/probe-mods/probe_mods.py create "<userdata>/mod" --supported-version "v1.37.*.*"  # EU4 1.37.x
```

This writes `mod/pmm_probe_{a,b,c}` folders and `mod/pmm_probe_{a,b,c}.mod`
descriptors into the game's user mod folder. They contain no gameplay content.
Remove them at the end with `probe_mods.py remove "<userdata>/mod"`.

## 4. Procedure

Each step lists the action, the expected observation, and what to record.

### Step 1: detect the installation and its mods (A)

1. Start `pmm-app` (normal profile, or `--profile` with a new directory) and
   select the game.
2. Expected: the game shows as installed with the version from
   `launcher-settings.json`; the install and user-data paths are correct; the
   inventory contains every folder under
   `<Steam library>/steamapps/workshop/content/<appid>/` and every local mod in
   `<userdata>/mod`, including the three probes.
3. Record: version shown, number of Workshop and local mods in the app versus
   on disk (`ls "<...>/workshop/content/<appid>" | wc -l`).

Workshop items that are registered with the game (a `mod/ugc_<id>.mod` file)
but whose content folder is missing, for example right after a reinstall,
are not listed as installed mods. A playset entry that refers to one shows as
**Missing** (and can be acquired), not as Broken. **Broken** is reserved for
installed mods whose descriptor or folder cannot be used.

### Step 2: create the validation playset (A)

Create a playset named `PMM validation` with exactly this order:

| # | Mod | Enabled |
| --- | --- | --- |
| 1 | PMM Probe C | yes |
| 2 | W1 | yes |
| 3 | PMM Probe A | yes |
| 4 | W3 | yes |
| 5 | PMM Probe B | no |
| 6 | W2 | no |

The order is deliberately not alphabetical (C before A) and mixes sources.
Expected: the list shows this order and these flags, also after restarting the
app. Record: a screenshot of the list.

### Step 3: export, then import into a clean profile (A to B)

1. On A: **Export code**, then save it to a file `validation.code` (export to
   file) and copy it to B.
2. Inspect it independently:
   `python scripts/share-code.py decode validation.code`.
   Expected: `game` is the game key, six `mods` in the order above, the enabled
   flags above, no paths.
3. On B: start `pmm-app --profile "<new empty dir>" --game <game>` (or a
   fresh installation on another machine), **Import code**, paste or open
   `validation.code`. The profile must be new: its database is created on
   this start and holds no playsets.
4. Expected preview: playset name, 6 entries, 4 enabled, W3 listed as missing
   (enabled), probes matched to the installed local probes, any warnings shown.
   Nothing is created until you confirm. Confirm with the default
   **new playset**.
5. Record: screenshot of the preview.

Read-only, scripted variant of steps 2, 3 and 5 (single machine, no
Workshop download; used for the recorded partial runs). Each command uses a
new profile folder; the verification switches imply `--read-only`:

```sh
pmm-app --profile "<dir A>" --game <game> \
        --self-test "config,disable:1,move:0:3,export:validation.code,dump:a.json"
pmm-app --profile "<new empty dir B>" --game <game> --self-test "import:validation.code,dump:b.json"
cmp a.json b.json        # identities, order and enabled flags must be identical
```

`config` builds the playset from the game's current configuration instead of
the probe playset above, `disable:1` and `move:0:3` make one entry disabled
and the order non-default, and `dump` writes the active playset's identities,
order and enabled flags as JSON.

### Step 4: resolve the missing Workshop mod (B)

Single machine only: before importing, unsubscribe from W3 in the Steam client
and wait until `workshop/content/<appid>/<W3>` disappears.

1. Acquisition of W3 starts after the import (or with **Download missing**).
   B must run without `--read-only` and `--no-steam` for this step.
2. Expected states, in order: pending, subscribing, subscribed, download
   requested and/or downloading, installed. The Steam client shows the
   subscription; `workshop/content/<appid>/<W3>/descriptor.mod` exists; the
   entry becomes Ready only after that.
3. Record: the state sequence with times, and the folder listing. This is the
   first live run of subscribe and download; copy the helper transcript from
   the application log into `docs/evidence/` after redaction.

### Step 5: confirm exact equality (B)

1. On B, export the imported playset to `imported.code` and compare:
   `python scripts/share-code.py compare validation.code imported.code`.
   Expected: `equal` (same game, identities, order and enabled flags). A note
   about `gameVersionHint` is acceptable when A and B run different versions.
2. Expected: no other mods are enabled in the playset; extra mods installed on
   B are not part of it.
3. Record: the compare output.

### Step 6: launch without the Paradox launcher and verify in-game (B, then A)

1. Make sure the game and the Paradox launcher are not running. Press **Play**.
2. Expected:
   - no Paradox launcher window; the game process starts directly
     (`tasklist /FI "IMAGENAME eq <exe>"` lists it and
     `tasklist | findstr /I "dowser bootstrapper Paradox"` lists nothing);
   - EU4 only (no `steam_appid.txt`): the game does not quit and restart
     itself through Steam; record the process list a few seconds after the
     start, and whether Steam shows the game as running;
   - `<userdata>/dlc_load.json` `enabled_mods` lists exactly the four enabled
     entries and nothing else, in the order the adapter documents in its
     `LoadOrderInfo`. With the launcher's observed convention (playset
     position 0 first) that is `mod/pmm_probe_c.mod`, `mod/ugc_<W1>.mod`,
     `mod/pmm_probe_a.mod`, `mod/ugc_<W3>.mod`; both games' existing files use
     the `mod/ugc_<id>.mod` naming for Workshop items. `disabled_dlcs` is
     unchanged from before the launch.
3. Wait for the main menu, quit the game, then analyse its log:

   ```sh
   python tools/probe-mods/probe_mods.py analyze "<userdata>/logs/error.log" --enabled c a --disabled b
   ```

   Expected JSON: `allEnabledLoaded: true`, `noDisabledLoaded: true`,
   `singleSharedWinner: true`, `ok: true`. `precedence` reports whether the
   first or the last enabled probe won the shared file.
4. Swap probes A and C in the playset (A first), Play again, and rerun
   `analyze` with `--enabled a c --disabled b`. Expected: the winner moves with
   the order, consistent with the reported precedence.
5. Record: both `analyze` outputs, the `dlc_load.json` contents, and the
   precedence rule (this is the evidence for the adapter's `LoadOrderInfo`).

### Step 7: blocking and unavailable entries (B)

Create two test codes from `validation.code`:

```sh
python scripts/share-code.py decode validation.code > manifest.json
# copy 1: append {"source":"steam-workshop","id":"<U>","enabled":true}  -> enabled-missing.json
# copy 2: append {"source":"steam-workshop","id":"<U>","enabled":false} -> disabled-unavailable.json
python scripts/share-code.py encode enabled-missing.json > enabled-missing.code
python scripts/share-code.py encode disabled-unavailable.json > disabled-unavailable.code
```

1. Import `enabled-missing.code` (new playset). Expected: U ends unavailable
   or failed, **Play is blocked**, and the blocker names U. Nothing is
   launched.
2. Import `disabled-unavailable.code`. Expected: U stays visible as
   unavailable and disabled; **Play is allowed** once the enabled entries are
   ready; after Play, `dlc_load.json` does not list U; re-exporting and running
   `share-code.py compare disabled-unavailable.code <re-export>` reports
   `equal` (no flag or order changed).
3. Record: screenshots of the blocker and of the unavailable entry, compare
   output.

### Step 8: restart persistence (B)

1. Start acquiring an item that will not finish immediately (a larger item,
   or with Steam set to offline so the job waits), then quit the manager.
2. Restart it with the same profile.
3. Expected: playsets unchanged (names, order, flags); the acquisition job is
   listed in its previous state and is reconciled by a fresh state query (no
   duplicate subscription; Steam shows one subscription).
4. Record: job states before and after the restart.

### Step 9: failure cases

| Case | How to induce | Expected | Revert |
| --- | --- | --- | --- |
| Failed download | Import a code with an enabled U, or disconnect the network while a download runs | Job fails with a reason (unavailable, or retryable with backoff until "automatic retries are exhausted"); the entry stays in the playset; Retry and the manual fallback are offered; Play blocked while the entry is enabled | Reconnect |
| Offline use | Steam: *Go Offline*, or disconnect the network, then start the manager | Playsets and inventory available; acquisition explains that Steam is offline or not signed in; record whether a ready playset still launches | Go online |
| Bad share code | Paste (a) the code without its last character, (b) the code with `PMM1.` changed to `PMM2.`, (c) arbitrary text, (d) the code with a space inserted | (a) truncated or damaged, (b) created by a newer version, (c) not a share code, (d) rejected; no playset, no job, no file written | none |
| Configuration permission failure | Hash `dlc_load.json` (`certutil -hashfile dlc_load.json SHA256`); `attrib +R "<userdata>\dlc_load.json"`; press Play | Launch refused with a permission message before the game starts; hash unchanged; no interrupted transaction left behind | `attrib -R ...` |
| Process-start failure | Deny execution of the game executable temporarily (may need an elevated prompt): `icacls "<install>\<exe>" /deny "%USERNAME%:(X)"`; press Play | Error that the game could not be started, with the OS error in the details and diagnostics; no Paradox launcher; record whether the configuration written for this launch was restored automatically or offered for restore | `icacls "<install>\<exe>" /remove:d "%USERNAME%"`; if in doubt, Steam *Verify integrity of game files* |

Afterwards: remove the probes (`probe_mods.py remove "<userdata>/mod"`), and
use **Restore pre-launch configuration** if you want the game's previous
`dlc_load.json` back.

## 5. Evidence

- Put screenshots, `analyze` outputs and transcripts under
  `docs/evidence/<date>-<game>/`.
- Redact before committing: user names, home paths (use `~` / `<user>`),
  SteamID64 values, and anything account-related. The application's
  **Copy diagnostics** output is meant to be redacted (check it anyway); raw
  log files are not.
- Never commit game files, Workshop content, `launcher-v2.sqlite`, or the
  application database.

## 6. Results

One row per game, Windows version and distribution combination (repeat rows for reruns).
**Result** is `pass`, `fail` or `partial`, with failed step numbers.

| Game | Game version (build) | OS | Distribution | App version (commit) | Date | Steps run | Result | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Stellaris | Cygnus v4.5.2 (9776) (25664273) | Windows 11 23H2 | Steam | 0.1.0-dev (branch `agent/app`) | 2026-10-29 | 2, 3, 5 read-only, scripted variant (one machine, isolated profiles, playset from the game's configuration) | partial: 4, 6-9 not run | [app.md](app.md#hidden-verification-switches) |
| Stellaris | Cygnus v4.5.2 (9776) (25664273) | Windows 11 23H2 | Steam | 0.1.0-dev (`66d91d4`) | 2026-10-30 | 1 read-only, without the probe mods ([real scan](evidence/real-scan-2026-10-29.md)); 2, 3, 5 read-only, scripted variant | partial: 4, 6-9 not run | [round trip](evidence/app-round-trip-2026-10-30.md) |
| Europa Universalis IV | v1.37.5.0 (15918133) | Windows 11 23H2 | Steam | 0.1.0-dev (`66d91d4`) | 2026-10-30 | 1 read-only, without the probe mods ([real scan](evidence/real-scan-2026-10-29.md)); 2, 3, 5 read-only, scripted variant | partial: 4, 6-9 not run | [round trip](evidence/app-round-trip-2026-10-30.md) |
| Europa Universalis IV | v1.37.5.0 (15918133) | Windows 11 23H2 | Steam | 0.1.0-dev (`91b973e`; failure cases `19e4705`) | 2026-10-30 | 1-6, 8, 9 live with probe mods (one machine, isolated profiles A and B; `--live-test`) | pass; step 6 shows that EU4 ignores the `enabled_mods` order for file overrides (name order decides); step 7 run for Stellaris only | [live validation](evidence/live-validation-2026-10-30.md) |
| Stellaris | Cygnus v4.5.2 (9776) (25664273) | Windows 11 23H2 | Steam | 0.1.0-dev (`19e4705`) | 2026-10-30 | 1-7 live (probe mods; step 4 with a separate approval, build `f809e91`; `--live-test`) | pass; 8-9 run for EU4 only (same code paths); step 7 found a stale Play blocker, fixed in `19e4705` and rerun | [live validation](evidence/live-validation-2026-10-30.md) |
