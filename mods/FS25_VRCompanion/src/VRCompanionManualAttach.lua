VRCompanion = VRCompanion or {}

local engine = VRCompanion.getEngineGlobal

local MOD_NAME = "FS25_manualAttach"
local CONSTANTS = { "MAX_ATTACH_DISTANCE_SQ", "MAX_ATTACH_ANGLE", "PLAYER_MIN_DISTANCE_SQ" }
local TIE_COSINE = 1e-6
local WAIT_FOR_OBJECTS_MS = 10000

-- The functions the module relies on: how many parameters the function must declare at least (a reshaped function declares fewer, self counts
-- in a method) and what they are, for the warning.
local RANGE_HELPER = {
    name = "getAttachableInJointRange", parameters = 6,
    describe = "(vehicleSystem, vehicle, attacherJoint, maxDistanceSq, maxAngle, isPlayerBased)",
}
local VALID_HELPER = { name = "isVehicleValid", parameters = 1, describe = "(vehicle)" }
local EXECUTE = { name = "executeAttachment", parameters = 2, describe = "(handler, attachment)" }
local ACTION_INFO = {
    name = "getActionEventInfo", parameters = 6,
    describe = "(attachment, attacherVehicle, attachable, attachedImplement, jointDescIndex, playerCanPerform)",
}
local PERFORM = {
    name = "performAttachment", parameters = 7,
    describe = "(attachment, attacherVehicle, attachable, attachedImplement, attacherJointDescIndex, attachableJointDescIndex, playerCanPerform)",
}

local CANDIDATE_UPDATE = { name = "updateCandidates", parameters = 1, describe = "(handler)" }

local function newState()
    return {
        installed = false,
        waitedMs = 0,
        waitingFor = nil,
        loggedErrors = {},    -- [message] = true for each error of the hitch listing already logged (once each)
        instance = nil,
        handler = nil,
        class = nil,
        hitches = {},
        count = 0,
        selected = nil,
        selectedKind = nil,
        landedVehicle = nil,
        landedJoint = nil,
        landedKind = nil,
    }
end

local state = newState()

local function shapeProblem(owner, ownerName, spec)
    return VRCompanion.GameWraps.shapeProblem(owner[spec.name], spec.parameters, spec.describe, ownerName .. "." .. spec.name)
end

local function findManualAttach()
    local env = engine(MOD_NAME)
    local instance = type(env) == "table" and env.g_manualAttach or engine("g_manualAttach")
    local class = type(env) == "table" and env.VehicleAttachmentHandler or engine("VehicleAttachmentHandler")
    if type(instance) ~= "table" and type(class) ~= "table" then
        local loaded = engine("g_modIsLoaded")
        if type(loaded) ~= "table" or loaded[MOD_NAME] ~= true then return nil, nil, nil, "the mod " .. MOD_NAME .. " is not loaded" end
    end
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
    if type(handler.playerCanPerformManualAttachment) ~= "boolean" then
        return nil, nil, nil, "the attachment handler has no playerCanPerformManualAttachment"
    end
    problem = shapeProblem(handler, "the attachment handler", EXECUTE) or shapeProblem(class, "VehicleAttachmentHandler", CANDIDATE_UPDATE)
    if problem ~= nil then return nil, nil, nil, problem end
    local joint = handler.vehicleJointAttachment
    if type(joint) ~= "table" then return nil, nil, nil, "the attachment handler has no vehicleJointAttachment" end
    problem = shapeProblem(joint, "the joint attachment", ACTION_INFO) or shapeProblem(joint, "the joint attachment", PERFORM)
    if problem ~= nil then return nil, nil, nil, problem end
    for _, entry in ipairs({ { "powerTakeOffAttachment", "the PTO attachment" }, { "connectionHosesAttachment", "the hoses attachment" } }) do
        local attachment = handler[entry[1]]
        if type(attachment) ~= "table" then return nil, nil, nil, "the attachment handler has no " .. entry[1] end
        problem = shapeProblem(attachment, entry[2], ACTION_INFO) or shapeProblem(attachment, entry[2], PERFORM)
        if problem ~= nil then return nil, nil, nil, problem end
    end
    return instance, handler, class, nil
end

local function anyHandTracked(self)
    return self:getHandState(self.MOTION_HAND_LEFT).tracked == true
        or self:getHandState(self.MOTION_HAND_RIGHT).tracked == true
end

local function makeCandidateUpdate(original)
    return function(handler, ...)
        original(handler, ...)
        local selected = state.selected
        if selected ~= nil and handler == state.handler and handler.playerCanPerformManualAttachment == true then
            handler.attacherVehicle, handler.attacherVehicleJointDescIndex = selected.vehicle, selected.jointIndex
            handler.attachable, handler.attachableJointDescIndex, handler.attachedImplement = selected.attachable, selected.attachableJointIndex, selected.implement
        end
    end
end

local wraps = VRCompanion.GameWraps.newSet({
    owner = "manualAttach",
    giveUp = "Manual Attach support disabled: %s. Motion Controllers cannot work Manual Attach's Coupling Points; Manual Attach is unchanged.",
    nameAsSubject = true,
    functions = {
        updateCandidates = {
            class = function() return state.class end, className = "VehicleAttachmentHandler",
            parameters = CANDIDATE_UPDATE.parameters, describe = CANDIDATE_UPDATE.describe, make = makeCandidateUpdate,
        },
    },
})

local function tryInstall(dt)
    local instance, handler, class, problem, mayAppear = findManualAttach()
    if problem ~= nil then
        if mayAppear then
            state.waitedMs = state.waitedMs + (tonumber(dt) or 0)
            if state.waitedMs < WAIT_FOR_OBJECTS_MS then
                state.waitingFor = problem
                return
            end
        end
        wraps:giveUp(problem)
        return
    end
    state.instance, state.handler, state.class = instance, handler, class
    wraps:wrapAll()
    state.installed = true
    print("[FS25_VR] Manual Attach support: installed")
end

function VRCompanion:resetManualAttachSupport()
    wraps:reset()
    state = newState()
end

VRCompanion:registerSubsystem("manualAttach", {
    mapUnload = function(self) self:resetManualAttachSupport() end,
})

local localPlayer = VRCompanion.getLocalPlayer

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
    return hitch
end

local function isInReach(joint, playerRoot)
    local localToLocal = engine("localToLocal")
    if type(localToLocal) ~= "function" or joint.jointTransform == nil or joint.jointTransform == 0
        or playerRoot == nil or playerRoot == 0 then
        return false
    end
    local exists = engine("entityExists")
    if type(exists) == "function" and (not exists(joint.jointTransform) or not exists(playerRoot)) then
        return false
    end
    local ok, x, y, z = pcall(localToLocal, joint.jointTransform, playerRoot, 0, 0, 0)
    if not ok or type(x) ~= "number" or type(y) ~= "number" or type(z) ~= "number" then
        return false
    end
    return x * x + y * y + z * z < state.class.PLAYER_MIN_DISTANCE_SQ
end

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

local function isPointable(attachment, hitch, canPerform)
    local ok, info = pcall(attachment.getActionEventInfo, attachment, hitch.vehicle, hitch.attachable, hitch.implement, hitch.jointIndex, canPerform)
    return ok and type(info) == "table" and info.visibility == true
end

-- The Coupling Point nearest the centre of the ray so far in this update (its angle's cosine, the hitch it belongs to, which of the three it
-- is, and its world position), reused each update.
local pick = { cos = 0, hitch = nil, kind = nil, x = 0, y = 0, z = 0 }

local function consider(hitch, kind, node, ray, getWorldTranslation)
    if node == nil or node == 0 or type(getWorldTranslation) ~= "function" then return end
    local exists = engine("entityExists")
    if type(exists) == "function" and not exists(node) then return end
    local ok, px, py, pz = pcall(getWorldTranslation, node)
    if not ok or type(px) ~= "number" or type(py) ~= "number" or type(pz) ~= "number" then return end
    local vx, vy, vz = px - ray.ox, py - ray.oy, pz - ray.oz
    local distance = math.sqrt(vx * vx + vy * vy + vz * vz)
    if distance <= 0.01 then return end
    local cos = (vx * ray.dx + vy * ray.dy + vz * ray.dz) / distance
    if cos >= pick.cos then
        pick.cos, pick.hitch, pick.kind = cos, hitch, kind
        pick.x, pick.y, pick.z = px, py, pz
    end
end

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

local function selectFromRay(self, ray, canPerform)
    local getWorldTranslation = engine("getWorldTranslation")
    pick.cos, pick.hitch, pick.kind = math.cos(math.rad(self.pointerConeDegrees)), nil, nil
    for i = 1, state.count do
        local hitch = state.hitches[i]
        if isPointable(state.handler.vehicleJointAttachment, hitch, canPerform) then
            consider(hitch, "hitch", hitch.joint.jointTransform, ray, getWorldTranslation)
        end
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

local function work(hitch, kind)
    local handler = state.handler
    handler.attacherVehicle, handler.attacherVehicleJointDescIndex = hitch.vehicle, hitch.jointIndex
    handler.attachable, handler.attachableJointDescIndex, handler.attachedImplement = hitch.attachable, hitch.attachableJointIndex, hitch.implement
    local attachment = handler.vehicleJointAttachment
    if kind == "pto" then attachment = handler.powerTakeOffAttachment
    elseif kind == "hoses" then attachment = handler.connectionHosesAttachment end
    return (pcall(handler.executeAttachment, handler, attachment))
end

local function feel(self, kind, hitch)
    self:onClickPointEvent(kind, hitch)
end

local function rayMayWork(self)
    if self.motionControllersEnabled == false then return false end
    if not self:isPointerRayInControl() or self:getPointerRay() == nil then return false end
    return not self:doesMenuTakeInput()
end

-- Called by the Pointer Ray code once per Companion Mod update, after the ray is placed. Returns whether the ray is on a Coupling Point that
-- can be worked and, then, where it ends. `suppressed`: nothing is selected. `clickCos`: the cosine of the angle from the ray's centre of the
-- Click Point Interactive Control would select; it keeps the ray when it is as near the centre as the Coupling Point or nearer.
function VRCompanion:updateManualAttach(dt, suppressed, clickCos)
    if not state.installed then
        if wraps:problem() ~= nil or self.motionControllersEnabled == false or not anyHandTracked(self) then return false end
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
        state.installed, state.selected, state.selectedKind, state.count = false, nil, nil, 0
        state.landedVehicle, state.landedJoint, state.landedKind = nil, nil, nil
        wraps:giveUp(problem)
        return false
    end
    if not collected then
        state.count = 0
        local message = tostring(problem)
        if not state.loggedErrors[message] then
            state.loggedErrors[message] = true
            print("[FS25_VR] [Warning] Manual Attach support error while looking for hitches (nothing is selected until it passes): " .. message)
        end
    end
    if collected then selected, kind = selectFromRay(self, self:getPointerRay(), canPerform) end
    if selected ~= nil and clickCos ~= nil and clickCos >= selected.cos - TIE_COSINE then selected, kind = nil, nil end
    state.selected, state.selectedKind = selected, kind
    local vehicle, joint = selected and selected.vehicle or nil, selected and selected.jointIndex or nil
    if vehicle ~= state.landedVehicle or joint ~= state.landedJoint or kind ~= state.landedKind then
        state.landedVehicle, state.landedJoint, state.landedKind = vehicle, joint, kind
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
        if pressed and work(selected, kind) then
            state.selected, state.selectedKind = nil, nil
            feel(self, "clicked", selected)
        end
    end
    return true, selected.x, selected.y, selected.z
end

function VRCompanion:getManualAttachDiagnostics()
    if wraps:problem() ~= nil then return "Manual Attach support: gave up (" .. wraps:problem() .. ")" end
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
        local which = state.selectedKind == "pto" and "PTO stub of " or state.selectedKind == "hoses" and "hose coupler of " or ""
        text = string.format("%shitch %s of %s", which, tostring(selected.jointIndex), name or "a vehicle")
    end
    return string.format("Manual Attach support: installed | hitches in reach: %d | selected: %s", state.count, text)
end
