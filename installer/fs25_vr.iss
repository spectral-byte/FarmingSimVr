; Inno Setup script for the FS25 VR Installer (see CONTEXT.md: Installer, Injector, Companion Mod).
; Built by scripts\package_release.ps1, which passes:
;   /DAppVersion=<modDesc version>   /DStageDir=<staged bundle folder>   /DOutputDir=<where to write the Setup exe>
; The staged bundle folder is the one the zip bundle is made from, so both carry identical files:
;   x64\dxgi.dll, mods\FS25_VRCompanion.zip, LICENSE, THIRD_PARTY_NOTICES.txt
;
; Command line (besides Inno's standard switches such as /VERYSILENT, /SUPPRESSMSGBOXES, /DIR=):
;   /FS25VR_DOCUMENTS=<folder>   hidden, test-only: replaces the user's Documents folder
;
; English only, requires admin, unsigned (spec #61).

#ifndef AppVersion
  #error AppVersion is not defined. Build with scripts\package_release.ps1 (or pass /DAppVersion=...).
#endif
#ifndef StageDir
  #error StageDir is not defined. Build with scripts\package_release.ps1 (or pass /DStageDir=...).
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define GameExe "x64\FarmingSimulator2025Game.exe"

[Setup]
; Fixed application ID: a newer Installer upgrades the existing install under one Add/Remove Programs entry.
; scripts\test_installer.ps1 looks the uninstall entry up by this ID.
AppId={{C85F7B59-39C3-48F5-94FB-38E291A376AE}
AppName=FS25 VR
AppVersion={#AppVersion}
AppPublisher=FS25 VR
AppPublisherURL=https://github.com/spectral-byte/FarmingSimVr
UninstallDisplayName=FS25 VR
OutputDir={#OutputDir}
OutputBaseFilename=FS25_VR_v{#AppVersion}_Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; The install target is the player's game folder, not a folder of ours.
DefaultDirName={autopf}\Farming Simulator 2025
DirExistsWarning=no
AppendDefaultDirName=no
DisableProgramGroupPage=yes
LicenseFile={#StageDir}\LICENSE
; Keep the uninstaller out of the game folder.
UninstallFilesDir={commonappdata}\FS25 VR

; Admin by default. Allowing /CURRENTUSER on the command line only lets the test script run
; without a UAC prompt from a non-elevated shell; the wizard never offers it.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
; The mods folder lives under the user's Documents (a user area) even though we run elevated.
UsedUserAreasWarning=no

[Messages]
FinishedLabel=FS25 VR is installed.%n%nBefore you play:%n  1. Start your OpenXR runtime (Meta Quest Link, Virtual Desktop, SteamVR, ...).%n  2. When you load a savegame, tick "FS25 VR Companion Mod" in the mod selection.
FinishedLabelNoIcons=FS25 VR is installed.%n%nBefore you play:%n  1. Start your OpenXR runtime (Meta Quest Link, Virtual Desktop, SteamVR, ...).%n  2. When you load a savegame, tick "FS25 VR Companion Mod" in the mod selection.

[Files]
; Injector: into the game's x64 folder.
Source: "{#StageDir}\x64\dxgi.dll"; DestDir: "{app}\x64"; Flags: ignoreversion
; Companion Mod: the zip, unchanged (the game reads it as a zip), into the mods folder.
Source: "{#StageDir}\mods\FS25_VRCompanion.zip"; DestDir: "{code:GetModsDir}"; Flags: ignoreversion
; Notices travel with the uninstaller.
Source: "{#StageDir}\LICENSE"; DestDir: "{commonappdata}\FS25 VR"; Flags: ignoreversion
Source: "{#StageDir}\THIRD_PARTY_NOTICES.txt"; DestDir: "{commonappdata}\FS25 VR"; Flags: ignoreversion

[UninstallDelete]
; Written by the Injector while the game runs. modSettings (the VR settings) is deliberately kept.
Type: files; Name: "{app}\x64\fs25_vr.log"

[Code]
function IsGameFolder(Dir: String): Boolean;
begin
  Result := FileExists(AddBackslash(Dir) + '{#GameExe}');
end;

const
  NotGameFolderMessage =
    'This folder is not a Farming Simulator 25 installation.' + #13#10#13#10 +
    'Choose the folder that contains x64\FarmingSimulator2025Game.exe.';

{ The user's Documents folder (honors OneDrive redirection through the known folder), unless the
  hidden test-only /FS25VR_DOCUMENTS switch replaces it. }
function GetDocumentsDir: String;
begin
  Result := ExpandConstant('{param:FS25VR_DOCUMENTS|}');
  if Result = '' then
    Result := ExpandConstant('{userdocs}');
end;

function GetMyGamesDir: String;
begin
  Result := AddBackslash(GetDocumentsDir) + 'My Games\FarmingSimulator2025';
end;

{ The directory of the game's active mods directory override in game.xml, or '' when game.xml is
  missing/unreadable or has no override element, or the override is inactive or has no directory.
  The file is only read, never written. }
function GetModsDirOverride: String;
var
  Doc, Node, ActiveAttr, DirAttr: Variant;
  Path: String;
begin
  Result := '';
  Path := AddBackslash(GetMyGamesDir) + 'game.xml';
  if not FileExists(Path) then
    Exit;
  try
    Doc := CreateOleObject('Msxml2.DOMDocument.6.0');
    Doc.async := False;
    if not Doc.load(Path) then
      Exit;
    Node := Doc.selectSingleNode('//modsDirectoryOverride');
    if VarIsNull(Node) or VarIsEmpty(Node) then
      Exit;
    ActiveAttr := Node.getAttribute('active');
    DirAttr := Node.getAttribute('directory');
    if VarIsNull(ActiveAttr) or VarIsNull(DirAttr) then
      Exit;
    if CompareText(Trim(String(ActiveAttr)), 'true') <> 0 then
      Exit;
    Result := Trim(String(DirAttr));
    StringChangeEx(Result, '/', '\', True);
    if Result <> '' then
      Result := RemoveBackslashUnlessRoot(Result);
  except
    Log('Could not read the mods directory override from ' + Path + ': ' + GetExceptionMessage);
    Result := '';
  end;
end;

{ Where the Companion Mod goes: the active mods directory override if there is one, else the
  default mods folder. Inno records the resolved path with the file, so uninstall removes the mod
  from wherever it was installed. }
function GetModsDir(Param: String): String;
begin
  Result := GetModsDirOverride;
  if Result = '' then
    Result := AddBackslash(GetMyGamesDir) + 'mods'
  else
    Log('Mods directory override is active; installing the Companion Mod into ' + Result);
end;

const
  GameProcessName = 'FarmingSimulator2025Game.exe';
  CloseGameMessage = 'Close Farming Simulator 25 to continue.';

{ True if a process whose image name is FarmingSimulator2025Game.exe is running (any installation,
  any user: the Installer must not run while the game holds dxgi.dll or can rewrite game.xml). WMI
  gives the image name without needing process handles. If WMI cannot be queried we cannot tell, so
  we log it and carry on rather than block every install. }
function IsGameRunning: Boolean;
var
  Locator, Service, Processes: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''' + GameProcessName + '''');
    Result := Processes.Count > 0;
  except
    Log('Could not check whether ' + GameProcessName + ' is running: ' + GetExceptionMessage);
  end;
end;

{ Shared by install and uninstall. Returns True when the game is not running (possibly after the
  player closed it and chose Retry). Returns False when the caller must stop: the player chose Cancel,
  or this is a silent run, which logs the reason and fails instead of waiting. A silent run must never
  show a message box. /SUPPRESSMSGBOXES without /SILENT answers Cancel, so it cannot loop forever. }
function EnsureGameNotRunning(Silent: Boolean): Boolean;
begin
  Result := True;
  while IsGameRunning do
  begin
    Log(CloseGameMessage);
    if Silent then
    begin
      Result := False;
      Exit;
    end;
    if SuppressibleMsgBox(CloseGameMessage, mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and not WizardSilent then
    if not IsGameFolder(WizardDirValue) then
    begin
      MsgBox(NotGameFolderMessage, mbError, MB_OK);
      Result := False;
    end;
end;

{ Also covers silent installs, where there is no directory page. A silent Setup must not wait on a
  message box, so it logs the reason and aborts; an interactive one shows the message. }
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not IsGameFolder(WizardDirValue) then
  begin
    if WizardSilent then
    begin
      Log(NotGameFolderMessage);
      Abort;
    end;
    Result := NotGameFolderMessage;
  end;
  if (Result = '') and not EnsureGameNotRunning(WizardSilent) then
    Abort;
end;

{ Uninstall must not run over a live game either (it holds dxgi.dll and rewrites game.xml on exit). }
function InitializeUninstall: Boolean;
begin
  Result := EnsureGameNotRunning(UninstallSilent);
end;
