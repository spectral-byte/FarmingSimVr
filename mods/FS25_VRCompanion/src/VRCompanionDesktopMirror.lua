VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_DESKTOP_MIRROR = true
VRCompanion.desktopMirror = VRCompanion.DEFAULT_DESKTOP_MIRROR

local SETTING_KEY = "vrSettings.desktopMirror"
local USAGE = "Usage: vrDesktopMirror [on|off] (saved, on by default)"

local function describe(enabled)
    return enabled and "on" or "off"
end

local function parseArgument(arg)
    return VRCompanion.parseOnOffArgument(arg)
end

function VRCompanion:applyDesktopMirror()
    if type(VRBridge) == "table" and type(VRBridge.setDesktopMirror) == "function" then
        VRBridge.setDesktopMirror(self.desktopMirror ~= false)
    end
end

function VRCompanion:setDesktopMirror(enabled, save)
    self.desktopMirror = (enabled ~= false)
    self:applyDesktopMirror()
    if save then
        self:saveConfiguration()
    end
end

function VRCompanion:saveDesktopMirrorSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY, self.desktopMirror ~= false)
    end
end

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

VRCompanion:registerSubsystem("desktopMirror", {
    mapLoad = function(self) self:applyDesktopMirror() end,
    saveSettings = function(self, xmlFile, xml) self:saveDesktopMirrorSetting(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadDesktopMirrorSetting(xmlFile, xml) end,
    resetSettings = function(self) self.desktopMirror = self.DEFAULT_DESKTOP_MIRROR end,
    consoleCommands = {
        { name = "vrDesktopMirror", description = "Turn the Desktop Mirror Window on or off while the headset is in use (saved; on by default; untested with frame generation): vrDesktopMirror [on|off]", method = "consoleCommandSetDesktopMirror" },
    },
})
