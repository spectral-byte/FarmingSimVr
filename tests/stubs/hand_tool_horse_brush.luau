    StubHandTool.animals, StubHandTool.cleanEvents, StubHandTool.actionEvents = {}, {}, {}
    local function normalize(x, y, z)
        local length = math.sqrt(x * x + y * y + z * z)
        if length < 1e-9 then return 0, 0, 1 end
        return x / length, y / length, z / length
    end

    local function castAtAnimals(x, y, z, dx, dy, dz, minDistance, maxDistance)
        local best = nil
        for _, animal in ipairs(StubHandTool.animals) do
            local cx, cy, cz = getWorldTranslation(animal.node)
            local ox, oy, oz = x - cx, y - cy, z - cz
            local b = ox * dx + oy * dy + oz * dz
            local disc = b * b - (ox * ox + oy * oy + oz * oz - animal.radius ^ 2)
            if disc >= 0 then
                local t = -b - math.sqrt(disc)
                if t >= minDistance and t <= maxDistance and (best == nil or t < best.distance) then
                    best = { node = animal.node, x = x + dx * t, y = y + dy * t, z = z + dz * t, distance = t }
                end
            end
        end
        return best
    end

    function StubHandTool.animalAt(ray, distance, radius, options)
        options = options or {}
        local x, y, z, dx, dy, dz = table.unpack(ray)
        dx, dy, dz = normalize(dx, dy, dz)
        local reach = distance + radius
        local index = #StubHandTool.animals + 1
        local node = StubScene.newNode("animal" .. index, 0, x + dx * reach, y + dy * reach, z + dz * reach)
        local cluster = { id = index, name = options.name or "Silver", dirt = options.dirt or 50 }
        function cluster:getName() return self.name end
        function cluster:changeDirt(delta) self.dirt = math.max(0, math.min(100, self.dirt + delta)) end
        local husbandry = { networkId = 100 + index, farmId = options.farmId or 1, clusters = { [cluster.id] = cluster } }
        function husbandry:getClusterById(id) return self.clusters[id] end
        local animal = { node = node, radius = radius, husbandry = husbandry, cluster = cluster, husbandryId = 200 + index, animalId = index }
        table.insert(StubHandTool.animals, animal)
        return animal
    end

    local function installTheGamesFarmAndNetwork()
        _G.getAnimalFromCollisionNode = function(node)
            for _, animal in ipairs(StubHandTool.animals) do
                if animal.node == node then return animal.husbandryId, animal.animalId end
            end
            return nil, nil
        end
        local mission = _G.g_currentMission or {}
        _G.g_currentMission = mission
        mission.husbandrySystem = {
            getClusterHusbandryById = function(self, husbandryId)
                for _, animal in ipairs(StubHandTool.animals) do
                    if animal.husbandryId == husbandryId then
                        return {
                            getPlaceable = function() return animal.husbandry end,
                            getClusterByAnimalId = function(_, animalId) return animal.cluster end,
                        }
                    end
                end
                return nil
            end,
        }
        mission.accessHandler = { canFarmAccess = function(self, farmId, husbandry) return farmId == husbandry.farmId end }
        _G.NetworkUtil = {
            getObjectId = function(object) return object.networkId end,
            getObject = function(id)
                for _, animal in ipairs(StubHandTool.animals) do
                    if animal.husbandry.networkId == id then return animal.husbandry end
                end
                return nil
            end,
        }
        _G.AnimalCleanEvent = {
            new = function(husbandry, clusterId, delta)
                return { husbandry = husbandry, clusterId = clusterId, delta = math.abs(math.floor(delta)) }
            end,
        }
        _G.g_client = {
            getServerConnection = function()
                return {
                    sendEvent = function(_, event)
                        table.insert(StubHandTool.cleanEvents, event)
                        local cluster = event.husbandry:getClusterById(event.clusterId)
                        if cluster ~= nil and cluster.changeDirt ~= nil then cluster:changeDirt(-event.delta) end
                    end,
                }
            end,
        }
    end

    local inputBinding = {
        setActionEventText = function(_, id, text) StubHandTool.actionEvents[id].text = text end,
        setActionEventActive = function(_, id, active) StubHandTool.actionEvents[id].active = active end,
    }
    function StubHandTool.inputHelp(tool)
        local event = StubHandTool.actionEvents[tool.spec_horseBrush.activateActionEventId]
        return event.active and event.text or ""
    end

    function StubHandTool.installHorseBrush(handlerVariant, lookupVariant)
        handlerVariant, lookupVariant = handlerVariant or "ok", lookupVariant or "ok"
        StubHandTool.animals, StubHandTool.cleanEvents, StubHandTool.actionEvents = {}, {}, {}
        StubHandTool.horseBrushes = {}
        StubHandTool.horseBrushHandlerName = handlerVariant == "missing" and "onBrushUse" or "onBrushAction"
        StubHandTool.horseBrushLookupName = lookupVariant == "missing" and "getAnimalFromNode" or "getHusbandryAndClusterFromNode"
        StubHandTool.horseBrushLookupVariant = lookupVariant
        local Class = {}
        _G.HandToolHorseBrush = Class
        installTheGamesFarmAndNetwork()

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
        if PlayerTargeter.getClosestTargetedNodeFromType == nil then
            function PlayerTargeter:getClosestTargetedNodeFromType(key)
                local target = (self.closestTargetsByKey or {})[key]
                return target ~= nil and target.node or nil
            end
        end
        local previousUpdate = PlayerTargeter.update
        function PlayerTargeter:update(dt)
            previousUpdate(self, dt)
            self.closestTargetsByKey = self.closestTargetsByKey or {}
            local targetedMask = (self.targetedMasks or {})[Class]
            self.closestTargetsByKey[Class] = nil
            if targetedMask ~= nil and self.lastRayX ~= nil then
                self.closestTargetsByKey[Class] = castAtAnimals(self.lastRayX, self.lastRayY, self.lastRayZ,
                    self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ, targetedMask.minDistance, targetedMask.maxDistance)
            end
        end

        local baseHold, baseDrop = StubHandTool.hold, StubHandTool.drop
        function StubHandTool.hold(tool)
            baseHold(tool)
            local spec = tool.spec_horseBrush
            if spec ~= nil and tool.carryingPlayer.isOwner then
                tool.carryingPlayer.targeter:addTargetType(Class, 0, 0.5, spec.animalDetectionDistance)
            end
        end
        function StubHandTool.drop(tool)
            if tool.spec_horseBrush ~= nil and tool.carryingPlayer ~= nil and tool.carryingPlayer.targeter ~= nil then
                tool.carryingPlayer.targeter:removeTargetType(Class)
            end
            baseDrop(tool)
        end

        local function lookup(player, node)
            if node == nil or not entityExists(node) then return nil, nil end
            local husbandryId, animalId = getAnimalFromCollisionNode(node)
            if husbandryId ~= nil and husbandryId ~= 0 then
                local mission = g_currentMission
                local clusterHusbandry = mission.husbandrySystem:getClusterHusbandryById(husbandryId)
                if clusterHusbandry ~= nil then
                    local husbandry = clusterHusbandry:getPlaceable()
                    local cluster = clusterHusbandry:getClusterByAnimalId(animalId)
                    if cluster ~= nil and mission.accessHandler:canFarmAccess(player.farmId, husbandry) then
                        if cluster.changeDirt ~= nil and cluster.getName ~= nil then return husbandry, cluster end
                    end
                end
            end
            return nil, nil
        end
        if lookupVariant == "reshaped" then
            Class.getHusbandryAndClusterFromNode = function(node) return lookup(StubHandTool.player, node) end
        else
            Class[StubHandTool.horseBrushLookupName] = lookup
        end
        local function askTheLookup(player, node)
            if lookupVariant == "reshaped" then return Class.getHusbandryAndClusterFromNode(node) end
            return Class[StubHandTool.horseBrushLookupName](player, node)
        end

        local function setCleaning(self, inputValue)
            table.insert(StubHandTool.useCalls, { tool = "horseBrush", value = inputValue, self = self })
            self.spec_horseBrush.isCleaning = inputValue > 0
        end
        if handlerVariant == "reshaped" then
            Class.onBrushAction = function() setCleaning(StubHandTool.heldTool, 1) end
        else
            Class[StubHandTool.horseBrushHandlerName] = function(self, _, inputValue) setCleaning(self, inputValue) end
        end

        function Class.onUpdate(self, dt)
            local player = self:getCarryingPlayer()
            if player == nil then return end
            local spec = self.spec_horseBrush
            spec.targetedHusbandryId, spec.targetedClusterId, spec.targetedHusbandry = nil, nil, nil

            if player.isOwner then
                local targetNode = player.targeter:getClosestTargetedNodeFromType(Class)
                local husbandry, cluster = askTheLookup(player, targetNode)
                spec.targetedHusbandry = husbandry

                if husbandry ~= nil then
                    inputBinding:setActionEventText(spec.activateActionEventId, string.format(spec.brushText, cluster:getName()))
                end
                inputBinding:setActionEventActive(spec.activateActionEventId, husbandry ~= nil)

                if husbandry ~= nil and spec.isCleaning then
                    spec.targetedHusbandryId = NetworkUtil.getObjectId(husbandry)
                    spec.targetedClusterId = cluster.id
                    if spec.brushNode ~= nil then
                        local x, y, z = spec.originalPos[1], spec.originalPos[2], spec.originalPos[3]
                        local now = _G.g_time or 0
                        x = x + math.sin(now * 0.25 * math.pi) * 0.005
                        y = y + math.sin(now * 0.25 * math.pi) * 0.001
                        z = z + math.sin(now * 0.25 * math.pi) * 0.05
                        setTranslation(spec.brushNode, x, y, z)
                    end
                end
            end

            spec.cleaningSoundPlaying = spec.targetedHusbandryId ~= nil

            if spec.targetedHusbandryId ~= nil then
                local husbandry = NetworkUtil.getObject(spec.targetedHusbandryId)
                if husbandry ~= nil then
                    spec.deltaSum = spec.deltaSum + spec.cleaningDeltaPerMs * dt
                    if spec.deltaSum > 5 then
                        local delta = math.floor(spec.deltaSum)
                        spec.deltaSum = spec.deltaSum - delta
                        if husbandry:getClusterById(spec.targetedClusterId) ~= nil then
                            g_client:getServerConnection():sendEvent(AnimalCleanEvent.new(husbandry, spec.targetedClusterId, delta))
                        end
                    end
                end
            else
                spec.deltaSum = 0
            end
        end

        function Class.onDraw(self)
            local spec = self.spec_horseBrush
            StubHandTool.render(self, spec.targetedHusbandry ~= nil and "brushCrosshair" or "defaultCrosshair")
        end

        if not StubHandTool.horseBrushUpdaterAdded then
            StubHandTool.horseBrushUpdaterAdded = true
            table.insert(StubHandTool.updaters, function(dt)
                local held = StubHandTool.heldTool
                if held ~= nil and held.spec_horseBrush ~= nil then _G.HandToolHorseBrush.onUpdate(held, dt) end
            end)
            table.insert(StubHandTool.drawers, function(tool)
                if tool.spec_horseBrush ~= nil then _G.HandToolHorseBrush.onDraw(tool) end
            end)
        end
    end

    function StubHandTool.horseBrush(player, options)
        options = options or {}
        local tool = StubHandTool.makeTool(player, "horseBrush", "spec_horseBrush", options)
        local spec = tool.spec_horseBrush
        player.farmId = player.farmId or 1
        spec.animalDetectionDistance = 3
        spec.brushNode = StubScene.newNode("brush", tool.rootNode, 0, 0, 0)
        spec.originalPos = { getTranslation(spec.brushNode) }
        spec.cleaningDeltaPerMs = 10 / 1000
        spec.deltaSum = 0
        spec.brushText = "Clean %s"
        spec.cleaningSoundPlaying = false
        local eventId = #StubHandTool.horseBrushes + 1
        StubHandTool.actionEvents[eventId] = { text = "", active = false }
        spec.activateActionEventId = eventId
        tool.activateAction = function(self, a, inputValue) return _G.HandToolHorseBrush[StubHandTool.horseBrushHandlerName](self, a, inputValue) end
        table.insert(StubHandTool.horseBrushes, tool)
        return tool
    end

    function StubHandTool.bristleRay(tool)
        local node = tool.spec_horseBrush.brushNode
        local x, y, z = getWorldTranslation(node)
        local dx, dy, dz = StubScene.worldDirection(node, 0, -1, 0)
        return x, y, z, dx, dy, dz
    end
