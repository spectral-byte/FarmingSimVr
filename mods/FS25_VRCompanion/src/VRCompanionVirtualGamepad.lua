-- ====================================================================
-- FS25 VR Companion Mod: Virtual Gamepad Subsystem (ADR-0028)
--
-- The Injector presents the Motion Controllers' triggers, grips, thumbsticks and buttons to the game as an Xbox
-- pad it reads through XInput (the Virtual Gamepad, CONTEXT.md), so the player binds them in the game's own
-- Controls screen. The pad itself lives in the Injector; this module owns what only the Companion Mod knows:
-- the saved `vrVirtualGamepad` setting, the Motion Controller switch the pad follows, and the game-log and
-- status-command words about it.
--
-- PUBLIC SURFACE
--
--   VRBridge.setVirtualGamepad(enabled, live)       tell the Injector what is wanted (sent when it changes)
--     enabled  the saved `vrVirtualGamepad` setting
--     live     Motion Controller input is on (`vrMotionControllers`); the pad reads neutral while it is not
--   VRBridge.getVirtualGamepad() -> present, slot, reason, leftGripHeld, rightGripHeld   what the game sees: the XInput
--                                                    slot (0..3) while present, otherwise why not; and whether each hand's grip is
--                                                    held as the pad reads it (the Injector owns that threshold and its hysteresis)
--
--   VRBridge.setVirtualGamepadUse(leftInputs, rightInputs)   which inputs of each hand the Companion Mod is using itself
--                                                    (bit set, see INPUT_BITS below); sent every update
--
--   VRCompanion.virtualGamepadEnabled  boolean, default true, saved
--                                      (`vrVirtualGamepad on|off`).
--   VRCompanion:getVirtualGamepadStatus() -> string  one phrase for the status command
--
--   VRCompanion.virtualGamepadDefaultsWritten  boolean, default false, saved: the VR-comfort default bindings have been
--                                      written to the Virtual Gamepad, once and for good (see THE ONE-TIME DEFAULT REWRITE).
--   VRCompanion.virtualGamepadRecenterMigrated  boolean, default false, saved: the Recenter migration has run or was not needed
--                                      (see THE RECENTER MIGRATION).
--
-- THE MOD'S OWN USES (ticket #101, ADR-0028): while the Companion Mod is using a Motion Controller input itself, that input
-- reads as released on the pad, so a VR use never also fires a game binding. A use asks for an input every update it wants
-- it, and the answer is whether it may have it:
--   VRCompanion:takeVirtualGamepadInput(hand, input) -> boolean   input is one of "trigger", "grip", "stick",
--                                      "stickClick", "lower", "upper", "menu". False while the input is still held for its
--                                      binding (held when the use would have begun): the use must not act on it.
-- Uses made here, at the end of every update: every input of both hands while a menu shows (the Pointer Ray menus act on
-- the raw hand state, as they always did), and the grip of a hand holding the Steering Wheel. Interactive Control's click
-- asks for the Pointer Hand's trigger itself, as the ray lands on a Click Point, and Hand Tools ask for the trigger of the Tool
-- Hand while it holds a supported Hand Tool (VRCompanionHandTools.lua, ADR-0029), which calls the tool's use handler with it.
-- THE SETTINGS PAGE EXCEPTION: the player binds the pad in the Controls screen, which is part of the pause menu's Settings page,
-- and the game's binding capture needs the pad's inputs to arrive (a pad that reads neutral cannot be bound). So while the
-- pause menu shows its Settings page (g_gui.currentGuiName == "InGameMenu" and g_inGameMenu.currentPage.id == "pageSettings",
-- both measured in the game) the menu does NOT take the pad's inputs; every other menu and page keeps taking them. The
-- Controls pages are sub-pages of Settings and cannot be told apart from it, so the exception covers all of Settings.
-- (g_inputBinding.isInputCapturing does not turn true during binding, so it cannot be used instead.) THE TRADE-OFF: on the
-- Settings page the Pointer Ray menu still works, and a trigger click there also reaches the game as RT (likewise the
-- grip or lower face button that goes back, and the stick that scrolls); that only matters for a binding the player has
-- on those inputs. If g_gui, g_inGameMenu or the page is missing, the menu counts as any other menu. The held-input rule
-- holds on the way into and out of the Settings page: what the menu took stays off the pad until it is let go, and what was
-- held on the Settings page keeps its binding when another page's menu use begins.
-- THE HELD-INPUT RULE: an input already held (as the pad reads it) when a use begins keeps its binding until it is let go,
-- and the use does not act on it meanwhile. The same holds the other way round: an input a use took stays off the pad
-- until it is let go, even if the use ends first (a grip that closed a menu does not fire its binding as the menu goes).
--
-- THE ONE-TIME DEFAULT REWRITE (ticket #104, ADR-0028): the game's default gamepad bindings apply to every pad and cannot target
-- one device, so the first time the Virtual Gamepad is among the game's input devices this module edits that one device's
-- bindings, as the Controls screen would, and saves them (the calls are the ones spike #99 proved in the game:
-- g_inputBinding:getActionByName, action.bindings, Binding.new + addBinding, taking a binding out of action.bindings, and
-- saveToXMLFile; plus resetActiveActionBindings, see below). The layout (button numbers are the game's own for an Xbox pad: 5 / 6 LB / RB, 10 Start, 11 / 12 the
-- left / right stick click, AXIS_3 the right stick X):
--   - Snap Turn left / right on the right stick's left / right half (AXIS_3- / AXIS_3+), off LB / RB
--   - the game's smooth look left / right (AXIS_LOOK_LEFTRIGHT_PLAYER and _VEHICLE) off the right stick X
--   - the VR View Toggle Action on left stick click + Start, off Back (which the pad never sends)
-- The Recenter Action is NOT rewritten: it keeps the Companion Mod's own GAMEPAD_DEFAULT for the pad, the left stick click
-- (BUTTON_11). Measured in the game: while VR_RECENTER_VIEW carries an XInput BUTTON_12 binding the engine never fires the action
-- in the world at all, keyboard numpad 5 included (BUTTON_8 and BUTTON_11 do not do that). An earlier build put Recenter on RS; see
-- THE RECENTER MIGRATION below.
-- Two things measured in the headset: (1) Binding.new(deviceId, inputNames, axisComponent, negative) takes an axis input's sign
-- from the fourth argument alone and silently drops a "-" inside the name, so Snap Turn left is Binding.new(device, { "AXIS_3" },
-- "+", true) (inputString "AXIS_3-"); (2) taking a binding out of action.bindings only changes what the Controls screen shows, the
-- game acts on action.activeBindings until g_inputBinding:resetActiveActionBindings() rebuilds it, so the rewrite calls that
-- after its changes (before saving).
-- A new binding takes the `index` slot the binding it replaces had, otherwise the lowest one the device has free on that
-- action (addBinding refuses an occupied slot). Only bindings of the Virtual Gamepad's device are looked at; a real pad's are
-- never touched. The device is "<slot>_XINPUT_GAMEPAD" with the slot VRBridge reports. It counts as among the game's input
-- devices once the game has attached it as a connected device (g_inputBinding:getDeviceById(<device>) is a table; the spike
-- found it nil for a pad that is only saved, where getMissingDeviceById has the table) and has bindings for it on the Companion
-- Mod's own actions (the game applies GAMEPAD_DEFAULT to every pad it lists). The bindings alone are not enough: an unplugged
-- pad that once sat in the same slot keeps its saved bindings, and editing those would set the flag before the game has the
-- Virtual Gamepad. Until then the module looks again about once a second and says only that it is waiting. (Without
-- getDeviceById the bindings alone are the signal.)
-- THE SAVED FLAG is one global flag (the spec's single saved flag), not one per device. The pad takes its slot once, when it
-- appears, and a real pad plugged in later takes a lower one, so the device is normally the same session after session. If
-- the slot ever does change, the new device keeps the game's defaults: that is the lesser harm next to rewriting a player's
-- bindings a second time. Once set (after the bindings were saved) the rewrite never runs again, so what the player changes
-- in the Controls screen sticks.
-- THE RECENTER MIGRATION: a profile whose rewrite already ran in a build that put Recenter on RS has VR_RECENTER_VIEW on
-- BUTTON_12 of the Virtual Gamepad's device, which stops the engine firing the action. A second saved flag
-- (virtualGamepadRecenterMigrated, default false) makes the next session, once the device is listed as for the rewrite, take a
-- VR_RECENTER_VIEW binding of that device whose input is exactly BUTTON_12 out of action.bindings, add BUTTON_11 for that device
-- unless the action already has it there (same index rule as the rewrite), rebuild the active bindings, save, and set the flag.
-- Nothing is changed when there is no such binding (a Recenter the player moved elsewhere stays), and no other device is looked
-- at. A fresh profile's rewrite sets the flag too. The fallback is the rewrite's: one warning, bindings restored, no flag.
-- THE FALLBACK: the binding API missing (g_inputBinding, Binding.new, getActionByName, addBinding, resetActiveActionBindings or saveToXMLFile) or raising
-- is one warning in the game log for the session, no flag, and the bindings put back as they were. The pad then keeps the
-- game's defaults plus the Companion Mod's GAMEPAD_DEFAULT bindings, and the player rebinds in the Controls screen.
--
--   Hooks into the rest of the mod (each a single guarded call):
--     MotionControllers.lua  updateMotionControllers -> updateVirtualGamepad(dt) first of all, saveMotionControllerSettings
--                            / loadMotionControllerSettings -> save/loadVirtualGamepadSetting, getMotionControllerStatus ->
--                            getVirtualGamepadStatus, registerMotionControllerConsoleCommands and resetMotionControllers ->
--                            registerVirtualGamepadConsoleCommands / resetVirtualGamepad
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.VirtualGamepad = VRCompanion.VirtualGamepad or {}

-- vrVirtualGamepad on|off: saved. Off removes the pad from the game altogether.
VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_ENABLED = true
VRCompanion.virtualGamepadEnabled = VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_ENABLED

-- Set once the VR-comfort default bindings have been written to the Virtual Gamepad and saved. Saved: never rewrites again.
VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_DEFAULTS_WRITTEN = false
VRCompanion.virtualGamepadDefaultsWritten = VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_DEFAULTS_WRITTEN

-- Set once the Recenter migration (see THE RECENTER MIGRATION) has run, or was not needed: a fresh profile's rewrite sets it too.
-- Saved: the migration never runs again.
VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_RECENTER_MIGRATED = false
VRCompanion.virtualGamepadRecenterMigrated = VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_RECENTER_MIGRATED

local SETTING_KEY_ENABLED = "vrSettings.virtualGamepad"
local SETTING_KEY_DEFAULTS_WRITTEN = "vrSettings.virtualGamepadDefaultsWritten"
local SETTING_KEY_RECENTER_MIGRATED = "vrSettings.virtualGamepadRecenterMigrated"

-- The inputs of a hand a use can take, with the bit the Injector knows each by (VirtualGamepad::kInput*).
local INPUTS = { "trigger", "grip", "stick", "stickClick", "lower", "upper", "menu" }
local INPUT_BITS = { trigger = 1, grip = 2, stick = 4, stickClick = 8, lower = 16, upper = 32, menu = 64 }
local INPUT_NAMES = {
    trigger = "trigger", grip = "grip", stick = "thumbstick", stickClick = "thumbstick click",
    lower = "lower button", upper = "upper button", menu = "menu button",
}
local HANDS = { 0, 1 }
local HAND_NAMES = { [0] = "left", [1] = "right" }

-- When an input counts as held, as the pad reads it (pressed above the first number, let go below the second): the trigger
-- is analog, so any pull the game would notice counts; the thumbstick is held out of the game's dead zone. The grip is the
-- pad's digital LB / RB: the Injector decides when it is held, and tells through VRBridge.getVirtualGamepad.
local HELD_THRESHOLDS = {
    trigger = { 0.12, 0.08 },
    stick = { 0.25, 0.15 },
}

local function newUses()
    local uses = {}
    for _, hand in ipairs(HANDS) do
        uses[hand] = {}
        for _, input in ipairs(INPUTS) do
            -- calledAt: the last update a use asked for it. down: held, as of the last look. yielding: held when the use
            -- began, so it keeps its binding. suppressed: reads as released on the pad.
            uses[hand][input] = { calledAt = -1, down = false, yielding = false, suppressed = false }
        end
    end
    return uses
end

local function newState()
    return {
        sentEnabled = nil,   -- what the Injector was last told
        sentLive = nil,
        seenKey = nil,       -- the last (present, slot, reason) the game log was told about
        update = 0,          -- counts the Companion Mod's updates
        uses = newUses(),
        sentUse = { [0] = 0, [1] = 0 },  -- the sets of inputs the Injector was last told the Companion Mod is using
    }
end

local state = newState()

local function bridge()
    local b = VRBridge or (_G and _G.VRBridge)
    return b
end

local function describe(present, slot, reason)
    if present then
        return string.format("present in XInput slot %d", slot)
    end
    return "not present (" .. tostring(reason) .. ")"
end

-- Tells the Injector what is wanted, when it changed, and says in the game log what the game now sees.
function VRCompanion:updateVirtualGamepad(dt)
    state.update = state.update + 1
    local b = bridge()
    if b == nil or type(b.setVirtualGamepad) ~= "function" then return end

    local enabled = self.virtualGamepadEnabled == true
    local live = self.motionControllersEnabled ~= false
    if enabled ~= state.sentEnabled or live ~= state.sentLive then
        b.setVirtualGamepad(enabled, live)
        state.sentEnabled, state.sentLive = enabled, live
    end

    if type(b.getVirtualGamepad) ~= "function" then return end
    local present, slot, reason = b.getVirtualGamepad()
    local key = present and ("slot" .. tostring(slot)) or reason
    if key ~= state.seenKey then
        -- The first look at a pad that is simply off says nothing: a player who never turned it on reads no line.
        if state.seenKey ~= nil or present or reason ~= "off" then
            print("[FS25_VR] Virtual Gamepad: " .. describe(present, slot, reason))
        end
        state.seenKey = key
    end

    self:updateVirtualGamepadDefaults(dt, present, slot)
end

-- ---- The Companion Mod's own uses --------------------------------------------------------------------

-- Whether the input is held on the pad right now, with hysteresis against a trigger or grip that rests half-way.
local function isHeld(self, hand, input, wasDown)
    if input == "grip" then
        local b = bridge()
        if b == nil or type(b.getVirtualGamepad) ~= "function" then return false end
        local _, _, _, leftHeld, rightHeld = b.getVirtualGamepad()
        local held = leftHeld
        if hand ~= 0 then held = rightHeld end
        return held == true
    end
    local hands = self:getHandState(hand)
    if hands == nil or hands.tracked ~= true then return false end
    local thresholds = HELD_THRESHOLDS[input]
    if input == "stick" then
        local magnitude = math.max(math.abs(hands.thumbX or 0), math.abs(hands.thumbY or 0))
        return magnitude >= (wasDown and thresholds[2] or thresholds[1])
    elseif thresholds ~= nil then
        return (hands.trigger or 0) >= (wasDown and thresholds[2] or thresholds[1])
    elseif input == "stickClick" then
        return hands.thumbClick == true
    elseif input == "lower" then
        return hands.buttonLower == true
    elseif input == "upper" then
        return hands.buttonUpper == true
    end
    return hands.menu == true
end

-- A use of the Companion Mod asks for an input of a hand, every update it wants it. True: the use has it, and the input
-- reads as released on the pad from here on. False: the input was already held when the use would have begun, so it keeps
-- its binding until it is let go and the use must not act on it. Asking again in the same update gives the same answer.
function VRCompanion:takeVirtualGamepadInput(hand, input)
    local record = state.uses[hand] and state.uses[hand][input]
    if record == nil then return true end
    if record.calledAt == state.update then return not record.yielding end

    local wasDown = record.down  -- as of the end of the update before
    local down = isHeld(self, hand, input, wasDown)
    record.down = down
    -- The use goes on from the update before, or from one that took the input and still has it held (a ray that flickers
    -- off a Click Point for a frame does not hand a pulled trigger to the pad). Otherwise it begins now, and an input
    -- that was already held before this update is the binding's: the squeeze that starts the use is not.
    local continuing = record.calledAt == state.update - 1 or record.suppressed
    if not continuing then record.yielding = wasDown end
    record.calledAt = state.update
    if record.yielding and not down then record.yielding = false end  -- let go: the use may have it from now on
    return not record.yielding
end

local function describeUse()
    local names = {}
    for _, hand in ipairs(HANDS) do
        for _, input in ipairs(INPUTS) do
            if state.sentUse[hand] % (INPUT_BITS[input] * 2) >= INPUT_BITS[input] then
                names[#names + 1] = HAND_NAMES[hand] .. " " .. INPUT_NAMES[input]
            end
        end
    end
    return #names > 0 and table.concat(names, ", ") or "none"
end

-- Whether the pause menu's Settings page is the page showing (the Controls screen is a sub-page of it and cannot be told apart).
-- Measured in the game: while it shows, g_gui.currentGuiName is "InGameMenu" and g_inGameMenu.currentPage is the page object whose
-- id is "pageSettings" (it is also g_inGameMenu.pageSettings). Anything missing or raising counts as not showing.
local function isSettingsPageShowing()
    local ok, showing = pcall(function()
        local gui = g_gui or (_G and _G.g_gui)
        local menu = g_inGameMenu or (_G and _G.g_inGameMenu)
        if type(gui) ~= "table" or gui.currentGuiName ~= "InGameMenu" then return false end
        if type(menu) ~= "table" then return false end
        local page = menu.currentPage
        if type(page) ~= "table" then return false end
        return page.id == "pageSettings" or (menu.pageSettings ~= nil and page == menu.pageSettings)
    end)
    return ok and showing == true
end

-- Last of the Companion Mod's update: the uses made here, then which inputs are off the pad this update, told to the Injector.
function VRCompanion:updateVirtualGamepadUse(dt)
    if self.motionControllersEnabled ~= false then
        if self:doesMenuTakeInput() and not isSettingsPageShowing() then
            -- Every input of both hands: the Pointer Ray menus act on the raw hand state themselves.
            for _, hand in ipairs(HANDS) do
                for _, input in ipairs(INPUTS) do
                    if not (hand == 1 and input == "menu") then  -- the right menu button feeds the pad nothing
                        self:takeVirtualGamepadInput(hand, input)
                    end
                end
            end
        end
        if type(self.isHandHoldingSteeringWheel) == "function" then
            for _, hand in ipairs(HANDS) do
                if self:isHandHoldingSteeringWheel(hand) == true then
                    self:takeVirtualGamepadInput(hand, "grip")
                end
            end
        end
    end

    for _, hand in ipairs(HANDS) do
        local mask = 0
        for _, input in ipairs(INPUTS) do
            local record = state.uses[hand][input]
            if record.calledAt == state.update then
                record.suppressed = not record.yielding
            else
                record.down = isHeld(self, hand, input, record.down)
                -- No use wants it any more, but what a use took stays off the pad until it is let go.
                record.suppressed = record.suppressed and record.down
                record.yielding = false
            end
            if record.suppressed then mask = mask + INPUT_BITS[input] end
        end
        state.sentUse[hand] = mask
    end
    local b = bridge()
    if b ~= nil and type(b.setVirtualGamepadUse) == "function" then
        b.setVirtualGamepadUse(state.sentUse[0], state.sentUse[1])
    end
end

-- ---- The one-time default rewrite ---------------------------------------------------------------------

-- The VR actions: the game applies GAMEPAD_DEFAULT to every pad it lists, so a binding of one of these on the Virtual
-- Gamepad's device is how the Companion Mod knows the game has given the device its defaults.
local VR_ACTIONS = { "VR_SNAP_TURN_LEFT", "VR_SNAP_TURN_RIGHT", "VR_RECENTER_VIEW", "VR_TOGGLE_VIEW", "VR_HUD_TOGGLE" }

-- What the rewrite takes off the Virtual Gamepad's device (action, the binding's input string), and what it puts on.
local TAKEN = {
    { action = "AXIS_LOOK_LEFTRIGHT_PLAYER", input = "AXIS_3-" },
    { action = "AXIS_LOOK_LEFTRIGHT_PLAYER", input = "AXIS_3+" },
    { action = "AXIS_LOOK_LEFTRIGHT_VEHICLE", input = "AXIS_3-" },
    { action = "AXIS_LOOK_LEFTRIGHT_VEHICLE", input = "AXIS_3+" },
    { action = "VR_SNAP_TURN_LEFT", input = "BUTTON_5" },
    { action = "VR_SNAP_TURN_RIGHT", input = "BUTTON_6" },
    { action = "VR_TOGGLE_VIEW", input = "BUTTON_9" },
}
-- axisComponent is "+" on every binding the rewrite adds, Snap Turn's two stick halves included. The Snap Turn actions are
-- plain (axisType HALF) actions, and the game stores a stick half on such an action with axisComponent "+" whatever its
-- sign: the shipped templates and the player's saved inputBinding.xml give every button and key binding of these actions "+",
-- and a half-axis action a player bound to a stick half by hand in the Controls screen (AutoDrive's
-- ADSelectPreviousTargetUnload on "BUTTON_5 BUTTON_6 AXIS_3-", ADDebugChangeNeighbor on "AXIS_4-", ADSelectNextFillType on
-- "AXIS_2-") is saved with axisComponent="+". The sign-matching pairs (AXIS_3- with "-") belong to FULL-axis actions such as
-- AXIS_LOOK_LEFTRIGHT_PLAYER, which the rewrite takes off the pad rather than adds.
local GIVEN = {
    { action = "VR_SNAP_TURN_LEFT", inputs = { "AXIS_3-" } },
    { action = "VR_SNAP_TURN_RIGHT", inputs = { "AXIS_3+" } },
    { action = "VR_TOGGLE_VIEW", inputs = { "BUTTON_11", "BUTTON_10" } },
}
local REWRITE_RULES = { taken = TAKEN, given = GIVEN, onlyIfTaken = false }

-- THE RECENTER MIGRATION: an earlier build's rewrite put the Recenter Action on RS (BUTTON_12), and the engine then never fires
-- that action at all (measured in the game: keyboard numpad 5 too). For a profile whose rewrite already ran, the migration takes a
-- VR_RECENTER_VIEW binding that is exactly BUTTON_12 off the Virtual Gamepad's device and, only then, gives it the stick click
-- (BUTTON_11, the Companion Mod's own GAMEPAD_DEFAULT for the action) unless the device already has it on the action.
local MIGRATION_RULES = {
    taken = { { action = "VR_RECENTER_VIEW", input = "BUTTON_12" } },
    given = { { action = "VR_RECENTER_VIEW", inputs = { "BUTTON_11" } } },
    onlyIfTaken = true,
}

-- Binding.new(deviceId, inputNames, axisComponent, negative) takes the sign of an axis input from its fourth argument alone:
-- a "-" inside the name is dropped ({ "AXIS_3-" } comes out "AXIS_3+", measured in the game). So a GIVEN input such as
-- "AXIS_3-" is passed as the name "AXIS_3" and negative = true; a name without a "-" passes nil (any non-nil value makes the
-- input negative). A combination such as { "BUTTON_11", "BUTTON_10" } has no axis and passes as it is.
local function bindingArguments(inputs)
    local names, negative = {}, nil
    for i, text in ipairs(inputs) do
        local name, sign = string.match(text, "^(AXIS_.-)([%+%-])$")
        if name ~= nil then
            names[i] = name
            if sign == "-" then negative = true end
        else
            names[i] = text
        end
    end
    return names, negative
end

local RECHECK_MS = 1000

-- Why the rewrite will not be tried again this game session (the one warning has been logged), or nil.
local rewriteProblem = nil

local function inputBindingApi()
    local inputBinding = g_inputBinding or (_G and _G.g_inputBinding)
    local bindingClass = Binding or (_G and _G.Binding)
    if inputBinding == nil then return nil, "g_inputBinding is missing" end
    if bindingClass == nil or type(bindingClass.new) ~= "function" then return nil, "Binding.new is missing" end
    for _, method in ipairs({ "getActionByName", "addBinding", "resetActiveActionBindings", "saveToXMLFile" }) do
        if type(inputBinding[method]) ~= "function" then return nil, "g_inputBinding:" .. method .. " is missing" end
    end
    return inputBinding, bindingClass
end

-- Looks at the game's bindings for what the rewrite would change on `deviceId`: nil and a word when the game does not list
-- the device yet, otherwise the plan: for each action, the bindings of the device to take out and the ones to put in.
local function planRewrite(inputBinding, deviceId, rules)
    if type(inputBinding.getDeviceById) == "function" and inputBinding:getDeviceById(deviceId) == nil then
        return nil, "the game has not attached it as a connected device yet"
    end
    local byName, plan, listed = {}, {}, false
    local function entry(name)
        local step = byName[name]
        if step ~= nil then return step end
        local action = inputBinding:getActionByName(name)
        if action == nil or type(action.bindings) ~= "table" then return nil end
        step = { name = name, action = action, take = {}, give = {} }
        byName[name] = step
        plan[#plan + 1] = step
        return step
    end

    for _, name in ipairs(VR_ACTIONS) do
        local step = entry(name)
        if step == nil then return nil, "the game does not know the action " .. name end
        for _, binding in ipairs(step.action.bindings) do
            if binding.deviceId == deviceId then listed = true end
        end
    end
    if not listed then return nil, "no bindings of its yet" end

    for _, taken in ipairs(rules.taken) do
        local step = entry(taken.action)
        if step == nil then return nil, "the game does not know the action " .. taken.action end
        for _, binding in ipairs(step.action.bindings) do
            if binding.deviceId == deviceId and binding.inputString == taken.input then
                step.take[#step.take + 1] = binding
            end
        end
    end

    for _, given in ipairs(rules.given) do
        local step = entry(given.action)
        if step == nil then return nil, "the game does not know the action " .. given.action end
        local inputString = table.concat(given.inputs, " ")
        local present, used, firstFree = false, {}, 1
        local replaced = {}
        for _, binding in ipairs(step.action.bindings) do
            if binding.deviceId == deviceId then
                local leaving = false
                for _, taken in ipairs(step.take) do
                    if taken == binding then leaving = true end
                end
                if leaving then
                    replaced[#replaced + 1] = binding.index
                else
                    used[binding.index] = true
                    if binding.inputString == inputString then present = true end
                end
            end
        end
        if not present and (not rules.onlyIfTaken or #step.take > 0) then
            -- The slot of the binding it replaces if there is one, otherwise the lowest the device has free on the action.
            local index = replaced[1]
            if index == nil or used[index] then
                index = 1
                while used[index] do index = index + 1 end
            end
            local names, negative = bindingArguments(given.inputs)
            step.give[#step.give + 1] = { inputs = given.inputs, names = names, negative = negative, index = index }
        end
    end
    return plan
end

local function removeFrom(list, item)
    for i = #list, 1, -1 do
        if list[i] == item then table.remove(list, i) end
    end
end

-- Makes the plan's changes and saves. Raises on any failure, which the caller answers by putting the bindings back.
local function applyRewrite(inputBinding, bindingClass, deviceId, plan)
    local changes = 0
    for _, step in ipairs(plan) do
        for _, binding in ipairs(step.take) do
            removeFrom(step.action.bindings, binding)
            changes = changes + 1
        end
        for _, give in ipairs(step.give) do
            -- axisComponent "+" and, for the left half of the stick, negative (see GIVEN and bindingArguments).
            local binding = bindingClass.new(deviceId, give.names, "+", give.negative)
            binding.neutralInput = 0
            binding.index = give.index
            binding:makeId()
            if inputBinding:addBinding(step.action, binding) ~= true then
                error("addBinding refused " .. table.concat(give.inputs, " ") .. " for " .. step.name)
            end
            changes = changes + 1
        end
    end
    -- The Controls screen shows action.bindings, the game acts on action.activeBindings: a binding taken out of the former
    -- keeps acting (the smooth look went on turning the view) until the latter is rebuilt from it.
    inputBinding:resetActiveActionBindings()
    inputBinding:saveToXMLFile()
    return changes
end

local function snapshotPlan(plan)
    local copies = {}
    for i, step in ipairs(plan) do
        copies[i] = {}
        for j, binding in ipairs(step.action.bindings) do copies[i][j] = binding end
    end
    return copies
end

local function restorePlan(plan, copies)
    for i, step in ipairs(plan) do
        local bindings = step.action.bindings
        for j = #bindings, 1, -1 do bindings[j] = nil end
        for j, binding in ipairs(copies[i]) do bindings[j] = binding end
    end
end

local function countChanges(plan)
    local changes = 0
    for _, step in ipairs(plan) do changes = changes + #step.take + #step.give end
    return changes
end

local function giveUpOnRewrite(reason, migrating)
    rewriteProblem = reason
    if migrating then
        print("[FS25_VR] [Warning] Virtual Gamepad Recenter binding not migrated: " .. reason .. ". The Recenter Action may stay on "
            .. "the Virtual Gamepad's right stick click, where the game does not fire it; rebind Recenter in the Controls screen.")
    else
        print("[FS25_VR] [Warning] Virtual Gamepad default bindings not written: " .. reason .. ". The Virtual Gamepad keeps the "
            .. "game's default bindings plus the Companion Mod's gamepad defaults; rebind Snap Turn in the Controls screen.")
    end
end

-- Once the Virtual Gamepad is present: writes the VR-comfort default bindings to its device, once for good. A profile whose
-- rewrite already ran in an earlier build gets the Recenter migration instead (also once for good, see MIGRATION_RULES).
function VRCompanion:updateVirtualGamepadDefaults(dt, present, slot)
    local migrating = self.virtualGamepadDefaultsWritten == true
    if migrating and self.virtualGamepadRecenterMigrated == true then return end
    if rewriteProblem ~= nil then return end
    if not present or type(slot) ~= "number" then return end
    state.defaultsWait = (state.defaultsWait or 0) - (dt or 0)
    if state.defaultsWait > 0 then return end
    state.defaultsWait = RECHECK_MS

    local inputBinding, bindingClass = inputBindingApi()
    if inputBinding == nil then
        giveUpOnRewrite("the game's binding API is not there (" .. tostring(bindingClass) .. ")", migrating)
        return
    end

    local deviceId = string.format("%d_XINPUT_GAMEPAD", slot)
    local rules = migrating and MIGRATION_RULES or REWRITE_RULES
    local ok, plan, why = pcall(planRewrite, inputBinding, deviceId, rules)
    if not ok then
        giveUpOnRewrite("looking at the game's bindings raised: " .. tostring(plan), migrating)
        return
    end
    if plan == nil then
        if state.waitingFor ~= deviceId then
            state.waitingFor = deviceId
            print("[FS25_VR] Virtual Gamepad default bindings: waiting for the game to list " .. deviceId .. " (" .. tostring(why) .. ")")
        end
        return
    end

    -- A profile that needs no migration changes nothing and saves no bindings: it is only flagged.
    local changes = 0
    if not migrating or countChanges(plan) > 0 then
        local copies = snapshotPlan(plan)
        local applied
        applied, changes = pcall(applyRewrite, inputBinding, bindingClass, deviceId, plan)
        if not applied then
            restorePlan(plan, copies)
            -- What the game acts on goes back with them (it may have been rebuilt from the changed bindings before the failure).
            if type(inputBinding.resetActiveActionBindings) == "function" then
                pcall(inputBinding.resetActiveActionBindings, inputBinding)
            end
            giveUpOnRewrite("the game's binding API raised: " .. tostring(changes), migrating)
            return
        end
    end

    -- A fresh profile gets the layout without Recenter on RS, so it never needs the migration either.
    self.virtualGamepadRecenterMigrated = true
    if migrating then
        if changes > 0 then
            print(string.format("[FS25_VR] Virtual Gamepad Recenter binding migrated on %s (RS to the left stick click, %d changes) and saved", deviceId, changes))
        end
    else
        self.virtualGamepadDefaultsWritten = true
        print(string.format("[FS25_VR] Virtual Gamepad default bindings written to %s (%d changes) and saved", deviceId, changes))
    end
    if type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
end

function VRCompanion:getVirtualGamepadStatus()
    local b = bridge()
    if b == nil or type(b.getVirtualGamepad) ~= "function" then
        return "not available (no Injector, or an older one)"
    end
    local present, slot, reason = b.getVirtualGamepad()
    local text = describe(present, slot, reason)
    if present then
        text = text .. "; the Companion Mod is using: " .. describeUse()
        if self.virtualGamepadDefaultsWritten == true then
            text = text .. "; default bindings written"
            if self.virtualGamepadRecenterMigrated ~= true and rewriteProblem ~= nil then
                text = text .. "; Recenter binding not migrated (" .. rewriteProblem .. ")"
            end
        elseif rewriteProblem ~= nil then
            text = text .. "; default bindings not written (" .. rewriteProblem .. ")"
        else
            text = text .. "; default bindings not written yet"
        end
    end
    return text
end

function VRCompanion:saveVirtualGamepadSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY_ENABLED, self.virtualGamepadEnabled == true)
        xml.setBool(xmlFile, SETTING_KEY_DEFAULTS_WRITTEN, self.virtualGamepadDefaultsWritten == true)
        xml.setBool(xmlFile, SETTING_KEY_RECENTER_MIGRATED, self.virtualGamepadRecenterMigrated == true)
    end
end

function VRCompanion:loadVirtualGamepadSetting(xmlFile, xml)
    local loaded = nil
    if type(xml.getBool) == "function" then
        loaded = xml.getBool(xmlFile, SETTING_KEY_ENABLED)
    end
    if loaded == nil and type(xml.getString) == "function" then
        local str = xml.getString(xmlFile, SETTING_KEY_ENABLED)
        if str ~= nil then loaded = VRCompanion.parseOnOffArgument(str) end
    end
    if loaded ~= nil then
        self.virtualGamepadEnabled = loaded
    end
    if type(xml.getBool) == "function" then
        local written = xml.getBool(xmlFile, SETTING_KEY_DEFAULTS_WRITTEN)
        if written ~= nil then
            self.virtualGamepadDefaultsWritten = written
        end
        local migrated = xml.getBool(xmlFile, SETTING_KEY_RECENTER_MIGRATED)
        if migrated ~= nil then
            self.virtualGamepadRecenterMigrated = migrated
        end
    end
end

function VRCompanion:consoleCommandSetVirtualGamepad(arg)
    local usage = "Usage: vrVirtualGamepad <on|off> (saved)"
    if arg == nil or arg == "" or string.lower(tostring(arg)) == "status" then
        return string.format("Virtual Gamepad is %s: %s. %s",
            self.virtualGamepadEnabled and "on" or "off", self:getVirtualGamepadStatus(), usage)
    end
    local enabled = VRCompanion.parseOnOffArgument(arg)
    if enabled == nil then
        return usage
    end
    self.virtualGamepadEnabled = enabled
    self:updateVirtualGamepad(0)
    if type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
    print(string.format("[FS25_VR] Virtual Gamepad set to: %s", enabled and "on" or "off"))
    return string.format("Virtual Gamepad set to: %s (Saved): %s", enabled and "on" or "off", self:getVirtualGamepadStatus())
end

function VRCompanion:registerVirtualGamepadConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrVirtualGamepad", "Turn the Virtual Gamepad (the Motion Controllers as an Xbox pad bound in the Controls screen) on or off (saved): vrVirtualGamepad [on|off]", "consoleCommandSetVirtualGamepad", self)
end

-- Map unload: the pad stays in the game (the OpenXR session goes on) but reads neutral; the next map sends
-- everything again.
function VRCompanion:resetVirtualGamepad()
    local b = bridge()
    if b ~= nil and type(b.setVirtualGamepad) == "function" and state.sentEnabled ~= nil then
        b.setVirtualGamepad(state.sentEnabled, false)
    end
    if b ~= nil and type(b.setVirtualGamepadUse) == "function" then
        b.setVirtualGamepadUse(0, 0)
    end
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrVirtualGamepad")
    end
    state = newState()
end

-- Export to VirtualGamepad subsystem table
VRCompanion.VirtualGamepad.updateVirtualGamepad = function(self, ...) return VRCompanion.updateVirtualGamepad(VRCompanion, ...) end
VRCompanion.VirtualGamepad.updateVirtualGamepadUse = function(self, ...) return VRCompanion.updateVirtualGamepadUse(VRCompanion, ...) end
VRCompanion.VirtualGamepad.takeVirtualGamepadInput = function(self, ...) return VRCompanion.takeVirtualGamepadInput(VRCompanion, ...) end
VRCompanion.VirtualGamepad.getVirtualGamepadStatus =function(self, ...) return VRCompanion.getVirtualGamepadStatus(VRCompanion, ...) end
VRCompanion.VirtualGamepad.consoleCommandSetVirtualGamepad = function(self, ...) return VRCompanion.consoleCommandSetVirtualGamepad(VRCompanion, ...) end

return VRCompanion.VirtualGamepad
