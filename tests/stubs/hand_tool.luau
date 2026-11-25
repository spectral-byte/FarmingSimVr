    StubHandTool = { useCalls = {}, rays = {}, tools = {}, updaters = {}, drawers = {}, rendered = {}, cameraAttaches = 0 }
    StubHandTool.CAMERA_PLACEMENT = { x = 0.2, y = -0.15, z = 0.5 }
    local nodes = StubScene.nodes

    local function placeAtCamera(tool)
        local camera = tool.carryingPlayer.camera
        link(camera.pitchNode, tool.rootNode)
        local p = StubHandTool.CAMERA_PLACEMENT
        setTranslation(tool.rootNode, p.x, p.y, p.z)
        setRotation(tool.rootNode, 0, 0, 0)
    end

    function StubHandTool.install(variant, options)
        variant = variant or "ok"
        options = options or {}
        StubHandTool.variant = variant
        StubHandTool.useCalls, StubHandTool.rays, StubHandTool.tools, StubHandTool.cameraAttaches = {}, {}, {}, 0
        StubHandTool.heldTool = nil

        local HandTool = {}
        _G.HandTool = HandTool
        function HandTool.getIsCarried(self) return self.carryingPlayer ~= nil end
        function HandTool.getIsHeld(self) return self:getIsCarried() and self.isHeld == true end
        function HandTool.getCarryingPlayer(self) return self.carryingPlayer end
        function HandTool.attachToolToHand(self)
            link(self.carryingPlayer.handNode, self.rootNode)
            setTranslation(self.rootNode, 0, 0, 0)
            setRotation(self.rootNode, 0, 0, 0)
            return true
        end
        function HandTool.attachTool(self)
            if not self:getIsHeld() then return end
            if self.carryingPlayer.camera.isFirstPerson then
                if StubHandTool.variant == "missing" then
                    self:attachToolToCameraNode()
                elseif StubHandTool.variant == "reshaped" then
                    HandTool.attachToolToCamera(self)
                else
                    self:attachToolToCamera()
                end
            else
                self:attachToolToHand()
            end
            setVisibility(self.rootNode, true)
        end
        function HandTool.detachTool(self)
            setVisibility(self.rootNode, false)
            unlink(self.rootNode)
        end
        local function attachToCamera(tool)
            if not tool:getIsHeld() or tool.rootNode == nil then return end
            StubHandTool.cameraAttaches = StubHandTool.cameraAttaches + 1
            placeAtCamera(tool)
        end
        if variant == "missing" then
            function HandTool.attachToolToCameraNode(self) attachToCamera(self) end
        elseif variant == "reshaped" then
            function HandTool.attachToolToCamera() attachToCamera(StubHandTool.heldTool) end
        else
            function HandTool.attachToolToCamera(self) attachToCamera(self) end
        end
        StubHandTool.originalAttachToolToCamera = HandTool.attachToolToCamera

        local Player = {}
        _G.Player = Player
        local lookRay = function(self)
            local x, y, z = getWorldTranslation(self.camera.firstPersonCamera)
            local dx, dy, dz = StubScene.worldDirection(self.camera.firstPersonCamera, 0, 0, -1)
            return x, y, z, dx, dy, dz
        end
        function Player.getHeldHandTool(self) return self.currentHandTool end
        StubHandTool.originalGetLookRay = nil
        if options.getLookRay == "missing" then
            Player.lookRay = lookRay
        elseif options.getLookRay == "reshaped" then
            Player.getLookRay = function() return lookRay(StubHandTool.player) end
        else
            Player.getLookRay = lookRay
            StubHandTool.originalGetLookRay = lookRay
        end

        local PlayerTargeter = {}
        PlayerTargeter.__index = PlayerTargeter
        _G.PlayerTargeter = PlayerTargeter
        function PlayerTargeter.new(player) return setmetatable({ player = player }, PlayerTargeter) end
        function PlayerTargeter:update(dt)
            local read = self.player.getLookRay or self.player.lookRay
            self.lastRayX, self.lastRayY, self.lastRayZ, self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ =
                read(self.player)
            table.insert(StubHandTool.rays, { self.lastRayX, self.lastRayY, self.lastRayZ,
                self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ })
        end
    end

    function StubHandTool.onFoot(options)
        local scene = StubScene.onFoot(options)
        local camera = scene.cam
        local player = setmetatable({ camera = camera, isOwner = true }, { __index = _G.Player })
        player.handNode = StubScene.newNode("thirdPersonRightHand", scene.yawNode, 0.3, 1.2, 0.2)
        player.targeter = _G.PlayerTargeter.new(player)
        StubHandTool.player = player
        _G.g_currentMission.player = player
        _G.g_localPlayer = player
        return { player = player, cam = camera, parent = scene.parent, yawNode = scene.yawNode,
                 firstPersonCamera = scene.firstPersonCamera }
    end

    function StubHandTool.thirdPerson(player, on)
        player.camera.isFirstPerson = not on
        if StubHandTool.heldTool ~= nil then StubHandTool.heldTool:attachTool() end
    end

    function StubHandTool.tick(dt)
        local player = _G.g_localPlayer
        if player ~= nil and player.targeter ~= nil then player.targeter:update(dt or 16.667) end
        for _, updater in ipairs(StubHandTool.updaters) do updater(dt or 16.667) end
    end

    function StubHandTool.lastRay()
        local ray = StubHandTool.rays[#StubHandTool.rays]
        if ray == nil then return nil end
        return table.unpack(ray)
    end

    function StubHandTool.render(tool, overlay) table.insert(StubHandTool.rendered, { tool = tool, overlay = overlay }) end
    function StubHandTool.draw()
        StubHandTool.rendered, StubHandTool.screen = {}, {}
        for _, tool in ipairs(StubHandTool.tools) do
            if tool.isHeld and tool.carryingPlayer ~= nil then
                for _, drawer in ipairs(StubHandTool.drawers) do drawer(tool) end
            end
        end
    end
    function StubHandTool.rendering(tool)
        local names = {}
        for _, entry in ipairs(StubHandTool.rendered) do
            if entry.tool == tool then table.insert(names, entry.overlay) end
        end
        return table.concat(names, "+")
    end

    function StubHandTool.makeTool(player, name, specName, options)
        options = options or {}
        local tool = setmetatable({ name = name, isHeld = false, carryingPlayer = nil }, { __index = _G.HandTool })
        tool.rootNode = StubScene.newNode(name .. "Root", 0)
        tool.firstPersonNode = StubScene.newNode(name .. "FirstPerson", tool.rootNode)
        tool[specName] = {}
        tool.player = player
        setVisibility(tool.rootNode, false)
        table.insert(StubHandTool.tools, tool)
        return tool
    end

    local function useHandler(tool, name)
        return function(self, _, inputValue)
            table.insert(StubHandTool.useCalls, { tool = name, value = inputValue, self = self })
        end
    end

    function StubHandTool.hold(tool)
        tool.isHeld = true
        tool.carryingPlayer = tool.player
        tool.player.currentHandTool = tool
        StubHandTool.heldTool = tool
        tool:attachTool()
    end

    function StubHandTool.drop(tool)
        tool:detachTool()
        tool.isHeld = false
        tool.carryingPlayer = nil
        if tool.player.currentHandTool == tool then tool.player.currentHandTool = nil end
        if StubHandTool.heldTool == tool then StubHandTool.heldTool = nil end
        StubHandTool.restoreNameReading(tool)
    end

    function StubHandTool.pressUseBinding(tool, inputValue)
        tool:activateAction(nil, inputValue)
    end

    function StubHandTool.lance(player, options)
        local tool = StubHandTool.makeTool(player, "lance", "spec_highPressureWasherLance", options)
        local spec = tool.spec_highPressureWasherLance
        spec.raycastNode = StubScene.newNode("lanceNozzle", tool.rootNode, 0, 0, 0.5)
        spec.isActivated = false
        local record = useHandler(tool, "lance")
        tool.onWashAction = function(self, a, inputValue)
            self.spec_highPressureWasherLance.isActivated = inputValue > 0
            record(self, a, inputValue)
        end
        tool.activateAction = tool.onWashAction
        return tool
    end

    function StubHandTool.nozzleRay(tool)
        local node = tool.spec_highPressureWasherLance.raycastNode
        local x, y, z = getWorldTranslation(node)
        local dx, dy, dz = StubScene.worldDirection(node, 0, 0, 1)
        return x, y, z, dx, dy, dz
    end

    local unreadableNames = {}
    function StubHandTool.makeNameUnreadable(tool, node)
        if StubHandTool.engineGetName == nil then
            local plainGetName = _G.getName
            StubHandTool.engineGetName = plainGetName
            _G.getName = function(id)
                if unreadableNames[id] ~= nil then error("the node " .. tostring(id) .. " is gone") end
                return plainGetName(id)
            end
        end
        unreadableNames[node] = tool
    end

    function StubHandTool.restoreNameReading(tool)
        for node, owner in pairs(unreadableNames) do
            if owner == tool then unreadableNames[node] = nil end
        end
        if StubHandTool.engineGetName ~= nil and next(unreadableNames) == nil then
            _G.getName = StubHandTool.engineGetName
            StubHandTool.engineGetName = nil
        end
    end

    function StubHandTool.otherModTool(player, options)
        options = options or {}
        local class = _G.OtherModHandTool
        if class == nil then
            class = {}
            _G.OtherModHandTool = class
            function class.onUseAction(self, _, inputValue)
                table.insert(StubHandTool.useCalls, { tool = "otherMod", value = inputValue, self = self })
            end
            function class.onRollAction(self, _, inputDelta) end
            for i = 1, options.extraClassFunctions or 0 do class["extraFunction" .. i] = function() end end
        end
        local tool = StubHandTool.makeTool(player, "otherMod", "spec_otherModTool", options)
        tool.configFileName = options.configFileName or "mods/FS25_OtherMod/handTools/otherModTool.xml"
        tool.typeName = "otherModTool"
        local spec = tool.spec_otherModTool
        spec.charge = 5
        spec.isReeling = false
        spec.label = "A rather long label that is cut short when it is written to the log file"
        spec.bobberNode = StubScene.newNode("otherModBobber", 0)
        spec.lines = { 1, 2, 3 }
        spec.onBite = function() end
        for i = 1, options.extraSpecFields or 0 do spec["extra" .. i] = i end
        if options.nestedTables then
            spec.nested = { inner = { deep = 1 }, speed = 2.5, name = "reel" }
            spec.emptyTable = {}
        end
        if options.bigTable then
            local big = {}
            for i = 1, options.bigTable do big["entry" .. i] = i end
            spec.bigTable = big
        end
        if options.pathNamedNode then spec.guiImage = StubScene.newNode("dataS/menu/gui.png", 0) end
        tool.spec_otherModLine = { length = 12.5 }
        tool.specializationNames = { "otherModTool" }
        tool.specializations = { class }
        for i = 1, options.extraSpecTables or 0 do
            local name = "otherModExtra" .. i
            local extra = {}
            for j = 1, 20 do extra["field" .. j] = j end
            tool["spec_" .. name] = extra
            table.insert(tool.specializationNames, name)
            table.insert(tool.specializations, {})
        end
        if options.brokenSpec then
            local brokenNode = StubScene.newNode("otherModBroken", 0)
            tool.spec_otherModBroken = { first = 1, gone = brokenNode, last = 2 }
            StubHandTool.makeNameUnreadable(tool, brokenNode)
            table.insert(tool.specializationNames, "otherModBroken")
            table.insert(tool.specializations, {})
        end
        local typeFunctions = {
            getFloatDepth = function(self) return self.spec_otherModTool.charge end,
            isOtherModReeling = function(self) return self.spec_otherModTool.isReeling end,
        }
        tool.type = { name = "otherModTool", functions = typeFunctions }
        for name, fn in pairs(typeFunctions) do tool[name] = fn end
        tool.drawOverlay = function(self) StubHandTool.render(self, "otherModGauge") end
        local rod = StubScene.newNode("otherModRod", tool.rootNode, 0, 0, 0.2)
        local guide = StubScene.newNode("otherModGuide", rod, 0, 0, 0.3)
        local tip = StubScene.newNode("otherModTip", guide, 0, 0, 0.3)
        StubScene.newNode("otherModFloat", tip, 0, 0, 0.1)
        for i = 1, options.extraNodes or 0 do StubScene.newNode("otherModExtra" .. i, tool.rootNode, 0, 0, 0) end
        if options.brokenNode then
            StubHandTool.makeNameUnreadable(tool, StubScene.newNode("otherModGone", tool.rootNode, 0, 0, 0))
        end
        local event = { actionName = "ACTIVATE_HANDTOOL", target = tool, triggerUp = true, triggerDown = true,
                        triggerAlways = false, isActive = true, isDown = false }
        event.callback = class.onUseAction
        tool.actionEvents = {
            event,
            { actionName = "AXIS_ROTATE_HANDTOOL", target = tool, callback = class.onRollAction, triggerUp = false, triggerDown = false,
              triggerAlways = true, isActive = true },
            { actionName = "AXIS_OTHERMOD_NUDGE", target = tool, callback = function() end, triggerUp = true, triggerDown = false,
              triggerAlways = false, isActive = true },
        }
        tool.activateAction = function(self, _, inputValue)
            if not event.isActive then return end
            local down = inputValue > 0
            if down and not event.isDown and event.triggerDown then
                event.callback(event.target, event.actionName, inputValue)
            elseif down and event.isDown and event.triggerAlways then
                event.callback(event.target, event.actionName, inputValue)
            elseif not down and event.isDown and event.triggerUp then
                event.callback(event.target, event.actionName, inputValue)
            end
            event.isDown = down
        end
        return tool
    end

    table.insert(StubHandTool.drawers, function(tool)
        if tool.spec_otherModTool ~= nil then tool:drawOverlay() end
    end)
