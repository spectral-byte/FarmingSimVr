// Motion Controller input in the Injector's OpenXR layer (ADR-0025): the action set lifecycle, the
// suggested bindings, graceful refusal, and hand state located like the head pose. Runs the real
// OpenXrManager against FakeOpenXrRuntime's controllers.

#include <gtest/gtest.h>
#include "fake_openxr_runtime.h"
#include <algorithm>
#include <functional>

namespace fs25vr {
namespace {

using Controllers = FakeOpenXrControllers;

bool HasAction(const Controllers& c, const std::string& name, XrActionType type) {
    return std::any_of(c.actions.begin(), c.actions.end(), [&](const Controllers::ActionRecord& a) {
        return a.name == name && a.type == type &&
               a.subactionPaths == std::vector<std::string>{ "/user/hand/left", "/user/hand/right" };
    });
}

HeadPose PoseAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

} // namespace

TEST(OpenXrManagerTest, MotionControllerActionSetIsDeclaredAndAttachedExactlyOncePerSession) {
    FakeTrackedHeadset headset;
    auto& controllers = headset.runtime->controllers;

    EXPECT_TRUE(headset.manager->HasMotionControllerActions());
    EXPECT_EQ(controllers.actionSetNames.size(), 1u);
    EXPECT_EQ(controllers.attachCalls, 1u);
    EXPECT_EQ(controllers.actionSetsAttached, 1u);

    // Asking for the session again, running many frames and a STOPPING -> READY cycle all reuse the
    // one attached set: OpenXR allows a single attach per session.
    EXPECT_TRUE(headset.manager->CreateSession());
    for (int i = 0; i < 5; ++i) headset.NextFrame();
    headset.runtime->events.PushSessionState(XR_SESSION_STATE_STOPPING);
    headset.manager->PollEvents();
    EXPECT_FALSE(headset.manager->IsSessionRunning());
    headset.runtime->events.PushSessionState(XR_SESSION_STATE_READY);
    headset.manager->PollEvents();
    EXPECT_TRUE(headset.manager->IsSessionRunning());
    for (int i = 0; i < 5; ++i) headset.NextFrame();

    EXPECT_EQ(controllers.attachCalls, 1u);
    EXPECT_EQ(controllers.actionSetsAttached, 1u);
    EXPECT_EQ(controllers.actionSetNames.size(), 1u);
}

TEST(OpenXrManagerTest, ActionSetHoldsRawPerHandInputsAndAHapticOutput) {
    FakeTrackedHeadset headset;
    const auto& controllers = headset.runtime->controllers;

    EXPECT_TRUE(HasAction(controllers, "aim_pose", XR_ACTION_TYPE_POSE_INPUT));
    EXPECT_TRUE(HasAction(controllers, "trigger", XR_ACTION_TYPE_FLOAT_INPUT));
    EXPECT_TRUE(HasAction(controllers, "grip", XR_ACTION_TYPE_FLOAT_INPUT));
    EXPECT_TRUE(HasAction(controllers, "thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT));
    EXPECT_TRUE(HasAction(controllers, "thumbstick_click", XR_ACTION_TYPE_BOOLEAN_INPUT));
    EXPECT_TRUE(HasAction(controllers, "button_lower", XR_ACTION_TYPE_BOOLEAN_INPUT));
    EXPECT_TRUE(HasAction(controllers, "button_upper", XR_ACTION_TYPE_BOOLEAN_INPUT));
    EXPECT_TRUE(HasAction(controllers, "menu", XR_ACTION_TYPE_BOOLEAN_INPUT));
    EXPECT_TRUE(HasAction(controllers, "haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT));
    EXPECT_EQ(controllers.actions.size(), 9u) << "only raw inputs and the haptic output belong in the set";
}

TEST(OpenXrManagerTest, SuggestedBindingsCoverTouchIndexViveAndKhronosSimpleControllers) {
    FakeTrackedHeadset headset;
    const auto& controllers = headset.runtime->controllers;

    for (const char* profile : { "/interaction_profiles/oculus/touch_controller",
                                 "/interaction_profiles/valve/index_controller",
                                 "/interaction_profiles/htc/vive_controller",
                                 "/interaction_profiles/khr/simple_controller" }) {
        EXPECT_EQ(controllers.suggestedBindings.count(profile), 1u) << profile;
        // Every profile aims with its hands and vibrates them.
        EXPECT_EQ(controllers.BindingsFor(profile, "aim_pose"),
                  (std::vector<std::string>{ "/user/hand/left/input/aim/pose", "/user/hand/right/input/aim/pose" })) << profile;
        EXPECT_EQ(controllers.BindingsFor(profile, "haptic"),
                  (std::vector<std::string>{ "/user/hand/left/output/haptic", "/user/hand/right/output/haptic" })) << profile;
    }

    const auto touch = "/interaction_profiles/oculus/touch_controller";
    EXPECT_EQ(controllers.BindingsFor(touch, "trigger"),
              (std::vector<std::string>{ "/user/hand/left/input/trigger/value", "/user/hand/right/input/trigger/value" }));
    EXPECT_EQ(controllers.BindingsFor(touch, "grip"),
              (std::vector<std::string>{ "/user/hand/left/input/squeeze/value", "/user/hand/right/input/squeeze/value" }));
    EXPECT_EQ(controllers.BindingsFor(touch, "thumbstick"),
              (std::vector<std::string>{ "/user/hand/left/input/thumbstick", "/user/hand/right/input/thumbstick" }));
    EXPECT_EQ(controllers.BindingsFor(touch, "thumbstick_click"),
              (std::vector<std::string>{ "/user/hand/left/input/thumbstick/click", "/user/hand/right/input/thumbstick/click" }));
    // Touch: X/Y on the left hand, A/B on the right; the menu button exists on the left only.
    EXPECT_EQ(controllers.BindingsFor(touch, "button_lower"),
              (std::vector<std::string>{ "/user/hand/left/input/x/click", "/user/hand/right/input/a/click" }));
    EXPECT_EQ(controllers.BindingsFor(touch, "button_upper"),
              (std::vector<std::string>{ "/user/hand/left/input/y/click", "/user/hand/right/input/b/click" }));
    EXPECT_EQ(controllers.BindingsFor(touch, "menu"), (std::vector<std::string>{ "/user/hand/left/input/menu/click" }));

    const auto index = "/interaction_profiles/valve/index_controller";
    EXPECT_EQ(controllers.BindingsFor(index, "button_lower"),
              (std::vector<std::string>{ "/user/hand/left/input/a/click", "/user/hand/right/input/a/click" }));
    EXPECT_EQ(controllers.BindingsFor(index, "button_upper"),
              (std::vector<std::string>{ "/user/hand/left/input/b/click", "/user/hand/right/input/b/click" }));
    EXPECT_TRUE(controllers.BindingsFor(index, "menu").empty()) << "Index controllers have no menu button";

    const auto vive = "/interaction_profiles/htc/vive_controller";
    EXPECT_EQ(controllers.BindingsFor(vive, "menu"),
              (std::vector<std::string>{ "/user/hand/left/input/menu/click", "/user/hand/right/input/menu/click" }));
    EXPECT_EQ(controllers.BindingsFor(vive, "thumbstick"),
              (std::vector<std::string>{ "/user/hand/left/input/trackpad", "/user/hand/right/input/trackpad" }));

    // The Khronos simple controller only has select and menu: select acts as the trigger.
    const auto simple = "/interaction_profiles/khr/simple_controller";
    EXPECT_EQ(controllers.BindingsFor(simple, "trigger"),
              (std::vector<std::string>{ "/user/hand/left/input/select/click", "/user/hand/right/input/select/click" }));
    EXPECT_EQ(controllers.BindingsFor(simple, "menu"),
              (std::vector<std::string>{ "/user/hand/left/input/menu/click", "/user/hand/right/input/menu/click" }));
}

// ---- A runtime that refuses the action set must cost nothing else ----

namespace {

// Runs the session with one action-set step refused and checks the promises of ADR-0025: the
// session still starts, head tracking works, and both hands report untracked.
void ExpectRefusalLeavesHeadTrackingAlone(const std::function<void(Controllers&)>& refuse, const char* what) {
    SCOPED_TRACE(what);
    auto runtime = std::make_shared<FakeOpenXrRuntime>();
    refuse(runtime->controllers);
    auto manager = std::make_shared<OpenXrManager>(runtime);
    ASSERT_TRUE(manager->Initialize());
    ASSERT_TRUE(manager->CreateSession());
    ASSERT_TRUE(manager->CreateTrackingSpaces());
    manager->PollEvents();

    EXPECT_TRUE(manager->IsSessionRunning());
    EXPECT_FALSE(manager->HasMotionControllerActions());

    HeadPose head = PoseAt(0.1f, 1.6f, -0.2f);
    runtime->tracking.SetHeadPose(head);
    runtime->controllers.hands[0].tracked = true;
    runtime->controllers.hands[1].tracked = true;
    ASSERT_TRUE(manager->BeginFrame());
    const HeadPose polled = manager->PollHeadPose();
    EXPECT_TRUE(polled.isValid);
    EXPECT_FLOAT_EQ(polled.position.y, 1.6f);
    for (uint32_t hand = 0; hand < 2; ++hand) {
        const HandState state = manager->GetHandState(hand);
        EXPECT_FALSE(state.tracked);
        EXPECT_FALSE(state.aim.isValid);
        EXPECT_FLOAT_EQ(state.trigger, 0.0f);
    }
    EXPECT_TRUE(manager->EndFrame());

    manager->Shutdown();
}

} // namespace

TEST(OpenXrManagerTest, RefusedActionSetStepLeavesSessionAndHeadTrackingUnchanged) {
    ExpectRefusalLeavesHeadTrackingAlone([](Controllers& c) { c.stringToPathResult = XR_ERROR_RUNTIME_FAILURE; }, "xrStringToPath");
    ExpectRefusalLeavesHeadTrackingAlone([](Controllers& c) { c.RefuseActionSet(); }, "xrCreateActionSet");
    ExpectRefusalLeavesHeadTrackingAlone([](Controllers& c) { c.createActionResult = XR_ERROR_RUNTIME_FAILURE; }, "xrCreateAction");
    ExpectRefusalLeavesHeadTrackingAlone([](Controllers& c) { c.suggestBindingsResult = XR_ERROR_RUNTIME_FAILURE; }, "xrSuggestInteractionProfileBindings");
    ExpectRefusalLeavesHeadTrackingAlone([](Controllers& c) { c.attachResult = XR_ERROR_RUNTIME_FAILURE; }, "xrAttachSessionActionSets");
    ExpectRefusalLeavesHeadTrackingAlone([](Controllers& c) { c.createActionSpaceResult = XR_ERROR_RUNTIME_FAILURE; }, "xrCreateActionSpace");
}

TEST(OpenXrManagerTest, PartiallyDeclaredActionSetIsReleasedWhenALaterStepIsRefused) {
    auto runtime = std::make_shared<FakeOpenXrRuntime>();
    runtime->controllers.attachResult = XR_ERROR_RUNTIME_FAILURE;
    OpenXrManager manager(runtime);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());

    EXPECT_EQ(runtime->controllers.actionSetNames.size(), 1u);
    EXPECT_EQ(runtime->controllers.destroyActionSetCalls, 1u);
    EXPECT_EQ(runtime->controllers.attachCalls, 1u) << "a refused session is not asked to attach again";
    EXPECT_TRUE(manager.CreateSession());
    EXPECT_EQ(runtime->controllers.attachCalls, 1u);
}

TEST(OpenXrManagerTest, ControllerProfilesTheRuntimeDoesNotKnowAreSkippedNotFatal) {
    auto runtime = std::make_shared<FakeOpenXrRuntime>();
    runtime->controllers.unsupportedProfiles = { "/interaction_profiles/htc/vive_controller",
                                                 "/interaction_profiles/khr/simple_controller" };
    OpenXrManager manager(runtime);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());

    EXPECT_TRUE(manager.HasMotionControllerActions());
    EXPECT_EQ(runtime->controllers.suggestedBindings.count("/interaction_profiles/oculus/touch_controller"), 1u);
    EXPECT_EQ(runtime->controllers.suggestedBindings.count("/interaction_profiles/htc/vive_controller"), 0u);
}

TEST(OpenXrManagerTest, ShutdownReleasesTheActionSpacesAndTheActionSet) {
    auto runtime = std::make_shared<FakeOpenXrRuntime>();
    OpenXrManager manager(runtime);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.HasMotionControllerActions());
    ASSERT_EQ(runtime->controllers.actionSpacesCreated, 2u);

    manager.Shutdown();

    EXPECT_EQ(runtime->controllers.destroyedActionSpaces, 2u);
    EXPECT_EQ(runtime->controllers.destroyActionSetCalls, 1u);
    EXPECT_FALSE(manager.HasMotionControllerActions());
    EXPECT_FALSE(manager.GetHandState(0).tracked);
}

TEST(OpenXrManagerTest, ANewSessionAfterShutdownDeclaresAndAttachesTheActionSetAgain) {
    auto runtime = std::make_shared<FakeOpenXrRuntime>();
    OpenXrManager manager(runtime);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    manager.Shutdown();

    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());

    EXPECT_TRUE(manager.HasMotionControllerActions());
    EXPECT_EQ(runtime->controllers.attachCalls, 2u) << "once per session, and this is a second session";
}

// ---- Reading a hand ----

TEST(OpenXrManagerTest, HandStateReportsTheRawInputsSyncedAtFrameBegin) {
    FakeTrackedHeadset headset;
    Controllers::Inputs right;
    right.trigger = 0.75f;
    right.grip = 0.25f;
    right.thumbX = -0.5f;
    right.thumbY = 0.9f;
    right.thumbClick = true;
    right.buttonLower = true;
    right.buttonUpper = false;
    right.menu = true;
    headset.SetHandPose(1, PoseAt(0.3f, 1.2f, -0.4f));
    headset.SetHandInputs(1, right);

    headset.NextFrame();

    const HandState hand = headset.manager->GetHandState(1);
    EXPECT_TRUE(hand.tracked);
    EXPECT_FLOAT_EQ(hand.trigger, 0.75f);
    EXPECT_FLOAT_EQ(hand.grip, 0.25f);
    EXPECT_FLOAT_EQ(hand.thumbX, -0.5f);
    EXPECT_FLOAT_EQ(hand.thumbY, 0.9f);
    EXPECT_TRUE(hand.thumbClick);
    EXPECT_TRUE(hand.buttonLower);
    EXPECT_FALSE(hand.buttonUpper);
    EXPECT_TRUE(hand.menu);

    // The other hand has its own inputs.
    const HandState left = headset.manager->GetHandState(0);
    EXPECT_FLOAT_EQ(left.trigger, 0.0f);
    EXPECT_FALSE(left.menu);
}

TEST(OpenXrManagerTest, HandsAreSyncedOncePerFrameNotOncePerRead) {
    FakeTrackedHeadset headset;
    auto& controllers = headset.runtime->controllers;

    headset.NextFrame();
    const uint32_t afterFirstFrame = controllers.syncCalls;
    EXPECT_EQ(afterFirstFrame, 1u);

    for (int i = 0; i < 10; ++i) {
        headset.manager->GetHandState(0);
        headset.manager->GetHandState(1);
    }
    EXPECT_EQ(controllers.syncCalls, afterFirstFrame) << "reads inside a frame use that frame's sync";

    headset.NextFrame();
    EXPECT_EQ(controllers.syncCalls, afterFirstFrame + 1);
}

TEST(OpenXrManagerTest, InputsChangedInsideAFrameShowAtTheNextFrame) {
    FakeTrackedHeadset headset;
    headset.NextFrame();
    EXPECT_FLOAT_EQ(headset.manager->GetHandState(0).trigger, 0.0f);

    Controllers::Inputs pulled;
    pulled.trigger = 1.0f;
    headset.SetHandInputs(0, pulled);
    EXPECT_FLOAT_EQ(headset.manager->GetHandState(0).trigger, 0.0f) << "a frame sees one consistent snapshot";

    headset.NextFrame();
    EXPECT_FLOAT_EQ(headset.manager->GetHandState(0).trigger, 1.0f);
}

TEST(OpenXrManagerTest, HandStateIsFreshEvenWhenNoFrameHasBegun) {
    // Before the first frame (or between frames) there is no frame snapshot: a read syncs itself,
    // the way PollHeadPose falls back to "now".
    FakeTrackedHeadset headset;
    ASSERT_FALSE(headset.manager->IsFrameBegun());
    Controllers::Inputs pulled;
    pulled.trigger = 0.5f;
    headset.SetHandInputs(1, pulled);
    headset.SetHandPose(1, PoseAt(0.0f, 1.0f, -0.5f));

    const HandState hand = headset.manager->GetHandState(1);

    EXPECT_TRUE(hand.tracked);
    EXPECT_FLOAT_EQ(hand.trigger, 0.5f);
    EXPECT_FLOAT_EQ(hand.aim.position.y, 1.0f);
}

TEST(OpenXrManagerTest, HandAimPoseUsesTheHeadPoseReferenceSpaceAndPredictedDisplayTime) {
    FakeTrackedHeadset headset;
    headset.SetHandPose(0, PoseAt(-0.3f, 1.1f, -0.2f));
    headset.NextFrame();
    ASSERT_TRUE(headset.manager->IsFrameBegun());

    const auto& locates = headset.runtime->controllers.locates;
    ASSERT_FALSE(locates.empty());
    for (const auto& located : locates) {
        EXPECT_EQ(located.baseSpace, headset.manager->GetLocalSpace()) << "same reference space as the head pose";
        EXPECT_EQ(located.time, 1000000) << "FakeOpenXrRuntime::WaitFrame's predicted display time, as the head pose uses";
    }
    const HandState hand = headset.manager->GetHandState(0);
    EXPECT_FLOAT_EQ(hand.aim.position.x, -0.3f);
    EXPECT_FLOAT_EQ(hand.aim.position.y, 1.1f);
    EXPECT_FLOAT_EQ(hand.aim.position.z, -0.2f);
    EXPECT_TRUE(hand.aim.isValid);
    EXPECT_TRUE(hand.aim.positionTracked);
    EXPECT_TRUE(hand.aim.orientationTracked);
}

TEST(OpenXrManagerTest, HandAimPoseUsesTheSameOrientationConventionAsTheHeadPose) {
    FakeTrackedHeadset headset;
    HeadPose aimed = PoseAt(0.0f, 1.0f, 0.0f);
    aimed.rotation = EulerAngles(0.0f, 0.5f, 0.0f);  // pitch, yaw, roll as for SetHeadPose
    headset.SetHeadPose(aimed);
    headset.SetHandPose(1, aimed);
    headset.NextFrame();

    const HeadPose head = headset.manager->PollHeadPose();
    const HandState hand = headset.manager->GetHandState(1);

    EXPECT_NEAR(hand.aim.orientation.x, head.orientation.x, 1e-6f);
    EXPECT_NEAR(hand.aim.orientation.y, head.orientation.y, 1e-6f);
    EXPECT_NEAR(hand.aim.orientation.z, head.orientation.z, 1e-6f);
    EXPECT_NEAR(hand.aim.orientation.w, head.orientation.w, 1e-6f);
    EXPECT_NEAR(hand.aim.rotation.yaw, head.rotation.yaw, 1e-6f);
    EXPECT_NEAR(hand.aim.rotation.yaw, 0.5f, 1e-4f);
}

TEST(OpenXrManagerTest, HandAimPoseFollowsTheRecenterActionAtOnce) {
    FakeTrackedHeadset headset;
    // The head stands at (1, 1.6, 0.5) looking 90 degrees to the left; the hand is one metre
    // straight ahead of it: (0, 0, -1) in head space.
    HeadPose head = PoseAt(1.0f, 1.6f, 0.5f);
    head.rotation = EulerAngles(0.0f, 1.5707964f, 0.0f);
    headset.SetHeadPose(head);
    headset.SetHandPose(1, PoseAt(0.0f, 1.6f, 0.5f));  // ahead of that head: head + (-1, 0, 0)
    headset.NextFrame();
    EXPECT_NEAR(headset.manager->GetHandState(1).aim.position.x, 0.0f, 1e-4f) << "before Recenter: in the original origin";
    EXPECT_NEAR(headset.manager->GetHandState(1).aim.position.z, 0.5f, 1e-4f);

    headset.manager->Recenter();

    // Recenter makes the head the origin facing -Z, so the hand is now straight ahead of it.
    const HandState hand = headset.manager->GetHandState(1);
    EXPECT_NEAR(hand.aim.position.x, 0.0f, 1e-4f);
    EXPECT_NEAR(hand.aim.position.y, 0.0f, 1e-4f);
    EXPECT_NEAR(hand.aim.position.z, -1.0f, 1e-4f);
}

TEST(OpenXrManagerTest, AnUntrackedControllerReportsUntrackedWithZeroedInputs) {
    FakeTrackedHeadset headset;
    Controllers::Inputs held;
    held.trigger = 1.0f;
    held.menu = true;
    headset.SetHandInputs(0, held);
    headset.SetHandPose(0, PoseAt(0.0f, 1.0f, 0.0f), /*tracked=*/false);

    headset.NextFrame();

    const HandState hand = headset.manager->GetHandState(0);
    EXPECT_FALSE(hand.tracked);
    EXPECT_FALSE(hand.aim.isValid);
    EXPECT_FLOAT_EQ(hand.trigger, 0.0f);
    EXPECT_FALSE(hand.menu);
}

TEST(OpenXrManagerTest, ASessionThatIsNotFocusedReportsNoInputWithoutAnError) {
    FakeTrackedHeadset headset;
    headset.SetHandPose(1, PoseAt(0.0f, 1.0f, -0.3f));
    headset.NextFrame();
    ASSERT_TRUE(headset.manager->GetHandState(1).tracked);

    headset.runtime->controllers.syncActionsResult = XR_SESSION_NOT_FOCUSED;
    headset.NextFrame();

    EXPECT_FALSE(headset.manager->GetHandState(1).tracked);
    EXPECT_TRUE(headset.manager->HasMotionControllerActions()) << "not being focused is not a refusal";
    EXPECT_TRUE(headset.manager->IsSessionRunning());

    headset.runtime->controllers.syncActionsResult = XR_SUCCESS;
    headset.NextFrame();
    EXPECT_TRUE(headset.manager->GetHandState(1).tracked);
}

TEST(OpenXrManagerTest, AnOutOfRangeHandReportsUntracked) {
    FakeTrackedHeadset headset;
    headset.NextFrame();
    EXPECT_FALSE(headset.manager->GetHandState(2).tracked);
    EXPECT_FALSE(headset.manager->GetHandState(1000).tracked);
}

TEST(OpenXrManagerTest, PassThroughReportsBothHandsUntracked) {
    auto runtime = std::make_shared<FakeOpenXrRuntime>();
    OpenXrManager manager(runtime);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    runtime->events.PushSessionState(XR_SESSION_STATE_LOSS_PENDING);
    manager.PollEvents();
    ASSERT_TRUE(manager.IsPassThrough());

    EXPECT_FALSE(manager.GetHandState(0).tracked);
    EXPECT_FALSE(manager.GetHandState(1).tracked);
}

// ---- Haptics through the dispatch (OpenXrManager::PulseHaptic is covered in test_haptics.cpp) ----

TEST(OpenXrManagerTest, HapticFeedbackAppliedThroughTheDispatchReachesTheNamedHandOnly) {
    FakeTrackedHeadset headset;
    auto& controllers = headset.runtime->controllers;
    const XrAction haptic = controllers.ActionByName("haptic");
    ASSERT_NE(haptic, XR_NULL_HANDLE);

    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.duration = 20000000;  // 20 ms in nanoseconds
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    vibration.amplitude = 0.5f;
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = haptic;
    info.subactionPath = controllers.HandPath(1);

    EXPECT_EQ(headset.runtime->ApplyHapticFeedback(headset.manager->GetSession(), &info,
                                                   reinterpret_cast<const XrHapticBaseHeader*>(&vibration)),
              XR_SUCCESS);
    EXPECT_EQ(headset.runtime->StopHapticFeedback(headset.manager->GetSession(), &info), XR_SUCCESS);

    ASSERT_EQ(headset.HapticPulses().size(), 1u);
    EXPECT_EQ(headset.HapticPulses()[0].hand, 1);
    EXPECT_EQ(headset.HapticPulses()[0].duration, 20000000);
    EXPECT_FLOAT_EQ(headset.HapticPulses()[0].amplitude, 0.5f);
    EXPECT_EQ(controllers.stopHapticCalls, 1u);
}

} // namespace fs25vr