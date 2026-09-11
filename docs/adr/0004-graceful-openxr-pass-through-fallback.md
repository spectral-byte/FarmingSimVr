# Graceful OpenXR Pass-Through Fallback

## Context and Decision
Players launch Farming Simulator 25 in diverse configurations, often wishing to play in standard 2D desktop mode without powering on or connecting an OpenXR VR headset. If `xrCreateInstance` fails or returns `XR_ERROR_RUNTIME_UNAVAILABLE`, the Injector (`dxgi.dll`) enters Pass-Through Mode, silently forwarding all DXGI and D3D12 calls to `C:\Windows\System32\dxgi.dll` without displaying error dialogs or aborting process execution.

## Status
Accepted

## Considered Options
- Displaying a blocking Win32 message box warning that VR was not found (rejected: disrupts desktop play and automated testing).
- Forcing process termination if OpenXR fails (rejected: unacceptable user friction).
- Silent graceful pass-through (selected: allows the proxy DLL to remain permanently installed in `x64/` without breaking standard desktop gaming).
