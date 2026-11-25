VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_FOV_OVERSCAN = 0
VRCompanion.MIN_FOV_OVERSCAN = 0
VRCompanion.MAX_FOV_OVERSCAN = 20
VRCompanion.fovOverscan = VRCompanion.DEFAULT_FOV_OVERSCAN

local SETTING_KEY = "vrSettings.fovOverscan"
local USAGE = "Usage: vrFovOverscan [0-20] (saved, 0 by default)"

local function parseArgument(arg)
    local percent = tonumber(arg)
    if percent == nil or percent ~= percent then return nil end
    if percent < VRCompanion.MIN_FOV_OVERSCAN or percent > VRCompanion.MAX_FOV_OVERSCAN then return nil end
    return percent
end

local function describe(percent)
    return string.format("%g%%", percent)
end

local renderedFovProjection = {}

local function describeRenderedFov()
    if type(VRBridge) ~= "table" or type(VRBridge.getEyeProjection) ~= "function" then return nil end
    local ok, projection = pcall(VRBridge.getEyeProjection, 0, renderedFovProjection)
    if not ok or type(projection) ~= "table" or projection.isValid ~= true then return nil end
    if type(projection.fovY) ~= "number" or type(projection.fovX) ~= "number" then return nil end
    return string.format("Rendered FOV %.1f x %.1f degrees (vertical x horizontal).", math.deg(projection.fovY), math.deg(projection.fovX))
end

function VRCompanion:applyFovOverscan()
    if type(VRBridge) == "table" and type(VRBridge.setFovOverscan) == "function" then
        VRBridge.setFovOverscan(self.fovOverscan or self.DEFAULT_FOV_OVERSCAN)
    end
end

function VRCompanion:setFovOverscan(percent, save)
    percent = parseArgument(percent)
    if percent == nil then return false end
    self.fovOverscan = percent
    self:applyFovOverscan()
    if not self.is2DViewActive then
        self:repositionAllHudDisplays()
    end
    if save then
        self:saveConfiguration()
    end
    return true
end

function VRCompanion:saveFovOverscanSetting(xmlFile, xml)
    if type(xml.setFloat) == "function" then
        xml.setFloat(xmlFile, SETTING_KEY, self.fovOverscan or self.DEFAULT_FOV_OVERSCAN)
    end
end

function VRCompanion:loadFovOverscanSetting(xmlFile, xml)
    if type(xml.getFloat) ~= "function" then return end
    local loaded = parseArgument(xml.getFloat(xmlFile, SETTING_KEY))
    if loaded ~= nil then
        self.fovOverscan = loaded
    end
end

function VRCompanion:consoleCommandSetFovOverscan(arg)
    if arg == nil or arg == "" then
        local current = self.fovOverscan or self.DEFAULT_FOV_OVERSCAN
        local rendered = describeRenderedFov()
        if rendered ~= nil then
            return string.format("FOV Overscan is %s. %s %s", describe(current), rendered, USAGE)
        end
        return string.format("FOV Overscan is %s. %s", describe(current), USAGE)
    end
    if not self:setFovOverscan(arg, true) then
        return USAGE
    end
    print(string.format("[FS25_VR] FOV Overscan set to: %s", describe(self.fovOverscan)))
    return string.format("FOV Overscan set to: %s (Saved)", describe(self.fovOverscan))
end

VRCompanion:registerSubsystem("fovOverscan", {
    mapLoad = function(self) self:applyFovOverscan() end,
    saveSettings = function(self, xmlFile, xml) self:saveFovOverscanSetting(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadFovOverscanSetting(xmlFile, xml) end,
    consoleCommands = {
        { name = "vrFovOverscan", description = "Widen the field of view the eyes are rendered with, in percent, to hide black edges during head movement (saved; 0 by default; a higher value softens the picture): vrFovOverscan [0-20]", method = "consoleCommandSetFovOverscan" },
    },
})
