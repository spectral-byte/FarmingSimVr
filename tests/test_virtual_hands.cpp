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
