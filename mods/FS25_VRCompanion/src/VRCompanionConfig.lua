VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL = 0.30
VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL = 0.22
VRCompanion.MIN_HUD_SAFE_AREA = 0.0
VRCompanion.MAX_HUD_SAFE_AREA_HORIZONTAL = 0.40
VRCompanion.MAX_HUD_SAFE_AREA_VERTICAL = 0.35

VRCompanion.HUD_GROUP_NAMES = { "speedometer", "map", "info", "controls" }
VRCompanion.DEFAULT_HUD_GROUP_LAYOUTS = {
    speedometer = { marginX = 0.30, marginY = 0.20, scale = 0.82 },
    map = { marginX = 0.30, marginY = 0.22, scale = 0.82 },
    info = { marginX = 0.30, marginY = 0.30, scale = 0.82 },
    controls = { marginX = 0.30, marginY = 0.30, scale = 0.82 },
}
VRCompanion.MIN_HUD_GROUP_MARGIN = 0.0
VRCompanion.MAX_HUD_GROUP_MARGIN = 0.45
VRCompanion.MIN_HUD_GROUP_SCALE = 0.50
VRCompanion.MAX_HUD_GROUP_SCALE = 1.50

VRCompanion.DEFAULT_HUD_GROUP_STREAMLINED = { map = false, speedometer = true, info = false, controls = false }
VRCompanion.DEFAULT_HUD_GROUP_FULL = { map = true, speedometer = true, info = true, controls = true }

VRCompanion.DEFAULT_CONTROLS_EXPANDED = true
VRCompanion.controlsExpanded = VRCompanion.DEFAULT_CONTROLS_EXPANDED

VRCompanion.HUD_LAYOUT_DEPTH = 1.8
VRCompanion.DEFAULT_HUD_DEPTH = 2.5
VRCompanion.DEFAULT_CONVERGENCE_DISPARITY = 0.012
VRCompanion.DEFAULT_IPD = 0.064
VRCompanion.MIN_IPD = 0.040
VRCompanion.MAX_IPD = 0.085
VRCompanion.MIN_CONVERGENCE_DEPTH = 0.5
VRCompanion.MAX_CONVERGENCE_DEPTH = 5.0
VRCompanion.MIN_HUD_DEPTH = 1.0
VRCompanion.MAX_HUD_DEPTH = 5.0
VRCompanion.MIN_CONVERGENCE_DISPARITY = 0.0
VRCompanion.MAX_CONVERGENCE_DISPARITY = 0.040
VRCompanion.CONVERGENCE_PARALLAX_FACTOR = 0.3375
VRCompanion.CONVERGENCE_SMOOTH_SPEED = 12.0

local function newDefaultHudGroupLayouts()
    local layouts = {}
    for _, name in ipairs(VRCompanion.HUD_GROUP_NAMES) do
        local defaults = VRCompanion.DEFAULT_HUD_GROUP_LAYOUTS[name]
        layouts[name] = { marginX = defaults.marginX, marginY = defaults.marginY, scale = defaults.scale }
    end
    return layouts
end
VRCompanion.hudGroupLayout = newDefaultHudGroupLayouts()

VRCompanion.HUD_FILL_LEVEL_GAP = 0.004

VRCompanion.DEFAULT_HUD_STATE = 0
VRCompanion.persistedHudState = VRCompanion.DEFAULT_HUD_STATE

VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION = false
VRCompanion.showHudLoadNotification = VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION

VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE = true
VRCompanion.recenterVrTrackingSpace = VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE

VRCompanion.DLSS_NEAR_CLIP_CAB = "cab"
VRCompanion.DLSS_NEAR_CLIP_OUTSIDE = "outside"
VRCompanion.DLSS_NEAR_CLIP_ON_FOOT = "onFoot"
VRCompanion.DLSS_NEAR_CLIP_FIELDS = {
    cab = "dlssNearClipCab",
    outside = "dlssNearClipOutside",
    onFoot = "dlssNearClipOnFoot",
}
VRCompanion.DEFAULT_DLSS_NEAR_CLIP_CAB = 0.1
VRCompanion.DEFAULT_DLSS_NEAR_CLIP_OUTSIDE = 0
VRCompanion.DEFAULT_DLSS_NEAR_CLIP_ON_FOOT = 0.15
VRCompanion.DEFAULT_DLSS_NEAR_CLIPS = {
    cab = VRCompanion.DEFAULT_DLSS_NEAR_CLIP_CAB,
    outside = VRCompanion.DEFAULT_DLSS_NEAR_CLIP_OUTSIDE,
    onFoot = VRCompanion.DEFAULT_DLSS_NEAR_CLIP_ON_FOOT,
}
VRCompanion.dlssNearClipCab = VRCompanion.DEFAULT_DLSS_NEAR_CLIP_CAB
VRCompanion.dlssNearClipOutside = VRCompanion.DEFAULT_DLSS_NEAR_CLIP_OUTSIDE
VRCompanion.dlssNearClipOnFoot = VRCompanion.DEFAULT_DLSS_NEAR_CLIP_ON_FOOT

VRCompanion.RECENTER_MODES = {
    BOTH = "Both",
    VEHICLE_ONLY = "VehicleOnly"
}

function VRCompanion.isFiniteNumber(value)
    return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
end

function VRCompanion.parseFiniteNumber(value, minVal, maxVal, integerOnly)
    local num = tonumber(value)
    if not VRCompanion.isFiniteNumber(num) then
        return nil
    end
    if integerOnly and num ~= math.floor(num) then
        return nil
    end
    if minVal ~= nil and num < minVal then
        return nil
    end
    if maxVal ~= nil and num > maxVal then
        return nil
    end
    return num
end

local function readXmlFloat(getXmlFloatFn, xmlFile, ...)
    local keys = {...}
    for _, k in ipairs(keys) do
        local val = getXmlFloatFn(xmlFile, k)
        if VRCompanion.isFiniteNumber(val) then
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
    if not VRCompanion.isFiniteNumber(val) then return minVal end
    if minVal ~= nil and val < minVal then return minVal end
    if maxVal ~= nil and val > maxVal then return maxVal end
    return val
end

function VRCompanion:sanitizeHudGroupLayout(groupName, marginX, marginY, scale)
    local defaults = self.DEFAULT_HUD_GROUP_LAYOUTS[groupName]
    return {
        marginX = self:clamp(marginX or defaults.marginX, self.MIN_HUD_GROUP_MARGIN, self.MAX_HUD_GROUP_MARGIN),
        marginY = self:clamp(marginY or defaults.marginY, self.MIN_HUD_GROUP_MARGIN, self.MAX_HUD_GROUP_MARGIN),
        scale = self:clamp(scale or defaults.scale, self.MIN_HUD_GROUP_SCALE, self.MAX_HUD_GROUP_SCALE)
    }
end

local function hudGroupSettingsKey(groupName)
    return "vrSettings.hudGroup" .. string.upper(string.sub(groupName, 1, 1)) .. string.sub(groupName, 2)
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
    local setXmlIntFn = setXMLInt or _G.setXMLInt
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
        setXmlFloatFn(xmlFile, "vrSettings.hudSafeAreaHorizontal", self.hudSafeAreaHorizontal or self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL)
        setXmlFloatFn(xmlFile, "vrSettings.hudSafeAreaVertical", self.hudSafeAreaVertical or self.DEFAULT_HUD_SAFE_AREA_VERTICAL)
        for _, groupName in ipairs(self.HUD_GROUP_NAMES) do
            local current = (self.hudGroupLayout or {})[groupName] or {}
            local layout = self:sanitizeHudGroupLayout(groupName, current.marginX, current.marginY, current.scale)
            local key = hudGroupSettingsKey(groupName)
            setXmlFloatFn(xmlFile, key .. "MarginHorizontal", layout.marginX)
            setXmlFloatFn(xmlFile, key .. "MarginVertical", layout.marginY)
            setXmlFloatFn(xmlFile, key .. "Scale", layout.scale)
        end
        setXmlFloatFn(xmlFile, "vrSettings.hudConvergenceDepth", self.hudConvergenceDepth or self.DEFAULT_HUD_DEPTH)
        if type(setXmlBoolFn) == "function" then
            setXmlBoolFn(xmlFile, "vrSettings.showHudLoadNotification", self.showHudLoadNotification == true)
            setXmlBoolFn(xmlFile, "vrSettings.recenterVrTrackingSpace", self.recenterVrTrackingSpace ~= false)
            for _, groupName in ipairs(self.HUD_GROUP_NAMES) do
                local group = (self.HUD_GROUPS or {})[groupName] or {}
                local streamlined, full = group.streamlined, group.full
                if streamlined == nil then streamlined = self.DEFAULT_HUD_GROUP_STREAMLINED[groupName] end
                if full == nil then full = self.DEFAULT_HUD_GROUP_FULL[groupName] end
                setXmlBoolFn(xmlFile, "vrSettings.hudGroups." .. groupName .. ".showStreamlined", streamlined == true)
                setXmlBoolFn(xmlFile, "vrSettings.hudGroups." .. groupName .. ".showFull", full == true)
            end
            setXmlBoolFn(xmlFile, "vrSettings.hudGroups.controls.expanded", self.controlsExpanded ~= false)
        end
        self:runSubsystemSaveSettings(xmlFile, { setBool = setXmlBoolFn, setFloat = setXmlFloatFn, setString = setXmlStringFn, setInt = setXmlIntFn })
        if type(setXmlStringFn) == "function" then
            local modeStr = (self.renderingMode == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS) and "DualPass" or "AFR"
            setXmlStringFn(xmlFile, "vrSettings.renderingMode", modeStr)
            setXmlStringFn(xmlFile, "vrSettings.hudState",
                self.HUD_STATE_NAMES[self.persistedHudState] or self.HUD_STATE_NAMES[self.DEFAULT_HUD_STATE])
        end
        setXmlFloatFn(xmlFile, "vrSettings.snapTurnAngle", self:getSnapTurnAngleDegrees())
        for mode, field in pairs(self.DLSS_NEAR_CLIP_FIELDS) do
            setXmlFloatFn(xmlFile, "vrSettings." .. field, self[field] or self.DEFAULT_DLSS_NEAR_CLIPS[mode])
        end

        local saveOk, saveResult = pcall(saveXmlFn, xmlFile)
        if type(deleteXmlFn) == "function" then
            pcall(deleteXmlFn, xmlFile)
        end
        if not saveOk or saveResult == false then
            print(string.format("[FS25_VR] [Error] Failed to save VR HUD configuration to: %s (%s)",
                tostring(path), not saveOk and tostring(saveResult) or "saveXMLFile returned false"))
            return false
        end
        print(string.format("[FS25_VR] Saved VR HUD configuration to: %s", tostring(path)))
        return true
    end
    print(string.format("[FS25_VR] [Error] Failed to create VR configuration XML: %s (%s)",
        tostring(path), not ok and tostring(xmlFile) or "createXMLFile returned invalid handle"))
    return false
end

function VRCompanion:loadConfiguration(xmlPath)
    local safeAreaHorizontal = self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL
    local safeAreaVertical = self.DEFAULT_HUD_SAFE_AREA_VERTICAL
    local groupLayouts = newDefaultHudGroupLayouts()
    local persistedHudState = self.DEFAULT_HUD_STATE
    local controlsExpanded = self.DEFAULT_CONTROLS_EXPANDED
    local renderingModeLoaded = false

    for _, groupName in ipairs(self.HUD_GROUP_NAMES) do
        local group = (self.HUD_GROUPS or {})[groupName]
        if group ~= nil then
            group.streamlined = self.DEFAULT_HUD_GROUP_STREAMLINED[groupName]
            group.full = self.DEFAULT_HUD_GROUP_FULL[groupName]
        end
    end

    local loadXmlFn = loadXMLFile or _G.loadXMLFile
    local getXmlFloatFn = getXMLFloat or _G.getXMLFloat
    local getXmlBoolFn = getXMLBool or _G.getXMLBool
    local getXmlStringFn = getXMLString or _G.getXMLString
    local getXmlIntFn = getXMLInt or _G.getXMLInt
    local hasXmlPropertyFn = hasXMLProperty or _G.hasXMLProperty
    local deleteXmlFn = delete or _G.delete

    if type(loadXmlFn) == "function" then
        self.hudConvergenceDepth = self.DEFAULT_HUD_DEPTH
        self.showHudLoadNotification = self.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION
        self.recenterVrTrackingSpace = self.DEFAULT_RECENTER_VR_TRACKING_SPACE
        self.snapTurnAngle = self.DEFAULT_SNAP_TURN_ANGLE
        for mode, field in pairs(self.DLSS_NEAR_CLIP_FIELDS) do
            self[field] = self.DEFAULT_DLSS_NEAR_CLIPS[mode]
        end
        self:runSubsystemResetSettings()
    end

    local function readSettingsFromXml(filePath)
        if filePath ~= nil and type(loadXmlFn) == "function" and type(getXmlFloatFn) == "function" then
            local ok, xmlFile = pcall(loadXmlFn, "VRCompanionConfig", filePath)
            if ok and xmlFile ~= 0 and xmlFile ~= nil then
                local loadedSafeAreaHorizontal = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudSafeAreaHorizontal")
                if loadedSafeAreaHorizontal ~= nil then safeAreaHorizontal = loadedSafeAreaHorizontal end

                local loadedSafeAreaVertical = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudSafeAreaVertical")
                if loadedSafeAreaVertical ~= nil then safeAreaVertical = loadedSafeAreaVertical end

                for _, groupName in ipairs(self.HUD_GROUP_NAMES) do
                    local key = hudGroupSettingsKey(groupName)
                    groupLayouts[groupName] = self:sanitizeHudGroupLayout(groupName,
                        readXmlFloat(getXmlFloatFn, xmlFile, key .. "MarginHorizontal"),
                        readXmlFloat(getXmlFloatFn, xmlFile, key .. "MarginVertical"),
                        readXmlFloat(getXmlFloatFn, xmlFile, key .. "Scale"))
                end

                local loadedDepth = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudConvergenceDepth")
                if loadedDepth ~= nil then
                    self.hudConvergenceDepth = self:clamp(loadedDepth, self.MIN_HUD_DEPTH, self.MAX_HUD_DEPTH)
                end

                local loadedShowNotification = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile,
                    "vrSettings.showHudLoadNotification")
                if loadedShowNotification ~= nil then
                    self.showHudLoadNotification = loadedShowNotification
                end

                local loadedRecenterTracking = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile,
                    "vrSettings.recenterVrTrackingSpace")
                if loadedRecenterTracking ~= nil then
                    self.recenterVrTrackingSpace = loadedRecenterTracking
                end

                for _, groupName in ipairs(self.HUD_GROUP_NAMES) do
                    local group = (self.HUD_GROUPS or {})[groupName]
                    if group ~= nil then
                        local prefix = "vrSettings.hudGroups." .. groupName
                        local loadedStreamlined = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile, prefix .. ".showStreamlined")
                        local loadedFull = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile, prefix .. ".showFull")
                        if loadedStreamlined ~= nil then group.streamlined = loadedStreamlined end
                        if loadedFull ~= nil then group.full = loadedFull end
                    end
                end

                local loadedControlsExpanded = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile,
                    "vrSettings.hudGroups.controls.expanded")
                if loadedControlsExpanded ~= nil then controlsExpanded = loadedControlsExpanded end

                self:runSubsystemLoadSettings(xmlFile, { has = hasXmlPropertyFn, getBool = getXmlBoolFn, getFloat = getXmlFloatFn, getString = getXmlStringFn, getInt = getXmlIntFn })

                local loadedSnapAngle = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.snapTurnAngle")
                if loadedSnapAngle ~= nil and loadedSnapAngle > 0 and loadedSnapAngle <= 360 then
                    self.snapTurnAngle = math.rad(loadedSnapAngle)
                end

                for _, field in pairs(self.DLSS_NEAR_CLIP_FIELDS) do
                    local loadedNear = readXmlFloat(getXmlFloatFn, xmlFile, "vrSettings." .. field)
                    if loadedNear ~= nil and loadedNear >= 0 then
                        self[field] = loadedNear
                    end
                end

                local loadedHudState = readXmlString(getXmlStringFn, xmlFile, "vrSettings.hudState")
                if loadedHudState ~= nil then
                    for state, name in pairs(self.HUD_STATE_NAMES) do
                        if string.lower(name) == string.lower(loadedHudState) then
                            persistedHudState = state
                        end
                    end
                end

                local loadedMode = readXmlString(getXmlStringFn, xmlFile,
                    "vrSettings.renderingMode")
                if loadedMode ~= nil and loadedMode ~= "" then
                    self:setRenderingMode(loadedMode, false)
                    renderingModeLoaded = true
                end

                if type(deleteXmlFn) == "function" then
                    pcall(deleteXmlFn, xmlFile)
                end
                return true
            end
        end
        return false
    end

    local userPath = nil
    local loadedUser = false
    if xmlPath ~= nil then
        readSettingsFromXml(xmlPath)
    else
        userPath = self:getUserSettingsPath()
        if userPath ~= nil then
            loadedUser = readSettingsFromXml(userPath)
            if loadedUser then
                print(string.format("[FS25_VR] Loaded user settings from: %s", tostring(userPath)))
            end
        end

        if not renderingModeLoaded and not self.renderingModeExplicit then
            self:setRenderingMode(self.DEFAULT_RENDERING_MODE, false)
        end
    end

    if self.hudConvergenceDepth == nil then
        self.hudConvergenceDepth = self.DEFAULT_HUD_DEPTH
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
    for mode, field in pairs(self.DLSS_NEAR_CLIP_FIELDS) do
        if self[field] == nil then
            self[field] = self.DEFAULT_DLSS_NEAR_CLIPS[mode]
        end
    end
    self.hudGroupLayout = groupLayouts
    self.persistedHudState = persistedHudState
    self.controlsExpanded = controlsExpanded
    self:configureHudSafeArea(safeAreaHorizontal, safeAreaVertical)

    if not loadedUser and userPath ~= nil then
        self:saveConfiguration(userPath)
    end

    return self:getHudSafeArea()
end
