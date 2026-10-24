// Virtual Hands (spec #82), seen from outside: the real Companion Mod runs against the real VRBridge
// over FakeTrackedHeadset, with scene-graph stubs that record the loaded hand model, its clones,
// visibility, scale, world transforms and shadow flags (companion_mod_stubs.h, StubModel). The test moves
// the fake Motion Controllers, the game clock and the game state, drives frames the way the game would
// and asserts what the player would see: whether each hand is shown, where it is in the world, and what
// the log says. Prior art: the Pointer Ray tests (test_pointer_ray.cpp).

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include <array>
#include <cmath>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

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

FakeOpenXrControllers::Inputs Squeeze(float trigger, float grip) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = trigger;
    inputs.grip = grip;
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
// somewhere else (the grip pose is where the hand holds the controller, not where it points). The game clock
// is set by the test once (the in-use timeout is measured on it).
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

// In the game the vehicle moves AFTER the Companion Mod's update in a frame, and its camera (and the head with it)
// follows before the frame is drawn. A hand placed at a world position in the update would then be drawn where the
// cab was, a frame's travel behind: a ghost trailing along the direction of travel. The hand must ride with the cab.
TEST_F(CompanionModTest, AVirtualHandRidesWithTheCabWhenTheVehicleMovesAfterTheModsUpdate) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "_G.cabScene = StubScene.cab({ x = 100, y = 2, z = 50, yaw = 0.7 })"));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    FRAMES(2);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local cab = { x = 100, y = 2, z = 50, yaw = 0.7 }
        -- one frame of driving at about 50 km/h, after the mod's update; the camera follows the cab
        setTranslation(cabScene.parent, cab.x + 0.15, cab.y, cab.z + 0.17)
        StubScene.updateCamera()
        cab.x, cab.z = cab.x + 0.15, cab.z + 0.17
        assert(handIsShown('right'), VRCompanion:getVirtualHandsDiagnostics())
        local x, y, z = handPosition('right')
        local ex, ey, ez = expectedOrigin(cab, 0.7 + math.pi, 0.25, -0.10, -0.20)
        assert(near(x, ex) and near(y, ey) and near(z, ez),
            string.format("the hand is at %.3f %.3f %.3f, the grip pose in the moved cab at %.3f %.3f %.3f", x, y, z, ex, ey, ez))
    )luau", "=movedAfterUpdate"), 0);
}

// A vehicle file may keep its cameras in a transform group marked invisible: a camera there still renders, but a
// shape below it is hidden. The head hangs below the cab camera's parent, so a hand linked below the head would
// vanish in such a cab. It hangs below the deepest visible node above the head instead, and still rides with the cab.
TEST_F(CompanionModTest, AVirtualHandIsShownAndRidesWithTheCabWhenTheCabCamerasHangInAnInvisibleGroup) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, R"luau(
        _G.cabScene = StubScene.cab({ x = 100, y = 2, z = 50, yaw = 0.7 })
        local cameras = StubScene.newNode("cameras", cabScene.parent)
        setVisibility(cameras, false)
        link(cameras, cabScene.cameraNode)
        link(cameras, cabScene.headTrackingNode)
    )luau"));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    FRAMES(2);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local cab = { x = 100, y = 2, z = 50, yaw = 0.7 }
        assert(handIsShown('right'), "the hand is shown in the cab: " .. VRCompanion:getVirtualHandsDiagnostics())
        local x, y, z = handPosition('right')
        local ex, ey, ez = expectedOrigin(cab, 0.7 + math.pi, 0.25, -0.10, -0.20)
        assert(near(x, ex) and near(y, ey) and near(z, ez),
            string.format("the hand is at %.3f %.3f %.3f, the grip pose at %.3f %.3f %.3f", x, y, z, ex, ey, ez))

        setTranslation(cabScene.parent, cab.x + 0.15, cab.y, cab.z + 0.17)
        StubScene.updateCamera()
        cab.x, cab.z = cab.x + 0.15, cab.z + 0.17
        x, y, z = handPosition('right')
        ex, ey, ez = expectedOrigin(cab, 0.7 + math.pi, 0.25, -0.10, -0.20)
        assert(near(x, ex) and near(y, ey) and near(z, ez),
            string.format("after driving the hand is at %.3f %.3f %.3f, the grip pose at %.3f %.3f %.3f", x, y, z, ex, ey, ez))
    )luau", "=invisibleCameraGroup"), 0);
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands ride on cab"), 1)
        << "one line names the node the hands ride on: the vehicle's component, below the camera nodes' reach";
    EXPECT_EQ(CountLogLines(" on cab "), 1) << "the geometry line names it too";
}

// A component hidden as a whole leaves the hands nothing visible to ride on in the vehicle: they stay at the scene root.
TEST_F(CompanionModTest, AVirtualHandIsStillShownWhenTheNodeCarryingTheHeadIsHidden) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "_G.cabScene = StubScene.cab({ x = 100, y = 2, z = 50, yaw = 0.7 }); setVisibility(cabScene.parent, false)"));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(handIsShown('right'), "the hand is shown: " .. VRCompanion:getVirtualHandsDiagnostics())
        local x, y, z = handPosition('right')
        local ex, ey, ez = expectedOrigin({ x = 100, y = 2, z = 50, yaw = 0.7 }, 0.7 + math.pi, 0.25, -0.10, -0.20)
        assert(near(x, ex) and near(y, ey) and near(z, ez), string.format("the hand is at %.3f %.3f %.3f", x, y, z))
    )luau", "=hiddenComponent"), 0);
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands ride on root"), 1);
}

// The game runs the mod's update once per AFR pair but renders each eye: the second eye's frame is rendered after the
// vehicle moved again with no update between (vrVirtualHandsTrace, headset check). A hand placed only in the update was
// a sharp copy lagging behind in that eye, and placing it in the draw came too late for the frame (the hand was right
// at draw time in both eyes, the copy stayed). The camera update, which places the eyes before every rendered frame,
// places the hands too.
TEST_F(CompanionModTest, AVirtualHandIsPlacedAgainWhenAFrameIsRenderedWithoutAnUpdateAfterTheVehicleMoved) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    // A hidden component leaves the hands on the scene root, as the headset check found them in the cab.
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "_G.cabScene = StubScene.cab({ x = 100, y = 2, z = 50, yaw = 0.7 }); setVisibility(cabScene.parent, false)"));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    FRAMES(2);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local cab = { x = 100, y = 2, z = 50, yaw = 0.7 }
        -- the second eye: the vehicle moved and the camera followed, but no update ran (and no draw yet)
        setTranslation(cabScene.parent, cab.x + 0.12, cab.y, cab.z + 0.05)
        StubScene.updateCamera()
        cab.x, cab.z = cab.x + 0.12, cab.z + 0.05
        assert(handIsShown('right'), VRCompanion:getVirtualHandsDiagnostics())
        local x, y, z = handPosition('right')
        local ex, ey, ez = expectedOrigin(cab, 0.7 + math.pi, 0.25, -0.10, -0.20)
        assert(near(x, ex) and near(y, ey) and near(z, ez),
            string.format("drawn at %.3f %.3f %.3f, the grip pose in the moved cab is at %.3f %.3f %.3f", x, y, z, ex, ey, ez))
    )luau", "=drawWithoutUpdate"), 0);
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands ride on root (cab is hidden)"), 1) << "the line says why the hands are on the root";
}

// A hand drawn stale in one eye only (a headset check under AFR) is found by where it is relative to the camera that
// renders each frame: vrVirtualHandsTrace logs that after the update and again at draw time, with the eye, for the
// frames asked for, then stops by itself.
TEST_F(CompanionModTest, VrVirtualHandsTraceLogsEachShownHandRelativeToTheRenderingCameraAfterTheUpdateAndAtDrawTime) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "StubScene.cab({ x = 100, y = 2, z = 50, yaw = 0.7 })"));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);

    const std::string reply = EvalLua(L, "runCommand('vrVirtualHandsTrace', '2')");
    EXPECT_NE(reply.find("2 frames"), std::string::npos) << reply;
    for (int frame = 0; frame < 4; ++frame) {
        FRAMES(1);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:draw()", "=draw"), 0);
    }
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands trace update"), 2) << "one line per traced frame after the update";
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands trace camera"), 2) << "one after the camera path placed it";
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands trace draw"), 2) << "and one at draw time, then it stops";
    EXPECT_EQ(CountLogLines("right in camera ("), 6) << "each line places the shown right hand in the rendering camera's space";
    EXPECT_EQ(CountLogLines("left in camera"), 0) << "a hidden hand is left out";
    EXPECT_EQ(CountLogLines(" eye 0 "), 6) << "each line names the eye the frame renders";

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=unloadMap"), 0);
    EXPECT_EQ(EvalLua(L, "registeredCommands['vrVirtualHandsTrace']"), "nil");
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

// The engine ignores a scale (a size or a mirror) on a node above a skinned mesh, so the hands are real-world size
// and handed in the FILES: the left Virtual Hand is the left file, the right one the right file (the left model
// mirrored across the model's X plane). The result in the grip pose frame is still the mirror image across the grip
// frame's X plane (both hands' grip frames have the same axes), which turns a left hand into a right hand and leaves
// the finger and thumb directions of the frame alone. Nothing at or above a model is scaled, and nothing is mirrored by
// a negative scale.
TEST_F(CompanionModTest, TheRightVirtualHandIsTheLeftHandMirroredAcrossTheGripFrameWithNoScaleAnywhereAndNeitherCastsShadows) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local hands = StubModel.hands()
        assert(hands.left ~= nil and hands.right ~= nil, "a hand for each Motion Controller")
        assert(#StubModel.clones == 2, "two clones in all")
        local left, right = StubModel.model(hands.left), StubModel.model(hands.right)
        for _, node in ipairs({ hands.left, hands.right, left, right }) do
            local scaled = StubModel.scaledNodes(node)
            assert(#scaled == 0, "a node at, above or below the model is scaled: " .. table.concat(scaled, ", "))
        end
        assert(StubModel.setScaleCalls == 0, "the mod never calls setScale")
        assert(not StubModel.castsShadow(hands.left) and not StubModel.castsShadow(hands.right), "neither hand casts shadows")
        assert(StubModel.isLinkedToScene(hands.left) and StubModel.isLinkedToScene(hands.right), "both are in the scene")

        -- Every part of the right hand is where the left hand's counterpart is with the grip frame's X negated
        -- (a mirror across the model's own X plane would negate the thumb-to-little-finger axis instead).
        for _, joint in ipairs({ "index1", "middle1", "ring1", "little1", "thumb1", "index3" }) do
            local lx, ly, lz = StubModel.jointInGripFrame(hands.left, joint)
            local rx, ry, rz = StubModel.jointInGripFrame(hands.right, joint)
            assert(near(rx, -lx) and near(ry, ly) and near(rz, lz),
                string.format("%s: left at %.3f %.3f %.3f, right at %.3f %.3f %.3f", joint, lx, ly, lz, rx, ry, rz))
        end
        -- The right hand's model-to-grip offset is the left one's mirrored across the grip frame's X plane, S R S with
        -- S = diag(-1, 1, 1), worked out here from the matrices and not from the mod's own formula.
        local function matrix(q)
            local x, y, z, w = q.x, q.y, q.z, q.w
            return { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                     { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                     { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } }
        end
        local offsets = VRCompanion.VIRTUAL_HAND_MODEL_TO_GRIP
        local rl, rr = matrix(offsets.left.rotation), matrix(offsets.right.rotation)
        local s = { -1, 1, 1 }
        for i = 1, 3 do
            for j = 1, 3 do
                assert(near(rr[i][j], s[i] * s[j] * rl[i][j]), string.format("right rotation [%d][%d] is %.3f, the mirrored left one is %.3f", i, j, rr[i][j], s[i] * s[j] * rl[i][j]))
            end
        end
        local det = rr[1][1] * (rr[2][2] * rr[3][3] - rr[2][3] * rr[3][2]) - rr[1][2] * (rr[2][1] * rr[3][3] - rr[2][3] * rr[3][1]) + rr[1][3] * (rr[2][1] * rr[3][2] - rr[2][2] * rr[3][1])
        assert(near(det, 1), "the right offset rotation is a proper rotation, determinant " .. det)
        local lo_, ro_ = offsets.left.gripOrigin, offsets.right.gripOrigin
        assert(near(ro_.x, -lo_.x) and near(ro_.y, lo_.y) and near(ro_.z, lo_.z), "the right grip origin is the left one with x negated")
        assert(offsets.scale == nil, "the offset carries no scale: the models are in metres")

        local lo = { StubModel.meshBoundsInGripFrame(hands.left) }
        local ro = { StubModel.meshBoundsInGripFrame(hands.right) }
        assert(near(ro[1], -lo[4]) and near(ro[4], -lo[1]) and near(ro[2], lo[2]) and near(ro[5], lo[5]) and near(ro[3], lo[3]) and near(ro[6], lo[6]),
            "the glove mesh of the right hand is the left one mirrored in X")
    )luau", "=mirrored"), 0);
}

// The grip pose frame (OpenXR, as ticket #84 states it): origin at the centre of the tube the closed non-thumb
// fingers form, -Z from the little finger toward the thumb, +X the palm normal pointing INTO the palm for a right
// hand (so out of the palm for a left hand). The fingers then point along -Y. The hand is real-world size.
TEST_F(CompanionModTest, BothVirtualHandsSitInTheGripPoseFrameAtRealHandSize) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local function grip(hand, joint) return StubModel.jointInGripFrame(StubModel.hands()[hand], joint) end
        for _, which in ipairs({ "left", "right" }) do
            local hand = StubModel.hands()[which]
            local where = which .. " hand: "

            -- the fingers point along -Y: from the wrist (the Hand bone) to the middle finger's knuckle
            local handX, handY, handZ = StubModel.boneInGripFrame(hand, "Hand")
            local mx, my, mz = grip(which, "middle1")
            local dx, dy, dz = mx - handX, my - handY, mz - handZ
            assert(dy < -0.06 and math.abs(dx) < 0.5 * math.abs(dy) and math.abs(dz) < 0.5 * math.abs(dy),
                where .. string.format("from the wrist to the middle knuckle is %.3f %.3f %.3f: the fingers must run along -Y", dx, dy, dz))

            -- -Z runs from the little finger toward the thumb: the index finger is on the -Z side of the little finger
            local ix, iy, iz = grip(which, "index1")
            local lx, ly, lz = grip(which, "little1")
            assert(iz - lz < -0.04 and math.abs(ix - lx) < 0.5 * math.abs(iz - lz) and math.abs(iy - ly) < 0.5 * math.abs(iz - lz),
                where .. string.format("little finger at %.3f %.3f %.3f, index finger at %.3f %.3f %.3f: -Z must run from little finger toward the thumb", lx, ly, lz, ix, iy, iz))
            local tx, ty, tz = grip(which, "thumb1")
            assert(tz < iz, where .. "the thumb is further along -Z than the index finger")

            -- the origin is the tube the four fingers close around: between the knuckles' span across, and between
            -- the wrist and the knuckles along the hand
            local rz = select(3, grip(which, "ring1"))
            local firstZ, lastZ = math.min(iz, lz, mz, rz), math.max(iz, lz, mz, rz)
            assert(firstZ < 0 and lastZ > 0, where .. string.format("the origin is not across the knuckle row (%.3f .. %.3f)", firstZ, lastZ))
            assert(handY > 0 and my < 0, where .. string.format("the origin is not between the wrist (%.3f) and the knuckles (%.3f)", handY, my))

            -- the palm normal: the glove lies behind the origin along it, in the hollow of the fist (+X is out of
            -- the palm of a left hand, into the palm of a right hand)
            local minX, minY, minZ, maxX, maxY, maxZ = StubModel.meshBoundsInGripFrame(hand)
            if which == "left" then
                assert(maxX < 0.012 and minX < -0.03, where .. string.format("the glove spans x %.3f .. %.3f: it must lie on the -X side of the origin", minX, maxX))
            else
                assert(minX > -0.012 and maxX > 0.03, where .. string.format("the glove spans x %.3f .. %.3f: it must lie on the +X side of the origin", minX, maxX))
            end

            -- real-world size: wrist and the stub of arm to the fingertips, thumb to little finger, thickness
            local length, width, thickness = maxY - minY, maxZ - minZ, maxX - minX
            assert(length > 0.18 and length < 0.32, where .. string.format("the glove is %.3f m long", length))
            assert(width > 0.10 and width < 0.18, where .. string.format("the glove is %.3f m wide", width))
            assert(thickness > 0.03 and thickness < 0.09, where .. string.format("the glove is %.3f m thick", thickness))
        end
    )luau", "=gripFrame"), 0);
}

TEST_F(CompanionModTest, EachHandModelFileIsLoadedOncePerMapAndDeletedWhenTheMapUnloads) {
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
    EXPECT_EQ(EvalLua(L, "#StubModel.loads"), "2") << "each of the two files loaded once, however often the hands hide and show";
    EXPECT_EQ(EvalLua(L, "#StubModel.clones"), "2");
    EXPECT_EQ(EvalLua(L, "StubModel.loads[1].filename:find('models/VirtualHandLeft.i3d', 1, true) ~= nil"), "true")
        << "the left hand model of the Companion Mod: " << EvalLua(L, "StubModel.loads[1].filename");
    EXPECT_EQ(EvalLua(L, "StubModel.loads[2].filename:find('models/VirtualHandRight.i3d', 1, true) ~= nil"), "true")
        << "the right hand model of the Companion Mod: " << EvalLua(L, "StubModel.loads[2].filename");
    EXPECT_EQ(EvalLua(L, "tostring(StubModel.loads[1].addPhysics) .. tostring(StubModel.loads[2].addPhysics)"), "falsefalse") << "no physics";

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.nodesBefore = { StubModel.hands().left, StubModel.hands().right, StubModel.loads[1].root, StubModel.loads[2].root }
        VRCompanion:deleteMap()
    )luau", "=unloadMap"), 0);
    EXPECT_EQ(EvalLua(L, "StubModel.deleted[nodesBefore[1]] and StubModel.deleted[nodesBefore[2]] and StubModel.deleted[nodesBefore[3]] and StubModel.deleted[nodesBefore[4]]"), "true")
        << "the clones and both loaded models are gone";
    EXPECT_EQ(EvalLua(L, "next(StubModel.hands()) == nil"), "true");

    // The next map loads it again.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('y')", "=loadAnotherMap"), 0);
    SetGameTime(20000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    EXPECT_EQ(EvalLua(L, "#StubModel.loads"), "4");
    EXPECT_EQ(EvalLua(L, "handIsShown('right')"), "true");
}

// ---- A model that is missing or fails to load ----

TEST_F(CompanionModTest, AMissingHandModelLogsOnceAndLeavesNoVirtualHandsWhilePointingStillWorks) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubModel.fileExists = false; StubIC.addPoint('lights', -0.3, 1.5, 2.3)", "=noModelFile"), 0);
    SetGameTime(10000);
    headset.SetHandPointing(kRight, HandAt(0.2f, 0.0f, -0.3f));
    FRAMES(1);
    headset.SetHandPointing(kRight, HandAt(0.3f, 0.0f, -0.3f));
    FRAMES(30);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    FRAMES(3);

    EXPECT_EQ(EvalLua(L, "#StubModel.clones"), "0") << "no Virtual Hands";
    EXPECT_EQ(EvalLua(L, "#StubModel.loads"), "0") << "the engine is not asked to load a file that is not there";
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands:"), 1) << "one line, however long the game runs";
    EXPECT_EQ(CountLogLines("VirtualHandLeft.i3d"), 1) << "naming the first file that is missing";
    EXPECT_EQ(CountLogLines("VirtualHandRight.i3d"), 0) << "one line for the two files";
    EXPECT_NE(EvalLua(L, "runCommand('vrVirtualHandsDiagnostics')").find("model not loaded"), std::string::npos);

    // The Pointer Ray and Click Points are unaffected.
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "lights");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lights");
}

TEST_F(CompanionModTest, OnlyTheRightHandModelMissingLogsOnceNamingItAndLeavesNoVirtualHandsAtAll) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubModel.missingFiles = { ['VirtualHandRight.i3d'] = true }", "=noRightModelFile"), 0);
    SetGameTime(10000);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);
    FRAMES(30);

    EXPECT_EQ(EvalLua(L, "#StubModel.clones"), "0") << "no Virtual Hands";
    EXPECT_EQ(EvalLua(L, "#StubModel.loads"), "0") << "neither file is loaded when one is missing";
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands:"), 1);
    EXPECT_EQ(CountLogLines("VirtualHandRight.i3d"), 1) << "naming the missing file";
}

TEST_F(CompanionModTest, OnlyTheRightHandModelFailingToLoadLogsOnceDropsTheLoadedLeftModelAndLeavesNoVirtualHands) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubModel.failFiles = { ['VirtualHandRight.i3d'] = true }", "=failingRightModel"), 0);
    SetGameTime(10000);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);
    FRAMES(30);

    EXPECT_EQ(EvalLua(L, "#StubModel.clones"), "1") << "the left clone was made before the right file failed";
    EXPECT_EQ(EvalLua(L, "next(StubModel.hands()) == nil"), "true") << "and was deleted again";
    EXPECT_EQ(EvalLua(L, "StubModel.deleted[StubModel.loads[1].root]"), "true") << "the left file is dropped too";
    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands:"), 1);
    EXPECT_EQ(CountLogLines("VirtualHandRight.i3d"), 1) << "naming the file that failed";
    EXPECT_EQ(CountLogLines("failed to load"), 1);
}

TEST_F(CompanionModTest, AHandModelThatFailsToLoadLogsOnceAndLeavesNoVirtualHandsWhilePointingStillWorks) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubModel.failLoad = true; StubIC.addPoint('lights', -0.3, 1.5, 2.3)", "=failingModel"), 0);
    SetGameTime(10000);
    headset.SetHandPointing(kRight, HandAt(0.2f, 0.0f, -0.3f));
    FRAMES(1);
    headset.SetHandPointing(kRight, HandAt(0.3f, 0.0f, -0.3f));
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

// ---- Finger curl (ticket #86) ----

namespace {

// Reads how far each finger of a Virtual Hand is curled, from the joints' rotations away from the model's rest
// pose (StubModel.curl: radians toward the palm).
constexpr const char* kCurlReaders = R"luau(
    _G.FINGERS = { "thumb", "index", "middle", "ring", "little" }
    -- the curl of the three joints of one finger of "left" / "right", as { joint1, joint2, joint3 }
    function _G.fingerCurl(which, finger)
        local hand = StubModel.hands()[which]
        assert(hand ~= nil, "there is a " .. which .. " Virtual Hand")
        local curls = {}
        for segment = 1, 3 do curls[segment] = StubModel.curl(hand, finger .. segment) end
        return curls
    end
    -- every joint of a finger is curled by `expected(segment)` radians
    function _G.assertFinger(which, finger, expected, message)
        local curls = fingerCurl(which, finger)
        for segment = 1, 3 do
            local want = expected(segment)
            assert(math.abs(curls[segment] - want) < 1e-3,
                string.format("%s %s%d is curled %.4f rad, expected %.4f (%s)", which, finger, segment, curls[segment], want, message or ""))
        end
    end
    function _G.assertStill(which, finger, message) assertFinger(which, finger, function() return 0 end, message) end
)luau";

} // namespace

TEST_F(CompanionModTest, TheIndexFingerCurlsInProportionToTheTriggerAndNoOtherFingerMoves) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kCurlReaders, "=curlReaders"), 0);
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.fullIndex = fingerCurl('right', 'index')
        for segment = 1, 3 do
            assert(fullIndex[segment] > math.rad(30) and fullIndex[segment] <= math.rad(110),
                string.format("index%d at full trigger is curled %.1f degrees", segment, math.deg(fullIndex[segment])))
        end
        for _, finger in ipairs({ 'middle', 'ring', 'little', 'thumb' }) do assertStill('right', finger, 'trigger only') end
    )luau", "=fullTrigger"), 0);

    headset.SetHandInputs(kRight, Trigger(0.5f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assertFinger('right', 'index', function(segment) return fullIndex[segment] * 0.5 end, 'half trigger')
        for _, finger in ipairs({ 'middle', 'ring', 'little', 'thumb' }) do assertStill('right', finger, 'half trigger') end
    )luau", "=halfTrigger"), 0);
}

TEST_F(CompanionModTest, TheMiddleRingAndLittleFingersCurlInProportionToTheGripIndependentlyOfTheTrigger) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kCurlReaders, "=curlReaders"), 0);
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);

    // A full squeeze with the trigger left alone.
    headset.SetHandInputs(kRight, Squeeze(0.0f, 1.0f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.fullGrip = fingerCurl('right', 'middle')
        for segment = 1, 3 do
            assert(fullGrip[segment] > math.rad(30) and fullGrip[segment] <= math.rad(110),
                string.format("middle%d at full grip is curled %.1f degrees", segment, math.deg(fullGrip[segment])))
        end
        for _, finger in ipairs({ 'middle', 'ring', 'little' }) do
            assertFinger('right', finger, function(segment) return fullGrip[segment] end, 'full grip')
        end
        assertStill('right', 'index', 'grip only')
        assertStill('right', 'thumb', 'grip only')
    )luau", "=fullGrip"), 0);

    // Both at once, each at its own amount: the trigger and the grip do not influence each other.
    headset.SetHandInputs(kRight, Squeeze(0.25f, 0.75f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        for _, finger in ipairs({ 'middle', 'ring', 'little' }) do
            assertFinger('right', finger, function(segment) return fullGrip[segment] * 0.75 end, 'three quarters grip')
        end
        local quarter = fingerCurl('right', 'index')
        assert(quarter[1] > 0 and quarter[1] < fullGrip[1], "the index finger is part way down with a quarter trigger")
        _G.quarterIndex = quarter
    )luau", "=bothInputs"), 0);
    headset.SetHandInputs(kRight, Squeeze(1.0f, 0.0f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assertFinger('right', 'index', function(segment) return quarterIndex[segment] * 4 end, 'a quarter trigger is a quarter of a full one')
        for _, finger in ipairs({ 'middle', 'ring', 'little' }) do assertStill('right', finger, 'trigger only') end
    )luau", "=triggerOnly"), 0);
}

TEST_F(CompanionModTest, LettingGoRestoresTheModelsRelaxedPoseAndTheThumbNeverMoves) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kCurlReaders, "=curlReaders"), 0);
    SetGameTime(10000);

    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        -- an open hand: every joint is exactly where the model put it, not turned to some zero rotation
        local hand = StubModel.hands().right
        for segment = 1, 3 do
            local x, y, z, w = getQuaternion(StubModel.joint(hand, 'index' .. segment))
            assert(math.abs(x) > 0.02 and math.abs(y) + math.abs(z) > 0.01 and w < 0.9999, "the rest pose of index" .. segment .. " is a bent, twisted one")
        end
        for _, finger in ipairs(FINGERS) do assertStill('right', finger, 'open hand') end
        _G.thumbRest = { getQuaternion(StubModel.joint(hand, 'thumb2')) }
    )luau", "=openHand"), 0);

    headset.SetHandInputs(kRight, Squeeze(1.0f, 1.0f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(fingerCurl('right', 'index')[1] > 0.5 and fingerCurl('right', 'little')[1] > 0.5, "a fist")
        assertStill('right', 'thumb', 'the thumb stays relaxed in a fist')
        local now = { getQuaternion(StubModel.joint(StubModel.hands().right, 'thumb2')) }
        for i = 1, 4 do assert(now[i] == thumbRest[i], "the thumb's rotation is untouched") end
    )luau", "=fist"), 0);

    headset.SetHandInputs(kRight, Squeeze(0.0f, 0.0f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        for _, finger in ipairs(FINGERS) do assertStill('right', finger, 'open again') end
    )luau", "=openAgain"), 0);
}

TEST_F(CompanionModTest, TheRightHandOfTheMirroredFileCurlsOnItsOwnInputsLikeTheLeftHandAndTheLeftHandIsNotAffected) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kCurlReaders, "=curlReaders"), 0);
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);

    headset.SetHandInputs(kLeft, Squeeze(1.0f, 1.0f));
    headset.SetHandInputs(kRight, Squeeze(0.0f, 0.0f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(#StubModel.scaledNodes(StubModel.model(StubModel.hands().right)) == 0, "the right hand is mirrored in its file, not by a scale")
        _G.leftFull = fingerCurl('left', 'index')
        for segment = 1, 3 do assert(leftFull[segment] > math.rad(30), "the left index finger curls toward the palm") end
        for _, finger in ipairs({ 'middle', 'ring', 'little' }) do
            assertFinger('left', finger, function(segment) return leftFull[segment] end, 'left grip, same amount as the trigger')
        end
        assertStill('left', 'thumb', 'left thumb')
        for _, finger in ipairs(FINGERS) do assertStill('right', finger, 'the right hand did not follow the left') end
    )luau", "=leftOnly"), 0);

    headset.SetHandInputs(kLeft, Squeeze(0.5f, 0.0f));
    headset.SetHandInputs(kRight, Squeeze(0.0f, 1.0f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assertFinger('left', 'index', function(segment) return leftFull[segment] * 0.5 end, 'left half trigger')
        for _, finger in ipairs({ 'middle', 'ring', 'little' }) do assertStill('left', finger, 'left grip released') end
        assertStill('right', 'index', 'right trigger released')
        local rightGrip = fingerCurl('right', 'middle')
        for segment = 1, 3 do assert(math.abs(rightGrip[segment] - leftFull[segment]) < 1e-3, "both hands curl the same way for the same input") end
    )luau", "=bothHands"), 0);
}

TEST_F(CompanionModTest, AHiddenVirtualHandIsCurledCorrectlyAsSoonAsItReappears) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kCurlReaders, "=curlReaders"), 0);
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    headset.SetHandInputs(kRight, Squeeze(1.0f, 1.0f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, "_G.fist = fingerCurl('right', 'index')", "=fist"), 0);

    // In a menu the hand is hidden and the player lets go: when the menu closes the hand is open.
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(2);
    headset.SetHandInputs(kRight, Squeeze(0.0f, 0.0f));
    FRAMES(5);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(handIsShown('right'), "back again")
        for _, finger in ipairs(FINGERS) do assertStill('right', finger, 'let go while hidden') end
    )luau", "=openOnReturn"), 0);

    // And the other way round: pulled while hidden, curled the moment it is back.
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(2);
    headset.SetHandInputs(kRight, Squeeze(0.5f, 1.0f));
    FRAMES(5);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assertFinger('right', 'index', function(segment) return fist[segment] * 0.5 end, 'half trigger on return')
        assertFinger('right', 'little', function(segment) return fist[segment] end, 'full grip on return')
    )luau", "=curledOnReturn"), 0);
}

TEST_F(CompanionModTest, AModelMissingSomeJointsLogsOnceAndCurlsTheJointsItHas) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kCurlReaders, "=curlReaders"), 0);
    // the model's index finger lacks its middle joint and its ring finger all three (the names are the file's own)
    ASSERT_EQ(RunLuauChunk(L, "StubModel.missingJoints = { ['Middle.001'] = true, ['Lower.003'] = true, ['Middle.003'] = true, ['Top.003'] = true }", "=lackingJoints"), 0);
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);
    headset.SetHandInputs(kRight, Squeeze(1.0f, 1.0f));
    FRAMES(30);

    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands: the model lacks joints"), 1) << "once, not per hand and not per frame";
    EXPECT_EQ(CountLogLines("Middle.001, Lower.003, Middle.003, Top.003"), 1) << "naming the joints as the model does";
    EXPECT_EQ(EvalLua(L, "handIsShown('right') and handIsShown('left')"), "true") << "the hands still show";
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local hand = StubModel.hands().right
        assert(StubModel.joint(hand, 'index2') == nil and StubModel.joint(hand, 'ring2') == nil, "the stub really lacks them")
        assert(StubModel.curl(hand, 'index1') > math.rad(30) and StubModel.curl(hand, 'index3') > math.rad(30), "the index joints it has curl")
        for _, name in ipairs({ 'middle1', 'middle2', 'middle3', 'little1', 'little2', 'little3' }) do
            assert(StubModel.curl(hand, name) > math.rad(30), name .. " curls")
        end
        for segment = 1, 3 do assert(StubModel.curl(hand, 'thumb' .. segment) == 0, "the thumb is still") end
    )luau", "=curlsWhatItHas"), 0);
}

TEST_F(CompanionModTest, AFingerWhoseBaseBoneIsMissingStaysStillWhileTheOtherFingersCurl) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kCurlReaders, "=curlReaders"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubModel.missingJoints = { ['Ring Finger'] = true }", "=noRingBone"), 0);
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    headset.SetHandInputs(kRight, Squeeze(1.0f, 1.0f));
    FRAMES(5);

    EXPECT_EQ(CountLogLines("[FS25_VR] Virtual Hands: the model lacks joints"), 1);
    EXPECT_EQ(CountLogLines("Ring Finger, Lower.003, Middle.003, Top.003"), 1) << "the finger is named by its base bone and its joints";
    EXPECT_EQ(EvalLua(L, "handIsShown('right')"), "true");
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local hand = StubModel.hands().right
        assert(StubModel.joint(hand, 'ring1') == nil, "the mod has no way to reach the ring joints")
        for _, name in ipairs({ 'Lower.003', 'Middle.003', 'Top.003' }) do
            assert(StubModel.curlOfNamed(hand, name) == 0, name .. " stays at rest: a finger with no base bone is not curled")
        end
        assert(fingerCurl('right', 'middle')[1] > math.rad(30) and fingerCurl('right', 'little')[1] > math.rad(30), "the other fingers curl")
    )luau", "=otherFingersCurl"), 0);
}

namespace {

// node name -> name of its parent TransformGroup ("" at the top) in an .i3d file's scene
std::map<std::string, std::string> TransformGroupParents(const std::string& xml) {
    std::map<std::string, std::string> parents;
    std::vector<std::string> open;
    const std::regex tag(R"re(<TransformGroup\s+name="([^"]*)"[^>]*?(/?)>|</TransformGroup>)re");
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), tag); it != std::sregex_iterator(); ++it) {
        if ((*it)[1].matched) {
            parents[(*it)[1]] = open.empty() ? "" : open.back();
            if ((*it)[2].str() != "/") open.push_back((*it)[1]);
        } else if (!open.empty()) {
            open.pop_back();
        }
    }
    return parents;
}

// The world position of every bone of an .i3d's scene, composed the way the engine composes node transforms:
// translation and rotation in degrees (Euler, R = Rz * Ry * Rx when zyx is true, the convention the GIANTS
// exporter writes, else R = Ry * Rx * Rz) of each TransformGroup below its parent. The scene's first group (the
// hand's root) is the identity. The skinned mesh's bind pose is this composed skeleton (the .shapes file holds
// no bind matrices), so it is the skeleton the mesh was modelled around or the hand is distorted in the game.
// extraXDegrees turns the named bones further about their own X axis (a finger curl). Besides each bone's position the
// result holds the point endLength along its -Z axis (the way the bones run) under "<bone>@end". The models are in
// metres, so positions and lengths are metres.
std::map<std::string, std::array<double, 3>> ComposedBonePositions(const std::string& xml, bool zyx,
                                                                   const std::map<std::string, double>& extraXDegrees = {},
                                                                   double endLength = 0.25 * 0.0875) {
    using Mat = std::array<double, 9>;
    const auto multiply = [](const Mat& a, const Mat& b) {
        Mat r{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) r[3 * i + j] += a[3 * i + k] * b[3 * k + j];
        return r;
    };
    const auto rotation = [](char axis, double degrees) {
        const double a = degrees * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
        if (axis == 'x') return Mat{ 1, 0, 0, 0, c, -s, 0, s, c };
        if (axis == 'y') return Mat{ c, 0, s, 0, 1, 0, -s, 0, c };
        return Mat{ c, -s, 0, s, c, 0, 0, 0, 1 };
    };
    const auto numbers = [](const std::string& text) {
        std::array<double, 3> v{};
        std::istringstream in(text);
        in >> v[0] >> v[1] >> v[2];
        return v;
    };
    struct Frame { Mat rotation; std::array<double, 3> position; };
    std::map<std::string, std::array<double, 3>> positions;
    std::vector<Frame> open;
    const std::regex tag(R"re(<TransformGroup\s+([^>]*?)(/?)>|</TransformGroup>)re");
    const std::regex attribute(R"re((\w+)="([^"]*)")re");
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), tag); it != std::sregex_iterator(); ++it) {
        if (!(*it)[1].matched) {
            if (!open.empty()) open.pop_back();
            continue;
        }
        std::map<std::string, std::string> attributes;
        const std::string attributeText = (*it)[1];
        for (auto a = std::sregex_iterator(attributeText.begin(), attributeText.end(), attribute); a != std::sregex_iterator(); ++a)
            attributes[(*a)[1]] = (*a)[2];
        const auto t = numbers(attributes.count("translation") ? attributes["translation"] : "0 0 0");
        const auto r = numbers(attributes.count("rotation") ? attributes["rotation"] : "0 0 0");
        Mat local = zyx ? multiply(multiply(rotation('z', r[2]), rotation('y', r[1])), rotation('x', r[0]))
                        : multiply(multiply(rotation('y', r[1]), rotation('x', r[0])), rotation('z', r[2]));
        const auto extra = extraXDegrees.find(attributes["name"]);
        if (extra != extraXDegrees.end()) local = multiply(local, rotation('x', extra->second));
        Frame frame{ local, t };
        if (!open.empty()) {
            const Frame& parent = open.back();
            for (int i = 0; i < 3; ++i)
                frame.position[i] = parent.position[i] + parent.rotation[3 * i] * t[0] + parent.rotation[3 * i + 1] * t[1] + parent.rotation[3 * i + 2] * t[2];
            frame.rotation = multiply(parent.rotation, local);
        }
        positions[attributes["name"]] = frame.position;
        positions[attributes["name"] + "@end"] = { frame.position[0] - endLength * frame.rotation[2], frame.position[1] - endLength * frame.rotation[5],
                                                   frame.position[2] - endLength * frame.rotation[8] };
        if ((*it)[2].str() != "/") open.push_back(frame);
    }
    return positions;
}

double Distance(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

// The shipped models are the Gloved Hand in metres: its original units times this (VRCompanion's scale is gone, the
// files carry the size). The right file is the left one mirrored across X = 0.
constexpr double kMetresPerUnit = 0.0875;

std::string ShippedModel(bool right) {
    return ReadFile(right ? "mods/FS25_VRCompanion/models/VirtualHandRight.i3d" : "mods/FS25_VRCompanion/models/VirtualHandLeft.i3d");
}

const char* HandLabel(bool right) { return right ? "right model file" : "left model file"; }

} // namespace

TEST_F(CompanionModTest, TheJointTableNamesThreeCurlJointsBelowTheBaseBoneOfEveryFingerInBothShippedModels) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    for (const bool right : { false, true }) {
    SCOPED_TRACE(HandLabel(right));
    const std::string model = ShippedModel(right);
    ASSERT_FALSE(model.empty());
    const auto parents = TransformGroupParents(model);

    // one line per finger: name|base bone|joint 1|joint 2|joint 3
    const std::string table = EvalLua(L, R"luau((function()
        local lines = {}
        for _, finger in ipairs(VRCompanion.VIRTUAL_HAND_FINGERS) do
            table.insert(lines, finger.name .. "|" .. finger.base .. "|" .. table.concat(finger.joints, "|"))
        end
        return table.concat(lines, "\n")
    end)())luau");
    std::istringstream lines(table);
    std::string line;
    std::vector<std::string> fingerNames;
    while (std::getline(lines, line)) {
        std::vector<std::string> names;
        std::istringstream parts(line);
        for (std::string part; std::getline(parts, part, '|');) names.push_back(part);
        ASSERT_EQ(names.size(), 5u) << line;
        fingerNames.push_back(names[0]);
        SCOPED_TRACE(line);
        ASSERT_TRUE(parents.count(names[1])) << names[1] << " is a bone of the model";
        EXPECT_EQ(parents.at(names[1]), "Hand") << "a finger's base bone hangs off the Hand bone";
        for (size_t joint = 2; joint < 5; ++joint) {
            ASSERT_TRUE(parents.count(names[joint])) << names[joint] << " is a bone of the model";
            EXPECT_EQ(parents.at(names[joint]), names[joint - 1]) << names[joint] << " follows " << names[joint - 1];
        }
    }
    EXPECT_EQ(fingerNames, (std::vector<std::string>{ "index", "middle", "ring", "little" })) << "the thumb is never curled";
    EXPECT_EQ(EvalLua(L, "VRCompanion.VIRTUAL_HAND_FINGERS[1].input .. VRCompanion.VIRTUAL_HAND_FINGERS[2].input"), "triggergrip");
    }
}

// The Gloved Hand's bones as the Blender armature has them at rest ("Gloved Hand LP.blend": each bone's head, in the
// exporter's Y-up axes x, z, -y), in the mesh's original units. The models' bone transforms must compose to these
// times 0.0875 m per unit, mirrored across X = 0 for the right hand.
TEST_F(CompanionModTest, TheShippedModelsBoneTransformsComposeToTheSkeletonTheMeshWasModelledAroundInMetres) {
    const std::map<std::string, std::array<double, 3>> armature = {
        { "Wrist", { 0.0630, 0.0551, 0.9691 } },        { "Hand", { 0.0630, 0.0551, 0.5246 } },
        { "Thumb", { 0.0695, 0.0551, -0.1343 } },       { "Lower", { 0.5854, 0.0551, 0.0182 } },
        { "Middle", { 0.7947, 0.0111, -0.1689 } },      { "Top", { 0.9515, -0.0800, -0.4387 } },
        { "Index Finger", { 0.0695, 0.0551, -0.1343 } }, { "Lower.001", { 0.3777, 0.0551, -0.6210 } },
        { "Middle.001", { 0.4523, 0.0218, -0.9363 } },  { "Top.001", { 0.5042, -0.0903, -1.2298 } },
        { "Middle Finger", { 0.0695, 0.0551, -0.1343 } }, { "Lower.002", { 0.1051, 0.0551, -0.7313 } },
        { "Middle.002", { 0.1343, 0.0317, -1.0804 } },  { "Top.002", { 0.1603, -0.0746, -1.3587 } },
        { "Ring Finger", { 0.0695, 0.0551, -0.1343 } }, { "Lower.003", { -0.1674, 0.0551, -0.6794 } },
        { "Middle.003", { -0.1902, 0.0418, -0.9521 } }, { "Top.003", { -0.2127, -0.0885, -1.2401 } },
        { "Pinky", { 0.0695, 0.0551, -0.1343 } },       { "Lower.004", { -0.3816, 0.0551, -0.6307 } },
        { "Middle.004", { -0.4387, 0.0330, -0.7780 } }, { "Top.004", { -0.5405, -0.0757, -0.9996 } },
    };

    for (const bool right : { false, true }) {
        const std::string model = ShippedModel(right);
        ASSERT_FALSE(model.empty());
        for (const bool zyx : { true, false }) {
            SCOPED_TRACE(std::string(HandLabel(right)) + (zyx ? ", Euler R = Rz * Ry * Rx (the exporter's convention)" : ", Euler R = Ry * Rx * Rz"));
            const auto composed = ComposedBonePositions(model, zyx);
            for (const auto& [name, units] : armature) {
                ASSERT_TRUE(composed.count(name)) << name << " is a bone of the model";
                const std::array<double, 3> want = { (right ? -1.0 : 1.0) * units[0] * kMetresPerUnit, units[1] * kMetresPerUnit, units[2] * kMetresPerUnit };
                // the other Euler order is only a few hundredths of a degree off for these small bends
                EXPECT_LT(Distance(composed.at(name), want), zyx ? 0.0005 : 0.002)
                    << name << " composes to (" << composed.at(name)[0] << ", " << composed.at(name)[1] << ", " << composed.at(name)[2]
                    << ") m, the armature has it at (" << want[0] << ", " << want[1] << ", " << want[2] << ") m";
            }
        }
    }
}

TEST_F(CompanionModTest, TheShippedModelsAreRealHandSizeWristToFingertipChainAsLongAsTheMeshAndNoFingerSpirals) {
    for (const bool right : { false, true }) {
        SCOPED_TRACE(HandLabel(right));
        const std::string model = ShippedModel(right);
        ASSERT_FALSE(model.empty());
        const auto bones = ComposedBonePositions(model, true);
        // 2.25 and 2.33 units times 0.0875 m per unit: 0.197 and 0.204 m, the length of a real hand from the wrist to the
        // middle fingertip joint. The first export of the model composed to 0.46 units (0.040 m): a fifth of the mesh.
        EXPECT_NEAR(Distance(bones.at("Wrist"), bones.at("Top.001")), 0.197, 0.005);
        EXPECT_NEAR(Distance(bones.at("Wrist"), bones.at("Top.002")), 0.204, 0.005);
        // across the knuckles (the metacarpals are 0.99 units apart, 0.086 m; the first finger joints are closer)
        EXPECT_NEAR(Distance(bones.at("Lower.001"), bones.at("Lower.004")), 0.0664, 0.004);
        const std::vector<std::vector<std::string>> fingers = {
            { "Index Finger", "Lower.001", "Middle.001", "Top.001" }, { "Middle Finger", "Lower.002", "Middle.002", "Top.002" },
            { "Ring Finger", "Lower.003", "Middle.003", "Top.003" },  { "Pinky", "Lower.004", "Middle.004", "Top.004" },
            { "Thumb", "Lower", "Middle", "Top" },
        };
        for (const auto& finger : fingers) {
            double along = 0;
            for (size_t joint = 1; joint < finger.size(); ++joint) along += Distance(bones.at(finger[joint - 1]), bones.at(finger[joint]));
            // a finger runs about straight (the thumb curves most, to 0.82): its ends are nearly as far apart as the joints along it, unlike a heap of 90 degree turns (0.3 to 0.5)
            EXPECT_GT(Distance(bones.at(finger.front()), bones.at(finger.back())), 0.8 * along) << finger.front() << " finger";
            EXPECT_GT(along, 0.06) << finger.front() << " finger is a real finger length (metres)";
        }
    }
}

// The size and the handedness are in the files: no node of either model carries a scale (the engine ignores one above
// a skinned mesh), and the right model is the left one mirrored across X = 0 with the same bone names and the same
// parents, each bone's rotation D R D (Euler X as it was, Euler Y and Z negated: a proper rotation, a small bend
// like the left one's, never a 90 degree pile-up) and each position with its x negated.
TEST_F(CompanionModTest, TheRightModelFileIsTheLeftOneMirroredWithTheSameBonesAndNoNodeOfEitherCarriesAScale) {
    const std::string left = ShippedModel(false), right = ShippedModel(true);
    ASSERT_FALSE(left.empty());
    ASSERT_FALSE(right.empty());
    for (const std::string* model : { &left, &right }) {
        EXPECT_EQ(model->find("scale=\""), std::string::npos) << "a node of the model carries a scale";
    }
    EXPECT_NE(left.find("externalShapesFile=\"VirtualHandLeft.i3d.shapes\""), std::string::npos);
    EXPECT_NE(right.find("externalShapesFile=\"VirtualHandRight.i3d.shapes\""), std::string::npos) << "the right model names its own shapes file";
    EXPECT_EQ(left.find("castsShadows=\"true\""), std::string::npos);
    EXPECT_EQ(right.find("castsShadows=\"true\""), std::string::npos);
    EXPECT_NE(right.find("castsShadows=\"false\""), std::string::npos);

    EXPECT_EQ(TransformGroupParents(right), TransformGroupParents(left)) << "the same bone names and hierarchy";
    const auto leftBones = ComposedBonePositions(left, true), rightBones = ComposedBonePositions(right, true);
    for (const auto& [name, position] : leftBones) {
        ASSERT_TRUE(rightBones.count(name)) << name;
        const std::array<double, 3> mirrored = { -position[0], position[1], position[2] };
        EXPECT_LT(Distance(rightBones.at(name), mirrored), 0.0005) << name << " of the right model is the left one's with x negated";
    }

    // every local rotation: read straight from the attributes
    const auto rotations = [](const std::string& xml) {
        std::map<std::string, std::array<double, 3>> found;
        const std::regex tag(R"re(<TransformGroup\s+([^>]*?)/?>)re");
        const std::regex attribute(R"re((\w+)="([^"]*)")re");
        for (auto it = std::sregex_iterator(xml.begin(), xml.end(), tag); it != std::sregex_iterator(); ++it) {
            std::map<std::string, std::string> attributes;
            const std::string text = (*it)[1];
            for (auto a = std::sregex_iterator(text.begin(), text.end(), attribute); a != std::sregex_iterator(); ++a) attributes[(*a)[1]] = (*a)[2];
            std::array<double, 3> degrees{};
            std::istringstream in(attributes.count("rotation") ? attributes["rotation"] : "0 0 0");
            in >> degrees[0] >> degrees[1] >> degrees[2];
            found[attributes["name"]] = degrees;
        }
        return found;
    };
    const auto leftRotations = rotations(left), rightRotations = rotations(right);
    ASSERT_EQ(leftRotations.size(), rightRotations.size());
    for (const auto& [name, degrees] : leftRotations) {
        SCOPED_TRACE(name);
        ASSERT_TRUE(rightRotations.count(name));
        const auto& mirrored = rightRotations.at(name);
        EXPECT_NEAR(mirrored[0], degrees[0], 0.01) << "Euler X is unchanged by the mirror";
        EXPECT_NEAR(mirrored[1], -degrees[1], 0.01) << "Euler Y changes sign";
        EXPECT_NEAR(mirrored[2], -degrees[2], 0.01) << "Euler Z changes sign";
        // small bends: the exporter's 90 degree pile-ups are gone (only the thumb's base is turned past 60 degrees)
        for (const double angle : mirrored) EXPECT_LT(std::abs(angle), name == "Thumb" ? 120.0 : 60.0) << "a rotation of " << angle << " degrees";
    }
}

// The curl the mod applies (VRCompanion.FINGER_CURL_DIRECTION * FINGER_CURL_DEGREES about each joint's own X), played
// on the shipped models' composed bones, for both hands. The models' axes: fingers run along -Z, the palm faces -Y in
// BOTH files (the right file is mirrored across X, which leaves Y and Z alone). The knuckle line of the fingers is
// 0.17 units (0.015 m) behind the palm plane (the palm surface is at y -0.115, the knuckles at 0.055). One direction
// number serves both hands because the right file's bone frames are proper-rotation mirrors of the left's.
TEST_F(CompanionModTest, AFullCurlOfEachShippedModelFoldsEveryFingerTowardThePalmAndRestsItsTipOnIt) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    const double direction = std::stod(EvalLua(L, "VRCompanion.FINGER_CURL_DIRECTION"));
    std::array<double, 3> degrees{};
    for (int segment = 0; segment < 3; ++segment) degrees[segment] = std::stod(EvalLua(L, "VRCompanion.FINGER_CURL_DEGREES[" + std::to_string(segment + 1) + "]"));

    const std::vector<std::vector<std::string>> fingers = {
        { "Lower.001", "Middle.001", "Top.001" }, { "Lower.002", "Middle.002", "Top.002" },
        { "Lower.003", "Middle.003", "Top.003" }, { "Lower.004", "Middle.004", "Top.004" },
    };
    std::map<std::string, double> leftTipFront;
    for (const bool right : { false, true }) {
        SCOPED_TRACE(HandLabel(right));
        const std::string model = ShippedModel(right);
        ASSERT_FALSE(model.empty());
        const auto rest = ComposedBonePositions(model, true);
        for (const auto& finger : fingers) {
            SCOPED_TRACE(finger[0]);
            std::map<std::string, double> half, full;
            for (int segment = 0; segment < 3; ++segment) {
                full[finger[segment]] = direction * degrees[segment];
                half[finger[segment]] = 0.5 * direction * degrees[segment];
            }
            const auto curled = ComposedBonePositions(model, true, full);
            const auto partly = ComposedBonePositions(model, true, half);
            const double knuckleY = rest.at(finger[0])[1], knuckleZ = rest.at(finger[0])[2];
            const auto inFront = [&](const std::map<std::string, std::array<double, 3>>& pose, const std::string& point) { return knuckleY - pose.at(point)[1]; };

            // toward the palm: the middle and the top joint swing to the palm side (-Y) as the curl grows, the most at full
            EXPECT_GT(inFront(partly, finger[1]), inFront(rest, finger[1]) + 0.05 * kMetresPerUnit) << "the middle joint at half curl";
            EXPECT_GT(inFront(curled, finger[1]), inFront(partly, finger[1])) << "the middle joint at full curl";
            EXPECT_GT(inFront(curled, finger[2]), inFront(rest, finger[2]) + 0.1 * kMetresPerUnit) << "the top joint";
            // and back toward the wrist, not out past the fingertips
            EXPECT_GT(curled.at(finger[2] + "@end")[2], knuckleZ + 0.2 * kMetresPerUnit) << "the fingertip folds back over the palm";
            // the tip rests on the palm: 0.05 to 0.45 units (0.004 to 0.039 m) in front of the knuckle line (the palm plane
            // is at 0.17 units), not through the back of the hand (an earlier 80, 100, 65 degrees ended the index tip behind the line)
            const double tipFront = inFront(curled, finger[2] + "@end");
            EXPECT_GT(tipFront, 0.05 * kMetresPerUnit) << "the tip does not go through the palm";
            EXPECT_LT(tipFront, 0.45 * kMetresPerUnit) << "the tip does not stick out of the palm";
            // and the right hand folds exactly as the left one does
            if (!right) leftTipFront[finger[0]] = tipFront;
            else EXPECT_NEAR(tipFront, leftTipFront[finger[0]], 0.0005) << "the right hand curls like the left one";
        }
    }
}
TEST_F(CompanionModTest, PullingTheTriggerAndSqueezingTheGripLeavesThePointerRayAtTheFingertipAndTheClickAsTheyWere) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kCurlReaders, "=curlReaders"), 0);
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, 0.0f, -0.30f, 0.25f, -0.10f, -0.20f);
    FRAMES(3);

    // The ray starts at the relaxed index fingertip of the right hand, 0.0052 m to the right, 0.0905 m along the fingers and
    // 0.0441 m toward the thumb of the grip pose (the grip pose here is not turned), and runs along the finger,
    // whatever the fingers are doing: the cab turns tracking space by a half turn about Y.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local ray = VRCompanion:getPointerRay()
        assert(ray ~= nil, "a Pointer Ray")
        local ex, ey, ez = expectedOrigin({ x = 0, y = 0, z = 0, yaw = 0 }, math.pi, 0.25 + 0.0052, -0.10 - 0.0905, -0.20 - 0.0441)
        assert(near(ray.ox, ex) and near(ray.oy, ey) and near(ray.oz, ez), string.format("the ray starts at the fingertip before the pull: %.3f %.3f %.3f vs %.3f %.3f %.3f", ray.ox, ray.oy, ray.oz, ex, ey, ez))
        local dx, dy, dz = turnedY(math.pi, -0.3349, -0.9232, -0.1885)
        assert(math.abs(ray.dx - dx) < 2e-3 and math.abs(ray.dy - dy) < 2e-3 and math.abs(ray.dz - dz) < 2e-3, "and runs along the finger")
        _G.originBefore = { ray.ox, ray.oy, ray.oz, ray.dx, ray.dy, ray.dz }
        -- a Click Point on the ray, two metres along the finger
        StubIC.addPoint('lights', ray.ox + ray.dx * 2, ray.oy + ray.dy * 2, ray.oz + ray.dz * 2)
    )luau", "=rayBefore"), 0);

    headset.SetHandInputs(kRight, Squeeze(0.45f, 1.0f));  // a trigger part way down: curled, but not a click
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local ray = VRCompanion:getPointerRay()
        for i, v in ipairs({ ray.ox, ray.oy, ray.oz, ray.dx, ray.dy, ray.dz }) do assert(near(v, originBefore[i]), "the ray did not move with the fingers") end
        assert(fingerCurl('right', 'index')[1] > 0, "the index finger is curling")
    )luau", "=rayDuring"), 0);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "lights") << "the Click Point is aimed at";
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0") << "a part-way trigger clicks nothing";

    headset.SetHandInputs(kRight, Squeeze(1.0f, 1.0f));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lights") << "the full pull clicks the Click Point as it always did";
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "1");
}

// The Pointer Ray starts at the index fingertip of the glove and runs along its finger from the knuckle
// (VRCompanion.VIRTUAL_HAND_POINTER). The table is derived offline from the glove's mesh with the same model-to-grip offset
// the hand is drawn with, so it must still agree with the knuckle bone, as the hand puts it into the grip frame, and with
// the glove's mesh. Nudging VIRTUAL_HAND_MODEL_TO_GRIP without deriving the table again fails here.
TEST_F(CompanionModTest, TheRaysFingertipAndDirectionAreTheIndexFingerOfTheGloveFromItsKnuckleInBothGripFrames) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    PICK_UP(kRight, 0.30f, -0.20f, -0.40f, 0.25f, -0.10f, -0.20f);
    PICK_UP(kLeft, -0.30f, -0.20f, -0.40f, -0.25f, -0.10f, -0.20f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        for _, which in ipairs({ "left", "right" }) do
            local hand = StubModel.hands()[which]
            local pointer = VRCompanion.VIRTUAL_HAND_POINTER[which]
            local kx, ky, kz = StubModel.jointInGripFrame(hand, "index1")  -- the index knuckle (Lower.001)
            local tip, direction = pointer.tip, pointer.direction
            local vx, vy, vz = tip.x - kx, tip.y - ky, tip.z - kz
            local length = math.sqrt(vx * vx + vy * vy + vz * vz)
            -- a real index finger is 0.07 to 0.08 m from the knuckle joint to the tip
            assert(length > 0.065 and length < 0.09, which .. string.format(": the fingertip is %.4f m from the knuckle", length))
            -- the direction is the way from the knuckle to the tip, to within a degree or two
            local cosine = (vx * direction.x + vy * direction.y + vz * direction.z) / length
            assert(cosine > math.cos(math.rad(2)), which .. string.format(": the direction is %.1f degrees off the knuckle to tip line", math.deg(math.acos(math.min(1, cosine)))))
            assert(math.abs(direction.x ^ 2 + direction.y ^ 2 + direction.z ^ 2 - 1) < 1e-3, which .. ": the direction is a unit vector")
            -- it runs toward the fingertips (-Y) and a little toward the thumb, as the index finger does
            assert(direction.y < -0.85, which .. ": the finger runs along -Y")
            assert(direction.z < 0, which .. ": and leans toward the thumb (-Z)")
            -- the tip is at the end of the glove finger: inside the glove's box, near its far end along the fingers
            local minX, minY, minZ, maxX, maxY, maxZ = StubModel.meshBoundsInGripFrame(hand)
            assert(tip.x > minX and tip.x < maxX and tip.y > minY and tip.y < maxY and tip.z > minZ and tip.z < maxZ, which .. ": the fingertip is inside the glove")
            assert(tip.y - minY < 0.03, which .. string.format(": the fingertip is %.3f m from the far end of the glove (the middle finger is a little longer)", tip.y - minY))
        end
    )luau", "=pointerOffset"), 0);
}