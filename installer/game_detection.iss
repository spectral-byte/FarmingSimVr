// Game installation detection for the Installer (spec #61, ticket #67). Textually #included into the
// [Code] section of fs25_vr.iss, after IsGameFolder (which it uses). Pascal Script, not ISPP.
// 
// Detection is not automated in CI: it reads the real machine. Keep it small and check it by hand
// with docs\installer-release-checklist.md before each release.
// 
// Sources, in order (a folder is only ever listed once, and only if IsGameFolder says it is valid):
//   Steam   registry InstallPath -> <Steam>\steamapps\libraryfolders.vdf -> every library's
//           steamapps\common\Farming Simulator 25
//   Epic    {commonappdata}\Epic\EpicGamesLauncher\Data\Manifests\*.item, "InstallLocation"
//           (plus the documented default folder)
//   GIANTS  registry InstallDir, falling back to the default install folders
// Microsoft Store / Game Pass folders are never listed and are rejected by IsStoreFolder.

const
  SteamGameFolderName = 'Farming Simulator 25';
  SteamKey = 'SOFTWARE\Valve\Steam';
  GiantsKey = 'SOFTWARE\GIANTS Software\FarmingSimulator2025';
  { AppId in fs25_vr.iss, with the _is1 suffix Inno gives its uninstall key. }
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{C85F7B59-39C3-48F5-94FB-38E291A376AE}_is1';
  StoreFolderMessage =
    'Microsoft Store / Game Pass installations of Farming Simulator 25 are not supported.' + #13#10#13#10 +
    'FS25 VR works with the Steam, Epic Games and GIANTS standalone versions of the game. ' +
    'Choose one of those, or the folder that contains x64\FarmingSimulator2025Game.exe.';

var
  Installations: array of String;        { valid game folders found, no duplicates }
  InstallationSources: array of String;  { parallel to Installations: 'Steam', 'Epic Games', 'GIANTS' }
  InstallPickerPage: TInputOptionWizardPage;

{ Folder as a clean backslash path without a trailing backslash (except for a drive root). }
function NormalizeDir(Dir: String): String;
begin
  Result := Trim(Dir);
  StringChangeEx(Result, '/', '\', True);
  Result := RemoveBackslashUnlessRoot(Result);
end;

{ Microsoft Store / Game Pass installs live under WindowsApps or XboxGames and carry a
  MicrosoftGame.config (Xbox app) or appxmanifest.xml (Store package), at the root or in Content. }
function IsStoreFolder(Dir: String): Boolean;
var
  D, Lower: String;
begin
  D := NormalizeDir(Dir);
  Lower := AddBackslash(Lowercase(D));
  Result :=
    (Pos('\windowsapps\', Lower) > 0) or (Pos('\xboxgames\', Lower) > 0) or
    FileExists(AddBackslash(D) + 'MicrosoftGame.config') or
    FileExists(AddBackslash(D) + 'appxmanifest.xml') or
    FileExists(AddBackslash(D) + 'Content\MicrosoftGame.config') or
    FileExists(AddBackslash(D) + 'Content\appxmanifest.xml');
end;

procedure AddInstallation(Dir, Source: String);
var
  I, N: Integer;
begin
  Dir := NormalizeDir(Dir);
  if (Dir = '') or IsStoreFolder(Dir) or not IsGameFolder(Dir) then
    Exit;
  N := GetArrayLength(Installations);
  for I := 0 to N - 1 do
    if CompareText(Installations[I], Dir) = 0 then
      Exit;
  SetArrayLength(Installations, N + 1);
  SetArrayLength(InstallationSources, N + 1);
  Installations[N] := Dir;
  InstallationSources[N] := Source;
  Log('Detected Farming Simulator 25 (' + Source + '): ' + Dir);
end;

{ The text between the first and the last quote after a leading quoted key, for both Steam's VDF
  ("path"		"D:\\SteamLibrary") and Epic's pretty-printed JSON ("InstallLocation": "C:\\Games\\FS25",).
  Backslashes arrive escaped (\\) and are undone. }
function QuotedValueAfterKey(Line, Key: String; var Value: String): Boolean;
var
  S: String;
  First, Last: Integer;
begin
  Result := False;
  S := Trim(Line);
  if Pos('"' + Key + '"', S) <> 1 then
    Exit;
  Delete(S, 1, Length(Key) + 2);
  First := Pos('"', S);
  if First = 0 then
    Exit;
  Delete(S, 1, First);
  Last := Length(S);
  while (Last > 0) and (S[Last] <> '"') do
    Last := Last - 1;
  if Last = 0 then
    Exit;
  Value := Copy(S, 1, Last - 1);
  StringChangeEx(Value, '\\', '\', True);
  StringChangeEx(Value, '\/', '/', True);
  Result := True;
end;

{ SteamDir's own library plus every "path" in its steamapps\libraryfolders.vdf (libraries on other drives). }
procedure DetectSteamLibraries(SteamDir: String);
var
  Lines: TArrayOfString;
  I: Integer;
  Value: String;
begin
  SteamDir := NormalizeDir(SteamDir);
  if SteamDir = '' then
    Exit;
  AddInstallation(SteamDir + '\steamapps\common\' + SteamGameFolderName, 'Steam');
  if LoadStringsFromFile(SteamDir + '\steamapps\libraryfolders.vdf', Lines) then
    for I := 0 to GetArrayLength(Lines) - 1 do
      if QuotedValueAfterKey(Lines[I], 'path', Value) then
        AddInstallation(NormalizeDir(Value) + '\steamapps\common\' + SteamGameFolderName, 'Steam');
end;

{ Each Epic Games Launcher manifest records where its game is installed. Whatever the game is, the
  folder only counts when it holds x64\FarmingSimulator2025Game.exe. }
procedure DetectEpicManifests(ManifestsDir: String);
var
  FindRec: TFindRec;
  Lines: TArrayOfString;
  I: Integer;
  Value: String;
begin
  ManifestsDir := NormalizeDir(ManifestsDir);
  if FindFirst(ManifestsDir + '\*.item', FindRec) then
  try
    repeat
      if LoadStringsFromFile(ManifestsDir + '\' + FindRec.Name, Lines) then
        for I := 0 to GetArrayLength(Lines) - 1 do
          if QuotedValueAfterKey(Lines[I], 'InstallLocation', Value) then
            AddInstallation(Value, 'Epic Games');
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

function QuerySteamDir: String;
begin
  if not RegQueryStringValue(HKLM32, SteamKey, 'InstallPath', Result) or (Result = '') then
    if not RegQueryStringValue(HKLM64, SteamKey, 'InstallPath', Result) or (Result = '') then
      if not RegQueryStringValue(HKCU, SteamKey, 'SteamPath', Result) then
        Result := '';
end;

{ The GIANTS installer's registry entry (InstallDir), then the default install folders. }
procedure DetectGiantsStandalone;
var
  Dir: String;
begin
  if RegQueryStringValue(HKLM32, GiantsKey, 'InstallDir', Dir) then AddInstallation(Dir, 'GIANTS');
  if RegQueryStringValue(HKLM64, GiantsKey, 'InstallDir', Dir) then AddInstallation(Dir, 'GIANTS');
  if RegQueryStringValue(HKCU, GiantsKey, 'InstallDir', Dir) then AddInstallation(Dir, 'GIANTS');
  AddInstallation(ExpandConstant('{commonpf64}') + '\Farming Simulator 2025', 'GIANTS');
  AddInstallation(ExpandConstant('{commonpf32}') + '\Farming Simulator 2025', 'GIANTS');
end;

procedure DetectInstallations;
begin
  SetArrayLength(Installations, 0);
  SetArrayLength(InstallationSources, 0);
  DetectSteamLibraries(QuerySteamDir);
  DetectEpicManifests(ExpandConstant('{commonappdata}') + '\Epic\EpicGamesLauncher\Data\Manifests');
  AddInstallation(ExpandConstant('{commonpf64}') + '\Epic Games\FarmingSimulator25', 'Epic Games');
  DetectGiantsStandalone;
end;

{ The folder a previous FS25 VR was installed to, if that is still a game folder. It wins over
  detection: an upgrade pre-selects where the player installed before. }
function PreviousGameFolder: String;
begin
  if not RegQueryStringValue(HKLM, UninstallKey, 'Inno Setup: App Path', Result) then
    if not RegQueryStringValue(HKCU, UninstallKey, 'Inno Setup: App Path', Result) then
      Result := '';
  if (Result <> '') and not IsGameFolder(Result) then
    Result := '';
end;

{ Called from InitializeWizard. Precedence for the game folder: /DIR= on the command line, then the
  previous install's folder, then detection (one installation is pre-selected, several get a
  picker page before the folder page). The folder page and its Browse button always remain. }
procedure InitGameDetection;
var
  I: Integer;
begin
  DetectInstallations;
  if ExpandConstant('{param:DIR|}') <> '' then
  begin
    Log('Game folder given with /DIR=; not pre-selecting a detected installation.');
    Exit;
  end;
  if PreviousGameFolder <> '' then
  begin
    Log('Upgrade: keeping the previous game folder ' + PreviousGameFolder);
    Exit;
  end;
  if GetArrayLength(Installations) = 1 then
    WizardForm.DirEdit.Text := Installations[0]
  else if GetArrayLength(Installations) > 1 then
  begin
    InstallPickerPage := CreateInputOptionPage(wpLicense,
      'Choose Installation', 'Which Farming Simulator 25 should get VR?',
      'More than one installation was found. Select the one you play. ' +
      'On the next page you can still browse to a different folder.',
      True, False);
    for I := 0 to GetArrayLength(Installations) - 1 do
      InstallPickerPage.Add(Installations[I] + '  (' + InstallationSources[I] + ')');
    InstallPickerPage.SelectedValueIndex := 0;
  end;
end;

{ Called from NextButtonClick: the picked installation becomes the folder shown on the folder page. }
procedure AcceptPickedInstallation(CurPageID: Integer);
begin
  if (InstallPickerPage <> nil) and (CurPageID = InstallPickerPage.ID) then
    WizardForm.DirEdit.Text := Installations[InstallPickerPage.SelectedValueIndex];
end;
