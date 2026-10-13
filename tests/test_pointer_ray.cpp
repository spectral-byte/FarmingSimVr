// The Pointer Ray and Interactive Control Click Points (spec #71), seen from outside: the
// real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with a stand-in for the
// Interactive Control mod (companion_mod_stubs.h). The test moves the fake Motion Controllers and the
// game clock, drives frames the way the game would (camera update, Interactive Control's own update,
// the Companion Mod's update) and asserts which Click Point Interactive Control executed, what the
// ray drew, and what the log says.

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

// The engine globals the Companion Mod looks at when it draws.
constexpr const char* kDrawStubs = R"luau(
    _G.debugLines, _G.debugPoints = {}, {}
    _G.drawDebugLine = function(x0, y0, z0, r0, g0, b0, x1, y1, z1, r1, g1, b1, solid)
        table.insert(_G.debugLines, { x0 = x0, y0 = y0, z0 = z0, x1 = x1, y1 = y1, z1 = z1 })
    end
    _G.drawDebugPoint = function(x, y, z, r, g, b, a, solid)
        table.insert(_G.debugPoints, { x = x, y = y, z = z })
    end
    _G.drawPass = function()
        _G.debugLines, _G.debugPoints = {}, {}
        VRCompanion:draw()
    end
)luau";

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

// Stubs of the engine's XML settings API that remember what was saved, so a later session can load it.
constexpr const char* kXmlStubs = R"luau(
    _G.savedXml = _G.savedXml or {}
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
// A cab at the world origin, an Interactive Control release the Companion Mod supports, and the mod
// started: the player sits at (0, 1.5, 0) looking along world +Z, so a hand held at (x, y, -z)
// from the head points along +Z from world (-x, 1.5 + y, +z).
void StartMod(lua_State* L, fs25vr::VRBridge& bridge, const char* icVariant = "ok", const char* scene = "StubScene.cab({})") {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubInteractiveControl, "=interactiveControl"), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, kDrawStubs, "=drawStubs"), 0);
    if (icVariant != nullptr) {
        ASSERT_EQ(RunLuauChunk(L, std::string("StubIC.install('") + icVariant + "')", "=installIc"), 0);
    }
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

} // namespace

#define FRAMES(count) \
    for (int frame_ = 0; frame_ < (count); ++frame_) { PreFrame(L); NextFrame(&headset); }

// The Pointer Hand: picks the Motion Controller up (it moves) and aims it along the given pose.
#define PICK_UP_AND_AIM(hand, x, y, z) \
    do { \
        SetGameTime(10000); \
        headset.SetHandPose((hand), HandAt((x) - 0.1f, (y), (z))); \
        FRAMES(1); \
        headset.SetHandPose((hand), HandAt((x), (y), (z))); \
        FRAMES(1); \
    } while (0)

TEST_F(CompanionModTest, PullingTheTriggerOnAClickPointInTheRayExecutesItOnce) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    // The hand is held 0.3 m right of and 0.3 m ahead of the head, pointing straight ahead: in the world
    // it starts at (-0.3, 1.5, 0.3) and points along +Z.
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('lights', -0.3, 1.5, 2.3)", "=addPoint"), 0);

    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "lights") << "Interactive Control treats the aimed Click Point as clickable";
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0") << "aiming alone never clicks";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "1");
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lights");
}

TEST_F(CompanionModTest, VrPointerDiagnosticsSaysWhyPointingWorksOrNot) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('lights', -0.3, 1.5, 2.3)", "=addPoint"), 0);

    FRAMES(1);
    const std::string idle = EvalLua(L, "runCommand('vrPointerDiagnostics')");
    EXPECT_NE(idle.find("there is no Pointer Hand"), std::string::npos) << idle;

    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    const std::string aimed = EvalLua(L, "runCommand('vrPointerDiagnostics')");
    EXPECT_NE(aimed.find("Pointer Ray: placed"), std::string::npos) << aimed;
    EXPECT_NE(aimed.find("Pointer Hand: right"), std::string::npos) << aimed;
    EXPECT_NE(aimed.find("pointer controlled by: ray"), std::string::npos) << aimed;
    EXPECT_NE(aimed.find("Interactive Control support: installed"), std::string::npos) << aimed;
    EXPECT_NE(aimed.find("selected: yes"), std::string::npos) << aimed;
}

TEST_F(CompanionModTest, ThePointerRayIsPlacedWhileTheEnginesHeadTrackingReadsOff) {
    // In the game isHeadTrackingActive() reads false (the engine's own setting) while the mod places the head
    // itself, so it must not stop the ray (seen in the headset: "the head is not placed in the world").
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('lights', -0.3, 1.5, 2.3)", "=addPoint"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:setHeadTrackingEnabled(false)", "=headTrackingOff"), 0);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHeadTrackingActive()"), "false");

    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "lights");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "1");
}

TEST_F(CompanionModTest, TheClickPointNearestTheRaysCenterInsideTheConeWinsAndNothingOutsideIsSelected) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    // The ray runs from world (-0.3, 1.5, 0.3) along +Z. Two metres ahead, a sideways offset of
    // 2 * tan(angle) is that many degrees off its centre: 3 degrees, 1.5 degrees on the other side,
    // and 8 degrees, which is outside the default 5 degree cone.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('threeDegrees', -0.3 + 0.1048, 1.5, 2.3)
        StubIC.addPoint('oneAndAHalfDegrees', -0.3 - 0.0524, 1.5, 2.3)
        StubIC.addPoint('eightDegrees', -0.3 + 0.2811, 1.5, 2.3)
    )luau", "=addPoints"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "oneAndAHalfDegrees") << "the nearest to the centre of the ray wins";

    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[2].inRange = false", "=hideNearest"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "threeDegrees");

    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[1].inRange = false", "=hideSecond"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "nil") << "8 degrees is outside the cone";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0") << "a pull with nothing selected clicks nothing";
}

TEST_F(CompanionModTest, VrPointerConeChangesTheConeAndIsSaved) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('eightDegrees', -0.3 + 0.2811, 1.5, 2.3)", "=addPoint"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "nil") << "outside the default cone";

    EXPECT_NE(EvalLua(L, "runCommand('vrPointerCone', '10')").find("10"), std::string::npos);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "eightDegrees") << "inside a 10 degree cone";

    EXPECT_EQ(EvalLua(L, "runCommand('vrPointerCone', '10') ~= nil and VRCompanion.pointerConeDegrees"), "10");
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerCone', 'wide')").find("Usage"), std::string::npos);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerCone', '500')").find("Usage"), std::string::npos);
    EXPECT_EQ(EvalLua(L, "VRCompanion.pointerConeDegrees"), "10") << "an invalid value changes nothing";
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerCone')").find("10"), std::string::npos) << "without a value it reports the current cone";

    // A later session loads what was saved.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:saveConfiguration()", "=save"), 0);
    EXPECT_EQ(EvalLua(L, "savedXml['vrSettings.pointerConeDegrees']"), "10");
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.pointerConeDegrees = VRCompanion.DEFAULT_POINTER_CONE_DEGREES", "=forget"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadConfiguration('mockSettings.xml')", "=reload"), 0);
    EXPECT_EQ(EvalLua(L, "VRCompanion.pointerConeDegrees"), "10");
}

TEST_F(CompanionModTest, OnePullClicksOnceHoldingKeepsOnlyAnAnalogClickPointActing) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('switch', -0.3, 1.5, 2.3)
        StubIC.addPoint('lever', -0.3, 1.5, 2.3, { analog = true, inRange = false })
    )luau", "=addPoints"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);

    // A pull held for ten frames clicks the switch once, however long it is held.
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(10);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "1");
    // Letting go and pulling again is a second click.
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "2");
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);

    // The analog Click Point keeps acting for as long as the trigger is held.
    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[1].inRange = false; StubIC.points[2].inRange = true; StubIC.executed = {}", "=useLever"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "lever");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(6);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "6") << "once per frame while held";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "6") << "and not after the trigger is let go";
}

TEST_F(CompanionModTest, AClickPointInteractiveControlWillNotTestIsNeverSelectedByTheRay) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    // Interactive Control tests a static outdoor Click Point that faces away from the camera with no
    // mouse position, so it can never be clicked. It is nearest the ray's centre here.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('away', -0.3, 1.5, 2.3, { facesAway = true })
        StubIC.addPoint('threeDegrees', -0.3 + 0.1048, 1.5, 2.3)
    )luau", "=addPoints"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "threeDegrees") << "the ray falls back to the Click Point Interactive Control allows";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "table.concat(StubIC.executed, ',')"), "threeDegrees");
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);

    // With only the Click Point that faces away in the cone nothing is selected, clicked or drawn.
    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[2].inRange = false; StubIC.executed = {}", "=hideSecond"), 0);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "nil");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0");
}

TEST_F(CompanionModTest, ATriggerThatRestsHalfWayNeitherClicksNorStopsAHoldFlickering) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('switch', -0.3, 1.5, 2.3)", "=addPoint"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(0.5f));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0") << "half-way is not a pull";
    headset.SetHandInputs(kRight, Trigger(0.7f));
    FRAMES(1);
    headset.SetHandInputs(kRight, Trigger(0.5f));  // wobbling between 0.4 and 0.6 while held
    FRAMES(1);
    headset.SetHandInputs(kRight, Trigger(0.7f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "1") << "one pull, however the trigger wobbles while held";
}

TEST_F(CompanionModTest, AClickDoesNotHappenWhenTheTriggerWasAlreadyDownBeforeTheHandWasAimed) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('switch', -0.3, 1.5, 2.3)", "=addPoint"), 0);
    SetGameTime(10000);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    headset.SetHandPose(kRight, HandAt(0.2f, 0.0f, -0.3f));
    FRAMES(2);  // trigger held, Motion Controller still: not in use, nothing to click with
    headset.SetHandPose(kRight, HandAt(0.3f, 0.0f, -0.3f));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1");
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0") << "a trigger that never went down while aiming is not a pull";
}

TEST_F(CompanionModTest, ThePointerRayIsALineFromTheHandWithADotWhereItLands) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('switch', -0.3, 1.5, 2.3)", "=addPoint"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);

    // On a Click Point the ray ends on it.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        drawPass()
        assert(#debugLines == 1 and #debugPoints == 1, "one line and one dot")
        local line, dot = debugLines[1], debugPoints[1]
        assert(math.abs(line.x0 + 0.3) < 1e-4 and math.abs(line.y0 - 1.5) < 1e-4 and math.abs(line.z0 - 0.3) < 1e-4, "the line starts at the hand")
        assert(math.abs(line.x1 + 0.3) < 1e-4 and math.abs(line.y1 - 1.5) < 1e-4 and math.abs(line.z1 - 2.3) < 1e-4, "and ends on the Click Point")
        assert(math.abs(dot.x + 0.3) < 1e-4 and math.abs(dot.z - 2.3) < 1e-4, "the dot is at the end")
    )luau", "=drawOnClickPoint"), 0);

    // Aimed past it, the ray runs on a fixed length along the aim.
    headset.SetHandPose(kRight, HandAt(0.3f, 0.0f, -0.3f, 0.0f, 0.5f));  // turned 0.5 rad
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        drawPass()
        assert(#debugLines == 1 and #debugPoints == 1)
        local line = debugLines[1]
        local length = math.sqrt((line.x1 - line.x0)^2 + (line.y1 - line.y0)^2 + (line.z1 - line.z0)^2)
        assert(math.abs(length - VRCompanion.POINTER_RAY_LENGTH) < 1e-3, "tracking-space turn of the hand turns the ray")
        -- a hand yawed +0.5 rad in tracking space aims along (-sin 0.5, 0, -cos 0.5); the cab turns that by pi
        local dx = (line.x1 - line.x0) / length
        assert(math.abs(dx - math.sin(0.5)) < 1e-3, "direction x " .. dx)
    )luau", "=drawPastTheClickPoint"), 0);
}

TEST_F(CompanionModTest, ThePointerRayIsHiddenWithoutActiveClickPointsWithoutAPointerHandAndWhenUntracked) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('switch', -0.3, 1.5, 2.3)", "=addPoint"), 0);
    auto drawn = [&]() {
        EXPECT_EQ(RunLuauChunk(L, "drawPass()", "=drawPass"), 0);
        return EvalLua(L, "#debugLines + #debugPoints");
    };

    EXPECT_EQ(drawn(), "0") << "no Pointer Hand yet";
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    EXPECT_EQ(drawn(), "2") << "Pointer Hand in use and a Click Point to hit";

    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[1].inRange = false", "=outOfRange"), 0);
    FRAMES(4);  // Interactive Control deactivates the Click Point; the ray has nothing to hit
    EXPECT_EQ(drawn(), "0") << "no active Click Points";
    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[1].inRange = true", "=inRange"), 0);
    FRAMES(2);
    EXPECT_EQ(drawn(), "2");

    headset.SetHandTracked(kRight, false);
    FRAMES(1);
    EXPECT_EQ(drawn(), "0") << "an untracked Pointer Hand leaves no frozen ray";
    headset.SetHandTracked(kRight, true);
    FRAMES(1);
    EXPECT_EQ(drawn(), "2") << "back when tracking returns while the hand is still in use";

    AdvanceGameTime(3100);  // the Motion Controller lies still for more than 3 s
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil");
    EXPECT_EQ(drawn(), "0") << "a Motion Controller left lying still has no ray";
}

// Where a hand held at (hx, hy, hz) from a head at the origin of the tracking space is in the world,
// computed independently of the mod: the cab's head tracking node turns tracking space by the cab's
// yaw plus the cab model's half turn (and by a Snap Turn), and sits 1.5 m above the cab.
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
)luau";

TEST_F(CompanionModTest, ThePointerRayStartsAtTheRealHandWhereverTheCabStandsAndTurns) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "StubScene.cab({ x = 100, y = 2, z = 50, yaw = 0.7 })"));
    ASSERT_EQ(RunLuauChunk(L, kExpectedWorldMath, "=expectedMath"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('switch', 0, 0, 0)", "=addPoint"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, -0.2f, -0.4f);
    FRAMES(2);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        drawPass()
        local line = debugLines[1]
        assert(line ~= nil, "a ray is drawn")
        local cab = { x = 100, y = 2, z = 50, yaw = 0.7 }
        local ex, ey, ez = expectedOrigin(cab, 0.7 + math.pi, 0.3, -0.2, -0.4)
        assert(near(line.x0, ex) and near(line.y0, ey) and near(line.z0, ez),
            string.format("origin %.3f %.3f %.3f expected %.3f %.3f %.3f", line.x0, line.y0, line.z0, ex, ey, ez))
        -- Aimed straight ahead in tracking space: along the cab's forward direction, half-turned.
        local fx, fy, fz = turnedY(0.7 + math.pi, 0, 0, -1)
        local length = math.sqrt((line.x1 - line.x0)^2 + (line.y1 - line.y0)^2 + (line.z1 - line.z0)^2)
        assert(near((line.x1 - line.x0) / length, fx) and near((line.z1 - line.z0) / length, fz), "direction")
    )luau", "=originAndDirection"), 0);
}

TEST_F(CompanionModTest, ThePointerRayStaysOnTheHandAfterASnapTurnAndTheRecenterAction) {
    FakeTrackedHeadset headset;
    HeadPose head;
    head.position = Vector3(0.2f, 0.05f, -0.1f);
    head.rotation = fs25vr::EulerAngles(0.0f, 0.4f, 0.0f);
    head.isValid = true;
    headset.SetHeadPose(head);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kExpectedWorldMath, "=expectedMath"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('switch', 0, 0, 0)", "=addPoint"), 0);
    PICK_UP_AND_AIM(kRight, 0.5f, 0.1f, -0.3f);
    FRAMES(2);

    // What must not change while the hand and head are held still is the ray's origin and direction in
    // the head's own frame (the Cyclopean Audio Listener Node is the head in the world).
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local function inverseRotate(qx, qy, qz, qw, x, y, z)
            qx, qy, qz = -qx, -qy, -qz
            local cx, cy, cz = qy * z - qz * y + qw * x, qz * x - qx * z + qw * y, qx * y - qy * x + qw * z
            return x + 2 * (qy * cz - qz * cy), y + 2 * (qz * cx - qx * cz), z + 2 * (qx * cy - qy * cx)
        end
        _G.handInHeadFrame = function()
            drawPass()
            local line = debugLines[1]
            assert(line ~= nil, "a ray is drawn")
            local lx, ly, lz = getWorldTranslation(VRCompanion.cyclopeanAudioListenerNode)
            local qx, qy, qz, qw = getWorldQuaternion(VRCompanion.cyclopeanAudioListenerNode)
            local ox, oy, oz = inverseRotate(qx, qy, qz, qw, line.x0 - lx, line.y0 - ly, line.z0 - lz)
            local dx, dy, dz = inverseRotate(qx, qy, qz, qw, line.x1 - line.x0, line.y1 - line.y0, line.z1 - line.z0)
            local length = math.sqrt(dx * dx + dy * dy + dz * dz)
            return { ox, oy, oz, dx / length, dy / length, dz / length }
        end
        _G.sameAs = function(a, b)
            for i = 1, 6 do assert(near(a[i], b[i]), "component " .. i .. ": " .. a[i] .. " vs " .. b[i]) end
        end
        _G.before = handInHeadFrame()
        local hx, hy, hz = turnedY(-0.4, 0.3, 0.05, -0.2)  -- hand minus head, turned back by the head's yaw
        assert(near(before[1], hx) and near(before[2], hy) and near(before[3], hz),
            string.format("hand offset in the head frame %.3f %.3f %.3f", before[1], before[2], before[3]))
    )luau", "=baseline"), 0);

    // Snap Turn: the cab view turns, the ray turns with the hand.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.worldBefore = { debugLines[1].x0, debugLines[1].z0 }
        assert(VRCompanion:snapTurnRight(nil, 20000) == true, "snap turn happened")
    )luau", "=snapTurn"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        sameAs(handInHeadFrame(), before)
        assert(not (near(debugLines[1].x0, worldBefore[1]) and near(debugLines[1].z0, worldBefore[2])),
            "the hand moved in the world with the turn")
    )luau", "=afterSnapTurn"), 0);

    // The Recenter Action moves the head and the hand in tracking space together; the ray follows.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onRecenterAction()", "=recenter"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "sameAs(handInHeadFrame(), before)", "=afterRecenter"), 0);
}

TEST_F(CompanionModTest, ThePointerRayLooksTheSameInBothEyesUnderAlternateFrameRendering) {
    FakeTrackedHeadset headset;
    headset.manager->SetIpd(0.064f);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "StubScene.cab({ x = 10, z = 20, yaw = 0.3 })"));
    RegisterTestSetFrameParity(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('switch', 0, 0, 0)
        VRCompanion:setRenderingMode('AFR')
        VRBridge.latchPendingRenderingMode()

    )luau", "=afr"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, -0.1f, -0.4f);

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setFrameParity(0)", "=leftEye"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "drawPass(); _G.leftEyeLine = debugLines[1]; _G.leftEyeCameraX, _G.leftEyeCameraY, _G.leftEyeCameraZ = StubScene.worldPosition(getCamera())", "=drawLeft"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setFrameParity(1)", "=rightEye"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        drawPass()
        local left, right = leftEyeLine, debugLines[1]
        assert(left ~= nil and right ~= nil, "drawn in both eyes")
        local rx, ry, rz = StubScene.worldPosition(getCamera())
        assert(math.abs(rx - leftEyeCameraX) + math.abs(rz - leftEyeCameraZ) > 0.03, "the camera is at the other eye")
        for _, key in ipairs({ "x0", "y0", "z0", "x1", "y1", "z1" }) do
            assert(math.abs(left[key] - right[key]) < 1e-6, key .. " differs between the eyes: " .. left[key] .. " vs " .. right[key])
        end
    )luau", "=sameInBothEyes"), 0);
}

TEST_F(CompanionModTest, ThePointerRayLooksTheSameInBothPassesUnderSequentialDualPass) {
    FakeTrackedHeadset headset;
    headset.manager->SetIpd(0.064f);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "StubScene.cab({ x = 10, z = 20, yaw = 0.3 })"));
    RegisterTestSetFrameParity(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('switch', 0, 0, 0)
        VRCompanion:setRenderingMode('DualPass')
        VRBridge.latchPendingRenderingMode()
        VRBridge.setSubPassIndex(0)
    )luau", "=sdp"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, -0.1f, -0.4f);
    FRAMES(2);

    // One update per logical frame; the camera is placed and the ray drawn in each of the two passes.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local lines = {}
        for pass = 0, 1 do
            VRBridge.setSubPassIndex(pass)
            StubScene.updateCamera()
            drawPass()
            lines[pass] = debugLines[1]
            assert(#debugPoints == 1)
        end
        assert(lines[0] ~= nil and lines[1] ~= nil, "drawn in both passes")
        for _, key in ipairs({ "x0", "y0", "z0", "x1", "y1", "z1" }) do
            assert(math.abs(lines[0][key] - lines[1][key]) < 1e-6, key .. " differs between the passes")
        end
    )luau", "=sameInBothPasses"), 0);
}

TEST_F(CompanionModTest, ThePointerRayStartsAtTheRealHandInAnOutsideCamera) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "StubScene.outside({ x = 10, y = 2, z = 30, yaw = -1.2 })"));
    ASSERT_EQ(RunLuauChunk(L, kExpectedWorldMath, "=expectedMath"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('hitch', 0, 0, 0)", "=addPoint"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, -0.2f, -0.4f);
    FRAMES(2);
    // The head is carried on a level base at the engine camera, turned by its heading, so the hand is
    // that far from the camera, turned the same way.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        drawPass()
        local line = debugLines[1]
        assert(line ~= nil, "a ray is drawn")
        local ex, ey, ez = turnedY(-1.2, 0.3, -0.2, -0.4)
        assert(near(line.x0, 10 + ex) and near(line.y0, 2 + ey) and near(line.z0, 30 + ez),
            string.format("origin %.3f %.3f %.3f", line.x0, line.y0, line.z0))
    )luau", "=outsideOrigin"), 0);
}

TEST_F(CompanionModTest, ThePointerRayStartsAtTheRealHandOnFootAndAfterASnapTurn) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "StubScene.onFoot({ x = 50, z = -20, yaw = 1.0 })"));
    ASSERT_EQ(RunLuauChunk(L, kExpectedWorldMath, "=expectedMath"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('door', 0, 0, 0)", "=addPoint"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, -0.2f, -0.4f);
    FRAMES(2);

    // With the head at the origin of tracking space, the hand is where it is in tracking space as the head sees it:
    // 0.3 right, 0.2 down and 0.4 ahead, whichever way the player's body faces.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local function headFrame()
            drawPass()
            local line = debugLines[1]
            assert(line ~= nil, "a ray is drawn")
            local node = VRCompanion.cyclopeanAudioListenerNode
            local lx, ly, lz = getWorldTranslation(node)
            local qx, qy, qz, qw = getWorldQuaternion(node)
            local function inverseRotate(x, y, z)
                local ix, iy, iz = -qx, -qy, -qz
                local cx, cy, cz = iy * z - iz * y + qw * x, iz * x - ix * z + qw * y, ix * y - iy * x + qw * z
                return x + 2 * (iy * cz - iz * cy), y + 2 * (iz * cx - ix * cz), z + 2 * (ix * cy - iy * cx)
            end
            return line, lx, ly, lz, inverseRotate(line.x0 - lx, line.y0 - ly, line.z0 - lz)
        end
        local line, lx, ly, lz, hx, hy, hz = headFrame()
        assert(math.abs(lx - 50) < 1 and math.abs(lz + 20) < 1, "the head is by the player")
        assert(near(hx, 0.3) and near(hy, -0.2) and near(hz, -0.4), string.format("hand in head frame %.3f %.3f %.3f", hx, hy, hz))
        _G.before = { line.x0, line.z0 }
        assert(VRCompanion:snapTurnLeft(nil, 20000) == true, "snap turn")
        _G.headFrame = headFrame
    )luau", "=onFoot"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local line, lx, ly, lz, hx, hy, hz = headFrame()
        assert(near(hx, 0.3) and near(hy, -0.2) and near(hz, -0.4), string.format("after the turn %.3f %.3f %.3f", hx, hy, hz))
        assert(not (near(line.x0, before[1]) and near(line.z0, before[2])), "the ray moved with the turn")
    )luau", "=onFootAfterTurn"), 0);
}

TEST_F(CompanionModTest, ClickPointsOnAVehicleExteriorAndOnAPlacedObjectCanBeClickedOnFoot) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "StubScene.onFoot({ x = 50, z = -20, yaw = 1.0 })"));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('probe', 0, 0, 0)", "=probe"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, -0.2f, -0.4f);
    FRAMES(2);
    // Where the ray runs now, whichever way the player's body faces: a vehicle's door handle (an
    // outdoor Click Point) two metres along it, and a gate control (a placed object) five metres.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        drawPass()
        local line = debugLines[1]
        local length = math.sqrt((line.x1 - line.x0)^2 + (line.y1 - line.y0)^2 + (line.z1 - line.z0)^2)
        local dx, dy, dz = (line.x1 - line.x0) / length, (line.y1 - line.y0) / length, (line.z1 - line.z0) / length
        StubIC.points[1].inRange = false
        local door = StubIC.addPoint('doorHandle', line.x0 + dx * 2, line.y0 + dy * 2, line.z0 + dz * 2)
        door.outdoor = true
    )luau", "=addExterior"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "doorHandle");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "doorHandle");

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        drawPass()
        local line = debugLines[1]
        local length = math.sqrt((line.x1 - line.x0)^2 + (line.y1 - line.y0)^2 + (line.z1 - line.z0)^2)
        local dx, dy, dz = (line.x1 - line.x0) / length, (line.y1 - line.y0) / length, (line.z1 - line.z0) / length
        StubIC.points[2].inRange = false
        StubIC.addPoint('gateControl', line.x0 + dx * 5, line.y0 + dy * 5, line.z0 + dz * 5)
    )luau", "=addPlaced"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "gateControl");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[2]"), "gateControl");
}

// ---- Interactive Control missing, mismatched, or taking over from the mouse ----

namespace {
int CountWarnings() {
    int count = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find("Interactive Control support disabled") != std::string::npos) ++count;
    }
    return count;
}
} // namespace

TEST_F(CompanionModTest, WithoutInteractiveControlTheModWarnsExactlyOnceAndDrawsNothing) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, nullptr));
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(30);  // NextFrame fails the test if the update raises an error
    EXPECT_EQ(CountWarnings(), 1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1") << "the Pointer Hand itself is unaffected";
    ASSERT_EQ(RunLuauChunk(L, "drawPass(); assert(#debugLines == 0 and #debugPoints == 0)", "=nothingDrawn"), 0);
}

TEST_F(CompanionModTest, NoWarningWhileNoMotionControllerIsTrackedOrWhileMotionControllersAreOff) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, nullptr));
    SetGameTime(10000);
    headset.SetHandTracked(kLeft, false);
    headset.SetHandTracked(kRight, false);
    FRAMES(5);
    EXPECT_EQ(CountWarnings(), 0) << "a player without Motion Controllers is never told about Interactive Control";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
    headset.SetHandTracked(kRight, true);
    FRAMES(5);
    EXPECT_EQ(CountWarnings(), 0) << "with Motion Controllers off the Interactive Control support is never looked at";
}

TEST_F(CompanionModTest, AClickPointClassMissingAFunctionWarnsOnceAndLeavesInteractiveControlAlone) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "renamedUpdateClickable"));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('switch', -0.3, 1.5, 2.3, { screenX = 0.4, screenY = 0.6 })
        _G.originalUpdate = StubIC.Point.update
        _G.originalSetActivated = StubIC.Point.setActivated
    )luau", "=addPoint"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(30);
    EXPECT_EQ(CountWarnings(), 1);
    bool namesTheFunction = false;
    for (const auto& line : s_capturedLogs) {
        if (line.find("Interactive Control support disabled") != std::string::npos && line.find("updateClickable") != std::string::npos) namesTheFunction = true;
    }
    EXPECT_TRUE(namesTheFunction) << "the warning says what is missing";

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(StubIC.Point.update == originalUpdate and StubIC.Point.setActivated == originalSetActivated, "no function was wrapped")
        assert(#StubIC.executed == 0, "the trigger clicked nothing")
        -- Interactive Control still works with its mouse.
        StubIC.mouseX, StubIC.mouseY = 0.4, 0.6
        StubIC.tick()
        assert(StubIC.selected() == 'switch', "its own mouse selection is unchanged")
        drawPass()
        assert(#debugLines == 0, "no ray without Click Point support")
    )luau", "=interactiveControlUnchanged"), 0);
}

TEST_F(CompanionModTest, AManagerMissingItsExecuteFunctionWarnsOnceAndWrapsNothing) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "missingExecute"));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('switch', -0.3, 1.5, 2.3)
        _G.originalUpdateClickable = StubIC.Point.updateClickable
    )luau", "=addPoint"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(20);
    EXPECT_EQ(CountWarnings(), 1);
    ASSERT_EQ(RunLuauChunk(L, "assert(StubIC.Point.updateClickable == originalUpdateClickable)", "=unwrapped"), 0);
}

TEST_F(CompanionModTest, TheMouseKeepsSelectingUntilThePointerHandIsInUseAndTakesOverAgainWhenItRests) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    // One Click Point under the desk mouse (screen 0.4, 0.6), far from where the ray will point.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('underMouse', 30, 1.5, 2.3, { screenX = 0.4, screenY = 0.6 })
        StubIC.mouseX, StubIC.mouseY = 0.4, 0.6
    )luau", "=underMouse"), 0);
    SetGameTime(10000);
    headset.SetHandPose(kRight, HandAt(0.2f, 0.0f, -0.3f));
    FRAMES(4);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "underMouse") << "Motion Controllers lying still: the mouse selects";
    EXPECT_EQ(EvalLua(L, "StubIC.blinks.underMouse ~= nil"), "true");

    headset.SetHandPose(kRight, HandAt(0.3f, 0.0f, -0.3f));
    FRAMES(4);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1");
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "nil") << "the Pointer Ray selects now, and it is not on the Click Point";

    AdvanceGameTime(3100);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil");
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "underMouse") << "the mouse selects again";
}

TEST_F(CompanionModTest, InteractiveControlKeepsItsHoverBlinkOnTheClickPointTheRaySelected) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('aimedAt', -0.3, 1.5, 2.3, { screenX = 0.2, screenY = 0.2 })
        StubIC.addPoint('other', -2.3, 1.5, 2.3, { screenX = 0.8, screenY = 0.8 })
    )luau", "=addPoints"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(6);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert((StubIC.blinks.aimedAt or 0) >= 3, "the selected Click Point blinks as hovered")
        assert(StubIC.blinks.other == nil, "the other one does not")
    )luau", "=blinks"), 0);
}

TEST_F(CompanionModTest, UnloadingTheMapHandsInteractiveControlBackExactlyAsFound) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('switch', -0.3, 1.5, 2.3)
        _G.originals = {}
        for _, name in ipairs({ 'update', 'updateClickable', 'setActivated' }) do originals[name] = StubIC.Point[name] end
    )luau", "=remember"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        for name, original in pairs(originals) do assert(StubIC.Point[name] ~= original, name .. " is wrapped while the support is on") end
        VRCompanion:deleteMap()
        for name, original in pairs(originals) do assert(StubIC.Point[name] == original, name .. " is back as found") end
    )luau", "=unload"), 0);
}

TEST_F(CompanionModTest, AClickPointInteractiveControlStoppedUpdatingIsNoLongerSelectedOrDrawnToward) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('switch', -0.3, 1.5, 2.3)", "=addPoint"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, "drawPass(); assert(#debugLines == 1, 'drawn while it updates')", "=whileUpdating"), 0);

    // The player climbs out: Interactive Control no longer updates the vehicle's Click Points, and
    // they stay marked active, but nobody can click them any more.
    ASSERT_EQ(RunLuauChunk(L, "StubIC.points = {}", "=vehicleGone"), 0);
    FRAMES(5);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "drawPass(); assert(#debugLines == 0, 'hidden once nothing updates it'); assert(#StubIC.executed == 0)", "=afterwards"), 0);
}
