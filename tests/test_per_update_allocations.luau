_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

local HAND_FIELDS = { "tracked", "x", "y", "z", "pitch", "yaw", "roll", "qx", "qy", "qz", "qw",
    "isValid", "isPositionTracked", "isOrientationTracked", "gripPoseTracked",
    "gripPoseX", "gripPoseY", "gripPoseZ", "gripPosePitch", "gripPoseYaw", "gripPoseRoll",
    "gripPoseQx", "gripPoseQy", "gripPoseQz", "gripPoseQw",
    "trigger", "grip", "thumbX", "thumbY", "thumbClick", "buttonLower", "buttonUpper", "menu" }
local function hasAllFields(t)
    if type(t) ~= "table" then return false end
    for _, k in ipairs(HAND_FIELDS) do
        if t[k] == nil then return false end
    end
    return true
end
local HEAD_POSE_FIELDS = { "x", "y", "z", "pitch", "yaw", "roll", "qx", "qy", "qz", "qw",
    "isValid", "isPositionTracked", "isOrientationTracked" }
local PANEL_HIT_FIELDS = { "hit", "u", "v" }
local function hasFields(t, fields)
    if type(t) ~= "table" then return false end
    for _, k in ipairs(fields) do
        if t[k] == nil then return false end
    end
    return true
end
local reads = 0
local panelHits = 0
local menuCursorCalls = 0
local virtualGamepadReads = 0
_G.VRBridge = {
    version = "1.0.0.0",
    isReady = function() return true end,
    handshake = function() return { success = true, version = "1.0.0.0", mode = "OpenXR" } end,
    getHeadPose = function(out)
        if not hasFields(out, HEAD_POSE_FIELDS) then
            out = { x = 0.1, y = 0.05, z = -0.1, pitch = 0.15, yaw = 0.2, roll = 0.0,
                qx = 0.0, qy = 0.0, qz = 0.0, qw = 1.0,
                isValid = true, isPositionTracked = true, isOrientationTracked = true }
        end
        return out
    end,
    getEyeProjection = function() return { isValid = true, fovY = 1.8326, xOffset = 0.0, yOffset = 0.0 } end,
    getHandState = function(hand, out)
        if not hasAllFields(out) then
            out = { gripPoseTracked = true, thumbClick = false, buttonLower = false, buttonUpper = false, menu = false,
                    grip = 0, thumbX = 0, thumbY = 0, gripPoseX = 0, gripPoseY = 0, gripPoseZ = 0,
                    gripPosePitch = 0, gripPoseYaw = 0, gripPoseRoll = 0,
                    gripPoseQx = 0, gripPoseQy = 0, gripPoseQz = 0, gripPoseQw = 1,
                    pitch = 0, yaw = 0, roll = 0 }
        end
        reads = reads + 1
        local t = reads * 0.01
        out.tracked = true
        out.isValid, out.isPositionTracked, out.isOrientationTracked = true, true, true
        out.gripPoseTracked = true
        out.x, out.y, out.z = 0.2 * (hand + 1) + t, 1.0, -0.4
        out.qx, out.qy, out.qz, out.qw = 0.0, 0.0, 0.0, 1.0
        out.gripPoseX, out.gripPoseY, out.gripPoseZ = out.x, out.y, out.z
        out.gripPoseQx, out.gripPoseQy, out.gripPoseQz, out.gripPoseQw = 0.0, 0.0, 0.0, 1.0
        out.trigger = 0
        return out
    end,
    getFrameParity = function() return 0 end,
    getIpd = function() return 0.064 end,
    setMenuVisible = function() end,
    alignMenuQuad = function() end,
    setMenuCursor = function(shown, u, v) menuCursorCalls = menuCursorCalls + 1 end,
    getPanelHit = function(hand, out)
        if not hasFields(out, PANEL_HIT_FIELDS) then
            out = { hit = true, u = 0.5, v = 0.5 }
        end
        panelHits = panelHits + 1
        out.hit = true
        out.u = 0.4 + (panelHits % 10) * 0.01
        out.v = 0.5
        return out
    end,
    getVirtualGamepad = function()
        virtualGamepadReads = virtualGamepadReads + 1
        return true, 3, "present", false, false
    end,
    setVirtualGamepad = function() end,
    setVirtualGamepadUse = function() end,
    pulseHaptic = function() return true end,
    recenter = function() end,
    resetTemporalHistory = function() end,
    onMapUnload = function() end,
}
VRBridge = _G.VRBridge

local mouseX = 0.5
_G.g_inputBinding = {
    getMousePosition = function(self)
        mouseX = mouseX + 0.0001
        return mouseX, 0.5
    end,
}

local function newGui()
    return {
        currentGuiName = "",
        currentDialogName = "",
        dialogs = {},
        getIsGuiVisible = function(self) return self.currentGuiName ~= "" or #self.dialogs > 0 end,
    }
end
_G.g_gui = newGui()

loadCompanionMod()

local passed = 0
local failed = 0

local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        passed = passed + 1
        print(string.format("  [PASS] %s", name))
    else
        failed = failed + 1
        print(string.format("  [FAIL] %s: %s", name, tostring(err)))
    end
    g_gui = newGui()
    _G.g_gui = g_gui
end

VRCompanion.handshakeComplete = true
VRCompanion.passThroughMode = false
VRCompanion.headTrackingEnabled = true
VRCompanion.motionControllersEnabled = true

-- Bytes the Luau state grows by across `updates` calls of fn, after a warm-up that lets lazily made state settle.
-- One table is 56 bytes or more, so a table made per update shows as at least 56 * updates.
local function bytesAllocatedOver(updates, fn)
    for _ = 1, 20 do fn() end
    collectgarbage("collect")
    collectgarbage("stop")  -- nothing is freed while counting, so every allocation shows
    fn()
    local before = collectgarbage("count") * 1024
    for _ = 1, updates do fn() end
    local after = collectgarbage("count") * 1024
    collectgarbage("restart")
    return after - before
end

local UPDATES = 300
-- Slack for the odd string or number the engine of the test makes; a table or closure per update is 300 times more
local SLACK_BYTES = 512

print("Running Per-Update Allocation Tests...")

local DOWNSTREAM_UPDATES = { "updateVirtualGamepad", "updateWheelGrab", "updateHandTools", "updateObjectGrab",
    "updateInteractiveControl", "updateAnimatedObjects", "updateMenuPointer", "updateVirtualHands",
    "updateVirtualGamepadUse" }

test("TheMotionControllerPollAllocatesNothingWhileBothHandsMove", function()
    local originals = {}
    for _, name in ipairs(DOWNSTREAM_UPDATES) do
        originals[name] = VRCompanion[name]
        VRCompanion[name] = function() end
    end
    local moved = 0
    local ok, bytes = pcall(bytesAllocatedOver, UPDATES, function()
        VRCompanion:updateMotionControllers(16.6)
        if VRCompanion:isHandInUse(0) and VRCompanion:isHandInUse(1) then moved = moved + 1 end
    end)
    for name, original in pairs(originals) do VRCompanion[name] = original end
    assert(ok, tostring(bytes))
    assert(moved >= UPDATES, "both hands must count as moving (in use) every update, got " .. moved)
    assert(bytes <= SLACK_BYTES,
        string.format("the Motion Controller poll allocated %d bytes over %d updates", bytes, UPDATES))
end)

test("TheGuiVisibilityPollAllocatesNothingWhileAMenuIsOpen", function()
    g_gui.currentGuiName = "ingameMenu"
    VRCompanion:pollGuiVisibility()
    assert(VRCompanion:isMenuQuadShown() == true, "the menu shows as the quad")
    local bytes = bytesAllocatedOver(UPDATES, function() VRCompanion:pollGuiVisibility() end)
    assert(bytes <= SLACK_BYTES,
        string.format("the GUI visibility poll allocated %d bytes over %d polls with a menu open", bytes, UPDATES))
end)

test("TheGuiVisibilityPollAllocatesNothingWhileAStereoDialogShows", function()
    local root = { target = {}, elements = {} }
    table.insert(g_gui.dialogs, root)
    g_gui.currentDialogName = "FishDialog"
    VRCompanion:pollGuiVisibility()
    assert(VRCompanion:isStereoDialogShowing() == true, "the fish catch dialog is a Stereo Dialog")
    assert(VRCompanion:findStereoDialog() == root, "the dialog root is found")
    local bytes = bytesAllocatedOver(UPDATES, function() VRCompanion:pollGuiVisibility() end)
    assert(bytes <= SLACK_BYTES,
        string.format("the GUI visibility poll allocated %d bytes over %d polls with a Stereo Dialog showing", bytes, UPDATES))
end)

local function withGlobals(overrides, fn)
    local saved = {}
    for k, v in pairs(overrides) do
        saved[k] = _G[k]
        _G[k] = v
    end
    local ok, err = pcall(fn)
    for k, v in pairs(saved) do
        _G[k] = v
    end
    for k in pairs(overrides) do
        if saved[k] == nil then _G[k] = nil end
    end
    assert(ok, tostring(err))
end

test("TheVisibleHudUpdateAndMapRenderAllocateNothing", function()
    local renderedCount = 0
    local overlayObj = { x = 0.1, y = 0.1, width = 0.05, height = 0.05 }
    local mapObj = {
        isFullscreen = false,
        x = 0.0, y = 0.0, width = 0.2, height = 0.2,
        setPosition = function(self, x, y) self.x, self.y = x, y end,
        setScale = function() end,
        storeScaledValues = function() end,
        reposition = function() end,
        updatePosition = function() end,
        getWidth = function() return 0.2 end,
        getHeight = function() return 0.2 end,
        draw = function(self)
            _G.Overlay.render(overlayObj, 0.1, 0.1, 0.2, 0.2)
            _G.IngameMapLayoutCircle:drawBefore()
            self:drawHotspots()
        end,
        drawHotspots = function(self)
            _G.Overlay.render(overlayObj)
        end,
        getIsVisible = function() return true end,
    }
    local fillLevelsObj = {
        x = 0.8, y = 0.1, width = 0.1, height = 0.04,
        fillLevelData = { { fillLevel = 100 } },
        setPosition = function(self, x, y) self.x, self.y = x, y end,
        setScale = function() end,
        storeScaledValues = function() end,
        reposition = function() end,
        updatePosition = function() end,
        getWidth = function() return 0.1 end,
        getHeight = function() return 0.04 end,
        draw = function() end,
        getIsVisible = function() return true end,
    }
    local simpleDisplay = function()
        return {
            x = 0.5, y = 0.5, width = 0.1, height = 0.1,
            setPosition = function(self, x, y) self.x, self.y = x, y end,
            setScale = function() end,
            storeScaledValues = function() end,
            reposition = function() end,
            updatePosition = function() end,
            getWidth = function() return 0.1 end,
            getHeight = function() return 0.1 end,
            draw = function() end,
            getIsVisible = function() return true end,
        }
    end
    local speedMeterObj = simpleDisplay()
    local inputHelpObj = simpleDisplay()
    local gameInfoObj = simpleDisplay()
    local contextActionObj = simpleDisplay()
    local layoutCircleCls = {
        mapPosX = 0.05, mapPosY = 0.05,
        drawBefore = function(self) end,
        getMapObjectPosition = function() return 0.1, 0.1, 0, true end,
    }
    local ingameMapCls = {
        draw = mapObj.draw,
        drawHotspots = mapObj.drawHotspots,
        getIsVisible = mapObj.getIsVisible,
        getMapObjectPosition = function() return 0.1, 0.1, 0, true end,
    }
    local fillLevelsCls = { draw = fillLevelsObj.draw, getIsVisible = fillLevelsObj.getIsVisible }
    local speedMeterCls = { draw = function() end, getIsVisible = function() return true end }
    local inputHelpCls = { draw = function() end, getIsVisible = function() return true end, setVisible = function() end }
    local gameInfoCls = { draw = function() end, getIsVisible = function() return true end }
    local contextActionCls = { draw = function() end, getIsVisible = function() return true end }
    local hudCls = {
        draw = function(self)
            self.ingameMap:draw()
            self.fillLevels:draw()
        end,
        setIsVisible = function() end,
        setInputHelpVisible = function() end,
        setIngameMapVisible = function() end,
        setGameInfoVisible = function() end,
    }
    local hudInst = setmetatable({
        speedMeter = speedMeterObj,
        fillLevels = fillLevelsObj,
        inputHelp = inputHelpObj,
        gameInfo = gameInfoObj,
        ingameMap = mapObj,
        contextAction = contextActionObj,
    }, { __index = hudCls })
    setmetatable(mapObj, { __index = ingameMapCls })
    setmetatable(fillLevelsObj, { __index = fillLevelsCls })
    local overlayCls = {
        render = function(overlay, c1, c2, c3, c4) renderedCount = renderedCount + 1 end,
    }
    local guiCls = {
        draw = function() end,
    }
    withGlobals({
        g_currentMission = { hud = hudInst },
        HUD = hudCls,
        IngameMap = ingameMapCls,
        IngameMapLayoutCircle = layoutCircleCls,
        FillLevelsDisplay = fillLevelsCls,
        SpeedMeterDisplay = speedMeterCls,
        InputHelpDisplay = inputHelpCls,
        GameInfoDisplay = gameInfoCls,
        ContextActionDisplay = contextActionCls,
        Overlay = overlayCls,
        Gui = guiCls,
        g_safeFrameOffsetX = 0.02,
        g_safeFrameOffsetY = 0.02,
    }, function()
        VRCompanion.is2DViewActive = false
        VRCompanion:hookHud()
        VRCompanion:hookGuiDraw()
        VRCompanion:setHudState(VRCompanion.HUD_STATE_FULL)
        local root = { absPosition = { 0, 0 }, setAbsolutePosition = function(self, x, y) self.absPosition[1], self.absPosition[2] = x, y end }
        table.insert(g_gui.dialogs, root)
        g_gui.currentDialogName = "FishDialog"
        VRCompanion:pollGuiVisibility()
        local bytes = bytesAllocatedOver(UPDATES, function()
            VRCompanion.frameTicks = (VRCompanion.frameTicks or 0) + 1
            VRCompanion:updateHudConvergence(16.6)
            hudInst:draw()
            guiCls.draw(g_gui)
        end)
        VRCompanion:unhookGuiDraw()
        VRCompanion:unhookHud()
        VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
        assert(renderedCount >= UPDATES * 2, "map and hotspot overlays must render each update")
        assert(bytes <= SLACK_BYTES,
            string.format("visible HUD + map + Stereo Dialog allocated %d bytes over %d updates", bytes, UPDATES))
    end)
end)

test("TheMenuPointerAllocatesNothingWhileActive", function()
    withGlobals({
        g_time = 10000,
        g_inputBinding = { getMousePosition = function() return 0.5, 0.5 end },
        entityExists = function(node) return node ~= nil and node ~= 0 end,
        getWorldTranslation = function() return 0, 1.7, 0 end,
        getWorldQuaternion = function() return 0, 0, 0, 1 end,
        worldToLocal = function(_, x, y, z) return x, y - 1.7, z end,
        localToWorld = function(_, x, y, z) return x, y + 1.7, z end,
        getParent = function() return 1 end,
        getRootNode = function() return 1 end,
    }, function()
        VRCompanion.cyclopeanAudioListenerNode = 10
        g_gui.currentGuiName = "ingameMenu"
        g_gui.mouseEvent = function() end
        VRCompanion:pollGuiVisibility()
        VRCompanion:updateMotionControllers(16.6)
        VRCompanion:notePointerRayMoved()
        local beforeHits = panelHits
        local bytes = bytesAllocatedOver(UPDATES, function()
            g_time = g_time + 16
            VRCompanion:updateMotionControllers(16.6)
        end)
        VRCompanion:resetMenuPointer()
        VRCompanion:resetMotionControllers()
        VRCompanion:resetVirtualGamepad()
        assert(panelHits - beforeHits >= UPDATES, "Menu Pointer must hit the panel each update")
        assert(bytes <= SLACK_BYTES,
            string.format("active Menu Pointer allocated %d bytes over %d updates", bytes, UPDATES))
    end)
end)

test("ObjectGrabAllocatesNothingWhileActive", function()
    local handToolHandsCls = {
        TARGET_MASK = 1,
        onDraw = function(tool) end,
        updateKinematicNode = function(tool) end,
        onHeldItemJointBroken = function(tool, jointIndex) end,
        getCanPickUpNode = function(tool, node) return true end,
    }
    local handsInst = setmetatable({
        spec_hands = { pickupDistance = 2.0 },
        getIsHeld = function(self) return true end,
        getIsHoldingItem = function(self) return false end,
        getCarryingPlayer = function(self) return _G.g_localPlayer end,
    }, { __index = handToolHandsCls })
    local grabbableTarget = { node = 42, x = 0.2, y = 1.0, z = -1.0, distance = 1.2 }
    local localPlayerObj = {
        isOwner = true,
        camera = { isFirstPerson = true },
        hands = handsInst,
        getHeldHandTool = function(self) return nil end,
        getLookRay = function(player) return 0, 1.7, 0, 0, 0, -1 end,
        targeter = { closestTargetsByKey = { [handToolHandsCls] = grabbableTarget } },
    }
    withGlobals({
        g_time = 10000,
        g_inputBinding = { getMousePosition = function() return 0.5, 0.5 end },
        g_localPlayer = localPlayerObj,
        g_currentMission = { player = localPlayerObj },
        Player = { getLookRay = localPlayerObj.getLookRay },
        HandToolHands = handToolHandsCls,
        entityExists = function(node) return node ~= nil and node ~= 0 end,
        getWorldTranslation = function() return 0, 1.7, 0 end,
        getWorldQuaternion = function() return 0, 0, 0, 1 end,
        worldToLocal = function(_, x, y, z) return x, y - 1.7, z end,
        localToWorld = function(_, x, y, z) return x, y + 1.7, z end,
        getParent = function() return 1 end,
        getRootNode = function() return 1 end,
        raycastClosest = function(ox, oy, oz, dx, dy, dz, dist, cbName, cbTarget)
            cbTarget[cbName](cbTarget, 42, 0.2, 1.0, -1.0, 1.2)
            return true
        end,
    }, function()
        VRCompanion.cyclopeanAudioListenerNode = 10
        VRCompanion.is2DViewActive = false
        VRCompanion.trackedCameraShown = true
        VRCompanion:pollGuiVisibility()
        VRCompanion:runSubsystemMapLoad()
        VRCompanion:updateMotionControllers(16.6)
        VRCompanion:notePointerRayMoved()
        local peekCalls = 0
        local bytes = bytesAllocatedOver(UPDATES, function()
            g_time = g_time + 16
            VRCompanion:updateMotionControllers(16.6)
            if VRCompanion:getPointerHand() ~= nil and VRCompanion:getAimedLookRay(localPlayerObj) ~= nil then
                peekCalls = peekCalls + 1
            end
        end)
        VRCompanion:runSubsystemMapUnload()
        assert(peekCalls >= UPDATES, "Object Grab must actively aim each update")
        assert(bytes <= SLACK_BYTES,
            string.format("active Object Grab allocated %d bytes over %d updates", bytes, UPDATES))
    end)
end)

test("TheFishingRodOverlaysAllocateNothingWhileRendering", function()
    local rodSpecName = "spec_pdlc_highlandsFishingPack.fishingRod"
    local rodClass = {}
    local lastRenderedX = nil
    local overlayRenderOrig = function(overlay) lastRenderedX = overlay.x end
    local function makeOverlay(x, y)
        return {
            x = x, y = y, width = 0.05, height = 0.04, offsetX = 0, offsetY = 0, filename = "bar.png",
            setPosition = function(self, nx, ny) self.x, self.y = nx, ny end,
            render = function(self) _G.Overlay.render(self) end,
        }
    end
    local reelingState = {
        posX = 0.65, posY = 0.40, width = 0.10, height = 0.32,
        fishYMin = 0.41, fishYMax = 0.69,
        targetLineXStart = 0.69, targetLineXEnd = 0.73,
        hookLineX = 0.76, hookStatusLineXStart = 0.746, hookStatusLineXEnd = 0.7474,
        progressBarOverlay = makeOverlay(0.655, 0.41),
        fishTargetBarOverlay = makeOverlay(0.685, 0.41),
        fishOverlay = makeOverlay(0.69, 0.41),
        hookOverlay = makeOverlay(0.755, 0.37),
        caughtOverlay = makeOverlay(0.657, 0.672),
        lostOverlay = makeOverlay(0.658, 0.419),
    }
    local rodTool = {
        rootNode = 20,
        isHeld = true,
        getIsHeld = function(self) return self.isHeld end,
        getCarryingPlayer = function() return _G.g_localPlayer end,
        getBobberPosition = function() return 0, 0, 0 end,
        specializationNames = { "pdlc_highlandsFishingPack.fishingRod" },
        specializations = { rodClass },
        actionEvents = {},
    }
    rodTool[rodSpecName] = {
        state = reelingState,
        states = { reelingState },
        fishingLine = { isInitialized = true },
        rodBendFactor = 0.5,
    }
    reelingState.fishingRod = rodTool
    rodClass.onActivateAction = function(self, action, val) end
    rodClass.onDraw = function(self)
        local s = self[rodSpecName].state
        s.progressBarOverlay:render()
        s.fishOverlay:setPosition(s.fishOverlay.x, s.fishYMin)
        s.fishOverlay:render()
    end
    local localPlayerObj = {
        isOwner = true,
        camera = { isFirstPerson = true },
        getHeldHandTool = function(self) return rodTool end,
        getLookRay = function(player) return 0, 1.7, 0, 0, 0, -1 end,
    }
    withGlobals({
        g_time = 10000,
        g_localPlayer = localPlayerObj,
        g_currentMission = { player = localPlayerObj },
        Overlay = { render = overlayRenderOrig },
        HandTool = { attachToolToCamera = function(tool) end },
        Player = { getLookRay = localPlayerObj.getLookRay },
        PlayerCamera = { getRotation = function(camera) return 0, 0, 0 end },
        HandToolChainsaw = { onUpdate = function(tool, dt) end, onRollAction = function(tool, action, inputDelta) end, onDraw = function(tool) end },
        HandToolFlashlight = { onUpdate = function(flashlight, dt) end },
        HandToolSprayCan = { onDraw = function(tool) end },
        HandToolHorseBrush = { onDraw = function(tool) end, getHusbandryAndClusterFromNode = function(player, node) end, onBrushAction = function(tool, action, inputValue) end },
        entityExists = function(node) return node ~= nil and node ~= 0 end,
        getParent = function() return 1 end,
        getRootNode = function() return 1 end,
        getWorldTranslation = function() return 0, 1.7, 0 end,
        getWorldQuaternion = function() return 0, 0, 0, 1 end,
        localDirectionToWorld = function() return 0, 0, -1 end,
        link = function() end,
        setTranslation = function() end,
        setQuaternion = function() end,
    }, function()
        VRCompanion.is2DViewActive = false
        VRCompanion.trackedCameraShown = true
        VRCompanion:pollGuiVisibility()
        VRCompanion:runSubsystemMapLoad()
        _G.HandTool.attachToolToCamera(rodTool)
        VRCompanion:updateHandTools(16.6)
        local draws = 0
        local bytes = bytesAllocatedOver(UPDATES, function()
            VRCompanion:updateHandTools(16.6)
            rodClass.onDraw(rodTool)
            if lastRenderedX ~= nil and lastRenderedX < 0.69 then
                draws = draws + 1
            end
        end)
        local globalOverlayUnwrapped = (_G.Overlay.render == overlayRenderOrig)
        local restoredX = reelingState.posX
        VRCompanion:runSubsystemMapUnload()
        assert(draws >= UPDATES, "fishing overlays must render pulled in each frame")
        assert(math.abs(restoredX - 0.65) < 1e-6, "layout posX must be restored after draw")
        assert(globalOverlayUnwrapped, "global Overlay.render must not be wrapped outside active diagnostics/tracing")
        assert(bytes <= SLACK_BYTES,
            string.format("fishing rod overlay draw allocated %d bytes over %d frames", bytes, UPDATES))
    end)
end)

test("ActionEventPollingAllocatesNothingWhenEventsAreActive", function()
    local events = {}
    local id = 0
    local inputBinding = {
        events = events,
        registerActionEvent = function(self, actionId, target, fn)
            id = id + 1
            local evId = "ev_" .. tostring(id)
            events[evId] = { actionName = actionId }
            return true, evId
        end,
        beginActionEventsModification = function() end,
        endActionEventsModification = function() end,
        removeActionEvent = function(self, evId) events[evId] = nil end,
    }
    withGlobals({
        g_inputBinding = inputBinding,
    }, function()
        VRCompanion:registerActionEvents(true)
        local bytes = bytesAllocatedOver(UPDATES, function()
            VRCompanion.frameTicks = 30
            VRCompanion:updateActionEvents()
        end)
        assert(bytes <= SLACK_BYTES,
            string.format("active action event polling allocated %d bytes over %d polls", bytes, UPDATES))
    end)
end)

test("VirtualHandsAnimatedObjectsAndChainsawAllocateNothing", function()
    local nextNode = 100
    local parents = { [1] = 0, [10] = 2, [2] = 1 }
    local names = { [1] = "RootNode", [2] = "PlayerRoot", [10] = "Listener" }
    local visibilities = { [1] = true, [2] = true, [10] = true }
    local function newNode(name, parent)
        nextNode = nextNode + 1
        parents[nextNode] = parent or 0
        names[nextNode] = name
        visibilities[nextNode] = true
        return nextNode
    end
    local function makeHandTree()
        local fileRoot = newNode("fileRoot", 0)
        local handRoot = newNode("HandRoot", fileRoot)
        for _, f in ipairs(VRCompanion.VIRTUAL_HAND_FINGERS) do
            local base = newNode(f.base, handRoot)
            for _, j in ipairs(f.joints) do
                newNode(j, base)
            end
        end
        return fileRoot
    end
    local animatedObjCls = {
        triggerCallback = function(object, triggerId, otherId, onEnter, onLeave, onStay) end,
        getCanBeTriggered = function(object) return true end,
    }
    local animatedActivatableCls = {
        onAnimationInputToggle = function(activatable) end,
    }
    local doorPartNode = newNode("doorPart", 1)
    local doorObj = {
        isDeleted = false,
        controls = { posAction = "ACTIVATE", posActionEventId = "ev1" },
        activatable = { onAnimationInputToggle = animatedActivatableCls.onAnimationInputToggle },
        animation = { parts = { { node = doorPartNode } } },
        getCanBeTriggered = animatedObjCls.getCanBeTriggered,
    }
    local sawTool = {
        rootNode = newNode("chainsawRoot", 1),
        graphicalNode = newNode("chainsawVis", 1),
        isHeld = true,
        getIsHeld = function(self) return self.isHeld end,
        getCarryingPlayer = function() return _G.g_localPlayer end,
        spec_chainsaw = {
            currentCutState = 0,
            rollInput = 0,
            currentCutRoll = 0,
            cutNode = nil,
        },
    }
    local sawClass = {
        MINIMUM_CUT_DISTANCE = 0.5,
        onUpdate = function(tool, dt) end,
        onRollAction = function(tool, action, inputDelta) end,
        onDraw = function(tool) end,
    }
    local localPlayerObj = {
        isOwner = true,
        rootNode = 2,
        camera = { isFirstPerson = true, getRotation = function() return 0, 0, 0 end },
        getHeldHandTool = function() return sawTool end,
        getLookRay = function(player) return 0, 1.7, 0, 0, 0, -1 end,
    }
    withGlobals({
        g_time = 10000,
        g_inputBinding = { getMousePosition = function() return 0.5, 0.5 end },
        g_localPlayer = localPlayerObj,
        g_currentMission = {
            player = localPlayerObj,
            missionInfo = { isa = function() return true end },
        },
        FSCareerMissionInfo = {},
        CollisionMask = { ALL = 0xFFFFFFFF },
        CollisionFlag = { TRIGGER = 1, PLAYER = 2, TERRAIN = 4, TERRAIN_DELTA = 8, TERRAIN_DISPLACEMENT = 16 },
        AnimatedObject = animatedObjCls,
        AnimatedObjectActivatable = animatedActivatableCls,
        HandTool = { attachToolToCamera = function(tool) end },
        Player = { getLookRay = localPlayerObj.getLookRay },
        PlayerCamera = { getRotation = function(camera) return 0, 0, 0 end },
        HandToolChainsaw = sawClass,
        ChainsawCutState = { IDLE = 0, CUTTING = 1, DELIMBING = 2 },
        HandToolFlashlight = { onUpdate = function(flashlight, dt) end },
        HandToolSprayCan = { onDraw = function(tool) end },
        HandToolHorseBrush = { onDraw = function(tool) end, getHusbandryAndClusterFromNode = function(player, node) end, onBrushAction = function(tool, action, inputValue) end },
        loadI3DFile = function() return makeHandTree() end,
        clone = function() return makeHandTree() end,
        createTransformGroup = function(name) return newNode(name, 0) end,
        link = function(parent, child) parents[child] = parent end,
        delete = function() end,
        fileExists = function() return true end,
        entityExists = function(node) return node ~= nil and node ~= 0 end,
        getParent = function(node) return parents[node] or 0 end,
        getRootNode = function() return 1 end,
        getName = function(node) return names[node] or "" end,
        getNumOfChildren = function(node)
            local n = 0
            for _, p in pairs(parents) do if p == node then n = n + 1 end end
            return n
        end,
        getChildAt = function(node, idx)
            local i = 0
            for child, p in pairs(parents) do
                if p == node then
                    if i == idx then return child end
                    i = i + 1
                end
            end
            return 0
        end,
        getVisibility = function(node) return visibilities[node] ~= false end,
        setVisibility = function() end,
        getHasClassId = function() return false end,
        setShapeCastShadowmap = function() end,
        getQuaternion = function() return 0, 0, 0, 1 end,
        setQuaternion = function() end,
        setTranslation = function() end,
        setRotation = function() end,
        getWorldTranslation = function() return 0, 1.7, 0 end,
        getWorldQuaternion = function() return 0, 0, 0, 1 end,
        worldToLocal = function(_, x, y, z) return x, y - 1.7, z end,
        localToWorld = function(_, x, y, z) return x, y + 1.7, z end,
        localDirectionToWorld = function() return 0, 0, -1 end,
        raycastClosest = function(ox, oy, oz, dx, dy, dz, dist, cbName, cbTarget)
            cbTarget[cbName](cbTarget, doorPartNode, 0, 1.7, -1.0, 1.0)
            return true
        end,
    }, function()
        VRCompanion.cyclopeanAudioListenerNode = 10
        VRCompanion.is2DViewActive = false
        VRCompanion.trackedCameraShown = true
        VRCompanion:pollGuiVisibility()
        VRCompanion:runSubsystemMapLoad()
        animatedObjCls.triggerCallback(doorObj, 1, localPlayerObj.rootNode, true, false, false)
        _G.HandTool.attachToolToCamera(sawTool)
        local bytes = bytesAllocatedOver(UPDATES, function()
            g_time = g_time + 16
            VRCompanion:updateMotionControllers(16.6)
            VRCompanion:placeVirtualHandsForRender()
            sawClass.onUpdate(sawTool, 16.6)
            VRCompanion:updateAnimatedObjects(16.6, false)
        end)
        VRCompanion:runSubsystemMapUnload()
        assert(bytes <= SLACK_BYTES,
            string.format("Virtual Hands + Animated Objects + Chainsaw allocated %d bytes over %d updates", bytes, UPDATES))
    end)
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_per_update_allocations.luau", failed))
