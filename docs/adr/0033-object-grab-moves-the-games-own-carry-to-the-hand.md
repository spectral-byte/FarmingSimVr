# Object Grab Moves the Game's Own Carry to the Hand

Extends [ADR-0029](0029-hand-tools-follow-the-tool-hand.md) to empty hands.

## Context and Decision
Players want to pick up small bales, bags and other loose objects with a Motion Controller. On foot with empty hands the game's "hands" hand tool (`HandToolHands`) carries one object at a time. It picks the target from the player's look ray (`PlayerTargeter`, fed by `Player:getLookRay`) and checks the mass against `currentMaximumMass`, skipping the object's own `getCanBePickedUp` under super strength. The object hangs on a spring joint whose stiffness scales with its mass and which breaks above a mass-scaled force, again except under super strength. The joint is fixed to a kinematic node that the game moves each update to `currentHoldDistance` along the look ray, kept level, with a child rotation node turned by the rotate and level actions. The owning player's client streams that node's position and rotation to the server, which owns the joint.

**While the Pointer Hand aims on foot with empty hands, the look ray starts at its fingertip (as the Pointer Ray does), so the game's own targeter, mass checks and super strength decide what can be grabbed. While a grip holds the object, the Companion Mod places the kinematic node from the hand's full pose with the offset it had at the grab.**
- What can be picked up stays the game's rule: the mod calls the game's pick-up path (`pickUpTarget`) and never reads the mass limit itself, so super strength and other mods that change those values work unchanged.
- Everything the game targets by look while the hands are empty follows the hand too: the prompts on animals, vehicles, trees and loose objects (`PlayerInputComponent`) and the click boxes on Hand Tool holders. It is the same single targeting path ADR-0029 chose for the Tool Hand.
- The kinematic node follows the hand in six degrees of freedom, so the object keeps the grab offset and turns with the wrist. The game's rotate and level actions still turn the rotation node, on top of the wrist. The node is placed through the game's own fields and dirty flag, so multiplayer sync is the game's.
- Letting go of the grip drops the object. If the hand was swinging, the object is thrown along the hand's velocity through `throwHeldItemWithForceVector`, no harder than the game's full throw.
- The game's own pick-up button still works. Its carry stays the game's: level, at its distance along the ray, which is now the hand's.
- The grip belongs to the Companion Mod only while its hand points at something it can grab and while it carries (`takeVirtualGamepadInput`, ADR-0028); otherwise it is the Virtual Gamepad's LB / RB.
- Either hand can grab, even while both are in use. The Pointer Hand's ray is the targeter's. For the other hand, the Companion Mod casts a ray along its finger only to find out whether it points at something `getCanPickUpNode` accepts, so its grip can be taken before it is squeezed (otherwise the held-input rule would hand that squeeze to the game). Squeezing it makes that hand the Pointer Hand, the targeter is run again with that hand's ray, and the game's `pickUpTarget` picks up what it finds. The check never decides by itself what is picked up.
- The game's crosshair and grab icon in the middle of the view are not drawn while the hand aims. The Pointer Ray shows on a grabbable object instead.
- Haptics: one light pulse as the ray lands on something it can grab, one when grabbing, and a sharp one when the joint breaks. There is no constant buzz while carrying.
- If the hand loses tracking while the grip is held, the node stays where the hand last was. Before a grab, a hand that is set down or not tracked hands the look ray back to the head. If a wrapped function is not what the wrap expects, the game's carry comes back and one warning is logged.
- `vrObjectGrab` (saved, on by default) switches it off, and `vrObjectGrabDiagnostics` logs the target, its mass against the limit, super strength, the grab offset, how far the object trails the hand, and the last throw.

## Status
Accepted

## Considered Options
- **The game's own carry, along the hand's ray and level** (rejected for the grip: the object would not turn with the wrist; kept for the game's own pick-up button).
- **Only Object Grab follows the hand, while the head keeps every other look target** (rejected: it needs a second targeting path for grabbing that repeats the targeter's masks, distances and filters; ADR-0029 rejected the same thing for the chainsaw). The off-hand's check above is narrower: it only decides who takes the grip, and the targeter still picks the object.
- **The off-hand's trigger first makes it the Pointer Hand, then its grip grabs** (rejected: grabbing with the off-hand takes two steps). **Only the Pointer Hand grabs** (rejected: the player asked for either hand).
- **Touch only** (rejected: super strength's 10 m reach would be lost; a touching hand is just a hit at about 0 m along the ray).
- **The grip always the Companion Mod's on foot with empty hands** (rejected: it takes away any binding the player has put on LB / RB on foot).
- **A buzz that grows as a heavy object trails the hand** (rejected by the player: no constant buzz).

## Consequences
- With a Motion Controller aiming and empty hands, getting into a vehicle or reading an animal's prompt means pointing at it, not looking at it.
- A game update that renames or reshapes `HandToolHands.updateKinematicNode`, `pickUpTarget`, `throwHeldItemWithForceVector`, the `spec_hands` fields or `PlayerTargeter:getLastLookRay` turns Object Grab off with a warning until the Companion Mod is updated.
- Not yet confirmed in game: the game's mass limits and throw constants (`MAXIMUM_PICKUP_MASS`, `PICKUP_DISTANCE`, `MAXIMUM_THROW_TIME` are stripped from the source; read them at run time), how a swing's speed maps to the full throw, and how the joint feels with a hand that moves much faster than the head.
