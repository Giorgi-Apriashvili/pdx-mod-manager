# Troubleshooting

Problems you may run into, what they mean, and what to do. Error messages in
the application start with an actionable explanation; technical details (OS
error text, file paths, Steam result codes) are kept separately and appear in
the diagnostics.

Some features referenced here belong to the desktop application, which is in
progress (see the [support matrix](support-matrix.md)); their behaviour follows
the design in [`ui-contract.md`](ui-contract.md) and
[`steam-workshop.md`](steam-workshop.md).

## Logs and diagnostics

| What | Windows | Linux | macOS |
| --- | --- | --- | --- |
| Log files | `%LOCALAPPDATA%\ParadoxModManager\logs\` | `~/.local/state/ParadoxModManager/logs/` (or `$XDG_STATE_HOME/...`) | `~/Library/Logs/ParadoxModManager/` |
| Database | `%APPDATA%\ParadoxModManager\state.sqlite` | `~/.local/share/ParadoxModManager/state.sqlite` (or `$XDG_DATA_HOME/...`) | `~/Library/Application Support/ParadoxModManager/state.sqlite` |
| Cache | `%LOCALAPPDATA%\ParadoxModManager\cache\` | `~/.cache/ParadoxModManager/` | `~/Library/Caches/ParadoxModManager/` |
| Game files the manager writes on Play | `Documents\Paradox Interactive\<Game>\dlc_load.json`, `...\mod\*.mod` | `~/.local/share/Paradox Interactive/<Game>/...` | `~/Documents/Paradox Interactive/<Game>/...` |

When started with an isolated profile directory (planned `--profile <dir>`
option), all application files live inside that directory instead.

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
written, the next start detects the interrupted transaction and offers
**Recover**. Recovery restores the files saved before the change (verified
against their recorded SHA-256) and deletes files that the interrupted launch
had created. Recover before launching that game again.

Transactions are journaled in the application data folder; each contains
`journal.json` and backup copies of the original files. Do not edit or delete
them by hand while an interrupted transaction is pending.

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

## Reporting a problem

Include the application version, OS, game and game version, distribution
(Steam, other), what you did, what you expected, and the **Copy diagnostics**
output. Check attached logs for personal paths before posting them.
