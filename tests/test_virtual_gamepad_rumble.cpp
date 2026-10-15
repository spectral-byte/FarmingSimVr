// Game rumble on the Motion Controllers (ADR-0028, spec #98, ticket #103), seen from outside: the real Companion
// Mod runs against the real VRBridge over FakeTrackedHeadset, the test plays the game by calling the answer the
// XInputSetState hook routes to (VirtualGamepad::SetState) and drives frames and the game clock, and reads which
// pulses the fake runtime was asked to play on which hand. Prior art: test_haptics.cpp (Click Point pulses) and
// test_virtual_gamepad.cpp (the pad itself).

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include "input/virtual_gamepad.h"

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;

constexpr int kLeft = 0;
constexpr int kRight = 1;

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
)luau";

constexpr const char* kXmlStubs = R"luau(
    _G.savedXml = {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function() return true end
    _G.createXMLFile = function() return 1 end
    _G.setXMLFloat = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLBool = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLString = function(file, key, value) _G.savedXml[key] = value end
    _G.saveXMLFile = function() return true end
    _G.delete = function() end
    _G.loadXMLFile = function() return 2 end
    _G.getXMLFloat = function(file, key) local v = _G.savedXml[key]; return type(v) == "number" and v or nil end
    _G.getXMLBool = function(file, key) local v = _G.savedXml[key]; if type(v) == "boolean" then return v end return nil end
    _G.getXMLString = function(file, key) local v = _G.savedXml[key]; return type(v) == "string" and v or nil end
)luau";

constexpr WORD kFull = 0xFFFF;

class VirtualGamepadRumbleTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{headset.manager, nullptr};

    void SetUp() override {
        CompanionModTest::SetUp();
        // The pad times the game's rumble against the Companion Mod's pulses by the game clock.
        bridge.GetVirtualGamepad().SetClockForTesting([this] { return GameTime(); });
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
        // A cab at the world origin and the Interactive Control stand-in, as test_haptics.cpp has them: the player sits
        // at (0, 1.5, 0) looking along +Z, so a hand held at (x, y, -z) points at world (-x, 1.5 + y, +z).
        ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
        ASSERT_EQ(RunLuauChunk(L, kStubInteractiveControl, "=interactiveControl"), 0);
        ASSERT_EQ(RunLuauChunk(L, "StubIC.install('ok')", "=installIc"), 0);
        ASSERT_EQ(RunLuauChunk(L, "StubScene.cab({})", "=scene"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        SetGameTime(10000);
        NextFrame(&headset);
    }

    void PreFrame() {
        ASSERT_EQ(RunLuauChunk(L, R"luau(
            if StubScene.updateCamera then StubScene.updateCamera() end
            if StubIC.tick then StubIC.tick() end
        )luau", "=preFrame"), 0);
    }

    // One game frame with the scene stand-ins ticking first, as test_haptics.cpp does.
    void Frame(double dtMs = 16.667) {
        PreFrame();
        NextFrame(&headset, dtMs);
    }

    // Short frames until the hand is told something new; how many it took, or -1 if nothing came in `maxFrames`.
    int FramesUntilAnotherPulseTo(int hand, double dtMs, int maxFrames) {
        const size_t before = PulsesTo(hand).size();
        for (int frame = 1; frame <= maxFrames; ++frame) {
            Frame(dtMs);
            if (PulsesTo(hand).size() > before) return frame;
        }
        return -1;
    }

    // Puts a Click Point dead ahead of the right hand, picks the hand up and aims it there: the Pointer Ray is on it.
    void AimRightHandAtAClickPoint() {
        ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('center', -0.3, 1.5, 2.3)", "=addPoint"), 0);
        headset.SetHandPointing(kRight, HandAt(0.2f, 0.0f, -0.3f));
        Frame();
        headset.SetHandPointing(kRight, HandAt(0.3f, 0.0f, -0.3f));
        Frame(1.0);  // a short frame: the Click Point is selected within a millisecond of the clock reading afterwards
    }

    static fs25vr::HeadPose HandAt(float x, float y, float z) {
        fs25vr::HeadPose pose;
        pose.position = fs25vr::Vector3(x, y, z);
        pose.isValid = true;
        pose.positionTracked = true;
        pose.orientationTracked = true;
        return pose;
    }

    void TurnPadOn() {
        EvalLua(L, "runCommand('vrVirtualGamepad', 'on')");
        NextFrame(&headset);
    }

    // The game sets the pad's vibration (slot 3: the highest free XInput slot of a machine without real pads).
    DWORD GameRumbles(WORD left, WORD right, DWORD slot = 3) {
        XINPUT_VIBRATION vibration{ left, right };
        return bridge.GetVirtualGamepad().SetState(slot, &vibration);
    }

    std::vector<fs25vr::FakeOpenXrControllers::HapticPulse> PulsesTo(int hand) const {
        std::vector<fs25vr::FakeOpenXrControllers::HapticPulse> pulses;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == hand) pulses.push_back(pulse);
        }
        return pulses;
    }
};

} // namespace

TEST_F(VirtualGamepadRumbleTest, EachMotorPlaysOnItsOwnHandSofterThanTheGamepadAndZeroSpeedStopsIt) {
    TurnPadOn();

    ASSERT_EQ(GameRumbles(kFull, 0), static_cast<DWORD>(ERROR_SUCCESS));
    NextFrame(&headset);
    ASSERT_FALSE(PulsesTo(kLeft).empty()) << "the left motor plays on the left hand";
    EXPECT_TRUE(PulsesTo(kRight).empty()) << "and the right hand feels nothing";
    const float fullLeft = PulsesTo(kLeft).back().amplitude;
    EXPECT_GT(fullLeft, 0.0f);
    EXPECT_LT(fullLeft, 1.0f) << "softer than a gamepad's full speed";

    GameRumbles(0, 0);
    headset.runtime->controllers.hapticPulses.clear();
    NextFrame(&headset);
    NextFrame(&headset);
    EXPECT_TRUE(headset.HapticPulses().empty()) << "zero speed stops the pulses";

    GameRumbles(0, kFull / 2 + 1);
    NextFrame(&headset);
    ASSERT_FALSE(PulsesTo(kRight).empty()) << "the right motor plays on the right hand";
    EXPECT_TRUE(PulsesTo(kLeft).empty());
    EXPECT_NEAR(PulsesTo(kRight).back().amplitude, fullLeft / 2.0f, 0.01f) << "the motor speed sets the strength";
}

TEST_F(VirtualGamepadRumbleTest, AClickPointTickOnAHandPlaysAsItAlwaysDidAndRumbleResumesAfterIt) {
    TurnPadOn();
    ASSERT_EQ(GameRumbles(0, 13107), static_cast<DWORD>(ERROR_SUCCESS));  // right motor at a fifth: a rumble of 0.1
    Frame();
    ASSERT_FALSE(PulsesTo(kRight).empty());
    const float rumble = PulsesTo(kRight).back().amplitude;
    ASSERT_GT(rumble, 0.0f);
    ASSERT_LT(rumble, 0.2f) << "a rumble clearly weaker than a Click Point tick, so the two can be told apart";

    AimRightHandAtAClickPoint();
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "center");
    ASSERT_GT(PulsesTo(kRight).back().amplitude, rumble) << "the landing tick is the last thing the hand was told";
    EXPECT_TRUE(PulsesTo(kLeft).empty());

    // The tick lasts 25 ms: four 5 ms frames after it the rumble is still off the hand, and it is back by the sixth.
    const int frames = FramesUntilAnotherPulseTo(kRight, 5.0, 10);
    EXPECT_GE(frames, 5) << "the rumble came back while the tick was still playing";
    EXPECT_LE(frames, 6) << "the rumble did not come back after the tick";
    EXPECT_FLOAT_EQ(PulsesTo(kRight).back().amplitude, rumble);
}

TEST_F(VirtualGamepadRumbleTest, AClickOnAHandPlaysOverTheRumbleAndTheRumbleComesBackAfterIt) {
    TurnPadOn();
    ASSERT_EQ(GameRumbles(0, 13107), static_cast<DWORD>(ERROR_SUCCESS));
    AimRightHandAtAClickPoint();
    for (int i = 0; i < 4; ++i) Frame();
    const float rumble = PulsesTo(kRight).back().amplitude;

    FakeOpenXrControllers::Inputs pull;
    pull.trigger = 1.0f;
    headset.SetHandInputs(kRight, pull);
    Frame(1.0);
    ASSERT_EQ(EvalLua(L, "#StubIC.executed"), "1");
    EXPECT_GT(PulsesTo(kRight).back().amplitude, rumble) << "the click is the last thing the hand was told, and it is felt";

    // The click lasts 45 ms: eight 5 ms frames after it the rumble is still off the hand, back by the tenth.
    const int frames = FramesUntilAnotherPulseTo(kRight, 5.0, 14);
    EXPECT_GE(frames, 9) << "the rumble came back while the click was still playing";
    EXPECT_LE(frames, 10) << "the rumble did not come back after the click";
    EXPECT_FLOAT_EQ(PulsesTo(kRight).back().amplitude, rumble);
    EXPECT_TRUE(PulsesTo(kLeft).empty());
}

TEST_F(VirtualGamepadRumbleTest, VibrationWhileMotionControllersAreOffReachesNoHand) {
    TurnPadOn();
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllers', 'off')").find("off"), std::string::npos);
    NextFrame(&headset);

    EXPECT_EQ(GameRumbles(kFull, kFull), static_cast<DWORD>(ERROR_SUCCESS)) << "the game still sees its pad";
    for (int i = 0; i < 4; ++i) Frame();
    EXPECT_TRUE(headset.HapticPulses().empty());

    // Back on, the same rumble plays; switched off again while it plays, it stops reaching the hands.
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllers', 'on')").find("on"), std::string::npos);
    Frame();
    Frame();
    ASSERT_FALSE(headset.HapticPulses().empty());
    EvalLua(L, "runCommand('vrMotionControllers', 'off')");
    Frame();  // the mod tells the Injector on this frame
    headset.runtime->controllers.hapticPulses.clear();
    Frame();
    Frame();
    EXPECT_TRUE(headset.HapticPulses().empty());
}

TEST_F(VirtualGamepadRumbleTest, VibrationWithoutTheVirtualGamepadReachesNoHand) {
    // The player turned the pad off: the game's call for any slot goes to the real devices.
    EvalLua(L, "runCommand('vrVirtualGamepad', 'off')");
    NextFrame(&headset);
    EXPECT_EQ(GameRumbles(kFull, kFull), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED));
    for (int i = 0; i < 3; ++i) Frame();
    EXPECT_TRUE(headset.HapticPulses().empty());

    // Turned off while rumbling, the rumble stops, and it does not come back when the pad does.
    TurnPadOn();
    GameRumbles(kFull, kFull);
    Frame();
    ASSERT_FALSE(headset.HapticPulses().empty());
    EvalLua(L, "runCommand('vrVirtualGamepad', 'off')");
    NextFrame(&headset);
    headset.runtime->controllers.hapticPulses.clear();
    Frame();
    Frame();
    EXPECT_TRUE(headset.HapticPulses().empty());
    EXPECT_EQ(GameRumbles(kFull, kFull), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED));

    TurnPadOn();
    Frame();
    Frame();
    EXPECT_TRUE(headset.HapticPulses().empty()) << "the game has not asked for any rumble from the new pad";
}

TEST_F(VirtualGamepadRumbleTest, VibrationSetForAnotherSlotReachesNoHandAndGoesToTheRealDevices) {
    TurnPadOn();
    EXPECT_EQ(GameRumbles(kFull, kFull, /*slot=*/0), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED))
        << "no real library to pass to in this test, and the answer is the one a missing pad gives";
    for (int i = 0; i < 3; ++i) Frame();
    EXPECT_TRUE(headset.HapticPulses().empty());
}
