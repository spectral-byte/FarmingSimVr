#include "mirror_window_controller.h"
#include "openxr/openxr_manager.h"
#include "core/logger.h"
#include "core/dll_lifecycle.h"
#include "core/injector_runtime.h"
#include <dxgi.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "comctl32.lib")

namespace fs25vr {

namespace {

thread_local bool t_isInternalWindowQuery = false;

struct InternalWindowQueryScope {
    InternalWindowQueryScope() { t_isInternalWindowQuery = true; }
    ~InternalWindowQueryScope() { t_isInternalWindowQuery = false; }
};

static HWND GetRootWindow(HWND hWnd) {
    if (!hWnd) return nullptr;
    HWND hAncestor = GetAncestor(hWnd, GA_ROOT);
    return hAncestor ? hAncestor : hWnd;
}

} // anonymous namespace

MirrorWindowController* MirrorWindowController::ForHook(FunctionDetour MirrorWindowController::*detour) {
    InjectorRuntime* runtime = InjectorRuntime::ForHook([detour](InjectorRuntime& r) {
        return (r.GetWindowController().*detour).IsInstalled();
    });
    return runtime ? &runtime->GetWindowController() : nullptr;
}

MirrorWindowController::MirrorWindowController(std::function<bool()> isVrActive,
                                               std::shared_ptr<IDesktopCursorPort> cursorPort)
    : m_isVrActive(std::move(isVrActive)), m_cursorPort(std::move(cursorPort)) {
    if (!m_cursorPort) {
        m_cursorPort = std::make_shared<Win32DesktopCursorPort>(m_detourClipCursor, m_detourSetCursorPos);
    }
}

MirrorWindowController::~MirrorWindowController() {
    if (IsProcessTerminating()) return;
    DetachAll();
}

bool MirrorWindowController::IsActive() const {
    if (m_isVrActive) {
        return m_isVrActive();
    }
    return false;
}

void MirrorWindowController::SetGameHwnd(HWND hWnd) {
    hWnd = GetRootWindow(hWnd);
    if (!hWnd) return;

    if (m_gameHwnd != hWnd) {
        RestoreTitle();
        if (m_gameHwnd && m_windowSubclassed) {
            DetachWindowSubclass();
        }
        m_gameHwnd = hWnd;
    }
    if (m_gameHwnd && !m_windowSubclassed && IsActive()) {
        AttachWindowSubclass(m_gameHwnd);
    }
}

bool MirrorWindowController::AttachWindowSubclass(HWND hWnd) {
    if (!IsActive()) {
        return false;
    }
    hWnd = GetRootWindow(hWnd);
    if (!hWnd) {
        return false;
    }

    if (m_windowSubclassed && m_gameHwnd == hWnd) {
        return true;
    }
    if (m_windowSubclassed && m_gameHwnd != hWnd) {
        DetachWindowSubclass();
    }
    m_gameHwnd = hWnd;
    BOOL res = SetWindowSubclass(hWnd, &MirrorWindowController::MirrorWindowSubclassProc, kMirrorWindowSubclassId, reinterpret_cast<DWORD_PTR>(this));
    m_windowSubclassed = (res != FALSE);
    if (m_windowSubclassed) {
        LogInfo("MirrorWindowController: Subclassed Desktop Mirror Window HWND=" + std::to_string(reinterpret_cast<uintptr_t>(hWnd)) + " for resolution decoupling.");
        InstallDetours();
    } else {
        LogError("MirrorWindowController: Failed to subclass Desktop Mirror Window HWND=" + std::to_string(reinterpret_cast<uintptr_t>(hWnd)));
    }
    return m_windowSubclassed;
}

bool MirrorWindowController::DetachWindowSubclass() {
    if (!m_windowSubclassed) {
        return false;
    }
    bool removed = false;
    if (m_gameHwnd && IsWindow(m_gameHwnd)) {
        removed = (RemoveWindowSubclass(m_gameHwnd, &MirrorWindowController::MirrorWindowSubclassProc, kMirrorWindowSubclassId) != FALSE);
        if (removed) {
            LogInfo("MirrorWindowController: Removed Desktop Mirror Window subclass from HWND=" + std::to_string(reinterpret_cast<uintptr_t>(m_gameHwnd)));
        }
    }
    UninstallDetours();
    m_windowSubclassed = false;
    m_inSizeMove = false;
    m_lastEnforcedWidth = 0;
    m_lastEnforcedHeight = 0;
    m_lastMonitor = nullptr;
    return removed;
}

bool MirrorWindowController::InstallDetours() {
    if (!m_ownedByRuntime) {
        LogWarn("MirrorWindowController: Not owned by a live InjectorRuntime; Win32 detours not installed.");
        return false;
    }
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) {
        return false;
    }

    if (!m_detourGetClientRect.IsInstalled()) {
        void* pfnGetClientRect = reinterpret_cast<void*>(GetProcAddress(hUser32, "GetClientRect"));
        if (pfnGetClientRect) {
            if (m_detourGetClientRect.Install(pfnGetClientRect, reinterpret_cast<void*>(&MirrorWindowController::Intercept_GetClientRect))) {
                LogInfo("MirrorWindowController: Installed GetClientRect detour.");
            } else {
                LogWarn("MirrorWindowController: Failed to install GetClientRect detour.");
            }
        }
    }
    if (!m_detourScreenToClient.IsInstalled()) {
        void* pfnScreenToClient = reinterpret_cast<void*>(GetProcAddress(hUser32, "ScreenToClient"));
        if (pfnScreenToClient) {
            if (m_detourScreenToClient.Install(pfnScreenToClient, reinterpret_cast<void*>(&MirrorWindowController::Intercept_ScreenToClient))) {
                LogInfo("MirrorWindowController: Installed ScreenToClient detour.");
            } else {
                LogWarn("MirrorWindowController: Failed to install ScreenToClient detour.");
            }
        }
    }
    if (!m_detourSetCursorPos.IsInstalled()) {
        void* pfnSetCursorPos = reinterpret_cast<void*>(GetProcAddress(hUser32, "SetCursorPos"));
        if (pfnSetCursorPos) {
            if (m_detourSetCursorPos.Install(pfnSetCursorPos, reinterpret_cast<void*>(&MirrorWindowController::Intercept_SetCursorPos))) {
                LogInfo("MirrorWindowController: Installed SetCursorPos detour.");
            } else {
                LogWarn("MirrorWindowController: Failed to install SetCursorPos detour.");
            }
        }
    }
    if (!m_detourClipCursor.IsInstalled()) {
        void* pfnClipCursor = reinterpret_cast<void*>(GetProcAddress(hUser32, "ClipCursor"));
        if (pfnClipCursor) {
            if (m_detourClipCursor.Install(pfnClipCursor, reinterpret_cast<void*>(&MirrorWindowController::Intercept_ClipCursor))) {
                LogInfo("MirrorWindowController: Installed ClipCursor detour.");
            } else {
                LogWarn("MirrorWindowController: Failed to install ClipCursor detour.");
            }
        }
    }
    return true;
}

void MirrorWindowController::UninstallDetours() {
    if (m_detourGetClientRect.IsInstalled()) {
        m_detourGetClientRect.Uninstall();
        LogInfo("MirrorWindowController: Uninstalled GetClientRect detour.");
    }
    if (m_detourScreenToClient.IsInstalled()) {
        m_detourScreenToClient.Uninstall();
        LogInfo("MirrorWindowController: Uninstalled ScreenToClient detour.");
    }
    if (m_detourSetCursorPos.IsInstalled()) {
        m_detourSetCursorPos.Uninstall();
        LogInfo("MirrorWindowController: Uninstalled SetCursorPos detour.");
    }
    if (m_detourClipCursor.IsInstalled()) {
        m_detourClipCursor.Uninstall();
        LogInfo("MirrorWindowController: Uninstalled ClipCursor detour.");
    }
}

void MirrorWindowController::ShowDesktopMirrorOffTitle() {
    if (m_titleSwapped || !m_gameHwnd || !IsWindow(m_gameHwnd)) return;
    const int length = GetWindowTextLengthW(m_gameHwnd);
    std::wstring title(static_cast<size_t>(length > 0 ? length : 0) + 1, L'\0');
    const int copied = GetWindowTextW(m_gameHwnd, title.data(), static_cast<int>(title.size()));
    title.resize(static_cast<size_t>(copied > 0 ? copied : 0));
    m_keptTitle = std::move(title);
    m_titleHwnd = m_gameHwnd;
    m_titleSwapped = SetWindowTextW(m_gameHwnd, kDesktopMirrorOffTitle) != FALSE;
}

void MirrorWindowController::RestoreTitle() {
    if (!m_titleSwapped) return;
    if (IsWindow(m_titleHwnd)) {
        SetWindowTextW(m_titleHwnd, m_keptTitle.c_str());
    }
    m_keptTitle.clear();
    m_titleHwnd = nullptr;
    m_titleSwapped = false;
}

void MirrorWindowController::DetachAll() {
    StopHoldingMouse("controller detached");
    m_gameClip.reset();
    m_lastMouseRelease.reset();
    RestoreTitle();
    DetachWindowSubclass();
    m_inSizeMove = false;
    m_windowClampDirty = true;
    m_lastEnforcedWidth = 0;
    m_lastEnforcedHeight = 0;
    m_lastMonitor = nullptr;
    m_targetRenderWidth = 0;
    m_targetRenderHeight = 0;
}

void MirrorWindowController::GetDecoupledTargetResolution(uint32_t& outWidth, uint32_t& outHeight, const OpenXrManager* openXrManager) const {
    if (m_targetRenderWidth > 0 && m_targetRenderHeight > 0) {
        outWidth = m_targetRenderWidth;
        outHeight = m_targetRenderHeight;
        return;
    }
    if (openXrManager && openXrManager->IsActive()) {
        if (openXrManager->HasEyeSwapchains()) {
            const auto& sc = openXrManager->GetEyeSwapchain(0);
            if (sc.width > 0 && sc.height > 0) {
                outWidth = sc.width;
                outHeight = sc.height;
                return;
            }
        }
        uint32_t recW = openXrManager->GetRecommendedEyeWidth(0);
        uint32_t recH = openXrManager->GetRecommendedEyeHeight(0);
        if (recW > 0 && recH > 0) {
            outWidth = recW;
            outHeight = recH;
            return;
        }
    }
    outWidth = 0;
    outHeight = 0;
}

RECT MirrorWindowController::ComputeClampedMirrorWindowRect(const RECT& workArea, DWORD dwStyle, DWORD dwExStyle, bool hasMenu) {
    int workWidth = workArea.right - workArea.left;
    int workHeight = workArea.bottom - workArea.top;
    if (workWidth <= 0 || workHeight <= 0) {
        return workArea;
    }

    RECT frameRect{ 0, 0, 0, 0 };
    AdjustWindowRectEx(&frameRect, dwStyle, hasMenu ? TRUE : FALSE, dwExStyle);
    int frameWidth = frameRect.right - frameRect.left;
    int frameHeight = frameRect.bottom - frameRect.top;
    if (frameWidth < 0) frameWidth = 0;
    if (frameHeight < 0) frameHeight = 0;

    int maxClientWidth = workWidth - frameWidth;
    int maxClientHeight = workHeight - frameHeight;
    if (maxClientWidth <= 0 || maxClientHeight <= 0) {
        return workArea;
    }

    constexpr int64_t kAspectWidth = 16;
    constexpr int64_t kAspectHeight = 9;

    int clientWidth = 0;
    int clientHeight = 0;
    if (static_cast<int64_t>(maxClientWidth) * kAspectHeight < static_cast<int64_t>(maxClientHeight) * kAspectWidth) {
        clientWidth = maxClientWidth;
        clientHeight = static_cast<int>((static_cast<int64_t>(clientWidth) * kAspectHeight) / kAspectWidth);
    } else {
        clientHeight = maxClientHeight;
        clientWidth = static_cast<int>((static_cast<int64_t>(clientHeight) * kAspectWidth) / kAspectHeight);
    }

    int outerWidth = clientWidth + frameWidth;
    int outerHeight = clientHeight + frameHeight;

    int outerX = workArea.left + (workWidth - outerWidth) / 2;
    int outerY = workArea.top + (workHeight - outerHeight) / 2;

    return RECT{ outerX, outerY, outerX + outerWidth, outerY + outerHeight };
}

RECT MirrorWindowController::ComputeClampedMirrorWindowRect(HWND hWnd) {
    hWnd = GetRootWindow(hWnd);
    if (!hWnd) {
        return RECT{ 0, 0, 0, 0 };
    }

    HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (!hMon) {
        return RECT{ 0, 0, 0, 0 };
    }
    MONITORINFO mi{ sizeof(MONITORINFO) };
    if (!GetMonitorInfo(hMon, &mi)) {
        return RECT{ 0, 0, 0, 0 };
    }
    DWORD dwStyle = static_cast<DWORD>(GetWindowLongPtr(hWnd, GWL_STYLE)) & ~(WS_MAXIMIZE | WS_MINIMIZE);
    DWORD dwExStyle = static_cast<DWORD>(GetWindowLongPtr(hWnd, GWL_EXSTYLE));
    bool hasMenu = (GetMenu(hWnd) != nullptr);
    return ComputeClampedMirrorWindowRect(mi.rcWork, dwStyle, dwExStyle, hasMenu);
}

bool MirrorWindowController::ApplyInitialWindowClamp(HWND hWnd) {
    if (!IsActive()) {
        return false;
    }
    hWnd = GetRootWindow(hWnd);
    if (!hWnd) {
        return false;
    }

    if (IsZoomed(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
    }

    RECT curRect{};
    if (!GetWindowRect(hWnd, &curRect)) return false;
    int curW = curRect.right - curRect.left;
    int curH = curRect.bottom - curRect.top;

    RECT clamped = ComputeClampedMirrorWindowRect(hWnd);
    int maxW = clamped.right - clamped.left;
    int maxH = clamped.bottom - clamped.top;
    if (maxW > 0 && maxH > 0 && (curW > maxW || curH > maxH)) {
        BOOL res = SetWindowPos(hWnd, nullptr, clamped.left, clamped.top, maxW, maxH,
                                SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (res) {
            m_lastEnforcedWidth = maxW;
            m_lastEnforcedHeight = maxH;
            m_lastMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
            LogInfo("MirrorWindowController: Applied initial clamped dimensions [" +
                    std::to_string(maxW) + "x" + std::to_string(maxH) + " at (" +
                    std::to_string(clamped.left) + "," + std::to_string(clamped.top) + ")]");
            return true;
        }
    } else {
        m_lastEnforcedWidth = curW;
        m_lastEnforcedHeight = curH;
        m_lastMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        return true;
    }
    return false;
}

void MirrorWindowController::EnforceClampedMirrorWindowDimensions(HWND hWnd) {
    if (!IsActive() || m_inSizeMove) return;
    m_windowClampDirty = false;

    hWnd = GetRootWindow(hWnd);
    if (!hWnd) return;

    if (IsZoomed(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
    }

    RECT curRect{};
    if (!GetWindowRect(hWnd, &curRect)) return;
    int curW = curRect.right - curRect.left;
    int curH = curRect.bottom - curRect.top;

    HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

    if (hMon == m_lastMonitor && curW == m_lastEnforcedWidth && curH == m_lastEnforcedHeight) {
        return;
    }

    RECT clamped = ComputeClampedMirrorWindowRect(hWnd);
    int maxW = clamped.right - clamped.left;
    int maxH = clamped.bottom - clamped.top;

    if (maxW > 0 && maxH > 0 && (curW > maxW || curH > maxH)) {
        BOOL res = SetWindowPos(hWnd, nullptr, 0, 0, maxW, maxH,
                                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (res) {
            m_lastEnforcedWidth = maxW;
            m_lastEnforcedHeight = maxH;
            m_lastMonitor = hMon;
            LogInfo("MirrorWindowController: Enforced clamped Desktop Mirror Window dimensions [" +
                    std::to_string(maxW) + "x" + std::to_string(maxH) + "]");
        }
    } else {
        m_lastEnforcedWidth = curW;
        m_lastEnforcedHeight = curH;
        m_lastMonitor = hMon;
    }
}

bool MirrorWindowController::ScreenToNormalizedClient(HWND hWnd, POINT screenPt, float& outNormX, float& outNormY) {
    if (!hWnd) return false;
    POINT pt = screenPt;
    InternalWindowQueryScope scope;
    if (!ScreenToClient(hWnd, &pt)) return false;
    RECT clientRect{};
    if (!GetClientRect(hWnd, &clientRect)) return false;
    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;
    if (clientWidth <= 0 || clientHeight <= 0) return false;
    if (pt.x < 0 || pt.x > clientWidth || pt.y < 0 || pt.y > clientHeight) return false;
    outNormX = static_cast<float>(pt.x) / static_cast<float>(clientWidth);
    outNormY = static_cast<float>(pt.y) / static_cast<float>(clientHeight);
    return true;
}

bool MirrorWindowController::GetPhysicalWindowMetrics(HWND hWnd, RECT& outPhysRect, POINT& outClientOrigin) const {
    if (!hWnd) return false;
    InternalWindowQueryScope scope;
    auto origGCR = reinterpret_cast<BOOL(WINAPI*)(HWND, LPRECT)>(
        m_detourGetClientRect.GetOriginal()
    );
    BOOL gotRect = origGCR ? origGCR(hWnd, &outPhysRect) : GetClientRect(hWnd, &outPhysRect);
    if (!gotRect) return false;
    outClientOrigin = { 0, 0 };
    ClientToScreen(hWnd, &outClientOrigin);
    return true;
}

bool MirrorWindowController::ScaleVirtualCursorToPhysical(
    HWND hWnd, int screenX, int screenY, int& outPhysScreenX, int& outPhysScreenY) const {
    if (!IsActive() || !hWnd) return false;
    if (m_targetRenderWidth == 0 || m_targetRenderHeight == 0) return false;

    RECT physRect{};
    POINT clientOrigin{ 0, 0 };
    if (!GetPhysicalWindowMetrics(hWnd, physRect, clientOrigin)) return false;

    int physW = physRect.right - physRect.left;
    int physH = physRect.bottom - physRect.top;
    if (physW <= 0 || physH <= 0) return false;

    int virtX = screenX - clientOrigin.x;
    int virtY = screenY - clientOrigin.y;

    if (virtX >= 0 && virtX <= static_cast<int>(m_targetRenderWidth) &&
        virtY >= 0 && virtY <= static_cast<int>(m_targetRenderHeight)) {
        int scaledPhysX = static_cast<int>(std::round(
            (static_cast<double>(virtX) * physW) / m_targetRenderWidth
        ));
        int scaledPhysY = static_cast<int>(std::round(
            (static_cast<double>(virtY) * physH) / m_targetRenderHeight
        ));
        outPhysScreenX = clientOrigin.x + scaledPhysX;
        outPhysScreenY = clientOrigin.y + scaledPhysY;
        return true;
    }

    return false;
}

bool MirrorWindowController::ConstrainVirtualClipRectToPhysical(HWND hWnd, const RECT* lpRect, RECT& outPhysClipRect) const {
    if (!IsActive() || !hWnd || !lpRect) return false;
    if (m_targetRenderWidth == 0 || m_targetRenderHeight == 0) return false;

    RECT physRect{};
    POINT clientOrigin{ 0, 0 };
    if (!GetPhysicalWindowMetrics(hWnd, physRect, clientOrigin)) return false;

    int physW = physRect.right - physRect.left;
    int physH = physRect.bottom - physRect.top;
    if (physW <= 0 || physH <= 0) return false;

    outPhysClipRect.left = clientOrigin.x;
    outPhysClipRect.top = clientOrigin.y;
    outPhysClipRect.right = clientOrigin.x + physW;
    outPhysClipRect.bottom = clientOrigin.y + physH;
    return true;
}

bool MirrorWindowController::IsGameWindowForeground() const {
    if (!m_gameHwnd) return false;
    HWND foreground = m_cursorPort->GetForegroundWindow();
    return foreground == m_gameHwnd || IsChild(m_gameHwnd, foreground);
}

void MirrorWindowController::UpdateMenuCursorState(IDXGISwapChain* swapChain, OpenXrManager* openXrManager) {
    if (!openXrManager) return;

    // The Pointer Ray has control: the cursor is drawn where the ray lands on the panel, whatever the
    // desk mouse is doing or which window has focus.
    const PointerCursorOverride& pointer = openXrManager->GetPointerCursorOverride();
    if (pointer.active) {
        openXrManager->SetMenuCursorState({ true, pointer.u, pointer.v, MenuCursorState::kDefaultCursorSize });
        return;
    }

    HWND hWnd = m_gameHwnd;
    if (!hWnd && swapChain) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow) {
            SetGameHwnd(desc.OutputWindow);
            hWnd = m_gameHwnd;
        }
    }

    if (!hWnd) {
        return;
    }

    if (!IsGameWindowForeground()) {
        openXrManager->SetMenuCursorState({ false, 0.0f, 0.0f, MenuCursorState::kDefaultCursorSize });
        return;
    }

    if (m_cursorPort->IsCursorShowing()) {
        POINT screenPt{};
        float normX = 0.0f, normY = 0.0f;
        if (m_cursorPort->GetCursorPos(screenPt) && ScreenToNormalizedClient(hWnd, screenPt, normX, normY)) {
            openXrManager->SetMenuCursorState({ true, normX, normY, MenuCursorState::kDefaultCursorSize });
            return;
        }
    }

    openXrManager->SetMenuCursorState({ false, 0.0f, 0.0f, MenuCursorState::kDefaultCursorSize });
}

bool MirrorWindowController::GetGamePicture(RECT& outPicture) const {
    RECT clientSize{};
    POINT clientOrigin{ 0, 0 };
    if (!GetPhysicalWindowMetrics(m_gameHwnd, clientSize, clientOrigin)) return false;
    const int width = clientSize.right - clientSize.left;
    const int height = clientSize.bottom - clientSize.top;
    if (width <= 0 || height <= 0) return false;
    outPicture = { clientOrigin.x, clientOrigin.y, clientOrigin.x + width, clientOrigin.y + height };
    return true;
}

void MirrorWindowController::UpdateMouseConfinement(MouseConfinementState state) {
    if (state != MouseConfinementState::Holding) {
        if (!m_holdingMouse) {
            // Turned off while it was not holding (focus already lost): when it is turned on again, that is the reason.
            if (state == MouseConfinementState::TurnedOffByCommand) m_lastMouseRelease = state;
            return;
        }
        const char* reason = "focus lost";
        if (state == MouseConfinementState::PassThroughMode) reason = "Pass-Through Mode";
        if (state == MouseConfinementState::SessionNotRunning) reason = "session not running";
        if (state == MouseConfinementState::TurnedOffByCommand) reason = "turned off by command (vrMouseConfinement off)";
        StopHoldingMouse(reason);
        m_lastMouseRelease = state;
        return;
    }

    RECT picture{};
    if (!m_gameHwnd || !GetGamePicture(picture)) return;

    if (!m_holdingMouse) {
        m_holdingMouse = true;
        const char* reason = "headset in use";
        if (m_lastMouseRelease == MouseConfinementState::FocusLost) reason = "focus regained";
        if (m_lastMouseRelease == MouseConfinementState::SessionNotRunning) reason = "session running again";
        if (m_lastMouseRelease == MouseConfinementState::TurnedOffByCommand) reason = "turned on by command";
        LogInfo(std::string("MirrorWindowController: Mouse Confinement holds the Desk Mouse to the game picture (") + reason + ").");

        // A Desk Mouse that is off the picture when holding starts comes to the middle of the physical client
        // area, through the port's original SetCursorPos. Only at the start: never while it keeps holding.
        POINT cursor{};
        if (m_cursorPort->GetCursorPos(cursor) && !PtInRect(&picture, cursor)) {
            m_cursorPort->SetCursorPos((picture.left + picture.right) / 2, (picture.top + picture.bottom) / 2);
        }
    }

    // Set again only when the clip differs from the picture: Windows or another program may have cleared it.
    RECT current{};
    if (!m_cursorPort->GetClip(current) || !EqualRect(&current, &picture)) {
        m_cursorPort->SetClip(&picture);
        m_setMouseClip = true;
    }
}

void MirrorWindowController::StopHoldingMouse(const char* reason) {
    if (!m_holdingMouse) return;
    m_holdingMouse = false;
    LogInfo(std::string("MirrorWindowController: Mouse Confinement released the Desk Mouse (") + reason + ").");
    // Put back what the game last asked for, so its own mouse-look capture survives; a clip the
    // controller did not set is not its to release.
    if (m_setMouseClip) {
        m_setMouseClip = false;
        m_cursorPort->SetClip(m_gameClip ? &*m_gameClip : nullptr);
    }
}

bool MirrorWindowController::HandleGameClipCursor(const RECT* lpRect) {
    const RECT* clip = lpRect;
    RECT physClip{};
    if (lpRect && IsActive() && m_gameHwnd && ConstrainVirtualClipRectToPhysical(m_gameHwnd, lpRect, physClip)) {
        clip = &physClip;
    }
    if (clip) {
        m_gameClip = *clip;
    } else {
        m_gameClip.reset();
    }

    RECT picture{};
    if (!clip && m_holdingMouse && GetGamePicture(picture)) {
        // The game lets go (it does in a menu) but the Desk Mouse stays on the picture.
        m_setMouseClip = true;
        return m_cursorPort->SetClip(&picture);
    }
    return m_cursorPort->SetClip(clip);
}

LRESULT CALLBACK MirrorWindowController::MirrorWindowSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto* controller = reinterpret_cast<MirrorWindowController*>(dwRefData);

    switch (uMsg) {
        case WM_ENTERSIZEMOVE: {
            if (controller) {
                controller->m_inSizeMove = true;
            }
            break;
        }
        case WM_EXITSIZEMOVE: {
            if (controller) {
                controller->m_inSizeMove = false;
                controller->m_windowClampDirty = true;
            }
            break;
        }
        case WM_GETMINMAXINFO: {
            LRESULT lres = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            if (mmi && controller && controller->IsActive()) {
                RECT clamped = ComputeClampedMirrorWindowRect(hWnd);
                int maxW = clamped.right - clamped.left;
                int maxH = clamped.bottom - clamped.top;
                if (maxW > 0 && maxH > 0) {
                    if (mmi->ptMinTrackSize.x > maxW) {
                        mmi->ptMinTrackSize.x = maxW;
                    }
                    if (mmi->ptMinTrackSize.y > maxH) {
                        mmi->ptMinTrackSize.y = maxH;
                    }
                    mmi->ptMaxTrackSize.x = (std::min)(mmi->ptMaxTrackSize.x, static_cast<LONG>(maxW));
                    mmi->ptMaxTrackSize.y = (std::min)(mmi->ptMaxTrackSize.y, static_cast<LONG>(maxH));
                    mmi->ptMaxSize.x = (std::min)(mmi->ptMaxSize.x, static_cast<LONG>(maxW));
                    mmi->ptMaxSize.y = (std::min)(mmi->ptMaxSize.y, static_cast<LONG>(maxH));
                }
            }
            return lres;
        }
        case WM_WINDOWPOSCHANGING: {
            LRESULT lres = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);
            if (wp && controller && controller->IsActive() && !(wp->flags & SWP_NOSIZE)) {
                RECT clamped = ComputeClampedMirrorWindowRect(hWnd);
                int maxW = clamped.right - clamped.left;
                int maxH = clamped.bottom - clamped.top;
                if (maxW > 0 && wp->cx > maxW) {
                    wp->cx = maxW;
                }
                if (maxH > 0 && wp->cy > maxH) {
                    wp->cy = maxH;
                }
            }
            return lres;
        }
        case WM_SIZE: {
            if (controller && controller->IsActive() && controller->m_targetRenderWidth > 0 && controller->m_targetRenderHeight > 0) {
                if (wParam != SIZE_MINIMIZED) {
                    LPARAM targetLParam = MAKELPARAM(
                        static_cast<WORD>(controller->m_targetRenderWidth),
                        static_cast<WORD>(controller->m_targetRenderHeight)
                    );
                    return DefSubclassProc(hWnd, uMsg, wParam, targetLParam);
                }
            }
            return DefSubclassProc(hWnd, uMsg, wParam, lParam);
        }
        case WM_DISPLAYCHANGE: {
            if (controller) {
                controller->m_windowClampDirty = true;
            }
            return DefSubclassProc(hWnd, uMsg, wParam, lParam);
        }
        case WM_WINDOWPOSCHANGED: {
            if (controller) {
                controller->m_windowClampDirty = true;
            }
            LRESULT lres = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            if (controller && controller->IsActive() && controller->m_targetRenderWidth > 0 && controller->m_targetRenderHeight > 0) {
                auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);
                if (wp && !(wp->flags & SWP_NOSIZE)) {
                    LPARAM targetLParam = MAKELPARAM(
                        static_cast<WORD>(controller->m_targetRenderWidth),
                        static_cast<WORD>(controller->m_targetRenderHeight)
                    );
                    DefSubclassProc(hWnd, WM_SIZE, SIZE_RESTORED, targetLParam);
                }
            }
            return lres;
        }
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK: {
            if (controller && controller->IsActive() && controller->m_targetRenderWidth > 0 && controller->m_targetRenderHeight > 0) {
                RECT physRect{};
                POINT dummyOrigin{};
                if (controller->GetPhysicalWindowMetrics(hWnd, physRect, dummyOrigin)) {
                    int physW = physRect.right - physRect.left;
                    int physH = physRect.bottom - physRect.top;
                    if (physW > 0 && physH > 0) {
                        int clientX = static_cast<short>(LOWORD(lParam));
                        int clientY = static_cast<short>(HIWORD(lParam));
                        int scaledX = static_cast<int>(std::round((static_cast<double>(clientX) * controller->m_targetRenderWidth) / physW));
                        int scaledY = static_cast<int>(std::round((static_cast<double>(clientY) * controller->m_targetRenderHeight) / physH));
                        scaledX = (std::max)(0, (std::min)(scaledX, static_cast<int>(controller->m_targetRenderWidth)));
                        scaledY = (std::max)(0, (std::min)(scaledY, static_cast<int>(controller->m_targetRenderHeight)));
                        LPARAM scaledLParam = MAKELPARAM(static_cast<WORD>(scaledX), static_cast<WORD>(scaledY));
                        return DefSubclassProc(hWnd, uMsg, wParam, scaledLParam);
                    }
                }
            }
            break;
        }
        case WM_NCDESTROY: {
            RemoveWindowSubclass(hWnd, &MirrorWindowController::MirrorWindowSubclassProc, uIdSubclass);
            if (controller) {
                controller->m_windowSubclassed = false;
                controller->m_inSizeMove = false;
                if (controller->m_gameHwnd == hWnd) {
                    controller->m_gameHwnd = nullptr;
                }
            }
            break;
        }
        default:
            break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

BOOL WINAPI MirrorWindowController::Intercept_GetClientRect(HWND hWnd, LPRECT lpRect) {
    MirrorWindowController* ctrl = ForHook(&MirrorWindowController::m_detourGetClientRect);
    auto orig = reinterpret_cast<BOOL(WINAPI*)(HWND, LPRECT)>(
        ctrl ? ctrl->m_detourGetClientRect.GetOriginal() : nullptr
    );
    if (t_isInternalWindowQuery) {
        return orig ? orig(hWnd, lpRect) : ::GetClientRect(hWnd, lpRect);
    }
    // Cheap conditions first: the root-window walk only runs once VR is active and a target
    // render resolution is set.
    if (ctrl && lpRect && ctrl->IsActive() && ctrl->m_gameHwnd
        && ctrl->m_targetRenderWidth > 0 && ctrl->m_targetRenderHeight > 0) {
        if (hWnd == ctrl->m_gameHwnd || GetRootWindow(hWnd) == ctrl->m_gameHwnd) {
            lpRect->left = 0;
            lpRect->top = 0;
            lpRect->right = static_cast<LONG>(ctrl->m_targetRenderWidth);
            lpRect->bottom = static_cast<LONG>(ctrl->m_targetRenderHeight);
            return TRUE;
        }
    }
    return orig ? orig(hWnd, lpRect) : ::GetClientRect(hWnd, lpRect);
}

BOOL WINAPI MirrorWindowController::Intercept_ScreenToClient(HWND hWnd, LPPOINT lpPoint) {
    MirrorWindowController* ctrl = ForHook(&MirrorWindowController::m_detourScreenToClient);
    auto orig = reinterpret_cast<BOOL(WINAPI*)(HWND, LPPOINT)>(
        ctrl ? ctrl->m_detourScreenToClient.GetOriginal() : nullptr
    );
    auto invokeOrig = [orig](HWND h, LPPOINT pt) -> BOOL {
        return orig ? orig(h, pt) : ::ScreenToClient(h, pt);
    };

    if (t_isInternalWindowQuery) {
        return invokeOrig(hWnd, lpPoint);
    }
    if (!invokeOrig(hWnd, lpPoint)) {
        return FALSE;
    }
    if (ctrl && ctrl->IsActive() && ctrl->m_gameHwnd && lpPoint) {
        if (hWnd == ctrl->m_gameHwnd || GetRootWindow(hWnd) == ctrl->m_gameHwnd) {
            if (ctrl->m_targetRenderWidth > 0 && ctrl->m_targetRenderHeight > 0) {
                RECT physRect{};
                POINT dummyOrigin{};
                if (ctrl->GetPhysicalWindowMetrics(hWnd, physRect, dummyOrigin)) {
                    int physW = physRect.right - physRect.left;
                    int physH = physRect.bottom - physRect.top;
                    if (physW > 0 && physH > 0) {
                        lpPoint->x = static_cast<LONG>(std::round((static_cast<double>(lpPoint->x) * ctrl->m_targetRenderWidth) / physW));
                        lpPoint->y = static_cast<LONG>(std::round((static_cast<double>(lpPoint->y) * ctrl->m_targetRenderHeight) / physH));
                    }
                }
            }
        }
    }
    return TRUE;
}

BOOL WINAPI MirrorWindowController::Intercept_SetCursorPos(int X, int Y) {
    MirrorWindowController* ctrl = ForHook(&MirrorWindowController::m_detourSetCursorPos);
    auto orig = reinterpret_cast<BOOL(WINAPI*)(int, int)>(
        ctrl ? ctrl->m_detourSetCursorPos.GetOriginal() : nullptr
    );
    // With a live runtime the move goes through the desktop cursor port; without one, straight to user32.
    auto invokeOrig = [orig, ctrl](int x, int y) -> BOOL {
        if (ctrl) return ctrl->m_cursorPort->SetCursorPos(x, y) ? TRUE : FALSE;
        return orig ? orig(x, y) : ::SetCursorPos(x, y);
    };

    if (t_isInternalWindowQuery) {
        return invokeOrig(X, Y);
    }

    if (ctrl && ctrl->IsActive() && ctrl->m_gameHwnd) {
        int physX = X;
        int physY = Y;
        if (ctrl->ScaleVirtualCursorToPhysical(ctrl->m_gameHwnd, X, Y, physX, physY)) {
            return invokeOrig(physX, physY);
        }
    }

    return invokeOrig(X, Y);
}

BOOL WINAPI MirrorWindowController::Intercept_ClipCursor(const RECT* lpRect) {
    MirrorWindowController* ctrl = ForHook(&MirrorWindowController::m_detourClipCursor);
    auto orig = reinterpret_cast<BOOL(WINAPI*)(const RECT*)>(
        ctrl ? ctrl->m_detourClipCursor.GetOriginal() : nullptr
    );
    // With a live runtime the clip goes through the desktop cursor port; without one, straight to user32.
    if (ctrl) {
        if (t_isInternalWindowQuery) return ctrl->m_cursorPort->SetClip(lpRect) ? TRUE : FALSE;
        return ctrl->HandleGameClipCursor(lpRect) ? TRUE : FALSE;
    }
    return orig ? orig(lpRect) : ::ClipCursor(lpRect);
}

} // namespace fs25vr
