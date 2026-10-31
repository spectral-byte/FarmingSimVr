-- ====================================================================
-- FS25 VR Companion Mod: FOV Overscan
--
-- The console command `vrFovOverscan [0-20]` widens the field of view the eyes are rendered with by that many percent beyond
-- what covers the headset's own FOV (spec #221), so a frame the VR runtime turns to follow the head still covers the display
-- and the black edges seen during head movement go away. The Companion Mod only carries the setting: it sends the percent to
-- VRBridge, and the Injector widens the eye projection it publishes (which this mod hands to the engine camera as it always
-- did) and submits to the headset, so the camera and the submitted frame widen together and the world keeps its true scale.
-- The Injector applies a new value when the next OpenXR frame begins, so both eyes of a pair share one FOV.
--
-- In this version the value lasts for the session: it survives leaving to the main menu and loading another savegame, but not
-- a restart of the game (saving it is a later step of spec #221). When a savegame loads, Core's loadMap calls
-- applyFovOverscan, which sends the value to VRBridge after the handshake.
--
-- PUBLIC SURFACE
--   VRCompanion.fovOverscan                          percent, 0 to 20 (0 by default)
--   VRCompanion:setFovOverscan(percent)              sets the field and pushes it to VRBridge; false (and nothing changes) when
--                                                    `percent` is not a number from 0 to 20
--   VRCompanion:applyFovOverscan()                   pushes the field to VRBridge (map load, after the handshake)
--   VRCompanion:consoleCommandSetFovOverscan(arg)    nil reports the percent and the rendered FOV, a number from 0 to 20 sets it,
--                                                    anything else returns the usage
--   VRCompanion:registerFovOverscanConsoleCommands() / resetFovOverscan()   map load / unload
--   Console: vrFovOverscan [0-20]
-- ====================================================================

VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_FOV_OVERSCAN = 0
VRCompanion.MIN_FOV_OVERSCAN = 0
VRCompanion.MAX_FOV_OVERSCAN = 20
VRCompanion.fovOverscan = VRCompanion.DEFAULT_FOV_OVERSCAN

local USAGE = "Usage: vrFovOverscan [0-20] (0 by default)"

-- The percent an argument says, or nil when it is not a number from 0 to 20
local function parseArgument(arg)
    local percent = tonumber(arg)
    if percent == nil or percent ~= percent then return nil end
    if percent < VRCompanion.MIN_FOV_OVERSCAN or percent > VRCompanion.MAX_FOV_OVERSCAN then return nil end
    return percent
end

-- "8", "7.5": no trailing zeros
local function describe(percent)
    return string.format("%g%%", percent)
end

-- The FOV the eyes are rendered with, as the Injector publishes it for the left eye; nil while there is no headset projection
local function describeRenderedFov()
    if type(VRBridge) ~= "table" or type(VRBridge.getEyeProjection) ~= "function" then return nil end
    local ok, projection = pcall(VRBridge.getEyeProjection, 0)
    if not ok or type(projection) ~= "table" or projection.isValid ~= true then return nil end
    if type(projection.fovY) ~= "number" or type(projection.fovX) ~= "number" then return nil end
    return string.format("Rendered FOV %.1f x %.1f degrees (vertical x horizontal).", math.deg(projection.fovY), math.deg(projection.fovX))
end

-- Send the field to the Injector. Called on map load after the handshake and whenever the command changes the field.
function VRCompanion:applyFovOverscan()
    if type(VRBridge) == "table" and type(VRBridge.setFovOverscan) == "function" then
        VRBridge.setFovOverscan(self.fovOverscan or self.DEFAULT_FOV_OVERSCAN)
    end
end

function VRCompanion:setFovOverscan(percent)
    percent = parseArgument(percent)
    if percent == nil then return false end
    self.fovOverscan = percent
    self:applyFovOverscan()
    return true
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
    if not self:setFovOverscan(arg) then
        return USAGE
    end
    print(string.format("[FS25_VR] FOV Overscan set to: %s", describe(self.fovOverscan)))
    return string.format("FOV Overscan set to: %s", describe(self.fovOverscan))
end

function VRCompanion:registerFovOverscanConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrFovOverscan", "Widen the field of view the eyes are rendered with, in percent, to hide black edges during head movement (0 by default; a higher value softens the picture): vrFovOverscan [0-20]", "consoleCommandSetFovOverscan", self)
end

-- Map unload: the command goes. The value stays for the next map load of this session (the Injector's VRBridge value is reset
-- by VRBridge.onMapUnload).
function VRCompanion:resetFovOverscan()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrFovOverscan")
    end
end
