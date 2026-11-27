VRCompanion = VRCompanion or {}

VRCompanion.SEAT_OFFSET_LIMIT = 0.5

-- seatOffsets[model][cameraIndex] = { forward = f, up = u, right = r }; only non-zero offsets are kept
VRCompanion.seatOffsets = VRCompanion.seatOffsets or {}

function VRCompanion:getSeatOffsetTarget(cam)
    if type(cam) ~= "table" or not cam.isInside then return nil end
    local veh = cam.vehicle
    if veh == nil then veh = self:getControlledVehicle() end
    if type(veh) ~= "table" or veh.configFileName == nil then return nil end
    local enterable = veh.spec_enterable
    local cameras = enterable and enterable.cameras
    if type(cameras) ~= "table" then return nil end
    for index, candidate in ipairs(cameras) do
        if candidate == cam or (cam.cameraNode ~= nil and candidate.cameraNode == cam.cameraNode) then
            return veh.configFileName, index
        end
    end
    return nil
end

function VRCompanion:getSeatOffset(model, cameraIndex)
    local perCamera = self.seatOffsets[model]
    local offset = perCamera and perCamera[cameraIndex]
    if offset == nil then return 0, 0, 0 end
    return offset.forward, offset.up, offset.right
end

function VRCompanion:clampSeatOffset(forward, up, right)
    local limit = self.SEAT_OFFSET_LIMIT
    local clamped = nil
    local function clampAxis(name, value)
        if value > limit or value < -limit then
            clamped = clamped or {}
            clamped[#clamped + 1] = name
            return value > 0 and limit or -limit
        end
        return value + 0
    end
    forward = clampAxis("forward", forward)
    up = clampAxis("up", up)
    right = clampAxis("right", right)
    return forward, up, right, clamped
end

function VRCompanion:setSeatOffset(model, cameraIndex, forward, up, right)
    local clamped
    forward, up, right, clamped = self:clampSeatOffset(forward, up, right)
    local perCamera = self.seatOffsets[model]
    if forward == 0 and up == 0 and right == 0 then
        if perCamera ~= nil then
            perCamera[cameraIndex] = nil
            if next(perCamera) == nil then self.seatOffsets[model] = nil end
        end
        return clamped
    end
    if perCamera == nil then
        perCamera = {}
        self.seatOffsets[model] = perCamera
    end
    perCamera[cameraIndex] = { forward = forward, up = up, right = right }
    return clamped
end

function VRCompanion:getSeatOffsetFrameNode(cam)
    local veh = cam.vehicle
    if veh == nil then veh = self:getControlledVehicle() end
    local root = type(veh) == "table" and veh.rootNode or nil
    if root == nil or root == 0 then return nil end
    return root
end

local function vectorBetweenFrames(fromNode, toNode, x, y, z)
    local localToLocalFn = localToLocal or (_G and _G.localToLocal)
    if fromNode == nil or fromNode == 0 or toNode == nil or toNode == 0 or fromNode == toNode or type(localToLocalFn) ~= "function" then
        return x, y, z
    end
    local ox, oy, oz = localToLocalFn(fromNode, toNode, 0, 0, 0)
    local px, py, pz = localToLocalFn(fromNode, toNode, x, y, z)
    return px - ox, py - oy, pz - oz
end

function VRCompanion:getCabSeatOffset(cam, camParent)
    local model, index = self:getSeatOffsetTarget(cam)
    if model == nil then return 0, 0, 0 end
    local forward, up, right = self:getSeatOffset(model, index)
    return vectorBetweenFrames(self:getSeatOffsetFrameNode(cam), camParent, -right, up, forward)
end

function VRCompanion:getSeatLeanOfVehicle(cam, camParent, x, y, z)
    local vx, vy, vz = vectorBetweenFrames(camParent, self:getSeatOffsetFrameNode(cam), x, y, z)
    return vz, vy, -vx
end

local SEAT_OFFSET_USAGE = "Usage: vrSeatOffset [<forward> <up> <right> | reset] (metres from the vehicle's own eye point, each axis up to 0.5 either way; the in-cab camera you are in)"

local function parseSeatOffsetAxis(text)
    return VRCompanion.parseFiniteNumber(text)
end

local function describeSeatOffset(forward, up, right)
    return string.format("forward %.3f, up %.3f, right %.3f", forward, up, right)
end

function VRCompanion:consoleCommandSeatOffset(...)
    local count = select("#", ...)
    local first, second, third = ...
    local forward, up, right
    local reset = false
    if count == 3 then
        forward, up, right = parseSeatOffsetAxis(first), parseSeatOffsetAxis(second), parseSeatOffsetAxis(third)
        if forward == nil or up == nil or right == nil then return SEAT_OFFSET_USAGE end
    elseif count == 1 and string.lower(tostring(first)) == "reset" then
        reset = true
    elseif count ~= 0 then
        return SEAT_OFFSET_USAGE
    end

    local model, index = self:getSeatOffsetTarget(self:getActiveVehicleCamera())
    if model == nil then
        return "There is no in-cab camera to set a seat offset for: get into a vehicle and look out of its cab camera."
    end

    if count == 0 then
        return string.format("Seat offset: %s m. %s", describeSeatOffset(self:getSeatOffset(model, index)), SEAT_OFFSET_USAGE)
    end

    local clamped
    if reset then
        self:setSeatOffset(model, index, 0, 0, 0)
    else
        clamped = self:setSeatOffset(model, index, forward, up, right)
    end
    self:signalDiscontinuityFlush()
    self:saveConfiguration()

    local text = reset and "Seat offset reset to the vehicle's own eye point."
        or string.format("Seat offset set to %s m.", describeSeatOffset(self:getSeatOffset(model, index)))
    if clamped ~= nil then
        text = string.format("%s Clamped to %s m: %s.", text, tostring(self.SEAT_OFFSET_LIMIT), table.concat(clamped, ", "))
    end
    print("[FS25_VR] " .. text)
    return text
end

local SEAT_AXIS_TEXTS = {
    forward = { key = "vrSeatOffset_axisForward" },
    up = { key = "vrSeatOffset_axisUp" },
    right = { key = "vrSeatOffset_axisRight" },
}

local function showBlinkingWarning(text)
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function() mission:showBlinkingWarning(text, 2500) end)
    end
end

function VRCompanion:getHeadLeanInCab(cam, pose)
    local getParentFn = getParent or (_G and _G.getParent)
    local camParent = 0
    if type(getParentFn) == "function" then camParent = getParentFn(cam.cameraNode) end
    local lookYaw = self:getVehicleLookYaw(cam) or 0
    local headYaw = self:normalizeAngle((cam.origRotY or 0) + lookYaw)
    local rest = self:getCabRestPose(cam, camParent, headYaw, lookYaw, 0)
    local x, y, z = self:cabPointFromHead(rest, camParent, pose.x or 0, pose.y or 0, pose.z or 0)
    return self:getSeatLeanOfVehicle(cam, camParent, x - rest.x, y - rest.y, z - rest.z)
end

function VRCompanion:onSeatSetAction()
    if self.is2DViewActive or self:isMenuQuadShown() or self:doesMenuTakeInput() then return end
    local cam = self:getActiveVehicleCamera()
    local model, index = self:getSeatOffsetTarget(cam)
    if model == nil or not self:isGameShowingCamera(cam.cameraNode) then return end
    local pose = self:getResolvedHeadPose()
    if not pose or not pose.isValid then return end
    local leanForward, leanUp, leanRight = self:getHeadLeanInCab(cam, pose)
    local forward, up, right = self:getSeatOffset(model, index)
    local clamped = self:setSeatOffset(model, index, forward + leanForward, up + leanUp, right + leanRight)
    self:recenterView(true)
    self:saveConfiguration()
    if clamped ~= nil then
        local axes = {}
        for i, name in ipairs(clamped) do
            local text = SEAT_AXIS_TEXTS[name]
            axes[i] = text and VRCompanion.getLocalisedText(text.key, name) or name
        end
        showBlinkingWarning(VRCompanion.formatLocalisedText("vrSeatOffset_limitReached", "VR seat position stopped at the %s m limit: %s",
            tostring(self.SEAT_OFFSET_LIMIT), table.concat(axes, ", ")))
    end
end

local SEAT_OFFSETS_KEY = "vrSettings.seatOffsets.seatOffset"

function VRCompanion:saveSeatOffsetSettings(xmlFile, setters)
    local setString, setInt, setFloat = setters.setString, setters.setInt, setters.setFloat
    if type(setString) ~= "function" or type(setInt) ~= "function" or type(setFloat) ~= "function" then return end

    local models = {}
    for model in pairs(self.seatOffsets) do models[#models + 1] = model end
    table.sort(models)
    local entry = 0
    for _, model in ipairs(models) do
        local cameras = {}
        for index in pairs(self.seatOffsets[model]) do cameras[#cameras + 1] = index end
        table.sort(cameras)
        for _, index in ipairs(cameras) do
            local offset = self.seatOffsets[model][index]
            local key = string.format("%s(%d)", SEAT_OFFSETS_KEY, entry)
            setString(xmlFile, key .. "#vehicle", model)
            setInt(xmlFile, key .. "#camera", index)
            setFloat(xmlFile, key .. "#forward", offset.forward)
            setFloat(xmlFile, key .. "#up", offset.up)
            setFloat(xmlFile, key .. "#right", offset.right)
            entry = entry + 1
        end
    end
end

function VRCompanion:loadSeatOffsetSettings(xmlFile, getters)
    local has, getString, getInt, getFloat = getters.has, getters.getString, getters.getInt, getters.getFloat
    self.seatOffsets = {}
    if type(has) ~= "function" or type(getString) ~= "function" or type(getInt) ~= "function" or type(getFloat) ~= "function" then return end

    local entry = 0
    while has(xmlFile, string.format("%s(%d)", SEAT_OFFSETS_KEY, entry)) do
        local key = string.format("%s(%d)", SEAT_OFFSETS_KEY, entry)
        local model, camera = getString(xmlFile, key .. "#vehicle"), getInt(xmlFile, key .. "#camera")
        local forward, up, right = getFloat(xmlFile, key .. "#forward"), getFloat(xmlFile, key .. "#up"), getFloat(xmlFile, key .. "#right")
        if model ~= nil and model ~= "" and VRCompanion.isFiniteNumber(camera) and camera >= 1 and camera == math.floor(camera)
            and VRCompanion.isFiniteNumber(forward) and VRCompanion.isFiniteNumber(up) and VRCompanion.isFiniteNumber(right) then
            self:setSeatOffset(model, camera, forward, up, right)
        end
        entry = entry + 1
    end
end

VRCompanion:registerSubsystem("seatOffset", {
    saveSettings = function(self, xmlFile, xml) self:saveSeatOffsetSettings(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadSeatOffsetSettings(xmlFile, xml) end,
    resetSettings = function(self) self.seatOffsets = {} end,
    consoleCommands = {
        { name = "vrSeatOffset", description = "Move the head's rest position in the cab you are in, in metres from the vehicle's own eye point (saved): vrSeatOffset [<forward> <up> <right> | reset]", method = "consoleCommandSeatOffset" },
    },
})
