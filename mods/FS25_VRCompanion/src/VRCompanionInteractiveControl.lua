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
-- The Pointer Ray controls selection only while it has control of the pointer (whichever of the desk
-- mouse and the Pointer Hand moved last; VRCompanion:isPointerRayInControl). While the mouse has
-- control Interactive Control selects by its own mouse test exactly as without Motion Controllers, the
-- Pointer Ray is not drawn and the trigger clicks nothing; a trigger pulled in that time does not click
-- (or keep an analog Click Point acting) once the hand moves, only a new pull does.
--
-- Later tickets build on:
--   VRCompanion:onClickPointEvent(kind, clickPoint)   optional hook, called with "selected" (the
--       selected Click Point changed, clickPoint may be nil) and "clicked" (a click was executed)
--   rayControlsSelection()                            the one place that decides whether the Pointer
--       Ray, rather than the mouse, selects Click Points
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.InteractiveControl = VRCompanion.InteractiveControl or {}

local MOD_NAME = "FS25_interactiveControl"
local CLICK_POINT_FUNCTIONS = {
    "update", "updateClickable", "updateScreenPosition", "setClickable", "isClickable", "isExecutable", "setActivated",
}
local MANAGER_FUNCTIONS = { "getActiveInteractiveController", "onActionEventExecute" }
-- A Click Point that Interactive Control has not updated for this many Companion Mod updates is gone
-- (its vehicle left, or it was deleted) and no longer counts as active.
local STALE_UPDATES = 2
-- Where the mouse test is pointed for a Click Point the ray did not select: nowhere on screen.
local OFF_SCREEN = -1000

local function newState()
    return {
        installed = false,
        gaveUp = false,       -- the support was found unusable (warned once); do not look again
        class = nil,
        originals = {},
        wrappers = {},
        points = setmetatable({}, { __mode = "k" }),  -- [clickPoint] = update count when last seen active
        updates = 0,
        selected = nil,
        pullArmed = false,    -- the trigger went down while the ray had control and is still down
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
        state.points[point] = state.updates
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
        return  -- Interactive Control has not created its manager yet
    end
    missing = missingFunctions(manager, MANAGER_FUNCTIONS)
    if missing ~= nil then
        return giveUp("the manager has no " .. missing)
    end
    installWrappers(self, class)
end

local function anyHandTracked(self)
    return self:getHandState(self.MOTION_HAND_LEFT).tracked == true
        or self:getHandState(self.MOTION_HAND_RIGHT).tracked == true
end

-- Picks the Click Point nearest the centre of the ray within the cone; counts the live Click Points.
-- Returns the number of live Click Points and the selected one with its world position.
local function selectFromRay(self, ray)
    local getWorldTranslationFn = getWorldTranslation or (_G and _G.getWorldTranslation)
    local cosCone = math.cos(math.rad(self.pointerConeDegrees))
    local live, best, bestCos, bx, by, bz = 0, nil, cosCone, nil, nil, nil
    for point, seen in pairs(state.points) do
        if state.updates - seen > STALE_UPDATES or not isPointActive(point) then
            state.points[point] = nil
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
    return live, best, bx, by, bz
end

-- One trigger pull runs Interactive Control's own execute path once; an analog Click Point keeps
-- acting every update while the trigger is held.
local function clickSelected(self)
    local pressed, held = self:getPointerTrigger()
    -- Only a pull that went down while the ray had control can keep an analog Click Point acting; one
    -- that began under the mouse never turns into a click when the hand moves later.
    if pressed then
        state.pullArmed = true
    elseif not held then
        state.pullArmed = false
    end
    local selected = state.selected
    if selected == nil or not (pressed or held) then return end
    local manager = currentManager()
    if manager == nil then return end
    local controller = manager:getActiveInteractiveController()
    if controller == nil then return end
    if selected.interactiveController ~= nil and controller ~= selected.interactiveController then return end
    local analog = type(controller.isAnalog) == "function" and controller:isAnalog() == true
    if pressed or (analog and state.pullArmed) then
        pcall(manager.onActionEventExecute, manager)
        notify(self, "clicked", selected)
    end
end

-- Called by the Pointer Ray code once per Companion Mod update, after the ray is placed. Returns
-- whether there are active Click Points (something the ray could hit) and, when one is selected, its
-- world position (where the ray ends).
function VRCompanion:updateInteractiveControl(dt)
    if not state.installed then
        if state.gaveUp or self.motionControllersEnabled == false or not anyHandTracked(self) then
            return false
        end
        tryInstall(self)
        if not state.installed then return false end
    end
    state.updates = state.updates + 1

    local ray = nil
    if rayControlsSelection(self) then
        ray = self:getPointerRay()
    end
    local live, selected, hx, hy, hz = selectFromRay(self, ray)
    local previous = state.selected
    state.selected = selected
    if selected ~= previous then
        notify(self, "selected", selected)
    end
    if ray ~= nil then
        clickSelected(self)
    else
        state.pullArmed = false
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

VRCompanion.InteractiveControl.updateInteractiveControl = function(self, ...) return VRCompanion.updateInteractiveControl(VRCompanion, ...) end
VRCompanion.InteractiveControl.resetInteractiveControlSupport = function(self, ...) return VRCompanion.resetInteractiveControlSupport(VRCompanion, ...) end

return VRCompanion.InteractiveControl
