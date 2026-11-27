VRCompanion = VRCompanion or {}

VRCompanion.MOTION_HAND_LEFT = 0
VRCompanion.MOTION_HAND_RIGHT = 1
VRCompanion.pointerHand = nil

VRCompanion.POINTER_CONTROL_MOUSE = "mouse"
VRCompanion.POINTER_CONTROL_RAY = "ray"
VRCompanion.POINTER_MOUSE_MOVE_EPSILON = 0.0005

VRCompanion.DEFAULT_MOTION_CONTROLLERS_ENABLED = true
VRCompanion.motionControllersEnabled = VRCompanion.DEFAULT_MOTION_CONTROLLERS_ENABLED

VRCompanion.MOTION_CONTROLLER_IN_USE_MS = 3000
VRCompanion.MOTION_CONTROLLER_MOVE_DISTANCE = 0.01
VRCompanion.MOTION_CONTROLLER_MOVE_ANGLE = 0.035

VRCompanion.DEFAULT_POINTER_CONE_DEGREES = 5
VRCompanion.MIN_POINTER_CONE_DEGREES = 0.5
VRCompanion.MAX_POINTER_CONE_DEGREES = 45
VRCompanion.pointerConeDegrees = VRCompanion.DEFAULT_POINTER_CONE_DEGREES

VRCompanion.POINTER_TRIGGER_PRESS = 0.6
VRCompanion.POINTER_TRIGGER_RELEASE = 0.4
VRCompanion.POINTER_RAY_LENGTH = 3

local HANDS = { 0, 1 }

local function newState()
    return {
        hands = {},      -- [hand] = table filled in place by VRBridge.getHandState
        motion = {},     -- [hand] = { ref = {x,y,z,qx,qy,qz,qw} | nil, lastMovedMs = number | nil }
        inUse = {},      -- [hand] = boolean
        triggerDown = {},     -- [hand] = trigger is pulled (with hysteresis)
        triggerPressed = {},  -- [hand] = it went down this update
        triggerConsumed = {},
        ray = { valid = false, ox = 0, oy = 0, oz = 0, dx = 0, dy = 0, dz = -1 },
        rayInHead = { valid = false },
        fingerLogged = {},    -- [hand] = the line about the ray starting at the index finger was logged this map
        headScratch = nil,
        pointerHand = nil,
        ignoredCleared = false,  -- Motion Controller input was ignored and the cached hands were already cleared
        bridgeMissing = false,
        pointerOwner = VRCompanion.POINTER_CONTROL_MOUSE,
        mousePosition = { seen = false, x = 0, y = 0 },
        movedThisUpdate = {},
    }
end

local state = newState()

local function bridgeCall()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.getHandState) == "function" then
        return bridge.getHandState
    end
    return nil
end

local function untrackedHand()
    return {
        tracked = false,
        x = 0, y = 0, z = 0, pitch = 0, yaw = 0, roll = 0, qx = 0, qy = 0, qz = 0, qw = 1,
        isValid = false, isPositionTracked = false, isOrientationTracked = false,
        gripPoseTracked = false,
        gripPoseX = 0, gripPoseY = 0, gripPoseZ = 0, gripPosePitch = 0, gripPoseYaw = 0, gripPoseRoll = 0,
        gripPoseQx = 0, gripPoseQy = 0, gripPoseQz = 0, gripPoseQw = 1,
        trigger = 0, grip = 0, thumbX = 0, thumbY = 0,
        thumbClick = false, buttonLower = false, buttonUpper = false, menu = false,
    }
end

local function initHands()
    for _, hand in ipairs(HANDS) do
        state.hands[hand] = untrackedHand()
        state.motion[hand] = {}
        state.inUse[hand] = false
        state.triggerDown[hand] = false
        state.triggerPressed[hand] = false
        state.triggerConsumed[hand] = false
    end
end
initHands()

function VRCompanion:getHandState(hand)
    return state.hands[hand] or state.hands[0]
end

function VRCompanion:isHandInUse(hand)
    return state.inUse[hand] == true
end

function VRCompanion:getPointerHand()
    return state.pointerHand
end

local function movedFromReference(motion, hand)
    local ref = motion.ref
    if ref == nil then
        motion.ref = { x = hand.x, y = hand.y, z = hand.z, qx = hand.qx, qy = hand.qy, qz = hand.qz, qw = hand.qw }
        return false
    end
    local dx, dy, dz = hand.x - ref.x, hand.y - ref.y, hand.z - ref.z
    local moved = math.sqrt(dx * dx + dy * dy + dz * dz) > VRCompanion.MOTION_CONTROLLER_MOVE_DISTANCE
    if not moved then
        local dot = math.abs(hand.qx * ref.qx + hand.qy * ref.qy + hand.qz * ref.qz + hand.qw * ref.qw)
        if dot > 1 then dot = 1 end
        moved = 2 * math.acos(dot) > VRCompanion.MOTION_CONTROLLER_MOVE_ANGLE
    end
    if moved then
        ref.x, ref.y, ref.z, ref.qx, ref.qy, ref.qz, ref.qw = hand.x, hand.y, hand.z, hand.qx, hand.qy, hand.qz, hand.qw
    end
    return moved
end

local function isHoldingSteeringWheel(self, hand)
    return self:isHandHoldingSteeringWheel(hand) == true
end

local function isHoldingHandTool(self, hand)
    return not self:isMenuQuadShown() and self:isHandHoldingHandTool(hand) == true
end

local function isCarryingHeldObject(self, hand)
    return not self:isMenuQuadShown() and self:isHandCarryingHeldObject(hand) == true
end

local function isPointerCandidate(self, hand)
    return state.inUse[hand] and not isHoldingSteeringWheel(self, hand) and not isHoldingHandTool(self, hand)
        and not isCarryingHeldObject(self, hand)
end

local function choosePointerHand(self)
    local left = isPointerCandidate(self, 0)
    local right = isPointerCandidate(self, 1)
    if left and not right then return 0 end
    if right and not left then return 1 end
    if not (left and right) then return nil end
    local previous = state.pointerHand
    for _, hand in ipairs(HANDS) do
        if hand ~= previous and state.triggerPressed[hand] then
            state.triggerPressed[hand] = false
            state.triggerConsumed[hand] = true
            return hand
        end
        if hand ~= previous and self:isObjectGrabSqueeze(hand) == true then
            return hand
        end
    end
    if previous == nil then
        local toolHand = type(self.getToolHand) == "function" and self:getToolHand() or nil
        if toolHand == 0 or toolHand == 1 then return toolHand end
        return self.MOTION_HAND_RIGHT
    end
    return previous
end

local function clearHands()
    local untracked = untrackedHand()
    for _, hand in ipairs(HANDS) do
        local cached = state.hands[hand]
        for key, value in pairs(untracked) do
            cached[key] = value
        end
        state.motion[hand] = {}
        state.inUse[hand] = false
        state.triggerDown[hand] = false
        state.triggerPressed[hand] = false
        state.triggerConsumed[hand] = false
    end
    state.pointerHand = nil
    state.ray.valid = false
    state.ray.visible = false
    VRCompanion.pointerHand = nil
end

local POINTER_OWNER_LOG_LIMIT = 30
local function setPointerOwner(self, owner, why)
    if state.pointerOwner ~= owner then
        state.ownerChanges = (state.ownerChanges or 0) + 1
        if state.ownerChanges <= POINTER_OWNER_LOG_LIMIT then
            print(string.format("[FS25_VR] Pointer control: %s (%s)", owner, why))
        end
    end
    state.pointerOwner = owner
end

function VRCompanion:notePointerRayMoved()
    setPointerOwner(self, self.POINTER_CONTROL_RAY, "the Pointer Hand moved")
end

function VRCompanion:notePointerMouseMoved(why)
    setPointerOwner(self, self.POINTER_CONTROL_MOUSE, why or "the desk mouse moved")
end

function VRCompanion:getPointerOwner()
    if state.pointerOwner == self.POINTER_CONTROL_RAY and state.pointerHand ~= nil
        and self.motionControllersEnabled ~= false then
        return self.POINTER_CONTROL_RAY
    end
    return self.POINTER_CONTROL_MOUSE
end

function VRCompanion:isPointerRayInControl()
    return self:getPointerOwner() == self.POINTER_CONTROL_RAY
end

local function noteDeskMouse(self, x, y, isEvent)
    if type(x) ~= "number" or type(y) ~= "number" then return end
    local last = state.mousePosition
    local moved = isEvent
    if last.seen then
        local epsilon = self.POINTER_MOUSE_MOVE_EPSILON
        moved = moved or math.abs(x - last.x) > epsilon or math.abs(y - last.y) > epsilon
    end
    last.x, last.y, last.seen = x, y, true
    if moved then
        self:notePointerMouseMoved(isEvent and "mouse event" or "mouse position sampled")
    end
end

function VRCompanion:mouseEvent(posX, posY, isDown, isUp, button)
    noteDeskMouse(self, posX, posY, true)
end

local function sampleDeskMouse(self)
    local inputBinding = g_inputBinding or (_G and _G.g_inputBinding)
    if inputBinding == nil or type(inputBinding.getMousePosition) ~= "function" then return end
    local ok, x, y = pcall(inputBinding.getMousePosition, inputBinding)
    if ok then
        noteDeskMouse(self, x, y, false)
    end
end

local function settleInteractiveControl(self, dt)
    self:updateInteractiveControl(dt)
    self:updateAnimatedObjects(dt, true)
    self:updateManualAttach(dt, true)
end

local function updatePointerUsers(self, dt)
    self:updateMenuPointer(dt)
    self:updateVirtualHands(dt)
    self:updateVirtualGamepadUse(dt)
end

function VRCompanion:updateMotionControllers(dt)
    self:updateVirtualGamepad(dt)
    sampleDeskMouse(self)
    if self.motionControllersEnabled == false then
        if not state.ignoredCleared then
            clearHands()
            state.ignoredCleared = true
        end
        self:updateWheelGrab(dt)
        self:updateHandTools(dt)
        self:updateObjectGrab(dt)
        settleInteractiveControl(self, dt)
        updatePointerUsers(self, dt)
        return
    end
    state.ignoredCleared = false

    local getHandState = bridgeCall()
    local bridgeWasMissing = state.bridgeMissing
    state.bridgeMissing = (getHandState == nil)
    if getHandState == nil then
        state.ray.valid = false
        state.ray.visible = false
        if not bridgeWasMissing then
            clearHands()
        end
        self:updateWheelGrab(dt)
        self:updateHandTools(dt)
        self:updateObjectGrab(dt)
        settleInteractiveControl(self, dt)
        updatePointerUsers(self, dt)
        return
    end
    local now = self:getCurrentTimeMs()
    for _, hand in ipairs(HANDS) do
        state.movedThisUpdate[hand] = false
    end
    for _, hand in ipairs(HANDS) do
        local result = getHandState(hand, state.hands[hand])
        if type(result) == "table" then
            state.hands[hand] = result
        end

        local current = state.hands[hand]
        local motion = state.motion[hand]
        if current.tracked == true then
            if movedFromReference(motion, current) then
                state.movedThisUpdate[hand] = true
                if now ~= nil then
                    motion.lastMovedMs = now
                end
            end
        else
            motion.ref = nil
        end
        local inUse = false
        if current.tracked == true and now ~= nil and motion.lastMovedMs ~= nil then
            local elapsed = now - motion.lastMovedMs
            if elapsed < 0 then
                motion.lastMovedMs = nil
            else
                inUse = elapsed < self.MOTION_CONTROLLER_IN_USE_MS
            end
        end
        state.inUse[hand] = inUse

        local wasDown = state.triggerDown[hand]
        local value = current.trigger or 0
        local isDown = value >= (wasDown and self.POINTER_TRIGGER_RELEASE or self.POINTER_TRIGGER_PRESS)
        if current.tracked ~= true then isDown = false end
        state.triggerDown[hand] = isDown
        if not isDown then state.triggerConsumed[hand] = false end
        state.triggerPressed[hand] = isDown and not wasDown
    end
    self:updateWheelGrab(dt)
    self:updateObjectGrab(dt)
    for _, hand in ipairs(HANDS) do
        if isHoldingSteeringWheel(self, hand) or isCarryingHeldObject(self, hand)
            or self:isHandKeptInUseByHandTool(hand) == true then
            state.inUse[hand] = true
            if now ~= nil then state.motion[hand].lastMovedMs = now end
        end
    end
    self:updateHandTools(dt)
    state.pointerHand = choosePointerHand(self)
    self.pointerHand = state.pointerHand
    if state.pointerHand ~= nil and state.movedThisUpdate[state.pointerHand] then
        self:notePointerRayMoved()
    end
    self:updatePointerRay(dt)
    updatePointerUsers(self, dt)
end

function VRCompanion:resetMotionControllerMovement()
    for _, hand in ipairs(HANDS) do
        state.motion[hand].ref = nil
    end
end

local qrotate = VRCompanion.rotateVectorByQuaternion

local function headIsPlacedInTheWorld(self)
    if self:isMenuQuadShown() or self.is2DViewActive or self.trackedCameraShown == false then return false end
    if self:isPassThroughMode() then return false end
    return true
end

local function qmultiply(ax, ay, az, aw, bx, by, bz, bw)
    return aw * bx + ax * bw + ay * bz - az * by,
           aw * by - ax * bz + ay * bw + az * bx,
           aw * bz + ax * by - ay * bx + az * bw,
           aw * bw - ax * bx - ay * by - az * bz
end

VRCompanion.multiplyQuaternions = qmultiply

function VRCompanion:isHeadPlacedInTheWorld()
    return headIsPlacedInTheWorld(self)
end

function VRCompanion:describeHeadPlacement()
    return string.format("menu=%s, 2D=%s, trackedCameraShown=%s, passThrough=%s",
        tostring(self:isMenuQuadShown()), tostring(self.is2DViewActive), tostring(self.trackedCameraShown),
        tostring(self:isPassThroughMode()))
end

function VRCompanion:headRelativePoseToWorld(x, y, z, qx, qy, qz, qw, out)
    local listener = self.cyclopeanAudioListenerNode
    local getWorldTranslationFn = getWorldTranslation or (_G and _G.getWorldTranslation)
    local getWorldQuaternionFn = getWorldQuaternion or (_G and _G.getWorldQuaternion)
    local bridge = VRBridge or (_G and _G.VRBridge)
    if listener == nil or listener == 0 then return false, "the Cyclopean Audio Listener Node does not exist" end
    if not headIsPlacedInTheWorld(self) then
        return false, "the head is not placed in the world (" .. self:describeHeadPlacement() .. ")"
    end
    if type(getWorldTranslationFn) ~= "function" or type(getWorldQuaternionFn) ~= "function" then
        return false, "getWorldTranslation / getWorldQuaternion are not available"
    end
    if bridge == nil or type(bridge.getHeadPose) ~= "function" then return false, "VRBridge.getHeadPose is not available" end
    state.headScratch = state.headScratch or {}
    local head = bridge.getHeadPose(state.headScratch)
    if type(head) ~= "table" then return false, "VRBridge.getHeadPose returned no table" end
    state.headScratch = head
    if head.isValid ~= true or head.qw == nil then return false, "the head pose is not valid" end

    local hx, hy, hz, hw = -head.qx, -head.qy, -head.qz, head.qw
    local offX, offY, offZ = qrotate(hx, hy, hz, hw, x - head.x, y - head.y, z - head.z)
    local rx, ry, rz, rw = qmultiply(hx, hy, hz, hw, qx, qy, qz, qw)

    local lx, ly, lz = getWorldTranslationFn(listener)
    local lqx, lqy, lqz, lqw = getWorldQuaternionFn(listener)
    if lx == nil or lqw == nil then return false, "the listener node has no world pose" end
    local wx, wy, wz = qrotate(lqx, lqy, lqz, lqw, offX, offY, offZ)
    local wqx, wqy, wqz, wqw = qmultiply(lqx, lqy, lqz, lqw, rx, ry, rz, rw)
    local length = math.sqrt(wqx * wqx + wqy * wqy + wqz * wqz + wqw * wqw)
    if length < 1e-6 then return false, "the pose has no rotation" end
    out.x, out.y, out.z = lx + wx, ly + wy, lz + wz
    out.qx, out.qy, out.qz, out.qw = wqx / length, wqy / length, wqz / length, wqw / length
    return true
end

VRCompanion.POINTER_RAY_FOLLOWS_FINGER_DIRECTION = true

local SOURCE_FINGER = "index finger"
local SOURCE_AIM = "aim pose"

local function pointerOffset(self, hand)
    local offsets = self.VIRTUAL_HAND_POINTER
    if type(offsets) ~= "table" then return nil end
    local offset = hand == self.MOTION_HAND_RIGHT and offsets.right or offsets.left
    if type(offset) ~= "table" or type(offset.tip) ~= "table" or type(offset.direction) ~= "table" then return nil end
    return offset
end

local function fingerRay(self, offset, gx, gy, gz, gqx, gqy, gqz, gqw, aqx, aqy, aqz, aqw)
    local tip, direction = offset.tip, offset.direction
    local tx, ty, tz = qrotate(gqx, gqy, gqz, gqw, tip.x, tip.y, tip.z)
    local dx, dy, dz
    if self.POINTER_RAY_FOLLOWS_FINGER_DIRECTION ~= false then
        dx, dy, dz = qrotate(gqx, gqy, gqz, gqw, direction.x, direction.y, direction.z)
    else
        dx, dy, dz = qrotate(aqx, aqy, aqz, aqw, 0, 0, -1)
    end
    return gx + tx, gy + ty, gz + tz, dx, dy, dz
end

function VRCompanion:getPointerRayInTrackingSpace(hand)
    local pose = state.hands[hand]
    if pose == nil or pose.tracked ~= true then return nil end
    local offset = pointerOffset(self, hand)
    local ox, oy, oz, dx, dy, dz, source
    if offset ~= nil and pose.gripPoseTracked == true then
        ox, oy, oz, dx, dy, dz = fingerRay(self, offset, pose.gripPoseX, pose.gripPoseY, pose.gripPoseZ,
            pose.gripPoseQx, pose.gripPoseQy, pose.gripPoseQz, pose.gripPoseQw, pose.qx, pose.qy, pose.qz, pose.qw)
        source = SOURCE_FINGER
    else
        ox, oy, oz = pose.x, pose.y, pose.z
        dx, dy, dz = qrotate(pose.qx, pose.qy, pose.qz, pose.qw, 0, 0, -1)
        source = SOURCE_AIM
    end
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 0.5 then return nil end
    return ox, oy, oz, dx / length, dy / length, dz / length, source
end

local function fingerAimAngleDegrees(self, hand)
    local pose = state.hands[hand]
    local offset = pointerOffset(self, hand)
    if offset == nil or pose.tracked ~= true or pose.gripPoseTracked ~= true then return nil end
    local direction = offset.direction
    local fx, fy, fz = qrotate(pose.gripPoseQx, pose.gripPoseQy, pose.gripPoseQz, pose.gripPoseQw, direction.x, direction.y, direction.z)
    local ax, ay, az = qrotate(pose.qx, pose.qy, pose.qz, pose.qw, 0, 0, -1)
    local lengths = math.sqrt(fx * fx + fy * fy + fz * fz) * math.sqrt(ax * ax + ay * ay + az * az)
    if lengths < 1e-6 then return nil end
    local cosine = math.max(-1, math.min(1, (fx * ax + fy * ay + fz * az) / lengths))
    return math.deg(math.acos(cosine))
end

local function placePointerRay(self, hand)
    local ray = state.ray
    local pose = state.hands[hand]
    if pose.tracked ~= true then return false, "the Pointer Hand is not tracked" end
    local offset = pointerOffset(self, hand)
    local fromFinger = offset ~= nil and pose.gripPoseTracked == true
    local ok, reason

    state.rayPose = state.rayPose or {}
    local aim = state.rayPose
    if not fromFinger or self.POINTER_RAY_FOLLOWS_FINGER_DIRECTION == false then
        ok, reason = self:headRelativePoseToWorld(pose.x, pose.y, pose.z, pose.qx, pose.qy, pose.qz, pose.qw, aim)
        if not ok then return false, reason end
    end

    local ox, oy, oz, dx, dy, dz
    if fromFinger then
        state.rayGripPose = state.rayGripPose or {}
        local grip = state.rayGripPose
        ok, reason = self:headRelativePoseToWorld(pose.gripPoseX, pose.gripPoseY, pose.gripPoseZ,
            pose.gripPoseQx, pose.gripPoseQy, pose.gripPoseQz, pose.gripPoseQw, grip)
        if not ok then return false, reason end
        ox, oy, oz, dx, dy, dz = fingerRay(self, offset, grip.x, grip.y, grip.z, grip.qx, grip.qy, grip.qz, grip.qw,
            aim.qx, aim.qy, aim.qz, aim.qw)
    else
        ox, oy, oz = aim.x, aim.y, aim.z
        dx, dy, dz = qrotate(aim.qx, aim.qy, aim.qz, aim.qw, 0, 0, -1)
    end
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 0.5 then return false, "the hand's aim direction is degenerate" end
    ray.ox, ray.oy, ray.oz = ox, oy, oz
    ray.dx, ray.dy, ray.dz = dx / length, dy / length, dz / length
    return true, fromFinger and SOURCE_FINGER or SOURCE_AIM
end

local function noteRayInHead(self, ray)
    local inHead = state.rayInHead
    local listener = self.cyclopeanAudioListenerNode
    local worldToLocalFn = worldToLocal or (_G and _G.worldToLocal)
    if listener == nil or listener == 0 or type(worldToLocalFn) ~= "function" then return end
    inHead.ox, inHead.oy, inHead.oz = worldToLocalFn(listener, ray.ox, ray.oy, ray.oz)
    inHead.ex, inHead.ey, inHead.ez = worldToLocalFn(listener, ray.ex, ray.ey, ray.ez)
    inHead.valid = true
end

function VRCompanion:updatePointerRay(dt)
    local ray = state.ray
    ray.valid = false
    ray.visible = false
    state.rayInHead.valid = false
    local hand = state.pointerHand
    local reason = "there is no Pointer Hand (no hand in use)"
    local source = nil
    if hand ~= nil then
        ray.valid, reason = placePointerRay(self, hand)
        if ray.valid then
            source = reason
            reason = source == SOURCE_FINGER and "placed" or "placed from the aim pose (the grip pose is not tracked)"
        end
    end
    if reason ~= state.rayReason then
        state.rayReason = reason
        print("[FS25_VR] Pointer Ray: " .. reason)
    end
    if source == SOURCE_FINGER and not state.fingerLogged[hand] then
        state.fingerLogged[hand] = true
        local angle = fingerAimAngleDegrees(self, hand)
        print(string.format("[FS25_VR] Pointer Ray: from the index finger (%s hand), the finger points %.1f degrees from the aim direction; the ray %s",
            hand == self.MOTION_HAND_RIGHT and "right" or "left", angle or 0,
            self.POINTER_RAY_FOLLOWS_FINGER_DIRECTION ~= false and "runs along the finger"
                or "keeps the aim direction (POINTER_RAY_FOLLOWS_FINGER_DIRECTION is false)"))
    end
    local hasTarget, hitX, hitY, hitZ = false, nil, nil, nil
    local clickCos = self:peekInteractiveControlTarget()
    local onCouplingPoint, couplingX, couplingY, couplingZ = self:updateManualAttach(dt, false, clickCos)
    hasTarget, hitX, hitY, hitZ = self:updateInteractiveControl(dt, onCouplingPoint)
    if onCouplingPoint then hasTarget, hitX, hitY, hitZ = true, couplingX, couplingY, couplingZ end
    local grabbable, grabX, grabY, grabZ, grabDistance = false, nil, nil, nil, nil
    if hitX == nil then
        grabbable, grabX, grabY, grabZ, grabDistance = self:peekObjectGrabRayTarget()
    end
    local onAnimatedObject, animatedX, animatedY, animatedZ = self:updateAnimatedObjects(dt, hitX ~= nil, grabbable and grabDistance or nil)
    if onAnimatedObject then
        hasTarget, hitX, hitY, hitZ = true, animatedX, animatedY, animatedZ
        grabbable = false
    end
    self:settleObjectGrabRayTarget(grabbable)
    if grabbable then
        hasTarget, hitX, hitY, hitZ = true, grabX, grabY, grabZ
    end
    if ray.valid and hasTarget then
        ray.visible = true
        if hitX ~= nil then
            ray.ex, ray.ey, ray.ez = hitX, hitY, hitZ
        else
            local length = self.POINTER_RAY_LENGTH
            ray.ex, ray.ey, ray.ez = ray.ox + ray.dx * length, ray.oy + ray.dy * length, ray.oz + ray.dz * length
        end
        noteRayInHead(self, ray)
    end
end

function VRCompanion:getPointerRay()
    if state.ray.valid then return state.ray end
    return nil
end

function VRCompanion:getPointerTrigger()
    local hand = state.pointerHand
    if hand == nil or state.triggerConsumed[hand] then return false, false end
    return state.triggerPressed[hand] == true, state.triggerDown[hand] == true
end

VRCompanion.HAPTIC_SELECT_MS = 25
VRCompanion.HAPTIC_SELECT_STRENGTH = 0.25
VRCompanion.HAPTIC_CLICK_MS = 45
VRCompanion.HAPTIC_CLICK_STRENGTH = 0.6

function VRCompanion:pulseHand(hand, durationMs, strength)
    if hand == nil or self.motionControllersEnabled == false then return false end
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge == nil or type(bridge.pulseHaptic) ~= "function" then return false end
    local ok, played = pcall(bridge.pulseHaptic, hand, durationMs, strength)
    return ok and played == true
end

function VRCompanion:pulsePointerHand(durationMs, strength)
    return self:pulseHand(state.pointerHand, durationMs, strength)
end

function VRCompanion:onClickPointEvent(kind, clickPoint)
    if kind == "selected" then
        if clickPoint ~= nil then
            self:pulsePointerHand(self.HAPTIC_SELECT_MS, self.HAPTIC_SELECT_STRENGTH)
        end
    elseif kind == "clicked" then
        self:pulsePointerHand(self.HAPTIC_CLICK_MS, self.HAPTIC_CLICK_STRENGTH)
    end
end

function VRCompanion:placePointerRayForRender()
    local inHead = state.rayInHead
    if not state.ray.visible or not inHead.valid then return end
    local listener = self.cyclopeanAudioListenerNode
    local localToWorldFn = localToWorld or (_G and _G.localToWorld)
    if listener == nil or listener == 0 or type(localToWorldFn) ~= "function" then return end
    local ox, oy, oz = localToWorldFn(listener, inHead.ox, inHead.oy, inHead.oz)
    local ex, ey, ez = localToWorldFn(listener, inHead.ex, inHead.ey, inHead.ez)
    local drawLine = drawDebugLine or (_G and _G.drawDebugLine)
    local drawPoint = drawDebugPoint or (_G and _G.drawDebugPoint)
    local r, g, b = 0.25, 0.85, 1.0
    if type(drawLine) == "function" then
        drawLine(ox, oy, oz, r, g, b, ex, ey, ez, r, g, b, false)
    end
    if type(drawPoint) == "function" then
        drawPoint(ex, ey, ez, 1.0, 1.0, 1.0, 1.0, false)
    end
end

function VRCompanion:draw()
    if self.perfTracing then
        self:perfTraceBegin("modDraw")
        self:drawFrame()
        self:perfTraceEnd("modDraw")
        return
    end
    self:drawFrame()
end

function VRCompanion:drawFrame()
    self:drawVirtualHands()
end

local SETTING_KEY_ENABLED = "vrSettings.motionControllers"

function VRCompanion.parseOnOffArgument(arg)
    local low = string.lower(tostring(arg))
    if low == "on" or low == "true" or low == "1" then return true end
    if low == "off" or low == "false" or low == "0" then return false end
    return nil
end

function VRCompanion:setMotionControllersEnabled(enabled, save)
    self.motionControllersEnabled = (enabled ~= false)
    if self.motionControllersEnabled then
        state.ignoredCleared = false
    end
    if save then
        self:saveConfiguration()
    end
end

function VRCompanion:saveMotionControllerSettings(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY_ENABLED, self.motionControllersEnabled ~= false)
    end
    self:savePointerConeSetting(xmlFile, xml)
end

function VRCompanion:loadMotionControllerSettings(xmlFile, xml)
    local loaded = nil
    if type(xml.getBool) == "function" then
        loaded = xml.getBool(xmlFile, SETTING_KEY_ENABLED)
    end
    if loaded == nil and type(xml.getString) == "function" then
        local str = xml.getString(xmlFile, SETTING_KEY_ENABLED)
        if str ~= nil then loaded = VRCompanion.parseOnOffArgument(str) end
    end
    if loaded ~= nil then
        self.motionControllersEnabled = loaded
    end
    self:loadPointerConeSetting(xmlFile, xml)
end

local SETTING_KEY_CONE = "vrSettings.pointerConeDegrees"

local function validPointerCone(self, degrees)
    return VRCompanion.isFiniteNumber(degrees) and degrees >= self.MIN_POINTER_CONE_DEGREES and degrees <= self.MAX_POINTER_CONE_DEGREES
end

function VRCompanion:savePointerConeSetting(xmlFile, xml)
    if type(xml.setFloat) == "function" then
        xml.setFloat(xmlFile, SETTING_KEY_CONE, self.pointerConeDegrees)
    end
end

function VRCompanion:loadPointerConeSetting(xmlFile, xml)
    if type(xml.getFloat) ~= "function" then return end
    local degrees = xml.getFloat(xmlFile, SETTING_KEY_CONE)
    if validPointerCone(self, degrees) then
        self.pointerConeDegrees = degrees
    end
end

function VRCompanion:consoleCommandSetPointerCone(arg)
    local usage = string.format("Usage: vrPointerCone <degrees, %s to %s> (saved)",
        tostring(self.MIN_POINTER_CONE_DEGREES), tostring(self.MAX_POINTER_CONE_DEGREES))
    if arg == nil or arg == "" then
        return string.format("Pointer Ray cone: %s degrees (default %s). %s",
            tostring(self.pointerConeDegrees), tostring(self.DEFAULT_POINTER_CONE_DEGREES), usage)
    end
    local degrees = VRCompanion.parseFiniteNumber(arg)
    if not validPointerCone(self, degrees) then
        return usage
    end
    self.pointerConeDegrees = degrees
    self:saveConfiguration()
    print(string.format("[FS25_VR] Pointer Ray cone set to: %s degrees", tostring(degrees)))
    return string.format("Pointer Ray cone set to: %s degrees (Saved)", tostring(degrees))
end

function VRCompanion:consoleCommandSetMotionControllers(arg)
    local enabled = VRCompanion.parseOnOffArgument(arg)
    if arg == nil or arg == "" or string.lower(tostring(arg)) == "status" then
        return string.format("Motion Controllers are %s. Usage: vrMotionControllers <on|off> (saved)",
            self.motionControllersEnabled and "on" or "off")
    end
    if enabled == nil then
        return "Usage: vrMotionControllers <on|off>"
    end
    self:setMotionControllersEnabled(enabled, true)
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(VRCompanion.formatLocalisedText("vrMotionControllers_set", "VR Motion Controllers: %s (Saved)",
                VRCompanion.onOffText(enabled)), 2500)
        end)
    end
    print(string.format("[FS25_VR] Motion Controllers set to: %s", enabled and "on" or "off"))
    return string.format("Motion Controllers set to: %s (Saved)", enabled and "on" or "off")
end

function VRCompanion:getMotionControllerStatus()
    local virtualGamepad = " | Virtual Gamepad: " .. self:getVirtualGamepadStatus()
    if self.motionControllersEnabled == false then
        return "Motion Controllers: off (Motion Controller input is ignored) | Left: ignored | Right: ignored | Pointer Hand: none" .. virtualGamepad
    end
    if state.bridgeMissing then
        return "Motion Controllers: on | VRBridge.getHandState is not available (no Injector, or an older one) | Pointer Hand: none" .. virtualGamepad
    end
    local function describe(hand)
        return string.format("%s, %s",
            state.hands[hand].tracked == true and "tracked" or "not tracked",
            state.inUse[hand] and "in use" or "not in use")
    end
    local pointer = state.pointerHand
    return string.format("Motion Controllers: on | Left: %s | Right: %s | Pointer Hand: %s%s",
        describe(0), describe(1), pointer == 0 and "left" or pointer == 1 and "right" or "none", virtualGamepad)
end

function VRCompanion:consoleCommandMotionControllerStatus()
    return self:getMotionControllerStatus()
end

local function describeHandPoses(self, hand)
    local pose = state.hands[hand]
    local aim = "aim not tracked"
    if pose.tracked == true then
        aim = string.format("aim (%.2f, %.2f, %.2f) tracked", pose.x, pose.y, pose.z)
    end
    local grip = "grip not tracked"
    if pose.gripPoseTracked == true then
        grip = string.format("grip (%.2f, %.2f, %.2f) tracked", pose.gripPoseX, pose.gripPoseY, pose.gripPoseZ)
    end
    local angle = fingerAimAngleDegrees(self, hand)
    local finger = angle ~= nil and string.format(", finger %.1f degrees from aim", angle) or ""
    return aim .. ", " .. grip .. finger
end

function VRCompanion:getPointerDiagnostics()
    local icText = self:getInteractiveControlDiagnostics()
    local virtualHandsText = " | " .. self:getVirtualHandsDiagnostics()
    virtualHandsText = virtualHandsText .. " | " .. self:getWheelGrabDiagnostics()
    virtualHandsText = virtualHandsText .. " | " .. self:getHandToolsDiagnostics()
    virtualHandsText = virtualHandsText .. " | " .. self:getObjectGrabDiagnostics()
    virtualHandsText = virtualHandsText .. " | " .. self:getAnimatedObjectsDiagnostics()
    virtualHandsText = virtualHandsText .. " | " .. self:getManualAttachDiagnostics()
    local pointer = state.pointerHand
    local trigger = pointer ~= nil and state.hands[pointer].trigger or 0
    return string.format("Pointer Ray: %s | Pointer Hand: %s | trigger: %.2f | pointer controlled by: %s | %s | Left hand: %s | Right hand: %s%s",
        tostring(state.rayReason or "not updated yet"),
        pointer == 0 and "left" or pointer == 1 and "right" or "none", trigger,
        self:getPointerOwner(), icText, describeHandPoses(self, 0), describeHandPoses(self, 1), virtualHandsText)
end

function VRCompanion:consoleCommandPointerDiagnostics()
    return self:getPointerDiagnostics()
end

function VRCompanion:resetMotionControllers()
    state = newState()
    initHands()
    self.pointerHand = nil
end

VRCompanion:registerSubsystem("motionControllers", {
    update = function(self, dt) self:updateMotionControllers(dt) end,
    saveSettings = function(self, xmlFile, xml) self:saveMotionControllerSettings(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadMotionControllerSettings(xmlFile, xml) end,
    resetSettings = function(self)
        self.motionControllersEnabled = self.DEFAULT_MOTION_CONTROLLERS_ENABLED
        self.pointerConeDegrees = self.DEFAULT_POINTER_CONE_DEGREES
    end,
    mapUnload = function(self) self:resetMotionControllers() end,
    consoleCommands = {
        { name = "vrPointerDiagnostics", description = "Report why the Pointer Ray may not select Click Points: vrPointerDiagnostics", method = "consoleCommandPointerDiagnostics" },
        { name = "vrMotionControllers", description = "Turn Motion Controller input on or off (saved; the Injector's session is untouched): vrMotionControllers [on|off]", method = "consoleCommandSetMotionControllers" },
        { name = "vrMotionControllerStatus", description = "Report each Motion Controller's tracked and in-use state and the Pointer Hand: vrMotionControllerStatus", method = "consoleCommandMotionControllerStatus" },
        { name = "vrPointerCone", description = "How wide (degrees) the cone around the Pointer Ray is inside which the nearest Click Point is selected (saved): vrPointerCone [degrees]", method = "consoleCommandSetPointerCone" },
    },
})
