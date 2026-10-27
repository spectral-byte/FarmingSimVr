-- ====================================================================
-- FS25 VR Companion Mod: Dual-Pass Physics Pause and the eye pair trace
--
-- Sequential Dual-Pass freezes the game's Lua simulation for the second sub-pass, but the engine simulates physics natively
-- every rendered frame, whatever dt the mission update is given. So a driven vehicle moves between the two eyes of a pair,
-- and the eye cameras ride on it. Driving at 20 km/h it covers about 6 cm in an 11 ms frame, one IPD: looking to the side
-- the eyes then seem twice as far apart (the world shrinks) or not apart at all (it goes flat and huge).
--
-- The engine launches each frame's physics step as the frame starts, with that frame's dt (the dt its update then gets) and
-- the pause and time scale the previous update left; the update after it sees the step's result (eye pair traces in the
-- headset: g_physicsDt is always the previous update's dt). So the second sub-pass's update pauses the next first sub-pass's
-- step, the one between the eyes of a pair, and the first sub-pass's update sets the time scale of the second's step, which
-- has to simulate the paused time too. It simulates one pair period: the headset's refresh locks each second sub-pass's
-- frame and the next first sub-pass's together (their dts add up to 22.2 ms within hundredths at 90 Hz, while a pair's own
-- two dts swing about 3 ms), so that sum, smoothed, is the time each pair is shown for. The step's own dt is not known yet
-- when the time scale is set, so it is predicted from the second sub-passes' smoothed dt; a miss is multiplied by the time
-- scale (about 2.4). The physics time owed (real dt the steps have not simulated) keeps physics with real time: a fraction of
-- what is left over is added each pair, so a missed prediction is made up over a few pairs instead of swinging the next one.
--
-- PUBLIC SURFACE
--   VRCompanion:setDualPassPhysicsPause(mode)                       "off" (the default) or "on"; session only
--   VRCompanion:applyDualPassPhysicsPause(subPass, frameDt)         called by runMissionUpdate at the end of each sub-pass's
--                                                                   update, with the frame's real dt
--   VRCompanion:releaseDualPassPhysicsPause()                       physics running at normal speed again; outside Sequential
--                                                                   Dual-Pass, on turning the pause off and on map unload
--   VRCompanion:traceEyeFrame(eyeIndex, camNode, subPass, frameDt)  one vrEyePairTrace line for a rendered eye frame (subPass
--                                                                   nil under AFR; frameDt the update's real dt); returns the
--                                                                   line, nil when not tracing
--   Console: vrDualPassPhysicsPause [off|on], vrEyePairTrace [frames]
--   VRCompanion:registerDualPassPhysicsConsoleCommands() / unregisterDualPassPhysicsConsoleCommands()   map load / unload
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.DualPassPhysics = VRCompanion.DualPassPhysics or {}

VRCompanion.DUAL_PASS_PHYSICS_PAUSE_MODES = { off = true, on = true }
VRCompanion.DUAL_PASS_PHYSICS_MAX_TIME_SCALE = 4.0
-- Physics time owed is kept within this many ms either way, so a hitch is not caught up over many pairs
VRCompanion.DUAL_PASS_PHYSICS_MAX_OWED_TIME = 100.0
-- How far each pair's measured period moves the smoothed one
VRCompanion.DUAL_PASS_PHYSICS_PAIR_PERIOD_SMOOTHING = 0.1
-- How far each second sub-pass's dt moves the smoothed one the resumed step's dt is predicted from
VRCompanion.DUAL_PASS_PHYSICS_SECOND_SUB_PASS_DT_SMOOTHING = 0.2
-- The fraction of the physics time left over that each pair's resumed step adds
VRCompanion.DUAL_PASS_PHYSICS_OWED_TIME_GAIN = 0.25
VRCompanion.EYE_PAIR_TRACE_DEFAULT_FRAMES = 32

VRCompanion.dualPassPhysicsPause = VRCompanion.dualPassPhysicsPause or "off"

local function engine(name)
    return _G[name]
end

-- The game pauses physics itself while it reloads vehicles; the mod must not resume it then
local function gameHoldsPhysics()
    local mission = g_currentMission or (_G and _G.g_currentMission)
    return mission ~= nil and mission.vehicleSystem ~= nil and mission.vehicleSystem.isReloadRunning == true
end

local function setPhysicsRunning(self, running, timeScale)
    local simulatePhysicsFn = engine("simulatePhysics")
    if type(simulatePhysicsFn) ~= "function" then return end
    if not running then
        if not self.physicsPauseApplied then
            simulatePhysicsFn(false)
            self.physicsPauseApplied = true
        end
        return
    end
    if self.physicsPauseApplied then
        simulatePhysicsFn(true)
        self.physicsPauseApplied = false
    end
    local timeScaleFn = engine("simulatePhysicsTimeScale")
    if type(timeScaleFn) == "function" and timeScale ~= (self.physicsTimeScaleApplied or 1.0) then
        timeScaleFn(timeScale)
        self.physicsTimeScaleApplied = timeScale
    end
end

function VRCompanion:setDualPassPhysicsPause(mode)
    if not self.DUAL_PASS_PHYSICS_PAUSE_MODES[mode] then return false end
    self.dualPassPhysicsPause = mode
    if mode == "off" then self:releaseDualPassPhysicsPause() end
    return true
end

function VRCompanion:releaseDualPassPhysicsPause()
    self.physicsTimeOwed = 0.0
    self.physicsSecondSubPassDt = nil
    self.physicsSecondSubPassDtSmoothed = nil
    self.physicsPairPeriod = nil
    if not self.physicsPauseApplied and (self.physicsTimeScaleApplied or 1.0) == 1.0 then return end
    if gameHoldsPhysics() then return end
    setPhysicsRunning(self, true, 1.0)
end

-- Called at the end of each sub-pass's update; frameDt is this frame's real dt, the dt this frame's physics step ran with.
-- The second sub-pass pauses the next step; the first resumes the one after it at the time scale that simulates the
-- smoothed pair period and a fraction of the time owed beyond this frame's paused step, over the step's predicted dt.
local function smoothed(current, sample, smoothing)
    if current == nil then return sample end
    return current + smoothing * (sample - current)
end

function VRCompanion:applyDualPassPhysicsPause(subPass, frameDt)
    if self.dualPassPhysicsPause ~= "on" then return end
    if gameHoldsPhysics() then return end
    frameDt = math.max(0.0, frameDt or 0.0)
    -- This frame's step ran with what the previous update left, which is still applied
    local stepPaused = self.physicsPauseApplied == true
    local simulated = stepPaused and 0.0 or frameDt * (self.physicsTimeScaleApplied or 1.0)
    local maxOwed = self.DUAL_PASS_PHYSICS_MAX_OWED_TIME
    local owed = math.max(-maxOwed, math.min(maxOwed, (self.physicsTimeOwed or 0.0) + frameDt - simulated))
    self.physicsTimeOwed = owed
    local lastSecondDt = self.physicsSecondSubPassDt
    if subPass == 1 then
        self.physicsSecondSubPassDt = frameDt
        self.physicsSecondSubPassDtSmoothed = smoothed(self.physicsSecondSubPassDtSmoothed, frameDt,
            self.DUAL_PASS_PHYSICS_SECOND_SUB_PASS_DT_SMOOTHING)
        setPhysicsRunning(self, false)
        return
    end
    local predicted = self.physicsSecondSubPassDtSmoothed
    -- Nothing paused yet (the pause was just turned on): this pair runs at normal speed and the next one starts the pause
    if not stepPaused or lastSecondDt == nil or predicted == nil or predicted <= 0 then
        setPhysicsRunning(self, true, 1.0)
        return
    end
    local period = smoothed(self.physicsPairPeriod, math.min(maxOwed, lastSecondDt + frameDt),
        self.DUAL_PASS_PHYSICS_PAIR_PERIOD_SMOOTHING)
    self.physicsPairPeriod = period
    -- What the last resumed step left over, before this frame's paused step added its dt
    local leftOver = owed - frameDt
    local target = period + self.DUAL_PASS_PHYSICS_OWED_TIME_GAIN * leftOver
    local scale = math.max(1.0, math.min(self.DUAL_PASS_PHYSICS_MAX_TIME_SCALE, target / predicted))
    setPhysicsRunning(self, true, scale)
end

-- ---- vrEyePairTrace [frames] -------------------------------------------------------------------
-- For the next frames (default 32), logs each rendered eye frame: the sub-pass (Sequential Dual-Pass) or AFR, the eye, the
-- engine's physics update index, the controlled vehicle's speed, and how far its root node and the eye camera moved in the
-- world since the previous eye frame, with the part of the vehicle's move along the eyes (the head's left-right axis), which
-- is what adds to or takes from the IPD. A still vehicle between the eyes of a pair shows as 0.00 cm on the second eye.
-- Then the timing behind the move: the update's real dt, the engine's physics dts as the game's Lua sees them (g_physicsDt,
-- g_physicsDtUnclamped, g_physicsDtNonInterpolated, which is 0 on a frame that simulated nothing), and the physics pause
-- state as it stands when the line is logged: set by the earlier sub-passes, before this one's own pause or resume.
local trace = { frames = 0 }

local function distance(ax, ay, az, bx, by, bz)
    local dx, dy, dz = ax - bx, ay - by, az - bz
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function formatDt(dt)
    return type(dt) == "number" and string.format("%.2f", dt) or "?"
end

function VRCompanion:traceEyeFrame(eyeIndex, camNode, subPass, frameDt)
    if trace.frames <= 0 then return nil end
    trace.frames = trace.frames - 1
    local getWorldTranslationFn = engine("getWorldTranslation")
    if type(getWorldTranslationFn) ~= "function" or camNode == nil or camNode == 0 then return nil end

    local cx, cy, cz = getWorldTranslationFn(camNode)
    local cameraMove = "?"
    if trace.cx ~= nil then
        cameraMove = string.format("%.2f cm", 100 * distance(cx, cy, cz, trace.cx, trace.cy, trace.cz))
    end
    trace.cx, trace.cy, trace.cz = cx, cy, cz

    local vehicleMove, speed = "no vehicle", "?"
    local vehicle = self:getControlledVehicle()
    if type(vehicle) == "table" and vehicle.rootNode ~= nil and vehicle.rootNode ~= 0 then
        local vx, vy, vz = getWorldTranslationFn(vehicle.rootNode)
        if trace.vehicle == vehicle and trace.vx ~= nil then
            local dx, dy, dz = vx - trace.vx, vy - trace.vy, vz - trace.vz
            local along = "?"
            local localDirectionToWorldFn = engine("localDirectionToWorld")
            if type(localDirectionToWorldFn) == "function" then
                local rx, ry, rz = localDirectionToWorldFn(camNode, 1, 0, 0)
                along = string.format("%.2f cm", 100 * (dx * rx + dy * ry + dz * rz))
            end
            vehicleMove = string.format("vehicle moved %.2f cm (%s along the eyes)",
                100 * math.sqrt(dx * dx + dy * dy + dz * dz), along)
        else
            vehicleMove = "vehicle moved ? (first frame)"
        end
        trace.vehicle, trace.vx, trace.vy, trace.vz = vehicle, vx, vy, vz
        if type(vehicle.getLastSpeed) == "function" then
            local ok, kmh = pcall(vehicle.getLastSpeed, vehicle)
            if ok and kmh ~= nil then speed = string.format("%.1f km/h", kmh) end
        end
    end

    local getPhysicsUpdateIndexFn = engine("getPhysicsUpdateIndex")
    local physicsIndex = type(getPhysicsUpdateIndexFn) == "function" and tostring(getPhysicsUpdateIndexFn()) or "?"
    local pass = subPass ~= nil and string.format("sub-pass %d", subPass) or "AFR"

    local physicsState = self.physicsPauseApplied and "paused"
        or string.format("scale %.2f", self.physicsTimeScaleApplied or 1.0)

    local line = string.format("[FS25_VR] Eye pair trace: frame %s %s eye %d | physics %s | %s | %s | camera moved %s | pause %s"
        .. " | rawDt %s | physics dts %s/%s/%s | physics %s",
        tostring(self.frameTicks), pass, eyeIndex or -1, physicsIndex, speed, vehicleMove, cameraMove,
        tostring(self.dualPassPhysicsPause), formatDt(frameDt), formatDt(engine("g_physicsDt")),
        formatDt(engine("g_physicsDtUnclamped")), formatDt(engine("g_physicsDtNonInterpolated")), physicsState)
    print(line)
    return line
end

function VRCompanion:consoleCommandEyePairTrace(arg)
    local frames = math.floor(tonumber(arg) or self.EYE_PAIR_TRACE_DEFAULT_FRAMES)
    if frames < 1 then
        return string.format("Usage: vrEyePairTrace [frames, default %d]", self.EYE_PAIR_TRACE_DEFAULT_FRAMES)
    end
    trace = { frames = frames }
    return string.format("Tracing %d eye frames into the log", frames)
end

function VRCompanion:consoleCommandDualPassPhysicsPause(arg)
    if arg == nil or arg == "" then
        return string.format("Dual-Pass Physics Pause is %s", self.dualPassPhysicsPause)
    end
    if not self:setDualPassPhysicsPause(string.lower(arg)) then
        return "Usage: vrDualPassPhysicsPause [off|on]"
    end
    return string.format("Dual-Pass Physics Pause is %s", self.dualPassPhysicsPause)
end

function VRCompanion:registerDualPassPhysicsConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrDualPassPhysicsPause", "Pause physics for one frame of each Sequential Dual-Pass pair, so a moving vehicle is still between the eyes (this session only; off by default): vrDualPassPhysicsPause [off|on]", "consoleCommandDualPassPhysicsPause", self)
    addConsoleCommand("vrEyePairTrace", "Log how far the vehicle and the eye camera moved between rendered eye frames, for some frames: vrEyePairTrace [frames]", "consoleCommandEyePairTrace", self)
end

-- Map unload: physics runs normally again and the trace stops
function VRCompanion:unregisterDualPassPhysicsConsoleCommands()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrDualPassPhysicsPause")
        removeConsoleCommand("vrEyePairTrace")
    end
    self:releaseDualPassPhysicsPause()
    trace = { frames = 0 }
end

VRCompanion.DualPassPhysics.consoleCommandDualPassPhysicsPause = function(self, ...) return VRCompanion.consoleCommandDualPassPhysicsPause(VRCompanion, ...) end
VRCompanion.DualPassPhysics.consoleCommandEyePairTrace = function(self, ...) return VRCompanion.consoleCommandEyePairTrace(VRCompanion, ...) end

return VRCompanion.DualPassPhysics
