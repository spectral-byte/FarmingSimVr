VRCompanion = VRCompanion or {}

VRCompanion.PERF_TRACE_DEFAULT_SECONDS = 10
VRCompanion.PERF_TRACE_MAX_SECONDS = 60
VRCompanion.perfTracing = false

local USAGE = string.format("Usage: vrPerfTrace [seconds, default %d, up to %d | stop]",
    VRCompanion.PERF_TRACE_DEFAULT_SECONDS, VRCompanion.PERF_TRACE_MAX_SECONDS)

local function bridge()
    local b = VRBridge or (_G and _G.VRBridge)
    if type(b) == "table" and type(b.startPerfTrace) == "function" then return b end
    return nil
end

function VRCompanion:perfTraceBegin(name)
    if not self.perfTracing then return end
    local b = bridge()
    if b ~= nil then b.perfTraceBegin(name) end
end

function VRCompanion:perfTraceEnd(name, ...)
    if self.perfTracing then
        local b = bridge()
        if b ~= nil then b.perfTraceEnd(name) end
    end
    return ...
end

function VRCompanion:refreshPerfTracing()
    if not self.perfTracing then return end
    local b = bridge()
    self.perfTracing = b ~= nil and b.isPerfTracing() == true
end

function VRCompanion:consoleCommandPerfTrace(arg)
    local b = bridge()
    if b == nil then
        return "vrPerfTrace needs the Injector (dxgi.dll), which is not loaded"
    end
    if arg ~= nil and string.lower(tostring(arg)) == "stop" then
        if not self.perfTracing then return "No frame timing trace is running" end
        b.stopPerfTrace()
        self.perfTracing = false
        return "Frame timing trace stopped; its summary is in fs25_vr.log"
    end
    local seconds = self.PERF_TRACE_DEFAULT_SECONDS
    if arg ~= nil and arg ~= "" then
        seconds = tonumber(arg)
        if seconds == nil or seconds < 1 or seconds > self.PERF_TRACE_MAX_SECONDS then
            return USAGE
        end
        seconds = math.floor(seconds)
    end
    b.startPerfTrace(seconds)
    self.perfTracing = true
    print(string.format("[FS25_VR] Frame timing trace started for %d s; results go to fs25_vr.log next to dxgi.dll.", seconds))
    return string.format("Tracing frame timing for %d s into fs25_vr.log (next to the game's dxgi.dll)", seconds)
end

VRCompanion:registerSubsystem("perfTrace", {
    mapUnload = function(self)
        if self.perfTracing then
            local b = bridge()
            if b ~= nil then b.stopPerfTrace() end
            self.perfTracing = false
        end
    end,
    consoleCommands = {
        { name = "vrPerfTrace", description = "Log where frame time goes (CPU, GPU, Companion Mod) to fs25_vr.log for some seconds: vrPerfTrace [seconds|stop]", method = "consoleCommandPerfTrace" },
    },
})
