-- ====================================================================
-- Test Suite: the core's game function wrap helper (VRCompanion.GameWraps, spec #280, ticket #285)
--
-- Seen from outside: each test makes stand-in game classes (globals, as the engine has them), declares the functions a Subsystem
-- would wrap, and reads what the classes and the log were given. Never the helper's own tables.
-- ====================================================================

_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

loadCompanionMod()

local GameWraps = VRCompanion.GameWraps

local passed = 0
local failed = 0

-- The lines the mod printed during the test running now.
local logged = {}
local realPrint = print
_G.print = function(...)
    local parts = {}
    for i = 1, select("#", ...) do parts[#parts + 1] = tostring(select(i, ...)) end
    logged[#logged + 1] = table.concat(parts, "\t")
end

local function test(name, fn)
    logged = {}
    local ok, err = pcall(fn)
    if ok then
        passed = passed + 1
        realPrint(string.format("  [PASS] %s", name))
    else
        failed = failed + 1
        realPrint(string.format("  [FAIL] %s: %s", name, tostring(err)))
    end
end

local function expectEqual(actual, expected, what)
    if actual ~= expected then
        error(string.format("%s: expected %s, got %s", what or "value", tostring(expected), tostring(actual)), 2)
    end
end

local function warnings()
    local lines = {}
    for _, line in ipairs(logged) do
        if string.find(line, "[Warning]", 1, true) then lines[#lines + 1] = line end
    end
    return lines
end

-- ---- Refusing a game function that is not the expected shape ------------------------------------------------------------

test("a missing function gives up with one warning that names it", function()
    _G.StubClass = {}
    local set = GameWraps.newSet({
        owner = "stub",
        giveUp = "Stub disabled: %s. The game is unchanged.",
        functions = {
            update = { class = "StubClass", parameters = 2, describe = "(object, dt)", make = function(original) return original end },
        },
    })
    set:wrapAll()
    set:check()
    set:check()
    local lines = warnings()
    expectEqual(#lines, 1, "warnings")
    expectEqual(lines[1], "[FS25_VR] [Warning] Stub disabled: StubClass.update: it is missing. The game is unchanged.", "the warning")
end)

-- A Subsystem with one function, update(object, dt), on StubClass, and the give-up sentence of the first test.
local function stubSet(make)
    return GameWraps.newSet({
        owner = "stub",
        giveUp = "Stub disabled: %s. The game is unchanged.",
        functions = {
            update = { class = "StubClass", parameters = 2, describe = "(object, dt)", make = make },
        },
    })
end

local function wrapperOf(original)
    return function(object, dt) return "wrapped", original(object, dt) end
end

test("a value that is not a function is refused as not a function", function()
    _G.StubClass = { update = 42 }
    stubSet(wrapperOf):check()
    expectEqual(warnings()[1], "[FS25_VR] [Warning] Stub disabled: StubClass.update: it is not a function. The game is unchanged.", "the warning")
    expectEqual(StubClass.update, 42, "the class is left alone")
end)

test("a function that declares too few parameters is refused with its count and the expected shape", function()
    local original = function(object) return "game" end
    _G.StubClass = { update = original }
    local set = stubSet(wrapperOf)
    set:wrapAll()
    set:check()
    expectEqual(warnings()[1],
        "[FS25_VR] [Warning] Stub disabled: StubClass.update: it takes 1 parameters, not (object, dt). The game is unchanged.", "the warning")
    expectEqual(StubClass.update, original, "the class keeps the game's function")
end)

test("a function that takes any number of parameters is accepted", function()
    _G.StubClass = { update = function(...) return "game" end }
    local set = stubSet(wrapperOf)
    set:wrapAll()
    set:check()
    expectEqual(#warnings(), 0, "warnings")
end)

test("the shape of a function is described with a name as its subject", function()
    expectEqual(GameWraps.shapeProblem(nil, 1, "(object)", "AnimatedObject.toggle"), "AnimatedObject.toggle is missing", "missing")
    expectEqual(GameWraps.shapeProblem(true, 1, "(object)", "AnimatedObject.toggle"), "AnimatedObject.toggle is not a function", "not a function")
    expectEqual(GameWraps.shapeProblem(function() end, 1, "(object)", "AnimatedObject.toggle"),
        "AnimatedObject.toggle takes 0 parameters, not (object)", "too few")
    expectEqual(GameWraps.shapeProblem(function(a) end, 1, "(object)"), nil, "as expected")
end)

test("a Subsystem can have its reasons name the function as their subject", function()
    _G.StubClass = { update = function(object) end }
    local set = GameWraps.newSet({
        owner = "stub",
        giveUp = "Stub disabled: %s. The game is unchanged.",
        nameAsSubject = true,
        functions = { update = { class = "StubClass", parameters = 2, describe = "(object, dt)" } },
    })
    set:check()
    expectEqual(warnings()[1], "[FS25_VR] [Warning] Stub disabled: StubClass.update takes 1 parameters, not (object, dt). The game is unchanged.", "the warning")
end)

-- ---- Putting a wrapper on and lifting it --------------------------------------------------------------------------------

test("a wrapper is put on the class once, however often the Subsystem wraps", function()
    local original = function(object, dt) return "game" end
    _G.StubClass = { update = original }
    local made = 0
    local set = stubSet(function(fn) made = made + 1; return wrapperOf(fn) end)
    set:wrapAll()
    local first = StubClass.update
    set:wrapAll()
    set:wrapAll()
    expectEqual(made, 1, "wrappers made")
    expectEqual(StubClass.update, first, "the wrapper stays")
    local a, b = StubClass.update({}, 1)
    expectEqual(a, "wrapped", "the wrapper runs")
    expectEqual(b, "game", "and calls the game's function")
end)

test("a wrapper is put on again when the game replaced the class table", function()
    _G.StubClass = { update = function(object, dt) return "old" end }
    local set = stubSet(wrapperOf)
    set:wrapAll()
    local newOriginal = function(object, dt) return "new" end
    _G.StubClass = { update = newOriginal }
    set:wrapAll()
    local a, b = StubClass.update({}, 1)
    expectEqual(a, "wrapped", "the new class is wrapped")
    expectEqual(b, "new", "around its own function")
    set:liftAll()
    expectEqual(StubClass.update, newOriginal, "lifting gives the new class its function back")
end)

test("lifting gives the class the game's function back", function()
    local original = function(object, dt) return "game" end
    _G.StubClass = { update = original }
    local set = stubSet(wrapperOf)
    set:wrapAll()
    set:liftAll()
    expectEqual(StubClass.update, original, "the game's function")
end)

test("lifting leaves another mod's later wrap in place", function()
    local original = function(object, dt) return "game" end
    _G.StubClass = { update = original }
    local set = stubSet(wrapperOf)
    set:wrapAll()
    local ours = StubClass.update
    local theirs = function(object, dt) return "theirs", ours(object, dt) end
    StubClass.update = theirs
    set:liftAll()
    expectEqual(StubClass.update, theirs, "the other mod's wrap stays")
end)

test("a declared function without a wrapper is only checked, never wrapped", function()
    local original = function(object, dt) return "game" end
    _G.StubClass = { update = original }
    local set = stubSet(nil)
    set:wrapAll()
    expectEqual(StubClass.update, original, "not wrapped")
    set:check()
    expectEqual(#warnings(), 0, "and as expected")
end)

test("a class the game does not have is neither wrapped nor a reason to give up", function()
    _G.StubClass = nil
    local set = stubSet(wrapperOf)
    set:wrapAll()
    set:check()
    expectEqual(#warnings(), 0, "warnings")
end)

test("a class that must be there is a reason to give up when the game does not have it", function()
    _G.StubClass = nil
    local set = GameWraps.newSet({
        owner = "stub",
        giveUp = "Stub disabled: %s. The game is unchanged.",
        functions = {
            update = { class = "StubClass", parameters = 2, describe = "(object, dt)", missingClass = "the game's StubClass class was not found" },
        },
    })
    set:check()
    expectEqual(warnings()[1], "[FS25_VR] [Warning] Stub disabled: the game's StubClass class was not found. The game is unchanged.", "the warning")
end)

test("a class can be found by a function, for one kept in another mod's environment", function()
    local original = function(object, dt) return "game" end
    local env = { StubClass = { update = original } }
    local set = GameWraps.newSet({
        owner = "stub",
        giveUp = "Stub disabled: %s. The game is unchanged.",
        functions = {
            update = { class = function() return env.StubClass end, className = "StubClass", parameters = 2, describe = "(object, dt)", make = wrapperOf },
        },
    })
    set:wrapAll()
    expectEqual((env.StubClass.update({}, 1)), "wrapped", "wrapped")
    set:liftAll()
    expectEqual(env.StubClass.update, original, "and lifted")
    env.StubClass.update = function() end
    set = GameWraps.newSet({
        owner = "stub",
        giveUp = "Stub disabled: %s. The game is unchanged.",
        functions = {
            update = { class = function() return env.StubClass end, className = "StubClass", parameters = 2, describe = "(object, dt)" },
        },
    })
    set:check()
    expectEqual(warnings()[1], "[FS25_VR] [Warning] Stub disabled: StubClass.update: it takes 0 parameters, not (object, dt). The game is unchanged.", "named by className")
end)

-- ---- Giving up ------------------------------------------------------------------------------------------------------------

-- Two functions on StubClass: update is wrapped, draw is only checked.
local function twoFunctionSet()
    return GameWraps.newSet({
        owner = "stub",
        giveUp = "Stub disabled: %s. The game is unchanged.",
        functions = {
            update = { class = "StubClass", parameters = 2, describe = "(object, dt)", make = wrapperOf },
            draw = { class = "StubClass", parameters = 1, describe = "(object)" },
        },
    })
end

test("giving up lifts every wrap and logs once", function()
    local original = function(object, dt) return "game" end
    _G.StubClass = { update = original }  -- draw is missing
    local set = twoFunctionSet()
    set:wrapAll()
    expectEqual((StubClass.update({}, 1)), "wrapped", "wrapped first")
    set:check()
    set:check()
    expectEqual(StubClass.update, original, "the game's function is back")
    expectEqual(#warnings(), 1, "one warning")
    expectEqual(warnings()[1], "[FS25_VR] [Warning] Stub disabled: StubClass.draw: it is missing. The game is unchanged.", "the warning")
    set:wrapAll()
    expectEqual(StubClass.update, original, "nothing is wrapped again after giving up")
end)

test("a Subsystem can give up on a problem found while it runs, once", function()
    local original = function(object, dt) return "game" end
    _G.StubClass = { update = original, draw = function(object) end }
    local set = twoFunctionSet()
    set:wrapAll()
    set:giveUp("setSteeringInput raised: boom")
    set:giveUp("setSteeringInput raised: again")
    expectEqual(StubClass.update, original, "lifted")
    expectEqual(#warnings(), 1, "one warning")
    expectEqual(warnings()[1], "[FS25_VR] [Warning] Stub disabled: setSteeringInput raised: boom. The game is unchanged.", "the warning")
    expectEqual(set:problem(), "setSteeringInput raised: boom", "the reason, for the diagnostics")
end)

test("a give-up sentence with a percent sign in the reason is logged as it is", function()
    _G.StubClass = { update = function(object, dt) end, draw = function(object) end }
    local set = twoFunctionSet()
    set:giveUp("100% gone")
    expectEqual(warnings()[1], "[FS25_VR] [Warning] Stub disabled: 100% gone. The game is unchanged.", "the warning")
end)

test("after a reset (map unload) the Subsystem wraps again and gives up again on the next map", function()
    local original = function(object, dt) return "game" end
    _G.StubClass = { update = original }
    local set = twoFunctionSet()
    set:wrapAll()
    set:check()
    set:reset()
    set:wrapAll()
    expectEqual((StubClass.update({}, 1)), "wrapped", "wrapped on the next map")
    set:check()
    expectEqual(#warnings(), 2, "warned on each map")
    expectEqual(StubClass.update, original, "lifted again")
end)

test("the diagnostics name each wrapped function's state", function()
    _G.StubClass = { update = function(object, dt) end, draw = function(object) end }
    local set = twoFunctionSet()
    expectEqual(table.concat(set:wrapNames(), ","), "update", "only wrapped functions are named")
    expectEqual(set:status("update"), "update wrap: not installed", "before")
    set:wrapAll()
    expectEqual(set:status("update"), "update wrap: installed", "wrapped")
    set:giveUp("it broke")
    expectEqual(set:status("update"), "update wrap: gave up (it broke)", "gave up")
end)

test("the game's own function stays reachable while it is wrapped", function()
    local original = function(object, dt) return "game" end
    _G.StubClass = { update = original }
    local set = twoFunctionSet()
    expectEqual(set:original("update"), nil, "not wrapped")
    set:wrapAll()
    expectEqual(set:original("update"), original, "the game's function")
    expectEqual(set:isWrapped("update"), true, "wrapped")
end)

-- ---- A wrap shared by two Subsystems ------------------------------------------------------------------------------------

GameWraps.defineShared("stubAim", {
    class = "StubPlayer", fn = "getRay",
    make = function(original) return function(player) return "aimed", original(player) end end,
})

local function aimingSet(owner, extra)
    local functions = { getRay = { class = "StubPlayer", parameters = 1, describe = "(player)", shared = "stubAim" } }
    if extra then functions.extra = extra end
    return GameWraps.newSet({ owner = owner, giveUp = owner .. " disabled: %s.", functions = functions })
end

test("a shared wrap is put on once for the Subsystems that hold it", function()
    local original = function(player) return "head" end
    _G.StubPlayer = { getRay = original }
    local a, b = aimingSet("a"), aimingSet("b")
    a:wrapAll()
    local wrapper = StubPlayer.getRay
    b:wrapAll()
    expectEqual(StubPlayer.getRay, wrapper, "one wrapper")
    local first, second = StubPlayer.getRay({})
    expectEqual(first, "aimed", "the shared wrapper runs")
    expectEqual(second, "head", "around the game's function")
    expectEqual(b:original("getRay"), original, "each holder reaches the game's function")
    a:reset()
    b:reset()
end)

test("one Subsystem giving up leaves the shared wrap to the other, and the last one lifts it", function()
    local original = function(player) return "head" end
    _G.StubPlayer = { getRay = original }
    local a = aimingSet("a", { class = "StubTool", parameters = 1, describe = "(tool)" })  -- a also needs StubTool.extra
    local b = aimingSet("b")
    _G.StubTool = { extra = 7 }
    a:wrapAll()
    b:wrapAll()
    a:check()
    b:check()
    expectEqual(warnings()[1], "[FS25_VR] [Warning] a disabled: StubTool.extra: it is not a function.", "a gave up")
    expectEqual(#warnings(), 1, "b did not")
    expectEqual((StubPlayer.getRay({})), "aimed", "b still aims")
    b:reset()
    expectEqual(StubPlayer.getRay, original, "the last holder lifted it")
end)

test("a shared function that is reshaped gives up every Subsystem that holds it", function()
    _G.StubPlayer = { getRay = function() return "head" end }
    local a, b = aimingSet("a"), aimingSet("b")
    a:wrapAll()
    b:wrapAll()
    a:check()
    b:check()
    expectEqual(#warnings(), 2, "both gave up")
    expectEqual(warnings()[2], "[FS25_VR] [Warning] b disabled: StubPlayer.getRay: it takes 0 parameters, not (player).", "b's warning")
end)

test("lifting a shared wrap leaves another mod's later wrap in place", function()
    _G.StubPlayer = { getRay = function(player) return "head" end }
    local a = aimingSet("a")
    a:wrapAll()
    local theirs = function(player) return "theirs" end
    StubPlayer.getRay = theirs
    a:reset()
    expectEqual(StubPlayer.getRay, theirs, "the other mod's wrap stays")
end)

test("a shared wrap is put on again when the game replaced the class table", function()
    _G.StubPlayer = { getRay = function(player) return "old" end }
    local a = aimingSet("a")
    a:wrapAll()
    local newOriginal = function(player) return "new" end
    _G.StubPlayer = { getRay = newOriginal }
    a:wrapAll()
    local first, second = StubPlayer.getRay({})
    expectEqual(first, "aimed", "the new class is wrapped")
    expectEqual(second, "new", "around its own function")
    a:reset()
    expectEqual(StubPlayer.getRay, newOriginal, "and lifted")
end)

-- ---- A wrap on one game object's own copy ------------------------------------------------------------------------------

test("an object's own copy of a function is wrapped, and given back when lifted", function()
    local classFn = function(object, dt) return "class" end
    _G.StubClass = { update = classFn, draw = function(object) end }
    local set = twoFunctionSet()
    set:wrapAll()
    local ownCopy = function(object, dt) return "own" end
    local object = { update = ownCopy }  -- copied from the class before it was wrapped
    set:wrapInstance(object, { "update" })
    local a, b = object.update(object, 1)
    expectEqual(a, "wrapped", "the copy is wrapped")
    expectEqual(b, "own", "around the copy")
    set:liftInstance()
    expectEqual(object.update, ownCopy, "the copy is back")
end)

test("an object that reaches the class wrap is not wrapped again", function()
    _G.StubClass = { update = function(object, dt) return "class" end, draw = function(object) end }
    local set = twoFunctionSet()
    set:wrapAll()
    local object = setmetatable({}, { __index = StubClass })
    set:wrapInstance(object, { "update" })
    expectEqual(rawget(object, "update"), nil, "nothing put on the object")
    local copied = { update = StubClass.update }  -- a copy of the class wrapper
    set:wrapInstance(copied, { "update" })
    expectEqual(copied.update, StubClass.update, "the copied wrapper is left as it is")
end)

test("an object whose own copy is wrapped through its class is given nothing of its own when lifted", function()
    local classFn = function(object, dt) return "class" end
    _G.StubClass = { update = classFn, draw = function(object) end }
    local set = twoFunctionSet()
    local object = setmetatable({}, { __index = StubClass })  -- reads the class's function, which is not wrapped
    set:wrapInstance(object, { "update" })
    expectEqual((object.update(object, 1)), "wrapped", "wrapped on the object")
    set:liftInstance()
    expectEqual(rawget(object, "update"), nil, "nothing left on the object")
end)

test("wrapping another object lifts the wrap of the first, and another mod's later wrap of an object stays", function()
    _G.StubClass = { update = function(object, dt) return "class" end, draw = function(object) end }
    local set = twoFunctionSet()
    local firstCopy = function(object, dt) return "first" end
    local first = { update = firstCopy }
    set:wrapInstance(first, { "update" })
    local second = { update = function(object, dt) return "second" end }
    set:wrapInstance(second, { "update" })
    expectEqual(first.update, firstCopy, "the first object has its copy back")
    expectEqual((second.update(second, 1)), "wrapped", "the second is wrapped")
    local theirs = function() return "theirs" end
    second.update = theirs
    set:liftInstance()
    expectEqual(second.update, theirs, "the other mod's wrap stays")
end)

test("an object's copy is wrapped again after the game replaced it on the same object", function()
    _G.StubClass = { update = function(object, dt) return "class" end, draw = function(object) end }
    local set = twoFunctionSet()
    local object = { update = function(object, dt) return "own" end }
    set:wrapInstance(object, { "update" })
    local wrapper = object.update
    set:wrapInstance(object, { "update" })
    expectEqual(object.update, wrapper, "wrapped once")
    object.update = function(object, dt) return "fresh" end
    set:wrapInstance(object, { "update" })
    local a, b = object.update(object, 1)
    expectEqual(a, "wrapped", "wrapped again")
    expectEqual(b, "fresh", "around the fresh copy")
end)

test("an object's copy that is reshaped is not wrapped, and giving up lifts the object's wrap", function()
    _G.StubClass = { update = function(object, dt) return "class" end, draw = function(object) end }
    local set = twoFunctionSet()
    local reshaped = function() end
    local odd = { update = reshaped }
    set:wrapInstance(odd, { "update" })
    expectEqual(odd.update, reshaped, "left alone")
    local ownCopy = function(object, dt) return "own" end
    local object = { update = ownCopy }
    set:wrapInstance(object, { "update" })
    set:giveUp("it broke")
    expectEqual(object.update, ownCopy, "lifted on giving up")
    set:wrapInstance(object, { "update" })
    expectEqual(object.update, ownCopy, "and not wrapped again")
end)

test("the Subsystem can tell which object's own copy is wrapped, for its diagnostics", function()
    _G.StubClass = { update = function(object, dt) return "class" end, draw = function(object) end }
    local set = twoFunctionSet()
    set:wrapAll()
    expectEqual(set:instanceTarget(), nil, "none before")
    local reaching = setmetatable({}, { __index = StubClass })
    set:wrapInstance(reaching, { "update" })
    expectEqual(set:instanceTarget(), nil, "none when the class wrap reaches the object")
    local object = { update = function(object, dt) return "own" end }
    set:wrapInstance(object, { "update" })
    expectEqual(set:instanceTarget(), object, "the object with its own copy")
    set:liftInstance()
    expectEqual(set:instanceTarget(), nil, "none once lifted")
end)

-- ---- The give-up warning's sentence ---------------------------------------------------------------------------------------

test("a give-up sentence without %s raises when the set is declared, naming the Subsystem", function()
    for _, sentence in ipairs({ "Stub disabled. The game is unchanged.", 42 }) do
        local ok, err = pcall(GameWraps.newSet, {
            owner = "stub",
            giveUp = sentence,
            functions = { update = { class = "StubClass", parameters = 2, describe = "(object, dt)" } },
        })
        expectEqual(ok, false, "declaring " .. tostring(sentence))
        expectEqual(string.find(tostring(err), "stub", 1, true) ~= nil, true, "the message names the Subsystem: " .. tostring(err))
        expectEqual(string.find(tostring(err), "%s", 1, true) ~= nil, true, "and what is missing: " .. tostring(err))
    end
end)

test("a give-up reason with a % in it is logged as it is", function()
    _G.StubClass = {}
    local set = stubSet(function(original) return original end)
    set:giveUp("100% gone")
    expectEqual(warnings()[1], "[FS25_VR] [Warning] Stub disabled: 100% gone. The game is unchanged.", "the warning")
end)

-- ---- The order the functions are checked in -----------------------------------------------------------------------------

test("a set declared with an order checks its functions in that order, so the first reason logged is the first declared", function()
    _G.StubClass = {}
    local set = GameWraps.newSet({
        owner = "stub",
        giveUp = "Stub disabled: %s. The game is unchanged.",
        order = { "zeta", "alpha" },
        functions = {
            alpha = { class = "StubClass", parameters = 1, describe = "(object)" },
            zeta = { class = "StubClass", parameters = 1, describe = "(object)" },
        },
    })
    set:check()
    expectEqual(warnings()[1], "[FS25_VR] [Warning] Stub disabled: StubClass.zeta: it is missing. The game is unchanged.", "the warning")
end)

test("a set declared without an order checks its functions by name", function()
    _G.StubClass = {}
    local set = GameWraps.newSet({
        owner = "stub",
        giveUp = "Stub disabled: %s. The game is unchanged.",
        functions = {
            zeta = { class = "StubClass", parameters = 1, describe = "(object)" },
            alpha = { class = "StubClass", parameters = 1, describe = "(object)" },
        },
    })
    set:check()
    expectEqual(warnings()[1], "[FS25_VR] [Warning] Stub disabled: StubClass.alpha: it is missing. The game is unchanged.", "the warning")
end)

test("an order that leaves out a declared function, or names one that is not declared, raises when the set is declared", function()
    for _, order in ipairs({ { "alpha" }, { "alpha", "zeta", "omega" } }) do
        local ok, err = pcall(GameWraps.newSet, {
            owner = "stub",
            giveUp = "Stub disabled: %s.",
            order = order,
            functions = {
                alpha = { class = "StubClass", parameters = 1, describe = "(object)" },
                zeta = { class = "StubClass", parameters = 1, describe = "(object)" },
            },
        })
        expectEqual(ok, false, "declaring the order " .. table.concat(order, ","))
        expectEqual(string.find(tostring(err), "stub", 1, true) ~= nil, true, "the message names the Subsystem: " .. tostring(err))
    end
end)

-- ---- Giving up once, without wraps ----------------------------------------------------------------------------------------

test("a Subsystem that wraps by itself gives up once with one warning, and again after a reset", function()
    local giveUp = GameWraps.newGiveUp("stub", "Stub disabled: %s. The game is unchanged.")
    expectEqual(giveUp:problem(), nil, "not given up")
    giveUp:giveUp("it broke")
    giveUp:giveUp("it broke again")
    expectEqual(#warnings(), 1, "warnings")
    expectEqual(warnings()[1], "[FS25_VR] [Warning] Stub disabled: it broke. The game is unchanged.", "the warning")
    expectEqual(giveUp:problem(), "it broke", "why")
    giveUp:reset()
    expectEqual(giveUp:problem(), nil, "forgotten at map unload")
    giveUp:giveUp("it broke on the next map")
    expectEqual(#warnings(), 2, "warned again on the next map")
end)

test("a give-up-once sentence without %s raises when it is made", function()
    local ok, err = pcall(GameWraps.newGiveUp, "stub", "Stub disabled.")
    expectEqual(ok, false, "making it")
    expectEqual(string.find(tostring(err), "stub", 1, true) ~= nil, true, "the message names the Subsystem: " .. tostring(err))
end)

realPrint(string.format("GameWraps: %d passed, %d failed", passed, failed))
