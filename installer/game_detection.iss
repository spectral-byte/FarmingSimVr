const
  SteamGameFolderName = 'Farming Simulator 25';
  SteamKey = 'SOFTWARE\Valve\Steam';
  GiantsKey = 'SOFTWARE\GIANTS Software\FarmingSimulator2025';
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{C85F7B59-39C3-48F5-94FB-38E291A376AE}_is1';
  StoreFolderMessage =
    'Microsoft Store / Game Pass installations of Farming Simulator 25 are not supported.' + #13#10#13#10 +
    'FS25 VR works with the Steam, Epic Games and GIANTS standalone versions of the game. ' +
    'Choose one of those, or the folder that contains x64\FarmingSimulator2025Game.exe.';

var
  Installations: array of String;
  InstallationSources: array of String;  { parallel to Installations: 'Steam', 'Epic Games', 'GIANTS' }
  InstallPickerPage: TInputOptionWizardPage;

function NormalizeDir(Dir: String): String;
begin
  Result := Trim(Dir);
  StringChangeEx(Result, '/', '\', True);
  Result := RemoveBackslashUnlessRoot(Result);
end;

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

function PreviousGameFolder: String;
begin
  if not RegQueryStringValue(HKLM, UninstallKey, 'Inno Setup: App Path', Result) then
    if not RegQueryStringValue(HKCU, UninstallKey, 'Inno Setup: App Path', Result) then
      Result := '';
  if (Result <> '') and not IsGameFolder(Result) then
    Result := '';
end;

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

procedure AcceptPickedInstallation(CurPageID: Integer);
begin
  if (InstallPickerPage <> nil) and (CurPageID = InstallPickerPage.ID) then
    WizardForm.DirEdit.Text := Installations[InstallPickerPage.SelectedValueIndex];
end;
