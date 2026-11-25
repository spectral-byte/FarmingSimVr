#pragma once

#include "proxy/desktop_cursor_port.h"
#include <windows.h>
#include <optional>

namespace fs25vr {

class FakeDesktopCursorPort : public IDesktopCursorPort {
public:
    HWND foreground = nullptr;
    bool cursorShowing = true;
    POINT cursor{ 0, 0 };

    // What the controller did. clip is the rectangle the cursor is clipped to; empty when it is free.
    std::optional<RECT> clip;
    int setClipCalls = 0;
    int setCursorPosCalls = 0;

    HWND GetForegroundWindow() override { return foreground; }

    bool GetClip(RECT& out) override {
        out = clip ? *clip : DesktopBounds();
        return true;
    }

    bool SetClip(const RECT* rect) override {
        ++setClipCalls;
        if (rect) {
            clip = *rect;
        } else {
            clip.reset();
        }
        return true;
    }

    bool GetCursorPos(POINT& out) override {
        out = cursor;
        return true;
    }

    bool SetCursorPos(int x, int y) override {
        ++setCursorPosCalls;
        cursor = { x, y };
        return true;
    }

    bool IsCursorShowing() override { return cursorShowing; }

    // The desktop the cursor is free on, as GetClipCursor reports it when nothing clips it.
    static RECT DesktopBounds() {
        const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        return { left, top, left + GetSystemMetrics(SM_CXVIRTUALSCREEN), top + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
    }
};

}
