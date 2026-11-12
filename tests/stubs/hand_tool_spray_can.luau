-- Hand Tools: the spray can (ADR-0030)
-- The spray can for StubHandTool, shaped after the game's HandToolSprayCan (the readable source in the SDK's
-- gameSource.zip). Run after hand_tool.luau (it needs model_loading.luau only through the Virtual Hands), then call
-- StubHandTool.installSprayCan(variant) BEFORE loading the Companion Mod, or any time before the map is loaded.
--
-- StubHandTool.installSprayCan(variant)   defines the global HandToolSprayCan (the class the game's spray can is made of) and makes the
--   targeter honour target types: PlayerTargeter:addTargetType(key, mask, minDistance, maxDistance) and :removeTargetType(key),
--   and each update fills closestTargetsByKey[key] with the closest trunk the look ray meets, measured from the ray's origin and
--   only between the type's minimum and maximum distance (the game's tryAddTargetWithMask), as { node, x, y, z, distance }.
--   Holding a can adds its target type (trees, 0.5 m to spec.sprayDetectionDistance, 1.5 m), as onHeldStart does; putting it
--   away removes it.
--   HandToolSprayCan.onUpdate(tool, dt)   the can's update, run each tick (StubHandTool.updaters) for a held can, looked up on
--     the class at every call: reads the found tree, acts on the RISING edge of spec.activatePressed (a marker event, or the
--     game's blinking warning when the can is empty or the tree is not the player's), places a delayed marker, shakes the can
--     node while spraying, ends the spray after its duration (500 ms on g_time) and clears spec.activatePressed.
--   HandToolSprayCan.onDraw(tool)   the can's draw (StubHandTool.draw), looked up on the class at every call: renders
--     "markerTypeIcon" and, while a tree is found, "treeIcon" (both at the centre of the view); nothing for an empty can.
--   variant   how that onDraw is in this "game version": nil / "ok", "missing" (a game update renamed it to onDrawOverlays,
--     which the draw calls instead) or "reshaped" (it takes no parameters, and draws the can being held)
--   The functions the game registers on the tool: activateSpraying(_, inputValue) (spec.activatePressed = inputValue ~= 0; the
--     handler the game's ACTIVATE_HANDTOOL binding calls every frame; recorded in StubHandTool.useCalls as tool = "sprayCan"),
--     tryToAddTreeMarker, processDelayedMarker, setIsSpraying(isSpraying, force), changeTreeMarkerType(_, inputValue) and
--     getIsSprayingAllowed(shape).
--
-- StubHandTool.sprayCan(player, options) -> tool   the spray can (spec_sprayCan), not yet held. options.markers: how many
--   markers it holds (default 50). tool.rootNode, and in tool.spec_sprayCan: canNode (sprayCan_vis, the game's shake node: the
--   can's visual node, whose +Z axis is the way it sprays), remainingNumMarkers, activatePressed, wasActivatePressed, isSpraying,
--   sprayStopTime, sprayDuration, sprayDetectionDistance, foundTargetTree ({ node, x, y, z, distance } or nil), treeMarkerTypeIndex,
--   treeMarkerType ({ name, title, iconFilename }, see the marker types below).
--   tool.activateAction is activateSpraying: the game's binding. g_currentMission also gets showBlinkingWarning (recorded in
--   StubHandTool.warnings as { text, ms }, text being what the game was asked to show) and a treeMarkerSystem with three marker types (CROSS, ARROW, CIRCLE)
--   when it has none, and a g_i18n (StubHandTool.texts) when there is none.
-- StubHandTool.canRay(tool) -> x, y, z, dirX, dirY, dirZ   where the can sprays from and along, in the world: the origin of canNode
--   and its +Z axis
-- StubHandTool.treeAt(ray, distance, radius) -> tree   a tall trunk the ray (x, y, z, dirX, dirY, dirZ in a table) meets at
--   `distance` metres along the ray (a vertical trunk: the horizontal distance of its surface is distance times the horizontal part of the
--   direction). StubHandTool.trees lists them. StubHandTool.forbid(tree): the player may not mark it (getIsSprayingAllowed).
-- StubHandTool.markers   every marker event the can sent (the game's SprayCanEvent), in order, as { tool, typeIndex, shape, x, y, z,
--   hitX, hitY, hitZ } with x, y, z the look ray's origin the game passed; StubHandTool.placedMarkers: the markers the game then
--   placed on the tree (addTreeMarkerCameraBased), same fields; StubHandTool.warnings: the blinking warnings

    StubHandTool.markers, StubHandTool.placedMarkers, StubHandTool.warnings = {}, {}, {}
    StubHandTool.sprayCans, StubHandTool.forbiddenTrees = {}, {}
    StubHandTool.trees = StubHandTool.trees or {}
    local function normalize(x, y, z)
        local length = math.sqrt(x * x + y * y + z * z)
        if length < 1e-9 then return 0, 0, 1 end
        return x / length, y / length, z / length
    end

    -- ---- trees --------------------------------------------------------------------------------------------------------
    -- (the chainsaw's chunk has the same: a game with both keeps the later definition, which is the same)
    function StubHandTool.tree(x, z, radius, y0, y1)
        local node = StubScene.newNode("tree", 0, x, y0, z)
        local tree = { node = node, x = x, z = z, radius = radius, y0 = y0, y1 = y1 }
        table.insert(StubHandTool.trees, tree)
        return tree
    end

    function StubHandTool.treeAt(ray, distance, radius)
        local x, y, z, dx, dy, dz = table.unpack(ray)
        local hx, _, hz = normalize(dx, 0, dz)
        local horizontal = distance * math.sqrt(dx * dx + dz * dz)
        return StubHandTool.tree(x + hx * (horizontal + radius), z + hz * (horizontal + radius), radius, y - 10, y + 10)
    end

    function StubHandTool.forbid(tree) StubHandTool.forbiddenTrees[tree.node] = true end

    -- the closest trunk a ray meets between a minimum and a maximum distance of its origin
    local function castAtTrees(x, y, z, dx, dy, dz, minDistance, maxDistance)
        local best = nil
        for _, tree in ipairs(StubHandTool.trees) do
            local ox, oz = x - tree.x, z - tree.z
            local a = dx * dx + dz * dz
            if a > 1e-9 then
                local b = ox * dx + oz * dz
                local disc = b * b - a * (ox * ox + oz * oz - tree.radius ^ 2)
                if disc >= 0 then
                    local t = (-b - math.sqrt(disc)) / a
                    local hitY = y + dy * t
                    if t >= minDistance and t <= maxDistance and hitY >= tree.y0 and hitY <= tree.y1 and (best == nil or t < best.distance) then
                        best = { node = tree.node, x = x + dx * t, y = hitY, z = z + dz * t, distance = t }
                    end
                end
            end
        end
        return best
    end

    function StubHandTool.installSprayCan(variant)
        variant = variant or "ok"
        StubHandTool.markers, StubHandTool.placedMarkers, StubHandTool.warnings = {}, {}, {}
        StubHandTool.sprayCans, StubHandTool.forbiddenTrees = {}, {}
        local Class = {}
        _G.HandToolSprayCan = Class

        -- ---- the targeter: target types with a minimum and a maximum distance
        local PlayerTargeter = _G.PlayerTargeter
        local baseUpdate = PlayerTargeter.update
        function PlayerTargeter:addTargetType(key, mask, minDistance, maxDistance)
            self.targetedMasks = self.targetedMasks or {}
            if self.targetedMasks[key] ~= nil then return end
            self.targetedMasks[key] = { key = key, mask = mask, minDistance = minDistance or 0, maxDistance = maxDistance }
        end
        function PlayerTargeter:removeTargetType(key)
            if self.targetedMasks ~= nil then self.targetedMasks[key] = nil end
            if self.closestTargetsByKey ~= nil then self.closestTargetsByKey[key] = nil end
        end
        function PlayerTargeter:update(dt)
            baseUpdate(self, dt)
            self.closestTargetsByKey = self.closestTargetsByKey or {}
            for key, targetedMask in pairs(self.targetedMasks or {}) do
                self.closestTargetsByKey[key] = nil
                if key == Class and self.lastRayX ~= nil then
                    self.closestTargetsByKey[key] = castAtTrees(self.lastRayX, self.lastRayY, self.lastRayZ,
                        self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ, targetedMask.minDistance, targetedMask.maxDistance)
                end
            end
        end

        -- a can taken in hand targets trees, one put away stops (the game's onHeldStart)
        local baseHold, baseDrop = StubHandTool.hold, StubHandTool.drop
        function StubHandTool.hold(tool)
            baseHold(tool)
            local spec = tool.spec_sprayCan
            if spec ~= nil and tool.carryingPlayer.isOwner then
                tool.carryingPlayer.targeter:addTargetType(Class, 0, 0.5, spec.sprayDetectionDistance)
            end
        end
        function StubHandTool.drop(tool)
            if tool.spec_sprayCan ~= nil and tool.carryingPlayer ~= nil and tool.carryingPlayer.targeter ~= nil then
                tool.carryingPlayer.targeter:removeTargetType(Class)
            end
            baseDrop(tool)
        end

        -- ---- the marker system and the player's rights
        -- The game's data/maps/maps_treeMarkerTypes.xml gives each type a name (an identifier) and a title (an l10n key written
        -- "$l10n_treeMarker_cross"), an icon and a texture; the icon's filename is the one field the readable Lua reads off the
        -- type (iconFilename). That the native type keeps the title as `title`, and unresolved, is a guess (headset-only).
        local markerTypes = {
            { name = "CROSS", title = "$l10n_treeMarker_cross", iconFilename = "data/shared/treeMarker/markerTreeCross_icon.png" },
            { name = "ARROW", title = "$l10n_treeMarker_arrow", iconFilename = "data/shared/treeMarker/markerTreeArrow_icon.png" },
            { name = "CIRCLE", title = "$l10n_treeMarker_circle", iconFilename = "data/shared/treeMarker/markerTreeCircle_icon.png" },
        }
        StubHandTool.markerTypes = markerTypes
        local treeMarkerSystem = {
            getNumOfTreeMarkerTypes = function(self) return #markerTypes end,
            getTreeMarkerTypeByIndex = function(self, index) return markerTypes[index] end,
            addTreeMarkerCameraBased = function(self, shape, typeIndex, r, g, b, a, x, y, z, hitX, hitY, hitZ, isOwner)
                table.insert(StubHandTool.placedMarkers, { shape = shape, typeIndex = typeIndex, x = x, y = y, z = z,
                    hitX = hitX, hitY = hitY, hitZ = hitZ })
            end,
        }

        -- ---- the functions the game registers on the tool
        local functions = {}
        function functions.activateSpraying(self, _, inputValue)
            table.insert(StubHandTool.useCalls, { tool = "sprayCan", value = inputValue, self = self })
            self.spec_sprayCan.activatePressed = inputValue ~= 0
        end

        function functions.getIsSprayingAllowed(self, shape)
            if shape == nil or shape == 0 then return true end
            return StubHandTool.forbiddenTrees[shape] == nil
        end

        function functions.setIsSpraying(self, isSpraying, force)
            local spec = self.spec_sprayCan
            if spec.isSpraying == isSpraying then return end
            if isSpraying then
                if self:getIsHeld() then
                    spec.shakeEndTime = (_G.g_time or 0) + spec.shakeDuration
                    spec.isSpraying = true
                end
            else
                spec.sprayStopTime = 0
                spec.isSpraying = false
            end
        end

        function functions.processDelayedMarker(self)
            local spec = self.spec_sprayCan
            local marker = spec.delayedMarker
            if marker == nil then return end
            if entityExists(marker.shape) then
                g_currentMission.treeMarkerSystem:addTreeMarkerCameraBased(marker.shape, marker.typeIndex, 1, 1, 1, 1,
                    marker.x, marker.y, marker.z, marker.hitX, marker.hitY, marker.hitZ, true)
            end
            spec.delayedMarker = nil
        end

        function functions.tryToAddTreeMarker(self, typeIndex, shape, x, y, z, hitX, hitY, hitZ)
            local spec = self.spec_sprayCan
            self:setIsSpraying(true, false)
            spec.sprayStopTime = (_G.g_time or 0) + spec.sprayDuration
            spec.remainingNumMarkers = spec.remainingNumMarkers - 1
            if shape ~= nil and self:getIsSprayingAllowed(shape) then
                spec.delayedMarker = { time = (_G.g_time or 0), shape = shape, typeIndex = typeIndex,
                    x = x, y = y, z = z, hitX = hitX, hitY = hitY, hitZ = hitZ }
            end
        end

        function functions.changeTreeMarkerType(self, _, inputValue)
            local spec = self.spec_sprayCan
            spec.treeMarkerTypeIndex = spec.treeMarkerTypeIndex + 1
            if spec.treeMarkerTypeIndex > g_currentMission.treeMarkerSystem:getNumOfTreeMarkerTypes() then spec.treeMarkerTypeIndex = 1 end
            spec.treeMarkerType = g_currentMission.treeMarkerSystem:getTreeMarkerTypeByIndex(spec.treeMarkerTypeIndex)
        end

        -- ---- the specialization's own update: on the class, read at every call (the game's event dispatch)
        function Class.onUpdate(self, dt)
            local spec = self.spec_sprayCan
            local player = self:getCarryingPlayer()
            local allowInput = player ~= nil and player.isOwner or false
            local now = _G.g_time or 0

            if allowInput then
                local targetTree = player.targeter.closestTargetsByKey[Class]
                spec.foundTargetTree = targetTree

                if spec.activatePressed then
                    if not spec.wasActivatePressed and not spec.isSpraying then
                        local mission = g_currentMission
                        if spec.remainingNumMarkers > 0 then
                            local x, y, z = player:getLookRay()
                            local typeIndex = spec.treeMarkerTypeIndex
                            local shape, hitX, hitY, hitZ = nil, nil, nil, nil
                            if targetTree ~= nil then
                                shape = targetTree.node
                                hitX, hitY, hitZ = targetTree.x, targetTree.y, targetTree.z
                            end
                            if self:getIsSprayingAllowed(shape) then
                                -- the game's SprayCanEvent, sent to the server (this client) and run there
                                table.insert(StubHandTool.markers, { tool = self, typeIndex = typeIndex, shape = shape,
                                    x = x, y = y, z = z, hitX = hitX, hitY = hitY, hitZ = hitZ })
                                self:tryToAddTreeMarker(typeIndex, shape, x, y, z, hitX, hitY, hitZ)
                            else
                                mission:showBlinkingWarning("warning_youAreNotAllowedToMarkThisTree", 2000)
                            end
                        else
                            mission:showBlinkingWarning("warning_sprayCanIsEmpty", 2000)
                        end
                        spec.wasActivatePressed = true
                    end
                end
            end

            if spec.delayedMarker ~= nil and spec.delayedMarker.time <= now then self:processDelayedMarker() end

            if spec.isSpraying then
                if allowInput then
                    -- the shake: the can node moves a few centimetres while the can sprays
                    local x, y, z = spec.originalPos[1], spec.originalPos[2], spec.originalPos[3]
                    local timeLeft = math.max(0, spec.shakeEndTime - now)
                    local factor = 1 - (timeLeft / spec.shakeDuration)
                    local animValue = spec.numShakes * factor
                    x = x + math.sin(animValue * math.pi) * 0.01
                    y = y + math.sin(animValue * math.pi) * 0.05
                    z = z + math.sin(animValue * math.pi) * 0.01
                    setTranslation(spec.canNode, x, y, z)
                end
                if now > spec.sprayStopTime then self:setIsSpraying(false, false) end
            end

            if not spec.activatePressed then spec.wasActivatePressed = false end
            spec.activatePressed = false
        end

        -- the draw (the game's HandToolSprayCan:onDraw): the marker type icon and, with a tree found, the tree icon, both at the
        -- centre of the view; nothing for a can that has no marker type or is empty
        local function draw(self)
            local spec = self.spec_sprayCan
            if spec.treeMarkerType == nil or spec.remainingNumMarkers == 0 then return end
            StubHandTool.render(self, "markerTypeIcon")
            if spec.foundTargetTree ~= nil then StubHandTool.render(self, "treeIcon") end
        end
        if variant == "missing" then
            Class.onDrawOverlays = draw
        elseif variant == "reshaped" then
            Class.onDraw = function() draw(StubHandTool.heldTool) end
        else
            Class.onDraw = draw
        end
        StubHandTool.sprayCanDrawVariant = variant

        -- the game's event dispatch: spec[eventName](object, ...), the class read at every call
        if not StubHandTool.sprayCanUpdaterAdded then
            StubHandTool.sprayCanUpdaterAdded = true
            table.insert(StubHandTool.updaters, function(dt)
                local held = StubHandTool.heldTool
                if held ~= nil and held.spec_sprayCan ~= nil then _G.HandToolSprayCan.onUpdate(held, dt) end
            end)
            table.insert(StubHandTool.drawers, function(tool)
                if tool.spec_sprayCan == nil then return end
                local class = _G.HandToolSprayCan
                local handler = StubHandTool.sprayCanDrawVariant == "missing" and class.onDrawOverlays or class.onDraw
                handler(tool)
            end)
        end

        StubHandTool.sprayCanFunctions, StubHandTool.treeMarkerSystem = functions, treeMarkerSystem
    end

    function StubHandTool.sprayCan(player, options)
        options = options or {}
        local tool = StubHandTool.makeTool(player, "sprayCan", "spec_sprayCan", options)
        local spec = tool.spec_sprayCan
        -- the can stands upright in its i3d: the visual node, a little up and forward of the root, sprays along its +Z
        spec.canNode = StubScene.newNode("sprayCan_vis", tool.rootNode, 0, 0.12, 0.06)
        spec.originalPos = { getTranslation(spec.canNode) }
        spec.remainingNumMarkers = options.markers or 50
        spec.sprayDetectionDistance = 1.5
        spec.sprayDuration, spec.sprayStopTime = 500, 0
        spec.wasActivatePressed, spec.activatePressed, spec.isSpraying = false, false, false
        spec.numShakes, spec.shakeDuration, spec.shakeEndTime = 3, 200, 0
        spec.delayedMarker, spec.foundTargetTree = nil, nil

        -- what the game's mission offers the can
        local mission = _G.g_currentMission
        mission.treeMarkerSystem = mission.treeMarkerSystem or StubHandTool.treeMarkerSystem
        if mission.showBlinkingWarning == nil then
            mission.showBlinkingWarning = function(self, text, ms) table.insert(StubHandTool.warnings, { text = text, ms = ms }) end
        end
        spec.treeMarkerTypeIndex = 1
        spec.treeMarkerType = mission.treeMarkerSystem:getTreeMarkerTypeByIndex(1)

        -- the game's texts: the marker types' titles (the game's l10n) and the Companion Mod's own (its modDesc.xml l10n). Added
        -- when the test has no i18n of its own; hasText/getText as the game's I18N has them (a missing text reads "Missing ...").
        if _G.g_i18n == nil then
            local texts = {
                treeMarker_cross = "Cross", treeMarker_arrow = "Arrow", treeMarker_circle = "Circle",
                vrHandTools_markerType = "Marker type: %s",
            }
            StubHandTool.texts = texts
            _G.g_i18n = {
                hasText = function(self, name) return texts[name] ~= nil end,
                getText = function(self, name) return texts[name] or ("Missing '" .. tostring(name) .. "' in l10n") end,
            }
        end

        for name, fn in pairs(StubHandTool.sprayCanFunctions) do tool[name] = fn end
        tool.activateAction = tool.activateSpraying
        table.insert(StubHandTool.sprayCans, tool)
        return tool
    end

    function StubHandTool.canRay(tool)
        local node = tool.spec_sprayCan.canNode
        local x, y, z = getWorldTranslation(node)
        local dx, dy, dz = StubScene.worldDirection(node, 0, 0, 1)
        return x, y, z, dx, dy, dz
    end
