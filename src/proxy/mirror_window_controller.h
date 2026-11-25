#pragma once

#include "core/detour.h"
#include "proxy/desktop_cursor_port.h"
#include <windows.h>
#include <commctrl.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

struct IDXGISwapChain;

namespace fs25vr {

class OpenXrManager;

inline constexpr UINT_PTR kMirrorWindowSubclassId = 0x56524653;

inline constexpr const wchar_t* kDesktopMirrorOffTitle =
    L"FS25 VR - desktop mirror is off. Type vrDesktopMirror on in the console to restore it.";

class MirrorWindowController {
public:
    explicit MirrorWindowController(std::function<bool()> isVrActive = nullptr,
                                    std::shared_ptr<IDesktopCursorPort> cursorPort = nullptr);
    ~MirrorWindowController();

    IDesktopCursorPort& GetCursorPort() { return *m_cursorPort; }

    bool IsActive() const;
    void SetIsVrActiveCallback(std::function<bool()> callback) { m_isVrActive = std::move(callback); }

    HWND GetGameHwnd() const { return m_gameHwnd; }
    void SetGameHwnd(HWND hWnd);

    bool AttachWindowSubclass(HWND hWnd);
    bool DetachWindowSubclass();
    bool IsWindowSubclassed() const { return m_windowSubclassed; }

    bool IsInSizeMove() const { return m_inSizeMove; }

    bool IsWindowClampDirty() const { return m_windowClampDirty; }
    void SetWindowClampDirty(bool dirty) { m_windowClampDirty = dirty; }

    uint32_t GetTargetRenderWidth() const { return m_targetRenderWidth; }
    uint32_t GetTargetRenderHeight() const { return m_targetRenderHeight; }
    void SetTargetRenderResolution(uint32_t width, uint32_t height) {
        m_targetRenderWidth = width;
        m_targetRenderHeight = height;
    }
    void GetDecoupledTargetResolution(uint32_t& outWidth, uint32_t& outHeight, const OpenXrManager* openXrManager = nullptr) const;

    static RECT ComputeClampedMirrorWindowRect(const RECT& workArea, DWORD dwStyle, DWORD dwExStyle, bool hasMenu = false);
    static RECT ComputeClampedMirrorWindowRect(HWND hWnd);
    bool ApplyInitialWindowClamp(HWND hWnd);
    void EnforceClampedMirrorWindowDimensions(HWND hWnd);

    static bool ScreenToNormalizedClient(HWND hWnd, POINT screenPt, float& outNormX, float& outNormY);
    bool ScaleVirtualCursorToPhysical(HWND hWnd, int screenX, int screenY, int& outPhysScreenX, int& outPhysScreenY) const;
    bool ConstrainVirtualClipRectToPhysical(HWND hWnd, const RECT* lpRect, RECT& outPhysClipRect) const;

    bool IsGameWindowForeground() const;

    void UpdateMenuCursorState(IDXGISwapChain* swapChain, OpenXrManager* openXrManager);

    enum class MouseConfinementState {
        Holding,
        PassThroughMode,
        SessionNotRunning,
        TurnedOffByCommand,
        FocusLost,
    };
    void UpdateMouseConfinement(MouseConfinementState state);

    bool HandleGameClipCursor(const RECT* lpRect);

    void SetOwnedByRuntime(bool owned) { m_ownedByRuntime = owned; }
    bool InstallDetours();
    void UninstallDetours();
    bool IsDetourGetClientRectInstalled() const { return m_detourGetClientRect.IsInstalled(); }
    bool IsDetourScreenToClientInstalled() const { return m_detourScreenToClient.IsInstalled(); }
    bool IsDetourSetCursorPosInstalled() const { return m_detourSetCursorPos.IsInstalled(); }
    bool IsDetourClipCursorInstalled() const { return m_detourClipCursor.IsInstalled(); }

    static LRESULT CALLBACK MirrorWindowSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static BOOL WINAPI Intercept_GetClientRect(HWND hWnd, LPRECT lpRect);
    static BOOL WINAPI Intercept_ScreenToClient(HWND hWnd, LPPOINT lpPoint);
    static BOOL WINAPI Intercept_SetCursorPos(int X, int Y);
    static BOOL WINAPI Intercept_ClipCursor(const RECT* lpRect);

    void ShowDesktopMirrorOffTitle();
    void RestoreTitle();
    bool IsDesktopMirrorOffTitleShown() const { return m_titleSwapped; }

    void DetachAll();

private:
    bool GetPhysicalWindowMetrics(HWND hWnd, RECT& outPhysRect, POINT& outClientOrigin) const;

    bool GetGamePicture(RECT& outPicture) const;
    void StopHoldingMouse(const char* reason);
    std::optional<RECT> GameClipOnScreen() const;
    bool SetPictureClip(const RECT& picture);

    static MirrorWindowController* ForHook(FunctionDetour MirrorWindowController::*detour);

    HWND m_gameHwnd = nullptr;
    std::function<bool()> m_isVrActive;
    std::shared_ptr<IDesktopCursorPort> m_cursorPort;

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

    std::optional<RECT> m_gameClip;
    std::optional<POINT> m_gameClipOrigin;
    bool m_holdingMouse = false;
    bool m_hasHeldMouse = false;  // it has held at least once since the controller was attached
    std::optional<RECT> m_setMousePicture;
    std::optional<MouseConfinementState> m_lastMouseRelease;

    FunctionDetour m_detourGetClientRect;
    FunctionDetour m_detourScreenToClient;
    FunctionDetour m_detourSetCursorPos;
    FunctionDetour m_detourClipCursor;
};

}
