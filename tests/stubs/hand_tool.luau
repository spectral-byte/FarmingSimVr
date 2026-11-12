-- Hand Tools (ADR-0029)
-- A player on foot who can hold a Hand Tool, shaped after the game's HandTool, Player and PlayerTargeter. Run after
-- scene_graph.luau (and after model_loading.luau when the Virtual Hands are needed: a hand tool goes into a Virtual Hand).
-- Run StubHandTool.install BEFORE loading the Companion Mod to have HandTool and Player there at file load, like the game.
--
-- StubHandTool.install(variant, options)   defines the globals the game defines: HandTool, Player and PlayerTargeter.
--   variant   how HandTool.attachToolToCamera is in this "game version": nil / "ok", "missing" (a game update renamed it
--             to attachToolToCameraNode: the class has no attachToolToCamera, the game attaches through the new name) or
--             "reshaped" (it takes no parameters at all, and attaches the tool being held)
--   options   getLookRay  "ok" (default), "missing" (Player has lookRay instead) or "reshaped" (Player.getLookRay takes no
--             parameters): for the look ray wrap. The targeter reads Player.lookRay in the "missing" game.
--             StubHandTool.originalGetLookRay is the game's own function ("ok" game only: nil in the others).
-- StubHandTool.onFoot(options) -> { player, cam, parent, yawNode, firstPersonCamera }   StubScene.onFoot(options) with a
--   Player: g_currentMission.player and g_localPlayer are it. player.camera is the camera of StubScene.onFoot (isFirstPerson,
--   pitchNode: the node the game links a held tool to). Player:getLookRay() -> x, y, z, dirX, dirY, dirZ is the first
--   person camera's position and its -Z axis in the world. player.targeter is a PlayerTargeter.
--   StubHandTool.thirdPerson(player, on)  the camera is (not) in first person; a held tool attaches again.
-- PlayerTargeter:update(dt)   reads player:getLookRay() once, like the game, and keeps it in targeter.lastRayX .. lastRayDirectionZ
--   (the fields the game keeps) and in StubHandTool.rays, in order, as { x, y, z, dirX, dirY, dirZ }.
--   StubHandTool.lastRay() -> x, y, z, dirX, dirY, dirZ   of the last update, nil before any
-- StubHandTool.tick(dt)   one frame of the game's player update: the targeter reads the look ray, then every registered
--   updater (StubHandTool.updaters, below) runs
-- StubHandTool.lance(player, options) -> tool   the pressure washer lance (spec_highPressureWasherLance), not yet held.
--   tool.rootNode (the node the game links), tool.firstPersonNode, tool.spec_highPressureWasherLance.raycastNode (the
--   nozzle: it washes from the node's origin along its +Z axis) and tool.activateAction, the function the game's
--   ACTIVATE_HANDTOOL binding calls (HandToolHPWLance:onWashAction(_, inputValue)).
--   StubHandTool.nozzleRay(tool) -> x, y, z, dirX, dirY, dirZ   where the lance washes from and along, in the world
-- StubHandTool.otherModTool(player, options) -> tool   another mod's Hand Tool (spec_otherModTool), which the Companion Mod
--   does not support: placed like any other by the game. Its use handler is OtherModHandTool.onUseAction(tool, _, inputValue),
--   on the specialization class only (recorded in StubHandTool.useCalls as tool = "otherMod"), and tool.actionEvents lists
--   its registered events { actionName, target, callback, triggerUp, triggerDown, triggerAlways, isActive }: ACTIVATE_HANDTOOL
--   (up and down, not always) first; tool.activateAction, the game's binding, calls it on those edges. The tool also carries
--   what a Hand Tool of the game has to be read from outside (config file, type, specializations, nodes): see the comment
--   above the function; options shape it (extra fields, nodes, a spec that cannot be read)
-- StubHandTool.hold(tool) / StubHandTool.drop(tool)   the player takes the tool in hand (the game's startHolding: isHeld,
--   the carrying player, attachTool) and puts it away (detachTool, not held)
-- StubHandTool.pressUseBinding(tool, inputValue)   the game's own binding (X on a pad, the mouse, the keyboard) calling the
--   tool's use handler with inputValue (1 pressed or held, 0 released)
-- StubHandTool.draw()   one frame of the game's draw: every held tool draws what it draws (the spray can's and the chainsaw's
--   onDraw are in their own chunks; another mod's tool draws "otherModGauge")
-- StubHandTool.rendering(tool) -> text   what the tool rendered in the last draw, in order, joined by "+" ("" for nothing)
-- StubHandTool.drawers   a list of function(tool) the draw runs for each held tool: a tool type's own onDraw goes in here;
--   StubHandTool.render(tool, overlay) records one thing rendered
-- StubHandTool.useCalls   every call of a tool's use handler, in order, as { tool = <name>, value = <inputValue>, self = tool }
-- StubHandTool.cameraAttaches   how many times the game's own attachToolToCamera ran (whatever placed the tool then)
-- StubHandTool.CAMERA_PLACEMENT   { x, y, z } where the game puts a tool's root below the pitch node (a tool placed the
--   game's way is there; the lance's firstPersonNode is its root's origin)
-- StubHandTool.tools   every tool made, in order
-- Extension points for the later Hand Tools tickets (each adds its own chunk, run after this one, and touches nothing above):
--   StubHandTool.updaters   a list of function(dt) the tick runs after the targeter: a tool's own onUpdate goes in here
--   StubHandTool.makeTool(player, name, specName, options) -> tool   the common tool: a root node, a firstPersonNode at its
--     origin, spec table `specName`, the HandTool methods through the class; a tool type adds its own nodes and handlers
--   (the flashlight and the chainsaw add StubHandTool.flashlight / chainsaw, in their own chunks)

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
        -- the game's placement in front of the face, whatever the version calls the function
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
        -- the game's own: the tool the player holds now (Player.currentHandTool, set by Player:setCurrentHandTool)
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
            -- a game whose getLookRay was renamed (options.getLookRay == "missing") reads it by the new name
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

    -- One frame of the game's draw: StubHandTool.rendered is emptied, then every held tool draws (each registered drawer, in
    -- StubHandTool.drawers, runs function(tool) for the tools of its kind, reading the class's onDraw at that moment, like the
    -- game's event dispatch). A tool's onDraw records what it rendered with StubHandTool.render(tool, overlay).
    function StubHandTool.render(tool, overlay) table.insert(StubHandTool.rendered, { tool = tool, overlay = overlay }) end
    function StubHandTool.draw()
        StubHandTool.rendered, StubHandTool.screen = {}, {}
        for _, tool in ipairs(StubHandTool.tools) do
            if tool.isHeld and tool.carryingPlayer ~= nil then
                for _, drawer in ipairs(StubHandTool.drawers) do drawer(tool) end
            end
        end
    end
    -- what the tool rendered in the last StubHandTool.draw(), in order, as one text ("" for nothing): "markerTypeIcon+treeIcon"
    function StubHandTool.rendering(tool)
        local names = {}
        for _, entry in ipairs(StubHandTool.rendered) do
            if entry.tool == tool then table.insert(names, entry.overlay) end
        end
        return table.concat(names, "+")
    end

    -- ---- the common tool ----------------------------------------------------------------------------------------
    -- A tool is what the game makes of a hand tool's i3d file: a root node, the first person node (where the game links
    -- the tool to the camera: here at the root's origin), the class's methods and the specialization's table.
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
        StubHandTool.restoreNameReading(tool)  -- a node name the tool made unreadable (otherModTool) reads again
    end

    function StubHandTool.pressUseBinding(tool, inputValue)
        tool:activateAction(nil, inputValue)
    end

    -- ---- the pressure washer lance ------------------------------------------------------------------------------
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

    -- A node whose name cannot be read (a node that is gone while a tool is being read): getName raises for it. The engine's
    -- getName is replaced only while a tool has such a node, kept in StubHandTool.engineGetName, and put back by drop(tool).
    local unreadableNames = {}  -- node -> the tool it belongs to
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

    -- ---- another mod's tool ---------------------------------------------------------------------------------------
    -- A Hand Tool the Companion Mod has never heard of, shaped like the tools mods add to the game: its own specialization
    -- (spec_otherModTool, which no SUPPORTED_TOOLS entry names), a use handler on its specialization class only
    -- (OtherModHandTool.onUseAction, called with the tool as `self`, like HandToolChainsaw.onCutAction) and an
    -- ACTIVATE_HANDTOOL action event registered for it (tool.actionEvents), as the game's registerActionEvent keeps one:
    -- called for trigger down (inputValue 1) and trigger up (0), not on every update the input is held.
    --
    -- It is also what the game gives any Hand Tool to be read from outside (HandTool.lua, SpecializationUtil.lua):
    --   tool.configFileName ("mods/FS25_OtherMod/handTools/otherModTool.xml"), tool.typeName
    --   tool.specializations / tool.specializationNames   the specialization classes and their names, in step (OtherModHandTool
    --     "otherModTool"); the specialization tables are tool.spec_<name>. A third table, spec_otherModLine, has no class.
    --   tool.type = { name, functions }   the tool's type and the functions it gives the instance (copied onto it)
    --   tool.actionEvents   three registered events: ACTIVATE_HANDTOOL (up and down, callback on the class), AXIS_ROTATE_HANDTOOL
    --     (every frame, callback on the class) and AXIS_OTHERMOD_NUDGE (up only, a callback that is on no class)
    --   the nodes: root -> FirstPerson, Rod -> Guide -> Tip -> Float (four levels below the root)
    -- options (all optional): nestedTables = true adds spec_otherModTool.nested ({ inner = { deep }, speed, name }) and an empty
    -- table; bigTable = N adds a table of N entries; pathNamedNode = true adds a field naming an entity whose name is a file path
    -- (as the engine's images and sounds are); extraSpecFields = N more fields in spec_otherModTool; extraSpecTables = N more specializations of
    -- 20 fields each; extraNodes = N more children of the root; extraClassFunctions = N more functions on OtherModHandTool (the
    -- first tool made in a game decides); brokenSpec = true adds spec_otherModBroken, which has a field
    -- naming a node whose getName raises (a node that is gone while the tool is being read); brokenNode = true adds a last child of
    -- the root whose name cannot be read the same way; both are the tool's, and StubHandTool.drop(tool) makes the names readable
    -- again (see makeNameUnreadable); configFileName replaces the name.
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
        -- the type's functions, which the game copies onto the instance
        local typeFunctions = {
            getFloatDepth = function(self) return self.spec_otherModTool.charge end,
            isOtherModReeling = function(self) return self.spec_otherModTool.isReeling end,
        }
        tool.type = { name = "otherModTool", functions = typeFunctions }
        for name, fn in pairs(typeFunctions) do tool[name] = fn end
        -- its own overlay, drawn by the game's draw (StubHandTool.draw) and never wrapped by anything of the Companion Mod
        tool.drawOverlay = function(self) StubHandTool.render(self, "otherModGauge") end
        -- the nodes below the root
        local rod = StubScene.newNode("otherModRod", tool.rootNode, 0, 0, 0.2)
        local guide = StubScene.newNode("otherModGuide", rod, 0, 0, 0.3)
        local tip = StubScene.newNode("otherModTip", guide, 0, 0, 0.3)
        StubScene.newNode("otherModFloat", tip, 0, 0, 0.1)
        for i = 1, options.extraNodes or 0 do StubScene.newNode("otherModExtra" .. i, tool.rootNode, 0, 0, 0) end
        if options.brokenNode then  -- the last child of the root: its name cannot be read
            StubHandTool.makeNameUnreadable(tool, StubScene.newNode("otherModGone", tool.rootNode, 0, 0, 0))
        end
        -- the registered action events
        local event = { actionName = "ACTIVATE_HANDTOOL", target = tool, triggerUp = true, triggerDown = true,
                        triggerAlways = false, isActive = true, isDown = false }
        event.callback = class.onUseAction  -- the function itself, as the game's registerActionEvent keeps it
        tool.actionEvents = {
            event,
            { actionName = "AXIS_ROTATE_HANDTOOL", target = tool, callback = class.onRollAction, triggerUp = false, triggerDown = false,
              triggerAlways = true, isActive = true },
            { actionName = "AXIS_OTHERMOD_NUDGE", target = tool, callback = function() end, triggerUp = true, triggerDown = false,
              triggerAlways = false, isActive = true },
        }
        -- the game's input binding: the registered event is called on the edges it was registered for
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

    -- ---- The chainsaw and the look ray: their own chunks add StubHandTool.chainsaw / ... with
    -- StubHandTool.makeTool and StubHandTool.updaters; nothing above changes. The flashlight is hand_tool_flashlight.luau.
