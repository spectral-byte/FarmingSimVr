VRCompanion = VRCompanion or {}

local engine = VRCompanion.getEngineGlobal

VRCompanion.DEFAULT_OBJECT_GRAB_ENABLED = true
VRCompanion.objectGrabEnabled = VRCompanion.DEFAULT_OBJECT_GRAB_ENABLED

VRCompanion.OBJECT_GRAB_REACH_BEHIND = 0.6

VRCompanion.OBJECT_GRAB_LAND_PULSE_MS = 25
VRCompanion.OBJECT_GRAB_LAND_PULSE_STRENGTH = 0.25

VRCompanion.OBJECT_GRAB_ANSWER_WAIT_MS = 250

VRCompanion.OBJECT_GRAB_GRAB_PULSE_MS = 40
VRCompanion.OBJECT_GRAB_GRAB_PULSE_STRENGTH = 0.6

VRCompanion.OBJECT_GRAB_THROW_MIN_SPEED = 1.0
VRCompanion.OBJECT_GRAB_THROW_FULL_SPEED = 4.0
VRCompanion.OBJECT_GRAB_THROW_WINDOW_MS = 100

VRCompanion.OBJECT_GRAB_SNAP_PULSE_MS = 120
VRCompanion.OBJECT_GRAB_SNAP_PULSE_STRENGTH = 1.0

local function newState()
    return {
        landedNode = nil,
        gripDown = { [0] = false, [1] = false },     -- per hand: the grip was held on the pad at the end of the last update
        gripPressed = { [0] = false, [1] = false },  -- per hand: the grip went down in this update
        carry = nil,         -- the hand carrying a Held Object it grabbed: { hand, hands }
        offHandSqueezed = nil,  -- the hand (not the Pointer Hand) whose grip was squeezed this update while it points at something grabbable
        pendingGrab = nil,   -- the off-hand squeeze waiting for the targeter's answer to the new ray: { hand, untilMs }
        dt = 0,              -- the dt of this update
        swing = { samples = {}, rig = nil, last = nil },  -- the carrying hand's recent places in the world, and the last release
        lastSnapMs = nil,
        peeked = { player = nil, node = nil, target = nil, waiting = false },
    }
end

local state = newState()

local WRAP_SPECS = {
    getLookRay = { class = "Player", parameters = 1, describe = "(player)", shared = "lookRay" },
    handsOnDraw = { class = "HandToolHands", fn = "onDraw", parameters = 1, describe = "(tool)" },
    handsUpdateKinematicNode = { class = "HandToolHands", fn = "updateKinematicNode", parameters = 1, describe = "(tool)" },
    handsJointBroken = { class = "HandToolHands", fn = "onHeldItemJointBroken", parameters = 2, describe = "(tool, jointIndex)" },
    getCanPickUpNode = { class = "HandToolHands", parameters = 2, describe = "(tool, node)" },
}

local wraps = VRCompanion.GameWraps.newSet({
    owner = "objectGrab",
    giveUp = "Object Grab disabled: %s. Picking up is unchanged.",
    functions = WRAP_SPECS,
})

local function gameHasHands()
    return type(engine("HandToolHands")) == "table"
end

local function wrapAll()
    if gameHasHands() then wraps:wrapAll() end
end

local function checkWraps()
    if not gameHasHands() then return wraps:problem() end
    return wraps:check()
end

local INSTANCE_WRAPS = { "handsUpdateKinematicNode", "handsJointBroken" }

local function localPlayer()
    local player = engine("g_localPlayer")
    if player == nil then
        local mission = engine("g_currentMission")
        player = type(mission) == "table" and mission.player or nil
    end
    return player
end

local function handToolInHand(player)
    if type(player) ~= "table" or type(player.getHeldHandTool) ~= "function" then return false end
    local ok, held = pcall(player.getHeldHandTool, player)
    return ok and held ~= nil
end

local function emptyHands(player)
    if type(player) ~= "table" or type(player.getHeldHandTool) ~= "function" then return nil end
    local ok, held = pcall(player.getHeldHandTool, player)
    if not ok or held ~= nil then return nil end
    local hands = player.hands
    if type(hands) ~= "table" or type(hands.spec_hands) ~= "table" then return nil end
    if type(hands.getIsHeld) == "function" then
        local gotHeld, isHeld = pcall(hands.getIsHeld, hands)
        if not gotHeld or isHeld ~= true then return nil end
    end
    return hands
end

local function whyNotAiming(self, player)
    if self.objectGrabEnabled == false then return "Object Grab is off" end
    if self.motionControllersEnabled == false then return "Motion Controllers are off" end
    if wraps:problem() ~= nil then return "Object Grab gave up (" .. wraps:problem() .. ")" end
    if type(player) ~= "table" or player ~= localPlayer() then return "not the local player" end
    if player.camera == nil or player.camera.isFirstPerson ~= true then return "not in the first person view" end
    if self:getControlledVehicle() ~= nil then return "in a vehicle" end
    if self:doesMenuTakeInput() then return "a menu takes the input" end
    if emptyHands(player) == nil then return handToolInHand(player) and "a Hand Tool is in hand" or "the hands are not held" end
    return nil
end

local function aimedRay(self, player)
    local why = whyNotAiming(self, player)
    if why ~= nil then return nil, why end
    if self.pointerHand == nil then return nil, "there is no Pointer Hand" end
    local ray = self:getPointerRay()
    if ray == nil then return nil, "the Pointer Ray is not placed" end
    local behind = self.OBJECT_GRAB_REACH_BEHIND
    return ray.ox - ray.dx * behind, ray.oy - ray.dy * behind, ray.oz - ray.dz * behind, ray.dx, ray.dy, ray.dz
end

VRCompanion:registerLookRayAimer("objectGrab", function(player)
    return aimedRay(VRCompanion, player)
end)

local function padGripHeld(hand)
    local bridge = engine("VRBridge")
    if type(bridge) ~= "table" or type(bridge.getVirtualGamepad) ~= "function" then return false end
    local ok, _, _, _, leftHeld, rightHeld = pcall(bridge.getVirtualGamepad)
    if not ok then return false end
    if hand == VRCompanion.MOTION_HAND_LEFT then return leftHeld == true end
    return rightHeld == true
end

local function trackGrips()
    for _, hand in ipairs({ VRCompanion.MOTION_HAND_LEFT, VRCompanion.MOTION_HAND_RIGHT }) do
        local down = padGripHeld(hand)
        state.gripPressed[hand] = down and not state.gripDown[hand]
        state.gripDown[hand] = down
    end
end

local function takeGrip(self, hand)
    return self:takeVirtualGamepadInput(hand, "grip")
end

local function qrotate(qx, qy, qz, qw, x, y, z)
    local cx, cy, cz = qy * z - qz * y + qw * x, qz * x - qx * z + qw * y, qx * y - qy * x + qw * z
    return x + 2 * (qy * cz - qz * cy), y + 2 * (qz * cx - qx * cz), z + 2 * (qx * cy - qy * cx)
end

local handPose = {}

local function handGripInWorld(self, hand)
    local pose = self:getHandState(hand)
    if type(pose) ~= "table" or pose.tracked ~= true then return nil end
    local placed
    if pose.gripPoseTracked == true then
        placed = self:headRelativePoseToWorld(pose.gripPoseX, pose.gripPoseY, pose.gripPoseZ,
            pose.gripPoseQx, pose.gripPoseQy, pose.gripPoseQz, pose.gripPoseQw, handPose)
    else
        placed = self:headRelativePoseToWorld(pose.x, pose.y, pose.z, pose.qx, pose.qy, pose.qz, pose.qw, handPose)
    end
    if placed ~= true then return nil end
    return handPose
end

local function carryNodes(hands)
    if type(hands.getKinematicNode) ~= "function" then return nil end
    local ok, kinematic, rotation = pcall(hands.getKinematicNode, hands)
    if not ok or kinematic == nil or kinematic == 0 or rotation == nil or rotation == 0 then return nil end
    return kinematic, rotation
end

local function recordGrabOffset(self, carry)
    local hands = carry.hands
    if type(hands.getIsAwaitingJointCreation) == "function" then
        local ok, awaiting = pcall(hands.getIsAwaitingJointCreation, hands)
        if ok and awaiting == true then return end
    end
    local _, rotation = carryNodes(hands)
    local hand = handGripInWorld(self, carry.hand)
    if rotation == nil or hand == nil then return end
    local x, y, z = engine("getWorldTranslation")(rotation)
    local qx, qy, qz, qw = engine("getWorldQuaternion")(rotation)
    local ix, iy, iz, iw = -hand.qx, -hand.qy, -hand.qz, hand.qw
    local ox, oy, oz = qrotate(ix, iy, iz, iw, x - hand.x, y - hand.y, z - hand.z)
    local rx, ry, rz, rw = self.multiplyQuaternions(ix, iy, iz, iw, qx, qy, qz, qw)
    carry.offset = { x = ox, y = oy, z = oz, qx = rx, qy = ry, qz = rz, qw = rw }
    carry.placed = { qx = qx, qy = qy, qz = qz, qw = qw, hx = hand.qx, hy = hand.qy, hz = hand.qz, hw = hand.qw }
end

local TURN_EPSILON = 1e-9  -- 1 - |dot| of two rotations: about 9e-5 rad, far below any turn the game makes in an update
local function absorbTurn(carry, rotation)
    local placed, offset = carry.placed, carry.offset
    local qx, qy, qz, qw = engine("getWorldQuaternion")(rotation)
    if 1 - math.abs(qx * placed.qx + qy * placed.qy + qz * placed.qz + qw * placed.qw) <= TURN_EPSILON then return end
    offset.qx, offset.qy, offset.qz, offset.qw =
        VRCompanion.multiplyQuaternions(-placed.hx, -placed.hy, -placed.hz, placed.hw, qx, qy, qz, qw)
    placed.qx, placed.qy, placed.qz, placed.qw = qx, qy, qz, qw
end

local function placeFromHand(self, tool, carry)
    local kinematic, rotation = carryNodes(tool)
    if kinematic == nil then return end
    local spec = tool.spec_hands
    if type(tool.raiseDirtyFlags) == "function" and spec ~= nil then pcall(tool.raiseDirtyFlags, tool, spec.dirtyFlag) end
    absorbTurn(carry, rotation)
    local hand = handGripInWorld(self, carry.hand)
    if hand == nil then return end
    local offset = carry.offset
    local ox, oy, oz = qrotate(hand.qx, hand.qy, hand.qz, hand.qw, offset.x, offset.y, offset.z)
    local px, py, pz = hand.x + ox, hand.y + oy, hand.z + oz
    local qx, qy, qz, qw = self.multiplyQuaternions(hand.qx, hand.qy, hand.qz, hand.qw, offset.qx, offset.qy, offset.qz, offset.qw)
    local fx, _, fz = qrotate(qx, qy, qz, qw, 0, 0, 1)
    if fx * fx + fz * fz > 0.0025 then carry.heading = math.atan2(fx, fz) end
    local heading = carry.heading or 0
    local yawX, yawY, yawZ, yawW = 0, math.sin(heading / 2), 0, math.cos(heading / 2)
    local lx, ly, lz = engine("getTranslation")(rotation)
    local rx, ry, rz = qrotate(yawX, yawY, yawZ, yawW, lx, ly, lz)
    engine("setWorldTranslation")(kinematic, px - rx, py - ry, pz - rz)
    engine("setWorldQuaternion")(kinematic, yawX, yawY, yawZ, yawW)
    engine("setWorldQuaternion")(rotation, qx, qy, qz, qw)
    local placed = carry.placed
    placed.qx, placed.qy, placed.qz, placed.qw = engine("getWorldQuaternion")(rotation)
    placed.hx, placed.hy, placed.hz, placed.hw = hand.qx, hand.qy, hand.qz, hand.qw
    local jointId = spec ~= nil and spec.heldItemJointId or nil
    if tool.isServer == true and jointId ~= nil and type(engine("setJointFrame")) == "function" then
        pcall(engine("setJointFrame"), jointId, 0, rotation)
    end
end

local function mayPlaceNodes(tool)
    if type(tool.getIsHeld) ~= "function" or type(tool.getCarryingPlayer) ~= "function" then return false end
    local ok, held = pcall(tool.getIsHeld, tool)
    if not ok or held ~= true then return false end
    local gotPlayer, player = pcall(tool.getCarryingPlayer, tool)
    return gotPlayer and type(player) == "table" and player.isOwner == true
end

WRAP_SPECS.handsUpdateKinematicNode.make = function(original)
    return function(tool, ...)
        local carry = state.carry
        if carry ~= nil and carry.hands == tool and carry.offset ~= nil and mayPlaceNodes(tool) then
            return placeFromHand(VRCompanion, tool, carry)
        end
        return original(tool, ...)
    end
end

local function wrapInstance(tool)
    wraps:wrapInstance(tool, INSTANCE_WRAPS)
end

WRAP_SPECS.handsJointBroken.make = function(original)
    return function(tool, jointIndex, ...)
        local carry = state.carry
        if jointIndex ~= nil and carry ~= nil and carry.hands == tool then carry.snapped = true end
        return original(tool, jointIndex, ...)
    end
end

local function holdsItem(hands)
    if type(hands) ~= "table" or type(hands.getIsHoldingItem) ~= "function" then return false end
    local ok, holding = pcall(hands.getIsHoldingItem, hands)
    return ok and holding == true
end

local function startGrab(self, hand, hands, target)
    if type(hands.pickUpTarget) ~= "function" then return end
    pcall(hands.pickUpTarget, hands, target)
    if not holdsItem(hands) then return end
    state.carry = { hand = hand, hands = hands, offset = nil, heading = nil }
    recordGrabOffset(self, state.carry)
    self:pulseHand(hand, self.OBJECT_GRAB_GRAB_PULSE_MS, self.OBJECT_GRAB_GRAB_PULSE_STRENGTH)
end

local function tryGrab(self, player, target, waiting)
    local hand = self.pointerHand
    if hand == nil or state.carry ~= nil then return end
    if not takeGrip(self, hand) or not (state.gripPressed[hand] or waiting) then return end
    state.pendingGrab = nil
    local hands = emptyHands(player)
    if hands ~= nil then startGrab(self, hand, hands, target) end
end

local OFF_HAND_MINIMUM_DISTANCE = 0.5

local checkPose = {}
local checkHit = {}

local function onCheckHit(self, node, x, y, z, distance)
    self.node, self.distance = node, distance
    return true
end
checkHit.onHit = onCheckHit

local function pointsAtGrabbable(self, hands, hand)
    local rayCast, key = engine("raycastClosest"), engine("HandToolHands")
    local spec = hands.spec_hands
    if type(rayCast) ~= "function" or type(key) ~= "table" or type(hands.getCanPickUpNode) ~= "function" or type(spec) ~= "table" then
        return false
    end
    local tx, ty, tz, tdx, tdy, tdz = self:getPointerRayInTrackingSpace(hand)
    if tx == nil then return false end
    if self:headRelativePoseToWorld(tx, ty, tz, 0, 0, 0, 1, checkPose) ~= true then return false end
    local dx, dy, dz = qrotate(checkPose.qx, checkPose.qy, checkPose.qz, checkPose.qw, tdx, tdy, tdz)
    local behind = self.OBJECT_GRAB_REACH_BEHIND
    checkHit.node, checkHit.distance = nil, nil
    local ok = pcall(rayCast, checkPose.x - dx * behind, checkPose.y - dy * behind, checkPose.z - dz * behind, dx, dy, dz,
        spec.pickupDistance or 0, "onHit", checkHit, key.TARGET_MASK)
    if not ok or checkHit.node == nil or (checkHit.distance or 0) < OFF_HAND_MINIMUM_DISTANCE then return false end
    local can, canPickUp = pcall(hands.getCanPickUpNode, hands, checkHit.node)
    return can and canPickUp == true
end

local function updateOffHand(self)
    state.offHandSqueezed = nil
    if state.carry ~= nil then return end
    local player = localPlayer()
    if whyNotAiming(self, player) ~= nil then return end
    local hands = emptyHands(player)
    for _, hand in ipairs({ VRCompanion.MOTION_HAND_LEFT, VRCompanion.MOTION_HAND_RIGHT }) do
        if hand ~= self.pointerHand and self:isHandInUse(hand) and pointsAtGrabbable(self, hands, hand) then
            if takeGrip(self, hand) and state.gripPressed[hand] then state.offHandSqueezed = hand end
        end
    end
end

function VRCompanion:isObjectGrabSqueeze(hand)
    return state.offHandSqueezed == hand
end

local function runTargeterAgain(player)
    local targeter, key = player.targeter, engine("HandToolHands")
    if type(targeter) ~= "table" or type(targeter.update) ~= "function" then return end
    if type(key) == "table" and type(targeter.closestTargetsByKey) == "table" then targeter.closestTargetsByKey[key] = nil end
    pcall(targeter.update, targeter, state.dt)
end

local function startWaitingForTheTargeter(self, player, hand)
    local now = self:getCurrentTimeMs()
    state.pendingGrab = now ~= nil and { hand = hand, untilMs = now + self.OBJECT_GRAB_ANSWER_WAIT_MS } or nil
    runTargeterAgain(player)
end

local function isWaitingForTheTargeter(self, hand)
    local pending = state.pendingGrab
    if pending == nil then return false end
    local now = self:getCurrentTimeMs()
    if pending.hand ~= hand or not state.gripDown[hand] or now == nil or now > pending.untilMs then
        state.pendingGrab = nil
        return false
    end
    return true
end

local SWING_RIG_JUMP_DISTANCE = 0.25  -- m; walking is a few cm per update
local SWING_RIG_JUMP_ANGLE = 0.02     -- rad; turning the head does not turn the tracking space
local rigPose = {}

local function sampleSwing(self)
    local swing = state.swing
    local carry = state.carry
    if carry == nil then
        if swing.rig ~= nil or #swing.samples > 0 then swing.samples, swing.rig = {}, nil end
        return
    end
    local hand = handGripInWorld(self, carry.hand)
    if hand == nil or self:headRelativePoseToWorld(0, 0, 0, 0, 0, 0, 1, rigPose) ~= true then return end
    local rig = swing.rig
    if rig ~= nil then
        local dx, dy, dz = rigPose.x - rig.x, rigPose.y - rig.y, rigPose.z - rig.z
        local dot = math.min(1, math.abs(rigPose.qx * rig.qx + rigPose.qy * rig.qy + rigPose.qz * rig.qz + rigPose.qw * rig.qw))
        if dx * dx + dy * dy + dz * dz > SWING_RIG_JUMP_DISTANCE ^ 2 or 2 * math.acos(dot) > SWING_RIG_JUMP_ANGLE then
            swing.samples = {}
        end
    else
        rig = {}
        swing.rig = rig
    end
    rig.x, rig.y, rig.z, rig.qx, rig.qy, rig.qz, rig.qw = rigPose.x, rigPose.y, rigPose.z, rigPose.qx, rigPose.qy, rigPose.qz, rigPose.qw
    local samples = swing.samples
    local now = self:getCurrentTimeMs()
    if now == nil then
        swing.samples = {}
        return
    end
    samples[#samples + 1] = { t = now, x = hand.x, y = hand.y, z = hand.z }
    while #samples > 1 and now - samples[1].t > self.OBJECT_GRAB_THROW_WINDOW_MS do table.remove(samples, 1) end
end

-- The hand's velocity in the world (m/s) from its oldest to its newest sample in the window: vx, vy, vz, speed; nil when the hand has
-- not been seen moving over two samples, or when the game clock is not available.
local function handVelocity(self)
    if self:getCurrentTimeMs() == nil then return nil end
    local samples = state.swing.samples
    local first, last = samples[1], samples[#samples]
    if first == nil or last == first or first.t == nil or last.t == nil or last.t <= first.t then return nil end
    local seconds = (last.t - first.t) / 1000
    local vx, vy, vz = (last.x - first.x) / seconds, (last.y - first.y) / seconds, (last.z - first.z) / seconds
    return vx, vy, vz, math.sqrt(vx * vx + vy * vy + vz * vz)
end

local function releaseHeldObject(self, carry)
    local hands = carry.hands
    local vx, vy, vz, speed = handVelocity(self)
    if speed ~= nil and speed > self.OBJECT_GRAB_THROW_MIN_SPEED and type(hands.throwHeldItemWithForceVector) == "function" then
        local dx, dy, dz = vx / speed, vy / speed, vz / speed
        local force = math.max(0, math.min(1, speed / self.OBJECT_GRAB_THROW_FULL_SPEED))
        if pcall(hands.throwHeldItemWithForceVector, hands, dx, dy, dz, force) then
            state.swing.last = { kind = "throw", speed = speed, dirX = dx, dirY = dy, dirZ = dz, force = force }
            return
        end
    end
    state.swing.last = { kind = "drop", speed = speed or 0, dirX = 0, dirY = 0, dirZ = 0, force = 0 }
    if type(hands.dropHeldItem) == "function" then pcall(hands.dropHeldItem, hands) end
end

-- What the last release did, for the diagnostics: kind ("throw" or "drop"), the hand's speed in m/s, the direction it travelled (a
-- unit vector; zero for a drop) and the force scalar (0 for a drop). Nil before the first release of the session.
function VRCompanion:getObjectGrabLastRelease()
    local last = state.swing.last
    if last == nil then return nil end
    return last.kind, last.speed, last.dirX, last.dirY, last.dirZ, last.force
end

function VRCompanion:isHandCarryingHeldObject(hand)
    return state.carry ~= nil and state.carry.hand == hand
end

local function carryHasEnded(self, carry)
    if self.objectGrabEnabled == false or self.motionControllersEnabled == false or wraps:problem() ~= nil then return true end
    return emptyHands(localPlayer()) ~= carry.hands or not holdsItem(carry.hands)
end

local function mustDropOnEnd(self, carry)
    if self.objectGrabEnabled ~= false and self.motionControllersEnabled ~= false and wraps:problem() == nil then return false end
    return emptyHands(localPlayer()) == carry.hands and holdsItem(carry.hands)
end

local function gripWasLetGo(self, carry)
    local pose = self:getHandState(carry.hand)
    return type(pose) == "table" and pose.tracked == true and not state.gripDown[carry.hand]
end

local function updateCarry(self)
    local carry = state.carry
    if carry == nil then return end
    if carryHasEnded(self, carry) then
        state.carry = nil
        if mustDropOnEnd(self, carry) and type(carry.hands.dropHeldItem) == "function" then pcall(carry.hands.dropHeldItem, carry.hands) end
        if carry.snapped then
            state.lastSnapMs = self:getCurrentTimeMs()
            self:pulseHand(carry.hand, self.OBJECT_GRAB_SNAP_PULSE_MS, self.OBJECT_GRAB_SNAP_PULSE_STRENGTH)
        end
        return
    end
    if gripWasLetGo(self, carry) then
        state.carry = nil
        releaseHeldObject(self, carry)
        return
    end
    if carry.offset == nil then recordGrabOffset(self, carry) end
    takeGrip(self, carry.hand)
end

function VRCompanion:updateObjectGrab(dt)
    state.dt = dt or 0
    trackGrips()
    if wraps:problem() == nil then
        local hands = emptyHands(localPlayer())
        if hands ~= nil then wrapInstance(hands) end
    end
    sampleSwing(self)
    updateCarry(self)
    updateOffHand(self)
end

local function grabbableTarget(player)
    local hands = emptyHands(player)
    local key = engine("HandToolHands")
    local targeter = player.targeter
    if hands == nil or type(key) ~= "table" or type(targeter) ~= "table" or type(targeter.closestTargetsByKey) ~= "table" then
        return nil
    end
    local target = targeter.closestTargetsByKey[key]
    if type(target) ~= "table" or target.node == nil or type(hands.getCanPickUpNode) ~= "function" then return nil end
    local ok, can = pcall(hands.getCanPickUpNode, hands, target.node)
    if not ok or can ~= true then return nil end
    return target.node, target.x, target.y, target.z, target
end

function VRCompanion:peekObjectGrabRayTarget()
    local peeked = state.peeked
    peeked.target = nil
    local player = localPlayer()
    if player == nil or aimedRay(self, player) == nil then
        state.landedNode = nil
        return false
    end
    local hand = self.pointerHand
    if state.offHandSqueezed == hand then startWaitingForTheTargeter(self, player, hand) end
    local waiting = isWaitingForTheTargeter(self, hand)
    if waiting then takeGrip(self, hand) end
    local node, x, y, z, target = grabbableTarget(player)
    if node == nil then
        state.landedNode = nil
        return false
    end
    peeked.player, peeked.node, peeked.target, peeked.waiting = player, node, target, waiting
    return true, x, y, z, (tonumber(target.distance) or 0) - self.OBJECT_GRAB_REACH_BEHIND
end

function VRCompanion:settleObjectGrabRayTarget(isTarget)
    local peeked = state.peeked
    local target = peeked.target
    peeked.target = nil
    if target == nil then return end
    if not isTarget then
        state.landedNode = nil
        return
    end
    if peeked.node ~= state.landedNode then
        state.landedNode = peeked.node
        self:pulsePointerHand(self.OBJECT_GRAB_LAND_PULSE_MS, self.OBJECT_GRAB_LAND_PULSE_STRENGTH)
    end
    tryGrab(self, peeked.player, target, peeked.waiting)
end

local function drawIsHeldBack(tool)
    if type(tool) ~= "table" or type(tool.getCarryingPlayer) ~= "function" then return false end
    local ok, player = pcall(tool.getCarryingPlayer, tool)
    if not ok or player == nil or player ~= localPlayer() then return false end
    return aimedRay(VRCompanion, player) ~= nil
end

WRAP_SPECS.handsOnDraw.make = function(original)
    return function(tool, ...)
        if drawIsHeldBack(tool) then return end
        return original(tool, ...)
    end
end

local SETTING_KEY_ENABLED = "vrSettings.objectGrab"

function VRCompanion:setObjectGrabEnabled(enabled, save)
    self.objectGrabEnabled = (enabled ~= false)
    if save then
        self:saveConfiguration()
    end
end

function VRCompanion:saveObjectGrabSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY_ENABLED, self.objectGrabEnabled ~= false)
    end
end

function VRCompanion:loadObjectGrabSetting(xmlFile, xml)
    local loaded = nil
    if type(xml.getBool) == "function" then
        loaded = xml.getBool(xmlFile, SETTING_KEY_ENABLED)
    end
    if loaded == nil and type(xml.getString) == "function" then
        local str = xml.getString(xmlFile, SETTING_KEY_ENABLED)
        if str ~= nil then loaded = VRCompanion.parseOnOffArgument(str) end
    end
    if loaded ~= nil then
        self.objectGrabEnabled = loaded
    end
end

function VRCompanion:consoleCommandSetObjectGrab(arg)
    local enabled = VRCompanion.parseOnOffArgument(arg)
    if arg == nil or arg == "" or string.lower(tostring(arg)) == "status" then
        return string.format("Object Grab is %s. Usage: vrObjectGrab <on|off> (saved)", self.objectGrabEnabled ~= false and "on" or "off")
    end
    if enabled == nil then
        return "Usage: vrObjectGrab <on|off>"
    end
    self:setObjectGrabEnabled(enabled, true)
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(VRCompanion.formatLocalisedText("vrObjectGrab_set", "VR Object Grab: %s (Saved)",
                VRCompanion.onOffText(enabled)), 2500)
        end)
    end
    print(string.format("[FS25_VR] Object Grab set to: %s", enabled and "on" or "off"))
    return string.format("Object Grab set to: %s (Saved)", enabled and "on" or "off")
end

local HAND_NAMES = { [0] = "left", [1] = "right" }

local function handName(hand)
    return HAND_NAMES[hand] or tostring(hand)
end

local function nodeMass(node)
    local class = engine("HandToolHands")
    if type(class) ~= "table" or type(class.calculateItemMass) ~= "function" then return nil end
    local ok, mass = pcall(class.calculateItemMass, node)
    if ok and type(mass) == "number" then return mass end
    return nil
end

local function statusField(self)
    if self.objectGrabEnabled == false then
        return "Object Grab: off (the setting is off: vrObjectGrab on turns it on)"
    end
    if state.carry ~= nil then return "Object Grab: on (carrying)" end
    local ray, why = aimedRay(self, localPlayer())
    if ray ~= nil then return "Object Grab: on (aiming)" end
    return "Object Grab: off (" .. tostring(why) .. ")"
end

local function aimingField(self)
    local ray = aimedRay(self, localPlayer())
    if ray == nil or self.pointerHand == nil then return "aiming: no" end
    return "aiming: the " .. handName(self.pointerHand) .. " hand (the Pointer Hand)"
end

local function limitsField(hands)
    local spec = hands ~= nil and hands.spec_hands or nil
    if type(spec) ~= "table" then return "limits: unknown (the hands are not held)" end
    return string.format("limits: maximum mass %.3f t, pickup distance %.2f m, super strength %s",
        tonumber(spec.currentMaximumMass) or 0, tonumber(spec.pickupDistance) or 0, spec.hasSuperStrength == true and "on" or "off")
end

local function rayTargetField(self, player, hands)
    if aimedRay(self, player) == nil then return "Pointer Ray on: nothing (not aiming)" end
    local key = engine("HandToolHands")
    local targeter = type(player) == "table" and player.targeter or nil
    local target = nil
    if type(key) == "table" and type(targeter) == "table" and type(targeter.closestTargetsByKey) == "table" then
        target = targeter.closestTargetsByKey[key]
    end
    if type(target) ~= "table" or target.node == nil then return "Pointer Ray on: nothing" end
    local spec = hands ~= nil and hands.spec_hands or nil
    local limit = type(spec) == "table" and tonumber(spec.currentMaximumMass) or nil
    local distance = tonumber(target.distance) or 0
    local mass = nodeMass(target.node)
    local text = string.format("Pointer Ray on: an object %.2f m from the fingertip (%.2f m along the look ray)",
        distance - self.OBJECT_GRAB_REACH_BEHIND, distance)
    if mass ~= nil then
        text = text .. string.format(", mass %.3f t%s", mass, limit ~= nil and string.format(" of %.3f t", limit) or "")
    end
    local can = false
    if hands ~= nil and type(hands.getCanPickUpNode) == "function" then
        local ok, result = pcall(hands.getCanPickUpNode, hands, target.node)
        can = ok and result == true
    end
    if can then return text .. ", the hands can pick it up" end
    if mass ~= nil and limit ~= nil and mass > limit then return text .. ", the hands cannot pick it up (too heavy)" end
    if holdsItem(hands) then return text .. ", the hands cannot pick it up (they hold something)" end
    return text .. ", the hands cannot pick it up (the game refuses it)"
end

local function trailText(self, carry)
    if carry.offset == nil then return "unknown (the grab offset is not recorded yet)" end
    local _, rotation = carryNodes(carry.hands)
    local hand = handGripInWorld(self, carry.hand)
    if rotation == nil then return "unknown (the game's rotation node is not there)" end
    if hand == nil then return "unknown (the hand is not tracked)" end
    local offset = carry.offset
    local ox, oy, oz = qrotate(hand.qx, hand.qy, hand.qz, hand.qw, offset.x, offset.y, offset.z)
    local x, y, z = engine("getWorldTranslation")(rotation)
    local dx, dy, dz = x - (hand.x + ox), y - (hand.y + oy), z - (hand.z + oz)
    return string.format("%.2f m", math.sqrt(dx * dx + dy * dy + dz * dz))
end

local function carryFields(self)
    local carry = state.carry
    if carry == nil then return { "carrying: no" } end
    local offsetText = "grab offset not recorded yet"
    if carry.offset ~= nil then
        local o = carry.offset
        offsetText = string.format("grab offset (%.2f, %.2f, %.2f) = %.2f m from the hand", o.x, o.y, o.z,
            math.sqrt(o.x * o.x + o.y * o.y + o.z * o.z))
    end
    return {
        string.format("carrying: the %s hand, %s", handName(carry.hand), offsetText),
        "Held Object trails the hand by " .. trailText(self, carry),
    }
end

local function releaseField()
    local last = state.swing.last
    if last == nil then return "last release: none" end
    if last.kind == "throw" then
        return string.format("last release: throw, hand speed %.1f m/s, direction (%.2f, %.2f, %.2f), force %.2f",
            last.speed, last.dirX, last.dirY, last.dirZ, last.force)
    end
    return string.format("last release: %s, hand speed %.1f m/s", tostring(last.kind), last.speed)
end

local function snapField(self)
    if state.lastSnapMs == nil then return "last snap: none" end
    local now = self:getCurrentTimeMs()
    if now == nil then return "last snap: yes (the game clock is not available)" end
    return string.format("last snap: %.0f s ago", math.max(0, now - state.lastSnapMs) / 1000)
end

local function diagnosticFields(self)
    local player = localPlayer()
    local hands = emptyHands(player)
    local fields = {
        statusField(self),
        aimingField(self),
        rayTargetField(self, player, hands),
        limitsField(hands),
    }
    for _, field in ipairs(carryFields(self)) do fields[#fields + 1] = field end
    fields[#fields + 1] = releaseField()
    fields[#fields + 1] = snapField(self)
    for _, name in ipairs(wraps:wrapNames()) do
        fields[#fields + 1] = wraps:status(name)
    end
    return fields
end

function VRCompanion:getObjectGrabDiagnostics()
    return table.concat(diagnosticFields(self), " | ")
end

function VRCompanion:consoleCommandObjectGrabDiagnostics()
    for _, field in ipairs(diagnosticFields(self)) do
        print("[FS25_VR] " .. field)
    end
    return self:getObjectGrabDiagnostics()
end

function VRCompanion:resetObjectGrab()
    wraps:reset()
    state = newState()
end

wrapAll()

VRCompanion:registerSubsystem("objectGrab", {
    mapLoad = function(self)
        wrapAll()
        checkWraps()
    end,
    mapUnload = function(self) self:resetObjectGrab() end,
    saveSettings = function(self, xmlFile, xml) self:saveObjectGrabSetting(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadObjectGrabSetting(xmlFile, xml) end,
    resetSettings = function(self) self.objectGrabEnabled = self.DEFAULT_OBJECT_GRAB_ENABLED end,
    consoleCommands = {
        { name = "vrObjectGrab", description = "Turn Object Grab, picking up objects aimed by a Motion Controller, on or off (saved): vrObjectGrab [on|off]", method = "consoleCommandSetObjectGrab" },
        { name = "vrObjectGrabDiagnostics", description = "Report whether Object Grab is on and why not, the aim, the target against the game's limits, the carry and the last release: vrObjectGrabDiagnostics", method = "consoleCommandObjectGrabDiagnostics" },
    },
})
