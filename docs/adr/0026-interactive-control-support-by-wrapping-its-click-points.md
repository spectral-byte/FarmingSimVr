# Interactive Control Support by Wrapping Its Click Points

## Context and Decision
The Interactive Control mod (`FS25_interactiveControl`, by VertexDezign) makes switches and levers clickable as Click Points in cabs, on vehicle exteriors and on placed objects. Each frame a Click Point projects its node to the screen and compares that with `g_inputBinding:getMousePosition()`. A click runs `IC_CLICK`, which executes the selected Click Point. Nothing in that path knows about a Pointer Ray.

**The Companion Mod wraps Interactive Control's Click Point functions itself, and switches that wrap off when the mod is missing or has changed. It does not depend on a hook from Interactive Control's authors.**
- The wrap is installed only when `FS25_interactiveControl` is loaded and its Click Point class has the functions the wrap expects. Otherwise the Companion Mod logs one warning and leaves Interactive Control, and the rest of the Motion Controller support, untouched.
- While the Pointer Hand is in use, the wrap selects Click Points from the Pointer Ray instead of the mouse, and the Pointer Hand's trigger runs Interactive Control's own execute path. Interactive Control keeps its networking, analog controls and icons.
- Interactive Control updates the Click Points in a cab every frame but those on a vehicle's exterior only on the vehicle's update tick, every few frames. So the wrap treats a Click Point as gone only after half a second without an update, not after a count of frames. A trigger pull that comes before Interactive Control has taken the selected Click Point as clickable waits for its next update of that Click Point. It waits at most that half second, and only while the ray stays on that Click Point.
- Interactive Control updates the Click Points in a cab from the vehicle's draw, and the game draws the vehicle being driven as part of the HUD. With the VR HUD hidden (its default) that draw does not run, so in a cab no Click Point became active and the ray had nothing to hit. While the HUD is hidden and no menu takes the input, the Companion Mod therefore makes the call Interactive Control's draw would make (`updateInteractiveController(true, false, <active for input>)`, when its state is on and its indoor check passes) once per update. Interactive Control does the same from its tick in the game's no-HUD mode. The hand-tool crosshair that draw would also render is left out. The game log says what Interactive Control reports for the vehicle being driven when that changes: its state, its indoor check, input, AI, and how many indoor Click Points are active.
- Using the Pointer Hand in a cab switches Interactive Control on for that vehicle, and putting it down restores the state it had. The Companion Mod writes that synced state only at those two moments, plus one more write each time the vehicle's camera changes, because Interactive Control switches itself off then and the wrap switches it on again. Never per frame.
- Interactive Control is an optional companion: without it, the Pointer Ray still works in menus.

## Status
Proposed.

## Considered Options
- **Wrap Interactive Control's Click Points from the Companion Mod** (selected: it works with the Interactive Control release players already have, and needs no one outside this project).
- **Ask VertexDezign for a pointer-source hook and use only that** (rejected: the feature would wait on a third party's release, and players on older releases would get nothing).
- **Move the desk mouse cursor to wherever the Pointer Ray points** (rejected: a ray from the hand does not pass through one screen point the way the mouse does. Under Alternate Frame Rendering the projected point also jumps between eyes every frame).

## Consequences
- An Interactive Control update that renames or reshapes the wrapped functions turns the wrap off, with a warning in the log, until the Companion Mod is updated. It must never break Interactive Control or the game.
- The functions the wrap depends on are pinned down by a test against a stub of Interactive Control's Click Point class. That keeps the dependency visible and catches a change on our side.
