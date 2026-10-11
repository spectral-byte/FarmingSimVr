# Installer release checklist (manual)

CI builds the Installer and runs it silently against a fake game folder (`scripts\test_installer.ps1`). It cannot check **game installation detection**, because that reads the real machine: the Steam registry entry and `libraryfolders.vdf`, the Epic Games Launcher manifests, and the GIANTS registry entry. Run this checklist by hand before each release, with the `FS25_VR_v<version>_Setup.exe` from `scripts\package_release.ps1`.

The detection code is `installer\game_detection.iss`, included by `installer\fs25_vr.iss`. Setup writes what it found to its log, so every check below can be confirmed with `/LOG`:

```powershell
.\FS25_VR_v<version>_Setup.exe /LOG="$env:TEMP\fs25vr-setup.log"
# afterwards, in the log:
Select-String "$env:TEMP\fs25vr-setup.log" -Pattern "Detected Farming Simulator 25|Upgrade:"
```

A line `Detected Farming Simulator 25 (Steam|Epic Games|GIANTS): <folder>` is logged for every installation found. To check detection without installing anything, run the wizard and cancel on the folder page.

Use a machine or VM where you can install the game from each store, and uninstall FS25 VR between items (Settings > Apps > FS25 VR) so a previous install does not pre-select its own folder (item 7 covers that on purpose). Cancel the wizard once you have seen the page; you only need to finish an install where the item says so.

## 1. Steam, default library

- [ ] FS25 installed through Steam in the default library (`...\Steam\steamapps\common\Farming Simulator 25`).
- [ ] Run the Installer. The folder page is pre-filled with that folder and no installation picker is shown.
- [ ] The log has `Detected Farming Simulator 25 (Steam): ...`.

## 2. Steam library on a second drive

- [ ] In Steam: Settings > Storage, add a library on another drive, then move FS25 there (right-click the game > Properties > Installed Files > Move install folder). The default library must no longer hold the game.
- [ ] `<Steam>\steamapps\libraryfolders.vdf` lists the second drive (`"path"` entry).
- [ ] Run the Installer. The folder page is pre-filled with the folder on the second drive.
- [ ] Finish the install and confirm `dxgi.dll` landed in that folder's `x64`, then uninstall.

## 3. Epic Games

- [ ] FS25 installed through the Epic Games Launcher; `C:\ProgramData\Epic\EpicGamesLauncher\Data\Manifests\*.item` contains a manifest whose `InstallLocation` is the game folder.
- [ ] Run the Installer. The folder page is pre-filled with the Epic folder. The log has `Detected Farming Simulator 25 (Epic Games): ...`.
- [ ] Note: the Epic `InstallLocation` is assumed to be the folder that contains `x64\FarmingSimulator2025Game.exe`. If the log shows nothing for Epic, compare the manifest's `InstallLocation` with where the exe really is and fix `DetectEpicManifests`.

## 4. GIANTS standalone

- [ ] FS25 installed from the GIANTS installer (no Steam or Epic copy on the machine).
- [ ] Run the Installer. The folder page is pre-filled with the GIANTS folder. The log has `Detected Farming Simulator 25 (GIANTS): ...`.
- [ ] Note which of the two sources found it: the registry value `InstallDir` under `HKLM\SOFTWARE\WOW6432Node\GIANTS Software\FarmingSimulator2025`, or the default folder `C:\Program Files\Farming Simulator 2025`. If the real registry key or value name differs, fix `DetectGiantsStandalone` and record the real names here.

## 5. Two installations at once

- [ ] With two of the above on one machine (for example Steam and GIANTS standalone), run the Installer.
- [ ] After the license page a **Choose Installation** page lists both folders with their source in brackets, the first one selected.
- [ ] Picking the second one puts that folder on the folder page. Back, then picking the first one, changes it again.
- [ ] The same folder is never listed twice (for example a Steam library that `libraryfolders.vdf` lists more than once).

## 6. Browse to an invalid folder

- [ ] On the folder page click **Browse...** and choose a folder that is not a game folder (for example `C:\Windows`), then **Next**. An error says the folder is not a Farming Simulator 25 installation, and the wizard stays on the page.
- [ ] Browse to the real game folder: **Next** is accepted. Browse still works when an installation was pre-selected or picked.

## 7. Upgrade keeps the previous folder

- [ ] Install FS25 VR into installation A. With a second detected installation B on the machine, run the Installer again without `/DIR`: the folder page shows A (the previous folder), not B, and no picker is shown. The log has `Upgrade: keeping the previous game folder ...`.

## 8. Game Pass / Microsoft Store folder

- [ ] Browse to a Game Pass copy of the game (`C:\XboxGames\Farming Simulator 25\Content`, or a folder under `WindowsApps`) and click **Next**: a message says Microsoft Store / Game Pass installations are not supported. The wizard stays on the page.
- [ ] A silent run against the same folder (`/VERYSILENT /DIR="..."`) exits with a non-zero code, shows no message box, and the log contains the same message. (`scripts\test_installer.ps1` automates this with fake folders; this item checks it against a real Game Pass install.)

## 9. Setup offers to uninstall

`scripts\test_installer.ps1` automates `/FS25VR_UNINSTALL` and the silent upgrade; the question itself needs a person.

- [ ] With FS25 VR installed, run the Installer (double-click). Before any wizard page, a dialog says FS25 VR is already installed and offers **Upgrade or reinstall**, **Uninstall** and **Cancel**.
- [ ] **Cancel** (and the window's close button) ends Setup; FS25 VR is still installed and nothing changed.
- [ ] **Upgrade or reinstall** continues with the normal wizard (item 7: the previous folder is pre-selected).
- [ ] **Uninstall** opens the normal uninstaller (its own confirmation and "uninstall complete" message), and Setup shows no wizard. Afterwards `dxgi.dll` and the Companion Mod zip are gone. With the game running, the uninstaller asks you to close it.
- [ ] With FS25 VR not installed, the dialog does not appear.
- [ ] `FS25_VR_v<version>_Setup.exe /VERYSILENT` over an existing install upgrades without any dialog.

## 10. Game running, interactive

- [ ] Start the game, then run the Installer and go through the wizard. At **Install** a box asks you to close the game. Choose **Cancel**: the wizard stops on the Preparing to Install page showing "Close Farming Simulator 25 to continue." with **Back** and **Cancel** available, and nothing was installed.
- [ ] Close the game, click **Back** and **Install** again: the install goes through.

## 11. Standard user elevating with another account

`scripts\test_installer.ps1` replaces the Documents folder with a fake one, so it never asks the player's own process where Documents is.

- [ ] On a Windows account that is not an administrator, launch the game once so `Documents\My Games\FarmingSimulator2025\game.xml` exists, then run the Installer with `/LOG` and enter an administrator's credentials at the UAC prompt.
- [ ] The log has `Documents folder: <the standard user's Documents>`, not the administrator's.
- [ ] Finish the install with the Render Resolution ticked: `FS25_VRCompanion.zip` is in the standard user's `mods` folder and their `game.xml` has the new width and height. Nothing was added to the administrator's `Documents\My Games`.
- [ ] Uninstall: the zip is gone from the standard user's `mods` folder and their `game.xml` has its original values again.

## After the checklist

Record the Installer version and the date you ran it in the release notes draft, together with any item you could not run (for example "no Epic machine available").
