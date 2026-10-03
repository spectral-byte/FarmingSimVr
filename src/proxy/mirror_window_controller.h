#pragma once

#include "core/detour.h"
#include <windows.h>
#include <commctrl.h>
#include <cstdint>
#include <functional>
#include <memory>

struct IDXGISwapChain;

namespace fs25vr {

class OpenXrManager;

inline constexpr UINT_PTR kMirrorWindowSubclassId = 0x56524653; // 'FSVR'

class MirrorWindowController {
public:
    static MirrorWindowController& Instance();

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
    void SetInSizeMove(bool inSizeMove) { m_inSizeMove = inSizeMove; }

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

    // Win32 Detours management
    bool InstallDetours();
    void UninstallDetours();
    bool IsDetourGetClientRectInstalled() const { return m_detourGetClientRect.IsInstalled(); }
    bool IsDetourScreenToClientInstalled() const { return m_detourScreenToClient.IsInstalled(); }
    bool IsDetourSetCursorPosInstalled() const { return m_detourSetCursorPos.IsInstalled(); }
    bool IsDetourClipCursorInstalled() const { return m_detourClipCursor.IsInstalled(); }

    // Detour Interceptors
    static LRESULT CALLBACK MirrorWindowSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static BOOL WINAPI Intercept_GetClientRect(HWND hWnd, LPRECT lpRect);
    static BOOL WINAPI Intercept_ScreenToClient(HWND hWnd, LPPOINT lpPoint);
    static BOOL WINAPI Intercept_SetCursorPos(int X, int Y);
    static BOOL WINAPI Intercept_ClipCursor(const RECT* lpRect);

    static MirrorWindowController* GetActiveController();
    static void SetActiveController(MirrorWindowController* controller);

    void DetachAll();

private:
    bool GetPhysicalWindowMetrics(HWND hWnd, RECT& outPhysRect, POINT& outClientOrigin) const;

    HWND m_gameHwnd = nullptr;
    std::function<bool()> m_isVrActive;

    bool m_windowSubclassed = false;
    bool m_inSizeMove = false;
    bool m_windowClampDirty = true;
    int m_lastEnforcedWidth = 0;
    int m_lastEnforcedHeight = 0;
    HMONITOR m_lastMonitor = nullptr;

    uint32_t m_targetRenderWidth = 0;
    uint32_t m_targetRenderHeight = 0;

    FunctionDetour m_detourGetClientRect;
    FunctionDetour m_detourScreenToClient;
    FunctionDetour m_detourSetCursorPos;
    FunctionDetour m_detourClipCursor;
};

} // namespace fs25vr
