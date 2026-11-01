-- ====================================================================
-- FS25 VR Companion Mod: Manual Attach support (ADR-0035, spec #227)
--
-- Lets the Pointer Ray work the hitches of the Manual Attach mod (FS25_manualAttach, release 1.1.0.0) on foot (CONTEXT.md: Coupling Point,
-- Implement). Manual Attach makes the player attach and detach Implements by hand, but picks the hitch by distance, not by aim, and puts the
-- action on a key. Here the player stands within Manual Attach's own reach of a hitch, points the Pointer Ray at it and pulls the trigger: the
-- Implement lined up there is attached, or the one on it is detached, by Manual Attach's own code (its reach, rules, warnings, sounds and
-- multiplayer events are unchanged). The hitch is the first of the three Coupling Points of the spec. The other two are on the same vehicle for
-- the Implement on the hitch: its PTO stub (each of its own PTO outputs for that joint that has an output node: a pass-through output, which
-- resolves to another vehicle, has none) and its hose couplers (each hose target node and custom hose target for that joint). A pull on the PTO
-- stub runs the handler's execute path for the PTO attachment, on any hose coupler for the hoses attachment (all the hoses together); each
-- is pointable when that attachment would show its prompt for the Implement, and the one of all of them nearest the centre of the ray is
-- selected. Pointing at a PTO stub never reaches the hoses or the hitch and the other way round: one pull, one of Manual Attach's paths.
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
--   VehicleAttachmentHandler.updateCandidates(handler) (wrapped, see below)
--   the handler   detectedVehicles, executeAttachment(attachment), vehicleJointAttachment, playerCanPerformManualAttachment, and the candidate fields attacherVehicle,
--       attacherVehicleJointDescIndex, attachable, attachableJointDescIndex, attachedImplement
--   the joint attachment   getActionEventInfo(attacherVehicle, attachable, attachedImplement, jointDescIndex, playerCanPerform), performAttachment
--   the PTO attachment (powerTakeOffAttachment) and the hoses attachment (connectionHosesAttachment)   the same getActionEventInfo and
--       performAttachment (they act on the attached Implement alone)
--   g_currentMission.vehicleSystem, g_localPlayer.rootNode, localToLocal, getWorldTranslation
--   on a vehicle   spec_attacherJoints.attacherJoints[i].jointTransform, getAttachedImplements() ({ object, jointDescIndex })
--   the game's vehicle side of the PTO and hoses, when a vehicle has the specialization (else it has no such Coupling Point; a specialization
--       with another shape turns the support off): getOutputPowerTakeOffs() ({ outputNode, attacherJointIndices = { [jointIndex] = true } }),
--       spec_connectionHoses.targetNodes ({ node, attacherJointIndices }) and .customHoseTargetsByAttacher[jointIndex] ({ node })
-- Prompts and keys follow the hitch (ticket #230). The module wraps ONE function of Manual Attach, VehicleAttachmentHandler.updateCandidates
-- (the candidate update, at the class level so the handler instance finds it), original first: after Manual Attach has picked its nearest
-- candidate, while the ray is on a hitch (and the player can attach by hand), the five candidate fields are set to that hitch's. Manual
-- Attach's own draw (the input help prompts, the context box) and its own keys read those fields, so the text, the keys and the pull never
-- disagree; with the ray on no hitch the wrap changes nothing. Map unload hands the function back as found. Nothing else of Manual Attach is
-- wrapped or rewritten, its bindings included; the module otherwise only reads, and sets the candidate fields before it runs the execute path.
-- When what it relies on (updateCandidates included, one parameter: the handler) is missing or has another shape, the Companion Mod logs ONE
-- warning when Motion Controllers are in use, wraps nothing and leaves Manual Attach, and every other feature, alone. Its instance and attachment
-- handler, which it makes as the map loads, are waited for (WAIT_FOR_OBJECTS_MS, counted over the updates with a tracked Motion Controller) before
-- that warning; the mod not being loaded, or a function missing or reshaped, is final at the first look.
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
--   VRCompanion:resetManualAttachSupport()               map unload (hands the wrapped candidate update back as found)
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
-- How long (ms of updates with a tracked Motion Controller) Manual Attach's instance and attachment handler are waited for before the support
-- gives up. Manual Attach makes them as the map loads, so in release 1.1.0.0 they are there at the first look.
local WAIT_FOR_OBJECTS_MS = 10000

-- The functions the module relies on: how many parameters the function must declare at least (a reshaped function declares fewer, self counts
-- in a method) and what they are, for the warning.
local RANGE_HELPER = { name = "getAttachableInJointRange", parameters = 6, describe = "(vehicleSystem, vehicle, attacherJoint, maxDistanceSq, maxAngle, isPlayerBased)" }
local VALID_HELPER = { name = "isVehicleValid", parameters = 1, describe = "(vehicle)" }
local EXECUTE = { name = "executeAttachment", parameters = 2, describe = "(handler, attachment)" }
local ACTION_INFO = { name = "getActionEventInfo", parameters = 6, describe = "(attachment, attacherVehicle, attachable, attachedImplement, jointDescIndex, playerCanPerform)" }
local PERFORM = { name = "performAttachment", parameters = 7, describe = "(attachment, attacherVehicle, attachable, attachedImplement, attacherJointDescIndex, attachableJointDescIndex, playerCanPerform)" }

local CANDIDATE_UPDATE = { name = "updateCandidates", parameters = 1, describe = "(handler)" }

local function newState()
    return {
        installed = false,
        gaveUp = false,       -- the support was found unusable (warned once); do not look again
        gaveUpBecause = nil,  -- why (the warning's problem), for the diagnostics
        waitedMs = 0,         -- how long Manual Attach's instance or handler has been waited for
        waitingFor = nil,     -- what is waited for (the problem found at the last look), for the diagnostics
        loggedErrors = {},    -- [message] = true for each error of the hitch listing already logged (once each)
        instance = nil,       -- g_manualAttach
        handler = nil,        -- its vehicle attachment handler
        class = nil,          -- the VehicleAttachmentHandler class
        hitches = {},         -- scratch, reused each update: the hitches in reach
        count = 0,            -- how many of them are in reach
        selected = nil,       -- the hitch the ray is on a Coupling Point of (one of hitches)
        selectedKind = nil,   -- which of its Coupling Points: "hitch" or "pto"
        landedVehicle = nil,  -- the hitch the ray is on and has pulsed for
        landedJoint = nil,
        landedKind = nil,
        candidateUpdate = nil,        -- the class's candidate update as found, while the module has it wrapped
        candidateUpdateWrap = nil,    -- and what the module put there
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
    -- The instance and its handler are made when the map loads: a release or a load order that makes them later is waited for (the fifth value
    -- says so). The class and everything below are there when the mod's files are, and are final.
    if type(instance) ~= "table" then return nil, nil, nil, "g_manualAttach was not found", true end
    if type(class) ~= "table" then return nil, nil, nil, "its VehicleAttachmentHandler class was not found" end
    local handler = instance.vehicleAttachmentHandler
    if type(handler) ~= "table" then return nil, nil, nil, "g_manualAttach has no attachment handler", true end

    for _, name in ipairs(CONSTANTS) do
        if type(class[name]) ~= "number" then return nil, nil, nil, "VehicleAttachmentHandler." .. name .. " is missing" end
    end
    local problem = shapeProblem(class, "VehicleAttachmentHandler", RANGE_HELPER) or shapeProblem(class, "VehicleAttachmentHandler", VALID_HELPER)
    if problem ~= nil then return nil, nil, nil, problem end
    if type(handler.detectedVehicles) ~= "table" then return nil, nil, nil, "the attachment handler has no detectedVehicles" end
    if type(instance.canPlayerPerformManualAttachment) ~= "boolean" then
        return nil, nil, nil, "g_manualAttach has no canPlayerPerformManualAttachment"
    end
    -- The candidate update wrap reads the handler's own copy of the flag.
    if type(handler.playerCanPerformManualAttachment) ~= "boolean" then
        return nil, nil, nil, "the attachment handler has no playerCanPerformManualAttachment"
    end
    problem = shapeProblem(handler, "the attachment handler", EXECUTE) or shapeProblem(class, "VehicleAttachmentHandler", CANDIDATE_UPDATE)
    if problem ~= nil then return nil, nil, nil, problem end
    local joint = handler.vehicleJointAttachment
    if type(joint) ~= "table" then return nil, nil, nil, "the attachment handler has no vehicleJointAttachment" end
    problem = shapeProblem(joint, "the joint attachment", ACTION_INFO) or shapeProblem(joint, "the joint attachment", PERFORM)
    if problem ~= nil then return nil, nil, nil, problem end
    -- The PTO and hoses attachments (the PTO stub and the hose couplers act through them) have the same two functions.
    for _, entry in ipairs({ { "powerTakeOffAttachment", "the PTO attachment" }, { "connectionHosesAttachment", "the hoses attachment" } }) do
        local attachment = handler[entry[1]]
        if type(attachment) ~= "table" then return nil, nil, nil, "the attachment handler has no " .. entry[1] end
        problem = shapeProblem(attachment, entry[2], ACTION_INFO) or shapeProblem(attachment, entry[2], PERFORM)
        if problem ~= nil then return nil, nil, nil, problem end
    end
    return instance, handler, class, nil
end

local function giveUp(problem)
    state.gaveUp, state.gaveUpBecause = true, problem
    print("[FS25_VR] [Warning] Manual Attach support disabled: " .. problem
        .. ". Motion Controllers cannot work hitches; Manual Attach is unchanged.")
end

local function anyHandTracked(self)
    return self:getHandState(self.MOTION_HAND_LEFT).tracked == true
        or self:getHandState(self.MOTION_HAND_RIGHT).tracked == true
end

-- Wraps the class's candidate update, original first: after Manual Attach has picked its nearest candidate, while the ray is on a hitch, the
-- five candidate fields are that hitch's, so Manual Attach's prompts, context box and keys describe and act on the pointed hitch.
local function wrapCandidateUpdate(class)
    local original = class.updateCandidates
    local function updateCandidates(handler, ...)
        original(handler, ...)
        local selected = state.selected
        if selected ~= nil and handler == state.handler and handler.playerCanPerformManualAttachment == true then
            handler.attacherVehicle, handler.attacherVehicleJointDescIndex = selected.vehicle, selected.jointIndex
            handler.attachable, handler.attachableJointDescIndex, handler.attachedImplement = selected.attachable, selected.attachableJointIndex, selected.implement
        end
    end
    state.candidateUpdate, state.candidateUpdateWrap = original, updateCandidates
    class.updateCandidates = updateCandidates
end

-- Looks for Manual Attach when a Motion Controller is in use.
local function tryInstall(dt)
    local instance, handler, class, problem, mayAppear = findManualAttach()
    if problem ~= nil then
        if mayAppear then
            -- Not there yet: look again next update, for a bounded time of updates with a tracked Motion Controller, then give up as for the rest.
            state.waitedMs = state.waitedMs + (tonumber(dt) or 0)
            if state.waitedMs < WAIT_FOR_OBJECTS_MS then
                state.waitingFor = problem
                return
            end
        end
        giveUp(problem)
        return
    end
    state.instance, state.handler, state.class = instance, handler, class
    wrapCandidateUpdate(class)
    state.installed = true
    print("[FS25_VR] Manual Attach support: installed")
end

-- Map unload (and the end of a session): the class's candidate update is handed back as found (unless something else wrapped it since, which
-- cannot be taken out from under it), and the module forgets.
local function unwrapCandidateUpdate()
    if state.class ~= nil and state.candidateUpdateWrap ~= nil and state.class.updateCandidates == state.candidateUpdateWrap then
        state.class.updateCandidates = state.candidateUpdate
    end
end

function VRCompanion:resetManualAttachSupport()
    unwrapCandidateUpdate()
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

-- Why the game's PTO output or hose target lookup on a vehicle is not what the module expects, or nil when it is (or the vehicle has none).
-- The vehicle side of the PTO stub and the hose couplers is read from the game's PowerTakeOffs and ConnectionHoses specializations.
local function vehicleLookupProblem(vehicle)
    if type(vehicle.spec_powerTakeOffs) == "table" and type(vehicle.getOutputPowerTakeOffs) ~= "function" then
        return "the game's getOutputPowerTakeOffs is missing on a vehicle"
    end
    local hoses = vehicle.spec_connectionHoses
    if type(hoses) == "table" and (type(hoses.targetNodes) ~= "table" or type(hoses.customHoseTargetsByAttacher) ~= "table") then
        return "the game's hose targets (spec_connectionHoses.targetNodes, customHoseTargetsByAttacher) are missing on a vehicle"
    end
    return nil
end

-- Lists every hitch within Manual Attach's reach of the player, from the vehicles its detection has found: a free hitch with an Implement
-- lined up (through its own helper and constants) and a hitch with an Implement on it, as its nearest-candidate search walks them. Returns why
-- the support cannot go on when the game's side of a vehicle has another shape than expected.
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
            local problem = vehicleLookupProblem(vehicle)
            if problem ~= nil then return problem end
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

-- Whether one of Manual Attach's attachments (the joint, PTO or hoses attachment) would show its prompt for the hitch's candidate. What it
-- refuses in its perform path is not asked here: a refused hitch is pointed at and the pull shows Manual Attach's warning.
local function isPointable(attachment, hitch, canPerform)
    local ok, info = pcall(attachment.getActionEventInfo, attachment, hitch.vehicle, hitch.attachable, hitch.implement, hitch.jointIndex, canPerform)
    return ok and type(info) == "table" and info.visibility == true
end

-- The Coupling Point nearest the centre of the ray so far in this update (its angle's cosine, the hitch it belongs to, which of the three it
-- is, and its world position), reused each update.
local pick = { cos = 0, hitch = nil, kind = nil, x = 0, y = 0, z = 0 }

-- Takes a node (a hitch, a PTO stub or a hose coupler) as the pick when it is within the pointer cone and nearer the centre than the pick so far.
local function consider(hitch, kind, node, ray, getWorldTranslation)
    local px, py, pz = getWorldTranslation(node)
    local vx, vy, vz = px - ray.ox, py - ray.oy, pz - ray.oz
    local distance = math.sqrt(vx * vx + vy * vy + vz * vz)
    if distance <= 0.01 then return end
    local cos = (vx * ray.dx + vy * ray.dy + vz * ray.dz) / distance
    if cos >= pick.cos then
        pick.cos, pick.hitch, pick.kind = cos, hitch, kind
        pick.x, pick.y, pick.z = px, py, pz
    end
end

-- The hitch's Coupling Points on the vehicle's side that are not the hitch: its own PTO outputs for the joint that have an output node (a
-- pass-through output, which resolves to another vehicle, has none of its own).
local function considerPtoStubs(hitch, ray, getWorldTranslation)
    local vehicle = hitch.vehicle
    if type(vehicle.getOutputPowerTakeOffs) ~= "function" then return end
    for _, output in ipairs(vehicle:getOutputPowerTakeOffs() or {}) do
        local joints = output.attacherJointIndices
        if output.outputNode ~= nil and type(joints) == "table" and joints[hitch.jointIndex] ~= nil then
            consider(hitch, "pto", output.outputNode, ray, getWorldTranslation)
        end
    end
end

-- The hitch's hose couplers: each of the vehicle's hose target nodes for the joint and each of its custom hose targets for it.
local function considerHoseCouplers(hitch, ray, getWorldTranslation)
    local spec = hitch.vehicle.spec_connectionHoses
    if type(spec) ~= "table" then return end
    if type(spec.targetNodes) == "table" then
        for _, target in ipairs(spec.targetNodes) do
            local joints = target.attacherJointIndices
            if target.node ~= nil and type(joints) == "table" and joints[hitch.jointIndex] ~= nil then
                consider(hitch, "hoses", target.node, ray, getWorldTranslation)
            end
        end
    end
    local custom = type(spec.customHoseTargetsByAttacher) == "table" and spec.customHoseTargetsByAttacher[hitch.jointIndex] or nil
    if type(custom) == "table" then
        for _, target in ipairs(custom) do
            if target.node ~= nil then consider(hitch, "hoses", target.node, ray, getWorldTranslation) end
        end
    end
end

-- The Coupling Point of the hitch nearest the centre of the ray within the pointer cone, or nil. Returns the hitch and which of its Coupling
-- Points ("hitch", "pto", "hoses") it is; the world position is in x, y, z of the hitch.
local function selectFromRay(self, ray, canPerform)
    local getWorldTranslation = engine("getWorldTranslation")
    pick.cos, pick.hitch, pick.kind = math.cos(math.rad(self.pointerConeDegrees)), nil, nil
    for i = 1, state.count do
        local hitch = state.hitches[i]
        if isPointable(state.handler.vehicleJointAttachment, hitch, canPerform) then
            hitch.pointable = true
            consider(hitch, "hitch", hitch.joint.jointTransform, ray, getWorldTranslation)
        end
        -- The PTO and the hoses act on the Implement on the hitch: with nothing attached there is nothing to connect.
        if hitch.implement ~= nil then
            if isPointable(state.handler.powerTakeOffAttachment, hitch, canPerform) then considerPtoStubs(hitch, ray, getWorldTranslation) end
            if isPointable(state.handler.connectionHosesAttachment, hitch, canPerform) then considerHoseCouplers(hitch, ray, getWorldTranslation) end
        end
    end
    local best = pick.hitch
    if best == nil then return nil, nil end
    best.x, best.y, best.z, best.cos = pick.x, pick.y, pick.z, pick.cos
    return best, pick.kind
end

-- ---- Working a hitch ----------------------------------------------------------------------------------------------------------

-- A pull sets the handler's candidate to the hitch and runs its own execute path for the attachment of the Coupling Point (the joint attachment
-- for the hitch, the PTO attachment for the PTO stub). Guarded: nothing reaches the game. Returns whether the path ran without an error.
local function work(hitch, kind)
    local handler = state.handler
    handler.attacherVehicle, handler.attacherVehicleJointDescIndex = hitch.vehicle, hitch.jointIndex
    handler.attachable, handler.attachableJointDescIndex, handler.attachedImplement = hitch.attachable, hitch.attachableJointIndex, hitch.implement
    local attachment = handler.vehicleJointAttachment
    if kind == "pto" then attachment = handler.powerTakeOffAttachment
    elseif kind == "hoses" then attachment = handler.connectionHosesAttachment end
    return (pcall(handler.executeAttachment, handler, attachment))
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
        tryInstall(dt)
        if not state.installed then return false end
    end
    state.selected, state.count = nil, 0
    local player = localPlayer()
    local canPerform = state.instance.canPlayerPerformManualAttachment == true
    if suppressed or not canPerform or type(player) ~= "table" or not rayMayWork(self) then
        state.landedVehicle, state.landedJoint, state.landedKind = nil, nil, nil
        return false
    end

    local selected, kind = nil, nil
    local collected, problem = pcall(collectHitches, player)
    if collected and problem ~= nil then
        -- The game's side of a vehicle is not what the support expects: it switches itself off, with one warning, for good.
        state.installed, state.selected, state.selectedKind, state.count = false, nil, nil, 0
        state.landedVehicle, state.landedJoint, state.landedKind = nil, nil, nil
        unwrapCandidateUpdate()
        giveUp(problem)
        return false
    end
    if not collected then
        -- An error in the game's side of a vehicle: nothing is selected this update. Each message is logged once (until the map is unloaded),
        -- not every frame.
        state.count = 0
        local message = tostring(problem)
        if not state.loggedErrors[message] then
            state.loggedErrors[message] = true
            print("[FS25_VR] [Warning] Manual Attach support error while looking for hitches (nothing is selected until it passes): " .. message)
        end
    end
    if collected then selected, kind = selectFromRay(self, self:getPointerRay(), canPerform) end
    -- (Within TIE_COSINE of each other, floating point noise included, they are equally near: the Click Point's.)
    if selected ~= nil and clickCos ~= nil and clickCos >= selected.cos - TIE_COSINE then selected, kind = nil, nil end
    state.selected, state.selectedKind = selected, kind
    -- The select pulse of a Click Point: one on the Pointer Hand as the ray lands on a hitch it was not on.
    local vehicle, joint = selected and selected.vehicle or nil, selected and selected.jointIndex or nil
    if vehicle ~= state.landedVehicle or joint ~= state.landedJoint or kind ~= state.landedKind then
        state.landedVehicle, state.landedJoint, state.landedKind = vehicle, joint, kind
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
        if pressed and work(selected, kind) then feel(self, "clicked", selected) end
    end
    return true, selected.x, selected.y, selected.z
end

-- One line for vrPointerDiagnostics: whether the support is installed, how many hitches are in reach and which one is selected.
function VRCompanion:getManualAttachDiagnostics()
    if state.gaveUp then return "Manual Attach support: gave up (" .. tostring(state.gaveUpBecause) .. ")" end
    if not state.installed then
        if state.waitingFor ~= nil then
            return "Manual Attach support: not installed yet (waiting for Manual Attach's objects: " .. state.waitingFor .. ")"
        end
        return "Manual Attach support: not installed yet (waiting for a tracked Motion Controller)"
    end
    local selected = state.selected
    local text = "none"
    if selected ~= nil then
        local name = nil
        if type(selected.vehicle.getName) == "function" then
            local ok, result = pcall(selected.vehicle.getName, selected.vehicle)
            if ok and type(result) == "string" then name = result end
        end
        local which = state.selectedKind == "pto" and "PTO stub" or state.selectedKind == "hoses" and "hose coupler" or "hitch"
        text = string.format("hitch %s of %s (%s)", tostring(selected.jointIndex), name or "a vehicle", which)
    end
    return string.format("Manual Attach support: installed | hitches in reach: %d | selected: %s", state.count, text)
end
