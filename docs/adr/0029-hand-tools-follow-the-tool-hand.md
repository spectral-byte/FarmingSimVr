# Hand Tools Follow the Tool Hand

## Context and Decision
Players want to hold the flashlight, the chainsaw and the pressure washer lance in a Motion Controller and use them where they point. In first person the game links a held Hand Tool to the player camera's pitch node (`HandTool:attachToolToCamera`), so it moves with the head. The tools aim in three different ways. The lance washes along its own nozzle node (`HandToolHPWLance`, `raycastNode`). The flashlight's beam is placed at the camera each update (`HandToolFlashlight`). The chainsaw takes its tree from the player's one look ray (`Player:getLookRay`, read by `PlayerTargeter:update`), turns its model and its cut plane to face the camera, and takes its cut angle from `AXIS_ROTATE_HANDTOOL`. Using the tool is `ACTIVATE_HANDTOOL`, which the game's gamepad defaults put on X: on the Virtual Gamepad, the left hand's lower face button.

**While the Tool Hand is tracked and holds a supported Hand Tool, the Companion Mod links the tool into the Tool Hand's Virtual Hand, makes the player's look ray start at the tool, and uses the Tool Hand's trigger as the tool's use.**
- Supported Hand Tools are the flashlight, the chainsaw and the lance, recognised by their specialization. Every other Hand Tool (the spray can, the horse brush, other mods' tools) keeps the game's placement, aim and bindings.
- The wrap of `attachToolToCamera` links the tool under the Tool Hand's Virtual Hand node, with the handle in the closed hand. The Virtual Hand is placed before every rendered frame (as the hands are in the cab), so the tool rides with it.
- The wrap of `getLookRay` returns a ray from the tool along its pointing axis (the chainsaw's bar, the lance's nozzle, the flashlight's beam). The chainsaw then targets the trunk its bar is against through the game's own targeter.
- The chainsaw's cut plane takes its heading from the bar and its tilt from the wrist's roll about the bar, clamped to the game's ±90°. The game reads the heading off the camera (`camera:getRotation`), so the wrap of `HandToolChainsaw.onUpdate` makes the camera answer the bar's heading for the length of the chainsaw's own update (it shadows `getRotation` on the camera instance and removes it again), and turns the model back along the tool afterwards. The wrap of `HandToolChainsaw.onRollAction` does nothing meanwhile. Once a cut starts it keeps the game's behaviour: height, heading and tilt are fixed, and a cut that stops being valid stops.
- The flashlight's beam is placed from the tool, not the camera: the wrap of `HandToolFlashlight.onUpdate` runs the game's own placement and then places the light node from the lens, and the Virtual Hands' render-time placement does the same for a frame rendered without an update.
- The Tool Hand's trigger calls the tool's own action handler, the one `ACTIVATE_HANDTOOL` calls: held, it cuts or sprays; pressed, it toggles the flashlight through `setFlashlightIsActive`, sent to other players like the game's own switch. That trigger reads as released on the Virtual Gamepad while the Tool Hand holds a supported Hand Tool, as the grip does in Wheel Grab (ADR-0028). The game's own binding keeps working.
- The Tool Hand is never the Pointer Hand in the world while it holds a Hand Tool (in a menu either hand can point). The Tool Hand is the right Motion Controller unless `vrToolHand left` (saved) says otherwise. It buzzes lightly while the chainsaw's trigger is held (more while cutting), and steadily while the lance sprays; nothing buzzes while a tool is only held, and the flashlight is silent.
- Only the local player's tool is moved; another player's tool is left to the game.
- When the Tool Hand is set down or not tracked, when the feature is switched off (`vrHandTools`), or when a wrapped function is not what the wrap expects, every wrap passes through. The game's head-locked placement and aim come back, and a missing function logs one warning.

## Status
Accepted. Extended by ADR-0030 (the spray can and the horse brush join the supported tools; no crosshair in the middle of the view).

## Considered Options
- **Look ray from the tool while it is held** (selected: the chainsaw cuts the trunk its bar is against through the game's own targeter, with no second targeting path to keep in step).
- **A separate ray, cast by the Companion Mod, for the chainsaw only** (rejected: it reimplements the targeter's filters and distances for one tool, and the game's highlighting would still follow the head while the saw follows the hand).
- **Aim by looking; only the model in the hand** (rejected: the tool in the hand would cut where the head looks, which feels broken).
- **The Tool Hand's trigger, owned by the Companion Mod** (selected: it is the obvious "use" and follows the Tool Hand to either side).
- **Leave `ACTIVATE_HANDTOOL` to the player's bindings, or add it to the Virtual Gamepad's one-time rewrite** (rejected: the default is a face button on the left hand; a rewrite onto RT is wrong as soon as the Tool Hand is the left).
- **Any Hand Tool into the Tool Hand, with aim moved only for supported ones** (rejected: a tool in the hand that aims from the head feels broken; a supported list is easy to explain).

## Consequences
- While a supported Hand Tool is held, anything the game highlights by look ray follows the tool, not the head.
- In multiplayer, other players see the tool in the avatar's third-person hand, as the game draws it. Cuts, washing and the flashlight sync through the game's own events, unchanged.
- The roll action does nothing while the Tool Hand is tracked and holds the chainsaw.
- A game update that renames or reshapes `HandTool.attachToolToCamera`, `Player.getLookRay`, `HandToolChainsaw.onUpdate`, `HandToolChainsaw.onRollAction`, `HandToolFlashlight.onUpdate`, `PlayerCamera.getRotation` (relied on, not wrapped) or the supported tools' handlers turns the feature off with a warning until the Companion Mod is updated. A game without a class at all (no flashlight, say) has nothing to move, which is not a failure.
- Not yet confirmed in game: whether the game draws first-person arms with a held tool (they would need hiding), and the handle offset per tool.
