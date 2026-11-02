-- ====================================================================
-- Test Suite: the Companion Mod allocates nothing per update in the Motion Controller poll
-- and in the GUI visibility poll (the Stereo Dialog check), so the garbage collector adds no
-- frame-time spikes.
-- Seen from outside: the memory the Luau state holds across many updates of the public functions
-- (VRCompanion:updateMotionControllers, VRCompanion:pollGuiVisibility) against a stubbed VRBridge,
-- g_gui and g_inputBinding. A table or closure made per update would add to the count.
-- ====================================================================

_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

-- Both Motion Controllers tracked and moving: each read puts the hand somewhere new, filling the table the mod hands
-- over in place the way the native binding does once the table holds every field.
local HAND_FIELDS = { "tracked", "x", "y", "z", "pitch", "yaw", "roll", "qx", "qy", "qz", "qw",
    "isValid", "isPositionTracked", "isOrientationTracked", "gripPoseTracked",
    "gripPoseX", "gripPoseY", "gripPoseZ", "gripPosePitch", "gripPoseYaw", "gripPoseRoll",
    "gripPoseQx", "gripPoseQy", "gripPoseQz", "gripPoseQw",
    "trigger", "grip", "thumbX", "thumbY", "thumbClick", "buttonLower", "buttonUpper", "menu" }
local function hasAllFields(t)
    if type(t) ~= "table" then return false end
    for _, k in ipairs(HAND_FIELDS) do
        if t[k] == nil then return false end
    end
    return true
end
local reads = 0
_G.VRBridge = {
    version = "1.0.0.0",
    isReady = function() return true end,
    handshake = function() return { success = true, version = "1.0.0.0", mode = "OpenXR" } end,
    getHeadPose = function(out)
        return { isValid = true, x = 0.1, y = 0.05, z = -0.1, pitch = 0.15, yaw = 0.2, roll = 0.0 }
    end,
    getEyeProjection = function() return { isValid = true, fovY = 1.8326, xOffset = 0.0, yOffset = 0.0 } end,
    getHandState = function(hand, out)
        if not hasAllFields(out) then
            out = { gripPoseTracked = true, thumbClick = false, buttonLower = false, buttonUpper = false, menu = false,
                    grip = 0, thumbX = 0, thumbY = 0, gripPoseX = 0, gripPoseY = 0, gripPoseZ = 0,
                    gripPosePitch = 0, gripPoseYaw = 0, gripPoseRoll = 0,
                    gripPoseQx = 0, gripPoseQy = 0, gripPoseQz = 0, gripPoseQw = 1,
                    pitch = 0, yaw = 0, roll = 0 }
        end
        reads = reads + 1
        local t = reads * 0.01  -- far enough every update to count as moved
        out.tracked = true
        out.isValid, out.isPositionTracked, out.isOrientationTracked = true, true, true
        out.x, out.y, out.z = 0.2 * (hand + 1) + t, 1.0, -0.4
        out.qx, out.qy, out.qz, out.qw = 0.0, 0.0, 0.0, 1.0
        out.trigger = 0
        return out
    end,
    getFrameParity = function() return 0 end,
    getIpd = function() return 0.064 end,
    setMenuVisible = function() end,
    alignMenuQuad = function() end,
    recenter = function() end,
    resetTemporalHistory = function() end,
    onMapUnload = function() end,
}
VRBridge = _G.VRBridge

-- The desk mouse moves too, so that the sampled position is stored on every update
local mouseX = 0.5
_G.g_inputBinding = {
    getMousePosition = function(self)
        mouseX = mouseX + 0.0001
        return mouseX, 0.5
    end,
}

-- The game's GUI as Gui.lua keeps it: a screen sets currentGuiName, a dialog is an element in dialogs
local function newGui()
    return {
        currentGuiName = "",
        currentDialogName = "",
        dialogs = {},
        getIsGuiVisible = function(self) return self.currentGuiName ~= "" or #self.dialogs > 0 end,
    }
end
_G.g_gui = newGui()

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
    g_gui = newGui()
    _G.g_gui = g_gui
end

-- The game in the headset: the stereoscopic view, head tracking on, Motion Controllers on
VRCompanion.handshakeComplete = true
VRCompanion.passThroughMode = false
VRCompanion.headTrackingEnabled = true
VRCompanion.motionControllersEnabled = true

-- Bytes the Luau state grows by across `updates` calls of fn, after a warm-up that lets lazily made state settle.
-- One table is 56 bytes or more, so a table made per update shows as at least 56 * updates.
local function bytesAllocatedOver(updates, fn)
    for _ = 1, 20 do fn() end
    collectgarbage("collect")
    collectgarbage("stop")  -- nothing is freed while counting, so every allocation shows
    local before = collectgarbage("count") * 1024
    for _ = 1, updates do fn() end
    local after = collectgarbage("count") * 1024
    collectgarbage("restart")
    return after - before
end

local UPDATES = 300
-- Slack for the odd string or number the engine of the test makes; a table or closure per update is 300 times more
local SLACK_BYTES = 512

print("Running Per-Update Allocation Tests...")

-- The poll hands the polled hands on to the subsystems that act on them (Wheel Grab, menus, ...). Their own updates are
-- their own tickets' concern: stood in for by nothing, so that this test measures the poll itself.
local DOWNSTREAM_UPDATES = { "updateVirtualGamepad", "updateWheelGrab", "updateHandTools", "updateObjectGrab",
    "updateInteractiveControl", "updateAnimatedObjects", "updateMenuPointer", "updateVirtualHands",
    "updateVirtualGamepadUse" }

test("TheMotionControllerPollAllocatesNothingWhileBothHandsMove", function()
    local originals = {}
    for _, name in ipairs(DOWNSTREAM_UPDATES) do
        originals[name] = VRCompanion[name]
        VRCompanion[name] = function() end
    end
    local moved = 0
    local ok, bytes = pcall(bytesAllocatedOver, UPDATES, function()
        VRCompanion:updateMotionControllers(16.6)
        if VRCompanion:isHandInUse(0) and VRCompanion:isHandInUse(1) then moved = moved + 1 end
    end)
    for name, original in pairs(originals) do VRCompanion[name] = original end
    assert(ok, tostring(bytes))
    assert(moved >= UPDATES, "both hands must count as moving (in use) every update, got " .. moved)
    assert(bytes <= SLACK_BYTES,
        string.format("the Motion Controller poll allocated %d bytes over %d updates", bytes, UPDATES))
end)

test("TheGuiVisibilityPollAllocatesNothingWhileAMenuIsOpen", function()
    g_gui.currentGuiName = "ingameMenu"
    VRCompanion:pollGuiVisibility()
    assert(VRCompanion:isMenuQuadShown() == true, "the menu shows as the quad")
    local bytes = bytesAllocatedOver(UPDATES, function() VRCompanion:pollGuiVisibility() end)
    assert(bytes <= SLACK_BYTES,
        string.format("the GUI visibility poll allocated %d bytes over %d polls with a menu open", bytes, UPDATES))
end)

test("TheGuiVisibilityPollAllocatesNothingWhileAStereoDialogShows", function()
    local root = { target = {}, elements = {} }
    table.insert(g_gui.dialogs, root)
    g_gui.currentDialogName = "FishDialog"
    VRCompanion:pollGuiVisibility()
    assert(VRCompanion:isStereoDialogShowing() == true, "the fish catch dialog is a Stereo Dialog")
    assert(VRCompanion:findStereoDialog() == root, "the dialog root is found")
    local bytes = bytesAllocatedOver(UPDATES, function() VRCompanion:pollGuiVisibility() end)
    assert(bytes <= SLACK_BYTES,
        string.format("the GUI visibility poll allocated %d bytes over %d polls with a Stereo Dialog showing", bytes, UPDATES))
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_per_update_allocations.luau", failed))
