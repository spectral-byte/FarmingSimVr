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
