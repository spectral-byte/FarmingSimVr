    StubInput = {}
    local actions = {}
    local actionOrder = {}
    local variant = "ok"
    local addCalls = 0
    local connected = {}

    local Binding = {}
    Binding.__index = Binding
    function Binding.new(deviceId, inputs, axisComponent, negative)
        local b = setmetatable({}, Binding)
        local names = {}
        for i, name in ipairs(inputs) do
            if string.sub(name, 1, 5) == "AXIS_" then name = (string.gsub(name, "[%+%-]$", "")) end
            names[i] = name
        end
        local isNegative = negative ~= nil
        local last = names[#names]
        if last ~= nil and string.sub(last, 1, 5) == "AXIS_" then
            names[#names] = last .. (isNegative and "-" or "+")
        end
        b.deviceId = deviceId
        b.inputString = table.concat(names, " ")
        b.inputComponent = isNegative and "-" or "+"
        b.axisComponent = axisComponent or "+"
        b.neutralInput = 0
        b.index = 1
        return b
    end
    function Binding:makeId()
        self.id = table.concat({ self.deviceId, self.inputString, self.axisComponent, tostring(self.neutralInput), tostring(self.index) }, "|")
    end

    local function actionNamed(name)
        local action = actions[name]
        if action == nil then
            action = { name = name, bindings = {}, activeBindings = {} }
            actions[name] = action
            table.insert(actionOrder, name)
        end
        return action
    end

    local function split(text, separator)
        local parts = {}
        for part in string.gmatch(text, "[^" .. separator .. "]+") do table.insert(parts, part) end
        return parts
    end

    local function put(name, deviceId, input, axisComponent, index)
        local negative = nil
        if string.match(input, "AXIS_[%w_]*%-$") then negative = true end
        local b = Binding.new(deviceId, split(input, " "), axisComponent or "+", negative)
        b.index = index
        b:makeId()
        local action = actionNamed(name)
        table.insert(action.bindings, b)
        table.insert(action.activeBindings, b)
    end

    local methods = {}
    methods.__index = methods
    function methods:getActionByName(name) return actions[name] end
    local function addBindingImpl(self, action, b)
        addCalls = addCalls + 1
        if variant == "raises" and addCalls >= 2 then error("InputBinding:addBinding: attempt to index nil") end
        for _, existing in ipairs(action.bindings) do
            if existing.deviceId == b.deviceId and existing.index == b.index then
                print("Warning: InputBinding:addBinding: occupied slot " .. tostring(b.index) .. " of " .. action.name)
                return false
            end
        end
        table.insert(action.bindings, b)
        return true
    end
    methods.addBinding = addBindingImpl
    function methods:getDeviceById(deviceId) if connected[deviceId] then return { id = deviceId } end return nil end
    function methods:getMissingDeviceById(deviceId) if not connected[deviceId] then return { id = deviceId } end return nil end
    function methods:deleteBinding(...) return true end
    local function resetActiveImpl(self)
        if variant == "resetRaises" then error("InputBinding:resetActiveActionBindings: attempt to index nil") end
        for _, name in ipairs(actionOrder) do
            local action = actions[name]
            action.activeBindings = {}
            for i, b in ipairs(action.bindings) do action.activeBindings[i] = b end
        end
    end
    methods.resetActiveActionBindings = resetActiveImpl
    function methods:saveToXMLFile()
        if variant == "saveRaises" then error("InputBinding:saveToXMLFile: attempt to call a nil value") end
        StubInput.saves = StubInput.saves + 1
        StubInput.saved = StubInput.dump(nil)
        return true
    end

    local function slotIndex(deviceId) return (tonumber(string.match(deviceId, "^(%d+)_")) or 0) + 1 end

    local function omitted(options, name, deviceId)
        for _, key in ipairs(options.omit or {}) do
            if key == name .. "@" .. deviceId then return true end
        end
        return false
    end

    local pendingOptions = {}
    local function isUnplugged(deviceId)
        for _, id in ipairs(pendingOptions.unplugged or {}) do
            if id == deviceId then return true end
        end
        return false
    end

    function StubInput.plug(deviceId) connected[deviceId] = true end

    function StubInput.addDevice(deviceId)
        connected[deviceId] = not isUnplugged(deviceId)
        local index = slotIndex(deviceId)
        local function pad(name, input, axisComponent)
            if not omitted(pendingOptions, name, deviceId) then put(name, deviceId, input, axisComponent, index) end
        end
        pad("JUMP", "BUTTON_2")
        pad("VR_RECENTER_VIEW", "BUTTON_11")
        pad("VR_HUD_TOGGLE", "BUTTON_10")
        pad("VR_SNAP_TURN_LEFT", "BUTTON_5")
        pad("VR_SNAP_TURN_RIGHT", "BUTTON_6")
        pad("VR_TOGGLE_VIEW", "BUTTON_9")
        pad("AXIS_LOOK_LEFTRIGHT_PLAYER", "AXIS_3+", "+")
        pad("AXIS_LOOK_LEFTRIGHT_PLAYER", "AXIS_3-", "-")
        pad("AXIS_LOOK_LEFTRIGHT_VEHICLE", "AXIS_3+", "+")
        pad("AXIS_LOOK_LEFTRIGHT_VEHICLE", "AXIS_3-", "-")
        pad("AXIS_LOOK_UPDOWN_PLAYER", "AXIS_4+", "+")
        pad("AXIS_LOOK_UPDOWN_PLAYER", "AXIS_4-", "-")
    end

    function StubInput.install(options)
        options = options or {}
        pendingOptions = options
        variant = options.variant or "ok"
        actions, actionOrder, addCalls, connected = {}, {}, 0, {}
        StubInput.saves, StubInput.saved = 0, nil
        _G.g_inputBinding = nil
        if variant == "missing" then return end

        _G.Binding = Binding
        local inputBinding = setmetatable({}, methods)
        methods.addBinding = (variant ~= "reshaped") and addBindingImpl or nil
        methods.resetActiveActionBindings = (variant ~= "noReset") and resetActiveImpl or nil
        _G.g_inputBinding = inputBinding

        if options.state ~= nil then
            for _, line in ipairs(split(options.state, "\n")) do
                local f = split(line, "|")
                put(f[1], f[2], f[3], f[4], tonumber(f[5]))
                if not isUnplugged(f[2]) then connected[f[2]] = true end
            end
        else
            for _, name in ipairs({ "VR_RECENTER_VIEW", "VR_HUD_TOGGLE", "VR_SNAP_TURN_LEFT", "VR_SNAP_TURN_RIGHT", "VR_TOGGLE_VIEW" }) do
                put(name, "KB_MOUSE_DEFAULT", "KEY_numPad5", "+", 1)
            end
            put("VR_SET_SEAT", "KB_MOUSE_DEFAULT", "KEY_rctrl KEY_numPad5", "+", 1)
            for _, deviceId in ipairs(options.devices or { "0_XINPUT_GAMEPAD", "3_XINPUT_GAMEPAD" }) do
                StubInput.addDevice(deviceId)
            end
            for _, extra in ipairs(options.extra or {}) do
                put(extra.action, extra.device, extra.input, extra.axisComponent, extra.index)
            end
        end
    end

    local function describe(b) return b.inputString .. "/" .. b.axisComponent .. "@" .. tostring(b.index) end

    local function listField(field, name, deviceId)
        local action = actions[name]
        local parts = {}
        for _, b in ipairs(action and action[field] or {}) do
            if b.deviceId == deviceId then table.insert(parts, describe(b)) end
        end
        table.sort(parts)
        return table.concat(parts, " ; ")
    end
    function StubInput.list(name, deviceId) return listField("bindings", name, deviceId) end
    function StubInput.listActive(name, deviceId) return listField("activeBindings", name, deviceId) end

    local function dumpField(field, deviceId)
        local lines = {}
        for _, name in ipairs(actionOrder) do
            local devices = {}
            for _, b in ipairs(actions[name][field]) do
                if deviceId == nil or b.deviceId == deviceId then devices[b.deviceId] = true end
            end
            local ids = {}
            for id in pairs(devices) do table.insert(ids, id) end
            table.sort(ids)
            for _, id in ipairs(ids) do table.insert(lines, name .. " " .. id .. ": " .. listField(field, name, id)) end
        end
        return table.concat(lines, "\n")
    end
    function StubInput.dump(deviceId) return dumpField("bindings", deviceId) end
    function StubInput.dumpActive(deviceId) return dumpField("activeBindings", deviceId) end

    function StubInput.get(name, deviceId)
        for _, b in ipairs(actions[name].bindings) do
            if b.deviceId == deviceId then return b end
        end
        return nil
    end

    function StubInput.add(name, deviceId, input, index) put(name, deviceId, input, "+", index) end

    function StubInput.change(name, deviceId, from, to)
        for _, b in ipairs(actions[name].bindings) do
            if b.deviceId == deviceId and b.inputString == from then
                b.inputString = to
                b:makeId()
                return
            end
        end
        error("no such binding: " .. name .. " " .. deviceId .. " " .. from)
    end

    function StubInput.serialize()
        local lines = {}
        for _, name in ipairs(actionOrder) do
            for _, b in ipairs(actions[name].bindings) do
                table.insert(lines, table.concat({ name, b.deviceId, b.inputString, b.axisComponent, tostring(b.index) }, "|"))
            end
        end
        return table.concat(lines, "\n")
    end

    StubInput.saves, StubInput.saved = 0, nil
