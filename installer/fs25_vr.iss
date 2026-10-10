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
; Upgrades: with the fixed AppId above, a newer Installer starts on the game folder used last time.
; Precedence for the folder shown/used: /DIR= on the command line, then that previous folder, then
; DefaultDirName (which game detection may refine). Inno applies this order itself.
UsePreviousAppDir=yes
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

function GetModsDir(Param: String): String;
begin
  Result := AddBackslash(GetDocumentsDir) + 'My Games\FarmingSimulator2025\mods';
end;

{ ---- An existing dxgi.dll in the game's x64 folder ----
  Ours (recognised by its version resource) is simply overwritten. Anything else, such as ReShade, is
  moved to dxgi.dll.fs25vr-backup first and put back by the uninstaller. An existing backup is never
  overwritten, so an upgrade can't replace the player's original DLL with an older copy of ours. }

const
  BackupSuffix = '.fs25vr-backup';
  OurProductName = 'FS25 VR';  { ProductName in the Injector's version resource: src\dxgi.rc.in }
  ForeignDxgiMessage =
    'The game folder already contains a dxgi.dll that is not part of FS25 VR (for example ReShade).' + #13#10#13#10 +
    'It is moved to dxgi.dll.fs25vr-backup before FS25 VR is installed, and restored when FS25 VR is uninstalled.';
  ForeignDxgiBlockedMessage =
    'The game folder contains a dxgi.dll that is not part of FS25 VR, and dxgi.dll.fs25vr-backup already exists, ' +
    'so there is nowhere safe to move it.' + #13#10#13#10 +
    'Move or rename one of the two files in the x64 folder, then run the installer again.';

function GetFileVersionInfoSizeW(FileName: String; var Handle: Cardinal): Cardinal;
  external 'GetFileVersionInfoSizeW@version.dll stdcall';
function GetFileVersionInfoW(FileName: String; Handle, Len: Cardinal; Data: AnsiString): Boolean;
  external 'GetFileVersionInfoW@version.dll stdcall';
function VerQueryValueW(Block: AnsiString; SubBlock: String; var Value: Cardinal; var Len: Cardinal): Boolean;
  external 'VerQueryValueW@version.dll stdcall';
function lstrcpynW(Dest: String; Source: Cardinal; MaxLen: Integer): Cardinal;
  external 'lstrcpynW@kernel32.dll stdcall';

{ The English ProductName string of a file's version resource, or '' if it has none. }
function GetProductName(FileName: String): String;
var
  Handle, Size, ValuePtr, ValueLen: Cardinal;
  Data: AnsiString;
begin
  Result := '';
  Size := GetFileVersionInfoSizeW(FileName, Handle);
  if Size = 0 then Exit;
  SetLength(Data, Size);
  if not GetFileVersionInfoW(FileName, 0, Size, Data) then Exit;
  if not VerQueryValueW(Data, '\StringFileInfo\040904b0\ProductName', ValuePtr, ValueLen) then Exit;
  if (ValuePtr = 0) or (ValueLen = 0) then Exit;
  SetLength(Result, ValueLen);
  lstrcpynW(Result, ValuePtr, ValueLen + 1);
  { ValueLen counts the terminating null; cut the string at it }
  if Pos(#0, Result) > 0 then Result := Copy(Result, 1, Pos(#0, Result) - 1);
  Result := Trim(Result);
end;

function IsOurInjector(FileName: String): Boolean;
begin
  Result := GetProductName(FileName) = OurProductName;
end;

{ Called from PrepareToInstall for the chosen game folder. Moves a foreign dxgi.dll aside (warning
  the player) and returns '' on success; returns the reason when it can't be done safely. }
function PrepareExistingDxgi(GameDir: String): String;
var
  Dxgi, Backup: String;
begin
  Result := '';
  Dxgi := AddBackslash(GameDir) + 'x64\dxgi.dll';
  Backup := Dxgi + BackupSuffix;
  if not FileExists(Dxgi) then Exit;
  if IsOurInjector(Dxgi) then Exit;
  if FileExists(Backup) then
  begin
    Result := ForeignDxgiBlockedMessage;
    Exit;
  end;
  Log(ForeignDxgiMessage);
  if not WizardSilent then
    MsgBox(ForeignDxgiMessage, mbInformation, MB_OK);
  if not RenameFile(Dxgi, Backup) then
    Result := 'Could not move ' + Dxgi + ' to ' + Backup + '. Close any program using it and try again.';
end;

{ Uninstall: put the player's original dxgi.dll back once ours is gone. }
procedure RestoreForeignDxgi(GameDir: String);
var
  Dxgi, Backup: String;
begin
  Dxgi := AddBackslash(GameDir) + 'x64\dxgi.dll';
  Backup := Dxgi + BackupSuffix;
  if not FileExists(Backup) then Exit;
  if FileExists(Dxgi) then
  begin
    Log('dxgi.dll is still in place; leaving ' + Backup + ' where it is.');
    Exit;
  end;
  if not RenameFile(Backup, Dxgi) then
    Log('Could not restore ' + Backup + ' as dxgi.dll.');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RestoreForeignDxgi(ExpandConstant('{app}'));
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

  if Result = '' then
  begin
    Result := PrepareExistingDxgi(WizardDirValue);
    if (Result <> '') and WizardSilent then
    begin
      Log(Result);
      Abort;
    end;
  end;
end;
