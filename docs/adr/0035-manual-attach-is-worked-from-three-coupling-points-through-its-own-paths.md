# Manual Attach Is Worked from Three Coupling Points Through Its Own Paths

## Context and Decision
The Manual Attach mod (`FS25_manualAttach`, by Wopster, release 1.1.0.0) makes the player attach and detach Implements on foot and connect their PTO and hoses by hand. It picks what to act on by distance, not by aim: the free hitch nearest the player (within about 2.8 m) with an Implement lined up within 0.7 m of it, or the nearest attached Implement. Three actions sit on two keys: `MA_ATTACH_VEHICLE` attaches or detaches, and `MA_ATTACH_PTO_HOSE` connects the PTO on a tap and the hoses on a hold. Nothing is drawn in the world to aim at. Through the Virtual Gamepad its default bindings are LB + Start and LB + Back, and the pad never sends Back, so the PTO and hoses cannot be reached from the Motion Controllers at all.

**On foot, the Pointer Ray works Manual Attach from three Coupling Points on the vehicle an Implement hitches to: the hitch, its PTO stub and its hose couplers. A trigger pull runs Manual Attach's own attach, PTO or hose path for that hitch's Implement, within Manual Attach's own reach and rules.**
- Each Coupling Point does one thing on one pull, as a Click Point does: the hitch attaches or detaches, the PTO stub connects or disconnects the PTO, and any hose coupler connects or disconnects all the hoses together. There is no tap or hold timing.
- The points are on the vehicle with the hitch: its attacher joint, its PTO output for that joint and its hose targets for that joint. They stay put, whereas the Implement's shaft and hose ends move between parked and connected.
- A Coupling Point can be pointed at whenever Manual Attach would offer its action from where the player stands, refusals included. A refused pull shows Manual Attach's own warning. Within that reach the player works the one they point at, not only Manual Attach's nearest candidate. While the ray is on one, Manual Attach's candidate is that hitch's Implement, so its prompts and context box describe it and its keys act on it too.
- Coupling Points rank with Click Points: of all of either in the cone, the one nearest the ray's centre wins, and both beat Animated Objects and objects Object Grab can pick up.
- While the ray is on a Coupling Point, the Pointer Hand's trigger reads released on the Virtual Gamepad. Manual Attach's bindings are not rewritten.
- In the cab nothing changes: Manual Attach's key and its auto attach work as before, and the ray stays with Click Points.
- The support is installed only when `FS25_manualAttach` is loaded and `g_manualAttach` has every function it calls. Otherwise the Companion Mod logs one warning and leaves Manual Attach, and the rest of the Motion Controller support, untouched. It has no switch of its own; it follows the Motion Controllers switch.

## Status
Proposed.

## Considered Options
- **Three Coupling Points, each running one of Manual Attach's own paths** (selected: one pull does one thing, the player points where they would plug in, and Manual Attach keeps its rules, warnings, sounds and multiplayer events).
- **The hitch only, with the PTO and hoses on another input** (rejected: it needs a second input or tap and hold timing on the trigger for two of the three actions).
- **Point at the Implement's body** (rejected: one target for three actions means tap and hold timing, or "each pull does the next step", which cannot disconnect the PTO on its own).
- **Give Manual Attach's actions reachable Virtual Gamepad defaults** (rejected: the one-time default rewrite has already run for existing profiles, so it would need another saved-flag migration, for bindings the pointer makes unnecessary).
- **Reach Coupling Points from anywhere along the ray** (rejected: it skips the point of the mod, walking to the hitch, as ADR-0034 rejected opening sheds from across the yard).

## Consequences
- The Companion Mod depends on Manual Attach's internals: `g_manualAttach`, its attachment handler's candidates and each attachment's `performAttachment`. A release that renames or reshapes them turns the support off with a warning, and must never break Manual Attach or the game. A test against a stub of them pins the dependency.
- The hitch and the PTO stub sit close together: 15 cm apart is about 4° from 2 m, inside the 5° cone. The nearest-to-centre rule picks between them, more easily up close.
- A vehicle with no PTO output or hose target of its own for a hitch (a pass-through PTO, for one) has no Coupling Point for that action, which stays on Manual Attach's key.
- Multiplayer stays in sync through Manual Attach's own events and the game's attach event, because its own paths are what runs.
