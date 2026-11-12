-- Hand Tools: scene helpers
-- Lua helpers for reading the scene as the player would see it, run once after the scene exists.
--   isBelow(node, ancestor)           the node hangs somewhere below the ancestor
--   handHolder(which)                 the Virtual Hand's node ("left" or "right"), nil without one
--   distance(a, b)                    metres between two nodes' world positions
--   offsetInHolder(node, which)       the node's position in the Virtual Hand's space, x y z
--   shown(which)                      the Virtual Hand is shown
--   fingerCurl(which, finger)         the curl of the three joints of a finger of a Virtual Hand, radians, toward the palm

    function _G.isBelow(node, ancestor)
        local guard = 0
        while node ~= nil and node ~= 0 and guard < 100 do
            if node == ancestor then return true end
            node = getParent(node)
            guard = guard + 1
        end
        return false
    end
    function _G.handHolder(which) return StubModel.hands()[which] end
    function _G.shown(which)
        local node = StubModel.hands()[which]
        return node ~= nil and StubModel.isShown(node)
    end
    function _G.distance(a, b)
        local ax, ay, az = getWorldTranslation(a)
        local bx, by, bz = getWorldTranslation(b)
        return math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2 + (az - bz) ^ 2)
    end
    function _G.offsetInHolder(node, which)
        local x, y, z = getWorldTranslation(node)
        return worldToLocal(StubModel.hands()[which], x, y, z)
    end
    function _G.fingerCurl(which, finger)
        local hand = StubModel.hands()[which]
        return { StubModel.curl(hand, finger .. '1'), StubModel.curl(hand, finger .. '2'), StubModel.curl(hand, finger .. '3') }
    end
