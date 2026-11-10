# The Installer Turns On Windows' Own Crash Dumps for the Game

Amends [ADR-0024](0024-unsigned-inno-setup-installer-and-static-runtime-injector.md): the Installer changes a Windows setting.

## Context and Decision
When the game crashes before the Injector has started, or while Windows is still loading it, the Injector cannot report anything: it writes no log line and no dump. Windows can. Windows Error Reporting writes a dump from outside the process, for every crash of an application whose exe has a key of its own under `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps`.

Until now the Installer changed only files: the Injector in the game's `x64` folder, the Companion Mod in the mods folder, and the Render Resolution in the player's `game.xml`, each recorded or backed up so the uninstall can undo it. This is the first change outside the game's and the player's folders: a setting of Windows itself, shared with every other program that reads it.

**The Installer creates the per-application `LocalDumps` key for the game's exe, records what it wrote, and the uninstall removes it only while it still holds exactly that.** It is on by default and the command-line switch `/FS25VR_NOCRASHDUMPS` leaves the setting alone.

1. **The key is `LocalDumps\FarmingSimulator2025Game.exe` in the 64-bit registry view.** Three DWORD values: `DumpType` 0 (a custom dump), `CustomDumpFlags` 0x1060 and `DumpCount` 3. The flags are `MiniDumpWithThreadInfo`, `MiniDumpWithIndirectlyReferencedMemory` and `MiniDumpWithUnloadedModules`, on top of the stacks and module list every minidump holds: the contents of the Injector's own dump, so both dumps of one crash can be read the same way. No full process memory, so a dump stays small enough to attach to an issue once zipped. There is no `DumpFolder`, so Windows' default applies, the `CrashDumps` folder in the player's local application data, which is where players are sent to look.
2. **It needs the administrative install mode.** A per-user run (`/CURRENTUSER`, which the test script uses outside an elevated shell) cannot write `HKEY_LOCAL_MACHINE`; it logs `Skipped the Windows crash dump setting` and the install succeeds. The setting never fails an install and never changes its exit code. It is written after the files are placed (`CurStepChanged`, `ssPostInstall`), so a rolled-back install writes nothing.
3. **A key that already exists for the game's exe is the player's or another tool's.** Install and uninstall leave it as it is, and the log says so. Nothing is recorded for it.
4. **The record sits next to the uninstaller, as the Render Resolution's does.** `{commonappdata}\FS25 VR\crash-dumps.ini` holds the exe name and the three numbers written. The uninstaller gets no command-line switches, so it works from the record alone. The record is saved before the key is written and deleted again if the write fails.
5. **The uninstall removes the key only when a record exists and the key still holds exactly the recorded values** (the three DWORDs, no other value, no subkey). If the player or another tool changed anything, the key is theirs now and stays. The record is deleted either way, and so is its folder once empty.
6. **An upgrade or reinstall applies the same rules.** It keeps the key and its record while the key is still its own, writes the key when there is none, and for a key that was changed since it leaves the key alone and deletes the record, which no longer describes anything of ours. An upgrade given `/FS25VR_NOCRASHDUMPS` touches neither.
7. **A hidden, test-only switch `/FS25VR_GAMEEXE=<file name>` replaces the exe name the key is written for**, as `/FS25VR_DOCUMENTS` replaces the Documents folder. Every Installer test case passes a throwaway name, so the key of the real game is never read or written by a test. A value that is not a plain file name writes no key, so it cannot reach into another key.
8. **The `LocalDumps` key itself.** Creating the first key under it creates it. Windows applies the values on `LocalDumps` itself, and where there are none its defaults (minidumps, ten kept), to every application that has no key of its own, so a `LocalDumps` key the Installer made would keep crash dumps on for the whole machine once the game's key is gone. The record therefore says whether the Installer's write created `LocalDumps`, and the uninstall deletes it again when it did and it is still empty. A `LocalDumps` key that was there before is never touched, and neither is one another application's key has gone into since.

Only this one per-application key is written. The Installer changes none of Windows Error Reporting's global settings, and Windows writes no dump at all where a policy has switched Windows Error Reporting off.

## Status
Accepted.

## Considered Options
- **The Injector or the Companion Mod writes the key** (rejected: neither runs with administrator rights, and a setting that outlives the game belongs to the program that can also take it away).
- **A README command only** (rejected as the only route: players who install with the Installer would not have it on when the game crashes. The README still gives manual installs the command that turns it on and the one that turns it off).
- **Ask in the wizard only, with no switch** (rejected: silent installs would have no way to keep Windows settings unchanged).
- **A global `LocalDumps` setting instead of a per-application key** (rejected: it changes dumps for every program on the machine).
- **Remove the key on uninstall without checking it** (rejected: it could delete a key the player set up or changed, as for the Render Resolution).
- **Leave `LocalDumps` behind on uninstall** (rejected: if Windows treats an empty `LocalDumps` key as the switch for every application, uninstalling would leave crash dumps on for the whole machine).
- **A per-application key written for the Installer's exe name regardless of the test** (rejected: a test run would then read and write the key of the real game on a developer's machine).
- **Chosen option** (selected). A per-application key from the administrative Installer, on by default, with a switch, a record and a remove-only-if-unchanged rule, in the way the Render Resolution is handled.

## Consequences
- A crash before the Injector starts leaves a dump in the player's `CrashDumps` folder. For an ordinary crash both dumps exist: the Injector's, and then Windows' once the Injector has passed the crash on.
- The Installer is no longer only about files. Anything else it changes outside the game's and the player's folders needs the same treatment: a record, a leave-it-alone rule for what was there before, and a remove-only-if-unchanged uninstall.
- The test for the cases that write `HKEY_LOCAL_MACHINE` runs only in an elevated shell. `scripts/test_installer.ps1` reports them as skipped, not passed, anywhere else; CI runs elevated.
- Whether Windows really counts an empty `LocalDumps` key as the switch for every application (item 8) has been read from its documentation and from the developer's notes, and has not been checked on a machine without the key.
- Manual installs turn the setting on and off with `reg` commands in the README. The values there must stay equal to the ones the Installer writes.
