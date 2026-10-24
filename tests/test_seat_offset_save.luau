-- ====================================================================
-- Test Suite for saving the Seat Offsets (spec #166, ticket #169): each vehicle model's in-cab cameras
-- come back with the Seat Offset the player gave them, in the next session. Drives the whole Companion
-- Mod through what the engine calls (saveConfiguration, loadConfiguration, the in-cab camera update and
-- the vrSeatOffset console command) over a mock scene graph and a mock XML store, and asserts on what the
-- engine would see: the keys written to settings.xml, and where the head is placed after a load.
-- Settings schema: vrSettings.seatOffsets.seatOffset(i) with #vehicle, #camera, #forward, #up, #right.
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

local function near(a, b) return math.abs(a - b) < 0.0005 end

-- --------------------------------------------------------------------
-- Mock scene (see test_seat_offset.luau): nodes with a parent, a local translation and a yaw
-- --------------------------------------------------------------------
local CAB = 999
local nodes

local function nodeOf(id)
    local n = nodes[id]
    if n == nil then
        n = { t = { 0, 0, 0 }, yaw = 0, parent = CAB }
        nodes[id] = n
    end
    return n
end

local function installScene()
    local nextNode = 7000
    nodes = {}
    _G.setTranslation = function(id, x, y, z) nodeOf(id).t = { x, y, z } end
    _G.getTranslation = function(id) local t = nodeOf(id).t return t[1], t[2], t[3] end
    _G.setRotation = function(id, rx, ry, rz) nodeOf(id).yaw = ry end
    _G.getRotation = function(id) return 0, nodeOf(id).yaw, 0 end
    _G.getParent = function(id) return nodeOf(id).parent end
    _G.link = function(parent, id) nodeOf(id).parent = parent end
    _G.createTransformGroup = function(name)
        nextNode = nextNode + 1
        nodeOf(nextNode)
        return nextNode
    end
    _G.localToLocal = function(from, to, x, y, z)
        local n = nodeOf(from)
        local rx, ry, rz = VRCompanion:rotateVectorByYaw(x, y, z, n.yaw)
        return n.t[1] + rx, n.t[2] + ry, n.t[3] + rz
    end
    _G.localRotationToLocal = function(from, to, rx, ry, rz) return rx, ry + nodeOf(from).yaw, rz end
end

local function installBridge()
    _G.VRBridge = {
        isReady = function() return true end,
        getFrameParity = function() return 0 end,
        getRequiresAfrFallback = function() return false end,
        getHeadPose = function() return { isValid = true, x = 0, y = 0, z = 0, pitch = 0, yaw = 0, roll = 0 } end,
        recenter = function() return { success = true } end,
        signalDiscontinuity = function() end,
        resetTemporalHistory = function() end,
    }
end

local function resetCompanion()
    VRCompanion.cameraNodeLeft = nil
    VRCompanion.cameraNodeRight = nil
    VRCompanion.cyclopeanAudioListenerNode = nil
    VRCompanion.activeRenderingCamera = nil
    VRCompanion.registeredEyeCameras = nil
    VRCompanion.gameCameraNode = nil
    VRCompanion.origCamSettings = {}
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false
    VRCompanion.menuTakesInput = false
    VRCompanion.recenterVrTrackingSpace = true
    VRCompanion.headPoseScratch = nil
    VRCompanion.vehicleRotationStates = {}
    VRCompanion.lastControlledVehicle = nil
end

local function makeVehicle(config, cams, index)
    local veh = { configFileName = config, spec_enterable = { cameras = cams, camIndex = index or 1 } }
    veh.getActiveCamera = function(self) return self.spec_enterable.cameras[self.spec_enterable.camIndex] end
    for _, cam in ipairs(cams) do cam.vehicle = veh end
    return veh
end

local nextCameraNode = 100
local nextHeadNode = 500
-- An in-cab camera with a head tracking node at (0, 1.6, 0.2), looking out of the windscreen
local function trackedCab()
    nextCameraNode = nextCameraNode + 1
    nextHeadNode = nextHeadNode + 1
    nodeOf(nextCameraNode).t = { 0, 1.6, 0.2 }
    nodeOf(nextHeadNode).t = { 0, 1.6, 0.2 }
    nodeOf(nextHeadNode).yaw = math.pi
    return { isInside = true, cameraNode = nextCameraNode, headTrackingNode = nextHeadNode, origRotY = math.pi, rotY = math.pi, origRotX = 0, rotX = 0 }
end

local function setup()
    installScene()
    installBridge()
    resetCompanion()
    _G.g_currentMission = nil
end

local function enter(veh)
    _G.g_currentMission = { controlledVehicle = veh }
    VRCompanion:onEnterVehicle(veh, true, nil)
end

local function translationOf(id) local t = nodeOf(id).t return { x = t[1], y = t[2], z = t[3] } end

-- Where the head-tracked view puts the engine camera, the eye camera and the audio listener
local function viewAfterUpdate(cam)
    VRCompanion:onVehicleCameraUpdate(cam, 16.6)
    return {
        engine = translationOf(cam.cameraNode),
        eye = translationOf(VRCompanion.cameraNodeLeft),
        listener = translationOf(VRCompanion.cyclopeanAudioListenerNode),
    }
end

local function assertMoved(view, base, dx, dy, dz, label)
    for _, place in ipairs({ "engine", "eye", "listener" }) do
        local v, b = view[place], base[place]
        assert(near(v.x - b.x, dx) and near(v.y - b.y, dy) and near(v.z - b.z, dz), string.format(
            "%s: the %s moved by (%.3f, %.3f, %.3f), expected (%.3f, %.3f, %.3f)",
            label, place, v.x - b.x, v.y - b.y, v.z - b.z, dx, dy, dz))
    end
end

local modelCount = 0
local function freshModel()
    modelCount = modelCount + 1
    return string.format("data/vehicles/make/saved%d.xml", modelCount)
end

-- --------------------------------------------------------------------
-- Mock XML: saving writes into `written` (key -> value) and counts saves; loading reads from a store
-- (key -> value). An element exists when any key starts with its path (an attribute is "path#name").
-- --------------------------------------------------------------------
local written, saveCount

local function installXmlWriter()
    written, saveCount = {}, 0
    _G.getUserProfileAppPath = function() return "C:/profile/" end
    _G.createXMLFile = function() return 777 end
    _G.setXMLFloat = function(_, key, value) written[key] = value end
    _G.setXMLBool = function(_, key, value) written[key] = value end
    _G.setXMLString = function(_, key, value) written[key] = value end
    _G.setXMLInt = function(_, key, value) written[key] = value end
    _G.saveXMLFile = function() saveCount = saveCount + 1 end
    _G.delete = function() end
end

local function installXmlReader(store)
    local function hasKey(key)
        if store[key] ~= nil then return true end
        for k in pairs(store) do
            if string.sub(k, 1, #key + 1) == key .. "#" or string.sub(k, 1, #key + 1) == key .. "." then return true end
        end
        return false
    end
    _G.loadXMLFile = function() return 888 end
    _G.hasXMLProperty = function(_, key) return hasKey(key) end
    _G.getXMLFloat = function(_, key) return store[key] end
    _G.getXMLBool = function(_, key) return nil end
    _G.getXMLString = function(_, key) return store[key] end
    _G.getXMLInt = function(_, key) return store[key] end
end

local function removeXmlMocks()
    for _, name in ipairs({ "getUserProfileAppPath", "createXMLFile", "setXMLFloat", "setXMLBool", "setXMLString", "setXMLInt", "saveXMLFile",
        "delete", "loadXMLFile", "hasXMLProperty", "getXMLFloat", "getXMLBool", "getXMLString", "getXMLInt" }) do
        _G[name] = nil
    end
end

local function copyOf(t)
    local c = {}
    for k, v in pairs(t) do c[k] = v end
    return c
end

-- The settings.xml entries written for the Seat Offsets, as { vehicle, camera, forward, up, right } rows, in file order
local function seatOffsetEntries()
    local rows = {}
    local i = 0
    while written[string.format("vrSettings.seatOffsets.seatOffset(%d)#vehicle", i)] ~= nil do
        local base = string.format("vrSettings.seatOffsets.seatOffset(%d)", i)
        rows[#rows + 1] = {
            vehicle = written[base .. "#vehicle"], camera = written[base .. "#camera"],
            forward = written[base .. "#forward"], up = written[base .. "#up"], right = written[base .. "#right"],
        }
        i = i + 1
    end
    return rows
end

local function findEntry(rows, vehicle, camera)
    for _, row in ipairs(rows) do
        if row.vehicle == vehicle and row.camera == camera then return row end
    end
    return nil
end

local function entryKey(i, name) return string.format("vrSettings.seatOffsets.seatOffset(%d)#%s", i, name) end

print("Running Seat Offset Save Tests...")

-- Slice 1: saving writes one entry per non-zero Seat Offset
test("SavingWritesOneEntryPerNonZeroSeatOffsetWithVehicleCameraAndAxes", function()
    setup()
    installXmlWriter()
    local driver, passenger = trackedCab(), trackedCab()
    local modelA, modelB = freshModel(), freshModel()
    local vehA = makeVehicle(modelA, { driver, passenger })
    local camB = trackedCab()
    local vehB = makeVehicle(modelB, { camB })
    enter(vehA)
    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    vehA.spec_enterable.camIndex = 2
    VRCompanion:consoleCommandSeatOffset("-0.4", "0", "0.25")
    enter(vehB)
    VRCompanion:consoleCommandSeatOffset("0", "-0.5", "0")

    written = {}
    assert(VRCompanion:saveConfiguration() == true, "the settings are saved")
    local rows = seatOffsetEntries()
    removeXmlMocks()

    assert(#rows == 3, "one entry per non-zero Seat Offset, got " .. #rows)
    local a1, a2, b1 = findEntry(rows, modelA, 1), findEntry(rows, modelA, 2), findEntry(rows, modelB, 1)
    assert(a1 and near(a1.forward, 0.1) and near(a1.up, 0.2) and near(a1.right, 0.3), "model A's driver camera")
    assert(a2 and near(a2.forward, -0.4) and near(a2.up, 0) and near(a2.right, 0.25), "model A's passenger camera")
    assert(b1 and near(b1.forward, 0) and near(b1.up, -0.5) and near(b1.right, 0), "model B's camera")
end)

test("AnOffsetResetToZeroIsNotWritten", function()
    setup()
    installXmlWriter()
    local cam = trackedCab()
    local model = freshModel()
    enter(makeVehicle(model, { cam }))
    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    VRCompanion:consoleCommandSeatOffset("reset")

    written = {}
    VRCompanion:saveConfiguration()
    local rows = seatOffsetEntries()
    removeXmlMocks()
    assert(findEntry(rows, model, 1) == nil, "a reset offset has no entry")
end)

-- Slice 2: a vrSeatOffset change saves the settings at once
test("ASeatOffsetSetByConsoleIsSavedAtOnce", function()
    setup()
    installXmlWriter()
    local cam = trackedCab()
    local model = freshModel()
    enter(makeVehicle(model, { cam }))
    local before = saveCount

    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    local rows = seatOffsetEntries()
    local savesAfterSet = saveCount
    removeXmlMocks()

    assert(savesAfterSet == before + 1, "the settings are saved by the change, saves: " .. (savesAfterSet - before))
    local row = findEntry(rows, model, 1)
    assert(row and near(row.forward, 0.1) and near(row.up, 0.2) and near(row.right, 0.3), "the new offset is in the file")
end)

test("AResetByConsoleIsSavedAtOnceAndDropsTheEntry", function()
    setup()
    installXmlWriter()
    local cam = trackedCab()
    local model = freshModel()
    enter(makeVehicle(model, { cam }))
    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    local before = saveCount

    written = {}
    VRCompanion:consoleCommandSeatOffset("reset")
    local rows = seatOffsetEntries()
    local savesAfterReset = saveCount
    removeXmlMocks()

    assert(savesAfterReset == before + 1, "the reset is saved at once")
    assert(findEntry(rows, model, 1) == nil, "the reset offset is gone from the file")
end)

test("PrintingAndRejectedConsoleInputSaveNothing", function()
    setup()
    installXmlWriter()
    local cam = trackedCab()
    enter(makeVehicle(freshModel(), { cam }))
    local before = saveCount
    VRCompanion:consoleCommandSeatOffset()
    VRCompanion:consoleCommandSeatOffset("nonsense")
    VRCompanion:consoleCommandSeatOffset("a", "b", "c")
    _G.g_currentMission = { controlledVehicle = nil }
    VRCompanion:consoleCommandSeatOffset("0.1", "0.1", "0.1")  -- not in a cab
    local saves = saveCount - before
    removeXmlMocks()
    assert(saves == 0, "nothing changed, nothing saved; saves: " .. saves)
end)

-- Slice 3: after a save and a load, each offset comes back to its own camera
test("SavedSeatOffsetsComeBackForEachModelAndForDriverAndPassengerCameras", function()
    setup()
    installXmlWriter()
    local driver, passenger, camB = trackedCab(), trackedCab(), trackedCab()
    local vehA = makeVehicle(freshModel(), { driver, passenger })
    local vehB = makeVehicle(freshModel(), { camB })
    enter(vehA)
    local baseDriver = viewAfterUpdate(driver)
    vehA.spec_enterable.camIndex = 2
    local basePassenger = viewAfterUpdate(passenger)
    enter(vehB)
    local baseB = viewAfterUpdate(camB)

    vehA.spec_enterable.camIndex = 1
    enter(vehA)
    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    vehA.spec_enterable.camIndex = 2
    VRCompanion:consoleCommandSeatOffset("-0.4", "0.05", "-0.25")
    enter(vehB)
    VRCompanion:consoleCommandSeatOffset("0.5", "-0.1", "0")
    local file = copyOf(written)

    -- The player tunes things further in a later session, then the saved file is read back
    VRCompanion:consoleCommandSeatOffset("0", "0", "0.4")
    vehA.spec_enterable.camIndex = 1
    enter(vehA)
    VRCompanion:consoleCommandSeatOffset("reset")
    installXmlReader(file)
    VRCompanion:loadConfiguration("settings.xml")
    removeXmlMocks()

    enter(vehA)
    assertMoved(viewAfterUpdate(driver), baseDriver, -0.3, 0.2, 0.1, "model A driver camera")
    vehA.spec_enterable.camIndex = 2
    assertMoved(viewAfterUpdate(passenger), basePassenger, 0.25, 0.05, -0.4, "model A passenger camera")
    enter(vehB)
    assertMoved(viewAfterUpdate(camB), baseB, 0, -0.1, 0.5, "model B camera")
end)

-- Slice 4: loaded values are clamped; malformed or incomplete entries are skipped without affecting the others
test("LoadedSeatOffsetsAreClampedToHalfAMetre", function()
    setup()
    local cam = trackedCab()
    local model = freshModel()
    local veh = makeVehicle(model, { cam })
    enter(veh)
    local base = viewAfterUpdate(cam)

    local store = {
        [entryKey(0, "vehicle")] = model, [entryKey(0, "camera")] = 1,
        [entryKey(0, "forward")] = 0.9, [entryKey(0, "up")] = -2.0, [entryKey(0, "right")] = 0.2,
    }
    installXmlReader(store)
    VRCompanion:loadConfiguration("settings.xml")
    removeXmlMocks()

    assertMoved(viewAfterUpdate(cam), base, -0.2, -0.5, 0.5, "clamped on load")
end)

test("MalformedOrIncompleteEntriesAreSkippedAndTheOthersStillLoad", function()
    setup()
    local camA, camC, camD, camB = trackedCab(), trackedCab(), trackedCab(), trackedCab()
    local modelA, modelC, modelD, modelB = freshModel(), freshModel(), freshModel(), freshModel()
    local vehA, vehC = makeVehicle(modelA, { camA }), makeVehicle(modelC, { camC })
    local vehD, vehB = makeVehicle(modelD, { camD }), makeVehicle(modelB, { camB })
    enter(vehA)
    local baseA = viewAfterUpdate(camA)
    enter(vehC)
    local baseC = viewAfterUpdate(camC)
    enter(vehD)
    local baseD = viewAfterUpdate(camD)
    enter(vehB)
    local baseB = viewAfterUpdate(camB)

    local store = {
        -- valid
        [entryKey(0, "vehicle")] = modelA, [entryKey(0, "camera")] = 1,
        [entryKey(0, "forward")] = 0.1, [entryKey(0, "up")] = 0.2, [entryKey(0, "right")] = 0.3,
        -- no vehicle
        [entryKey(1, "camera")] = 1, [entryKey(1, "forward")] = 0.3, [entryKey(1, "up")] = 0.3, [entryKey(1, "right")] = 0.3,
        -- no up axis
        [entryKey(2, "vehicle")] = modelC, [entryKey(2, "camera")] = 1, [entryKey(2, "forward")] = 0.4, [entryKey(2, "right")] = 0.4,
        -- camera 0 is not a camera index
        [entryKey(3, "vehicle")] = modelD, [entryKey(3, "camera")] = 0,
        [entryKey(3, "forward")] = 0.4, [entryKey(3, "up")] = 0.4, [entryKey(3, "right")] = 0.4,
        -- no camera
        [entryKey(4, "vehicle")] = modelD, [entryKey(4, "forward")] = 0.4, [entryKey(4, "up")] = 0.4, [entryKey(4, "right")] = 0.4,
        -- valid, after the broken ones
        [entryKey(5, "vehicle")] = modelB, [entryKey(5, "camera")] = 1,
        [entryKey(5, "forward")] = -0.1, [entryKey(5, "up")] = 0, [entryKey(5, "right")] = -0.3,
    }
    installXmlReader(store)
    VRCompanion:loadConfiguration("settings.xml")
    removeXmlMocks()

    enter(vehA)
    assertMoved(viewAfterUpdate(camA), baseA, -0.3, 0.2, 0.1, "the valid entry before the broken ones")
    enter(vehC)
    assertMoved(viewAfterUpdate(camC), baseC, 0, 0, 0, "the entry with no up axis")
    enter(vehD)
    assertMoved(viewAfterUpdate(camD), baseD, 0, 0, 0, "the entries with camera 0 and no camera")
    enter(vehB)
    assertMoved(viewAfterUpdate(camB), baseB, 0.3, 0, -0.1, "the valid entry after the broken ones")
end)

-- Slice 5: a settings file from before this feature loads with every Seat Offset at 0 0 0
test("ASettingsFileWithoutSeatOffsetsLoadsWithEveryOffsetAtZero", function()
    setup()
    local cam = trackedCab()
    local veh = makeVehicle(freshModel(), { cam })
    enter(veh)
    local base = viewAfterUpdate(cam)

    installXmlReader({ ["vrSettings.snapTurnAngle"] = 45.0 })
    VRCompanion:loadConfiguration("settings.xml")
    removeXmlMocks()

    assertMoved(viewAfterUpdate(cam), base, 0, 0, 0, "no offset saved")
end)

print(string.format("Seat Offset Save Tests: %d passed, %d failed", passed, failed))
if failed > 0 then
    error(string.format("%d Seat Offset Save test(s) failed", failed))
end
