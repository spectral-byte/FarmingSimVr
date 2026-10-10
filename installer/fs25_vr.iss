; Inno Setup script for the FS25 VR Installer (see CONTEXT.md: Installer, Injector, Companion Mod).
; Built by scripts\package_release.ps1, which passes:
;   /DAppVersion=<modDesc version>   /DStageDir=<staged bundle folder>   /DOutputDir=<where to write the Setup exe>
; The staged bundle folder is the one the zip bundle is made from, so both carry identical files:
;   x64\dxgi.dll, mods\FS25_VRCompanion.zip, LICENSE, THIRD_PARTY_NOTICES.txt
;
; Command line (besides Inno's standard switches such as /VERYSILENT, /SUPPRESSMSGBOXES, /DIR=):
;   /FS25VR_DOCUMENTS=<folder>   hidden, test-only: replaces the user's Documents folder
;   /FS25VR_RESOLUTION=<W>x<H>   sets the game's Render Resolution (see below) without the wizard page.
;                                Whole numbers from 640 to 8192, e.g. /FS25VR_RESOLUTION=4000x2500.
;                                An invalid value fails the install before anything is placed. With no
;                                game.xml the install succeeds and the resolution is skipped (logged).
;                                In a wizard run it pre-ticks and pre-fills the Render Resolution page.
;
; Render Resolution (spec #61, ticket #66)
;   The game's settings file is <Documents>\My Games\FarmingSimulator2025\game.xml. It is NOT
;   gameSettings.xml (that one holds modsDirectoryOverride). Shape of game.xml (UTF-8 with a BOM, CRLF
;   line endings, 4-space indent):
;     <game>
;       <graphic>
;         <display>
;           <width>3840</width>  <height>2160</height>  <fullscreenMode>windowed</fullscreenMode>
;           ... previousWidth, previousHeight, previousFullscreenMode and more display elements ...
;   The Installer changes the text of exactly three elements inside <display>: <width>, <height> and
;   <fullscreenMode> (set to "windowed"). It edits the bytes of the file in place, not through an XML
;   serializer, so everything else (BOM, line endings, indentation, the previous* elements) stays
;   identical. Before the first edit ever, game.xml is copied to game.xml.fs25vr-backup next to it; an
;   existing backup is never overwritten.
;
; Render Resolution record (read by the uninstall restore, ticket #69)
;   {commonappdata}\FS25 VR\render-resolution.ini, next to the uninstaller. UTF-8 with a BOM, one
;   Key=Value per line after a [RenderResolution] header:
;     GameXml=<full path of the game.xml that was edited>
;     Backup=<full path of game.xml.fs25vr-backup>
;     OriginalWidth= OriginalHeight= OriginalFullscreenMode=      what game.xml held before the Installer
;     WrittenWidth=  WrittenHeight=  WrittenFullscreenMode=       what the Installer wrote
;   Restore rule for #69: if game.xml's display width, height and fullscreenMode all still equal the
;   Written* values, write the Original* values back; otherwise leave game.xml alone. The record is
;   only written when an edit is applied. When an Installer run applies again and game.xml still holds
;   the previous Written* values, the Original* values are carried over (so they stay the player's
;   real settings); otherwise the values found in game.xml become the new Original* values.
;   Pascal helpers in [Code]: TRenderResolutionRecord, GetRenderResolutionRecordPath,
;   LoadRenderResolutionRecord (usable from the uninstaller), ReadDisplaySettings, WriteDisplaySettings.
;   The uninstall does not delete the record; #69 should do so after restoring.
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

function GetModsDir(Param: String): String;
begin
  Result := AddBackslash(GetDocumentsDir) + 'My Games\FarmingSimulator2025\mods';
end;

{ ---- Render Resolution (ticket #66; file formats are described at the top of this script) ---- }

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

{ Whole number from 640 to 8192, digits only. }
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

{ One character per byte, for searching only: positions in the result are byte positions in Raw.
  (Bytes of 0x80 and up become other characters, which never match the ASCII tags searched for.) }
function BytesToText(const Raw: AnsiString): String;
var
  I: Integer;
begin
  Result := '';
  for I := 1 to Length(Raw) do
    Result := Result + Chr(Ord(Raw[I]));
end;

{ Finds the text of <Tag>...</Tag> inside the (first) <display> element. }
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

{ Replaces the text of <Tag> inside <display> with NewValue, leaving every other byte as it is. }
function SetDisplayElement(var Raw: AnsiString; const Tag, NewValue: String): Boolean;
var
  S, L: Integer;
begin
  Result := LocateDisplayElement(BytesToText(Raw), Tag, S, L);
  if Result then
  begin
    Delete(Raw, S, L);
    Insert(AnsiString(NewValue), Raw, S);
  end;
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

{ Backs up game.xml (once), sets the display width, height and fullscreen mode, and records what
  changed. Returns '' on success, otherwise the reason. Nothing is touched unless the edit is possible. }
function ApplyRenderResolution(W, H: Integer): String;
var
  Raw: AnsiString;
  Path, CurW, CurH, CurMode: String;
  R, Old: TRenderResolutionRecord;
  CarryOver: Boolean;
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
  { If game.xml still holds what an earlier run wrote, the originals stay the player's real settings. }
  CarryOver := LoadRenderResolutionRecord(Old) and SameText(Old.GameXml, Path) and (Old.WrittenWidth = CurW) and
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

  if not (SetDisplayElement(Raw, 'width', R.WrittenWidth) and SetDisplayElement(Raw, 'height', R.WrittenHeight) and
          SetDisplayElement(Raw, 'fullscreenMode', R.WrittenFullscreenMode)) then
  begin
    Result := RenderUnreadableMessage;
    Exit;
  end;
  { Never overwrite an existing backup: it holds the settings from before the first edit ever. }
  if not FileExists(R.Backup) then
    if not FileCopy(Path, R.Backup, True) then
    begin
      Result := 'Could not back up ' + Path + ' to ' + R.Backup;
      Exit;
    end;
  if not SaveStringToFile(Path, Raw, False) then
  begin
    Result := 'Could not write ' + Path;
    Exit;
  end;
  if not SaveRenderResolutionRecord(R) then
    Result := 'Could not record the Render Resolution change in ' + GetRenderResolutionRecordPath;
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

{ The optional "Render Resolution" page: unchecked, fields pre-filled from game.xml, disabled with a
  message when game.xml is missing. A /FS25VR_RESOLUTION value pre-ticks and pre-fills it. }
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

{ Page validation: only checked values need to be valid. }
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

{ Decides whether the Render Resolution is applied and with what values. Returns an error message,
  or '' (RenderApply says whether to apply). Silent runs take /FS25VR_RESOLUTION; a missing game.xml
  skips the step in a silent run and is never an error. }
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

procedure InitializeWizard;
begin
  InitializeRenderResolutionPage;
end;

{ Before any file is placed, so a failure leaves the game folder and mods folder as they were. }
procedure CurStepChanged(CurStep: TSetupStep);
var
  Problem: String;
begin
  if (CurStep = ssInstall) and RenderApply then
  begin
    Problem := ApplyRenderResolution(RenderWidth, RenderHeight);
    if Problem <> '' then
    begin
      Log(Problem);
      if not WizardSilent then MsgBox(Problem, mbError, MB_OK);
      Abort;
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
  if Result then Result := RenderPageNextOk(CurPageID);
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
    Result := PrepareRenderResolution;
    if (Result <> '') and WizardSilent then
    begin
      Log(Result);
      Abort;
    end;
  end;
end;
