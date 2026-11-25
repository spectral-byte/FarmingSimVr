# Decisions behind the Installer

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0024](0024-unsigned-inno-setup-installer-and-static-runtime-injector.md), [ADR-0038](0038-the-injector-is-the-games-version-dll.md), [ADR-0039](0039-the-installer-turns-on-windows-crash-dumps-for-the-game.md).

## Context and Decision
This record holds the non-obvious decisions behind the Installer: finding the game, the mods folder and the Render Resolution, uninstalling, and the test that drives it. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `installer/fs25_vr.iss`

- **the file**:

  ```text
  Inno Setup script for the FS25 VR Installer (see CONTEXT.md: Installer, Injector, Companion Mod).
  Built by scripts\package_release.ps1, which passes:
    /DAppVersion=<modDesc version>   /DStageDir=<staged bundle folder>   /DOutputDir=<where to write the Setup exe>
  The staged bundle folder is the one the zip bundle is made from, so both carry identical files:
    x64\version.dll, mods\FS25_VRCompanion.zip, LICENSE, THIRD_PARTY_NOTICES.txt

  The Injector is the game's version.dll (docs/adr/0038-the-injector-is-the-games-version-dll.md). FS25 VR up to
  1.1.9 was the game's dxgi.dll instead: an upgrade removes that one and puts back a dxgi.dll it had moved aside
  (ReShade, for one). A dxgi.dll that is not ours is never touched.

  Command line (besides Inno's standard switches such as /VERYSILENT, /SUPPRESSMSGBOXES, /DIR=):
    /FS25VR_DOCUMENTS=<folder>   hidden, test-only: replaces the user's Documents folder
    /FS25VR_GAMEEXE=<file name>  hidden, test-only: replaces the game's exe file name (FarmingSimulator2025Game.exe)
                                 that Windows' crash dump key is written for (see below), so the Installer
                                 test never touches the key of the real game. Not a file name: no key.
    /FS25VR_NOCRASHDUMPS         leaves Windows' crash dump setting for the game alone (see below): no key and
                                 no record. An upgrade given the switch keeps what the install before it made.
                                 In a wizard run it unticks the Crash Dumps page's checkbox.
    /FS25VR_RESOLUTION=<W>x<H>   sets the game's Render Resolution (see below) without the wizard page.
                                 Whole numbers from 640 to 8192, e.g. /FS25VR_RESOLUTION=4000x2500.
                                 An invalid value fails the install before anything is placed. With no
                                 game.xml the install succeeds and the resolution is skipped (logged).
                                 If game.xml can't be edited once the files are placed (read-only, locked),
                                 the install is kept and Setup exits with code 20 (logged).
                                 In a wizard run it pre-ticks and pre-fills the Render Resolution page.
    /FS25VR_UNINSTALL            runs the existing uninstaller unattended instead of installing (no /DIR needed),
                                 waits for it and exits with its exit code. With nothing installed it logs
                                 "FS25 VR is not installed" and exits 1. Setup shows no window and
                                 no message box for it.
  Exit codes: 20 = game.xml could not be edited once the files were placed (see /FS25VR_RESOLUTION above);
    21 = the files are placed, but the dxgi.dll of an earlier FS25 VR version could not be deleted (logged);
    the new Injector stays inactive while it is there. The Windows crash dump setting never changes the exit code.

  Running Setup interactively while FS25 VR is installed first asks: Upgrade or reinstall / Uninstall / Cancel
  (see "Uninstalling through the Setup exe" in [Code]). Silent runs never ask and keep upgrading.

  Render Resolution
    The game's settings file is <Documents>\My Games\FarmingSimulator2025\game.xml. It is NOT
    gameSettings.xml (that one holds modsDirectoryOverride). Shape of game.xml (UTF-8 with a BOM, CRLF
    line endings, 4-space indent):
      <game>
        <graphic>
          <display>
            <width>3840</width>  <height>2160</height>  <fullscreenMode>windowed</fullscreenMode>
            ... previousWidth, previousHeight, previousFullscreenMode and more display elements ...
    The Installer changes the text of exactly three elements inside <display>: <width>, <height> and
    <fullscreenMode> (set to "windowed"). It edits the bytes of the file in place, not through an XML
    serializer, so everything else (BOM, line endings, indentation, the previous* elements) stays
    identical. Before the first edit ever, game.xml is copied to game.xml.fs25vr-backup next to it; an
    existing backup is never overwritten.

  Render Resolution record (read again by later installs and by the uninstall restore)
    {commonappdata}\FS25 VR\render-resolution.ini, next to the uninstaller. UTF-8 with a BOM, one
    Key=Value per line after a [RenderResolution] header:
      GameXml=<full path of the game.xml that was edited>
      Backup=<full path of game.xml.fs25vr-backup>
      OriginalWidth= OriginalHeight= OriginalFullscreenMode=      what game.xml held before the Installer
      WrittenWidth=  WrittenHeight=  WrittenFullscreenMode=       what the Installer wrote
    Restore rule: if game.xml's display width, height and fullscreenMode all still equal the
    Written* values, write the Original* values back; otherwise leave game.xml alone. The record is
    only written when an edit is applied. When an Installer run applies again and game.xml still holds
    the previous Written* values, the Original* values are carried over (so they stay the player's
    real settings); otherwise the values found in game.xml become the new Original* values.
    Pascal helpers in [Code]: TRenderResolutionRecord, GetRenderResolutionRecordPath,
    LoadRenderResolutionRecord (usable from the uninstaller), ReadDisplaySettings, WriteDisplaySettings.
    The uninstall (RestoreRenderResolution) applies that rule to the game.xml named in the
    record, then deletes the record. It never touches game.xml.fs25vr-backup or the Companion Mod's
    modSettings. With no record (the Installer never set a Render Resolution) it does nothing.

  Windows' own crash dumps for the game (docs/adr/0039-the-installer-turns-on-windows-crash-dumps-for-the-game.md)
    Windows Error Reporting writes a dump for an application when a key named after its exe exists under
      HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps
    in the 64-bit registry view. After the files are placed, an administrative install creates it for
    FarmingSimulator2025Game.exe with three DWORD values: DumpType 0 (custom), CustomDumpFlags 0x1060 (thread
    information, the memory the stacks point at, unloaded modules: the contents of the Injector's own dump) and
    DumpCount 3. No DumpFolder, so Windows' default CrashDumps folder applies. It is on unless
    /FS25VR_NOCRASHDUMPS is given. The wizard has a "Crash Dumps" page after the Render Resolution page with
    one checkbox, ticked (unticked when the switch is given), that says what Windows keeps and where; a silent
    run has no page and is answered by the switch alone. A per-user run (/CURRENTUSER) cannot write
    HKEY_LOCAL_MACHINE: it has no page, and it logs that the setting was skipped. Unticked is the same as the
    switch: no key and no record. Nothing here ever fails the install.
    A key that already exists for the exe is the player's or another tool's: install and uninstall leave it
    alone (logged).

  Crash dump record (read again by later installs and by the uninstall)
    {commonappdata}\FS25 VR\crash-dumps.ini, next to the uninstaller. UTF-8 with a BOM, one Key=Value per line
    after a [CrashDumps] header:
      ExeName=<exe file name the key was written for>   (the uninstaller does not get /FS25VR_GAMEEXE)
      WrittenDumpType= WrittenCustomDumpFlags= WrittenDumpCount=   the numbers the Installer wrote
      CreatedLocalDumpsKey=<1 | 0>   whether the LocalDumps key itself was not there before the write
    The record is written before the key and deleted again if the key cannot be written. Remove rule: the
    uninstall deletes the exe's key only when a record exists and the key still holds exactly those three
    values (no other value, no subkey); then, if CreatedLocalDumpsKey is 1, it deletes LocalDumps too unless
    something else has gone into it. Microsoft does not say what a LocalDumps key with nothing in it does (it
    may keep crash dumps on for every application), so the Installer leaves the registry as it found it
    (docs/adr/0039, item 8). Either way the record is deleted. An upgrade or reinstall keeps the key and its
    record while the key is still its own, writes the key when there is none (and, when the earlier record says
    the install created LocalDumps and LocalDumps is still empty, carries CreatedLocalDumpsKey=1 forward), and
    for a key that was changed since leaves it alone and deletes the record.
    Pascal helpers in [Code]: TCrashDumpRecord, SetUpCrashDumps, RemoveCrashDumps, CrashDumpKeyIsUnchanged.

  English only, requires admin, unsigned (see docs/adr/0024-unsigned-inno-setup-installer-and-static-runtime-injector.md).
  ```

- **`AppId={{C85F7B59-39C3-48F5-94FB-38E291A376AE}`**: Fixed application ID: a newer Installer upgrades the existing install under one Add/Remove Programs entry. scripts\test_installer.ps1 looks the uninstall entry up by this ID.
- **`UsePreviousAppDir=yes`**: Upgrades: with the fixed AppId above, a newer Installer starts on the game folder used last time. Precedence for the folder shown/used: /DIR= on the command line, then that previous folder, then DefaultDirName (which game detection may refine). Inno applies this order itself.
- **`UninstallFilesDir={commonappdata}\FS25 VR`**: Keep the uninstaller out of the game folder.
- **`PrivilegesRequired=admin`**: Admin by default. Allowing /CURRENTUSER on the command line only lets the test script run without a UAC prompt from a non-elevated shell; the wizard never offers it.
- **`UsedUserAreasWarning=no`**: The mods folder and game.xml live under the player's Documents (a user area) even though we run elevated. GetDocumentsDir asks the player's own process for that folder instead of using {userdocs}, which is the administrator's when a standard user elevates with an administrator's credentials.
- **`Type: files; Name: "{app}\x64\fs25_vr.log"`**: Written by the Injector while the game runs, and when it crashes (the minidump next to the log, under a fixed name). modSettings (the VR settings) is deliberately kept.
- **`Type: dirifempty; Name: "{commonappdata}\FS25 VR"`**: The folder holding the uninstaller, the notices and (while they are in place) the Render Resolution and crash dump records is created by the Installer's own code before Inno places files in it, so Inno would leave it behind empty.
- **`GameFolderProblem`**: Empty when Dir can be installed to; otherwise the reason it cannot. A Microsoft Store / Game Pass folder is reported as unsupported rather than as "not a Farming Simulator 25 installation".
- **`QueryOriginalUserDocuments`**: Asks a process of the user who started Setup (normally not elevated) for their Documents folder. That process writes the answer into a folder in Setup's temporary folder, which belongs to the account Setup runs as, so the folder is opened to Authenticated Users first; it goes away with the temporary folder. '' (logged) when that fails.
- **`GetDocumentsDir`**: The Documents folder of the player who started Setup (honors OneDrive redirection through the known folder), unless the hidden test-only /FS25VR_DOCUMENTS switch replaces it. In administrative install mode Setup may run as another account than the player's (a standard user who elevates with an administrator's credentials), and the userdocs constant is then the administrator's Documents; so the player's own process is asked, with userdocs as the fallback. Install only (the uninstaller works from recorded paths).
- **`GetModsDirOverride`**: The directory of the game's active mods directory override in gameSettings.xml (not game.xml, which only holds the display settings), or '' when gameSettings.xml is missing/unreadable or has no override element, or the override is inactive or has no directory. The file is only read, never written.
- **`GetModsDir`**: Where the Companion Mod goes: the active mods directory override if there is one, else the default mods folder. Inno records the resolved path with the file, so uninstall removes the mod from wherever it was installed.
- **`IsGameRunning`**: True if a process whose image name is FarmingSimulator2025Game.exe is running (any installation, any user: the Installer must not run while the game holds the Injector or can rewrite game.xml). WMI gives the image name without needing process handles. If WMI cannot be queried we cannot tell, so we log it and carry on rather than block every install.
- **`EnsureGameNotRunning`**: Shared by install and uninstall. Returns True when the game is not running (possibly after the player closed it and chose Retry). Returns False when the caller must stop: the player chose Cancel, or this is a silent run, which logs the reason and fails instead of waiting. A silent run must never show a message box. /SUPPRESSMSGBOXES without /SILENT answers Cancel, so it cannot loop forever.
- **`const`**: The Injector's file in the game's x64 folder, and the one an earlier version used The Injector is version.dll. Ours there (recognised by its version resource) is simply overwritten. Any other version.dll is moved to version.dll.fs25vr-backup first and put back by the uninstaller. An existing backup is never overwritten, so an upgrade can't replace the player's original DLL with an older copy of ours. FS25 VR up to 1.1.9 was dxgi.dll, and moved a foreign dxgi.dll (ReShade, ...) to dxgi.dll.fs25vr-backup the same way. Once the files are placed, an upgrade deletes that dxgi.dll of ours (while it is there, the new Injector stays inactive) and puts the backup back. A dxgi.dll that is not ours is left alone.
- **`KeepSuffix = '.fs25vr-keep';`**: A DLL the player put in place of ours after installing, kept from the uninstall's file removal.
- **`if Pos(#0, Result) > 0 then Result := Copy(Result, 1, Pos(#0, Result) - 1);`**: ValueLen counts the terminating null; cut the string at it
- **`IsLegacyInjector`**: An Injector from before the version resource (the v1.0.0 zip bundle and builds up to this Installer) has no ProductName. It carries the name of the log file it writes, fs25_vr.log, as text (ASCII in v1.0.0, UTF-16 in later builds), which no other DLL does. Those Injectors were all dxgi.dll.
- **`FilesPlaced: Boolean;`**: set at ssPostInstall: from then on the uninstaller puts that version.dll back
- **`PrepareExistingInjector`**: Called from PrepareToInstall for the chosen game folder. Moves a foreign version.dll aside (warning the player) and returns '' on success; returns the reason when it can't be done safely.
- **`SetAsideReplacedDll`**: Uninstall, before Inno removes the files: Inno deletes x64\version.dll by name, whatever it is by then, and x64\dxgi.dll too if an earlier FS25 VR version installed it. If the player has put a DLL of their own there since (ReShade's dxgi.dll, for one), move it out of the way so it survives; RestoreForeignDll puts it back.
- **`RestoreForeignDll`**: Uninstall, an upgrade from an earlier version, and a rolled-back install: put the player's DLL back once ours is gone. One the player put there after installing (set aside by SetAsideReplacedDll) wins over the older one moved aside at install, which then stays under its backup name.
- **`RemoveEarlierInjector`**: Deletes the dxgi.dll of an earlier FS25 VR version, if there is one, and puts back the dxgi.dll it had moved aside. A dxgi.dll that is not ours (ReShade's, ...) is left alone. False when ours is there and could not be deleted.
- **`if CurUninstallStep = usUninstall then`**: Before Inno removes the files, so the records are gone and their folder (common appdata, FS25 VR)

  - is removed along with them, and so a DLL that is not ours is out of the way of the removal.

- **`RemoveEarlierInjector(ExpandConstant('{app}'));`**: An earlier version's dxgi.dll that is still there (it was not in this install's file list, or could

  - not be deleted at upgrade) goes too, and what it had moved aside comes back.

- **`BytesToText`**: One character per byte, for searching only: positions in the result are byte positions in Raw. (Bytes of 0x80 and up become other characters, which never match the ASCII tags searched for.)
- **`SetDisplayElement`**: Replaces the text of \<Tag> inside \<display> with NewValue in both Raw and its BytesToText copy Text, leaving every other byte as it is. Text is edited alongside so it need not be rebuilt for the next tag.
- **`ApplyRenderResolution`**: Backs up game.xml (once), sets the display width, height and fullscreen mode, and records what changed. Returns '' on success, otherwise the reason. Nothing is touched unless the edit is possible.
- **`HadOld := LoadRenderResolutionRecord(Old);`**: If game.xml still holds what an earlier run wrote, the originals stay the player's real settings.
- **`if not FileExists(R.Backup) then`**: Never overwrite an existing backup: it holds the settings from before the first edit ever.
- **`if not SaveRenderResolutionRecord(R) then`**:

  ```text
  The record goes first: if writing game.xml then fails, the record names values game.xml does not
     hold and the uninstall leaves game.xml alone. The other way round, game.xml could be changed
     with nothing recorded to undo it.
  ```

- **`if HadOld then`**: game.xml is as it was, so the record goes back to describing it: the earlier record (whose values

  - the uninstall may still restore), or none.

- **`RestoreRenderResolution`**: Uninstall: undo the Render Resolution change, but only while it is still ours. Without the Injector there is no window clamping (ADR-0014), so a headset-sized window would be left behind. If game.xml's display width, height and fullscreen mode all still equal what the Installer wrote, the recorded originals are written back; if the player changed any of them, game.xml is left alone. The game.xml named in the record is used (the uninstaller does not get the /FS25VR_DOCUMENTS switch). The game.xml backup is never touched. The record is deleted once handled. Failures are logged, never shown: the rest of the uninstall must go on.
- **`InitializeRenderResolutionPage`**: The optional "Render Resolution" page: unchecked, fields pre-filled from game.xml, disabled with a message when game.xml is missing. A /FS25VR_RESOLUTION value pre-ticks and pre-fills it.
- **`PrepareRenderResolution`**: Decides whether the Render Resolution is applied and with what values. Returns an error message, or '' (RenderApply says whether to apply). Silent runs take /FS25VR_RESOLUTION; a missing game.xml skips the step in a silent run and is never an error.
- **`CrashDumpType = 0;`**:

  ```text
  What is written for the game's exe. DumpType 0 is a custom dump, whose contents CustomDumpFlags names (a
     MINIDUMP_TYPE mask): thread information $1000, the memory the stacks point at $40 and the unloaded
     modules $20, on top of the stacks and the module list every minidump holds. Those are the contents of
     the Injector's own dump. No DumpFolder: Windows' default CrashDumps folder applies.
  ```

- **`TCrashDumpRecord = record`**:

  ```text
  What the Installer recorded about the key it made. ExeName is empty when there is no record. The Written
     values are the numbers it wrote, as text. CreatedLocalDumpsKey is '1' when the LocalDumps key itself was
     not there and the Installer's write created it (what Windows does with a LocalDumps key that has nothing
     in it is not documented, so the uninstall takes it away again when it is empty: docs/adr/0039, item 8).
  ```

- **`CrashDumpsRoot`**: Where the key lives: the 64-bit view of HKEY_LOCAL_MACHINE, which is the one Windows Error Reporting reads.
- **`IsPlainFileName`**: A registry key name made from the exe name must not reach into a different key.
- **`CrashDumpsAllowed`**: True when this run may write a machine-wide setting: only an administrative install can.
- **`CrashDumpsOffered: Boolean;`**: True when the wizard offers the checkbox: only in the administrative install mode, the only one that can

  - write the setting. A per-user run has no page and no checkbox.

- **`CrashDumpsAnsweredByWizard`**: The wizard's checkbox answers when it was offered and the player sees the wizard; a silent run is answered by the command line alone.
- **`WantCrashDumps`**: Whether the player wants the setting. In a wizard run it is the checkbox, which starts ticked and starts unticked when /FS25VR_NOCRASHDUMPS is given; in a silent run it is yes unless that switch is given.
- **`InitializeCrashDumpsPage`**: The optional "Crash Dumps" page: one checkbox, ticked, and what it does. /FS25VR_NOCRASHDUMPS unticks it, as /FS25VR_RESOLUTION pre-ticks the Render Resolution page. Created only when this run can write the setting, so a per-user run never sees it.
- **`GetCrashDumpExeName`**: The exe file name the key is written for: the game's, unless the hidden test-only /FS25VR_GAMEEXE switch replaces it (as /FS25VR_DOCUMENTS replaces the Documents folder), so the Installer test never touches the key of the real game.
- **`CrashDumpKeyIsUnchanged`**: True when the exe's key holds exactly what the record says the Installer wrote: the three DWORD values and nothing else (no other value, no subkey). Any change by the player or another tool makes it theirs.
- **`SetUpCrashDumps`**: Turns on Windows' own crash dumps for the game. Called once the files are placed, in the administrative install mode only. It never fails the install: whatever it cannot do is logged. The record goes first and is deleted again if the key cannot be written, so a record never names a key that is not there.
- **`if RegKeyExists(CrashDumpsRoot, Key) then`**:

  ```text
  A key that is already there is left exactly as it is, and so is its record. It is ours when an earlier
     run recorded it and it still holds what was written (an upgrade or a reinstall): it stays, with its
     record. Otherwise it is the player's or another tool's, and a record of an earlier key of ours that
     was changed since no longer describes anything of ours.
  ```

- **`R.CreatedLocalDumpsKey := '1';`**: The game's key is gone, but LocalDumps is still the empty key an earlier run of this Installer created:

  - the new record carries that on, so the uninstall that follows takes LocalDumps away too.

- **`RemoveCrashDumps`**: Uninstall: remove the key, but only while it is still ours. It is when a record exists and the key still holds exactly what the Installer wrote; otherwise it is the player's (or another tool's) and stays. The record is deleted either way. The record names the exe (the uninstaller does not get the hidden /FS25VR_GAMEEXE switch). Failures are logged, never shown: the rest of the uninstall must go on.
- **`if R.CreatedLocalDumpsKey = '1' then`**: The LocalDumps key this Installer's write created goes too, unless something else has gone into it.
- **`EarlierInjectorLeftExitCode = 21;`**: The files are placed, but the dxgi.dll of an earlier FS25 VR version could not be deleted, and the new

  - Injector stays inactive while it is there.

- **`CurStepChanged`**: The Render Resolution is applied after the files are placed (PrepareToInstall has already checked everything that can be checked), so a failure while placing files, such as a full disk, never leaves game.xml edited for an install that was rolled back. A failure here comes too late to fail the install (Abort does not stop Setup at ssPostInstall): the install is kept, the player is told, and Setup ends with RenderResolutionFailedExitCode (GetCustomSetupExitCode) so scripts notice.
- **`if not RemoveEarlierInjector(ExpandConstant('{app}')) then`**: Only now that the new Injector is in place: a failed install keeps the earlier one working.
- **`DeinitializeSetup`**: A foreign version.dll was moved aside but the files were never placed (the install was cancelled, or failed while placing files and was rolled back): no uninstaller is registered to put it back, so put it back now.
- **`PrepareToInstall`**: Also covers silent installs, where there is no directory page. Everything that can refuse the install runs first (game folder, game running, Render Resolution input and game.xml); only then is the player's own version.dll moved aside, so a refusal never leaves it moved. A refusal is returned, so a wizard run stops on the Preparing to Install page with the reason (Back and Cancel stay available). A silent run must not wait on a message box, but for a returned reason Inno shows one unless /SUPPRESSMSGBOXES is given; so a silent run logs the reason and raises Abort instead, which ends Setup the same way Inno's own handling of a returned reason does after its box.
- **`InitializeUninstall`**: Uninstall must not run over a live game either (it holds the Injector and rewrites game.xml on exit).
- **`ExitProcess`**: Uninstalling through the Setup exe Running Setup while FS25 VR is installed asks first (interactive runs only): upgrade or reinstall, uninstall, or cancel. Uninstall starts the existing uninstaller with its normal windows and Setup ends. A silent run never asks and goes on upgrading. /FS25VR_UNINSTALL runs the existing uninstaller unattended instead of installing, waits until it has finished and ends with its exit code (1 when FS25 VR is not installed), so scripts and CI can uninstall with the same exe they installed with. Both go through the uninstaller Inno registered (the UninstallString of the Add/Remove Programs entry), so everything the uninstaller does (the game-running check, the restores) happens exactly as when it is started from Windows Settings.
- **`HasCommandLineSwitch`**: /Name or -Name on the command line (Inno's own param constant only finds the /Name=value form).
- **`IsUnattendedRun`**: True when Setup must never show a question. WizardSilent is not relied on alone because this runs in InitializeSetup, before the wizard exists.
- **`FindExistingUninstaller`**: The uninstaller of an existing FS25 VR install, from Inno's Add/Remove Programs entry. Looks in the machine and the current user hive, in the 64-bit and the 32-bit view of each. False (with a log line) when there is no entry or the uninstaller it names is gone.
- **`RunExistingUninstaller`**: Runs the uninstaller, waits for it and returns its exit code (1 if it could not be started). Inno's uninstaller hands the work to a temporary copy and exits when that copy removes the original, last of all; so after a success wait until the original is really gone before reporting it.
- **`UninstallThroughSwitch`**: /FS25VR_UNINSTALL: very silent (plain /SILENT when Setup itself was given /SILENT, which only adds a progress window), never asking, never restarting. Returns the exit code Setup ends with.
- **`AskUpgradeOrUninstall`**: IDYES = upgrade or reinstall, IDNO = uninstall, IDCANCEL = cancel (also the window's close button). Only called for interactive runs: /SUPPRESSMSGBOXES does nothing without /SILENT or /VERYSILENT.
- **`SetArrayLength(Labels, 2);`**: Each label is the button text, a line break, then the note under it. (Built here because a line

  - that starts with "[" is read as a section header.)

- **`DelTree(ExpandConstant('{tmp}'), True, True, True);`**: Setup has no other way to end with the uninstaller's exit code. ExitProcess skips Setup's own

  - cleanup, so remove its temporary folder first.

### `installer/game_detection.iss`

- **the file**:

  ```text
  Game installation detection for the Installer. Textually #included into the
  [Code] section of fs25_vr.iss, after IsGameFolder (which it uses). Pascal Script, not ISPP.

  Detection is not automated in CI: it reads the real machine. Keep it small and check it by hand
  with docs\installer-release-checklist.md before each release.

  Sources, in order (a folder is only ever listed once, and only if IsGameFolder says it is valid):
    Steam   registry InstallPath -> <Steam>\steamapps\libraryfolders.vdf -> every library's
            steamapps\common\Farming Simulator 25
    Epic    {commonappdata}\Epic\EpicGamesLauncher\Data\Manifests\*.item, "InstallLocation"
            (plus the documented default folder)
    GIANTS  registry InstallDir, falling back to the default install folders
  Microsoft Store / Game Pass folders are never listed and are rejected by IsStoreFolder.
  ```

- **`UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{C85F7B59...`**: AppId in fs25_vr.iss, with the \_is1 suffix Inno gives its uninstall key.
- **`IsStoreFolder`**: Microsoft Store / Game Pass installs live under WindowsApps or XboxGames and carry a MicrosoftGame.config (Xbox app) or appxmanifest.xml (Store package), at the root or in Content.
- **`QuotedValueAfterKey`**: The text between the first and the last quote after a leading quoted key, for both Steam's VDF ("path" "D:\\SteamLibrary") and Epic's pretty-printed JSON ("InstallLocation": "C:\\Games\\FS25",). Backslashes arrive escaped (\\) and are undone.
- **`DetectEpicManifests`**: Each Epic Games Launcher manifest records where its game is installed. Whatever the game is, the folder only counts when it holds x64\FarmingSimulator2025Game.exe.
- **`PreviousGameFolder`**: The folder a previous FS25 VR was installed to, if that is still a game folder. It wins over detection: an upgrade pre-selects where the player installed before.
- **`InitGameDetection`**: Called from InitializeWizard. Precedence for the game folder: /DIR= on the command line, then the previous install's folder, then detection (one installation is pre-selected, several get a picker page before the folder page). The folder page and its Browse button always remain.

### `scripts/test_installer.ps1`

- **the file**: Drives the built Installer (FS25_VR_v\<version>_Setup.exe) silently against a throwaway fake game folder and a throwaway Documents folder, then inspects the files on disk. Run it after scripts\package_release.ps1. Fails (non-zero exit) if any assertion fails. No Pester.

  The Installer needs admin. CI runs elevated and exercises the real path; when this script is run from a non-elevated shell it passes /CURRENTUSER (which the Installer allows on the command line only) so it can run without a UAC prompt, and looks for the uninstall entry in HKCU instead.

  Windows' own crash dumps for the game are a key under HKEY_LOCAL_MACHINE, which only an elevated Setup can write. The cases that need that run only in an elevated shell and are reported as [SKIP], never as [PASS], in a non-elevated one (Test-Case -Needs Elevated). The case for the per-user run passes /CURRENTUSER itself, so it runs in both kinds of shell. Every Setup run is given a throwaway game exe name (the hidden /FS25VR_GAMEEXE switch, see New-TestWorld), so the key of the real FarmingSimulator2025Game.exe is never read or written.

  On GitHub Actions (GITHUB_ACTIONS is "true"), which is elevated, nothing may be skipped: a skipped case there means the elevated cases are not running, and the script then fails instead of reporting success.

  Extending: add a Test-Case at the bottom. Build the world with New-FakeGame / New-FakeDocuments, drive it with Install-Fs25Vr / Uninstall-Fs25Vr (both always clean up via Remove-TestWorld), assert with the Assert-\* helpers.

- **`$uninstallKeyPaths`**: Where an install can be found. An elevated run also makes per-user installs (/CURRENTUSER, see the per-user case), which register under HKCU.
- **`$sampleGameXml`**: A game.xml shaped like the game's (\<game>\<graphic>\<display>), used as the untouched-settings baseline. It holds the display width, height and fullscreen mode only; the mods directory override lives in gameSettings.xml, not here.
- **`$sampleGameSettingsXml`**: A gameSettings.xml shaped like the game's: the mods directory override is its second line, here in the state a real machine has after the player once looked at the setting (inactive, directory left over).
- **`New-TestWorld`, `Game      = Join-Path $root "Game folder"`**: spaces on purpose
- **`New-TestWorld`, `ExeName   = "fs25vr-test-" + [guid]::NewGuid().ToString("N").Substring(0, 8) ...`**: The exe name every Setup run writes Windows' crash dump key for (the hidden /FS25VR_GAMEEXE switch). Unique per world, never the real game's.
- **`New-RealGameXml`**: A game.xml in the shape the game really writes (UTF-8 with BOM, CRLF line endings, 4-space indent; copied from a real FS25 game.xml). The display \<width>, \<height> and \<fullscreenMode> are the elements the Render Resolution step edits; \<previousWidth>, \<previousHeight> and \<previousFullscreenMode> sit next to them and must stay as they are. The expected file after an edit is rendered from this same template with different numbers, so the assertions do not depend on how the Installer edits.
- **`$text`**: Real non-ASCII text (this script stays ASCII), so UTF-8 multi-byte sequences sit in the file and must survive an edit byte for byte.
- **`Wait-GuardedProcess`**: Waits for a process, but never forever: a stray dialog in a silent run must fail the case, not hang CI.
- **`Install-Fs25Vr`, `if (-not $isAdmin -and $setupArgs -notcontains "/CURRENTUSER") { $setupArgs +...`**: A non-elevated run can only install per user; an elevated one does so when a case asks for /CURRENTUSER.
- **`Uninstall-Fs25Vr`**: Runs the registered uninstaller silently and waits until it is really gone: Inno's uninstaller hands over to a temporary copy of itself, so the first process can exit before the work is done.
- **`Uninstall-Fs25Vr`, `while ((Test-Path $uninstaller) -and (Get-Date) -lt $deadline) { Start-Sleep ...`**: The registry entry goes first; the uninstaller's own [Code] (restoring a backed-up version.dll, ...) runs after it. The uninstaller deletes itself last, so wait for that.
- **`Invoke-Uninstaller`**: Runs the registered uninstaller silently once and returns its exit code, without requiring that it succeeds (Uninstall-Fs25Vr throws unless the Add/Remove Programs entry disappears). Then waits for Inno's temporary second-phase copy to be gone, so the files can be inspected.
- **`$script:standIn = $null`**: A stand-in for the running game: a copy of a harmless Windows exe named FarmingSimulator2025Game.exe, running for a long time. The Installer must detect the game by process image name, so this is enough.
- **`$localDumpsKeyPath`**: Windows' own crash dumps for the game: the per-application key under Windows Error Reporting's LocalDumps key in HKEY_LOCAL_MACHINE (64-bit view), and the Installer's record of what it wrote, next to the uninstaller. Every helper refuses the real game's exe name: the tests only ever use a throwaway one.
- **`Remove-TestWorld`, `Stop-StandInGame`**: a running game would (rightly) block the cleanup uninstall
- **`Test-Case`**: $data (optional) is handed to the body as its second argument, for cases generated in a loop. -Needs Elevated marks a case that writes or reads machine-wide state only an elevated Setup can write. In a non-elevated run it is reported as [SKIP], not [PASS], and its body does not run. (Every other case runs in both kinds of run; the per-user case drives /CURRENTUSER itself so that elevated CI runs it too.)
- **`if (Get-UninstallEntry) { Write-Error "FS25 VR is already installed on this m...`**: Refuse to run over a real FS25 VR install: the fixed application ID means we would upgrade it.
- **`$localDumpsExistedAtStart`**: Whether Windows Error Reporting's LocalDumps key was there before this run. An install creates it when it is not (and the uninstall takes it away again); if a failing case left it behind, the end of the run does.
- **`New-GameSettingsWithOverride`**: Mods directory override The override lives in gameSettings.xml (\<modsDirectoryOverride active="..." directory="..."/>), not in game.xml. game.xml only holds the display settings the Render Resolution step edits.
- **`Get-ForeignDllBytes`**: A real version.dll that is not ours: the copy of Windows' own one (it has a version resource, just a different product).
- **`foreach ($encoding in @("ASCII", "UTF-16")) {`**: An Injector from before the version resource (the v1.0.0 zip bundle carries "fs25_vr.log" as ASCII, later builds as UTF-16) is ours too, and was a dxgi.dll: the upgrade deletes it like any earlier Injector.
- **`Set-Content -Path $p.DefaultMods -Value "in the way" -Encoding ascii`**: A file where the mods folder must go: the Companion Mod cannot be placed, so Setup rolls back.
- **`Set-Content -Path $p.DefaultMods -Value "in the way" -Encoding ascii`**: A file where the mods folder must go: the Companion Mod cannot be placed, so Setup rolls back after the foreign version.dll was moved aside.
- **`Test-Case "A Game Pass folder (XboxGames\...\Content with MicrosoftGame.confi...`**: Game installation detection reads the real machine, so only the Game Pass rejection is automated; the rest is docs\installer-release-checklist.md. A silent run must refuse a Store folder with a logged reason, never a message box (Wait-GuardedProcess fails the case if a dialog hangs Setup).
- **`Assert-ForeignDllUntouched`**: A failed install must leave the player's own version.dll (another mod's) exactly where it was: under its original name, with no backup made, because nothing is registered to put it back.
- **`Set-Content -Path $p.DefaultMods -Value "in the way" -Encoding ascii`**: A file where the mods folder must go: the Companion Mod cannot be placed, so Setup fails after validation.
- **`$world.Documents = Join-Path $world.Root ("Dokumente " + [char]0xF6 + [char]0...`**: A Documents path with non-ASCII characters checks that the record survives the round trip (the Installer re-reads it to decide which originals to keep).
- **`foreach ($changed in @(`**: Each of the three values on its own: a change to any one means the player's choice wins.
- **`Set-RealGameXml $world "4000" "2500" "windowed"`**: the player's own values, even if they look like a headset size
- **`Invoke-SetupWithArgs`**: Setup offers to uninstall Running the Setup exe while FS25 VR is installed asks (interactive runs only) whether to upgrade, uninstall or cancel; the question needs a person, so only the unattended parts are automated here (see docs\installer-release-checklist.md for the dialog). /FS25VR_UNINSTALL runs the existing uninstaller unattended through Setup; silent runs never ask and keep upgrading.
- **`Invoke-SetupWithArgs`**: Runs the Setup exe with exactly these arguments (plus /LOG, /FS25VR_DOCUMENTS and, outside an elevated shell, /CURRENTUSER) and returns its exit code. Unlike Install-Fs25Vr it does not add /SUPPRESSMSGBOXES or /DIR, so a message box the Installer should not show would hang and fail the case.
- **`Get-SetupTempFolders`**: Setup /FS25VR_UNINSTALL, run the way CI would. It must not need /DIR. Setup ends itself early for this switch, so also check that it left none of its temporary is-\*.tmp folders behind. (The uninstaller's own is-\*-uninstall.tmp folder is removed by its temporary copy a moment after Setup returns; not checked.)
- **`Assert-Missing $p.Injector "Injector"`**: Checked right away: Setup must wait until the uninstaller is completely done.
- **`$exit`**: a message box here would hang and fail the case
- **`foreach ($mode in @("/VERYSILENT", "/SILENT")) {`**: Silent runs never show the upgrade/uninstall question. These run WITHOUT /SUPPRESSMSGBOXES (which would answer the question for them), so a question shown anyway hangs Setup and fails the case.
- **`Test-Case "A per-user install writes no Windows crash dump key, logs that the...`**: Windows' own crash dumps for the game The Installer turns them on by creating the game exe's key under Windows Error Reporting's LocalDumps key (HKEY_LOCAL_MACHINE, 64-bit view) and records what it wrote next to the uninstaller (docs\adr\0039-...). Only an administrative install can write it. Every Setup run here is given a throwaway exe name (/FS25VR_GAMEEXE), so the real game's key is never read or written; see the header of this script for why some of these cases are skipped outside an elevated run.
- **`Test-Case "A per-user install writes no Windows crash dump key, logs that the...`**: Runs in an elevated shell too: /CURRENTUSER makes Setup install in the non administrative install mode there as well (Inno Setup, "Setup Command Line Parameters"), which registers under HKCU and cannot write the key.
- **`Test-Case "A silent install given /FS25VR_NOCRASHDUMPS logs that the switch l...`**: A silent run is answered by the command line alone: the wizard's Crash Dumps checkbox (ticked by default, not offered at all in a per-user run) takes no part in it. With the switch, the log names the switch in an elevated and in a per-user run alike, and no key and no record appear either way, not even by the uninstall. This one runs in both.
- **`Assert-CrashDumpKeyIsOurs`**: What the Installer writes for the game's exe, spelled out here from the spec and not from the Installer: a custom dump (DumpType 0) with the Injector's own dump contents (CustomDumpFlags 0x1060 = MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules, next to the stacks and the module list every minidump holds), 3 dumps kept, and no DumpFolder (Windows' default CrashDumps folder applies).
- **`foreach ($existing in @(`**: A key that was there before the Installer: the player's own, or another tool's. Either way it is theirs.
- **`Test-Case "The Installer records the crash dump key it wrote next to the unin...`**: Like the Render Resolution: the Installer records what it wrote next to the uninstaller, the uninstall removes the key only while it still holds exactly that, and the record is deleted either way.
- **`foreach ($change in @(`**: A change to the key by anyone after the install means it is theirs now: the uninstall leaves it.
- **`Test-Case "An upgrade over an install that has the crash dump key keeps it an...`**: An upgrade or reinstall applies the same rules: it keeps the key and its record while the key is still its own, writes the key when there is none, and leaves a key the player changed alone (and forgets the record, which no longer describes anything of ours).
- **`Test-Case "An upgrade writes the crash dump key again when it is gone, and th...`**: The first install may have created the LocalDumps key itself (when the machine had none). The key being gone while LocalDumps stayed must not make the upgrade forget that: the uninstall after it still takes away what the install made, and leaves LocalDumps as the first install found it (ADR-0039, item 8). On a machine that had LocalDumps already, the same steps check that the upgrade does not start to remove a key that was never ours.
- **`Test-Case "An install and an uninstall leave the LocalDumps key as they found...`**: Writing the game exe's key creates the LocalDumps key when it is not there. Microsoft does not say what a LocalDumps key with nothing in it does (it may keep crash dumps on for the other applications too), so the Installer leaves the registry as it found it: it records that it created LocalDumps, and the uninstall takes it away again when it is still empty (docs\adr\0039-..., item 8).
- **`if ($env:GITHUB_ACTIONS -eq 'true') {`**: CI is elevated, so nothing may be skipped there: a skip means the elevated cases are not running, and a green run that skipped them would say nothing about the Windows crash dump setting.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 121 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
