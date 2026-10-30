# Windows installer verification, 2026-10-30

Real runs of the Inno Setup installer on the development machine (Windows 11
Enterprise 23H2): everything in a scratch folder, nothing left installed. Commands
were run from PowerShell; `<scratch>` is a private temp folder, and the install
folder `<scratch>\inst folder` has a space in its name on purpose.

| Item | Value |
| --- | --- |
| Compiler | Inno Setup 6.7.3, the copy already installed on this machine (`ISCC.exe` signature valid). The `windows-2025` runner image has 6.7.1 |
| Package | `release` preset with `-B <scratch>`, `-DPMM_BUILD_TESTS=OFF`, MSVC 19.51, Qt 6.10.3, 0 warnings |
| Build | `cmake --build <scratch>\build\release --target installer`: exit 0, about 10 s when the application is already built, no compiler warnings |
| Result | `ParadoxModManager-0.1.0-windows-x86_64-setup.exe`, 16 580 812 bytes |
| ZIP from the same tree | `ParadoxModManager-0.1.0-windows-x86_64.zip`, 22 078 369 bytes, 80 files; the installer stage has the same 80 paths |

The 0.1.1 and 0.1.2 builds came from a copy of the stage with `bin\pmm-app.exe`
replaced by a stand-in, a small program that appends a line to a marker file and
exits, so a relaunch is recorded and never opens the real application against the
standard profile:

```text
ISCC /Q /DAppVersion=0.1.1 /DStageDir=<scratch>\stage-standin /O<scratch>\out packaging\windows\installer.iss
```

`Local\ParadoxModManagerRunning` was held by a separate hidden PowerShell process,
because this branch's `pmm-app` does not contain the mutex yet (it comes with the
updater change):

```powershell
$m = [System.Threading.Mutex]::new($true, 'Local\ParadoxModManagerRunning')
Start-Sleep -Seconds $Seconds
$m.ReleaseMutex(); $m.Dispose()
```

## Inno Setup ordering

From the 6.7.1 source (`Setup.MainFunc.pas`, `InitializeSetup` procedure): the
script's `InitializeSetup` runs first, then `while CheckForMutexes(ExpandedAppMutex)`
with the standard prompt, then the `SetupMutex` check. So the update run can wait
in `InitializeSetup` and arrive at the `AppMutex` check with the application gone,
and `AppMutex` can stay as a plain directive for interactive runs and the uninstaller
(`Setup.Uninstall.pas` checks it too). `/NOICONS` only has an effect with
`AllowNoIcons=yes` (`Setup.WizardForm.pas`). The uninstaller hands over to a copy in
`%TEMP%` and exits early, so its result has to be polled for.

## Results

| # | Run | Arguments | Result |
| --- | --- | --- | --- |
| 1 | Fresh install, real 0.1.0 | `/VERYSILENT /SUPPRESSMSGBOXES /SP- /NORESTART /NOICONS /DIR="<scratch>\inst folder" /LOG=...` | Exit 0, 1.5 s. 82 files (80 + `unins000.exe`, `unins000.dat`). HKCU entry with version 0.1.0 and the scratch location. No Start Menu group, no desktop link, nothing in `%LOCALAPPDATA%\Programs` |
| 2 | Installed application starts | `bin\pmm-app.exe --profile <scratch>\profile --game stellaris --screenshot <png>` | Exit 0. The capture shows the normal main window: games detected, Stellaris selected, read-only notes, Play disabled. Still 82 files afterwards. Scratch profile has `state.sqlite` and a log; `keep-me.txt` added |
| 3 | **Update 0.1.0 to 0.1.1, mutex held for 8 s** | exactly `/SILENT /SP- /NORESTART /PMMUPDATE=1` | Mutex released at 14:00:24.28. Log: `Update run: waiting` at 14:00:16.79, `the application is not running` at 14:00:24.45, installation started 14:00:24.68. Exit 0 after 9.4 s, no prompt. Version 0.1.1 in the same folder (remembered, no `/DIR`), still `unins000.exe`, no Start Menu group (the earlier `/NOICONS` is remembered). Marker: stand-in started at 14:00:25 from `bin\` with no arguments. Planted `bin\stale-old.dll` and `qml\StaleModule\` are gone. `keep-me.txt` and `state.sqlite` unchanged |
| 4 | **Timeout**, mutex held for 75 s | update arguments + `/SUPPRESSMSGBOXES /LOG=...` | Exit 1 after 63.6 s (240 polls of 250 ms plus timer slack). Version stays 0.1.1, no relaunch. The log records the message, see below |
| 5 | Interactive prompt, mutex held | `/VERYSILENT /SUPPRESSMSGBOXES /SP- /NORESTART /LOG=...` (no `/PMMUPDATE`) | Exit 1 in 0.2 s; the standard text reached immediately, no waiting, see below |
| 6 | Uninstall, mutex held | `unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=...` | Exit 1; everything still installed; standard text in the log, see below |
| 7 | Plain silent upgrade to 0.1.2, no mutex | `/VERYSILENT /SUPPRESSMSGBOXES /SP- /NORESTART /LOG=...` | Exit 0, 1.3 s, version 0.1.2. **No relaunch** (marker unchanged), since this is not an update run |
| 8 | Update run, no mutex | `/SILENT /SP- /NORESTART /PMMUPDATE=1 /LOG=...` | Exit 0, 1.6 s. Wait ended immediately. Relaunch recorded at 14:02:08 |
| 9 | Real 0.1.0 over the stand-in tree, then start | `/VERYSILENT ...`, then `pmm-app --profile <scratch>\profile --game eu4 --screenshot <png>` | Exit 0, exit 0: an in-place upgrade gives a working application |
| 10 | Uninstall | `unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=...` | Exit 0. Folder gone, HKCU entry gone. `<scratch>\profile\keep-me.txt` and `state.sqlite` still there |
| 11 | Smoke test, as in the workflow | `pwsh packaging/windows/smoke-test-installer.ps1 -Installer <setup> -Version 0.1.0` | Exit 0: "Installed 0.1.0 with 82 files", "Uninstalled; the folder and the HKCU entry are gone." |
| 12 | Smoke test must fail | `-Version 9.9.9`; and once with the application installed | Exit 1 with "uninstall entry has version '0.1.0', expected '9.9.9'", leaving nothing behind; exit 1 with "Paradox Mod Manager is installed for this user; uninstall it first.", leaving the installation alone |
| 13 | Compiler missing | `ISCC=C:\does\not\exist\ISCC.exe`, the build script | Exit 1: "ISCC.exe not found. Install Inno Setup 6 (https://jrsoftware.org/isdl.php or `choco install innosetup`) or set the ISCC environment variable to its full path." |

### What the user sees

Boxes were suppressed in these runs so nothing modal opened on the desktop; Inno
Setup logs the text of a suppressed box. Run 4, update timed out:

```text
Defaulting to Cancel for suppressed message box (Retry/Cancel):
Paradox Mod Manager is still running after 60 seconds, so the update could not be installed. Your current version has not been changed.

Close Paradox Mod Manager (if no window is visible, end pmm-app.exe in Task Manager), then click Retry. Click Cancel to stop and install the update later.
```

Run 5, a normal install with the application running:

```text
Defaulting to Cancel for suppressed message box (OK/Cancel):
Setup has detected that Paradox Mod Manager is currently running.

Please close all instances of it now, then click OK to continue, or Cancel to exit.
```

Run 6, the uninstaller:

```text
Defaulting to Cancel for suppressed message box (OK/Cancel):
Uninstall has detected that Paradox Mod Manager is currently running.

Please close all instances of it now, then click OK to continue, or Cancel to exit.
```

## Residue check

After the last run: no `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall`
entry for the application, no `%LOCALAPPDATA%\Programs\ParadoxModManager`, no Start
Menu group, no desktop link, no `pmm-app` or setup process. The real
`%LOCALAPPDATA%\ParadoxModManager` still has the modification time from before the
runs. The real application was never started against the standard profile.

An uninstall leaves a folder `%TEMP%\is-*-uninstall.tmp` with two small temp files
for a while. Those from these runs were removed by hand.

## Not verified

- The wizard itself: license page, task page, the "launch" checkbox, the look of
  the three boxes above, and Retry after a timeout. They need clicks on a visible
  window, which these runs avoid on purpose.
- The update run with the real `pmm-app` holding the real mutex and quitting; the
  mutex was held by a stand-in process and the relaunch recorded by a stand-in
  executable. That needs the updater change merged.
- Inno Setup 6.7.1 as on the runner image (6.7.3 here), and the `Release` workflow:
  the workflow cannot run before it is pushed. Its bash steps were run locally
  against a stub `gh`, see below.

## Release workflow, what could be checked

- `actionlint` 1.7.12 (release archive digest checked against the GitHub API) on
  `ci.yml` and `release.yml`: no findings. `shellcheck` is not installed here, so
  actionlint's shell checks did not run.
- The bash steps of the validate and publish jobs were extracted from `release.yml`
  and run with a stub `gh` that logs each call and reaches nothing: 27 of 27 checks
  passed. Refused as intended: a bad format, a pre-release suffix, a mismatch with
  `project(VERSION)`, a non-owner, a commit not on main, an existing tag, an existing
  release, a wrong tag target. The publish sequence ran in the order leftover drafts
  removed, draft created, assets uploaded, assets compared, draft flipped.
  `SHA256SUMS.txt` has lowercase hex, two spaces, LF, four lines, and does not list
  itself. Notes come from the changelog section, or the generated notes with a
  warning, and end with the fixed note.
- Against the real repository (read-only): `compare/main...<main HEAD>` gives
  `identical`, `compare/main...<older commit>` gives `behind`, which the check
  accepts.
- `cmake/Packaging.cmake` on Linux (WSL, GCC 16, without Qt): configures, builds and
  makes `ParadoxModManager-0.1.0-linux-x86_64.tar.gz`; there is no `installer` target.
