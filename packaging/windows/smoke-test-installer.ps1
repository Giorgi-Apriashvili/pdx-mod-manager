# Installs a built setup.exe silently into temporary folders, checks the result and
# uninstalls it again: a first install, one into a folder that holds another
# program's files and one into a folder with a non-ASCII name; the single Start Menu
# shortcut and the desktop shortcut; the refusal to replace a newer installed
# version; upgrades that fail and leave the installed version working, also in an
# update run, which then starts that version again; a working upgrade; and the
# update run exactly as the application starts it, waiting for the application's
# mutex and starting bin\pmm-app.exe. The installer's side of the update contract is
# in packaging/update-contract.txt.
#
# The update runs start the installed pmm-app.exe for real, with a window and the
# standard profile of the user who runs the test; it is stopped again. So the script
# runs only where that does not matter: on a GitHub runner (GITHUB_ACTIONS=true), or
# with -DisposableMachine on a machine that can be thrown away. It also refuses when
# Paradox Mod Manager is installed or running, or one of its shortcuts exists,
# because the test uses the application's fixed uninstall registration, mutex and
# shortcut names.
#
# Used by .github/workflows/release.yml:
#
#   pwsh packaging/windows/smoke-test-installer.ps1 -Installer <setup.exe> -Version <x.y.z> [-WorkDir <empty folder>] [-DisposableMachine]
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Installer,
    [Parameter(Mandatory)] [string] $Version,
    [string] $WorkDir = (Join-Path ([IO.Path]::GetTempPath()) "pmm-installer-smoke-$PID"),
    [switch] $DisposableMachine
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$appId = '{25EA6705-9979-4D3F-AF89-BE3398B30CA0}_is1'
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$appId"
$mutexName = 'Local\ParadoxModManagerRunning'
# Exactly what the application passes (update_contract: installer_arguments).
$updateArguments = @('/SILENT', '/SP-', '/NORESTART', '/PMMUPDATE=1')
$programsDir = [Environment]::GetFolderPath('Programs')
$startMenuShortcut = Join-Path $programsDir 'Paradox Mod Manager.lnk'
# Where builds before 0.1.0 put their shortcut; the setup removes it.
$oldStartMenuGroup = Join-Path $programsDir 'Paradox Mod Manager'
$desktopShortcut = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Paradox Mod Manager.lnk'
$expectedFiles = @(
    'unins000.exe', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses\LGPL-3.0.txt',
    'bin\pmm-app.exe', 'bin\pmm-steam-helper.exe', 'bin\Qt6Core.dll', 'bin\Qt6Quick.dll',
    'bin\qt.conf', 'plugins\platforms\qwindows.dll', 'plugins\tls\qschannelbackend.dll',
    'qml\QtQuick\Controls\Basic\qmldir'
)
# What an earlier version could have left in the folders an upgrade replaces.
$staleFiles = @('bin\stale-old.dll', 'plugins\stale-old.dll', 'qml\StaleModule\qmldir')
# Another program's files, in a folder that the user chose for a first install.
$foreignFiles = @('keep.txt', 'bin\my-own-tool.txt', 'plugins\my-plugin.txt', 'qml\my-notes.txt')
$upgradeBackup = 'pmm-upgrade-backup'
$failures = [System.Collections.Generic.List[string]]::new()

function Fail([string] $message) {
    throw "smoke test: $message"
}

function Check([bool] $ok, [string] $message) {
    if ($ok) {
        Write-Host "ok: $message"
    }
    else {
        Write-Host "FAILED: $message"
        $failures.Add($message)
    }
}

function New-Files([string] $root, [string[]] $files) {
    foreach ($file in $files) {
        New-Item -ItemType File -Force -Path (Join-Path $root $file) | Out-Null
    }
}

# The files for which existing differs from $exist; always an array.
function Find-Files([string] $root, [string[]] $files, [bool] $exist) {
    return , @($files | Where-Object { (Test-Path -LiteralPath (Join-Path $root $_)) -ne $exist })
}

function Test-MutexExists {
    $mutex = $null
    if ([System.Threading.Mutex]::TryOpenExisting($mutexName, [ref] $mutex)) {
        $mutex.Dispose() # a handle kept here would keep the mutex alive
        return $true
    }
    return $false
}

function Invoke-Silently([string] $file, [string[]] $arguments) {
    # WaitForExit, not Start-Process -Wait: the latter also waits for descendants.
    $process = Start-Process -FilePath $file -ArgumentList $arguments -PassThru
    $null = $process.Handle # keeps the exit code available after the process ends
    if (-not $process.WaitForExit(300000)) {
        $process.Kill()
        Fail "$file did not finish within 5 minutes"
    }
    return $process.ExitCode
}

# /NOICONS unless -WithIcons: most runs leave the Start Menu alone.
function Invoke-Setup([string] $name, [string[]] $arguments, [switch] $WithIcons) {
    $log = Join-Path $work "$name.log"
    $options = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/SP-', '/NORESTART', "/LOG=`"$log`"")
    if (-not $WithIcons) {
        $options += '/NOICONS'
    }
    $code = Invoke-Silently $setup ($options + $arguments)
    if ($code -ne 0) {
        Write-Host "setup ($name) exited with code $code; log: $log"
    }
    return $code
}

function Get-SetupLog([string] $name) {
    $log = Join-Path $work "$name.log"
    if (Test-Path -LiteralPath $log) {
        return Get-Content -LiteralPath $log -Raw
    }
    return ''
}

# The uninstaller hands over to a copy of itself in %TEMP% and exits early, so
# the result has to be polled for. Returns whether $gone and the entry are gone.
function Invoke-Uninstall([string] $installDir, [string] $gone = $installDir) {
    $code = Invoke-Silently (Join-Path $installDir 'unins000.exe') @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'
    )
    if ($code -ne 0) {
        Write-Host "uninstaller exited with code $code"
    }
    $deadline = (Get-Date).AddSeconds(120)
    while (((Test-Path -LiteralPath $gone) -or (Test-Path -LiteralPath $uninstallKey)) -and
           ((Get-Date) -lt $deadline)) {
        Start-Sleep -Milliseconds 500
    }
    return -not ((Test-Path -LiteralPath $gone) -or (Test-Path -LiteralPath $uninstallKey))
}

# Processes started from below $dir (the application and its Steam helper).
function Get-InstalledProcesses([string] $dir) {
    $prefix = $dir.TrimEnd('\') + '\'
    return , @(Get-Process | Where-Object {
            $path = $null
            try { $path = $_.Path } catch { $path = $null }
            $path -and $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
        })
}

function Wait-AppStarted([string] $dir) {
    $exe = Join-Path $dir 'bin\pmm-app.exe'
    $deadline = (Get-Date).AddSeconds(30)
    do {
        $found = @(Get-Process -Name 'pmm-app' -ErrorAction SilentlyContinue |
                Where-Object { $_.Path -eq $exe })
        if ($found.Count -gt 0) {
            return $true
        }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    return $false
}

function Stop-InstalledApp([string] $dir) {
    foreach ($process in (Get-InstalledProcesses $dir)) {
        Write-Host "stopping $($process.ProcessName) ($($process.Id)) started by the setup"
        $process.Kill($true)
        $null = $process.WaitForExit(30000)
    }
}

function Set-InstalledVersion([string] $value) {
    Set-ItemProperty -LiteralPath $uninstallKey -Name 'DisplayVersion' -Value $value
}

function Get-InstalledVersion {
    return (Get-ItemProperty -LiteralPath $uninstallKey).DisplayVersion
}

# An upgrade with the file locked, by something Setup is not allowed to close. The
# previous version must come through complete, with its old files as a witness.
function Test-FailedUpgrade([string] $name, [string] $lockedFile, [string[]] $arguments = @()) {
    $locked = [IO.File]::Open((Join-Path $installDir $lockedFile), 'Open', 'Read', 'None')
    try {
        $code = Invoke-Setup $name (@('/NOCLOSEAPPLICATIONS') + $arguments)
    }
    finally {
        $locked.Dispose()
    }
    Check ($code -ne 0) "$name`: setup reports the failure"
    $missing = Find-Files $installDir ($expectedFiles + $staleFiles) $true
    Check ($missing.Count -eq 0) "$name`: the previous version is complete (missing: $missing)"
    Check (-not (Test-Path -LiteralPath (Join-Path $installDir $upgradeBackup))) "$name`: no backup is left"
    Check (Test-Path -LiteralPath $uninstallKey) "$name`: the uninstall entry stays"
}

# A stand-in for the running application: another process that holds the mutex
# for $seconds and then exits. Returns once the mutex exists.
function Start-MutexHolder([int] $seconds) {
    $command = "`$m = [System.Threading.Mutex]::new(`$false, '$mutexName'); " +
        "Start-Sleep -Seconds $seconds; `$m.Dispose()"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $holder = Start-Process -FilePath (Get-Process -Id $PID).Path -PassThru -WindowStyle Hidden `
        -ArgumentList @('-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded)
    $null = $holder.Handle
    $deadline = (Get-Date).AddSeconds(30)
    while (-not (Test-MutexExists)) {
        if ($holder.HasExited -or (Get-Date) -gt $deadline) {
            Fail 'the stand-in for the application did not create the mutex'
        }
        Start-Sleep -Milliseconds 100
    }
    return $holder
}

if (-not ($env:GITHUB_ACTIONS -eq 'true' -or $DisposableMachine)) {
    Fail ('this test starts the installed application with the standard profile of this user; ' +
        'run it on a GitHub runner, or with -DisposableMachine on a machine that can be thrown away.')
}
if (Test-Path -LiteralPath $uninstallKey) {
    Fail 'Paradox Mod Manager is installed for this user; uninstall it first.'
}
if (Test-MutexExists) {
    Fail 'Paradox Mod Manager is running; close it first.'
}
foreach ($path in $startMenuShortcut, $oldStartMenuGroup, $desktopShortcut) {
    if (Test-Path -LiteralPath $path) {
        Fail "$path exists; the test would replace or delete it."
    }
}
$parsedVersion = [version] $Version
$setup = (Resolve-Path -LiteralPath $Installer).Path

# %TEMP% on a runner has 8.3 short names (RUNNER~1). Long names everywhere make the
# folders of the installation and of its processes compare equal.
Add-Type -Namespace PmmSmokeTest -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern uint GetLongPathNameW(string shortPath, System.Text.StringBuilder longPath, uint size);
'@
function Get-LongPath([string] $path) {
    $buffer = [System.Text.StringBuilder]::new(32768)
    $length = [PmmSmokeTest.Native]::GetLongPathNameW($path, $buffer, [uint32] $buffer.Capacity)
    if ($length -eq 0 -or $length -ge $buffer.Capacity) {
        Fail "could not resolve the long name of $path"
    }
    return $buffer.ToString()
}

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$work = Get-LongPath (Resolve-Path -LiteralPath $WorkDir).Path
$installDir = Join-Path $work 'install'
$foreignDir = Join-Path $work 'foreign'
# Latin letters with diaeresis and two CJK characters, written as escapes so that
# this file stays ASCII.
$unicodeDir = Join-Path $work "install-`u{00DC}nic`u{00F6}de-`u{65E5}`u{672C}"
# The package has only Qt's windows platform plugin, so the application the update
# runs start opens its window on the runner's desktop.
Remove-Item -LiteralPath Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue

try {
    # First install into a folder that holds another program's files.
    New-Files $foreignDir $foreignFiles
    $code = Invoke-Setup 'first-install-foreign' @("/DIR=`"$foreignDir`"")
    Check ($code -eq 0) 'first install into an existing folder succeeds'
    $lost = Find-Files $foreignDir $foreignFiles $true
    Check ($lost.Count -eq 0) "first install keeps the folder's own files (lost: $lost)"
    Check (-not (Test-Path -LiteralPath $startMenuShortcut)) '/NOICONS creates no Start Menu shortcut'
    if (Test-Path -LiteralPath (Join-Path $foreignDir 'unins000.exe')) {
        Check (Invoke-Uninstall $foreignDir (Join-Path $foreignDir 'bin\pmm-app.exe')) 'uninstall from that folder finishes'
        $lost = Find-Files $foreignDir $foreignFiles $true
        Check ($lost.Count -eq 0) "uninstall keeps the folder's own files (lost: $lost)"
    }

    # First install into a folder whose name is not ASCII.
    $code = Invoke-Setup 'first-install-unicode' @("/DIR=`"$unicodeDir`"")
    Check ($code -eq 0) 'first install into a folder with a non-ASCII name succeeds'
    $missing = Find-Files $unicodeDir $expectedFiles $true
    Check ($missing.Count -eq 0) "the non-ASCII folder has the expected files (missing: $missing)"
    if (Test-Path -LiteralPath $uninstallKey) {
        # By its name only: the temporary folder may be written with 8.3 short names.
        $location = "$((Get-ItemProperty -LiteralPath $uninstallKey).InstallLocation)".TrimEnd('\')
        Check ((Split-Path -Leaf $location) -eq (Split-Path -Leaf $unicodeDir) -and
            (Test-Path -LiteralPath (Join-Path $location 'unins000.exe'))) "the uninstall entry names the non-ASCII folder ('$location')"
    }
    if (Test-Path -LiteralPath (Join-Path $unicodeDir 'unins000.exe')) {
        Check (Invoke-Uninstall $unicodeDir) 'uninstall from the non-ASCII folder removes it and the entry'
    }

    # First install into a new folder, with both shortcuts.
    $code = Invoke-Setup 'first-install' @("/DIR=`"$installDir`"", '/TASKS=desktopicon') -WithIcons
    if ($code -ne 0) {
        Fail "setup exited with code $code"
    }
    $missing = Find-Files $installDir $expectedFiles $true
    Check ($missing.Count -eq 0) "the expected files are installed (missing: $missing)"
    if (-not (Test-Path -LiteralPath $uninstallKey)) {
        Fail 'no uninstall entry in HKCU'
    }
    $displayVersion = Get-InstalledVersion
    Check ($displayVersion -eq $Version) "the uninstall entry has version '$displayVersion', expected '$Version'"
    Check (Test-Path -LiteralPath $startMenuShortcut) "the Start Menu has the shortcut $startMenuShortcut"
    if (Test-Path -LiteralPath $startMenuShortcut) {
        $shell = New-Object -ComObject WScript.Shell
        $target = $shell.CreateShortcut($startMenuShortcut).TargetPath
        Check ($target.EndsWith('\install\bin\pmm-app.exe', [StringComparison]::OrdinalIgnoreCase) -and
            (Test-Path -LiteralPath $target)) "the Start Menu shortcut starts bin\pmm-app.exe ('$target')"
    }
    Check (-not (Test-Path -LiteralPath $oldStartMenuGroup)) 'there is no Start Menu folder'
    Check (Test-Path -LiteralPath $desktopShortcut) 'the desktop shortcut is created when its task is chosen'
    Write-Host "Installed $Version with $((Get-ChildItem -Recurse -File $installDir).Count) files."
    # As if the user had deleted it; only an upgrade that is not an update run brings it back.
    Remove-Item -LiteralPath $desktopShortcut -Force -ErrorAction SilentlyContinue

    # A newer installed version is never replaced, also not in an update run. The
    # stale files are the witness that nothing was touched.
    New-Files $installDir $staleFiles
    $newerVersion = "$($parsedVersion.Major + 1).0.0"
    Set-InstalledVersion $newerVersion
    foreach ($run in @(@{ Name = 'downgrade'; Arguments = @() },
                       @{ Name = 'downgrade-update-run'; Arguments = @('/PMMUPDATE=1') })) {
        $code = Invoke-Setup $run.Name $run.Arguments
        Check ($code -eq 1) "$($run.Name): setup over version $newerVersion exits with code 1 (got $code)"
        $log = Get-SetupLog $run.Name
        Check ($log.Contains("Version $newerVersion of Paradox Mod Manager is already installed, which is newer than this setup ($Version). Setup will not replace it.")) "$($run.Name): the log has the refusal"
        $missing = Find-Files $installDir ($expectedFiles + $staleFiles) $true
        Check ($missing.Count -eq 0) "$($run.Name): the installed files are untouched (missing: $missing)"
        Check ((Get-InstalledVersion) -eq $newerVersion) "$($run.Name): the uninstall entry keeps version $newerVersion"
    }
    Start-Sleep -Seconds 2
    Check ((Get-InstalledProcesses $installDir).Count -eq 0) 'a refused update run starts nothing'
    Stop-InstalledApp $installDir
    Set-InstalledVersion $Version

    # A failed upgrade, once while copying and once while moving the old files away.
    Test-FailedUpgrade 'upgrade-locked-outside' 'LICENSE'
    Test-FailedUpgrade 'upgrade-locked-inside' 'bin\qt.conf'

    # A failed update run: the application quit for it, so the previous version is
    # started again.
    Test-FailedUpgrade 'update-run-locked' 'LICENSE' @('/PMMUPDATE=1')
    Check (Wait-AppStarted $installDir) 'update-run-locked: the previous version is started again from bin'
    Check ((Get-SetupLog 'update-run-locked').Contains('Update run: started the previous version again.')) 'update-run-locked: the log has the restart'
    Stop-InstalledApp $installDir

    # An upgrade that works, from an older version, over the same folder: the setup
    # finds it by itself, and removes the Start Menu folder of earlier builds.
    New-Files $oldStartMenuGroup @('Paradox Mod Manager.lnk')
    if ($parsedVersion -gt [version] '0.0.0') {
        Set-InstalledVersion '0.0.0'
    }
    $code = Invoke-Setup 'upgrade' @()
    Check ($code -eq 0) 'the upgrade succeeds'
    $missing = Find-Files $installDir $expectedFiles $true
    Check ($missing.Count -eq 0) "the upgrade installs the expected files (missing: $missing)"
    $left = Find-Files $installDir $staleFiles $false
    Check ($left.Count -eq 0) "the upgrade removes what the new version does not ship (left: $left)"
    Check (-not (Test-Path -LiteralPath (Join-Path $installDir $upgradeBackup))) 'the upgrade leaves no backup'
    Check ((Get-InstalledVersion) -eq $Version) "the upgrade records version $Version"
    Check (-not (Test-Path -LiteralPath $oldStartMenuGroup)) 'the upgrade removes the Start Menu folder of earlier builds'
    Check (Test-Path -LiteralPath $startMenuShortcut) 'the Start Menu shortcut stays'
    Check (Test-Path -LiteralPath $desktopShortcut) 'an upgrade that is not an update run brings the desktop shortcut back'
    Check ((Get-InstalledProcesses $installDir).Count -eq 0) 'an upgrade that is not an update run starts nothing'
    Remove-Item -LiteralPath $desktopShortcut -Force -ErrorAction SilentlyContinue

    # The update run as the application starts it: exactly its arguments, while the
    # application still holds the mutex for a few seconds after starting the setup.
    New-Files $installDir $staleFiles
    $logsBefore = @(Get-ChildItem -LiteralPath ([IO.Path]::GetTempPath()) -Filter 'Setup Log *.txt' -File |
            ForEach-Object FullName)
    $holder = Start-MutexHolder 5
    $started = Get-Date
    $code = Invoke-Silently $setup $updateArguments
    $elapsed = ((Get-Date) - $started).TotalSeconds
    Check ($code -eq 0) "update run: exits with code 0 (got $code)"
    Check ($holder.HasExited -and $elapsed -ge 2) "update run: waited for the mutex ($([math]::Round($elapsed, 1)) s)"
    $newLogs = @(Get-ChildItem -LiteralPath ([IO.Path]::GetTempPath()) -Filter 'Setup Log *.txt' -File |
            Where-Object { $logsBefore -notcontains $_.FullName } | Sort-Object LastWriteTime)
    $log = if ($newLogs.Count -gt 0) { Get-Content -LiteralPath $newLogs[-1].FullName -Raw } else { '' }
    foreach ($line in 'Update run: waiting for the application to exit.',
                      'Update run: the application is not running.',
                      'Upgrade: the previous program files are in pmm-upgrade-backup.') {
        Check ($log.Contains($line)) "update run: the log has '$line'"
    }
    Check (Wait-AppStarted $installDir) 'update run: bin\pmm-app.exe is started'
    $left = Find-Files $installDir $staleFiles $false
    Check ($left.Count -eq 0) "update run: replaces the program files (left: $left)"
    Check (-not (Test-Path -LiteralPath $desktopShortcut)) 'update run: does not bring back a deleted desktop shortcut'
    Stop-InstalledApp $installDir

    if (-not (Invoke-Uninstall $installDir)) {
        Fail "the uninstaller left $installDir or the HKCU entry behind"
    }
    Write-Host 'Uninstalled; the folder and the HKCU entry are gone.'
    Check (-not (Test-Path -LiteralPath $startMenuShortcut)) 'uninstall removes the Start Menu shortcut'
    Check (-not (Test-Path -LiteralPath $desktopShortcut)) 'no desktop shortcut is left'

    if ($failures.Count -gt 0) {
        Fail "$($failures.Count) check(s) failed:`n  $($failures -join "`n  ")"
    }
}
finally {
    # A failed check must not leave an installation registered or running.
    foreach ($dir in $foreignDir, $unicodeDir, $installDir) {
        Stop-InstalledApp $dir
        if (Test-Path -LiteralPath (Join-Path $dir 'unins000.exe')) {
            $null = Invoke-Uninstall $dir (Join-Path $dir 'unins000.exe')
        }
    }
    foreach ($path in $startMenuShortcut, $oldStartMenuGroup, $desktopShortcut) {
        Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
