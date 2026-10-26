-- ====================================================================
-- FS25 VR Companion Mod: Dual-Pass Physics Pause and the eye pair trace
--
-- Sequential Dual-Pass freezes the game's Lua simulation for the second sub-pass, but the engine simulates physics natively
-- every rendered frame, whatever dt the mission update is given. So a driven vehicle moves between the two eyes of a pair,
-- and the eye cameras ride on it. Driving at 20 km/h it covers about 6 cm in an 11 ms frame, one IPD: looking to the side
-- the eyes then seem twice as far apart (the world shrinks) or not apart at all (it goes flat and huge).
--
-- The pause stops physics for one frame of each pair and runs the other at the pair's time scale, so physics keeps game time.
-- Whether the engine applies simulatePhysics to the frame whose update sets it or to the next is not known, so it has a
-- phase: "on" pauses from the second sub-pass's update to the first's, "shifted" from the first's to the second's. The eye
-- pair trace shows which of them keeps the vehicle still between the eyes.
--
-- PUBLIC SURFACE
--   VRCompanion:setDualPassPhysicsPause(mode)                       "off" (the default), "on" or "shifted"; session only
--   VRCompanion:applyDualPassPhysicsPause(subPass, frameDt)         called by runMissionUpdate at the end of each sub-pass's
--                                                                   update, with the frame's real dt
--   VRCompanion:releaseDualPassPhysicsPause()                       physics running at normal speed again; outside Sequential
--                                                                   Dual-Pass, on turning the pause off and on map unload
--   VRCompanion:traceEyeFrame(eyeIndex, camNode, subPass)           one vrEyePairTrace line for a rendered eye frame (subPass
--                                                                   nil under AFR); returns the line, nil when not tracing
--   Console: vrDualPassPhysicsPause [off|on|shifted], vrEyePairTrace [frames]
--   VRCompanion:registerDualPassPhysicsConsoleCommands() / unregisterDualPassPhysicsConsoleCommands()   map load / unload
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.DualPassPhysics = VRCompanion.DualPassPhysics or {}

VRCompanion.DUAL_PASS_PHYSICS_PAUSE_MODES = { off = true, on = true, shifted = true }
VRCompanion.DUAL_PASS_PHYSICS_MAX_TIME_SCALE = 4.0
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
    if not self.physicsPauseApplied and (self.physicsTimeScaleApplied or 1.0) == 1.0 then return end
    if gameHoldsPhysics() then return end
    setPhysicsRunning(self, true, 1.0)
end

-- Pauses or resumes physics for the frame after this sub-pass's update; frameDt is this frame's real dt. The frame that
-- resumes covers the paused frame's time too, so it runs at (paused dt + its own dt) / its own dt, about 2.
function VRCompanion:applyDualPassPhysicsPause(subPass, frameDt)
    local mode = self.dualPassPhysicsPause
    if mode ~= "on" and mode ~= "shifted" then return end
    if gameHoldsPhysics() then return end
    local pausedDt = self.physicsPausedFrameDt
    local pauseNow = (mode == "on" and subPass == 1) or (mode == "shifted" and subPass == 0)
    if pauseNow then
        self.physicsPausedFrameDt = frameDt
        setPhysicsRunning(self, false)
        return
    end
    local scale = self.physicsTimeScaleApplied or 1.0
    if self.physicsPauseApplied then
        scale = 2.0
        if frameDt ~= nil and frameDt > 0 and pausedDt ~= nil and pausedDt >= 0 then
            scale = math.max(1.0, math.min(self.DUAL_PASS_PHYSICS_MAX_TIME_SCALE, (pausedDt + frameDt) / frameDt))
        end
    end
    setPhysicsRunning(self, true, scale)
end

-- ---- vrEyePairTrace [frames] -------------------------------------------------------------------
-- For the next frames (default 32), logs each rendered eye frame: the sub-pass (Sequential Dual-Pass) or AFR, the eye, the
-- engine's physics update index, the controlled vehicle's speed, and how far its root node and the eye camera moved in the
-- world since the previous eye frame, with the part of the vehicle's move along the eyes (the head's left-right axis), which
-- is what adds to or takes from the IPD. A still vehicle between the eyes of a pair shows as 0.00 cm on the second eye.
local trace = { frames = 0 }

local function distance(ax, ay, az, bx, by, bz)
    local dx, dy, dz = ax - bx, ay - by, az - bz
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

function VRCompanion:traceEyeFrame(eyeIndex, camNode, subPass)
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

    local line = string.format("[FS25_VR] Eye pair trace: frame %s %s eye %d | physics %s | %s | %s | camera moved %s | pause %s",
        tostring(self.frameTicks), pass, eyeIndex or -1, physicsIndex, speed, vehicleMove, cameraMove,
        tostring(self.dualPassPhysicsPause))
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
        return "Usage: vrDualPassPhysicsPause [off|on|shifted]"
    end
    return string.format("Dual-Pass Physics Pause is %s", self.dualPassPhysicsPause)
end

function VRCompanion:registerDualPassPhysicsConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrDualPassPhysicsPause", "Pause physics for one frame of each Sequential Dual-Pass pair, so a moving vehicle is still between the eyes (this session only; off by default): vrDualPassPhysicsPause [off|on|shifted]", "consoleCommandDualPassPhysicsPause", self)
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
