
-- ====================================================================
-- Test Suite for Stereo Dialog depth (spec #142, ticket #146, ADR-0032)
-- A Stereo Dialog (the fish catch dialog) is drawn at HUD depth: while it shows in the stereoscopic view, its root is shifted by
-- the HUD Convergence Offset on alternating AFR eye frames, and moved back by the same amount after each draw.
-- Seen from outside: the Companion Mod's update tick runs against a stubbed g_gui and VRBridge, and the test draws the GUI the
-- way the engine does (Gui:draw, once per rendered frame) and reads where the dialog is when it is drawn, and where it is left.
-- ====================================================================

_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

-- The eye the stubbed engine renders next: the frame parity VRBridge reports
local parity = 0
local twoDActive = false
_G.VRBridge = {
    version = "1.0.0.0",
    isReady = function() return true end,
    handshake = function() return { success = true, version = "1.0.0.0", mode = "OpenXR" } end,
    getIs2DViewActive = function() return twoDActive end,
    set2DViewActive = function(active) twoDActive = (active == true) end,
    getHeadPose = function()
        return { isValid = true, x = 0.1, y = 0.05, z = -0.1, pitch = 0.15, yaw = 0.2, roll = 0.0 }
    end,
    getEyeProjection = function() return { isValid = true, fovY = 1.8326, xOffset = 0.0, yOffset = 0.0 } end,
    getFrameParity = function() return parity end,
    getIpd = function() return 0.064 end,
    setMenuVisible = function() end,
    alignMenuQuad = function() end,
    recenter = function() end,
    resetTemporalHistory = function() end,
    onMapUnload = function() end,
}
VRBridge = _G.VRBridge

-- The game's GUI, as Gui.lua keeps it and the headset probe of ticket #143 measured the fish dialog: the dialog is a full-screen
-- root element (position 0,0, size 1x1) in g_gui.dialogs, whose target is the controller with the visible box
-- (target.dialogElement). GuiElement:setAbsolutePosition moves an element and all its children by the same amount.
local DRAW_DIALOG_X = 0.414
local drawn = {}      -- per Gui:draw: where the root and the visible box were when they were drawn
local failNextDraw = false

local function newElement(x, y)
    local element = { absPosition = { x, y }, elements = {} }
    function element.setAbsolutePosition(self, newX, newY)
        local dx, dy = newX - self.absPosition[1], newY - self.absPosition[2]
        self.absPosition[1], self.absPosition[2] = newX, newY
        for _, child in ipairs(self.elements) do child:setAbsolutePosition(child.absPosition[1] + dx, child.absPosition[2] + dy) end
    end
    return element
end

_G.Gui = {}
function Gui.draw(self)
    for _, root in pairs(self.dialogs) do
        table.insert(drawn, { root = root.absPosition[1], box = root.target.dialogElement.absPosition[1] })
    end
    if failNextDraw then
        failNextDraw = false
        error("the dialog draw failed")
    end
end
local function newGui()
    return setmetatable({
        currentGuiName = "",
        currentDialogName = "",
        dialogs = {},
        getIsGuiVisible = function(self) return self.currentGuiName ~= "" or #self.dialogs > 0 end,
    }, { __index = Gui })
end
_G.g_gui = newGui()

local function showDialog(name, noMove)
    local root = newElement(0, 0)
    if noMove then root.setAbsolutePosition = nil end
    local box = newElement(DRAW_DIALOG_X, 0.391)
    table.insert(root.elements, box)
    root.target = { dialogElement = box }
    root.name = name
    table.insert(g_gui.dialogs, root)
    g_gui.currentDialogName = name
    return root
end
local function closeDialog()
    table.remove(g_gui.dialogs)
    g_gui.currentDialogName = ""
end

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
    closeDialog()
    g_gui = newGui()
    _G.g_gui = g_gui
    VRCompanion:set2DViewActive(false)
end

print("Running Stereo Dialog Depth Tests...")

-- The game in the headset: the stereoscopic view, head tracking on
VRCompanion.handshakeComplete = true
VRCompanion.passThroughMode = false
VRCompanion.headTrackingEnabled = true

-- Ticks the update until the HUD convergence has settled on its disparity
local function settle()
    for _ = 1, 40 do VRCompanion:update(16.6) end
end

-- One rendered frame of the eye with the given parity: the engine draws the GUI. Returns where the dialog was drawn.
local function renderFrame(eye)
    parity = eye
    drawn = {}
    g_gui:draw()
    return drawn[1]
end

local function near(a, b) return math.abs(a - b) < 0.0001 end

-- The disparity of a point at the given depth: ipd / depth * the HUD's parallax factor (0.064 m ipd in the stub)
local function disparityAt(depth) return 0.064 / depth * VRCompanion.CONVERGENCE_PARALLAX_FACTOR end

test("AStereoDialogIsDrawnShiftedAlternatingWithFrameParityToItsOwnDepthBehindTheHud", function()
    assert(VRCompanion.DEFAULT_STEREO_DIALOG_DEPTH == 2.5, "the dialog sits at 2.5 m by default, further back than the HUD's 1.8 m")
    local root = showDialog("FishDialog")
    settle()
    local want = disparityAt(2.5)
    local left = renderFrame(0)
    assert(left ~= nil, "the dialog is drawn")
    assert(near(left.root, want), string.format("left eye frame: root at +%f, got %f", want, left.root))
    assert(near(left.box, DRAW_DIALOG_X + want), string.format("the visible box moves with the root, got %f", left.box))
    local right = renderFrame(1)
    assert(near(right.root, -want), string.format("right eye frame: root at -%f, got %f", want, right.root))
    assert(near(right.box, DRAW_DIALOG_X - want), string.format("the visible box moves with the root, got %f", right.box))
    -- Less disparity than the HUD Groups get: the dialog reads further away than the HUD
    parity = 0
    VRCompanion:update(16.6)
    assert(math.abs(left.root) < math.abs(VRCompanion.currentConvergenceOffset), "further back than the HUD")
end)

test("VrDialogDepthSetsHowFarBackTheDialogReadsAndReportsAndRefusesBadValues", function()
    local root = showDialog("FishDialog")
    settle()
    local report = VRCompanion:consoleCommandSetStereoDialogDepth(nil)
    assert(string.find(report, "2.5", 1, true), "no argument reports the depth: " .. report)
    local set = VRCompanion:consoleCommandSetStereoDialogDepth("3.2")
    assert(string.find(set, "3.2", 1, true), "reports the new depth: " .. set)
    assert(near(renderFrame(0).root, disparityAt(3.2)), "the dialog is drawn at the new depth")
    for _, bad in ipairs({ "far", "0", "-1", "99", "0.1" }) do
        local refused = VRCompanion:consoleCommandSetStereoDialogDepth(bad)
        assert(string.find(refused, "Usage", 1, true), "refused with the usage: " .. bad)
    end
    assert(near(renderFrame(0).root, disparityAt(3.2)), "a refused value leaves the depth alone")
    VRCompanion:consoleCommandSetStereoDialogDepth("2.5")
end)

test("TheDialogDepthSurvivesASaveAndLoad", function()
    local store = {}
    VRCompanion:setStereoDialogDepth(3.0)
    VRCompanion:saveStereoDialogDepthSetting("file", { setFloat = function(_, key, value) store[key] = value end })
    assert(store["vrSettings.stereoDialogDepth"] == 3.0, "the depth is saved")
    VRCompanion:setStereoDialogDepth(VRCompanion.DEFAULT_STEREO_DIALOG_DEPTH)
    VRCompanion:loadStereoDialogDepthSetting("file", { getFloat = function(_, key) return store[key] end })
    assert(VRCompanion.stereoDialogDepth == 3.0, "the depth is loaded")
    VRCompanion:loadStereoDialogDepthSetting("file", { getFloat = function() return nil end })
    assert(VRCompanion.stereoDialogDepth == 3.0, "a missing key keeps the value")
    VRCompanion:loadStereoDialogDepthSetting("file", { getFloat = function() return 40.0 end })
    assert(VRCompanion.stereoDialogDepth == 3.0, "a value out of range keeps the value")
    VRCompanion:setStereoDialogDepth(VRCompanion.DEFAULT_STEREO_DIALOG_DEPTH)
end)

test("ADialogIsPutBackAfterEachDrawAndDoesNotDriftOverManyFrames", function()
    local root = showDialog("FishDialog")
    settle()
    for frame = 1, 2000 do
        if frame % 2 == 0 then VRCompanion:update(16.6) end
        renderFrame(frame % 2)
        assert(math.abs(root.absPosition[1]) < 1e-9, "root back at 0 after the draw " .. frame)
        assert(math.abs(root.target.dialogElement.absPosition[1] - DRAW_DIALOG_X) < 1e-9, "box back where it was after the draw " .. frame)
    end
end)

test("AFailingDrawStillPutsTheDialogBack", function()
    local root = showDialog("FishDialog")
    settle()
    failNextDraw = true
    local ok = pcall(function() g_gui:draw() end)
    assert(not ok, "the draw's error is not swallowed")
    assert(math.abs(root.absPosition[1]) < 1e-9 and math.abs(root.target.dialogElement.absPosition[1] - DRAW_DIALOG_X) < 1e-9, "put back")
end)

test("NothingIsShiftedWhileNoDialogIsOpen", function()
    settle()
    for eye = 0, 1 do
        drawn = {}
        parity = eye
        g_gui:draw()
        assert(#drawn == 0, "nothing to draw, nothing moved")
    end
    -- And the dialog that opens afterwards starts from where the game placed it
    local root = showDialog("FishDialog")
    closeDialog()
    local after = renderFrame(0)
    assert(after == nil, "a closed dialog is not drawn")
    assert(root.absPosition[1] == 0 and root.target.dialogElement.absPosition[1] == DRAW_DIALOG_X, "a closed dialog was never moved")
end)

test("ADialogInTwoDFloatingWindowModeIsNotShifted", function()
    VRCompanion:set2DViewActive(true)
    local root = showDialog("FishDialog")
    settle()
    for eye = 0, 1 do
        local d = renderFrame(eye)
        assert(d.root == 0 and d.box == DRAW_DIALOG_X, "in 2D Floating Window Mode the dialog is drawn where the game placed it")
    end
end)

test("ADialogThatIsNotAStereoDialogIsNotShifted", function()
    showDialog("YesNoDialog")
    settle()
    for eye = 0, 1 do
        local d = renderFrame(eye)
        assert(d.root == 0 and d.box == DRAW_DIALOG_X, "a dialog on the menu quad is not moved")
    end
end)

test("ADialogWhoseRootCannotBeMovedIsDrawnWithZeroDisparity", function()
    showDialog("FishDialog", true)
    settle()
    for eye = 0, 1 do
        local d = renderFrame(eye)
        assert(d.root == 0 and d.box == DRAW_DIALOG_X, "no root position to move: drawn where the game placed it")
    end
end)

print(string.format("Stereo Dialog Depth Tests: %d passed, %d failed", passed, failed))
assert(failed == 0, "assertions failed")
