VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_MOUSE_CONFINEMENT = true
VRCompanion.mouseConfinement = VRCompanion.DEFAULT_MOUSE_CONFINEMENT

local SETTING_KEY = "vrSettings.mouseConfinement"
local USAGE = "Usage: vrMouseConfinement [on|off] (saved, on by default)"

local function describe(enabled)
    return enabled and "on" or "off"
end

function VRCompanion:applyMouseConfinement()
    if type(VRBridge) == "table" and type(VRBridge.setMouseConfinement) == "function" then
        VRBridge.setMouseConfinement(self.mouseConfinement ~= false)
    end
end

function VRCompanion:setMouseConfinement(enabled, save)
    self.mouseConfinement = (enabled ~= false)
    self:applyMouseConfinement()
    if save then
        self:saveConfiguration()
    end
end

function VRCompanion:saveMouseConfinementSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY, self.mouseConfinement ~= false)
    end
end

function VRCompanion:loadMouseConfinementSetting(xmlFile, xml)
    local loaded = nil
    if type(xml.getBool) == "function" then
        loaded = xml.getBool(xmlFile, SETTING_KEY)
    end
    if loaded == nil and type(xml.getString) == "function" then
        local str = xml.getString(xmlFile, SETTING_KEY)
        if str ~= nil then loaded = VRCompanion.parseOnOffArgument(str) end
    end
    if loaded ~= nil then
        self.mouseConfinement = loaded
    end
end

function VRCompanion:consoleCommandSetMouseConfinement(arg)
    if arg == nil or arg == "" then
        return string.format("Mouse Confinement is %s. %s", describe(self.mouseConfinement ~= false), USAGE)
    end
    local enabled = VRCompanion.parseOnOffArgument(arg)
    if enabled == nil then
        return USAGE
    end
    self:setMouseConfinement(enabled, true)
    print(string.format("[FS25_VR] Mouse Confinement set to: %s", describe(enabled)))
    return string.format("Mouse Confinement set to: %s (Saved)", describe(enabled))
end

VRCompanion:registerSubsystem("mouseConfinement", {
    mapLoad = function(self) self:applyMouseConfinement() end,
    saveSettings = function(self, xmlFile, xml) self:saveMouseConfinementSetting(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadMouseConfinementSetting(xmlFile, xml) end,
    resetSettings = function(self) self.mouseConfinement = self.DEFAULT_MOUSE_CONFINEMENT end,
    consoleCommands = {
        { name = "vrMouseConfinement", description = "Keep the Desk Mouse on the game picture while the headset is in use (saved; on by default): vrMouseConfinement [on|off]", method = "consoleCommandSetMouseConfinement" },
    },
})
