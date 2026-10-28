#pragma once

#include "core/detour.h"
#include <windows.h>
#include <commctrl.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct IDXGISwapChain;

namespace fs25vr {

class OpenXrManager;

inline constexpr UINT_PTR kMirrorWindowSubclassId = 0x56524653; // 'FSVR'

// The game window's title while the Desktop Mirror Window is off: says so and names the command that restores it.
inline constexpr const wchar_t* kDesktopMirrorOffTitle =
    L"FS25 VR - desktop mirror is off. Type vrDesktopMirror on in the console to restore it.";

class MirrorWindowController {
public:
    explicit MirrorWindowController(std::function<bool()> isVrActive = nullptr);
    ~MirrorWindowController();

    // Active VR state predicate
    bool IsActive() const;
    void SetIsVrActiveCallback(std::function<bool()> callback) { m_isVrActive = std::move(callback); }

    // Win32 Game HWND Lifecycle
    HWND GetGameHwnd() const { return m_gameHwnd; }
    void SetGameHwnd(HWND hWnd);

    // Subclassing
    bool AttachWindowSubclass(HWND hWnd);
    bool DetachWindowSubclass();
    bool IsWindowSubclassed() const { return m_windowSubclassed; }

    bool IsInSizeMove() const { return m_inSizeMove; }

    bool IsWindowClampDirty() const { return m_windowClampDirty; }
    void SetWindowClampDirty(bool dirty) { m_windowClampDirty = dirty; }

    // Target render resolution decoupling
    uint32_t GetTargetRenderWidth() const { return m_targetRenderWidth; }
    uint32_t GetTargetRenderHeight() const { return m_targetRenderHeight; }
    void SetTargetRenderResolution(uint32_t width, uint32_t height) {
        m_targetRenderWidth = width;
        m_targetRenderHeight = height;
    }
    void GetDecoupledTargetResolution(uint32_t& outWidth, uint32_t& outHeight, const OpenXrManager* openXrManager = nullptr) const;

    // Monitor work area clamping calculations
    static RECT ComputeClampedMirrorWindowRect(const RECT& workArea, DWORD dwStyle, DWORD dwExStyle, bool hasMenu = false);
    static RECT ComputeClampedMirrorWindowRect(HWND hWnd);
    bool ApplyInitialWindowClamp(HWND hWnd);
    void EnforceClampedMirrorWindowDimensions(HWND hWnd);

    // Coordinate transformations
    static bool ScreenToNormalizedClient(HWND hWnd, POINT screenPt, float& outNormX, float& outNormY);
    bool ScaleVirtualCursorToPhysical(HWND hWnd, int screenX, int screenY, int& outPhysScreenX, int& outPhysScreenY) const;
    bool ConstrainVirtualClipRectToPhysical(HWND hWnd, const RECT* lpRect, RECT& outPhysClipRect) const;

    // Menu cursor state update
    void UpdateMenuCursorState(IDXGISwapChain* swapChain, OpenXrManager* openXrManager);

    // Win32 Detours management. Detours are installed only while a live InjectorRuntime owns the
    // controller, so the hooks can always find the controller that holds their originals.
    void SetOwnedByRuntime(bool owned) { m_ownedByRuntime = owned; }
    bool InstallDetours();
    void UninstallDetours();
    bool IsDetourGetClientRectInstalled() const { return m_detourGetClientRect.IsInstalled(); }
    bool IsDetourScreenToClientInstalled() const { return m_detourScreenToClient.IsInstalled(); }
    bool IsDetourSetCursorPosInstalled() const { return m_detourSetCursorPos.IsInstalled(); }
    bool IsDetourClipCursorInstalled() const { return m_detourClipCursor.IsInstalled(); }

    // Detour interceptors. The Win32 hooks act for the controller that installed them; the window
    // subclass procedure acts for the controller recorded in its reference data.
    static LRESULT CALLBACK MirrorWindowSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static BOOL WINAPI Intercept_GetClientRect(HWND hWnd, LPRECT lpRect);
    static BOOL WINAPI Intercept_ScreenToClient(HWND hWnd, LPPOINT lpPoint);
    static BOOL WINAPI Intercept_SetCursorPos(int X, int Y);
    static BOOL WINAPI Intercept_ClipCursor(const RECT* lpRect);

    // Game window title while the Desktop Mirror Window is off: keeps the window's current title and sets
    // the off note; RestoreTitle puts the kept title back. Both are no-ops when there is nothing to do, and
    // the controller restores the title itself in DetachAll.
    void ShowDesktopMirrorOffTitle();
    void RestoreTitle();
    bool IsDesktopMirrorOffTitleShown() const { return m_titleSwapped; }

    void DetachAll();

private:
    bool GetPhysicalWindowMetrics(HWND hWnd, RECT& outPhysRect, POINT& outClientOrigin) const;

    // The controller a Win32 hook acts for: the one in a live runtime that installed `detour`, else
    // the current runtime's; null when no runtime is alive.
    static MirrorWindowController* ForHook(FunctionDetour MirrorWindowController::*detour);

    HWND m_gameHwnd = nullptr;
    std::function<bool()> m_isVrActive;

    bool m_ownedByRuntime = false;
    bool m_windowSubclassed = false;
    bool m_inSizeMove = false;
    bool m_windowClampDirty = true;
    int m_lastEnforcedWidth = 0;
    int m_lastEnforcedHeight = 0;
    HMONITOR m_lastMonitor = nullptr;

    // The title the game window had before ShowDesktopMirrorOffTitle replaced it, on m_titleHwnd.
    std::wstring m_keptTitle;
    HWND m_titleHwnd = nullptr;
    bool m_titleSwapped = false;

    uint32_t m_targetRenderWidth = 0;
    uint32_t m_targetRenderHeight = 0;

    FunctionDetour m_detourGetClientRect;
    FunctionDetour m_detourScreenToClient;
    FunctionDetour m_detourSetCursorPos;
    FunctionDetour m_detourClipCursor;
};

} // namespace fs25vr
