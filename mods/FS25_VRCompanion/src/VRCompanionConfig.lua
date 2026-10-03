-- ====================================================================
-- FS25 VR Companion Mod: Config Subsystem
-- Manages XML persistence and settings configuration
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.Config = VRCompanion.Config or {}

-- VR HUD Inset and UI Scale Constants
VRCompanion.DEFAULT_HUD_INSET_X = 0.20
VRCompanion.DEFAULT_HUD_INSET_Y = 0.16
VRCompanion.DEFAULT_HUD_INSET_TOP = nil
VRCompanion.DEFAULT_HUD_UI_SCALE = 0.88

VRCompanion.MIN_HUD_INSET_X = 0.0
VRCompanion.MAX_HUD_INSET_X = 0.40
VRCompanion.MIN_HUD_INSET_Y = 0.0
VRCompanion.MAX_HUD_INSET_Y = 0.35
VRCompanion.MIN_HUD_INSET_TOP = 0.0
VRCompanion.MAX_HUD_INSET_TOP = 0.50
VRCompanion.MIN_HUD_UI_SCALE = 0.50
VRCompanion.MAX_HUD_UI_SCALE = 1.50

-- VR HUD Convergence Parity Constants
VRCompanion.DEFAULT_CONVERGENCE_DEPTH = 1.8
VRCompanion.DEFAULT_CONVERGENCE_DISPARITY = 0.012
VRCompanion.DEFAULT_IPD = 0.064
VRCompanion.MIN_IPD = 0.040
VRCompanion.MAX_IPD = 0.085
VRCompanion.MIN_CONVERGENCE_DEPTH = 0.5
VRCompanion.MAX_CONVERGENCE_DEPTH = 5.0
VRCompanion.MIN_CONVERGENCE_DISPARITY = 0.0
VRCompanion.MAX_CONVERGENCE_DISPARITY = 0.040
VRCompanion.CONVERGENCE_PARALLAX_FACTOR = 0.3375
VRCompanion.CONVERGENCE_SMOOTH_SPEED = 12.0

-- VR Fill Level Element Positioning
VRCompanion.DEFAULT_FILL_LEVEL_UNDER_VEHICLE = true
VRCompanion.fillLevelUnderVehicle = true
VRCompanion.DEFAULT_HUD_FILL_LEVEL_GAP = 0.004
VRCompanion.hudFillLevelGap = VRCompanion.DEFAULT_HUD_FILL_LEVEL_GAP

-- VR HUD Notifications
VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION = false
VRCompanion.showHudLoadNotification = false

-- Recenter VR Tracking Space Configuration
VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE = true
VRCompanion.recenterVrTrackingSpace = true

VRCompanion.RECENTER_MODES = {
    BOTH = "Both",
    VEHICLE_ONLY = "VehicleOnly"
}

-- XML Helper Functions
local function readXmlFloat(getXmlFloatFn, xmlFile, ...)
    local keys = {...}
    for _, k in ipairs(keys) do
        local val = getXmlFloatFn(xmlFile, k)
        if val ~= nil then
            return val
        end
    end
    return nil
end

local function readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile, ...)
    local keys = {...}
    for _, k in ipairs(keys) do
        if type(getXmlBoolFn) == "function" then
            local val = getXmlBoolFn(xmlFile, k)
            if val ~= nil then
                return val
            end
        end
        if type(getXmlStringFn) == "function" then
            local str = getXmlStringFn(xmlFile, k)
            if str ~= nil then
                local lower = string.lower(tostring(str))
                if lower == "true" or lower == "1" then return true
                elseif lower == "false" or lower == "0" then return false end
            end
        end
    end
    return nil
end

local function readXmlString(getXmlStringFn, xmlFile, ...)
    local keys = {...}
    for _, k in ipairs(keys) do
        if type(getXmlStringFn) == "function" then
            local val = getXmlStringFn(xmlFile, k)
            if val ~= nil and val ~= "" then
                return tostring(val)
            end
        end
    end
    return nil
end

function VRCompanion:clamp(val, minVal, maxVal)
    if val == nil then return minVal end
    if minVal ~= nil and val < minVal then return minVal end
    if maxVal ~= nil and val > maxVal then return maxVal end
    return val
end

function VRCompanion:getUserSettingsPath()
    local getAppPathFn = getUserProfileAppPath or _G.getUserProfileAppPath
    if type(getAppPathFn) == "function" then
        local appPath = getAppPathFn()
        if appPath ~= nil and appPath ~= "" then
            return appPath .. "modSettings/FS25_VRCompanion/settings.xml"
        end
    end
    return nil
end

function VRCompanion:saveConfiguration(xmlPath)
    local path = xmlPath or self:getUserSettingsPath()
    if path == nil then return false end

    local createFolderFn = createFolder or _G.createFolder
    local createXmlFn = createXMLFile or _G.createXMLFile
    local setXmlFloatFn = setXMLFloat or _G.setXMLFloat
    local setXmlBoolFn = setXMLBool or _G.setXMLBool
    local setXmlStringFn = setXMLString or _G.setXMLString
    local saveXmlFn = saveXMLFile or _G.saveXMLFile
    local deleteXmlFn = delete or _G.delete

    if type(createXmlFn) ~= "function" or type(setXmlFloatFn) ~= "function" or type(saveXmlFn) ~= "function" then
        return false
    end

    local getAppPathFn = getUserProfileAppPath or _G.getUserProfileAppPath
    if type(createFolderFn) == "function" and type(getAppPathFn) == "function" then
        local appPath = getAppPathFn()
        if appPath ~= nil and appPath ~= "" then
            pcall(createFolderFn, appPath .. "modSettings")
            pcall(createFolderFn, appPath .. "modSettings/FS25_VRCompanion")
        end
    end

    local ok, xmlFile = pcall(createXmlFn, "VRCompanionConfig", path, "vrSettings")
    if ok and xmlFile ~= 0 and xmlFile ~= nil then
        setXmlFloatFn(xmlFile, "vrSettings.hudInsetHorizontal", self.hudInsetX or self.DEFAULT_HUD_INSET_X)
        setXmlFloatFn(xmlFile, "vrSettings.hudInsetVertical", self.hudInsetY or self.DEFAULT_HUD_INSET_Y)
        setXmlFloatFn(xmlFile, "vrSettings.hudInsetTop", self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_Y)
        setXmlFloatFn(xmlFile, "vrSettings.hudUiScale", self.hudUiScaleMultiplier or self.DEFAULT_HUD_UI_SCALE)
        setXmlFloatFn(xmlFile, "vrSettings.hudConvergenceDepth", self.hudConvergenceDepth or self.DEFAULT_CONVERGENCE_DEPTH)
        if type(setXmlBoolFn) == "function" then
            setXmlBoolFn(xmlFile, "vrSettings.fillLevelUnderVehicle", self.fillLevelUnderVehicle == true)
            setXmlBoolFn(xmlFile, "vrSettings.showHudLoadNotification", self.showHudLoadNotification == true)
            setXmlBoolFn(xmlFile, "vrSettings.recenterVrTrackingSpace", self.recenterVrTrackingSpace ~= false)
        end
        if type(setXmlStringFn) == "function" then
            local modeStr = (self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS) and "DualPass" or "AFR"
            setXmlStringFn(xmlFile, "vrSettings.renderingMode", modeStr)
        end
        setXmlFloatFn(xmlFile, "vrSettings.hudFillLevelGap", self.hudFillLevelGap or self.DEFAULT_HUD_FILL_LEVEL_GAP)
        setXmlFloatFn(xmlFile, "vrSettings.snapTurnAngle", self:getSnapTurnAngleDegrees())

        pcall(saveXmlFn, xmlFile)
        if type(deleteXmlFn) == "function" then
            pcall(deleteXmlFn, xmlFile)
        end
        print(string.format("[FS25_VR] Saved VR HUD configuration to: %s", tostring(path)))
        return true
    end
    return false
end

function VRCompanion:loadConfiguration(xmlPath)
    local insetX = self.DEFAULT_HUD_INSET_X
    local insetY = self.DEFAULT_HUD_INSET_Y
    local scale = self.DEFAULT_HUD_UI_SCALE
    local insetTop = nil

    local loadXmlFn = loadXMLFile or _G.loadXMLFile
    local getXmlFloatFn = getXMLFloat or _G.getXMLFloat
    local getXmlBoolFn = getXMLBool or _G.getXMLBool
    local getXmlStringFn = getXMLString or _G.getXMLString
    local deleteXmlFn = delete or _G.delete

    local function readSettingsFromXml(filePath)
        if filePath ~= nil and type(loadXmlFn) == "function" and type(getXmlFloatFn) == "function" then
            local ok, xmlFile = pcall(loadXmlFn, "VRCompanionConfig", filePath)
            if ok and xmlFile ~= 0 and xmlFile ~= nil then
                local loadedX = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudInsetHorizontal", "vrSettings.hudInsetHorizontal#value",
                    "modDesc.vrSettings.hudInsetHorizontal", "modDesc.vrSettings.hudInsetHorizontal#value")
                if loadedX ~= nil then insetX = loadedX end

                local loadedY = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudInsetVertical", "vrSettings.hudInsetVertical#value",
                    "modDesc.vrSettings.hudInsetVertical", "modDesc.vrSettings.hudInsetVertical#value")
                if loadedY ~= nil then insetY = loadedY end

                local loadedInsetTop = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudInsetTop", "vrSettings.hudInsetTop#value",
                    "vrSettings.hudTopInset", "vrSettings.hudTopInset#value",
                    "vrSettings.hudInsetVerticalTop", "vrSettings.hudInsetVerticalTop#value",
                    "modDesc.vrSettings.hudInsetTop", "modDesc.vrSettings.hudInsetTop#value",
                    "modDesc.vrSettings.hudTopInset", "modDesc.vrSettings.hudTopInset#value",
                    "modDesc.vrSettings.hudInsetVerticalTop", "modDesc.vrSettings.hudInsetVerticalTop#value")
                if loadedInsetTop ~= nil then
                    insetTop = self:clamp(loadedInsetTop, self.MIN_HUD_INSET_TOP, self.MAX_HUD_INSET_TOP)
                end

                local loadedScale = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudUiScale", "vrSettings.hudUiScale#value",
                    "modDesc.vrSettings.hudUiScale", "modDesc.vrSettings.hudUiScale#value")
                if loadedScale ~= nil then scale = loadedScale end

                local loadedDepth = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudConvergenceDepth", "vrSettings.hudConvergenceDepth#value",
                    "modDesc.vrSettings.hudConvergenceDepth", "modDesc.vrSettings.hudConvergenceDepth#value")
                if loadedDepth ~= nil then
                    self.hudConvergenceDepth = loadedDepth
                else
                    self.hudConvergenceDepth = self.DEFAULT_CONVERGENCE_DEPTH
                end

                local loadedFillUnder = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile,
                    "vrSettings.fillLevelUnderVehicle", "vrSettings.fillLevelUnderVehicle#value",
                    "modDesc.vrSettings.fillLevelUnderVehicle", "modDesc.vrSettings.fillLevelUnderVehicle#value")
                if loadedFillUnder ~= nil then
                    self.fillLevelUnderVehicle = loadedFillUnder
                end

                local loadedGap = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudFillLevelGap", "vrSettings.hudFillLevelGap#value",
                    "modDesc.vrSettings.hudFillLevelGap", "modDesc.vrSettings.hudFillLevelGap#value")
                if loadedGap ~= nil then
                    self.hudFillLevelGap = self:clamp(loadedGap, 0.0, 0.05)
                else
                    self.hudFillLevelGap = self.DEFAULT_HUD_FILL_LEVEL_GAP
                end

                local loadedShowNotification = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile,
                    "vrSettings.showHudLoadNotification", "vrSettings.showHudLoadNotification#value",
                    "vrSettings.showHudLoadAlert", "vrSettings.showHudLoadAlert#value",
                    "vrSettings.showLoadNotification", "vrSettings.showLoadNotification#value",
                    "modDesc.vrSettings.showHudLoadNotification", "modDesc.vrSettings.showHudLoadNotification#value",
                    "modDesc.vrSettings.showHudLoadAlert", "modDesc.vrSettings.showHudLoadAlert#value")
                if loadedShowNotification ~= nil then
                    self.showHudLoadNotification = loadedShowNotification
                end

                local loadedRecenterTracking = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile,
                    "vrSettings.recenterVrTrackingSpace", "vrSettings.recenterVrTrackingSpace#value",
                    "vrSettings.recenterTracking", "vrSettings.recenterTracking#value",
                    "modDesc.vrSettings.recenterVrTrackingSpace", "modDesc.vrSettings.recenterVrTrackingSpace#value",
                    "modDesc.vrSettings.recenterTracking", "modDesc.vrSettings.recenterTracking#value")
                if loadedRecenterTracking ~= nil then
                    self.recenterVrTrackingSpace = loadedRecenterTracking
                end

                local loadedSnapAngle = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.snapTurnAngle", "vrSettings.snapTurnAngle#value",
                    "modDesc.vrSettings.snapTurnAngle", "modDesc.vrSettings.snapTurnAngle#value")
                if loadedSnapAngle ~= nil and loadedSnapAngle > 0 then
                    self.snapTurnAngle = math.rad(loadedSnapAngle)
                end

                local loadedMode = readXmlString(getXmlStringFn, xmlFile,
                    "vrSettings.renderingMode", "vrSettings.renderingMode#value",
                    "renderingMode", "renderingMode#value",
                    "modDesc.vrSettings.renderingMode", "modDesc.vrSettings.renderingMode#value")
                if loadedMode ~= nil and loadedMode ~= "" then
                    self:setRenderingMode(loadedMode, false)
                end

                if type(deleteXmlFn) == "function" then
                    pcall(deleteXmlFn, xmlFile)
                end
                return true
            end
        end
        return false
    end

    if xmlPath ~= nil then
        readSettingsFromXml(xmlPath)
    else
        -- 1. Read base defaults from modDesc.xml
        local modDir = self.modDirectory or _G.g_currentModDirectory or ""
        if modDir ~= "" then
            readSettingsFromXml(modDir .. "modDesc.xml")
        end

        -- 2. Read user override from modSettings/FS25_VRCompanion/settings.xml if present
        local userPath = self:getUserSettingsPath()
        local loadedUser = false
        if userPath ~= nil then
            loadedUser = readSettingsFromXml(userPath)
            if loadedUser then
                print(string.format("[FS25_VR] Loaded user settings from: %s", tostring(userPath)))
            end
        end

        -- 3. If user settings file doesn't exist, create it with current values
        if not loadedUser and userPath ~= nil then
            self:saveConfiguration(userPath)
        end
    end

    if self.fillLevelUnderVehicle == nil then
        self.fillLevelUnderVehicle = self.DEFAULT_FILL_LEVEL_UNDER_VEHICLE
    end
    if self.showHudLoadNotification == nil then
        self.showHudLoadNotification = self.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION
    end
    if self.recenterVrTrackingSpace == nil then
        self.recenterVrTrackingSpace = self.DEFAULT_RECENTER_VR_TRACKING_SPACE
    end
    if self.snapTurnAngle == nil then
        self.snapTurnAngle = self.DEFAULT_SNAP_TURN_ANGLE
    end

    self:configureHudInsets(insetX, insetY, scale, insetTop)
    return self:getHudInsets()
end

-- Export to Config subsystem table
VRCompanion.Config.clamp = function(self, ...) return VRCompanion.clamp(VRCompanion, ...) end
VRCompanion.Config.getUserSettingsPath = function(self, ...) return VRCompanion.getUserSettingsPath(VRCompanion, ...) end
VRCompanion.Config.saveConfiguration = function(self, ...) return VRCompanion.saveConfiguration(VRCompanion, ...) end
VRCompanion.Config.loadConfiguration = function(self, ...) return VRCompanion.loadConfiguration(VRCompanion, ...) end

return VRCompanion.Config
