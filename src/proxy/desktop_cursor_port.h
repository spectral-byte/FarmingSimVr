#pragma once

#include "core/detour.h"
#include <windows.h>

namespace fs25vr {

class IDesktopCursorPort {
public:
    virtual ~IDesktopCursorPort() = default;

    virtual HWND GetForegroundWindow() = 0;

    // The rectangle the cursor is clipped to; the whole desktop when nothing clips it.
    virtual bool GetClip(RECT& out) = 0;
    virtual bool SetClip(const RECT* rect) = 0;

    virtual bool GetCursorPos(POINT& out) = 0;
    virtual bool SetCursorPos(int x, int y) = 0;

    virtual bool IsCursorShowing() = 0;
};

class Win32DesktopCursorPort : public IDesktopCursorPort {
public:
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

}
