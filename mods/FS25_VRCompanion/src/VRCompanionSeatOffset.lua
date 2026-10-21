-- ====================================================================
-- FS25 VR Companion Mod: Seat Offset
--
-- How far the head's rest position in a cab is moved from the vehicle's own in-cab eye point (spec #166, ticket #168). Each in-cab
-- camera of each vehicle model keeps its own: forward, up and right, in metres, each axis clamped to +-0.5 m. See CONTEXT.md.
--
-- PUBLIC SURFACE
--   VRCompanion:getSeatOffset(model, cameraIndex)                 forward, up, right (0, 0, 0 when none is set)
--   VRCompanion:setSeatOffset(model, cameraIndex, f, u, r)        clamps, stores (0 0 0 is dropped), returns the axes it clamped
--   VRCompanion:getCabSeatOffset(cam)                             the offset of the vehicle and in-cab camera `cam` as a translation in the
--                                                                 camera's parent space (x, y, z), 0 0 0 where `cam` is not an in-cab
--                                                                 camera of a vehicle; the cab camera path adds it to the head's rest position
--   VRCompanion:getSeatOffsetTarget(cam)                          model, cameraIndex of an in-cab camera, nil otherwise
--   VRCompanion:onSeatSetAction()                                 the Seat Set Action: adds the head's lean to the offset, recenters, saves
--   Console: vrSeatOffset [<forward> <up> <right> | reset]
--   VRCompanion:registerSeatOffsetConsoleCommands() / unregisterSeatOffsetConsoleCommands()   map load / unload
--
-- IDENTITY: the vehicle model is the vehicle's configFileName (two vehicles of one model, mod vehicles included, share an offset), the
-- camera is its index in the vehicle's camera list (vehicle.spec_enterable.cameras). A passenger camera is another index.
--
-- FRAME: the in-cab camera's parent space, where forward is the vehicle's forward (+Z) and the vehicle's left is +X (the engine's
-- vehicles; the head pose's x is to the right). So the offset (forward f, up u, right r) is the translation (-r, u, f). It is not rotated
-- by look yaw or a Snap Turn. No rotation component.
--
-- The Seat Set Action (VR_SET_SEAT, onSeatSetAction): in an in-cab camera, takes the head's lean as the new rest position (ticket #170).
-- Later tickets add saving to this module.
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.SeatOffset = VRCompanion.SeatOffset or {}

VRCompanion.SEAT_OFFSET_LIMIT = 0.5  -- metres, each axis, either way

-- seatOffsets[model][cameraIndex] = { forward = f, up = u, right = r }; only non-zero offsets are kept
VRCompanion.seatOffsets = VRCompanion.seatOffsets or {}

-- The vehicle and the camera index of an in-cab camera: the camera's own vehicle, else the controlled vehicle, whose camera list holds it
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

-- Each axis is brought within +-SEAT_OFFSET_LIMIT. Gives the clamped forward, up and right and the names of the axes that were clamped
-- (nil when none was).
function VRCompanion:clampSeatOffset(forward, up, right)
    local limit = self.SEAT_OFFSET_LIMIT
    local clamped = nil
    local function clampAxis(name, value)
        if value > limit or value < -limit then
            clamped = clamped or {}
            clamped[#clamped + 1] = name
            return value > 0 and limit or -limit
        end
        return value + 0  -- no negative zero
    end
    forward = clampAxis("forward", forward)
    up = clampAxis("up", up)
    right = clampAxis("right", right)
    return forward, up, right, clamped
end

-- Stores the Seat Offset of one camera of one vehicle model, clamped; 0 0 0 is no offset and is dropped. Gives the names of the axes
-- that were clamped (nil when none was).
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

function VRCompanion:getCabSeatOffset(cam)
    local model, index = self:getSeatOffsetTarget(cam)
    if model == nil then return 0, 0, 0 end
    local forward, up, right = self:getSeatOffset(model, index)
    return -right, up, forward
end

local SEAT_OFFSET_USAGE = "Usage: vrSeatOffset [<forward> <up> <right> | reset] (metres from the vehicle's own eye point, each axis up to 0.5 either way; the in-cab camera you are in)"

local function parseSeatOffsetAxis(text)
    local value = tonumber(text)
    if value == nil or value ~= value or value == math.huge or value == -math.huge then return nil end
    return value
end

local function describeSeatOffset(forward, up, right)
    return string.format("forward %.3f, up %.3f, right %.3f", forward, up, right)
end

-- Console `vrSeatOffset`: prints, sets (absolute, in metres) or resets the Seat Offset of the vehicle and in-cab camera being shown
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
    -- The next camera update places the head with it; the view jumps, so the upscaler drops its history
    self:signalDiscontinuityFlush()

    local text = reset and "Seat offset reset to the vehicle's own eye point."
        or string.format("Seat offset set to %s m.", describeSeatOffset(self:getSeatOffset(model, index)))
    if clamped ~= nil then
        text = string.format("%s Clamped to %s m: %s.", text, tostring(self.SEAT_OFFSET_LIMIT), table.concat(clamped, ", "))
    end
    print("[FS25_VR] " .. text)
    return text
end

-- A short blinking message on screen, when the mission can show one
local function showBlinkingWarning(text)
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function() mission:showBlinkingWarning(text, 2500) end)
    end
end

-- How far the head is from where it would rest, in the in-cab camera's parent space (x, y, z): the head pose's position carried
-- into the cab the way the cab camera path does (through the head tracking node's rotation, look yaw included, else the camera's
-- own eye point turned by the view's yaw).
function VRCompanion:getHeadLeanInCab(cam, pose)
    local getParentFn = getParent or (_G and _G.getParent)
    local camParent = 0
    if type(getParentFn) == "function" then camParent = getParentFn(cam.cameraNode) end
    local lookYaw = (type(self.getVehicleLookYaw) == "function" and self:getVehicleLookYaw(cam)) or 0
    local headYaw = self:normalizeAngle((cam.origRotY or math.pi) + lookYaw)
    local rest = self:getCabRestPose(cam, camParent, headYaw, lookYaw)
    local x, y, z = self:cabPointFromHead(rest, camParent, pose.x or 0, pose.y or 0, pose.z or 0)
    return x - rest.x, y - rest.y, z - rest.z
end

-- The Seat Set Action: in an in-cab camera, makes where the head is now the head's rest position (see CONTEXT.md). The lean is added
-- to the Seat Offset, each axis clamped; the view then recenters as the Recenter Action does, always re-zeroing the tracking space,
-- so the head rests where it leaned. The settings are saved. Only a clamped axis is shown: a short blinking warning.
function VRCompanion:onSeatSetAction()
    -- Only the head-tracked view of an in-cab camera, with no menu shown: anywhere else it is a no-op
    if self.is2DViewActive or self:doesMenuTakeInput() then return end
    local cam = self:getActiveVehicleCamera()
    local model, index = self:getSeatOffsetTarget(cam)
    if model == nil or not self:isGameShowingCamera(cam.cameraNode) then return end
    local pose = self:getResolvedHeadPose()
    if not pose or not pose.isValid then return end
    local dx, dy, dz = self:getHeadLeanInCab(cam, pose)
    local forward, up, right = self:getSeatOffset(model, index)
    local clamped = self:setSeatOffset(model, index, forward + dz, up + dy, right - dx)
    self:recenterView(true)
    self:saveConfiguration()
    if clamped ~= nil then
        showBlinkingWarning(string.format("VR seat position stopped at the %s m limit: %s", tostring(self.SEAT_OFFSET_LIMIT), table.concat(clamped, ", ")))
    end
end

function VRCompanion:registerSeatOffsetConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrSeatOffset", "Move the head's rest position in the cab you are in, in metres from the vehicle's own eye point (this session only): vrSeatOffset [<forward> <up> <right> | reset]", "consoleCommandSeatOffset", self)
end

function VRCompanion:unregisterSeatOffsetConsoleCommands()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrSeatOffset")
    end
end

VRCompanion.SeatOffset.getCabSeatOffset = function(self, ...) return VRCompanion.getCabSeatOffset(VRCompanion, ...) end
VRCompanion.SeatOffset.onSeatSetAction = function(self, ...) return VRCompanion.onSeatSetAction(VRCompanion, ...) end
VRCompanion.SeatOffset.consoleCommandSeatOffset = function(self, ...) return VRCompanion.consoleCommandSeatOffset(VRCompanion, ...) end

return VRCompanion.SeatOffset
