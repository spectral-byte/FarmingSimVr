VRCompanion = VRCompanion or {}
VRCompanion.HandTools = VRCompanion.HandTools or {}

local engine = VRCompanion.getEngineGlobal

local HAND_NAMES = { [0] = "left", [1] = "right" }

local function newState()
    return {
        tool = nil,
        cameraPlacedTool = nil,
        linkedTool = nil,
        linkedHand = nil,
        linkedHolder = nil,
        reported = nil,
        givingBack = false,
        trigger = {
            tool = nil,
            entry = nil,
            hand = nil,
            down = false,
            active = false,
        },
        chainsaw = {
            tool = nil,
            yaw = nil,
            minimumWarned = false,
        },
        haptics = {
            level = nil,
            hand = nil,
            sinceMs = 0,
        },
        sprayCan = {
            tool = nil,
            typeIndex = nil,
            treeNode = nil,
        },
        fishingRod = {
            tool = nil,
            state = nil,
            cast = nil,      -- the last cast: { tool, rod = {dx, dy, dz} | nil, head = {dx, dy, dz} | nil, bobber = {x, y, z} | nil
                             -- (where it landed), bobberLast = {x, y, z} | nil (where it was at the last update, until it landed) }
            action = nil,    -- the last action a rod was given: { tool, name = "primary" | "secondary", by = text, value }
            draws = {},      -- per rod state: its last draw (see drawPulledIn)
        },
        toolProblems = {},
        checkedTools = setmetatable({}, { __mode = "k" }),  -- the tools toolProblem found as expected (not checked again)
        fieldsPending = nil, -- { tool, sinceMs }: the held tool whose fields were not yet as expected, since when (game clock)
    }
end

local state = newState()

VRCompanion.DEFAULT_HAND_TOOLS_ENABLED = true
VRCompanion.handToolsEnabled = VRCompanion.DEFAULT_HAND_TOOLS_ENABLED

local SETTING_KEY_ENABLED = "vrSettings.handTools"

function VRCompanion:saveHandToolsSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY_ENABLED, self.handToolsEnabled ~= false)
    end
    self:saveToolHandSetting(xmlFile, xml)
    self:saveChainsawReachSetting(xmlFile, xml)
end

function VRCompanion:loadHandToolsSetting(xmlFile, xml)
    local loaded = nil
    if type(xml.getBool) == "function" then
        loaded = xml.getBool(xmlFile, SETTING_KEY_ENABLED)
    end
    if loaded == nil and type(xml.getString) == "function" then
        local str = xml.getString(xmlFile, SETTING_KEY_ENABLED)
        if str ~= nil then loaded = VRCompanion.parseOnOffArgument(str) end
    end
    if loaded ~= nil then
        self.handToolsEnabled = loaded
    end
    self:loadToolHandSetting(xmlFile, xml)
    self:loadChainsawReachSetting(xmlFile, xml)
end

local SETTING_KEY_TOOL_HAND = "vrSettings.toolHand"
local TOOL_HAND_BY_NAME = { left = 0, right = 1 }

function VRCompanion:saveToolHandSetting(xmlFile, xml)
    if type(xml.setString) == "function" then
        xml.setString(xmlFile, SETTING_KEY_TOOL_HAND, HAND_NAMES[self:getToolHand()])
    end
end

function VRCompanion:loadToolHandSetting(xmlFile, xml)
    if type(xml.getString) ~= "function" then return end
    local str = xml.getString(xmlFile, SETTING_KEY_TOOL_HAND)
    local hand = type(str) == "string" and TOOL_HAND_BY_NAME[string.lower(str)] or nil
    if hand ~= nil then self.toolHand = hand end
end

local SETTING_KEY_CHAINSAW_REACH = "vrSettings.chainsawReach"
local CHAINSAW_REACH_GAME = "game"  -- the word for the default, accepted as a synonym of `default`
VRCompanion.MAX_CHAINSAW_REACH = 3  -- metres; the game's targeter does not find a trunk much further from the bar than this anyway
local CHAINSAW_REACH_USAGE = "Usage: vrChainsawReach [<metres, 0 to 3> | default | game] (saved). default and game: the game's own reach; vrChainsawReach 0.15 for contact-only cutting"

-- What a text names as the reach: ok, the setting (nil: the game's own reach, for `default` and `game`; or a number of metres, at
-- most the maximum) and whether the metres were cut down to the maximum. Not ok for anything else: a word, a negative number, a
-- number that is not finite.
local function parseChainsawReach(text)
    local word = string.lower(tostring(text))
    if word == "default" or word == CHAINSAW_REACH_GAME then return true, nil, false end
    local metres = tonumber(word)
    if metres == nil or metres ~= metres or metres == math.huge or metres < 0 then return false end
    return true, math.min(metres, VRCompanion.MAX_CHAINSAW_REACH), metres > VRCompanion.MAX_CHAINSAW_REACH
end

function VRCompanion:saveChainsawReachSetting(xmlFile, xml)
    if type(xml.setString) ~= "function" then return end
    local reach = self.chainsawReach
    xml.setString(xmlFile, SETTING_KEY_CHAINSAW_REACH, type(reach) == "number" and string.format("%g", reach) or "default")
end

function VRCompanion:loadChainsawReachSetting(xmlFile, xml)
    if type(xml.getString) ~= "function" then return end
    local str = xml.getString(xmlFile, SETTING_KEY_CHAINSAW_REACH)
    if str == nil then return end
    local ok, reach = parseChainsawReach(str)
    self.chainsawReach = ok and reach or nil
end

local function chainsawTipReach()
    local reach = VRCompanion.chainsawReach
    if type(reach) == "number" then return reach end
    return nil
end

local FISHING_ROD_KIND = "fishingrod"

local function toggleFlashlight(tool)
    local spec = tool.spec_flashlight
    local on = not (spec ~= nil and spec.isActive == true)
    tool:setFlashlightIsActive(on)
    local player = type(tool.getCarryingPlayer) == "function" and tool:getCarryingPlayer() or nil
    if type(player) == "table" and type(player.isFlashlightActive) == "boolean" then player.isFlashlightActive = on end
end

local SUPPORTED_TOOLS = {
    { kind = "lance", label = "pressure washer lance", spec = "spec_highPressureWasherLance", useHandler = "onWashAction" },
    { kind = "flashlight", label = "flashlight", spec = "spec_flashlight", toggle = toggleFlashlight },
    { kind = "chainsaw", label = "chainsaw", spec = "spec_chainsaw", useHandler = "onCutAction", useHandlerClass = "HandToolChainsaw", useOnce = true },
    { kind = "spraycan", label = "spray can", spec = "spec_sprayCan", useHandler = "activateSpraying" },
    { kind = "horsebrush", label = "horse brush", spec = "spec_horseBrush", useHandler = "onBrushAction", useHandlerClass = "HandToolHorseBrush" },
    { kind = FISHING_ROD_KIND, label = "fishing rod", spec = "spec_pdlc_highlandsFishingPack.fishingRod", useHandler = "onActivateAction",
      useHandlerSpecialization = "pdlc_highlandsFishingPack.fishingRod", useOnce = true,
      fields = {
          { name = "state", type = "table" },
          { name = "fishingLine", type = "table" },
          { name = "fishingLine.isInitialized", type = "boolean" },
          { name = "rodBendFactor", type = "number" },
          { name = "getBobberPosition", type = "function", onTool = true },
      } },
}

VRCompanion.HAND_TOOL_HANDLE_OFFSETS = {
    lance = {
        position = { x = 0.0, y = -0.04, z = 0.0 },
        rotation = { x = 0.0, y = 0.7071068, z = -0.7071068, w = 0.0 },
    },
    flashlight = {
        position = { x = 0.0, y = 0.0, z = -0.02 },
        rotation = { x = 0.0, y = 0.9238795, z = -0.3826834, w = 0.0 },
    },
    chainsaw = {
        position = { x = 0.056, y = -0.276, z = -0.232 },
        rotation = { x = 0.0, y = 0.9238795, z = -0.3826834, w = 0.0 },
    },
    spraycan = {
        position = { x = -0.01, y = 0.0, z = 0.05 },
        rotation = { x = 0.0, y = 0.7071068, z = -0.7071068, w = 0.0 },
    },
    horsebrush = {
        position = { x = 0.03, y = 0.009, z = -0.033 },
        rotation = { x = 0.0, y = 0.0, z = -0.7071068, w = 0.7071068 },
    },
    fishingrod = {
        position = { x = 0.0, y = 0.0, z = -0.1 },
        rotation = { x = 0.0, y = 0.7071068, z = 0.0, w = 0.7071068 },
    },
}

local function copyOffset(offset)
    return {
        position = { x = offset.position.x, y = offset.position.y, z = offset.position.z },
        rotation = { x = offset.rotation.x, y = offset.rotation.y, z = offset.rotation.z, w = offset.rotation.w },
    }
end

local DEFAULT_HANDLE_OFFSETS = {}
for kind, offset in pairs(VRCompanion.HAND_TOOL_HANDLE_OFFSETS) do
    DEFAULT_HANDLE_OFFSETS[kind] = copyOffset(offset)
end

local function chainsawMinimumCutDistance()
    local class = engine("HandToolChainsaw")
    local minimum = type(class) == "table" and class.MINIMUM_CUT_DISTANCE or nil
    if type(minimum) ~= "number" or minimum ~= minimum then return nil end
    return minimum
end

local function chainsawReachBehind(tool)
    local minimum = chainsawMinimumCutDistance()
    if minimum == nil and not state.chainsaw.minimumWarned then
        state.chainsaw.minimumWarned = true
        print("[FS25_VR] [Warning] Hand Tools: the chainsaw's minimum cut distance (HandToolChainsaw.MINIMUM_CUT_DISTANCE)"
            .. " could not be read. The chainsaw's ray is not moved back and its reach is the game's own.")
    end
    return minimum
end

local function chainsawLowerEdge(tool)
    local spec, model, root = tool.spec_chainsaw, tool.graphicalNode, tool.rootNode
    local cutNode = type(spec) == "table" and spec.cutNode or nil
    if cutNode == nil or model == nil or root == nil or cutNode == root then return nil end
    local ok, y = pcall(function()
        local node = cutNode
        while node ~= model do
            node = engine("getParent")(node)
            if node == nil or node == 0 or node == root then return nil end
        end
        local x, y, z = engine("getWorldTranslation")(cutNode)
        x, y, z = engine("worldToLocal")(model, x, y, z)
        x, y, z = engine("localToWorld")(tool.graphicalNodeParent or root, x, y, z)
        local _, rootY = engine("worldToLocal")(root, x, y, z)
        return rootY
    end)
    if not ok or type(y) ~= "number" or y ~= y or y == math.huge or y == -math.huge then return nil end
    return { x = 0.0, y = y, z = 0.0 }
end

VRCompanion.HAND_TOOL_POINTING_AXES = {
    lance = {
        node = function(tool) return tool.spec_highPressureWasherLance.raycastNode end,
        direction = { x = 0.0, y = 0.0, z = 1.0 },
    },
    flashlight = {
        node = function(tool) return tool.spec_flashlight.lightNodeParent end,
        position = function(tool)
            local spec = tool.spec_flashlight
            return { x = spec.lightNodeOffsetX or 0.0, y = spec.lightNodeOffsetY or 0.0, z = spec.lightNodeOffsetZ or 0.0 }
        end,
        direction = function(tool)
            local spec = tool.spec_flashlight
            return { x = spec.lightNodeForwardX or 0.0, y = spec.lightNodeForwardY or 0.0, z = spec.lightNodeForwardZ or 1.0 }
        end,
    },
    chainsaw = {
        node = function(tool) return tool.rootNode end,
        position = chainsawLowerEdge,
        direction = { x = 0.0, y = 0.0, z = 1.0 },
        reachBehind = chainsawReachBehind,
        barLength = 0.4,
        tipReach = 0.15,
    },
    spraycan = {
        node = function(tool) return tool.spec_sprayCan.canNode end,
        direction = { x = 0.0, y = 0.0, z = 1.0 },
        reachBehind = 0.5,
    },
    horsebrush = {
        node = function(tool) return tool.spec_horseBrush.brushNode end,
        direction = { x = 0.0, y = -1.0, z = 0.0 },
        reachBehind = 0.5,
        touchReach = 0.25,
    },
    fishingrod = {
        node = function(tool) return tool.rootNode end,
        direction = { x = 0.0, y = -1.0, z = 0.0 },
    },
}

local function supportedKind(tool)
    if type(tool) ~= "table" then return nil end
    for _, entry in ipairs(SUPPORTED_TOOLS) do
        if tool[entry.spec] ~= nil then return entry.kind, entry end
    end
    return nil
end

local function specializationClass(tool, name)
    local names, classes = tool.specializationNames, tool.specializations
    if type(names) ~= "table" or type(classes) ~= "table" then return nil end
    for index, specName in ipairs(names) do
        if specName == name then
            local class = classes[index]
            return type(class) == "table" and class or nil
        end
    end
    return nil
end

local function useHandlerOf(tool, entry)
    local handler = tool[entry.useHandler]
    if type(handler) ~= "function" and entry.useHandlerClass ~= nil then
        local class = engine(entry.useHandlerClass)
        handler = type(class) == "table" and class[entry.useHandler] or nil
    end
    if type(handler) ~= "function" and entry.useHandlerSpecialization ~= nil then
        local class = specializationClass(tool, entry.useHandlerSpecialization)
        handler = class ~= nil and class[entry.useHandler] or nil
    end
    return handler
end

local WRAP_SPECS = {
    attachToolToCamera = { class = "HandTool", parameters = 1, describe = "(tool)" },
    getLookRay = { class = "Player", parameters = 1, describe = "(player)", shared = "lookRay" },
    chainsawOnUpdate = { class = "HandToolChainsaw", fn = "onUpdate", parameters = 2, describe = "(tool, dt)" },
    chainsawOnRoll = { class = "HandToolChainsaw", fn = "onRollAction", parameters = 3, describe = "(tool, action, inputDelta)" },
    flashlightBeam = { class = "HandToolFlashlight", fn = "onUpdate", parameters = 2, describe = "(flashlight, dt)" },
    sprayCanOnDraw = { class = "HandToolSprayCan", fn = "onDraw", parameters = 1, describe = "(tool)" },
    chainsawOnDraw = { class = "HandToolChainsaw", fn = "onDraw", parameters = 1, describe = "(tool)" },
    horseBrushOnDraw = { class = "HandToolHorseBrush", fn = "onDraw", parameters = 1, describe = "(tool)" },
    horseBrushLookup = { class = "HandToolHorseBrush", fn = "getHusbandryAndClusterFromNode", parameters = 2, describe = "(player, node)" },
    horseBrushOnBrushAction = { class = "HandToolHorseBrush", fn = "onBrushAction", parameters = 3, describe = "(tool, action, inputValue)" },
    playerCameraGetRotation = { class = "PlayerCamera", fn = "getRotation", parameters = 1, describe = "(camera)" },
}

local wraps = VRCompanion.GameWraps.newSet({
    owner = "handTools",
    giveUp = "Hand Tools disabled: %s. Hand Tools are unchanged.",
    functions = WRAP_SPECS,
})

local function wrapAll()
    wraps:wrapAll()
end

local function checkWraps()
    return wraps:check()
end

local USE_HANDLER_SHAPE = { parameters = 3, describe = "(tool, action, inputValue)" }

-- The value at a dotted path ("fishingLine.isInitialized") below a table, or nil where the path breaks off. May raise (a reshaped
-- table can): toolFieldsProblem reads guarded.
local function valueAt(t, path)
    for key in string.gmatch(path, "[^%.]+") do
        if type(t) ~= "table" then return nil end
        t = t[key]
    end
    return t
end

local function toolFieldsProblem(tool, entry)
    if entry.fields == nil then return nil end
    local spec = tool[entry.spec]
    for _, field in ipairs(entry.fields) do
        local where = field.onTool and (entry.useHandlerSpecialization or entry.kind) or entry.spec
        local ok, value = pcall(valueAt, field.onTool and tool or spec, field.name)
        local problem = nil
        if not ok then
            problem = "reading it raised (" .. tostring(value) .. ")"
        elseif value == nil then
            problem = "it is missing"
        elseif type(value) ~= field.type then
            problem = "it is a " .. type(value) .. ", not a " .. field.type
        end
        if problem ~= nil then return where .. "." .. field.name .. ": " .. problem end
    end
    return nil
end

local TOOL_FIELDS_GRACE_MS = 5000

local function toolProblem(tool, entry, nowMs)
    if entry.useHandlerSpecialization == nil and entry.fields == nil then return nil end
    local known = state.toolProblems[entry.kind]
    if known ~= nil then return known end
    if state.checkedTools[tool] then return nil end
    local problem = nil
    if entry.useHandlerSpecialization ~= nil then
        problem = VRCompanion.GameWraps.shapeProblem(useHandlerOf(tool, entry), USE_HANDLER_SHAPE.parameters, USE_HANDLER_SHAPE.describe)
        if problem ~= nil then problem = entry.useHandlerSpecialization .. "." .. entry.useHandler .. ": " .. problem end
    end
    if problem == nil then
        problem = toolFieldsProblem(tool, entry)
        local pending = state.fieldsPending
        if problem ~= nil and (pending == nil or pending.tool ~= tool) then
            state.fieldsPending = { tool = tool, sinceMs = nowMs }
            return nil
        elseif problem ~= nil and nowMs - pending.sinceMs < TOOL_FIELDS_GRACE_MS then
            return nil
        end
        state.fieldsPending = nil
    end
    if problem == nil then
        state.checkedTools[tool] = true
        return nil
    end
    state.toolProblems[entry.kind] = problem
    print("[FS25_VR] [Warning] Hand Tools: the " .. entry.label .. " is left to the game: " .. problem .. ". The " .. entry.label
        .. " is unchanged.")
    return problem
end

function VRCompanion:getToolHand()
    return self.toolHand or self.MOTION_HAND_RIGHT
end

local function heldTool()
    local tool = state.tool
    if tool == nil then return nil end
    if type(tool.getIsHeld) ~= "function" or tool.rootNode == nil or not tool:getIsHeld() then
        state.tool = nil
        state.fieldsPending = nil
        return nil
    end
    return tool
end

function VRCompanion:getHeldHandTool()
    local tool = heldTool()
    if tool == nil then return nil, nil end
    return tool, (supportedKind(tool))
end

local function carryingPlayer(tool)
    local player = type(tool.getCarryingPlayer) == "function" and tool:getCarryingPlayer() or nil
    return type(player) == "table" and player or nil
end

local localPlayer = VRCompanion.getLocalPlayer

local function localCarryingPlayer(tool)
    local player = carryingPlayer(tool)
    return (player ~= nil and player == localPlayer()) and player or nil
end

local function isAnotherPlayersTool(tool)
    local player = carryingPlayer(tool)
    return player ~= nil and player ~= localPlayer()
end

local function toolHandFor(self, tool)
    if self.handToolsEnabled == false then return nil, "Hand Tools are off" end
    if self.motionControllersEnabled == false then return nil, "Motion Controllers are off" end
    if wraps:problem() ~= nil then return nil, "Hand Tools gave up (" .. wraps:problem() .. ")" end
    if state.givingBack then return nil, "the tool is being given back to the game" end
    if tool == nil then return nil, "no Hand Tool is held" end
    local kind, entry = supportedKind(tool)
    if kind == nil then return nil, "the Hand Tool is not supported" end
    local player = localCarryingPlayer(tool)
    if player == nil then return nil, "the Hand Tool is not the local player's" end
    if player.camera == nil or player.camera.isFirstPerson ~= true then
        return nil, "not in the first person view"
    end
    local problem = toolProblem(tool, entry, self:getCurrentTimeMs())
    if problem ~= nil then return nil, "the " .. entry.label .. " is left to the game (" .. problem .. ")" end
    local hand = self:getToolHand()
    local pose = self:getHandState(hand)
    if pose.tracked ~= true then return nil, "the Tool Hand is not tracked" end
    if not self:isHandInUse(hand) then return nil, "the Tool Hand is not in use" end
    if pose.gripPoseTracked ~= true then return nil, "the Tool Hand's grip pose is not tracked" end
    if not self:isHeadPlacedInTheWorld() then return nil, "the head is not placed in the world" end
    if self:getVirtualHandHolder(hand) == nil then
        return nil, "the Virtual Hand is not there"
    end
    return hand, nil
end

function VRCompanion:isHandHoldingHandTool(hand)
    local tool = heldTool()
    if tool == nil then return false end
    return toolHandFor(self, tool) == hand
end

local function forgetLink()
    state.linkedTool, state.linkedHand, state.linkedHolder = nil, nil, nil
end

local function linkIntoHand(self, tool, hand)
    local holder = self:getVirtualHandHolder(hand)
    local kind = supportedKind(tool)
    local offset = kind ~= nil and self.HAND_TOOL_HANDLE_OFFSETS[kind] or nil
    if holder == nil or offset == nil then return false end
    local root = tool.rootNode
    engine("link")(holder, root)
    engine("setTranslation")(root, offset.position.x, offset.position.y, offset.position.z)
    engine("setQuaternion")(root, offset.rotation.x, offset.rotation.y, offset.rotation.z, offset.rotation.w)
    state.linkedTool, state.linkedHand, state.linkedHolder = tool, hand, holder
    return true
end

local function reportPlacement(tool, hand)
    local text = hand ~= nil and string.format("Hand Tools: the %s is in the %s hand", tostring(supportedKind(tool)), HAND_NAMES[hand])
        or "Hand Tools: the Hand Tool is where the game puts it"
    if text ~= state.reported then
        state.reported = text
        print("[FS25_VR] " .. text)
    end
end

local function returnToTheGame(tool)
    forgetLink()
    state.givingBack = true
    local ok, err = pcall(function()
        if type(tool.attachTool) == "function" then
            tool:attachTool()
        elseif type(tool.attachToolToCamera) == "function" then
            tool:attachToolToCamera()
        end
    end)
    state.givingBack = false
    if not ok then error(err, 0) end
end

WRAP_SPECS.attachToolToCamera.make = function(original)
    return function(tool, ...)
        if type(tool) == "table" and tool.spec_hands == nil and not isAnotherPlayersTool(tool) then
            state.tool = tool
            state.cameraPlacedTool = tool
            local hand = toolHandFor(VRCompanion, tool)
            if hand ~= nil and linkIntoHand(VRCompanion, tool, hand) then
                reportPlacement(tool, hand)
                return
            end
        end
        return original(tool, ...)
    end
end

local function callGuarded(entry, what, fn, ...)
    local ok, err = pcall(fn, ...)
    if not ok and state.useError ~= tostring(err) then
        state.useError = tostring(err)
        print("[FS25_VR] [Warning] Hand Tools: the " .. entry.label .. "'s " .. what .. " failed: " .. tostring(err))
    end
end

local function callUseHandler(tool, entry, value)
    local handler = useHandlerOf(tool, entry)
    if type(handler) ~= "function" then return end
    if entry.kind == FISHING_ROD_KIND then
        state.fishingRod.action = { tool = tool, name = "primary", by = "the Tool Hand's trigger", value = value }
    end
    callGuarded(entry, "use handler", handler, tool, "ACTIVATE_HANDTOOL", value)
end

local function callToggle(tool, entry)
    callGuarded(entry, "switch", entry.toggle, tool)
end

local function endTriggerUse()
    local use = state.trigger
    if use.active and use.tool ~= nil and use.entry ~= nil then callUseHandler(use.tool, use.entry, 0) end
    use.tool, use.entry, use.hand, use.down, use.active = nil, nil, nil, false, false
end

local function updateTrigger(self)
    local use = state.trigger
    if self:doesMenuTakeInput() and self:isStereoDialogShowing() then
        endTriggerUse()
        return
    end
    local tool = heldTool()
    local kind, entry = nil, nil
    if tool ~= nil then kind, entry = supportedKind(tool) end
    local hand = nil
    if entry ~= nil and (entry.useHandler ~= nil or entry.toggle ~= nil) then hand = toolHandFor(self, tool) end
    if hand == nil then
        endTriggerUse()
        return
    end
    if use.tool ~= tool or use.hand ~= hand then
        endTriggerUse()
        use.tool, use.entry, use.hand = tool, entry, hand
    end
    local wasDown = use.down
    local mine = self:takeVirtualGamepadInput(hand, "trigger")
    if mine then
        local value = self:getHandState(hand).trigger or 0
        use.down = value >= (use.down and self.POINTER_TRIGGER_RELEASE or self.POINTER_TRIGGER_PRESS)
    else
        use.down = false
    end
    if entry.toggle ~= nil then
        if use.down and not wasDown then callToggle(tool, entry) end
    elseif use.down then
        if not use.active or not entry.useOnce then callUseHandler(tool, entry, 1) end
        use.active = true
    elseif use.active then
        callUseHandler(tool, entry, 0)
        use.active = false
    end
end

local function fishingRodOf(tool)
    if tool == nil then tool = heldTool() end
    local kind, entry = supportedKind(tool)
    if kind ~= FISHING_ROD_KIND then return nil end
    return tool, entry
end

local FISHING_ROD_STATE_KEYS = {
    { key = "angleScale", state = "charging" },
    { key = "biteTime", state = "waiting" },
    { key = "catchTime", state = "bite" },
    { key = "bendResetRate", state = "caught" },
}

local function readFishingRod(tool, specName)
    local spec = tool[specName]
    if type(spec) ~= "table" then return nil end
    local current, line, bend = spec.state, spec.fishingLine, spec.rodBendFactor
    if type(current) ~= "table" or type(line) ~= "table" or type(bend) ~= "number" then return nil end
    local lineOut = line.isInitialized
    if type(lineOut) ~= "boolean" then return nil end
    for _, known in ipairs(FISHING_ROD_STATE_KEYS) do
        if current[known.key] ~= nil then return known.state, lineOut, bend end
    end
    if lineOut then
        if bend > 0 then return "reeling", lineOut, bend end
        return nil
    end
    for key in pairs(current) do
        if key ~= "fishingRod" then return nil end
    end
    return "idle", lineOut, bend
end

function VRCompanion:getFishingRodState(tool)
    local rod, entry = fishingRodOf(tool)
    if rod == nil then return nil end
    local ok, rodState, lineOut, bend = pcall(readFishingRod, rod, entry.spec)
    if not ok or rodState == nil then return "unknown", false, 0 end
    return rodState, lineOut, bend
end

local function isTriggerUsing(hand)
    local use = state.trigger
    return use.active and use.hand == hand
end

local function isLineOutInHand(self, hand)
    local tool = heldTool()
    if tool == nil or state.linkedTool ~= tool or state.linkedHand ~= hand then return false end
    local _, lineOut = self:getFishingRodState(tool)
    return lineOut == true
end

local function isActiveFlashlightInHand(hand)
    local tool = heldTool()
    if tool == nil or state.linkedTool ~= tool or state.linkedHand ~= hand then return false end
    local spec = tool.spec_flashlight
    return type(spec) == "table" and spec.isActive == true
end

function VRCompanion:isHandKeptInUseByHandTool(hand)
    if self:getHandState(hand).tracked ~= true then return false end
    return isTriggerUsing(hand) or isLineOutInHand(self, hand) or isActiveFlashlightInHand(hand)
end

local FISHING_ROD_STATE_TEXT = { waiting = "line out" }

local FISHING_ROD_SECONDARY_ACTION = "ACTIVATE_HANDTOOL_SECONDARY"

-- The secondary event wraps: the table that holds the callback -> { original, wrapper } (weak: an event the game let go of goes).
local rodEventWraps = setmetatable({}, { __mode = "k" })

local function wrapRodSecondaryEvent(tool)
    local events = tool.actionEvents
    if type(events) ~= "table" then return end
    local inputBinding = engine("g_inputBinding")
    for key, entry in pairs(events) do
        if type(entry) == "table" then
            local bound = type(inputBinding) == "table" and type(inputBinding.events) == "table"
                and entry.actionEventId ~= nil and inputBinding.events[entry.actionEventId] or nil
            if type(bound) ~= "table" then bound = nil end
            local actionName = entry.actionName or (bound ~= nil and bound.actionName) or (type(key) == "string" and key or nil)
            if actionName == FISHING_ROD_SECONDARY_ACTION then
                local holders = { entry }
                if bound ~= nil then holders[2] = bound end
                for _, holder in ipairs(holders) do
                    local original = holder.callback
                    local wrapped = rodEventWraps[holder]
                    if type(original) == "function" and (wrapped == nil or wrapped.wrapper ~= original) then
                        local wrapper = function(target, name, inputValue, ...)
                            state.fishingRod.action = { tool = tool, name = "secondary", by = "the game's binding", value = inputValue }
                            return original(target, name, inputValue, ...)
                        end
                        rodEventWraps[holder] = { original = original, wrapper = wrapper }
                        holder.callback = wrapper
                    end
                end
            end
        end
    end
end

local function liftRodEventWraps()
    if next(rodEventWraps) == nil then return end
    for holder, wrapped in pairs(rodEventWraps) do
        if holder.callback == wrapped.wrapper then holder.callback = wrapped.original end
    end
    rodEventWraps = setmetatable({}, { __mode = "k" })
end

VRCompanion.FISHING_ROD_OVERLAY_PULL = 0.5
VRCompanion.FISHING_ROD_OVERLAY_DROP = 0.05

local FISHING_ROD_LAYOUT_X = { "posX", "targetLineXStart", "targetLineXEnd", "hookLineX", "hookStatusLineXStart", "hookStatusLineXEnd" }
local FISHING_ROD_LAYOUT_Y = { "posY", "fishYMin", "fishYMax" }

local rodDrawWrap = nil        -- { class, original, wrapper }: the rod class's onDraw, while this module has it wrapped
local overlayRenderWrap = nil  -- { class, original, wrapper }: Overlay.render, likewise
local rodDrawNotes = nil       -- while the local rod's draw runs: what it rendered (see drawPulledIn)

local function isOverlay(value)
    return type(value) == "table" and type(value.x) == "number" and type(value.y) == "number"
        and (value.overlayId ~= nil or value.filename ~= nil)
end

local function rodOverlays(tool, spec)
    local found, seen = {}, { [tool] = true }
    if type(spec.states) == "table" then seen[spec.states] = true end
    local function look(t, depth)
        for _, value in pairs(t) do
            if type(value) == "table" and not seen[value] then
                seen[value] = true
                if isOverlay(value) then
                    found[#found + 1] = value
                elseif depth > 0 then
                    look(value, depth - 1)
                end
            end
        end
    end
    look(spec, 0)
    if type(spec.state) == "table" then look(spec.state, 1) end
    return found
end

local function layoutShift(layout)
    local x, y, width = layout.posX, layout.posY, layout.width
    if type(x) ~= "number" or type(y) ~= "number" or type(width) ~= "number" then return nil end
    local middle = x + width / 2
    return 0.5 + (middle - 0.5) * VRCompanion.FISHING_ROD_OVERLAY_PULL - middle, -VRCompanion.FISHING_ROD_OVERLAY_DROP
end

local function renderOverlayNoted(original, overlay, ...)
    local notes = rodDrawNotes
    if notes ~= nil and type(overlay) == "table" and type(overlay.x) == "number" and type(overlay.y) == "number" then
        local moved = notes.moving[overlay] == true
        local x = overlay.x + (tonumber(overlay.offsetX) or 0) - (moved and notes.dx or 0)
        local y = overlay.y + (tonumber(overlay.offsetY) or 0) - (moved and notes.dy or 0)
        local width, height = tonumber(overlay.width) or 0, tonumber(overlay.height) or 0
        notes.calls = notes.calls + 1
        if not moved then notes.unmoved = notes.unmoved + 1 end
        notes.from, notes.to = math.min(notes.from or x, x), math.max(notes.to or (x + width), x + width)
        notes.bottom, notes.top = math.min(notes.bottom or y, y), math.max(notes.top or (y + height), y + height)
    end
    return original(overlay, ...)
end

local function moveNumber(moved, t, key, by)
    local value = t[key]
    if type(value) ~= "number" then return end
    t[key] = value + by
    moved[#moved + 1] = { t = t, key = key, was = value, by = by }
end

local function takeMovesOff(moved)
    for _, move in ipairs(moved) do
        local value = move.t[move.key]
        if value == move.was + move.by then
            move.t[move.key] = move.was
        elseif type(value) == "number" then
            move.t[move.key] = value - move.by
        end
    end
end

-- Runs the rod's own onDraw with its minigame moved (see above) and notes what it drew: { calls, unmoved, from, to, bottom, top
-- (the area rendered, as the rod placed it), overlays, numbers (how many were moved), dx, dy (by how much) }.
local function drawPulledIn(original, tool, ...)
    local _, entry = fishingRodOf(tool)
    local spec = tool[entry.spec]
    local notes = { calls = 0, unmoved = 0, overlays = 0, numbers = 0, moving = {}, dx = 0, dy = 0 }
    local moved = {}
    local layout = type(spec) == "table" and spec.state or nil
    local dx, dy = nil, nil
    if type(layout) == "table" then dx, dy = layoutShift(layout) end
    if dx ~= nil then
        notes.dx, notes.dy = dx, dy
        for _, key in ipairs(FISHING_ROD_LAYOUT_X) do moveNumber(moved, layout, key, dx) end
        for _, key in ipairs(FISHING_ROD_LAYOUT_Y) do moveNumber(moved, layout, key, dy) end
        notes.numbers = #moved
        local ok, overlays = pcall(rodOverlays, tool, spec)
        for _, overlay in ipairs(ok and overlays or {}) do
            moveNumber(moved, overlay, "x", dx)
            moveNumber(moved, overlay, "y", dy)
            notes.moving[overlay] = true
            notes.overlays = notes.overlays + 1
        end
    end
    local outer = rodDrawNotes
    rodDrawNotes = notes
    local results = table.pack(pcall(original, tool, ...))
    rodDrawNotes = outer
    takeMovesOff(moved)
    notes.moving = nil
    state.fishingRod.draws[VRCompanion:getFishingRodState(tool) or "unknown"] = notes
    if not results[1] then error(results[2], 0) end
    return table.unpack(results, 2, results.n)
end

local function pullsRodOverlaysIn(tool)
    return VRCompanion.handToolsEnabled ~= false and VRCompanion.is2DViewActive ~= true and fishingRodOf(tool) ~= nil
        and localCarryingPlayer(tool) ~= nil
end

local function wrapFunctionOn(held, class, name, make)
    if type(class) ~= "table" or type(class[name]) ~= "function" then return held end
    if held ~= nil and held.class == class and class[name] == held.wrapper then return held end
    local original = class[name]
    local wrapper = make(original)
    class[name] = wrapper
    return { class = class, original = original, wrapper = wrapper }
end

local function wrapRodDraw(tool)
    local _, entry = fishingRodOf(tool)
    local class = entry ~= nil and specializationClass(tool, entry.useHandlerSpecialization) or nil
    rodDrawWrap = wrapFunctionOn(rodDrawWrap, class, "onDraw", function(original)
        return function(drawn, ...)
            if pullsRodOverlaysIn(drawn) then return drawPulledIn(original, drawn, ...) end
            return original(drawn, ...)
        end
    end)
    overlayRenderWrap = wrapFunctionOn(overlayRenderWrap, engine("Overlay"), "render", function(original)
        return function(...) return renderOverlayNoted(original, ...) end
    end)
end

local function liftWrapOn(held, name)
    if held ~= nil and held.class[name] == held.wrapper then held.class[name] = held.original end
end

local function liftRodDrawWrap()
    liftWrapOn(rodDrawWrap, "onDraw")
    liftWrapOn(overlayRenderWrap, "render")
    rodDrawWrap, overlayRenderWrap, rodDrawNotes = nil, nil, nil
end

local function rodDrawsText()
    local parts = {}
    for _, name in ipairs({ "idle", "charging", "waiting", "bite", "reeling", "caught", "unknown" }) do
        local notes = state.fishingRod.draws[name]
        if notes ~= nil then
            local label = FISHING_ROD_STATE_TEXT[name] or name
            local moved = notes.numbers == 0 and notes.overlays == 0 and "nothing moved"
                or string.format("%d layout numbers and %d Overlays moved by %.3f, %.3f", notes.numbers, notes.overlays, notes.dx, notes.dy)
            if notes.calls == 0 then
                parts[#parts + 1] = string.format("%s: nothing rendered, %s", label, moved)
            else
                parts[#parts + 1] = string.format("%s: %d rendered (%d unmoved), x %.3f to %.3f, y %.3f to %.3f, %s", label, notes.calls,
                    notes.unmoved, notes.from, notes.to, notes.bottom, notes.top, moved)
            end
        end
    end
    return #parts > 0 and table.concat(parts, "; ") or "none yet"
end

local function rodValueText(value)
    if type(value) == "number" then return string.format("%.4f", value) end
    if isOverlay(value) then
        return string.format("Overlay %s (%.4f, %.4f, %.4f, %.4f)", tostring(value.filename), value.x, value.y, tonumber(value.width) or 0,
            tonumber(value.height) or 0)
    end
    if type(value) == "table" then
        local count = 0
        for _ in pairs(value) do count = count + 1 end
        return "table (" .. count .. " keys)"
    end
    if type(value) == "string" then return string.format("%q", string.sub(value, 1, 40)) end
    return tostring(value)
end

local function rodLayoutLines(tool)
    local ok, lines = pcall(function()
        local _, entry = fishingRodOf(tool)
        local spec = tool[entry.spec]
        local function fields(t, keep)
            local keys = {}
            for key, value in pairs(t) do
                if type(key) == "string" and keep(value) then keys[#keys + 1] = key end
            end
            table.sort(keys)
            for i, key in ipairs(keys) do keys[i] = key .. " = " .. rodValueText(t[key]) end
            return table.concat(keys, ", ")
        end
        return {
            "fishing rod reeling state: " .. fields(spec.state, function(value) return value ~= tool end),
            "fishing rod spec numbers: " .. fields(spec, function(value) return type(value) == "number" or isOverlay(value) end),
        }
    end)
    return ok and lines or {}
end

local function threeNumbersFrom(first, fn, ...)
    if type(fn) ~= "function" then return nil end
    local answer = { pcall(fn, ...) }
    if not answer[1] then return nil end
    local a, b, c = answer[first + 1], answer[first + 2], answer[first + 3]
    if type(a) ~= "number" or type(b) ~= "number" or type(c) ~= "number" then return nil end
    return { a, b, c }
end

local function headDirection(player)
    return threeNumbersFrom(4, wraps:original("getLookRay") or player.getLookRay, player)
end

local function bobberPosition(tool)
    return threeNumbersFrom(1, tool.getBobberPosition, tool)
end

local function fishingRodInTheHand(self)
    local tool = fishingRodOf()
    if tool == nil or toolHandFor(self, tool) == nil then return nil end
    return tool
end

local BOBBER_AT_REST_SPEED = 1.0

local function watchFishingRod(self, dt)
    local inTheHand = fishingRodInTheHand(self)
    if inTheHand ~= nil then pcall(wrapRodSecondaryEvent, inTheHand) else liftRodEventWraps() end
    local rodNotes = state.fishingRod
    local tool = fishingRodOf()
    local player = tool ~= nil and localCarryingPlayer(tool) or nil
    if player ~= nil and self.handToolsEnabled ~= false then pcall(wrapRodDraw, tool) else liftRodDrawWrap() end
    if player == nil then
        rodNotes.tool, rodNotes.state = nil, nil
        return
    end
    local rodState = self:getFishingRodState(tool)
    if rodNotes.tool == tool and rodNotes.state == "charging" and rodState ~= "charging" then
        local _, _, _, dx, dy, dz = self:getHandToolRay(player)
        rodNotes.cast = { tool = tool, rod = dx ~= nil and { dx, dy, dz } or nil, head = headDirection(player) }
    end
    local cast = rodNotes.cast
    if cast ~= nil and cast.tool == tool and rodState == "waiting" and cast.bobber == nil then
        local position = bobberPosition(tool)
        local last, seconds = cast.bobberLast, (tonumber(dt) or 0) / 1000
        if position ~= nil and last ~= nil and seconds > 0 then
            local dx, dy, dz = position[1] - last[1], position[2] - last[2], position[3] - last[3]
            if math.sqrt(dx * dx + dy * dy + dz * dz) <= BOBBER_AT_REST_SPEED * seconds then
                cast.bobber = position
            end
        end
        cast.bobberLast = position
    end
    rodNotes.tool, rodNotes.state = tool, rodState
end

local function vectorText(label, vector)
    return string.format("%s (%.2f, %.2f, %.2f)", label, vector[1], vector[2], vector[3])
end

local function lastCastText(tool)
    local cast = state.fishingRod.cast
    if cast == nil or cast.tool ~= tool then return "none yet" end
    return table.concat({
        cast.rod ~= nil and vectorText("rod direction", cast.rod) or "rod direction: none (the rod was not in the Tool Hand)",
        cast.head ~= nil and vectorText("head direction", cast.head) or "head direction: unknown",
        cast.bobber ~= nil and vectorText("bobber at", cast.bobber)
            or "bobber: not landed (not seen at rest while the line was out waiting for a bite)",
    }, ", ")
end

local function lastActionText(tool)
    local action = state.fishingRod.action
    if action == nil or action.tool ~= tool then return "none yet" end
    return string.format("%s, by %s (input %s)", action.name, action.by, tostring(action.value))
end

local function fishingRodLines(self)
    local ok, result = pcall(function()
        local tool = fishingRodInTheHand(self)
        if tool == nil then return {} end
        local rodState, lineOut = self:getFishingRodState(tool)
        local lines = {
            "fishing rod state: " .. (FISHING_ROD_STATE_TEXT[rodState] or tostring(rodState)),
            "fishing rod last cast: " .. lastCastText(tool),
            "fishing rod last action: " .. lastActionText(tool),
            "fishing rod screen draw: " .. rodDrawsText(),
        }
        if rodState == "reeling" or (rodState == "unknown" and lineOut) then
            for _, line in ipairs(rodLayoutLines(tool)) do lines[#lines + 1] = line end
        end
        return lines
    end)
    if ok then return result end
    return { "fishing rod: could not be described (" .. tostring(result) .. ")" }
end

local function resolveForTool(tool, value)
    if type(value) == "function" then return value(tool) end
    return value
end

local function reachBehindOf(tool, axis)
    local ok, reach = pcall(resolveForTool, tool, axis.reachBehind)
    return ok and tonumber(reach) or nil
end

-- Where the tool points now: the world position of its pointing node and the world direction of its pointing axis, then the
-- HAND_TOOL_POINTING_AXES entry they came from; nil when the tool has no pointing axis or the node is not there.
local function toolPointingRay(self, tool)
    local kind = supportedKind(tool)
    local axis = kind ~= nil and self.HAND_TOOL_POINTING_AXES[kind] or nil
    if axis == nil then return nil end
    local node = axis.node(tool)
    if node == nil then return nil end
    local direction, position = resolveForTool(tool, axis.direction), resolveForTool(tool, axis.position)
    local x, y, z
    if position ~= nil then
        x, y, z = engine("localToWorld")(node, position.x, position.y, position.z)
    else
        x, y, z = engine("getWorldTranslation")(node)
    end
    local dx, dy, dz = engine("localDirectionToWorld")(node, direction.x, direction.y, direction.z)
    if type(x) ~= "number" or type(dx) ~= "number" then return nil end
    return x, y, z, dx, dy, dz, axis
end

-- The pointing ray of the tool in the Tool Hand and the HAND_TOOL_POINTING_AXES entry it comes from (the six numbers, then the
-- entry), or nil: the one place that decides whether there is a ray for `player` (getHandToolRay, getHandToolLookRay).
local function heldToolPointingRay(self, player)
    local tool = heldTool()
    if tool == nil or state.linkedTool ~= tool then return nil end
    if player ~= nil and tool:getCarryingPlayer() ~= player then return nil end
    if toolHandFor(self, tool) == nil then return nil end
    local ok, x, y, z, dx, dy, dz, axis = pcall(toolPointingRay, self, tool)
    if not ok then return nil end
    return x, y, z, dx, dy, dz, axis
end

function VRCompanion:getHandToolRay(player)
    local x, y, z, dx, dy, dz = heldToolPointingRay(self, player)
    return x, y, z, dx, dy, dz
end

function VRCompanion:getHandToolLookRay(player)
    local x, y, z, dx, dy, dz, axis = heldToolPointingRay(self, player)
    if x == nil then return nil end
    local behind = reachBehindOf(heldTool(), axis) or 0
    if self:getChainsawCutState(heldTool()) == "cutting" then behind = 0 end
    return x - dx * behind, y - dy * behind, z - dz * behind, dx, dy, dz
end

VRCompanion:registerLookRayAimer("handTools", function(player)
    return VRCompanion:getHandToolLookRay(player)
end)

function VRCompanion:getChainsawCutState(tool)
    if tool == nil then tool = heldTool() end
    if type(tool) ~= "table" or tool.spec_chainsaw == nil then return nil end
    local states = engine("ChainsawCutState")
    if type(states) ~= "table" then return nil end
    local current = tool.spec_chainsaw.currentCutState
    if current == states.CUTTING then return "cutting" end
    if current == states.DELIMBING then return "delimbing" end
    return "idle"
end

local function chainsawInTheHand(tool)
    if type(tool) ~= "table" or tool.spec_chainsaw == nil or state.linkedTool ~= tool then return false end
    return toolHandFor(VRCompanion, tool) ~= nil
end

local function wristRoll(tool)
    local ok, x, _, _, dx, dy, dz = pcall(toolPointingRay, VRCompanion, tool)
    if not ok or x == nil then return nil end
    local ux, uy, uz = -dy * dx, 1 - dy * dy, -dy * dz
    local length = math.sqrt(ux * ux + uy * uy + uz * uz)
    if length < 1e-3 then return nil end
    ux, uy, uz = ux / length, uy / length, uz / length
    local tx, ty, tz = engine("localDirectionToWorld")(tool.rootNode, 0, 1, 0)
    if type(tx) ~= "number" then return nil end
    local sine = (uy * tz - uz * ty) * dx + (uz * tx - ux * tz) * dy + (ux * ty - uy * tx) * dz
    return math.atan2(sine, ux * tx + uy * ty + uz * tz)
end

function VRCompanion:getChainsawCutRoll(tool)
    if not chainsawInTheHand(tool) then return nil end
    return wristRoll(tool)
end

local function barYaw(tool)
    local ok, x, _, _, dx, _, dz = pcall(toolPointingRay, VRCompanion, tool)
    if not ok or x == nil or dx * dx + dz * dz < 1e-6 then return nil end
    return math.atan2(dx, dz)
end

local function updateChainsawFromTheTool(original, tool, dt, ...)
    local spec = tool.spec_chainsaw
    local states = engine("ChainsawCutState")
    local cutting = type(states) == "table" and spec.currentCutState == states.CUTTING
    local chainsaw = state.chainsaw
    if chainsaw.tool ~= tool then chainsaw.tool, chainsaw.yaw = tool, nil end
    if spec.rollInput ~= nil then spec.rollInput = 0 end

    if not cutting then
        chainsaw.yaw = barYaw(tool) or chainsaw.yaw
        local roll = VRCompanion:getChainsawCutRoll(tool)
        if type(roll) == "number" then spec.currentCutRoll = math.max(-math.pi * 0.5, math.min(math.pi * 0.5, roll)) end
    end

    local player = tool:getCarryingPlayer()
    local camera = player ~= nil and player.camera or nil
    local yaw = chainsaw.yaw
    if yaw ~= nil and type(camera) == "table" and type(camera.getRotation) == "function" then
        local own = rawget(camera, "getRotation")
        local gameGetRotation = camera.getRotation
        camera.getRotation = function(self, ...)
            local pitch, _, roll = gameGetRotation(self, ...)
            return pitch, yaw, roll
        end
        local ok, err = pcall(original, tool, dt, ...)
        camera.getRotation = own
        if not ok then error(err, 0) end
    else
        original(tool, dt, ...)
    end

    if tool.graphicalNode ~= nil and type(states) == "table" and spec.currentCutState ~= states.CUTTING then
        engine("setRotation")(tool.graphicalNode, 0, 0, 0)
    end
end

local function chainsawReachFromTheBase(tool)
    local axis = VRCompanion.HAND_TOOL_POINTING_AXES.chainsaw
    local behind = reachBehindOf(tool, axis)
    if behind == nil then return nil end
    local length, past = tonumber(axis.barLength), chainsawTipReach()
    if length == nil or past == nil then return nil, behind end
    return length + past, behind
end

local function chainsawTargetLimit(tool)
    local reach, behind = chainsawReachFromTheBase(tool)
    if reach == nil then return nil end
    return behind + reach
end

local function stopCutOutOfReach(tool)
    local states = engine("ChainsawCutState")
    local spec = tool.spec_chainsaw
    if type(states) ~= "table" or spec.currentCutState ~= states.CUTTING then return end
    local reach = chainsawReachFromTheBase(tool)
    if reach == nil or spec.cameraRotationNode == nil or type(tool.stopCutting) ~= "function" then return end
    local ok, x, y, z = pcall(toolPointingRay, VRCompanion, tool)
    if not ok or x == nil then return end
    local cutX, cutY, cutZ = engine("getWorldTranslation")(spec.cameraRotationNode)
    if type(cutX) ~= "number" then return end
    local dx, dy, dz = cutX - x, cutY - y, cutZ - z
    if math.sqrt(dx * dx + dy * dy + dz * dz) > reach then tool:stopCutting(false) end
end

local function updateChainsawWithinReach(original, tool, ...)
    pcall(stopCutOutOfReach, tool)
    local ok, targeterAnswers, chainsawType, hiddenTree = pcall(function()
        local player = tool:getCarryingPlayer()
        local targeter = type(player) == "table" and player.targeter or nil
        local closestTargets = type(targeter) == "table" and targeter.closestTargetsByKey or nil
        local chainsawClass = engine("HandToolChainsaw")
        local closestTree = type(closestTargets) == "table" and chainsawClass ~= nil and closestTargets[chainsawClass] or nil
        local limit = chainsawTargetLimit(tool)
        local states = engine("ChainsawCutState")
        local cutting = type(states) == "table" and tool.spec_chainsaw.currentCutState == states.CUTTING
        if cutting then return nil end
        if type(closestTree) ~= "table" or limit == nil or (tonumber(closestTree.distance) or 0) <= limit then return nil end
        closestTargets[chainsawClass] = nil
        return closestTargets, chainsawClass, closestTree
    end)
    if not ok or targeterAnswers == nil then return updateChainsawFromTheTool(original, tool, ...) end
    local done, result = pcall(updateChainsawFromTheTool, original, tool, ...)
    targeterAnswers[chainsawType] = hiddenTree
    if not done then error(result, 0) end
    return result
end

WRAP_SPECS.chainsawOnUpdate.make = function(original)
    return function(tool, ...)
        if chainsawInTheHand(tool) then return updateChainsawWithinReach(original, tool, ...) end
        return original(tool, ...)
    end
end

WRAP_SPECS.chainsawOnRoll.make = function(original)
    return function(tool, ...)
        if chainsawInTheHand(tool) then return end
        return original(tool, ...)
    end
end

local function placeFlashlightBeam(tool)
    local spec = type(tool) == "table" and tool.spec_flashlight or nil
    if spec == nil or spec.lightNode == nil or heldTool() ~= tool then return end
    local x, y, z, dx, dy, dz = VRCompanion:getHandToolRay()
    if x == nil then return end
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 1e-6 then return end
    local ux, uy, uz = 0, 1, 0
    if math.abs(dy) > 0.99 * length then
        uy, uz = 0, 1
    end
    engine("setWorldTranslation")(spec.lightNode, x, y, z)
    engine("setDirection")(spec.lightNode, dx, dy, dz, ux, uy, uz)
end

WRAP_SPECS.flashlightBeam.make = function(original)
    return function(tool, ...)
        local result = original(tool, ...)
        pcall(placeFlashlightBeam, tool)
        return result
    end
end

function VRCompanion:placeHandToolsForRender()
    local tool = state.linkedTool
    if tool ~= nil and tool.spec_flashlight ~= nil then pcall(placeFlashlightBeam, tool) end
end

local function drawIsHeldBack(tool)
    if type(tool) ~= "table" or state.linkedTool ~= tool then return false end
    return toolHandFor(VRCompanion, tool) ~= nil
end

local function makeDrawWrap(original)
    return function(tool, ...)
        if drawIsHeldBack(tool) then return end
        return original(tool, ...)
    end
end

WRAP_SPECS.sprayCanOnDraw.make = makeDrawWrap
WRAP_SPECS.chainsawOnDraw.make = makeDrawWrap
WRAP_SPECS.horseBrushOnDraw.make = makeDrawWrap

local function touchLimit(kind)
    local axis = kind ~= nil and VRCompanion.HAND_TOOL_POINTING_AXES[kind] or nil
    if axis == nil or tonumber(axis.touchReach) == nil then return nil end
    return (tonumber(axis.reachBehind) or 0) + axis.touchReach
end

local function brushTargetDistance(player)
    local targeter = type(player) == "table" and player.targeter or nil
    local closest = type(targeter) == "table" and type(targeter.closestTargetsByKey) == "table"
        and targeter.closestTargetsByKey[engine("HandToolHorseBrush")] or nil
    return type(closest) == "table" and tonumber(closest.distance) or nil
end

local function animalIsOutOfTouch(player)
    local tool = heldTool()
    if tool == nil or state.linkedTool ~= tool then return false end
    local kind = supportedKind(tool)
    if kind ~= "horsebrush" then return false end
    if type(player) ~= "table" or tool:getCarryingPlayer() ~= player or toolHandFor(VRCompanion, tool) == nil then return false end
    local limit, distance = touchLimit(kind), brushTargetDistance(player)
    return limit ~= nil and distance ~= nil and distance > limit
end

WRAP_SPECS.horseBrushLookup.make = function(original)
    return function(player, ...)
        local ok, outOfTouch = pcall(animalIsOutOfTouch, player)
        if ok and outOfTouch then return nil, nil end
        return original(player, ...)
    end
end

local DRAW_HELD_BACK_KINDS = { spraycan = true, chainsaw = true, horsebrush = true }

VRCompanion.HAND_TOOL_HAPTIC_PULSES = {
    running = { ms = 70, strength = 0.2 },
    cutting = { ms = 70, strength = 0.6 },
    spraying = { ms = 70, strength = 0.35 },
    misting = { ms = 70, strength = 0.2 },
    brushing = { ms = 70, strength = 0.15 },
    biting = { ms = 40, strength = 0.9, intervalMs = 150 },
    reeling = { ms = 70, strength = 0.8 },
}
VRCompanion.HAND_TOOL_HAPTIC_INTERVAL_MS = 50
-- The single short pulse that is not a buzz: the spray can's tick when its nozzle comes onto a tree (see "The spray can's feedback").
VRCompanion.HAND_TOOL_TICK_PULSE = { ms = 30, strength = 0.4 }

VRCompanion.HAND_TOOL_HAPTICS = {
    chainsaw = function(self, tool)
        local cutState = self:getChainsawCutState(tool)
        if cutState == nil then return nil end
        if cutState == "idle" then return nil end
        return cutState == "cutting" and "cutting" or "running"
    end,
    lance = function(self, tool)
        local spec = tool.spec_highPressureWasherLance
        return (spec ~= nil and spec.isActivated == true) and "spraying" or nil
    end,
    spraycan = function(self, tool)
        local spec = tool.spec_sprayCan
        return (spec ~= nil and spec.isSpraying == true) and "misting" or nil
    end,
    horsebrush = function(self, tool)
        local spec = tool.spec_horseBrush
        return (spec ~= nil and spec.targetedHusbandryId ~= nil) and "brushing" or nil
    end,
    fishingrod = function(self, tool)
        local rodState, _, bend = self:getFishingRodState(tool)
        if rodState == "bite" then return "biting" end
        if rodState == "reeling" then
            local full = self.HAND_TOOL_HAPTIC_PULSES.reeling.strength
            return "reeling", full * math.max(0, math.min(1, tonumber(bend) or 0))
        end
        return nil
    end,
}

local function updateHaptics(self, dt)
    local haptics = state.haptics
    local tool = heldTool()
    local hand = tool ~= nil and toolHandFor(self, tool) or nil
    local kind = hand ~= nil and supportedKind(tool) or nil
    local levelOf = kind ~= nil and self.HAND_TOOL_HAPTICS[kind] or nil
    local level, strength = nil, nil
    if levelOf ~= nil then
        local ok, answer, answeredStrength = pcall(levelOf, self, tool)
        if ok then level, strength = answer, tonumber(answeredStrength) end
    end
    local pulse = level ~= nil and self.HAND_TOOL_HAPTIC_PULSES[level] or nil
    strength = math.min(1, strength or (pulse ~= nil and pulse.strength) or 0)
    if pulse == nil or strength <= 0 then
        haptics.level, haptics.hand, haptics.sinceMs = nil, nil, 0
        return
    end
    haptics.sinceMs = haptics.sinceMs + (tonumber(dt) or 0)
    local interval = tonumber(pulse.intervalMs) or self.HAND_TOOL_HAPTIC_INTERVAL_MS
    if haptics.level ~= level or haptics.hand ~= hand or haptics.sinceMs >= interval then
        haptics.level, haptics.hand, haptics.sinceMs = level, hand, 0
        self:pulseHand(hand, pulse.ms, strength)
    end
end

local function showBlinkingWarning(text)
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function() mission:showBlinkingWarning(text, 2500) end)
    end
end

local function l10nText(key)
    return VRCompanion.getLocalisedText(key, nil)
end

local function markerTypeName(spec)
    local markerType = spec.treeMarkerType
    if type(markerType) == "table" then
        local title = markerType.title
        if type(title) == "string" and title ~= "" then
            local key = string.match(title, "^%$l10n_(.+)$")
            local text = l10nText(key or title)
            if text ~= nil then return text end
            if key == nil then return title end
        end
        local name = markerType.name
        if type(name) == "string" and name ~= "" then
            return l10nText("treeMarker_" .. string.lower(name)) or name
        end
    end
    return tostring(spec.treeMarkerTypeIndex)
end

local MARKER_TYPE_TEXT_KEY = "vrHandTools_markerType"
local function markerTypeMessage(spec)
    local name = markerTypeName(spec)
    local ok, message = pcall(string.format, VRCompanion.getLocalisedText(MARKER_TYPE_TEXT_KEY, "Marker type: %s"), name)
    if ok then return message end
    return "Marker type: " .. name
end

local function updateSprayCanFeedback(self)
    local feedback = state.sprayCan
    local tool = heldTool()
    if tool == nil or supportedKind(tool) ~= "spraycan" then
        feedback.tool, feedback.typeIndex, feedback.treeNode = nil, nil, nil
        return
    end
    local hand = toolHandFor(self, tool)
    local spec = hand ~= nil and tool.spec_sprayCan or nil
    if spec == nil then
        feedback.treeNode = nil
        return
    end
    if feedback.tool ~= tool then feedback.tool, feedback.typeIndex, feedback.treeNode = tool, nil, nil end

    if feedback.typeIndex ~= spec.treeMarkerTypeIndex then
        feedback.typeIndex = spec.treeMarkerTypeIndex
        showBlinkingWarning(markerTypeMessage(spec))
    end

    local found = spec.foundTargetTree
    local node = type(found) == "table" and found.node or nil
    if node ~= nil and node ~= feedback.treeNode then
        local tick = self.HAND_TOOL_TICK_PULSE
        self:pulseHand(hand, tick.ms, tick.strength)
    end
    feedback.treeNode = node
end

local function syncPlacement(self)
    local tool = heldTool()
    if tool == nil then
        forgetLink()
        return
    end
    local hand = toolHandFor(self, tool)
    if hand ~= nil then
        local holder = self:getVirtualHandHolder(hand)
        local placed = state.linkedTool == tool and state.linkedHand == hand and state.linkedHolder == holder
            and engine("getParent")(tool.rootNode) == holder
        if not placed and linkIntoHand(self, tool, hand) then reportPlacement(tool, hand) end
    elseif state.linkedTool == tool then
        returnToTheGame(tool)
        reportPlacement(tool, nil)
    end
end

function VRCompanion:updateHandTools(dt)
    syncPlacement(self)
    updateTrigger(self)
    updateHaptics(self, dt)
    updateSprayCanFeedback(self)
    watchFishingRod(self, dt)
end

function VRCompanion:setHandToolsEnabled(enabled, save)
    self.handToolsEnabled = (enabled ~= false)
    syncPlacement(self)
    if not self.handToolsEnabled then
        endTriggerUse()
        liftRodEventWraps()
        liftRodDrawWrap()
    end
    if save then
        self:saveConfiguration()
    end
end

function VRCompanion:consoleCommandSetHandTools(arg)
    local enabled = VRCompanion.parseOnOffArgument(arg)
    if arg == nil or arg == "" or string.lower(tostring(arg)) == "status" then
        return string.format("Hand Tools are %s. Usage: vrHandTools <on|off> (saved)", self.handToolsEnabled ~= false and "on" or "off")
    end
    if enabled == nil then
        return "Usage: vrHandTools <on|off>"
    end
    self:setHandToolsEnabled(enabled, true)
    showBlinkingWarning(VRCompanion.formatLocalisedText("vrHandTools_set", "VR Hand Tools: %s (Saved)", VRCompanion.onOffText(enabled)))
    print(string.format("[FS25_VR] Hand Tools set to: %s", enabled and "on" or "off"))
    return string.format("Hand Tools set to: %s (Saved)", enabled and "on" or "off")
end

function VRCompanion:setToolHand(hand, save)
    if hand ~= self.MOTION_HAND_LEFT and hand ~= self.MOTION_HAND_RIGHT then return end
    local changed = hand ~= self:getToolHand()
    self.toolHand = hand
    if changed then
        syncPlacement(self)
        endTriggerUse()
    end
    if save then
        self:saveConfiguration()
    end
end

function VRCompanion:consoleCommandSetToolHand(arg)
    local hand = TOOL_HAND_BY_NAME[string.lower(tostring(arg or ""))]
    if arg == nil or arg == "" or string.lower(tostring(arg)) == "status" then
        return string.format("The Tool Hand is the %s hand. Usage: vrToolHand <left|right> (saved)", HAND_NAMES[self:getToolHand()])
    end
    if hand == nil then
        return "Usage: vrToolHand <left|right>"
    end
    self:setToolHand(hand, true)
    local handText = (hand == self.MOTION_HAND_LEFT) and VRCompanion.getLocalisedText("vrHandTools_left", "left")
        or VRCompanion.getLocalisedText("vrHandTools_right", "right")
    showBlinkingWarning(VRCompanion.formatLocalisedText("vrHandTools_toolHandSet", "VR Tool Hand: %s (Saved)", handText))
    print(string.format("[FS25_VR] Tool Hand set to: %s", HAND_NAMES[hand]))
    return string.format("Tool Hand set to: %s (Saved)", HAND_NAMES[hand])
end

local function describeChainsawReach(self)
    local metres = chainsawTipReach()
    if metres == nil then return "the game's own distance (default)" end
    return string.format("%.2f m past the tip of its bar", metres)
end

function VRCompanion:consoleCommandSetChainsawReach(arg)
    if arg == nil or arg == "" or string.lower(tostring(arg)) == "status" then
        return string.format("The chainsaw reaches %s. %s", describeChainsawReach(self), CHAINSAW_REACH_USAGE)
    end
    local ok, reach, clamped = parseChainsawReach(arg)
    if not ok then
        return string.format("'%s' is not a reach. %s", tostring(arg), CHAINSAW_REACH_USAGE)
    end
    self.chainsawReach = reach
    self:saveConfiguration()
    local text = describeChainsawReach(self)
    local metres = chainsawTipReach()
    local shownReach = (metres == nil)
        and VRCompanion.getLocalisedText("vrHandTools_chainsawReachOfTheGame", "the game's own distance (default)")
        or VRCompanion.formatLocalisedText("vrHandTools_chainsawReachMetres", "%.2f m past the tip of its bar", metres)
    showBlinkingWarning(VRCompanion.formatLocalisedText("vrHandTools_chainsawReachSet", "VR Chainsaw reach: %s (Saved)", shownReach))
    print(string.format("[FS25_VR] Chainsaw reach set to: %s", text))
    return string.format("Chainsaw reach set to: %s (Saved)%s", text,
        clamped and string.format(". %g m is the most.", self.MAX_CHAINSAW_REACH) or "")
end

local function describeLookRay(tool)
    local player = tool ~= nil and type(tool.getCarryingPlayer) == "function" and tool:getCarryingPlayer() or nil
    player = player or localPlayer()
    if player == nil or type(player.getLookRay) ~= "function" then return "look ray: unknown (no player)" end
    local ok, x, y, z = pcall(player.getLookRay, player)
    if not ok or type(x) ~= "number" then return "look ray: unknown (getLookRay failed)" end
    local fromTheTool = VRCompanion:getHandToolRay(player) ~= nil
    return string.format("look ray starts at (%.2f, %.2f, %.2f), from the %s", x, y, z, fromTheTool and "tool" or "head")
end

local function describeBrushReach(tool, inTheHand)
    local spec = tool.spec_horseBrush
    if spec == nil or spec.targetedHusbandry == nil then return "animal in reach: no" end
    local distance = brushTargetDistance(carryingPlayer(tool))
    if distance == nil then return "animal in reach: yes" end
    local axis = VRCompanion.HAND_TOOL_POINTING_AXES.horsebrush
    return string.format("animal in reach: yes (%.2f m)", math.max(0, distance - (inTheHand and (tonumber(axis.reachBehind) or 0) or 0)))
end

local function diagnosticFields(self)
    local tool, kind = self:getHeldHandTool()
    local hand, why = toolHandFor(self, tool)
    local toolText = "held Hand Tool: none"
    if tool ~= nil then
        toolText = kind ~= nil and ("held Hand Tool: " .. kind .. " (supported)") or "held Hand Tool: unsupported"
    end
    local function wrapStatus(name)
        return wraps:status(name)
    end
    local wrapText = wrapStatus("attachToolToCamera")
    local lookRayWrapText = wrapStatus("getLookRay")
    local beamWrapText = wrapStatus("flashlightBeam")
    local fields = {
        string.format("Hand Tools: %s", self.handToolsEnabled ~= false and "on" or "off"),
        string.format("Tool Hand: %s", HAND_NAMES[self:getToolHand()]),
        toolText,
        hand ~= nil and "in the Tool Hand: yes" or ("in the Tool Hand: no (" .. tostring(why) .. ")"),
        describeLookRay(tool),
        wrapText,
        lookRayWrapText,
        beamWrapText,
        wrapStatus("sprayCanOnDraw"),
        wrapStatus("chainsawOnDraw"),
        wrapStatus("horseBrushOnDraw"),
        wrapStatus("horseBrushLookup"),
        "centre overlays held back: " .. ((kind ~= nil and DRAW_HELD_BACK_KINDS[kind] and drawIsHeldBack(tool)) and "yes" or "no"),
    }
    table.insert(fields, "haptics: " .. tostring(state.haptics.level or "none"))
    if kind == "chainsaw" then
        table.insert(fields, string.format("chainsaw: %s, cut plane heading from the %s, cut roll from the %s", tostring(self:getChainsawCutState(tool)),
            (hand ~= nil and state.chainsaw.yaw ~= nil) and "bar" or "camera",
            hand ~= nil and "wrist" or "game's roll action"))
        local minimum = chainsawMinimumCutDistance()
        table.insert(fields, "minimum cut distance: " .. (minimum ~= nil and string.format("%.2f m", minimum) or "could not be read"))
        local axis = VRCompanion.HAND_TOOL_POINTING_AXES.chainsaw
        local reach, behind = chainsawReachFromTheBase(tool)
        if reach ~= nil then
            table.insert(fields, string.format("reach past the tip: %.2f m (bar %.2f m)", chainsawTipReach() or 0, tonumber(axis.barLength) or 0))
        elseif behind ~= nil and chainsawTipReach() == nil then
            table.insert(fields, "reach past the tip: the game's own reach (default)")
        else
            table.insert(fields, "reach past the tip: not applied (the game's own reach)")
        end
    end
    if kind == "horsebrush" then
        table.insert(fields, describeBrushReach(tool, hand ~= nil))
        table.insert(fields, "cleaning: " .. ((tool.spec_horseBrush or {}).targetedHusbandryId ~= nil and "yes" or "no"))
    end
    table.insert(fields, wrapStatus("chainsawOnUpdate"))
    table.insert(fields, wrapStatus("chainsawOnRoll"))
    return fields
end

local PROBE_TAG = "Hand Tool probe: "

local function unsupportedHeldTool()
    local tool = heldTool()
    if tool == nil then
        local player = localPlayer()
        if type(player) == "table" and type(player.getHeldHandTool) == "function" then
            local held = player:getHeldHandTool()
            tool = type(held) == "table" and held or nil
        end
    end
    if tool == nil or supportedKind(tool) ~= nil or isAnotherPlayersTool(tool) then return nil end
    return tool
end

local PROBE_STRING_LENGTH = 30
local PROBE_MAX_LINES = 400          -- lines per run, the line that says how many were left out included
local PROBE_MAX_SPEC_FIELDS = 100    -- fields shown per spec table
local PROBE_MAX_NESTED_KEYS = 40     -- a table field with more keys than this is only counted, not opened
local PROBE_MAX_FUNCTION_NAMES = 100 -- function names shown per class or type
local PROBE_MAX_NODES = 40           -- nodes shown in the tree
local PROBE_NODE_DEPTH = 3           -- levels of nodes below the root that are named

local function sortedKeys(t)
    local keys = {}
    for key in pairs(t) do keys[#keys + 1] = key end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    return keys
end

local function shortValue(value)
    local kind = type(value)
    if kind == "number" then
        local text = tostring(value)
        local entityExists, getName = engine("entityExists"), engine("getName")
        if value > 1 and value == math.floor(value) and type(entityExists) == "function" and type(getName) == "function"
            and entityExists(value) then
            local name = tostring(getName(value))
            if name ~= "" and not string.find(name, "[/\\]") then text = string.format('%s (node "%s")', text, name) end
        end
        return text
    elseif kind == "boolean" then
        return tostring(value)
    elseif kind == "string" then
        if #value > PROBE_STRING_LENGTH then return '"' .. string.sub(value, 1, PROBE_STRING_LENGTH) .. '..."' end
        return '"' .. value .. '"'
    elseif kind == "table" then
        local count = 0
        for _ in pairs(value) do count = count + 1 end
        return count .. " keys"
    end
    return nil
end

local function describeNestedTable(path, value, add)
    if type(value) ~= "table" then return end
    local ok, err = pcall(function()
        local keys = sortedKeys(value)
        if #keys == 0 or #keys > PROBE_MAX_NESTED_KEYS then return end
        for _, key in ipairs(keys) do
            local entryOk, text = pcall(function()
                local entry = value[key]
                local shown = shortValue(entry)
                return string.format("%s.%s (%s)%s", path, tostring(key), type(entry), shown ~= nil and (" = " .. shown) or "")
            end)
            add(entryOk and text or string.format("%s.%s could not be read (%s)", path, tostring(key), tostring(text)))
        end
    end)
    if not ok then add(path .. " could not be opened (" .. tostring(err) .. ")") end
end

local function describeSpecTables(tool, add)
    local specNames = {}
    for _, key in ipairs(sortedKeys(tool)) do
        if type(key) == "string" and string.sub(key, 1, 5) == "spec_" and type(tool[key]) == "table" then specNames[#specNames + 1] = key end
    end
    for _, specName in ipairs(specNames) do
        local ok, err = pcall(function()
            local spec = tool[specName]
            local keys = sortedKeys(spec)
            for index, key in ipairs(keys) do
                if index > PROBE_MAX_SPEC_FIELDS then
                    add(string.format("%s: %d more fields left out", specName, #keys - PROBE_MAX_SPEC_FIELDS))
                    break
                end
                local fieldOk, text = pcall(function()
                    local value = spec[key]
                    local shown = shortValue(value)
                    return string.format("%s.%s (%s)%s", specName, tostring(key), type(value), shown ~= nil and (" = " .. shown) or "")
                end)
                add(fieldOk and text or string.format("%s.%s could not be read (%s)", specName, tostring(key), tostring(text)))
                describeNestedTable(specName .. "." .. tostring(key), spec[key], add)
            end
        end)
        if not ok then add(specName .. " could not be read (" .. tostring(err) .. ")") end
    end
end

local function functionNames(t)
    local names = {}
    for key, value in pairs(t) do
        if type(value) == "function" then names[#names + 1] = tostring(key) end
    end
    table.sort(names)
    if #names == 0 then return "none" end
    if #names <= PROBE_MAX_FUNCTION_NAMES then return table.concat(names, ", ") end
    local shown = {}
    for index = 1, PROBE_MAX_FUNCTION_NAMES do shown[index] = names[index] end
    return string.format("%s (and %d more left out)", table.concat(shown, ", "), #names - PROBE_MAX_FUNCTION_NAMES)
end

local function globalNameOf(value)
    for name, candidate in pairs(_G) do
        if candidate == value and type(name) == "string" then return name end
    end
    return nil
end

local function describeSpecializationClasses(tool, add)
    local names, classes = tool.specializationNames, tool.specializations
    if type(names) ~= "table" or type(classes) ~= "table" then return end
    for index, specName in ipairs(names) do
        local class = classes[index]
        if type(class) == "table" then
            local ok, err = pcall(function()
                add(string.format("specialization %s: class %s, functions: %s", tostring(specName),
                    globalNameOf(class) or "(no global name)", functionNames(class)))
            end)
            if not ok then add(string.format("specialization %s: class could not be read (%s)", tostring(specName), tostring(err))) end
        end
    end
end

local function describeTypeFunctions(tool, add)
    local toolType = tool.type
    if type(toolType) ~= "table" or type(toolType.functions) ~= "table" then return end
    add(string.format("type %s: functions: %s", tostring(toolType.name or tool.typeName), functionNames(toolType.functions)))
end

local function callbackName(tool, callback)
    if type(callback) ~= "function" then return nil end
    if type(tool.specializations) == "table" then
        for _, class in ipairs(tool.specializations) do
            if type(class) == "table" then
                for name, fn in pairs(class) do
                    if fn == callback then return (globalNameOf(class) or "(class)") .. "." .. tostring(name) end
                end
            end
        end
    end
    local toolType = tool.type
    if type(toolType) == "table" and type(toolType.functions) == "table" then
        for name, fn in pairs(toolType.functions) do
            if fn == callback then return "type function " .. tostring(name) end
        end
    end
    for name, fn in pairs(tool) do
        if fn == callback then return "tool." .. tostring(name) end
    end
    return nil
end

local function fieldNames(t)
    local names = {}
    for _, key in ipairs(sortedKeys(t)) do names[#names + 1] = tostring(key) end
    return table.concat(names, ", ")
end

local function edgesOf(read)
    local down, up, always = read("triggerDown"), read("triggerUp"), read("triggerAlways")
    if type(down) ~= "boolean" and type(up) ~= "boolean" and type(always) ~= "boolean" then return nil end
    local edges = {}
    if down == true then edges[#edges + 1] = "press" end
    if up == true then edges[#edges + 1] = "release" end
    if always == true then edges[#edges + 1] = "every frame" end
    return #edges > 0 and table.concat(edges, ", ") or "nothing"
end

local function describeActionEvents(tool, add)
    local events = tool.actionEvents
    if type(events) ~= "table" then return end
    local inputBinding = engine("g_inputBinding")
    local lines = {}
    for key, entry in pairs(events) do
        local ok, err = pcall(function()
            if type(entry) ~= "table" then return end
            local bound = type(inputBinding) == "table" and type(inputBinding.events) == "table"
                and entry.actionEventId ~= nil and inputBinding.events[entry.actionEventId] or nil
            local function read(field)
                local value = entry[field]
                if value == nil and type(bound) == "table" then value = bound[field] end
                return value
            end
            local actionName = read("actionName") or (type(key) == "string" and key or nil) or "(unnamed)"
            local callback = read("callback")
            local name = callbackName(tool, callback)
            local callbackText = name ~= nil and ("callback " .. name)
                or (type(callback) == "function" and "callback not found among the class and type functions" or "callback not readable")
            local edges = edgesOf(read)
            local edgesText = edges ~= nil and ("fires on: " .. edges) or ("edges not readable (fields: " .. fieldNames(entry) .. ")")
            lines[#lines + 1] = string.format("action event %s: %s, %s", tostring(actionName), callbackText, edgesText)
        end)
        if not ok then lines[#lines + 1] = string.format("action event %s could not be read (%s)", tostring(key), tostring(err)) end
    end
    table.sort(lines)
    for _, line in ipairs(lines) do add(line) end
end

local function describeNodes(tool, add)
    local root = tool.rootNode
    local getName, getNumOfChildren, getChildAt = engine("getName"), engine("getNumOfChildren"), engine("getChildAt")
    if root == nil or type(getName) ~= "function" or type(getNumOfChildren) ~= "function" or type(getChildAt) ~= "function" then
        add("node tree: the tool has no root node to read")
        return
    end
    local shown, leftOut = 0, 0
    local function walk(node, path, depth)  -- depth: the levels of `node` below the root
        local count = getNumOfChildren(node)
        for index = 0, count - 1 do
            local child = getChildAt(node, index)
            local childPath = path .. "/" .. tostring(getName(child))
            local notListed = (depth + 1 == PROBE_NODE_DEPTH) and getNumOfChildren(child) or 0
            if shown < PROBE_MAX_NODES then
                shown = shown + 1
                add("node " .. childPath .. (notListed > 0 and string.format(" (%d child%s not listed)", notListed, notListed == 1 and "" or "ren") or ""))
            else
                leftOut = leftOut + 1
            end
            if depth + 1 < PROBE_NODE_DEPTH then walk(child, childPath, depth + 1) end
        end
    end
    local ok, err = pcall(function() walk(root, tostring(getName(root)), 0) end)
    if leftOut > 0 then add(string.format("node tree: %d more nodes left out", leftOut)) end
    if not ok then add("node tree could not be read to the end (" .. tostring(err) .. ")") end
end

local function describePlacement(tool, add)
    if not wraps:isWrapped("attachToolToCamera") then
        add("placement: unknown (the placement wrap is not installed)")
        return
    end
    local player = carryingPlayer(tool) or localPlayer()
    local camera = type(player) == "table" and player.camera or nil
    local inFirstPerson = type(camera) == "table" and camera.isFirstPerson == true
    if state.cameraPlacedTool == tool and inFirstPerson then
        add("placement: through attachToolToCamera")
    else
        add("placement: not through attachToolToCamera (the game used another path)")
    end
end

local function guarded(what, part, tool, add)
    local ok, err = pcall(part, tool, add)
    if not ok then add(what .. " could not be read (" .. tostring(err) .. ")") end
end

local function describeUnsupportedTool(tool)
    local lines, leftOut = {}, 0
    local function add(line)
        if #lines < PROBE_MAX_LINES then lines[#lines + 1] = line else leftOut = leftOut + 1 end
    end
    guarded("config file", function(t) add("config file: " .. tostring(t.configFileName)) end, tool, add)
    guarded("placement", describePlacement, tool, add)
    guarded("specializations", describeSpecializationClasses, tool, add)
    guarded("type", describeTypeFunctions, tool, add)
    guarded("action events", describeActionEvents, tool, add)
    guarded("node tree", describeNodes, tool, add)
    guarded("spec tables", describeSpecTables, tool, add)
    if leftOut > 0 then lines[#lines] = string.format("%d more lines left out", leftOut + 1) end
    return lines
end

local function probeLines()
    local ok, result = pcall(function()
        local tool = unsupportedHeldTool()
        if tool == nil then return {} end
        return describeUnsupportedTool(tool)
    end)
    if ok then return result end
    return { "could not be read: " .. tostring(result) }
end

function VRCompanion:getHandToolsDiagnostics()
    return table.concat(diagnosticFields(self), " | ")
end

function VRCompanion:consoleCommandHandToolsDiagnostics()
    for _, field in ipairs(diagnosticFields(self)) do
        print("[FS25_VR] " .. field)
    end
    for _, line in ipairs(fishingRodLines(self)) do
        print("[FS25_VR] " .. line)
    end
    for _, line in ipairs(probeLines()) do
        print("[FS25_VR] " .. PROBE_TAG .. line)
    end
    return self:getHandToolsDiagnostics()
end

local MAX_NUDGE_METRES = 0.5
local MAX_TURN_DEGREES = 360

local function quaternionProduct(a, b)
    return {
        w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    }
end

local function quaternionAbout(axis, degrees)
    local half = math.rad(degrees) / 2
    local s = math.sin(half)
    return { x = axis == "x" and s or 0.0, y = axis == "y" and s or 0.0, z = axis == "z" and s or 0.0, w = math.cos(half) }
end

local function describeOffset(kind, offset)
    return string.format("%s: position = { x = %.4f, y = %.4f, z = %.4f }, rotation = { x = %.4f, y = %.4f, z = %.4f, w = %.4f }",
        kind, offset.position.x, offset.position.y, offset.position.z,
        offset.rotation.x, offset.rotation.y, offset.rotation.z, offset.rotation.w)
end

local TUNING_USAGE = "Usage: vrHandToolOffset <lance|flashlight|chainsaw|spraycan|horsebrush|fishingrod> <nudge dx dy dz | turn rx ry rz | reset> "
    .. "(nudge: metres, turn: degrees about the hand's X, Y, Z axes; this session only)"

local function replaceHeldToolInTheHand(self)
    local tool = state.linkedTool
    if tool ~= nil and state.linkedHand ~= nil and heldTool() == tool then
        linkIntoHand(self, tool, state.linkedHand)
    end
end

function VRCompanion:consoleCommandHandToolOffset(kind, action, a, b, c)
    local offsets = self.HAND_TOOL_HANDLE_OFFSETS
    local name = kind ~= nil and string.lower(tostring(kind)) or ""
    if name == "" or name == "status" then
        local lines = {}
        for _, entry in ipairs(SUPPORTED_TOOLS) do
            table.insert(lines, describeOffset(entry.kind, offsets[entry.kind]))
        end
        return table.concat(lines, "\n") .. "\n" .. TUNING_USAGE
    end
    local offset = offsets[name]
    if offset == nil then return TUNING_USAGE end
    action = action ~= nil and string.lower(tostring(action)) or "status"

    if action == "status" then
        return describeOffset(name, offset)
    elseif action == "reset" then
        offsets[name] = copyOffset(DEFAULT_HANDLE_OFFSETS[name])
    elseif action == "nudge" then
        local dx, dy, dz = tonumber(a), tonumber(b), tonumber(c)
        if dx == nil or dy == nil or dz == nil or math.abs(dx) > MAX_NUDGE_METRES or math.abs(dy) > MAX_NUDGE_METRES
            or math.abs(dz) > MAX_NUDGE_METRES then
            return TUNING_USAGE
        end
        offset.position.x, offset.position.y, offset.position.z = offset.position.x + dx, offset.position.y + dy, offset.position.z + dz
    elseif action == "turn" then
        local rx, ry, rz = tonumber(a), tonumber(b), tonumber(c)
        if rx == nil or ry == nil or rz == nil or math.abs(rx) > MAX_TURN_DEGREES or math.abs(ry) > MAX_TURN_DEGREES
            or math.abs(rz) > MAX_TURN_DEGREES then
            return TUNING_USAGE
        end
        local turn = quaternionProduct(quaternionAbout("z", rz), quaternionProduct(quaternionAbout("y", ry), quaternionAbout("x", rx)))
        local q = quaternionProduct(turn, { x = offset.rotation.x, y = offset.rotation.y, z = offset.rotation.z, w = offset.rotation.w })
        local length = math.sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w)
        offset.rotation = { x = q.x / length, y = q.y / length, z = q.z / length, w = q.w / length }
    else
        return TUNING_USAGE
    end

    offset = offsets[name]
    replaceHeldToolInTheHand(self)
    local text = describeOffset(name, offset)
    print("[FS25_VR] Hand Tools offset " .. text)
    return text
end

function VRCompanion:releaseHandToolsFromHands()
    local tool = state.linkedTool
    endTriggerUse()
    if tool ~= nil and heldTool() == tool then pcall(returnToTheGame, tool) end
    forgetLink()
end

function VRCompanion:resetHandTools()
    local tool = state.linkedTool
    endTriggerUse()
    wraps:reset()
    liftRodEventWraps()
    liftRodDrawWrap()
    if tool ~= nil and heldTool() == tool then pcall(returnToTheGame, tool) end
    state = newState()
end

wrapAll()

VRCompanion:registerSubsystem("handTools", {
    mapLoad = function(self)
        wrapAll()
        checkWraps()
    end,
    mapUnload = function(self) self:resetHandTools() end,
    saveSettings = function(self, xmlFile, xml) self:saveHandToolsSetting(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadHandToolsSetting(xmlFile, xml) end,
    consoleCommands = {
        { name = "vrHandToolOffset", description = "Move or turn a Hand Tool in the Tool Hand while you look at it, this session only: vrHandToolOffset <lance|flashlight|chainsaw|spraycan|horsebrush|fishingrod> <nudge dx dy dz | turn rx ry rz | reset>", method = "consoleCommandHandToolOffset" },
        { name = "vrHandTools", description = "Turn Hand Tools, the flashlight, chainsaw, pressure washer lance, spray can, horse brush and fishing rod held in a Motion Controller, on or off (saved): vrHandTools [on|off]", method = "consoleCommandSetHandTools" },
        { name = "vrToolHand", description = "Choose the Motion Controller that holds Hand Tools, left or right (saved; right by default): vrToolHand [left|right]", method = "consoleCommandSetToolHand" },
        { name = "vrChainsawReach", description = "How far past the tip of its bar the chainsaw in the Tool Hand still finds a tree: metres (0 to 3; 0.15 for contact-only cutting), or default / game (the game's own reach and end of cut, the default), saved: vrChainsawReach [<metres> | default | game]", method = "consoleCommandSetChainsawReach" },
        { name = "vrHandToolsDiagnostics", description = "Report the Tool Hand, the held Hand Tool, whether it is in the hand and where the look ray starts: vrHandToolsDiagnostics", method = "consoleCommandHandToolsDiagnostics" },
    },
})


VRCompanion.HandTools.probeReaders = {
    shortValue = shortValue, sortedKeys = sortedKeys, functionNames = functionNames, globalNameOf = globalNameOf,
}
