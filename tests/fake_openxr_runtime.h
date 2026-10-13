#pragma once

#include <gtest/gtest.h>
#include "fake_openxr_controllers.h"
#include "fake_openxr_events.h"
#include "fake_openxr_tracking.h"
#include <cstring>
#include <memory>

namespace fs25vr {

// Minimal always-available OpenXR runtime for tests that only need tracking: a headset whose
// head pose and eye FOVs the test drives through `tracking`.
class FakeOpenXrRuntime : public IOpenXrDispatch {
public:
    FakeOpenXrTracking tracking;
    FakeOpenXrControllers controllers{tracking};   // Motion Controllers: poses, inputs, recorded haptics
    FakeOpenXrEventQueue events;

    XrResult EnumerateInstanceExtensionProperties(const char*, uint32_t capacity, uint32_t* count, XrExtensionProperties* properties) override {
        if (count) *count = 1;
        if (capacity > 0 && properties) {
            properties[0].type = XR_TYPE_EXTENSION_PROPERTIES;
            properties[0].next = nullptr;
            strncpy_s(properties[0].extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME, XR_MAX_EXTENSION_NAME_SIZE - 1);
            properties[0].extensionVersion = 1;
        }
        return XR_SUCCESS;
    }
    XrResult CreateInstance(const XrInstanceCreateInfo*, XrInstance* instance) override {
        if (instance) *instance = reinterpret_cast<XrInstance>(static_cast<uintptr_t>(0xDEADBEEF));
        return XR_SUCCESS;
    }
    XrResult DestroyInstance(XrInstance) override { return XR_SUCCESS; }
    XrResult GetInstanceProperties(XrInstance, XrInstanceProperties* props) override {
        if (props) {
            strncpy_s(props->runtimeName, "FakeVR Runtime", XR_MAX_RUNTIME_NAME_SIZE - 1);
            props->runtimeVersion = XR_MAKE_VERSION(1, 0, 0);
        }
        return XR_SUCCESS;
    }
    XrResult GetSystem(XrInstance, const XrSystemGetInfo*, XrSystemId* systemId) override {
        if (systemId) *systemId = 12345;
        return XR_SUCCESS;
    }
    XrResult GetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* props) override {
        if (props) {
            props->systemId = 12345;
            props->trackingProperties.orientationTracking = XR_TRUE;
            props->trackingProperties.positionTracking = XR_TRUE;
        }
        return XR_SUCCESS;
    }
    XrResult EnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacity, uint32_t* count, XrViewConfigurationView* views) override {
        if (count) *count = 2;
        if (capacity >= 2 && views) {
            for (uint32_t i = 0; i < 2; ++i) {
                views[i] = { XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 2064, 4096, 2208, 4096, 1, 4 };
            }
        }
        return XR_SUCCESS;
    }
    XrResult GetD3D12GraphicsRequirementsKHR(XrInstance, XrSystemId, XrGraphicsRequirementsD3D12KHR* reqs) override {
        if (reqs) {
            reqs->minFeatureLevel = D3D_FEATURE_LEVEL_12_0;
            reqs->adapterLuid = { 0, 0 };
        }
        return XR_SUCCESS;
    }
    XrResult PollEvent(XrInstance, XrEventDataBuffer* eventData) override { return events.Poll(eventData); }
    XrResult CreateSession(XrInstance, const XrSessionCreateInfo*, XrSession* session) override {
        if (session) *session = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xCAFE0001));
        events.PushSessionState(XR_SESSION_STATE_READY);
        return XR_SUCCESS;
    }
    XrResult BeginSession(XrSession, const XrSessionBeginInfo*) override { return XR_SUCCESS; }
    XrResult EndSession(XrSession) override { return XR_SUCCESS; }
    XrResult DestroySession(XrSession) override {
        controllers.OnSessionDestroyed();
        return XR_SUCCESS;
    }
    XrResult CreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space) override {
        return tracking.CreateReferenceSpace(createInfo, space);
    }
    XrResult DestroySpace(XrSpace space) override {
        if (!controllers.DestroyActionSpace(space)) {
            tracking.DestroySpace(space);
        }
        return XR_SUCCESS;
    }
    XrResult LocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location) override {
        if (controllers.OwnsSpace(space)) {
            return controllers.LocateActionSpace(space, baseSpace, time, location);
        }
        return tracking.LocateSpace(baseSpace, location);
    }

    // ---- Motion Controllers ----
    XrResult StringToPath(XrInstance, const char* pathString, XrPath* path) override {
        return controllers.StringToPath(pathString, path);
    }
    XrResult CreateActionSet(XrInstance, const XrActionSetCreateInfo* info, XrActionSet* actionSet) override {
        return controllers.CreateActionSet(info, actionSet);
    }
    XrResult DestroyActionSet(XrActionSet actionSet) override { return controllers.DestroyActionSet(actionSet); }
    XrResult CreateAction(XrActionSet actionSet, const XrActionCreateInfo* info, XrAction* action) override {
        return controllers.CreateAction(actionSet, info, action);
    }
    XrResult DestroyAction(XrAction) override { return XR_SUCCESS; }
    XrResult SuggestInteractionProfileBindings(XrInstance, const XrInteractionProfileSuggestedBinding* suggested) override {
        return controllers.SuggestInteractionProfileBindings(suggested);
    }
    XrResult AttachSessionActionSets(XrSession, const XrSessionActionSetsAttachInfo* info) override {
        return controllers.AttachSessionActionSets(info);
    }
    XrResult CreateActionSpace(XrSession, const XrActionSpaceCreateInfo* info, XrSpace* space) override {
        return controllers.CreateActionSpace(info, space);
    }
    XrResult SyncActions(XrSession, const XrActionsSyncInfo* info) override { return controllers.SyncActions(info); }
    XrResult GetActionStateBoolean(XrSession, const XrActionStateGetInfo* info, XrActionStateBoolean* state) override {
        return controllers.GetActionStateBoolean(info, state);
    }
    XrResult GetActionStateFloat(XrSession, const XrActionStateGetInfo* info, XrActionStateFloat* state) override {
        return controllers.GetActionStateFloat(info, state);
    }
    XrResult GetActionStateVector2f(XrSession, const XrActionStateGetInfo* info, XrActionStateVector2f* state) override {
        return controllers.GetActionStateVector2f(info, state);
    }
    XrResult GetActionStatePose(XrSession, const XrActionStateGetInfo* info, XrActionStatePose* state) override {
        return controllers.GetActionStatePose(info, state);
    }
    XrResult ApplyHapticFeedback(XrSession, const XrHapticActionInfo* info, const XrHapticBaseHeader* feedback) override {
        return controllers.ApplyHapticFeedback(info, feedback);
    }
    XrResult StopHapticFeedback(XrSession, const XrHapticActionInfo* info) override {
        return controllers.StopHapticFeedback(info);
    }
    XrResult LocateViews(XrSession, const XrViewLocateInfo*, XrViewState* viewState, uint32_t capacity, uint32_t* count, XrView* views) override {
        return tracking.LocateViews(viewState, capacity, count, views);
    }
    XrResult EnumerateSwapchainFormats(XrSession, uint32_t capacity, uint32_t* count, int64_t* formats) override {
        if (count) *count = 1;
        if (capacity > 0 && formats) formats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        return XR_SUCCESS;
    }
    XrResult CreateSwapchain(XrSession, const XrSwapchainCreateInfo*, XrSwapchain* swapchain) override {
        static uintptr_t s_chain = 0x54AC1001;
        if (swapchain) *swapchain = reinterpret_cast<XrSwapchain>(s_chain++);
        return XR_SUCCESS;
    }
    XrResult DestroySwapchain(XrSwapchain) override { return XR_SUCCESS; }
    XrResult EnumerateSwapchainImages(XrSwapchain, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* images) override {
        if (count) *count = 2;
        if (capacity >= 2 && images) {
            for (uint32_t i = 0; i < 2; ++i) {
                auto* image = reinterpret_cast<XrSwapchainImageD3D12KHR*>(&images[i]);
                image->type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
                image->next = nullptr;
                image->texture = nullptr;
            }
        }
        return XR_SUCCESS;
    }
    XrResult AcquireSwapchainImage(XrSwapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index) override {
        if (index) *index = 0;
        return XR_SUCCESS;
    }
    XrResult WaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo*) override { return XR_SUCCESS; }
    XrResult ReleaseSwapchainImage(XrSwapchain, const XrSwapchainImageReleaseInfo*) override { return XR_SUCCESS; }
    XrResult WaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* frameState) override {
        if (frameState) {
            frameState->type = XR_TYPE_FRAME_STATE;
            frameState->predictedDisplayTime = 1000000;
            frameState->predictedDisplayPeriod = 11111111;
            frameState->shouldRender = XR_TRUE;
        }
        return XR_SUCCESS;
    }
    XrResult BeginFrame(XrSession, const XrFrameBeginInfo*) override { return XR_SUCCESS; }
    XrResult EndFrame(XrSession, const XrFrameEndInfo*) override { return XR_SUCCESS; }
};

// Options for FakeTrackedHeadset that must be in place before the session is created.
struct FakeTrackedHeadsetOptions {
    // The runtime refuses the Motion Controller action set (head tracking still works).
    bool refuseActionSet = false;
};

// An OpenXrManager running against a FakeOpenXrRuntime with its session started and tracking
// spaces created, ready for the test to place the head and the hands and to set eye FOVs.
// Hand ids: 0 = left, 1 = right.
struct FakeTrackedHeadset {
    std::shared_ptr<FakeOpenXrRuntime> runtime = std::make_shared<FakeOpenXrRuntime>();
    std::shared_ptr<OpenXrManager> manager;

    explicit FakeTrackedHeadset(const FakeTrackedHeadsetOptions& options = FakeTrackedHeadsetOptions()) {
        runtime->controllers.RefuseActionSet(options.refuseActionSet);
        manager = std::make_shared<OpenXrManager>(runtime);
        bool ok = manager->Initialize() && manager->CreateSession() && manager->CreateTrackingSpaces();
        manager->PollEvents();  // consume READY so the session is running
        ok = ok && manager->IsSessionRunning();
        if (!ok) {
            ADD_FAILURE() << "FakeTrackedHeadset failed to start an OpenXR session";
        }
    }

    void SetHeadPose(const HeadPose& pose) { runtime->tracking.SetHeadPose(pose); }
    void SetEyeFov(uint32_t eyeIndex, const XrFovf& fov) { runtime->tracking.SetEyeFov(eyeIndex, fov); }

    // Places a hand's aim pose in the runtime's tracking origin (before Recenter re-bases it) and
    // switches the controller on or off. Only position and orientation of `pose` are used.
    // The grip pose follows the aim pose until SetHandGripPose gives it a pose of its own.
    void SetHandPose(uint32_t hand, const HeadPose& pose, bool tracked = true) {
        auto& sim = runtime->controllers.hands[hand & 1];
        sim.pose = FakeOpenXrTracking::ToXrPose(pose);
        sim.gripPose = sim.pose;
        sim.tracked = tracked;
    }
    // Places a hand's grip pose apart from its aim pose in the tracking origin. `tracked = false` makes
    // the grip pose alone untracked: the controller (and so the aim pose) stays on.
    void SetHandGripPose(uint32_t hand, const HeadPose& pose, bool tracked = true) {
        auto& sim = runtime->controllers.hands[hand & 1];
        sim.gripPose = FakeOpenXrTracking::ToXrPose(pose);
        sim.gripTracked = tracked;
    }
    void SetHandTracked(uint32_t hand, bool tracked) { runtime->controllers.hands[hand & 1].tracked = tracked; }
    void SetHandInputs(uint32_t hand, const FakeOpenXrControllers::Inputs& inputs) {
        runtime->controllers.hands[hand & 1].inputs = inputs;
    }
    const std::vector<FakeOpenXrControllers::HapticPulse>& HapticPulses() const { return runtime->controllers.hapticPulses; }

    // One game frame boundary: ends the frame in flight and begins the next, which syncs the
    // actions and locates the hands at that frame's predicted display time, as the Injector does
    // right after Present.
    void NextFrame() {
        manager->EndFrame();
        manager->BeginFrame();
    }
};

} // namespace fs25vr