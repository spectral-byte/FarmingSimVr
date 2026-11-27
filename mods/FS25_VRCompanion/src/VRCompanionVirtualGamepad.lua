VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_ENABLED = true
VRCompanion.virtualGamepadEnabled = VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_ENABLED

VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_DEFAULTS_WRITTEN = false
VRCompanion.virtualGamepadDefaultsWritten = VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_DEFAULTS_WRITTEN

VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_RECENTER_MIGRATED = false
VRCompanion.virtualGamepadRecenterMigrated = VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_RECENTER_MIGRATED

local SETTING_KEY_ENABLED = "vrSettings.virtualGamepad"
local SETTING_KEY_DEFAULTS_WRITTEN = "vrSettings.virtualGamepadDefaultsWritten"
local SETTING_KEY_RECENTER_MIGRATED = "vrSettings.virtualGamepadRecenterMigrated"

local INPUTS = { "trigger", "grip", "stick", "stickClick", "lower", "upper", "menu" }
local INPUT_BITS = { trigger = 1, grip = 2, stick = 4, stickClick = 8, lower = 16, upper = 32, menu = 64 }
local INPUT_NAMES = {
    trigger = "trigger", grip = "grip", stick = "thumbstick", stickClick = "thumbstick click",
    lower = "lower button", upper = "upper button", menu = "menu button",
}
local HANDS = { 0, 1 }
local HAND_NAMES = { [0] = "left", [1] = "right" }

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
            -- began, so it keeps its binding. suppressed: reads as released on the pad. heldAtDialog: held when a Stereo
            -- Dialog opened, so it is not the dialog's and reads released until it is let go.
            uses[hand][input] = { calledAt = -1, down = false, yielding = false, suppressed = false, heldAtDialog = false }
        end
    end
    return uses
end

local function newState()
    return {
        sentEnabled = nil,
        sentLive = nil,
        seenKey = nil,       -- the last (present, slot, reason) the game log was told about
        update = 0,
        dialogShowing = false,
        uses = newUses(),
        sentUse = { [0] = 0, [1] = 0 },
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
        if state.seenKey ~= nil or present or reason ~= "off" then
            print("[FS25_VR] Virtual Gamepad: " .. describe(present, slot, reason))
        end
        state.seenKey = key
    end

    self:updateVirtualGamepadDefaults(dt, present, slot)
end

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

function VRCompanion:takeVirtualGamepadInput(hand, input)
    local record = state.uses[hand] and state.uses[hand][input]
    if record == nil then return true end
    if record.calledAt == state.update then return not record.yielding end

    local wasDown = record.down
    local down = isHeld(self, hand, input, wasDown)
    record.down = down
    local continuing = record.calledAt == state.update - 1 or record.suppressed
    if not continuing then record.yielding = wasDown end
    record.calledAt = state.update
    if record.yielding and not down then record.yielding = false end
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

function VRCompanion:updateVirtualGamepadUse(dt)
    local dialogShowing = self:doesMenuTakeInput() and self:isStereoDialogShowing()
    local dialogOpening = dialogShowing and not state.dialogShowing
    state.dialogShowing = dialogShowing

    if self.motionControllersEnabled ~= false then
        if self:doesMenuTakeInput() and not self:isStereoDialogShowing() and not isSettingsPageShowing() then
            for _, hand in ipairs(HANDS) do
                for _, input in ipairs(INPUTS) do
                    if not (hand == 1 and input == "menu") then
                        self:takeVirtualGamepadInput(hand, input)
                    end
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
                record.heldAtDialog = false
            else
                record.down = isHeld(self, hand, input, record.down)
                if dialogOpening then record.heldAtDialog = record.down end
                record.suppressed = record.suppressed and record.down
                record.yielding = false
                if record.heldAtDialog then
                    record.heldAtDialog = record.down
                    if record.down then record.suppressed = true end
                end
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

local VR_ACTIONS = { "VR_SNAP_TURN_LEFT", "VR_SNAP_TURN_RIGHT", "VR_RECENTER_VIEW", "VR_TOGGLE_VIEW", "VR_HUD_TOGGLE" }

local TAKEN = {
    { action = "AXIS_LOOK_LEFTRIGHT_PLAYER", input = "AXIS_3-" },
    { action = "AXIS_LOOK_LEFTRIGHT_PLAYER", input = "AXIS_3+" },
    { action = "AXIS_LOOK_LEFTRIGHT_VEHICLE", input = "AXIS_3-" },
    { action = "AXIS_LOOK_LEFTRIGHT_VEHICLE", input = "AXIS_3+" },
    { action = "VR_SNAP_TURN_LEFT", input = "BUTTON_5" },
    { action = "VR_SNAP_TURN_RIGHT", input = "BUTTON_6" },
    { action = "VR_TOGGLE_VIEW", input = "BUTTON_9" },
}
local GIVEN = {
    { action = "VR_SNAP_TURN_LEFT", inputs = { "AXIS_3-" } },
    { action = "VR_SNAP_TURN_RIGHT", inputs = { "AXIS_3+" } },
    { action = "VR_TOGGLE_VIEW", inputs = { "BUTTON_11", "BUTTON_10" } },
}
local REWRITE_RULES = { taken = TAKEN, given = GIVEN, onlyIfTaken = false }

local MIGRATION_RULES = {
    taken = { { action = "VR_RECENTER_VIEW", input = "BUTTON_12" } },
    given = { { action = "VR_RECENTER_VIEW", inputs = { "BUTTON_11" } } },
    onlyIfTaken = true,
}

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

local function applyRewrite(inputBinding, bindingClass, deviceId, plan)
    local changes = 0
    for _, step in ipairs(plan) do
        for _, binding in ipairs(step.take) do
            removeFrom(step.action.bindings, binding)
            changes = changes + 1
        end
        for _, give in ipairs(step.give) do
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

    local changes = 0
    if not migrating or countChanges(plan) > 0 then
        local copies = snapshotPlan(plan)
        local applied
        applied, changes = pcall(applyRewrite, inputBinding, bindingClass, deviceId, plan)
        if not applied then
            restorePlan(plan, copies)
            if type(inputBinding.resetActiveActionBindings) == "function" then
                pcall(inputBinding.resetActiveActionBindings, inputBinding)
            end
            giveUpOnRewrite("the game's binding API raised: " .. tostring(changes), migrating)
            return
        end
    end

    self.virtualGamepadRecenterMigrated = true
    if migrating then
        if changes > 0 then
            print(string.format("[FS25_VR] Virtual Gamepad Recenter binding migrated on %s (RS to the left stick click, %d changes) and saved", deviceId, changes))
        end
    else
        self.virtualGamepadDefaultsWritten = true
        print(string.format("[FS25_VR] Virtual Gamepad default bindings written to %s (%d changes) and saved", deviceId, changes))
    end
    self:saveConfiguration()
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
    self:saveConfiguration()
    print(string.format("[FS25_VR] Virtual Gamepad set to: %s", enabled and "on" or "off"))
    return string.format("Virtual Gamepad set to: %s (Saved): %s", enabled and "on" or "off", self:getVirtualGamepadStatus())
end

function VRCompanion:resetVirtualGamepad()
    local b = bridge()
    if b ~= nil and type(b.setVirtualGamepad) == "function" and state.sentEnabled ~= nil then
        b.setVirtualGamepad(state.sentEnabled, false)
    end
    if b ~= nil and type(b.setVirtualGamepadUse) == "function" then
        b.setVirtualGamepadUse(0, 0)
    end
    state = newState()
end

VRCompanion:registerSubsystem("virtualGamepad", {
    mapUnload = function(self) self:resetVirtualGamepad() end,
    saveSettings = function(self, xmlFile, xml) self:saveVirtualGamepadSetting(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadVirtualGamepadSetting(xmlFile, xml) end,
    resetSettings = function(self)
        self.virtualGamepadEnabled = self.DEFAULT_VIRTUAL_GAMEPAD_ENABLED
        self.virtualGamepadDefaultsWritten = self.DEFAULT_VIRTUAL_GAMEPAD_DEFAULTS_WRITTEN
        self.virtualGamepadRecenterMigrated = self.DEFAULT_VIRTUAL_GAMEPAD_RECENTER_MIGRATED
    end,
    consoleCommands = {
        { name = "vrVirtualGamepad", description = "Turn the Virtual Gamepad (the Motion Controllers as an Xbox pad bound in the Controls screen) on or off (saved): vrVirtualGamepad [on|off]", method = "consoleCommandSetVirtualGamepad" },
    },
})
