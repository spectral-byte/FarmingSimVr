    local nodes = StubScene.nodes
    StubHandTool.trees, StubHandTool.cuts, StubHandTool.inputLocks = {}, {}, 0

    local function qmul(ax, ay, az, aw, bx, by, bz, bw)
        return aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
               aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz
    end
    local function eulerToQuat(rx, ry, rz)
        local cx, sx = math.cos(rx * 0.5), math.sin(rx * 0.5)
        local cy, sy = math.cos(ry * 0.5), math.sin(ry * 0.5)
        local cz, sz = math.cos(rz * 0.5), math.sin(rz * 0.5)
        local x, y, z, w = qmul(0, sy, 0, cy, sx, 0, 0, cx)
        return qmul(x, y, z, w, 0, 0, sz, cz)
    end
    local function normalize(x, y, z)
        local length = math.sqrt(x * x + y * y + z * z)
        if length < 1e-9 then return 0, 0, 1 end
        return x / length, y / length, z / length
    end
    _G.setWorldRotation = function(node, rx, ry, rz)
        local qx, qy, qz, qw = eulerToQuat(rx, ry, rz)
        local px, py, pz, pw = getWorldQuaternion(getParent(node))
        setQuaternion(node, qmul(-px, -py, -pz, pw, qx, qy, qz, qw))
    end
    _G.setDirection = _G.setDirection or function(node, dx, dy, dz, ux, uy, uz)
        local zx, zy, zz = normalize(dx, dy, dz)
        local xx, xy, xz = normalize(uy * zz - uz * zy, uz * zx - ux * zz, ux * zy - uy * zx)
        local yx, yy, yz = zy * xz - zz * xy, zz * xx - zx * xz, zx * xy - zy * xx
        local trace = xx + yy + zz
        local qx, qy, qz, qw
        if trace > 0 then
            local s = math.sqrt(trace + 1) * 2
            qw, qx, qy, qz = 0.25 * s, (yz - zy) / s, (zx - xz) / s, (xy - yx) / s
        elseif xx > yy and xx > zz then
            local s = math.sqrt(1 + xx - yy - zz) * 2
            qw, qx, qy, qz = (yz - zy) / s, 0.25 * s, (yx + xy) / s, (zx + xz) / s
        elseif yy > zz then
            local s = math.sqrt(1 + yy - xx - zz) * 2
            qw, qx, qy, qz = (zx - xz) / s, (yx + xy) / s, 0.25 * s, (zy + yz) / s
        else
            local s = math.sqrt(1 + zz - xx - yy) * 2
            qw, qx, qy, qz = (xy - yx) / s, (zx + xz) / s, (zy + yz) / s, 0.25 * s
        end
        setQuaternion(node, qx, qy, qz, qw)
    end
    _G.rotateAboutLocalAxis = function(node, angle, ax, ay, az)
        ax, ay, az = normalize(ax, ay, az)
        local s = math.sin(angle * 0.5)
        local qx, qy, qz, qw = getQuaternion(node)
        setQuaternion(node, qmul(qx, qy, qz, qw, ax * s, ay * s, az * s, math.cos(angle * 0.5)))
    end
    _G.localDirectionToLocal = function(from, to, x, y, z)
        local wx, wy, wz = StubScene.worldDirection(from, x, y, z)
        local tx, ty, tz = getWorldTranslation(to)
        return worldToLocal(to, tx + wx, ty + wy, tz + wz)
    end

    local MAX_TARGET_DISTANCE = 3.0

    function StubHandTool.tree(x, z, radius, y0, y1)
        local node = StubScene.newNode("tree", 0, x, y0, z)
        local tree = { node = node, x = x, z = z, radius = radius, y0 = y0, y1 = y1 }
        table.insert(StubHandTool.trees, tree)
        return tree
    end

    function StubHandTool.treeAcross(ray, distance, radius)
        local x, y, z, dx, dy, dz = table.unpack(ray)
        local hx, _, hz = normalize(dx, 0, dz)
        return StubHandTool.tree(x + hx * (distance + radius), z + hz * (distance + radius), radius, y - 10, y + 10)
    end

    -- the closest trunk a ray meets between a minimum and a maximum distance of its origin, as the targeter reports a target:
    -- { node, x, y, z, distance }
    local function castAtTrees(x, y, z, dx, dy, dz, minDistance, maxDistance)
        local best = nil
        for _, tree in ipairs(StubHandTool.trees) do
            local ox, oz = x - tree.x, z - tree.z
            local a = dx * dx + dz * dz
            if a > 1e-9 then
                local b = ox * dx + oz * dz
                local disc = b * b - a * (ox * ox + oz * oz - tree.radius ^ 2)
                if disc >= 0 then
                    local t = (-b - math.sqrt(disc)) / a
                    local hitY = y + dy * t
                    if t >= minDistance and t <= maxDistance and hitY >= tree.y0 and hitY <= tree.y1 and (best == nil or t < best.distance) then
                        best = { node = tree.node, x = x + dx * t, y = hitY, z = z + dz * t, distance = t }
                    end
                end
            end
        end
        return best
    end

    local function treeOfNode(node)
        for _, tree in ipairs(StubHandTool.trees) do
            if tree.node == node then return tree end
        end
        return nil
    end

    _G.testSplitShape = function(treeNode, x, y, z, nx, ny, nz, yx, yy, yz, width, depth)
        local tree = treeOfNode(treeNode)
        if tree == nil then return nil end
        return -tree.radius, tree.radius, -tree.radius, tree.radius
    end

    function StubHandTool.installChainsaw(variant)
        variant = variant or "ok"
        StubHandTool.trees, StubHandTool.cuts, StubHandTool.inputLocks = {}, {}, 0
        local State = { IDLE = 0, CUTTING = 1, DELIMBING = 2 }
        _G.ChainsawCutState = State
        local Class = { MINIMUM_CUT_DISTANCE = 0, MAXIMUM_CUT_DISTANCE = 1.5 }
        if variant == "minimumMissing" then Class.MINIMUM_CUT_DISTANCE = nil end
        _G.HandToolChainsaw = Class
        _G.g_cameraManager = { getActiveCamera = function() return getCamera() end }

        local PlayerCamera = {}
        _G.PlayerCamera = PlayerCamera
        function PlayerCamera.getRotation(self)
            local dx, dy, dz = StubScene.worldDirection(self.cameraRootNode, 0, 0, -1)
            return -math.atan2(dy, math.sqrt(dx * dx + dz * dz)), math.atan2(dx, dz), 0
        end

        local PlayerTargeter = _G.PlayerTargeter
        if PlayerTargeter.addTargetType == nil then
            function PlayerTargeter:addTargetType(key, mask, minDistance, maxDistance)
                self.targetedMasks = self.targetedMasks or {}
                if self.targetedMasks[key] ~= nil then return end
                self.targetedMasks[key] = { key = key, mask = mask, minDistance = minDistance or 0, maxDistance = maxDistance }
            end
            function PlayerTargeter:removeTargetType(key)
                if self.targetedMasks ~= nil then self.targetedMasks[key] = nil end
                if self.closestTargetsByKey ~= nil then self.closestTargetsByKey[key] = nil end
            end
        end
        local baseUpdate = PlayerTargeter.update
        function PlayerTargeter:update(dt)
            baseUpdate(self, dt)
            self.closestTargetsByKey = self.closestTargetsByKey or {}
            local targetedMask = (self.targetedMasks or {})[Class]
            self.closestTargetsByKey[Class] = nil
            if targetedMask ~= nil and self.lastRayX ~= nil then
                self.closestTargetsByKey[Class] = castAtTrees(self.lastRayX, self.lastRayY, self.lastRayZ,
                    self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ, targetedMask.minDistance, targetedMask.maxDistance)
            end
        end

        local baseHold, baseDrop = StubHandTool.hold, StubHandTool.drop
        function StubHandTool.hold(tool)
            baseHold(tool)
            if tool.spec_chainsaw ~= nil and tool.carryingPlayer.isOwner then
                tool.carryingPlayer.targeter:addTargetType(Class, 0, Class.MINIMUM_CUT_DISTANCE or 0, MAX_TARGET_DISTANCE)
            end
        end
        function StubHandTool.drop(tool)
            if tool.spec_chainsaw ~= nil and tool.spec_chainsaw.currentCutState == State.CUTTING then tool:stopCutting(false) end
            if tool.spec_chainsaw ~= nil and tool.carryingPlayer ~= nil and tool.carryingPlayer.targeter ~= nil then
                tool.carryingPlayer.targeter:removeTargetType(Class)
            end
            baseDrop(tool)
        end

        local functions = {}
        function functions.setCurrentCutState(self, cutState, isVerticalCut)
            self.spec_chainsaw.currentCutState = cutState
            self.spec_chainsaw.isVerticalCut = isVerticalCut
        end

        function functions.calculateCutPlane(self)
            local spec = self.spec_chainsaw
            local x, y, z = getWorldTranslation(spec.splitPlaneNode)
            local downX, downY, downZ = localDirectionToWorld(spec.splitPlaneNode, 0, -1, 0)
            local leftX, leftY, leftZ = localDirectionToWorld(spec.splitPlaneNode, 1, 0, 0)
            return x, y, z, downX, downY, downZ, leftX, leftY, leftZ, spec.maximumCutDiameter, spec.maximumCutDiameter
        end

        function functions.updateSplitPlane(self)
            local spec = self.spec_chainsaw
            if spec.currentCutState == State.DELIMBING then
                local x, y, z = localToWorld(self.graphicalNode, 0, -spec.maximumDelimbDiameter * 0.5, 0)
                setWorldTranslation(spec.cameraRotationNode, x, y, z)
                local cameraNode = g_cameraManager:getActiveCamera()
                local upX, upY, upZ = localDirectionToWorld(cameraNode, 0, 1, 0)
                local dirX, dirY, dirZ = localDirectionToWorld(cameraNode, 0, 0, -1)
                setDirection(spec.cameraRotationNode, dirX, dirY, dirZ, upX, upY, upZ)
            else
                local _, playerYaw = self:getCarryingPlayer().camera:getRotation()
                setWorldRotation(spec.cameraRotationNode, 0, playerYaw, 0)
            end
            rotateAboutLocalAxis(spec.cameraRotationNode, spec.currentCutRoll, 0, 0, 1)

            spec.cutMinimumY, spec.cutMaximumY, spec.cutMinimumZ, spec.cutMaximumZ = nil, nil, nil, nil
            if spec.currentCutState == State.CUTTING then
                if spec.cuttingTreeNode == nil or not entityExists(spec.cuttingTreeNode) then return end
                spec.cutMinimumY, spec.cutMaximumY, spec.cutMinimumZ, spec.cutMaximumZ =
                    testSplitShape(spec.cuttingTreeNode, self:calculateCutPlane())
            elseif spec.currentCutState == State.IDLE then
                if spec.targetedTree == nil or not entityExists(spec.targetedTree.node) then return end
                setWorldTranslation(spec.cameraRotationNode, spec.targetedTree.x, spec.targetedTree.y, spec.targetedTree.z)
                spec.cutMinimumY, spec.cutMaximumY, spec.cutMinimumZ, spec.cutMaximumZ =
                    testSplitShape(spec.targetedTree.node, self:calculateCutPlane())
            end
        end

        function functions.testIfCutValid(self, treeNode, cutX, cutZ, minY, maxY, minZ, maxZ)
            local spec = self.spec_chainsaw
            if minY == nil or maxY == nil or minZ == nil or maxZ == nil then return false end
            return not (maxY - minY >= spec.maximumCutDiameter or maxZ - minZ >= spec.maximumCutDiameter)
        end

        function functions.updateRingSelector(self, targetedTree, isCutPossible)
            local spec = self.spec_chainsaw
            setVisibility(spec.ringNode, targetedTree ~= nil)
            if targetedTree == nil then return end
            local tree = treeOfNode(targetedTree.node)
            local _, cutY = getWorldTranslation(spec.cameraRotationNode)
            local x, y, z = worldToLocal(getParent(spec.ringNode), tree.x, cutY, tree.z)
            setTranslation(spec.ringNode, x, y, z)
            spec.ringIsValid = isCutPossible
        end

        function functions.beginDelimbing(self)
            self:setCurrentCutState(State.DELIMBING, true)
        end
        function functions.stopDelimbing(self)
            self:setCurrentCutState(State.IDLE, true)
        end

        function functions.beginCutting(self)
            local spec = self.spec_chainsaw
            setVisibility(spec.ringNode, false)
            if spec.possibleTreeNode == nil then return end
            self:setCurrentCutState(State.CUTTING, true)
            spec.cuttingTreeNode = spec.possibleTreeNode
            spec.currentCutTime = 0
            spec.cutStartPositionX, spec.cutStartPositionY, spec.cutStartPositionZ =
                localToWorld(spec.splitPlaneNode, spec.cutMinimumY, 0, spec.cutMinimumZ)
            spec.cutEndPositionX, spec.cutEndPositionY, spec.cutEndPositionZ =
                localToWorld(spec.splitPlaneNode, spec.cutMaximumY, 0, spec.cutMinimumZ)
            link(spec.cutGuideNode, self.graphicalNode)
            setTranslation(self.graphicalNode, 0, 0, 0)
            setRotation(self.graphicalNode, 0, 0, 0)
            StubHandTool.inputLocks = StubHandTool.inputLocks + 1
        end

        function functions.stopCutting(self, sliceTree)
            local spec = self.spec_chainsaw
            local cuttingTreeNode = spec.cuttingTreeNode
            spec.cuttingTreeNode = nil
            self:setCurrentCutState(State.IDLE, true)
            spec.currentCutTime = 0
            link(self.graphicalNodeParent, self.graphicalNode)
            setTranslation(self.graphicalNode, 0, 0, 0)
            StubHandTool.inputLocks = StubHandTool.inputLocks - 1
            if sliceTree and cuttingTreeNode ~= nil then
                local x, y, z, nx, ny, nz, ux, uy, uz = self:calculateCutPlane()
                table.insert(StubHandTool.cuts, { tree = cuttingTreeNode, x = x, y = y, z = z,
                    normalX = nx, normalY = ny, normalZ = nz, upX = ux, upY = uy, upZ = uz })
            end
        end

        function functions.updateCutting(self, dt)
            local spec = self.spec_chainsaw
            local cutX, cutY, cutZ = getWorldTranslation(spec.cameraRotationNode)
            local targeter = self:getCarryingPlayer().targeter
            local dx, dy, dz = targeter.lastRayX - cutX, targeter.lastRayY - cutY, targeter.lastRayZ - cutZ
            spec.currentCutTime = spec.currentCutTime + dt
            if math.sqrt(dx * dx + dy * dy + dz * dz) >= Class.MAXIMUM_CUT_DISTANCE then
                self:stopCutting(false)
                return
            end
            local progress = math.clamp(spec.currentCutTime / spec.cutTime, 0, 1)
            setWorldTranslation(spec.cutGuideNode,
                spec.cutStartPositionX + (spec.cutEndPositionX - spec.cutStartPositionX) * progress,
                spec.cutStartPositionY + (spec.cutEndPositionY - spec.cutStartPositionY) * progress,
                spec.cutStartPositionZ + (spec.cutEndPositionZ - spec.cutStartPositionZ) * progress)
            if spec.currentCutTime >= spec.cutTime then self:stopCutting(true) end
        end

        local function update(self, dt)
            local spec = self.spec_chainsaw
            local carryingPlayer = self:getCarryingPlayer()
            if carryingPlayer == nil or not carryingPlayer.isOwner or not self:getIsHeld() then return end
            spec.targetedTree = carryingPlayer.targeter.closestTargetsByKey and carryingPlayer.targeter.closestTargetsByKey[Class] or nil
            spec.possibleTreeNode = nil
            self:updateSplitPlane()

            if spec.currentCutState ~= State.CUTTING then
                if spec.rollInput ~= 0 then
                    spec.currentCutRoll = math.clamp(spec.currentCutRoll + 0.001 * spec.rollInput * dt, -math.pi * 0.5, math.pi * 0.5)
                    spec.rollInput = 0
                end
                local upX, upY, upZ = localDirectionToLocal(spec.cameraRotationNode, getParent(self.graphicalNode), 0, 1, 0)
                local dirX, dirY, dirZ = localDirectionToLocal(getCamera(), getParent(self.graphicalNode), 0, 0, -1)
                setDirection(self.graphicalNode, dirX, dirY, dirZ, upX, upY, upZ)
            end

            local isCutPossible = false
            if spec.currentCutState == State.IDLE or spec.currentCutState == State.CUTTING then
                local treeNode = spec.cuttingTreeNode
                if treeNode == nil and spec.targetedTree ~= nil then treeNode = spec.targetedTree.node end
                if treeNode ~= nil then
                    local cutX, _, cutZ = getWorldTranslation(spec.cameraRotationNode)
                    if not self:testIfCutValid(treeNode, cutX, cutZ, spec.cutMinimumY, spec.cutMaximumY, spec.cutMinimumZ, spec.cutMaximumZ) then
                        if spec.cuttingTreeNode ~= nil then self:stopCutting(false) end
                    else
                        if spec.cuttingTreeNode == nil then spec.possibleTreeNode = treeNode end
                        isCutPossible = true
                    end
                end
            end

            if spec.currentCutState == State.IDLE then
                self:updateRingSelector(spec.targetedTree, isCutPossible)
            elseif spec.currentCutState == State.CUTTING then
                self:updateCutting(dt)
            end

            if spec.currentCutState == State.DELIMBING then
                local x, y, z, nx, ny, nz, ux, uy, uz = self:calculateCutPlane()
                spec.lastDelimbPosX, spec.lastDelimbPosY, spec.lastDelimbPosZ = x, y, z
                spec.lastDelimbNormalX, spec.lastDelimbNormalY, spec.lastDelimbNormalZ = nx, ny, nz
                spec.lastDelimbUpX, spec.lastDelimbUpY, spec.lastDelimbUpZ = ux, uy, uz
            end
        end
        if variant == "missing" then
            Class.onUpdateTick = update
        elseif variant == "reshaped" then
            Class.onUpdate = function() update(StubHandTool.heldTool, 16.667) end
        else
            Class.onUpdate = update
        end

        function Class.onCutAction(self, _, inputValue)
            local spec = self.spec_chainsaw
            table.insert(StubHandTool.useCalls, { tool = "chainsaw", value = inputValue, self = self })
            if not (inputValue > 0) then
                if spec.currentCutState == State.CUTTING then
                    self:stopCutting(false)
                elseif spec.currentCutState == State.DELIMBING then
                    self:stopDelimbing()
                else
                    spec.currentCutState = State.IDLE
                end
                return
            end
            if spec.targetedTree == nil then
                self:beginDelimbing()
            else
                self:beginCutting()
                if spec.currentCutState ~= State.CUTTING then self:beginDelimbing() end
            end
        end

        function Class.onRollAction(self, _, inputDelta)
            self.spec_chainsaw.rollInput = self.spec_chainsaw.rollInput + inputDelta
        end
        if variant == "rollMissing" then
            Class.onRollAction = nil
        elseif variant == "rollReshaped" then
            Class.onRollAction = function() end
        end

        table.insert(StubHandTool.updaters, function(dt)
            local held = StubHandTool.heldTool
            if held ~= nil and held.spec_chainsaw ~= nil then
                local handler = variant == "missing" and Class.onUpdateTick or Class.onUpdate
                handler(held, dt)
            end
        end)

        local function draw(self)
            if self.spec_chainsaw.currentCutState == State.IDLE then StubHandTool.render(self, "crosshair") end
        end
        if variant == "drawMissing" then
            Class.onDrawOverlays = draw
        elseif variant == "drawReshaped" then
            Class.onDraw = function() draw(StubHandTool.heldTool) end
        else
            Class.onDraw = draw
        end
        table.insert(StubHandTool.drawers, function(tool)
            if tool.spec_chainsaw ~= nil then
                local handler = variant == "drawMissing" and Class.onDrawOverlays or Class.onDraw
                handler(tool)
            end
        end)

        StubHandTool.chainsawFunctions = functions
    end

    function StubHandTool.chainsaw(player, options)
        options = options or {}
        local tool = StubHandTool.makeTool(player, "chainsaw", "spec_chainsaw", options)
        local spec = tool.spec_chainsaw
        if getmetatable(player.camera) == nil then setmetatable(player.camera, { __index = _G.PlayerCamera }) end

        tool.graphicalNodeParent = StubScene.newNode("chainsawModelParent", tool.rootNode)
        tool.graphicalNode = StubScene.newNode("chainsawModel", tool.graphicalNodeParent)
        spec.cutNode = StubScene.newNode("chainsawCutNode", tool.graphicalNode, 0, -0.04, 0.4)
        spec.cutGuideNode = createTransformGroup("chainsawCutGuideNode")
        link(getRootNode(), spec.cutGuideNode)
        spec.cameraRotationNode = createTransformGroup("chainsawCameraRotationNode")
        link(getRootNode(), spec.cameraRotationNode)
        spec.splitPlaneNode = createTransformGroup("chainsawSplitPlaneNode")
        link(spec.cameraRotationNode, spec.splitPlaneNode)
        setRotation(spec.splitPlaneNode, 0, 0, -math.pi * 0.5)
        spec.ringNode = StubScene.newNode("chainsawRing", spec.cameraRotationNode)
        setVisibility(spec.ringNode, false)

        spec.currentCutState = _G.ChainsawCutState.IDLE
        spec.isVerticalCut = true
        spec.currentCutRoll, spec.rollInput = 0, 0
        spec.maximumCutDiameter, spec.maximumDelimbDiameter = 1, 1
        spec.cutTime, spec.currentCutTime = options.cutTime or 600, 0
        spec.targetedTree, spec.cuttingTreeNode, spec.possibleTreeNode = nil, nil, nil

        for name, fn in pairs(StubHandTool.chainsawFunctions) do tool[name] = fn end
        tool.activateAction = function(self, a, inputValue) return _G.HandToolChainsaw.onCutAction(self, a, inputValue) end
        return tool
    end

    function StubHandTool.barRay(tool)
        local x, y, z = getWorldTranslation(tool.rootNode)
        local dx, dy, dz = StubScene.worldDirection(tool.rootNode, 0, 0, 1)
        return x, y, z, dx, dy, dz
    end

    function StubHandTool.modelDirection(tool)
        return StubScene.worldDirection(tool.graphicalNode, 0, 0, 1)
    end
