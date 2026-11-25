VRCompanion = VRCompanion or {}

VRCompanion.DUAL_PASS_PHYSICS_PAUSE_MODES = { off = true, on = true }
VRCompanion.DUAL_PASS_PHYSICS_MAX_TIME_SCALE = 4.0
VRCompanion.DUAL_PASS_PHYSICS_MAX_OWED_TIME = 100.0
VRCompanion.DUAL_PASS_PHYSICS_PAIR_PERIOD_SMOOTHING = 0.1
VRCompanion.DUAL_PASS_PHYSICS_SECOND_SUB_PASS_DT_SMOOTHING = 0.2
VRCompanion.DUAL_PASS_PHYSICS_OWED_TIME_GAIN = 0.25
VRCompanion.EYE_PAIR_TRACE_DEFAULT_FRAMES = 32

VRCompanion.dualPassPhysicsPause = VRCompanion.dualPassPhysicsPause or "on"

local function engine(name)
    return _G[name]
end

local function gameHoldsPhysics()
    local mission = g_currentMission or (_G and _G.g_currentMission)
    return mission ~= nil and mission.vehicleSystem ~= nil and mission.vehicleSystem.isReloadRunning == true
end

local function isMultiplayer()
    local mission = g_currentMission or (_G and _G.g_currentMission)
    return mission ~= nil and mission.missionDynamicInfo ~= nil and mission.missionDynamicInfo.isMultiplayer == true
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
    self.physicsSecondSubPassDtPredicted = nil
    self.physicsPairPeriod = nil
    if not self.physicsPauseApplied and (self.physicsTimeScaleApplied or 1.0) == 1.0 then return end
    if gameHoldsPhysics() then return end
    setPhysicsRunning(self, true, 1.0)
end

local function smoothed(current, sample, smoothing)
    if current == nil then return sample end
    return current + smoothing * (sample - current)
end

function VRCompanion:applyDualPassPhysicsPause(subPass, frameDt)
    if self.dualPassPhysicsPause ~= "on" then return end
    if gameHoldsPhysics() then return end
    if isMultiplayer() then
        self:releaseDualPassPhysicsPause()
        return
    end
    frameDt = math.max(0.0, frameDt or 0.0)
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
    self.physicsSecondSubPassDtPredicted = predicted
    if not stepPaused or lastSecondDt == nil or predicted == nil or predicted <= 0 then
        setPhysicsRunning(self, true, 1.0)
        return
    end
    local period = smoothed(self.physicsPairPeriod, math.min(maxOwed, lastSecondDt + frameDt),
        self.DUAL_PASS_PHYSICS_PAIR_PERIOD_SMOOTHING)
    self.physicsPairPeriod = period
    local leftOver = owed - frameDt
    local target = period + self.DUAL_PASS_PHYSICS_OWED_TIME_GAIN * leftOver
    local scale = math.max(1.0, math.min(self.DUAL_PASS_PHYSICS_MAX_TIME_SCALE, target / predicted))
    setPhysicsRunning(self, true, scale)
end

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
    local rawDt = formatDt(frameDt)
    if subPass == 1 and self.physicsSecondSubPassDtPredicted ~= nil then
        rawDt = string.format("%s (predicted %s)", rawDt, formatDt(self.physicsSecondSubPassDtPredicted))
    end

    local line = string.format("[FS25_VR] Eye pair trace: frame %s %s eye %d | physics %s | %s | %s | camera moved %s | pause %s"
        .. " | rawDt %s | physics dts %s/%s/%s | physics %s",
        tostring(self.frameTicks), pass, eyeIndex or -1, physicsIndex, speed, vehicleMove, cameraMove,
        tostring(self.dualPassPhysicsPause), rawDt, formatDt(engine("g_physicsDt")),
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

local function describePause(self)
    local held = (self.dualPassPhysicsPause == "on" and isMultiplayer()) and " (held off in multiplayer)" or ""
    return string.format("Dual-Pass Physics Pause is %s%s", self.dualPassPhysicsPause, held)
end

function VRCompanion:consoleCommandDualPassPhysicsPause(arg)
    if arg == nil or arg == "" then
        return describePause(self)
    end
    if not self:setDualPassPhysicsPause(string.lower(arg)) then
        return "Usage: vrDualPassPhysicsPause [off|on]"
    end
    return describePause(self)
end

VRCompanion:registerSubsystem("dualPassPhysics", {
    mapUnload = function(self)
        self:releaseDualPassPhysicsPause()
        trace = { frames = 0 }
    end,
    consoleCommands = {
        { name = "vrDualPassPhysicsPause", description = "Pause physics for one frame of each Sequential Dual-Pass pair, so a moving vehicle is still between the eyes (this session only; on by default; held off in multiplayer): vrDualPassPhysicsPause [off|on]", method = "consoleCommandDualPassPhysicsPause" },
        { name = "vrEyePairTrace", description = "Log how far the vehicle and the eye camera moved between rendered eye frames, for some frames: vrEyePairTrace [frames]", method = "consoleCommandEyePairTrace" },
    },
})