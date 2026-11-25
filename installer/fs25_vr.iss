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

DefaultDirName={autopf}\Farming Simulator 2025
DirExistsWarning=no
AppendDefaultDirName=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
LicenseFile={#StageDir}\LICENSE
UninstallFilesDir={commonappdata}\FS25 VR

PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
UsedUserAreasWarning=no

[Messages]
FinishedLabel=FS25 VR is installed.%n%nBefore you play:%n  1. Start your OpenXR runtime (Meta Quest Link, Virtual Desktop, SteamVR, ...).%n  2. When you load a savegame, tick "FS25 VR Companion Mod" in the mod selection.
FinishedLabelNoIcons=FS25 VR is installed.%n%nBefore you play:%n  1. Start your OpenXR runtime (Meta Quest Link, Virtual Desktop, SteamVR, ...).%n  2. When you load a savegame, tick "FS25 VR Companion Mod" in the mod selection.

[Files]
Source: "{#StageDir}\x64\version.dll"; DestDir: "{app}\x64"; Flags: ignoreversion
Source: "{#StageDir}\mods\FS25_VRCompanion.zip"; DestDir: "{code:GetModsDir}"; Flags: ignoreversion
Source: "{#StageDir}\LICENSE"; DestDir: "{commonappdata}\FS25 VR"; Flags: ignoreversion
Source: "{#StageDir}\THIRD_PARTY_NOTICES.txt"; DestDir: "{commonappdata}\FS25 VR"; Flags: ignoreversion

[UninstallDelete]
Type: files; Name: "{app}\x64\fs25_vr.log"
Type: files; Name: "{app}\x64\fs25_vr_crash.dmp"
Type: dirifempty; Name: "{commonappdata}\FS25 VR"

[Code]
function IsGameFolder(Dir: String): Boolean;
begin
  Result := FileExists(AddBackslash(Dir) + '{#GameExe}');
end;

const
  NotGameFolderMessage =
    'This folder is not a Farming Simulator 25 installation.' + #13#10#13#10 +
    'Choose the folder that contains x64\FarmingSimulator2025Game.exe.';

#include "game_detection.iss"

function GameFolderProblem(Dir: String): String;
begin
  if IsStoreFolder(Dir) then
    Result := StoreFolderMessage
  else if not IsGameFolder(Dir) then
    Result := NotGameFolderMessage
  else
    Result := '';
end;

procedure InitializeRenderResolutionPage; forward;
procedure InitializeCrashDumpsPage; forward;

procedure InitializeWizard;
begin
  InitGameDetection;
  InitializeRenderResolutionPage;
  InitializeCrashDumpsPage;
end;

function QueryOriginalUserDocuments: String;
var
  ProbeDir, ProbeFile, PsPath: String;
  Lines: TArrayOfString;
  Code: Integer;
begin
  Result := '';
  ProbeDir := ExpandConstant('{tmp}\documents-probe');
  ProbeFile := ProbeDir + '\documents.txt';
  if not ForceDirectories(ProbeDir) then
  begin
    Log('Could not create ' + ProbeDir + ' to ask for the Documents folder.');
    Exit;
  end;
  if not Exec(ExpandConstant('{sys}\icacls.exe'), AddQuotes(ProbeDir) + ' /grant *S-1-5-11:(OI)(CI)M', '',
              SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
    Log('Could not open ' + ProbeDir + ' to the original user (icacls exit code ' + IntToStr(Code) + ').');
  PsPath := ProbeFile;
  StringChangeEx(PsPath, '''', '''''', True);
  try
    if not ExecAsOriginalUser(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
         '-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "[IO.File]::WriteAllText(''' + PsPath +
         ''', [Environment]::GetFolderPath(''MyDocuments''))"', '', SW_HIDE, ewWaitUntilTerminated, Code) then
      Log('Could not ask the original user for their Documents folder: ' + SysErrorMessage(Code))
    else if Code <> 0 then
      Log('Asking the original user for their Documents folder failed with exit code ' + IntToStr(Code) + '.');
  except
    Log('Could not ask the original user for their Documents folder: ' + GetExceptionMessage);
  end;
  if LoadStringsFromFile(ProbeFile, Lines) and (GetArrayLength(Lines) > 0) then
    Result := Trim(Lines[0]);
  if (Result <> '') and not DirExists(Result) then
  begin
    Log('The original user''s Documents folder ' + Result + ' does not exist.');
    Result := '';
  end;
end;

var
  DocumentsDir: String;

function GetDocumentsDir: String;
begin
  if DocumentsDir = '' then
  begin
    DocumentsDir := ExpandConstant('{param:FS25VR_DOCUMENTS|}');
    if (DocumentsDir = '') and IsAdminInstallMode then
      DocumentsDir := QueryOriginalUserDocuments;
    if DocumentsDir = '' then
      DocumentsDir := ExpandConstant('{userdocs}');
    Log('Documents folder: ' + DocumentsDir);
  end;
  Result := DocumentsDir;
end;

function GetMyGamesDir: String;
begin
  Result := AddBackslash(GetDocumentsDir) + 'My Games\FarmingSimulator2025';
end;

function GetModsDirOverride: String;
var
  Doc, Node, ActiveAttr, DirAttr: Variant;
  Path: String;
begin
  Result := '';
  Path := AddBackslash(GetMyGamesDir) + 'gameSettings.xml';
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

const
  InjectorFileName = 'version.dll';
  EarlierInjectorFileName = 'dxgi.dll';
  BackupSuffix = '.fs25vr-backup';
  KeepSuffix = '.fs25vr-keep';
  OurProductName = 'FS25 VR';  { ProductName in the Injector's version resource: src\injector.rc.in }
  LegacyLogFileName = 'fs25_vr.log';
  LegacyLogFileNameUtf16 = 'f'#0's'#0'2'#0'5'#0'_'#0'v'#0'r'#0'.'#0'l'#0'o'#0'g'#0;
  ForeignInjectorMessage =
    'The game folder already contains a version.dll that is not part of FS25 VR.' + #13#10#13#10 +
    'It is moved to version.dll.fs25vr-backup before FS25 VR is installed, and restored when FS25 VR is uninstalled.';
  ForeignInjectorBlockedMessage =
    'The game folder contains a version.dll that is not part of FS25 VR, and version.dll.fs25vr-backup already exists, ' +
    'so there is nowhere safe to move it.' + #13#10#13#10 +
    'Move or rename one of the two files in the x64 folder, then run the installer again.';
  EarlierInjectorLeftMessage =
    'FS25 VR is installed, but the dxgi.dll of an earlier FS25 VR version could not be deleted from the game''s x64 folder.' + #13#10#13#10 +
    'Delete it: FS25 VR stays inactive while it is there.';

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
  if Pos(#0, Result) > 0 then Result := Copy(Result, 1, Pos(#0, Result) - 1);
  Result := Trim(Result);
end;

function IsLegacyInjector(FileName: String): Boolean;
var
  Raw: AnsiString;
begin
  Result := LoadStringFromFile(FileName, Raw) and
            ((Pos(LegacyLogFileName, Raw) > 0) or (Pos(LegacyLogFileNameUtf16, Raw) > 0));
end;

function IsOurInjector(FileName: String): Boolean;
begin
  Result := GetProductName(FileName) = OurProductName;
  if not Result then
  begin
    Result := IsLegacyInjector(FileName);
    if Result then
      Log(FileName + ' is an FS25 VR Injector from before the Installer.');
  end;
end;

var
  MovedInjectorGameDir: String;  { the game folder whose foreign version.dll PrepareExistingInjector moved aside }
  FilesPlaced: Boolean;

function GameDll(GameDir, FileName: String): String;
begin
  Result := AddBackslash(GameDir) + 'x64\' + FileName;
end;

function PrepareExistingInjector(GameDir: String): String;
var
  Dll, Backup: String;
begin
  Result := '';
  Dll := GameDll(GameDir, InjectorFileName);
  Backup := Dll + BackupSuffix;
  if not FileExists(Dll) then Exit;
  if IsOurInjector(Dll) then Exit;
  if FileExists(Backup) then
  begin
    Result := ForeignInjectorBlockedMessage;
    Exit;
  end;
  Log(ForeignInjectorMessage);
  if not WizardSilent then
    MsgBox(ForeignInjectorMessage, mbInformation, MB_OK);
  if RenameFile(Dll, Backup) then
    MovedInjectorGameDir := GameDir
  else
    Result := 'Could not move ' + Dll + ' to ' + Backup + '. Close any program using it and try again.';
end;

procedure SetAsideReplacedDll(GameDir, FileName: String);
var
  Dll, Keep: String;
begin
  Dll := GameDll(GameDir, FileName);
  Keep := Dll + KeepSuffix;
  if not FileExists(Dll) or IsOurInjector(Dll) then Exit;
  if FileExists(Keep) then
    Log(Dll + ' is not ours, but ' + Keep + ' already exists; the uninstall may remove it.')
  else if RenameFile(Dll, Keep) then
    Log(Dll + ' is not ours; keeping it.')
  else
    Log('Could not move ' + Dll + ' to ' + Keep + '; the uninstall may remove it.');
end;

procedure RestoreForeignDll(GameDir, FileName: String);
var
  Dll, Backup, Keep: String;
begin
  Dll := GameDll(GameDir, FileName);
  Backup := Dll + BackupSuffix;
  Keep := Dll + KeepSuffix;
  if FileExists(Keep) then
  begin
    if FileExists(Dll) then
      Log(FileName + ' is still in place; leaving ' + Keep + ' where it is.')
    else if not RenameFile(Keep, Dll) then
      Log('Could not restore ' + Keep + ' as ' + FileName + '.')
    else if FileExists(Backup) then
      Log('Kept the ' + FileName + ' that replaced FS25 VR; the one moved aside at install stays as ' + Backup + '.');
    Exit;
  end;
  if not FileExists(Backup) then Exit;
  if FileExists(Dll) then
  begin
    Log(FileName + ' is still in place; leaving ' + Backup + ' where it is.');
    Exit;
  end;
  if RenameFile(Backup, Dll) then
    Log('Restored ' + Backup + ' as ' + FileName + '.')
  else
    Log('Could not restore ' + Backup + ' as ' + FileName + '.');
end;

function RemoveEarlierInjector(GameDir: String): Boolean;
var
  Dll: String;
begin
  Result := True;
  Dll := GameDll(GameDir, EarlierInjectorFileName);
  if FileExists(Dll) and IsOurInjector(Dll) then
  begin
    if not DeleteFile(Dll) then
    begin
      Log('Could not delete ' + Dll + ', the Injector of an earlier FS25 VR version.');
      Result := False;
      Exit;
    end;
    Log('Deleted ' + Dll + ', the Injector of an earlier FS25 VR version.');
  end;
  RestoreForeignDll(GameDir, EarlierInjectorFileName);
end;

procedure RestoreRenderResolution; forward;
procedure RemoveCrashDumps; forward;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RestoreRenderResolution;
    RemoveCrashDumps;
    SetAsideReplacedDll(ExpandConstant('{app}'), InjectorFileName);
    SetAsideReplacedDll(ExpandConstant('{app}'), EarlierInjectorFileName);
  end;
  if CurUninstallStep = usPostUninstall then
  begin
    RestoreForeignDll(ExpandConstant('{app}'), InjectorFileName);
    RemoveEarlierInjector(ExpandConstant('{app}'));
  end;
end;

const
  RenderMinSize = 640;
  RenderMaxSize = 8192;
  RenderWindowedMode = 'windowed';
  RenderCheckCaption = 'Set the game''s Render Resolution';
  RenderHint = 'Use your Headset''s per-eye resolution';
  RenderNoGameXmlMessage = 'Launch Farming Simulator 25 once, then run the installer again to set the resolution.';
  RenderUnreadableMessage = 'game.xml has no display width, height and fullscreen mode, so the Render Resolution can''t be set.';
  RenderRangeMessage = 'Enter whole numbers from 640 to 8192 for the width and the height.';

type
  { What the Installer recorded about the Render Resolution it changed. All values are the text
    found in / written to game.xml. GameXml is empty when there is no record. }
  TRenderResolutionRecord = record
    GameXml, Backup: String;
    OriginalWidth, OriginalHeight, OriginalFullscreenMode: String;
    WrittenWidth, WrittenHeight, WrittenFullscreenMode: String;
  end;

function GetGameXmlPath: String;
begin
  Result := AddBackslash(GetDocumentsDir) + 'My Games\FarmingSimulator2025\game.xml';
end;

function ParseRenderSize(const S: String; var Value: Integer): Boolean;
var
  I: Integer;
begin
  Value := 0;
  Result := (Length(S) >= 1) and (Length(S) <= 5);
  if not Result then Exit;
  for I := 1 to Length(S) do
    if (S[I] < '0') or (S[I] > '9') then
    begin
      Result := False;
      Exit;
    end;
  Value := StrToInt(S);
  Result := (Value >= RenderMinSize) and (Value <= RenderMaxSize);
end;

{ "<width>x<height>", the /FS25VR_RESOLUTION form. }
function ParseResolution(const S: String; var W, H: Integer): Boolean;
var
  P: Integer;
begin
  P := Pos('x', S);
  if P = 0 then P := Pos('X', S);
  Result := (P > 1) and ParseRenderSize(Copy(S, 1, P - 1), W) and ParseRenderSize(Copy(S, P + 1, Length(S)), H);
end;

function PosFrom(const SubStr, S: String; From: Integer): Integer;
var
  P: Integer;
begin
  Result := 0;
  P := Pos(SubStr, Copy(S, From, Length(S)));
  if P > 0 then Result := P + From - 1;
end;

function BytesToText(const Raw: AnsiString): String;
var
  I: Integer;
begin
  SetLength(Result, Length(Raw));
  for I := 1 to Length(Raw) do
    Result[I] := Chr(Ord(Raw[I]));
end;

function LocateDisplayElement(const Text, Tag: String; var ValueStart, ValueLen: Integer): Boolean;
var
  SectionStart, SectionEnd, P, Q: Integer;
  OpenTag, CloseTag: String;
begin
  Result := False;
  SectionStart := Pos('<display>', Text);
  if SectionStart = 0 then Exit;
  SectionEnd := PosFrom('</display>', Text, SectionStart);
  if SectionEnd = 0 then Exit;
  OpenTag := '<' + Tag + '>';
  CloseTag := '</' + Tag + '>';
  P := PosFrom(OpenTag, Text, SectionStart);
  if (P = 0) or (P > SectionEnd) then Exit;
  ValueStart := P + Length(OpenTag);
  Q := PosFrom(CloseTag, Text, ValueStart);
  if (Q = 0) or (Q > SectionEnd) then Exit;
  ValueLen := Q - ValueStart;
  Result := Pos('<', Copy(Text, ValueStart, ValueLen)) = 0;
end;

function ReadDisplayElement(const Text, Tag: String; var Value: String): Boolean;
var
  S, L: Integer;
begin
  Result := LocateDisplayElement(Text, Tag, S, L);
  if Result then Value := Trim(Copy(Text, S, L));
end;

function SetDisplayElement(var Raw: AnsiString; var Text: String; const Tag, NewValue: String): Boolean;
var
  S, L: Integer;
begin
  Result := LocateDisplayElement(Text, Tag, S, L);
  if Result then
  begin
    Delete(Raw, S, L);
    Insert(AnsiString(NewValue), Raw, S);
    Delete(Text, S, L);
    Insert(NewValue, Text, S);
  end;
end;

{ Sets the display width, height and fullscreen mode in Raw. False (Raw partly edited) if one is missing. }
function SetDisplaySettings(var Raw: AnsiString; const W, H, Mode: String): Boolean;
var
  Text: String;
begin
  Text := BytesToText(Raw);
  Result := SetDisplayElement(Raw, Text, 'width', W) and SetDisplayElement(Raw, Text, 'height', H) and
            SetDisplayElement(Raw, Text, 'fullscreenMode', Mode);
end;

function ReadDisplaySettingsFrom(const Raw: AnsiString; var W, H, Mode: String): Boolean;
var
  Text: String;
begin
  Text := BytesToText(Raw);
  Result := ReadDisplayElement(Text, 'width', W) and ReadDisplayElement(Text, 'height', H) and
            ReadDisplayElement(Text, 'fullscreenMode', Mode);
end;

{ The display width, height and fullscreen mode now in game.xml. False if there is no game.xml or it
  has no such elements. }
function ReadDisplaySettings(var W, H, Mode: String): Boolean;
var
  Raw: AnsiString;
begin
  Result := LoadStringFromFile(GetGameXmlPath, Raw) and ReadDisplaySettingsFrom(Raw, W, H, Mode);
end;

function GetRenderResolutionRecordPath: String;
begin
  Result := ExpandConstant('{commonappdata}\FS25 VR\render-resolution.ini');
end;

{ Usable from the uninstaller too. False when there is no record. }
function LoadRenderResolutionRecord(var R: TRenderResolutionRecord): Boolean;
var
  Lines: TArrayOfString;
  I, P: Integer;
  Key, Value: String;
begin
  R.GameXml := '';
  R.Backup := '';
  R.OriginalWidth := '';
  R.OriginalHeight := '';
  R.OriginalFullscreenMode := '';
  R.WrittenWidth := '';
  R.WrittenHeight := '';
  R.WrittenFullscreenMode := '';
  Result := False;
  if not LoadStringsFromFile(GetRenderResolutionRecordPath, Lines) then Exit;
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    P := Pos('=', Lines[I]);
    if P > 0 then
    begin
      Key := Copy(Lines[I], 1, P - 1);
      Value := Copy(Lines[I], P + 1, Length(Lines[I]));
      if Key = 'GameXml' then R.GameXml := Value
      else if Key = 'Backup' then R.Backup := Value
      else if Key = 'OriginalWidth' then R.OriginalWidth := Value
      else if Key = 'OriginalHeight' then R.OriginalHeight := Value
      else if Key = 'OriginalFullscreenMode' then R.OriginalFullscreenMode := Value
      else if Key = 'WrittenWidth' then R.WrittenWidth := Value
      else if Key = 'WrittenHeight' then R.WrittenHeight := Value
      else if Key = 'WrittenFullscreenMode' then R.WrittenFullscreenMode := Value;
    end;
  end;
  Result := R.GameXml <> '';
end;

function SaveRenderResolutionRecord(const R: TRenderResolutionRecord): Boolean;
var
  Lines: TArrayOfString;
begin
  SetArrayLength(Lines, 9);
  Lines[0] := '[RenderResolution]';
  Lines[1] := 'GameXml=' + R.GameXml;
  Lines[2] := 'Backup=' + R.Backup;
  Lines[3] := 'OriginalWidth=' + R.OriginalWidth;
  Lines[4] := 'OriginalHeight=' + R.OriginalHeight;
  Lines[5] := 'OriginalFullscreenMode=' + R.OriginalFullscreenMode;
  Lines[6] := 'WrittenWidth=' + R.WrittenWidth;
  Lines[7] := 'WrittenHeight=' + R.WrittenHeight;
  Lines[8] := 'WrittenFullscreenMode=' + R.WrittenFullscreenMode;
  ForceDirectories(ExtractFilePath(GetRenderResolutionRecordPath));
  Result := SaveStringsToUTF8File(GetRenderResolutionRecordPath, Lines, False);
end;

function ApplyRenderResolution(W, H: Integer): String;
var
  Raw: AnsiString;
  Path, CurW, CurH, CurMode: String;
  R, Old: TRenderResolutionRecord;
  HadOld, CarryOver: Boolean;
begin
  Result := '';
  Path := GetGameXmlPath;
  if not LoadStringFromFile(Path, Raw) then
  begin
    Result := 'Could not read ' + Path;
    Exit;
  end;
  if not ReadDisplaySettingsFrom(Raw, CurW, CurH, CurMode) then
  begin
    Result := RenderUnreadableMessage;
    Exit;
  end;

  R.GameXml := Path;
  R.Backup := Path + '.fs25vr-backup';
  R.WrittenWidth := IntToStr(W);
  R.WrittenHeight := IntToStr(H);
  R.WrittenFullscreenMode := RenderWindowedMode;
  HadOld := LoadRenderResolutionRecord(Old);
  CarryOver := HadOld and SameText(Old.GameXml, Path) and (Old.WrittenWidth = CurW) and
               (Old.WrittenHeight = CurH) and (Old.WrittenFullscreenMode = CurMode);
  if CarryOver then
  begin
    R.OriginalWidth := Old.OriginalWidth;
    R.OriginalHeight := Old.OriginalHeight;
    R.OriginalFullscreenMode := Old.OriginalFullscreenMode;
  end
  else
  begin
    R.OriginalWidth := CurW;
    R.OriginalHeight := CurH;
    R.OriginalFullscreenMode := CurMode;
  end;

  if not SetDisplaySettings(Raw, R.WrittenWidth, R.WrittenHeight, R.WrittenFullscreenMode) then
  begin
    Result := RenderUnreadableMessage;
    Exit;
  end;
  if not FileExists(R.Backup) then
    if not FileCopy(Path, R.Backup, True) then
    begin
      Result := 'Could not back up ' + Path + ' to ' + R.Backup;
      Exit;
    end;
  if not SaveRenderResolutionRecord(R) then
  begin
    Result := 'Could not record the Render Resolution change in ' + GetRenderResolutionRecordPath;
    Exit;
  end;
  if not SaveStringToFile(Path, Raw, False) then
  begin
    Result := 'Could not write ' + Path;
    if HadOld then
      SaveRenderResolutionRecord(Old)
    else
      DeleteFile(GetRenderResolutionRecordPath);
  end;
end;

procedure RestoreRenderResolution;
var
  R: TRenderResolutionRecord;
  Raw: AnsiString;
  CurW, CurH, CurMode: String;
begin
  if not LoadRenderResolutionRecord(R) then Exit;

  if not FileExists(R.GameXml) then
    Log('Render Resolution: ' + R.GameXml + ' is gone; nothing to restore.')
  else if not LoadStringFromFile(R.GameXml, Raw) then
    Log('Render Resolution: could not read ' + R.GameXml + '; leaving it alone.')
  else if not ReadDisplaySettingsFrom(Raw, CurW, CurH, CurMode) then
    Log('Render Resolution: ' + R.GameXml + ' has no display width, height and fullscreen mode; leaving it alone.')
  else if (CurW <> R.WrittenWidth) or (CurH <> R.WrittenHeight) or (CurMode <> R.WrittenFullscreenMode) then
    Log('Render Resolution: ' + R.GameXml + ' was changed since install (' + CurW + 'x' + CurH + ' ' + CurMode +
        '); leaving it alone.')
  else if not SetDisplaySettings(Raw, R.OriginalWidth, R.OriginalHeight, R.OriginalFullscreenMode) then
    Log('Render Resolution: could not edit ' + R.GameXml + '; leaving it alone.')
  else if not SaveStringToFile(R.GameXml, Raw, False) then
    Log('Render Resolution: could not write ' + R.GameXml + '.')
  else
    Log('Render Resolution: restored ' + R.OriginalWidth + 'x' + R.OriginalHeight + ' ' + R.OriginalFullscreenMode + ' in ' + R.GameXml + '.');

  if not DeleteFile(GetRenderResolutionRecordPath) then
    Log('Render Resolution: could not delete ' + GetRenderResolutionRecordPath + '.');
end;
var
  RenderPage: TWizardPage;
  RenderCheck: TNewCheckBox;
  RenderWidthEdit, RenderHeightEdit: TNewEdit;
  RenderApply: Boolean;
  RenderWidth, RenderHeight: Integer;

procedure UpdateRenderControls;
begin
  RenderWidthEdit.Enabled := RenderCheck.Enabled and RenderCheck.Checked;
  RenderHeightEdit.Enabled := RenderCheck.Enabled and RenderCheck.Checked;
end;

procedure RenderCheckClick(Sender: TObject);
begin
  UpdateRenderControls;
end;

procedure InitializeRenderResolutionPage;
var
  CurW, CurH, CurMode, Notice: String;
  SwitchW, SwitchH: Integer;
  Hint, WidthLabel, HeightLabel, NoticeLabel: TNewStaticText;
begin
  RenderPage := CreateCustomPage(wpSelectDir, 'Render Resolution',
    'Optionally set the game''s Render Resolution to your headset''s resolution.');

  RenderCheck := TNewCheckBox.Create(RenderPage);
  RenderCheck.Parent := RenderPage.Surface;
  RenderCheck.Left := 0;
  RenderCheck.Top := 0;
  RenderCheck.Width := RenderPage.SurfaceWidth;
  RenderCheck.Caption := RenderCheckCaption;
  RenderCheck.Checked := False;
  RenderCheck.OnClick := @RenderCheckClick;

  WidthLabel := TNewStaticText.Create(RenderPage);
  WidthLabel.Parent := RenderPage.Surface;
  WidthLabel.Caption := 'Width (pixels):';
  WidthLabel.Left := ScaleX(16);
  WidthLabel.Top := ScaleY(36);

  RenderWidthEdit := TNewEdit.Create(RenderPage);
  RenderWidthEdit.Parent := RenderPage.Surface;
  RenderWidthEdit.Left := ScaleX(120);
  RenderWidthEdit.Top := ScaleY(32);
  RenderWidthEdit.Width := ScaleX(80);
  RenderWidthEdit.MaxLength := 5;

  HeightLabel := TNewStaticText.Create(RenderPage);
  HeightLabel.Parent := RenderPage.Surface;
  HeightLabel.Caption := 'Height (pixels):';
  HeightLabel.Left := ScaleX(16);
  HeightLabel.Top := ScaleY(66);

  RenderHeightEdit := TNewEdit.Create(RenderPage);
  RenderHeightEdit.Parent := RenderPage.Surface;
  RenderHeightEdit.Left := ScaleX(120);
  RenderHeightEdit.Top := ScaleY(62);
  RenderHeightEdit.Width := ScaleX(80);
  RenderHeightEdit.MaxLength := 5;

  Hint := TNewStaticText.Create(RenderPage);
  Hint.Parent := RenderPage.Surface;
  Hint.Caption := RenderHint;
  Hint.Left := ScaleX(16);
  Hint.Top := ScaleY(94);

  NoticeLabel := TNewStaticText.Create(RenderPage);
  NoticeLabel.Parent := RenderPage.Surface;
  NoticeLabel.Left := 0;
  NoticeLabel.Top := ScaleY(130);
  NoticeLabel.Width := RenderPage.SurfaceWidth;
  NoticeLabel.AutoSize := False;
  NoticeLabel.WordWrap := True;
  NoticeLabel.Height := ScaleY(48);

  Notice := '';
  if not FileExists(GetGameXmlPath) then
    Notice := RenderNoGameXmlMessage
  else if not ReadDisplaySettings(CurW, CurH, CurMode) then
    Notice := RenderUnreadableMessage;
  NoticeLabel.Caption := Notice;
  RenderCheck.Enabled := Notice = '';
  if Notice = '' then
  begin
    RenderWidthEdit.Text := CurW;
    RenderHeightEdit.Text := CurH;
    if ParseResolution(ExpandConstant('{param:FS25VR_RESOLUTION|}'), SwitchW, SwitchH) then
    begin
      RenderWidthEdit.Text := IntToStr(SwitchW);
      RenderHeightEdit.Text := IntToStr(SwitchH);
      RenderCheck.Checked := True;
    end;
  end;
  UpdateRenderControls;
end;

function RenderPageNextOk(CurPageID: Integer): Boolean;
var
  Ignored: Integer;
begin
  Result := True;
  if (CurPageID = RenderPage.ID) and RenderCheck.Checked then
    if not (ParseRenderSize(RenderWidthEdit.Text, Ignored) and ParseRenderSize(RenderHeightEdit.Text, Ignored)) then
    begin
      MsgBox(RenderRangeMessage, mbError, MB_OK);
      Result := False;
    end;
end;

function PrepareRenderResolution: String;
var
  Switch, CurW, CurH, CurMode: String;
  W, H: Integer;
begin
  Result := '';
  RenderApply := False;
  Switch := ExpandConstant('{param:FS25VR_RESOLUTION|}');
  if WizardSilent then
  begin
    if Switch = '' then Exit;
    if not ParseResolution(Switch, W, H) then
    begin
      Result := 'Invalid /FS25VR_RESOLUTION value "' + Switch + '". Use <width>x<height> with whole numbers from 640 to 8192, for example 4000x2500.';
      Exit;
    end;
    if not FileExists(GetGameXmlPath) then
    begin
      Log('Skipped the Render Resolution: ' + RenderNoGameXmlMessage);
      Exit;
    end;
  end
  else
  begin
    if not (RenderCheck.Enabled and RenderCheck.Checked) then Exit;
    if not (ParseRenderSize(RenderWidthEdit.Text, W) and ParseRenderSize(RenderHeightEdit.Text, H)) then
    begin
      Result := RenderRangeMessage;
      Exit;
    end;
  end;
  if not ReadDisplaySettings(CurW, CurH, CurMode) then
  begin
    Result := RenderUnreadableMessage;
    Exit;
  end;
  RenderApply := True;
  RenderWidth := W;
  RenderHeight := H;
end;

const
  CrashDumpsBaseKey = 'SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps';
  CrashDumpsGameExe = 'FarmingSimulator2025Game.exe';
  CrashDumpType = 0;
  CrashDumpFlags = $1060;
  CrashDumpCount = 3;
  CrashDumpsSkippedPerUserMessage =
    'Skipped the Windows crash dump setting: it needs an administrator install and this is a per-user run.';
  NoCrashDumpsSwitch = 'FS25VR_NOCRASHDUMPS';

function HasCommandLineSwitch(const Name: String): Boolean; forward;

type
  TCrashDumpRecord = record
    ExeName: String;
    WrittenDumpType, WrittenCustomDumpFlags, WrittenDumpCount: String;
    CreatedLocalDumpsKey: String;
  end;

function CrashDumpsRoot: Integer;
begin
  Result := HKLM64;
end;

function CrashDumpsKey(ExeName: String): String;
begin
  Result := CrashDumpsBaseKey + '\' + ExeName;
end;

function IsPlainFileName(S: String): Boolean;
begin
  Result := (S <> '') and (Pos('\', S) = 0) and (Pos('/', S) = 0);
end;

function CrashDumpsAllowed: Boolean;
begin
  Result := IsAdminInstallMode;
end;

var
  CrashDumpsPage: TWizardPage;
  CrashDumpsCheck: TNewCheckBox;
  CrashDumpsOffered: Boolean;

function CrashDumpsAnsweredByWizard: Boolean;
begin
  Result := CrashDumpsOffered and not WizardSilent;
end;

function WantCrashDumps: Boolean;
begin
  if CrashDumpsAnsweredByWizard then
    Result := CrashDumpsCheck.Checked
  else
    Result := not HasCommandLineSwitch(NoCrashDumpsSwitch);
end;

const
  CrashDumpsCheckCaption = 'Have Windows keep a crash dump when the game crashes';
  { What the page says, in plain words: what Windows keeps, where, and what the Installer changes. The folder
    and the count are the ones the key leaves to Windows' defaults (see the README's "The game crashed"). }
  CrashDumpsExplanation =
    'When Farming Simulator 25 crashes, Windows saves a small crash dump file that you can attach to a bug report. ' +
    'Windows keeps the last 3 dumps, in the CrashDumps folder in your local application data ' +
    '(%LOCALAPPDATA%\CrashDumps). A dump can contain file paths and your Windows user name, so zip it before ' +
    'you share it.' + #13#10#13#10 +
    'This turns on one Windows setting, for FarmingSimulator2025Game.exe only. Uninstalling FS25 VR turns it off ' +
    'again, and a setting you already had for the game is left alone. Untick the box and Setup changes nothing ' +
    'in Windows.';

procedure InitializeCrashDumpsPage;
var
  Explanation: TNewStaticText;
begin
  CrashDumpsOffered := CrashDumpsAllowed;
  if not CrashDumpsOffered then Exit;

  CrashDumpsPage := CreateCustomPage(RenderPage.ID, 'Crash Dumps',
    'Keep a file to attach to a bug report if the game crashes.');

  CrashDumpsCheck := TNewCheckBox.Create(CrashDumpsPage);
  CrashDumpsCheck.Parent := CrashDumpsPage.Surface;
  CrashDumpsCheck.Left := 0;
  CrashDumpsCheck.Top := 0;
  CrashDumpsCheck.Width := CrashDumpsPage.SurfaceWidth;
  CrashDumpsCheck.Caption := CrashDumpsCheckCaption;
  CrashDumpsCheck.Checked := not HasCommandLineSwitch(NoCrashDumpsSwitch);

  Explanation := TNewStaticText.Create(CrashDumpsPage);
  Explanation.Parent := CrashDumpsPage.Surface;
  Explanation.Left := 0;
  Explanation.Top := ScaleY(28);
  Explanation.Width := CrashDumpsPage.SurfaceWidth;
  Explanation.AutoSize := False;
  Explanation.WordWrap := True;
  Explanation.Height := ScaleY(150);
  Explanation.Caption := CrashDumpsExplanation;
end;

function GetCrashDumpExeName: String;
begin
  Result := Trim(ExpandConstant('{param:FS25VR_GAMEEXE|}'));
  if Result = '' then
    Result := CrashDumpsGameExe;
end;

function GetCrashDumpRecordPath: String;
begin
  Result := ExpandConstant('{commonappdata}\FS25 VR\crash-dumps.ini');
end;

function LoadCrashDumpRecord(var R: TCrashDumpRecord): Boolean;
var
  Lines: TArrayOfString;
  I, P: Integer;
  Key, Value: String;
begin
  R.ExeName := '';
  R.WrittenDumpType := '';
  R.WrittenCustomDumpFlags := '';
  R.WrittenDumpCount := '';
  R.CreatedLocalDumpsKey := '';
  Result := False;
  if not LoadStringsFromFile(GetCrashDumpRecordPath, Lines) then Exit;
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    P := Pos('=', Lines[I]);
    if P > 0 then
    begin
      Key := Copy(Lines[I], 1, P - 1);
      Value := Copy(Lines[I], P + 1, Length(Lines[I]));
      if Key = 'ExeName' then R.ExeName := Value
      else if Key = 'WrittenDumpType' then R.WrittenDumpType := Value
      else if Key = 'WrittenCustomDumpFlags' then R.WrittenCustomDumpFlags := Value
      else if Key = 'WrittenDumpCount' then R.WrittenDumpCount := Value
      else if Key = 'CreatedLocalDumpsKey' then R.CreatedLocalDumpsKey := Value;
    end;
  end;
  Result := R.ExeName <> '';
end;

function SaveCrashDumpRecord(const R: TCrashDumpRecord): Boolean;
var
  Lines: TArrayOfString;
begin
  SetArrayLength(Lines, 6);
  Lines[0] := '[CrashDumps]';
  Lines[1] := 'ExeName=' + R.ExeName;
  Lines[2] := 'WrittenDumpType=' + R.WrittenDumpType;
  Lines[3] := 'WrittenCustomDumpFlags=' + R.WrittenCustomDumpFlags;
  Lines[4] := 'WrittenDumpCount=' + R.WrittenDumpCount;
  Lines[5] := 'CreatedLocalDumpsKey=' + R.CreatedLocalDumpsKey;
  ForceDirectories(ExtractFilePath(GetCrashDumpRecordPath));
  Result := SaveStringsToUTF8File(GetCrashDumpRecordPath, Lines, False);
end;

function CrashDumpKeyIsUnchanged(const R: TCrashDumpRecord): Boolean;
var
  Key: String;
  Names, SubKeys: TArrayOfString;
  Current: Cardinal;
begin
  Result := False;
  Key := CrashDumpsKey(R.ExeName);
  if not RegGetValueNames(CrashDumpsRoot, Key, Names) or (GetArrayLength(Names) <> 3) then Exit;
  if RegGetSubkeyNames(CrashDumpsRoot, Key, SubKeys) and (GetArrayLength(SubKeys) > 0) then Exit;
  if not RegQueryDWordValue(CrashDumpsRoot, Key, 'DumpType', Current) or (IntToStr(Current) <> R.WrittenDumpType) then Exit;
  if not RegQueryDWordValue(CrashDumpsRoot, Key, 'CustomDumpFlags', Current) or (IntToStr(Current) <> R.WrittenCustomDumpFlags) then Exit;
  if not RegQueryDWordValue(CrashDumpsRoot, Key, 'DumpCount', Current) or (IntToStr(Current) <> R.WrittenDumpCount) then Exit;
  Result := True;
end;

function CrashDumpsKeyHoldsNothing(const Key: String): Boolean;
var
  Names, SubKeys: TArrayOfString;
begin
  Result := RegKeyExists(CrashDumpsRoot, Key) and RegGetValueNames(CrashDumpsRoot, Key, Names) and
            RegGetSubkeyNames(CrashDumpsRoot, Key, SubKeys) and
            (GetArrayLength(Names) = 0) and (GetArrayLength(SubKeys) = 0);
end;

procedure SetUpCrashDumps;
var
  ExeName, Key: String;
  R, Old: TCrashDumpRecord;
  HaveOld: Boolean;
begin
  if not WantCrashDumps then
  begin
    if CrashDumpsAnsweredByWizard then
      Log('Windows crash dump setting: left alone because the Crash Dumps checkbox was unticked.')
    else
      Log('Windows crash dump setting: left alone because of /' + NoCrashDumpsSwitch + '.');
    Exit;
  end;
  if not CrashDumpsAllowed then
  begin
    Log(CrashDumpsSkippedPerUserMessage);
    Exit;
  end;
  ExeName := GetCrashDumpExeName;
  if not IsPlainFileName(ExeName) then
  begin
    Log('Skipped the Windows crash dump setting: "' + ExeName + '" is not a file name.');
    Exit;
  end;
  Key := CrashDumpsKey(ExeName);

  HaveOld := LoadCrashDumpRecord(Old);
  if HaveOld then
    HaveOld := SameText(Old.ExeName, ExeName);

  if RegKeyExists(CrashDumpsRoot, Key) then
  begin
    if HaveOld and CrashDumpKeyIsUnchanged(Old) then
      Log('Windows crash dump setting: HKLM\' + Key + ' is still the one this Installer made; keeping it and its record.')
    else
    begin
      Log('Windows crash dump setting: HKLM\' + Key + ' already exists; leaving it alone.');
      if HaveOld then
      begin
        Log('Windows crash dump setting: it was changed since this Installer made it; deleting the record of it.');
        DeleteFile(GetCrashDumpRecordPath);
      end;
    end;
    Exit;
  end;

  R.ExeName := ExeName;
  R.WrittenDumpType := IntToStr(CrashDumpType);
  R.WrittenCustomDumpFlags := IntToStr(CrashDumpFlags);
  R.WrittenDumpCount := IntToStr(CrashDumpCount);
  if not RegKeyExists(CrashDumpsRoot, CrashDumpsBaseKey) then
    R.CreatedLocalDumpsKey := '1'
  else if HaveOld and (Old.CreatedLocalDumpsKey = '1') and CrashDumpsKeyHoldsNothing(CrashDumpsBaseKey) then
  begin
    R.CreatedLocalDumpsKey := '1';
    Log('Windows crash dump setting: HKLM\' + CrashDumpsBaseKey + ' is still the empty key an earlier install created; the record keeps that.');
  end
  else
    R.CreatedLocalDumpsKey := '0';
  if not SaveCrashDumpRecord(R) then
  begin
    Log('Skipped the Windows crash dump setting: could not write ' + GetCrashDumpRecordPath + '.');
    Exit;
  end;
  if not (RegWriteDWordValue(CrashDumpsRoot, Key, 'DumpType', CrashDumpType) and
          RegWriteDWordValue(CrashDumpsRoot, Key, 'CustomDumpFlags', CrashDumpFlags) and
          RegWriteDWordValue(CrashDumpsRoot, Key, 'DumpCount', CrashDumpCount)) then
  begin
    Log('The Windows crash dump setting was not set: could not write HKLM\' + Key + '.');
    RegDeleteKeyIncludingSubkeys(CrashDumpsRoot, Key);
    if R.CreatedLocalDumpsKey = '1' then
      RegDeleteKeyIfEmpty(CrashDumpsRoot, CrashDumpsBaseKey);
    DeleteFile(GetCrashDumpRecordPath);
    Exit;
  end;
  Log('Windows crash dump setting: created HKLM\' + Key + '.');
end;

procedure RemoveCrashDumps;
var
  R: TCrashDumpRecord;
  Key: String;
begin
  if not LoadCrashDumpRecord(R) then Exit;

  Key := CrashDumpsKey(R.ExeName);
  if not IsPlainFileName(R.ExeName) then
    Log('Windows crash dump setting: the record names "' + R.ExeName + '", which is not a file name; leaving the registry alone.')
  else if not RegKeyExists(CrashDumpsRoot, Key) then
    Log('Windows crash dump setting: HKLM\' + Key + ' is gone; nothing to remove.')
  else if not CrashDumpKeyIsUnchanged(R) then
    Log('Windows crash dump setting: HKLM\' + Key + ' was changed since install; leaving it alone.')
  else if RegDeleteKeyIncludingSubkeys(CrashDumpsRoot, Key) then
  begin
    Log('Windows crash dump setting: removed HKLM\' + Key + '.');
    if R.CreatedLocalDumpsKey = '1' then
      if RegDeleteKeyIfEmpty(CrashDumpsRoot, CrashDumpsBaseKey) then
        Log('Windows crash dump setting: removed HKLM\' + CrashDumpsBaseKey + ', which the install had created.')
      else
        Log('Windows crash dump setting: HKLM\' + CrashDumpsBaseKey + ' holds other entries now; leaving it.');
  end
  else
    Log('Windows crash dump setting: could not remove HKLM\' + Key + '.');

  if not DeleteFile(GetCrashDumpRecordPath) then
    Log('Windows crash dump setting: could not delete ' + GetCrashDumpRecordPath + '.');
end;

const
  RenderResolutionFailedExitCode = 20;
  EarlierInjectorLeftExitCode = 21;

var
  RenderFailed: Boolean;
  EarlierInjectorLeft: Boolean;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Problem: String;
begin
  if CurStep = ssPostInstall then
  begin
    FilesPlaced := True;
    if not RemoveEarlierInjector(ExpandConstant('{app}')) then
    begin
      EarlierInjectorLeft := True;
      Log(EarlierInjectorLeftMessage);
      if not WizardSilent then
        MsgBox(EarlierInjectorLeftMessage, mbError, MB_OK);
    end;
    if RenderApply then
    begin
      Problem := ApplyRenderResolution(RenderWidth, RenderHeight);
      if Problem <> '' then
      begin
        RenderFailed := True;
        Log('The Render Resolution was not set: ' + Problem);
        if not WizardSilent then
          MsgBox('FS25 VR is installed, but the Render Resolution was not set:' + #13#10#13#10 + Problem, mbError, MB_OK);
      end;
    end;
    SetUpCrashDumps;
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  if RenderFailed then
    Result := RenderResolutionFailedExitCode
  else if EarlierInjectorLeft then
    Result := EarlierInjectorLeftExitCode
  else
    Result := 0;
end;

procedure DeinitializeSetup;
begin
  if (MovedInjectorGameDir <> '') and not FilesPlaced then
  begin
    Log('The install did not finish; putting back the version.dll that was moved aside.');
    RestoreForeignDll(MovedInjectorGameDir, InjectorFileName);
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  AcceptPickedInstallation(CurPageID);
  if (CurPageID = wpSelectDir) and not WizardSilent then
    if GameFolderProblem(WizardDirValue) <> '' then
    begin
      MsgBox(GameFolderProblem(WizardDirValue), mbError, MB_OK);
      Result := False;
    end;
  if Result then Result := RenderPageNextOk(CurPageID);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := GameFolderProblem(WizardDirValue);
  if (Result = '') and not EnsureGameNotRunning(WizardSilent) then
    Result := CloseGameMessage;
  if Result = '' then
    Result := PrepareRenderResolution;
  if Result = '' then
    Result := PrepareExistingInjector(WizardDirValue);
  if Result <> '' then
  begin
    Log(Result);
    if WizardSilent then
      Abort;
  end;
end;

function InitializeUninstall: Boolean;
begin
  Result := EnsureGameNotRunning(UninstallSilent);
end;

procedure ExitProcess(uExitCode: Cardinal);
  external 'ExitProcess@kernel32.dll stdcall';

const
  UninstallSwitch = 'FS25VR_UNINSTALL';
  UninstallWaitMilliseconds = 60000;
  NotInstalledMessage = 'FS25 VR is not installed, so there is nothing to uninstall.';

function HasCommandLineSwitch(const Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if SameText(ParamStr(I), '/' + Name) or SameText(ParamStr(I), '-' + Name) then
      Result := True;
end;

function IsUnattendedRun: Boolean;
begin
  Result := WizardSilent or HasCommandLineSwitch('SILENT') or HasCommandLineSwitch('VERYSILENT');
end;

function QueryUninstallString(RootKey: Integer; var Value: String): Boolean;
begin
  Value := '';
  Result := RegQueryStringValue(RootKey, UninstallKey, 'UninstallString', Value) and (Trim(Value) <> '');
end;

function FindExistingUninstaller(var Exe: String): Boolean;
var
  Value: String;
  Found: Boolean;
begin
  Exe := '';
  Found := QueryUninstallString(HKLM64, Value);
  if not Found then Found := QueryUninstallString(HKLM32, Value);
  if not Found then Found := QueryUninstallString(HKCU64, Value);
  if not Found then Found := QueryUninstallString(HKCU32, Value);
  Result := False;
  if not Found then Exit;
  Exe := RemoveQuotes(Trim(Value));
  if FileExists(Exe) then
    Result := True
  else
    Log('The uninstaller registered for FS25 VR is missing: ' + Exe);
end;

function RunExistingUninstaller(const Exe, Params: String; ShowCmd: Integer): Integer;
var
  Code, Waited: Integer;
begin
  Log('Running the uninstaller ' + Exe + ' ' + Params);
  if not Exec(Exe, Params, '', ShowCmd, ewWaitUntilTerminated, Code) then
  begin
    Log('Could not start the uninstaller: ' + SysErrorMessage(Code));
    Result := 1;
    Exit;
  end;
  Log('The uninstaller finished with exit code ' + IntToStr(Code) + '.');
  Result := Code;
  if Code = 0 then
  begin
    Waited := 0;
    while FileExists(Exe) and (Waited < UninstallWaitMilliseconds) do
    begin
      Sleep(250);
      Waited := Waited + 250;
    end;
    if FileExists(Exe) then
      Log('The uninstaller is still there after ' + IntToStr(UninstallWaitMilliseconds div 1000) + ' seconds.');
  end;
end;

function UninstallThroughSwitch: Integer;
var
  Exe, Params: String;
begin
  if not FindExistingUninstaller(Exe) then
  begin
    Log(NotInstalledMessage);
    Result := 1;
    Exit;
  end;
  if HasCommandLineSwitch('SILENT') and not HasCommandLineSwitch('VERYSILENT') then
    Params := '/SILENT'
  else
    Params := '/VERYSILENT';
  Result := RunExistingUninstaller(Exe, Params + ' /SUPPRESSMSGBOXES /NORESTART', SW_HIDE);
end;

function AskUpgradeOrUninstall: Integer;
var
  Labels: TArrayOfString;
begin
  SetArrayLength(Labels, 2);
  Labels[0] := 'Upgrade or reinstall' + #13#10 + 'Install this version over the existing one.';
  Labels[1] := 'Uninstall' + #13#10 + 'Remove FS25 VR from this computer.';
  Result := TaskDialogMsgBox('FS25 VR is already installed', 'What do you want to do?',
    mbConfirmation, MB_YESNOCANCEL, Labels, 0);
end;

function InitializeSetup: Boolean;
var
  Exe: String;
  ExitCode: Integer;
begin
  Result := True;
  if HasCommandLineSwitch(UninstallSwitch) then
  begin
    ExitCode := UninstallThroughSwitch;
    DelTree(ExpandConstant('{tmp}'), True, True, True);
    ExitProcess(ExitCode);
    Result := False;
    Exit;
  end;
  if IsUnattendedRun then Exit;
  if not FindExistingUninstaller(Exe) then Exit;
  case AskUpgradeOrUninstall of
    IDNO:
      begin
        RunExistingUninstaller(Exe, '', SW_SHOWNORMAL);
        Result := False;
      end;
    IDCANCEL:
      Result := False;
  end;
end;
