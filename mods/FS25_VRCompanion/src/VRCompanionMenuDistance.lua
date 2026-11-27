VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_MENU_DISTANCE = 1.5
VRCompanion.MIN_MENU_DISTANCE = 1.0
VRCompanion.MAX_MENU_DISTANCE = 5.0
VRCompanion.menuDistance = VRCompanion.DEFAULT_MENU_DISTANCE

local SETTING_KEY = "vrSettings.menuDistance"
local USAGE = "Usage: vrMenuDistance [1.0-5.0] (metres, saved, 1.5 by default)"

local function parseArgument(arg)
    return VRCompanion.parseFiniteNumber(arg, VRCompanion.MIN_MENU_DISTANCE, VRCompanion.MAX_MENU_DISTANCE)
end

local function describe(metres)
    return string.format("%g m", metres)
end

function VRCompanion:applyMenuDistance()
    if type(VRBridge) == "table" and type(VRBridge.setMenuDistance) == "function" then
        VRBridge.setMenuDistance(self.menuDistance or self.DEFAULT_MENU_DISTANCE)
    end
end

function VRCompanion:setMenuDistance(metres, save)
    metres = parseArgument(metres)
    if metres == nil then return false end
    self.menuDistance = metres
    self:applyMenuDistance()
    if save then
        self:saveConfiguration()
    end
    return true
end

function VRCompanion:saveMenuDistanceSetting(xmlFile, xml)
    if type(xml.setFloat) == "function" then
        xml.setFloat(xmlFile, SETTING_KEY, self.menuDistance or self.DEFAULT_MENU_DISTANCE)
    end
end

function VRCompanion:loadMenuDistanceSetting(xmlFile, xml)
    if type(xml.getFloat) ~= "function" then return end
    local loaded = parseArgument(xml.getFloat(xmlFile, SETTING_KEY))
    if loaded ~= nil then
        self.menuDistance = loaded
    end
end

function VRCompanion:consoleCommandSetMenuDistance(arg)
    if arg == nil or arg == "" then
        return string.format("Menu distance is %s. %s", describe(self.menuDistance or self.DEFAULT_MENU_DISTANCE), USAGE)
    end
    if not self:setMenuDistance(arg, true) then
        return USAGE
    end
    print(string.format("[FS25_VR] Menu distance set to: %s", describe(self.menuDistance)))
    return string.format("Menu distance set to: %s (Saved)", describe(self.menuDistance))
end

VRCompanion:registerSubsystem("menuDistance", {
    mapLoad = function(self) self:applyMenuDistance() end,
    saveSettings = function(self, xmlFile, xml) self:saveMenuDistanceSetting(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadMenuDistanceSetting(xmlFile, xml) end,
    resetSettings = function(self) self.menuDistance = self.DEFAULT_MENU_DISTANCE end,
    consoleCommands = {
        { name = "vrMenuDistance", description = "How far in front of you menus, the main menu included, and 2D Floating Window Mode are shown, in metres (saved; 1.5 by default; further away they fill less of the view): vrMenuDistance [1.0-5.0]", method = "consoleCommandSetMenuDistance" },
    },
})
