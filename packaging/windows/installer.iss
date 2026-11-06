; Windows installer (Inno Setup 6): per-user, no administrator rights. See
; docs/packaging.md. Built from the staged `cmake --install` tree by
;
;   cmake --build <build dir> --target installer
;
; which runs ISCC /DAppVersion=<x.y.z> /DStageDir=<stage> /DLicensePath=<file>
; /O<output dir> on this script (packaging/windows/build-installer.cmake).
;
; The update contract with the application, frozen since 0.1.0 because every
; released copy has its side compiled in (packaging/update-contract.txt, checked by
; scripts/check-update-contract.sh; docs/updates.md):
;   - the application holds the mutex below while it runs;
;   - it starts this installer with /SILENT /SP- /NORESTART /PMMUPDATE=1 and quits;
;   - an installed copy is recognised by ..\unins000.exe next to bin\;
;   - AppId, the per-user installation and bin\pmm-app.exe never change.

#if Ver < EncodeVer(6, 5, 2)
  #error Inno Setup 6.5.2 or newer is required (x64compatible, PNG wizard images)
#endif
#ifndef AppVersion
  #error Define AppVersion: ISCC /DAppVersion=x.y.z
#endif
#ifndef StageDir
  #error Define StageDir: ISCC /DStageDir=<cmake --install prefix>
#endif
#ifndef LicensePath
  #define LicensePath SourcePath + "license-page.rtf"
#endif

; Generated from packaging/icons, see docs/packaging.md ("Application icon").
#define SetupIconPath SourcePath + "app.ico"
#define WizardSmallImages SourcePath + "wizard-small\app-*.png"

#define AppDisplayName "Paradox Mod Manager"
#define AppExe "bin\pmm-app.exe"
#define AppRunningMutex "Local\ParadoxModManagerRunning"
#define AppUninstallKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\{25EA6705-9979-4D3F-AF89-BE3398B30CA0}_is1"
#define UpgradeBackupFolder "pmm-upgrade-backup"
#define ProjectUrl "https://github.com/Giorgi-Apriashvili/pdx-mod-manager"

[Setup]
; Never change AppId: it is what ties an upgrade to the existing installation.
AppId={{25EA6705-9979-4D3F-AF89-BE3398B30CA0}
AppName={#AppDisplayName}
AppVersion={#AppVersion}
AppVerName={#AppDisplayName} {#AppVersion}
AppPublisher=Giorgi Apriashvili
AppPublisherURL={#ProjectUrl}
AppSupportURL={#ProjectUrl}/issues
AppUpdatesURL={#ProjectUrl}/releases
AppCopyright=Copyright (c) 2026 Giorgi Apriashvili. MIT License.
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName={#AppDisplayName}
VersionInfoDescription={#AppDisplayName} setup

; {autopf} is %LOCALAPPDATA%\Programs in a per-user installation. An upgrade
; stays in the folder of the existing installation (UsePreviousAppDir default)
; and skips the folder page (DisableDirPage=auto default).
PrivilegesRequired=lowest
DefaultDirName={autopf}\ParadoxModManager
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1809, the minimum of Qt 6.10.
MinVersion=10.0.17763

; One Start Menu shortcut, no folder (see [Icons]).
DisableProgramGroupPage=yes
; Needed for /NOICONS to have an effect.
AllowNoIcons=yes
LicenseFile={#LicensePath}
UninstallDisplayName={#AppDisplayName}
UninstallDisplayIcon={app}\{#AppExe}

OutputBaseFilename=ParadoxModManager-{#AppVersion}-windows-x86_64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#SetupIconPath}
; One image per display scale; Setup picks the closest. Transparent, on the white
; header of the light-only modern style.
WizardSmallImageFile={#WizardSmallImages}
; A log in %TEMP% for every run: a failed update has no other trace.
SetupLogging=yes

; Running application. Inno Setup checks AppMutex after InitializeSetup, so an
; update run has already waited for the application to quit (see [Code]), and an
; interactive run gets the standard "close it, then OK" prompt. The uninstaller
; checks it too. CloseApplications covers whatever else holds a file (the Steam
; helper): a prompt when interactive, closed automatically when silent. Nothing
; is restarted that way: the update run relaunches the application itself.
AppMutex={#AppRunningMutex}
SetupMutex=ParadoxModManagerSetup
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
; %1 is the application's name; %% is a percent sign.
UninstalledAll=%1 was successfully removed from your computer.%n%nYour playsets, settings and backups stay in %%APPDATA%%\ParadoxModManager, and the logs and the cache in %%LOCALAPPDATA%%\ParadoxModManager, so a later installation finds them again. Delete those folders to remove them as well.
UninstalledMost=%1 was removed from your computer, but some of its files could not be removed. You can delete them by hand.%n%nYour playsets, settings and backups stay in %%APPDATA%%\ParadoxModManager, and the logs and the cache in %%LOCALAPPDATA%%\ParadoxModManager. Delete those folders to remove them as well.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Earlier builds put their shortcut into a Start Menu folder of its own.
Type: files; Name: "{autoprograms}\{#AppDisplayName}\{#AppDisplayName}.lnk"
Type: dirifempty; Name: "{autoprograms}\{#AppDisplayName}"

[UninstallDelete]
; Only there if an upgrade could not delete it (see [Code]).
Type: filesandordirs; Name: "{app}\{#UpgradeBackupFolder}"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; Directly in the Start Menu's program list. /NOICONS leaves it out.
Name: "{autoprograms}\{#AppDisplayName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin"; Check: not WizardNoIcons
Name: "{autodesktop}\{#AppDisplayName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin"; Tasks: desktopicon; Check: KeepDesktopIcon

[Run]
Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin"; Description: "{cm:LaunchProgram,{#AppDisplayName}}"; Flags: nowait postinstall skipifsilent; Check: not IsPmmUpdate
; The relaunch after an update run takes no arguments: the session that started the
; update cannot pass its own on.
Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin"; Flags: nowait runasoriginaluser; Check: IsPmmUpdate

[Code]
const
  UpdateWaitSeconds = 60;
  PollMilliseconds = 250;
  ReplacedFolderCount = 3;
  MoveAttempts = 20;
  MoveRetryMilliseconds = 500;

var
  AppDir: String;
  FolderMoved: array[0..2] of Boolean;
  InstallFinished: Boolean;
  // An update run that got past the wait: the application quit for it.
  AppQuitForUpdate: Boolean;

function IsPmmUpdate: Boolean;
begin
  Result := ExpandConstant('{param:PMMUPDATE|0}') = '1';
end;

// An update run refreshes the desktop shortcut, but does not bring back one the
// user deleted.
function KeepDesktopIcon: Boolean;
begin
  Result := not IsPmmUpdate or FileExists(ExpandConstant('{autodesktop}\{#AppDisplayName}.lnk'));
end;

// The version of this AppId's installation when it is newer than this setup.
// One that cannot be read or compared does not stop Setup.
function InstalledNewerVersion(var Installed: String): Boolean;
var
  InstalledPacked, ThisPacked: Int64;
begin
  Result := False;
  if not RegQueryStringValue(HKCU, '{#AppUninstallKey}', 'DisplayVersion', Installed) then
    Exit;
  Log('Installed version: ' + Installed);
  if not StrToVersion(Installed, InstalledPacked) or not StrToVersion('{#AppVersion}', ThisPacked) then
  begin
    Log('The installed version cannot be compared with {#AppVersion}.');
    Exit;
  end;
  Result := ComparePackedVersion(InstalledPacked, ThisPacked) > 0;
end;

function WaitForAppExit(Seconds: Integer): Boolean;
var
  Waited: Integer;
begin
  Waited := 0;
  while CheckForMutexes('{#AppRunningMutex}') and (Waited < Seconds * 1000) do
  begin
    Sleep(PollMilliseconds);
    Waited := Waited + PollMilliseconds;
  end;
  Result := not CheckForMutexes('{#AppRunningMutex}');
end;

function InitializeSetup: Boolean;
var
  Installed, Message: String;
begin
  // An older setup would put back files and settings the newer version has
  // changed. Silent and update runs end here too, with exit code 1.
  if InstalledNewerVersion(Installed) then
  begin
    SuppressibleMsgBox('Version ' + Installed + ' of {#AppDisplayName} is already installed, '
      + 'which is newer than this setup ({#AppVersion}). Setup will not replace it.',
      mbError, MB_OK, IDOK);
    Result := False;
    Exit;
  end;
  Result := True;
  if not IsPmmUpdate then
    Exit;

  Log('Update run: waiting for the application to exit.');
  Message := '{#AppDisplayName} is still running after '
    + IntToStr(UpdateWaitSeconds) + ' seconds, so the update could not be installed. '
    + 'Your current version has not been changed.' + #13#10#13#10
    + 'Close {#AppDisplayName}. If no window is visible, end {#AppDisplayName} (pmm-app.exe) '
    + 'in Task Manager. Then click Retry. Click Cancel to stop and install the update later.';
  while not WaitForAppExit(UpdateWaitSeconds) do
    if SuppressibleMsgBox(Message, mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
    begin
      Log('Update run: the application did not exit; nothing was changed.');
      Result := False;
      Exit;
    end;
  Log('Update run: the application is not running.');
  AppQuitForUpdate := True;
end;

// An upgrade replaces the Qt files of the previous version whole: a module or
// plugin the new version no longer ships must not be picked up. They are moved
// aside, not deleted, so a failed or cancelled upgrade can put them back, and only
// in a folder that really is this application's installation: a folder chosen for
// a first install can have its own bin, plugins or qml.
function ReplacedFolder(Index: Integer): String;
begin
  case Index of
    0: Result := 'bin';
    1: Result := 'plugins';
  else
    Result := 'qml';
  end;
end;

function LivePath(Index: Integer): String;
begin
  Result := AppDir + '\' + ReplacedFolder(Index);
end;

function BackupRoot: String;
begin
  Result := AppDir + '\{#UpgradeBackupFolder}';
end;

function BackupPath(Index: Integer): String;
begin
  Result := BackupRoot + '\' + ReplacedFolder(Index);
end;

function IsUpgradeOfThisFolder: Boolean;
var
  Location: String;
begin
  Result := FileExists(AppDir + '\unins000.exe')
    and RegQueryStringValue(HKCU, '{#AppUninstallKey}', 'InstallLocation', Location)
    and (CompareText(RemoveBackslashUnlessRoot(Location), RemoveBackslashUnlessRoot(AppDir)) = 0);
end;

function RestoreMovedFolders: Boolean;
var
  I: Integer;
begin
  Result := True;
  for I := 0 to ReplacedFolderCount - 1 do
    if FolderMoved[I] then
    begin
      if DirExists(LivePath(I)) then
        DelTree(LivePath(I), True, True, True);
      if RenameFile(BackupPath(I), LivePath(I)) then
        FolderMoved[I] := False
      else
      begin
        Log('Upgrade: could not restore ' + LivePath(I));
        Result := False;
      end;
    end;
  if Result then
    RemoveDir(BackupRoot);
end;

// Setup killed during an earlier upgrade leaves a backup. The installed folder wins
// when there is one, because this run replaces it anyway.
function SettleEarlierBackup: Boolean;
var
  I: Integer;
begin
  Result := True;
  for I := 0 to ReplacedFolderCount - 1 do
    if DirExists(BackupPath(I)) then
    begin
      if DirExists(LivePath(I)) then
        Result := DelTree(BackupPath(I), True, True, True) and Result
      else
        Result := RenameFile(BackupPath(I), LivePath(I)) and Result;
    end;
end;

function TryMoveFoldersAside: Boolean;
var
  I: Integer;
begin
  Result := SettleEarlierBackup and (DirExists(BackupRoot) or CreateDir(BackupRoot));
  if not Result then
    Exit;
  for I := 0 to ReplacedFolderCount - 1 do
    if DirExists(LivePath(I)) then
    begin
      if not RenameFile(LivePath(I), BackupPath(I)) then
      begin
        Log('Upgrade: could not move ' + LivePath(I));
        RestoreMovedFolders;
        Result := False;
        Exit;
      end;
      FolderMoved[I] := True;
    end;
end;

// A process that has just released the mutex may still hold its files for a moment.
procedure MoveFoldersAside;
var
  Attempt: Integer;
  Message: String;
begin
  Message := 'Setup cannot replace the program files in ' + AppDir + ', because another '
    + 'program has one of them open. Your current version has not been changed.' + #13#10#13#10
    + 'Close whatever is using that folder (a command window or a file viewer, for example), '
    + 'then click Retry. Click Cancel to stop and install the update later.';
  repeat
    for Attempt := 1 to MoveAttempts do
    begin
      if TryMoveFoldersAside then
      begin
        Log('Upgrade: the previous program files are in {#UpgradeBackupFolder}.');
        Exit;
      end;
      Sleep(MoveRetryMilliseconds);
    end;
  until SuppressibleMsgBox(Message, mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY;
  Log('Upgrade: the program files could not be moved; nothing was changed.');
  // Ends Setup here, before anything is copied (CurStepChanged(ssInstall) only).
  Abort;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    AppDir := ExpandConstant('{app}');
    if IsUpgradeOfThisFolder then
      MoveFoldersAside
    else
      Log('No upgrade of this folder: its bin, plugins and qml folders stay as they are.');
  end
  else if CurStep = ssPostInstall then
  begin
    InstallFinished := True;
    if DirExists(BackupRoot) and not DelTree(BackupRoot, True, True, True) then
      Log('Upgrade: could not delete ' + BackupRoot + '; the next upgrade or uninstall will.');
  end;
end;

// The application quit for an update that was not installed: start the version
// that is still in place again, as the user who started the update.
procedure StartPreviousVersion;
var
  Dir: String;
  ResultCode: Integer;
begin
  // Setup can end before it knows the folder ({app}), for example when it is
  // cancelled while preparing.
  Dir := AppDir;
  if Dir = '' then
    if not RegQueryStringValue(HKCU, '{#AppUninstallKey}', 'InstallLocation', Dir) then
      Exit;
  Dir := RemoveBackslashUnlessRoot(Dir);
  if not FileExists(Dir + '\{#AppExe}') then
  begin
    Log('Update run: ' + Dir + '\{#AppExe} is missing; nothing was started.');
    Exit;
  end;
  try
    if ExecAsOriginalUser(Dir + '\{#AppExe}', '', Dir + '\bin', SW_SHOWNORMAL, ewNoWait, ResultCode) then
      Log('Update run: started the previous version again.')
    else
      Log('Update run: the previous version could not be started: ' + SysErrorMessage(ResultCode));
  except
    Log('Update run: the previous version could not be started: ' + GetExceptionMessage);
  end;
end;

// Runs however Setup ends, after Inno Setup has rolled back what it copied.
procedure DeinitializeSetup;
begin
  if InstallFinished then
    Exit;
  if FolderMoved[0] or FolderMoved[1] or FolderMoved[2] then
  begin
    Log('Upgrade: not completed; restoring the previous program files.');
    if not RestoreMovedFolders then
    begin
      SuppressibleMsgBox('The update was not installed, and the previous program files could not '
        + 'be put back. Your playsets and settings are not affected.' + #13#10#13#10
        + 'To repair the installation, run this setup again:' + #13#10
        + ExpandConstant('{srcexe}') + #13#10#13#10
        + 'If that file is gone, download the setup again from {#ProjectUrl}/releases',
        mbCriticalError, MB_OK, IDOK);
      Exit;
    end;
    if IsPmmUpdate then
      SuppressibleMsgBox('The update was not installed. Your current version of {#AppDisplayName} '
        + 'is still in place and starts again when you click OK.',
        mbInformation, MB_OK, IDOK);
  end;
  if AppQuitForUpdate then
    StartPreviousVersion;
end;
