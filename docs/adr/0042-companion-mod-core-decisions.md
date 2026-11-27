# Decisions behind the Companion Mod Core, Registry and Session Settings

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0002](0002-in-process-luau-state-interception.md), [ADR-0018](0018-lua-and-present-share-one-thread.md).

## Context and Decision
This record holds the non-obvious decisions behind the Companion Mod core: the subsystem registry, the game wraps, the version handshake with the Injector, the localised texts and the small saved settings (Desktop Mirror, FOV Overscan, Menu Distance, Mouse Confinement, the performance trace). They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `mods/FS25_VRCompanion/src/VRCompanionConfig.lua`

- **`VRCompanion.HUD_GROUP_NAMES`**: HUD Group Margin and scale constants. HUD_GROUP_NAMES is the one ordered list of groups: layout, visibility, save/load and listing in messages all follow it.
- **`VRCompanion.DEFAULT_CONTROLS_EXPANDED`**: Whether the Controls group shows its full list of controls (maximized) or is minimized, as the player last chose with the game's help toggle (F1). Saved between sessions; starts maximized.
- **`VRCompanion.HUD_LAYOUT_DEPTH`**: VR HUD Convergence Parity Constants The HUD Depth the HUD Groups' layouts are set for: there they are drawn at their layout size, and at another HUD Depth smaller or bigger as a panel moved back or forward (ADR-0037).
- **`VRCompanion.MIN_HUD_DEPTH`**: The HUD Depth range vrHudDepth takes. The HUD Groups grow as the depth comes nearer than the one their layouts are set for, so the range stops at 1 m, where the default layout, drawn 1.8 times its size, reaches the screen's edges.
- **`VRCompanion.HUD_FILL_LEVEL_GAP`**: Speedometer group stacking: the fixed gap between the speedometer and the fill levels, and between fill level rows, at scale 1.0. It is multiplied by the group's scale and is not adjustable.
- **`VRCompanion.DEFAULT_HUD_STATE`**: The HUD state (Hidden, Streamlined or Full) a map starts in: the one the player last chose, saved between sessions. A first install starts Hidden (VRCompanion.HUD_STATE_HIDDEN).
- **`VRCompanion.DLSS_NEAR_CLIP_CAB`**: DLSS near plane per camera mode (vrDlssNearClipCab / Outside / OnFoot), in metres; 0 = auto, which the Injector estimates from the engine's motion vectors. The renderer's near differs between a vehicle's inside camera, its other cameras and the on-foot camera, so each can be fixed on its own. The cab and on-foot defaults are the renderer's known nears; the outside cameras vary, so they default to auto.
- **`VRCompanion.isFiniteNumber`, `VRCompanion.parseFiniteNumber`**: Shared finite-number validation rejecting `NaN`, positive/negative infinity, and (in `parseFiniteNumber`) out-of-range or non-integer values across `VRCompanion:clamp`, XML float reads, and console commands.
- **`VRCompanion:saveConfiguration`**: Checks both the `pcall` result and `saveXMLFile`'s return value (`false` or raise logs `[FS25_VR] [Error]` and returns `false`, while `nil` from void test stubs still succeeds); invalid handles from `createXMLFile` also log an error and return `false`.
- **`VRCompanion:saveConfiguration`, `modeStr`**: Persist the mode last requested, not VRBridge's: while the compositor is active the bridge only queues a change until the next frame boundary, so it still reports the old mode here.
- **`readSettingsFromXml`, `loadedSafeAreaHorizontal`**: The old hudInsetHorizontal, hudInsetVertical and hudInsetTop keys are ignored (ADR-0022)
- **`readSettingsFromXml`, `if loadedDepth ~= nil then`**: A depth out of vrHudDepth's range (older versions allowed 0.5 m) loads as the nearest one allowed; non-finite values are rejected by `readXmlFloat` so the default remains.
- **`VRCompanion:loadConfiguration`, `runSubsystemResetSettings`**: When `loadXMLFile` is present, resets all core scalar settings and invokes every subsystem's `resetSettings` hook before reading XML, so missing files or missing/non-finite keys cannot retain stale in-memory values across map reloads while direct calls to a subsystem's `loadSettings` hook still preserve current values on missing keys (and tests that pre-seed fields without XML stubs are unaffected).
- **`VRCompanion:loadConfiguration`, `if not renderingModeLoaded and not self.renderingModeExplicit then`**: No saved Stereo Presentation Mode and none chosen this session: apply the default through setRenderingMode so it reaches VRBridge and is marked explicit (otherwise a parity-only bridge reads as AFR).

### `mods/FS25_VRCompanion/src/VRCompanionCore.lua`

- **`VRCompanion.VERSION`**: The release this Companion Mod is: the \<version> of modDesc.xml, which a test pins this constant to. The Injector is released with it, so the two are the same number; the handshake passes it to the Injector, and a map load compares it with the Injector's (see VRCompanion:describeVersionMismatch). Not read from the game's mod manager, so nothing depends on an engine API.
- **`VRCompanion.LEGACY_INJECTOR_VERSION`**: Every Injector up to release 1.2 reported this fixed text as its version, whatever release it was.
- **`VRCompanion.VERSION_WARNING_MS`**: How long the version warning blinks, in milliseconds: long enough to read in the headset.
- **`VRCompanion.VEHICLE_SPEED_HOOK_RETRY_TICKS`**: Slow retry cadence (in update ticks) for re-running the dt=0 guard install so a late Ferry class or newly spawned vehicles are picked up without scanning \_G every frame.
- **`VRCompanion.getEngineGlobal`**: An engine global by name, or nil when the game does not have it: the engine's functions and tables are read through this, so that one that is missing is a nil to test for and not an error. Virtual Hands and Wheel Grab alias it as their local `engine`; this file loads before both.
- **`VRCompanion.LOOK_RAY_AIMER_ORDER`**: Who aims the look ray The game's Player:getLookRay is wrapped once, as the shared wrap "lookRay" of VRCompanionGameWraps.lua, for as long as a subsystem that aims holds the wrap (Hand Tools and Object Grab each hold it, so that one giving up leaves the other aiming). The wrap asks each subsystem that can aim, in this fixed order, and answers the first ray it gets: the game's own (the head's) when none does. A subsystem registers by name with an aimer, function(player) -> x, y, z, dirX, dirY, dirZ in the world, or nil when it is not aiming for that player (Hand Tools: a supported Hand Tool is in a tracked Tool Hand; Object Grab: the hands are empty and the Pointer Hand points).
- **`aimersLoggedAsRaising`**: The ray the first aiming subsystem answers for `player`, or nil (the game's own ray). An aimer that raises is not aiming: it is logged once, by name, with its error, and the next one is asked.
- **`subsystems`**:

  ```text
  The subsystem registry
  A subsystem (a feature in its own file: Desktop Mirror, Hand Tools, ...) registers itself by name when its file loads, with the
  hooks it needs, and the core runs them. Adding a subsystem is its file plus a line in modDesc.xml, not an edit of the core's lists.

    VRCompanion:registerSubsystem(name, {
        mapLoad         = function(self) end,        -- map load: apply the saved setting, wrap the game's classes, ...
        mapUnload       = function(self) end,        -- map unload: undo what mapLoad did
        update          = function(self, dt) end,    -- every update
        updateEarly     = function(self, dt) end,    -- every update, before the core polls the GUI (see below); rarely needed
        updateBeforeActions = function(self, dt) end, -- every update, before the core checks the VR action events (see below); rarely needed
        saveSettings    = function(self, xmlFile, xml) end,   -- saveConfiguration: write its settings with xml.setBool / setFloat /
                                                              -- setString / setInt (the engine's setters, any may be nil)
        loadSettings    = function(self, xmlFile, xml) end,   -- loadConfiguration: read them back with xml.has / getBool / getFloat
                                                              -- / getString / getInt; a key that is missing keeps the current value
        resetSettings   = function(self) end,                 -- loadConfiguration: reset its settings to defaults before reading XML
        consoleCommands = { { name = "vrX", description = "...", method = "consoleCommandX" }, ... },
    })

  A subsystem owns its settings' keys in settings.xml; the settings code (VRCompanionConfig.lua) opens the file, writes and reads
  the core's own settings (HUD Safe Area, HUD Groups and their visibility, the Controls group's expanded state, HUD Depth, the HUD
  load alert, the Recenter Action mode, Stereo Presentation Mode, HUD state, Snap Turn angle, the DLSS near planes) and runs the
  hooks, but names no subsystem. A load hook only stores the value; sending it to VRBridge is its mapLoad's job (every load hook
  does only that). A subsystem that wraps a game function does it through the shared wrap helper (VRCompanionGameWraps.lua), which
  the registry does not call: the subsystem wraps from its own hooks (mapLoad, or its update for a class that appears later) and
  lifts its wraps in its mapUnload.

  Every field but the name is optional; `self` is the Companion Mod the run point was called on (hooks are called as methods, with
  plain functions, so a hook that calls `self:foo()` finds `foo` when it runs). Registering a name again replaces its hooks and
  keeps its place. A bad name, a hook that is not a function and a console command without a name, a description or a method
  raise when the file loads, not when the map does.

  Order. Map-load and update hooks run in registration order, which is modDesc.xml order; map-unload hooks run in reverse. Where
  two subsystems need an order, their file order gives it and the registration says so in a comment. The core's own steps come
  first and are not hooks: the handshake with VRBridge, the engine's stereo and head tracking toggles, the camera and mission
  hooks and loadConfiguration. The run points in the core are:
    loadMap   runSubsystemMapLoad        after the handshake and loadConfiguration, so a hook sends VRBridge the setting its load
                                         hook stored; before the DLSS near plane is sent (neither reads what the other writes,
                                         see loadMap) and before the HUD load alert, which names the HUD state the HUD's hook set
    loadMap   addSubsystemConsoleCommands   with the console commands, after the core's own list
    update    runSubsystemUpdateEarly    right before the core polls the GUI visibility (pollGuiVisibility), so a hook that has to
                                         change what the poll sees (the fish catch answers its dialog, which must be gone when the
                                         poll looks) runs here, in registration order; for hooks that truly need it, not a default
    update    runSubsystemUpdateBeforeActions   after the core has hooked the game's classes and the GUI draw, right before the core
                                         checks the VR action events (updateActionEvents) and the vehicle transition, so a hook sees
                                         this update's GUI poll and nothing of the action events or the transition; the HUD's
                                         convergence (updateHudConvergence) runs here, where it ran as a direct call before it was a
                                         hook, so the order of the update is the one it was
    update    runSubsystemUpdate         after the core's own update steps, so a hook sees this update's GUI poll and vehicle
                                         transition; before applyDlssNearClip, which reads only the controlled vehicle and its
                                         camera, which no update hook changes
    deleteMap runSubsystemMapUnload      the last step of the map unload, after the core's own teardown: removes a subsystem's
                                         console commands, then runs its mapUnload, subsystems in reverse order
    saveConfiguration runSubsystemSaveSettings   after the core's HUD settings, before the Stereo Presentation Mode, the HUD
                                         state, the Snap Turn angle and the DLSS near planes, in registration order
    loadConfiguration runSubsystemResetSettings then runSubsystemLoadSettings at the same place in the load (after the HUD Groups,
                                         before the Snap Turn angle, the DLSS near planes, the HUD state and the Stereo Presentation
                                         Mode), in registration order. resetSettings runs before reading XML so missing keys revert to
                                         defaults, while direct calls to a load hook keep existing values when a key is missing.
  Two steps of the update are direct calls of the core, because no run point is where they must be: the Perf Trace refresh
  (the very top: the Injector may have ended the trace, and nothing after it may time into an ended trace) and the menu's
  background camera (right after the GUI poll).

  A third, the Input subsystem's check of the VR action events (updateActionEvents in VRCompanionInput.lua), is a direct call
  that a run point could now hold: updateBeforeActions runs right before it, and Input registers after the HUD (its file loads
  after the HUD's in modDesc.xml), so a hook of Input's would run after the HUD's convergence and before the vehicle
  transition, the order ADR-0045 gives. It was not moved, so the update is as it was; as a hook, a raise in the check would be
  logged once per subsystem instead of reaching the caller of update.

  A hook that raises is caught: it is logged once per subsystem and hook, as
    [FS25_VR] [Error] Subsystem '<name>' <mapLoad|updateEarly|updateBeforeActions|update|mapUnload|saveSettings|loadSettings|resetSettings> hook raised: <error>
  and the other hooks still run. This is by design (spec #244: a fault in one subsystem does not cost the player the others), so a
  raise in a hook, the Motion Controllers' update included, does not reach the caller of update or of loadMap; the log line is
  how it is found, and the test harnesses fail a test in which that line was logged unless the test expects it. The log-once set
  is kept for the life of the Lua state and is not cleared at map unload (a hook that raised on one map is not logged again on
  the next: the same fault every time is one line, not one per map; test_subsystem_registry.cpp pins it).
  ```

- **`updateEarlyHooks`, `updateBeforeActionsHooks`**: The subsystems with an updateEarly / updateBeforeActions / update hook, in registration order, as { name = \<string>, hook = \<function> }: built when a subsystem registers, so that an update loops over the few that update and not over every subsystem (it runs every frame)
- **`VRCompanion:runSubsystemUpdateEarly`**: The three update run points run every frame: plain loops over the flat lists, no allocation
- **`mismatchedInjector`**: What the version mismatch warning calls the Injector, or nil when it is the same release as this Companion Mod (or reports none, which no release does). An Injector that reports the fixed text of the releases up to 1.2 is older(): the number would say nothing; any other is version(injectorVersion).
- **`VRCompanion:describeVersionMismatchOnScreen`**: The same warning as the player reads it on the screen: in the game's language, "FS25 VR: " and the sentence in English otherwise. log.txt keeps the English sentence (describeVersionMismatch).
- **`VRCompanion:loadMap`, `if VRBridge == nil and type(isHeadTrackingAvailable) == "function" then`**: Proactively trigger native detour in case loadMap executes before any camera update
- **`VRCompanion:loadMap`, `self:set2DViewActive(false)`**: 0. Cleanly initialize 2D Floating Window Mode state to 3D VR mode (is2DViewActive = false). A core step, not a Stereo

  hook, so that it tells VRBridge before the handshake below. Leaving the 2D view resets the Injector's sub-pass index and frame parity when it changes its state, and the handshake aligns both to 0 again, so the first frame starts aligned.

- **`VRCompanion:loadMap`, `handshakeResult`**: The first argument is the optional rendering mode, left to the Injector. The second is this mod's version: an Injector from before it existed ignores it.
- **`VRCompanion:loadMap`, `injectorVersion`**: Both versions go to log.txt on every load, matching or not; a mismatch is a warning there and on the screen (shown at the end of the load, so that the HUD notice does not replace it)
- **`VRCompanion:loadMap`, `if VRBridge ~= nil then`**: 3. Report the stereo requirement (a fixed verdict of the Injector, ADR-0017)
- **`VRCompanion:loadMap`, `self.isMenuVisible`**: (The Dual Eye Camera Nodes and the Cyclopean Audio Listener Node are made by the Stereo subsystem, and the VR action events are registered by the Input subsystem, in their mapLoad hooks below, after the GUI poll and loadConfiguration. Neither of those needs them: the poll reads the GUI and tells VRBridge, and on a map that has just loaded it restores no menu camera and applies no HUD state (no menu was shown before); loadConfiguration reads settings.xml, sends the Stereo Presentation Mode to VRBridge (setRenderingMode, which touches no node), works out the HUD anchors (configureHudSafeArea, fields only) and, on a first run, writes the file. None of them reads an eye node, the listener, the camera settings Stereo saves (origCamSettings) or an action event id, and the nodes are made from defaults no setting changes.)
- **`VRCompanion:loadMap`, `self:runSubsystemMapLoad()`**: 7-. The registered subsystems load. They come after the handshake above and after the settings are read: Desktop Mirror, FOV Overscan, Menu Distance and Mouse Confinement send their saved settings to VRBridge here, and the HUD applies its Safe Area and the HUD state the player last chose. The DLSS near plane below may come before or after them: the HUD hook writes the HUD (its displays, the anchors, hudState) and asks VRBridge for nothing, and the near plane reads the camera mode (the controlled vehicle and its camera) and the near plane settings, so neither reads what the other writes. The re-wraps of Hand Tools, Object Grab, Wheel Grab and the Animated Objects trigger callback run here too: they wrap game classes the HUD, the near plane and the console commands do not call into at map load. The HUD load alert below comes after them, because it names the HUD state the HUD hook set.
- **`VRCompanion:loadMap`, `if versionWarning ~= nil then`**: 10. The Injector is of another release: say so after the HUD notice, so that one does not replace it. Nothing else changes.
- **`VRCompanion:pollGuiVisibility`, `wasStereoDialogShowing`**: A visible GUI takes the input. It also shows the menu quad, unless it is a Stereo Dialog, which stays in the stereoscopic view. In 2D Floating Window Mode the presentation is already the quad, so a Stereo Dialog is any other menu.
- **`VRCompanion:pollGuiVisibility`, `if self.stereoDialogShowing and not wasStereoDialogShowing then`**: A Stereo Dialog does not switch the presentation, so it is told apart from the menu quad in the log.
- **`VRCompanion.STEREO_DIALOGS`**: The Stereo Dialogs (ADR-0032): the game dialogs that stay in the stereoscopic view instead of getting the menu quad, by the name the game gives the dialog in g_gui.currentDialogName. A dialog joins this list only after it is tested in the headset; one that is not on it gets the quad. Measured in the game (ticket #143): the fish catch dialog (Highlands Fishing DLC) is "FishDialog", a yes/no dialog whose buttons are answered by the gamepad's MENU_ACCEPT (SELL) and MENU_BACK (RELEASE).
- **`readLoneStereoDialog`**: The Stereo Dialog that is the only thing the game shows, as the dialog's root element (g_gui.dialogs[1], whose `target` is its controller object and whose buttons and position later tickets work on), or nil. A dialog opened over a screen (the pause menu sets g_gui.currentGuiName; a dialog does not) or over another dialog is not alone, and anything that cannot be read as a lone listed dialog counts as a menu, so that the player is never left in stereo with a GUI they cannot use. The GUI fields read in one protected call. Defined once, so that polling a menu every update makes no closure.
- **`VRCompanion:isMenuQuadShown`**:

  ```text
  A visible game GUI means two different things, and every reader asks for the one it needs:
    isMenuQuadShown()  - the presentation: the menu quad is what the player sees (VRBridge is told so, the
                         stereo pipeline and HUD convergence stand down, the menu background camera runs, the
                         head is not placed in the world, the Virtual Hands are off)
    doesMenuTakeInput() - the input: a menu has the controls (locomotion, Snap Turn and Wheel Grab are refused,
                         the menu buttons and the Virtual Gamepad go to the menu)
  pollGuiVisibility sets both together. The quad is the field isMenuVisible (the name VRBridge uses for it); a
  shown quad always takes the input, so doesMenuTakeInput also holds while it is shown.
  ```

- **`VRCompanion:isStereoDialogShowing`**: A Stereo Dialog is showing in the stereoscopic view: a menu takes the input but the quad is not shown. The dialog is answered with the gamepad's own menu buttons through the Virtual Gamepad, so the Companion Mod's quad menu (the Pointer Ray, the grip and lower face button as back, the thumbstick scroll) and the Virtual Gamepad's taking of every input are not engaged.
- **`VRCompanion:applyDlssNearClip`**: Sends the current camera's near plane to the Injector when it differs from the one last sent, so entering or leaving a vehicle or switching its camera takes effect on the next update. force resends it anyway. Returns the bridge's result when it sends, otherwise false.
- **`VRCompanion:applyDlssNearClip`, `self.appliedDlssNearClip`**: Recorded even when the bridge has no NGX interceptor, so that is not retried every frame
- **`VRCompanion:hookTableSpeed`**: Wraps target.updateVehicleSpeed through a `GameWraps.newGroup({ weakTargets = true })` pool with the dt=0 guard, skipping instances that already inherit an active wrapper from a wrapped class and restoring only untouched wrappers on unload (weak-keyed so gone vehicles can be collected).
- **`VRCompanion:installVehicleSpeedGuards`**: Installs the dt=0 guards on the named classes, the mission's vehicles and Ferry. scanGlobals additionally walks every global table for classes with updateVehicleSpeed, which only the map load install does: it is too slow to repeat during play.
- **`VRCompanion:installVehicleSpeedGuards`, `self.lastVehicleSpeedHookTick`**: Recorded here so the install from loadMap also restarts the slow retry cadence.
- **`withRaiseTraceback`**: Appends the traceback from where an error was raised, so rethrowing it from the outermost frame-check scope does not hide its origin.
- **`endNestedFrameChecks`**: Closes a nested frame-check scope after a normal return. An error skips this; the outermost scope then closes every scope opened inside it.
- **`endOutermostFrameChecks`**: Closes the outermost frame-check scope however the call ended, then passes on its results or rethrows its error, so an error never leaves a scope open.
- **`runWithFrameChecks`**: Runs fn(self, ...) inside a frame-check scope. Only the outermost scope runs fn protected, so the game's own mission update is not wrapped twice when the game runs VRCompanion:update inside it.
- **`VRCompanion:runMissionUpdate`, `self.inMissionUpdate`**: The game's update runs marked as running, so a second wrapper further down the class chain passes straight through and the mod's update logic runs once. An error skips the clear here; the outermost frame-check scope clears it.
- **`VRCompanion:runMissionUpdate`, `eye`**: Sub-pass 0 advances the simulation and sub-pass 1 replays it frozen; which eye each sub-pass renders alternates between pairs (see getDualPassEye).
- **`VRCompanion:runMissionUpdate`, `self:beginEyeProjectionScope()`**: One eye projection fetch and one rendering camera bind per sub-pass: camera updates inside the sub-pass share the cached projection and leave the bind to the single bindRenderingCamera call at the end.
- **`VRCompanion:runMissionUpdate`, `self:applyDualPassPhysicsPause(subPass, rawDt)`**: The engine's physics runs natively whatever dt the update gets (VRCompanionDualPassPhysics.lua)
- **`VRCompanion:runSubPassStep`**: Runs one camera step of a sub-pass; an error is logged instead of escaping, so the sub-pass still ends with its single camera bind and closes its projection scope.
- **`VRCompanion:reportEyeCamera`**: Tells the Injector which camera this sub-pass rendered with: the bound eye camera's world transform and projection, keyed by the eye (the sub-pass index alone does not name it under Eye Order Alternation). The Injector stamps it with its rendered-frame counter. Any missing engine query skips the report, which the Injector then treats as a frame without a camera record.
- **`VRCompanion:reportAfrEyeCamera`**: Alternate Frame Rendering renders one eye per frame, bound by the camera updates rather than a sub-pass. Reports that eye's camera like a sub-pass does, so the Injector's motion vector correction can re-aim the engine's vectors at the same eye's camera two frames back. Sequential Dual-Pass reports from its mission update instead, so this stays silent there. An engine query failure costs a missing record, never the camera update.
- **`VRCompanion:runUpdate`, `self:runSubsystemUpdateEarly(dt)`**: The registered subsystems that update before the poll (the fish catch answers its dialog, so that it is gone when the poll looks)
- **`VRCompanion:runUpdate`, `speedHookRetryDue`**: Install dt=0 guards once, then only retry (to catch a late Ferry class or newly spawned vehicles) on a slow cadence or on a vehicle transition, never every frame.
- **`VRCompanion:runUpdate`, `self:updateActionEvents()`**: Keep in-engine action events active across context transitions, and register them all again after a binding change (Input; before the vehicle transition below, which registers them again on its own)
- **`VRCompanion:runUpdate`, `self:refreshVehicleSpeedGuards()`**: A new vehicle may have just been spawned/entered: re-run the guard install.
- **`VRCompanion:deleteMap`, `self.deferRenderingCameraBind`**: Hand rendering back to the game's active camera before the Dual Eye Camera Nodes go away
- **`VRCompanion:deleteMap`, `if removeConsoleCommand ~= nil then`**: (Stereo deletes its nodes and puts the cameras and the 2D view back, the HUD goes back to the full screen, Input lets go of the VR action events and Locomotion forgets the vehicles' rotation states, in the mapUnload hooks below. The registry runs them after the core's teardown above and, in reverse file order, after the subsystems registered later. What that order has to give:

  - The HUD unloads before Stereo (it registers after it). The order is kept because it is the order of the unload before the registry, where the HUD went back to the full screen before Stereo left 2D Floating Window Mode, so that unloading behaves as it did (the HUD's unload reads whether that mode is on). Stereo's mapUnload leaves the mode last of all, with leave2DViewOnMapUnload rather than set2DViewActive: returning to VR would apply the HUD Safe Area and the HUD state again after the HUD went back to the full screen, and leave the game's HUD anchors at the Safe Area.
  - Within Stereo's mapUnload the nodes go before the camera settings are restored; the restore skips every camera that no longer exists (entityExists), the eye nodes among them.
  - Hand Tools unload before the Virtual Hands (they register after them): a tool hangs below a hand.
  - Stereo's nodes go after VRBridge.onMapUnload, which does not matter: the Injector never sees a node; onMapUnload resets only the Injector's own state (src/luau/vr_bridge.cpp, Lua_OnMapUnload), and Stereo only touches the scene.
  - Nothing in the core's teardown needs what the hooks free: the eye nodes and the listener are read by the camera and hand placement of an update (Stereo, Locomotion, Motion Controllers, Virtual Hands), never by a reset; the action event ids are read by Input alone; the rotation states by the camera hooks, which are lifted at the top of this function.
  - Nothing the four do reaches a wrap another subsystem's reset lifts: the HUD's displays, the message center, the cameras' FOV and projection and the game's menu camera are not among the classes the subsystems wrap (Player.getLookRay, HandTool, Drivable's steering, the animated objects' trigger callback, the attachable candidates, Interactive Control's class).
  - The other resets (Perf Trace, Fish Catch, Dual Pass Physics, the Motion Controllers' group) are described where the Motion Controllers register; they touch only their own state, a wrap, the Injector's pad, cursor and trace, or the hands' nodes.)

### `mods/FS25_VRCompanion/src/VRCompanionDesktopMirror.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Desktop Mirror switch

  The console command `vrDesktopMirror [on|off]` turns the Desktop Mirror Window off while the headset session runs, so the GPU
  time the desktop compositor spends scaling the full-size backbuffer goes to the headset (spec #191). The Companion Mod only
  carries the setting: it sets the VRBridge desktop-mirror flag and the Injector's DXGI proxy reads it at every present. With
  the mirror off the Injector does not call the game's real Present; the headset is unchanged. The mirror shows by itself while
  the session is not running, in Pass-Through Mode and before the Companion Mod's handshake.

  The setting is saved in settings.xml (on by default). When a savegame loads, Core's loadMap reads settings.xml and then runs the
  map-load hook this file registers (see the registry in VRCompanionCore.lua), which sends the value to VRBridge after the handshake.

  PUBLIC SURFACE
    VRCompanion.desktopMirror                          true (the default) or false
    VRCompanion:setDesktopMirror(enabled, save)        sets the field, pushes it to VRBridge, saves settings.xml when `save`
    VRCompanion:applyDesktopMirror()                   pushes the field to VRBridge (the map-load hook, after loadConfiguration)
    VRCompanion:saveDesktopMirrorSetting(xmlFile, xml) / loadDesktopMirrorSetting(xmlFile, xml)   hooks of saveConfiguration / loadConfiguration
    VRCompanion:consoleCommandSetDesktopMirror(arg)    nil reports the state, "on" / "off" sets and saves it, anything else returns the usage
    Registered with the core: a map-load hook (applyDesktopMirror) and the console command; the registry adds and removes the command
    Console: vrDesktopMirror [on|off]
  ```

- **`registerSubsystem "desktopMirror"`**: Map load: the saved setting goes to the Injector (the core has done the handshake and read settings.xml by then). Map unload: the registry removes the command; the saved setting stays as it is, and the next map load reads it again (the Injector's VRBridge flag is reset by VRBridge.onMapUnload). Order: the saved settings reach VRBridge in file order, Desktop Mirror first, then FOV Overscan, Menu Distance and Mouse Confinement (each registers after this file). None matters: each sets an Injector field of its own (src/luau/vr_bridge.cpp).

### `mods/FS25_VRCompanion/src/VRCompanionFovOverscan.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: FOV Overscan

  The console command `vrFovOverscan [0-20]` widens the field of view the eyes are rendered with by that many percent beyond
  what covers the headset's own FOV (spec #221), so a frame the VR runtime turns to follow the head still covers the display
  and the black edges seen during head movement go away. The Companion Mod only carries the setting: it sends the percent to
  VRBridge, and the Injector widens the eye projection it publishes (which this mod hands to the engine camera as it always
  did) and submits to the headset, so the camera and the submitted frame widen together and the world keeps its true scale.
  The Injector applies a new value when the next OpenXR frame begins, so both eyes of a pair share one FOV.

  The setting is saved in settings.xml (0 by default), like the Desktop Mirror setting. When a savegame loads, Core's loadMap
  reads settings.xml and then runs the map-load hook this file registers (see the registry in VRCompanionCore.lua), which sends the
  value to VRBridge after the handshake.

  PUBLIC SURFACE
    VRCompanion.fovOverscan                          percent, 0 to 20 (0 by default)
    VRCompanion:setFovOverscan(percent, save)        sets the field, pushes it to VRBridge and saves settings.xml when `save`;
                                                     false (and nothing changes) when `percent` is not a number from 0 to 20
    VRCompanion:applyFovOverscan()                   pushes the field to VRBridge (map load, after loadConfiguration and the handshake)
    VRCompanion:saveFovOverscanSetting(xmlFile, xml) / loadFovOverscanSetting(xmlFile, xml)   hooks of saveConfiguration / loadConfiguration
    VRCompanion:consoleCommandSetFovOverscan(arg)    nil reports the percent and the rendered FOV, a number from 0 to 20 sets
                                                     and saves it, anything else returns the usage
    Registered with the core: a map-load hook (applyFovOverscan) and the console command; the registry adds and removes the command
    Console: vrFovOverscan [0-20]
  ```

- **`renderedFovProjection`**: The table VRBridge.getEyeProjection fills in place, kept so a call allocates nothing on the game's heap (spec #245)
- **`VRCompanion:setFovOverscan`, `if not self.is2DViewActive then`**: The HUD is placed against the overscan (VRCompanionHud.lua), so it moves with it. 2D Floating Window Mode keeps the engine's layout.
- **`registerSubsystem "fovOverscan"`**: Map load: the saved setting goes to the Injector (a map unload put it back to 0 there). The core has done the handshake and read settings.xml by then. Map unload: the registry removes the command; the saved setting stays as it is, and the next map load reads it again (the Injector's VRBridge value is reset by VRBridge.onMapUnload). Order: Desktop Mirror (VRCompanionDesktopMirror.lua) sends its setting before this file does, then this file, then Menu Distance and Mouse Confinement, which load after it: file order. None matters: each sets an Injector field of its own.

### `mods/FS25_VRCompanion/src/VRCompanionGameWraps.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: game function wraps (spec #280)

  The one implementation of wrapping the game's functions that the Subsystems share: counting a function's declared parameters,
  saying why a game function is not the shape a Subsystem expects, putting a wrapper on a class once per class table (and again
  when the game replaced the table), lifting a wrapper only while it is still the class's function (so another mod's later wrap is
  never undone), wraps two Subsystems share, a wrap on one game object's own copy of a function, and giving up once with a warning.

  A Subsystem declares the functions it wraps as a SET, and gets the checking, wrapping, lifting and giving up from here:

    local wraps = VRCompanion.GameWraps.newSet({
        owner = "objectGrab",                  -- the Subsystem's name: what holds a shared wrap
        giveUp = "Object Grab disabled: %s. Picking up is unchanged.",   -- the warning, %s the reason ("[FS25_VR] [Warning] " before it);
                                               -- newSet raises when it is not a string with a %s
        nameAsSubject = false,                 -- reasons read "<Class.fn>: it is missing" (default) or "<Class.fn> is missing" (true)
        order = nil,                           -- { <name>, ... }: every declared name, in the order they are checked and wrapped,
                                               -- so the first reason logged is the first in it; nil: by name (see CHECK ORDER)
        functions = {                          -- keyed by a name the Subsystem uses for the function
            <name> = {
                class = "HandToolHands",       -- the engine global the function is on, or function() -> the table (another mod's environment)
                className = nil,               -- the class's name in reasons, when `class` is a function
                fn = "onDraw",                 -- the function's name on the class, when it is not <name>
                parameters = 1,                -- how many parameters it must declare at least (a reshaped function declares fewer)
                describe = "(tool)",           -- what they are, for the reason
                make = function(original) return wrapper end,   -- nil: CHECK-ONLY (checked, never wrapped); may be set after newSet
                shared = nil,                  -- the key of a shared wrap (GameWraps.defineShared) held instead of making one
                missingClass = nil,            -- the reason when the game has no such class; nil: a missing class is not a failure
            },
        },
    })

    wraps:wrapAll()                 wraps each function on its class (once per class table), none once the set gave up; a function
                                    that is not the expected shape is left alone (check reports it)
    wraps:check() -> reason|nil     gives up (once) on the first declared function (in `order`, or by name) that is missing or
                                    reshaped
    wraps:findProblem() -> reason|nil   the same reason without giving up (for a Subsystem that warns only later)
    wraps:giveUp(reason)            lifts every wrap and logs the warning, once: a later give-up is ignored
    wraps:problem() -> reason|nil   why the set gave up
    wraps:liftAll()                 lifts every wrap, each only while it is still the function there; lets go of shared wraps
    wraps:reset()                   map unload: liftAll, and forgets that the set gave up
    wraps:wrapInstance(target, { <name>, ... })   wraps the target's own copies (a vehicle's, a tool's: the game copies type
                                    functions onto an object), unless the class wrap reaches them; replaces a wrap of an earlier target
    wraps:liftInstance()            gives the target its own copies back (or nothing of its own, when it had none), where still the wrapper
    wraps:instanceTarget()          the target whose own copies are wrapped, or nil
    wraps:isWrapped(name), wraps:original(name)   whether the class wrap is on, and the game's function under it
    wraps:wrapNames(), wraps:status(name)         the wrapped (not check-only) names, sorted, and "<name> wrap: installed" (or
                                    "not installed", "gave up (<reason>)") for the diagnostics

    GameWraps.newGiveUp(owner, sentence)   the give-up part alone, for a Subsystem that wraps by itself: giveUp(reason) logs the
                                    sentence (as a set's giveUp) once and returns true, false when it already gave up; problem();
                                    reset() at map unload. A set gives up through one of its own.
    GameWraps.parameterCount(fn) -> number|nil   nil when it takes any number, or the runtime cannot tell
    GameWraps.shapeProblem(fn, parameters, describe, subject) -> reason|nil   "<subject> is missing", "... is not a function",
                                    "... takes N parameters, not <describe>"; subject defaults to "it"
    GameWraps.defineShared(key, { class, fn, make })   a wrap more than one Subsystem holds: put on with the first hold, lifted
                                    when the last holder lets go; a set's function declared with `shared = key` holds it

  CHECK ORDER: when two declared functions are missing or reshaped, the warning names the first one checked. A Subsystem whose
  own checks went down a list before the helper (Animated Objects: the trigger callback, the "can be triggered" check, the
  toggle handler) declares that list as `order`, so it warns as before. Hand Tools and Object Grab went over their table with
  pairs, in no fixed order, so they declare none and are checked by name: a fixed order, where theirs had none.

  THE SHARED LOOK RAY: Player:getLookRay is the shared wrap "lookRay", held by Hand Tools and Object Grab, so that one giving up
  leaves the other aiming. Its wrapper answers VRCompanion:getAimedLookRay (Core) and the game's own ray otherwise.
  ```

- **`sortedNames`**: The declared names, sorted, so that the first reason found does not depend on the table's order.
- **`Set:wrapInstance`**: Wraps the target's own copies of the named functions (each with its declared `make`), for as long as it is the target. A copy that is the class wrapper (or matches the class's current method while the class wrap reaches it) or is not the expected shape is left alone; one the game replaced since is wrapped again.
- **`GameWraps.newGroup`**: Multi-target wrap pool (optionally weak-keyed via `{ weakTargets = true }`) for core and HUD hooks that wrap dynamic classes and live instances without giving up: skips targets that already inherit an active wrapped method from a base prototype (`rawget` + `__index` chain), tracks `hadOwn` so inherited methods are cleared back to `nil` on `liftAll()`, checks `rawget(target, methodName) == entry.wrapper` before restoring so downstream third-party wraps stay intact, and marks `entry.active = false` on lift so any remaining outer wrap passes straight through.

### `mods/FS25_VRCompanion/src/VRCompanionLocalisedTexts.lua`

- **the file**: FS25 VR Companion Mod: localised texts

  The texts the player reads in the game's language: an l10n key's text (the game's own, or this mod's from modDesc.xml's \<l10n>), with today's English text where the game has none. Notifications and blinking warnings are made with these; console output and log lines stay English. Loads right after the core, whose getEngineGlobal it reads g_i18n through.

- **`VRCompanion.getLocalisedText`**: The text the player reads for an l10n key (the game's own, or this mod's from modDesc.xml) in the game's current language, else englishText: when the game has no text for the key, has no g_i18n, or asking it raises. A key is never returned as the text. englishText may be nil, for a caller that tries something else next.
- **`VRCompanion.formatLocalisedText`**: A notification's text: the text for an l10n key (see getLocalisedText) formatted with the values, else englishFormat formatted with them, also when the game's text does not take these values. Console output and log lines are not made with it: they stay English.

### `mods/FS25_VRCompanion/src/VRCompanionMenuDistance.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Menu Distance

  The console command `vrMenuDistance [1.0-5.0]` sets how far in front of the head, in metres, the menu quad is placed: the
  panel that shows the game's menus and 2D Floating Window Mode (1.5 m by default). The panel keeps its size, so further away
  it fills less of the view. The Companion Mod only carries the setting: it sends the metres to VRBridge, and the Injector
  places the quad that far away. A quad already showing moves at once; every menu opened after that is placed at the new
  distance.

  The setting is saved in settings.xml (1.5 by default), like FOV Overscan. When a savegame loads, Core's loadMap reads
  settings.xml and then runs the map-load hook this file registers (see the registry in VRCompanionCore.lua), which sends the
  value to VRBridge after the handshake. The main menu is shown
  before the Companion Mod runs, so the Injector reads the saved value from settings.xml itself when the game starts, and a
  map unload leaves VRBridge's value as it is: the main menu after leaving a savegame keeps the distance too.

  PUBLIC SURFACE
    VRCompanion.menuDistance                          metres, 1.0 to 5.0 (1.5 by default)
    VRCompanion:setMenuDistance(metres, save)         sets the field, pushes it to VRBridge and saves settings.xml when `save`;
                                                      false (and nothing changes) when `metres` is not a number from 1.0 to 5.0
    VRCompanion:applyMenuDistance()                   pushes the field to VRBridge (map load, after loadConfiguration and the handshake)
    VRCompanion:saveMenuDistanceSetting(xmlFile, xml) / loadMenuDistanceSetting(xmlFile, xml)   hooks of saveConfiguration / loadConfiguration
    VRCompanion:consoleCommandSetMenuDistance(arg)    nil reports the distance, a number from 1.0 to 5.0 sets and saves it,
                                                      anything else returns the usage
    Registered with the core: a map-load hook (applyMenuDistance) and the console command; the registry adds and removes the command
    Console: vrMenuDistance [1.0-5.0]
  ```

- **`registerSubsystem "menuDistance"`**: Map load: the saved setting goes to the Injector (the Injector read settings.xml at launch, and this sends what it holds now). The core has done the handshake and read settings.xml by then. Map unload: the registry removes the command; the saved setting stays as it is, and the next map load reads it again. VRBridge keeps its value, so the main menu stays at the distance. Order: after FOV Overscan and before Mouse Confinement (file order); none matters, each sets an Injector field of its own.
- **`registerSubsystem "menuDistance"`, `saveSettings`**: The key and its values stay as they are: the Injector reads vrSettings.menuDistance from this file at launch (ADR-0036)

### `mods/FS25_VRCompanion/src/VRCompanionMouseConfinement.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Mouse Confinement switch

  The console command `vrMouseConfinement [on|off]` turns Mouse Confinement off and on (spec #214). While the headset is in use
  and the game window has focus, the Injector keeps the Desk Mouse on the game picture so that a click the player cannot see
  never lands on another program. The Companion Mod only carries the setting: it sets the VRBridge Mouse Confinement flag and
  the Injector's DXGI proxy reads it at every present. The Companion Mod only runs once a savegame has loaded, so Mouse
  Confinement always holds in the main menu before then (and again after the player leaves to the main menu, which resets the
  VRBridge flag to on).

  The setting is saved in settings.xml (on by default). When a savegame loads, Core's loadMap reads settings.xml and then runs the
  map-load hook this file registers (see the registry in VRCompanionCore.lua), which sends the value to VRBridge after the handshake.

  PUBLIC SURFACE
    VRCompanion.mouseConfinement                          true (the default) or false
    VRCompanion:setMouseConfinement(enabled, save)        sets the field, pushes it to VRBridge, saves settings.xml when `save`
    VRCompanion:applyMouseConfinement()                   pushes the field to VRBridge (map load, after loadConfiguration)
    VRCompanion:saveMouseConfinementSetting(xmlFile, xml) / loadMouseConfinementSetting(xmlFile, xml)   hooks of saveConfiguration / loadConfiguration
    VRCompanion:consoleCommandSetMouseConfinement(arg)    nil reports the state, "on" / "off" sets and saves it, anything else returns the usage
    Registered with the core: a map-load hook (applyMouseConfinement) and the console command; the registry adds and removes the command
    Console: vrMouseConfinement [on|off]
  ```

- **`registerSubsystem "mouseConfinement"`**: Map load: the saved setting goes to the Injector (the core has done the handshake and read settings.xml by then). Map unload: the registry removes the command; the saved setting stays as it is, and the next map load reads it again (the Injector's VRBridge flag is reset by VRBridge.onMapUnload). Order: after Desktop Mirror, FOV Overscan and Menu Distance (file order); none matters, each sets an Injector field of its own.

### `mods/FS25_VRCompanion/src/VRCompanionPerfTrace.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: frame timing trace

  The console command `vrPerfTrace [seconds]` times the frame for a few seconds and logs it to fs25_vr.log (not log.txt): one
  line per second, then a summary. The Injector times its own work (its present, the eye copies, xrWaitFrame, xrEndFrame, the
  game's real Present) and the GPU work it records (the eye copies, the motion vector pass). The Companion Mod adds its hooks
  (core/perf_trace.h in the Injector lists every section):
    gameUpdate  the game's mission update, which runs the Companion Mod's update inside it
    modUpdate   VRCompanion:update
    modCamera   the vehicle and player camera hooks
    modDraw     VRCompanion:draw
  Off, a hook costs one field read: VRBridge is only called while `perfTracing` is set. The Injector ends the trace by itself,
  and the update clears `perfTracing` once VRBridge says it has.

  PUBLIC SURFACE
    VRCompanion.perfTracing                         true while a trace runs
    VRCompanion:perfTraceBegin(name) / perfTraceEnd(name, ...)   around a section; End returns its extra arguments, so it can
                                                    wrap a call's results
    VRCompanion:refreshPerfTracing()                 the update: notices the Injector ended the trace (the core's update calls it
                                                    first, while `perfTracing` is set)
    VRCompanion:consoleCommandPerfTrace(arg)         starts a trace of `arg` seconds (default 10, up to 60); "stop" ends it
    Registered with the core: a map-unload hook that ends a running trace, and the console command; the registry adds and
    removes the command
    Console: vrPerfTrace [seconds|stop]
  ```

- **`registerSubsystem "perfTrace"`**: No update hook: the Injector ends a trace by itself, and the core's update notices (refreshPerfTracing), as the very first step of the update, a direct call because no run point is that early: a camera hook that runs in the steps after it, and the update's own sections, must not time into a trace the Injector has ended. Map unload: the registry removes the command, then a running trace ends with its summary. Order: none matters against the other subsystems.

### `tests/test_companion_mod.cpp`

- **`CompanionModTest.PackagedModZipContainsModDescIconAndEveryModuleFile`, `EXPECT_NE(script.find("src/*.lua"), std::string::npos);`**: Packaging globs src/\*.lua; every modDesc-listed file must live directly in src/.
- **`CompanionModTest.EveryScriptThatZipsTheModShipsTheVirtualHandModelItsShapesAndItsAttribution`, `for (const char* scriptPath : { "scripts/deploy.ps1", "scripts/package_releas...`**: The Virtual Hand model is an asset, not a script: the zip must carry models/ or the Companion Mod starts without Virtual Hands in the player's game.
- **`CompanionModTest.ReleaseBundleAndModZipShipTheGplLicense`, `script`**: GPL-3.0 requires conveying a copy of the license with every distributed copy; the mod zip is also published on its own, so it carries LICENSE too.
- **`CompanionModTest.CompletesItsHandshakeWhateverVersionTheInjectorReports`, `bridge`**: The Companion Mod only prints the Injector's version for now; an Injector of another release than the mod's must not stop the handshake.
- **`CompanionModTest.RequiresAfrFallbackFromTheFirstFrameWithoutPollingTelemetry`, `ASSERT_EQ(RunLuauChunk(L, R"luau(`**: The fixed verdict (ADR-0017) holds before the first update, with no probe to wait for.
- **`CompanionModTest.InCabVehicleCameraFollowsHeadPoseWithCabReferenceBinding`, `ASSERT_EQ(RunLuauChunk(L, R"(`**: Clear and test outside camera: the engine camera sits at (10, 2, 30) looking along world +X (heading -pi/2) and is only read. The head pose is composited on a level base node at that position and heading, and rendered through the eye camera linked under it.

### `tests/test_companion_mod_globals.cpp`

- **the file**: The globals the Companion Mod creates are pinned (spec #280, ticket #288), so a missing `local` or a mistyped name that leaks a global fails a test. The shipped mod runs in a Luau state with the engine stubs: it is loaded in modDesc.xml order, a map is loaded, the mod updates on foot (holding a Hand Tool) and in a cab, and the map is unloaded. Every global created along the way is attributed to the function that created it, and only those created by the mod's own files count: the stubs and this fixture create many, which the game would have defined.
- **`kPinnedModGlobals`**: The globals the Companion Mod creates on purpose. Add one here only when the mod means to create it; a global that leaked by accident is fixed in the mod.
- **`kTrackGlobalCreation`**: Records, for every global created from now on, the source of the function that created it (debug.info(2, "s"), with the chunk name's "=" or "@" removed): \__newindex on \_G only runs for a name that is not there yet.
- **`CompareALeakedGlobal`**: Self-contained so that EXPECT_NONFATAL_FAILURE can run them.

### `tests/test_injector_version_warning.cpp`

- **`InjectorVersionWarningTest.AnInjectorOf1_0_0IsCalledAnOlderVersionNotQuoted`, `bridge`**: Every release up to 1.2 reported the fixed text 1.0.0, which says nothing about the release.

### `tests/test_menu_distance.cpp`

- **`CompanionSettingsTest.ReadsTheNumberOfAnElementAsTheEngineWritesIt`**: The Injector reads the saved Menu Distance from the Companion Mod's settings.xml when the game starts, so the main menu, shown before any savegame (and so before the Companion Mod) has loaded, is placed at it.

### `tests/test_per_update_allocations.luau`

- **the file**: Test Suite: the Companion Mod allocates nothing per update in the Motion Controller poll and in the GUI visibility poll (the Stereo Dialog check), so the garbage collector adds no frame-time spikes. Seen from outside: the memory the Luau state holds across many updates of the public functions (VRCompanion:updateMotionControllers, VRCompanion:pollGuiVisibility) against a stubbed VRBridge, g_gui and g_inputBinding. A table or closure made per update would add to the count.
- **`HAND_FIELDS`**: Both Motion Controllers tracked and moving: each read puts the hand somewhere new, filling the table the mod hands over in place the way the native binding does once the table holds every field.
- **`DOWNSTREAM_UPDATES`**: The poll hands the polled hands on to the subsystems that act on them (Wheel Grab, menus, ...). Their own updates are their own tickets' concern: stood in for by nothing, so that this test measures the poll itself.

### `tests/test_perf_trace.luau`

- **`countBegins`**: The game's mission classes form a chain: the Mission00 subclass either inherits FSBaseMission's update or calls up to it. The mod wraps both, so the inner wrapper must pass straight through under AFR.

### `tests/test_settings_subsystems.cpp`

- **the file**: The settings and diagnostics subsystems on the subsystem registry (spec #244, ticket #257): FOV Overscan, Menu Distance, Mouse Confinement, the performance trace, Seat Offset, the fish catch and the dialog diagnostics. Seen from outside: the real Companion Mod is loaded in modDesc order and driven as the game does (map load, map unload), and the test asserts what the game was given: which console commands exist after a map loads and after it unloads, and which saved values VRBridge was sent, in which order, after the handshake. Never the registry's contents. Prior art: test_subsystem_registry.cpp (Desktop Mirror through the registry), test_companion_mod.cpp EveryConsoleCommandIsRemovedOnUnloadSoASecondMapLoadsCleanly.

### `tests/test_subsystem_registry.cpp`

- **`SubsystemRegistryTest.AnUpdateRunsTheCoresStepsAndTheSubsystemsInTheOrderTheyRanBeforeTheRegistry`**: The order of a map's update: the registry moved the subsystems' updates into hooks, but the steps the core runs between them keep their places (spec #244: the order of effects the game and the Injector observe stays).
- **`SubsystemRegistryTest.EveryConsoleCommandTheModAddsExistsAfterMapLoadAndNoneRemainsAfterMapUnload`**: Every console command the mod adds, subsystem by subsystem and in the core: the whole set exists once a map has loaded and not one is left once it has unloaded, so a subsystem that forgets to remove (or to add) a command fails here. The names are written out on purpose: a command added or dropped is a decision, and this list is where it shows. Ticket #261.
- **`SubsystemRegistryLuauRunnerTest.ALoggedHookErrorFailsTheRunUnlessTheChunkExpectsIt`**: The standalone Luau runner has the same rule: a hook error in the log fails the run unless the chunk expects it.

### `tests/test_subsystem_settings.cpp`

- **`kEngineStubs`**: The engine's XML functions over a settings file kept in Lua (\_G.xmlFiles[path] = { key = value }), and the few engine functions a map load calls. A getter answers only for a value of its own type, as a key written by another setter would read back nothing useful from the engine either; getXMLString answers for a bool too ("true"/"false"), as the engine does.
- **`SubsystemSettingsTest.ASavedFileHoldsTheSameKeysAndValuesAsBeforeTheRegistry`**: Every Subsystem setting away from its default, saved: the file holds what it held before the registry saved the settings. The settings are set as the player sets them, with the console commands, after a map load. The Virtual Gamepad (whose command rewrites the game's bindings), its defaults-written and Recenter-migrated flags (no command) and a Seat Offset (whose command needs a vehicle to sit in) are set on VRCompanion directly; loading them from a file is the other test.
- **`SubsystemSettingsTest.AnOldFileLoadsToTheSameSettingsAndTheSameVRBridgeCallsAtMapLoad`**: A file written before the change, every Subsystem setting away from its default, loaded at map load: the mod makes the same calls on VRBridge, in the same order, as before the registry loaded the settings, and a save after the load writes every setting of the old file back with the value it had.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 101 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
