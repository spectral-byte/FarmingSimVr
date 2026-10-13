// Virtual Hands (spec #82), seen from outside: the real Companion Mod runs against the real VRBridge
// over FakeTrackedHeadset, with scene-graph stubs that record the loaded hand model, its clones,
// visibility, scale, world transforms and shadow flags (companion_mod_stubs.h, StubModel). The test moves
// the fake Motion Controllers, the game clock and the game state, drives frames the way the game would
// and asserts what the player would see: whether each hand is shown, where it is in the world, and what
// the log says. Prior art: the Pointer Ray tests (test_pointer_ray.cpp).

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

HeadPose HandAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

HeadPose HandAt(float x, float y, float z, float pitch, float yaw) {
    HeadPose pose = HandAt(x, y, z);
    pose.rotation = fs25vr::EulerAngles(pitch, yaw, 0.0f);
    return pose;
}

FakeOpenXrControllers::Inputs Trigger(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = value;
    return inputs;
}

// The engine's console and settings globals the Companion Mod registers into.
constexpr const char* kConsoleStubs = R"luau(
    _G.registeredCommands = {}
    _G.addConsoleCommand = function(name, desc, method, target)
        _G.registeredCommands[name] = { desc = desc, method = method, target = target }
    end
    _G.removeConsoleCommand = function(name) _G.registeredCommands[name] = nil end
    _G.runCommand = function(name, ...)
        local command = _G.registeredCommands[name]
        assert(command ~= nil, name .. " is not registered")
        return command.target[command.method](command.target, ...)
    end
    _G.Input = { MOUSE_BUTTON_NONE = 0, MOUSE_BUTTON_LEFT = 1 }
    _G.g_gui = {
        visible = false,
        getIsGuiVisible = function(self) return self.visible end,
        mouseEvent = function() end,
    }
)luau";

// Where a hand held at (hx, hy, hz) from a head at the origin of the tracking space is in the world,
// computed independently of the mod: the cab's head tracking node turns tracking space by the cab's
// yaw plus the cab model's half turn, and sits 1.5 m above the cab (the same worked example as the
// Pointer Ray tests).
constexpr const char* kExpectedWorldMath = R"luau(
    function _G.turnedY(angle, x, y, z)
        return x * math.cos(angle) + z * math.sin(angle), y, -x * math.sin(angle) + z * math.cos(angle)
    end
    function _G.expectedOrigin(cab, turn, hx, hy, hz)
        local ox, oy, oz = turnedY(turn, hx, hy, hz)
        local bx, by, bz = turnedY(cab.yaw, 0, 1.5, 0)
        return cab.x + bx + ox, cab.y + by + oy, cab.z + bz + oz
    end
    function _G.near(a, b) return math.abs(a - b) < 1e-3 end
    -- World position of a Virtual Hand ("left" or "right"), or nil when there is none.
    function _G.handPosition(which)
        local node = StubModel.hands()[which]
        if node == nil then return nil end
        return StubScene.worldPosition(node)
    end
    function _G.handIsShown(which)
        local node = StubModel.hands()[which]
        return node ~= nil and StubModel.isShown(node)
    end
)luau";

// The mod started on a map with a given scene (a cab at the world origin by default), the hand model
// available, and Interactive Control installed (the Pointer Ray and Click Points must keep working).
void StartMod(lua_State* L, fs25vr::VRBridge& bridge, const char* scene = "StubScene.cab({})") {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubModelLoading, "=modelLoading"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubInteractiveControl, "=interactiveControl"), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, kExpectedWorldMath, "=expectedMath"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.install('ok')", "=installIc"), 0);
    ASSERT_EQ(RunLuauChunk(L, scene, "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
}

// What the game does each frame before the Companion Mod's update: the camera update that places the
// head in the world, and Interactive Control's own update of its Click Points.
void PreFrame(lua_State* L) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        if StubScene.updateCamera then StubScene.updateCamera() end
        if StubIC.tick then StubIC.tick() end
    )luau", "=preFrame"), 0);
}

int CountLogLines(const std::string& text) {
    int count = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find(text) != std::string::npos) ++count;
    }
    return count;
}

} // namespace

#define FRAMES(count) \
    for (int frame_ = 0; frame_ < (count); ++frame_) { PreFrame(L); NextFrame(&headset); }

// A Motion Controller picked up: it moves a little, then holds the aim pose given, with its grip pose
// somewhere else (the grip pose is where the hand holds the controller, not where it points). The game clock\n// is set by the test once (the in-use timeout is measured on it).
#define PICK_UP(hand, aimX, aimY, aimZ, gripX, gripY, gripZ) \
    do { \
        headset.SetHandPose((hand), HandAt((aimX) - 0.1f, (aimY), (aimZ))); \
        FRAMES(1); \
        headset.SetHandPose((hand), HandAt((aimX), (aimY), (aimZ))); \
        headset.SetHandGripPose((hand), HandAt((gripX), (gripY), (gripZ))); \
        FRAMES(1); \
    } while (0)

TEST_F(CompanionModTest, BothVirtualHandsAppearAtTheirGripPosesNotTheirAimPosesInTheCab) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "StubScene.cab({ x = 100, y = 2, z = 50, yaw = 0.7 })"));
    SetGameTime(10000);
    EXPECT_EQ(EvalLua(L, "handIsShown('left') or handIsShown('right')"), "false") << "no hand before a Motion Controller is used";

    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local cab = { x = 100, y = 2, z = 50, yaw = 0.7 }
        assert(handIsShown('right') and handIsShown('left'), "both Virtual Hands are shown: " .. VRCompanion:getVirtualHandsDiagnostics())
        local rx, ry, rz = handPosition('right')
        local ex, ey, ez = expectedOrigin(cab, 0.7 + math.pi, 0.25, -0.10, -0.20)
        assert(near(rx, ex) and near(ry, ey) and near(rz, ez),
            string.format("right hand at %.3f %.3f %.3f, grip pose is at %.3f %.3f %.3f", rx, ry, rz, ex, ey, ez))
        local lx, ly, lz = handPosition('left')
        ex, ey, ez = expectedOrigin(cab, 0.7 + math.pi, -0.25, -0.10, -0.20)
        assert(near(lx, ex) and near(ly, ey) and near(lz, ez),
            string.format("left hand at %.3f %.3f %.3f, grip pose is at %.3f %.3f %.3f", lx, ly, lz, ex, ey, ez))
    )luau", "=gripPlacement"), 0);
}

TEST_F(CompanionModTest, AVirtualHandTurnsWithItsGripPose) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "StubScene.cab({ x = 100, y = 2, z = 50, yaw = 0.7 })"));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    headset.SetHandGripPose(kRight, HandAt(0.25f, -0.10f, -0.20f, 0.0f, 0.5f));  // the grip pose is turned 0.5 rad
    FRAMES(1);

    // A grip pose yawed +0.5 rad in tracking space has its -Z axis along (-sin 0.5, 0, -cos 0.5); the cab
    // turns that by its yaw plus the half turn of the cab model.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local node = StubModel.hands().right
        assert(node ~= nil and StubModel.isShown(node), "the right hand is shown")
        local dx, dy, dz = StubScene.worldDirection(node, 0, 0, -1)
        local ex, ey, ez = turnedY(0.7 + math.pi, -math.sin(0.5), 0, -math.cos(0.5))
        assert(near(dx, ex) and near(dy, ey) and near(dz, ez),
            string.format("hand's -Z axis %.3f %.3f %.3f, expected %.3f %.3f %.3f", dx, dy, dz, ex, ey, ez))
    )luau", "=orientation"), 0);
}

TEST_F(CompanionModTest, VirtualHandsAppearAtTheGripPosesInAnOutsideCamera) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "StubScene.outside({ x = 10, y = 2, z = 30, yaw = -1.2 })"));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);

    // The head is carried on a level base at the engine camera, turned by its heading.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(handIsShown('right') and handIsShown('left'), VRCompanion:getVirtualHandsDiagnostics())
        local x, y, z = handPosition('right')
        local ex, ey, ez = turnedY(-1.2, 0.25, -0.10, -0.20)
        assert(near(x, 10 + ex) and near(y, 2 + ey) and near(z, 30 + ez), string.format("right hand at %.3f %.3f %.3f", x, y, z))
        x, y, z = handPosition('left')
        ex, ey, ez = turnedY(-1.2, -0.25, -0.10, -0.20)
        assert(near(x, 10 + ex) and near(y, 2 + ey) and near(z, 30 + ez), string.format("left hand at %.3f %.3f %.3f", x, y, z))
    )luau", "=outsideCamera"), 0);
}

TEST_F(CompanionModTest, VirtualHandsAppearAtTheGripPosesOnFootWhicheverWayTheBodyFaces) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "StubScene.onFoot({ x = 50, z = -20, yaw = 1.0 })"));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);

    // With the head at the origin of tracking space, a hand is where it is in tracking space as the head
    // sees it, whichever way the player's body faces.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(handIsShown('right') and handIsShown('left'), VRCompanion:getVirtualHandsDiagnostics())
        local node = VRCompanion.cyclopeanAudioListenerNode
        local lx, ly, lz = getWorldTranslation(node)
        local qx, qy, qz, qw = getWorldQuaternion(node)
        assert(math.abs(lx - 50) < 1 and math.abs(lz + 20) < 1, "the head is by the player")
        local function inHeadFrame(x, y, z)
            local ix, iy, iz = -qx, -qy, -qz
            x, y, z = x - lx, y - ly, z - lz
            local cx, cy, cz = iy * z - iz * y + qw * x, iz * x - ix * z + qw * y, ix * y - iy * x + qw * z
            return x + 2 * (iy * cz - iz * cy), y + 2 * (iz * cx - ix * cz), z + 2 * (ix * cy - iy * cx)
        end
        local hx, hy, hz = inHeadFrame(handPosition('right'))
        assert(near(hx, 0.25) and near(hy, -0.10) and near(hz, -0.20), string.format("right hand in head frame %.3f %.3f %.3f", hx, hy, hz))
        hx, hy, hz = inHeadFrame(handPosition('left'))
        assert(near(hx, -0.25) and near(hy, -0.10) and near(hz, -0.20), string.format("left hand in head frame %.3f %.3f %.3f", hx, hy, hz))
    )luau", "=onFoot"), 0);
}

TEST_F(CompanionModTest, AVirtualHandHidesWhenSetDownOrUntrackedAndComesBackAsSoonAsItIsUsedAgain) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    auto shown = [&](const char* which) { return EvalLua(L, std::string("handIsShown('") + which + "')"); };
    EXPECT_EQ(shown("right"), "true");
    EXPECT_EQ(shown("left"), "false") << "a Motion Controller nobody picked up has no hand";

    // Set down: it lies still for longer than the in-use timeout.
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(shown("right"), "false") << "a Motion Controller left lying still has no hand";
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    EXPECT_EQ(shown("right"), "true") << "back as soon as it moves again";

    // Tracking lost.
    headset.SetHandTracked(kRight, false);
    FRAMES(1);
    EXPECT_EQ(shown("right"), "false") << "an untracked Motion Controller leaves no frozen hand";
    headset.SetHandTracked(kRight, true);
    FRAMES(1);
    EXPECT_EQ(shown("right"), "true");

    // Only the grip pose lost: the aim pose still works, but there is nowhere to put the hand.
    headset.SetHandGripPose(kRight, HandAt(0.25f, -0.10f, -0.20f), /*tracked=*/false);
    FRAMES(1);
    EXPECT_EQ(shown("right"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true") << "the Motion Controller itself is still in use";
    headset.SetHandGripPose(kRight, HandAt(0.25f, -0.10f, -0.20f), /*tracked=*/true);
    FRAMES(1);
    EXPECT_EQ(shown("right"), "true");
}

TEST_F(CompanionModTest, VirtualHandsHideInAMenuIn2DModeInPassThroughModeAndWithMotionControllersOffAndComeBackAfterEach) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);
    auto bothShown = [&]() { return EvalLua(L, "handIsShown('left') and handIsShown('right')"); };
    auto anyShown = [&]() { return EvalLua(L, "handIsShown('left') or handIsShown('right')"); };
    ASSERT_EQ(bothShown(), "true");

    struct Case { const char* name; const char* enter; const char* leave; bool pickUpAgain; };
    const Case cases[] = {
        { "a menu", "g_gui.visible = true", "g_gui.visible = false", false },
        { "2D Floating Window Mode", "VRCompanion:set2DViewActive(true)", "VRCompanion:set2DViewActive(false)", false },
        { "Pass-Through Mode", "VRCompanion:setPassThroughMode(true)", "VRCompanion:setPassThroughMode(false)", false },
        { "vrMotionControllers off", "runCommand('vrMotionControllers', 'off')", "runCommand('vrMotionControllers', 'on')", true },  // switching them off forgets what was in use
        { "another camera shown", "_G.otherCamera = StubScene.newNode('other', 0); _G.g_cameraManager = { getActiveCamera = function() return otherCamera end }",
          "_G.g_cameraManager = nil", false },
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        ASSERT_EQ(RunLuauChunk(L, c.enter, "=enter"), 0);
        FRAMES(2);
        EXPECT_EQ(anyShown(), "false") << "no Virtual Hand in " << c.name << ": " << EvalLua(L, "VRCompanion:getVirtualHandsDiagnostics()");
        ASSERT_EQ(RunLuauChunk(L, c.leave, "=leave"), 0);
        FRAMES(2);
        if (c.pickUpAgain) {
            PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
            PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);
        }
        EXPECT_EQ(bothShown(), "true") << "both hands are back after " << c.name << ": " << EvalLua(L, "VRCompanion:getVirtualHandsDiagnostics()");
    }
}

TEST_F(CompanionModTest, VirtualHandsStayOnTheHandsAfterASnapTurnTheRecenterActionAndWhileTheVehicleMoves) {
    FakeTrackedHeadset headset;
    HeadPose head;
    head.position = Vector3(0.2f, 0.05f, -0.1f);
    head.rotation = fs25vr::EulerAngles(0.0f, 0.4f, 0.0f);
    head.isValid = true;
    headset.SetHeadPose(head);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "_G.cabScene = StubScene.cab({})"));
    SetGameTime(10000);
    PICK_UP(kRight, 0.50f, 0.10f, -0.30f, 0.45f, 0.05f, -0.20f);
    FRAMES(2);

    // What must not change while the hand and head are held still is the Virtual Hand's position and
    // direction in the head's own frame (the Cyclopean Audio Listener Node is the head in the world).
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local function inverseRotate(qx, qy, qz, qw, x, y, z)
            qx, qy, qz = -qx, -qy, -qz
            local cx, cy, cz = qy * z - qz * y + qw * x, qz * x - qx * z + qw * y, qx * y - qy * x + qw * z
            return x + 2 * (qy * cz - qz * cy), y + 2 * (qz * cx - qx * cz), z + 2 * (qx * cy - qy * cx)
        end
        _G.handInHeadFrame = function()
            local node = StubModel.hands().right
            assert(node ~= nil and StubModel.isShown(node), "the hand is shown: " .. VRCompanion:getVirtualHandsDiagnostics())
            local x, y, z = StubScene.worldPosition(node)
            local lx, ly, lz = getWorldTranslation(VRCompanion.cyclopeanAudioListenerNode)
            local qx, qy, qz, qw = getWorldQuaternion(VRCompanion.cyclopeanAudioListenerNode)
            local ox, oy, oz = inverseRotate(qx, qy, qz, qw, x - lx, y - ly, z - lz)
            local dx, dy, dz = StubScene.worldDirection(node, 0, 0, -1)
            dx, dy, dz = inverseRotate(qx, qy, qz, qw, dx, dy, dz)
            return { ox, oy, oz, dx, dy, dz, worldX = x, worldY = y, worldZ = z }
        end
        _G.sameAs = function(a, b)
            for i = 1, 6 do assert(near(a[i], b[i]), "component " .. i .. ": " .. a[i] .. " vs " .. b[i]) end
        end
        _G.before = handInHeadFrame()
        -- the grip pose (0.45, 0.05, -0.20) minus the head (0.2, 0.05, -0.1), turned back by the head's yaw
        local hx, hy, hz = turnedY(-0.4, 0.25, 0.0, -0.1)
        assert(near(before[1], hx) and near(before[2], hy) and near(before[3], hz),
            string.format("hand offset in the head frame %.3f %.3f %.3f", before[1], before[2], before[3]))
    )luau", "=baseline"), 0);

    // Snap Turn: the view turns, the hand turns with it.
    ASSERT_EQ(RunLuauChunk(L, "assert(VRCompanion:snapTurnRight(nil, 20000) == true, 'snap turn happened')", "=snapTurn"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local after = handInHeadFrame()
        sameAs(after, before)
        assert(not (near(after.worldX, before.worldX) and near(after.worldZ, before.worldZ)), "the hand moved in the world with the turn")
        _G.before = after
    )luau", "=afterSnapTurn"), 0);

    // The Recenter Action moves the head and the hand in tracking space together.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onRecenterAction()", "=recenter"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "sameAs(handInHeadFrame(), before)", "=afterRecenter"), 0);

    // The vehicle drives 5 m east and 3 m north: the hand goes along.
    ASSERT_EQ(RunLuauChunk(L, "_G.before = handInHeadFrame(); setTranslation(cabScene.parent, 5, 0, 3)", "=drive"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local after = handInHeadFrame()
        sameAs(after, before)
        assert(near(after.worldX - before.worldX, 5) and near(after.worldY - before.worldY, 0) and near(after.worldZ - before.worldZ, 3),
            string.format("moved by %.3f %.3f %.3f", after.worldX - before.worldX, after.worldY - before.worldY, after.worldZ - before.worldZ))
    )luau", "=afterDriving"), 0);
}

TEST_F(CompanionModTest, VirtualHandsLookTheSameInBothEyesUnderAlternateFrameRendering) {
    FakeTrackedHeadset headset;
    headset.manager->SetIpd(0.064f);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "StubScene.cab({ x = 10, z = 20, yaw = 0.3 })"));
    RegisterTestSetFrameParity(bridge);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:setRenderingMode('AFR'); VRBridge.latchPendingRenderingMode()", "=afr"), 0);
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.10f, -0.40f, 0.25f, -0.10f, -0.20f);

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setFrameParity(0)", "=leftEye"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "_G.leftEye = { handPosition('right') }; _G.leftEyeCamera = { StubScene.worldPosition(getCamera()) }", "=readLeft"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setFrameParity(1)", "=rightEye"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local right = { handPosition('right') }
        local camera = { StubScene.worldPosition(getCamera()) }
        assert(handIsShown('right') and #leftEye == 3 and #right == 3, "shown in both eyes")
        assert(math.abs(camera[1] - leftEyeCamera[1]) + math.abs(camera[3] - leftEyeCamera[3]) > 0.03, "the camera is at the other eye")
        for i = 1, 3 do assert(math.abs(leftEye[i] - right[i]) < 1e-6, "component " .. i .. " differs between the eyes") end
    )luau", "=sameInBothEyes"), 0);
}

TEST_F(CompanionModTest, VirtualHandsAreOnePlacementForBothPassesUnderSequentialDualPass) {
    FakeTrackedHeadset headset;
    headset.manager->SetIpd(0.064f);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "StubScene.cab({ x = 10, z = 20, yaw = 0.3 })"));
    RegisterTestSetFrameParity(bridge);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:setRenderingMode('DualPass'); VRBridge.latchPendingRenderingMode(); VRBridge.setSubPassIndex(0)", "=sdp"), 0);
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.10f, -0.40f, 0.25f, -0.10f, -0.20f);
    FRAMES(2);

    // One update per logical frame; the camera is placed and the scene drawn in each of the two passes.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local seen = {}
        for pass = 0, 1 do
            VRBridge.setSubPassIndex(pass)
            StubScene.updateCamera()
            assert(handIsShown('right'), "shown in pass " .. pass)
            seen[pass] = { handPosition('right') }
        end
        for i = 1, 3 do assert(math.abs(seen[0][i] - seen[1][i]) < 1e-6, "component " .. i .. " differs between the passes") end
    )luau", "=sameInBothPasses"), 0);
}

TEST_F(CompanionModTest, TheLeftVirtualHandIsTheRightHandModelMirroredAndNeitherCastsShadows) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local hands = StubModel.hands()
        assert(hands.left ~= nil and hands.right ~= nil, "a clone for each hand")
        assert(#StubModel.clones == 2, "two clones in all")
        local lx, ly, lz = StubModel.scale(hands.left)
        local rx, ry, rz = StubModel.scale(hands.right)
        assert(lx == -1 and ly == 1 and lz == 1, "the left hand is the model mirrored along X")
        assert(rx == 1 and ry == 1 and rz == 1, "the right hand is the model as it is")
        assert(not StubModel.castsShadow(hands.left) and not StubModel.castsShadow(hands.right), "neither hand casts shadows")
        assert(StubModel.isLinkedToScene(hands.left) and StubModel.isLinkedToScene(hands.right), "both are in the scene")
    )luau", "=mirrored"), 0);
}

TEST_F(CompanionModTest, TheHandModelIsLoadedOncePerMapAndDeletedWhenTheMapUnloads) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "#StubModel.loads"), "1") << "loaded once, however often the hands hide and show";
    EXPECT_EQ(EvalLua(L, "#StubModel.clones"), "2");
    EXPECT_EQ(EvalLua(L, "StubModel.loads[1].filename:find('virtualHandRight.i3d', 1, true) ~= nil"), "true")
        << "the right-hand model of the Companion Mod: " << EvalLua(L, "StubModel.loads[1].filename");
    EXPECT_EQ(EvalLua(L, "StubModel.loads[1].addPhysics"), "false") << "no physics";

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.nodesBefore = { StubModel.hands().left, StubModel.hands().right, StubModel.loads[1].root }
        VRCompanion:deleteMap()
    )luau", "=unloadMap"), 0);
    EXPECT_EQ(EvalLua(L, "StubModel.deleted[nodesBefore[1]] and StubModel.deleted[nodesBefore[2]] and StubModel.deleted[nodesBefore[3]]"), "true")
        << "the clones and the loaded model are gone";
    EXPECT_EQ(EvalLua(L, "next(StubModel.hands()) == nil"), "true");

    // The next map loads it again.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('y')", "=loadAnotherMap"), 0);
    SetGameTime(20000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    EXPECT_EQ(EvalLua(L, "#StubModel.loads"), "2");
    EXPECT_EQ(EvalLua(L, "handIsShown('right')"), "true");
}

class VirtualHandsModelTest : public CompanionModTest {};

// ---- A model that is missing or fails to load ----

TEST_F(CompanionModTest, AMissingHandModelLogsOnceAndLeavesNoVirtualHandsWhilePointingStillWorks) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubModel.fileExists = false; StubIC.addPoint('lights', -0.3, 1.5, 2.3)", "=noModelFile"), 0);
    SetGameTime(10000);
    headset.SetHandPose(kRight, HandAt(0.2f, 0.0f, -0.3f));
    FRAMES(1);
    headset.SetHandPose(kRight, HandAt(0.3f, 0.0f, -0.3f));
    FRAMES(30);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    FRAMES(3);

    EXPECT_EQ(EvalLua(L, "#StubModel.clones"), "0") << "no Virtual Hands";
    EXPECT_EQ(EvalLua(L, "#StubModel.loads"), "0") << "the engine is not asked to load a file that is not there";
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands:"), 1) << "one line, however long the game runs";
    EXPECT_EQ(CountLogLines("virtualHandRight.i3d"), 1) << "naming the file";
    EXPECT_NE(EvalLua(L, "runCommand('vrVirtualHandsDiagnostics')").find("model not loaded"), std::string::npos);

    // The Pointer Ray and Click Points are unaffected.
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "lights");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lights");
}

TEST_F(CompanionModTest, AHandModelThatFailsToLoadLogsOnceAndLeavesNoVirtualHandsWhilePointingStillWorks) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubModel.failLoad = true; StubIC.addPoint('lights', -0.3, 1.5, 2.3)", "=failingModel"), 0);
    SetGameTime(10000);
    headset.SetHandPose(kRight, HandAt(0.2f, 0.0f, -0.3f));
    FRAMES(1);
    headset.SetHandPose(kRight, HandAt(0.3f, 0.0f, -0.3f));
    FRAMES(30);

    EXPECT_EQ(EvalLua(L, "#StubModel.clones"), "0");
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands:"), 1);
    EXPECT_EQ(CountLogLines("failed to load"), 1);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "lights");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lights");
}

// ---- Log line and diagnostics ----

TEST_F(CompanionModTest, TheVirtualHandsLogLineChangesOnlyWhenTheReasonChanges) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    FRAMES(20);
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands:"), 1) << "one line while nothing changes";
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands: available"), 1);

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(10);
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands:"), 2);
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands: head not placed in the world"), 1);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    FRAMES(10);
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands: available"), 2) << "it says when they are back";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
    FRAMES(10);
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands: Motion Controllers off"), 1);
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands:"), 4);
}

TEST_F(CompanionModTest, TheDiagnosticsCommandsReportEachVirtualHandAndWhyItIsHidden) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);

    std::string report = EvalLua(L, "runCommand('vrVirtualHandsDiagnostics')");
    EXPECT_NE(report.find("Virtual Hands: available"), std::string::npos) << report;
    EXPECT_NE(report.find("Right Virtual Hand: shown"), std::string::npos) << report;
    EXPECT_NE(report.find("Left Virtual Hand: hidden (Motion Controller not in use)"), std::string::npos) << report;
    const std::string pointer = EvalLua(L, "runCommand('vrPointerDiagnostics')");
    EXPECT_NE(pointer.find("Right Virtual Hand: shown"), std::string::npos) << "the Motion Controller diagnostics carry it too: " << pointer;
    EXPECT_NE(pointer.find("Pointer Ray:"), std::string::npos) << "next to the existing report: " << pointer;

    headset.SetHandGripPose(kRight, HandAt(0.25f, -0.10f, -0.20f), /*tracked=*/false);
    FRAMES(1);
    report = EvalLua(L, "runCommand('vrVirtualHandsDiagnostics')");
    EXPECT_NE(report.find("Right Virtual Hand: hidden (grip pose not tracked)"), std::string::npos) << report;

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:set2DViewActive(true)", "=2d"), 0);
    FRAMES(1);
    report = EvalLua(L, "runCommand('vrVirtualHandsDiagnostics')");
    EXPECT_NE(report.find("Virtual Hands: head not placed in the world"), std::string::npos) << report;
    EXPECT_NE(report.find("Right Virtual Hand: hidden (head not placed in the world"), std::string::npos) << report;

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:set2DViewActive(false); runCommand('vrMotionControllers', 'off')", "=off"), 0);
    FRAMES(1);
    report = EvalLua(L, "runCommand('vrVirtualHandsDiagnostics')");
    EXPECT_NE(report.find("Virtual Hands: Motion Controllers off"), std::string::npos) << report;

    // The command goes with the map.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=unloadMap"), 0);
    EXPECT_EQ(EvalLua(L, "registeredCommands['vrVirtualHandsDiagnostics']"), "nil");
}