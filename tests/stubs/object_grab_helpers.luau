-- Object Grab: Lua helpers
-- Lua helpers: the rays a test compares, each { x, y, z, dirX, dirY, dirZ } in the world, and what was drawn.
--   targeterRay()      what the targeter cast in the last game frame
--   headRay()          what the game's own getLookRay answers for the player: the first person camera
--   pointerRay()       the Pointer Ray the Companion Mod placed (VRCompanion:getPointerRay), nil without one
--   pulledBack(r, m)   the ray r with its origin moved back along it by m metres
--   sameRay(a, b), apart(a, b), rayText(r)
--   drawnRayEnd()      runs the camera path (which draws the Pointer Ray for a frame) and answers the end point (x, y, z) of the line it drew for the Pointer Ray, or nil

    function _G.targeterRay() return { StubHandTool.lastRay() } end
    function _G.headRay() return { StubHandTool.originalGetLookRay(foot.player) } end
    function _G.pointerRay()
        local r = VRCompanion:getPointerRay()
        if r == nil then return nil end
        return { r.ox, r.oy, r.oz, r.dx, r.dy, r.dz }
    end
    function _G.pulledBack(r, m) return { r[1] - r[4] * m, r[2] - r[5] * m, r[3] - r[6] * m, r[4], r[5], r[6] } end
    function _G.sameRay(a, b)
        if a == nil or b == nil then return false end
        for i = 1, 6 do
            if a[i] == nil or b[i] == nil or math.abs(a[i] - b[i]) > 1e-3 then return false end
        end
        return true
    end
    function _G.apart(a, b) return math.sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2) end
    function _G.rayText(r) return (r and r[1]) and string.format("(%.3f %.3f %.3f) -> (%.3f %.3f %.3f)", table.unpack(r)) or "none" end
    _G.drawnLines = {}
    _G.drawDebugLine = function(x1, y1, z1, r1, g1, b1, x2, y2, z2) table.insert(_G.drawnLines, { x2, y2, z2 }) end
    _G.drawDebugPoint = function() end
    function _G.drawnRayEnd()
        _G.drawnLines = {}
        StubScene.updateCamera()
        local line = _G.drawnLines[#_G.drawnLines]
        if line == nil then return nil end
        return line[1], line[2], line[3]
    end
