-- ====================================================================
-- FS25 VR Companion Mod: Interactive Control support (ADR-0026)
--
-- Lets the Pointer Ray select and click the Click Points of the Interactive Control mod
-- (FS25_interactiveControl). It does that by wrapping the Click Point class's own functions, because
-- Interactive Control has no hook for another pointer. The wrap is installed only when the mod is
-- loaded and its Click Point class and manager have every function this module relies on; otherwise
-- the Companion Mod logs ONE warning and leaves Interactive Control, and every other feature, alone.
--
-- What it relies on (release 1.2.0.1; pinned by tests/test_pointer_ray.cpp against a stand-in):
--   FS25_interactiveControl.InteractiveClickPoint   update, updateClickable, updateScreenPosition,
--       setClickable, isClickable, isExecutable, setActivated        (instances also have isActivated,
--       node, screenPosX / screenPosY, interactiveController)
--   g_currentMission.interactiveControl             getActiveInteractiveController, onActionEventExecute
--   the active controller                            isAnalog
--
-- While the Pointer Ray controls selection, Interactive Control's mouse test is replaced: exactly the
-- Click Point nearest the ray's centre within `vrPointerCone` degrees (angle at the hand, not
-- Interactive Control's screen-space test, which would jump between eyes under Alternate Frame
-- Rendering) is made clickable. Everything else of Interactive Control keeps running: its icons, hover
-- blink, action text and its own execute path, which one trigger pull runs once (and every frame while
-- the trigger is held on an analog Click Point).
--
-- Interactive Control updates the Click Points in a cab every frame, from the vehicle's draw, but those on a vehicle's
-- exterior only on the vehicle's update tick, every few frames; in between it does not look at them, and its execute
-- path runs the controller it took as clickable on that last update. So a Click Point stays active until Interactive
-- Control has not updated it for STALE_MS (counting updates instead made one outside the cab drop out and come back on
-- every tick), and a pull that comes before Interactive Control has taken the selected Click Point waits for it.
--
-- The Pointer Ray controls selection only while it has control of the pointer (whichever of the desk
-- mouse and the Pointer Hand moved last; VRCompanion:isPointerRayInControl). While the mouse has
-- control Interactive Control selects by its own mouse test exactly as without Motion Controllers, the
-- Pointer Ray is not drawn and the trigger clicks nothing; a trigger pulled in that time does not click
-- (or keep an analog Click Point acting) once the hand moves, only a new pull does.
--
-- A Click Point shares the cone with a Coupling Point of Manual Attach (VRCompanionManualAttach.lua, ADR-0035): the one nearer the ray's centre is the
-- target. peekInteractiveControlTarget() reports the best Click Point's angle (as a cosine) before it is committed, and
-- updateInteractiveControl(dt, selectNone) selects no Click Point for that update when the Coupling Point is nearer.
--
-- Other modules build on:
--   VRCompanion:onClickPointEvent(kind, clickPoint)   optional hook, called with "selected" (the
--       selected Click Point changed, clickPoint may be nil) and "clicked" (a trigger pull was executed:
--       once per pull, not on every frame an analog Click Point keeps acting)
--   rayControlsSelection()                            the one place that decides whether the Pointer
--       Ray, rather than the mouse, selects Click Points
-- ====================================================================

VRCompanion = VRCompanion or {}

local MOD_NAME = "FS25_interactiveControl"
local CLICK_POINT_FUNCTIONS = {
    "update", "updateClickable", "updateScreenPosition", "setClickable", "isClickable", "isExecutable", "setActivated",
}
local MANAGER_FUNCTIONS = { "getActiveInteractiveController", "onActionEventExecute" }
-- A Click Point that Interactive Control has not updated for this long (ms, summed over the Companion Mod's updates) is
-- gone (its vehicle left, or it was deleted) and no longer counts as active. It is longer than the vehicle's update tick,
-- on which Interactive Control updates the Click Points outside the cab. A pull waits at most as long for Interactive
-- Control to take the Click Point it was aimed at.
local STALE_MS = 500
-- Where the mouse test is pointed for a Click Point the ray did not select: nowhere on screen.
local OFF_SCREEN = -1000

local function newState()
    return {
        installed = false,
        gaveUp = false,       -- the support was found unusable (warned once); do not look again
        class = nil,
        originals = {},
        wrappers = {},
        points = setmetatable({}, { __mode = "k" }),  -- [clickPoint] = clockMs when last seen active
        refused = setmetatable({}, { __mode = "k" }), -- [clickPoint] = clockMs when Interactive Control last refused it a mouse test
        updates = 0,
        clockMs = 0,          -- the dt of every update, summed
        selected = nil,
        autoOn = nil,         -- see "Interactive Control switches on while the Pointer Hand is in use"
        pullArmed = false,    -- the trigger went down while the ray had control and is still down
        pullPoint = nil,      -- the Click Point a pull was aimed at that Interactive Control has not executed yet
        pullAtMs = 0,         -- clockMs of that pull
        vehicleReport = nil,  -- what was last logged about the vehicle being driven (see reportVehicle)
        vehicleCheckMs = 0,   -- time until reportVehicle looks again
        vehicleLogs = 0,      -- lines reportVehicle logged this map
    }
end

local state = newState()

local function currentManager()
    local mission = g_currentMission or (_G and _G.g_currentMission)
    return mission ~= nil and mission.interactiveControl or nil
end

local function missingFunctions(owner, names)
    local missing = nil
    for _, name in ipairs(names) do
        if type(owner[name]) ~= "function" then
            missing = (missing and (missing .. ", ") or "") .. name
        end
    end
    return missing
end

local function giveUp(reason)
    state.gaveUp = true
    print("[FS25_VR] [Warning] Interactive Control support disabled: " .. reason
        .. ". Motion Controllers cannot select Click Points; Interactive Control itself is unchanged.")
end

local function isPointActive(point)
    local isActivated = point.isActivated
    if type(isActivated) == "function" then
        return isActivated(point) == true
    end
    return point.activated == true
end

-- Who selects Click Points: the Pointer Ray while it has control of the pointer (the Pointer Hand moved
-- after the desk mouse did, see VRCompanion:isPointerRayInControl) and could be placed in the world;
-- otherwise Interactive Control's own mouse selection, untouched.
local function rayControlsSelection(self)
    return state.installed and self:isPointerRayInControl() and self:getPointerRay() ~= nil
end

local function notify(self, kind, point)
    if type(self.onClickPointEvent) == "function" then
        self:onClickPointEvent(kind, point)
    end
end

-- Remembers a Click Point Interactive Control just updated while it is active; forgets it otherwise.
local function trackPoint(point)
    if isPointActive(point) then
        state.points[point] = state.clockMs
    else
        state.points[point] = nil
    end
end

local function installWrappers(self, class)
    local originals = state.originals
    for _, name in ipairs(CLICK_POINT_FUNCTIONS) do
        originals[name] = class[name]
    end
    local wrappers = state.wrappers

    wrappers.update = function(point, ...)
        local a, b, c = originals.update(point, ...)
        if state.installed then trackPoint(point) end
        return a, b, c
    end
    wrappers.setActivated = function(point, ...)
        local a, b, c = originals.setActivated(point, ...)
        if state.installed then trackPoint(point) end
        return a, b, c
    end
    -- The mouse test. While the Pointer Ray controls selection the selected Click Point is shown to
    -- the original as if the mouse were on it (so its hover blink and icon logic run) and every other
    -- Click Point as if the mouse were nowhere; the ray's choice is then final.
    wrappers.updateClickable = function(point, mx, my, ...)
        -- Interactive Control asks for a test with no mouse position when it will not let this Click Point
        -- be pointed at (a static outdoor one facing away from the camera): it is then not clickable,
        -- whatever the ray says, and selectFromRay leaves it out. Everything else that decides whether
        -- a Click Point can be executed (its folding / animation / forced-state conditions and the
        -- indoor or outdoor mode) is isActivated, which selection already requires.
        if mx == nil or my == nil then
            state.refused[point] = state.clockMs
            return originals.updateClickable(point, mx, my, ...)
        end
        state.refused[point] = nil
        if not rayControlsSelection(self) then
            return originals.updateClickable(point, mx, my, ...)
        end
        local isSelected = (state.selected == point)
        local testX, testY = OFF_SCREEN, OFF_SCREEN
        if isSelected then
            testX, testY = point.screenPosX or mx, point.screenPosY or my
        end
        originals.updateClickable(point, testX, testY, ...)
        point:setClickable(isSelected)
    end

    class.update = wrappers.update
    class.setActivated = wrappers.setActivated
    class.updateClickable = wrappers.updateClickable
    state.class = class
    state.installed = true
end

-- Checks the installed Interactive Control and wraps its Click Points. Gives up with one warning when
-- the mod is missing or no longer has what the wrap expects; waits when its manager is not there yet.
local function tryInstall(self)
    local loaded = g_modIsLoaded or (_G and _G.g_modIsLoaded)
    if type(loaded) ~= "table" or loaded[MOD_NAME] ~= true then
        return giveUp("the mod " .. MOD_NAME .. " is not loaded")
    end
    local env = _G and _G[MOD_NAME]
    local class = type(env) == "table" and env.InteractiveClickPoint or nil
    if type(class) ~= "table" then
        return giveUp("its Click Point class was not found")
    end
    local missing = missingFunctions(class, CLICK_POINT_FUNCTIONS)
    if missing ~= nil then
        return giveUp("the Click Point class has no " .. missing)
    end
    local manager = currentManager()
    if manager == nil then
        -- Interactive Control has not created its manager yet; say so once so a stuck wait shows in the log.
        if not state.waitingLogged then
            state.waitingLogged = true
            print("[FS25_VR] Interactive Control support: waiting for g_currentMission.interactiveControl")
        end
        return
    end
    missing = missingFunctions(manager, MANAGER_FUNCTIONS)
    if missing ~= nil then
        return giveUp("the manager has no " .. missing)
    end
    installWrappers(self, class)
    print("[FS25_VR] Interactive Control support: installed")
end

-- ---- Cab Click Points while the VR HUD is hidden ------------------------------------------------
-- Interactive Control updates a cab's Click Points from the vehicle's draw, and the game draws the vehicle being driven as
-- part of the HUD. With the VR HUD hidden (its default, VRCompanionHud.lua) that draw never runs, so no indoor Click Point
-- became active and the ray had nothing to hit in a cab; outdoor ones are updated on the vehicle's tick and kept working.
-- While the HUD is hidden the Companion Mod runs Interactive Control's indoor update itself, once per update, as Interactive
-- Control does from its tick in the game's no-HUD mode: the call its onDraw makes, under the same conditions (its state on
-- and its indoor check), without the hand-tool crosshair the draw would also render. Not while a menu takes the input.
local CAB_UPDATE_FUNCTIONS = { "updateInteractiveController", "isInteractiveControlActivated", "isIndoorActive", "getIsActiveForInput" }

local function updateCabClickPointsWithoutDraw(self)
    if self:getHudState() ~= self.HUD_STATE_HIDDEN or self:doesMenuTakeInput() then return end
    local vehicle = self:getControlledVehicle()
    if vehicle == nil or type(vehicle.spec_interactiveControl) ~= "table" then return end
    for _, name in ipairs(CAB_UPDATE_FUNCTIONS) do
        if type(vehicle[name]) ~= "function" then return end
    end
    if vehicle:isInteractiveControlActivated() ~= true or vehicle:isIndoorActive() ~= true then return end
    pcall(vehicle.updateInteractiveController, vehicle, true, false, vehicle:getIsActiveForInput(true))
end

-- ---- What Interactive Control reports for the vehicle being driven ------------------------------
-- Interactive Control makes a cab's (indoor) Click Points active only while its state is on, its indoor check passes (an
-- inside camera, or indoor sound), the vehicle is active for input (entered, no AI helper driving it), and the vehicle's
-- draw runs, which updates them every frame. When the ray finds nothing in a cab, this says which of those failed: it is
-- logged when it changes (looked at once a second while driving, at most VEHICLE_LOG_LIMIT lines a map) and shown by
-- vrPointerDiagnostics.
local VEHICLE_CHECK_MS = 1000
local VEHICLE_LOG_LIMIT = 20
local INDOOR_TYPES = { [1] = true, [3] = true }  -- InteractiveAction.ACTION_TYPES INDOOR and INDOOR_OUTDOOR

local function askVehicle(vehicle, name, ...)
    if type(vehicle[name]) ~= "function" then return "?" end
    local ok, value = pcall(vehicle[name], vehicle, ...)
    return ok and tostring(value) or "error"
end

-- One phrase for the vehicle being driven, or nil when it has no Interactive Control.
local function describeVehicle(self)
    local vehicle = self:getControlledVehicle()
    local spec = vehicle ~= nil and vehicle.spec_interactiveControl or nil
    if type(spec) ~= "table" or type(spec.interactiveControllers) ~= "table" then return nil end
    local indoor, active = 0, 0
    for _, controller in ipairs(spec.interactiveControllers) do
        local actions = type(controller) == "table" and controller.interactiveActions or nil
        if type(actions) == "table" then
            for _, action in ipairs(actions) do
                -- Click Points are the actions with a mouse test; buttons have none.
                if type(action) == "table" and action.updateClickable ~= nil and INDOOR_TYPES[action.type] then
                    indoor = indoor + 1
                    if isPointActive(action) then active = active + 1 end
                end
            end
        end
    end
    local updated = 0
    for point, seen in pairs(state.points) do
        local controller = point.interactiveController
        if type(controller) == "table" and controller.target == vehicle and state.clockMs - seen <= STALE_MS then
            updated = updated + 1
        end
    end
    return string.format("state on: %s, indoor check: %s, active for input: %s, AI driving: %s | indoor Click Points: %d (active %d, updated lately %d)",
        askVehicle(vehicle, "isInteractiveControlActivated"), askVehicle(vehicle, "isIndoorActive"),
        askVehicle(vehicle, "getIsActiveForInput", true), askVehicle(vehicle, "getIsAIActive"), indoor, active, updated)
end

local function reportVehicle(self, dt)
    state.vehicleCheckMs = (state.vehicleCheckMs or 0) - (type(dt) == "number" and dt or 0)
    if state.vehicleCheckMs > 0 then return end
    state.vehicleCheckMs = VEHICLE_CHECK_MS
    local report = describeVehicle(self)
    if report ~= nil and report ~= state.vehicleReport and state.vehicleLogs < VEHICLE_LOG_LIMIT then
        state.vehicleLogs = state.vehicleLogs + 1
        print("[FS25_VR] Interactive Control in the driven vehicle: " .. report)
    end
    state.vehicleReport = report
end

-- One line for vrPointerDiagnostics: whether the wrap is installed and what it currently sees.
function VRCompanion:getInteractiveControlDiagnostics()
    if state.gaveUp then return "Interactive Control support: gave up (see the warning in the log)" end
    if not state.installed then
        return "Interactive Control support: not installed yet (" .. (state.waitingLogged and "no manager"
            or "waiting for a tracked Motion Controller") .. ")"
    end
    local live, tracked = 0, 0
    for point in pairs(state.points) do
        tracked = tracked + 1
        if isPointActive(point) then live = live + 1 end
    end
    return string.format("Interactive Control support: installed | Click Points seen: %d (active %d) | selected: %s | updates: %d | driven vehicle: %s",
        tracked, live, state.selected ~= nil and "yes" or "none", state.updates,
        describeVehicle(self) or "none with Interactive Control")
end

local function anyHandTracked(self)
    return self:getHandState(self.MOTION_HAND_LEFT).tracked == true
        or self:getHandState(self.MOTION_HAND_RIGHT).tracked == true
end

-- Picks the Click Point nearest the centre of the ray within the cone; counts the live Click Points.
-- Returns the number of live Click Points and the selected one with its world position and the cosine of its angle from the ray's centre.
local function selectFromRay(self, ray)
    local getWorldTranslationFn = getWorldTranslation or (_G and _G.getWorldTranslation)
    local cosCone = math.cos(math.rad(self.pointerConeDegrees))
    local live, best, bestCos, bx, by, bz = 0, nil, cosCone, nil, nil, nil
    for point, seen in pairs(state.points) do
        if state.clockMs - seen > STALE_MS or not isPointActive(point) then
            state.points[point] = nil
        elseif state.refused[point] ~= nil and state.clockMs - state.refused[point] <= STALE_MS then
            -- Interactive Control will not let the pointer have this Click Point right now.
        else
            live = live + 1
            if ray ~= nil and point.node ~= nil and type(getWorldTranslationFn) == "function" then
                local px, py, pz = getWorldTranslationFn(point.node)
                local vx, vy, vz = px - ray.ox, py - ray.oy, pz - ray.oz
                local distance = math.sqrt(vx * vx + vy * vy + vz * vz)
                if distance > 0.01 then
                    local cos = (vx * ray.dx + vy * ray.dy + vz * ray.dz) / distance
                    if cos >= bestCos then
                        best, bestCos, bx, by, bz = point, cos, px, py, pz
                    end
                end
            end
        end
    end
    return live, best, bx, by, bz, bestCos
end

-- One trigger pull runs Interactive Control's own execute path once; an analog Click Point keeps
-- acting every update while the trigger is held. Only the pull itself is a click event (the haptic pulse).
local function clickSelected(self, mayUseTrigger)
    local pressed, held = self:getPointerTrigger()
    -- A trigger already held for a Virtual Gamepad binding when the ray landed on the Click Point stays that binding's until
    -- it is let go (VRCompanionVirtualGamepad.lua): it clicks nothing.
    if mayUseTrigger == false then pressed, held = false, false end
    -- Only a pull that went down while the ray had control can keep an analog Click Point acting; one
    -- that began under the mouse never turns into a click when the hand moves later.
    if pressed then
        state.pullArmed = true
    elseif not held then
        state.pullArmed = false
    end
    local selected = state.selected
    -- Interactive Control executes the controller it took as clickable on its last update of the Click Point (outside the
    -- cab, the vehicle's last tick). A pull it has not taken yet waits for that, even once the trigger is let go, but only
    -- while the ray stays on the Click Point it was aimed at, and at most STALE_MS.
    if pressed and selected ~= nil then
        state.pullPoint, state.pullAtMs = selected, state.clockMs
    elseif state.pullPoint ~= nil and (state.pullPoint ~= selected or state.clockMs - state.pullAtMs > STALE_MS) then
        state.pullPoint = nil
    end
    local pulled = selected ~= nil and state.pullPoint == selected
    if selected == nil or not (pulled or held) then return end
    local manager = currentManager()
    if manager == nil then return end
    local controller = manager:getActiveInteractiveController()
    if controller == nil then return end
    if selected.interactiveController ~= nil and controller ~= selected.interactiveController then return end
    local analog = type(controller.isAnalog) == "function" and controller:isAnalog() == true
    if pulled or (analog and state.pullArmed) then
        pcall(manager.onActionEventExecute, manager)
        if pulled then
            state.pullPoint = nil
            notify(self, "clicked", selected)
        end
    end
end

-- ---- Interactive Control switches on while the Pointer Hand is in use in a cab ---------------------
-- Picking up the Pointer Hand in a vehicle switches Interactive Control on for that vehicle, so the
-- player need not press its toggle key first; putting the Pointer Hand down, or leaving the vehicle,
-- puts that vehicle's state back as it was. Interactive Control's synced state is written ONLY at those
-- transitions (and once more when the vehicle's camera changes, because Interactive Control switches
-- itself off then), never per frame, so multiplayer sees no extra traffic. When Interactive Control's
-- own setting says always on or always off, that setting wins and nothing is written.
--
-- Also relied on (release 1.2.0.1): g_currentMission.interactiveControl.settings:getSetting("IC_STATE")
-- with FS25_interactiveControl.InteractiveControlManager.SETTING_STATE_TOGGLE; on the vehicle
-- spec_interactiveControl (state, interactiveControllers) and activateInteractiveControl(state,
-- noEventSend). When they are not there the Pointer Hand simply does not switch Interactive Control.

-- Whether Interactive Control is in its toggle mode, the only mode in which its state is ours to write.
local function stateSettingIsToggle()
    local manager = currentManager()
    local settings = manager ~= nil and manager.settings or nil
    local env = _G and _G[MOD_NAME]
    local managerClass = type(env) == "table" and env.InteractiveControlManager or nil
    if type(settings) ~= "table" or type(settings.getSetting) ~= "function"
        or type(managerClass) ~= "table" or managerClass.SETTING_STATE_TOGGLE == nil then
        return false
    end
    local ok, value = pcall(settings.getSetting, settings, "IC_STATE")
    return ok and value == managerClass.SETTING_STATE_TOGGLE
end

-- The vehicle's synced Interactive Control state, or nil when it has none to write.
local function vehicleState(vehicle)
    local spec = vehicle ~= nil and vehicle.spec_interactiveControl or nil
    if type(spec) ~= "table" or type(vehicle.activateInteractiveControl) ~= "function" then return nil end
    if type(spec.interactiveControllers) == "table" and #spec.interactiveControllers == 0 then return nil end
    return spec.state == true
end

local function activeCameraOf(vehicle)
    if type(vehicle.getActiveCamera) == "function" then
        local ok, cam = pcall(vehicle.getActiveCamera, vehicle)
        if ok then return cam end
    end
    return nil
end

-- state.autoOn = nil | { vehicle, previous, wrote, camera }: the vehicle Interactive Control was
-- switched on for while the Pointer Hand is in use.
local function writeState(vehicle, value)
    pcall(vehicle.activateInteractiveControl, vehicle, value)
end

local function endAutoOn(autoOn)
    state.autoOn = nil
    if autoOn.wrote and vehicleState(autoOn.vehicle) ~= autoOn.previous and stateSettingIsToggle() then
        writeState(autoOn.vehicle, autoOn.previous)
    end
end

local function updateAutoOn(self)
    local autoOn = state.autoOn
    local vehicle = nil
    -- A hand holding the Steering Wheel is not the Pointer Hand, but grabbing it does not touch Interactive Control's
    -- state either: what was switched on stays on while a hand holds (Wheel Grab never starts it).
    local holding = autoOn ~= nil and type(self.isSteeringWheelHeld) == "function" and self:isSteeringWheelHeld()
    if (self:getPointerHand() ~= nil or holding) and self.motionControllersEnabled ~= false then
        vehicle = self:getControlledVehicle()
    end
    if autoOn ~= nil and autoOn.vehicle ~= vehicle then
        endAutoOn(autoOn)
        autoOn = nil
    end
    if vehicle == nil then return end

    if autoOn == nil then
        local previous = vehicleState(vehicle)
        if previous == nil then return end
        autoOn = { vehicle = vehicle, previous = previous, wrote = false, camera = activeCameraOf(vehicle) }
        state.autoOn = autoOn
        if not previous and stateSettingIsToggle() then
            autoOn.wrote = true
            writeState(vehicle, true)
        end
        return
    end

    -- Interactive Control switched itself off when the camera changed: switch it on again, once.
    local camera = activeCameraOf(vehicle)
    if camera ~= autoOn.camera then
        autoOn.camera = camera
        if vehicleState(vehicle) == false and stateSettingIsToggle() then
            autoOn.wrote = true
            writeState(vehicle, true)
        end
    end
end

-- The Click Point the ray would select now, before updateInteractiveControl commits: the cosine of its angle from the ray's centre
-- (larger is nearer the centre), or nil when none is within the cone. The Pointer Ray code ranks it against a Coupling Point
-- (VRCompanionManualAttach.lua) and then tells updateInteractiveControl whether to select none.
function VRCompanion:peekInteractiveControlTarget()
    if not state.installed or not rayControlsSelection(self) then return nil end
    if self:doesMenuTakeInput() and self:isStereoDialogShowing() then return nil end
    local _, best, _, _, _, cos = selectFromRay(self, self:getPointerRay())
    return best ~= nil and cos or nil
end

-- Called by the Pointer Ray code once per Companion Mod update, after the ray is placed. Returns
-- whether there are active Click Points (something the ray could hit) and, when one is selected, its
-- world position (where the ray ends). selectNone: a Coupling Point is nearer the ray's centre, so no
-- Click Point is selected for this update (none is clickable, and Interactive Control's hover state follows).
function VRCompanion:updateInteractiveControl(dt, selectNone)
    if not state.installed then
        if state.gaveUp or self.motionControllersEnabled == false or not anyHandTracked(self) then
            return false
        end
        tryInstall(self)
        if not state.installed then return false end
    end
    state.updates = state.updates + 1
    if type(dt) == "number" and dt > 0 then state.clockMs = state.clockMs + dt end
    updateAutoOn(self)
    updateCabClickPointsWithoutDraw(self)
    reportVehicle(self, dt)

    -- A Stereo Dialog takes the input and is answered with the pad's buttons: the Pointer Ray hits no Click Point behind it and
    -- the trigger clicks nothing, so a press for the dialog is not also a click.
    local ray = nil
    if rayControlsSelection(self) and not (self:doesMenuTakeInput() and self:isStereoDialogShowing()) then
        ray = self:getPointerRay()
    end
    local live, selected, hx, hy, hz = selectFromRay(self, ray)
    if selectNone then selected, hx, hy, hz = nil, nil, nil, nil end
    local previous = state.selected
    state.selected = selected
    if selected ~= previous then
        notify(self, "selected", selected)
    end
    if ray ~= nil then
        -- While the ray is on a Click Point the Pointer Hand's trigger is Interactive Control's: it reads released on the
        -- Virtual Gamepad, unless it was already held when the ray landed.
        local mayUseTrigger = true
        local pointerHand = self:getPointerHand()
        if selected ~= nil and pointerHand ~= nil and type(self.takeVirtualGamepadInput) == "function" then
            mayUseTrigger = self:takeVirtualGamepadInput(pointerHand, "trigger")
        end
        clickSelected(self, mayUseTrigger)
    else
        state.pullArmed, state.pullPoint = false, nil
    end
    -- With the mouse in control there is nothing for the ray to hit: it is not drawn.
    return ray ~= nil and live > 0, hx, hy, hz
end

-- Map unload (and the end of a session): hands Interactive Control back exactly as found.
function VRCompanion:resetInteractiveControlSupport()
    if state.installed and state.class ~= nil then
        for name, wrapper in pairs(state.wrappers) do
            if state.class[name] == wrapper then
                state.class[name] = state.originals[name]
            end
        end
    end
    state = newState()
end
