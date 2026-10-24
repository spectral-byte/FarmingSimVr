-- ====================================================================
-- FS25 VR Companion Mod: Animated Objects (ADR-0034, spec #180)
--
-- Lets the Pointer Ray work the Animated Objects of the game (CONTEXT.md: a building's door or gate, a barrier placed on the map)
-- on foot: standing in the zone around one, the player points the Pointer Ray at one of its moving parts (or touches it up close) and
-- pulls the trigger, and it does what its prompt does: it opens, closes, or turns back while it moves. Standing in the zones of
-- several, the player works the one the ray is on. The game's prompt and its bound button keep working as before.
--
-- How it works. The game puts an Animated Object's activatable in its system while the local player is in the object's zone, from the
-- trigger callback the engine calls by name. The module wraps that callback at the class level, ORIGINAL FIRST and with the same
-- conditions the original applies, to know which objects the player is in the zone of. Each update it casts the Pointer Ray against the
-- world's collision (the engine's raycastClosest, up to the ray's length) and walks up from the hit node to one of the animation parts
-- of an object in the zone: that object is the selected one, and the ray ends where it hit. A pull of the trigger runs the same toggle
-- the prompt's button runs (the activatable's toggle handler), guarded so that an error never reaches the game.
-- The Pointer Hand feels it as a Click Point (VRCompanion:onClickPointEvent): a light pulse as the ray lands on an object it was not on
-- (not when it moves to another part of the same one), a firmer one for each pull that toggled. Never the other hand.
--
-- What it relies on (release of the game this was written against: the SDK's AnimatedObject.lua, AnimatedObjectActivatable.lua; pinned by
-- tests/test_animated_objects_wraps.cpp against stand-ins, with the game's functions missing or reshaped):
--   AnimatedObject.triggerCallback(self, triggerId, otherId, onEnter, onLeave, onStay)   wrapped
--   AnimatedObject.getCanBeTriggered(self)       the game's "can be triggered" check, asked of the object itself (an own copy on a
--       placeable's object, which also asks the farm, answers the question the prompt asks)
--   AnimatedObjectActivatable.onAnimationInputToggle(self)   the toggle handler of a single press action; never wrapped
--   g_currentMission.missionInfo:isa(FSCareerMissionInfo), g_localPlayer.rootNode, raycastClosest, getParent
--   on an object: controls.posAction / controls.negAction (a single press action has the first and not the second), animation.parts[i].node,
--       activatable, isDeleted
-- When the game does not have the functions the wrap needs, or they have another shape, the Companion Mod logs ONE warning when
-- Motion Controllers are in use and leaves the game's Animated Objects, and every other feature, alone.
--
-- What is selected. An object in the zone whose "can be triggered" check answers true and whose action is a single press (a hold-to-move
-- one, with a negative action or the same action for both directions, is never selected: it needs the button held). The cast starts a
-- little behind the fingertip, so a finger that has gone into the leaf still sees it, and only a hit within the Pointer Ray's length
-- counts. The ray is not drawn on an Animated Object nor is the trigger taken while the desk mouse has control of the pointer, in a menu
-- or a Stereo Dialog, in a vehicle, off the first person view, or with Motion Controllers off.
--
-- PUBLIC SURFACE
--   VRCompanion:updateAnimatedObjects(dt, suppressed, rivalDistance) -> hasTarget, hitX, hitY, hitZ   once per update after the Pointer
--       Ray is placed (VRCompanion:updatePointerRay); suppressed: another target (a Click Point) has the ray, so nothing is selected;
--       rivalDistance: how far along the ray an object Object Grab can pick up is (nil: none), and the nearer of the two is the ray's
--       target (ticket #183).
--       While the ray is on an Animated Object the Pointer Hand's trigger is taken off the Virtual Gamepad
--       (VRCompanion:takeVirtualGamepadInput(hand, "trigger")).
--   VRCompanion:getAnimatedObjectsDiagnostics() -> string   one line for vrPointerDiagnostics
--   VRCompanion:loadAnimatedObjectsSupport() / resetAnimatedObjectsSupport()   map load / unload (hands the class back as found)
--
-- Hooks into the rest of the mod
--   MotionControllers   registerMotionControllerConsoleCommands -> loadAnimatedObjectsSupport() at map load; updatePointerRay ->
--       updateAnimatedObjects(dt, ...) after Interactive Control, before Object Grab; settleInteractiveControl (the paths where Motion
--       Controller input is ignored) -> updateAnimatedObjects(dt, true) so the selection clears; getPointerDiagnostics;
--       resetMotionControllers -> resetAnimatedObjectsSupport()
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.AnimatedObjects = VRCompanion.AnimatedObjects or {}

local engine = VRCompanion.getEngineGlobal

-- The cast starts as far (m) behind the fingertip as the game's hands' look ray does: Object Grab's VRCompanion.OBJECT_GRAB_REACH_BEHIND,
-- the one place it is tuned (the nearer-wins comparison with an object Object Grab can pick up measures both from the same point). The
-- fallback is for a mod that loads without Object Grab. A hit nearer than that minus the margin is behind the finger and does not count.
local DEFAULT_REACH_BEHIND = 0.6
local BEHIND_MARGIN = 0.1

-- The game's names the module relies on: class, function, how many parameters the function must declare at least (a reshaped
-- function declares fewer, self counts) and what they are, for the warning.
local TRIGGER_CALLBACK = {
    class = "AnimatedObject", fn = "triggerCallback", parameters = 6,
    describe = "(object, triggerId, otherId, onEnter, onLeave, onStay)",
}
local CAN_BE_TRIGGERED = { class = "AnimatedObject", fn = "getCanBeTriggered", parameters = 1, describe = "(object)" }
local TOGGLE_HANDLER = { class = "AnimatedObjectActivatable", fn = "onAnimationInputToggle", parameters = 1, describe = "(activatable)" }

-- The collision flags the ray cast leaves out: the zone is made of trigger shapes, the player's own capsule is around the head, and the
-- terrain is never a door.
local IGNORED_FLAGS = { "TRIGGER", "PLAYER", "TERRAIN", "TERRAIN_DELTA", "TERRAIN_DISPLACEMENT" }
local MAX_PARENTS = 64

local function newState()
    return {
        installed = false,
        gaveUp = false,    -- the support was found unusable (warned once); do not look again
        problem = nil,     -- why the support cannot be installed, found at map load (warned once, when Motion Controllers are in use)
        class = nil,
        original = nil,
        wrapper = nil,
        zone = setmetatable({}, { __mode = "k" }),  -- [object] = true while the local player is in the object's zone
        parts = {},        -- scratch: [animation part node] = the object in the zone that can be worked now
        hit = {},          -- scratch: what the ray cast hit
        selected = nil,
        landedOn = nil,    -- the object the ray is on and has pulsed for
    }
end

local state = newState()

-- ---- Checking what the game has -------------------------------------------------------------------------------------------

-- How many parameters a Lua function declares, or nil when it takes any number or the runtime cannot tell.
local function parameterCount(fn)
    local library = _G and _G.debug
    if type(library) ~= "table" then return nil end
    if type(library.info) == "function" then
        local ok, count, isVararg = pcall(library.info, fn, "a")
        if ok and type(count) == "number" then return (not isVararg) and count or nil end
    end
    if type(library.getinfo) == "function" then
        local ok, info = pcall(library.getinfo, fn, "u")
        if ok and type(info) == "table" and type(info.nparams) == "number" then
            return (not info.isvararg) and info.nparams or nil
        end
    end
    return nil
end

-- Why a function the module relies on is not what it expects, or nil when it is.
local function shapeProblem(spec)
    local class = engine(spec.class)
    if type(class) ~= "table" then return "the game's " .. spec.class .. " class was not found" end
    local fn = class[spec.fn]
    local name = spec.class .. "." .. spec.fn
    if fn == nil then return name .. " is missing" end
    if type(fn) ~= "function" then return name .. " is not a function" end
    local count = parameterCount(fn)
    if count ~= nil and count < spec.parameters then
        return string.format("%s takes %d parameters, not %s", name, count, spec.describe)
    end
    return nil
end

local function findProblem()
    return shapeProblem(TRIGGER_CALLBACK) or shapeProblem(CAN_BE_TRIGGERED) or shapeProblem(TOGGLE_HANDLER)
end

local function giveUp(problem)
    state.gaveUp = true
    print("[FS25_VR] [Warning] Animated Objects support disabled: " .. problem
        .. ". Motion Controllers cannot work doors and gates; the game's prompts are unchanged.")
end

-- ---- The zone -----------------------------------------------------------------------------------------------------------------

local function localPlayer()
    local player = engine("g_localPlayer")
    if player == nil then
        local mission = engine("g_currentMission")
        player = type(mission) == "table" and mission.player or nil
    end
    return player
end

-- The three conditions the game's trigger callback applies before it puts the activatable in its system or takes it out: a career
-- mission, an enter or a leave, and the local player's root node. Errors never reach the game (the original has run by now).
local function trackZone(object, otherId, onEnter, onLeave)
    local mission, career = engine("g_currentMission"), engine("FSCareerMissionInfo")
    if type(mission) ~= "table" or mission.missionInfo == nil or career == nil then return end
    if not mission.missionInfo:isa(career) then return end
    if not (onEnter or onLeave) then return end
    local player = localPlayer()
    if player == nil or otherId ~= player.rootNode then return end
    state.zone[object] = onEnter and true or nil
end

local function anyHandTracked(self)
    return self:getHandState(self.MOTION_HAND_LEFT).tracked == true
        or self:getHandState(self.MOTION_HAND_RIGHT).tracked == true
end

local function install()
    local class = engine(TRIGGER_CALLBACK.class)
    local original = class[TRIGGER_CALLBACK.fn]
    local wrapper = function(object, triggerId, otherId, onEnter, onLeave, ...)
        local a, b, c = original(object, triggerId, otherId, onEnter, onLeave, ...)
        if state.installed then pcall(trackZone, object, otherId, onEnter, onLeave) end
        return a, b, c
    end
    state.class, state.original, state.wrapper = class, original, wrapper
    class[TRIGGER_CALLBACK.fn] = wrapper
    state.installed = true
    print("[FS25_VR] Animated Objects support: installed")
end

-- Map load: wraps the game's trigger callback when the game has what the module relies on. When it does not, the reason is kept and
-- warned of once, when Motion Controllers are in use (a player without them is never told).
function VRCompanion:loadAnimatedObjectsSupport()
    if state.installed or state.gaveUp then return end
    state.problem = findProblem()
    if state.problem == nil then install() end
end

-- Map unload (and the end of a session): hands the game's class back exactly as found.
function VRCompanion:resetAnimatedObjectsSupport()
    if state.installed and state.class ~= nil and state.class[TRIGGER_CALLBACK.fn] == state.wrapper then
        state.class[TRIGGER_CALLBACK.fn] = state.original
    end
    state = newState()
end

-- ---- Selecting ----------------------------------------------------------------------------------------------------------------

-- Whether the Pointer Ray may work Animated Objects now. Nothing here changes what the game's prompt does.
local function rayMayWork(self)
    if not state.installed or self.motionControllersEnabled == false then return false end
    if not self:isPointerRayInControl() or self:getPointerRay() == nil then return false end
    -- A menu or a Stereo Dialog takes the input; a vehicle and the other views are out of scope (ADR-0034).
    if self:doesMenuTakeInput() or self:getControlledVehicle() ~= nil then return false end
    local player = localPlayer()
    return type(player) == "table" and player.camera ~= nil and player.camera.isFirstPerson == true
end

-- Whether the object can be worked with one press of its action right now: its "can be triggered" check (asked of the object itself)
-- answers true, its action is a single press, and the activatable has the toggle handler.
local function isWorkable(object)
    local controls = object.controls
    if type(controls) ~= "table" or controls.posAction == nil or controls.negAction ~= nil then return false end
    local activatable = object.activatable
    if type(activatable) ~= "table" or type(activatable.onAnimationInputToggle) ~= "function" then return false end
    if type(object.getCanBeTriggered) ~= "function" then return false end
    local ok, can = pcall(object.getCanBeTriggered, object)
    return ok and can == true
end

-- Fills state.parts with the animation parts of every object in the zone that can be worked; returns how many objects that is.
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

-- The mask of the ray cast: everything but the flags the zone and the player are made of, or nil when the game does not have them.
local function castMask()
    local mask, flags = engine("CollisionMask"), engine("CollisionFlag")
    if type(mask) ~= "table" or type(flags) ~= "table" or type(mask.ALL) ~= "number" then return nil end
    local result = mask.ALL
    for _, name in ipairs(IGNORED_FLAGS) do
        if type(flags[name]) == "number" then result = result - flags[name] end
    end
    return result
end

-- The callback of the engine's ray cast: the closest hit.
local function onHit(hit, node, x, y, z, distance)
    hit.node, hit.x, hit.y, hit.z, hit.distance = node, x, y, z, distance
    return true
end

-- The object whose animation part the ray is on, with where it hits and how far along the ray from the fingertip, or nil.
local function objectUnderTheRay(self, ray)
    local rayCast, getParent = engine("raycastClosest"), engine("getParent")
    if type(rayCast) ~= "function" or type(getParent) ~= "function" then return nil end
    local hit = state.hit
    hit.node, hit.onHit = nil, onHit
    local behind = tonumber(self.OBJECT_GRAB_REACH_BEHIND) or DEFAULT_REACH_BEHIND
    local ok = pcall(rayCast, ray.ox - ray.dx * behind, ray.oy - ray.dy * behind, ray.oz - ray.dz * behind, ray.dx, ray.dy, ray.dz,
        self.POINTER_RAY_LENGTH + behind, "onHit", hit, castMask())
    if not ok or hit.node == nil or hit.distance < behind - BEHIND_MARGIN then return nil end
    -- From the hit node up: a leaf's shape is below the part's node, the part itself counts, a building around it does not.
    local node = hit.node
    for _ = 1, MAX_PARENTS do
        if node == nil or node == 0 then return nil end
        local object = state.parts[node]
        if object ~= nil then return object, hit.x, hit.y, hit.z, hit.distance - behind end
        local okParent, parent = pcall(getParent, node)
        if not okParent then return nil end
        node = parent
    end
    return nil
end

-- A pull runs the toggle the prompt's button runs. When the prompt is not the one shown, its action event is not registered, and the
-- handler's refresh of the prompt's texts would raise for it: the refresh is left out then. Guarded: nothing reaches the game.
-- Returns whether the handler ran without an error.
local function toggle(object)
    local activatable = object.activatable
    local target = activatable
    if object.controls.posActionEventId == nil then
        target = setmetatable({ updateActionEventTexts = function() end }, { __index = activatable })
    end
    return (pcall(activatable.onAnimationInputToggle, target))
end

-- The pulses of a Click Point, on the Pointer Hand only: "selected" as the ray lands, "clicked" for a pull that toggled.
local function feel(self, kind, object)
    if type(self.onClickPointEvent) == "function" then self:onClickPointEvent(kind, object) end
end

-- Called by the Pointer Ray code once per Companion Mod update, after the ray is placed. Returns whether the ray is on an Animated
-- Object that can be worked and, then, where it hits. `suppressed`: a Click Point has the ray. `rivalDistance`: how far along the ray
-- from the fingertip a loose object Object Grab can pick up is; one nearer than the Animated Object is the ray's target instead (a tie
-- is the Animated Object's), and nothing is selected then.
function VRCompanion:updateAnimatedObjects(dt, suppressed, rivalDistance)
    if not state.installed then
        -- Found unusable at map load: said once, to a player who uses Motion Controllers.
        if state.problem ~= nil and not state.gaveUp and self.motionControllersEnabled ~= false and anyHandTracked(self) then
            giveUp(state.problem)
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
    -- The select pulse of a Click Point: one on the Pointer Hand as the ray lands on an Animated Object it was not on, again only after
    -- it was on none or on another. Moving from one moving part to another of the same object is not landing.
    if selected ~= state.landedOn then
        state.landedOn = selected
        if selected ~= nil then feel(self, "selected", selected) end
    end
    if selected == nil then return false end

    -- While the ray is on an Animated Object the Pointer Hand's trigger is its: it reads released on the Virtual Gamepad, unless it was
    -- already held when the ray landed (it then keeps its binding and toggles nothing). Only the update the trigger went down toggles.
    local mayUseTrigger = true
    local pointerHand = self:getPointerHand()
    if pointerHand ~= nil and type(self.takeVirtualGamepadInput) == "function" then
        mayUseTrigger = self:takeVirtualGamepadInput(pointerHand, "trigger")
    end
    if mayUseTrigger then
        local pressed = self:getPointerTrigger()
        if pressed and toggle(selected) then feel(self, "clicked", selected) end
    end
    return true, hx, hy, hz
end

-- What names an object in the diagnostics: its save id (a placed object has one), else the name of its first animation part's node (a
-- building's door has none, only the nodes of its i3d), else that node's id, else just "yes".
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

-- One line for vrPointerDiagnostics: whether the wrap is installed and what it currently sees.
function VRCompanion:getAnimatedObjectsDiagnostics()
    if state.gaveUp then return "Animated Objects support: gave up (see the warning in the log)" end
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

VRCompanion.AnimatedObjects.updateAnimatedObjects = function(self, ...) return VRCompanion.updateAnimatedObjects(VRCompanion, ...) end
VRCompanion.AnimatedObjects.resetAnimatedObjectsSupport = function(self, ...) return VRCompanion.resetAnimatedObjectsSupport(VRCompanion, ...) end

return VRCompanion.AnimatedObjects
