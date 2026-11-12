-- Manual Attach (ADR-0035)
-- StubManualAttach.install(variant)   defines what Manual Attach has, in its mod environment (_G.FS25_manualAttach, with g_modIsLoaded):
--   g_manualAttach (canPlayerPerformManualAttachment, vehicleAttachmentHandler), the VehicleAttachmentHandler class (the constants
--   MAX_ATTACH_DISTANCE_SQ, MAX_ATTACH_ANGLE and PLAYER_MIN_DISTANCE_SQ; the static getAttachableInJointRange and isVehicleValid; the instance
--   methods updateCandidates, executeAttachment and getTargetImplement, and the five candidate fields), and the three attachments of the handler
--   (vehicleJointAttachment, powerTakeOffAttachment, connectionHosesAttachment, each with getActionEventInfo and performAttachment). The
--   mission's vehicleSystem.inputAttacherJoints, accessHandler:canFarmAccess and showBlinkingWarning are the game's.
--   variant   how Manual Attach is in this "release": nil / "ok", "globalFallback" (g_manualAttach and the class are plain globals, not in the
--   mod environment), "notLoaded" (the mod is not loaded), "instanceMissing" (no g_manualAttach), "handlerMissing" (no attachment handler on it),
--   "classMissing" (no VehicleAttachmentHandler class), "rangeHelperMissing" / "rangeHelperReshaped" (getAttachableInJointRange renamed / taking
--   fewer parameters), "validHelperMissing" / "validHelperReshaped" (isVehicleValid), "constantsMissing" (PLAYER_MIN_DISTANCE_SQ gone),
--   "actionInfoMissing" / "actionInfoReshaped" (the joint attachment's getActionEventInfo), "executeMissing" / "executeReshaped"
--   (executeAttachment), "performMissing" (the joint attachment's performAttachment), "detectedMissing" (the handler has no detectedVehicles),
--   "handlerFlagMissing" (the handler has no playerCanPerformManualAttachment, which its candidate update reads),
--   "ptoActionInfoMissing" / "ptoActionInfoReshaped" / "ptoPerformMissing" and "hoseActionInfoMissing" / "hoseActionInfoReshaped" /
--   "hosePerformMissing" (the same of the PTO and the hoses attachments), "ptoLookupReshaped" (the game's vehicles have the PTO specialization
--   without getOutputPowerTakeOffs), "hoseTargetsReshaped" (the game's vehicles have spec_connectionHoses without targetNodes).
-- StubManualAttach.tick()   Manual Attach's update for one game frame: the flag is true when the player is on foot, has no Hand Tool in hand and
--   the hands hold nothing; the handler's candidate update then picks its nearest candidates (as its own does).
-- StubManualAttach.detect(...) / detectAll()   the vehicles Manual Attach's detection has found (the handler's detectedVehicles).
-- StubManualAttach.performs   every performAttachment of an attachment, in order: { kind ("joint", "pto", "hoses"), attacherVehicle, jointIndex,
--   attachable, attachedImplement }. StubManualAttach.warnings   every blinking warning, as the text key (info_lower_warning,
--   info_detach_pto_warning, info_detach_hoses_warning). StubManualAttach.performCount(kind)
-- The vehicles (manual_attach_vehicles.luau): vehicle(name, joints, options), tractor(options), trailer(options), implement(name, options), lineUp, attachTo, ...

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
        -- getContextInfo: the text of the context box, the name of the Implement the prompts are about
        function Joint:getContextInfo(attacherVehicle, attachable, attachedImplement, jointIndex, playerCan)
            local target = attachable or attachedImplement
            if target == nil then return nil, nil end
            return target:getName(), 1
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
        local function updateCandidates(self)
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
        if variant == "candidateUpdateMissing" then Handler.updateNearest = updateCandidates
        elseif variant == "candidateUpdateReshaped" then Handler.updateCandidates = function() end
        else Handler.updateCandidates = updateCandidates end
        S.originalCandidateUpdate = Handler.updateCandidates
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
        if variant == "handlerFlagMissing" then handler.playerCanPerformManualAttachment = nil end
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
        if S.variant ~= "handlerFlagMissing" then handler.playerCanPerformManualAttachment = can end
        for _, joint in ipairs(S.inputJoints) do
            local x, y, z = getWorldTranslation(joint.node)
            joint.translation[1], joint.translation[2], joint.translation[3] = x, y, z
        end
        if handler.updateCandidates ~= nil then handler:updateCandidates() end
        S.drawPrompts()
    end

    -- VehicleAttachmentHandler:draw: the input help prompt of each attachment ({ text, visible } by kind: joint, pto, hoses) and the context box
    -- (the text of the last attachment that has one), for the candidate fields as the update left them.
    function S.drawPrompts()
        local handler = S.handler
        S.prompts, S.contextBox = {}, nil
        for kind, attachment in pairs({ joint = handler.vehicleJointAttachment, pto = handler.powerTakeOffAttachment, hoses = handler.connectionHosesAttachment }) do
            local args = { handler.attacherVehicle, handler.attachable, handler:getTargetImplement(), handler.attacherVehicleJointDescIndex,
                handler.playerCanPerformManualAttachment }
            if type(attachment.getActionEventInfo) == "function" then
                local ok, info = pcall(attachment.getActionEventInfo, attachment, table.unpack(args, 1, 5))
                if ok and type(info) == "table" then S.prompts[kind] = { text = info.text, visible = info.visibility == true } end
            end
            if handler.playerCanPerformManualAttachment and type(attachment.getContextInfo) == "function" then
                local ok, text = pcall(attachment.getContextInfo, attachment, table.unpack(args, 1, 5))
                if ok and text ~= nil then S.contextBox = text end
            end
        end
    end

    -- Manual Attach's own key: the handler's execute path for an attachment (joint, pto, hoses), on whatever its candidate fields are now
    function S.pressKey(kind)
        local handler = S.handler
        local attachment = ({ joint = handler.vehicleJointAttachment, pto = handler.powerTakeOffAttachment, hoses = handler.connectionHosesAttachment })[kind]
        handler:executeAttachment(attachment)
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
