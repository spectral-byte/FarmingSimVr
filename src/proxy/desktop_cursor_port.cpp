#include "desktop_cursor_port.h"

namespace fs25vr {

Win32DesktopCursorPort::Win32DesktopCursorPort(const FunctionDetour& clipCursorDetour, const FunctionDetour& setCursorPosDetour)
    : m_clipCursorDetour(clipCursorDetour), m_setCursorPosDetour(setCursorPosDetour) {
}

HWND Win32DesktopCursorPort::GetForegroundWindow() {
    return ::GetForegroundWindow();
}

bool Win32DesktopCursorPort::GetClip(RECT& out) {
    return ::GetClipCursor(&out) != FALSE;
}

bool Win32DesktopCursorPort::SetClip(const RECT* rect) {
    auto original = reinterpret_cast<BOOL(WINAPI*)(const RECT*)>(m_clipCursorDetour.GetOriginal());
    return (original ? original(rect) : ::ClipCursor(rect)) != FALSE;
}

bool Win32DesktopCursorPort::GetCursorPos(POINT& out) {
    if (::GetCursorPos(&out)) {
        return true;
    }
    CURSORINFO info{ sizeof(CURSORINFO) };
    if (!::GetCursorInfo(&info)) {
        return false;
    }
    out = info.ptScreenPos;
    return true;
}

bool Win32DesktopCursorPort::SetCursorPos(int x, int y) {
    auto original = reinterpret_cast<BOOL(WINAPI*)(int, int)>(m_setCursorPosDetour.GetOriginal());
    return (original ? original(x, y) : ::SetCursorPos(x, y)) != FALSE;
}

bool Win32DesktopCursorPort::IsCursorShowing() {
    CURSORINFO info{ sizeof(CURSORINFO) };
    return ::GetCursorInfo(&info) != FALSE && (info.flags & CURSOR_SHOWING) != 0;
}

} // namespace fs25vr
