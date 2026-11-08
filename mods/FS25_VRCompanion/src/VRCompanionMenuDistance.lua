-- ====================================================================
-- FS25 VR Companion Mod: Menu Distance
--
-- The console command `vrMenuDistance [1.0-5.0]` sets how far in front of the head, in metres, the menu quad is placed: the
-- panel that shows the game's menus and 2D Floating Window Mode (1.5 m by default). The panel keeps its size, so further away
-- it fills less of the view. The Companion Mod only carries the setting: it sends the metres to VRBridge, and the Injector
-- places the quad that far away. A quad already showing moves at once; every menu opened after that is placed at the new
-- distance.
--
-- The setting is saved in settings.xml (1.5 by default), like FOV Overscan. When a savegame loads, Core's loadMap reads
-- settings.xml and then calls applyMenuDistance, which sends the value to VRBridge after the handshake. The main menu is shown
-- before the Companion Mod runs, so the Injector reads the saved value from settings.xml itself when the game starts, and a
-- map unload leaves VRBridge's value as it is: the main menu after leaving a savegame keeps the distance too.
--
-- PUBLIC SURFACE
--   VRCompanion.menuDistance                          metres, 1.0 to 5.0 (1.5 by default)
--   VRCompanion:setMenuDistance(metres, save)         sets the field, pushes it to VRBridge and saves settings.xml when `save`;
--                                                     false (and nothing changes) when `metres` is not a number from 1.0 to 5.0
--   VRCompanion:applyMenuDistance()                   pushes the field to VRBridge (map load, after loadConfiguration and the handshake)
--   VRCompanion:saveMenuDistanceSetting(xmlFile, xml) / loadMenuDistanceSetting(xmlFile, xml)   hooks of saveConfiguration / loadConfiguration
--   VRCompanion:consoleCommandSetMenuDistance(arg)    nil reports the distance, a number from 1.0 to 5.0 sets and saves it,
--                                                     anything else returns the usage
--   VRCompanion:registerMenuDistanceConsoleCommands() / resetMenuDistance()   map load / unload
--   Console: vrMenuDistance [1.0-5.0]
-- ====================================================================

VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_MENU_DISTANCE = 1.5
VRCompanion.MIN_MENU_DISTANCE = 1.0
VRCompanion.MAX_MENU_DISTANCE = 5.0
VRCompanion.menuDistance = VRCompanion.DEFAULT_MENU_DISTANCE

local SETTING_KEY = "vrSettings.menuDistance"
local USAGE = "Usage: vrMenuDistance [1.0-5.0] (metres, saved, 1.5 by default)"

-- The metres an argument says, or nil when it is not a number from 1.0 to 5.0
local function parseArgument(arg)
    local metres = tonumber(arg)
    if metres == nil or metres ~= metres then return nil end
    if metres < VRCompanion.MIN_MENU_DISTANCE or metres > VRCompanion.MAX_MENU_DISTANCE then return nil end
    return metres
end

-- "2.5 m", "3 m": no trailing zeros
local function describe(metres)
    return string.format("%g m", metres)
end

-- Send the field to the Injector. Called on map load after the handshake and whenever the command changes the field.
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
    if save and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
    return true
end

function VRCompanion:saveMenuDistanceSetting(xmlFile, xml)
    if type(xml.setFloat) == "function" then
        xml.setFloat(xmlFile, SETTING_KEY, self.menuDistance or self.DEFAULT_MENU_DISTANCE)
    end
end

-- A key that is missing, or a value the command would refuse, keeps the current value (1.5 by default). Sending it to VRBridge
-- is applyMenuDistance's job.
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

function VRCompanion:registerMenuDistanceConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrMenuDistance", "How far in front of you menus, the main menu included, and 2D Floating Window Mode are shown, in metres (saved; 1.5 by default; further away they fill less of the view): vrMenuDistance [1.0-5.0]", "consoleCommandSetMenuDistance", self)
end

-- Map unload: the command goes. The saved setting stays as it is, and the next map load reads it again. VRBridge keeps its
-- value, so the main menu stays at the distance.
function VRCompanion:resetMenuDistance()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrMenuDistance")
    end
end
