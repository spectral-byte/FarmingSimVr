-- ====================================================================
-- FS25 VR Companion Mod: Desktop Mirror switch
--
-- The console command `vrDesktopMirror [on|off]` turns the Desktop Mirror Window off while the headset session runs, so the GPU
-- time the desktop compositor spends scaling the full-size backbuffer goes to the headset (spec #191). The Companion Mod only
-- carries the setting: it sets the VRBridge desktop-mirror flag and the Injector's DXGI proxy reads it at every present. With
-- the mirror off the Injector does not call the game's real Present; the headset is unchanged. The mirror shows by itself while
-- the session is not running, in Pass-Through Mode and before the Companion Mod's handshake.
--
-- The setting is saved in settings.xml (on by default). When a savegame loads, Core's loadMap reads settings.xml and then calls
-- applyDesktopMirror, which sends the value to VRBridge after the handshake.
--
-- PUBLIC SURFACE
--   VRCompanion.desktopMirror                          true (the default) or false
--   VRCompanion:setDesktopMirror(enabled, save)        sets the field, pushes it to VRBridge, saves settings.xml when `save`
--   VRCompanion:applyDesktopMirror()                   pushes the field to VRBridge (map load, after loadConfiguration)
--   VRCompanion:saveDesktopMirrorSetting(xmlFile, xml) / loadDesktopMirrorSetting(xmlFile, xml)   hooks of saveConfiguration / loadConfiguration
--   VRCompanion:consoleCommandSetDesktopMirror(arg)    nil reports the state, "on" / "off" sets and saves it, anything else returns the usage
--   VRCompanion:registerDesktopMirrorConsoleCommands() / resetDesktopMirror()   map load / unload
--   Console: vrDesktopMirror [on|off]
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.DesktopMirror = VRCompanion.DesktopMirror or {}

VRCompanion.DEFAULT_DESKTOP_MIRROR = true
VRCompanion.desktopMirror = VRCompanion.DEFAULT_DESKTOP_MIRROR

local SETTING_KEY = "vrSettings.desktopMirror"
local USAGE = "Usage: vrDesktopMirror [on|off] (saved, on by default)"

local function describe(enabled)
    return enabled and "on" or "off"
end

-- The on/off an argument says, or nil when it says neither (the parser is shared with the other on/off commands)
local function parseArgument(arg)
    return VRCompanion.parseOnOffArgument(arg)
end

-- Send the field to the Injector. Called on map load after the handshake and whenever the command changes the field.
function VRCompanion:applyDesktopMirror()
    if type(VRBridge) == "table" and type(VRBridge.setDesktopMirror) == "function" then
        VRBridge.setDesktopMirror(self.desktopMirror ~= false)
    end
end

function VRCompanion:setDesktopMirror(enabled, save)
    self.desktopMirror = (enabled ~= false)
    self:applyDesktopMirror()
    if save and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
end

function VRCompanion:saveDesktopMirrorSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY, self.desktopMirror ~= false)
    end
end

-- A key that is missing keeps the current value (on by default). Sending it to VRBridge is applyDesktopMirror's job.
function VRCompanion:loadDesktopMirrorSetting(xmlFile, xml)
    local loaded = nil
    if type(xml.getBool) == "function" then
        loaded = xml.getBool(xmlFile, SETTING_KEY)
    end
    if loaded == nil and type(xml.getString) == "function" then
        local str = xml.getString(xmlFile, SETTING_KEY)
        if str ~= nil then loaded = parseArgument(str) end
    end
    if loaded ~= nil then
        self.desktopMirror = loaded
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
    self:setDesktopMirror(enabled, true)
    print(string.format("[FS25_VR] Desktop Mirror set to: %s", describe(enabled)))
    return string.format("Desktop Mirror set to: %s (Saved)", describe(enabled))
end

function VRCompanion:registerDesktopMirrorConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrDesktopMirror", "Turn the Desktop Mirror Window on or off while the headset is in use (saved; on by default; untested with frame generation): vrDesktopMirror [on|off]", "consoleCommandSetDesktopMirror", self)
end

-- Map unload: the command goes. The saved setting stays as it is, and the next map load reads it again (the Injector's VRBridge
-- flag is reset by VRBridge.onMapUnload).
function VRCompanion:resetDesktopMirror()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrDesktopMirror")
    end
end

VRCompanion.DesktopMirror.consoleCommandSetDesktopMirror = function(self, ...) return VRCompanion.consoleCommandSetDesktopMirror(VRCompanion, ...) end

return VRCompanion.DesktopMirror
