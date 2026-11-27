VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_WHEEL_GRAB_ENABLED = true
VRCompanion.wheelGrabEnabled = VRCompanion.DEFAULT_WHEEL_GRAB_ENABLED

VRCompanion.WHEEL_GRAB_GRIP_PRESS = 0.6
VRCompanion.WHEEL_GRAB_GRIP_RELEASE = 0.4

VRCompanion.WHEEL_GRAB_RING_INNER = 0.10
VRCompanion.WHEEL_GRAB_RING_OUTER = 0.25
VRCompanion.WHEEL_GRAB_RING_HALF_THICKNESS = 0.15

VRCompanion.WHEEL_GRAB_PULL_AWAY_DISTANCE = 0.30

VRCompanion.WHEEL_GRAB_GRAB_PULSE_MS = 30
VRCompanion.WHEEL_GRAB_GRAB_PULSE_STRENGTH = 0.4
VRCompanion.WHEEL_GRAB_FULL_LOCK_PULSE_MS = 70
VRCompanion.WHEEL_GRAB_FULL_LOCK_PULSE_STRENGTH = 0.8

VRCompanion.WHEEL_GRAB_STEERING_FAILURES_BEFORE_GIVING_UP = 3

local HANDS = { 0, 1 }
local HAND_NAMES = { [0] = "left", [1] = "right" }

local function newState()
    return {
        hands = {
            [0] = { gripDown = false, pressed = false, holding = false, angle = 0, tracked = false },
            [1] = { gripDown = false, pressed = false, holding = false, angle = 0, tracked = false },
        },
        vehicle = nil,
        wheelNode = nil,
        direction = nil,
        target = 0,          -- the target angle of the Steering Wheel (radians, unwrapped) while held
        sending = false,
        reason = "not updated yet",
        reported = nil,
        scratch = nil,
        sent = nil,
        atFullLock = false,
        sendFailures = 0,
    }
end

local state = newState()

local engine = VRCompanion.getEngineGlobal

local function isNode(node)
    return node ~= nil and node ~= 0
end

local STEERING_FUNCTION = "setSteeringInput"

local function isHeldIn(vehicle)
    return vehicle ~= nil and state.vehicle == vehicle and (state.hands[0].holding or state.hands[1].holding)
        and not state.sending and VRCompanion.wheelGrabEnabled ~= false and VRCompanion.motionControllersEnabled ~= false
end

local function makeWrapper(original)
    return function(vehicle, inputValue, isAnalog, deviceCategory, ...)
        if isHeldIn(vehicle) then return end
        return original(vehicle, inputValue, isAnalog, deviceCategory, ...)
    end
end

local STEERING_PARAMETERS = 4  -- (vehicle, inputValue, isAnalog, deviceCategory)
local STEERING_DESCRIBE = "(vehicle, value, isAnalog, deviceCategory)"
local wraps = VRCompanion.GameWraps.newSet({
    owner = "wheelGrab",
    giveUp = "Wheel Grab disabled: %s. Steering from the keyboard, a gamepad or a Wheel Peripheral is unchanged.",
    functions = {
        [STEERING_FUNCTION] = {
            class = "Drivable", parameters = STEERING_PARAMETERS, describe = STEERING_DESCRIBE, make = makeWrapper,
            missingClass = "Drivable is missing",
        },
    },
})

local function wrapClass()
    wraps:wrapAll()
end

local function unwrapInstance()
    wraps:liftInstance()
end

local function wrapInstance(vehicle)
    wraps:wrapInstance(vehicle, { STEERING_FUNCTION })
end

local function steeringInputProblem(vehicle)
    if wraps:problem() ~= nil then return wraps:problem() end
    wrapClass()
    local problem = wraps:check()
    if problem ~= nil then return problem end
    local instanceProblem = VRCompanion.GameWraps.shapeProblem(vehicle[STEERING_FUNCTION], STEERING_PARAMETERS, STEERING_DESCRIBE)
    if instanceProblem ~= nil then
        problem = "the vehicle's " .. STEERING_FUNCTION .. ": " .. instanceProblem
        wraps:giveUp(problem)
    end
    return problem
end

wrapClass()

local function steeringWheelOf(vehicle)
    local direction = 1
    local wheel = vehicle.spec_drivable and vehicle.spec_drivable.steeringWheel or nil
    local reverse = vehicle.spec_reverseDriving
    if reverse ~= nil and reverse.isReverseDriving then
        direction = -1
        if reverse.steeringWheel ~= nil then wheel = reverse.steeringWheel end
    end
    local crab = vehicle.spec_crabSteering
    if crab ~= nil and crab.hasSteeringModes and crab.steeringModes ~= nil then
        local mode = crab.steeringModes[crab.state]
        if mode ~= nil and mode.steeringWheel ~= nil then wheel = mode.steeringWheel end
    end
    if wheel == nil or not isNode(wheel.node) then return nil end
    return wheel, direction
end

local function actualAngle(vehicle, wheel, direction)
    return (vehicle.rotatedTime or 0) * (wheel.indoorRotation or 0) * direction
end

local function angleLimits(vehicle, wheel, direction)
    local a = (vehicle.minRotTime or 0) * (wheel.indoorRotation or 0) * direction
    local b = (vehicle.maxRotTime or 0) * (wheel.indoorRotation or 0) * direction
    if a <= b then return a, b end
    return b, a
end

local function steeringValue(vehicle, wheel, direction, angle)
    local rotation = (wheel.indoorRotation or 0) * direction
    if rotation == 0 then return nil end
    local rotatedTime = angle / rotation
    local steeringDirection = 1
    if type(vehicle.getSteeringDirection) == "function" then
        local ok, answer = pcall(vehicle.getSteeringDirection, vehicle)
        if not ok or type(answer) ~= "number" then return nil end
        steeringDirection = answer
    end
    if math.abs(steeringDirection) < 0.05 then return nil end
    local span = rotatedTime >= 0 and (vehicle.maxRotTime or 0) or (vehicle.minRotTime or 0)
    if span == 0 then return nil end
    local value = rotatedTime / (span * steeringDirection)
    if rotatedTime >= 0 then value = -value end
    return math.max(-1, math.min(1, value))
end

local function askVehicle(vehicle, method)
    if type(vehicle[method]) ~= "function" then return nil end
    local ok, answer = pcall(vehicle[method], vehicle)
    if ok then return answer end
    return nil
end

local function findSteeringWheel(self)
    if self.wheelGrabEnabled == false then return nil, nil, nil, "Wheel Grab is off" end
    if self.motionControllersEnabled == false then return nil, nil, nil, "Motion Controllers are off" end
    if self:doesMenuTakeInput() then return nil, nil, nil, "a menu is open" end
    if self.is2DViewActive == true then return nil, nil, nil, "2D Floating Window Mode is on" end
    local vehicle = self:getControlledVehicle()
    if vehicle == nil then return nil, nil, nil, "not driving a vehicle" end
    local camera = self:getVehicleActiveCamera(vehicle)
    if camera == nil or camera.isInside ~= true or camera.isPassengerCamera == true then
        return vehicle, nil, nil, "not in the in-cab camera"
    end
    local wheel, direction = steeringWheelOf(vehicle)
    if wheel == nil then return vehicle, nil, nil, "the vehicle has no Steering Wheel" end
    if engine("InputDevice") == nil or engine("InputDevice").CATEGORY == nil or engine("InputDevice").CATEGORY.WHEEL == nil then
        return vehicle, nil, nil, "the vehicle takes no steering input"
    end
    local problem = steeringInputProblem(vehicle)
    if problem ~= nil then return vehicle, nil, nil, "steering input is not supported (" .. problem .. ")" end
    if askVehicle(vehicle, "getIsEntered") == false then
        return vehicle, nil, nil, "the vehicle is not entered"
    end
    if askVehicle(vehicle, "getIsAIActive") == true then
        return vehicle, nil, nil, "a hired helper is driving"
    end
    if askVehicle(vehicle, "getIsPlayerVehicleControlAllowed") == false then
        return vehicle, nil, nil, "steering the vehicle is not allowed right now"
    end
    if askVehicle(vehicle, "getIsPowered") == false then
        return vehicle, nil, nil, "the vehicle is not powered"
    end
    return vehicle, wheel, direction, nil
end

local function updateGrips(self)
    for _, hand in ipairs(HANDS) do
        local entry = state.hands[hand]
        local pose = self:getHandState(hand)
        local squeezed = false
        if pose.tracked == true then
            local threshold = entry.gripDown and self.WHEEL_GRAB_GRIP_RELEASE or self.WHEEL_GRAB_GRIP_PRESS
            squeezed = (pose.grip or 0) >= threshold
        end
        entry.pressed = squeezed and not entry.gripDown and entry.tracked
        entry.gripDown = squeezed
        entry.tracked = pose.tracked == true
    end
end

local MEASURE_FUNCTIONS = { "getParent", "worldToLocal", "getTranslation" }

local function measureHand(self, hand, wheel)
    local pose = self:getHandState(hand)
    if pose.tracked ~= true or pose.gripPoseTracked ~= true then return nil, "grip pose not tracked" end
    state.scratch = state.scratch or {}
    local ok, why = self:headRelativePoseToWorld(pose.gripPoseX, pose.gripPoseY, pose.gripPoseZ,
        pose.gripPoseQx, pose.gripPoseQy, pose.gripPoseQz, pose.gripPoseQw, state.scratch)
    if not ok then return nil, why end
    for _, name in ipairs(MEASURE_FUNCTIONS) do
        if type(engine(name)) ~= "function" then return nil, name .. " is not available" end
    end
    local parent = engine("getParent")(wheel.node)
    local x, y, z = engine("worldToLocal")(parent, state.scratch.x, state.scratch.y, state.scratch.z)
    local cx, cy, cz = engine("getTranslation")(wheel.node)
    local dx, dy, dz = x - cx, y - cy, z - cz
    return math.atan2(-dz, dx), math.sqrt(dx * dx + dz * dz), dy
end

local function ringDistance(self, radial, axial)
    local along = math.max(self.WHEEL_GRAB_RING_INNER - radial, radial - self.WHEEL_GRAB_RING_OUTER, 0)
    local across = math.max(math.abs(axial) - self.WHEEL_GRAB_RING_HALF_THICKNESS, 0)
    return math.sqrt(along * along + across * across)
end

local function wrapAngle(angle)
    return (angle + math.pi) % (2 * math.pi) - math.pi
end

local function releaseHand(hand)
    local entry = state.hands[hand]
    entry.holding = false
    entry.pin = nil
end

local function pinHand(hand, wheel)
    local entry = state.hands[hand]
    entry.pin = nil
    local worldToLocal, getWorldQuaternion = engine("worldToLocal"), engine("getWorldQuaternion")
    if type(worldToLocal) ~= "function" or type(getWorldQuaternion) ~= "function" then return end
    local world = state.scratch
    local ok, lx, ly, lz, nx, ny, nz, nw = pcall(function()
        local x, y, z = worldToLocal(wheel.node, world.x, world.y, world.z)
        local qx, qy, qz, qw = getWorldQuaternion(wheel.node)
        return x, y, z, qx, qy, qz, qw
    end)
    if not ok or type(lx) ~= "number" then return end
    local qx, qy, qz, qw = VRCompanion.multiplyQuaternions(-nx, -ny, -nz, nw, world.qx, world.qy, world.qz, world.qw)
    entry.pin = { node = wheel.node, x = lx, y = ly, z = lz, qx = qx, qy = qy, qz = qz, qw = qw }
end

local function pinInTheWorld(pin)
    local localToWorld, getWorldQuaternion = engine("localToWorld"), engine("getWorldQuaternion")
    if pin == nil or type(localToWorld) ~= "function" or type(getWorldQuaternion) ~= "function" then return nil end
    local ok, x, y, z, nx, ny, nz, nw = pcall(function()
        local px, py, pz = localToWorld(pin.node, pin.x, pin.y, pin.z)
        local qx, qy, qz, qw = getWorldQuaternion(pin.node)
        return px, py, pz, qx, qy, qz, qw
    end)
    if not ok or type(x) ~= "number" then return nil end
    local qx, qy, qz, qw = VRCompanion.multiplyQuaternions(nx, ny, nz, nw, pin.qx, pin.qy, pin.qz, pin.qw)
    return x, y, z, qx, qy, qz, qw
end

local function distanceToPin(hand)
    local x, y, z = pinInTheWorld(state.hands[hand].pin)
    if x == nil then return nil end
    local world = state.scratch
    local dx, dy, dz = world.x - x, world.y - y, world.z - z
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function anyHolding()
    return state.hands[0].holding or state.hands[1].holding
end

local function reportAvailability(reason)
    local text = reason ~= nil and ("unavailable (" .. reason .. ")") or "available"
    if text ~= state.reported then
        state.reported = text
        print("[FS25_VR] Wheel Grab: " .. text)
    end
    state.reason = reason
end

local function reportSqueeze(self, hand, inUse, angle, radial, axial)
    local outcome
    if not inUse then
        outcome = "not taken: the hand has not moved in the last few seconds, so it is not in use"
    elseif angle == nil then
        outcome = "not taken: the hand is not located (" .. tostring(radial) .. ")"
    else
        local distance = ringDistance(self, radial, axial)
        if distance == 0 then
            outcome = "taken"
        else
            outcome = string.format("not taken: %.2f m from the ring", distance)
        end
    end
    local where = ""
    if angle ~= nil then
        where = string.format(" (%.2f m from the axis, %.2f m from the plane)", radial, axial)
    end
    print(string.format("[FS25_VR] Wheel Grab: %s grip squeezed, Steering Wheel %s%s", HAND_NAMES[hand], outcome, where))
end

local function sendSteering(vehicle, value)
    state.sending = true
    local ok, failure = pcall(vehicle.setSteeringInput, vehicle, value, true, engine("InputDevice").CATEGORY.WHEEL)
    state.sending = false
    if ok then
        state.sendFailures = 0
        return false
    end
    state.sendFailures = state.sendFailures + 1
    if state.sendFailures < VRCompanion.WHEEL_GRAB_STEERING_FAILURES_BEFORE_GIVING_UP then return false end
    wraps:giveUp("setSteeringInput raised: " .. tostring(failure))
    return true
end

function VRCompanion:updateWheelGrab(dt)
    updateGrips(self)
    local vehicle, wheel, direction, reason = findSteeringWheel(self)
    reportAvailability(reason)
    if wheel == nil or (state.vehicle ~= nil and (state.vehicle ~= vehicle or state.wheelNode ~= wheel.node or state.direction ~= direction)) then
        for _, hand in ipairs(HANDS) do releaseHand(hand) end
    end
    if not anyHolding() then
        state.vehicle, state.wheelNode, state.direction, state.sent, state.atFullLock, state.sendFailures = nil, nil, nil, nil, false, 0
    end
    if wheel == nil then
        unwrapInstance()
        return
    end

    local low, high = angleLimits(vehicle, wheel, direction)
    local sum, count = 0, 0
    for _, hand in ipairs(HANDS) do
        local entry = state.hands[hand]
        if entry.holding and not entry.gripDown then releaseHand(hand) end
        if entry.holding then
            if not self:takeVirtualGamepadInput(hand, "grip") then
                releaseHand(hand)
            else
                local angle = measureHand(self, hand, wheel)
                if angle == nil then
                    releaseHand(hand)
                else
                    local away = distanceToPin(hand)
                    if away ~= nil and away > self.WHEEL_GRAB_PULL_AWAY_DISTANCE then
                        releaseHand(hand)
                    else
                        sum = sum + wrapAngle(angle - entry.angle)
                        count = count + 1
                        entry.angle = angle
                    end
                end
            end
        elseif entry.pressed then
            local inUse = self:isHandInUse(hand)
            local angle, radial, axial = measureHand(self, hand, wheel)
            local atRim = inUse and angle ~= nil and ringDistance(self, radial, axial) == 0
            local mayGrab = atRim and self:takeVirtualGamepadInput(hand, "grip") == true
            reportSqueeze(self, hand, inUse, angle, radial, axial)
            if mayGrab then
                if not anyHolding() then
                    state.target = actualAngle(vehicle, wheel, direction)
                    state.atFullLock = state.target <= low or state.target >= high
                    state.vehicle, state.wheelNode, state.direction = vehicle, wheel.node, direction
                end
                entry.holding = true
                entry.angle = angle
                pinHand(hand, wheel)
                self:pulseHand(hand, self.WHEEL_GRAB_GRAB_PULSE_MS, self.WHEEL_GRAB_GRAB_PULSE_STRENGTH)
            end
        elseif self:isHandInUse(hand) then
            local angle, radial, axial = measureHand(self, hand, wheel)
            if angle ~= nil and ringDistance(self, radial, axial) == 0 then
                self:takeVirtualGamepadInput(hand, "grip")
            end
        end
    end
    if not anyHolding() then
        state.vehicle, state.wheelNode, state.direction, state.sent, state.atFullLock, state.sendFailures = nil, nil, nil, nil, false, 0
        unwrapInstance()
        return
    end
    wrapInstance(vehicle)

    local wasAtFullLock = state.atFullLock
    if count > 0 then
        state.target = state.target + sum / count
    end
    state.target = math.max(low, math.min(high, state.target))
    state.atFullLock = state.target <= low or state.target >= high
    if state.atFullLock and not wasAtFullLock then
        for _, hand in ipairs(HANDS) do
            if state.hands[hand].holding then
                self:pulseHand(hand, self.WHEEL_GRAB_FULL_LOCK_PULSE_MS, self.WHEEL_GRAB_FULL_LOCK_PULSE_STRENGTH)
            end
        end
    end
    local value = steeringValue(vehicle, wheel, direction, state.target)
    state.sent = value
    if value ~= nil and sendSteering(vehicle, value) then
        for _, hand in ipairs(HANDS) do releaseHand(hand) end
        state.vehicle, state.wheelNode, state.direction, state.sent, state.atFullLock = nil, nil, nil, nil, false
    end
end

function VRCompanion:isHandHoldingSteeringWheel(hand)
    local entry = state.hands[hand]
    return entry ~= nil and entry.holding
end

function VRCompanion:getWheelGrabHandPose(hand)
    local entry = state.hands[hand]
    if entry == nil or not entry.holding then return nil end
    return pinInTheWorld(entry.pin)
end

function VRCompanion:isSteeringWheelHeld()
    return anyHolding()
end

function VRCompanion:isWheelGrabAvailable()
    return state.reason == nil, state.reason
end

local SETTING_KEY_ENABLED = "vrSettings.wheelGrab"

function VRCompanion:setWheelGrabEnabled(enabled, save)
    self.wheelGrabEnabled = (enabled ~= false)
    if save then
        self:saveConfiguration()
    end
end

function VRCompanion:saveWheelGrabSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY_ENABLED, self.wheelGrabEnabled ~= false)
    end
end

function VRCompanion:loadWheelGrabSetting(xmlFile, xml)
    local loaded = nil
    if type(xml.getBool) == "function" then
        loaded = xml.getBool(xmlFile, SETTING_KEY_ENABLED)
    end
    if loaded == nil and type(xml.getString) == "function" then
        local str = xml.getString(xmlFile, SETTING_KEY_ENABLED)
        if str ~= nil then loaded = VRCompanion.parseOnOffArgument(str) end
    end
    if loaded ~= nil then
        self.wheelGrabEnabled = loaded
    end
end

function VRCompanion:consoleCommandSetWheelGrab(arg)
    local enabled = VRCompanion.parseOnOffArgument(arg)
    if arg == nil or arg == "" or string.lower(tostring(arg)) == "status" then
        return string.format("Wheel Grab is %s. Usage: vrWheelGrab <on|off> (saved)", self.wheelGrabEnabled ~= false and "on" or "off")
    end
    if enabled == nil then
        return "Usage: vrWheelGrab <on|off>"
    end
    self:setWheelGrabEnabled(enabled, true)
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(VRCompanion.formatLocalisedText("vrWheelGrab_set", "VR Wheel Grab: %s (Saved)",
                VRCompanion.onOffText(enabled)), 2500)
        end)
    end
    print(string.format("[FS25_VR] Wheel Grab set to: %s", enabled and "on" or "off"))
    return string.format("Wheel Grab set to: %s (Saved)", enabled and "on" or "off")
end

local function describeHand(self, hand, wheel)
    local pose = self:getHandState(hand)
    local where, detail = "no Steering Wheel to measure from", ""
    if wheel ~= nil then
        local angle, radial, axial = measureHand(self, hand, wheel)
        if angle == nil then
            where = "not located (" .. tostring(radial) .. ")"
        else
            local distance = ringDistance(self, radial, axial)
            where = distance == 0 and "inside the ring" or string.format("%.2f m from the ring", distance)
            detail = string.format(" (%.2f m from the axis, %.2f m from the plane)", radial, axial)
            local away = state.hands[hand].holding and distanceToPin(hand) or nil
            if away ~= nil then
                detail = detail .. string.format(", %.2f m from its pinned point", away)
            end
        end
    end
    local name = (HAND_NAMES[hand]:gsub("^%l", string.upper))
    return string.format("%s hand: %s, grip %.2f, %s%s", name, where, pose.grip or 0,
        state.hands[hand].holding and "holding" or "not holding", detail)
end

local function diagnosticFields(self)
    local vehicle = self:getControlledVehicle()
    local wheel, direction = nil, nil
    if vehicle ~= nil then wheel, direction = steeringWheelOf(vehicle) end
    local _, _, _, reason = findSteeringWheel(self)
    local wheelText = "Steering Wheel: none"
    if wheel ~= nil then
        local low, high = angleLimits(vehicle, wheel, direction)
        wheelText = string.format("Steering Wheel: found (full lock %.2f to %.2f rad)", low, high)
    end
    local sentText = "steering sent: none (nothing held)"
    if state.sent ~= nil then sentText = string.format("steering sent: %.2f (analog, Wheel Peripheral)", state.sent) end
    local wrapText = "steering input wrap: not installed"
    if wraps:problem() ~= nil then
        wrapText = "steering input wrap: gave up (" .. wraps:problem() .. ")"
    elseif wraps:isWrapped(STEERING_FUNCTION) then
        wrapText = "steering input wrap: installed" .. (wraps:instanceTarget() ~= nil and " (on the vehicle too)" or "")
    end
    return {
        string.format("Wheel Grab: %s", self.wheelGrabEnabled ~= false and "on" or "off"),
        reason == nil and "available: yes" or ("available: no (" .. reason .. ")"),
        wheelText,
        describeHand(self, 0, wheel),
        describeHand(self, 1, wheel),
        sentText,
        string.format("at full lock: %s", state.atFullLock and "yes" or "no"),
        wrapText,
    }
end

function VRCompanion:getWheelGrabDiagnostics()
    return table.concat(diagnosticFields(self), " | ")
end

function VRCompanion:consoleCommandWheelGrabDiagnostics()
    for _, field in ipairs(diagnosticFields(self)) do
        print("[FS25_VR] " .. field)
    end
    return self:getWheelGrabDiagnostics()
end

function VRCompanion:loadWheelGrab()
    wrapClass()
end

function VRCompanion:resetWheelGrab()
    wraps:reset()
    state = newState()
end

VRCompanion:registerSubsystem("wheelGrab", {
    mapLoad = function(self) self:loadWheelGrab() end,
    mapUnload = function(self) self:resetWheelGrab() end,
    saveSettings = function(self, xmlFile, xml) self:saveWheelGrabSetting(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadWheelGrabSetting(xmlFile, xml) end,
    consoleCommands = {
        { name = "vrWheelGrab", description = "Turn Wheel Grab, steering by holding the Steering Wheel with a Motion Controller, on or off (saved): vrWheelGrab [on|off]", method = "consoleCommandSetWheelGrab" },
        { name = "vrWheelGrabDiagnostics", description = "Report the Steering Wheel, each hand's distance to the rim ring, the grab and the steering sent: vrWheelGrabDiagnostics", method = "consoleCommandWheelGrabDiagnostics" },
    },
})
