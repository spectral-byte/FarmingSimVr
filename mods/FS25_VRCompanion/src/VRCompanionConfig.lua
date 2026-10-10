-- ====================================================================
-- FS25 VR Companion Mod: Config Subsystem
-- Manages XML persistence and settings configuration
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.Config = VRCompanion.Config or {}

-- HUD Safe Area constants (the engine's global HUD anchors, ADR-0022)
VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL = 0.30
VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL = 0.22
VRCompanion.MIN_HUD_SAFE_AREA = 0.0
VRCompanion.MAX_HUD_SAFE_AREA_HORIZONTAL = 0.40
VRCompanion.MAX_HUD_SAFE_AREA_VERTICAL = 0.35

-- HUD Group Margin and scale constants. HUD_GROUP_NAMES is the one ordered list of groups:
-- layout, visibility, save/load and listing in messages all follow it.
VRCompanion.HUD_GROUP_NAMES = { "speedometer", "map", "info", "controls" }
-- Each group's default margins and scale: the layout a first install starts with, tuned in the headset
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

-- HUD Group visibility (CONTEXT.md, ADR-0022): which groups show in the Streamlined and the Full HUD.
VRCompanion.DEFAULT_HUD_GROUP_STREAMLINED = { map = false, speedometer = true, info = false, controls = false }
VRCompanion.DEFAULT_HUD_GROUP_FULL = { map = true, speedometer = true, info = true, controls = true }

-- Whether the Controls group shows its full list of controls (maximized) or is minimized, as the
-- player last chose with the game's help toggle (F1). Saved between sessions; starts maximized.
VRCompanion.DEFAULT_CONTROLS_EXPANDED = true
VRCompanion.controlsExpanded = VRCompanion.DEFAULT_CONTROLS_EXPANDED

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

-- Per-group HUD Group Margins and scale multipliers, by group name
local function newDefaultHudGroupLayouts()
    local layouts = {}
    for _, name in ipairs(VRCompanion.HUD_GROUP_NAMES) do
        local defaults = VRCompanion.DEFAULT_HUD_GROUP_LAYOUTS[name]
        layouts[name] = { marginX = defaults.marginX, marginY = defaults.marginY, scale = defaults.scale }
    end
    return layouts
end
VRCompanion.hudGroupLayout = newDefaultHudGroupLayouts()

-- Speedometer group stacking: the fixed gap between the speedometer and the fill levels, and between
-- fill level rows, at scale 1.0. It is multiplied by the group's scale and is not adjustable.
VRCompanion.HUD_FILL_LEVEL_GAP = 0.004

-- The HUD state (Hidden, Streamlined or Full) a map starts in: the one the player last chose, saved between
-- sessions. A first install starts Hidden (VRCompanion.HUD_STATE_HIDDEN).
VRCompanion.DEFAULT_HUD_STATE = 0
VRCompanion.persistedHudState = VRCompanion.DEFAULT_HUD_STATE

-- VR HUD Notifications
VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION = false
VRCompanion.showHudLoadNotification = VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION

-- Recenter VR Tracking Space Configuration
VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE = true
VRCompanion.recenterVrTrackingSpace = VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE

-- Oblique Frustum (ADR-0005): symmetric projection by default; per-eye asymmetric offsets are opt-in
VRCompanion.DEFAULT_ALLOW_OBLIQUE_PROJECTION = false
VRCompanion.allowObliqueProjection = VRCompanion.DEFAULT_ALLOW_OBLIQUE_PROJECTION

-- DLSS near plane per camera mode (vrDlssNearClipCab / Outside / OnFoot), in metres; 0 = auto, which the
-- Injector estimates from the engine's motion vectors. The renderer's near differs between a vehicle's
-- inside camera, its other cameras and the on-foot camera, so each can be fixed on its own. The cab and
-- on-foot defaults are the renderer's known nears; the outside cameras vary, so they default to auto.
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

-- A group's margins and scale clamped to their ranges; anything not given falls back to that group's default
function VRCompanion:sanitizeHudGroupLayout(groupName, marginX, marginY, scale)
    local defaults = self.DEFAULT_HUD_GROUP_LAYOUTS[groupName]
    return {
        marginX = self:clamp(marginX or defaults.marginX, self.MIN_HUD_GROUP_MARGIN, self.MAX_HUD_GROUP_MARGIN),
        marginY = self:clamp(marginY or defaults.marginY, self.MIN_HUD_GROUP_MARGIN, self.MAX_HUD_GROUP_MARGIN),
        scale = self:clamp(scale or defaults.scale, self.MIN_HUD_GROUP_SCALE, self.MAX_HUD_GROUP_SCALE)
    }
end

-- Settings-file key prefix of a group, e.g. "vrSettings.hudGroupMap"
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
        setXmlFloatFn(xmlFile, "vrSettings.hudConvergenceDepth", self.hudConvergenceDepth or self.DEFAULT_CONVERGENCE_DEPTH)
        if type(setXmlBoolFn) == "function" then
            setXmlBoolFn(xmlFile, "vrSettings.showHudLoadNotification", self.showHudLoadNotification == true)
            setXmlBoolFn(xmlFile, "vrSettings.recenterVrTrackingSpace", self.recenterVrTrackingSpace ~= false)
            setXmlBoolFn(xmlFile, "vrSettings.allowObliqueProjection", self.allowObliqueProjection == true)
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
        if type(setXmlStringFn) == "function" then
            -- Persist the mode last requested, not VRBridge's: while the compositor is active the bridge
            -- only queues a change until the next frame boundary, so it still reports the old mode here.
            local modeStr = (self.renderingMode == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS) and "DualPass" or "AFR"
            setXmlStringFn(xmlFile, "vrSettings.renderingMode", modeStr)
            setXmlStringFn(xmlFile, "vrSettings.hudState",
                self.HUD_STATE_NAMES[self.persistedHudState] or self.HUD_STATE_NAMES[self.DEFAULT_HUD_STATE])
        end
        setXmlFloatFn(xmlFile, "vrSettings.snapTurnAngle", self:getSnapTurnAngleDegrees())
        for mode, field in pairs(self.DLSS_NEAR_CLIP_FIELDS) do
            setXmlFloatFn(xmlFile, "vrSettings." .. field, self[field] or self.DEFAULT_DLSS_NEAR_CLIPS[mode])
        end

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
    local safeAreaHorizontal = self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL
    local safeAreaVertical = self.DEFAULT_HUD_SAFE_AREA_VERTICAL
    local groupLayouts = newDefaultHudGroupLayouts()
    local persistedHudState = self.DEFAULT_HUD_STATE
    local controlsExpanded = self.DEFAULT_CONTROLS_EXPANDED
    local renderingModeLoaded = false

    -- Group visibility starts from the defaults; the settings file overrides the keys it has
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
    local deleteXmlFn = delete or _G.delete

    local function readSettingsFromXml(filePath)
        if filePath ~= nil and type(loadXmlFn) == "function" and type(getXmlFloatFn) == "function" then
            local ok, xmlFile = pcall(loadXmlFn, "VRCompanionConfig", filePath)
            if ok and xmlFile ~= 0 and xmlFile ~= nil then
                -- The old hudInsetHorizontal, hudInsetVertical and hudInsetTop keys are ignored (ADR-0022)
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
                    self.hudConvergenceDepth = loadedDepth
                else
                    self.hudConvergenceDepth = self.DEFAULT_CONVERGENCE_DEPTH
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

                local loadedAllowOblique = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile,
                    "vrSettings.allowObliqueProjection")
                if loadedAllowOblique ~= nil then
                    self.allowObliqueProjection = loadedAllowOblique
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

                local loadedSnapAngle = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.snapTurnAngle")
                if loadedSnapAngle ~= nil and loadedSnapAngle > 0 then
                    self.snapTurnAngle = math.rad(loadedSnapAngle)
                end

                for mode, field in pairs(self.DLSS_NEAR_CLIP_FIELDS) do
                    local loadedNear = readXmlFloat(getXmlFloatFn, xmlFile, "vrSettings." .. field)
                    if loadedNear ~= nil and loadedNear >= 0 then
                        self[field] = loadedNear
                    else
                        self[field] = self.DEFAULT_DLSS_NEAR_CLIPS[mode]
                    end
                end

                -- The HUD state is saved by name; an unknown or missing one keeps the default
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

    -- Defaults are the DEFAULT_* constants; the user settings file is the only XML source.
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

        -- No saved Stereo Presentation Mode and none chosen this session: apply the default through
        -- setRenderingMode so it reaches VRBridge and is marked explicit (otherwise a parity-only
        -- bridge reads as AFR).
        if not renderingModeLoaded and not self.renderingModeExplicit then
            self:setRenderingMode(self.DEFAULT_RENDERING_MODE, false)
        end
    end

    if self.hudConvergenceDepth == nil then
        self.hudConvergenceDepth = self.DEFAULT_CONVERGENCE_DEPTH
    end
    if self.showHudLoadNotification == nil then
        self.showHudLoadNotification = self.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION
    end
    if self.recenterVrTrackingSpace == nil then
        self.recenterVrTrackingSpace = self.DEFAULT_RECENTER_VR_TRACKING_SPACE
    end
    if self.allowObliqueProjection == nil then
        self.allowObliqueProjection = self.DEFAULT_ALLOW_OBLIQUE_PROJECTION
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

    -- First run: create the user settings file from the values just applied
    if not loadedUser and userPath ~= nil then
        self:saveConfiguration(userPath)
    end

    return self:getHudSafeArea()
end

-- Export to Config subsystem table
VRCompanion.Config.clamp = function(self, ...) return VRCompanion.clamp(VRCompanion, ...) end
VRCompanion.Config.getUserSettingsPath = function(self, ...) return VRCompanion.getUserSettingsPath(VRCompanion, ...) end
VRCompanion.Config.saveConfiguration = function(self, ...) return VRCompanion.saveConfiguration(VRCompanion, ...) end
VRCompanion.Config.loadConfiguration = function(self, ...) return VRCompanion.loadConfiguration(VRCompanion, ...) end

return VRCompanion.Config
