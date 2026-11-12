-- Hand Tools: the flashlight
-- Run after hand_tool.luau (and after model_loading.luau: it uses setVisibility and setWorldQuaternion). Its own chunk, so that
-- the other Hand Tools tickets do not edit the same lines. Shaped after the game's HandToolFlashlight (the readable source in
-- the SDK's gameSource.zip), with the game's placement of the light as ADR-0029 describes it: at the first person camera.
--
-- StubHandTool.installFlashlight(variant)   defines the global HandToolFlashlight, the class the game's flashlight is made of.
--   Run it BEFORE loading the Companion Mod to have it there at file load, or any time before the map is loaded.
--   variant   how the class's event HandToolFlashlight.onUpdate(tool, dt) is in this "game version": nil / "ok",
--             "missing" (a game update renamed it to onUpdateBeam: the class has no onUpdate, the game updates through
--             the new name) or "reshaped" (it takes no parameters at all, and updates the flashlight being held)
--   StubHandTool.originalFlashlightOnUpdate   is the game's own onUpdate ("ok" game only: nil in the others)
--   HandToolFlashlight.updateTransform(tool, isHeld)   the game's placement of the light node: at the camera of the carrying
--     player, along its view. (Each flashlight keeps its own copy of the function, as the game's tools do, so that a wrap of
--     the class does not reach it.)
--   The game updates each carried flashlight once per StubHandTool.tick, after the targeter, through the class's onUpdate
--   looked up at that moment, as the game raises its events.
-- StubHandTool.flashlight(player, options) -> tool   the flashlight (spec_flashlight), not yet held. The spec is the game's:
--   lightNode (the light, linked to the world root; visible only while the flashlight is on), lightNodeParent (the node the
--   light was below in the model: the lens, 0.15 m along the tool's +Z), lightNodeOffsetX/Y/Z (where the light sits in the lens)
--   and lightNodeForwardX/Y/Z (the way it shines in the lens, +Z), isActive.
--   tool:setFlashlightIsActive(isActive, noEventSend)   the flashlight's own switch; every call is in
--     StubHandTool.flashlightCalls as { tool = <the flashlight>, isActive = <the argument>, noEventSend = <the argument> }
--   player.isFlashlightActive   the player's own flag for the light, false at first (the game restores the light from it)
--   StubHandTool.beam(tool) -> x, y, z, dirX, dirY, dirZ   where the light node is now and the way it shines, in the world
--   StubHandTool.lensRay(tool) -> x, y, z, dirX, dirY, dirZ   where the light leaves the lens and the way it shines, in the world
--   StubHandTool.flashlights   every flashlight made, in order
--   setDirection(node, dirX, dirY, dirZ, upX, upY, upZ)   the engine's call: the node's +Z axis along the direction

    StubHandTool.flashlightCalls = {}
    StubHandTool.flashlights = {}
    local nodes = StubScene.nodes

    _G.setDirection = function(node, dx, dy, dz, ux, uy, uz)
        local function norm(x, y, z) local l = math.sqrt(x * x + y * y + z * z); return x / l, y / l, z / l end
        local zx, zy, zz = norm(dx, dy, dz)
        local xx, xy, xz = norm(uy * zz - uz * zy, uz * zx - ux * zz, ux * zy - uy * zx)
        local yx, yy, yz = zy * xz - zz * xy, zz * xx - zx * xz, zx * xy - zy * xx
        -- the rotation matrix with columns x, y, z as a quaternion
        local trace = xx + yy + zz
        local qx, qy, qz, qw
        if trace > 0 then
            local s = math.sqrt(trace + 1) * 2
            qw, qx, qy, qz = s / 4, (yz - zy) / s, (zx - xz) / s, (xy - yx) / s
        elseif xx > yy and xx > zz then
            local s = math.sqrt(1 + xx - yy - zz) * 2
            qw, qx, qy, qz = (yz - zy) / s, s / 4, (yx + xy) / s, (zx + xz) / s
        elseif yy > zz then
            local s = math.sqrt(1 + yy - xx - zz) * 2
            qw, qx, qy, qz = (zx - xz) / s, (yx + xy) / s, s / 4, (zy + yz) / s
        else
            local s = math.sqrt(1 + zz - xx - yy) * 2
            qw, qx, qy, qz = (xy - yx) / s, (zx + xz) / s, (zy + yz) / s, s / 4
        end
        setWorldQuaternion(node, qx, qy, qz, qw)
    end

    function StubHandTool.installFlashlight(variant)
        variant = variant or "ok"
        StubHandTool.flashlightVariant = variant
        StubHandTool.flashlightCalls, StubHandTool.flashlights = {}, {}
        local HandToolFlashlight = {}
        _G.HandToolFlashlight = HandToolFlashlight

        -- the game's placement of the light: at the camera, along the view
        function HandToolFlashlight.updateTransform(self, isHeld)
            local player = self:getCarryingPlayer()
            if player == nil then return end
            local camera = player.camera.firstPersonCamera
            local x, y, z = getWorldTranslation(camera)
            local dx, dy, dz = StubScene.worldDirection(camera, 0, 0, -1)
            setWorldTranslation(self.spec_flashlight.lightNode, x, y, z)
            setDirection(self.spec_flashlight.lightNode, dx, dy, dz, 0, 1, 0)
        end
        if variant == "missing" then
            function HandToolFlashlight.onUpdateBeam(self, dt) self:updateTransform(self:getIsHeld()) end
        elseif variant == "reshaped" then
            function HandToolFlashlight.onUpdate()
                local held = StubHandTool.heldTool
                if held ~= nil and held.spec_flashlight ~= nil then held:updateTransform(true) end
            end
        else
            function HandToolFlashlight.onUpdate(self, dt) self:updateTransform(self:getIsHeld()) end
        end
        StubHandTool.originalFlashlightOnUpdate = variant == "ok" and HandToolFlashlight.onUpdate or nil

        if not StubHandTool.flashlightUpdaterAdded then
            StubHandTool.flashlightUpdaterAdded = true
            table.insert(StubHandTool.updaters, function(dt)
                for _, tool in ipairs(StubHandTool.flashlights) do
                    if tool:getIsCarried() then
                        local class = _G.HandToolFlashlight
                        local update = class.onUpdate or class.onUpdateBeam
                        update(tool, dt)
                    end
                end
            end)
        end
    end

    function StubHandTool.flashlight(player, options)
        local tool = StubHandTool.makeTool(player, "flashlight", "spec_flashlight", options)
        local spec = tool.spec_flashlight
        spec.isActive = false
        spec.lightNodeParent = StubScene.newNode("flashlightLens", tool.rootNode, 0, 0, 0.15)
        spec.lightNodeOffsetX, spec.lightNodeOffsetY, spec.lightNodeOffsetZ = 0, 0.01, 0.02
        spec.lightNodeForwardX, spec.lightNodeForwardY, spec.lightNodeForwardZ = 0, 0, 1
        spec.lightNode = StubScene.newNode("flashlightLight", getRootNode())
        setVisibility(spec.lightNode, false)
        player.isFlashlightActive = false
        -- a tool's functions are copies made when it is made
        tool.updateTransform = _G.HandToolFlashlight.updateTransform
        tool.setFlashlightIsActive = function(self, isActive, noEventSend)
            self.spec_flashlight.isActive = isActive
            setVisibility(self.spec_flashlight.lightNode, isActive)
            table.insert(StubHandTool.flashlightCalls, { tool = self, isActive = isActive, noEventSend = noEventSend })
        end
        -- the game's own binding for the flashlight is not the use binding: the tool has no activateAction
        table.insert(StubHandTool.flashlights, tool)
        return tool
    end

    function StubHandTool.beam(tool)
        local node = tool.spec_flashlight.lightNode
        local x, y, z = getWorldTranslation(node)
        local dx, dy, dz = StubScene.worldDirection(node, 0, 0, 1)
        return x, y, z, dx, dy, dz
    end

    function StubHandTool.lensRay(tool)
        local spec = tool.spec_flashlight
        local x, y, z = localToWorld(spec.lightNodeParent, spec.lightNodeOffsetX, spec.lightNodeOffsetY, spec.lightNodeOffsetZ)
        local dx, dy, dz = StubScene.worldDirection(spec.lightNodeParent, spec.lightNodeForwardX, spec.lightNodeForwardY, spec.lightNodeForwardZ)
        return x, y, z, dx, dy, dz
    end
