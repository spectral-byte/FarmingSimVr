#include <gtest/gtest.h>
#include "proxy/mirror_window_controller.h"
#include "core/detour.h"
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

// Counts GetAncestor calls on this thread: the GetClientRect hook's walk to the root window.
static thread_local int t_getAncestorCalls = 0;
static FunctionDetour* s_getAncestorDetour = nullptr;
static HWND WINAPI CountingGetAncestor(HWND hWnd, UINT flags) {
    ++t_getAncestorCalls;
    auto orig = reinterpret_cast<HWND(WINAPI*)(HWND, UINT)>(s_getAncestorDetour->GetOriginal());
    return orig(hWnd, flags);
}

TEST(MirrorWindowControllerTest, GetClientRectHookChecksCheapConditionsBeforeTheRootWindowWalk) {
    HWND gameWnd = CreateWindowA("STATIC", "TestGameWnd", WS_OVERLAPPEDWINDOW, 10, 10, 800, 600, nullptr, nullptr, nullptr, nullptr);
    HWND otherWnd = CreateWindowA("STATIC", "TestOtherWnd", WS_OVERLAPPEDWINDOW, 20, 20, 400, 300, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(gameWnd != nullptr && otherWnd != nullptr);

    bool vrActive = true;
    auto controllerPtr = std::make_shared<MirrorWindowController>();
    InjectorRuntime::Dependencies deps;
    deps.windowController = controllerPtr;
    InjectorRuntime runtime(std::move(deps));
    MirrorWindowController& controller = *controllerPtr;
    controller.SetIsVrActiveCallback([&vrActive]() { return vrActive; });
    ASSERT_TRUE(controller.AttachWindowSubclass(gameWnd));
    ASSERT_TRUE(controller.IsDetourGetClientRectInstalled());

    FunctionDetour getAncestorDetour;
    s_getAncestorDetour = &getAncestorDetour;
    void* pfnGetAncestor = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("user32.dll"), "GetAncestor"));
    ASSERT_TRUE(getAncestorDetour.Install(pfnGetAncestor, reinterpret_cast<void*>(&CountingGetAncestor)));

    RECT rc{};
    // VR active but no target render resolution yet: no walk.
    t_getAncestorCalls = 0;
    EXPECT_TRUE(GetClientRect(otherWnd, &rc));
    EXPECT_EQ(t_getAncestorCalls, 0);

    // Target resolution set but VR inactive: no walk.
    controller.SetTargetRenderResolution(3840, 2160);
    vrActive = false;
    t_getAncestorCalls = 0;
    EXPECT_TRUE(GetClientRect(otherWnd, &rc));
    EXPECT_EQ(t_getAncestorCalls, 0);

    // Both set: a window other than the game's is walked to its root, and keeps its real size.
    vrActive = true;
    t_getAncestorCalls = 0;
    EXPECT_TRUE(GetClientRect(otherWnd, &rc));
    EXPECT_GT(t_getAncestorCalls, 0);
    EXPECT_NE(rc.right, 3840);

    getAncestorDetour.Uninstall();
    s_getAncestorDetour = nullptr;
    controller.DetachAll();
    DestroyWindow(otherWnd);
    DestroyWindow(gameWnd);
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
