VRCompanion = VRCompanion or {}

local engine = VRCompanion.getEngineGlobal

local DEFAULT_REACH_BEHIND = 0.6
local BEHIND_MARGIN = 0.1

local WRAP_SPECS = {
    callback = {
        class = "AnimatedObject", fn = "triggerCallback", parameters = 6,
        describe = "(object, triggerId, otherId, onEnter, onLeave, onStay)",
        missingClass = "the game's AnimatedObject class was not found",
    },
    canBeTriggered = {
        class = "AnimatedObject", fn = "getCanBeTriggered", parameters = 1, describe = "(object)",
        missingClass = "the game's AnimatedObject class was not found",
    },
    toggle = {
        class = "AnimatedObjectActivatable", fn = "onAnimationInputToggle", parameters = 1, describe = "(activatable)",
        missingClass = "the game's AnimatedObjectActivatable class was not found",
    },
}

local wraps = VRCompanion.GameWraps.newSet({
    owner = "animatedObjects",
    giveUp = "Animated Objects support disabled: %s. Motion Controllers cannot work doors and gates; the game's prompts are unchanged.",
    nameAsSubject = true,
    order = { "callback", "canBeTriggered", "toggle" },
    functions = WRAP_SPECS,
})

local IGNORED_FLAGS = { "TRIGGER", "PLAYER", "TERRAIN", "TERRAIN_DELTA", "TERRAIN_DISPLACEMENT" }
local MAX_PARENTS = 64

local function newState()
    return {
        installed = false,
        problem = nil,
        zone = setmetatable({}, { __mode = "k" }),  -- [object] = true while the local player is in the object's zone
        parts = {},        -- scratch: [animation part node] = the object in the zone that can be worked now
        hit = {},
        selected = nil,
        landedOn = nil,    -- the object the ray is on and has pulsed for
    }
end

local state = newState()

local localPlayer = VRCompanion.getLocalPlayer

local function trackZone(object, otherId, onEnter, onLeave)
    local mission, career = engine("g_currentMission"), engine("FSCareerMissionInfo")
    if type(mission) ~= "table" or mission.missionInfo == nil or career == nil then return end
    if not mission.missionInfo:isa(career) then return end
    if not (onEnter or onLeave) then return end
    local player = localPlayer()
    if player == nil or otherId ~= player.rootNode then return end
    local count = state.zone[object] or 0
    if onEnter then count = count + 1 end
    if onLeave then count = count - 1 end
    state.zone[object] = count > 0 and count or nil
end

local function anyHandTracked(self)
    return self:getHandState(self.MOTION_HAND_LEFT).tracked == true
        or self:getHandState(self.MOTION_HAND_RIGHT).tracked == true
end

WRAP_SPECS.callback.make = function(original)
    return function(object, triggerId, otherId, onEnter, onLeave, ...)
        local a, b, c = original(object, triggerId, otherId, onEnter, onLeave, ...)
        if state.installed then pcall(trackZone, object, otherId, onEnter, onLeave) end
        return a, b, c
    end
end

local function install()
    wraps:wrapAll()
    state.installed = true
    print("[FS25_VR] Animated Objects support: installed")
end

function VRCompanion:loadAnimatedObjectsSupport()
    if state.installed or wraps:problem() ~= nil then return end
    state.problem = wraps:findProblem()
    if state.problem == nil then install() end
end

function VRCompanion:resetAnimatedObjectsSupport()
    wraps:reset()
    state = newState()
end

VRCompanion:registerSubsystem("animatedObjects", {
    mapLoad = function(self) self:loadAnimatedObjectsSupport() end,
    mapUnload = function(self) self:resetAnimatedObjectsSupport() end,
})

local function rayMayWork(self)
    if not state.installed or self.motionControllersEnabled == false then return false end
    if not self:isPointerRayInControl() or self:getPointerRay() == nil then return false end
    if self:doesMenuTakeInput() or self:getControlledVehicle() ~= nil then return false end
    local player = localPlayer()
    return type(player) == "table" and player.camera ~= nil and player.camera.isFirstPerson == true
end

local function isWorkable(object)
    local controls = object.controls
    if type(controls) ~= "table" or controls.posAction == nil or controls.negAction ~= nil then return false end
    local activatable = object.activatable
    if type(activatable) ~= "table" or type(activatable.onAnimationInputToggle) ~= "function" then return false end
    if type(object.getCanBeTriggered) ~= "function" then return false end
    local ok, can = pcall(object.getCanBeTriggered, object)
    return ok and can == true
end

local function collectParts()
    local parts = state.parts
    for node in pairs(parts) do parts[node] = nil end
    local count = 0
    for object in pairs(state.zone) do
        if object.isDeleted == true then
            state.zone[object] = nil
        elseif isWorkable(object) then
            local animation = object.animation
            local list = type(animation) == "table" and animation.parts or nil
            if type(list) == "table" then
                count = count + 1
                for _, part in ipairs(list) do
                    if part.node ~= nil then parts[part.node] = object end
                end
            end
        end
    end
    return count
end

local function castMask()
    local mask, flags = engine("CollisionMask"), engine("CollisionFlag")
    if type(mask) ~= "table" or type(flags) ~= "table" or type(mask.ALL) ~= "number" then return nil end
    local result = mask.ALL
    for _, name in ipairs(IGNORED_FLAGS) do
        if type(flags[name]) == "number" then
            result = bit32.band(result, bit32.bnot(flags[name]))
        end
    end
    return result
end

local function onHit(hit, node, x, y, z, distance)
    hit.node, hit.x, hit.y, hit.z, hit.distance = node, x, y, z, distance
    return true
end

local function findPartObject(node, getParent)
    for _ = 1, MAX_PARENTS do
        if node == nil or node == 0 then return nil end
        local object = state.parts[node]
        if object ~= nil then return object end
        local okParent, parent = pcall(getParent, node)
        if not okParent then return nil end
        node = parent
    end
    return nil
end

local function objectUnderTheRay(self, ray)
    local rayCast, getParent = engine("raycastClosest"), engine("getParent")
    if type(rayCast) ~= "function" or type(getParent) ~= "function" then return nil end
    local hit = state.hit
    local mask = castMask()
    local behind = tonumber(self.OBJECT_GRAB_REACH_BEHIND) or DEFAULT_REACH_BEHIND
    hit.node, hit.onHit = nil, onHit
    local ok = pcall(rayCast, ray.ox - ray.dx * behind, ray.oy - ray.dy * behind, ray.oz - ray.dz * behind, ray.dx, ray.dy, ray.dz,
        self.POINTER_RAY_LENGTH + behind, "onHit", hit, mask)
    if not ok or hit.node == nil then return nil end
    if hit.distance >= behind - BEHIND_MARGIN then
        local object = findPartObject(hit.node, getParent)
        if object ~= nil then return object, hit.x, hit.y, hit.z, hit.distance - behind end
    end
    if hit.distance < behind then
        hit.node, hit.onHit = nil, onHit
        local forwardOk = pcall(rayCast, ray.ox, ray.oy, ray.oz, ray.dx, ray.dy, ray.dz,
            self.POINTER_RAY_LENGTH, "onHit", hit, mask)
        if forwardOk and hit.node ~= nil then
            local forwardObject = findPartObject(hit.node, getParent)
            if forwardObject ~= nil then return forwardObject, hit.x, hit.y, hit.z, hit.distance end
        end
    end
    return nil
end

local function toggle(object)
    local activatable = object.activatable
    local target = activatable
    if object.controls.posActionEventId == nil then
        target = setmetatable({ updateActionEventTexts = function() end }, { __index = activatable })
    end
    return (pcall(activatable.onAnimationInputToggle, target))
end

local function feel(self, kind, object)
    self:onClickPointEvent(kind, object)
end

function VRCompanion:updateAnimatedObjects(dt, suppressed, rivalDistance)
    if not state.installed then
        if state.problem ~= nil and wraps:problem() == nil and self.motionControllersEnabled ~= false and anyHandTracked(self) then
            wraps:giveUp(state.problem)
        end
        return false
    end
    state.selected = nil
    if suppressed or not rayMayWork(self) then
        state.landedOn = nil
        return false
    end

    local selected, hx, hy, hz, distance = nil, nil, nil, nil, nil
    if collectParts() > 0 then
        selected, hx, hy, hz, distance = objectUnderTheRay(self, self:getPointerRay())
    end
    if selected ~= nil and rivalDistance ~= nil and rivalDistance < distance then selected = nil end
    state.selected = selected
    if selected ~= state.landedOn then
        state.landedOn = selected
        if selected ~= nil then feel(self, "selected", selected) end
    end
    if selected == nil then return false end

    local mayUseTrigger = true
    local pointerHand = self:getPointerHand()
    if pointerHand ~= nil then
        mayUseTrigger = self:takeVirtualGamepadInput(pointerHand, "trigger")
    end
    if mayUseTrigger then
        local pressed = self:getPointerTrigger()
        if pressed and toggle(selected) then feel(self, "clicked", selected) end
    end
    return true, hx, hy, hz
end

local function describeObject(object)
    if object.saveId ~= nil then return tostring(object.saveId) end
    local animation = object.animation
    local part = type(animation) == "table" and type(animation.parts) == "table" and animation.parts[1] or nil
    local node = type(part) == "table" and part.node or nil
    if node == nil then return "yes" end
    local getName = engine("getName")
    if type(getName) == "function" then
        local ok, name = pcall(getName, node)
        if ok and type(name) == "string" and name ~= "" then return name end
    end
    return "node " .. tostring(node)
end

function VRCompanion:getAnimatedObjectsDiagnostics()
    if wraps:problem() ~= nil then return "Animated Objects support: gave up (see the warning in the log)" end
    if not state.installed then
        return "Animated Objects support: not installed" .. (state.problem ~= nil and (" (" .. state.problem .. ")") or " yet")
    end
    local inZone = 0
    for object in pairs(state.zone) do
        if object.isDeleted ~= true then inZone = inZone + 1 end
    end
    local selected = state.selected
    return string.format("Animated Objects support: installed | in zone: %d | selected: %s", inZone,
        selected ~= nil and describeObject(selected) or "none")
end
