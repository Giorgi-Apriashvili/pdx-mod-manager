# Troubleshooting

Problems you may run into, what they mean, and what to do. Error messages in
the application start with an actionable explanation; technical details (OS
error text, file paths, Steam result codes) are kept separately and appear in
the diagnostics.

Behaviour that has not yet been observed with a real game (launching, live
Workshop downloads) follows the design in [`ui-contract.md`](ui-contract.md),
[`app.md`](app.md) and [`steam-workshop.md`](steam-workshop.md); the
[support matrix](support-matrix.md) says what has been verified.

## Logs and diagnostics

| What | Windows | Linux | macOS |
| --- | --- | --- | --- |
| Log files | `%LOCALAPPDATA%\ParadoxModManager\logs\` | `~/.local/state/ParadoxModManager/logs/` (or `$XDG_STATE_HOME/...`) | `~/Library/Logs/ParadoxModManager/` |
| Database | `%APPDATA%\ParadoxModManager\state.sqlite` | `~/.local/share/ParadoxModManager/state.sqlite` (or `$XDG_DATA_HOME/...`) | `~/Library/Application Support/ParadoxModManager/state.sqlite` |
| Cache | `%LOCALAPPDATA%\ParadoxModManager\cache\` | `~/.cache/ParadoxModManager/` | `~/Library/Caches/ParadoxModManager/` |
| Configuration journals and backups | `%APPDATA%\ParadoxModManager\transactions\` | `~/.local/share/ParadoxModManager/transactions/` | `~/Library/Application Support/ParadoxModManager/transactions/` |
| Game files the manager writes on Play | `Documents\Paradox Interactive\<Game>\dlc_load.json`, `...\mod\*.mod` | `~/.local/share/Paradox Interactive/<Game>/...` | `~/Documents/Paradox Interactive/<Game>/...` |

When started with an isolated profile (`pmm-app --profile <dir>`), all
application files live inside `<dir>` instead. **Settings → About** shows the
folders of the running profile and opens the logs folder. `--verbose` adds
debug-level records to the log.

**Copy diagnostics** in the application produces a report for bug reports. It is
passed through the redaction rules: your home folder becomes `~`, your user
name becomes `<user>`, and credential-like values (`password=`, `token=`,
`api_key=`, ...) become `<redacted>`. Raw log files are **not** redacted: they
may contain full paths. The Steam library used by the Workshop helper prints
your SteamID64 to the helper's error output; do not paste raw helper output
publicly. Nothing is sent anywhere automatically; there is no telemetry.

## Steam is not running, not signed in, or the game is not owned

Workshop acquisition uses your running Steam client (ADR 0003). Jobs then wait
in *needs user action* with one of these explanations:

| Helper result | Meaning | What to do |
| --- | --- | --- |
| `steam_not_running` | The Steam client is not running | Start Steam, sign in, then **Retry** |
| `not_logged_on` | Steam runs but no user is signed in | Sign in, then **Retry** |
| `init_failed` | Steam refused the session: the signed-in account does not own the game, Steam is still starting, or a stray `steam_appid.txt` names another app | Sign in with the account that owns the game; wait until Steam has finished starting; **Retry** |
| `library_load_failed` | The game's Steam API library is missing or cannot be loaded | In Steam: game *Properties > Installed Files > Verify integrity*; or use the manual fallback |
| `symbols_missing` | The game's Steam API library lacks functions the helper needs | Use the manual fallback (open in Steam, subscribe there, rescan) |
| `invalid_arguments` | Internal: the helper was started incorrectly | Report a bug with diagnostics |

The manager never asks for or stores your Steam password. While the helper
runs, Steam treats the game as running: friends may see you as playing it and
Steam's "last played" changes. The manager stops the helper when downloads are
idle and before it launches the game.

## A Workshop item is private, deleted, banned or for another game

The entry becomes **unavailable**: Steam answered that the item does not exist
for your account (deleted, hidden, friends-only), is banned, or belongs to a
different game (checked through the Steam Web API before downloading). The
entry stays in the playset, visibly unresolved, with its original enabled flag:
nothing is dropped or reordered silently.

- If the entry is **disabled**, you can play; it never blocks launching.
- If it is **enabled**, Play is blocked. Ask the person who shared the code for
  a replacement, or disable or remove the entry yourself (this changes the
  imported setup, and the app says so).
- Some errors need your action rather than being final (account limits,
  parental controls, too many subscriptions); the job then says so and offers
  **Retry**.

## Offline use

Without a network connection, or with Steam in offline mode, the manager still
starts, shows playsets and inventory, and accepts mods that are already
installed. Subscribing and downloading wait: jobs fail with an offline or
"not signed in" reason and retry automatically with backoff (5 s doubling up
to 2 min, three attempts per cycle), then show "automatic retries are
exhausted". Press **Retry** once you are online. Whether the game itself runs
offline is up to the game and Steam.

## A download does not finish

- No progress for 5 minutes marks the job failed (stalled). Check Steam's
  *Downloads* page: downloads may be paused, scheduled, or blocked by low disk
  space; then **Retry**.
- "Installed content is unusable" means Steam reported the item as installed
  but the folder has no `descriptor.mod`. Verify the item in Steam or
  resubscribe there, then **Retry**.
- If automatic acquisition cannot work on your system, use **Open in Steam
  (manual)**: subscribe on the page that opens, then rescan. This fallback is
  limited and does not count as automatic download.

Cancelling an import stops the manager's own jobs; subscriptions and downloads
that already happened stay in Steam. The manager never unsubscribes or deletes
mods.

## Configuration permission errors

Play writes the game's load-order file (for Stellaris `dlc_load.json`) and
missing mod descriptors in the game's user-data folder. If that fails, the
launch is refused, any partial change is rolled back, and the game is not
started. Common causes:

- **Read-only file**: Windows: file *Properties* > clear *Read-only*, or
  `attrib -R "<file>"`; Linux/macOS: `chmod u+w "<file>"` and check that the
  folder is writable.
- **Windows Controlled folder access** (ransomware protection) blocks writes
  to `Documents`: *Windows Security > Virus & threat protection > Ransomware
  protection > Allow an app through Controlled folder access*, add the
  manager.
- **OneDrive-redirected Documents**: the manager follows the redirected folder
  (the same one the game uses). If files are "online-only" or OneDrive is
  paused or out of space, make the folder available offline.
- **macOS privacy prompt**: allow the manager to access the Documents folder
  (*System Settings > Privacy & Security > Files and Folders*).
- **Another program holds the file** (the Paradox launcher, a sync tool, an
  editor): close it and try again.

## Interrupted launch: recovering configuration changes

If the manager or the computer stopped while game configuration was being
written, the next start shows a notice and offers **Recover** next to Play.
Recovery restores the files saved before the change (verified against their
recorded SHA-256) and deletes files that the interrupted launch had created.
An interrupted change blocks launching only the game it belongs to; for other
games it is listed as a warning next to Play. Recover before launching that
game again.

Transactions are journaled in the application data folder (`transactions/`,
see the table above); each `<id>` folder contains `journal.json` and backup
copies of the original files. Do not edit them by hand.

### Recover reports a missing or damaged backup

"The backup of ... is missing or unreadable" or "... is damaged, so nothing
was restored" means the change cannot be undone automatically: nothing was
changed by the attempt, and the transaction keeps blocking launches of that
game. The application has no action yet to discard such a record (planned).
Until then:

1. Close the manager.
2. Check the files named in the message in the game's user-data folder, for
   example `dlc_load.json`. If in doubt, let the Paradox launcher write the
   game's configuration again by starting the game once through it, then
   close the game and the launcher.
3. Move the transaction's `<id>` folder out of `transactions/` (keep it until
   you are sure you do not need its backups; do not delete files inside it).
4. Start the manager: the game is no longer blocked.

## Restoring the pre-launch configuration and using the official launcher

The configuration the manager writes stays in effect after the game exits:
starting the game later through Steam or the Paradox launcher may use it until
the launcher rewrites it from its own playset. The manager never reads or
modifies the launcher's database (`launcher-v2.sqlite`).

To go back to the files as they were before the manager's last launch, use
**Restore pre-launch configuration**. It puts back the exact saved files and
removes descriptors the launch created; it can be repeated. The Paradox
launcher can be used normally afterwards; the next time you press Play in the
manager, it writes its configuration again.

## The game still opens the Paradox launcher

The manager starts the game executable directly (for Stellaris
`stellaris.exe` with the arguments from `launcher-settings.json`), never
Steam's *Play* button or a `steam://` link, which start the launcher
(`dowser.exe` in the game folder, then `Paradox Launcher.exe`). If you see the
launcher:

- You started the game from Steam or a desktop shortcut rather than from the
  manager.
- The detected executable is wrong (for example a manually selected folder
  where the launcher replaced the game executable): rescan, or locate the
  installation again, and check the executable shown in the details and
  diagnostics.
- The game itself decided to show the launcher (for example after an update
  that requires it). Record the game version and report it; the
  [support matrix](support-matrix.md) lists which versions were verified.
- Europa Universalis IV: its folder has no `steam_appid.txt`, so a directly
  started `eu4.exe` depends on the `SteamAppId`/`SteamGameId` environment
  variables the manager sets. If the game quits and restarts through Steam
  (which then shows the launcher), report it with the diagnostics; this
  behaviour is not yet verified ([support matrix](support-matrix.md)).

## The game does not start (process start failures)

The error says the game could not be started and the details contain the
operating system's reason:

| Reason | What to do |
| --- | --- |
| File not found | The game was moved or uninstalled: rescan or locate the installation |
| Access denied | Antivirus or an access rule blocks the executable: allow it, or check file permissions (`icacls` / `ls -l`); on Linux/macOS make sure it is executable (`chmod +x`) |
| Bad executable format / wrong architecture | The selected file is not the game for this OS (for example a Windows `.exe` on Linux): select the native installation |
| Working directory missing | Rescan; the game folder changed |

The manager starts processes with an argument list (no shell) and suppresses
Windows "bad image" dialogs, so failures are reported instead of hanging. If
the configuration was already written for this launch, use **Restore
pre-launch configuration** if you do not want to keep it.

## "The game is already running" or another program is writing its configuration

Play is blocked while the game or a conflicting configuration writer (such as
the Paradox launcher) runs, so neither overwrites the other. Close it (check
the task manager / `ps`) and press Play again.

## "Already running for this profile"

Only one copy of the manager can use a profile at a time, because the
configuration journal, the download jobs and the Steam helper sessions belong
to that one process. A second start waits up to 2 seconds for the profile and
then shows this message. Switch to the window that is already open (it may be
minimised or on another desktop), or close it first. A lock left behind by a
crashed manager is detected and taken over automatically. To run two copies
side by side, give the second one its own profile (`--profile <dir>`).

## "Automatic downloads are unavailable" or Play is disabled in read-only mode

`--no-steam` turns Workshop downloads off for the session: missing mods stay
listed as missing, nothing is subscribed or downloaded, and an import queues
no downloads. `--read-only` does the same and also never writes game files
or launches a game; Play then shows "Read-only verification mode". Start the
manager without these options to download or play; imported entries that are
still missing can then be fetched with **Download missing**.

## The database was created by a newer version

The message says the data was created by a newer version of the application.
Your data is untouched: the file is refused before anything is written. Install
the newer version again, or start with a separate profile. Do not delete
`state.sqlite`: it holds your playsets.

## The database is damaged

The message includes a recovery hint; the details contain SQLite's
diagnostics. Procedure ([persistence](persistence.md#integrity-checks-and-recovery)):

1. Close the manager.
2. Rename `state.sqlite` (for example to `state.sqlite.damaged`) and delete
   leftover `state.sqlite-wal` and `state.sqlite-shm` files belonging to it.
3. Rename the newest `state.sqlite.bak-v<N>` (made before each schema upgrade)
   to `state.sqlite`, or start without a database to get a new empty one.
4. Rescan games. Playsets exported as share codes can be imported again.

## Running the Steam helper by hand

Only for diagnosing Workshop problems. **While it runs, Steam treats the game
as running.** Use read-only commands:

```sh
pmm-steam-helper --app-id 281990 --steam-api "<Steam library>/steamapps/common/Stellaris/steam_api64.dll"
{"cmd":"ping"}
{"cmd":"state","ids":["<workshop id>"]}
{"cmd":"quit"}
```

The first output line is the `ready` event (`ok`, `loggedOn`, `ownsApp`, or an
`error` from the table above). Exit codes 2 to 8 correspond to those errors
([steam-workshop.md §2](steam-workshop.md#2-helper-protocol-version-1)). The
error output can contain your SteamID64.

## An update did not install

Your playsets, settings and logs are outside the install folder, so a failed update
does not touch them. Every installer run writes `%TEMP%\Setup Log <date> #<n>.txt`;
look at the newest one first.

| Message | Meaning | What to do |
| --- | --- | --- |
| "... is still running after 60 seconds" | The application did not quit in time. Nothing has changed | Close it (if no window is visible, end `pmm-app.exe` in Task Manager), then **Retry** |
| "Setup cannot replace the program files ..." | Another program has a file in the install folder open, such as a command window in `bin` or a file viewer. Nothing has changed | Close it, then **Retry**, or **Cancel** and update later |
| "The update was not installed. Your current version ... is still in place" | The installer failed or was cancelled while copying and put the previous files back | Start the application as usual and try again; the log has the reason |
| "... the previous program files could not be put back" | Something kept the folder from being restored | Run the setup from the [releases page](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases) to repair the installation |

"Update and restart" is held back while a game is being launched or Workshop
downloads are running; it says so, and you can press it again afterwards. Only a
copy installed with the setup can update itself. A ZIP copy gets **Open release
page** instead. Windows SmartScreen may warn about the setup, because it is not code
signed yet (*More info > Run anyway*). The first update between two published
releases has not happened yet, so please report anything unexpected.

## Reporting a problem

Include the application version, OS, game and game version, distribution
(Steam, other), what you did, what you expected, and the **Copy diagnostics**
output. Check attached logs for personal paths before posting them.
