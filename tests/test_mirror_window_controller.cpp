#include <gtest/gtest.h>
#include "proxy/mirror_window_controller.h"
#include "core/injector_runtime.h"
#include <windows.h>
#include <commctrl.h>

namespace fs25vr {

TEST(MirrorWindowControllerTest, DefaultsToInactiveWhenNoCallbackProvided) {
    MirrorWindowController controller;
    EXPECT_FALSE(controller.IsActive());
    EXPECT_EQ(controller.GetGameHwnd(), nullptr);
    EXPECT_FALSE(controller.IsWindowSubclassed());
    EXPECT_FALSE(controller.IsInSizeMove());
}

TEST(MirrorWindowControllerTest, RespectsActiveVrStateCallback) {
    bool vrActive = false;
    MirrorWindowController controller([&vrActive]() { return vrActive; });

    EXPECT_FALSE(controller.IsActive());
    vrActive = true;
    EXPECT_TRUE(controller.IsActive());
}

TEST(MirrorWindowControllerTest, ClampedMirrorWindowRect16x9Displays) {
    RECT work1080{ 0, 0, 1920, 1080 };
    RECT res1080 = MirrorWindowController::ComputeClampedMirrorWindowRect(work1080, WS_POPUP, 0, false);
    EXPECT_EQ(res1080.left, 0);
    EXPECT_EQ(res1080.top, 0);
    EXPECT_EQ(res1080.right, 1920);
    EXPECT_EQ(res1080.bottom, 1080);

    RECT workUltrawide{ 0, 0, 2560, 1080 };
    RECT resUW = MirrorWindowController::ComputeClampedMirrorWindowRect(workUltrawide, WS_POPUP, 0, false);
    EXPECT_EQ(resUW.left, 320);
    EXPECT_EQ(resUW.right, 2240);
    EXPECT_EQ(resUW.top, 0);
    EXPECT_EQ(resUW.bottom, 1080);
}

TEST(MirrorWindowControllerTest, SubclassLifecycleAttachesWhenActiveAndDetachesCleanly) {
    HWND testWnd = CreateWindowA("STATIC", "TestSubclassWnd", WS_OVERLAPPEDWINDOW, 10, 10, 800, 600, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    bool vrActive = false;
    auto controllerPtr = std::make_shared<MirrorWindowController>();
    InjectorRuntime::Dependencies deps;
    deps.windowController = controllerPtr;
    InjectorRuntime runtime(std::move(deps));
    MirrorWindowController& controller = *controllerPtr;
    // The runtime's proxy points the controller at its own VR state; drive it from this test instead.
    controller.SetIsVrActiveCallback([&vrActive]() { return vrActive; });

    // Inert in pass-through (vrActive = false)
    EXPECT_FALSE(controller.AttachWindowSubclass(testWnd));
    EXPECT_FALSE(controller.IsWindowSubclassed());

    // Active VR attaches cleanly
    vrActive = true;
    EXPECT_TRUE(controller.AttachWindowSubclass(testWnd));
    EXPECT_TRUE(controller.IsWindowSubclassed());
    EXPECT_EQ(controller.GetGameHwnd(), testWnd);
    EXPECT_TRUE(controller.IsDetourGetClientRectInstalled());
    EXPECT_TRUE(controller.IsDetourScreenToClientInstalled());
    EXPECT_TRUE(controller.IsDetourSetCursorPosInstalled());
    EXPECT_TRUE(controller.IsDetourClipCursorInstalled());

    // DetachAll
    controller.DetachAll();
    EXPECT_FALSE(controller.IsWindowSubclassed());
    EXPECT_FALSE(controller.IsDetourGetClientRectInstalled());
    EXPECT_FALSE(controller.IsDetourScreenToClientInstalled());
    EXPECT_FALSE(controller.IsDetourSetCursorPosInstalled());
    EXPECT_FALSE(controller.IsDetourClipCursorInstalled());

    DestroyWindow(testWnd);
}

TEST(MirrorWindowControllerTest, StandaloneControllerSubclassesButInstallsNoWin32Detours) {
    HWND testWnd = CreateWindowA("STATIC", "TestStandaloneWnd", WS_OVERLAPPEDWINDOW, 10, 10, 800, 600, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    // No runtime owns this controller, so no hook could find its originals: it must not detour user32.
    MirrorWindowController controller([]() { return true; });
    EXPECT_TRUE(controller.AttachWindowSubclass(testWnd));
    EXPECT_TRUE(controller.IsWindowSubclassed());
    EXPECT_FALSE(controller.IsDetourGetClientRectInstalled());
    EXPECT_FALSE(controller.IsDetourScreenToClientInstalled());
    EXPECT_FALSE(controller.IsDetourSetCursorPosInstalled());
    EXPECT_FALSE(controller.IsDetourClipCursorInstalled());

    RECT rc{};
    EXPECT_TRUE(GetClientRect(testWnd, &rc));

    controller.DetachAll();
    DestroyWindow(testWnd);
}

TEST(MirrorWindowControllerTest, DecoupledResolutionCoordinateTransforms) {
    HWND testWnd = CreateWindowA("STATIC", "TestCoordWnd", WS_OVERLAPPEDWINDOW, 50, 50, 1000, 800, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    MirrorWindowController controller([]() { return true; });
    controller.SetTargetRenderResolution(3840, 2160);
    ASSERT_TRUE(controller.AttachWindowSubclass(testWnd));

    // Get physical dimensions
    controller.DetachWindowSubclass();
    RECT physRc{};
    GetClientRect(testWnd, &physRc);
    int physW = physRc.right - physRc.left;
    int physH = physRc.bottom - physRc.top;
    ASSERT_TRUE(controller.AttachWindowSubclass(testWnd));

    POINT clientOrigin{ 0, 0 };
    ClientToScreen(testWnd, &clientOrigin);

    // Virtual cursor at center (1920, 1080)
    int physScreenX = 0;
    int physScreenY = 0;
    EXPECT_TRUE(controller.ScaleVirtualCursorToPhysical(testWnd, clientOrigin.x + 1920, clientOrigin.y + 1080, physScreenX, physScreenY));
    EXPECT_NEAR(physScreenX, clientOrigin.x + physW / 2, 1);
    EXPECT_NEAR(physScreenY, clientOrigin.y + physH / 2, 1);

    // Virtual clip rect
    RECT virtClip{ clientOrigin.x, clientOrigin.y, clientOrigin.x + 3840, clientOrigin.y + 2160 };
    RECT physClip{};
    EXPECT_TRUE(controller.ConstrainVirtualClipRectToPhysical(testWnd, &virtClip, physClip));
    EXPECT_EQ(physClip.left, clientOrigin.x);
    EXPECT_EQ(physClip.top, clientOrigin.y);
    EXPECT_EQ(physClip.right, clientOrigin.x + physW);
    EXPECT_EQ(physClip.bottom, clientOrigin.y + physH);

    controller.DetachAll();
    DestroyWindow(testWnd);
}

} // namespace fs25vr
