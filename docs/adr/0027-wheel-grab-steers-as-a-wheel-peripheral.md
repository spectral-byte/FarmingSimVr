# Wheel Grab Steers as a Wheel Peripheral

## Context and Decision
Wheel Grab lets the player hold the Steering Wheel with a Motion Controller and steer the vehicle being driven. The Steering Wheel turns exactly as far as the hands do, and while it is held the hands' steering wins over every other steering input. The game has two places that steering could enter: `Drivable:setSteeringInput`, which every steering device calls (keyboard, gamepad, Wheel Peripheral, and the game's own phone tilt steering), and the steering value the vehicle keeps (`spec_drivable.axisSide`), which `Drivable:onUpdate` moves toward the input each frame and syncs in multiplayer.

**The Companion Mod steers by calling `setSteeringInput` each frame as an analog Wheel Peripheral (`InputDevice.CATEGORY.WHEEL`), and wraps `Drivable.setSteeringInput` to drop calls from every other device while the Steering Wheel is held.**
- The value sent is the Steering Wheel angle the hands ask for, divided by the vehicle's full-lock Steering Wheel angle, in the direction the game turns that Steering Wheel (reversed when the driver's seat is turned).
- When the grab ends, the Companion Mod stops calling it and lifts the wrap, so the game's own steering back takes over, as for any other input.
- The wrap covers the `Drivable` class function and the controlled vehicle's own copy of it, because the engine copies `setSteeringInput` onto each vehicle when it builds the vehicle types; Wheel Grab's own call is told apart from the other devices' by a flag set while it makes the call, since the game's own tilt steering also calls it as a Wheel Peripheral.
- The wrap passes every call through unchanged when no Steering Wheel is held, when Wheel Grab is off, or when the Companion Mod cannot find the function it expects. Then it logs one warning and leaves Wheel Grab unavailable, never breaking steering.

## Status
Proposed.

## Considered Options
- **Call `setSteeringInput` as a Wheel Peripheral** (selected: it is the function the game's own devices and phone tilt steering use, so multiplayer sync, articulated and crab steering, turning on the spot and speed handling come for free, and it touches no game internals).
- **Write `axisSide` from a wrapped `Drivable:onUpdate`** (rejected: the Steering Wheel would follow the hands with no lag, but it depends on the inside of a long update function a game patch can change, and the Companion Mod would have to redo turning on the spot and keep the multiplayer sync fields in step).
- **Feed a virtual Wheel Peripheral from the Injector** (rejected: the Injector would need to emulate an input device at the OS or DirectInput level, and still could not drop the keyboard and gamepad while the Steering Wheel is held).

## Consequences
- Analog steering is rate-limited by the game: from centre to full lock takes about 0.4 of the vehicle's steering duration. A fast yank of the hands runs ahead of the Steering Wheel for a moment. The Virtual Hand is pinned to the rim and turns with the Steering Wheel's actual rotation, so the player sees the hand stay on the wheel, not the gap.
- Any game code that steers through `setSteeringInput` while the Steering Wheel is held is ignored, including a Wheel Peripheral, until the player lets go.
- Wheel Grab is unavailable whenever steering from the keyboard would be, such as while a hired helper drives. A steering assist reacts to it as it reacts to any steering input. Grabbing never dismisses a helper or switches an assist off itself.
- A game update that renames or reshapes `Drivable.setSteeringInput` turns Wheel Grab off with a warning until the Companion Mod is updated.
