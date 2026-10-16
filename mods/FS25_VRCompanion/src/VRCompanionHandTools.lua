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
-- SUPPORTED TOOLS are recognised by the game's specialization (the spec_ table a tool has): the pressure washer lance and
-- the chainsaw today; the flashlight is added by its own ticket. Every other Hand Tool (the spray can, the horse
-- brush, other mods' tools) is left exactly as the game has it, and the Virtual Hand holding it is an empty hand.
--
-- SECTIONS of this file (one per concern, so that a ticket adds to its own section):
--   * Settings              vrHandTools on|off, saved
--   * Supported tools       which tools are supported, and where each sits in the hand (HAND_TOOL_HANDLE_OFFSETS)
--   * Wraps                 the game functions this module wraps (WRAP_SPECS, keyed by function name) and how they give up
--   * Placement             the Tool Hand, whether a tool is in it, linking it into the Virtual Hand and back
--   * Trigger               the Tool Hand's trigger uses the tool (its ACTIVATE_HANDTOOL handler) and reads released on the pad
--   * The look ray          the tool's pointing ray and the Player:getLookRay wrap (HAND_TOOL_POINTING_AXES)
--   * The chainsaw          the cut plane and the model aligned from the bar, not the camera (wrap of HandToolChainsaw.onUpdate)
--   * Update               the per-update step
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
--   VRCompanion:isHandToolTriggerDown(hand) -> boolean  the hand's trigger is using its Hand Tool now (Motion Controllers keep that hand in use)
--   VRCompanion.HAND_TOOL_POINTING_AXES                 per tool kind: the node a tool aims from and the axis of it that it aims along
--   VRCompanion:getHandToolRay([player]) -> x, y, z, dirX, dirY, dirZ   the tool's pointing ray in the world, while a supported tool
--                                                       is in the Tool Hand (nil otherwise: the head's ray is the game's). This is
--                                                       what Player:getLookRay answers then, so what the targeter casts.
--   VRCompanion:getChainsawCutState([tool]) -> "idle" | "delimbing" | "cutting" | nil   what the chainsaw is doing (the motor is revved
--                                                       unless "idle"); nil when the tool (default: the one held) is no chainsaw
--   VRCompanion:getChainsawCutRoll(tool) -> radians|nil the seam for the wrist's roll (ticket #115): asked before each update that is
--                                                       not cutting; nil leaves the roll to the game's roll action
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
--                      commands and the reset chain through the Motion Controller functions of the same names. Pointer Hand
--                      selection never picks a hand that isHandHoldingHandTool in the world (a menu is not the world: either hand
--                      can point), and a hand whose isHandToolTriggerDown stays in use however still it is held
--   VirtualGamepad     updateHandTools asks takeVirtualGamepadInput(toolHand, "trigger") every update the Tool Hand holds a
--                      supported tool (the use is made here, so VirtualGamepad needs no hook); false is the held-input handover
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
        trigger = {          -- the Tool Hand's trigger as a use of the tool (see "Trigger")
            tool = nil,      -- the tool the trigger is using
            entry = nil,     -- ... and its SUPPORTED_TOOLS entry
            hand = nil,      -- ... and the hand
            down = false,    -- the trigger counts as pulled (with hysteresis)
            active = false,  -- the tool's use handler was last called as held, and has not been called as released since
        },
        chainsaw = {         -- the chainsaw's cut plane (see "The chainsaw")
            tool = nil,      -- the chainsaw it is of
            yaw = nil,       -- the heading of the plane: the bar's, locked while a cut is made
        },
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
-- `useHandler` is the name of the tool's own function the game's ACTIVATE_HANDTOOL binding calls (callback(self, action,
-- inputValue)); the Tool Hand's trigger calls it too (see "Trigger"). The lance washes while the input is held.
-- `useHandlerClass` (optional) names the global specialization class to look the handler up on when the tool does not carry it
-- itself: the game registers some handlers as functions of the tool (the lance's onWashAction) and leaves others on the
-- specialization class only, called with the tool as `self` by the action event (HandToolChainsaw.onCutAction).
-- `useOnce` (optional) marks a tool whose handler is called once per press and once per release, as the game's action event does
-- for it (the chainsaw's, registered for trigger up and down), not on every update the input is held (the lance's).
local SUPPORTED_TOOLS = {
    { kind = "lance", label = "pressure washer lance", spec = "spec_highPressureWasherLance", useHandler = "onWashAction" },
    -- Chainsaw (ticket #114)
    { kind = "chainsaw", label = "chainsaw", spec = "spec_chainsaw", useHandler = "onCutAction", useHandlerClass = "HandToolChainsaw", useOnce = true },
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
    -- Chainsaw (ticket #114): the saw's bar runs along the tool's +Z, as the game turns the model (setDirection) to point
    -- its +Z where the camera looks. UNCERTAIN: the same first guess as the lance (the root at the grip origin, the bar along
    -- the fingers); the handle and the bar's direction are found in the headset (#118).
    chainsaw = {
        position = { x = 0.0, y = 0.0, z = 0.0 },
        rotation = { x = 0.0, y = 0.7071068, z = -0.7071068, w = 0.0 },
    },
}

-- WHERE EACH TOOL POINTS (the look ray, ticket #112): the ray the game's targeter casts starts at `node(tool)`'s origin and runs
-- along `direction`, a unit vector in that node's own axes. One entry per tool kind, the place to adjust how a tool aims.
-- The lance washes from its raycastNode along that node's +Z axis, the nozzle. UNCERTAIN until checked in the headset (#118):
-- only this table changes.
VRCompanion.HAND_TOOL_POINTING_AXES = {
    lance = {
        node = function(tool) return tool.spec_highPressureWasherLance.raycastNode end,
        direction = { x = 0.0, y = 0.0, z = 1.0 },
    },
    -- Chainsaw (ticket #114): from the tool's root along its +Z, the bar. The root, not the model node: the game turns the
    -- model toward the camera each update and moves it along the cut while cutting, while the root stays where the hand put it.
    -- The hit point of this ray on the trunk is where the game puts the cut plane's node, so the cut height is where the bar
    -- meets the trunk. UNCERTAIN until checked in the headset (#118).
    chainsaw = {
        node = function(tool) return tool.rootNode end,
        direction = { x = 0.0, y = 0.0, z = 1.0 },
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
--   name         the function's name on that table when it is not the entry's key (a key like `chainsawOnUpdate` keeps two
--                classes' functions of the same name apart)
--   parameters   how many parameters the wrap needs the function to declare at least (a reshaped function declares fewer)
--   describe     what the parameters are, for the warning
--   make         function(original) -> the wrapper (filled in by the section that owns the wrap); an entry without one is a
--                function that is only checked at map load (the feature gives up when it is missing or reshaped)
-- Each later wrap is one more entry.
local WRAP_SPECS = {
    attachToolToCamera = { class = "HandTool", parameters = 1, describe = "(tool)" },
    getLookRay = { class = "Player", parameters = 1, describe = "(player)" },
    -- Chainsaw (ticket #114): the specialization's update (the game's event dispatch reads it off the class at every call), and
    -- the camera's rotation, which the chainsaw's cut plane is aligned from
    chainsawOnUpdate = { class = "HandToolChainsaw", name = "onUpdate", parameters = 2, describe = "(tool, dt)" },
    playerCameraGetRotation = { class = "PlayerCamera", name = "getRotation", parameters = 1, describe = "(camera)" },
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
    local functionName = spec.name or name
    local original = class[functionName]
    if shapeProblem(spec, original) ~= nil then return end
    local wrapper = spec.make(original)
    wraps[name] = { class = class, original = original, wrapper = wrapper, name = functionName }
    class[functionName] = wrapper
end

local function wrapAll()
    for name in pairs(WRAP_SPECS) do wrapFunction(name) end
end

-- Takes the wraps off again, each only when it is still the wrapper put there.
local function liftWraps()
    for _, wrap in pairs(wraps) do
        if wrap.class[wrap.name] == wrap.wrapper then wrap.class[wrap.name] = wrap.original end
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
            local functionName = spec.name or name
            local problem = shapeProblem(spec, current ~= nil and current.original or class[functionName])
            if problem ~= nil then
                giveUp(spec.class .. "." .. functionName .. ": " .. problem)
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

-- ---- Trigger ------------------------------------------------------------------------------------------------------------
-- While a supported tool is in the tracked Tool Hand, that hand's trigger uses it: the Companion Mod calls the tool's own
-- ACTIVATE_HANDTOOL handler (SUPPORTED_TOOLS[].useHandler) with the input held (1) while the trigger is pulled (on every update,
-- or once when the tool is `useOnce`, as the game's action event calls it) and with it released (0) when it is let go, and takes the trigger off the Virtual Gamepad (takeVirtualGamepadInput, ADR-0028) so that
-- whatever is bound to it does not also fire; the held-input rule applies as for every other use (a trigger already held when
-- the tool comes into the hand keeps its binding and uses nothing until it is let go). The game's own binding (X on a pad, the
-- mouse, the keyboard) still calls the same handler: nothing here replaces it.

-- Calls the tool's use handler as the game's action event does: callback(self, actionName, inputValue).
local function callUseHandler(tool, entry, value)
    local handler = tool[entry.useHandler]
    if type(handler) ~= "function" and entry.useHandlerClass ~= nil then
        local class = engine(entry.useHandlerClass)  -- a handler the game keeps on the specialization class only
        handler = type(class) == "table" and class[entry.useHandler] or nil
    end
    if type(handler) ~= "function" then return end
    local ok, err = pcall(handler, tool, "ACTIVATE_HANDTOOL", value)
    if not ok and state.useError ~= tostring(err) then
        state.useError = tostring(err)  -- once per distinct error, not once per frame
        print("[FS25_VR] [Warning] Hand Tools: the " .. entry.label .. "'s use handler failed: " .. tostring(err))
    end
end

-- Ends a use that is in progress (the tool is released), and forgets the trigger.
local function endTriggerUse()
    local use = state.trigger
    if use.active and use.tool ~= nil and use.entry ~= nil then callUseHandler(use.tool, use.entry, 0) end
    use.tool, use.entry, use.hand, use.down, use.active = nil, nil, nil, false, false
end

local function updateTrigger(self)
    local use = state.trigger
    local tool = heldTool()
    local kind, entry = nil, nil
    if tool ~= nil then kind, entry = supportedKind(tool) end
    local hand = nil
    if entry ~= nil and entry.useHandler ~= nil then hand = toolHandFor(self, tool) end
    if hand == nil then
        endTriggerUse()
        return
    end
    if use.tool ~= tool or use.hand ~= hand then
        endTriggerUse()
        use.tool, use.entry, use.hand = tool, entry, hand
    end
    -- The trigger is ours unless it was already held when the tool came into the hand (the pad keeps it until it is let go).
    local mine = true
    if type(self.takeVirtualGamepadInput) == "function" then mine = self:takeVirtualGamepadInput(hand, "trigger") end
    if mine then
        local value = self:getHandState(hand).trigger or 0
        use.down = value >= (use.down and self.POINTER_TRIGGER_RELEASE or self.POINTER_TRIGGER_PRESS)
    else
        use.down = false
    end
    if use.down then
        if not use.active or not entry.useOnce then callUseHandler(tool, entry, 1) end
        use.active = true
    elseif use.active then
        callUseHandler(tool, entry, 0)
        use.active = false
    end
end

-- Whether the hand's trigger is using its Hand Tool right now. The Tool Hand does not stay "in use" just by holding a tool (it
-- is set down after the in-use timeout like any hand), so Motion Controllers keeps it in use while this is so: a cut or a
-- spray held with a still hand must not drop the tool.
function VRCompanion:isHandToolTriggerDown(hand)
    local use = state.trigger
    return use.active and use.hand == hand and self:getHandState(hand).tracked == true
end

-- ---- The look ray (ticket #112) -----------------------------------------------------------------------------------------

-- Where the tool points now: the world position of its pointing node and the world direction of its pointing axis, or nil
-- when the tool has no pointing axis (HAND_TOOL_POINTING_AXES) or the node is not there.
local function toolPointingRay(self, tool)
    local kind = supportedKind(tool)
    local axis = kind ~= nil and self.HAND_TOOL_POINTING_AXES[kind] or nil
    if axis == nil then return nil end
    local node = axis.node(tool)
    if node == nil then return nil end
    local x, y, z = engine("getWorldTranslation")(node)
    local dx, dy, dz = engine("localDirectionToWorld")(node, axis.direction.x, axis.direction.y, axis.direction.z)
    if type(x) ~= "number" or type(dx) ~= "number" then return nil end
    return x, y, z, dx, dy, dz
end

-- The ray the player aims with while a supported Hand Tool is in the Tool Hand: x, y, z, dirX, dirY, dirZ in the world, or
-- nil (the head's ray is the game's). `player` limits it to the player who holds the tool (nil: whoever holds it). The tool
-- must be where this module put it, so that the ray starts where the tool is drawn. Read live by the Player:getLookRay wrap.
function VRCompanion:getHandToolRay(player)
    local tool = heldTool()
    if tool == nil or state.linkedTool ~= tool then return nil end
    if player ~= nil and tool:getCarryingPlayer() ~= player then return nil end
    if toolHandFor(self, tool) == nil then return nil end
    local ok, x, y, z, dx, dy, dz = pcall(toolPointingRay, self, tool)
    if not ok then return nil end
    return x, y, z, dx, dy, dz
end

-- The wrap of Player:getLookRay, the single source PlayerTargeter:update reads (and everything that highlights by look): the
-- tool's ray while a supported Hand Tool is in a tracked Tool Hand, the game's own (the head's) otherwise.
WRAP_SPECS.getLookRay.make = function(original)
    return function(player, ...)
        if type(player) == "table" then
            local x, y, z, dx, dy, dz = VRCompanion:getHandToolRay(player)
            if x ~= nil then return x, y, z, dx, dy, dz end
        end
        return original(player, ...)
    end
end

-- ---- The chainsaw (ticket #114) -------------------------------------------------------------------------------------------
-- The chainsaw cuts where its bar is. In the Tool Hand it is placed like the other tools (SUPPORTED_TOOLS, the handle offset),
-- the look ray is its bar (HAND_TOOL_POINTING_AXES), so the game's targeter finds the trunk the bar is against and the game puts
-- the node the cut plane hangs from (cameraRotationNode) at the hit point: the cut height is where the bar meets the trunk, and
-- the game's ring marker shows it. What the game still takes from the CAMERA, and this section takes from the tool:
--   * the plane's heading: HandToolChainsaw:updateSplitPlane turns cameraRotationNode to the camera's yaw (camera:getRotation).
--     The wrap of HandToolChainsaw.onUpdate gives it the bar's yaw instead, while the game's own update runs, so that everything
--     the update works out (the plane the trunk is tested against, whether a cut is valid, the plane a finished cut slices along)
--     is of the bar's plane and not of one that is turned afterwards;
--   * the model: the update turns the model (graphicalNode) to face the camera; afterwards it is put back as the i3d has it,
--     which is pointing along the tool's root, i.e. along the hand.
-- A cut keeps the heading it started with (the game does not move the plane while it cuts, and takes the player's look away
-- while it does; here the heading is locked), so moving the hand does not move the plane. The cut ends as in the game when the
-- bar is taken off the trunk: updateCutting stops it once the targeter's last ray (now the bar) starts too far from the cut.
-- Holding the trigger cuts through the chainsaw's own cut handler (HandToolChainsaw.onCutAction: called when the trigger is
-- pulled and when it is let go, as the game's action event does, "useOnce" above).
-- The cut roll (the plane's tilt about the bar) is still the game's: its roll action. getChainsawCutRoll is where the wrist's
-- roll replaces it (ticket #115).
--
-- THE CHAINSAW'S STATE for the haptics (ticket #116): getChainsawCutState(tool) answers "idle", "delimbing" (the motor is
-- revved: the trigger is held without a cut) or "cutting".

-- The chainsaw's state: "idle", "delimbing" (revving without a cut) or "cutting"; nil when the tool is not a chainsaw (the
-- Hand Tool the player holds when no tool is given).
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

-- The cut roll the chainsaw should have now, in radians (about its bar; the game clamps it to +-pi/2), or nil to leave it to
-- the game's roll action. Ticket #115 answers the wrist's roll relative to the bar here. Asked before each update that is not
-- cutting; a cut keeps the roll it began with.
function VRCompanion:getChainsawCutRoll(tool)
    return nil
end

-- Whether the chainsaw is the tool this module has put in the Tool Hand, which is the only time the wrap changes anything.
local function chainsawInTheHand(tool)
    if type(tool) ~= "table" or tool.spec_chainsaw == nil or state.linkedTool ~= tool then return false end
    return toolHandFor(VRCompanion, tool) ~= nil
end

-- The heading of the bar: the yaw the game's own alignment would take from the camera (MathUtil.directionToPitchYaw: the
-- angle about the vertical of the direction), or nil when the bar points straight up or down.
local function barYaw(tool)
    local ok, x, _, _, dx, _, dz = pcall(toolPointingRay, VRCompanion, tool)
    if not ok or x == nil or dx * dx + dz * dz < 1e-6 then return nil end
    return math.atan2(dx, dz)
end

-- The chainsaw's update with the bar's heading in place of the camera's, and the model back along the tool afterwards.
local function updateChainsawFromTheTool(original, tool, dt, ...)
    local spec = tool.spec_chainsaw
    local states = engine("ChainsawCutState")
    local cutting = type(states) == "table" and spec.currentCutState == states.CUTTING
    local chainsaw = state.chainsaw
    if chainsaw.tool ~= tool then chainsaw.tool, chainsaw.yaw = tool, nil end

    if not cutting then
        chainsaw.yaw = barYaw(tool) or chainsaw.yaw  -- a cut keeps the last one
        local roll = VRCompanion:getChainsawCutRoll(tool)
        if type(roll) == "number" then spec.currentCutRoll = math.max(-math.pi * 0.5, math.min(math.pi * 0.5, roll)) end
    end

    local player = tool:getCarryingPlayer()
    local camera = player ~= nil and player.camera or nil
    local yaw = chainsaw.yaw
    if yaw ~= nil and type(camera) == "table" and type(camera.getRotation) == "function" then
        -- The game reads the heading of the plane off the camera: for the length of its update the camera answers the bar's.
        local own = rawget(camera, "getRotation")  -- nil when the camera has it from its class
        local gameGetRotation = camera.getRotation
        camera.getRotation = function(self, ...)
            local pitch, _, roll = gameGetRotation(self, ...)
            return pitch, yaw, roll
        end
        local ok, err = pcall(original, tool, dt, ...)
        camera.getRotation = own
        if not ok then error(err, 0) end
    else
        original(tool, dt, ...)  -- no heading to align from (a bar pointing straight up or down has none): the game's
    end

    -- The update turned the model to face the camera unless it is cutting (it then rides the cut guide): back along the tool.
    if tool.graphicalNode ~= nil and type(states) == "table" and spec.currentCutState ~= states.CUTTING then
        engine("setRotation")(tool.graphicalNode, 0, 0, 0)
    end
end

WRAP_SPECS.chainsawOnUpdate.make = function(original)
    return function(tool, ...)
        if chainsawInTheHand(tool) then return updateChainsawFromTheTool(original, tool, ...) end
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
    updateTrigger(self)
end

-- ---- Settings command, diagnostics, console commands, reset ---------------------------------------------------------------

function VRCompanion:setHandToolsEnabled(enabled, save)
    self.handToolsEnabled = (enabled ~= false)
    syncPlacement(self)  -- at once: the tool moves between the hand and the camera without waiting for the next update
    if not self.handToolsEnabled then endTriggerUse() end  -- and a spray in progress ends
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
    local fromTheTool = VRCompanion:getHandToolRay(player) ~= nil
    return string.format("look ray starts at (%.2f, %.2f, %.2f), from the %s", x, y, z, fromTheTool and "tool" or "head")
end

local function diagnosticFields(self)
    local tool, kind = self:getHeldHandTool()
    local hand, why = toolHandFor(self, tool)
    local toolText = "held Hand Tool: none"
    if tool ~= nil then
        toolText = kind ~= nil and ("held Hand Tool: " .. kind .. " (supported)") or "held Hand Tool: unsupported"
    end
    local function wrapStatus(name)
        if state.problem ~= nil then return name .. " wrap: gave up (" .. state.problem .. ")" end
        return name .. " wrap: " .. (wraps[name] ~= nil and "installed" or "not installed")
    end
    local wrapText = wrapStatus("attachToolToCamera")
    local lookRayWrapText = wrapStatus("getLookRay")
    local fields = {
        string.format("Hand Tools: %s", self.handToolsEnabled ~= false and "on" or "off"),
        string.format("Tool Hand: %s", HAND_NAMES[self:getToolHand()]),
        toolText,
        hand ~= nil and "in the Tool Hand: yes" or ("in the Tool Hand: no (" .. tostring(why) .. ")"),
        describeLookRay(tool),
        wrapText,
        lookRayWrapText,
    }
    -- Chainsaw (ticket #114)
    if kind == "chainsaw" then
        table.insert(fields, string.format("chainsaw: %s, cut plane heading from the %s", tostring(self:getChainsawCutState(tool)),
            (hand ~= nil and state.chainsaw.yaw ~= nil) and "bar" or "camera"))
    end
    table.insert(fields, wrapStatus("chainsawOnUpdate"))
    return fields
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
    endTriggerUse()
    if tool ~= nil and heldTool() == tool then pcall(returnToTheGame, tool) end
    state.linkedTool, state.linkedHand, state.linkedHolder = nil, nil, nil
end

-- Map unload: the tool goes back to the game's placement (the Virtual Hand it hangs below is deleted next), the wraps come
-- off and everything seen this session is forgotten. The saved setting stays as it is.
function VRCompanion:resetHandTools()
    local tool = state.linkedTool
    endTriggerUse()
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
