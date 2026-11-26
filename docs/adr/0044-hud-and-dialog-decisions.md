# Decisions behind the HUD, Stereo Dialogs and the Fish Catch Answer

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0010](0010-in-engine-vr-hud-inset-and-convergence-parity.md), [ADR-0022](0022-per-group-hud-margins-and-a-separate-safe-area.md), [ADR-0032](0032-stereo-dialogs-stay-in-the-stereoscopic-view.md), [ADR-0037](0037-the-hud-depth-moves-the-hud-groups-as-one-panel.md).

## Context and Decision
This record holds the non-obvious decisions behind the HUD Groups and the HUD Convergence Offset, Stereo Dialogs and their probe, and the fish catch auto-answer. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `mods/FS25_VRCompanion/src/VRCompanionDialogProbe.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Dialog probe

  A diagnostic console command for Stereo Dialogs (spec #142, ADR-0032): while a game dialog is open it writes what the game shows
  to the game log, so that a headset run on a real fish catch (the Highlands Fishing DLC's Lua is encrypted) tells what the
  Stereo Dialog support needs: the dialog's GUI name, how sell and release are answered (a callback or a method), which input
  actions answer it, which fish fields it holds (species, weight, price) and where its root sits. Read-only: nothing is called,
  nothing is written to the game's tables, and any field that cannot be read becomes a line, never an error.

  The game's GUI (Gui.lua): g_gui.currentGuiName is the open SCREEN's name (a dialog does not set it), g_gui.dialogs holds the open
  dialogs' root GuiElements in the order they opened, and each root's `target` is the dialog's controller object, whose class chain
  holds the methods (the sell and release handlers, the base dialog's). The newest dialog is the one the player answers.

  PUBLIC SURFACE
    VRCompanion:getDialogDiagnostics() -> lines   the lines the command writes, without the tag ("no dialog open" when none is)
    Registered with the core: the console command only; the registry adds it on map load and removes it on map unload
    Console: vrDialogDiagnostics   logs, tagged "[FS25_VR] dialog probe: " in the game log (Documents\My Games\FarmingSimulator2025\log.txt):
      g_gui.currentGuiName and currentDialogName, the open dialogs' names, the newest dialog's name and class, its class chain
      (each class's method names), its root element's fields and its target's own fields (one level deep, functions by name), and
      every position, size, margin or offset field of the root and of the target's elements, the yes and no buttons and the text
      element dumped whole (their input action and click handler), and every long string in full (the dialog's text holds the catch)

  The dump's readers are the Hand Tool probe's (VRCompanionHandTools.lua: VRCompanion.HandTools.probeReaders).
  ```

- **`DUMPED_ELEMENTS`**: The target's elements dumped whole: the answer buttons (their input action, click handler) and the text the player reads.
- **`dumpTable`**: Writes a table's own fields: a line per field with its name, Lua type and short value, a small table field's entries one level further, and the function names in one line (a function is a field like any other, by name). Reads with rawget, so no \__index of the game's runs.
- **`dumpTable`, `local plain, rest = {}, {}`**: Plain values (numbers, strings, booleans: the fish's species, weight and price) come before tables, so that a dialog with hundreds of element tables is cut among those, not among the values.
- **`guarded`**: Every part runs on its own: a failure becomes a line, and the other parts go on.

### `mods/FS25_VRCompanion/src/VRCompanionFishCatch.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Fish catch auto-answer

  The saved console command `vrFishCatch [ask|sell|release]` (spec #142, ADR-0032) answers the fish catch dialog (Highlands Fishing
  DLC), the Stereo Dialog, as it opens, so the player never sees it, and shows a short notification of the catch instead. It
  applies in the stereoscopic view and in 2D Floating Window Mode alike. A flat choice: no rule by weight, species or price.

  PUBLIC SURFACE
    VRCompanion.fishCatchAnswer                  "ask" | "sell" | "release", default "ask", saved (`vrFishCatch`); `ask` leaves the dialog alone
    VRCompanion:updateFishCatch()                once per update, before the GUI is polled (the early update hook this file registers)
    Registered with the core: an early update hook (updateFishCatch), a map-unload hook and the console command; the registry adds
    and removes the command
    Console: vrFishCatch [ask|sell|release]      no argument reports the value, an invalid one is refused with the usage

  THE ANSWER PATH (chosen for ticket #147): the dialog's own answer method, `target:onYes()` for SELL and `target:onNo()` for RELEASE,
  where `target` is the dialog root's controller (the Stereo Dialog found by VRCompanion:findStereoDialog). The headset probe (ticket
  #143) found that the dialog is a generic yes/no dialog whose class chain has both methods and whose yesButton is SELL and noButton is
  RELEASE; the engine's own YesNoDialog:inputEvent calls self:onNo() for the back action, so calling it is the same step the dialog
  itself takes.
  A dialog without the method is left open (the player answers it with the buttons, as with `ask`) and the game log says so once: the
  Virtual Gamepad only mirrors the Motion Controllers (ADR-0028), so the mod cannot press a pad button of its own.
  Called before the GUI poll, the answer closes the dialog before the poll sees it, so the quad of 2D Floating Window Mode does not
  flash for it either. Once per opening: the dialog that was answered is remembered until it is gone.

  THE NOTIFICATION: the dialog holds no species, weight or price field, only its text (target.dialogTextElement.text, measured:
  "You've caught a trout!\n\nDo you want to release the fish to the water or sell it for 100 $?"). Where the text matches that
  template, the notification names the species (and the price when sold); otherwise it is "Fish sold" / "Fish released". There is no weight.
  ```

- **`pendingNotice`**: announced once the dialog is gone after an attempt, not before: the call may have done nothing
- **`readCatch`**: Species and price from the dialog's text, nil where it is not the template the probe measured
- **`VRCompanion:updateFishCatch`, `if type(dialogTarget.isInputDisabled) == "function" then`**: Not while the dialog says it takes no input yet: it would ignore the call
- **`RETRY_TICKS`, `MAX_ATTEMPTS`**: The answer is asked again because one is not enough, and the dialog gives no sign of when it would be. Checked in the headset on 2026-11-26 (spectral-byte/FarmingSimVr-issues#228): a single `onYes` or `onNo`, sent in the update the fish dialog first appears and with `isInputDisabled()` false, left the dialog open for sell and for release alike. With the retry both closed it on the third attempt, about 0.4 s after it opened. The dialog's own code is not in the readable source, so why it ignores an early answer is not known.
- **`registerSubsystem "fishCatch"`**: Every update, as an early hook: the core polls the GUI visibility after it, so the answered dialog is gone when the poll looks (the quad of 2D Floating Window Mode does not flash for it). Map unload: the registry removes the command, then the dialog that was answered is forgotten. The saved setting stays as it is. Order: none matters against the other subsystems; the early phase is what places it before the poll.

### `mods/FS25_VRCompanion/src/VRCompanionHud.lua`

- **`VRCompanion.HUD_LOAD_ALERT_TEXTS`**: The map-load HUD alert for each HUD state: its l10n key (modDesc.xml) and its English text. It names no key or button, since the HUD Toggle Action can be rebound.
- **`VRCompanion.HUD_GROUPS`**: The four HUD Groups (CONTEXT.md, ADR-0022). Each group lists its engine displays and its fixed home corner, and says whether it shows in the Streamlined and Full HUD. Placement and visibility both look a display up through its group. The context action display is in no group. The group names, in order, are VRCompanion.HUD_GROUP_NAMES (VRCompanionConfig.lua): that one list drives layout, visibility, save/load and listing.
- **`CONTEXT_ACTION_DISPLAY`**: Not in any group: bottom-centered on the Speedometer group's bottom margin, drawn at that group's scale, and shown in both the Streamlined and Full HUD.
- **`VRCompanion:getHudFovCompensation`**: FOV Overscan widens the view by (1 + percent / 100) in tangent, so the same normalized screen position and size would sit further out, look bigger and read nearer. The HUD shrinks toward the screen centre by this factor instead, which keeps its angular position and size (and, with the convergence disparity, its depth) as without the overscan.
- **`VRCompanion:getHudDepthScale`**: The HUD Depth moves the HUD Groups back or forward as one panel straight ahead. Their layouts are set for HUD_LAYOUT_DEPTH (1.8 m), so at `depth` a group looks 1.8 / depth times its size and sits that much nearer to the screen centre, as a panel does from further away; the convergence disparity puts it at the same depth. 2D Floating Window Mode has no depth.
- **`VRCompanion:getHudGroupViewScale`**: How much the HUD Groups are shrunk toward the screen centre: the FOV Overscan compensation and the HUD Depth together. The HUD Safe Area takes only the overscan: warnings, notifications and other mods' HUDs are not drawn at the groups' scale.
- **`VRCompanion:placeHudGroupEdgeDistance`**: A HUD Group Margin, as the normalized distance from the screen edge the group is placed at: pulled in toward the centre, or pushed out toward the edge by a HUD Depth nearer than the default, but never past the edge
- **`VRCompanion:getHudDisplayPosition`**: Where a display's bottom-left corner goes: its group's outer edges sit at the group's margins from the home corner, with the display's width or height subtracted for right- and top-anchored groups. The Speedometer group's bottom margin is measured to the bottom of the lowest fill level, so the fill levels sit on it and the speedometer stacks above them, one scaled gap clear. The convergence offset moves every display horizontally by the same amount.
- **`VRCompanion:getHudDisplayPosition`, `if fillHeight > 0 then`**: No fill levels shown: nothing to stack above, so no space and no gap are reserved
- **`renderedTanHalfFovX`**: tan(fovX / 2) of the view the eyes are rendered with, or nil before the camera path has fetched a headset projection. The FOV is the one fetchEyeProjection last read (both eyes are rendered with the same symmetric FOV, which already holds the FOV Overscan), so the HUD asks VRBridge nothing more per update.
- **`VRCompanion:computeConvergenceDisparity`**: The share of the screen width each eye's HUD is shifted by so that it reads `depth` metres away: each eye sees a point that far straight ahead half the IPD to the side, (ipd / 2) / depth in tangent, on a screen 2 tan(fovX / 2) wide. Without a headset projection, a fixed factor (right for a 73 degree view) stands in, shrunk by the FOV Overscan.
- **`VRCompanion:computeConvergenceDisparity`, `compensation`**: The disparity is a share of the screen width, which the FOV Overscan widens
- **`VRCompanion:setHudConvergenceDepth`**: `vrHudDepth [1.0-5.0]` sets the HUD Depth, how far away the HUD Groups are in the headset, in metres (2.5 by default, saved as vrSettings.hudConvergenceDepth). The groups move back or forward as one panel: further away they are drawn smaller and nearer to the screen centre (getHudDepthScale) and shifted less between the eyes (the HUD Convergence Offset, which the next updates ease to the new depth). They are placed again at once.
- **`HUD_TRACE_DEFAULT_FRAMES`**:

  ```text
  `vrHudTrace [frames]` (diagnostics, not saved): for the next rendered frames (12 by default, up to 120) logs to log.txt
  where the HUD is placed for each eye, to find out in the headset why the HUD only shifts sideways instead of taking its depth.
    update lines: every HUD convergence update, with the frame and eye it ran for, the offset it placed the HUD Groups at and
                  where each group's display says it is.
    rendered lines: one per rendered frame, keyed by VRBridge's frame and sub-pass, with the eye that frame goes to and, for
                  each HUD display, the x range its overlays were drawn over in that frame ("other": overlays drawn outside
                  the displays' own draw).
  Under Sequential Dual-Pass a display's range should differ by twice the offset between the two frames of a pair: the same
  range in both eyes is a display drawn with one layout for both. Overlay.render, and the draw of each display instance, are
  wrapped only while the trace runs.
  ```

- **`recordHudTraceOverlay`, `if VRCompanion.mapDrawDepth > 0 and VRCompanion.mapHotspotDepth == 0 then`**: The mod's own Overlay.render hook, which this one wraps, moves the map's overlays to the Map group as they are drawn (runMapDraw): log where they land, not where the engine put them
- **`VRCompanion:traceHudConvergenceUpdate`, `print("[FS25_VR] HUD trace: no overlays were rendered during the trace")`**: Nothing was rendered through Overlay.render (the HUD hidden, or drawn some other way): end rather than log forever
- **`VRCompanion.HUD_EYE_TEST_SHIFT`**: `vrHudEyeTest [left|right|off]` (diagnostics, not saved, off after a map unload): moves the whole HUD far to the right, by HUD_EYE_TEST_SHIFT of the screen width, in the frames rendered for the named eye only. Closing one eye at a time then shows which eye's image those frames' HUD actually reaches: only that eye (the per-eye HUD works), the other eye (the HUD reaches the eyes swapped), both eyes or neither (one frame's HUD is shown to both eyes).
- **`VRCompanion:getFillLevelsBlockSize`, `numLevels`**: The display's own list of fill levels is authoritative, and may be empty. Only a display that says nothing about its levels is assumed to be showing one row.
- **`VRCompanion:positionFillLevelsUnderSpeedometer`**: Places the Speedometer group's stack: the fill levels right-aligned on the group's bottom margin, with the speedometer directly above them (see getHudDisplayPosition). Returns the fill levels' x and y and the speedometer's y.
- **`VRCompanion:updateHudConvergence`, `isTrackingActive`**: The mod's own head-tracking switch, not the engine's setting (isHeadTrackingActive): the mod places the head itself, and in the game the engine's setting reads off, which kept the HUD at zero disparity whatever its depth.
- **`VRCompanion:updateHudConvergence`, `if self:getHudState() ~= self.HUD_STATE_HIDDEN then`**: Anchors and convergence state above always update; moving the elements themselves is wasted work while the HUD is Hidden. setHudState repositions them against this state when the HUD becomes visible.
- **`VRCompanion:repositionHudForConvergence`, `place`**: Each display sits at its group's margins, shifted by the same convergence offset
- **`VRCompanion:applyHudState`, `inputHelpVis`**: The input help's own visibility is the game's minimized/maximized toggle (F1), so a shown Controls group keeps the player's choice instead of always opening maximized
- **`VRCompanion:setHudState`, `if state ~= self.HUD_STATE_HIDDEN and not self.is2DViewActive then`**: Hidden updates skip repositioning, so place the elements now with the current convergence offset. 2D Floating Window Mode keeps its own anchors.
- **`VRCompanion:rememberControlsExpanded`**: Remembers the game setting the input help shown (maximized) or hidden (minimized), which is how its help toggle (F1) works, and saves it when it changes. Only a change while the Controls group is shown and no menu is open is the player's: the mod hides the input help in the other HUD states, and menus such as the construction screen open it for as long as they are up.
- **`VRCompanion:rememberControlsExpanded`, `menuOpen`**: Asked of the GUI directly: a menu opens the input help as it opens, before the menu state is next polled
- **`VRCompanion:calculateHudAnchors`**: The HUD Safe Area: the engine's global HUD anchors, which keep everything outside the HUD Groups (warnings, notifications, other mods' HUDs) inside the lens. It does not place the HUD Groups.
- **`VRCompanion:positionHudDisplay`, `if type(display.setScale) == "function" then`**: Scale first, so the display's size is measured as it will be drawn
- **`VRCompanion:positionHudDisplay`, `local anchorX, anchorY = self:getHudDisplayPosition(display, displayName, sel...`**: The current convergence offset shifts every display, as it does in repositionHudForConvergence
- **`VRCompanion:repositionAllHudDisplays`, `d6`**: The fill levels go first: the speedometer sits above them, so their size must be settled
- **`MAP_DRAW_CLASSES`**: The engine builds the map's layout in the screen corner (from g_safeFrameOffsetX/Y) and draws its content, ring, north arrow and hotspots there at absolute overlay positions that setPosition does not move, so placing only the element leaves them outside the lens. While the map's draw methods run, every overlay rendered, and its clip rectangle, is shifted from the engine's corner to the Map group.
- **`VRCompanion:getMapDrawShift`**: How far the map's draw moves from the engine's corner to the Map group, or nothing where the engine's own position applies: the fullscreen map, the Hidden HUD and 2D Floating Window Mode
- **`VRCompanion:callWithMapLayoutShifted`**: Runs fn(layout, ...) with the layout's position fields moved to the Map group. The layout crops the map to its circle with a native 2D mask it sets in drawBefore, which a mod cannot intercept, and draws its text with natives too, but both take their absolute coordinates from these fields.
- **`VRCompanion:hookHud`, `if displayName == "InputHelpDisplay" and type(cls.setVisible) == "function" then`**: The help toggle (F1) may set the input help's visibility directly rather than through the HUD
- **`VRCompanion:hookHud`, `self.mapEngineBaseX`**: The map draws at the engine's corner: its draw methods run as one map draw, during which Overlay:render shifts every overlay and clip rectangle to the Map group's position
- **`VRCompanion:hookHud`, `hookMethod(cls, key, function(layout, ...)`**: The layout sets the map's circular mask in drawBefore and places its text, both at absolute coordinates taken from its own position fields, so they run with those fields moved
- **`VRCompanion:hookHud`, `hookMethod(cls, key, function(...)`**: Hotspots take their position from getMapObjectPosition, shifted below, so the overlay shift must not move them a second time
- **`VRCompanion:hookHud`, `if type(cls.getMapObjectPosition) == "function" then`**: Hotspots (fields, vehicles) are drawn with natives at the screen position the layout hands out
- **`VRCompanion.DEFAULT_STEREO_DIALOG_DEPTH`**: A Stereo Dialog (ADR-0032) is drawn at HUD depth: while it shows in the stereoscopic view, the game's GUI draw runs with the dialog's root moved by the HUD Convergence Offset of the eye being rendered, the same displacement the HUD Groups get (under AFR the sign alternates with the frame parity). The root is a full-screen element, and GuiElement:setAbsolutePosition moves it and all its descendants by the same amount, so the whole dialog moves with it. The draw is where the move happens, so that the eye is the one being rendered, and what was moved is moved back by the same amount afterwards, from wherever the draw left it, so that nothing the game places from the dialog's own values drifts (the lesson of the rod minigame, #140). A dialog whose root cannot be moved this way is drawn where the game placed it: zero disparity. The dialog has a depth of its own, `vrDialogDepth` (default 2.5 m, saved), apart from the HUD Depth: it fills more of the view than a HUD group, and read too close at the HUD's former 1.8 m default in the headset. Its disparity is that of the depth, with the sign of the eye being rendered; like the HUD's it is nil (no shift) while the HUD's convergence is off (pass-through, no tracking).
- **`registerSubsystem "hud"`**: The HUD in the subsystem registry Map load (after the core has read settings.xml): the HUD Safe Area and the HUD Groups are applied, then the HUD goes to the state the player last chose; a first install starts Hidden for full VR immersion. Map unload: the HUD goes back to the full screen in the Full state, the diagnostics stop and the convergence state is reset. Every update: its updateBeforeActions hook runs updateHudConvergence, at the place in the update where the core called it before (after the camera and mission hooks, before the VR action events and the vehicle transition, whose applyHudState follows it), so a raise in it is logged once and the rest of the update runs. The camera and mission hooks (hookHud, hookGuiDraw, unhookHud, unhookGuiDraw) stay direct calls of the core, and so do the two calls of updateHudConvergence in the mission update's Sub-Passes (runMissionUpdate, after finishSubPass): they are not part of the update and are as they were. Unload runs after Input's (no order matters between the two) and before Stereo's, as it did before the registry; Stereo then leaves 2D Floating Window Mode without applying the Safe Area or the HUD state again (see the core's deleteMap).

### `tests/test_dialog_diagnostics.cpp`

- **the file**: Stereo Dialogs, the probe (spec #142, ticket #143): the console command vrDialogDiagnostics logs what the game shows while a dialog is open, so that a headset run on a real fish catch (Highlands Fishing DLC, whose Lua is encrypted) tells what the rest of the spec needs: the dialog's GUI name, how sell and release are answered, which input actions answer it, which fish fields it holds and where its root sits. Seen from outside: the real Companion Mod runs with the game stood in by Lua stubs (the console and g_gui as the engine's Gui.lua keeps them: `dialogs` holds the open dialogs' root elements, each with a `target` that is the dialog's controller object), the test runs the command as the game would and asserts what the developer reads in the game log and what the command answers. Never the mod's own state. Prior art: test_hand_tools_describe_unsupported.cpp (a probe's log lines), test_virtual_gamepad_menu_use.cpp (g_gui stubs).
- **`kStubs`**: The engine's console and the game's GUI. A dialog is the root GuiElement in g_gui.dialogs; its `target` is the controller object (Gui:loadGui, Gui.lua), whose class chain holds the methods. `showFishDialog` opens a stand-in for the fish catch dialog: its own fields (the fish), a class with the answer handlers and a parent class with the base dialog's methods.
- **`CompanionModTest.ADialogWithManyElementFieldsStillLogsItsPlainValuesBeforeTheCut`**: A real dialog controller holds hundreds of element tables next to its few values: the values come first, so a cut leaves the fish's species, weight and price in the log.
- **`CompanionModTest.TheCommandLogsTheFullTextsAndTheAnswerButtonsOwnFields`**: The headset's first run (a generic yes/no dialog: SELL and RELEASE buttons, the catch only in a long text the log cut short): the full text, and the answer buttons' own fields (their input action, their click handler) are what the next step needs.

### `tests/test_fish_catch_auto_answer.cpp`

- **`kFishStubs`**:

  ```text
  The game's GUI with the fish catch dialog as the headset probe measured it (spec #142, the first comment): the dialog is the
  root element in g_gui.dialogs, g_gui.currentDialogName is "FishDialog", g_gui.currentGuiName stays empty, its target is a
  generic yes/no dialog (onYes is SELL, onNo is RELEASE) and the catch is only in the text, target.dialogTextElement.text.
  Answering a dialog closes it, as the game's does. The answers it was given are recorded in `answers`, the notifications the
  mission was asked to show in `notices`. `showFishDialog{...}` opens one:
    text         the text the dialog shows (the probe's, by default)
    stayOpen     an answer does not close the dialog
    noMethods    the dialog has no onYes / onNo
    inputDisabled  isInputDisabled() is true, and an answer is ignored, while `fishInputDisabled` is true
    ignoreFirst  the first N calls to onYes / onNo do nothing
  `showDialog(name)` opens another dialog.
  ```

### `tests/test_hud_convergence.luau`

- **`TheHudKeepsItsDepthWhileTheEnginesHeadTrackingSettingIsOff`**: In the game the engine's own head-tracking setting reads off (no TrackIR) while the mod places the head itself (as found for the Pointer Ray, 2b334b7): the HUD must keep the depth it is set to, or vrHudDepth and vrDialogDepth do nothing.
- **`TheHudShiftIsTheDisparityOfItsDepthInTheRenderedView`**: The shift that puts the HUD at a depth comes from the view the eyes are rendered with: a point `depth` metres ahead is seen half the IPD to the side by each eye, which is (ipd / 2) / depth in tangent, over a screen 2 tan(fovX / 2) wide. (Headset check: with a fixed factor the HUD read at about half the depth set on a 109 degree view.)
- **`TheHudShiftIsTheDisparityOfItsDepthInTheRenderedView`, `assert(VRCompanion:fetchEyeProjection(0), "the projection is fetched")`**: The camera path fetches the eye projection every sub-pass; the HUD takes the FOV from that fetch
- **`TheHudShiftIsTheDisparityOfItsDepthInTheRenderedView`, `VRCompanion.fovOverscan`**: The FOV Overscan widens the rendered view, which the projection already says: it is not taken off a second time
- **`TheHudDepthTheCommandSetsIsWhatSettingsXmlGetsAndGivesBack`, `store["vrSettings.hudConvergenceDepth"] = 0.5`**: Older versions allowed 0.5 m, and the file can be edited by hand: a depth out of range loads as the nearest one allowed

### `tests/test_hud_insets.luau`

- **`OVERSCAN_COMPENSATION`**: FOV Overscan widens the view by (1 + percent / 100) in tangent, so a screen-space HUD placed as before would grow and sit nearer. HUD placement, scale and convergence disparity shrink toward the screen centre by the same factor instead.
- **`withHudDepth`**: vrHudDepth moves the HUD Groups back or forward as one panel straight ahead. Their layouts are set for the default depth (1.8 m); at another depth each group is drawn default / depth times its size and that much nearer to (or further from) the screen centre, which is how a panel looks from further away, while the convergence disparity gives the same depth.

### `tests/test_hud_state_machine.luau`

- **`MapDrawIsShiftedFromTheEnginesCornerToTheMapGroup`**: The engine builds the map's layout in the screen corner and draws its content, ring and north arrow there at absolute overlay positions that setPosition does not move, so the draw itself is shifted from the engine's corner to the Map group's position.
- **`MapMaskAndTextFollowTheMapGroup`**: The circular crop of the map is a 2D mask the layout sets in drawBefore at absolute screen coordinates, and its coordinates readout is drawn from the same layout fields, so both follow the shift as well.
- **`MapHotspotsAreDrawnAtTheMapGroupsPositionOnce`**: Hotspots (fields, vehicles) are placed at the screen position the layout's getMapObjectPosition returns and then drawn with natives, so that position follows the Map group; hotspots that render through overlays are not shifted a second time by the overlay shift.
- **`ModHasNoRawKeyEventHandlerSoBindingsComeOnlyFromInputBinding`**: Bindings live in modDesc.xml and reach the mod only through g_inputBinding action events. A keyEvent on the mod listener would act on raw keys and ignore rebinding in the Controls menu.

### `tests/test_stereo_dialog_depth.luau`

- **`DRAW_DIALOG_X`**: The game's GUI, as Gui.lua keeps it and the headset probe of ticket #143 measured the fish dialog: the dialog is a full-screen root element (position 0,0, size 1x1) in g_gui.dialogs, whose target is the controller with the visible box (target.dialogElement). GuiElement:setAbsolutePosition moves an element and all its children by the same amount.

### `tests/test_stereo_dialogs.cpp`

- **`kGuiStubs`**: The game's GUI, as the headset probe measured it for the fish catch dialog (spec #142, the first comment): the dialog is the root element in g_gui.dialogs, g_gui.currentDialogName is its name ("FishDialog", loaded from pdlc/highlandsFishingPack/gui/ FishDialog.xml), g_gui.currentGuiName stays empty (it names the open SCREEN, a dialog does not set it), and it is a generic yes/no dialog whose yesButton is SELL (MENU_ACCEPT) and noButton is RELEASE (MENU_BACK). A screen (the pause menu) sets currentGuiName and has no dialog. getIsGuiVisible is true for either. The menu input the Companion Mod sends to a menu (a back press, the mouse) is recorded.
- **`CompanionModTest.WhileTheFishDialogShowsTheVirtualGamepadSendsTheMotionControllersToTheDialogAndTheModsOwnMenuBackStaysOut`**: The dialog is answered with its own gamepad buttons (MENU_ACCEPT, MENU_BACK) through the Virtual Gamepad: whatever the Motion Controllers' bindings are, the game reads them from the pad as it does outside menus. The Companion Mod's own menu inputs, which would answer the dialog a second time (the grip as back), are not engaged.
- **`CompanionModTest.AButtonHeldWhenTheFishDialogOpensDoesNotAnswerItButAFreshPressDoes`**: The held-input rule of any menu, for a dialog the pad answers: an input already held when the dialog opens (the lower face button that cast the rod is still down) is not the dialog's, so letting it go cannot answer it (the answer buttons act on release). It reads released on the pad until it is let go; a button pressed afterwards is the dialog's.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 70 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
