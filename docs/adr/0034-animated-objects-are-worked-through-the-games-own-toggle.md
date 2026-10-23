# Animated Objects Are Worked Through the Game's Own Toggle

## Context and Decision
Out of the cab the player wanted to open and close building doors with the Motion Controllers, as Click Points are clicked in the cab. Those doors are not Click Points: Interactive Control (1.2.0.1) only puts Click Points on vehicles. They are the game's Animated Objects. Each has moving parts with their own collision shapes and one trigger zone. While the player stands on foot in the zone, the game adds the door to its activatable objects, and the nearest one in range shows a prompt bound to an action (`ACTIVATE_HANDTOOL` on all 452 in the base game, every one a single-press toggle).

**The Pointer Ray works an Animated Object by hitting one of its moving parts and running the same toggle the prompt's button runs. That holds only while the player stands in that object's zone and the game's own check says it can be triggered.**
- What the ray is on is found from the collision it hits: a moving part of an Animated Object, or a node below one.
- The pull calls the object's own toggle, the path its prompt button takes, so the game's networking, sounds and texts are unchanged. A pull while it moves turns it back, as the button does.
- The ray reaches only an Animated Object whose zone the player is in and whose `getCanBeTriggered` is true. That check covers the building's owner and doors that depend on another door. Standing in several zones, the player works whichever one they point at, not only the nearest one the prompt offers.
- Only single-press toggles can be pointed at. One moved by holding a button stays on its buttons.
- While the ray is on an Animated Object, the Pointer Hand's trigger reads released on the Virtual Gamepad, as on a Click Point. Otherwise its `ACTIVATE_HANDTOOL` binding would toggle the door a second time.

## Status
Proposed.

## Considered Options
- **Run the hit object's own toggle, gated by its zone and the game's check** (selected: it keeps the game's rules and reaches every door the player stands near).
- **Press the prompt's action for the player** (rejected: the game offers only the nearest activatable, so pointing at another door in range would work the wrong one).
- **Reach Animated Objects from anywhere along the ray** (rejected: it skips the game's own rule that the player stands at the door, and would open sheds from across the yard).
- **Treat doors as Click Points** (rejected: Interactive Control has none on buildings, and there is no icon or node to select in a cone).

## Consequences
- The Companion Mod has to know which Animated Objects the player stands in the zone of. The game only tracks the activatable it shows, so the mod follows the zones itself.
- An Animated Object whose moving parts have no collision cannot be pointed at. Its prompt still works.
- A game update that renames the toggle or the trigger check switches the pointing off for Animated Objects, and their prompts keep working.
