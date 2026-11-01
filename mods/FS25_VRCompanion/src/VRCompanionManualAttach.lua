-- ====================================================================
-- FS25 VR Companion Mod: Manual Attach support (ADR-0035, spec #227)
--
-- Lets the Pointer Ray work the hitches of the Manual Attach mod (FS25_manualAttach, release 1.1.0.0) on foot (CONTEXT.md: Coupling Point,
-- Implement). Manual Attach makes the player attach and detach Implements by hand, but picks the hitch by distance, not by aim, and puts the
-- action on a key. Here the player stands within Manual Attach's own reach of a hitch, points the Pointer Ray at it and pulls the trigger: the
-- Implement lined up there is attached, or the one on it is detached, by Manual Attach's own code (its reach, rules, warnings, sounds and
-- multiplayer events are unchanged). The hitch is the first of the three Coupling Points of the spec; the PTO stub and the hose couplers are a
-- later ticket.
--
-- How it works. Manual Attach keeps the vehicles its detection has found (handler.detectedVehicles). Each update the module lists, from those
-- and with Manual Attach's own helpers and constants, every hitch within its reach of the player: a free hitch with an Implement lined up
-- (VehicleAttachmentHandler.getAttachableInJointRange) and a hitch with an Implement on it. Manual Attach's own search keeps only the nearest of
-- them; the module keeps all. A hitch can be pointed at when Manual Attach's joint attachment would show its attach or detach prompt for that
-- hitch's candidate (getActionEventInfo), so what Manual Attach refuses in its perform path (an Implement not lowered, a PTO or hoses still
-- connected) is still pointed at, and the pull shows Manual Attach's warning. The hitch nearest the centre of the ray, within the pointer cone
-- (vrPointerCone, the angle at the hand, as for Click Points), is selected, and the ray ends on it. A pull sets the handler's five candidate
-- fields to that hitch's and runs the handler's own execute path for the joint attachment, guarded so that an error never reaches the game.
--
-- What it relies on (release 1.1.0.0; pinned by tests/test_manual_attach_wraps.cpp against stand-ins, with the functions missing or reshaped):
--   FS25_manualAttach.g_manualAttach (the global table is the fallback)   canPlayerPerformManualAttachment, vehicleAttachmentHandler
--   FS25_manualAttach.VehicleAttachmentHandler (the global table is the fallback)   the constants MAX_ATTACH_DISTANCE_SQ, MAX_ATTACH_ANGLE and
--       PLAYER_MIN_DISTANCE_SQ; the static getAttachableInJointRange(vehicleSystem, vehicle, attacherJoint, maxDistanceSq, maxAngle, isPlayerBased)
--       and isVehicleValid(vehicle)
--   the handler   detectedVehicles, executeAttachment(attachment), vehicleJointAttachment, and the candidate fields attacherVehicle,
--       attacherVehicleJointDescIndex, attachable, attachableJointDescIndex, attachedImplement
--   the joint attachment   getActionEventInfo(attacherVehicle, attachable, attachedImplement, jointDescIndex, playerCanPerform), performAttachment
--   g_currentMission.vehicleSystem, g_localPlayer.rootNode, localToLocal, getWorldTranslation
--   on a vehicle   spec_attacherJoints.attacherJoints[i].jointTransform, getAttachedImplements() ({ object, jointDescIndex })
-- Nothing of Manual Attach is wrapped or rewritten, its bindings included; the module only reads, and sets the candidate fields before it runs
-- the execute path. When what it relies on is missing or has another shape, the Companion Mod logs ONE warning when Motion Controllers are in
-- use and leaves Manual Attach, and every other feature, alone.
--
-- When the ray can work a hitch. Only while Manual Attach says the player can attach by hand (canPlayerPerformManualAttachment: on foot, no Hand
-- Tool in hand, no Held Object), only while the Pointer Ray has control of the pointer (not the desk mouse), the head is placed in the world,
-- no menu or Stereo Dialog takes the input, and Motion Controllers are on. Of a Click Point and a hitch both within the cone, the one nearer the
-- ray's centre wins (a tie is the Click Point's) and the other selects nothing; either one over an Animated Object and over an object Object Grab
-- can pick up.
--
-- PUBLIC SURFACE
--   VRCompanion:updateManualAttach(dt, suppressed, clickCos) -> hasTarget, hitX, hitY, hitZ   once per update after the Pointer Ray is placed
--       (VRCompanion:updatePointerRay), after Interactive Control has reported its best Click Point (peekInteractiveControlTarget) and before it
--       commits; suppressed: nothing is selected; clickCos: the cosine of that Click Point's angle from the ray's centre, or nil without one.
--       While the ray is on a hitch the Pointer Hand's trigger is taken off the Virtual Gamepad (VRCompanion:takeVirtualGamepadInput(hand,
--       "trigger")). The Pointer Hand feels it as a Click Point (VRCompanion:onClickPointEvent): a light pulse as the ray lands on a hitch
--       it was not on, a firmer one for each pull that ran Manual Attach's path.
--   VRCompanion:getManualAttachDiagnostics() -> string   one line for vrPointerDiagnostics
--   VRCompanion:resetManualAttachSupport()               map unload (hands back anything it set up: nothing is wrapped)
--
-- Hooks into the rest of the mod
--   MotionControllers   updatePointerRay -> peekInteractiveControlTarget(), updateManualAttach(dt, false, clickCos), updateInteractiveControl(dt,
--       selectNone), before Animated Objects and Object Grab; settleInteractiveControl (the paths where Motion Controller input is ignored) -> updateManualAttach(dt, true) so the selection
--       clears; getPointerDiagnostics -> getManualAttachDiagnostics(); resetMotionControllers -> resetManualAttachSupport()
-- ====================================================================

VRCompanion = VRCompanion or {}

local engine = VRCompanion.getEngineGlobal

local MOD_NAME = "FS25_manualAttach"
local CONSTANTS = { "MAX_ATTACH_DISTANCE_SQ", "MAX_ATTACH_ANGLE", "PLAYER_MIN_DISTANCE_SQ" }
-- How near in cosine (about 0.08 degrees at the ray's centre) a Click Point and a hitch must be to count as equally near the ray's centre.
local TIE_COSINE = 1e-6

-- The functions the module relies on: how many parameters the function must declare at least (a reshaped function declares fewer, self counts
-- in a method) and what they are, for the warning.
local RANGE_HELPER = { name = "getAttachableInJointRange", parameters = 6, describe = "(vehicleSystem, vehicle, attacherJoint, maxDistanceSq, maxAngle, isPlayerBased)" }
local VALID_HELPER = { name = "isVehicleValid", parameters = 1, describe = "(vehicle)" }
local EXECUTE = { name = "executeAttachment", parameters = 2, describe = "(handler, attachment)" }
local ACTION_INFO = { name = "getActionEventInfo", parameters = 6, describe = "(attachment, attacherVehicle, attachable, attachedImplement, jointDescIndex, playerCanPerform)" }
local PERFORM = { name = "performAttachment", parameters = 7, describe = "(attachment, attacherVehicle, attachable, attachedImplement, attacherJointDescIndex, attachableJointDescIndex, playerCanPerform)" }

local function newState()
    return {
        installed = false,
        gaveUp = false,       -- the support was found unusable (warned once); do not look again
        instance = nil,       -- g_manualAttach
        handler = nil,        -- its vehicle attachment handler
        class = nil,          -- the VehicleAttachmentHandler class
        hitches = {},         -- scratch, reused each update: the hitches in reach
        count = 0,            -- how many of them are in reach
        selected = nil,       -- the hitch the ray is on (one of hitches)
        landedVehicle = nil,  -- the hitch the ray is on and has pulsed for
        landedJoint = nil,
    }
end

local state = newState()

-- ---- Checking what Manual Attach has ---------------------------------------------------------------------------------------

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

-- Why a function of an owner is not what the module expects, or nil when it is.
local function shapeProblem(owner, ownerName, spec)
    local fn = owner[spec.name]
    local name = ownerName .. "." .. spec.name
    if fn == nil then return name .. " is missing" end
    if type(fn) ~= "function" then return name .. " is not a function" end
    local count = parameterCount(fn)
    if count ~= nil and count < spec.parameters then
        return string.format("%s takes %d parameters, not %s", name, count, spec.describe)
    end
    return nil
end

-- Manual Attach's objects, looked up in its mod environment with the global table as the fallback; and why they cannot be used, or nil.
local function findManualAttach()
    local env = engine(MOD_NAME)
    local instance = type(env) == "table" and env.g_manualAttach or engine("g_manualAttach")
    local class = type(env) == "table" and env.VehicleAttachmentHandler or engine("VehicleAttachmentHandler")
    if type(instance) ~= "table" and type(class) ~= "table" then
        local loaded = engine("g_modIsLoaded")
        if type(loaded) ~= "table" or loaded[MOD_NAME] ~= true then return nil, nil, nil, "the mod " .. MOD_NAME .. " is not loaded" end
    end
    if type(instance) ~= "table" then return nil, nil, nil, "g_manualAttach was not found" end
    if type(class) ~= "table" then return nil, nil, nil, "its VehicleAttachmentHandler class was not found" end
    local handler = instance.vehicleAttachmentHandler
    if type(handler) ~= "table" then return nil, nil, nil, "g_manualAttach has no attachment handler" end

    for _, name in ipairs(CONSTANTS) do
        if type(class[name]) ~= "number" then return nil, nil, nil, "VehicleAttachmentHandler." .. name .. " is missing" end
    end
    local problem = shapeProblem(class, "VehicleAttachmentHandler", RANGE_HELPER) or shapeProblem(class, "VehicleAttachmentHandler", VALID_HELPER)
    if problem ~= nil then return nil, nil, nil, problem end
    if type(handler.detectedVehicles) ~= "table" then return nil, nil, nil, "the attachment handler has no detectedVehicles" end
    if type(instance.canPlayerPerformManualAttachment) ~= "boolean" then
        return nil, nil, nil, "g_manualAttach has no canPlayerPerformManualAttachment"
    end
    problem = shapeProblem(handler, "the attachment handler", EXECUTE)
    if problem ~= nil then return nil, nil, nil, problem end
    local joint = handler.vehicleJointAttachment
    if type(joint) ~= "table" then return nil, nil, nil, "the attachment handler has no vehicleJointAttachment" end
    problem = shapeProblem(joint, "the joint attachment", ACTION_INFO) or shapeProblem(joint, "the joint attachment", PERFORM)
    if problem ~= nil then return nil, nil, nil, problem end
    return instance, handler, class, nil
end

local function giveUp(problem)
    state.gaveUp = true
    print("[FS25_VR] [Warning] Manual Attach support disabled: " .. problem
        .. ". Motion Controllers cannot work hitches; Manual Attach is unchanged.")
end

local function anyHandTracked(self)
    return self:getHandState(self.MOTION_HAND_LEFT).tracked == true
        or self:getHandState(self.MOTION_HAND_RIGHT).tracked == true
end

-- Looks for Manual Attach when a Motion Controller is in use. Nothing of it is wrapped, so there is nothing to hand back at map unload.
local function tryInstall()
    local instance, handler, class, problem = findManualAttach()
    if problem ~= nil then
        giveUp(problem)
        return
    end
    state.instance, state.handler, state.class = instance, handler, class
    state.installed = true
    print("[FS25_VR] Manual Attach support: installed")
end

-- Map unload (and the end of a session): the module wrapped nothing, so it only forgets.
function VRCompanion:resetManualAttachSupport()
    state = newState()
end

-- ---- The hitches in reach -----------------------------------------------------------------------------------------------------

local function localPlayer()
    local player = engine("g_localPlayer")
    if player == nil then
        local mission = engine("g_currentMission")
        player = type(mission) == "table" and mission.player or nil
    end
    return player
end

-- Fills the next record of the scratch list for a hitch, and returns it.
local function addHitch(vehicle, jointIndex, attachable, attachableJointIndex, implement, joint)
    local count = state.count + 1
    state.count = count
    local hitch = state.hitches[count]
    if hitch == nil then
        hitch = {}
        state.hitches[count] = hitch
    end
    hitch.vehicle, hitch.jointIndex, hitch.attachable, hitch.attachableJointIndex, hitch.implement = vehicle, jointIndex, attachable, attachableJointIndex, implement
    hitch.joint = joint
    hitch.pointable = false
    return hitch
end

-- Whether a joint is within Manual Attach's reach of the player: its own constant, measured as it measures it.
local function isInReach(joint, playerRoot)
    local localToLocal = engine("localToLocal")
    local x, y, z = localToLocal(joint.jointTransform, playerRoot, 0, 0, 0)
    return x * x + y * y + z * z < state.class.PLAYER_MIN_DISTANCE_SQ
end

-- Lists every hitch within Manual Attach's reach of the player, from the vehicles its detection has found: a free hitch with an Implement
-- lined up (through its own helper and constants) and a hitch with an Implement on it, as its nearest-candidate search walks them.
local function collectHitches(player)
    state.count = 0
    local playerRoot = player.rootNode
    if playerRoot == nil then return end
    local class, vehicleSystem = state.class, nil
    local mission = engine("g_currentMission")
    vehicleSystem = type(mission) == "table" and mission.vehicleSystem or nil
    for _, vehicle in ipairs(state.handler.detectedVehicles) do
        local spec = vehicle.spec_attacherJoints
        if class.isVehicleValid(vehicle) and type(spec) == "table" and type(spec.attacherJoints) == "table" then
            if type(vehicle.getAttachedImplements) == "function" then
                for _, implement in ipairs(vehicle:getAttachedImplements()) do
                    local joint = spec.attacherJoints[implement.jointDescIndex]
                    if implement.object ~= nil and joint ~= nil and joint.jointTransform ~= nil and isInReach(joint, playerRoot) then
                        addHitch(vehicle, implement.jointDescIndex, nil, nil, implement.object, joint)
                    end
                end
            end
            if vehicleSystem ~= nil then
                for index, joint in ipairs(spec.attacherJoints) do
                    if joint.jointIndex == 0 and joint.jointTransform ~= nil and isInReach(joint, playerRoot) then
                        local attachable, attachableJointIndex = class.getAttachableInJointRange(vehicleSystem, vehicle, joint,
                            class.MAX_ATTACH_DISTANCE_SQ, class.MAX_ATTACH_ANGLE, true)
                        if attachable ~= nil then addHitch(vehicle, index, attachable, attachableJointIndex, nil, joint) end
                    end
                end
            end
        end
    end
end

-- Whether Manual Attach's joint attachment would show its attach or detach prompt for the hitch's candidate. What it refuses in its perform
-- path is not asked here: a refused hitch is pointed at and the pull shows Manual Attach's warning.
local function isPointable(hitch, canPerform)
    local attachment = state.handler.vehicleJointAttachment
    local ok, info = pcall(attachment.getActionEventInfo, attachment, hitch.vehicle, hitch.attachable, hitch.implement, hitch.jointIndex, canPerform)
    return ok and type(info) == "table" and info.visibility == true
end

-- The hitch nearest the centre of the ray within the pointer cone, or nil; its world position is in x, y, z of the result.
local function selectFromRay(self, ray, canPerform)
    local getWorldTranslation = engine("getWorldTranslation")
    local bestCos = math.cos(math.rad(self.pointerConeDegrees))
    local best = nil
    for i = 1, state.count do
        local hitch = state.hitches[i]
        if isPointable(hitch, canPerform) then
            hitch.pointable = true
            local px, py, pz = getWorldTranslation(hitch.joint.jointTransform)
            local vx, vy, vz = px - ray.ox, py - ray.oy, pz - ray.oz
            local distance = math.sqrt(vx * vx + vy * vy + vz * vz)
            if distance > 0.01 then
                local cos = (vx * ray.dx + vy * ray.dy + vz * ray.dz) / distance
                if cos >= bestCos then
                    best, bestCos = hitch, cos
                    hitch.x, hitch.y, hitch.z, hitch.cos = px, py, pz, cos
                end
            end
        end
    end
    return best
end

-- ---- Working a hitch ----------------------------------------------------------------------------------------------------------

-- A pull sets the handler's candidate to the hitch and runs its own execute path for the joint attachment. Guarded: nothing reaches the game.
-- Returns whether the path ran without an error.
local function work(hitch)
    local handler = state.handler
    handler.attacherVehicle, handler.attacherVehicleJointDescIndex = hitch.vehicle, hitch.jointIndex
    handler.attachable, handler.attachableJointDescIndex, handler.attachedImplement = hitch.attachable, hitch.attachableJointIndex, hitch.implement
    return (pcall(handler.executeAttachment, handler, handler.vehicleJointAttachment))
end

-- The pulses of a Click Point, on the Pointer Hand only: "selected" as the ray lands, "clicked" for a pull that ran Manual Attach's path.
local function feel(self, kind, hitch)
    if type(self.onClickPointEvent) == "function" then self:onClickPointEvent(kind, hitch) end
end

-- Whether the Pointer Ray may work hitches now. Manual Attach's own on-foot check (its flag) covers the cab, Hand Tools and Held Objects.
local function rayMayWork(self)
    if self.motionControllersEnabled == false then return false end
    if not self:isPointerRayInControl() or self:getPointerRay() == nil then return false end
    -- A menu or a Stereo Dialog takes the input.
    return not self:doesMenuTakeInput()
end

-- Called by the Pointer Ray code once per Companion Mod update, after the ray is placed. Returns whether the ray is on a hitch that can be
-- worked and, then, where it ends. `suppressed`: nothing is selected. `clickCos`: the cosine of the angle from the ray's centre of the Click Point
-- Interactive Control would select; it keeps the ray when it is as near the centre as the hitch or nearer.
function VRCompanion:updateManualAttach(dt, suppressed, clickCos)
    if not state.installed then
        -- Looked for once, when a Motion Controller is in use: a player without them is never told.
        if state.gaveUp or self.motionControllersEnabled == false or not anyHandTracked(self) then return false end
        tryInstall()
        if not state.installed then return false end
    end
    state.selected, state.count = nil, 0
    local player = localPlayer()
    local canPerform = state.instance.canPlayerPerformManualAttachment == true
    if suppressed or not canPerform or type(player) ~= "table" or not rayMayWork(self) then
        state.landedVehicle, state.landedJoint = nil, nil
        return false
    end

    local selected = nil
    if pcall(collectHitches, player) then selected = selectFromRay(self, self:getPointerRay(), canPerform) end
    -- (Within TIE_COSINE of each other, floating point noise included, they are equally near: the Click Point's.)
    if selected ~= nil and clickCos ~= nil and clickCos >= selected.cos - TIE_COSINE then selected = nil end
    state.selected = selected
    -- The select pulse of a Click Point: one on the Pointer Hand as the ray lands on a hitch it was not on.
    local vehicle, joint = selected and selected.vehicle or nil, selected and selected.jointIndex or nil
    if vehicle ~= state.landedVehicle or joint ~= state.landedJoint then
        state.landedVehicle, state.landedJoint = vehicle, joint
        if selected ~= nil then feel(self, "selected", selected) end
    end
    if selected == nil then return false end

    -- While the ray is on a hitch the Pointer Hand's trigger is Manual Attach's: it reads released on the Virtual Gamepad, unless it was
    -- already held when the ray landed (it then keeps its binding and works nothing). Only the update the trigger went down works the hitch.
    local mayUseTrigger = true
    local pointerHand = self:getPointerHand()
    if pointerHand ~= nil and type(self.takeVirtualGamepadInput) == "function" then
        mayUseTrigger = self:takeVirtualGamepadInput(pointerHand, "trigger")
    end
    if mayUseTrigger then
        local pressed = self:getPointerTrigger()
        if pressed and work(selected) then feel(self, "clicked", selected) end
    end
    return true, selected.x, selected.y, selected.z
end

-- One line for vrPointerDiagnostics: whether the support is installed, how many hitches are in reach and which one is selected.
function VRCompanion:getManualAttachDiagnostics()
    if state.gaveUp then return "Manual Attach support: gave up (see the warning in the log)" end
    if not state.installed then return "Manual Attach support: not installed yet (waiting for a tracked Motion Controller)" end
    local selected = state.selected
    local text = "none"
    if selected ~= nil then
        local name = nil
        if type(selected.vehicle.getName) == "function" then
            local ok, result = pcall(selected.vehicle.getName, selected.vehicle)
            if ok and type(result) == "string" then name = result end
        end
        text = string.format("hitch %s of %s", tostring(selected.jointIndex), name or "a vehicle")
    end
    return string.format("Manual Attach support: installed | hitches in reach: %d | selected: %s", state.count, text)
end
