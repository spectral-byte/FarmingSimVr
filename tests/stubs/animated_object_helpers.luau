-- Animated Objects: Lua helpers
-- Lua helper: animatedCasts() are the ray casts along the Pointer Ray (to its length and the cast starting behind the fingertip), the ones
-- Object Grab's check of the off-hand does not make.

    function _G.animatedCasts()
        local length = VRCompanion.POINTER_RAY_LENGTH + VRCompanion.OBJECT_GRAB_REACH_BEHIND
        local casts = {}
        for _, cast in ipairs(StubAnimatedObject.casts) do
            if math.abs(cast.maxDistance - length) < 1e-6 then table.insert(casts, cast) end
        end
        return casts
    end
