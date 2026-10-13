-- ====================================================================
-- FS25 VR Companion Mod: Motion Controller button mappings
--
-- Lets a player holding a Motion Controller trigger the existing VR actions without the keyboard.
-- The module reads the per-frame hand state of VRCompanion:getHandState and, on the edge of an
-- input, calls the same handlers the key actions call: thumbstick left/right -> Snap Turn,
-- thumbstick click -> Recenter Action, menu button -> HUD Toggle Action.
--
--   * Both hands behave the same, whichever one is the Pointer Hand. A hand that is not tracked
--     does nothing; a hand that becomes tracked while an input is held does not fire until the
--     input is released and pressed again.
--   * An input acts once per press (a button) or once per push (the thumbstick), like a key action
--     does, then the handler's own debounce applies (Snap Turn 250 ms, HUD toggle 300 ms). If both
--     hands press in the same frame the action runs once.
--   * Nothing happens while VRCompanion.motionControllersEnabled is false (vrMotionControllers off).
--   * Snap Turn stays off while a menu is showing (VRCompanion:snapTurn refuses).
--
-- Reuse for other tickets: processControllerButtons() is called once per update from
-- updateMotionControllers; the rising-edge bookkeeping is per hand in `edges`.
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.ControllerMap = VRCompanion.ControllerMap or {}

-- The thumbstick counts as pushed sideways at this deflection (and while the push is more sideways
-- than up or down), and as let go again below the release value; the gap between them keeps a
-- wobbling stick from turning the player twice.
VRCompanion.CONTROLLER_STICK_ENGAGE = 0.7
VRCompanion.CONTROLLER_STICK_RELEASE = 0.4

local HANDS = { 0, 1 }

-- Which input drives which action. Buttons are the booleans of the hand state.
VRCompanion.DEFAULT_CONTROLLER_MAP = {
    snapTurn = "thumbX",       -- left/right push of the thumbstick
    recenter = "thumbClick",
    hudToggle = "menu",
}
VRCompanion.controllerMap = {
    snapTurn = VRCompanion.DEFAULT_CONTROLLER_MAP.snapTurn,
    recenter = VRCompanion.DEFAULT_CONTROLLER_MAP.recenter,
    hudToggle = VRCompanion.DEFAULT_CONTROLLER_MAP.hudToggle,
}

-- [hand] = { stick = -1 | 0 | 1, down = { [button] = bool } } for every tracked hand; nil for a
-- hand that is not tracked.
local edges = {}

local function sign(value)
    if value > 0 then return 1 end
    if value < 0 then return -1 end
    return 0
end

-- The sideways push of the thumbstick: -1 (left), 0 (none) or 1 (right), with hysteresis.
local function stickDirection(handState, engaged)
    local x, y = handState.thumbX or 0, handState.thumbY or 0
    if math.abs(x) >= VRCompanion.CONTROLLER_STICK_ENGAGE and math.abs(x) > math.abs(y) then
        return sign(x)
    end
    if engaged ~= 0 and math.abs(x) >= VRCompanion.CONTROLLER_STICK_RELEASE and sign(x) == engaged then
        return engaged
    end
    return 0
end

function VRCompanion:processControllerButtons()
    if self.motionControllersEnabled == false then
        edges = {}
        return
    end
    local snapDirection = 0
    local pressed = {}
    for _, hand in ipairs(HANDS) do
        local handState = self:getHandState(hand)
        if handState.tracked ~= true then
            edges[hand] = nil
        else
            local previous = edges[hand]
            local current = { stick = 0, down = {} }
            edges[hand] = current
            local map = self.controllerMap
            if map.snapTurn == "thumbX" then
                current.stick = stickDirection(handState, previous and previous.stick or 0)
                if previous ~= nil and current.stick ~= 0 and current.stick ~= previous.stick and snapDirection == 0 then
                    snapDirection = current.stick
                end
            end
            for action, input in pairs(map) do
                if action ~= "snapTurn" and input ~= nil and input ~= false then
                    local down = handState[input] == true
                    current.down[input] = down
                    if down and previous ~= nil and previous.down[input] ~= true then
                        pressed[action] = true
                    end
                end
            end
        end
    end

    if snapDirection < 0 then
        self:onSnapTurnLeftAction()
    elseif snapDirection > 0 then
        self:onSnapTurnRightAction()
    end
    if pressed.recenter then
        self:onRecenterAction()
    end
    if pressed.hudToggle then
        self:onHudToggleAction()
    end
end

-- Map unload: forget what was held.
function VRCompanion:resetControllerButtons()
    edges = {}
end

-- Export to ControllerMap subsystem table
VRCompanion.ControllerMap.processControllerButtons = function(self, ...) return VRCompanion.processControllerButtons(VRCompanion, ...) end

return VRCompanion.ControllerMap
