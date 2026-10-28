-- ====================================================================
-- FS25 VR Companion Mod: Desktop Mirror switch
--
-- The console command `vrDesktopMirror [on|off]` turns the Desktop Mirror Window off while the headset session runs, so the GPU
-- time the desktop compositor spends scaling the full-size backbuffer goes to the headset (spec #191). The Companion Mod only
-- carries the setting: it sets the VRBridge desktop-mirror flag and the Injector's DXGI proxy reads it at every present. With
-- the mirror off the Injector does not call the game's real Present; the headset is unchanged. The mirror shows by itself while
-- the session is not running, in Pass-Through Mode and before the Companion Mod's handshake. For now the setting lasts for the
-- session only.
--
-- PUBLIC SURFACE
--   VRCompanion.desktopMirror                          true (the default) or false
--   VRCompanion:setDesktopMirror(enabled)              sets the field and pushes it to VRBridge
--   VRCompanion:consoleCommandSetDesktopMirror(arg)    nil reports the state, "on" / "off" sets it, anything else returns the usage
--   VRCompanion:registerDesktopMirrorConsoleCommands() / resetDesktopMirror()   map load / unload
--   Console: vrDesktopMirror [on|off]
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.DesktopMirror = VRCompanion.DesktopMirror or {}

VRCompanion.DEFAULT_DESKTOP_MIRROR = true
VRCompanion.desktopMirror = VRCompanion.DEFAULT_DESKTOP_MIRROR

local USAGE = "Usage: vrDesktopMirror [on|off]"

local function describe(enabled)
    return enabled and "on" or "off"
end

-- The on/off an argument says, or nil when it says neither (the parser is shared with the other on/off commands)
local function parseArgument(arg)
    if type(VRCompanion.parseOnOffArgument) == "function" then
        return VRCompanion.parseOnOffArgument(arg)
    end
    local low = string.lower(tostring(arg))
    if low == "on" or low == "true" or low == "1" then return true end
    if low == "off" or low == "false" or low == "0" then return false end
    return nil
end

function VRCompanion:setDesktopMirror(enabled)
    self.desktopMirror = (enabled ~= false)
    if type(VRBridge) == "table" and type(VRBridge.setDesktopMirror) == "function" then
        VRBridge.setDesktopMirror(self.desktopMirror)
    end
end

function VRCompanion:consoleCommandSetDesktopMirror(arg)
    if arg == nil or arg == "" then
        return string.format("Desktop Mirror is %s. %s", describe(self.desktopMirror ~= false), USAGE)
    end
    local enabled = parseArgument(arg)
    if enabled == nil then
        return USAGE
    end
    self:setDesktopMirror(enabled)
    print(string.format("[FS25_VR] Desktop Mirror set to: %s", describe(enabled)))
    return string.format("Desktop Mirror set to: %s", describe(enabled))
end

function VRCompanion:registerDesktopMirrorConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrDesktopMirror", "Turn the Desktop Mirror Window on or off while the headset is in use: vrDesktopMirror [on|off]", "consoleCommandSetDesktopMirror", self)
end

-- Map unload: the mirror is on again (the Injector's VRBridge flag is reset by VRBridge.onMapUnload)
function VRCompanion:resetDesktopMirror()
    self.desktopMirror = self.DEFAULT_DESKTOP_MIRROR
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrDesktopMirror")
    end
end

VRCompanion.DesktopMirror.consoleCommandSetDesktopMirror = function(self, ...) return VRCompanion.consoleCommandSetDesktopMirror(VRCompanion, ...) end

return VRCompanion.DesktopMirror
