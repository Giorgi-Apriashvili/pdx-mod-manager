# Installs a built setup.exe silently into a temporary folder, checks the result
# and uninstalls it again. Also: a first install leaves another program's bin,
# plugins and qml alone, an upgrade removes what the new version does not ship, and
# an upgrade that fails leaves the installed version working. The application itself
# is never started, so an update run (/PMMUPDATE=1) is not part of this test.
#
# Used by .github/workflows/release.yml; safe to run locally as long as Paradox Mod
# Manager is not installed (it refuses otherwise, because the test shares the
# application's fixed uninstall registration).
#
#   pwsh packaging/windows/smoke-test-installer.ps1 -Installer <setup.exe> -Version <x.y.z> [-WorkDir <empty folder>]
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Installer,
    [Parameter(Mandatory)] [string] $Version,
    [string] $WorkDir = (Join-Path ([IO.Path]::GetTempPath()) "pmm-installer-smoke-$PID")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$appId = '{25EA6705-9979-4D3F-AF89-BE3398B30CA0}_is1'
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$appId"
$startMenuGroup = Join-Path ([Environment]::GetFolderPath('Programs')) 'Paradox Mod Manager'
$expectedFiles = @(
    'unins000.exe', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses\LGPL-3.0.txt',
    'bin\pmm-app.exe', 'bin\pmm-steam-helper.exe', 'bin\Qt6Core.dll', 'bin\Qt6Quick.dll',
    'bin\qt.conf', 'plugins\platforms\qwindows.dll', 'qml\QtQuick\Controls\Basic\qmldir'
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

function Invoke-Setup([string] $name, [string[]] $arguments) {
    $log = Join-Path $work "$name.log"
    $code = Invoke-Silently $setup (@(
            '/VERYSILENT', '/SUPPRESSMSGBOXES', '/SP-', '/NORESTART', '/NOICONS', "/LOG=`"$log`""
        ) + $arguments)
    if ($code -ne 0) {
        Write-Host "setup ($name) exited with code $code; log: $log"
    }
    return $code
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

# An upgrade with the file locked, by something Setup is not allowed to close. The
# previous version must come through complete, with its old files as a witness.
function Test-FailedUpgrade([string] $name, [string] $lockedFile) {
    $locked = [IO.File]::Open((Join-Path $installDir $lockedFile), 'Open', 'Read', 'None')
    try {
        $code = Invoke-Setup $name @('/NOCLOSEAPPLICATIONS')
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

if (Test-Path -LiteralPath $uninstallKey) {
    Fail 'Paradox Mod Manager is installed for this user; uninstall it first.'
}
$setup = (Resolve-Path -LiteralPath $Installer).Path

$work = $WorkDir
$installDir = Join-Path $work 'install'
$foreignDir = Join-Path $work 'foreign'
New-Item -ItemType Directory -Force -Path $work | Out-Null

try {
    # First install into a folder that holds another program's files.
    New-Files $foreignDir $foreignFiles
    $code = Invoke-Setup 'first-install-foreign' @("/DIR=`"$foreignDir`"")
    Check ($code -eq 0) 'first install into an existing folder succeeds'
    $lost = Find-Files $foreignDir $foreignFiles $true
    Check ($lost.Count -eq 0) "first install keeps the folder's own files (lost: $lost)"
    if (Test-Path -LiteralPath (Join-Path $foreignDir 'unins000.exe')) {
        Check (Invoke-Uninstall $foreignDir (Join-Path $foreignDir 'bin\pmm-app.exe')) 'uninstall from that folder finishes'
        $lost = Find-Files $foreignDir $foreignFiles $true
        Check ($lost.Count -eq 0) "uninstall keeps the folder's own files (lost: $lost)"
    }

    # First install into a new folder.
    $code = Invoke-Setup 'first-install' @("/DIR=`"$installDir`"")
    if ($code -ne 0) {
        Fail "setup exited with code $code"
    }
    $missing = Find-Files $installDir $expectedFiles $true
    Check ($missing.Count -eq 0) "the expected files are installed (missing: $missing)"
    if (-not (Test-Path -LiteralPath $uninstallKey)) {
        Fail 'no uninstall entry in HKCU'
    }
    $displayVersion = (Get-ItemProperty -LiteralPath $uninstallKey).DisplayVersion
    Check ($displayVersion -eq $Version) "the uninstall entry has version '$displayVersion', expected '$Version'"
    Check (-not (Test-Path -LiteralPath $startMenuGroup)) '/NOICONS creates no Start Menu folder'
    Write-Host "Installed $Version with $((Get-ChildItem -Recurse -File $installDir).Count) files."

    # A failed upgrade, once while copying and once while moving the old files away.
    New-Files $installDir $staleFiles
    Test-FailedUpgrade 'upgrade-locked-outside' 'LICENSE'
    Test-FailedUpgrade 'upgrade-locked-inside' 'bin\qt.conf'

    # An upgrade that works, over the same folder: the setup finds it by itself.
    $code = Invoke-Setup 'upgrade' @()
    Check ($code -eq 0) 'the upgrade succeeds'
    $missing = Find-Files $installDir $expectedFiles $true
    Check ($missing.Count -eq 0) "the upgrade installs the expected files (missing: $missing)"
    $left = Find-Files $installDir $staleFiles $false
    Check ($left.Count -eq 0) "the upgrade removes what the new version does not ship (left: $left)"
    Check (-not (Test-Path -LiteralPath (Join-Path $installDir $upgradeBackup))) 'the upgrade leaves no backup'

    if (-not (Invoke-Uninstall $installDir)) {
        Fail "the uninstaller left $installDir or the HKCU entry behind"
    }
    Write-Host 'Uninstalled; the folder and the HKCU entry are gone.'

    if ($failures.Count -gt 0) {
        Fail "$($failures.Count) check(s) failed:`n  $($failures -join "`n  ")"
    }
}
finally {
    # A failed check must not leave an installation registered.
    foreach ($dir in $foreignDir, $installDir) {
        if (Test-Path -LiteralPath (Join-Path $dir 'unins000.exe')) {
            $null = Invoke-Uninstall $dir (Join-Path $dir 'unins000.exe')
        }
    }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
