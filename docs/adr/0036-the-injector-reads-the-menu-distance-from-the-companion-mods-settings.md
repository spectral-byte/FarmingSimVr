# The Injector Reads the Menu Distance from the Companion Mod's Settings

Amends [ADR-0006](0006-openxr-floating-quad-layer-for-menus.md).

## Context and Decision
ADR-0006 put the floating menu quad about 1.5 m in front of the player. At a high Render Resolution the game's menus fill much of that panel, so they can read as too close. `vrMenuDistance [1.0-5.0]` lets the player move the quad further away. The quad keeps its 0.9 m height, so further away it fills less of the view.

Every other setting the Companion Mod hands to the Injector (`vrDesktopMirror`, `vrMouseConfinement`, `vrFovOverscan`) reaches it only when a savegame loads, and `VRBridge.onMapUnload` puts it back to its default. That is fine for those settings, because none of them does anything in the main menu. The Menu Distance is different: the main menu is a menu, and it is shown before the Companion Mod runs.

**The Injector reads `menuDistance` from the Companion Mod's settings.xml once at launch, and a map unload leaves the value alone.**

- The Companion Mod still owns the setting. It writes `vrSettings.menuDistance` to settings.xml and sends the value to VRBridge on every map load and every change. The Injector only ever reads the file.
- The file is `<Documents>\My Games\FarmingSimulator2025\modSettings\FS25_VRCompanion\settings.xml`, where Documents is the known folder of the user running the game. The Installer makes the same assumption about the game's profile folder. If the file is missing, has no `menuDistance`, or holds a value the command would refuse, the main menu uses 1.5 m. Once a savegame loads, the Companion Mod sends its value anyway, so a profile kept somewhere else still works from then on.
- The production runtime (`InjectorRuntime::Instance()`) passes that path in. Test runtimes pass none, so they never read the developer's own settings. `SHGetKnownFolderPath` is looked up when it is needed, so the Injector's import table gains neither shell32 nor ole32.
- Since a map unload keeps the value, the main menu after leaving a savegame stays at the player's distance, and so does a savegame that runs without the Companion Mod.

## Status
Accepted.

## Considered Options
- **Keep the setting savegame-only, like FOV Overscan** (rejected: the main menu, the one menu every session starts in, would stay at 1.5 m).
- **Give the Injector a settings file of its own and have VRBridge write it** (rejected: two copies of one setting that can drift apart, and one more file for the Installer to remove).
- **Have the Injector read the Companion Mod's settings.xml at launch (selected).** One source of truth, read-only on the Injector side, and it falls back to the default when the file is not where the game keeps it by default.
