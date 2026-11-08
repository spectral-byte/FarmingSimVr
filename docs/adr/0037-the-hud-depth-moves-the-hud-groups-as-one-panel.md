# The HUD Depth Moves the HUD Groups as One Panel

Amends [ADR-0010](0010-in-engine-vr-hud-inset-and-convergence-parity.md), item 2.

## Context and Decision
ADR-0010 put the HUD at a reading depth of about 1.8 m through the HUD Convergence Offset alone: each eye's copy of the HUD is shifted sideways by the disparity of that depth, and nothing else changes. `vrHudDepth` exposed that depth, and in the headset it read as nothing but a sideways shift. There were two reasons.

- **The disparity was wrong for the headset's view.** It was `(ipd / depth) * 0.3375`, a factor that only fits a view about 73 degrees wide. With the 109 degree view of the headset tested, the HUD sat at about half the depth asked for (1.8 m read as about 0.95 m, 5 m as about 2.65 m). The disparity is now taken from the field of view the eyes are actually rendered with: `ipd / (4 * depth * tan(fovX / 2))`, a share of the screen width, with `fovX` from the eye projection the camera path last fetched. That FOV already holds the FOV Overscan, so the overscan is not taken off a second time. Before a headset projection has been fetched, the old factor stands in.
- **Disparity alone does not make a flat HUD look further away.** A HUD that keeps its size and its place in the view while only its disparity changes has nothing else telling the eye it moved. What a player means by "further away so it is easier to see" is what happens to a panel pushed back: it looks smaller and sits nearer the middle of the view, where the lens is sharpest.

**The HUD Depth moves all the HUD Groups back or forward as one panel straight ahead.**

- Each group's layout (its HUD Group Margins and scale) is set for a depth of 1.8 m (`HUD_LAYOUT_DEPTH`). At depth `d`, every group is drawn `1.8 / d` times its size and placed `1.8 / d` times as far from the screen centre, and the HUD Convergence Offset gives the same depth. Size, place and disparity all agree.
- From 1.2, a new install starts at 2.5 m (`DEFAULT_HUD_DEPTH`): the groups are drawn 0.72 times their layout size, nearer the middle of the view. An existing install keeps the depth its settings file holds, since every save writes it. The layouts stay set for 1.8 m, so their margins and scales are unchanged.
- This is the same shrink toward the centre that the FOV Overscan compensation already applied, and the two multiply.
- `vrHudDepth` takes 1.0 to 5.0 m (it took 0.5 to 5.0). Nearer than 1.8 m the groups grow toward their corners; at 1 m the default layout, drawn 1.8 times its size, reaches the screen's edges. A group is never pushed past the edge. A saved depth out of range, such as 0.5 m from an older version, loads as the nearest one allowed.
- The HUD Safe Area does not move with the HUD Depth. Warnings, notifications and other mods' HUDs are not drawn at the groups' scale, so pulling their anchors in would only crowd them into the middle of the view. They keep the overscan compensation as before.
- Setting the depth places the groups again at once. 2D Floating Window Mode keeps the engine's layout, as it does for every other HUD setting.
- At 1.8 m nothing moves or changes size. The corrected disparity alone puts the HUD at the 1.8 m it was always meant to read at.

## Status
Accepted.

## Considered Options
- **Keep the HUD Depth as disparity only, with the corrected formula** (rejected: in the headset it still read as a sideways shift).
- **A separate command for HUD size and position, leaving the depth to disparity** (rejected: the player would have to match two settings by eye to get a HUD that looks further away, and `vrHudGroup` already sets each group's own size and place).
- **Move the HUD Safe Area with the HUD Depth too** (rejected: the elements it places do not shrink, so at a far depth they would crowd the centre of the view).
- **Scale the groups and their distance from the centre by the default depth over the HUD Depth, with the matching disparity (selected).** One setting moves the whole HUD the way a panel moves, and every cue says the same depth.
