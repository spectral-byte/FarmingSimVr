#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <algorithm>
#include <string>

namespace {

using namespace hand_tools_test;

constexpr const char* kFeedbackHelpers = R"luau(
    function _G.canRay() return { StubHandTool.canRay(can) } end
    function _G.plantTrunk(distance)
        _G.trunk = StubHandTool.treeAt(canRay(), distance, 0.15)
        return trunk
    end
    function _G.messages()
        local lines = {}
        for _, warning in ipairs(StubHandTool.warnings) do table.insert(lines, warning.text) end
        return table.concat(lines, "\n")
    end
    function _G.messagesNaming(text)
        local count = 0
        for _, warning in ipairs(StubHandTool.warnings) do
            if string.find(warning.text, text, 1, true) ~= nil then count = count + 1 end
        end
        return count
    end
)luau";

constexpr float kAimPitch = 1.4f;
constexpr float kAimYaw = 1.2f;

// Pulses stronger than this are not "light" (the lance's pressure is 0.35, the chainsaw's cut 0.6).
constexpr float kLight = 0.5f;

class VirtualGamepadHandToolsSprayCanFeedbackTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };
    std::string stubs;

    void Start() {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        stubs = StubChunk("xml_settings") + SprayCanStubs();
        StartOptions options;
        options.extraStubs = stubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, kFeedbackHelpers, "=feedbackHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    void AimTheHand(uint32_t hand, float x, float y, float z) {
        const float side = hand == kRight ? 1.0f : -1.0f;
        headset.SetHandPose(hand, HandAt(x, y, z, kAimPitch, kAimYaw * side));
        headset.SetHandGripPose(hand, HandAt(x, y, z, kAimPitch, kAimYaw * side));
    }

    void StartWithTheCanIn(uint32_t hand, const char* canLua = "_G.can = StubHandTool.sprayCan(foot.player); StubHandTool.hold(can)") {
        ASSERT_NO_FATAL_FAILURE(Start());
        if (hand == kLeft) ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'left')", "=toolHandLeft"), 0);
        ASSERT_EQ(RunLuauChunk(L, canLua, "=holdCan"), 0);
        const float side = hand == kRight ? 1.0f : -1.0f;
        PICK_UP(hand, 0.25f * side, -0.10f, -0.30f);
        AimTheHand(hand, 0.25f * side, -0.10f, -0.30f);
        FRAMES(2);
        ASSERT_EQ(EvalLua(L, std::string("VRCompanion:isHandHoldingHandTool(") + std::to_string(hand) + ")"), "true") << Dump();
    }

    std::string Dump() { return EvalLua(L, "VRCompanion:getHandToolsDiagnostics()"); }

    size_t PulsesTo(int hand) const {
        size_t count = 0;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == hand) ++count;
        }
        return count;
    }
    float StrongestTo(int hand) const {
        float strongest = 0.0f;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == hand) strongest = std::max(strongest, pulse.amplitude);
        }
        return strongest;
    }
    void ForgetPulses() { headset.runtime->controllers.hapticPulses.clear(); }

    int LongestSilenceTo(int hand, int frames) {
        int longest = 0, current = 0;
        for (int i = 0; i < frames; ++i) {
            const size_t before = PulsesTo(hand);
            FRAMES(1);
            if (PulsesTo(hand) > before) {
                current = 0;
            } else {
                longest = std::max(longest, ++current);
            }
        }
        return longest;
    }

    std::string Messages() { return EvalLua(L, "messages()"); }
    int MessagesNaming(const char* text) { return static_cast<int>(Number(L, std::string("messagesNaming('") + text + "')")); }
    void ChangeTheMarkerType() { ASSERT_EQ(RunLuauChunk(L, "can:changeTreeMarkerType(nil, 1)", "=changeMarkerType"), 0); }
};

}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, TheToolHandTicksOnceWhenTheNozzleComesOntoATreeNotWhileItStaysOnItAndAgainOnAnotherAndOnComingBack) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kRight));
    ForgetPulses();
    FRAMES(5);
    ASSERT_EQ(PulsesTo(kRight), 0u) << "a can that points at no tree and does not spray is silent";

    ASSERT_EQ(RunLuauChunk(L, "plantTrunk(0.4)", "=trunk"), 0);
    FRAMES(1);
    ASSERT_EQ(PulsesTo(kRight), 1u) << "the nozzle came onto a tree: one tick";
    EXPECT_GT(StrongestTo(kRight), 0.0f);
    EXPECT_LE(StrongestTo(kRight), kLight) << "and a light one";
    FRAMES(30);
    EXPECT_EQ(PulsesTo(kRight), 1u) << "staying on the same tree makes no more";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}; plantTrunk(0.8)", "=anotherTrunk"), 0);
    FRAMES(1);
    EXPECT_EQ(PulsesTo(kRight), 2u) << "straight onto another tree: another tick";
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 2u);

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=noTree"), 0);
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 2u) << "coming off a tree is silent";

    ASSERT_EQ(RunLuauChunk(L, "plantTrunk(0.8)", "=backOntoATree"), 0);
    FRAMES(1);
    EXPECT_EQ(PulsesTo(kRight), 3u) << "and back onto one ticks again";
    EXPECT_EQ(PulsesTo(kLeft), 0u) << "only the Tool Hand feels it";
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, WithTheLeftHandAsTheToolHandTheTickIsFeltInTheLeftMotionController) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kLeft));
    ForgetPulses();
    ASSERT_EQ(RunLuauChunk(L, "plantTrunk(0.4)", "=trunk"), 0);
    FRAMES(2);
    EXPECT_EQ(PulsesTo(kLeft), 1u);
    EXPECT_EQ(PulsesTo(kRight), 0u);
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, NoTickWhileTheToolHandIsUntrackedSetDownOrHandToolsAreOff) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kRight));
    ForgetPulses();

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}; StubHandTool.treeAt({ StubHandTool.originalGetLookRay(foot.player) }, 1.0, 0.15)", "=headTrunk"), 0);
    FRAMES(5);
    ASSERT_EQ(EvalLua(L, "tostring((foot.player.targeter.closestTargetsByKey[HandToolSprayCan] or {}).node ~= nil)"), "true");
    EXPECT_EQ(PulsesTo(kRight), 0u) << "untracked: the can is where the game puts it and the game's icons show the tree";

    headset.SetHandTracked(kRight, true);
    FRAMES(2);
    ForgetPulses();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}; StubHandTool.treeAt({ StubHandTool.originalGetLookRay(foot.player) }, 0.8, 0.15)", "=anotherHeadTrunk"), 0);
    FRAMES(5);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "Hand Tools off: nothing is added to the game";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    FRAMES(2);
    AdvanceGameTime(3100);
    FRAMES(2);
    ForgetPulses();
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}; StubHandTool.treeAt({ StubHandTool.originalGetLookRay(foot.player) }, 1.1, 0.15)", "=setDownTrunk"), 0);
    FRAMES(5);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "set down: the same";
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, TheMarkerTypeIsNamedInABlinkingMessageWhenTheCanIsTakenInHandAndEachTimeItIsChangedAndNotWhenNothingChanged) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kRight));
    EXPECT_EQ(MessagesNaming("Cross"), 1) << "taken in hand: the first marker type is named, " << Messages();
    FRAMES(30);
    EXPECT_EQ(MessagesNaming("Cross"), 1) << "once, not every frame";

    ASSERT_NO_FATAL_FAILURE(ChangeTheMarkerType());
    FRAMES(2);
    EXPECT_EQ(MessagesNaming("Arrow"), 1) << "changed: the new one is named, " << Messages();
    ASSERT_NO_FATAL_FAILURE(ChangeTheMarkerType());
    FRAMES(2);
    EXPECT_EQ(MessagesNaming("Circle"), 1);
    ASSERT_NO_FATAL_FAILURE(ChangeTheMarkerType());
    FRAMES(2);
    EXPECT_EQ(MessagesNaming("Cross"), 2) << "round to the first: named again";
    FRAMES(30);
    EXPECT_EQ(MessagesNaming("Arrow") + MessagesNaming("Circle") + MessagesNaming("Cross"), 4);
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, AMarkerTypeWithoutANameOrATitleIsNamedByItsIndex) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.markerTypes[2].name = nil; StubHandTool.markerTypes[2].title = nil", "=nameless"), 0);
    ASSERT_NO_FATAL_FAILURE(ChangeTheMarkerType());
    FRAMES(2);
    EXPECT_EQ(MessagesNaming("Marker type: 2"), 1) << Messages();
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, TheMessageIsTheCompanionModsTextAndTheTypeIsNamedByTheGamesTextForItsTitle) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kRight));
    EXPECT_EQ(MessagesNaming("Marker type: Cross"), 1) << "the title's text, not \"$l10n_treeMarker_cross\" or \"CROSS\", " << Messages();
    EXPECT_EQ(MessagesNaming("l10n"), 0) << Messages();

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.texts.vrHandTools_markerType = 'Markierungstyp: %s'; StubHandTool.texts.treeMarker_arrow = 'Pfeil'", "=german"), 0);
    ASSERT_NO_FATAL_FAILURE(ChangeTheMarkerType());
    FRAMES(2);
    EXPECT_EQ(MessagesNaming("Markierungstyp: Pfeil"), 1) << "the sentence and the name are the game's current language, " << Messages();
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, ATitleThatIsAlreadyTextOrWithoutAnL10nTextIsStillNamedAndNeverAsAKey) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.markerTypes[2].title = 'Pfeil'", "=resolvedTitle"), 0);
    ASSERT_NO_FATAL_FAILURE(ChangeTheMarkerType());
    FRAMES(2);
    EXPECT_EQ(MessagesNaming("Marker type: Pfeil"), 1) << Messages();

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.markerTypes[3].title = '$l10n_treeMarker_unknown'", "=unknownTitle"), 0);
    ASSERT_NO_FATAL_FAILURE(ChangeTheMarkerType());
    FRAMES(2);
    EXPECT_EQ(MessagesNaming("Marker type: Circle"), 1) << Messages();

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.markerTypes[1].title = '$l10n_treeMarker_unknown'; StubHandTool.markerTypes[1].name = 'SPIRAL'", "=unknownEverything"), 0);
    ASSERT_NO_FATAL_FAILURE(ChangeTheMarkerType());
    FRAMES(2);
    EXPECT_EQ(MessagesNaming("Marker type: SPIRAL"), 1) << Messages();
    EXPECT_EQ(MessagesNaming("l10n"), 0) << Messages();
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, WithoutTheGamesTextsTheTypeIsStillNamedInEnglishByItsName) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, "_G.g_i18n = nil", "=noI18n"), 0);
    ASSERT_NO_FATAL_FAILURE(ChangeTheMarkerType());
    FRAMES(2);
    EXPECT_EQ(MessagesNaming("Marker type: ARROW"), 1) << Messages();
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, TheMarkerTypeIsNotRepeatedAfterIdleTimeoutOrMenuAndOnlyFlashesOnEquipOrTypeChange) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kRight));
    ASSERT_EQ(MessagesNaming("Cross"), 1);

    AdvanceGameTime(3100);
    FRAMES(2);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true");
    EXPECT_EQ(MessagesNaming("Cross"), 1);

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menuOpen"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=menuClose"), 0);
    FRAMES(2);
    EXPECT_EQ(MessagesNaming("Cross"), 1);

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    ASSERT_NO_FATAL_FAILURE(ChangeTheMarkerType());
    FRAMES(5);
    EXPECT_EQ(MessagesNaming("Arrow"), 0) << "untracked: the game's icon shows it, no message";
    headset.SetHandTracked(kRight, true);
    FRAMES(3);
    EXPECT_EQ(MessagesNaming("Arrow"), 1) << "and back in the hand it is named";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(can)", "=dropCan"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.hold(can)", "=equipCan"), 0);
    FRAMES(2);
    EXPECT_EQ(MessagesNaming("Arrow"), 2);
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, NoMarkerTypeMessageWithHandToolsOffAndNoneForAnotherModsTool) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    ASSERT_EQ(RunLuauChunk(L, "_G.can = StubHandTool.sprayCan(foot.player); StubHandTool.hold(can)", "=holdCan"), 0);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(3);
    ASSERT_NO_FATAL_FAILURE(ChangeTheMarkerType());
    FRAMES(3);
    EXPECT_EQ(MessagesNaming("Marker type"), 0) << "Hand Tools off: the game shows the icon, the Companion Mod names no type, " << Messages();
    ForgetPulses();
    ASSERT_EQ(RunLuauChunk(L, "plantTrunk(0.4)", "=trunk"), 0);
    FRAMES(3);
    EXPECT_EQ(PulsesTo(kRight), 0u);

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(can)", "=putAway"), 0);
    ASSERT_NO_FATAL_FAILURE(HoldOtherModTool(L));
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    FRAMES(3);
    EXPECT_EQ(MessagesNaming("Marker type"), 0) << "another mod's tool names no marker type, " << Messages();
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, TheToolHandBuzzesLightlyAndSteadilyWhileTheCanSpraysAndIsQuietBeforeAndAfter) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, "plantTrunk(0.4)", "=trunk"), 0);
    FRAMES(5);
    ForgetPulses();
    FRAMES(10);
    ASSERT_EQ(PulsesTo(kRight), 0u) << "only holding the can, aimed at a tree: quiet";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "tostring(can.spec_sprayCan.isSpraying)"), "true");
    ForgetPulses();
    EXPECT_LE(LongestSilenceTo(kRight, 20), 3) << "a buzz that goes on while it sprays";
    EXPECT_GT(StrongestTo(kRight), 0.0f);
    EXPECT_LE(StrongestTo(kRight), kLight) << "and a light one";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(60);
    ASSERT_EQ(EvalLua(L, "tostring(can.spec_sprayCan.isSpraying)"), "false");
    ForgetPulses();
    FRAMES(20);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "quiet again";
    EXPECT_EQ(PulsesTo(kLeft), 0u);
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, AnEmptyCanAndATreeThePlayerMayNotMarkMakeNoBuzz) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kRight, "_G.can = StubHandTool.sprayCan(foot.player, { markers = 0 }); StubHandTool.hold(can)"));
    ASSERT_EQ(RunLuauChunk(L, "plantTrunk(0.4)", "=trunk"), 0);
    FRAMES(5);
    ForgetPulses();
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(30);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "an empty can sprays nothing and the hand is quiet";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(3);

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(can); _G.can = StubHandTool.sprayCan(foot.player); StubHandTool.hold(can); StubHandTool.forbid(trunk)", "=fullCanForbiddenTree"), 0);
    FRAMES(5);
    ForgetPulses();
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(30);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "a tree the player may not mark: no spray, no buzz";
}

TEST_F(VirtualGamepadHandToolsSprayCanFeedbackTest, TheBuzzStopsWhenTheToolHandLosesTrackingWhileTheCanSprays) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, "plantTrunk(0.4)", "=trunk"), 0);
    FRAMES(3);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "tostring(can.spec_sprayCan.isSpraying)"), "true");

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "the hand lost tracking while the can sprays: no more buzz";
}
