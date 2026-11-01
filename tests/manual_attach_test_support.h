// What the Manual Attach tests (tests/test_manual_attach*.cpp, ADR-0035, spec #227, ticket #228) share, on top of animated_object_test_support.h (the
// player on foot with the game's empty hands, the Lua helpers that read the rays, the macros that point a Motion Controller): a stand-in for the
// Manual Attach mod (FS25_manualAttach, release 1.1.0.0, shaped after its src/ files), the vehicles it works on as scene nodes, and a fixture.
// Header-only (inline), in its own namespace; a test file brings it in with `using namespace manual_attach_test;` inside its anonymous namespace.
//
// The stand-in is two chunks (kStubManualAttach, kStubManualAttachVehicles), like the Hand Tools' tool chunks, passed as StartOptions::extraStubs
// (ManualAttachStubs(variant)): they run after the Companion Mod's files are loaded and before the map is, so the mod finds Manual Attach when
// it looks. The vehicles are made after the scene exists. The game of a frame runs StubManualAttach.tick() (Manual Attach's own update: the
// "can attach by hand" flag from the player, and its handler's candidate update); this header defines HAND_TOOLS_PRE_FRAME_EXTRA_LUA to do so
// (and Interactive Control's update of its Click Points, when a test has them), so it is included FIRST by a test file.
//
// StubManualAttach.install(variant)   defines what Manual Attach has, in its mod environment (_G.FS25_manualAttach, with g_modIsLoaded):
//   g_manualAttach (canPlayerPerformManualAttachment, vehicleAttachmentHandler), the VehicleAttachmentHandler class (the constants
//   MAX_ATTACH_DISTANCE_SQ, MAX_ATTACH_ANGLE and PLAYER_MIN_DISTANCE_SQ; the static getAttachableInJointRange and isVehicleValid; the instance
//   methods updateCandidates, executeAttachment and getTargetImplement, and the five candidate fields), and the three attachments of the handler
//   (vehicleJointAttachment, powerTakeOffAttachment, connectionHosesAttachment, each with getActionEventInfo and performAttachment). The
//   mission's vehicleSystem.inputAttacherJoints, accessHandler:canFarmAccess and showBlinkingWarning are the game's.
//   variant   how Manual Attach is in this "release": nil / "ok", "globalFallback" (g_manualAttach and the class are plain globals, not in the
//   mod environment), "notLoaded" (the mod is not loaded), "instanceMissing" (no g_manualAttach), "handlerMissing" (no attachment handler on it),
//   "classMissing" (no VehicleAttachmentHandler class), "rangeHelperMissing" / "rangeHelperReshaped" (getAttachableInJointRange renamed / taking
//   fewer parameters), "validHelperMissing" / "validHelperReshaped" (isVehicleValid), "constantsMissing" (PLAYER_MIN_DISTANCE_SQ gone),
//   "actionInfoMissing" / "actionInfoReshaped" (the joint attachment's getActionEventInfo), "executeMissing" / "executeReshaped"
//   (executeAttachment), "performMissing" (the joint attachment's performAttachment), "detectedMissing" (the handler has no detectedVehicles),
//   "ptoActionInfoMissing" / "ptoActionInfoReshaped" / "ptoPerformMissing" and "hoseActionInfoMissing" / "hoseActionInfoReshaped" /
//   "hosePerformMissing" (the same of the PTO and the hoses attachments), "ptoLookupReshaped" (the game's vehicles have the PTO specialization
//   without getOutputPowerTakeOffs), "hoseTargetsReshaped" (the game's vehicles have spec_connectionHoses without targetNodes).
// StubManualAttach.tick()   Manual Attach's update for one game frame: the flag is true when the player is on foot, has no Hand Tool in hand and
//   the hands hold nothing; the handler's candidate update then picks its nearest candidates (as its own does).
// StubManualAttach.detect(...) / detectAll()   the vehicles Manual Attach's detection has found (the handler's detectedVehicles).
// StubManualAttach.performs   every performAttachment of an attachment, in order: { kind ("joint", "pto", "hoses"), attacherVehicle, jointIndex,
//   attachable, attachedImplement }. StubManualAttach.warnings   every blinking warning, as the text key (info_lower_warning,
//   info_detach_pto_warning, info_detach_hoses_warning). StubManualAttach.performCount(kind)
// The vehicles (below): vehicle(name, joints, options), tractor(options), trailer(options), implement(name, options), lineUp, attachTo, ...

#pragma once

// What the game does each frame before the Companion Mod's update: Interactive Control's update of its Click Points (when a test has them) and
// Manual Attach's own update.
#ifndef HAND_TOOLS_PRE_FRAME_EXTRA_LUA
#define HAND_TOOLS_PRE_FRAME_EXTRA_LUA \
    "if StubIC and StubIC.tick then StubIC.tick() end; if StubManualAttach and StubManualAttach.tick then StubManualAttach.tick() end"
#endif

#include "animated_object_test_support.h"
#include <string>

inline constexpr const char* kStubManualAttach = R"luau(
    StubManualAttach = {}
    local S = StubManualAttach

    function S.install(variant)
        variant = variant or "ok"
        S.variant = variant
        S.performs, S.warnings, S.vehicles, S.inputJoints, S.detectedList = {}, {}, {}, {}, {}

        _G.g_currentMission = _G.g_currentMission or {}
        local mission = _G.g_currentMission
        mission.vehicleSystem = { inputAttacherJoints = S.inputJoints }
        mission.accessHandler = { canFarmAccess = function(_, farm, object) return object.farmId == nil or object.farmId == farm end }
        mission.showBlinkingWarning = function(_, text, ms) table.insert(S.warnings, text) end

        -- ManualAttach.shouldHandleJoint (ManualAttach.lua): the stand-in's joints are all manual ones
        local function shouldHandleJoint(vehicle, object, jointIndex, playerCan, canToggle)
            if vehicle == nil or object == nil then return false end
            local jointDesc
            if jointIndex ~= nil then jointDesc = vehicle:getAttacherJointByJointDescIndex(jointIndex)
            else jointDesc = vehicle:getAttacherJointDescFromObject(object) end
            if jointDesc == nil then return false end
            return (jointDesc.isManual ~= false) == playerCan
        end

        -- ---- the joint attachment (VehicleJointAttachment.lua)
        local Joint = {}
        Joint.__index = Joint
        function Joint:canPerformAttachment(attacherVehicle, attachable, jointDescIndex)
            if attacherVehicle == nil or attachable == nil then return false end
            return mission.accessHandler:canFarmAccess(attacherVehicle:getActiveFarm(), attachable)
        end
        function Joint:canPerformDetachment(implement)
            if implement == nil or implement.getAttacherVehicle == nil then return false end
            if implement:getAttacherVehicle() == nil then return false end
            return implement:isDetachAllowed()
        end
        local function detachImplement(attacherVehicle, implement)
            local jointDesc = attacherVehicle:getAttacherJointDescFromObject(implement)
            local warning = nil
            if implement.allowsLowering and jointDesc.allowsLowering and not jointDesc.moveDown then warning = "info_lower_warning" end
            if warning == nil and implement.ptoConnected then warning = "info_detach_pto_warning" end
            if warning == nil and implement.hosesConnected then warning = "info_detach_hoses_warning" end
            if warning == nil then S.detach(attacherVehicle, implement) else mission:showBlinkingWarning(warning, 2000) end
        end
        local function performJoint(self, attacherVehicle, attachable, attachedImplement, attacherJointDescIndex, attachableJointDescIndex, playerCan)
            table.insert(S.performs, { kind = "joint", attacherVehicle = attacherVehicle, jointIndex = attacherJointDescIndex,
                attachable = attachable, attachedImplement = attachedImplement })
            if attacherVehicle ~= nil and attachable ~= nil and self:canPerformAttachment(attacherVehicle, attachable, attacherJointDescIndex)
                and shouldHandleJoint(attacherVehicle, attachable, attacherJointDescIndex, playerCan) then
                local jointDesc = attacherVehicle.spec_attacherJoints.attacherJoints[attacherJointDescIndex]
                if jointDesc ~= nil and jointDesc.jointIndex == 0 then S.attach(attacherVehicle, attachable, attachableJointDescIndex, attacherJointDescIndex) end
                return
            end
            if attachedImplement == nil or attachedImplement.getAttacherVehicle == nil then return end
            local detachFrom = attachedImplement:getAttacherVehicle()
            if detachFrom == nil or not self:canPerformDetachment(attachedImplement)
                or not shouldHandleJoint(detachFrom, attachedImplement, nil, playerCan) then
                return
            end
            detachImplement(detachFrom, attachedImplement)
        end
        local function actionInfo(self, attacherVehicle, attachable, attachedImplement, jointIndex, playerCan)
            local info = { id = 1, text = nil, priority = 1, visibility = false, isExtraInfo = false }
            if self:canPerformAttachment(attacherVehicle, attachable, jointIndex) and shouldHandleJoint(attacherVehicle, attachable, jointIndex, playerCan) then
                info.text, info.visibility = "Attach", true
                return info
            end
            if self:canPerformDetachment(attachedImplement) then
                if shouldHandleJoint(attachedImplement:getAttacherVehicle(), attachedImplement, nil, playerCan) then
                    info.text, info.visibility = "Detach", true
                end
            end
            return info
        end
        if variant == "performMissing" then Joint.perform = performJoint else Joint.performAttachment = performJoint end
        if variant == "actionInfoMissing" then Joint.getActionInfo = actionInfo
        elseif variant == "actionInfoReshaped" then Joint.getActionEventInfo = function(self) return { visibility = true } end
        else Joint.getActionEventInfo = actionInfo end

        -- ---- the PTO and hoses attachments (PowerTakeOffAttachment.lua, ConnectionHosesAttachment.lua): they act on the attached Implement
        -- alone (its attacher vehicle is where the PTO and hoses go), as the real ones do: a connect when it can be connected, else a disconnect.
        -- The Implement has a PTO or hoses when it says so (hasPto, hasHoses) and its vehicle has the outputs or targets for them.
        -- `prefix` names the variants: <prefix>PerformMissing, <prefix>ActionInfoMissing, <prefix>ActionInfoReshaped.
        local function other(kind, prefix)
            local connectedKey, hasKey = kind == "pto" and "ptoConnected" or "hosesConnected", kind == "pto" and "hasPto" or "hasHoses"
            local attachment = {}
            local function vehicleSide(vehicle)
                if kind == "pto" then return vehicle.getOutputPowerTakeOffs ~= nil and #vehicle:getOutputPowerTakeOffs() > 0 end
                return vehicle.spec_connectionHoses ~= nil
            end
            local function canConnect(attacherVehicle, implement)
                return attacherVehicle ~= nil and implement ~= nil and implement[hasKey] == true and vehicleSide(attacherVehicle) and not implement[connectedKey]
            end
            local function canDisconnect(attacherVehicle, implement)
                return attacherVehicle ~= nil and implement ~= nil and implement[connectedKey] == true
            end
            local function perform(self, av, at, impl, ji, aji, playerCan)
                table.insert(S.performs, { kind = kind, attacherVehicle = av, jointIndex = ji, attachable = at, attachedImplement = impl })
                if impl == nil or not playerCan then return end
                local attacherVehicle = impl:getAttacherVehicle()
                if canConnect(attacherVehicle, impl) then impl[connectedKey] = true
                elseif canDisconnect(attacherVehicle, impl) then impl[connectedKey] = false end
            end
            local function info(self, av, at, impl, ji, playerCan)
                local result = { id = 2, text = nil, priority = 1, visibility = false, isExtraInfo = kind == "hoses" }
                if impl == nil or not playerCan then return result end
                local attacherVehicle = impl:getAttacherVehicle()
                if canConnect(attacherVehicle, impl) then result.text, result.visibility = "Connect", true
                elseif canDisconnect(attacherVehicle, impl) then result.text, result.visibility = "Disconnect", true end
                return result
            end
            if variant == prefix .. "PerformMissing" then attachment.perform = perform else attachment.performAttachment = perform end
            if variant == prefix .. "ActionInfoMissing" then attachment.getActionInfo = info
            elseif variant == prefix .. "ActionInfoReshaped" then attachment.getActionEventInfo = function(self) return { visibility = true } end
            else attachment.getActionEventInfo = info end
            return attachment
        end

        -- ---- the handler (VehicleAttachmentHandler.lua)
        local Handler = {}
        Handler.__index = Handler
        Handler.MAX_ATTACH_DISTANCE_SQ = 0.7 * 0.7
        Handler.MAX_ATTACH_ANGLE = 0.34202
        if variant ~= "constantsMissing" then Handler.PLAYER_MIN_DISTANCE_SQ = 8 end
        local function isVehicleValid(vehicle) return not vehicle.isDeleted and vehicle.rootNode ~= nil and entityExists(vehicle.rootNode) end
        if variant == "validHelperMissing" then Handler.isVehicleLive = isVehicleValid
        elseif variant == "validHelperReshaped" then Handler.isVehicleValid = function() return true end
        else Handler.isVehicleValid = isVehicleValid end

        -- getAttachableInJointRange: the closest input joint of an Implement in range, lined up with the hitch (the angle test is the joint's alignment)
        local function inJointRange(vehicleSystem, vehicle, attacherJoint, maxDistanceSq, maxAngle, isPlayerBased)
            local found, foundIndex, minDist = nil, nil, math.huge
            local x, y, z = getWorldTranslation(attacherJoint.jointTransform)
            for _, info in ipairs(vehicleSystem.inputAttacherJoints) do
                local candidate = info.vehicle
                if candidate ~= vehicle and attacherJoint.jointType == info.jointType and candidate.activeInputJoint == nil then
                    local dx, dz, dy = x - info.translation[1], z - info.translation[3], y - info.translation[2]
                    local distSq = dx * dx + dz * dz
                    if distSq < maxDistanceSq and distSq < minDist and dy * dy < maxDistanceSq * 4 and info.alignment > maxAngle then
                        minDist, found, foundIndex = distSq, candidate, info.jointIndex
                    end
                end
            end
            return found, foundIndex
        end
        if variant == "rangeHelperMissing" then Handler.getAttachableInRange = inJointRange
        elseif variant == "rangeHelperReshaped" then Handler.getAttachableInJointRange = function(a) return nil, nil end
        else Handler.getAttachableInJointRange = inJointRange end

        -- getCandidatesInAttachRange for the player: the nearest hitch with an Implement lined up, or the nearest attached Implement
        function Handler:updateCandidates()
            if #S.detectedList == 0 then return end
            local player = self.playerCanPerformManualAttachment and _G.g_localPlayer or nil
            if player == nil then
                self.attacherVehicle, self.attacherVehicleJointDescIndex, self.attachable, self.attachableJointDescIndex, self.attachedImplement = nil, nil, nil, nil, nil
                return
            end
            local av, aji, at, atji, impl = nil, nil, nil, nil, nil
            local minDist, minImplDist = math.huge, math.huge
            for _, vehicle in ipairs(S.detectedList) do
                local spec = vehicle.spec_attacherJoints
                if isVehicleValid(vehicle) and spec ~= nil then
                    for _, implement in ipairs(vehicle:getAttachedImplements()) do
                        local x, y, z = localToLocal(spec.attacherJoints[implement.jointDescIndex].jointTransform, player.rootNode, 0, 0, 0)
                        local distSq = x * x + y * y + z * z
                        if distSq < (Handler.PLAYER_MIN_DISTANCE_SQ or 8) and distSq < minImplDist then impl, minImplDist = implement.object, distSq end
                    end
                    for k, joint in ipairs(spec.attacherJoints) do
                        if joint.jointIndex == 0 then
                            local x, y, z = localToLocal(joint.jointTransform, player.rootNode, 0, 0, 0)
                            local distSq = x * x + y * y + z * z
                            if distSq < (Handler.PLAYER_MIN_DISTANCE_SQ or 8) and distSq < minDist then
                                local found, foundIndex = inJointRange(mission.vehicleSystem, vehicle, joint,
                                    Handler.MAX_ATTACH_DISTANCE_SQ, Handler.MAX_ATTACH_ANGLE, true)
                                if found ~= nil then av, aji, at, atji, minDist = vehicle, k, found, foundIndex, distSq end
                            end
                        end
                    end
                end
            end
            self.attacherVehicle, self.attacherVehicleJointDescIndex, self.attachable, self.attachableJointDescIndex, self.attachedImplement = av, aji, at, atji, impl
        end
        function Handler:getTargetImplement() return self.attachedImplement end
        local function execute(self, attachment)
            attachment:performAttachment(self.attacherVehicle, self.attachable, self:getTargetImplement(), self.attacherVehicleJointDescIndex,
                self.attachableJointDescIndex, self.playerCanPerformManualAttachment)
        end
        if variant == "executeMissing" then Handler.executeAttachmentNow = execute
        elseif variant == "executeReshaped" then Handler.executeAttachment = function(self) execute(self, self.vehicleJointAttachment) end
        else Handler.executeAttachment = execute end

        local handler = setmetatable({ detectedVehicles = {}, playerCanPerformManualAttachment = false }, Handler)
        S.detectedList = S.detectedList or {}
        handler.detectedVehicles = S.detectedList
        if variant == "detectedMissing" then handler.detectedVehicles = nil end
        handler.vehicleJointAttachment = setmetatable({}, Joint)
        handler.powerTakeOffAttachment = other("pto", "pto")
        handler.connectionHosesAttachment = other("hoses", "hose")
        handler.attachments = { handler.vehicleJointAttachment, handler.powerTakeOffAttachment, handler.connectionHosesAttachment }
        S.handler, S.Handler = handler, Handler

        -- ---- the mod (ManualAttach.lua, init.lua)
        local instance = { canPlayerPerformManualAttachment = false, vehicleAttachmentHandler = handler }
        if variant == "handlerMissing" then instance.vehicleAttachmentHandler = nil end
        if variant == "flagMissing" then instance.canPlayerPerformManualAttachment = nil end
        S.instance = instance

        local env = { g_manualAttach = instance, VehicleAttachmentHandler = Handler }
        if variant == "instanceMissing" then env.g_manualAttach = nil end
        if variant == "classMissing" then env.VehicleAttachmentHandler = nil end
        _G.g_modIsLoaded = _G.g_modIsLoaded or {}
        if variant == "notLoaded" then
            _G.g_modIsLoaded["FS25_manualAttach"], _G.FS25_manualAttach = nil, nil
        elseif variant == "globalFallback" then
            _G.g_modIsLoaded["FS25_manualAttach"], _G.FS25_manualAttach = true, {}
            _G.g_manualAttach, _G.VehicleAttachmentHandler = env.g_manualAttach, env.VehicleAttachmentHandler
        else
            _G.g_modIsLoaded["FS25_manualAttach"], _G.FS25_manualAttach = true, env
        end
    end

    -- ManualAttach:update for one game frame
    function S.tick()
        local instance, handler = S.instance, S.handler
        if instance == nil then return end
        local player = _G.g_localPlayer
        if player ~= nil and player.rootNode == nil and _G.foot ~= nil then player.rootNode = _G.foot.parent end
        local hands = StubHandTool.hands_
        local can = player ~= nil and _G.g_currentMission.controlledVehicle == nil and player.currentHandTool == nil
            and not (hands ~= nil and hands.spec_hands.heldItemNode ~= nil)
        if S.variant ~= "flagMissing" then instance.canPlayerPerformManualAttachment = can end
        handler.playerCanPerformManualAttachment = can
        for _, joint in ipairs(S.inputJoints) do
            local x, y, z = getWorldTranslation(joint.node)
            joint.translation[1], joint.translation[2], joint.translation[3] = x, y, z
        end
        handler:updateCandidates()
    end

    function S.detect(...)
        local list = { ... }
        local detected = S.detectedList
        for i = #detected, 1, -1 do detected[i] = nil end
        for _, vehicle in ipairs(list) do table.insert(detected, vehicle) end
    end
    function S.detectAll() S.detect(table.unpack(S.vehicles)) end
    function S.performCount(kind)
        local count = 0
        for _, perform in ipairs(S.performs) do
            if perform.kind == kind then count = count + 1 end
        end
        return count
    end
)luau";

// The vehicles Manual Attach works on, as scene nodes (shaped after the game's AttacherJoints and Attachable): a vehicle with attacher
// joints, an Implement with an input attacher joint, and what lines up and attaches them.
//
// StubManualAttach.vehicle(name, joints, options) -> vehicle   a vehicle with an attacher joint at each world position { x, y, z } of `joints` (index
//   1.. in order). options: farmId (default 1). vehicle.rootNode, vehicle.spec_attacherJoints.attacherJoints[i] = { jointTransform, jointType,
//   jointIndex (0 free, else attached), allowsLowering, moveDown }, getAttachedImplements() -> { { object, jointDescIndex } }, getAttacherVehicle(),
//   getAttacherJointByJointDescIndex(i), getAttacherJointDescFromObject(object), getActiveFarm(), isDetachAllowed(), getFullName(). It is also an
//   Implement: its own input attacher joint is at its root (a trailer is one: StubManualAttach.trailer).
// StubManualAttach.tractor(options) -> vehicle   joint 1 is the rear hitch at options.rear (default -0.4, -0.2, 2.0), joint 2 the front one at
//   options.front (default 0.8, -0.2, 2.0). It has the vehicle side of the PTO and the hoses for each joint (options.pto, options.hoses, both
//   on by default; options.passThroughPto: PTO outputs with no node of their own instead): a PTO output with an output node 0.4 m above the
//   joint and 0.3 m behind it (getOutputPowerTakeOffs(), spec_powerTakeOffs.outputPowerTakeOffs), a hose target node 0.35 m to the joint's +X
//   and a custom hose target 0.35 m to its -X, both 0.15 m above and 0.3 m behind (spec_connectionHoses.targetNodes,
//   customHoseTargetsByAttacher). Any vehicle() takes these options too.
// StubManualAttach.implement(name, options) -> vehicle   an Implement with no hitch of its own, 20 m away from everything until lined up.
//   options: farmId, allowsLowering (default true), hasPto, hasHoses (the Implement has a PTO / hoses; default no). The PTO and hoses attachments
//   connect or disconnect them (implement.ptoConnected, implement.hosesConnected) when the Implement is attached and its vehicle has the
//   outputs or targets, and Manual Attach refuses to detach while they are connected.
// StubManualAttach.trailer(name, hitch, options) -> vehicle   an Implement with a hitch of its own at the world position `hitch`.
// StubManualAttach.lineUp(implement, vehicle, jointIndex)   the Implement's input joint is at the hitch, lined up. misalign(implement): turned across it.
// StubManualAttach.attach(vehicle, implement, inputJointIndex, jointIndex) / detach(vehicle, implement)   Manual Attach's own attach and detach of the
//   state (the stand-in's perform path calls them): the hitch is taken, the Implement is lowered when it allows lowering.
// StubManualAttach.raise(implement)   the hitch of an attached Implement is raised (not lowered): Manual Attach refuses to detach it.
inline constexpr const char* kStubManualAttachVehicles = R"luau(
    local S = StubManualAttach

    local function newVehicle(name, joints, options)
        options = options or {}
        local rootX, rootY, rootZ = 0, 0, 0
        for _, p in ipairs(joints) do rootX, rootY, rootZ = rootX + p[1], rootY + p[2], rootZ + p[3] end
        local count = math.max(1, #joints)
        local vehicle = { name = name, farmId = options.farmId, allowsLowering = options.allowsLowering ~= false, attachedImplements = {} }
        vehicle.rootNode = StubScene.newNode(name, 0, rootX / count, rootY / count, rootZ / count)
        vehicle.spec_attacherJoints = { attacherJoints = {} }
        for i, p in ipairs(joints) do
            table.insert(vehicle.spec_attacherJoints.attacherJoints, {
                jointTransform = StubScene.newNode(name .. "Hitch" .. i, 0, p[1], p[2], p[3]),
                jointType = 1, jointIndex = 0, allowsLowering = true, moveDown = false,
            })
        end
        -- the vehicle side of the PTO and the hoses (PowerTakeOffs.lua, ConnectionHoses.lua), per attacher joint, beside it on the tractor's side:
        -- a PTO output with an output node 0.4 m above the joint and 0.3 m behind it, a hose target node 0.35 m to its +X and a custom hose target
        -- 0.35 m to its -X (both 0.15 m above and 0.3 m behind). options.pto / options.hoses: have them; options.passThroughPto: PTO outputs
        -- with no node of their own that skip to the vehicle's own attacher vehicle (a trailer passing the tractor's PTO on).
        if options.pto or options.passThroughPto then
            vehicle.spec_powerTakeOffs = { outputPowerTakeOffs = {} }
            for i, p in ipairs(joints) do
                local output = { attacherJointIndices = { [i] = true }, ptoName = "DEFAULT_PTO" }
                if options.passThroughPto then output.skipToInputAttacherIndex = 1
                else output.outputNode = StubScene.newNode(name .. "PtoOutput" .. i, 0, p[1], p[2] + 0.4, p[3] - 0.3) end
                table.insert(vehicle.spec_powerTakeOffs.outputPowerTakeOffs, output)
            end
            if S.variant ~= "ptoLookupReshaped" then
                function vehicle:getOutputPowerTakeOffs() return self.spec_powerTakeOffs.outputPowerTakeOffs end
            end
        end
        if options.hoses then
            vehicle.spec_connectionHoses = { targetNodes = {}, customHoseTargetsByAttacher = {} }
            for i, p in ipairs(joints) do
                table.insert(vehicle.spec_connectionHoses.targetNodes, { type = "HYDRAULIC", attacherJointIndices = { [i] = i },
                    node = StubScene.newNode(name .. "HoseTarget" .. i, 0, p[1] + 0.35, p[2] + 0.15, p[3] - 0.3) })
                vehicle.spec_connectionHoses.customHoseTargetsByAttacher[i] = { { type = "CUSTOM", attacherJointIndices = { [i] = i },
                    node = StubScene.newNode(name .. "CustomHoseTarget" .. i, 0, p[1] - 0.35, p[2] + 0.15, p[3] - 0.3) } }
            end
            if S.variant == "hoseTargetsReshaped" then
                vehicle.spec_connectionHoses.targetsByNode, vehicle.spec_connectionHoses.targetNodes = vehicle.spec_connectionHoses.targetNodes, nil
            end
        end
        vehicle.hasPto, vehicle.hasHoses = options.hasPto == true, options.hasHoses == true
        function vehicle:getAttachedImplements() return self.attachedImplements end
        function vehicle:getAttacherVehicle() return self.attacherVehicle end
        function vehicle:getAttacherJointByJointDescIndex(i) return self.spec_attacherJoints.attacherJoints[i] end
        function vehicle:getAttacherJointDescFromObject(object)
            for _, implement in ipairs(self.attachedImplements) do
                if implement.object == object then return self.spec_attacherJoints.attacherJoints[implement.jointDescIndex] end
            end
            return nil
        end
        function vehicle:getActiveFarm() return 1 end
        function vehicle:isDetachAllowed() return true end
        function vehicle:getAllowsLowering() return self.allowsLowering end
        function vehicle:getFullName() return self.name end
        function vehicle:getName() return self.name end
        -- the Implement side: an input attacher joint, 20 m away until lined up
        vehicle.inputNode = StubScene.newNode(name .. "Input", 0, 20, -0.2, 20)
        table.insert(S.inputJoints, { vehicle = vehicle, node = vehicle.inputNode, translation = { 20, -0.2, 20 }, jointType = 1, jointIndex = 1,
            alignment = 1, inputAttacherJoint = { attachAngleLimitAxis = 1 } })
        S.inputJointOf = S.inputJointOf or {}
        S.inputJointOf[vehicle] = S.inputJoints[#S.inputJoints]
        table.insert(S.vehicles, vehicle)
        return vehicle
    end

    function S.vehicle(name, joints, options) return newVehicle(name, joints, options) end
    function S.tractor(options)
        options = options or {}
        local rear, front = options.rear or { -0.4, -0.2, 2.0 }, options.front or { 0.8, -0.2, 2.0 }
        if options.pto == nil then options.pto = not options.passThroughPto end
        if options.hoses == nil then options.hoses = true end
        return newVehicle(options.name or "tractor", { rear, front }, options)
    end
    function S.implement(name, options) return newVehicle(name, {}, options) end
    function S.trailer(name, hitch, options) return newVehicle(name, { hitch }, options) end

    function S.lineUp(implement, vehicle, jointIndex)
        local x, y, z = getWorldTranslation(vehicle.spec_attacherJoints.attacherJoints[jointIndex].jointTransform)
        setTranslation(implement.inputNode, x + 0.1, y, z)
        local joint = S.inputJointOf[implement]
        joint.alignment = 1
        joint.translation[1], joint.translation[2], joint.translation[3] = x + 0.1, y, z
    end
    function S.misalign(implement) S.inputJointOf[implement].alignment = 0 end

    function S.attach(vehicle, implement, inputJointIndex, jointIndex)
        local joint = vehicle.spec_attacherJoints.attacherJoints[jointIndex]
        joint.jointIndex = 1
        joint.moveDown = implement.allowsLowering
        table.insert(vehicle.attachedImplements, { object = implement, jointDescIndex = jointIndex })
        implement.attacherVehicle, implement.activeInputJoint = vehicle, inputJointIndex or 1
    end
    function S.detach(vehicle, implement)
        for i, entry in ipairs(vehicle.attachedImplements) do
            if entry.object == implement then
                vehicle.spec_attacherJoints.attacherJoints[entry.jointDescIndex].jointIndex = 0
                table.remove(vehicle.attachedImplements, i)
                break
            end
        end
        implement.attacherVehicle, implement.activeInputJoint = nil, nil
    end
    function S.raise(implement)
        local vehicleJoint = implement.attacherVehicle:getAttacherJointDescFromObject(implement)
        vehicleJoint.moveDown = false
    end
)luau";

namespace manual_attach_test {

using namespace animated_object_test;

// Manual Attach (kStubManualAttach and its vehicles), for StartOptions::extraStubs. variant: see the header above.
inline std::string ManualAttachStubs(const char* variant = "ok") {
    return std::string(kStubManualAttach) + "\n" + kStubManualAttachVehicles + "\nStubManualAttach.install('" + variant + "')\n";
}

// A hand pointing at tracking (0.4, -0.2, -0.3) has its fingertip at world (-0.4, -0.2, 0.3) and the Pointer Ray runs along +Z (the same
// geometry as the Object Grab and Animated Objects tests): the rear hitch of the stand-in's tractor is straight ahead of it, 1.7 m away.
// A tractor on foot's side, within Manual Attach's reach of the player (who stands at the world origin).
constexpr const char* kTractorAhead = "_G.tractor = StubManualAttach.tractor({}); StubManualAttach.detectAll()";
// The same tractor with a plow lined up at its rear hitch, as _G.tractor and _G.plow.
constexpr const char* kTractorAndImplement = R"luau(
    _G.tractor = StubManualAttach.tractor({})
    _G.plow = StubManualAttach.implement('plow')
    StubManualAttach.lineUp(plow, tractor, 1)
    StubManualAttach.detectAll()
)luau";
// A tractor with an Implement lined up at each hitch: the rear one 2 m ahead (where the ray of kHandX runs), the front one nearer to the player and
// to the side (where the ray of a hand 0.8 m to the side runs, at world -1.2, -0.2, 1.0), so Manual Attach's own nearest candidate is the front one.
// _G.tractor, _G.rearPlow (joint 1), _G.frontLoader (joint 2).
constexpr const char* kFrontAndRear = R"luau(
    _G.tractor = StubManualAttach.tractor({ rear = { -0.4, -0.2, 2.0 }, front = { -1.2, -0.2, 1.0 } })
    _G.rearPlow = StubManualAttach.implement('rearPlow')
    _G.frontLoader = StubManualAttach.implement('frontLoader')
    StubManualAttach.lineUp(rearPlow, tractor, 1)
    StubManualAttach.lineUp(frontLoader, tractor, 2)
    StubManualAttach.detectAll()
)luau";
// Where the ray of the hand at kHandX ends when it is on that rear hitch.
constexpr const char* kRearHitch = "-0.40 -0.20 2.00";
// The same tractor with a plow that has a PTO and hoses already attached at its rear hitch, as _G.tractor and _G.plow (the PTO and the hoses are
// not connected yet).
constexpr const char* kTractorWithAttachedImplement = R"luau(
    _G.tractor = StubManualAttach.tractor({})
    _G.plow = StubManualAttach.implement('plow', { hasPto = true, hasHoses = true })
    StubManualAttach.lineUp(plow, tractor, 1)
    StubManualAttach.attach(tractor, plow, 1, 1)
    StubManualAttach.detectAll()
)luau";
// Where the rays end on the tractor's Coupling Points of the rear hitch other than the hitch: its PTO stub, its hose target and its custom hose
// target. The hand that points at the hitch (kHandX, kHandY) points at these at kHandX / kHandY of kPtoHandY, kHoseHandX / kHoseHandY and
// kCustomHoseHandX / kHoseHandY (the tracking space is the world mirrored in X and Z).
constexpr const char* kRearPtoStub = "-0.40 0.20 1.70";
constexpr const char* kRearHoseCoupler = "-0.05 -0.05 1.70";
constexpr const char* kRearCustomHoseCoupler = "-0.75 -0.05 1.70";
constexpr float kPtoHandY = 0.2f, kHoseHandX = 0.05f, kCustomHoseHandX = 0.75f, kHoseHandY = -0.05f;

// A test of the Pointer Ray working hitches, seen from what Manual Attach's stand-in recorded and what the game reads from the Virtual Gamepad:
// the mod started on a player on foot with the game's empty hands, the Virtual Gamepad on, the game clock set, and Manual Attach in the game.
// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) covers.
class VirtualGamepadManualAttachTest : public VirtualGamepadAnimatedObjectsTest {
protected:
    // The game with Manual Attach ("ok" or a variant).
    void StartWithManualAttach(const char* variant = "ok") {
        stubs_ = std::string(kStubAnimatedObjects) + "\nStubAnimatedObject.install('ok')\n" + ManualAttachStubs(variant);
        StartOptions options;
        options.extraStubs = stubs_.c_str();
        ASSERT_NO_FATAL_FAILURE(Start(options));
        ASSERT_EQ(RunLuauChunk(L, kAnimatedObjectHelpers, "=animatedObjectHelpers"), 0);
    }

    void Lua(const std::string& chunk) { ASSERT_EQ(RunLuauChunk(L, chunk.c_str(), "=test"), 0) << chunk; }

    // How many times Manual Attach's perform path ran for an attachment ("joint", "pto", "hoses"), or in all.
    int Performs(const char* kind) { return static_cast<int>(Number(L, std::string("StubManualAttach.performCount('") + kind + "')")); }
    int Performs() { return static_cast<int>(Number(L, "#StubManualAttach.performs")); }
    int Warnings() { return CountLogLines("Manual Attach support disabled"); }
    int BlinkingWarnings() { return static_cast<int>(Number(L, "#StubManualAttach.warnings")); }
    std::string Attached(const char* implement) { return EvalLua(L, std::string("tostring(") + implement + ".attacherVehicle ~= nil)"); }
    // "true" when the Implement's PTO / hoses are connected.
    std::string PtoConnected(const char* implement) { return EvalLua(L, std::string("tostring(") + implement + ".ptoConnected == true)"); }
    std::string HosesConnected(const char* implement) { return EvalLua(L, std::string("tostring(") + implement + ".hosesConnected == true)"); }

    // The game without Manual Attach at all.
    void StartWithoutManualAttach() {
        stubs_ = std::string(kStubAnimatedObjects) + "\nStubAnimatedObject.install('ok')\n";
        StartOptions options;
        options.extraStubs = stubs_.c_str();
        ASSERT_NO_FATAL_FAILURE(Start(options));
        ASSERT_EQ(RunLuauChunk(L, kAnimatedObjectHelpers, "=animatedObjectHelpers"), 0);
    }

    // Takes note of every function of Manual Attach's class, instance, handler and attachments, and later says whether they are still as found
    // (nothing wrapped, replaced or added).
    void NoteManualAttachFunctions() {
        Lua(R"luau(
            function _G.manualAttachFunctions()
                local list = {}
                local handler = StubManualAttach.handler
                for _, owner in ipairs({ StubManualAttach.Handler, StubManualAttach.instance, handler, handler.vehicleJointAttachment,
                        getmetatable(handler.vehicleJointAttachment), handler.powerTakeOffAttachment, handler.connectionHosesAttachment }) do
                    for key, value in pairs(owner) do
                        if type(value) == "function" then list[tostring(owner) .. key] = value end
                    end
                end
                return list
            end
            _G.functionsBefore = manualAttachFunctions()
        )luau");
    }
    bool ManualAttachFunctionsAreAsFound() {
        return EvalLua(L, R"luau((function()
            local after = manualAttachFunctions()
            for key, value in pairs(functionsBefore) do if after[key] ~= value then return 'false' end end
            for key, value in pairs(after) do if functionsBefore[key] ~= value then return 'false' end end
            return 'true'
        end)())luau") == "true";
    }

private:
    std::string stubs_;
};

}  // namespace manual_attach_test
