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
--   VRCompanion.virtualGamepadEnabled  boolean, default false (the last ticket of spec #98 turns it on), saved
--                                      (`vrVirtualGamepad on|off`).
--   VRCompanion:getVirtualGamepadStatus() -> string  one phrase for the status command
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

local function newState()
    return {
        sentEnabled = nil,   -- what the Injector was last told
        sentLive = nil,
        seenKey = nil,       -- the last (present, slot, reason) the game log was told about
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

function VRCompanion:getVirtualGamepadStatus()
    local b = bridge()
    if b == nil or type(b.getVirtualGamepad) ~= "function" then
        return "not available (no Injector, or an older one)"
    end
    local present, slot, reason = b.getVirtualGamepad()
    return describe(present, slot, reason)
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
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrVirtualGamepad")
    end
    state = newState()
end

-- Export to VirtualGamepad subsystem table
VRCompanion.VirtualGamepad.updateVirtualGamepad = function(self, ...) return VRCompanion.updateVirtualGamepad(VRCompanion, ...) end
VRCompanion.VirtualGamepad.getVirtualGamepadStatus = function(self, ...) return VRCompanion.getVirtualGamepadStatus(VRCompanion, ...) end
VRCompanion.VirtualGamepad.consoleCommandSetVirtualGamepad = function(self, ...) return VRCompanion.consoleCommandSetVirtualGamepad(VRCompanion, ...) end

return VRCompanion.VirtualGamepad
