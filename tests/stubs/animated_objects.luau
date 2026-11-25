    StubAnimatedObject = {}
    local S = StubAnimatedObject

    local nextBox = 0
    function S.install(variant)
        variant = variant or "ok"
        S.toggles, S.casts, S.textCalls, S.activatables, S.registered, S.boxList, S.boxes = {}, {}, {}, {}, {}, {}, {}
        S.callbackName, S.toggleName, S.canName, S.pending, S.raised = "triggerCallback", "onAnimationInputToggle", "getCanBeTriggered", nil, 0
        S.castSeesLooseObjects = true

        _G.CollisionFlag = { DEFAULT = 1, STATIC_OBJECT = 2, DYNAMIC_OBJECT = 4, PLAYER = 8, TRIGGER = 16, TERRAIN = 32, TERRAIN_DELTA = 64,
            TERRAIN_DISPLACEMENT = 128, FILLABLE = 256, BUILDING = 512 }
        _G.CollisionMask = { ALL = 4294967295 }

        local FSCareerMissionInfo = {}
        _G.FSCareerMissionInfo = FSCareerMissionInfo
        _G.g_currentMission = _G.g_currentMission or {}
        _G.g_currentMission.missionInfo = { isa = function(self, class) return class == FSCareerMissionInfo end }
        _G.g_currentMission.activatableObjectsSystem = {
            addActivatable = function(self, activatable) S.activatables[activatable] = true end,
            removeActivatable = function(self, activatable) S.activatables[activatable] = nil end,
        }

        local AnimatedObject = {}
        AnimatedObject.__index = AnimatedObject
        _G.AnimatedObject = AnimatedObject
        local AnimatedMapObject = setmetatable({}, { __index = AnimatedObject })
        AnimatedMapObject.__index = AnimatedMapObject
        _G.AnimatedMapObject = AnimatedMapObject
        local Activatable = {}
        Activatable.__index = Activatable
        _G.AnimatedObjectActivatable = Activatable
        S.classes = { AnimatedObject = AnimatedObject, AnimatedMapObject = AnimatedMapObject, Activatable = Activatable }

        local function triggerCallback(self, triggerId, otherId, onEnter, onLeave, onStay)
            if g_currentMission.missionInfo:isa(FSCareerMissionInfo) then
                if onEnter or onLeave then
                    if g_localPlayer ~= nil and otherId == g_localPlayer.rootNode then
                        if onEnter then
                            g_currentMission.activatableObjectsSystem:addActivatable(self.activatable)
                        else
                            g_currentMission.activatableObjectsSystem:removeActivatable(self.activatable)
                        end
                        self:raiseActive()
                    end
                end
            end
        end
        if variant == "triggerCallbackMissing" then
            AnimatedObject.onTriggerCallback = triggerCallback
            S.callbackName = "onTriggerCallback"
        elseif variant == "triggerCallbackReshaped" then
            AnimatedObject.triggerCallback = function(self)
                local a = S.pending
                triggerCallback(self, a[1], a[2], a[3], a[4], a[5])
            end
        else
            AnimatedObject.triggerCallback = triggerCallback
        end
        function AnimatedObject:raiseActive() S.raised = S.raised + 1 end
        S.originalTriggerCallback = AnimatedObject.triggerCallback

        local function canBeTriggered(self) return self.canBeTriggered ~= false end
        if variant == "canBeTriggeredMissing" then
            AnimatedObject.getIsTriggerable = canBeTriggered
            S.canName = "getIsTriggerable"
        elseif variant == "canBeTriggeredReshaped" then
            AnimatedObject.getCanBeTriggered = function() return true end
        else
            AnimatedObject.getCanBeTriggered = canBeTriggered
        end

        function AnimatedObject:setDirection(direction)
            local animation = self.animation
            if direction == 0 then direction = animation.time >= 0.5 and -1 or 1 end
            animation.direction = direction
        end

        function Activatable:getIsActivatable()
            return self.animatedObject[S.canName](self.animatedObject) == true and self.animatedObject.isEnabled ~= false
        end
        local function toggle(self)
            local object = self.animatedObject
            local direction = object.animation.direction * -1
            table.insert(S.toggles, { object = object, direction = direction })
            object:setDirection(direction)
            self:updateActionEventTexts()
        end
        if variant == "toggleMissing" then
            Activatable.onAnimationToggle = toggle
            S.toggleName = "onAnimationToggle"
        elseif variant == "toggleReshaped" then
            Activatable.onAnimationInputToggle = function() toggle(S.pressTarget) end
        else
            Activatable.onAnimationInputToggle = toggle
        end
        function Activatable:updateActionEventTexts()
            local controls = self.animatedObject.controls
            if controls.posAction and not controls.negAction and controls.posActionText ~= nil and controls.negActionText ~= nil then
                S.setActionEventText(controls.posActionEventId, controls.posActionText)
            end
        end
        function S.setActionEventText(eventId, text)
            if eventId == nil then error("setActionEventText: no action event") end
            table.insert(S.textCalls, { eventId = eventId, text = text })
        end

        _G.raycastClosest = function(x, y, z, dx, dy, dz, maxDistance, callbackName, target, mask)
            table.insert(S.casts, { x = x, y = y, z = z, dx = dx, dy = dy, dz = dz, maxDistance = maxDistance, mask = mask })
            local best, bestNode = nil, nil
            for _, node in ipairs(S.boxList) do
                local box = S.boxes[node]
                if StubScene.nodes[node] ~= nil then
                    local ox, oy, oz = worldToLocal(node, x, y, z)
                    local ex, ey, ez = worldToLocal(node, x + dx, y + dy, z + dz)
                    local d = { ex - ox, ey - oy, ez - oz }
                    local o, h = { ox, oy, oz }, { box.hx, box.hy, box.hz }
                    local tmin, tmax, miss = -math.huge, math.huge, false
                    for i = 1, 3 do
                        if math.abs(d[i]) < 1e-9 then
                            if math.abs(o[i]) > h[i] then miss = true end
                        else
                            local t1, t2 = (-h[i] - o[i]) / d[i], (h[i] - o[i]) / d[i]
                            if t1 > t2 then t1, t2 = t2, t1 end
                            tmin, tmax = math.max(tmin, t1), math.min(tmax, t2)
                        end
                    end
                    if not miss and tmin <= tmax and tmin >= 0 and tmin <= maxDistance and (best == nil or tmin < best) then
                        best, bestNode = tmin, node
                    end
                end
            end
            if S.castSeesLooseObjects and StubHandTool and StubHandTool.castAtLooseObjects then
                S.looseHit = S.looseHit or {}
                local loose = StubHandTool.castAtLooseObjects(S.looseHit, x, y, z, dx, dy, dz, 0, maxDistance)
                if loose ~= nil and (best == nil or loose.distance < best) then best, bestNode = loose.distance, loose.node end
            end
            if best == nil then return 0 end
            target[callbackName](target, bestNode, x + dx * best, y + dy * best, z + dz * best, best, 0, 0, 1, 0, 0, true)
            return 1
        end
    end

    local function newBox(name, parent, x, y, z, hx, hy, hz)
        local node = StubScene.newNode(name, parent, x, y, z)
        S.boxes[node] = { hx = hx, hy = hy, hz = hz }
        table.insert(S.boxList, node)
        return node
    end

    function S.wall(name, x, y, z, hx, hy, hz) return newBox(name, 0, x, y, z, hx, hy, hz) end

    local doors = 0
    function S.door(options)
        options = options or {}
        doors = doors + 1
        local name = options.name or ("door" .. doors)
        local class = options.map and S.classes.AnimatedMapObject or S.classes.AnimatedObject
        local object = setmetatable({ saveId = name, isEnabled = true, canBeTriggered = options.canBeTriggered ~= false }, class)
        if options.noSaveId then object.saveId = nil end
        object.controls = { posAction = "ACTIVATE_HANDTOOL", negAction = options.negAction, posActionText = "Open", negActionText = "Close" }
        if options.noAction then object.controls.posAction = nil end
        if options.sameAction then object.controls.negAction = object.controls.posAction end
        object.animation = { parts = {}, time = 0, direction = 0, duration = 2000 }
        object.triggerNode = StubScene.newNode(name .. "Trigger", 0)
        object.activatable = setmetatable({ animatedObject = object }, S.classes.Activatable)
        for _, part in ipairs(options.parts or { { x = options.x, y = options.y, z = options.z, hx = options.hx, hy = options.hy, hz = options.hz } }) do
            local px, py, pz = part.x or 0, part.y or 0, part.z or 2
            local hx, hy, hz = part.hx or 0.5, part.hy or 0.5, part.hz or 0.05
            local node
            if options.collisionChild then
                node = StubScene.newNode(name .. "Part", 0, px, py, pz)
                newBox(name .. "PartShape", node, 0, 0, 0, hx, hy, hz)
            else
                node = newBox(name .. "Part", 0, px, py, pz, hx, hy, hz)
            end
            table.insert(object.animation.parts, { node = node })
        end
        if options.farmCheck then
            local super = object[S.canName]
            object.farmAllowed = true
            object[S.canName] = function(self) return super(self) and self.farmAllowed ~= false end
        end
        return object
    end

    local function playerRootNode()
        local player = g_localPlayer
        if player.rootNode == nil then player.rootNode = (_G.foot and _G.foot.parent) or StubScene.newNode("playerRoot", 0) end
        return player.rootNode
    end
    local function callTrigger(object, otherId, onEnter, onLeave)
        S.pending = { object.triggerNode, otherId, onEnter, onLeave, false }
        object[S.callbackName](object, object.triggerNode, otherId, onEnter, onLeave, false)
    end
    function S.enterZone(object) callTrigger(object, playerRootNode(), true, false) end
    function S.leaveZone(object) callTrigger(object, playerRootNode(), false, true) end
    function S.otherEnters(object) callTrigger(object, StubScene.newNode("someoneElse", 0), true, false) end
    function S.inZone(object) return S.activatables[object.activatable] == true end
    function S.delete(object) object.isDeleted = true end

    function S.showPrompt(object)
        object.controls.posActionEventId = 100 + #S.registered + 1
        table.insert(S.registered, { object = object, callback = object.activatable[S.toggleName] })
    end
    function S.pressButton(object)
        for _, entry in ipairs(S.registered) do
            if entry.object == object then
                S.pressTarget = object.activatable
                entry.callback(object.activatable)
                return
            end
        end
        error("the prompt is not showing")
    end

    function S.toggleCount(object)
        local count = 0
        for _, toggle in ipairs(S.toggles) do
            if toggle.object == object then count = count + 1 end
        end
        return count
    end
    function S.advance(object, fraction)
        local animation = object.animation
        if animation.direction == 0 then return end
        animation.time = math.max(0, math.min(1, animation.time + animation.direction * fraction))
        if animation.time <= 0 or animation.time >= 1 then animation.direction = 0 end
    end
