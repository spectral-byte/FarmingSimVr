-- A vehicle with a Steering Wheel
-- Run after scene_graph.luau. The vehicle side of Wheel Grab (ADR-0027): what the game does with a steering input, as
-- the real Drivable and Wheels code does it (Drivable.lua setSteeringInput / onUpdate / updateSteeringWheel,
-- Wheels.lua updateVehiclePhysics), so a test sees what the player would: the calls that reached the vehicle and how
-- the Steering Wheel turned.
--
-- StubSteer.install(variant)  defines the globals the game defines: InputDevice.CATEGORY and the Drivable class whose
--   setSteeringInput records every call that reaches a vehicle in StubSteer.calls as { value, analog, category }
--   and then stores the input like the game (the analog flag and category only change on a non-zero input).
--   variant: nil ("ok"), "missing" (a game update renamed it: the class has setSteeringAxis, same arguments, and no
--   setSteeringInput) or "reshaped" (setSteeringInput(vehicle, { value, isAnalog, category }) takes one table).
--   or "raises" (the "ok" function, but a call in the Wheel Peripheral's category -- the one Wheel Grab makes -- raises
--   an error and records nothing; the other categories work). StubSteer.failNextWheelCalls(n) makes only the next n
--   such calls raise, in any variant.
--   Run it BEFORE loading the Companion Mod to have Drivable there at file load, like the game; StubSteer.original is
--   the unwrapped function of the "ok" and "raises" variants.
-- StubSteer.keyboard / gamepad / wheelPeripheral (vehicle, value)  another device steering: the call it makes (by the
--   variant's entry point); StubSteer.send(vehicle, value, isAnalog, category) is the general form.
-- StubSteer.equip(vehicle, options) -> vehicle   makes a StubScene.cab vehicle steerable. Like the game it COPIES
--   Drivable.setSteeringInput onto the vehicle instance, so wrapping only the class afterwards would not reach it.
--   options:
--     parent            the node the Steering Wheel hangs in (the cab's parent from StubScene.cab); default the world root
--     position          { x, y, z } of the Steering Wheel's centre in that node (default 0, 1.2, 0.5: in front of
--                       and below the head of StubScene.cab)
--     tilt              radians the wheel's base leans about X (default -0.6: the axis points up and toward the driver).
--                       The tilt is on a node ABOVE the wheel: the game sets the wheel node's own rotation to (0, angle, 0)
--     rotationDeg       the in-cab rotation at full lock, in degrees (default 540: vanilla); outdoorRotation is half
--     maxRotTime        (default 1) and minRotTime (default -maxRotTime): the vehicle's steering times
--     steeringDuration  seconds of the vehicle's wheelSteeringDuration (default 1)
--     rotatedTime       where the steering starts (default 0, straight ahead)
--     noWheel           true: the vehicle has no Steering Wheel node
--     powered           false: the motor is off, the vehicle does not steer
--     controlAllowed    false: getIsPlayerVehicleControlAllowed says no (an attached tool blocks the steering)
--     turnedSeat        true: the driver's seat is turned to drive backwards. Like ReverseDriving it flips BOTH the direction
--                       the game turns the Steering Wheel node in (-1) and getSteeringDirection() (-1)
--     turnedSeatWheel   with turnedSeat: the vehicle has a Steering Wheel of its own for the turned seat (another node,
--                       spec_reverseDriving.steeringWheel), which the game turns instead of the drivable's
--     helper            true: a hired helper is driving (getIsAIActive says yes), as in StubSteer.setHelper
--     copyOfOriginal    true: the vehicle's copy of setSteeringInput is the unwrapped StubSteer.original, as if it was
--                       copied before the Companion Mod wrapped the class
-- StubSteer.tick(dtMs)  one frame of the game's vehicle update for every equipped vehicle: the steering input of the
--   last frame moves the steering value toward it (analog input at 2.5 steering durations per full range, keyboard
--   input at 1), the input is cleared (so it must be sent again every frame), the wheels' rotated time follows, and the
--   Steering Wheel node is set to (0, angle, 0). With no input the value goes back to centre at the rate of the last
--   non-zero input, which stays "analog" after a Wheel Peripheral or Wheel Grab (the game's analog steering back).
-- StubSteer.calls                every setSteeringInput that reached a vehicle, in order
-- StubSteer.setHelper(vehicle, driving)  a hired helper starts (true) or stops driving the vehicle
-- StubSteer.changeCamera(vehicle, cam)  the vehicle's active camera becomes cam (e.g. { isInside = false }, an Outside
--   Camera, or { isInside = true, isPassengerCamera = true })
-- StubSteer.wheelAngle(vehicle)  the node's angle in radians, unwrapped (rotatedTime * in-cab rotation), 0 without a wheel
-- StubSteer.wheelNode(vehicle)   the Steering Wheel node the game turns now; StubSteer.wheelBase(vehicle) the node above it
-- StubSteer.roadWheelAngle(vehicle)  rotatedTime: how far the vehicle's wheels on the ground are turned (positive and
--   negative are the two ways the vehicle steers)
-- StubSteer.rimPoint(vehicle, radius, angle, lift) -> x, y, z   the world position of the point on the wheel at
--   `radius` from its axis and `lift` along it (default 0) when the wheel is turned to `angle` radians

    StubSteer = {}
    StubSteer.calls = {}
    local equipped = {}

    function StubSteer.install(variant)
        _G.InputDevice = { CATEGORY = { UNKNOWN = 0, KEYBOARD_MOUSE = 1, GAMEPAD = 2, WHEEL = 3 } }
        StubSteer.calls = {}
        StubSteer.variant = variant or "ok"
        StubSteer.original = nil
        equipped = {}
        _G.Drivable = {}
        StubSteer.wheelCallFailures = StubSteer.variant == "raises" and math.huge or 0
        local function steer(self, inputValue, isAnalog, deviceCategory)
            if deviceCategory == InputDevice.CATEGORY.WHEEL and StubSteer.wheelCallFailures > 0 then
                StubSteer.wheelCallFailures = StubSteer.wheelCallFailures - 1
                error("setSteeringInput: the Wheel Peripheral branch is broken")  -- before the call is recorded
            end
            table.insert(StubSteer.calls, { value = inputValue, analog = isAnalog, category = deviceCategory })
            local last = self.spec_drivable.lastInputValues
            last.axisSteer = inputValue
            if inputValue ~= 0 then
                last.axisSteerIsAnalog = isAnalog
                last.axisSteerDeviceCategory = deviceCategory
            end
        end
        if StubSteer.variant == "missing" then
            Drivable.setSteeringAxis = steer
        elseif StubSteer.variant == "reshaped" then
            function Drivable.setSteeringInput(self, input) steer(self, input.value, input.isAnalog, input.category) end
        else
            Drivable.setSteeringInput = steer
            StubSteer.original = steer
        end
    end

    function StubSteer.failNextWheelCalls(count) StubSteer.wheelCallFailures = count end

    -- Another device steering: the call the keyboard, a gamepad or a Wheel Peripheral makes, through whatever the
    -- installed game variant calls its steering function.
    function StubSteer.send(vehicle, value, isAnalog, category)
        if StubSteer.variant == "missing" then
            vehicle:setSteeringAxis(value, isAnalog, category)
        elseif StubSteer.variant == "reshaped" then
            vehicle:setSteeringInput({ value = value, isAnalog = isAnalog, category = category })
        else
            vehicle:setSteeringInput(value, isAnalog, category)
        end
    end
    function StubSteer.keyboard(vehicle, value) StubSteer.send(vehicle, value, false, InputDevice.CATEGORY.KEYBOARD_MOUSE) end
    function StubSteer.gamepad(vehicle, value) StubSteer.send(vehicle, value, true, InputDevice.CATEGORY.GAMEPAD) end
    function StubSteer.wheelPeripheral(vehicle, value) StubSteer.send(vehicle, value, true, InputDevice.CATEGORY.WHEEL) end

    function StubSteer.equip(vehicle, options)
        options = options or {}
        local position = options.position or { x = 0, y = 1.2, z = 0.5 }
        local rotation = math.rad(options.rotationDeg or 540)
        vehicle.maxRotTime = options.maxRotTime or 1
        vehicle.minRotTime = options.minRotTime or -vehicle.maxRotTime
        vehicle.wheelSteeringDuration = options.steeringDuration or 1
        vehicle.rotatedTime = options.rotatedTime or 0
        vehicle.stubPowered = options.powered ~= false
        vehicle.stubControlAllowed = options.controlAllowed ~= false
        local spec = { axisSide = 0, lastInputValues = { axisSteer = 0, axisSteerIsAnalog = false } }
        if vehicle.rotatedTime >= 0 then
            spec.axisSide = vehicle.maxRotTime ~= 0 and -vehicle.rotatedTime / vehicle.maxRotTime or 0
        else
            spec.axisSide = vehicle.minRotTime ~= 0 and vehicle.rotatedTime / vehicle.minRotTime or 0
        end
        local function newWheel()
            local base = StubScene.newNode("steeringWheelBase", options.parent or 0, position.x, position.y, position.z,
                options.tilt or -0.6, 0, 0)
            local node = StubScene.newNode("steeringWheel", base, 0, 0, 0)
            return { node = node, base = base, indoorRotation = rotation, outdoorRotation = rotation / 2 }
        end
        if not options.noWheel then
            spec.steeringWheel = newWheel()
        end
        vehicle.spec_drivable = spec
        if options.turnedSeat then
            vehicle.spec_reverseDriving = { isReverseDriving = true, steeringWheel = options.turnedSeatWheel and newWheel() or nil }
        end
        vehicle.stubHelper = options.helper == true
        vehicle.setSteeringInput = Drivable.setSteeringInput  -- Vehicle:load copies the class functions onto the instance
        vehicle.setSteeringAxis = Drivable.setSteeringAxis    -- (the "missing" variant renames it)
        if options.copyOfOriginal then
            vehicle.setSteeringInput = StubSteer.original      -- a copy taken before anything wrapped the class
        end
        function vehicle:getSteeringDirection()
            local reverse = self.spec_reverseDriving  -- ReverseDriving:getSteeringDirection (settled: the seat is not turning)
            return (reverse ~= nil and reverse.isReverseDriving) and -1 or 1
        end
        function vehicle:getIsAIActive() return self.stubHelper end
        function vehicle:getIsEntered() return true end
        function vehicle:getIsPowered() return self.stubPowered end
        function vehicle:getIsPlayerVehicleControlAllowed() return self.stubControlAllowed end
        table.insert(equipped, vehicle)
        StubSteer.step(vehicle, 0)  -- puts the Steering Wheel node at its starting angle
        return vehicle
    end

    -- The wheel the game turns now, and the direction it turns the node in (ReverseDriving:updateSteeringWheel).
    local function activeWheel(vehicle)
        local wheel = vehicle.spec_drivable.steeringWheel
        local reverse = vehicle.spec_reverseDriving
        if reverse ~= nil and reverse.isReverseDriving then
            return reverse.steeringWheel or wheel, -1
        end
        return wheel, 1
    end

    function StubSteer.wheelNode(vehicle)
        local wheel = activeWheel(vehicle)
        return wheel and wheel.node or nil
    end
    function StubSteer.wheelBase(vehicle)
        local wheel = activeWheel(vehicle)
        return wheel and wheel.base or nil
    end
    function StubSteer.setHelper(vehicle, driving) vehicle.stubHelper = driving == true end
    function StubSteer.changeCamera(vehicle, cam) vehicle.getActiveCamera = function() return cam end end
    function StubSteer.wheelAngle(vehicle) return vehicle.stubWheelAngle or 0 end
    function StubSteer.roadWheelAngle(vehicle) return vehicle.rotatedTime end

    function StubSteer.rimPoint(vehicle, radius, angle, lift)
        -- the wheel node is turned about its Y axis: a point at (radius, lift, 0) goes to (r cos a, lift, -r sin a)
        return localToWorld(StubSteer.wheelBase(vehicle), radius * math.cos(angle), lift or 0, -radius * math.sin(angle))
    end

    -- One frame of the vehicle update for one vehicle (dt in ms).
    function StubSteer.step(vehicle, dt)
        local spec = vehicle.spec_drivable
        local last = spec.lastInputValues
        if vehicle:getIsPowered() then
            local speedFactor = last.axisSteerIsAnalog and 2.5 or 1
            local delta = dt / (vehicle.wheelSteeringDuration * 1000) * speedFactor
            if last.axisSteer > spec.axisSide then
                spec.axisSide = math.min(last.axisSteer, spec.axisSide + delta)
            elseif last.axisSteer < spec.axisSide then
                spec.axisSide = math.max(last.axisSteer, spec.axisSide - delta)
            end
        end
        last.axisSteer = 0
        -- Wheels:updateVehiclePhysics
        local axisSide = vehicle:getSteeringDirection() * spec.axisSide
        if axisSide < 0 then
            vehicle.rotatedTime = math.min(-vehicle.maxRotTime * axisSide, vehicle.maxRotTime)
        else
            vehicle.rotatedTime = math.max(vehicle.minRotTime * axisSide, vehicle.minRotTime)
        end
        -- Drivable:updateSteeringWheel: the in-cab rotation in an inside camera that is not a passenger's
        local wheel, direction = activeWheel(vehicle)
        if wheel ~= nil then
            local camera = vehicle:getActiveCamera()
            local maxRotation = wheel.outdoorRotation
            if camera ~= nil and camera.isInside and not camera.isPassengerCamera then maxRotation = wheel.indoorRotation end
            vehicle.stubWheelAngle = vehicle.rotatedTime * maxRotation * direction
            setRotation(wheel.node, 0, vehicle.stubWheelAngle, 0)
        end
    end

    function StubSteer.tick(dt)
        for _, vehicle in ipairs(equipped) do StubSteer.step(vehicle, dt) end
    end
