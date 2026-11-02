-- ====================================================================
-- Test Suite for the frame timing trace (vrPerfTrace)
-- ====================================================================

-- Mock GIANTS Engine globals before loading VRCompanion
_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

-- The game classes the Companion Mod hooks from its update
_G.missionUpdates = 0
_G.FSBaseMission = { update = function(missionSelf, dt) _G.missionUpdates = _G.missionUpdates + 1 end }
_G.VehicleCamera = { update = function(cam, dt) end }

-- Mock VRBridge: records every frame timing call
_G.perfCalls = {}
_G.perfTracing = false
local mockVRBridge = {
    version = "1.0.0.0",
    isReady = function() return true end,
    getRenderingMode = function() return "AFR" end,
    getFrameParity = function() return 0 end,
    setMenuVisible = function(v) end,
    resetTemporalHistory = function() end,
    startPerfTrace = function(seconds)
        table.insert(_G.perfCalls, "start " .. tostring(seconds))
        _G.perfTracing = true
        return true
    end,
    stopPerfTrace = function()
        table.insert(_G.perfCalls, "stop")
        _G.perfTracing = false
    end,
    isPerfTracing = function()
        table.insert(_G.perfCalls, "isPerfTracing")
        return _G.perfTracing
    end,
    perfTraceBegin = function(name) table.insert(_G.perfCalls, "begin " .. name) end,
    perfTraceEnd = function(name) table.insert(_G.perfCalls, "end " .. name) end,
}
_G.VRBridge = mockVRBridge
VRBridge = mockVRBridge

-- Load the Companion Mod module files in modDesc order, exactly as the engine does
loadCompanionMod()

local passed = 0
local failed = 0

local function test(name, fn)
    _G.perfCalls = {}
    local ok, err = pcall(fn)
    if ok then
        passed = passed + 1
        print(string.format("  [PASS] %s", name))
    else
        failed = failed + 1
        print(string.format("  [FAIL] %s: %s", name, tostring(err)))
    end
end

local function calls()
    return table.concat(_G.perfCalls, ", ")
end

local function hasCall(entry)
    for _, call in ipairs(_G.perfCalls) do
        if call == entry then return true end
    end
    return false
end

-- One game frame as the engine runs it: the mission update (which runs the mod update), a camera update, the draw
local function frame()
    FSBaseMission.update({}, 16.6)
    VRCompanion:update(16.6)
    VehicleCamera.update({}, 16.6)
    VRCompanion:draw()
end

print("Running Frame Timing Trace Tests...")

-- The first update installs the mission and camera hooks
VRCompanion:update(16.6)

test("WithoutATraceTheHooksNeverCallVRBridge", function()
    assert(VRCompanion.perfTracing == false, "no trace runs at load")
    frame()
    frame()
    assert(#_G.perfCalls == 0, "unexpected frame timing calls: " .. calls())
    assert(_G.missionUpdates > 0, "the mission update hook ran")
end)

test("StartingATraceTimesTheGameUpdateTheModUpdateTheCameraAndTheDraw", function()
    local reply = VRCompanion:consoleCommandPerfTrace("5")
    assert(string.find(reply, "for 5 s into fs25_vr.log", 1, true), reply)
    assert(VRCompanion.perfTracing == true, "the trace runs")
    assert(_G.perfCalls[1] == "start 5", calls())
    _G.perfCalls = {}
    frame()
    for _, section in ipairs({ "gameUpdate", "modUpdate", "modCamera", "modDraw" }) do
        assert(hasCall("begin " .. section) and hasCall("end " .. section), section .. " not timed: " .. calls())
    end
    VRCompanion:consoleCommandPerfTrace("stop")
end)

test("TheUpdateNoticesTheInjectorEndedTheTrace", function()
    VRCompanion:consoleCommandPerfTrace("")
    assert(_G.perfCalls[1] == "start 10", "default length: " .. calls())
    _G.perfTracing = false  -- the Injector's trace reached its end
    VRCompanion:update(16.6)
    assert(VRCompanion.perfTracing == false, "the flag follows the Injector")
    _G.perfCalls = {}
    frame()
    assert(#_G.perfCalls == 0, "no calls after the trace ended: " .. calls())
end)

test("StopEndsARunningTraceAndSaysWhenNoneRuns", function()
    VRCompanion:consoleCommandPerfTrace("3")
    local reply = VRCompanion:consoleCommandPerfTrace("STOP")
    assert(hasCall("stop"), calls())
    assert(string.find(reply, "stopped", 1, true), reply)
    assert(VRCompanion.perfTracing == false)
    reply = VRCompanion:consoleCommandPerfTrace("stop")
    assert(reply == "No frame timing trace is running", reply)
end)

test("ALengthOutsideOneToSixtySecondsGetsTheUsage", function()
    for _, arg in ipairs({ "0", "61", "abc", "-3" }) do
        local reply = VRCompanion:consoleCommandPerfTrace(arg)
        assert(string.find(reply, "Usage: vrPerfTrace", 1, true), arg .. ": " .. tostring(reply))
    end
    assert(#_G.perfCalls == 0, "nothing started: " .. calls())
    assert(VRCompanion:consoleCommandPerfTrace("2.7") ~= nil and _G.perfCalls[1] == "start 2", "fractions round down: " .. calls())
    VRCompanion:consoleCommandPerfTrace("stop")
end)

test("WithoutTheInjectorTheCommandSaysSo", function()
    local saved = _G.VRBridge
    _G.VRBridge = nil
    VRBridge = nil
    local ok, reply = pcall(VRCompanion.consoleCommandPerfTrace, VRCompanion, "5")
    _G.VRBridge = saved
    VRBridge = saved
    assert(ok, tostring(reply))
    assert(string.find(reply, "needs the Injector", 1, true), reply)
    assert(VRCompanion.perfTracing == false)
end)

test("MapUnloadEndsARunningTrace", function()
    VRCompanion:consoleCommandPerfTrace("5")
    VRCompanion:unregisterPerfTraceConsoleCommands()
    assert(hasCall("stop"), calls())
    assert(VRCompanion.perfTracing == false)
end)

-- The game's mission classes form a chain: the Mission00 subclass either inherits FSBaseMission's update
-- or calls up to it. The mod wraps both, so the inner wrapper must pass straight through under AFR.
local function countBegins(section)
    local n = 0
    for _, call in ipairs(_G.perfCalls) do
        if call == "begin " .. section then n = n + 1 end
    end
    return n
end

local function withMissionChain(buildChain, fn)
    local savedBase, savedM00 = _G.FSBaseMission, _G.Mission00
    VRCompanion:unhookMissionUpdate()
    local runs = { base = 0 }
    buildChain(runs)
    VRCompanion:hookMissionUpdate()
    VRCompanion:consoleCommandPerfTrace("5")
    _G.perfCalls = {}
    local ok, err = pcall(fn, runs)
    VRCompanion:consoleCommandPerfTrace("stop")
    VRCompanion:unhookMissionUpdate()
    _G.FSBaseMission, _G.Mission00 = savedBase, savedM00
    VRCompanion:hookMissionUpdate()
    assert(ok, tostring(err))
end

test("UnderAfrAnInheritedMissionUpdateRunsTheModLogicOnce", function()
    withMissionChain(function(runs)
        _G.FSBaseMission = { update = function(self, dt) runs.base = runs.base + 1 end }
        _G.Mission00 = setmetatable({}, { __index = _G.FSBaseMission })
    end, function(runs)
        _G.Mission00.update({}, 16.6)
        assert(runs.base == 1, "the game's update ran " .. runs.base .. " times")
        assert(countBegins("gameUpdate") == 1, "game update samples: " .. calls())
    end)
end)

test("UnderAfrASubclassCallingUpToTheBaseUpdateRunsTheModLogicOnce", function()
    withMissionChain(function(runs)
        _G.FSBaseMission = { update = function(self, dt) runs.base = runs.base + 1 end }
        _G.Mission00 = { update = function(self, dt) _G.FSBaseMission.update(self, dt) end }
    end, function(runs)
        _G.Mission00.update({}, 16.6)
        assert(runs.base == 1, "the game's update ran " .. runs.base .. " times")
        assert(countBegins("gameUpdate") == 1, "game update samples: " .. calls())
    end)
end)

test("UnderAfrAnErrorInTheGameUpdateStillClearsTheRunningMark", function()
    withMissionChain(function(runs)
        _G.FSBaseMission = {
            update = function(self, dt)
                runs.base = runs.base + 1
                if runs.base == 1 then error("game update failed") end
            end,
        }
        _G.Mission00 = setmetatable({}, { __index = _G.FSBaseMission })
    end, function(runs)
        local ok, err = pcall(_G.Mission00.update, {}, 16.6)
        assert(not ok and string.find(tostring(err), "game update failed", 1, true), "the error reaches the engine: " .. tostring(err))
        _G.perfCalls = {}
        _G.Mission00.update({}, 16.6)
        assert(runs.base == 2, "the next update ran the game's update")
        assert(countBegins("gameUpdate") == 1, "the next update still runs the mod logic: " .. calls())
    end)
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_perf_trace.luau", failed))
