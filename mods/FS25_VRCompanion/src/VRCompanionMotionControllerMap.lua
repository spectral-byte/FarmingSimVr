-- ====================================================================
-- FS25 VR Companion Mod: Motion Controller button mappings
--
-- Lets a player holding a Motion Controller trigger the existing VR actions without the keyboard.
-- The module reads the per-frame hand state of VRCompanion:getHandState and, on the edge of an
-- input, calls the same handlers the key actions call: thumbstick left/right -> Snap Turn,
-- thumbstick click -> Recenter Action, menu button -> HUD Toggle Action.
--
--   * Both hands behave the same, whichever one is the Pointer Hand. A hand that is not tracked
--     does nothing; a hand that becomes tracked while an input is held does not fire until the
--     input is released and pressed again.
--   * An input acts once per press (a button) or once per push (the thumbstick), like a key action
--     does, then the handler's own debounce applies (Snap Turn 250 ms, HUD toggle 300 ms). If both
--     hands press in the same frame the action runs once.
--   * Nothing happens while VRCompanion.motionControllersEnabled is false (vrMotionControllers off).
--   * Snap Turn stays off while a menu is showing (VRCompanion:snapTurn refuses).
--
-- processMotionControllerButtons() is called once per update from updateMotionControllers; the\r\n-- rising-edge bookkeeping is per hand in `edges`.
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.MotionControllerMap = VRCompanion.MotionControllerMap or {}

-- The thumbstick counts as pushed sideways at this deflection (and while the push is more sideways
-- than up or down), and as let go again below the release value; the gap between them keeps a
-- wobbling stick from turning the player twice.
VRCompanion.MOTION_CONTROLLER_STICK_ENGAGE = 0.7
VRCompanion.MOTION_CONTROLLER_STICK_RELEASE = 0.4

local HANDS = { 0, 1 }

-- Which input drives which action. Buttons are the booleans of the hand state.
VRCompanion.DEFAULT_MOTION_CONTROLLER_MAP = {
    snapTurn = "thumbX",       -- left/right push of the thumbstick
    recenter = "thumbClick",
    hudToggle = "menu",
}
VRCompanion.motionControllerMap = {
    snapTurn = VRCompanion.DEFAULT_MOTION_CONTROLLER_MAP.snapTurn,
    recenter = VRCompanion.DEFAULT_MOTION_CONTROLLER_MAP.recenter,
    hudToggle = VRCompanion.DEFAULT_MOTION_CONTROLLER_MAP.hudToggle,
}

-- The inputs each action can be put on. The trigger and the grip are not offered: pointing at and
-- clicking a Click Point and the menu scroll/back use them. A thumbstick is only a Snap Turn.
local ACTIONS = { "snapTurn", "recenter", "hudToggle" }
local BUTTON_INPUTS = { "thumbClick", "buttonLower", "buttonUpper", "menu" }
local ALLOWED_INPUTS = {
    snapTurn = { "thumbX" },
    recenter = BUTTON_INPUTS,
    hudToggle = BUTTON_INPUTS,
}
local NO_INPUT = "none"
local SETTING_KEY_PREFIX = "vrSettings.motionControllerMap."

-- [hand] = { stick = -1 | 0 | 1, down = { [button] = bool } } for every tracked hand; nil for a
-- hand that is not tracked.
local edges = {}

local function sign(value)
    if value > 0 then return 1 end
    if value < 0 then return -1 end
    return 0
end

-- The sideways push of the thumbstick: -1 (left), 0 (none) or 1 (right), with hysteresis.
local function stickDirection(handState, engaged)
    local x, y = handState.thumbX or 0, handState.thumbY or 0
    if math.abs(x) >= VRCompanion.MOTION_CONTROLLER_STICK_ENGAGE and math.abs(x) > math.abs(y) then
        return sign(x)
    end
    if engaged ~= 0 and math.abs(x) >= VRCompanion.MOTION_CONTROLLER_STICK_RELEASE and sign(x) == engaged then
        return engaged
    end
    return 0
end

function VRCompanion:processMotionControllerButtons()
    if self.motionControllersEnabled == false then
        edges = {}
        return
    end
    local snapDirection = 0
    local pressed = {}
    for _, hand in ipairs(HANDS) do
        local handState = self:getHandState(hand)
        if handState.tracked ~= true then
            edges[hand] = nil
        else
            local previous = edges[hand]
            local current = { stick = 0, down = {} }
            edges[hand] = current
            local map = self.motionControllerMap
            if map.snapTurn == "thumbX" then
                current.stick = stickDirection(handState, previous and previous.stick or 0)
                if previous ~= nil and current.stick ~= 0 and current.stick ~= previous.stick and snapDirection == 0 then
                    snapDirection = current.stick
                end
            end
            for action, input in pairs(map) do
                if action ~= "snapTurn" and input ~= nil and input ~= false then
                    local down = handState[input] == true
                    current.down[input] = down
                    if down and previous ~= nil and previous.down[input] ~= true then
                        pressed[action] = true
                    end
                end
            end
        end
    end

    -- The lower face button is also back / close in a menu (VRCompanionMenuPointer.lua): while a menu
    -- is showing, a world action the player put on it (recenter, HUD toggle) yields to that.
    if self.isMenuVisible == true then
        for action, input in pairs(self.motionControllerMap) do
            if input == "buttonLower" then
                pressed[action] = nil
            end
        end
    end

    if snapDirection < 0 then
        self:onSnapTurnLeftAction()
    elseif snapDirection > 0 then
        self:onSnapTurnRightAction()
    end
    if pressed.recenter then
        self:onRecenterAction()
    end
    if pressed.hudToggle then
        self:onHudToggleAction()
    end
end

-- Map unload (and Motion Controllers off): forget what was held.
function VRCompanion:resetMotionControllerButtons()
    edges = {}
end

-- ---- Mapping, persistence, console command ------------------------------------------------------

local function findCaseInsensitive(list, wanted)
    local low = string.lower(tostring(wanted))
    for _, entry in ipairs(list) do
        if string.lower(entry) == low then return entry end
    end
    return nil
end

-- The canonical input name when `input` can drive `action`, NO_INPUT for "none", otherwise nil.
local function validInput(action, input)
    if input == nil then return nil end
    if string.lower(tostring(input)) == NO_INPUT then return NO_INPUT end
    return findCaseInsensitive(ALLOWED_INPUTS[action], input)
end

-- Puts `action` on `input` (an input name, "none" to remove it, or nil for the default). False when
-- the action or input is not one the module knows.
function VRCompanion:setMotionControllerMapping(action, input, save)
    action = findCaseInsensitive(ACTIONS, action)
    if action == nil then return false end
    if input == nil then
        self.motionControllerMap[action] = self.DEFAULT_MOTION_CONTROLLER_MAP[action]
    else
        local valid = validInput(action, input)
        if valid == nil then return false end
        self.motionControllerMap[action] = (valid ~= NO_INPUT) and valid or false
    end
    if save and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
    return true
end

function VRCompanion:resetMotionControllerMappings(save)
    for _, action in ipairs(ACTIONS) do
        self.motionControllerMap[action] = self.DEFAULT_MOTION_CONTROLLER_MAP[action]
    end
    if save and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
end

function VRCompanion:getMotionControllerMappingReport()
    local parts = {}
    for _, action in ipairs(ACTIONS) do
        parts[#parts + 1] = string.format("%s=%s", action, self.motionControllerMap[action] or NO_INPUT)
    end
    return table.concat(parts, ", ")
end

-- Called from saveMotionControllerSettings with the open settings file and the engine's setters.
function VRCompanion:saveMotionControllerMapSettings(xmlFile, xml)
    if type(xml.setString) ~= "function" then return end
    for _, action in ipairs(ACTIONS) do
        xml.setString(xmlFile, SETTING_KEY_PREFIX .. action, self.motionControllerMap[action] or NO_INPUT)
    end
end

-- Called from loadMotionControllerSettings with the open settings file and the engine's getters. A
-- key that is missing or holds something that is not an input for that action keeps the current value.
function VRCompanion:loadMotionControllerMapSettings(xmlFile, xml)
    if type(xml.getString) ~= "function" then return end
    for _, action in ipairs(ACTIONS) do
        local valid = validInput(action, xml.getString(xmlFile, SETTING_KEY_PREFIX .. action))
        if valid ~= nil then
            self.motionControllerMap[action] = (valid ~= NO_INPUT) and valid or false
        end
    end
end

function VRCompanion:consoleCommandMotionControllerMap(action, input)
    local usage = "Usage: vrMotionControllerMap [snapTurn|recenter|hudToggle] [<input>|none|default] | vrMotionControllerMap reset. "
        .. "Inputs: thumbClick, buttonLower, buttonUpper, menu (snapTurn: thumbX)"
    if action == nil or action == "" or string.lower(tostring(action)) == "status" then
        return "Motion Controller mappings: " .. self:getMotionControllerMappingReport() .. ". " .. usage
    end
    if string.lower(tostring(action)) == "reset" then
        self:resetMotionControllerMappings(true)
        return "Motion Controller mappings reset (Saved): " .. self:getMotionControllerMappingReport()
    end
    if input == nil or input == "" then
        return usage
    end
    local wanted = input
    if string.lower(tostring(input)) == "default" then wanted = nil end
    if not self:setMotionControllerMapping(action, wanted, true) then
        return usage
    end
    print(string.format("[FS25_VR] Motion Controller mappings: %s", self:getMotionControllerMappingReport()))
    return "Motion Controller mappings: " .. self:getMotionControllerMappingReport() .. " (Saved)"
end

function VRCompanion:registerMotionControllerMapConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrMotionControllerMap", "Show, change or remove the Motion Controller button mappings (saved): vrMotionControllerMap [snapTurn|recenter|hudToggle] [<input>|none|default] | reset", "consoleCommandMotionControllerMap", self)
end

function VRCompanion:unregisterMotionControllerMapConsoleCommands()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrMotionControllerMap")
    end
    edges = {}
end

-- Export to MotionControllerMap subsystem table
VRCompanion.MotionControllerMap.processMotionControllerButtons = function(self, ...) return VRCompanion.processMotionControllerButtons(VRCompanion, ...) end
VRCompanion.MotionControllerMap.consoleCommandMotionControllerMap = function(self, ...) return VRCompanion.consoleCommandMotionControllerMap(VRCompanion, ...) end

return VRCompanion.MotionControllerMap
