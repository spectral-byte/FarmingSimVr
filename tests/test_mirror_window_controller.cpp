#include <gtest/gtest.h>
#include "proxy/desktop_cursor_port.h"
#include "proxy/mirror_window_controller.h"
#include "core/detour.h"
#include "core/injector_runtime.h"
#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

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
    controller.SetIsVrActiveCallback([&vrActive]() { return vrActive; });

    EXPECT_FALSE(controller.AttachWindowSubclass(testWnd));
    EXPECT_FALSE(controller.IsWindowSubclassed());

    vrActive = true;
    EXPECT_TRUE(controller.AttachWindowSubclass(testWnd));
    EXPECT_TRUE(controller.IsWindowSubclassed());
    EXPECT_EQ(controller.GetGameHwnd(), testWnd);
    EXPECT_TRUE(controller.IsDetourGetClientRectInstalled());
    EXPECT_TRUE(controller.IsDetourScreenToClientInstalled());
    EXPECT_TRUE(controller.IsDetourSetCursorPosInstalled());
    EXPECT_TRUE(controller.IsDetourClipCursorInstalled());

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

static int s_standInClipCalls = 0;
static bool s_standInClipWasRelease = false;
static RECT s_standInClip{};
static int s_standInMoveCalls = 0;
static POINT s_standInMove{};
static int s_detourClipCalls = 0;
static int s_detourMoveCalls = 0;
__declspec(noinline) static BOOL WINAPI StandInClipCursor(const RECT* rect) {
    ++s_standInClipCalls;
    s_standInClipWasRelease = (rect == nullptr);
    if (rect) s_standInClip = *rect;
    return TRUE;
}
__declspec(noinline) static BOOL WINAPI StandInSetCursorPos(int x, int y) {
    ++s_standInMoveCalls;
    s_standInMove = { x, y };
    return TRUE;
}
static BOOL WINAPI StandInClipCursorDetour(const RECT*) { ++s_detourClipCalls; return FALSE; }
static BOOL WINAPI StandInSetCursorPosDetour(int, int) { ++s_detourMoveCalls; return FALSE; }

TEST(MirrorWindowControllerTest, TheProductionPortSetsTheClipAndMovesTheCursorThroughTheDetoursOriginals) {
    FunctionDetour clipDetour;
    FunctionDetour moveDetour;
    ASSERT_TRUE(clipDetour.Install(reinterpret_cast<void*>(&StandInClipCursor), reinterpret_cast<void*>(&StandInClipCursorDetour)));
    ASSERT_TRUE(moveDetour.Install(reinterpret_cast<void*>(&StandInSetCursorPos), reinterpret_cast<void*>(&StandInSetCursorPosDetour)));
    s_standInClipCalls = s_standInMoveCalls = s_detourClipCalls = s_detourMoveCalls = 0;

    Win32DesktopCursorPort port(clipDetour, moveDetour);
    const RECT clip{ 10, 20, 810, 620 };
    EXPECT_TRUE(port.SetClip(&clip));
    EXPECT_EQ(s_standInClipCalls, 1);
    EXPECT_EQ(s_standInClip.left, 10);
    EXPECT_EQ(s_standInClip.top, 20);
    EXPECT_EQ(s_standInClip.right, 810);
    EXPECT_EQ(s_standInClip.bottom, 620);
    EXPECT_FALSE(s_standInClipWasRelease);

    EXPECT_TRUE(port.SetClip(nullptr));
    EXPECT_EQ(s_standInClipCalls, 2);
    EXPECT_TRUE(s_standInClipWasRelease);

    EXPECT_TRUE(port.SetCursorPos(300, 400));
    EXPECT_EQ(s_standInMoveCalls, 1);
    EXPECT_EQ(s_standInMove.x, 300);
    EXPECT_EQ(s_standInMove.y, 400);

    EXPECT_EQ(s_detourClipCalls, 0);
    EXPECT_EQ(s_detourMoveCalls, 0);

    moveDetour.Uninstall();
    clipDetour.Uninstall();
}

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
    t_getAncestorCalls = 0;
    EXPECT_TRUE(GetClientRect(otherWnd, &rc));
    EXPECT_EQ(t_getAncestorCalls, 0);

    controller.SetTargetRenderResolution(3840, 2160);
    vrActive = false;
    t_getAncestorCalls = 0;
    EXPECT_TRUE(GetClientRect(otherWnd, &rc));
    EXPECT_EQ(t_getAncestorCalls, 0);

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

    controller.DetachWindowSubclass();
    RECT physRc{};
    GetClientRect(testWnd, &physRc);
    int physW = physRc.right - physRc.left;
    int physH = physRc.bottom - physRc.top;
    ASSERT_TRUE(controller.AttachWindowSubclass(testWnd));

    POINT clientOrigin{ 0, 0 };
    ClientToScreen(testWnd, &clientOrigin);

    int physScreenX = 0;
    int physScreenY = 0;
    EXPECT_TRUE(controller.ScaleVirtualCursorToPhysical(testWnd, clientOrigin.x + 1920, clientOrigin.y + 1080, physScreenX, physScreenY));
    EXPECT_NEAR(physScreenX, clientOrigin.x + physW / 2, 1);
    EXPECT_NEAR(physScreenY, clientOrigin.y + physH / 2, 1);

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

TEST(MirrorWindowControllerTest, WmNcDestroyFollowedByDetachAllUninstallsAllUser32Detours) {
    HWND testWnd = CreateWindowA("STATIC", "NcDestroyDetourWnd", WS_OVERLAPPEDWINDOW, 10, 10, 800, 600, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    auto controllerPtr = std::make_shared<MirrorWindowController>();
    InjectorRuntime::Dependencies deps;
    deps.windowController = controllerPtr;
    InjectorRuntime runtime(std::move(deps));
    MirrorWindowController& controller = *controllerPtr;
    controller.SetIsVrActiveCallback([]() { return true; });

    ASSERT_TRUE(controller.AttachWindowSubclass(testWnd));
    ASSERT_TRUE(controller.IsWindowSubclassed());
    ASSERT_TRUE(controller.IsDetourGetClientRectInstalled());
    ASSERT_TRUE(controller.IsDetourScreenToClientInstalled());
    ASSERT_TRUE(controller.IsDetourSetCursorPosInstalled());
    ASSERT_TRUE(controller.IsDetourClipCursorInstalled());

    DestroyWindow(testWnd);
    EXPECT_FALSE(controller.IsWindowSubclassed());

    controller.DetachAll();
    EXPECT_FALSE(controller.IsDetourGetClientRectInstalled());
    EXPECT_FALSE(controller.IsDetourScreenToClientInstalled());
    EXPECT_FALSE(controller.IsDetourSetCursorPosInstalled());
    EXPECT_FALSE(controller.IsDetourClipCursorInstalled());
}

namespace {

struct RecordedSizeEvent {
    WPARAM wParam;
    UINT width;
    UINT height;
};

LRESULT CALLBACK RecordingSizeSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_SIZE && dwRefData) {
        auto* events = reinterpret_cast<std::vector<RecordedSizeEvent>*>(dwRefData);
        events->push_back({ wParam, LOWORD(lParam), HIWORD(lParam) });
    } else if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, &RecordingSizeSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

class ThreadSafeSpyCursorPort : public IDesktopCursorPort {
public:
    HWND foreground = nullptr;
    std::atomic<int> activeSetClip{0};
    std::atomic<int> maxActiveSetClip{0};

    HWND GetForegroundWindow() override { return foreground; }

    bool GetClip(RECT& out) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        out = m_clip ? *m_clip : RECT{ 0, 0, 1920, 1080 };
        return true;
    }

    bool SetClip(const RECT* rect) override {
        const int concurrent = activeSetClip.fetch_add(1) + 1;
        int prevMax = maxActiveSetClip.load();
        while (concurrent > prevMax && !maxActiveSetClip.compare_exchange_weak(prevMax, concurrent)) {}
        std::this_thread::yield();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (rect) {
                m_clip = *rect;
            } else {
                m_clip.reset();
            }
        }
        activeSetClip.fetch_sub(1);
        return true;
    }

    bool GetCursorPos(POINT& out) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        out = m_cursor;
        return true;
    }

    bool SetCursorPos(int x, int y) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cursor = { x, y };
        return true;
    }

    bool IsCursorShowing() override { return true; }

private:
    std::mutex m_mutex;
    std::optional<RECT> m_clip;
    POINT m_cursor{ 0, 0 };
};

}

TEST(MirrorWindowControllerTest, WmWindowPosChangedDoesNotSynthesizeSizeRestoredWhileMinimized) {
    HWND testWnd = CreateWindowA("STATIC", "MinimizeWnd", WS_OVERLAPPEDWINDOW, 10, 10, 800, 600, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    std::vector<RecordedSizeEvent> recordedSizes;
    ASSERT_TRUE(SetWindowSubclass(testWnd, &RecordingSizeSubclassProc, 0x1234, reinterpret_cast<DWORD_PTR>(&recordedSizes)));

    MirrorWindowController controller([]() { return true; });
    controller.SetTargetRenderResolution(3840, 2160);
    ASSERT_TRUE(controller.AttachWindowSubclass(testWnd));

    recordedSizes.clear();
    ShowWindow(testWnd, SW_MINIMIZE);
    ASSERT_TRUE(IsIconic(testWnd));

    WINDOWPOS wp{};
    wp.hwnd = testWnd;
    wp.cx = 160;
    wp.cy = 28;
    wp.flags = SWP_NOMOVE | SWP_NOZORDER;
    SendMessageW(testWnd, WM_WINDOWPOSCHANGED, 0, reinterpret_cast<LPARAM>(&wp));

    for (const auto& ev : recordedSizes) {
        EXPECT_NE(ev.wParam, static_cast<WPARAM>(SIZE_RESTORED));
    }

    recordedSizes.clear();
    ShowWindow(testWnd, SW_RESTORE);
    ASSERT_FALSE(IsIconic(testWnd));
    bool sawRestoredTarget = false;
    for (const auto& ev : recordedSizes) {
        if (ev.wParam == SIZE_RESTORED && ev.width == 3840 && ev.height == 2160) {
            sawRestoredTarget = true;
        }
    }
    EXPECT_TRUE(sawRestoredTarget);

    controller.DetachAll();
    DestroyWindow(testWnd);
}

TEST(MirrorWindowControllerTest, HandleGameClipCursorAndUpdateMouseConfinementSynchronizeState) {
    HWND testWnd = CreateWindowA("STATIC", "ClipRaceWnd", WS_OVERLAPPEDWINDOW, 10, 10, 800, 600, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    auto port = std::make_shared<ThreadSafeSpyCursorPort>();
    port->foreground = testWnd;
    MirrorWindowController controller([]() { return true; }, port);
    controller.SetTargetRenderResolution(3840, 2160);
    controller.SetGameHwnd(testWnd);

    std::atomic<bool> start{false};
    std::thread updater([&]() {
        while (!start.load()) {}
        for (int i = 0; i < 400; ++i) {
            controller.UpdateMouseConfinement(
                (i % 2 == 0) ? MirrorWindowController::MouseConfinementState::Holding
                             : MirrorWindowController::MouseConfinementState::FocusLost);
        }
    });
    std::thread clipper([&]() {
        while (!start.load()) {}
        const RECT gameRect{ 0, 0, 1920, 1080 };
        for (int i = 0; i < 400; ++i) {
            controller.HandleGameClipCursor((i % 2 == 0) ? &gameRect : nullptr);
        }
    });

    start.store(true);
    updater.join();
    clipper.join();

    EXPECT_LE(port->maxActiveSetClip.load(), 1);

    controller.DetachAll();
    DestroyWindow(testWnd);
}

}
