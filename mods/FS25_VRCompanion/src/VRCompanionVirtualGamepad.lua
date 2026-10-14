-- ====================================================================
-- FS25 VR Companion Mod: Virtual Gamepad Subsystem (ADR-0028)
--
-- The Injector presents the Motion Controllers' triggers, grips, thumbsticks and buttons to the game as an Xbox
-- pad it reads through XInput (the Virtual Gamepad, CONTEXT.md), so the player binds them in the game's own
-- Controls screen. The pad itself lives in the Injector; this module owns what only the Companion Mod knows:
-- the saved `vrVirtualGamepad` setting, the Motion Controller switch the pad follows, and the game-log and
-- status-command words about it.
--
-- PUBLIC SURFACE
--
--   VRBridge.setVirtualGamepad(enabled, live)       tell the Injector what is wanted (sent when it changes)
--     enabled  the saved `vrVirtualGamepad` setting
--     live     Motion Controller input is on (`vrMotionControllers`); the pad reads neutral while it is not
--   VRBridge.getVirtualGamepad() -> present, slot, reason   what the game sees: the XInput slot (0..3) while
--                                                    present, otherwise why not
--
--   VRBridge.setVirtualGamepadUse(leftInputs, rightInputs)   which inputs of each hand the Companion Mod is using itself
--                                                    (bit set, see INPUT_BITS below); sent every update
--
--   VRCompanion.virtualGamepadEnabled  boolean, default false (the last ticket of spec #98 turns it on), saved
--                                      (`vrVirtualGamepad on|off`).
--   VRCompanion:getVirtualGamepadStatus() -> string  one phrase for the status command
--
-- THE MOD'S OWN USES (ticket #101, ADR-0028): while the Companion Mod is using a Motion Controller input itself, that input
-- reads as released on the pad, so a VR use never also fires a game binding. A use asks for an input every update it wants
-- it, and the answer is whether it may have it:
--   VRCompanion:takeVirtualGamepadInput(hand, input) -> boolean   input is one of "trigger", "grip", "stick",
--                                      "stickClick", "lower", "upper", "menu". False while the input is still held for its
--                                      binding (held when the use would have begun): the use must not act on it.
-- Uses made here, at the end of every update: every input of both hands while a menu shows (the Pointer Ray menus act on
-- the raw hand state, as they always did), and the grip of a hand holding the Steering Wheel. Interactive Control's click
-- asks for the Pointer Hand's trigger itself, as the ray lands on a Click Point.
-- THE HELD-INPUT RULE: an input already held (as the pad reads it) when a use begins keeps its binding until it is let go,
-- and the use does not act on it meanwhile. The same holds the other way round: an input a use took stays off the pad
-- until it is let go, even if the use ends first (a grip that closed a menu does not fire its binding as the menu goes).
--
--   Hooks into the rest of the mod (each a single guarded call):
--     MotionControllers.lua  updateMotionControllers -> updateVirtualGamepad(dt) first of all, saveMotionControllerSettings
--                            / loadMotionControllerSettings -> save/loadVirtualGamepadSetting, getMotionControllerStatus ->
--                            getVirtualGamepadStatus, registerMotionControllerConsoleCommands and resetMotionControllers ->
--                            registerVirtualGamepadConsoleCommands / resetVirtualGamepad
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.VirtualGamepad = VRCompanion.VirtualGamepad or {}

-- vrVirtualGamepad on|off: saved. Off removes the pad from the game altogether.
VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_ENABLED = false
VRCompanion.virtualGamepadEnabled = VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_ENABLED

local SETTING_KEY_ENABLED = "vrSettings.virtualGamepad"

-- The inputs of a hand a use can take, with the bit the Injector knows each by (VirtualGamepad::kInput*).
local INPUTS = { "trigger", "grip", "stick", "stickClick", "lower", "upper", "menu" }
local INPUT_BITS = { trigger = 1, grip = 2, stick = 4, stickClick = 8, lower = 16, upper = 32, menu = 64 }
local INPUT_NAMES = {
    trigger = "trigger", grip = "grip", stick = "thumbstick", stickClick = "thumbstick click",
    lower = "lower button", upper = "upper button", menu = "menu button",
}
local HANDS = { 0, 1 }
local HAND_NAMES = { [0] = "left", [1] = "right" }

-- When an input counts as held, as the pad reads it (pressed above the first number, let go below the second): the trigger
-- is analog, so any pull the game would notice counts; the grip is the pad's digital LB / RB; the thumbstick is held out of
-- the game's dead zone.
local HELD_THRESHOLDS = {
    trigger = { 0.12, 0.08 },
    grip = { 0.6, 0.4 },
    stick = { 0.25, 0.15 },
}

local function newUses()
    local uses = {}
    for _, hand in ipairs(HANDS) do
        uses[hand] = {}
        for _, input in ipairs(INPUTS) do
            -- calledAt: the last update a use asked for it. down: held, as of the last look. yielding: held when the use
            -- began, so it keeps its binding. suppressed: reads as released on the pad.
            uses[hand][input] = { calledAt = -1, down = false, yielding = false, suppressed = false }
        end
    end
    return uses
end

local function newState()
    return {
        sentEnabled = nil,   -- what the Injector was last told
        sentLive = nil,
        seenKey = nil,       -- the last (present, slot, reason) the game log was told about
        update = 0,          -- counts the Companion Mod's updates
        uses = newUses(),
        sentUse = { [0] = 0, [1] = 0 },  -- the sets of inputs the Injector was last told the Companion Mod is using
    }
end

local state = newState()

local function bridge()
    local b = VRBridge or (_G and _G.VRBridge)
    return b
end

local function describe(present, slot, reason)
    if present then
        return string.format("present in XInput slot %d", slot)
    end
    return "not present (" .. tostring(reason) .. ")"
end

-- Tells the Injector what is wanted, when it changed, and says in the game log what the game now sees.
function VRCompanion:updateVirtualGamepad(dt)
    state.update = state.update + 1
    local b = bridge()
    if b == nil or type(b.setVirtualGamepad) ~= "function" then return end

    local enabled = self.virtualGamepadEnabled == true
    local live = self.motionControllersEnabled ~= false
    if enabled ~= state.sentEnabled or live ~= state.sentLive then
        b.setVirtualGamepad(enabled, live)
        state.sentEnabled, state.sentLive = enabled, live
    end

    if type(b.getVirtualGamepad) ~= "function" then return end
    local present, slot, reason = b.getVirtualGamepad()
    local key = present and ("slot" .. tostring(slot)) or reason
    if key ~= state.seenKey then
        -- The first look at a pad that is simply off says nothing: a player who never turned it on reads no line.
        if state.seenKey ~= nil or present or reason ~= "off" then
            print("[FS25_VR] Virtual Gamepad: " .. describe(present, slot, reason))
        end
        state.seenKey = key
    end
end

-- ---- The Companion Mod's own uses --------------------------------------------------------------------

-- Whether the input is held on the pad right now, with hysteresis against a trigger or grip that rests half-way.
local function isHeld(self, hand, input, wasDown)
    local hands = self:getHandState(hand)
    if hands == nil or hands.tracked ~= true then return false end
    local thresholds = HELD_THRESHOLDS[input]
    if input == "stick" then
        local magnitude = math.max(math.abs(hands.thumbX or 0), math.abs(hands.thumbY or 0))
        return magnitude >= (wasDown and thresholds[2] or thresholds[1])
    elseif thresholds ~= nil then
        local value = (input == "trigger") and hands.trigger or hands.grip
        return (value or 0) >= (wasDown and thresholds[2] or thresholds[1])
    elseif input == "stickClick" then
        return hands.thumbClick == true
    elseif input == "lower" then
        return hands.buttonLower == true
    elseif input == "upper" then
        return hands.buttonUpper == true
    end
    return hands.menu == true
end

-- A use of the Companion Mod asks for an input of a hand, every update it wants it. True: the use has it, and the input
-- reads as released on the pad from here on. False: the input was already held when the use would have begun, so it keeps
-- its binding until it is let go and the use must not act on it. Asking again in the same update gives the same answer.
function VRCompanion:takeVirtualGamepadInput(hand, input)
    local record = state.uses[hand] and state.uses[hand][input]
    if record == nil then return true end
    if record.calledAt == state.update then return not record.yielding end

    local wasDown = record.down  -- as of the end of the update before
    local down = isHeld(self, hand, input, wasDown)
    record.down = down
    -- The use goes on from the update before, or from one that took the input and still has it held (a ray that flickers
    -- off a Click Point for a frame does not hand a pulled trigger to the pad). Otherwise it begins now, and an input
    -- that was already held before this update is the binding's: the squeeze that starts the use is not.
    local continuing = record.calledAt == state.update - 1 or record.suppressed
    if not continuing then record.yielding = wasDown end
    record.calledAt = state.update
    if record.yielding and not down then record.yielding = false end  -- let go: the use may have it from now on
    return not record.yielding
end

local function describeUse()
    local names = {}
    for _, hand in ipairs(HANDS) do
        for _, input in ipairs(INPUTS) do
            if state.sentUse[hand] % (INPUT_BITS[input] * 2) >= INPUT_BITS[input] then
                names[#names + 1] = HAND_NAMES[hand] .. " " .. INPUT_NAMES[input]
            end
        end
    end
    return #names > 0 and table.concat(names, ", ") or "none"
end

-- Last of the Companion Mod's update: the uses made here, then which inputs are off the pad this update, told to the Injector.
function VRCompanion:updateVirtualGamepadUse(dt)
    if self.motionControllersEnabled ~= false then
        if self.isMenuVisible == true then
            -- Every input of both hands: the Pointer Ray menus act on the raw hand state themselves.
            for _, hand in ipairs(HANDS) do
                for _, input in ipairs(INPUTS) do
                    if not (hand == 1 and input == "menu") then  -- the right menu button feeds the pad nothing
                        self:takeVirtualGamepadInput(hand, input)
                    end
                end
            end
        end
        if type(self.isHandHoldingSteeringWheel) == "function" then
            for _, hand in ipairs(HANDS) do
                if self:isHandHoldingSteeringWheel(hand) == true then
                    self:takeVirtualGamepadInput(hand, "grip")
                end
            end
        end
    end

    for _, hand in ipairs(HANDS) do
        local mask = 0
        for _, input in ipairs(INPUTS) do
            local record = state.uses[hand][input]
            if record.calledAt == state.update then
                record.suppressed = not record.yielding
            else
                record.down = isHeld(self, hand, input, record.down)
                -- No use wants it any more, but what a use took stays off the pad until it is let go.
                record.suppressed = record.suppressed and record.down
                record.yielding = false
            end
            if record.suppressed then mask = mask + INPUT_BITS[input] end
        end
        state.sentUse[hand] = mask
    end
    local b = bridge()
    if b ~= nil and type(b.setVirtualGamepadUse) == "function" then
        b.setVirtualGamepadUse(state.sentUse[0], state.sentUse[1])
    end
end

function VRCompanion:getVirtualGamepadStatus()
    local b = bridge()
    if b == nil or type(b.getVirtualGamepad) ~= "function" then
        return "not available (no Injector, or an older one)"
    end
    local present, slot, reason = b.getVirtualGamepad()
    local text = describe(present, slot, reason)
    if present then
        text = text .. "; the Companion Mod is using: " .. describeUse()
    end
    return text
end

function VRCompanion:saveVirtualGamepadSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY_ENABLED, self.virtualGamepadEnabled == true)
    end
end

function VRCompanion:loadVirtualGamepadSetting(xmlFile, xml)
    local loaded = nil
    if type(xml.getBool) == "function" then
        loaded = xml.getBool(xmlFile, SETTING_KEY_ENABLED)
    end
    if loaded == nil and type(xml.getString) == "function" then
        local str = xml.getString(xmlFile, SETTING_KEY_ENABLED)
        if str ~= nil then loaded = VRCompanion.parseOnOffArgument(str) end
    end
    if loaded ~= nil then
        self.virtualGamepadEnabled = loaded
    end
end

function VRCompanion:consoleCommandSetVirtualGamepad(arg)
    local usage = "Usage: vrVirtualGamepad <on|off> (saved)"
    if arg == nil or arg == "" or string.lower(tostring(arg)) == "status" then
        return string.format("Virtual Gamepad is %s: %s. %s",
            self.virtualGamepadEnabled and "on" or "off", self:getVirtualGamepadStatus(), usage)
    end
    local enabled = VRCompanion.parseOnOffArgument(arg)
    if enabled == nil then
        return usage
    end
    self.virtualGamepadEnabled = enabled
    self:updateVirtualGamepad(0)
    if type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
    print(string.format("[FS25_VR] Virtual Gamepad set to: %s", enabled and "on" or "off"))
    return string.format("Virtual Gamepad set to: %s (Saved): %s", enabled and "on" or "off", self:getVirtualGamepadStatus())
end

function VRCompanion:registerVirtualGamepadConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrVirtualGamepad", "Turn the Virtual Gamepad (the Motion Controllers as an Xbox pad bound in the Controls screen) on or off (saved): vrVirtualGamepad [on|off]", "consoleCommandSetVirtualGamepad", self)
end

-- Map unload: the pad stays in the game (the OpenXR session goes on) but reads neutral; the next map sends
-- everything again.
function VRCompanion:resetVirtualGamepad()
    local b = bridge()
    if b ~= nil and type(b.setVirtualGamepad) == "function" and state.sentEnabled ~= nil then
        b.setVirtualGamepad(state.sentEnabled, false)
    end
    if b ~= nil and type(b.setVirtualGamepadUse) == "function" then
        b.setVirtualGamepadUse(0, 0)
    end
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrVirtualGamepad")
    end
    state = newState()
end

-- Export to VirtualGamepad subsystem table
VRCompanion.VirtualGamepad.updateVirtualGamepad = function(self, ...) return VRCompanion.updateVirtualGamepad(VRCompanion, ...) end
VRCompanion.VirtualGamepad.updateVirtualGamepadUse = function(self, ...) return VRCompanion.updateVirtualGamepadUse(VRCompanion, ...) end
VRCompanion.VirtualGamepad.takeVirtualGamepadInput = function(self, ...) return VRCompanion.takeVirtualGamepadInput(VRCompanion, ...) end
VRCompanion.VirtualGamepad.getVirtualGamepadStatus =function(self, ...) return VRCompanion.getVirtualGamepadStatus(VRCompanion, ...) end
VRCompanion.VirtualGamepad.consoleCommandSetVirtualGamepad = function(self, ...) return VRCompanion.consoleCommandSetVirtualGamepad(VRCompanion, ...) end

return VRCompanion.VirtualGamepad
