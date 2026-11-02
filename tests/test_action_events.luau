-- ====================================================================
-- Test Suite for the VR action events (spec #199, ticket #208): the six events (HUD Toggle, Recenter, the two
-- Snap Turns, VR View Toggle, Seat Set) are registered from one table, so a seventh is one more row. Drives
-- registerActionEvents through a mock g_inputBinding and asserts on what the engine would see: the registered
-- action, callback and triggers, the input context, the text priority, activity, visibility and translated
-- text, and the log line (one per event per map).
-- ====================================================================

_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

loadCompanionMod()

local passed = 0
local failed = 0

local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        passed = passed + 1
        print(string.format("  [PASS] %s", name))
    else
        failed = failed + 1
        print(string.format("  [FAIL] %s: %s", name, tostring(err)))
    end
end

-- The six events as the Controls screen and the input help know them
local SIX = {
    { action = "VR_HUD_TOGGLE", idField = "hudToggleActionEventId", handler = "onHudToggleAction", loggedField = "hudToggleLoggedOnce", fallback = "Toggle VR HUD Mode" },
    { action = "VR_RECENTER_VIEW", idField = "recenterActionEventId", handler = "onRecenterAction", loggedField = "recenterLoggedOnce", fallback = "Recenter VR View" },
    { action = "VR_SNAP_TURN_LEFT", idField = "snapLeftActionEventId", handler = "onSnapTurnLeftAction", loggedField = "snapLeftLoggedOnce", fallback = "VR Snap Turn Left" },
    { action = "VR_SNAP_TURN_RIGHT", idField = "snapRightActionEventId", handler = "onSnapTurnRightAction", loggedField = "snapRightLoggedOnce", fallback = "VR Snap Turn Right" },
    { action = "VR_TOGGLE_VIEW", idField = "toggleViewActionEventId", handler = "onToggleVrViewAction", loggedField = "toggleViewLoggedOnce", fallback = "Toggle VR / 2D View" },
    { action = "VR_SET_SEAT", idField = "seatSetActionEventId", handler = "onSeatSetAction", loggedField = "seatSetLoggedOnce", fallback = "Set VR Seat Position" },
}

local registered, priorities, active, visible, texts, contexts, logs
local nextEventId

local function installBinding()
    registered, priorities, active, visible, texts, contexts, logs = {}, {}, {}, {}, {}, {}, {}
    nextEventId = 700
    _G.g_inputBinding = {
        events = {},
        registerActionEvent = function(self, actionName, target, callback, triggerUp, triggerDown, triggerAlways, startActive)
            nextEventId = nextEventId + 1
            local event = { id = nextEventId, actionName = actionName, target = target, callback = callback,
                triggerUp = triggerUp, triggerDown = triggerDown, triggerAlways = triggerAlways, startActive = startActive }
            registered[actionName] = event
            self.events[event.id] = event
            return true, event.id
        end,
        beginActionEventsModification = function(self, contextName) contexts[#contexts + 1] = contextName end,
        endActionEventsModification = function() end,
        setActionEventTextPriority = function(self, id, priority) priorities[id] = priority end,
        setActionEventActive = function(self, id, isActive) active[id] = isActive end,
        setActionEventTextVisibility = function(self, id, isVisible) visible[id] = isVisible end,
        setActionEventText = function(self, id, text) texts[id] = text end,
    }
    _G.g_currentMission = nil
    VRCompanion.actionEventsContextName = nil
    for _, row in ipairs(SIX) do
        VRCompanion[row.idField] = nil
        VRCompanion[row.loggedField] = false
    end
end

local realPrint = print
local function captureLogs()
    print = function(...)
        local parts = {}
        for i = 1, select("#", ...) do parts[i] = tostring((select(i, ...))) end
        local line = table.concat(parts, "\t")
        logs[#logs + 1] = line
        realPrint(...)
    end
end
local function stopCapturingLogs() print = realPrint end

local function countLogs(needle)
    local n = 0
    for _, line in ipairs(logs) do
        if string.find(line, needle, 1, true) then n = n + 1 end
    end
    return n
end

local function cleanup()
    stopCapturingLogs()
    _G.g_inputBinding, _G.g_i18n, _G.GS_PRIO_HIGH = nil, nil, nil
end

test("AllSixEventsAreRegisteredWithTheirActionCallbackAndTriggers", function()
    installBinding()
    VRCompanion:registerActionEvents(true)
    for _, row in ipairs(SIX) do
        local event = registered[row.action]
        assert(event ~= nil, row.action .. " must be registered")
        assert(event.target == VRCompanion and event.callback == VRCompanion[row.handler], row.action .. " calls " .. row.handler)
        assert(event.triggerUp == false and event.triggerDown == true and event.triggerAlways == false and event.startActive == true,
            row.action .. " fires on a single press and starts active")
        assert(VRCompanion[row.idField] == event.id, row.idField .. " holds the registered id")
    end
    cleanup()
end)

test("EachEventIsInTheGameplayContextHighPriorityActiveAndVisibleWithItsTranslatedText", function()
    installBinding()
    _G.GS_PRIO_HIGH = 2
    _G.g_i18n = { getText = function(self, key) return "translated " .. key end }
    VRCompanion:registerActionEvents(true)
    assert(#contexts == 1 and contexts[1] == "PLAYER", "registered once into the on-foot context, got " .. tostring(contexts[1]))
    for _, row in ipairs(SIX) do
        local id = registered[row.action].id
        assert(priorities[id] == 2, row.action .. " has high text priority")
        assert(active[id] == true, row.action .. " is active")
        assert(visible[id] == true, row.action .. " has its text visible")
        assert(texts[id] == "translated input_" .. row.action, row.action .. " shows its translated text, got " .. tostring(texts[id]))
    end
    cleanup()
end)

test("WithoutTranslationsEachEventShowsItsFallbackText", function()
    installBinding()
    _G.g_i18n = nil
    VRCompanion:registerActionEvents(true)
    for _, row in ipairs(SIX) do
        local id = registered[row.action].id
        assert(texts[id] == row.fallback, row.action .. " falls back to " .. row.fallback .. ", got " .. tostring(texts[id]))
    end
    cleanup()
end)

test("EachEventIsLoggedOncePerMapHoweverOftenItIsRegisteredAgain", function()
    installBinding()
    captureLogs()
    VRCompanion:registerActionEvents(true)
    VRCompanion:registerActionEvents(true)
    for _, row in ipairs(SIX) do
        local n = countLogs("Registered in-engine " .. row.action .. " event.")
        assert(n == 1, row.action .. " logged once, got " .. n)
    end
    -- A new map logs them again
    VRCompanion:deleteMap()
    installBinding()
    captureLogs()
    VRCompanion:registerActionEvents(true)
    for _, row in ipairs(SIX) do
        local n = countLogs("Registered in-engine " .. row.action .. " event.")
        assert(n == 1, row.action .. " logged again on the next map, got " .. n)
    end
    cleanup()
end)

test("OnlyTheEventsTheEngineDroppedAreRegisteredAgain", function()
    installBinding()
    VRCompanion:registerActionEvents(true)
    local before = nextEventId
    VRCompanion:registerActionEvents(false)
    assert(nextEventId == before, "nothing is registered again while all six are in place")
    local dropped = VRCompanion.recenterActionEventId
    _G.g_inputBinding.events[dropped] = nil
    VRCompanion:registerActionEvents(false)
    assert(nextEventId == before + 1, "only the dropped one is registered again, got " .. (nextEventId - before))
    assert(VRCompanion.recenterActionEventId ~= dropped and active[VRCompanion.recenterActionEventId] == true, "it has a fresh id")
    cleanup()
end)

-- The point of the table: a seventh event is one more row
test("ASeventhRowIsRegisteredLoggedOnceAndForgottenOnMapUnloadWithoutOtherChanges", function()
    local rows = VRCompanion.ACTION_EVENTS
    assert(type(rows) == "table" and #rows == 6, "the six events are described by one table of six rows")
    installBinding()
    captureLogs()
    _G.g_i18n = { getText = function(self, key) return key == "input_VR_TEST_SEVENTH" and "Seventh" or key end }
    VRCompanion.onTestSeventhAction = function() end
    rows[#rows + 1] = {
        idField = "testSeventhActionEventId",
        action = "VR_TEST_SEVENTH",
        handler = "onTestSeventhAction",
        textKey = "input_VR_TEST_SEVENTH",
        fallbackText = "Test Seventh",
        loggedField = "testSeventhLoggedOnce",
    }
    local ok, err = pcall(function()
        VRCompanion:registerActionEvents(true)
        local event = registered["VR_TEST_SEVENTH"]
        assert(event ~= nil, "the seventh row is registered")
        assert(event.callback == VRCompanion.onTestSeventhAction and event.triggerDown == true, "with its handler on a single press")
        assert(VRCompanion.testSeventhActionEventId == event.id, "its id is kept")
        assert(texts[event.id] == "Seventh" and active[event.id] == true and visible[event.id] == true, "active, visible, with its text")
        assert(countLogs("Registered in-engine VR_TEST_SEVENTH event.") == 1, "logged once")

        -- The periodic check sees it in place, and puts it back when the engine drops it
        local before = nextEventId
        VRCompanion:registerActionEvents(false)
        assert(nextEventId == before, "nothing is registered again while it is in place")
        _G.g_inputBinding.events[event.id] = nil
        VRCompanion:registerActionEvents(false)
        assert(registered["VR_TEST_SEVENTH"].id ~= event.id, "a dropped seventh event is registered again")
        assert(countLogs("Registered in-engine VR_TEST_SEVENTH event.") == 1, "and not logged again")

        VRCompanion:deleteMap()
        assert(VRCompanion.testSeventhActionEventId == nil, "map unload forgets its id")
        assert(not VRCompanion.testSeventhLoggedOnce, "and that it was logged")
    end)
    rows[#rows] = nil
    VRCompanion.onTestSeventhAction = nil
    cleanup()
    assert(ok, err)
end)

-- The game ties an action event to its action's bindings when the event is registered, so a binding the player changes in
-- the Controls screen (Snap Turn taken off the right stick for smooth look) reaches the VR events only once they are
-- registered again. They listen for the game's INPUT_BINDINGS_CHANGED, as the on-foot player and an entered vehicle do.
local subscriptions

local function installMessageCenter()
    subscriptions = {}
    _G.MessageType = { INPUT_BINDINGS_CHANGED = 77 }
    _G.g_messageCenter = {
        subscribe = function(self, messageType, callback, target)
            subscriptions[#subscriptions + 1] = { messageType = messageType, callback = callback, target = target }
        end,
        unsubscribe = function(self, messageType, target)
            for i = #subscriptions, 1, -1 do
                if subscriptions[i].messageType == messageType and subscriptions[i].target == target then
                    table.remove(subscriptions, i)
                end
            end
        end,
    }
end

local function publish(messageType)
    for _, subscription in ipairs(subscriptions) do
        if subscription.messageType == messageType then subscription.callback(subscription.target) end
    end
end

local function cleanupMessageCenter()
    VRCompanion:unsubscribeFromInputBindingChanges()
    _G.g_messageCenter, _G.MessageType = nil, nil
end

test("TheMapListensForBindingChangesUntilItIsUnloaded", function()
    installBinding()
    installMessageCenter()
    VRCompanion:loadMap("mapUS")
    assert(#subscriptions == 1, "one subscription while the map is loaded, got " .. #subscriptions)
    assert(subscriptions[1].messageType == 77 and subscriptions[1].target == VRCompanion
        and subscriptions[1].callback == VRCompanion.onInputBindingsChanged, "to INPUT_BINDINGS_CHANGED, answered by the VR events")
    VRCompanion:deleteMap()
    assert(#subscriptions == 0, "map unload unsubscribes, so the next map does not listen twice")
    cleanupMessageCenter()
    cleanup()
end)

test("AChangedBindingRegistersEveryVrEventAgainAtTheNextUpdate", function()
    installBinding()
    installMessageCenter()
    local removed = {}
    _G.g_inputBinding.removeActionEvent = function(self, id)
        removed[id] = true
        self.events[id] = nil
    end
    VRCompanion:subscribeToInputBindingChanges()
    VRCompanion:registerActionEvents(true)
    local old = {}
    for _, row in ipairs(SIX) do old[row.action] = VRCompanion[row.idField] end

    -- Off the periodic check's tick: nothing is registered while every event is in place and no binding changed
    VRCompanion.frameTicks = 0
    VRCompanion:update(16.6)
    local before = nextEventId
    assert(VRCompanion.frameTicks % 30 ~= 0, "the update is off the periodic tick")

    captureLogs()
    publish(77)
    publish(77)
    stopCapturingLogs()
    assert(nextEventId == before, "the change is acted on at the next update, not inside the message")
    assert(countLogs("Input bindings changed: the VR action events are registered again.") == 1,
        "the game log says so once for changes that come before the update")
    VRCompanion:update(16.6)
    assert(nextEventId == before + #SIX, "all six are registered again, got " .. (nextEventId - before))
    for _, row in ipairs(SIX) do
        local id = VRCompanion[row.idField]
        assert(id ~= old[row.action] and registered[row.action].id == id, row.action .. " has a fresh event")
        assert(removed[old[row.action]] == true, row.action .. "'s old event, tied to the old bindings, is removed")
    end

    VRCompanion:update(16.6)
    assert(nextEventId == before + #SIX, "once: the next update registers nothing again")
    cleanupMessageCenter()
    cleanup()
end)

print(string.format("Action Event Tests: %d passed, %d failed", passed, failed))
if failed > 0 then
    error(string.format("%d Action Event test(s) failed", failed))
end
