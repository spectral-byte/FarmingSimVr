// Hand Tools, the fishing rod (ADR-0031), seen from outside: the real Companion Mod runs against the real VRBridge over
// FakeTrackedHeadset, with the game stood in by Lua stubs (companion_mod_stubs.h, StubHandTool: a player on foot, and the Highlands
// Fishing DLC's rod, kStubHandToolFishingRod, shaped after the #125 probe dump: a specialization class with no global name, its
// handlers on the class only, a state machine recognised by the keys of spec.state). A test moves the fake Motion Controllers and
// the game clock, drives frames the way the game would and asserts what the player or the game would see: where the rod is, the
// ray the targeter cast, the calls the rod's handlers received and what the rod did with them (its state, its casts), what XInput
// answers for the Tool Hand's trigger, what the game's draw rendered, the console commands' answers and the log. Never the mod's
// own state.
// Prior art: test_hand_tools_horse_brush.cpp and test_hand_tools_chainsaw.cpp (handlers on the specialization class only, useOnce),
// test_hand_tools_trigger.cpp (the held-input rule), test_hand_tools_draw.cpp (overlays), test_hand_tools_describe_unsupported.cpp
// (the probe).

#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

namespace {

using namespace hand_tools_test;

// Lua helpers: where the rod is and what the targeter did, as the game would know it. Each ray is { x, y, z, dirX, dirY, dirZ } in
// the world.
//   isWhereTheGamePutsIt(tool)   the tool is below the camera's pitch node at the game's placement
//   isInTheHand(tool, which)     the tool hangs below the Virtual Hand
//   rodRay()                     the rod's root and the way to its tip
//   headRay()                    the first person camera: the ray the game casts without a Hand Tool
//   targeterRay()                what the targeter cast in the last game frame
//   sameRay(a, b)                the two rays are the same
//   useCalls(action)             the values the rod's handler for `action` was called with, in order, as one text ("1,0")
constexpr const char* kFishingRodHelpers = R"luau(
    function _G.isWhereTheGamePutsIt(tool)
        if getParent(tool.rootNode) ~= foot.cam.pitchNode then return false end
        local x, y, z = getTranslation(tool.rootNode)
        local p = StubHandTool.CAMERA_PLACEMENT
        return math.abs(x - p.x) < 1e-6 and math.abs(y - p.y) < 1e-6 and math.abs(z - p.z) < 1e-6
    end
    function _G.isInTheHand(tool, which) return isBelow(tool.rootNode, handHolder(which)) end
    function _G.rodRay() return { StubHandTool.rodRay(rod) } end
    function _G.headRay() return { StubHandTool.originalGetLookRay(foot.player) } end
    function _G.targeterRay() return { StubHandTool.lastRay() } end
    function _G.sameRay(a, b)
        for i = 1, 6 do
            if a[i] == nil or b[i] == nil or math.abs(a[i] - b[i]) > 1e-3 then return false end
        end
        return true
    end
    function _G.rayText(r) return r[1] and string.format("(%.3f %.3f %.3f) -> (%.3f %.3f %.3f)", table.unpack(r)) or "none" end
    function _G.useCalls(action)
        local values = {}
        for _, call in ipairs(StubHandTool.useCalls) do
            if call.tool == "fishingRod" and call.action == action then table.insert(values, tostring(call.value)) end
        end
        return table.concat(values, ",")
    end
)luau";

// The hand held so that the rod points well away from the way the head looks (down -Z).
constexpr float kAimPitch = 1.2f;
constexpr float kAimYaw = 1.0f;

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) already covers.
class VirtualGamepadHandToolsFishingRodTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };
    std::string stubs;

    // The mod started with a player on foot in a game with the Highlands Fishing DLC (its rod's handler as the variant says), the
    // Virtual Gamepad on and the game clock set (the in-use timeout is on it).
    void Start(const char* variant = "ok") {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        stubs = std::string(kXmlStubs) + FishingRodStubs(variant);
        StartOptions options;
        options.extraStubs = stubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, kFishingRodHelpers, "=fishingRodHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    // The rod held on foot with the right Motion Controller picked up and a few frames run: the common start of the tests below.
    void StartWithTheRodInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldFishingRod(L));
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(1);
        ASSERT_EQ(InTheHand("right"), "true") << Dump();
    }

    // Both the aim and the grip pose of the hand set (the Virtual Hand follows the grip pose), turned as kAimPitch and kAimYaw say.
    void AimTheHand(uint32_t hand, float x, float y, float z) {
        headset.SetHandPose(hand, HandAt(x, y, z, kAimPitch, kAimYaw));
        headset.SetHandGripPose(hand, HandAt(x, y, z, kAimPitch, kAimYaw));
    }

    std::string InTheHand(const char* which) { return EvalLua(L, std::string("isInTheHand(rod, '") + which + "')"); }
    std::string WhereTheGamePutsIt() { return EvalLua(L, "isWhereTheGamePutsIt(rod)"); }
    std::string Dump() { return EvalLua(L, "VRCompanion:getHandToolsDiagnostics()"); }
    std::string RodState() { return EvalLua(L, "StubHandTool.rodState(rod)"); }
    std::string UseCalls() { return EvalLua(L, "useCalls('ACTIVATE_HANDTOOL')"); }

    // What the game reads from XInput slot 3, the highest free slot of a machine without real pads.
    XINPUT_GAMEPAD Pad() {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    }
};

}  // namespace

// ---- Placement ------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsFishingRodTest, TheRodHeldOnFootHangsBelowTheRightVirtualHandWithItsHandleOffsetAndFollowsIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    EXPECT_EQ(WhereTheGamePutsIt(), "false");
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local offset = VRCompanion.HAND_TOOL_HANDLE_OFFSETS.fishingrod
        assert(offset ~= nil, "the rod has a handle offset")
        local x, y, z = getTranslation(rod.rootNode)
        assert(getParent(rod.rootNode) == handHolder('right'), "linked straight below the Virtual Hand's holder")
        assert(math.abs(x - offset.position.x) < 1e-6 and math.abs(y - offset.position.y) < 1e-6 and math.abs(z - offset.position.z) < 1e-6,
            "at the handle offset")
    )luau", "=offset"), 0);
    ASSERT_EQ(RunLuauChunk(L, "_G.before = { offsetInHolder(rod.rootNode, 'right') }", "=before"), 0);

    MOVE_HAND(kRight, 0.55f, 0.10f, -0.45f);
    FRAMES(1);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = offsetInHolder(rod.rootNode, 'right')
        assert(math.abs(x - before[1]) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4,
            "the rod stayed where it was in the hand")
    )luau", "=follows"), 0);
    EXPECT_NE(Dump().find("held Hand Tool: fishingrod (supported)"), std::string::npos) << Dump();
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, TheRodIsWhereTheGamePutsItSetDownUntrackedOutOfFirstPersonOrWithHandToolsOffAndBackInTheHandAfterwards) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());

    AdvanceGameTime(3100);  // set down: the Motion Controller lies still for longer than the in-use timeout
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "set down: the game's own attachToolToCamera put the rod in front of the view";
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_EQ(InTheHand("right"), "true") << "picked up again";

    headset.SetHandTracked(kRight, false);
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "untracked";
    headset.SetHandTracked(kRight, true);
    FRAMES(1);
    EXPECT_EQ(InTheHand("right"), "true") << "tracked again";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.thirdPerson(foot.player, true)", "=thirdPerson"), 0);
    FRAMES(1);
    EXPECT_EQ(InTheHand("right"), "false") << "third person: the game's own hold";
    EXPECT_EQ(EvalLua(L, "tostring(getParent(rod.rootNode) == foot.player.handNode)"), "true");
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.thirdPerson(foot.player, false)", "=firstPerson"), 0);
    FRAMES(1);
    EXPECT_EQ(InTheHand("right"), "true") << "first person again";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "vrHandTools off";
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    FRAMES(1);
    EXPECT_EQ(InTheHand("right"), "true") << "vrHandTools on";
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheLeftHandChosenAsTheToolHandTheRodGoesIntoTheLeftVirtualHandWithTheSameOffset) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldFishingRod(L));
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'left')", "=toolHandLeft"), 0);
    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    FRAMES(1);

    EXPECT_EQ(InTheHand("left"), "true") << Dump();
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local offset = VRCompanion.HAND_TOOL_HANDLE_OFFSETS.fishingrod
        local x, y, z = getTranslation(rod.rootNode)
        assert(getParent(rod.rootNode) == handHolder('left'), "linked straight below the left Virtual Hand's holder")
        assert(math.abs(x - offset.position.x) < 1e-6 and math.abs(y - offset.position.y) < 1e-6 and math.abs(z - offset.position.z) < 1e-6,
            "at the same handle offset")
    )luau", "=offset"), 0);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_EQ(InTheHand("left"), "true") << "the other hand being picked up does not take it";
    EXPECT_EQ(InTheHand("right"), "false");
}

// ---- The look ray ---------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheRodInTheToolHandTheTargetersRayRunsFromItsRootTowardItsTipAndIsTheHeadsOtherwise) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), rodRay())"), "true")
        << "from the rod's root along the rod, with nothing behind it: " << EvalLua(L, "rayText(targeterRay())") << " against " << EvalLua(L, "rayText(rodRay())");
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "false");

    AimTheHand(kRight, 0.45f, 0.10f, -0.50f);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), rodRay())"), "true") << "it follows the hand";

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "untracked: the head's ray, " << EvalLua(L, "rayText(targeterRay())");
    headset.SetHandTracked(kRight, true);
    FRAMES(2);
    AdvanceGameTime(3100);  // set down
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "set down: the head's ray";
}
