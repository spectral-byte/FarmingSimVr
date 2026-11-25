    local SPEC_NAME = "pdlc_highlandsFishingPack.fishingRod"
    local SPEC = "spec_" .. SPEC_NAME
    StubHandTool.fishingRods, StubHandTool.casts = {}, {}
    local stateNames = {}

    -- What reached the screen in a draw: { what (the overlay's file, or the text), x, y, width (0 for text) }.
    StubHandTool.screen = {}
    if _G.Overlay == nil then
        local Overlay = {}
        local Overlay_mt = { __index = Overlay }
        function Overlay.new(filename, x, y, width, height)
            return setmetatable({ filename = filename, x = x, y = y, width = width, height = height, offsetX = 0, offsetY = 0,
                                  visible = true }, Overlay_mt)
        end
        function Overlay:render()
            if not self.visible then return end
            table.insert(StubHandTool.screen, { what = self.filename, x = self.x + self.offsetX, y = self.y + self.offsetY, width = self.width })
        end
        function Overlay:setPosition(x, y) self.x, self.y = x or self.x, y or self.y end
        _G.Overlay = Overlay
    end
    local function renderTextNative(x, y, size, text) table.insert(StubHandTool.screen, { what = tostring(text), x = x, y = y, width = 0 }) end
    local function drawRectNative(name, x, y, width, height) table.insert(StubHandTool.screen, { what = name, x = x, y = y, width = width }) end
    local reelHeld = {}

    function StubHandTool.rodSpec(tool) return tool[SPEC] end
    function StubHandTool.rodState(tool) return stateNames[tool[SPEC].state] end

    local function normalize(x, y, z)
        local length = math.sqrt(x * x + y * y + z * z)
        if length < 1e-9 then return 0, 0, 1 end
        return x / length, y / length, z / length
    end

    local function makeStates(tool)
        local overlay = function() return { width = 0.08, height = 0.08, visible = true } end
        local states = {
            idle = { fishingRod = tool },
            charging = { fishingRod = tool, angleScale = 0, angleScaleDecreasePerMs = 0.006, angleScaleIncreasePerMs = 0.0007,
                         castingAngle = 0, castingChargeOverlay = Overlay.new("castingCharge.dds", 0.45, 0.45, 0.10, 0.02),
                         inputValue = 0, maxAngleScale = 1,
                         rotBackSpeed = 0.0014, rotForwardSpeed = 0.0084 },
            waiting = { fishingRod = tool, biteHintTimeThreshold = 3500, biteTime = 0, biteTimeMax = 25000, biteTimeMin = 10000,
                        multiplier = 1.584, overlayDefaultWidth = 0.078125, overlayWidthVariation = 0.0015625, playedSample = false,
                        waitForBiteOverlay = overlay() },
            bite = { fishingRod = tool, bobberDepthBit = 0.08, catchTime = 0, duration = 880, glyphButtonHeight = 0.05,
                     glyphButtonOverlay = overlay(), glyphButtonText = "Hook fish",
                     hookFishOverlay = Overlay.new("hookFish.dds", 0.47, 0.45, 0.06, 0.04),
                     hookFishOverlayFadeInDuration = 200, hookFishOverlayWidthMax = 0.4, hookFishOverlayWidthMin = 0.3,
                     minCatchTimeThreshold = 700 },
            reeling = { fishingRod = tool, posX = 0.65, posY = 0.40, width = 0.10, height = 0.32,
                        fishYMin = 0.41, fishYMax = 0.69, cursorPos = 0.5, cursorSpeed = 0, cursorInTargetRange = true,
                        targetPosPercentage = 0.5, threshold = 0.2, targetLineXStart = 0.69, targetLineXEnd = 0.73,
                        targetLineWidth = 0.0026, hookLineX = 0.76, hookLineOffsetY = 0.0213, hookLineWidth = 0.0026,
                        hookStatusLineXStart = 0.746, hookStatusLineXEnd = 0.7474, hookStatusLineWidth = 0.0056, progress = 0.1,
                        reelingStartTime = 0, lastInputValue = 0, glyphButtonHeight = 0.03, glyphButtonOverlay = overlay(),
                        progressBarOverlay = Overlay.new("progressBar.png", 0.655, 0.41, 0.02, 0.30),
                        fishTargetBarOverlay = Overlay.new("fishTargetBar.png", 0.685, 0.41, 0.05, 0.30),
                        fishOverlay = Overlay.new("fish.png", 0.69, 0.41, 0.05, 0.04),
                        hookOverlay = Overlay.new("hook.png", 0.755, 0.37, 0.015, 0.026),
                        caughtOverlay = Overlay.new("caught.png", 0.657, 0.672, 0.02, 0.04),
                        lostOverlay = Overlay.new("lost.png", 0.658, 0.419, 0.019, 0.022) },
            caught = { fishingRod = tool, angleScaleDecreasePerMs = 0.006, bendResetRate = 0.001 },
        }
        local list = { states.idle, states.charging, states.waiting, states.bite, states.reeling, states.caught }
        for i = 1, 5 do table.insert(list, { fishingRod = tool, unseenValue = i }) end
        for name, stateTable in pairs(states) do stateNames[stateTable] = name end
        return states, list
    end

    local function stateTable(tool, name) return tool.rodStates[name] end

    local function setState(tool, name)
        local spec = tool[SPEC]
        spec.state = stateTable(tool, name)
        local line = spec.fishingLine
        if name == "idle" or name == "charging" or name == "caught" then
            line.isInitialized, line.visibility, line.ropeLength, line.numActiveNodes = false, false, 0, 0
            spec.rodBendFactor = 0
            reelHeld[tool] = nil
        elseif not line.isInitialized then
            line.isInitialized, line.visibility, line.ropeLength, line.numActiveNodes = true, true, 22.4, 46
        end
        if name ~= "reeling" then spec.rodBendFactor = 0 end
        if name == "charging" then
            spec.state.angleScale, spec.state.inputValue, spec.angleScale = 0, 1, 0
        elseif name == "waiting" then
            spec.state.biteTime = (_G.g_time or 0) + spec.state.biteTimeMin
        elseif name == "bite" then
            spec.state.catchTime = (_G.g_time or 0) + spec.state.duration
            tool.hookWindowMs = 0
        end
    end

    function StubHandTool.setRodState(tool, name) setState(tool, name) end
    function StubHandTool.biteRod(tool)
        assert(StubHandTool.rodState(tool) == "waiting", "a fish bites only while the rod waits")
        setState(tool, "bite")
    end
    function StubHandTool.setFishPull(tool, pull) tool.fishPull = pull end

    local function cast(tool)
        local spec = tool[SPEC]
        local angleScale = spec.state.angleScale
        setState(tool, "waiting")
        spec.fishingLine.ropeLength = 2 + 20 * angleScale
        local player = tool:getCarryingPlayer()
        local ray = nil
        if player ~= nil then
            local read = player.getLookRay or player.lookRay
            ray = { read(player) }
        end
        local bobber = nil
        if ray ~= nil and ray[1] ~= nil then
            local dx, dy, dz = normalize(ray[4], ray[5], ray[6])
            local length = spec.fishingLine.ropeLength
            setTranslation(spec.bobber, ray[1] + dx * length, ray[2] + dy * length, ray[3] + dz * length)
            bobber = { getWorldTranslation(spec.bobber) }
        end
        table.insert(StubHandTool.casts, { angleScale = angleScale, ray = ray, bobber = bobber })
    end

    local function activate(tool, inputValue)
        local name = StubHandTool.rodState(tool)
        if inputValue > 0 then
            if name == "idle" then
                setState(tool, "charging")
            elseif name == "bite" then
                setState(tool, "reeling")
                reelHeld[tool] = true
            elseif name == "reeling" then
                reelHeld[tool] = true
            elseif name == "caught" then
                tool[SPEC].item = nil
                setState(tool, "idle")
            end
        else
            if name == "charging" then
                cast(tool)
            elseif name == "reeling" then
                reelHeld[tool] = nil
            end
        end
    end

    function StubHandTool.installFishingRod(variant)
        variant = variant or "ok"
        StubHandTool.fishingRods, StubHandTool.casts = {}, {}
        StubHandTool.fishingRodHandlerName = variant == "missing" and "onPrimaryAction" or "onActivateAction"
        local Class = {}
        StubHandTool.fishingRodClass = Class
        StubHandTool.storableClass = {
            getHolsterNodeByType = function() return nil end, onLoad = function() end,
            prerequisitesPresent = function() return true end, registerEventListeners = function() end,
            registerFunctions = function() end, registerXMLPaths = function() end,
        }

        local function record(self, action, inputValue)
            table.insert(StubHandTool.useCalls, { tool = "fishingRod", action = action, value = inputValue, self = self })
        end
        if variant == "reshaped" then
            Class.onActivateAction = function()
                local rod = StubHandTool.heldTool
                record(rod, "ACTIVATE_HANDTOOL", 1)
                activate(rod, 1)
            end
        else
            Class[StubHandTool.fishingRodHandlerName] = function(self, actionName, inputValue)
                record(self, "ACTIVATE_HANDTOOL", inputValue)
                activate(self, inputValue)
            end
        end
        function Class.onActivateSecondaryAction(self, actionName, inputValue) record(self, "ACTIVATE_HANDTOOL_SECONDARY", inputValue) end

        Class.setState = function(self, name) setState(self, name) end
        Class.getBobberPosition = function(self) return getWorldTranslation(self[SPEC].bobber) end
        Class.getTipPosition = function(self) return getWorldTranslation(self[SPEC].tipLine) end
        Class.getAngleScale = function(self) return self[SPEC].angleScale end
        Class.getRodBendFactor = function(self) return self[SPEC].rodBendFactor end
        Class.setRodBendFactor = function(self, factor) self[SPEC].rodBendFactor = factor end
        Class.getCurrentItem = function(self) return self[SPEC].item end
        Class.setCurrentItem = function(self, item) self[SPEC].item = item end
        Class.getExtendState = function(self) return 1 end
        for _, name in ipairs({ "setPrimaryAction", "setSecondaryAction", "placeBobber", "reelInBobber", "rotateReel", "createLine",
                                "destroyLine", "setAngleScale", "setBobberVisibility", "setTipLineVisibility", "setMovementDisabled",
                                "playSample", "stopSample", "getSample", "playExtendAnimation", "updateGraphicalNode",
                                "getInputOverlays" }) do
            Class[name] = function() end
        end
        for _, name in ipairs({ "onHeldStart", "onHeldEnd", "onRegisterActionEvents", "onInputModeChanged", "onLoad", "onDelete",
                                "onPlayerPreTeleport", "prerequisitesPresent", "registerEventListeners", "registerFunctions",
                                "registerXMLPaths" }) do
            Class[name] = function() end
        end

        function Class.onUpdate(self, dt)
            local spec = self[SPEC]
            if spec.state == nil then spec.state = self.rodStates.idle end
            local name = stateNames[spec.state]
            if name == "charging" then
                spec.state.angleScale = math.min(spec.state.maxAngleScale, spec.state.angleScale + spec.state.angleScaleIncreasePerMs * dt)
                spec.angleScale = spec.state.angleScale
            elseif name == "bite" then
                self.hookWindowMs = (self.hookWindowMs or 0) + dt
                if self.hookWindowMs > spec.state.duration then setState(self, "waiting") end
            elseif name == "reeling" then
                local held = reelHeld[self] == true
                spec.rodBendFactor = (self.fishPull or 0.5) * (held and 1 or 1 / 3)
                if held then
                    local line = spec.fishingLine
                    line.ropeLength = math.max(line.minLength, line.ropeLength - 0.01 * dt)
                    line.numActiveNodes = math.floor(line.ropeLength / line.nodeDistance)
                    if line.ropeLength <= line.minLength then
                        spec.item = { name = "fishingItem_trout", dialogText = "You've caught a trout!", dialogTitle = "Well done!",
                                      sellPrice = 100, distance = 1.15 }
                        setState(self, "caught")
                    end
                end
            end
        end

        function Class.onDraw(self)
            local current = self[SPEC].state
            local name = stateNames[current]
            if name == "charging" then
                StubHandTool.render(self, "castingChargeOverlay")
                current.castingChargeOverlay:render()
            elseif name == "waiting" then
                StubHandTool.render(self, "waitForBiteOverlay")
                renderTextNative(0.5, 0.40, 0.02, "Wait for a bite")
            elseif name == "bite" then
                StubHandTool.render(self, "hookFishOverlay")
                current.hookFishOverlay:render()
            elseif name == "reeling" then
                local s = current
                local function fishY(share) return s.fishYMin + share * (s.fishYMax - s.fishYMin) end
                drawRectNative("panel", s.posX, s.posY, s.width, s.height)
                s.progressBarOverlay:render()
                s.fishTargetBarOverlay:render()
                s.fishOverlay:setPosition(s.fishOverlay.x, fishY(s.cursorPos))
                s.fishOverlay:render()
                drawRectNative("targetLine", s.targetLineXStart, fishY(s.targetPosPercentage), s.targetLineXEnd - s.targetLineXStart, 0.002)
                s.hookOverlay:setPosition(s.hookOverlay.x, s.hookOverlay.y)
                s.hookOverlay:render()
                drawRectNative("hookLine", s.hookLineX, s.posY, s.hookLineWidth, s.height)
            end
        end
        StubHandTool.fishingRodOriginalOnDraw = Class.onDraw

        if not StubHandTool.fishingRodUpdaterAdded then
            StubHandTool.fishingRodUpdaterAdded = true
            table.insert(StubHandTool.updaters, function(dt)
                local held = StubHandTool.heldTool
                if held ~= nil and held[SPEC] ~= nil then StubHandTool.fishingRodClass.onUpdate(held, dt) end
            end)
            table.insert(StubHandTool.drawers, function(tool)
                if tool[SPEC] ~= nil then StubHandTool.fishingRodClass.onDraw(tool) end
            end)
        end
    end

    function StubHandTool.fishingRod(player, options)
        options = options or {}
        local Class = StubHandTool.fishingRodClass
        assert(Class ~= nil, "StubHandTool.installFishingRod first: the game has no fishing rod without the DLC")
        local tool = StubHandTool.makeTool(player, "fishingRod", SPEC, options)
        tool.configFileName = "C:/Program Files (x86)/Steam/steamapps/common/Farming Simulator 25/pdlc/highlandsFishingPack/handTools/fishingRod/fishingRod.xml"
        tool.typeName = SPEC_NAME
        tool.fishPull = options.fishPull or 0.5
        tool.spec_storable = { holsterNodes = {} }
        tool.specializationNames = { "storable", SPEC_NAME }
        tool.specializations = { StubHandTool.storableClass, Class }

        local root = tool.rootNode
        local model = StubScene.newNode("fishingRod", root, 0, 0, 0)
        local bones = StubScene.newNode("bodyBoneJoint", model, 0, 0, 0)
        local handle = StubScene.newNode("handleJoint", bones, 0.25, -0.06, 0.05)
        local yoke = StubScene.newNode("yokeJoint", bones, 0.25, -0.05, 0)
        local bail = StubScene.newNode("bailJoint", yoke, 0, -0.02, 0)
        local spool = StubScene.newNode("spoolJoint", bones, 0.27, -0.06, 0)
        local segment = StubScene.newNode("segment01Joint", bones, 0.6, 0, 0)
        local tip = StubScene.newNode("tipLine", segment, 1.3, 0, 0)
        StubScene.newNode("body", model, 0, 0, 0)
        tool.handNode = StubScene.newNode("handNode", model, 0.12, -0.01, 0)
        delete(tool.firstPersonNode)
        tool.firstPersonNode = StubScene.newNode("firstPersonNode", model, 0, 0, 0)
        local ropeFrame = StubScene.newNode("ropeReferenceFrame", root, 0, 0, 0)
        for _, name in ipairs({ "rope_10_100", "rope_20_200", "rope_40_400", "rope_60_600" }) do StubScene.newNode(name, ropeFrame, 0, 0, 0) end

        local spec = tool[SPEC]
        spec.bobber = StubScene.newNode("bobber", 0, 0, 0, 0)
        spec.bobberPhysicsHelper = StubScene.newNode("bobberPhysicsHelper", spec.bobber, 0, 0, 0)
        spec.tipLine = tip
        spec.tipLinePhysicsHelper = StubScene.newNode("fishingLineTipPhysicsHelper", tip, 0, 0, 0)
        spec.tipLineRopeVisualNode = StubScene.newNode("tipLineRopeVisualNode", tip, 0, 0, 0)
        spec.handle, spec.rotor, spec.spool, spec.bail, spec.mesh = handle, yoke, spool, bail, model
        spec.angleMax, spec.bendMax, spec.angleScale, spec.rodBendFactor = 2.2689280275926285, 0.2, 0, 0
        spec.reelInSoundSpeed, spec.spoolRecipricationValue = 0, 0
        spec.activateActionId, spec.activateActionSecondaryId = "activate", "activateSecondary"
        spec.actionEvents = {}
        spec.fishingLine = { isInitialized = false, visibility = false, ropeLength = 0, ropeLengthSumUp = 0, numActiveNodes = 0,
                             minLength = 2, maxLength = 40, nodeDistance = 0.5, diameter = 0.003, linkNode = root,
                             referenceFrame = ropeFrame }
        spec.item = nil
        local states, list = makeStates(tool)
        tool.rodStates = states
        spec.states = list
        spec.state = nil

        local typeFunctions = {}
        for _, name in ipairs({ "setState", "getBobberPosition", "getTipPosition", "getAngleScale", "getRodBendFactor",
                                "setRodBendFactor", "getCurrentItem", "setCurrentItem", "getExtendState" }) do
            typeFunctions[name] = Class[name]
            tool[name] = Class[name]
        end
        tool.type = { name = SPEC_NAME, functions = typeFunctions }

        local function event(actionName, callback)
            return { actionName = actionName, target = tool, callback = callback, triggerUp = true, triggerDown = true,
                     triggerAlways = false, isActive = true, isDown = false }
        end
        local primary = event("ACTIVATE_HANDTOOL", Class[StubHandTool.fishingRodHandlerName] or Class.onActivateAction)
        local secondary = event("ACTIVATE_HANDTOOL_SECONDARY", Class.onActivateSecondaryAction)
        tool.actionEvents = { primary, secondary }
        local function binding(entry)
            return function(self, _, inputValue)
                if not entry.isActive then return end
                local down = inputValue > 0
                if down ~= entry.isDown then entry.callback(entry.target, entry.actionName, inputValue) end
                entry.isDown = down
            end
        end
        tool.activateAction = binding(primary)
        tool.activateSecondaryAction = binding(secondary)

        table.insert(StubHandTool.fishingRods, tool)
        return tool
    end

    function StubHandTool.pressSecondaryBinding(tool, inputValue) tool:activateSecondaryAction(nil, inputValue) end

    function StubHandTool.rodFaceRay(tool)
        local x, y, z = getWorldTranslation(tool.rootNode)
        local tx, ty, tz = getWorldTranslation(tool[SPEC].tipLine)
        local ax, ay, az = normalize(tx - x, ty - y, tz - z)
        local sx, sy, sz = getWorldTranslation(tool[SPEC].spool)
        local vx, vy, vz = sx - x, sy - y, sz - z
        local along = vx * ax + vy * ay + vz * az
        local dx, dy, dz = normalize(vx - along * ax, vy - along * ay, vz - along * az)
        return x, y, z, dx, dy, dz
    end
