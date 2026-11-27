VRCompanion = VRCompanion or {}

VRCompanion.MENU_BACK_PRESS = 0.6
VRCompanion.MENU_BACK_RELEASE = 0.3

local DEFAULT_MOUSE_BUTTON_NONE = 0
local DEFAULT_MOUSE_BUTTON_LEFT = 1

local function newState()
    return {
        hit = nil,             -- table the bridge fills in place (adopted from its first reply)
        clickDown = false,
        lastX = nil,
        lastY = nil,
        cursorShown = false,
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

local function reportFailureOnce(ok, err)
    if not ok and not state.reportedFailure then
        state.reportedFailure = true
        print("[FS25_VR] menu pointer: menu input failed: " .. tostring(err))
    end
end

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

VRCompanion.MENU_SCROLL_REPEAT_MS = 150
VRCompanion.MENU_SCROLL_ENGAGE = 0.7
VRCompanion.MENU_SCROLL_RELEASE = 0.4

local scroll = { dir = 0, nextMs = nil }

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

    if not self:doesMenuTakeInput() or self:isStereoDialogShowing() or self:isSettingsPageShowing() then
        scroll.dir, scroll.nextMs = 0, nil
        return
    end
    if goBack then
        sendBack()
    end

    local now = self:getCurrentTimeMs()
    local step = 0
    if pushed ~= 0 and not (scroll.nextMs ~= nil and scroll.dir == pushed) then
        step = pushed
    elseif held ~= 0 and scroll.nextMs ~= nil and scroll.dir == held then
        if now ~= nil and now >= scroll.nextMs then
            step = held
        end
    else
        scroll.nextMs = nil
    end
    if step ~= 0 then
        scroll.nextMs = now ~= nil and (now + self.MENU_SCROLL_REPEAT_MS) or nil
        local x, y = scrollPosition(self)
        if x ~= nil then
            sendWheel(x, y, step > 0)
        end
    end
    scroll.dir = (step ~= 0) and step or held
end

function VRCompanion:updateMenuPointer(dt)
    updateMenuButtons(self)
    local panelShowing = self:isMenuQuadShown() or self.is2DViewActive == true
    local getPanelHit = bridgeFunction("getPanelHit")
    local hand = self:getPointerHand()
    if not (panelShowing and hand ~= nil and getPanelHit ~= nil and self:isPointerRayInControl()) then
        letGo()
        return
    end

    local getPanelHitAlongRay = bridgeFunction("getPanelHitAlongRay")
    local ox, oy, oz, dx, dy, dz = nil, nil, nil, nil, nil, nil
    if getPanelHitAlongRay ~= nil then
        ox, oy, oz, dx, dy, dz = self:getPointerRayInTrackingSpace(hand)
    end
    local result
    if ox ~= nil then
        result = getPanelHitAlongRay(hand, ox, oy, oz, dx, dy, dz, state.hit)
    else
        result = getPanelHit(hand, state.hit)
    end
    if type(result) == "table" then
        state.hit = result
    end
    local hit = state.hit or {}

    local pulled, held = self:getPointerTrigger()

    local onPanel = hit.hit == true
    if onPanel then
        state.lastX, state.lastY = hit.u, 1 - hit.v
        sendMove(state.lastX, state.lastY)
        local setMenuCursor = bridgeFunction("setMenuCursor")
        if setMenuCursor ~= nil then
            setMenuCursor(true, hit.u, hit.v)
            state.cursorShown = true
        end
    elseif state.cursorShown then
        state.cursorShown = false
        local setMenuCursor = bridgeFunction("setMenuCursor")
        if setMenuCursor ~= nil then
            setMenuCursor(false)
        end
    end

    local mayClick = self:doesMenuTakeInput()
    if mayClick and pulled and onPanel and not state.clickDown then
        state.clickDown = true
        sendMouse(state.lastX, state.lastY, true, false, mouseButton("MOUSE_BUTTON_LEFT", DEFAULT_MOUSE_BUTTON_LEFT))
    elseif (not held or not mayClick) and state.clickDown then
        state.clickDown = false
        sendMouse(state.lastX, state.lastY, false, true, mouseButton("MOUSE_BUTTON_LEFT", DEFAULT_MOUSE_BUTTON_LEFT))
    end
end

function VRCompanion:resetMenuPointer()
    letGo()
    state = newState()
    hands = {}
    scroll.dir, scroll.nextMs = 0, nil
end

VRCompanion:registerSubsystem("menuPointer", {
    mapUnload = function(self) self:resetMenuPointer() end,
})
