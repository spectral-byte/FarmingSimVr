# Spray Can and Horse Brush Join the Tool Hand

## Context and Decision
ADR-0029 puts the flashlight, the chainsaw and the pressure washer lance into the Tool Hand and leaves every other Hand Tool where the game puts it. Players want the spray can (which marks trees) and the horse brush in the hand too. Both are base-game Hand Tools that the game links to the camera with `HandTool:attachToolToCamera` and aims through the player's one look ray (`Player:getLookRay`, cast by `PlayerTargeter:update`), so ADR-0029's two wraps already cover their placement and aim. What is new:
- Each adds a target type to the targeter with a 0.5 m minimum distance (`HandToolSprayCan`: trees, up to `sprayCan#distance`, 1.5 m; `HandToolHorseBrush`: animals, up to 3 m). A ray that starts at the nozzle or the bristles misses a trunk the can is held against and an animal the brush is touching.
- Both draw their feedback as 2D overlays in the middle of the view (`onDraw`), as the chainsaw does while it is not cutting. The spray can's overlays are the only display of its marker type and of "a tree is targeted".
- The spray can feeds `activateSpraying` every frame (one marker per press, on the rising edge). Marker type is changed with `SPRAYCAN_CHANGE_MARKER`. The marker is placed by the native `treeMarkerSystem:addTreeMarkerCameraBased`, given the look ray's origin.
- The horse brush's `ACTIVATE_HANDTOOL` handler, `HandToolHorseBrush.onBrushAction`, is on the specialization class only and is held (`isCleaning = inputValue > 0`). The game turns the action event off while no animal is targeted.

The fishing rod comes from the Highlands Fishing DLC, whose source cannot be read. It stays with the game until a runtime dump of the held tool shows how it works.

**The spray can and the horse brush go into the Tool Hand like ADR-0029's tools. The look ray for them starts behind the tool by the game's minimum distance. The brush only reaches an animal it is touching. While any supported Hand Tool is in a tracked Tool Hand, its screen-centre overlays are not drawn.**
- Both are recognised by their specialization (`spec_sprayCan`, `spec_horseBrush`) and placed by the wrap of `attachToolToCamera` with a handle offset, as before. They point along the spray can's nozzle (`sprayCan_vis`) and the brush's bristles (`brush`). The axes are inferred from the i3d and checked in the headset.
- For these two tools, the wrap of `getLookRay` returns the tool's pointing ray moved back along its axis by the game's 0.5 m minimum. The game's own targeter then measures its reach from the tool's working end: the spray can marks a tree up to 1 m past its nozzle. The brush is further limited to an animal within about 0.25 m of the bristles. Beyond that, the targeted animal is treated as none, so the brush neither cleans nor shows its action.
- The Tool Hand's trigger calls the tool's own `ACTIVATE_HANDTOOL` handler, as in ADR-0029. For the spray can, `activateSpraying` is called every update with the trigger's state, so one pull is one marker. For the brush, `HandToolHorseBrush.onBrushAction` (from the class) gets 1 while held and 0 on release, and 0 when the tool leaves the hand. `SPRAYCAN_CHANGE_MARKER` stays a Virtual Gamepad binding, as the chainsaw's roll action does.
- While a supported Hand Tool is in a tracked Tool Hand, the wrap of its specialization's `onDraw` draws nothing. This holds for the chainsaw too, where the game's ring marker on the trunk already shows the cut. The spray can makes up for its overlays in two ways. It gives a light haptic tick when its target tree changes from none to one. It also names its marker type as a short message when it is taken in hand and when the type changes.
- Haptics: the spray can buzzes lightly while the game says it is spraying (`spec.isSpraying`), so an empty can or a forbidden tree is silent. The brush buzzes lightly while it cleans an animal.
- `addTreeMarkerCameraBased` is given the moved ray's origin. If it also reads the camera, the marker may face the head rather than the can. That is accepted unless it looks wrong in the headset.
- `vrHandToolsDiagnostics` also dumps an unsupported held Hand Tool to the log: its `spec_*` tables, the functions on its type and its registered action events. A player can hold the fishing rod once and send the log, and the rod gets its own design. **Built** (#120, `VRCompanionHandTools.lua`, "Describing an unsupported held Hand Tool"). The probe only reads: it adds no wrap and changes nothing about the tool, the supported list or the placement, and the command's one-line answer is unchanged. For the unsupported tool the local player holds it writes, one log line each, `[FS25_VR] Hand Tool probe: <line>`:
  - the tool's `configFileName`;
  - the placement path: `through attachToolToCamera` when ADR-0029's wrap of `attachToolToCamera` saw the game place the tool and the player is still in the first person view, `not through attachToolToCamera (the game used another path)` otherwise, and `unknown (the placement wrap is not installed)` when that wrap is not there (nothing was seen, so no path is claimed);
  - each specialization: its name, the global name of its class and the names of the functions on the class (where handlers kept on the class only are, as the chainsaw's);
  - the functions on the tool's type;
  - each registered action event: the action name, the callback's name where it is found among the class, type and tool functions, and the edges it fires on (press, release, every frame);
  - the node tree below the tool's root by path, to depth 3, with the number of children not listed at the last level;
  - every `spec_*` table one level deep: each field's name, Lua type and a short value (a long string cut, a table by its key count, a number that is an entity id with the node's name; a table field of 40 keys or fewer is also opened one level, entry by entry).

  Every read is guarded: a failure is a line, not an error, and the other parts go on; a node that cannot be read ends the node tree, not the description. The caps, each cut saying how much was left out: 400 lines per run (the line that counts the lines left out is one of the 400; the spec tables come last, so they are what a cut takes), 100 fields per spec table, 100 function names per class or type, 40 nodes. The tool is the one the game last attached to the camera, or else the one the player says it holds (`getHeldHandTool()`), since a tool the game put in the hand of the third person view never went through `attachToolToCamera`; another player's tool is never described.

## Status
Proposed. The diagnostic dump of an unsupported held Hand Tool (last bullet above) is built and covered by Luau-stub tests; the rest of this decision is not built yet. Extended by ADR-0031 (the fishing rod joins the Tool Hand).

## Considered Options
- **Move the ray's origin back by the minimum distance** (selected: the game's targeter, filters and distances stay in charge, with one number per tool).
- **Change the targeter's minimum distance while a tool is held** (rejected: it means wrapping `addTargetType` and undoing it on every hand change, for the same result).
- **Keep the game's distances from the tool** (rejected: a can on the trunk and a brush on the horse would do nothing).
- **The brush at the game's 3 m reach** (rejected: cleaning a horse from across the stall feels broken with the brush in the hand).
- **Keep the screen-centre overlays, or only the spray can's marker-type icon** (rejected: in the headset they sit in the middle of the view and say "aim where you look", which is no longer true).
- **A world-space reticle where the tool's ray hits** (deferred: a new drawing feature, for later).
- **Gestures (stroking with the brush, flicking a cast)** (deferred: they need tuning in the headset and are their own design).
- **Guess the fishing rod's shape now** (rejected: nothing about it is readable, and a wrong guess costs more than one log from the headset).

## Consequences
- Supported Hand Tools are now the flashlight, the chainsaw, the lance, the spray can and the horse brush.
- While the spray can or the brush is in the Tool Hand, anything else the game highlights by look ray is also found from 0.5 m behind the tool.
- With the chainsaw, spray can or brush in a tracked Tool Hand, nothing is drawn at the centre of the view. The game's in-world feedback (the chainsaw's ring), the input help and the Companion Mod's ticks and messages remain.
- A game update that renames or reshapes `HandToolSprayCan.activateSpraying`, `HandToolHorseBrush.onBrushAction`, `HandToolHorseBrush.getHusbandryAndClusterFromNode` (wrapped for the touching cap), the specializations' `onDraw`, or the targeter's `closestTargetsByKey` / `getClosestTargetedNodeFromType` turns the feature off with a warning, as in ADR-0029.
- `vrHandToolsDiagnostics` with an unsupported tool in hand writes up to 200 more lines to the log (one run, tagged `Hand Tool probe:`); with nothing, or a supported tool, held it logs what it logged before. Not yet confirmed in game: what the fishing rod's dump reads like, as the stub tools are shaped after readable game code.
- Not yet confirmed in game: the spray and bristle axes, each tool's handle offset, whether the marker faces the can, and whether the brush's 0.25 m reach feels like touching.
