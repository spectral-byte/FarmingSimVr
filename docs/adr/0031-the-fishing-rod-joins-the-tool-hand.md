# The Fishing Rod Joins the Tool Hand

## Context and Decision
ADR-0030 left the fishing rod with the game until a runtime dump of the held tool showed how it works. The dump (#125, `vrHandToolsDiagnostics` in the headset) and play show:
- The rod is `pdlc/highlandsFishingPack/handTools/fishingRod/fishingRod.xml`, with one specialization, class `pdlc_highlandsFishingPack.fishingRod`, which has no global name. The Highlands Fishing DLC's scripts cannot be read, so everything here comes from the dump and from play.
- The game places it through `HandTool:attachToolToCamera`, so ADR-0029's placement wrap reaches it. Its nodes include `fishingRod/handNode` (the third person hold), the rod's joints (`handleJoint`, `yokeJoint`, `spoolJoint`, `segment01Joint`) and the line's `ropeReferenceFrame`.
- It registers two action events, each firing on press and on release: `ACTIVATE_HANDTOOL` -> `onActivateAction` and `ACTIVATE_HANDTOOL_SECONDARY` -> `onActivateSecondaryAction`, both on the class. Holding the first charges a cast (`angleScale` 0 to 1) and letting go throws it; the same action hooks a biting fish and reels.
- It runs a state machine (`spec.state` is one of the 11 `spec.states`, recognisable only by its keys): idle, charging, waiting for a bite (10 to 25 s after the cast), a bite with a hook window of under a second, reeling (`rodBendFactor` up to about 0.93), and caught. `fishingLine.isInitialized` is true from the cast until the line is in.
- Its `onDraw` draws a charge meter, a waiting hint and a "Hook fish" prompt at the centre of the view.

**The fishing rod goes into the Tool Hand like ADR-0030's tools: its look ray runs along the rod and the Tool Hand's trigger is its `ACTIVATE_HANDTOOL`. Unlike them, it keeps its screen-centre overlays, and the Tool Hand stays in use however still it is held while the rod's line is out.**
- Recognised by its specialization table and placed by the wrap of `attachToolToCamera` with a handle offset (`vrHandToolOffset fishingrod`). The first guess puts the hand at the rod's root, because the dump names `handNode` (the game's third person hold) but not its position, and the DLC's i3d is packed; the offset is fitted in the headset (#140).
- The wrap of `getLookRay` returns a ray from the rod's root along the rod toward its tip, with no `reachBehind`. The cast should land the way the rod points. Whether the DLC aims its cast with `getLookRay`, the camera or its own nodes cannot be read: the first headset build checks it, and a camera shadow while the rod casts (as for the chainsaw's heading, ADR-0029) is added only if the cast follows the head.
- The trigger calls `onActivateAction` from the class as the action event does: 1 once on the pull and 0 once on the let-go (`useOnce`, like the chainsaw), so whatever the game's button does in each state, the trigger does. When the rod leaves the hand mid-use, the 0 is sent as for every tool. For the rod that throws a charged cast, which is accepted: it is rare and harmless, while a release never sent could leave the rod charging.
- `ACTIVATE_HANDTOOL_SECONDARY` stays a Virtual Gamepad binding, as `SPRAYCAN_CHANGE_MARKER` does. What it does is recorded in the headset. If it turns out to be part of every catch, it moves to the Tool Hand's grip.
- The rod's overlays are not held back: its charge meter and bite prompts stay at the centre of the view. They are the DLC's only display of the charge and of the hook window, and they are a meter and prompts, not an aim crosshair. The headset showed the reeling minigame drawn so far right it was hard to see, so while `onDraw` runs, the Overlays it renders are pulled toward the centre of the screen and down a little (through `Overlay.render`: the engine's natives cannot be wrapped), and its text stays where it is (#140).
- The Tool Hand is set down after 3 s of being held still (`MOTION_CONTROLLER_IN_USE_MS`), and a bite comes 10 to 25 s after the cast. While the rod in the Tool Hand has its line out, the hand stays in use however still it is held, as a hand whose trigger is using a tool does. Losing tracking still gives the rod back to the game.
- Haptics: a sharp pulse, repeated through the hook window, when a fish bites; while reeling, a buzz that grows with `rodBendFactor`. Nothing while charging or waiting.
- With the rod in the Tool Hand, `vrHandToolsDiagnostics` reports the rod's state (from the keys of `spec.state`), the rod's ray and the head's direction at the last cast and where the bobber landed (`getBobberPosition`), and the last action the rod was given. The `Hand Tool probe:` dump no longer runs for it, since it is supported.
- Every read of the rod's spec is guarded. A DLC update that reshapes `onActivateAction` or the fields read here gives the rod back to the game with one warning, as in ADR-0029; a game without the DLC has no rod to move, which is not a failure.

## Status
Proposed.

## Considered Options
- **The rod in the hand with the game's own input and cast** (selected: the existing wraps place and aim it, and the DLC's state machine stays in charge).
- **Gestures: a flick to cast, cranking to reel, a jerk to hook** (deferred again, as in ADR-0030: they would drive a state machine nobody can read, and need tuning in the headset).
- **Hide the overlays and give haptics instead** (rejected: a buzz does not show how far a cast is charged, and the overlays are the rod's only display of it).
- **Keep only the "Hook fish" prompt** (rejected: `onDraw` is one unreadable function, so it is all or nothing).
- **Leave the in-use timeout as it is** (rejected: the rod would jump back in front of the view while the player waits, still, for a bite).
- **Send no release when the rod leaves the hand mid-charge** (rejected: the DLC might never learn the action ended).
- **The secondary action on the grip now** (deferred until it is known what it does).

## Consequences
- Supported Hand Tools are now the flashlight, the chainsaw, the lance, the spray can, the horse brush and the fishing rod. Without the DLC nothing changes.
- While the rod is in the Tool Hand, anything the game highlights by look ray follows the rod.
- The rod is the one supported Hand Tool that still draws at the centre of the view.
- A Tool Hand holding the rod with its line out is never set down for being still, so it never becomes the Pointer Hand then (ADR-0029).
- Not yet confirmed in game: the handle offset, the rod's axis, whether the cast follows the rod, what the secondary action does, whether the bite and reeling states can be told apart reliably from `spec.state`'s keys for the haptics, and whether the line still leaves from the tip with the rod in the hand.
