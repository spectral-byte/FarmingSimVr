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

-- The grip counts as pressed (back / close) at 0.6 and as let go below 0.3.
VRCompanion.MENU_BACK_PRESS = 0.6
VRCompanion.MENU_BACK_RELEASE = 0.3

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

-- A failure inside menu code must never break the update, and is reported once rather than every
-- frame. Takes the results of a pcall.
local function reportFailureOnce(ok, err)
    if not ok and not state.reportedFailure then
        state.reportedFailure = true
        print("[FS25_VR] menu pointer: menu input failed: " .. tostring(err))
    end
end

-- One mouse event into the game's GUI.
local function sendMouse(x, y, isDown, isUp, button)
    local gui = g_gui or (_G and _G.g_gui)
    if gui == nil or type(gui.mouseEvent) ~= "function" then
        return
    end
    reportFailureOnce(pcall(gui.mouseEvent, gui, x, y, isDown, isUp, button))
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

-- ---- Back / close ---------------------------------------------------------------------------------

-- [hand] = { back = bool } for every tracked hand; nil for a hand that is not tracked. Edges are
-- tracked all the time, also with no menu showing, so a button already held when a menu opens (or a
-- hand appears, or Motion Controllers come back on) is not a press.
local hands = {}

local function backHeld(handState, wasHeld)
    if handState.buttonLower == true then
        return true
    end
    local grip = handState.grip or 0
    if wasHeld then
        return grip >= VRCompanion.MENU_BACK_RELEASE
    end
    return grip >= VRCompanion.MENU_BACK_PRESS
end

-- The game's own menu input for "back": the call the engine's input binding makes for MENU_BACK, so
-- screens, dialogs and the in-game menu react exactly as to the keyboard.
local function sendBack()
    local gui = g_gui or (_G and _G.g_gui)
    local actions = InputAction or (_G and _G.InputAction)
    if gui == nil or actions == nil or actions.MENU_BACK == nil then
        return
    end
    local method = gui.onMenuInput or gui.notifyControls
    if type(method) ~= "function" then
        return
    end
    reportFailureOnce(pcall(method, gui, actions.MENU_BACK, 1))
end

-- ---- Scroll ---------------------------------------------------------------------------------------

-- Thumbstick up/down is the mouse wheel. The game's list and slider elements do not read the wheel
-- from the mouse event: they poll the native mouse state, Input.isMouseButtonPressed(wheel button),
-- while they handle a mouse-down event under the cursor. A mod cannot move the real wheel, so for the
-- duration of ONE synchronous mouse event the poll answers "yes" for the wheel button being sent,
-- then the original function is put back (also when the menu code raises an error). Everything else
-- is the game's own handling: the event goes through g_gui:mouseEvent to whatever is under the cursor.
local function sendWheel(x, y, up)
    local button = mouseButton(up and "MOUSE_BUTTON_WHEEL_UP" or "MOUSE_BUTTON_WHEEL_DOWN", nil)
    local gui = g_gui or (_G and _G.g_gui)
    if button == nil or gui == nil or type(gui.mouseEvent) ~= "function" then
        return
    end
    local input = Input or (_G and _G.Input)
    local original = input ~= nil and input.isMouseButtonPressed or nil
    local patched, patchError = pcall(function()
        input.isMouseButtonPressed = function(pressedButton)
            if pressedButton == button then
                return true
            end
            if type(original) == "function" then
                return original(pressedButton)
            end
            return false
        end
    end)
    if not patched then
        reportFailureOnce(false, "the mouse wheel state of the game cannot be set, menus will not scroll: " .. tostring(patchError))
    end
    reportFailureOnce(pcall(gui.mouseEvent, gui, x, y, true, false, button))
    if patched then
        pcall(function() input.isMouseButtonPressed = original end)
    end
end

-- Where the scroll lands: the cursor the ray moved last while it controls the pointer, otherwise the
-- desk mouse's position when the game can tell it, otherwise where the ray last pointed.
local function scrollPosition(self)
    if not self:isPointerRayInControl() then
        local binding = g_inputBinding or (_G and _G.g_inputBinding)
        if binding ~= nil and type(binding.getMousePosition) == "function" then
            local ok, x, y = pcall(binding.getMousePosition, binding)
            if ok and type(x) == "number" and type(y) == "number" then
                return x, y
            end
        end
    end
    return state.lastX, state.lastY
end

-- A scroll step is one wheel notch. While the stick stays pushed it repeats at this pace.
VRCompanion.MENU_SCROLL_REPEAT_MS = 150
VRCompanion.MENU_SCROLL_ENGAGE = 0.7
VRCompanion.MENU_SCROLL_RELEASE = 0.4

local scroll = { dir = 0, nextMs = nil }

-- The vertical push of the thumbstick: 1 (up), -1 (down) or 0, with hysteresis.
local function stickScrollDirection(handState, engaged)
    local x, y = handState.thumbX or 0, handState.thumbY or 0
    local direction = (y > 0) and 1 or -1
    if math.abs(y) >= VRCompanion.MENU_SCROLL_ENGAGE and math.abs(y) > math.abs(x) then
        return direction
    end
    if engaged ~= 0 and math.abs(y) >= VRCompanion.MENU_SCROLL_RELEASE and direction == engaged then
        return engaged
    end
    return 0
end

-- Grip or the lower face button, on either hand, goes back once per press; the thumbstick up/down
-- scrolls. Both only while a menu shows and Motion Controllers are on.
local function updateMenuButtons(self)
    if self.motionControllersEnabled == false then
        hands = {}
        scroll.dir, scroll.nextMs = 0, nil
        return
    end
    local goBack = false
    local pushed, held = 0, 0
    for _, hand in ipairs({ self.MOTION_HAND_LEFT, self.MOTION_HAND_RIGHT }) do
        local handState = self:getHandState(hand)
        if handState.tracked ~= true then
            hands[hand] = nil
        else
            local previous = hands[hand]
            local current = {
                back = backHeld(handState, previous ~= nil and previous.back),
                stick = stickScrollDirection(handState, previous and previous.stick or 0),
            }
            hands[hand] = current
            if previous ~= nil and current.back and not previous.back then
                goBack = true
            end
            if held == 0 then
                held = current.stick
            end
            if previous ~= nil and pushed == 0 and current.stick ~= 0 and current.stick ~= previous.stick then
                pushed = current.stick
            end
        end
    end

    if self.isMenuVisible ~= true then
        scroll.dir, scroll.nextMs = 0, nil
        return
    end
    if goBack then
        sendBack()
    end

    -- A fresh push scrolls at once; a held stick repeats. A second hand joining a push that is
    -- already scrolling is not a fresh push.
    local now = self:getCurrentTimeMs()
    local step = 0
    if pushed ~= 0 and not (scroll.nextMs ~= nil and scroll.dir == pushed) then
        step = pushed
    elseif held ~= 0 and scroll.nextMs ~= nil and scroll.dir == held then
        if now >= scroll.nextMs then
            step = held
        end
    else
        scroll.nextMs = nil
    end
    if step ~= 0 then
        scroll.nextMs = now + self.MENU_SCROLL_REPEAT_MS
        local x, y = scrollPosition(self)
        if x ~= nil then
            sendWheel(x, y, step > 0)
        end
    end
    scroll.dir = (step ~= 0) and step or held
end

function VRCompanion:updateMenuPointer(dt)
    updateMenuButtons(self)
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

    local trigger = self:getHandState(hand).trigger or 0
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
    hands = {}
    scroll.dir, scroll.nextMs = 0, nil
end

VRCompanion.MenuPointer.updateMenuPointer = function(self, ...) return VRCompanion.updateMenuPointer(VRCompanion, ...) end
VRCompanion.MenuPointer.resetMenuPointer = function(self, ...) return VRCompanion.resetMenuPointer(VRCompanion, ...) end

return VRCompanion.MenuPointer
