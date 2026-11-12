-- Hand Tools: the fishing rod (ADR-0031)
-- The Highlands Fishing DLC's fishing rod for StubHandTool. The DLC's scripts cannot be read: this is shaped after the probe dump
-- of the real rod (#125: its specializations, class functions, action events, nodes and spec fields, and how spec.state changed
-- while fishing) and after play, not after source. Run after hand_tool.luau, then call StubHandTool.installFishingRod(variant) BEFORE
-- loading the Companion Mod, or any time before the map is loaded. A game without the DLC simply never calls it.
--
-- StubHandTool.installFishingRod(variant)   makes the DLC's specialization class. It has NO global name (the DLC's scripts run in
--   their own environment): it is StubHandTool.fishingRodClass, and the game reaches it only through each rod's
--   tool.specializations, next to its name "pdlc_highlandsFishingPack.fishingRod" in tool.specializationNames (with the storable
--   specialization before it, as in the dump). Every handler is on the class only, never on the tool, called with the rod as self.
--   variant   how the class's ACTIVATE_HANDTOOL handler is in this "DLC version": nil / "ok" (onActivateAction(rod, actionName,
--             inputValue)), "missing" (a DLC update renamed it to onPrimaryAction, which the game's binding calls instead) or
--             "reshaped" (onActivateAction takes no parameters, and acts as a press on the rod being held)
--   Class functions (the dump's): onActivateAction, onActivateSecondaryAction, onUpdate, onDraw, onHeldStart, onHeldEnd,
--     onRegisterActionEvents, onInputModeChanged, setState, getBobberPosition, getTipPosition, getAngleScale, getRodBendFactor,
--     getCurrentItem, ... The type's functions (getBobberPosition, getTipPosition, getAngleScale, getRodBendFactor, getCurrentItem,
--     setState, ...) are also copied onto each rod, as the game does; the action handlers, onUpdate and onDraw are not.
--   The class's onUpdate runs each StubHandTool.tick for the held rod, and its onDraw in StubHandTool.draw, both looked up on the
--   class at every call (the game's event dispatch).
--
-- StubHandTool.fishingRod(player, options) -> tool   a rod, not yet held. Its spec table is tool["spec_pdlc_highlandsFishingPack.fishingRod"]
--   (also StubHandTool.rodSpec(tool)); tool.spec_storable is the storable specialization's. In the spec, as in the dump:
--     state          the current state: ONE OF spec.states (11 tables), recognised only by its keys (each has `fishingRod`, the rod).
--                    nil when the game attaches the rod: the rod's first update while held sets it to idle (the headset showed
--                    the real rod's state missing at the attach, 2026-10-18):
--       idle        { fishingRod }                                   (1 key)
--       charging     angleScale (0 to 1 while held), angleScaleIncreasePerMs (0.0007), inputValue, castingChargeOverlay, ... (10 keys)
--       waiting      biteTime, biteTimeMin (10000), biteTimeMax (25000), biteHintTimeThreshold, waitForBiteOverlay, ... (10 keys)
--       bite         catchTime, duration (880, the hook window), minCatchTimeThreshold (700), glyphButtonText ("Hook fish"),
--                    hookFishOverlay, ... (12 keys)
--       reeling      the minigame's layout (48 keys; the #140 headset dump named them): posX, posY, width, height, fishYMin,
--                    fishYMax, cursorPos, targetLineXStart/End, hookLineX, hookStatusLineXStart/End, progressBarOverlay,
--                    fishTargetBarOverlay, fishOverlay, hookOverlay, ... None of them tells the state by itself; the reeling
--                    shows in rodBendFactor and fishingLine instead.
--       caught       { fishingRod, angleScaleDecreasePerMs, bendResetRate }  (3 keys)
--       (and five more states the dump never showed the rod in)
--     states         the 11 state tables
--     fishingLine    { isInitialized (true from the cast until the line is in), ropeLength, numActiveNodes, minLength (2),
--                    maxLength (40), visibility, ... }
--     rodBendFactor  0 unless reeling: while reeling, the fish's pull (options.fishPull, 0.5; StubHandTool.setFishPull) while the
--                    reel is held, a third of it while it is not
--     angleScale, angleMax (2.27), bendMax (0.2), item (the catch, nil until one), the nodes bobber, bobberPhysicsHelper, tipLine,
--     tipLinePhysicsHelper, handle (handleJoint), rotor (yokeJoint), spool (spoolJoint), bail (bailJoint), mesh,
--     activateActionId / activateActionSecondaryId
--   The nodes: fishingRodRoot -> fishingRod -> bodyBoneJoint -> handleJoint, yokeJoint -> bailJoint, spoolJoint, segment01Joint
--   -> tipLine (the rod's tip, 1.9 m along the root's +X; the reel below, at -Y); fishingRod -> body, handNode (the game's third person hold, a little up
--   the handle from the root), firstPersonNode; fishingRodRoot -> ropeReferenceFrame -> rope_10_100 ...; the bobber is linked to
--   the world. tool.handNode is the handNode (the stub's own guess: the i3d is in the packed DLC).
--   tool.actionEvents: ACTIVATE_HANDTOOL -> onActivateAction and ACTIVATE_HANDTOOL_SECONDARY -> onActivateSecondaryAction, both
--   on the class, both firing on press and release (not every frame). tool.activateAction(self, _, inputValue) and
--   tool.activateSecondaryAction(self, _, inputValue) are the game's bindings (X on a pad, the mouse): they call the event's
--   callback on those edges only.
--   options: fishPull (0.5)
--
-- What the rod does when its ACTIVATE_HANDTOOL handler is called (from play: hold to charge, let go to throw; the same action hooks
-- and reels). Each call is in StubHandTool.useCalls as { tool = "fishingRod", action = "ACTIVATE_HANDTOOL", value, self }:
--   idle, pressed      -> charging; each update the charge grows (angleScale += 0.0007 per ms, to 1)
--   charging, released -> the cast: the line goes out (isInitialized, ropeLength 2 + 20 * angleScale), the bobber lands ropeLength
--                         along the player's look ray as the game answers it now (Player:getLookRay: whether the real DLC aims
--                         that way is not known), and the rod waits. Each cast is in StubHandTool.casts as
--                         { angleScale, ray = { x, y, z, dirX, dirY, dirZ }, bobber = { x, y, z } }
--   waiting            -> a bite comes only when the test says: StubHandTool.biteRod(tool) (not by the clock)
--   bite, pressed      -> reeling (hooked); not pressed within the hook window (880 ms of updates) -> waiting again (got away)
--   reeling            -> while the reel is held (pressed, not yet released) each update takes 10 m per second off the line;
--                         at minLength the fish is caught: item set, the line in (isInitialized false), state caught
--   caught, pressed    -> idle (the catch dialog is closed), item nil
-- The secondary handler only records itself, as { tool = "fishingRod", action = "ACTIVATE_HANDTOOL_SECONDARY", value, self }:
-- what it does is not known yet.
-- The draw (onDraw, at the centre of the view): "castingChargeOverlay" while charging, "waitForBiteOverlay" while waiting and
-- "hookFishOverlay" in the hook window; nothing otherwise.
--
-- StubHandTool.rodState(tool) -> "idle" | "charging" | "waiting" | "bite" | "reeling" | "caught"   what the DLC is doing (the stub
--   knows its own state tables; the Companion Mod has only the keys)
-- StubHandTool.setRodState(tool, name)   puts the rod straight into a state, with the line and the bend that go with it
-- StubHandTool.biteRod(tool)   a fish bites (waiting -> bite)
-- StubHandTool.setFishPull(tool, pull)   how hard the fish pulls while reeling (0..1)
-- StubHandTool.rodFaceRay(tool) -> x, y, z, dirX, dirY, dirZ   the rod's root and the way its face points, in the world: square
--   to the rod, toward the side its reel hangs on (the way the tip swings in a cast)
-- StubHandTool.pressSecondaryBinding(tool, inputValue)   the game's own ACTIVATE_HANDTOOL_SECONDARY binding
-- StubHandTool.fishingRods   every rod made; StubHandTool.casts   every cast

    local SPEC_NAME = "pdlc_highlandsFishingPack.fishingRod"
    local SPEC = "spec_" .. SPEC_NAME
    StubHandTool.fishingRods, StubHandTool.casts = {}, {}
    local stateNames = {}  -- a state table -> its name (the stub's own knowledge; the tables carry no name)

    -- What reached the screen in a draw: { what (the overlay's file, or the text), x, y, width (0 for text) }.
    StubHandTool.screen = {}
    -- The game's Overlay (dataS/scripts/gui/base/Overlay.lua), as far as the rod's draw uses it: Overlay.new(filename, x, y, width,
    -- height), and overlay:render(), looked up on the class at every call, which puts it on the screen at x + offsetX, y + offsetY.
    -- Defined here unless another stub has.
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
    -- The game's natives for text and filled rectangles: the engine resolves natives at load, so a mod cannot wrap them (locals
    -- here, not globals). Each notes what reached the screen, a rectangle by the name the stub gives it.
    local function renderTextNative(x, y, size, text) table.insert(StubHandTool.screen, { what = tostring(text), x = x, y = y, width = 0 }) end
    local function drawRectNative(name, x, y, width, height) table.insert(StubHandTool.screen, { what = name, x = x, y = y, width = width }) end
    local reelHeld = {}    -- a rod -> its reel is held (pressed and not yet released)

    function StubHandTool.rodSpec(tool) return tool[SPEC] end
    function StubHandTool.rodState(tool) return stateNames[tool[SPEC].state] end

    local function normalize(x, y, z)
        local length = math.sqrt(x * x + y * y + z * z)
        if length < 1e-9 then return 0, 0, 1 end
        return x / length, y / length, z / length
    end

    -- the 11 state tables of a rod, by the stub's names; each has the keys the dump showed for it
    local function makeStates(tool)
        local overlay = function() return { width = 0.08, height = 0.08, visible = true } end  -- one the stub's draw does not render
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
            -- the minigame's layout, by the keys the #140 headset dump showed (its numbers rounded, and some of its keys left out)
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
        elseif not line.isInitialized then  -- waiting, bite, reeling: the line is out
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

    -- the cast: the line out, the bobber ropeLength along the look ray the game answers now
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

    -- what the ACTIVATE_HANDTOOL action does, by state
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
        StubHandTool.fishingRodClass = Class  -- no global name
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

        -- the type's functions, copied onto each rod by StubHandTool.fishingRod
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
        -- the specialization's events and registration (on the class only)
        for _, name in ipairs({ "onHeldStart", "onHeldEnd", "onRegisterActionEvents", "onInputModeChanged", "onLoad", "onDelete",
                                "onPlayerPreTeleport", "prerequisitesPresent", "registerEventListeners", "registerFunctions",
                                "registerXMLPaths" }) do
            Class[name] = function() end
        end

        -- the update: the charge grows, a missed hook window lets the fish go, a held reel takes the line in
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

        -- the draw: the charge meter, the waiting hint and the "Hook fish" prompt, at the centre of the view, each also put on the
        -- screen (StubHandTool.screen) from the state's Overlays or as text; and while reeling a minigame far to the right (the
        -- headset showed one there; its shape here is made up: a tension bar and its fill, Overlays, and a hint the native puts
        -- below the bar, at a place taken from the bar's Overlay, as the real minigame's panel and lines are)
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
                -- as the real minigame (#140 dump): the panel and the lines with the natives at the layout's numbers, the bars
                -- from the state's Overlays, and the fish and the hook placed again at every draw from where they are
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

        -- the nodes: the rod lies along its +X, root to tip, the reel below it at -Y (the headset, #140: the rod's +Z is its side)
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
        delete(tool.firstPersonNode)  -- the common tool's; the rod's is below its model
        tool.firstPersonNode = StubScene.newNode("firstPersonNode", model, 0, 0, 0)
        local ropeFrame = StubScene.newNode("ropeReferenceFrame", root, 0, 0, 0)
        for _, name in ipairs({ "rope_10_100", "rope_20_200", "rope_40_400", "rope_60_600" }) do StubScene.newNode(name, ropeFrame, 0, 0, 0) end

        -- the spec, as the dump showed it with the rod held and idle
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
        spec.state = nil  -- until the rod's first update while held

        -- the type's functions, copied onto the rod as the game does (not the handlers, the update or the draw)
        local typeFunctions = {}
        for _, name in ipairs({ "setState", "getBobberPosition", "getTipPosition", "getAngleScale", "getRodBendFactor",
                                "setRodBendFactor", "getCurrentItem", "setCurrentItem", "getExtendState" }) do
            typeFunctions[name] = Class[name]
            tool[name] = Class[name]
        end
        tool.type = { name = SPEC_NAME, functions = typeFunctions }

        -- the registered action events, both on the class, both on press and release
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
