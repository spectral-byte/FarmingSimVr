# Code Carries No Comments

## Context and Decision
The code had 7,039 comments, and 11,608 of its 123,000 lines were nothing else. Most repeated the code. Some excused an implementation that should have been fixed, and code written later copied both the excuse and the implementation. The decisions among them were hard to find and were only as current as the last edit of the line below. **Code carries no comments. A decision that the code cannot show goes into an ADR, a shortcut that needs an excuse goes into the issue tracker, and everything else is said by names and tests.**

1. **Every comment was read and put into a category.** A tautology repeats the code. An excuse lets a hacky implementation stand. A stale comment speaks of code that is gone or of something since settled. A decision record gives a non-obvious reason or fact. The first three were deleted, and each excuse has an issue in `spectral-byte/FarmingSimVr-issues`. The decision records moved to ADR-0042 to ADR-0060, under the file and symbol they stood at.
2. **Reference comments stay until their names can say it.** 637 comments fit no category: units, field contracts, the meaning of a bit, and hand-worked expected values in tests. They are in the baseline.
3. **The rule is a ratchet.** `scripts/check_comments.ps1` counts the comments of each tracked source file and compares them with `scripts/comment_baseline.txt`. A file may not have more than its baseline, and a file without a line there may have none. A file that has fewer fails too, until `scripts/check_comments.ps1 -Update` lowers the baseline, so that the count only goes down. `-Update` never raises one.
4. **Three kinds of comment are not counted**, because a tool reads them: the version beside a pinned hash, which `scripts/check_pins.ps1` requires, `# noqa` in Python, and `#!` and `#Requires` lines.

| Record | Area |
|---|---|
| [ADR-0042](0042-companion-mod-core-decisions.md) | Companion Mod Core, Registry and Session Settings |
| [ADR-0043](0043-stereo-view-decisions.md) | Stereoscopic View and Dual-Pass Physics |
| [ADR-0044](0044-hud-and-dialog-decisions.md) | HUD, Stereo Dialogs and the Fish Catch Answer |
| [ADR-0045](0045-actions-locomotion-and-seat-decisions.md) | VR Actions, Locomotion and the Seat Offset |
| [ADR-0046](0046-motion-controller-and-pointer-decisions.md) | Motion Controllers, the Pointer Ray and Interactive Control |
| [ADR-0047](0047-virtual-hand-decisions.md) | Virtual Hands and the Shipped Hand Models |
| [ADR-0048](0048-virtual-gamepad-decisions.md) | Virtual Gamepad |
| [ADR-0049](0049-wheel-grab-decisions.md) | Wheel Grab |
| [ADR-0050](0050-hand-tool-decisions.md) | Hand Tools in the Tool Hand |
| [ADR-0051](0051-object-grab-decisions.md) | Object Grab |
| [ADR-0052](0052-animated-object-and-manual-attach-decisions.md) | Animated Objects and Manual Attach |
| [ADR-0053](0053-injector-core-decisions.md) | Injector Startup, Lifecycle, Logging and Crash Reporting |
| [ADR-0054](0054-vrbridge-decisions.md) | VRBridge and the Luau State |
| [ADR-0055](0055-upscaler-interception-decisions.md) | Upscaler Interception and the Motion Vector Correction |
| [ADR-0056](0056-openxr-decisions.md) | OpenXR Session, Eye Images and Input |
| [ADR-0057](0057-present-and-desktop-decisions.md) | Present, the DXGI Proxy and the Desktop Mirror Window |
| [ADR-0058](0058-installer-decisions.md) | Installer |
| [ADR-0059](0059-build-ci-and-tool-decisions.md) | Build, CI, Release Scripts and Tools |
| [ADR-0060](0060-test-stub-and-fake-decisions.md) | Test Stubs and Fakes |

## Status
Accepted.

## Considered Options
- Ban only new tautologies in review (rejected: nothing checks it, and a reviewer argues about each one).
- Delete every comment at once, the 637 reference comments too (rejected for now: a unit or a hand-worked value that goes is lost, and a rename for each is a change of its own).
- Count comments for the whole repository instead of per file (rejected: deleting one in an old file would pay for a new one elsewhere).
- An off-the-shelf linter per language (rejected: six languages, no common tool, and none has a ratchet).
- **One script with a lexer for each language and a per-file baseline (selected).**

## Consequences
- The reason for a piece of code is no longer next to it. A change to a file starts with a search of `docs/adr` for the symbol.
- The records hold the comments' own words, so they are catalogues, longer and less even than the other ADRs.
- Luau inside a C++ string is a string to the rule, so comments in it are not counted.
- `.xml`, `.i3d`, `.json` and `.md` files are outside the rule.
