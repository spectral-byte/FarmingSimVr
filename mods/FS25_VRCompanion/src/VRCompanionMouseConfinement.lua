-- ====================================================================
-- FS25 VR Companion Mod: Mouse Confinement switch
--
-- The console command `vrMouseConfinement [on|off]` turns Mouse Confinement off and on (spec #214). While the headset is in use
-- and the game window has focus, the Injector keeps the Desk Mouse on the game picture so that a click the player cannot see
-- never lands on another program. The Companion Mod only carries the setting: it sets the VRBridge Mouse Confinement flag and
-- the Injector's DXGI proxy reads it at every present. The Companion Mod only runs once a savegame has loaded, so Mouse
-- Confinement always holds in the main menu before then (and again after the player leaves to the main menu, which resets the
-- VRBridge flag to on).
--
-- The setting is saved in settings.xml (on by default). When a savegame loads, Core's loadMap reads settings.xml and then calls
-- applyMouseConfinement, which sends the value to VRBridge after the handshake.
--
-- PUBLIC SURFACE
--   VRCompanion.mouseConfinement                          true (the default) or false
--   VRCompanion:setMouseConfinement(enabled, save)        sets the field, pushes it to VRBridge, saves settings.xml when `save`
--   VRCompanion:applyMouseConfinement()                   pushes the field to VRBridge (map load, after loadConfiguration)
--   VRCompanion:saveMouseConfinementSetting(xmlFile, xml) / loadMouseConfinementSetting(xmlFile, xml)   hooks of saveConfiguration / loadConfiguration
--   VRCompanion:consoleCommandSetMouseConfinement(arg)    nil reports the state, "on" / "off" sets and saves it, anything else returns the usage
--   VRCompanion:registerMouseConfinementConsoleCommands() / resetMouseConfinement()   map load / unload
--   Console: vrMouseConfinement [on|off]
-- ====================================================================

VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_MOUSE_CONFINEMENT = true
VRCompanion.mouseConfinement = VRCompanion.DEFAULT_MOUSE_CONFINEMENT

local SETTING_KEY = "vrSettings.mouseConfinement"
local USAGE = "Usage: vrMouseConfinement [on|off] (saved, on by default)"

local function describe(enabled)
    return enabled and "on" or "off"
end

-- Send the field to the Injector. Called on map load after the handshake and whenever the command changes the field.
function VRCompanion:applyMouseConfinement()
    if type(VRBridge) == "table" and type(VRBridge.setMouseConfinement) == "function" then
        VRBridge.setMouseConfinement(self.mouseConfinement ~= false)
    end
end

function VRCompanion:setMouseConfinement(enabled, save)
    self.mouseConfinement = (enabled ~= false)
    self:applyMouseConfinement()
    if save and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
end

function VRCompanion:saveMouseConfinementSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY, self.mouseConfinement ~= false)
    end
end

-- A key that is missing keeps the current value (on by default). Sending it to VRBridge is applyMouseConfinement's job.
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

function VRCompanion:registerMouseConfinementConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrMouseConfinement", "Keep the Desk Mouse on the game picture while the headset is in use (saved; on by default): vrMouseConfinement [on|off]", "consoleCommandSetMouseConfinement", self)
end

-- Map unload: the command goes. The saved setting stays as it is, and the next map load reads it again (the Injector's VRBridge
-- flag is reset by VRBridge.onMapUnload).
function VRCompanion:resetMouseConfinement()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrMouseConfinement")
    end
end
