; Windows installer (Inno Setup 6): per-user, no administrator rights. See
; docs/packaging.md. Built from the staged `cmake --install` tree by
;
;   cmake --build <build dir> --target installer
;
; which runs ISCC /DAppVersion=<x.y.z> /DStageDir=<stage> /DLicensePath=<file>
; /O<output dir> on this script (packaging/windows/build-installer.cmake).
;
; The update contract with the application (src/app, docs/releasing.md):
;   - the application holds the mutex below while it runs;
;   - it starts this installer with /SILENT /SP- /NORESTART /PMMUPDATE=1 and quits;
;   - an installed copy is recognised by ..\unins000.exe next to bin\.

#if Ver < EncodeVer(6, 3, 0)
  #error Inno Setup 6.3 or newer is required (x64compatible)
#endif
#ifndef AppVersion
  #error Define AppVersion: ISCC /DAppVersion=x.y.z
#endif
#ifndef StageDir
  #error Define StageDir: ISCC /DStageDir=<cmake --install prefix>
#endif
#ifndef LicensePath
  #define LicensePath StageDir + "\LICENSE"
#endif

#define AppDisplayName "Paradox Mod Manager"
#define AppExe "bin\pmm-app.exe"
#define AppRunningMutex "Local\ParadoxModManagerRunning"
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

DefaultGroupName={#AppDisplayName}
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

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; The Qt files of the previous version: a module or plugin the new version no
; longer ships must not be picked up. User data is never in the install folder.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\plugins"
Type: filesandordirs; Name: "{app}\qml"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppDisplayName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin"
Name: "{autodesktop}\{#AppDisplayName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin"; Description: "{cm:LaunchProgram,{#AppDisplayName}}"; Flags: nowait postinstall skipifsilent; Check: not IsPmmUpdate
Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin"; Flags: nowait; Check: IsPmmUpdate

[Code]
const
  UpdateWaitSeconds = 60;
  PollMilliseconds = 250;

function IsPmmUpdate: Boolean;
begin
  Result := ExpandConstant('{param:PMMUPDATE|0}') = '1';
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
  Message: String;
begin
  Result := True;
  if not IsPmmUpdate then
    Exit;

  Log('Update run: waiting for the application to exit.');
  Message := '{#AppDisplayName} is still running after '
    + IntToStr(UpdateWaitSeconds) + ' seconds, so the update could not be installed. '
    + 'Your current version has not been changed.' + #13#10#13#10
    + 'Close {#AppDisplayName} (if no window is visible, end pmm-app.exe in Task Manager), '
    + 'then click Retry. Click Cancel to stop and install the update later.';
  while not WaitForAppExit(UpdateWaitSeconds) do
    if SuppressibleMsgBox(Message, mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
    begin
      Log('Update run: the application did not exit; nothing was changed.');
      Result := False;
      Exit;
    end;
  Log('Update run: the application is not running.');
end;
