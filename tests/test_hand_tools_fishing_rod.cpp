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

    // The game clock moved on by `seconds`, one second a frame, with the right hand moved a little each second (kept in use).
    void HoldOnFor(int seconds) {
        for (int second = 0; second < seconds; ++second) {
            AdvanceGameTime(1000);
            MOVE_HAND(kRight, 0.25f + 0.02f * static_cast<float>(second % 2), -0.10f, -0.30f);
        }
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

// The real rod has no state yet when the game attaches it, and gets one on its first update while held (the headset, 2026-10-18,
// where the rod was given back for it): it goes into the hand all the same, and stays there, with no warning.
TEST_F(VirtualGamepadHandToolsFishingRodTest, ARodWithNoStateYetWhenTheGameAttachesItGoesIntoTheHandAndStaysWithoutAWarning) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldFishingRod(L));
    ASSERT_EQ(EvalLua(L, "StubHandTool.rodSpec(rod).state == nil"), "true") << "no state at the attach";
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);
    EXPECT_EQ(InTheHand("right"), "true") << Dump();
    HoldOnFor(10);
    EXPECT_EQ(InTheHand("right"), "true") << Dump();
    EXPECT_EQ(RodState(), "idle");
    EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 0);
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

// ---- The trigger: hold to charge, let go to cast, the same trigger hooks and reels ---------------------------------------------

TEST_F(VirtualGamepadHandToolsFishingRodTest, APullCallsTheRodsHandlerOnceWith1HoldingCallsNothingMoreAndTheLetGoCallsItOnceWith0ThrowingTheCast) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    FRAMES(2);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(UseCalls(), "1") << "the pull: once, as the game's ACTIVATE_HANDTOOL press";
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.useCalls[1].self == rod)"), "true") << "with the rod as self, off the class";
    EXPECT_EQ(RodState(), "charging");
    EXPECT_EQ(Pad().bRightTrigger, 0) << "the trigger that fishes is not also a binding";

    FRAMES(60);
    EXPECT_EQ(UseCalls(), "1") << "holding calls nothing more";
    EXPECT_EQ(RodState(), "charging");
    EXPECT_GT(Number(L, "StubHandTool.rodSpec(rod).state.angleScale"), 0.5) << "the cast charges while the trigger is held";
    EXPECT_EQ(Pad().bRightTrigger, 0);

    ASSERT_EQ(RunLuauChunk(L, "_G.rayAtTheCast = rodRay()", "=rayAtTheCast"), 0);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(UseCalls(), "1,0") << "the let-go: once, as the game's release";
    EXPECT_EQ(RodState(), "waiting") << "the cast is thrown";
    EXPECT_EQ(Number(L, "#StubHandTool.casts"), 1.0);
    EXPECT_EQ(EvalLua(L, "sameRay(StubHandTool.casts[1].ray, rayAtTheCast)"), "true")
        << "thrown along the rod: " << EvalLua(L, "rayText(StubHandTool.casts[1].ray)");
    FRAMES(10);
    EXPECT_EQ(UseCalls(), "1,0") << "and nothing while the trigger stays let go";
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, TheSameTriggerHooksABitingFishReelsWhileHeldAndLandsTheCatch) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'waiting')", "=lineOut"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.biteRod(rod)", "=bite"), 0);
    FRAMES(1);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(RodState(), "reeling") << "a pull in the hook window hooks the fish";
    FRAMES(200);
    EXPECT_EQ(RodState(), "caught") << "holding the trigger reels the fish in";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(UseCalls(), "1,0") << "one press and one release for the whole reel";
    EXPECT_EQ(EvalLua(L, "StubHandTool.rodSpec(rod).item.name"), "fishingItem_trout");
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, ATriggerAlreadyHeldWhenTheRodComesIntoTheHandKeepsItsBindingAndDoesNothingUntilLetGo) {
    ASSERT_NO_FATAL_FAILURE(Start());
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
    headset.SetHandInputs(kRight, Trigger(1.0f));  // held for a binding, with no Hand Tool in the player's hands
    FRAMES(2);
    ASSERT_EQ(Pad().bRightTrigger, 255);

    ASSERT_NO_FATAL_FAILURE(HoldFishingRod(L));
    FRAMES(30);
    ASSERT_EQ(InTheHand("right"), "true") << Dump();
    EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger keeps its binding";
    EXPECT_EQ(UseCalls(), "") << "and the rod does not charge on its own";
    EXPECT_EQ(RodState(), "idle");

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(UseCalls(), "") << "nor cast when it is let go";
    EXPECT_EQ(Pad().bRightTrigger, 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(UseCalls(), "1") << "pulled afresh it charges";
    EXPECT_EQ(Pad().bRightTrigger, 0) << "and is not a binding";
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, TheRodLeavingTheHandMidPullSendsTheReleaseWhichThrowsTheChargedCast) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(20);
    ASSERT_EQ(RodState(), "charging");

    headset.SetHandTracked(kRight, false);
    FRAMES(1);
    EXPECT_EQ(UseCalls(), "1,0") << "the hand lost tracking mid-pull: the release is sent";
    EXPECT_EQ(RodState(), "waiting") << "and the charged cast is thrown, not left charging";
    FRAMES(10);
    EXPECT_EQ(UseCalls(), "1,0") << "once";

    // Hand Tools turned off mid-pull, and the rod put away mid-pull, end the use the same way.
    headset.SetHandTracked(kRight, true);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'idle'); StubHandTool.useCalls = {}", "=lineIn"), 0);
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(5);
    ASSERT_EQ(UseCalls(), "1");
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    EXPECT_EQ(UseCalls(), "1,0") << "vrHandTools off mid-pull";
}

// The game's own binding (X on a pad, the mouse) still calls the same handler, off the class; the secondary action is the pad's.
TEST_F(VirtualGamepadHandToolsFishingRodTest, TheGamesOwnBindingsStillFishWhileTheRodIsInTheHandAndTheSecondaryActionIsNeverTheTriggers) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(rod, 1)", "=bindingDown"), 0);
    FRAMES(10);
    EXPECT_EQ(RodState(), "charging") << "the game's binding charges";
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(rod, 0)", "=bindingUp"), 0);
    EXPECT_EQ(RodState(), "waiting") << "and casts";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(5);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "useCalls('ACTIVATE_HANDTOOL_SECONDARY')"), "") << "the trigger never calls the secondary action";
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressSecondaryBinding(rod, 1); StubHandTool.pressSecondaryBinding(rod, 0)", "=secondary"), 0);
    EXPECT_EQ(EvalLua(L, "useCalls('ACTIVATE_HANDTOOL_SECONDARY')"), "1,0") << "the game's own secondary binding still reaches the rod";
}

// ---- The rod's state ------------------------------------------------------------------------------------------------------------

// What the Companion Mod reads of the rod (its state from the keys of spec.state, its line, its bend), for the haptics and the
// diagnostics: one value per line, "state lineOut bend".
TEST_F(VirtualGamepadHandToolsFishingRodTest, TheRodsStateIsReadFromItsKeysIdleChargingWaitingBiteReelingAndCaughtWithItsLineAndBend) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "function _G.readRod() return table.concat({ tostring(select(1, VRCompanion:getFishingRodState())), "
                              "tostring(select(2, VRCompanion:getFishingRodState())), "
                              "string.format('%.2f', select(3, VRCompanion:getFishingRodState())) }, ' ') end", "=readRod"), 0);
    auto read = [&]() { return EvalLua(L, "readRod()"); };

    EXPECT_EQ(read(), "idle false 0.00");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(5);
    EXPECT_EQ(read(), "charging false 0.00");
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(read(), "waiting true 0.00") << "the cast: the line is out";
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.biteRod(rod)", "=bite"), 0);
    FRAMES(1);
    EXPECT_EQ(read(), "bite true 0.00");
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setFishPull(rod, 0.9)", "=pull"), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(read(), "reeling true 0.90") << "hooked and reeled: the rod bends with the fish's pull";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    EXPECT_EQ(read(), "reeling true 0.30") << "the reel let go: the rod bends less";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(200);
    EXPECT_EQ(read(), "caught false 0.00") << "landed: the line is in";
}

// The dump never named the reeling state's keys: reeling is told by the line out and the rod bent (rodBendFactor). With the line
// out, a state with none of the known keys and the rod straight is not taken for reeling: it is unknown, silent and keeping
// nothing in use, until the fish bends the rod.
TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheLineOutAStateWithNoneOfTheKnownKeysIsReelingOnlyWhileTheRodBends) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    auto read = [&]() {
        return EvalLua(L, "(function() local s, l, b = VRCompanion:getFishingRodState(); return string.format('%s %s %.2f', tostring(s), tostring(l), b) end)()");
    };
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'waiting'); StubHandTool.setRodState(rod, 'reeling')", "=hooked"), 0);
    ASSERT_EQ(Number(L, "StubHandTool.rodSpec(rod).rodBendFactor"), 0.0) << "hooked, before the fish pulls";
    EXPECT_EQ(read(), "unknown false 0.00") << "the line out and the rod straight is no evidence of reeling";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.rodSpec(rod).state = StubHandTool.rodSpec(rod).states[7]", "=unseenState"), 0);
    EXPECT_EQ(read(), "unknown false 0.00") << "a state the dump never showed, with the line out";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'reeling'); StubHandTool.rodSpec(rod).rodBendFactor = 0.4", "=pulling"), 0);
    EXPECT_EQ(read(), "reeling true 0.40") << "the fish bends the rod";
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, TheRodsStateIsNilForAnyOtherToolAndForNoTool) {
    ASSERT_NO_FATAL_FAILURE(Start());
    EXPECT_EQ(EvalLua(L, "tostring(VRCompanion:getFishingRodState())"), "nil") << "nothing held";
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "tostring(VRCompanion:getFishingRodState())"), "nil") << "the lance held";
}

// A read of what the state comes from that fails while the rod is in the hand (a rod reshaped from the start is the game's, see
// the end of this file): the answer is then "unknown", with the line in and no bend, never an error.
TEST_F(VirtualGamepadHandToolsFishingRodTest, ARodSpecThatCannotBeReadIsUnknownWithoutAnError) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.spec = StubHandTool.rodSpec(rod)
        _G.kept = { state = spec.state, fishingLine = spec.fishingLine, rodBendFactor = spec.rodBendFactor }
        function _G.readAfter(change)
            StubHandTool.setRodState(rod, 'waiting')
            change()
            local ok, state, lineOut, bend = pcall(function() return VRCompanion:getFishingRodState() end)
            for key, value in pairs(kept) do spec[key] = value end
            setmetatable(spec, nil)
            if not ok then return "error: " .. tostring(state) end
            return string.format("%s %s %s", tostring(state), tostring(lineOut), tostring(bend))
        end
    )luau", "=readAfter"), 0);
    const char* changes[] = {
        "function() spec.state = nil end",
        "function() spec.state = 'waiting' end",
        "function() spec.fishingLine = nil end",
        "function() spec.fishingLine = { isInitialized = 'yes' } end",
        "function() spec.rodBendFactor = nil end",
        "function() spec.state = nil; setmetatable(spec, { __index = function(_, key) error('reshaped: ' .. key) end }) end",
        "function() rod['spec_pdlc_highlandsFishingPack.fishingRod'] = setmetatable({}, { __index = function() error('reshaped') end }) end",
    };
    for (const char* change : changes) {
        EXPECT_EQ(EvalLua(L, std::string("readAfter(") + change + ")"), "unknown false 0") << change;
        ASSERT_EQ(RunLuauChunk(L, "rod['spec_pdlc_highlandsFishingPack.fishingRod'] = spec", "=restore"), 0);
    }
    EXPECT_EQ(EvalLua(L, "readAfter(function() end)"), "waiting true 0") << "readable again";
}

// ---- Staying in use while the line is out ---------------------------------------------------------------------------------------
// A Tool Hand lying still for 3 s is set down, and a bite comes 10 to 25 s after the cast while the player holds the rod still: with
// the line out the Tool Hand stays in use however still it is held (ADR-0031).

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheLineOutAStillToolHandStaysInUsePastTheTimeoutAndTheRodStaysInItsVirtualHand) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(5);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    ASSERT_EQ(RodState(), "waiting");

    for (int second = 0; second < 25; ++second) {
        AdvanceGameTime(1000);
        FRAMES(1);
    }
    EXPECT_EQ(InTheHand("right"), "true") << "25 still seconds waiting for a bite: " << Dump();
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true");

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.biteRod(rod)", "=bite"), 0);
    FRAMES(1);
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(InTheHand("right"), "true") << "the bite, with the hand still";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    ASSERT_EQ(RodState(), "reeling");
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(InTheHand("right"), "true") << "reeling with the reel let go, the hand still";
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheLineInAStillToolHandIsSetDownAfterTheTimeoutAsBefore) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'waiting')", "=lineOut"), 0);
    FRAMES(1);
    AdvanceGameTime(3100);
    FRAMES(1);
    ASSERT_EQ(InTheHand("right"), "true");

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'caught')", "=landed"), 0);  // the line is in
    FRAMES(1);
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "caught, the line in: set down after three still seconds";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false");

    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    ASSERT_EQ(InTheHand("right"), "true");
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'charging')", "=charging"), 0);
    FRAMES(1);
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "charging with the game's own binding, the line in: set down as before";
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, LosingTrackingWithTheLineOutSetsTheToolHandDownAndGivesTheRodBack) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'waiting')", "=lineOut"), 0);
    FRAMES(1);

    headset.SetHandTracked(kRight, false);
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "untracked with the line out";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false");
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "and it stays the game's";

    headset.SetHandTracked(kRight, true);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_EQ(InTheHand("right"), "true") << "tracked and picked up again";
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(InTheHand("right"), "true") << "the line still out: kept in use again";
}

// An unknown state keeps nothing in use: a state that cannot be read must not keep a Motion Controller in use forever.
TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheRodsStateUnreadableAStillToolHandIsSetDownAfterTheTimeoutEvenWithTheLineOut) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'waiting'); StubHandTool.rodSpec(rod).rodBendFactor = 'bent'", "=unreadable"), 0);
    ASSERT_EQ(EvalLua(L, "VRCompanion:getFishingRodState()"), "unknown");
    FRAMES(1);
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "the line out but the state unknown: set down as before";
}

// The other hand, holding no rod, is set down as ever while the Tool Hand waits for a bite.
TEST_F(VirtualGamepadHandToolsFishingRodTest, TheLineOutKeepsOnlyTheToolHandInUse) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandInUse(0)"), "true");
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'waiting')", "=lineOut"), 0);
    FRAMES(1);
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(0)"), "false") << "the left hand lies still";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true") << "the right hand holds the rod with its line out";
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheLeftHandChosenAsTheToolHandTheLineOutKeepsTheLeftHandInUse) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldFishingRod(L));
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'left')", "=toolHandLeft"), 0);
    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    ASSERT_EQ(InTheHand("left"), "true") << Dump();
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'waiting')", "=lineOut"), 0);
    FRAMES(1);
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(InTheHand("left"), "true") << "the left Tool Hand waits for a bite, still";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false") << "the right hand lies still";
}

// ---- The rod keeps its overlays ------------------------------------------------------------------------------------------------

// The rod is the one supported Hand Tool whose screen-centre overlays stay: its charge meter, waiting hint and "Hook fish" prompt.
TEST_F(VirtualGamepadHandToolsFishingRodTest, TheRodsChargeMeterWaitingHintAndHookPromptAreStillDrawnWhileItIsInTheToolHand) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    auto draws = [&]() {
        EXPECT_EQ(RunLuauChunk(L, "StubHandTool.draw()", "=draw"), 0);
        return EvalLua(L, "StubHandTool.rendering(rod)");
    };

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(5);
    EXPECT_EQ(draws(), "castingChargeOverlay") << "charging in the hand: the charge meter";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    ASSERT_EQ(InTheHand("right"), "true");
    EXPECT_EQ(draws(), "waitForBiteOverlay") << "waiting: the hint";
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.biteRod(rod)", "=bite"), 0);
    FRAMES(1);
    EXPECT_EQ(draws(), "hookFishOverlay") << "a bite: the \"Hook fish\" prompt";
    EXPECT_NE(Dump().find("centre overlays held back: no"), std::string::npos) << Dump();
}

// In the headset the reeling minigame sat so far right that it was hard to see (2026-10-18): what the rod draws is pulled in toward
// the centre of the screen, an element's middle at half its distance from the centre, its width unchanged. Each element as it reached
// the screen: "what@x".
TEST_F(VirtualGamepadHandToolsFishingRodTest, WhatTheRodDrawsIsPulledInTowardTheCentreSoTheReelingMinigameIsInViewAndTheCentredOverlaysStay) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.gameNatives = { renderOverlay, drawFilledRect, renderText }
        function _G.drawnScreen()
            StubHandTool.draw()
            local parts = {}
            for _, e in ipairs(StubHandTool.screen) do table.insert(parts, string.format("%s@%.3f", e.what, e.x)) end
            return table.concat(parts, " ")
        end
        function _G.nativesAreTheGames()
            return renderOverlay == gameNatives[1] and drawFilledRect == gameNatives[2] and renderText == gameNatives[3]
        end
    )luau", "=screen"), 0);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(5);
    EXPECT_EQ(EvalLua(L, "drawnScreen()"), "castingChargeOverlay@0.450") << "the charge meter, centred, stays";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "drawnScreen()"), "Wait for a bite@0.500") << "the hint at the centre stays";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'reeling')", "=reel"), 0);
    EXPECT_EQ(EvalLua(L, "drawnScreen()"), "reelingTensionBar@0.640 rect@0.645 Reel in@0.640")
        << "the minigame, drawn at 0.78 to 0.84, is pulled in";
    EXPECT_EQ(EvalLua(L, "nativesAreTheGames()"), "true") << "the game has its natives back after the rod's draw";
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, TheRodsOverlaysAreWhereTheRodDrawsThemWithHandToolsOffInThe2DViewAndForAnotherPlayersRod) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubHandTool.setRodState(rod, 'reeling')
        function _G.barAt()
            StubHandTool.draw()
            return string.format("%.3f", StubHandTool.screen[1].x)
        end
    )luau", "=reel"), 0);
    EXPECT_EQ(EvalLua(L, "barAt()"), "0.640");

    headset.SetHandTracked(kRight, false);
    FRAMES(1);
    ASSERT_EQ(WhereTheGamePutsIt(), "true");
    EXPECT_EQ(EvalLua(L, "barAt()"), "0.640") << "the rod out of the hand: still pulled in, the minigame is on the screen all the same";
    headset.SetHandTracked(kRight, true);
    FRAMES(1);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.is2DViewActive = true", "=2d"), 0);
    EXPECT_EQ(EvalLua(L, "barAt()"), "0.800") << "the 2D view";
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.is2DViewActive = false", "=vr"), 0);
    EXPECT_EQ(EvalLua(L, "barAt()"), "0.640");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "barAt()"), "0.800") << "Hand Tools off";
    EXPECT_EQ(EvalLua(L, "StubHandTool.fishingRodClass.onDraw == StubHandTool.fishingRodOriginalOnDraw"), "true")
        << "and the rod's draw is the game's own again";
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    FRAMES(1);

    EXPECT_EQ(EvalLua(L, "barAt()"), "0.640") << "on again";

    // another player's rod, drawn by the same class while the local player's rod is in the hand
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local pitch = StubScene.newNode("remotePitch", 0, 4, 1.7, 4)
        _G.remote = setmetatable({ isOwner = false, camera = { isFirstPerson = true, pitchNode = pitch } }, { __index = _G.Player })
        _G.remoteRod = StubHandTool.fishingRod(remote)
        remoteRod.isHeld = true
        remoteRod.carryingPlayer = remote
        remoteRod:attachTool()
        StubHandTool.setRodState(remoteRod, 'reeling')
    )luau", "=holdRemoteRod"), 0);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, R"luau((function()
        StubHandTool.screen = {}
        StubHandTool.fishingRodClass.onDraw(remoteRod)
        return string.format("%.3f", StubHandTool.screen[1].x)
    end)())luau"), "0.800") << "another player's rod";
}

// ---- Tuning and diagnostics -------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsFishingRodTest, VrHandToolOffsetFishingRodMovesAndTurnsTheRodInTheHandAndResetPutsItBack) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "_G.before = { offsetInHolder(rod.rootNode, 'right') }; _G.tipBefore = { offsetInHolder(StubHandTool.rodSpec(rod).tipLine, 'right') }", "=before"), 0);

    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolOffset', 'fishingrod', 'nudge', 0.02, 0, 0)").find("fishingrod: position"), std::string::npos);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = offsetInHolder(rod.rootNode, 'right')
        assert(math.abs(x - before[1] - 0.02) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4,
            "the rod moved 2 cm along the hand's x: " .. (x - before[1]))
    )luau", "=nudged"), 0);

    EvalLua(L, "runCommand('vrHandToolOffset', 'fishingrod', 'turn', 0, 30, 0)");
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = offsetInHolder(StubHandTool.rodSpec(rod).tipLine, 'right')
        local moved = math.sqrt((x - tipBefore[1]) ^ 2 + (y - tipBefore[2]) ^ 2 + (z - tipBefore[3]) ^ 2)
        assert(moved > 0.5, "turned, the rod's tip swung round in the hand: " .. moved)
    )luau", "=turned"), 0);

    EvalLua(L, "runCommand('vrHandToolOffset', 'fishingrod', 'reset')");
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = offsetInHolder(rod.rootNode, 'right')
        assert(math.abs(x - before[1]) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4, "back where it was")
        local tx, ty, tz = offsetInHolder(StubHandTool.rodSpec(rod).tipLine, 'right')
        assert(math.abs(tx - tipBefore[1]) < 1e-4 and math.abs(ty - tipBefore[2]) < 1e-4 and math.abs(tz - tipBefore[3]) < 1e-4, "and turned back")
    )luau", "=reset"), 0);
    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolOffset')").find("fishingrod"), std::string::npos) << "the list of tools names it";
    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolOffset', 'fishingrod', 'wiggle')").find("fishingrod"), std::string::npos) << "and so does the usage";
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, TheDiagnosticsShowTheRodAsASupportedKindAndNoLongerDescribeItInHandToolProbeLines) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    s_capturedLogs.clear();
    const std::string answer = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");

    EXPECT_NE(answer.find("held Hand Tool: fishingrod (supported)"), std::string::npos) << answer;
    EXPECT_NE(answer.find("in the Tool Hand: yes"), std::string::npos) << answer;
    EXPECT_NE(answer.find("from the tool"), std::string::npos) << answer;
    EXPECT_EQ(CountLogLines("Hand Tool probe:"), 0) << "the rod is supported: no probe dump";

    AdvanceGameTime(3100);  // set down: the rod is the game's, but still a supported tool
    FRAMES(1);
    s_capturedLogs.clear();
    EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_EQ(CountLogLines("Hand Tool probe:"), 0) << "not even out of the hand";
}

// ---- Multiplayer, a game without the DLC, a DLC update that reshapes the rod ---------------------------------------------------

// The game attaches a Hand Tool of every player in the session through the same function. Only the local player's rod is moved.
TEST_F(VirtualGamepadHandToolsFishingRodTest, AnotherPlayersRodIsNeverTakenIntoTheToolHandNorUsedByTheTrigger) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local pitch = StubScene.newNode("remotePitch", 0, 4, 1.7, 4)
        _G.remote = setmetatable({ isOwner = false, camera = { isFirstPerson = true, pitchNode = pitch } }, { __index = _G.Player })
        _G.remoteRod = StubHandTool.fishingRod(remote)
        remoteRod.isHeld = true
        remoteRod.carryingPlayer = remote
        remoteRod:attachTool()
    )luau", "=holdRemoteRod"), 0);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "tostring(getParent(remoteRod.rootNode) == remote.camera.pitchNode)"), "true") << "where the game put it";
    EXPECT_EQ(EvalLua(L, "isBelow(remoteRod.rootNode, handHolder('right'))"), "false");
    EXPECT_EQ(InTheHand("right"), "true") << "the local player's rod stays in the hand";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(UseCalls(), "1");
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.useCalls[1].self == rod)"), "true") << "the trigger uses the local player's rod only";
}

// A game without the Highlands Fishing DLC has no rod class and no rod: nothing changes and nothing warns.
TEST_F(VirtualGamepadHandToolsFishingRodTest, AGameWithoutTheDlcLogsNoWarningAndItsHandToolsWorkAsBefore) {
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    SetGameTime(10000);
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << Dump();
    EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    std::string warnings;
    for (const auto& line : s_capturedLogs) {
        if (line.find("[Warning]") != std::string::npos) warnings += line + "\n";
    }
    EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 0) << warnings;
}

// The fixture's checks that a rod whose handler a DLC update renamed or reshaped is the game's, with one warning naming the handler.
#define EXPECT_THE_GAMES_ROD_AND_ONE_WARNING() \
    do { \
        ASSERT_NO_FATAL_FAILURE(HoldFishingRod(L)); \
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f); \
        AimTheHand(kRight, 0.25f, -0.10f, -0.30f); \
        FRAMES(2); \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "exactly one warning"; \
        EXPECT_EQ(CountLogLines("onActivateAction"), 1) << "that names the handler"; \
        EXPECT_EQ(InTheHand("right"), "false") << "the rod is given back to the game"; \
        EXPECT_EQ(WhereTheGamePutsIt(), "true"); \
        EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "and aimed by looking"; \
        headset.SetHandInputs(kRight, Trigger(1.0f)); \
        FRAMES(5); \
        EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger is the pad's"; \
        EXPECT_EQ(UseCalls(), "") << "and uses nothing"; \
        headset.SetHandInputs(kRight, Trigger(0.0f)); \
        ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(rod); StubHandTool.hold(rod)", "=again"), 0); \
        FRAMES(2); \
        EvalLua(L, "runCommand('vrHandToolsDiagnostics')"); \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "and the warning is not repeated"; \
    } while (0)

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheRodsHandlerMissingTheRodIsTheGamesAndTheLogHasOneWarning) {
    ASSERT_NO_FATAL_FAILURE(Start("missing"));
    EXPECT_THE_GAMES_ROD_AND_ONE_WARNING();
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheRodsHandlerReshapedTheRodIsTheGamesAndTheLogHasOneWarning) {
    ASSERT_NO_FATAL_FAILURE(Start("reshaped"));
    EXPECT_THE_GAMES_ROD_AND_ONE_WARNING();
}

// A DLC update could as well rename or reshape what the rod's state is read from (#125: the spec's state, fishingLine with its
// isInitialized, rodBendFactor, and the rod's getBobberPosition): such a rod is the game's from the start, with one warning that
// names the field, as for a reshaped handler. The real rod fills its fields in after the game attaches it (its state on its first
// update), so a field is found reshaped only once it has stayed so for 5 s of holding; until then the rod is in the hand. `reshape`
// runs on the new rod (`rod`, its spec `spec`) before it is taken in hand.
#define EXPECT_A_RESHAPED_ROD_IS_THE_GAMES_WITH_ONE_WARNING(reshape, named) \
    do { \
        ASSERT_NO_FATAL_FAILURE(Start()); \
        ASSERT_EQ(RunLuauChunk(L, "_G.rod = StubHandTool.fishingRod(foot.player); local spec = StubHandTool.rodSpec(rod); " reshape \
                                  "; StubHandTool.hold(rod)", "=reshapedRod"), 0); \
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f); \
        FRAMES(2); \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 0) << "not yet"; \
        EXPECT_EQ(InTheHand("right"), "true") << "in the hand meanwhile: " << Dump(); \
        HoldOnFor(4); \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 0) << "not after 4 s"; \
        HoldOnFor(2); \
        AimTheHand(kRight, 0.25f, -0.10f, -0.30f); \
        FRAMES(2); \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "exactly one warning"; \
        EXPECT_EQ(CountLogLines(named), 1) << "that names " << named; \
        EXPECT_EQ(InTheHand("right"), "false") << "the rod is given back to the game"; \
        EXPECT_EQ(WhereTheGamePutsIt(), "true"); \
        headset.SetHandInputs(kRight, Trigger(1.0f)); \
        FRAMES(5); \
        EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger is the pad's"; \
        EXPECT_EQ(UseCalls(), "") << "and uses nothing"; \
        headset.SetHandInputs(kRight, Trigger(0.0f)); \
        ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(rod); StubHandTool.hold(rod)", "=again"), 0); \
        FRAMES(2); \
        EvalLua(L, "runCommand('vrHandToolsDiagnostics')"); \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "and the warning is not repeated"; \
        EXPECT_EQ(InTheHand("right"), "false"); \
    } while (0)

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheRodsStateReshapedTheRodIsTheGamesAndTheLogHasOneWarningNamingIt) {
    EXPECT_A_RESHAPED_ROD_IS_THE_GAMES_WITH_ONE_WARNING("spec.state = 'idle'", "fishingRod.state:");
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheRodsFishingLineRenamedTheRodIsTheGamesAndTheLogHasOneWarningNamingIt) {
    EXPECT_A_RESHAPED_ROD_IS_THE_GAMES_WITH_ONE_WARNING("spec.line = spec.fishingLine; spec.fishingLine = nil", "fishingRod.fishingLine:");
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheRodsLineFlagReshapedTheRodIsTheGamesAndTheLogHasOneWarningNamingIt) {
    EXPECT_A_RESHAPED_ROD_IS_THE_GAMES_WITH_ONE_WARNING("spec.fishingLine.isInitialized = 'no'", "fishingRod.fishingLine.isInitialized:");
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheRodsBendRenamedTheRodIsTheGamesAndTheLogHasOneWarningNamingIt) {
    EXPECT_A_RESHAPED_ROD_IS_THE_GAMES_WITH_ONE_WARNING("spec.bendFactor = spec.rodBendFactor; spec.rodBendFactor = nil", "fishingRod.rodBendFactor:");
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, WithTheRodsBobberPositionMissingTheRodIsTheGamesAndTheLogHasOneWarningNamingIt) {
    EXPECT_A_RESHAPED_ROD_IS_THE_GAMES_WITH_ONE_WARNING("rod.getBobberPosition = 42", "getBobberPosition:");
}

TEST_F(VirtualGamepadHandToolsFishingRodTest, AReshapedRodLeavesTheOtherHandToolsInTheToolHand) {
    ASSERT_NO_FATAL_FAILURE(Start("reshaped"));
    ASSERT_NO_FATAL_FAILURE(HoldFishingRod(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);
    ASSERT_EQ(CountLogLines("[Warning] Hand Tools"), 1);

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(rod)", "=putAway"), 0);
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "true") << "the lance still goes into the hand: " << Dump();
}
