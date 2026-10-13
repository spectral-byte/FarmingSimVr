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

} // namespace fs25vr