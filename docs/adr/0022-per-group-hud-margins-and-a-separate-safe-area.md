# Per-Group HUD Margins and a Separate Safe Area

Amends [ADR-0010](0010-in-engine-vr-hud-inset-and-convergence-parity.md).

## Context and Decision
ADR-0010 places every HUD element with one global VR HUD Inset: a horizontal inset shared by both sides, a bottom inset, a top inset and one UI scale, all written into the engine's `g_hudAnchor*` globals. Each element is pinned to a hard-coded corner of that box, and the fill levels have their own "under vehicle" toggle and gap on top. In practice this was confusing to tune: moving the map also moved the controls help, and the Streamlined HUD's contents were fixed in code. The map was never visible in the headset at all: it shows in 2D Floating Window Mode, where the anchors are the full screen, but in the stereoscopic view it sits outside the lens, apparently not taking the position the mod gives it.

**The HUD is split into four HUD Groups, each placed by its own HUD Group Margin from a fixed home corner, with its own scale and its own visibility in the Streamlined and Full HUD.**

- The groups are the Map group (bottom-left), the Speedometer group with the fill levels stacked under the speedometer (bottom-right), the Info group with clock and money (top-right), and the Controls group (top-left).
- A margin is the normalized distance from the group's home corner edges to the group's outer edges, so a larger element grows inward and never off-screen. The Speedometer group's bottom margin measures to the bottom of the fill levels.
- Home corners are fixed. The two margins already move a group anywhere along its edges.
- The context action prompt is not a group. It stays centered at the bottom on the Speedometer group's bottom margin and shows in both the Streamlined and Full HUD.
- **The `g_hudAnchor*` globals keep a single HUD Safe Area margin** for everything outside the groups: blinking warnings, notifications and other mods' HUDs. Without it those would fall outside the lens, the same way the map did.
- Convergence offset shifts every group equally, and 2D Floating Window Mode still uses the engine's default layout.

## Status
Accepted

## Considered Options
- **Raw X/Y per group** (rejected: a wider element runs off the edge, and the player has to remember which corner of the element the coordinate refers to).
- **Keep the global inset and add a nudge per group** (rejected: keeps the two layers that made tuning confusing).
- **Reset `g_hudAnchor*` to the full screen once the groups place themselves** (rejected: warnings, notifications and other mods' HUDs would drift outside the lens).

## Consequences
- The settings file changes shape. The old `hudInsetHorizontal`, `hudInsetVertical`, `hudInsetTop`, `hudUiScale`, `fillLevelUnderVehicle` and `hudFillLevelGap` values are ignored rather than translated, so every player starts from the new defaults once.
- The `vrHudInsets`, `vrHudInsetTop`, `vrHudFillUnderVehicle` and `vrHudFillLevelGap` console commands are replaced by per-group and safe-area commands.
