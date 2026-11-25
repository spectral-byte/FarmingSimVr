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
                error("setSteeringInput: the Wheel Peripheral branch is broken")
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
        vehicle.setSteeringInput = Drivable.setSteeringInput
        vehicle.setSteeringAxis = Drivable.setSteeringAxis
        if options.copyOfOriginal then
            vehicle.setSteeringInput = StubSteer.original
        end
        function vehicle:getSteeringDirection()
            local reverse = self.spec_reverseDriving
            return (reverse ~= nil and reverse.isReverseDriving) and -1 or 1
        end
        function vehicle:getIsAIActive() return self.stubHelper end
        function vehicle:getIsEntered() return true end
        function vehicle:getIsPowered() return self.stubPowered end
        function vehicle:getIsPlayerVehicleControlAllowed() return self.stubControlAllowed end
        table.insert(equipped, vehicle)
        StubSteer.step(vehicle, 0)
        return vehicle
    end

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
        return localToWorld(StubSteer.wheelBase(vehicle), radius * math.cos(angle), lift or 0, -radius * math.sin(angle))
    end

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
        local axisSide = vehicle:getSteeringDirection() * spec.axisSide
        if axisSide < 0 then
            vehicle.rotatedTime = math.min(-vehicle.maxRotTime * axisSide, vehicle.maxRotTime)
        else
            vehicle.rotatedTime = math.max(vehicle.minRotTime * axisSide, vehicle.minRotTime)
        end
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
