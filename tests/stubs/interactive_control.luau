-- Interactive Control
-- StubIC.install(variant)  installs the globals the Companion Mod looks at, in the release-1.2.0.1 shape
--   variants: nil / "ok", "renamedUpdateClickable" (the Click Point class lacks updateClickable),
--             "missingExecute" (the manager lacks onActionEventExecute)
--   Without a call there is no Interactive Control at all (g_modIsLoaded has no entry).
-- StubIC.addPoint(name, x, y, z, options) -> Click Point at a world position
--   options: analog (bool), inRange (default true: IC activates it), screenX, screenY (where the stub's
--   mouse test finds it; default 0.5, 0.5), facesAway (bool: like a static outdoor Click Point facing away
--   from the camera, IC tests it with no mouse position, so it is never clickable), outdoor (bool: on a
--   vehicle's exterior, for a player on foot; see StubIC.tick), offScreen (bool: it projects off the screen, so
--   IC runs no mouse test on it at all and its clickable state stays as it was)
-- StubIC.equip(vehicle, state) / StubIC.stateSetting (1 toggle, 2 always on, 3 always off) /
--   StubIC.stateWrites / StubIC.changeCamera(vehicle, cam)  the vehicle side, see below
-- StubIC.tick()  one frame of IC's own update of its Click Points (activation, the mouse hit test through
--   updateClickable, hover blink). Like IC, a Click Point in a cab updates every frame (IC updates those from the
--   vehicle's draw), an outdoor one only on the vehicle's update tick: every StubIC.outdoorTickFrames frames
--   (default 4). StubIC.framesToOutdoorTick is how many frames until the next tick (1: the next frame has it).
-- StubIC.executed  names of the Click Points IC executed through its controller, in order
-- StubIC.blinks[name]  hover blinks IC drew on a Click Point     StubIC.mouseX / mouseY  the desk mouse
-- StubIC.selected()  name of the Click Point IC currently treats as the clickable one, or nil

    StubIC = {}
    StubIC.stateWrites = {}
    function StubIC.install(variant)
        variant = variant or "ok"
        local Point = {}
        Point.__index = Point
        local Manager = {}
        Manager.__index = Manager

        StubIC.points, StubIC.executed, StubIC.blinks = {}, {}, {}
        StubIC.mouseX, StubIC.mouseY = 0.9, 0.9
        StubIC.outdoorTickFrames, StubIC.framesToOutdoorTick = 4, 1
        StubIC.Point = Point

        function Point:isActivated() return self.activated end
        function Point:setActivated(activated) self.activated = activated end
        function Point:isClickable() return self.clickable end
        function Point:setClickable(clickable) self.clickable = clickable end
        function Point:isExecutable() return self.activated and self.clickable end
        local function hitTest(self, mx, my)
            if mx == nil or my == nil then self:setClickable(false) return end
            local half = self.size * 0.5
            local over = math.abs(mx - self.screenPosX) < half and math.abs(my - self.screenPosY) < half
            if over then StubIC.blinks[self.name] = (StubIC.blinks[self.name] or 0) + 1 end
            self:setClickable(over)
        end
        if variant == "renamedUpdateClickable" then
            Point.updateClickableRenamed = hitTest
        else
            Point.updateClickable = hitTest
        end
        function Point:updateScreenPosition(mx, my)
            if self.offScreen then return end
            if self.facesAway then mx, my = nil, nil end
            (self.updateClickable or self.updateClickableRenamed)(self, mx, my)
        end
        function Point:update(isIndoor, isOutdoor, hasInput)
            self:setActivated(self.inRange)
            if self.activated then
                local mx, my = _G.g_inputBinding:getMousePosition()
                self:updateScreenPosition(mx, my, isIndoor, isOutdoor)
            else
                self:setClickable(false)
            end
        end

        function Manager:getActiveInteractiveController()
            for _, point in ipairs(StubIC.points) do
                if point:isExecutable() then return point.interactiveController end
            end
            return nil
        end
        if variant ~= "missingExecute" then
            function Manager:onActionEventExecute()
                local controller = self:getActiveInteractiveController()
                if controller ~= nil then controller:execute() end
            end
        end

        -- IC's own settings: the state setting (toggle key, always on or always off).
        Manager.SETTING_STATE_TOGGLE, Manager.SETTING_STATE_ALWAYS_ON, Manager.SETTING_STATE_OFF = 1, 2, 3
        StubIC.stateSetting = Manager.SETTING_STATE_TOGGLE
        StubIC.stateWrites = {}
        Manager.settings = {
            getSetting = function(_, name)
                if name == "IC_STATE" then return StubIC.stateSetting end
                return nil
            end,
        }

        _G.g_inputBinding = _G.g_inputBinding or {}
        _G.g_inputBinding.getMousePosition = function() return StubIC.mouseX, StubIC.mouseY end
        _G.g_modIsLoaded = _G.g_modIsLoaded or {}
        _G.g_modIsLoaded["FS25_interactiveControl"] = true
        _G.FS25_interactiveControl = { InteractiveClickPoint = Point, InteractiveControlManager = Manager }
        _G.g_currentMission = _G.g_currentMission or {}
        _G.g_currentMission.interactiveControl = setmetatable({}, Manager)
    end

    function StubIC.addPoint(name, x, y, z, options)
        options = options or {}
        local controller = { analog = options.analog == true, point = nil }
        function controller:isAnalog() return self.analog end
        function controller:execute() table.insert(StubIC.executed, self.point.name) end
        local node = StubScene.newNode(name, 0, x, y, z)
        local point = setmetatable({
            name = name, node = node, activated = false, clickable = false, size = 0.04,
            inRange = options.inRange ~= false, facesAway = options.facesAway == true,
            outdoor = options.outdoor == true, offScreen = options.offScreen == true,
            screenPosX = options.screenX or 0.5, screenPosY = options.screenY or 0.5,
            interactiveController = controller,
        }, StubIC.Point)
        controller.point = point
        table.insert(StubIC.points, point)
        return point
    end

    -- StubIC.equip(vehicle, state) gives a vehicle Interactive Control's per-vehicle state: the synced
    -- on/off state (spec_interactiveControl.state), isInteractiveControlActivated and
    -- activateInteractiveControl(state, noEventSend), which records every call in StubIC.stateWrites
    -- as { state, noEventSend } and, like IC, changes the state only when it differs.
    function StubIC.equip(vehicle, initialState)
        vehicle.spec_interactiveControl = { state = initialState == true, interactiveControllers = { {} } }
        function vehicle:isInteractiveControlActivated() return self.spec_interactiveControl.state end
        function vehicle:activateInteractiveControl(newState, noEventSend)
            table.insert(StubIC.stateWrites, { state = newState, noEventSend = noEventSend })
            self.spec_interactiveControl.state = newState
        end
        return vehicle
    end

    -- StubIC.changeCamera(vehicle, cam): the vehicle's active camera becomes `cam`. Like IC (unless it
    -- keeps itself alive) the vehicle switches itself off when the new camera is an inside camera.
    function StubIC.changeCamera(vehicle, cam)
        vehicle.getActiveCamera = function() return cam end
        if cam.isInside and not StubIC.keepAlive then vehicle:activateInteractiveControl(false) end
    end

    function StubIC.tick()
        StubIC.framesToOutdoorTick = (StubIC.framesToOutdoorTick or 1) - 1
        local outdoorTick = StubIC.framesToOutdoorTick <= 0
        if outdoorTick then StubIC.framesToOutdoorTick = StubIC.outdoorTickFrames or 1 end
        for _, point in ipairs(StubIC.points or {}) do
            if not point.outdoor then
                -- The cab's Click Points are updated from the vehicle's draw, which the game skips while the HUD is hidden
                -- (StubIC.noVehicleDraw).
                if not StubIC.noVehicleDraw then point:update(true, false, true) end
            elseif outdoorTick then
                point:update(false, true, false)
            end
        end
    end

    function StubIC.selected()
        for _, point in ipairs(StubIC.points or {}) do
            if point:isExecutable() then return point.name end
        end
        return nil
    end
