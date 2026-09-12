-- ====================================================================
-- FS25 VR Companion Mod: In-Engine Luau Coordination & Handshake
-- Interfaces with in-process native VRBridge table injected by dxgi.dll
-- ====================================================================

VRCompanion = {}
VRCompanion.name = g_currentModName or "FS25_VRCompanion"
VRCompanion.version = "1.0.0.0"
VRCompanion.handshakeComplete = false

function VRCompanion:loadMap(name)
    print(string.format("[FS25_VR] Initializing Companion Mod on map '%s'...", tostring(name)))

    if VRBridge ~= nil then
        local version = VRBridge.version or "unknown"
        local ready = false
        if type(VRBridge.isReady) == "function" then
            ready = VRBridge.isReady()
        end

        local handshakeResult = nil
        if type(VRBridge.handshake) == "function" then
            handshakeResult = VRBridge.handshake()
        end

        if handshakeResult and handshakeResult.success == true then
            self.handshakeComplete = true
            print(string.format("[FS25_VR] Handshake successful! VRBridge detected (version: %s, mode: %s, ready: %s).",
                tostring(handshakeResult.version or version),
                tostring(handshakeResult.mode or "unknown"),
                tostring(ready)))
        else
            self.handshakeComplete = false
            print(string.format("[FS25_VR] Handshake failed or rejected by native VRBridge (version: %s, ready: %s).",
                tostring(version),
                tostring(ready)))
        end
    else
        print("[FS25_VR] VRBridge not detected. Running in standard 2D Pass-Through Mode.")
    end
end

function VRCompanion:deleteMap()
    print("[FS25_VR] Unloading Companion Mod from map.")
    self.handshakeComplete = false
end

-- Register as an engine mod event listener if in-game environment
if addModEventListener ~= nil then
    addModEventListener(VRCompanion)
end
