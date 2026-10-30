# Installs a built setup.exe silently into a temporary folder, checks the result
# and uninstalls it again. Used by .github/workflows/release.yml; safe to run
# locally as long as Paradox Mod Manager is not installed (it refuses otherwise,
# because the test shares the application's fixed uninstall registration).
#
#   pwsh packaging/windows/smoke-test-installer.ps1 -Installer <setup.exe> -Version <x.y.z>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Installer,
    [Parameter(Mandatory)] [string] $Version
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

function Fail([string] $message) {
    throw "smoke test: $message"
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

# The uninstaller hands over to a copy of itself in %TEMP% and exits early, so
# the result has to be polled for. Returns whether everything is gone.
function Invoke-Uninstall([string] $installDir) {
    $code = Invoke-Silently (Join-Path $installDir 'unins000.exe') @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'
    )
    if ($code -ne 0) {
        Write-Host "uninstaller exited with code $code"
    }
    $deadline = (Get-Date).AddSeconds(120)
    while (((Test-Path -LiteralPath $installDir) -or (Test-Path -LiteralPath $uninstallKey)) -and
           ((Get-Date) -lt $deadline)) {
        Start-Sleep -Milliseconds 500
    }
    return -not ((Test-Path -LiteralPath $installDir) -or (Test-Path -LiteralPath $uninstallKey))
}

if (Test-Path -LiteralPath $uninstallKey) {
    Fail 'Paradox Mod Manager is installed for this user; uninstall it first.'
}
$setup = (Resolve-Path -LiteralPath $Installer).Path

$work = Join-Path ([IO.Path]::GetTempPath()) "pmm-installer-smoke-$PID"
$installDir = Join-Path $work 'install'
$log = Join-Path $work 'setup.log'
New-Item -ItemType Directory -Force -Path $work | Out-Null

try {
    $code = Invoke-Silently $setup @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/SP-', '/NORESTART', '/NOICONS',
        "/DIR=`"$installDir`"", "/LOG=`"$log`""
    )
    if ($code -ne 0) {
        Get-Content -LiteralPath $log -ErrorAction SilentlyContinue | Write-Host
        Fail "setup exited with code $code"
    }

    foreach ($file in $expectedFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $installDir $file))) {
            Fail "$file is missing from the installation"
        }
    }

    if (-not (Test-Path -LiteralPath $uninstallKey)) {
        Fail 'no uninstall entry in HKCU'
    }
    $displayVersion = (Get-ItemProperty -LiteralPath $uninstallKey).DisplayVersion
    if ($displayVersion -ne $Version) {
        Fail "uninstall entry has version '$displayVersion', expected '$Version'"
    }
    if (Test-Path -LiteralPath $startMenuGroup) {
        Fail '/NOICONS still created a Start Menu folder'
    }
    Write-Host "Installed $Version with $((Get-ChildItem -Recurse -File $installDir).Count) files."

    if (-not (Invoke-Uninstall $installDir)) {
        Fail "the uninstaller left $installDir or the HKCU entry behind"
    }
    Write-Host 'Uninstalled; the folder and the HKCU entry are gone.'
}
finally {
    # A failed check must not leave an installation registered.
    if (Test-Path -LiteralPath (Join-Path $installDir 'unins000.exe')) {
        $null = Invoke-Uninstall $installDir
    }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
