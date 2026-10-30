#pragma once

#include "core/detour.h"
#include <windows.h>

namespace fs25vr {

// Everything the Mirror Window Controller asks Windows about the cursor and focus, behind one narrow
// interface: which window is in the foreground, the cursor clip (read, set, clear), the cursor position
// and moving the cursor. The controller makes no user32 call for these except through its port, so a
// test can supply a fake and never touch the developer's own mouse.
class IDesktopCursorPort {
public:
    virtual ~IDesktopCursorPort() = default;

    virtual HWND GetForegroundWindow() = 0;

    // The rectangle the cursor is clipped to; the whole desktop when nothing clips it.
    virtual bool GetClip(RECT& out) = 0;
    // Clips the cursor to *rect, or releases the clip when rect is null.
    virtual bool SetClip(const RECT* rect) = 0;

    virtual bool GetCursorPos(POINT& out) = 0;
    virtual bool SetCursorPos(int x, int y) = 0;

    // Whether the system cursor is visible (the game hides it while it holds the mouse for mouse-look).
    virtual bool IsCursorShowing() = 0;
};

// The port that talks to user32. The clip and the cursor move go through the originals of the
// ClipCursor and SetCursorPos detours, so it never re-enters the Injector's own hooks; until a detour
// is installed it calls user32 directly.
class Win32DesktopCursorPort : public IDesktopCursorPort {
public:
    // The detours are the controller's own and must outlive the port.
    Win32DesktopCursorPort(const FunctionDetour& clipCursorDetour, const FunctionDetour& setCursorPosDetour);

    HWND GetForegroundWindow() override;
    bool GetClip(RECT& out) override;
    bool SetClip(const RECT* rect) override;
    bool GetCursorPos(POINT& out) override;
    bool SetCursorPos(int x, int y) override;
    bool IsCursorShowing() override;

private:
    const FunctionDetour& m_clipCursorDetour;
    const FunctionDetour& m_setCursorPosDetour;
};

} // namespace fs25vr
