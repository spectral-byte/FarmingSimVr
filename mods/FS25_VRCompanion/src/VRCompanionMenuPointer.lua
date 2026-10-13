-- ====================================================================
-- FS25 VR Companion Mod: Menu Pointer
--
-- Lets the player point at a game menu, or at the 2D Floating Window Mode panel, with the Pointer
-- Hand: aiming moves the game's cursor, a trigger pull is a left click (ADR-0025). It works without
-- Interactive Control and does nothing unless the Pointer Ray has control of the pointer (see "Who
-- controls the pointer" in VRCompanionMotionControllers.lua).
--
-- How it works, per update (called from updateMotionControllers, once the hands were polled):
--   * The Injector owns the panel pose (ADR-0006, ADR-0008), so it reports where the Pointer Hand's
--     ray lands on the visible panel: VRBridge.getPanelHit(hand) -> {hit, u, v, distance}, with u
--     across from the left and v down from the top, both 0..1.
--   * The game's cursor is moved and clicked through the game's own GUI mouse handling,
--     g_gui:mouseEvent(posX, posY, isDown, isUp, button), exactly the call the engine makes for the
--     desk mouse (the Injector never synthesizes OS input). The GUI measures y up from the BOTTOM, so
--     the cursor is at (u, 1 - v).
--   * The drawn cursor (ADR-0009) is placed with VRBridge.setMenuCursor(true, u, v) while the ray has
--     control, and handed back to the desk mouse with VRBridge.setMenuCursor(false).
--   * The trigger is the left mouse button: down on the pull (only if the ray was on the panel), up on
--     the release, wherever the hand points then. Holding it while moving the hand drags. A pull
--     counts at 0.6 and the release at 0.3, so a wobbling trigger is one click.
--
-- Not here: scrolling and back/close (ticket #80), button mappings (ticket #75).
--
-- PUBLIC SURFACE
--   VRCompanion.MENU_TRIGGER_PRESS / VRCompanion.MENU_TRIGGER_RELEASE   trigger thresholds (0..1)
--   VRCompanion:updateMenuPointer(dt)   called by updateMotionControllers; idempotent per frame
--   VRCompanion:resetMenuPointer()      map unload: lets go of the button and the drawn cursor
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.MenuPointer = VRCompanion.MenuPointer or {}

VRCompanion.MENU_TRIGGER_PRESS = 0.6
VRCompanion.MENU_TRIGGER_RELEASE = 0.3

local DEFAULT_MOUSE_BUTTON_NONE = 0
local DEFAULT_MOUSE_BUTTON_LEFT = 1

local function newState()
    return {
        hit = nil,             -- table the bridge fills in place (adopted from its first reply)
        triggerHeld = false,   -- the trigger is past the press threshold (with hysteresis)
        clickDown = false,     -- a left button press was delivered and not yet released
        lastX = nil,           -- last position delivered to the GUI (GUI coordinates)
        lastY = nil,
        cursorShown = false,   -- the drawn cursor is placed by the ray
        reportedFailure = false,
    }
end

local state = newState()

local function bridgeFunction(name)
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge[name]) == "function" then
        return bridge[name]
    end
    return nil
end

local function mouseButton(name, default)
    local input = Input or (_G and _G.Input)
    if input ~= nil and type(input[name]) == "number" then
        return input[name]
    end
    return default
end

-- One mouse event into the game's GUI. A failure inside menu code must never break the update, and
-- is reported once rather than every frame.
local function sendMouse(x, y, isDown, isUp, button)
    local gui = g_gui or (_G and _G.g_gui)
    if gui == nil or type(gui.mouseEvent) ~= "function" then
        return
    end
    local ok, err = pcall(gui.mouseEvent, gui, x, y, isDown, isUp, button)
    if not ok and not state.reportedFailure then
        state.reportedFailure = true
        print("[FS25_VR] menu pointer: the game's GUI raised an error on a mouse event: " .. tostring(err))
    end
end

local function sendMove(x, y)
    sendMouse(x, y, false, false, mouseButton("MOUSE_BUTTON_NONE", DEFAULT_MOUSE_BUTTON_NONE))
end

-- Lets go of anything the ray was holding: a pressed button (released where it last was) and the
-- drawn cursor (given back to the desk mouse).
local function letGo()
    if state.clickDown then
        state.clickDown = false
        if state.lastX ~= nil then
            sendMouse(state.lastX, state.lastY, false, true, mouseButton("MOUSE_BUTTON_LEFT", DEFAULT_MOUSE_BUTTON_LEFT))
        end
    end
    if state.cursorShown then
        state.cursorShown = false
        local setMenuCursor = bridgeFunction("setMenuCursor")
        if setMenuCursor ~= nil then
            setMenuCursor(false)
        end
    end
end

-- While the ray does not point, a trigger that is already held must not turn into a click the moment
-- the ray takes over: remember the trigger as held.
local function followTriggerWhileIdle(self)
    local held = false
    for _, hand in ipairs({ self.MOTION_HAND_LEFT, self.MOTION_HAND_RIGHT }) do
        local pad = self:getHandState(hand)
        if pad ~= nil and (pad.trigger or 0) >= self.MENU_TRIGGER_RELEASE then
            held = true
        end
    end
    state.triggerHeld = held
end

function VRCompanion:updateMenuPointer(dt)
    local panelShowing = self.isMenuVisible == true or self.is2DViewActive == true
    local getPanelHit = bridgeFunction("getPanelHit")
    local hand = self:getPointerHand()
    if not (panelShowing and hand ~= nil and getPanelHit ~= nil and self:isPointerRayInControl()) then
        letGo()
        followTriggerWhileIdle(self)
        return
    end

    local result = getPanelHit(hand, state.hit)
    if type(result) == "table" then
        state.hit = result
    end
    local hit = state.hit or {}

    -- A pull that switched the Pointer Hand to this hand is not a click, even held on.
    local trigger = self:getHandState(hand).trigger or 0
    if self:isPointerTriggerConsumed() then trigger = 0 end
    local pulled, released = false, false
    if state.triggerHeld then
        if trigger < self.MENU_TRIGGER_RELEASE then
            state.triggerHeld = false
            released = true
        end
    elseif trigger >= self.MENU_TRIGGER_PRESS then
        state.triggerHeld = true
        pulled = true
    end

    local onPanel = hit.hit == true
    if onPanel then
        state.lastX, state.lastY = hit.u, 1 - hit.v
        sendMove(state.lastX, state.lastY)
        local setMenuCursor = bridgeFunction("setMenuCursor")
        if setMenuCursor ~= nil then
            setMenuCursor(true, hit.u, hit.v)
            state.cursorShown = true
        end
    end

    if pulled and onPanel and not state.clickDown then
        state.clickDown = true
        sendMouse(state.lastX, state.lastY, true, false, mouseButton("MOUSE_BUTTON_LEFT", DEFAULT_MOUSE_BUTTON_LEFT))
    elseif released and state.clickDown then
        state.clickDown = false
        sendMouse(state.lastX, state.lastY, false, true, mouseButton("MOUSE_BUTTON_LEFT", DEFAULT_MOUSE_BUTTON_LEFT))
    end
end

-- Map unload: let go of the button and the drawn cursor and forget everything.
function VRCompanion:resetMenuPointer()
    letGo()
    state = newState()
end

VRCompanion.MenuPointer.updateMenuPointer = function(self, ...) return VRCompanion.updateMenuPointer(VRCompanion, ...) end
VRCompanion.MenuPointer.resetMenuPointer = function(self, ...) return VRCompanion.resetMenuPointer(VRCompanion, ...) end

return VRCompanion.MenuPointer
