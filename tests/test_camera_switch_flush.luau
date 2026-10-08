-- ====================================================================
-- Test Suite: Discontinuity Flush on every vehicle camera switch (Issue #45)
-- ====================================================================
-- Drives the Companion Mod through the engine's vehicle camera change hook
-- (Enterable.setActiveCameraIndex, wrapped by hookEnterable) and asserts on the
-- discontinuity signals VRBridge receives.

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

print("Running Camera Switch Discontinuity Flush Tests...")

-- A vehicle with one cab camera and two Outside Cameras (an orbit camera and a fixed hitch camera).
local function makeVehicle()
    local veh = {
        spec_enterable = {
            cameras = {
                { isInside = true, isRotatable = true, rotY = 0.3, rotX = 0.1, cameraNode = 101 },
                { isInside = false, isRotatable = true, rotY = 1.0, rotX = 0.4, cameraNode = 102 },
                { isInside = false, isRotatable = false, rotY = 0.0, rotX = 0.6, cameraNode = 103 },
            },
            camIndex = 1,
        },
    }
    veh.spec_enterable.activeCamera = veh.spec_enterable.cameras[1]
    return veh
end

-- Installs a fake Enterable whose setActiveCameraIndex switches the vehicle's active camera like the
-- engine does, hooks it through the Companion Mod, and makes `veh` the controlled vehicle.
-- Returns the signal log and a cleanup function.
local function setUpScene(veh)
    local signals = { discontinuity = 0, resetTemporalHistory = 0 }
    _G.VRBridge = {
        isReady = function() return true end,
        signalDiscontinuity = function() signals.discontinuity = signals.discontinuity + 1 end,
        resetTemporalHistory = function() signals.resetTemporalHistory = signals.resetTemporalHistory + 1 end,
    }
    _G.Enterable = {
        setActiveCameraIndex = function(v, index)
            v.spec_enterable.camIndex = index
            v.spec_enterable.activeCamera = v.spec_enterable.cameras[index]
        end,
    }
    _G.g_localPlayer = {
        getCurrentVehicle = function() return veh end,
    }
    VRCompanion.enterableHooked = false
    VRCompanion:hookEnterable()

    local function cleanup()
        _G.Enterable = nil
        _G.g_localPlayer = nil
        _G.VRBridge = nil
        VRCompanion.enterableHooked = false
        VRCompanion.origEnterableSetActiveCameraIndex = nil
        VRCompanion.vehicleRotationStates = {}
    end
    return signals, cleanup
end

local function totalSignals(signals)
    return signals.discontinuity + signals.resetTemporalHistory
end

test("EveryVehicleCameraSwitchSignalsExactlyOneDiscontinuity", function()
    local veh = makeVehicle()
    local signals, cleanup = setUpScene(veh)

    local steps = {
        { index = 2, label = "cab -> orbit Outside Camera" },
        { index = 3, label = "orbit -> fixed Outside Camera" },
        { index = 2, label = "fixed -> orbit Outside Camera" },
        { index = 1, label = "Outside Camera -> cab" },
        { index = 3, label = "cab -> fixed Outside Camera" },
        { index = 1, label = "fixed Outside Camera -> cab" },
    }
    for _, step in ipairs(steps) do
        local before = totalSignals(signals)
        Enterable.setActiveCameraIndex(veh, step.index)
        local sent = totalSignals(signals) - before
        if sent ~= 1 then
            cleanup()
            error(string.format("%s: expected exactly 1 discontinuity signal, got %d", step.label, sent))
        end
    end

    cleanup()
end)

test("CabToCabSwitchSignalsDiscontinuity", function()
    local veh = makeVehicle()
    -- A second inside camera (e.g. a passenger-seat view).
    table.insert(veh.spec_enterable.cameras, { isInside = true, isRotatable = true, rotY = -0.2, rotX = 0.0, cameraNode = 104 })
    local signals, cleanup = setUpScene(veh)

    Enterable.setActiveCameraIndex(veh, 4)
    local sent = totalSignals(signals)

    cleanup()
    assert(sent == 1, string.format("cab -> cab: expected exactly 1 discontinuity signal, got %d", sent))
end)

test("SwitchOnAVehicleOtherThanTheControlledOneDoesNotSignal", function()
    local controlled = makeVehicle()
    local other = makeVehicle()
    local signals, cleanup = setUpScene(controlled)

    Enterable.setActiveCameraIndex(other, 2)
    local sent = totalSignals(signals)

    cleanup()
    assert(sent == 0, string.format("Camera switch on a non-controlled vehicle must not flush, got %d signals", sent))
end)

test("SwitchingBackToTheCabStillRestoresThePerVehicleLookYaw", function()
    local veh = makeVehicle()
    local signals, cleanup = setUpScene(veh)

    VRCompanion.vehicleRotationStates = VRCompanion.vehicleRotationStates or {}
    VRCompanion.vehicleRotationStates[veh] = { rotY = 0.9, rotX = -0.05 }

    Enterable.setActiveCameraIndex(veh, 2)
    local orbit = veh.spec_enterable.cameras[2]
    local orbitRotY, orbitRotX = orbit.rotY, orbit.rotX

    Enterable.setActiveCameraIndex(veh, 1)
    local cab = veh.spec_enterable.cameras[1]
    local cabRotY, cabRotX = cab.rotY, cab.rotX

    cleanup()
    assert(math.abs(cabRotY - 0.9) < 1e-6, string.format("Cab look yaw must be restored to 0.9, got %f", cabRotY))
    assert(math.abs(cabRotX - (-0.05)) < 1e-6, string.format("Cab look pitch must be restored to -0.05, got %f", cabRotX))
    assert(orbitRotY == 1.0 and orbitRotX == 0.4, "Outside Camera orbit rotation must not be touched by the camera switch")
end)

print(string.format("\nCamera Switch Flush Tests: %d passed, %d failed", passed, failed))
if failed > 0 then
    error(string.format("%d camera switch flush test(s) failed", failed))
end
