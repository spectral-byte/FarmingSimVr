-- ====================================================================
-- FS25 VR Companion Mod: frame timing trace
--
-- The console command `vrPerfTrace [seconds]` times the frame for a few seconds and logs it to fs25_vr.log (not log.txt): one
-- line per second, then a summary. The Injector times its own work (its present, the eye copies, xrWaitFrame, xrEndFrame, the
-- game's real Present) and the GPU work it records (the eye copies, the motion vector pass). The Companion Mod adds its hooks
-- (core/perf_trace.h in the Injector lists every section):
--   gameUpdate  the game's mission update, which runs the Companion Mod's update inside it
--   modUpdate   VRCompanion:update
--   modCamera   the vehicle and player camera hooks
--   modDraw     VRCompanion:draw
-- Off, a hook costs one field read: VRBridge is only called while `perfTracing` is set. The Injector ends the trace by itself,
-- and the update clears `perfTracing` once VRBridge says it has.
--
-- PUBLIC SURFACE
--   VRCompanion.perfTracing                         true while a trace runs
--   VRCompanion:perfTraceBegin(name) / perfTraceEnd(name, ...)   around a section; End returns its extra arguments, so it can
--                                                   wrap a call's results
--   VRCompanion:refreshPerfTracing()                 the update: notices the Injector ended the trace
--   VRCompanion:consoleCommandPerfTrace(arg)         starts a trace of `arg` seconds (default 10, up to 60); "stop" ends it
--   VRCompanion:registerPerfTraceConsoleCommands() / unregisterPerfTraceConsoleCommands()   map load / unload
--   Console: vrPerfTrace [seconds|stop]
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.PerfTrace = VRCompanion.PerfTrace or {}

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

function VRCompanion:registerPerfTraceConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrPerfTrace", "Log where frame time goes (CPU, GPU, Companion Mod) to fs25_vr.log for some seconds: vrPerfTrace [seconds|stop]", "consoleCommandPerfTrace", self)
end

-- Map unload: the command goes and a running trace ends with its summary.
function VRCompanion:unregisterPerfTraceConsoleCommands()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrPerfTrace")
    end
    if self.perfTracing then
        local b = bridge()
        if b ~= nil then b.stopPerfTrace() end
        self.perfTracing = false
    end
end

VRCompanion.PerfTrace.consoleCommandPerfTrace = function(self, ...) return VRCompanion.consoleCommandPerfTrace(VRCompanion, ...) end

return VRCompanion.PerfTrace
