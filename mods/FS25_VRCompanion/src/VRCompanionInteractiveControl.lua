VRCompanion = VRCompanion or {}

local MOD_NAME = "FS25_interactiveControl"
local CLICK_POINT_FUNCTIONS = {
    "update", "updateClickable", "updateScreenPosition", "setClickable", "isClickable", "isExecutable", "setActivated",
}
local MANAGER_FUNCTIONS = { "getActiveInteractiveController", "onActionEventExecute" }
local STALE_MS = 500
local OFF_SCREEN = -1000

local function newState()
    return {
        installed = false,
        class = nil,
        originals = {},
        wrappers = {},
        points = setmetatable({}, { __mode = "k" }),  -- [clickPoint] = clockMs when last seen active
        refused = setmetatable({}, { __mode = "k" }), -- [clickPoint] = clockMs when Interactive Control last refused it a mouse test
        updates = 0,
        clockMs = 0,
        selected = nil,
        autoOn = nil,
        pullArmed = false,
        pullPoint = nil,      -- the Click Point a pull was aimed at that Interactive Control has not executed yet
        pullAtMs = 0,
        vehicleReport = nil,
        vehicleCheckMs = 0,
        vehicleLogs = 0,
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

local giveUpOnce = VRCompanion.GameWraps.newGiveUp("interactiveControl",
    "Interactive Control support disabled: %s. Motion Controllers cannot select Click Points; Interactive Control itself is unchanged.")

local function giveUp(reason)
    giveUpOnce:giveUp(reason)
end

local function isPointActive(point)
    local isActivated = point.isActivated
    if type(isActivated) == "function" then
        return isActivated(point) == true
    end
    return point.activated == true
end

local function rayControlsSelection(self)
    return state.installed and self:isPointerRayInControl() and self:getPointerRay() ~= nil
end

local function notify(self, kind, point)
    self:onClickPointEvent(kind, point)
end

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
    wrappers.updateClickable = function(point, mx, my, ...)
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

local VEHICLE_CHECK_MS = 1000
local VEHICLE_LOG_LIMIT = 20
local INDOOR_TYPES = { [1] = true, [3] = true }  -- InteractiveAction.ACTION_TYPES INDOOR and INDOOR_OUTDOOR

local function askVehicle(vehicle, name, ...)
    if type(vehicle[name]) ~= "function" then return "?" end
    local ok, value = pcall(vehicle[name], vehicle, ...)
    return ok and tostring(value) or "error"
end

local function describeVehicle(self)
    local vehicle = self:getControlledVehicle()
    local spec = vehicle ~= nil and vehicle.spec_interactiveControl or nil
    if type(spec) ~= "table" or type(spec.interactiveControllers) ~= "table" then return nil end
    local indoor, active = 0, 0
    for _, controller in ipairs(spec.interactiveControllers) do
        local actions = type(controller) == "table" and controller.interactiveActions or nil
        if type(actions) == "table" then
            for _, action in ipairs(actions) do
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

function VRCompanion:getInteractiveControlDiagnostics()
    if giveUpOnce:problem() ~= nil then return "Interactive Control support: gave up (see the warning in the log)" end
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

local function selectFromRay(self, ray)
    local getWorldTranslationFn = getWorldTranslation or (_G and _G.getWorldTranslation)
    local cosCone = math.cos(math.rad(self.pointerConeDegrees))
    local live, best, bestCos, bx, by, bz = 0, nil, cosCone, nil, nil, nil
    for point, seen in pairs(state.points) do
        if state.clockMs - seen > STALE_MS or not isPointActive(point) then
            state.points[point] = nil
        elseif state.refused[point] ~= nil and state.clockMs - state.refused[point] <= STALE_MS then
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

local function clickSelected(self, mayUseTrigger)
    local pressed, held = self:getPointerTrigger()
    if mayUseTrigger == false then pressed, held = false, false end
    if pressed then
        state.pullArmed = true
    elseif not held then
        state.pullArmed = false
    end
    local selected = state.selected
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
    local holding = autoOn ~= nil and self:isSteeringWheelHeld()
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

    local camera = activeCameraOf(vehicle)
    if camera ~= autoOn.camera then
        autoOn.camera = camera
        if vehicleState(vehicle) == false and stateSettingIsToggle() then
            autoOn.wrote = true
            writeState(vehicle, true)
        end
    end
end

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
        if giveUpOnce:problem() ~= nil or self.motionControllersEnabled == false or not anyHandTracked(self) then
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
        local mayUseTrigger = true
        local pointerHand = self:getPointerHand()
        if selected ~= nil and pointerHand ~= nil then
            mayUseTrigger = self:takeVirtualGamepadInput(pointerHand, "trigger")
        end
        clickSelected(self, mayUseTrigger)
    else
        state.pullArmed, state.pullPoint = false, nil
    end
    return ray ~= nil and live > 0, hx, hy, hz
end

function VRCompanion:resetInteractiveControlSupport()
    if state.installed and state.class ~= nil then
        for name, wrapper in pairs(state.wrappers) do
            if state.class[name] == wrapper then
                state.class[name] = state.originals[name]
            end
        end
    end
    giveUpOnce:reset()
    state = newState()
end

VRCompanion:registerSubsystem("interactiveControl", {
    mapUnload = function(self) self:resetInteractiveControlSupport() end,
})
