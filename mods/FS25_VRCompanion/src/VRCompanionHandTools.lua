-- ====================================================================
-- FS25 VR Companion Mod: Hand Tools
--
-- Puts the player's Hand Tools into the Tool Hand (spec #109, ADR-0029). In first person on foot the game links a held Hand
-- Tool to the player camera's pitch node (HandTool:attachToolToCamera), so it moves with the head. While the Tool Hand is
-- tracked, this module links a SUPPORTED Hand Tool into the Tool Hand's Virtual Hand instead; when the Tool Hand is set
-- down or loses tracking, or the feature is off, the tool is where the game puts it again.
--
-- TERMS (CONTEXT.md): a Hand Tool is a tool the player holds on foot (the flashlight, the chainsaw, the pressure washer
-- lance, the spray can). The Tool Hand is the Motion Controller that holds it: the right one by default.
--
-- SUPPORTED TOOLS are recognised by the game's specialization (the spec_ table a tool has): the pressure washer lance
-- today; the flashlight and the chainsaw are added by their own tickets. Every other Hand Tool (the spray can, the horse
-- brush, other mods' tools) is left exactly as the game has it, and the Virtual Hand holding it is an empty hand.
--
-- SECTIONS of this file (one per concern, so that a ticket adds to its own section):
--   * Settings              vrHandTools on|off, saved
--   * Supported tools       which tools are supported, and where each sits in the hand (HAND_TOOL_HANDLE_OFFSETS)
--   * Wraps                 the game functions this module wraps (WRAP_SPECS, keyed by function name) and how they give up
--   * Placement             the Tool Hand, whether a tool is in it, linking it into the Virtual Hand and back
--   * Update                the per-update step
--   * Diagnostics, console commands, reset
--
-- THE WRAPS (ADR-0026, ADR-0027): every wrap passes through to the game's own function when the feature is off, Motion
-- Controllers are off, the Tool Hand is untracked or set down, or the tool is unsupported; these are re-checked on every
-- call, never cached from the last update, so that "off" takes effect at once. If a function a wrap expects is missing or
-- has another shape when the map loads, the feature gives up for the map: every wrap is lifted, ONE warning is logged and
-- the game's Hand Tools work unchanged. A wrap is only taken off again when it is still the function the module put there:
-- another mod's later wrap is never undone.
--
-- PUBLIC SURFACE
--   VRCompanion.handToolsEnabled                        boolean, default true, saved (`vrHandTools on|off`)
--   VRCompanion.HAND_TOOL_HANDLE_OFFSETS                per tool kind: where the tool's root node sits in the Virtual Hand's grip frame
--   VRCompanion:getToolHand() -> hand                   the Motion Controller that holds Hand Tools (0 left, 1 right)
--   VRCompanion:isHandHoldingHandTool(hand) -> boolean  the hand is the Tool Hand and a supported Hand Tool is in it now
--   VRCompanion:getHeldHandTool() -> tool, kind        the Hand Tool the player holds and which supported kind it is (nil kind: unsupported)
--   VRCompanion:updateHandTools(dt)                     once per update, called by updateMotionControllers on every path
--   VRCompanion:setHandToolsEnabled(enabled, save)
--   VRCompanion:saveHandToolsSetting(xmlFile, xml) / loadHandToolsSetting(xmlFile, xml)   settings chain
--   VRCompanion:getHandToolsDiagnostics() -> string     the state of Hand Tools, one line
--   VRCompanion:registerHandToolsConsoleCommands() / resetHandTools()   map load / unload
--   Console: vrHandTools [on|off], vrHandToolsDiagnostics
--
-- Hooks into the rest of the mod (each a single guarded call):
--   MotionControllers  updateMotionControllers -> updateHandTools(dt) on every path (also when input is ignored or the
--                      Injector offers no hand states, so a tool goes back to the camera); the saved setting, the console
--                      commands and the reset chain through the Motion Controller functions of the same names
--   VirtualHands       getVirtualHandHolder(hand) is the node a tool is linked below; a hand that isHandHoldingHandTool is
--                      drawn closed around the handle, its index finger still following the trigger
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.HandTools = VRCompanion.HandTools or {}

local engine = VRCompanion.getEngineGlobal  -- VRCompanionCore.lua, loaded before this file

local HAND_NAMES = { [0] = "left", [1] = "right" }

local function newState()
    return {
        tool = nil,          -- the Hand Tool the game last attached to the camera (checked to be held before it is used)
        linkedTool = nil,    -- the tool this module has linked into a Virtual Hand
        linkedHand = nil,    -- ... and the hand
        linkedHolder = nil,  -- ... and the Virtual Hand's node
        reported = nil,      -- the placement last logged
        givingBack = false,  -- the tool is being handed back to the game's own placement
        problem = nil,       -- why Hand Tools gave up, once it did (the warning is logged then and never again)
    }
end

local state = newState()

-- ---- Settings -----------------------------------------------------------------------------------------------------------

-- vrHandTools on|off: saved, default on. Hand Tools also need Motion Controllers on.
VRCompanion.DEFAULT_HAND_TOOLS_ENABLED = true
VRCompanion.handToolsEnabled = VRCompanion.DEFAULT_HAND_TOOLS_ENABLED

local SETTING_KEY_ENABLED = "vrSettings.handTools"

function VRCompanion:saveHandToolsSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY_ENABLED, self.handToolsEnabled ~= false)
    end
end

-- A key that is missing keeps the current value.
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
end

-- ---- Supported tools ----------------------------------------------------------------------------------------------------
-- The tools that go into the Tool Hand, recognised by the specialization table the game gives the tool (`spec`). A later
-- ticket adds its tool here (the flashlight, the chainsaw) and a HAND_TOOL_HANDLE_OFFSETS entry of the same kind.
local SUPPORTED_TOOLS = {
    { kind = "lance", label = "pressure washer lance", spec = "spec_highPressureWasherLance" },
}

-- WHERE EACH TOOL SITS IN THE HAND: the pose of the tool's ROOT node in the Virtual Hand's grip frame (the frame of the
-- holder node: origin at the centre of the tube the closed fingers form, -Y toward the fingertips, -Z from the little finger
-- toward the thumb, +X out of a left palm and into a right one; VRCompanionVirtualHands.lua). The one place to adjust how a
-- tool sits in the hand. The rotation maps the tool's own axes onto the hand's so that the tool points where the fingers
-- point: the tool's +Z (the lance's nozzle runs along it) onto the grip frame's -Y, the tool's +Y (up) onto -Z (toward the
-- thumb), which is the half turn about (0, 1, -1) below. Metres, a unit quaternion.
-- UNCERTAIN: these are a first guess (the root at the grip origin). Each tool's real handle position is found in the
-- headset (ticket #118); only this table changes.
VRCompanion.HAND_TOOL_HANDLE_OFFSETS = {
    lance = {
        position = { x = 0.0, y = 0.0, z = 0.0 },
        rotation = { x = 0.0, y = 0.7071068, z = -0.7071068, w = 0.0 },
    },
}

-- The supported kind of a Hand Tool and its entry, or nil for an unsupported one.
local function supportedKind(tool)
    if type(tool) ~= "table" then return nil end
    for _, entry in ipairs(SUPPORTED_TOOLS) do
        if tool[entry.spec] ~= nil then return entry.kind, entry end
    end
    return nil
end

-- ---- Wraps --------------------------------------------------------------------------------------------------------------
-- WRAP_SPECS is keyed by the name of the function wrapped. Each entry:
--   class        the name of the global table the function is on
--   parameters   how many parameters the wrap needs the function to declare at least (a reshaped function declares fewer)
--   describe     what the parameters are, for the warning
--   make         function(original) -> the wrapper (filled in by the section that owns the wrap)
-- Each later wrap is one more entry.
local WRAP_SPECS = {
    attachToolToCamera = { class = "HandTool", parameters = 1, describe = "(tool)" },
}

-- wraps[functionName] = { class = <the table wrapped>, original = <what it was>, wrapper = <what the wrap put there> }
local wraps = {}

-- How many parameters a Lua function declares, or nil when it takes any number or the runtime cannot tell.
local function parameterCount(fn)
    local library = _G and _G.debug
    if type(library) ~= "table" then return nil end
    if type(library.info) == "function" then
        local ok, count, isVararg = pcall(library.info, fn, "a")
        if ok and type(count) == "number" then return (not isVararg) and count or nil end
    end
    if type(library.getinfo) == "function" then
        local ok, info = pcall(library.getinfo, fn, "u")
        if ok and type(info) == "table" and type(info.nparams) == "number" then
            return (not info.isvararg) and info.nparams or nil
        end
    end
    return nil
end

-- Why a function is not what its wrap expects, or nil when it is.
local function shapeProblem(spec, fn)
    if fn == nil then return "it is missing" end
    if type(fn) ~= "function" then return "it is not a function" end
    local count = parameterCount(fn)
    if count ~= nil and count < spec.parameters then
        return string.format("it takes %d parameters, not %s", count, spec.describe)
    end
    return nil
end

-- Wraps a function on its class once per class table. A function that is not as expected is left alone (checkWraps reports it).
local function wrapFunction(name)
    local spec = WRAP_SPECS[name]
    local class = engine(spec.class)
    if type(class) ~= "table" or spec.make == nil then return end
    local current = wraps[name]
    if current ~= nil and current.class == class then return end
    local original = class[name]
    if shapeProblem(spec, original) ~= nil then return end
    local wrapper = spec.make(original)
    wraps[name] = { class = class, original = original, wrapper = wrapper }
    class[name] = wrapper
end

local function wrapAll()
    for name in pairs(WRAP_SPECS) do wrapFunction(name) end
end

-- Takes the wraps off again, each only when it is still the wrapper put there.
local function liftWraps()
    for name, wrap in pairs(wraps) do
        if wrap.class[name] == wrap.wrapper then wrap.class[name] = wrap.original end
    end
    wraps = {}
end

-- Hand Tools cannot rely on the game's functions: ONE warning, every wrap lifted, so that the game's Hand Tools are exactly
-- the game's.
local function giveUp(problem)
    state.problem = problem
    liftWraps()
    print("[FS25_VR] [Warning] Hand Tools disabled: " .. problem .. ". Hand Tools are unchanged.")
end

-- At map load: gives up (once) when a function the wraps expect is missing or reshaped. A game without the class at all has
-- no Hand Tools to move, which is not a failure.
local function checkWraps()
    if state.problem ~= nil then return state.problem end
    for name, spec in pairs(WRAP_SPECS) do
        local class = engine(spec.class)
        if type(class) == "table" then
            local current = wraps[name]
            local problem = shapeProblem(spec, current ~= nil and current.original or class[name])
            if problem ~= nil then
                giveUp(spec.class .. "." .. name .. ": " .. problem)
                return state.problem
            end
        end
    end
    return nil
end

-- ---- Placement ----------------------------------------------------------------------------------------------------------

function VRCompanion:getToolHand()
    return self.toolHand or self.MOTION_HAND_RIGHT
end

-- The Hand Tool the game last attached, while it is still held (and nil when it was put away).
local function heldTool()
    local tool = state.tool
    if tool == nil then return nil end
    if type(tool.getIsHeld) ~= "function" or tool.rootNode == nil or not tool:getIsHeld() then
        state.tool = nil
        return nil
    end
    return tool
end

function VRCompanion:getHeldHandTool()
    local tool = heldTool()
    if tool == nil then return nil, nil end
    return tool, (supportedKind(tool))
end

-- Whether the Tool Hand holds the tool now: the feature is on, Motion Controllers are on, the tool is supported and held
-- in the first person view (the game keeps its own third person placement), and the Tool Hand's Virtual Hand is there to
-- hold it (the hand in use, its grip pose tracked, the head placed in the world). Answers the hand, or nil and why not.
-- Read live, never cached, by the wraps and by the update.
local function toolHandFor(self, tool)
    if self.handToolsEnabled == false then return nil, "Hand Tools are off" end
    if self.motionControllersEnabled == false then return nil, "Motion Controllers are off" end
    if state.problem ~= nil then return nil, "Hand Tools gave up (" .. state.problem .. ")" end
    if state.givingBack then return nil, "the tool is being given back to the game" end
    if tool == nil then return nil, "no Hand Tool is held" end
    if supportedKind(tool) == nil then return nil, "the Hand Tool is not supported" end
    local player = type(tool.getCarryingPlayer) == "function" and tool:getCarryingPlayer() or nil
    if player == nil or player.camera == nil or player.camera.isFirstPerson ~= true then
        return nil, "not in the first person view"
    end
    local hand = self:getToolHand()
    local pose = self:getHandState(hand)
    if pose.tracked ~= true then return nil, "the Tool Hand is not tracked" end
    if not self:isHandInUse(hand) then return nil, "the Tool Hand is not in use" end
    if pose.gripPoseTracked ~= true then return nil, "the Tool Hand's grip pose is not tracked" end
    if not self:isHeadPlacedInTheWorld() then return nil, "the head is not placed in the world" end
    if type(self.getVirtualHandHolder) ~= "function" or self:getVirtualHandHolder(hand) == nil then
        return nil, "the Virtual Hand is not there"
    end
    return hand, nil
end

function VRCompanion:isHandHoldingHandTool(hand)
    local tool = heldTool()
    if tool == nil then return false end
    return toolHandFor(self, tool) == hand
end

-- Links the tool's root below the Virtual Hand's node with the tool's handle offset. Returns whether it did.
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

-- Logs where the tool is when that changes (a headset check reads it back from the log).
local function reportPlacement(tool, hand)
    local text = hand ~= nil and string.format("Hand Tools: the %s is in the %s hand", tostring(supportedKind(tool)), HAND_NAMES[hand])
        or "Hand Tools: the Hand Tool is where the game puts it"
    if text ~= state.reported then
        state.reported = text
        print("[FS25_VR] " .. text)
    end
end

-- The game's own placement again: the game's attachTool decides between the camera and the hand of the third person view.
local function returnToTheGame(tool)
    state.linkedTool, state.linkedHand, state.linkedHolder = nil, nil, nil
    state.givingBack = true  -- the wrap passes this call through, whatever the hand looks like
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

-- The wrap of HandTool:attachToolToCamera: a supported tool goes into the Tool Hand while it holds it, anything else (and
-- everything while the hand does not) is the game's own.
WRAP_SPECS.attachToolToCamera.make = function(original)
    return function(tool, ...)
        if type(tool) == "table" then
            state.tool = tool
            local hand = toolHandFor(VRCompanion, tool)
            if hand ~= nil and linkIntoHand(VRCompanion, tool, hand) then
                reportPlacement(tool, hand)
                return
            end
        end
        return original(tool, ...)
    end
end

-- ---- Update -------------------------------------------------------------------------------------------------------------

-- Brings the held tool to where it belongs now: into the Tool Hand, or back where the game puts it. Cheap when nothing changed.
local function syncPlacement(self)
    local tool = heldTool()
    if tool == nil then
        state.linkedTool, state.linkedHand, state.linkedHolder = nil, nil, nil
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

-- Once per update, after the hands are polled and before the Virtual Hands are placed (every path of the update).
function VRCompanion:updateHandTools(dt)
    syncPlacement(self)
end

-- ---- Settings command, diagnostics, console commands, reset ---------------------------------------------------------------

function VRCompanion:setHandToolsEnabled(enabled, save)
    self.handToolsEnabled = (enabled ~= false)
    syncPlacement(self)  -- at once: the tool moves between the hand and the camera without waiting for the next update
    if save and type(self.saveConfiguration) == "function" then
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
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR Hand Tools: %s (Saved)", enabled and "on" or "off"), 2500)
        end)
    end
    print(string.format("[FS25_VR] Hand Tools set to: %s", enabled and "on" or "off"))
    return string.format("Hand Tools set to: %s (Saved)", enabled and "on" or "off")
end

-- Where the game's look ray starts now, for the diagnostics: the player's own getLookRay.
local function describeLookRay(tool)
    local player = tool ~= nil and type(tool.getCarryingPlayer) == "function" and tool:getCarryingPlayer() or nil
    player = player or (g_currentMission or _G.g_currentMission or {}).player
    if player == nil or type(player.getLookRay) ~= "function" then return "look ray: unknown (no player)" end
    local ok, x, y, z = pcall(player.getLookRay, player)
    if not ok or type(x) ~= "number" then return "look ray: unknown (getLookRay failed)" end
    return string.format("look ray starts at (%.2f, %.2f, %.2f)", x, y, z)
end

local function diagnosticFields(self)
    local tool, kind = self:getHeldHandTool()
    local hand, why = toolHandFor(self, tool)
    local toolText = "held Hand Tool: none"
    if tool ~= nil then
        toolText = kind ~= nil and ("held Hand Tool: " .. kind .. " (supported)") or "held Hand Tool: unsupported"
    end
    local wrapText = "attachToolToCamera wrap: not installed"
    if state.problem ~= nil then
        wrapText = "attachToolToCamera wrap: gave up (" .. state.problem .. ")"
    elseif wraps.attachToolToCamera ~= nil then
        wrapText = "attachToolToCamera wrap: installed"
    end
    return {
        string.format("Hand Tools: %s", self.handToolsEnabled ~= false and "on" or "off"),
        string.format("Tool Hand: %s", HAND_NAMES[self:getToolHand()]),
        toolText,
        hand ~= nil and "in the Tool Hand: yes" or ("in the Tool Hand: no (" .. tostring(why) .. ")"),
        describeLookRay(tool),
        wrapText,
    }
end

-- One line (the Pointer diagnostics may carry it too).
function VRCompanion:getHandToolsDiagnostics()
    return table.concat(diagnosticFields(self), " | ")
end

-- Prints the fields to the game log, one tagged line each, and returns them as one line.
function VRCompanion:consoleCommandHandToolsDiagnostics()
    for _, field in ipairs(diagnosticFields(self)) do
        print("[FS25_VR] " .. field)
    end
    return self:getHandToolsDiagnostics()
end

function VRCompanion:registerHandToolsConsoleCommands()
    wrapAll()    -- map load: the class again (resetHandTools lifted the wrap with the last map)
    checkWraps()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrHandTools", "Turn Hand Tools, the flashlight, chainsaw and pressure washer lance held in a Motion Controller, on or off (saved): vrHandTools [on|off]", "consoleCommandSetHandTools", self)
    addConsoleCommand("vrHandToolsDiagnostics", "Report the Tool Hand, the held Hand Tool, whether it is in the hand and where the look ray starts: vrHandToolsDiagnostics", "consoleCommandHandToolsDiagnostics", self)
end

-- Called by the Virtual Hands just before they delete their nodes: the tool hangs below one, and deleting the node would
-- delete the tool. It goes back to where the game puts it.
function VRCompanion:releaseHandToolsFromHands()
    local tool = state.linkedTool
    if tool ~= nil and heldTool() == tool then pcall(returnToTheGame, tool) end
    state.linkedTool, state.linkedHand, state.linkedHolder = nil, nil, nil
end

-- Map unload: the tool goes back to the game's placement (the Virtual Hand it hangs below is deleted next), the wraps come
-- off and everything seen this session is forgotten. The saved setting stays as it is.
function VRCompanion:resetHandTools()
    local tool = state.linkedTool
    liftWraps()
    if tool ~= nil and heldTool() == tool then pcall(returnToTheGame, tool) end
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrHandTools")
        removeConsoleCommand("vrHandToolsDiagnostics")
    end
    state = newState()
end

wrapAll()  -- the game's HandTool is there as the files load

VRCompanion.HandTools.updateHandTools = function(self, ...) return VRCompanion.updateHandTools(VRCompanion, ...) end
VRCompanion.HandTools.isHandHoldingHandTool = function(self, ...) return VRCompanion.isHandHoldingHandTool(VRCompanion, ...) end
VRCompanion.HandTools.getToolHand = function(self, ...) return VRCompanion.getToolHand(VRCompanion, ...) end
VRCompanion.HandTools.consoleCommandSetHandTools = function(self, ...) return VRCompanion.consoleCommandSetHandTools(VRCompanion, ...) end
VRCompanion.HandTools.consoleCommandHandToolsDiagnostics = function(self, ...) return VRCompanion.consoleCommandHandToolsDiagnostics(VRCompanion, ...) end

return VRCompanion.HandTools
