-- ====================================================================
-- FS25 VR Companion Mod: Object Grab
--
-- Lets the player pick up, carry and throw loose objects with the Motion Controllers (spec #156, ADR-0033). On foot with empty hands
-- the game carries one object at a time with its "hands" hand tool (HandToolHands): it targets what the look ray meets
-- (PlayerTargeter, fed by Player:getLookRay) and the player's pick-up button takes it. This file is built up ticket by ticket; so far
-- the Pointer Hand AIMS the empty hands (#158) and the grip grabs and carries (#159).
--
-- TERMS (CONTEXT.md): the Pointer Hand is the Motion Controller that points, the Pointer Ray runs from its index fingertip along the
-- finger, empty hands are the game's hands tool held, which the game counts as a hand tool and the Hand Tools do not.
--
-- AIMING: while Object Grab is on, the local player is on foot in the first person view with empty hands, no menu takes the input, and
-- the Pointer Hand is tracked and in use (it has a Pointer Ray), Player:getLookRay answers the Pointer Ray's ray with its origin moved
-- back along the finger by OBJECT_GRAB_REACH_BEHIND. So everything the game targets by looking follows the hand: the hands' pick-up
-- target, the prompts on animals, vehicles and trees, and the holders Hand Tools are taken from. A supported Hand Tool in a tracked
-- Tool Hand aims first (Core asks Hand Tools before Object Grab, see "Who aims the look ray"). Otherwise, and before a grab, the ray
-- is the game's own (the head's). Another player's look ray is never touched.
-- The origin is moved back because the hands' target type only finds things between 0.5 m and the player's pickup distance of the ray's
-- origin: a hand that touches an object, or has gone a little into it, would be below the minimum. (The spray can and the horse brush
-- do the same.) The 10 cm beyond the minimum are for a fingertip that has gone into the object.
--
-- THE POINTER RAY ON AN OBJECT: the targeter's closest target for the hands (closestTargetsByKey[HandToolHands]) is a node the hands
-- can pick up when the game's own getCanPickUpNode says so (so the mass limit, the pickup distance and super strength are the game's).
-- While the Pointer Hand aims and the target is one, the Pointer Ray is drawn, to the point where the targeter hit it, and the Pointer
-- Hand gets one light pulse as the ray lands (again only after it left the object, or on another object).
--
-- NO CENTRE CROSSHAIR: while the hand aims, the hands tool's onDraw is skipped for the local player (its crosshair and grab icon at the
-- centre of the view are head-locked, and would point where the hand does not). The game's own function draws them otherwise.
--
-- THE GAME'S PICK-UP BUTTON is the game's: it picks up what the targeter found, which is now what the hand points at. The game then
-- carries it the game's way (along the look ray, so along the hand), and the grip leaves it alone.
--
-- THE GRAB (#159): while the Pointer Hand points at an object the hands can pick up, and while a hand carries, Object Grab takes that
-- hand's grip from the Virtual Gamepad (ADR-0028, takeVirtualGamepadInput(hand, "grip")), so the grip reads released on the pad. A
-- squeeze the pad let the hand have calls the game's pickUpTarget with the targeter's target, with one pulse on the hand. A grip held
-- before the ray landed keeps its binding and grabs nothing until it is let go and squeezed again. A squeeze at nothing grabbable is LB/RB.
--
-- THE OFF-HAND (#161): either hand grabs in one squeeze, even with both in use. See "The off-hand": a physics ray cast along the other
-- hand's finger only tells whether its grip may be taken; the squeeze makes it the Pointer Hand, the targeter runs again with its ray,
-- and the game's pickUpTarget picks up what the targeter finds. A trigger pull still moves the Pointer Hand, as before.
--
-- THE CARRY: the hand that grabbed carries the Held Object until the grip is let go (releaseHeldObject: a drop through the game's
-- dropHeldItem) or the game itself ends the carry (getting into a vehicle, taking a Hand Tool): the carry ends cleanly, and the grip
-- goes back to the pad once it is let go. While it lasts the hand is in use however still it is held, is never the Pointer Hand (the
-- other hand can point), and the kinematic node and the rotation node of the game's carry are placed from the hand (see "Placing the
-- Held Object"), keeping the object's place and angle in the hand as it was at the grab. A hand that is not tracked leaves them where
-- they were, and has not let go; they follow it again when it is tracked. Nothing is felt while it carries.
--
-- WRAPS: Player.getLookRay (shared with Hand Tools: Core owns the one wrap and each holds it, so that one giving up does not take the
-- look ray away from the other), HandToolHands.onDraw and HandToolHands.updateKinematicNode (the class AND the local player's hands
-- tool, which may hold a copy of it); getCanPickUpNode is only checked. When one is missing or has another shape
-- at map load, Object Grab gives up: ONE warning ("Object Grab disabled: ..."), its wraps are lifted, and picking up is the
-- game's. Hand Tools are not affected. A game without HandToolHands (no hands to aim) is not a failure: nothing is wrapped.
--
-- PUBLIC SURFACE
--   VRCompanion.objectGrabEnabled              boolean, default true, saved (`vrObjectGrab on|off`)
--   VRCompanion.OBJECT_GRAB_REACH_BEHIND       how far (m) the look ray's origin is moved back along the finger
--   VRCompanion.OBJECT_GRAB_LAND_PULSE_MS / _STRENGTH   the light pulse on the Pointer Hand as the ray lands on something grabbable
--   VRCompanion:getObjectGrabAimedRay(player) -> x, y, z, dirX, dirY, dirZ | nil, why not   what Player:getLookRay answers
--   VRCompanion:getObjectGrabRayTarget() -> hasTarget, x, y, z   the Pointer Ray's object target (called by updatePointerRay); a
--                                      squeeze while the ray is on the target grabs it
--   VRCompanion:updateObjectGrab(dt)    once per Motion Controller update, after the hands are polled and before the Pointer Hand is
--                                      chosen: the grip edges, and the carry (records the offset, lets go, ends)
--   VRCompanion:isHandCarryingHeldObject(hand) -> boolean   the hand carries a Held Object it grabbed (stays in use, never points)
--   VRCompanion:isObjectGrabSqueeze(hand) -> boolean   the hand, not the Pointer Hand, was squeezed this update while pointing at
--                                      something grabbable: it becomes the Pointer Hand (asked by choosePointerHand)
--   VRCompanion.OBJECT_GRAB_ANSWER_WAIT_MS   how long such a squeeze waits for the targeter's answer to the new ray
--   VRCompanion.OBJECT_GRAB_GRAB_PULSE_MS / _STRENGTH   the pulse on the hand that grabs
--   VRCompanion:setObjectGrabEnabled(enabled, save)
--   VRCompanion:saveObjectGrabSetting(xmlFile, xml) / loadObjectGrabSetting(xmlFile, xml)   settings chain
--   VRCompanion:getObjectGrabDiagnostics() -> string          the state of Object Grab, one line
--   VRCompanion:registerObjectGrabConsoleCommands() / resetObjectGrab()   map load / unload
--   Console: vrObjectGrab [on|off]
--
-- Hooks into the rest of the mod (each a single guarded call):
--   Core               registerLookRayAimer("objectGrab", ...) and holdLookRayWrap("objectGrab")
--   MotionControllers  updatePointerRay -> getObjectGrabRayTarget() (after Interactive Control); updateMotionControllers ->
--                      updateObjectGrab(dt) on every path; isHandCarryingHeldObject(hand) keeps the hand in use and out of the Pointer
--                      Hand candidates; the saved setting, the console commands, the diagnostics and the reset chain through the
--                      Motion Controller functions of the same names
--   VirtualGamepad     takeVirtualGamepadInput(hand, "grip"); VRBridge.getVirtualGamepad() for what the pad reads as squeezed
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.ObjectGrab = VRCompanion.ObjectGrab or {}

local engine = VRCompanion.getEngineGlobal

-- vrObjectGrab on|off: saved, default on. Object Grab also needs Motion Controllers on.
VRCompanion.DEFAULT_OBJECT_GRAB_ENABLED = true
VRCompanion.objectGrabEnabled = VRCompanion.DEFAULT_OBJECT_GRAB_ENABLED

-- The look ray starts this far (m) behind the fingertip: the game's 0.5 m minimum distance of the hands' target type, and 10 cm for a
-- fingertip that has gone into the object. Tune in the headset (#164).
VRCompanion.OBJECT_GRAB_REACH_BEHIND = 0.6

-- HAPTICS (milliseconds, strength 0..1): one light pulse on the Pointer Hand as the ray lands on something it can grab. For scale, the
-- Pointer Hand's Click Point selection is 25 ms at 0.25. Tune in the headset (#164).
VRCompanion.OBJECT_GRAB_LAND_PULSE_MS = 25
VRCompanion.OBJECT_GRAB_LAND_PULSE_STRENGTH = 0.25

-- How long (ms) an off-hand's squeeze waits for the targeter's answer to the new ray (the game's ray cast is asynchronous).
VRCompanion.OBJECT_GRAB_ANSWER_WAIT_MS = 250

-- One firmer pulse on the hand that grabs. Nothing is felt while it carries. Tune in the headset (#164).
VRCompanion.OBJECT_GRAB_GRAB_PULSE_MS = 40
VRCompanion.OBJECT_GRAB_GRAB_PULSE_STRENGTH = 0.6

local function newState()
    return {
        problem = nil,       -- why Object Grab gave up, once it did (the warning is logged then and never again)
        landedNode = nil,    -- the node the Pointer Ray is on and has pulsed for
        gripDown = { [0] = false, [1] = false },     -- per hand: the grip was held on the pad at the end of the last update
        gripPressed = { [0] = false, [1] = false },  -- per hand: the grip went down in this update
        carry = nil,         -- the hand carrying a Held Object it grabbed: { hand, hands }
        offHandSqueezed = nil,  -- the hand (not the Pointer Hand) whose grip was squeezed this update while it points at something grabbable
        pendingGrab = nil,   -- the off-hand squeeze waiting for the targeter's answer to the new ray: { hand, untilMs }
        dt = 0,              -- the dt of this update
    }
end

local state = newState()

-- ---- Wraps ------------------------------------------------------------------------------------------------------------
-- WRAP_SPECS is keyed by the name of the function wrapped. Each entry:
--   class        the name of the global table the function is on
--   fn           the function's name on the class, when it is not the key
--   parameters   how many parameters the function must declare at least (a reshaped function declares fewer)
--   describe     what the parameters are, for the warning
--   make         function(original) -> the wrapper; an entry without one is CHECK-ONLY (checked at map load, never wrapped)
--   shared       the wrap is Core's (VRCompanion:holdLookRayWrap)
local WRAP_SPECS = {
    getLookRay = { class = "Player", parameters = 1, describe = "(player)", shared = true },
    handsOnDraw = { class = "HandToolHands", fn = "onDraw", parameters = 1, describe = "(tool)" },
    handsUpdateKinematicNode = { class = "HandToolHands", fn = "updateKinematicNode", parameters = 1, describe = "(tool)" },
    getCanPickUpNode = { class = "HandToolHands", parameters = 2, describe = "(tool, node)" },
}

-- wraps[key] = { class = <the table wrapped>, fn = <the function's name on it>, original = <what it was>, wrapper = <what the wrap put there> }
local wraps = {}

-- How many parameters a Lua function declares, or nil when it takes any number or the runtime cannot tell.
local function parameterCount(fn)
    local library = _G and _G.debug
    if type(library) ~= "table" then return nil end
    if type(library.info) == "function" then
        local ok, count, isVararg = pcall(library.info, fn, "a")
        if ok and type(count) == "number" then return (not isVararg) and count or nil end
    end
    if type(library.getinfo) == "function" then
        local ok, info = pcall(library.getinfo, fn, "u")
        if ok and type(info) == "table" and type(info.nparams) == "number" then
            return (not info.isvararg) and info.nparams or nil
        end
    end
    return nil
end

-- Why a function is not what its wrap expects, or nil when it is.
local function shapeProblem(spec, fn)
    if fn == nil then return "it is missing" end
    if type(fn) ~= "function" then return "it is not a function" end
    local count = parameterCount(fn)
    if count ~= nil and count < spec.parameters then
        return string.format("it takes %d parameters, not %s", count, spec.describe)
    end
    return nil
end

-- Object Grab has hands to aim only in a game that has them.
local function gameHasHands()
    return type(engine("HandToolHands")) == "table"
end

local function wrapFunction(name)
    local spec = WRAP_SPECS[name]
    local class = engine(spec.class)
    if type(class) ~= "table" or (spec.make == nil and not spec.shared) then return end
    local current = wraps[name]
    if current ~= nil and current.class == class then return end
    local fn = spec.fn or name
    local original = spec.shared and VRCompanion:getLookRayWrapOriginal() or class[fn]
    if shapeProblem(spec, original) ~= nil then return end
    if spec.shared then
        if VRCompanion:holdLookRayWrap("objectGrab") then
            wraps[name] = { class = class, fn = fn, original = VRCompanion:getLookRayWrapOriginal(), shared = true }
        end
        return
    end
    local wrapper = spec.make(original)
    wraps[name] = { class = class, fn = fn, original = original, wrapper = wrapper }
    class[fn] = wrapper
end

local function wrapAll()
    if state.problem ~= nil or not gameHasHands() then return end
    for name in pairs(WRAP_SPECS) do wrapFunction(name) end
end

-- The hands tool's own copy of a function (see "Placing the Held Object"), wrapped for as long as the hands are the local player's:
-- { tool, original, hadOwn, wrapper }.
local instanceWrap = nil

-- Gives the hands tool its own copy back, unless something else has replaced the wrapper since.
local function unwrapInstance()
    local current = instanceWrap
    instanceWrap = nil
    local fn = WRAP_SPECS.handsUpdateKinematicNode.fn
    if current ~= nil and rawget(current.tool, fn) == current.wrapper then
        current.tool[fn] = current.hadOwn and current.original or nil
    end
end

-- Takes the wraps off again, each only when it is still the wrapper put there.
local function liftWraps()
    unwrapInstance()
    for _, wrap in pairs(wraps) do
        if wrap.shared then
            VRCompanion:releaseLookRayWrap("objectGrab")  -- Core lifts it when Hand Tools do not hold it either
        elseif wrap.class[wrap.fn] == wrap.wrapper then
            wrap.class[wrap.fn] = wrap.original
        end
    end
    wraps = {}
end

-- Object Grab cannot rely on the game's functions: ONE warning, every wrap lifted, so that picking up is exactly the game's. Hand
-- Tools are not touched.
local function giveUp(problem)
    state.problem = problem
    liftWraps()
    print("[FS25_VR] [Warning] Object Grab disabled: " .. problem .. ". Picking up is unchanged.")
end

-- At map load: gives up (once) when a function the wraps expect is missing or reshaped.
local function checkWraps()
    if state.problem ~= nil or not gameHasHands() then return state.problem end
    for name, spec in pairs(WRAP_SPECS) do
        local class = engine(spec.class)
        if type(class) == "table" then
            local current = wraps[name]
            local fn = spec.fn or name
            local problem = shapeProblem(spec, current ~= nil and current.original or class[fn])
            if problem ~= nil then
                giveUp(spec.class .. "." .. fn .. ": " .. problem)
                return state.problem
            end
        end
    end
    return nil
end

local function wrapStatus(name)
    if state.problem ~= nil then return name .. " wrap: gave up (" .. state.problem .. ")" end
    return name .. " wrap: " .. (wraps[name] ~= nil and "installed" or "not installed")
end

-- ---- Aiming -----------------------------------------------------------------------------------------------------------

local function localPlayer()
    local player = engine("g_localPlayer")
    if player == nil then
        local mission = engine("g_currentMission")
        player = type(mission) == "table" and mission.player or nil
    end
    return player
end

-- The game's hands tool when it is the hand tool the player holds (empty hands), or nil.
local function emptyHands(player)
    if type(player) ~= "table" or type(player.getHeldHandTool) ~= "function" then return nil end
    local ok, held = pcall(player.getHeldHandTool, player)
    if ok and type(held) == "table" and held.spec_hands ~= nil then return held end
    return nil
end

-- The look ray the Pointer Hand gives `player` now (x, y, z, dirX, dirY, dirZ), or nil and why the hand does not aim. Read live,
-- never cached, by the look ray wrap, the draw wrap and the update.
local function whyNotAiming(self, player)
    if self.objectGrabEnabled == false then return "Object Grab is off" end
    if self.motionControllersEnabled == false then return "Motion Controllers are off" end
    if state.problem ~= nil then return "Object Grab gave up (" .. state.problem .. ")" end
    if type(player) ~= "table" or player ~= localPlayer() then return "not the local player" end
    if player.camera == nil or player.camera.isFirstPerson ~= true then return "not in the first person view" end
    if self:getControlledVehicle() ~= nil then return "in a vehicle" end
    if self:doesMenuTakeInput() then return "a menu takes the input" end
    if emptyHands(player) == nil then return "the hands are not empty" end
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

function VRCompanion:getObjectGrabAimedRay(player)
    return aimedRay(self, player)
end

-- Core asks for the ray of this subsystem after Hand Tools'.
VRCompanion:registerLookRayAimer("objectGrab", function(player)
    return aimedRay(VRCompanion, player)
end)

-- ---- The grip ---------------------------------------------------------------------------------------------------------
-- The grip is the Virtual Gamepad's LB / RB (ADR-0028), and what counts as squeezed is what the pad reads: the Injector owns the
-- threshold and its hysteresis, and an untracked hand's grip is not held. Object Grab takes a hand's grip from the pad while that hand
-- points at something the hands can pick up and while it carries, and acts on a squeeze only when the pad let it have the grip (a grip
-- already held when the use would begin keeps its binding, and does not grab, until it is let go).

-- Whether the hand's grip is held as the pad reads it.
local function padGripHeld(hand)
    local bridge = engine("VRBridge")
    if type(bridge) ~= "table" or type(bridge.getVirtualGamepad) ~= "function" then return false end
    local ok, _, _, _, leftHeld, rightHeld = pcall(bridge.getVirtualGamepad)
    if not ok then return false end
    if hand == VRCompanion.MOTION_HAND_LEFT then return leftHeld == true end
    return rightHeld == true
end

-- Once per update: which grips went down this update (state.gripPressed), whatever the hand points at.
local function trackGrips()
    for _, hand in ipairs({ VRCompanion.MOTION_HAND_LEFT, VRCompanion.MOTION_HAND_RIGHT }) do
        local down = padGripHeld(hand)
        state.gripPressed[hand] = down and not state.gripDown[hand]
        state.gripDown[hand] = down
    end
end

-- Asks the Virtual Gamepad for the hand's grip (every update the use wants it). False: it was held first, and keeps its binding.
local function takeGrip(self, hand)
    if type(self.takeVirtualGamepadInput) ~= "function" then return true end
    return self:takeVirtualGamepadInput(hand, "grip")
end

-- ---- Placing the Held Object ------------------------------------------------------------------------------------------------
-- The game carries the object with two nodes: a kinematic node (a physics body the object's joint hangs from) and a rotation node under
-- it, which the joint's frame follows. The game places the kinematic node along the look ray, level, every update
-- (HandToolHands:updateKinematicNode). While the hand carries, Object Grab places them from the hand instead, so that the object keeps
-- the place and angle it had in the hand when it was grabbed (OFFSET), and moves and turns with the wrist.
-- THE MULTIPLAYER SHAPE (from the game source): the owning client streams to the server only the kinematic node's world position, its
-- level heading and the rotation node's WORLD rotation. The rotation node's position under the kinematic node is never streamed: the
-- server sets it at the grab. So the kinematic node stays level, the wrist's tilt goes into the rotation node, and the kinematic
-- node is placed so that the rotation node's offset (read each update: it is the game's) lands where the hand holds the object. The
-- server applies the rotation node to the joint with setJointFrame, as the game's rotate action does; a single player game is the
-- server and has no stream, so it does that itself. The dirty flag is raised, as the game's own update raises it.
-- THE OFFSET is recorded once the rotation node is at the object. The game moves it there itself (orientRotationNodeToItem): at once
-- on a client, but on the server only when the joint is made, a few physics frames after the pick-up. Until then the game's own
-- placement runs.

local function qrotate(qx, qy, qz, qw, x, y, z)
    local cx, cy, cz = qy * z - qz * y + qw * x, qz * x - qx * z + qw * y, qx * y - qy * x + qw * z
    return x + 2 * (qy * cz - qz * cy), y + 2 * (qz * cx - qx * cz), z + 2 * (qx * cy - qy * cx)
end

local handPose = {}  -- scratch: the hand's grip pose in the world

-- The grip pose of the hand in the world (x, y, z, qx, qy, qz, qw in the returned table, valid until the next call), or nil while the
-- hand is not tracked or the head is not placed in the world.
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

-- The hands tool's kinematic node and rotation node (the game's getKinematicNode), or nil while they are not there.
local function carryNodes(hands)
    if type(hands.getKinematicNode) ~= "function" then return nil end
    local ok, kinematic, rotation = pcall(hands.getKinematicNode, hands)
    if not ok or kinematic == nil or kinematic == 0 or rotation == nil or rotation == 0 then return nil end
    return kinematic, rotation
end

-- Records where the Held Object is in the hand's frame (carry.offset), from the rotation node, which the game puts at the object. Not
-- yet while the game has the rotation node still to move (the server's joint is made a few physics frames after the pick-up), or the
-- hand cannot be placed.
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
    local ix, iy, iz, iw = -hand.qx, -hand.qy, -hand.qz, hand.qw  -- the hand's rotation undone
    local ox, oy, oz = qrotate(ix, iy, iz, iw, x - hand.x, y - hand.y, z - hand.z)
    local rx, ry, rz, rw = self.multiplyQuaternions(ix, iy, iz, iw, qx, qy, qz, qw)
    carry.offset = { x = ox, y = oy, z = oz, qx = rx, qy = ry, qz = rz, qw = rw }
end

-- Places the kinematic node and the rotation node of the carrying `tool` from the hand: the object at the hand's pose times the
-- offset. A hand that cannot be placed (tracking lost) leaves the nodes where they were.
local function placeFromHand(self, tool, carry)
    local kinematic, rotation = carryNodes(tool)
    if kinematic == nil then return end
    local spec = tool.spec_hands
    if type(tool.raiseDirtyFlags) == "function" and spec ~= nil then pcall(tool.raiseDirtyFlags, tool, spec.dirtyFlag) end
    local hand = handGripInWorld(self, carry.hand)
    if hand == nil then return end
    local offset = carry.offset
    -- the rotation node's world pose: the hand's, times the offset
    local ox, oy, oz = qrotate(hand.qx, hand.qy, hand.qz, hand.qw, offset.x, offset.y, offset.z)
    local px, py, pz = hand.x + ox, hand.y + oy, hand.z + oz
    local qx, qy, qz, qw = self.multiplyQuaternions(hand.qx, hand.qy, hand.qz, hand.qw, offset.qx, offset.qy, offset.qz, offset.qw)
    -- the kinematic node's level heading: the way the object faces (when it faces straight up or down it has no heading to give, and
    -- the last one is kept). The rotation node is placed in the world, so the heading does not matter to where the object is.
    local fx, _, fz = qrotate(qx, qy, qz, qw, 0, 0, 1)
    if fx * fx + fz * fz > 0.0025 then carry.heading = math.atan2(fx, fz) end
    local heading = carry.heading or 0
    local yawX, yawY, yawZ, yawW = 0, math.sin(heading / 2), 0, math.cos(heading / 2)
    -- the kinematic node goes where the rotation node's offset from it (the game's) lands on the place the hand holds the object
    local lx, ly, lz = engine("getTranslation")(rotation)
    local rx, ry, rz = qrotate(yawX, yawY, yawZ, yawW, lx, ly, lz)
    engine("setWorldTranslation")(kinematic, px - rx, py - ry, pz - rz)
    engine("setWorldQuaternion")(kinematic, yawX, yawY, yawZ, yawW)
    engine("setWorldQuaternion")(rotation, qx, qy, qz, qw)
    local jointId = spec ~= nil and spec.heldItemJointId or nil
    if tool.isServer == true and jointId ~= nil and type(engine("setJointFrame")) == "function" then
        pcall(engine("setJointFrame"), jointId, 0, rotation)
    end
end

-- The wrap of the game's updateKinematicNode: the carry places the nodes from the hand once the offset is recorded, and the game
-- places them otherwise (another player's hands, a pick-up by the game's own button, a grab not settled yet).
WRAP_SPECS.handsUpdateKinematicNode.make = function(original)
    return function(tool, ...)
        local carry = state.carry
        if carry ~= nil and carry.hands == tool and carry.offset ~= nil then
            return placeFromHand(VRCompanion, tool, carry)
        end
        return original(tool, ...)
    end
end

-- The game registers updateKinematicNode on the hands tool's type, and the type's functions may be copied onto the tool: a wrap of the
-- class does not reach a tool that holds its own copy (the trap of Wheel Grab's steering wrap). So the local player's hands tool is
-- wrapped itself, for as long as it is the hands, unless the class wrap reaches it.
local function wrapInstance(tool)
    local spec = WRAP_SPECS.handsUpdateKinematicNode
    local current = instanceWrap
    if current ~= nil and current.tool == tool and rawget(tool, spec.fn) == current.wrapper then return end
    if current ~= nil then unwrapInstance() end
    local original = tool[spec.fn]
    local classWrap = wraps.handsUpdateKinematicNode
    if shapeProblem(spec, original) ~= nil or (classWrap ~= nil and original == classWrap.wrapper) then return end
    local wrapper = spec.make(original)
    instanceWrap = { tool = tool, original = original, hadOwn = rawget(tool, spec.fn) ~= nil, wrapper = wrapper }
    tool[spec.fn] = wrapper
end

-- ---- The grab ---------------------------------------------------------------------------------------------------------------

-- Whether the hands tool holds something (the game's own getIsHoldingItem).
local function holdsItem(hands)
    if type(hands) ~= "table" or type(hands.getIsHoldingItem) ~= "function" then return false end
    local ok, holding = pcall(hands.getIsHoldingItem, hands)
    return ok and holding == true
end

-- The game picks the target up (pickUpTarget, with the targeter's own target: the game decides, and sends the event) and the hand
-- carries it from now on. One pulse on the hand that grabbed.
local function startGrab(self, hand, hands, target)
    if type(hands.pickUpTarget) ~= "function" then return end
    pcall(hands.pickUpTarget, hands, target)
    if not holdsItem(hands) then return end
    state.carry = { hand = hand, hands = hands, offset = nil, heading = nil }
    recordGrabOffset(self, state.carry)
    self:pulseHand(hand, self.OBJECT_GRAB_GRAB_PULSE_MS, self.OBJECT_GRAB_GRAB_PULSE_STRENGTH)
end

-- The Pointer Hand is on an object the hands can pick up (`target`, the targeter's): a squeeze of its grip that the pad let the hand
-- have grabs it. `waiting`: the squeeze was an off-hand's, made a few updates ago (see "The off-hand").
local function tryGrab(self, player, target, waiting)
    local hand = self.pointerHand
    if hand == nil or state.carry ~= nil then return end
    if not takeGrip(self, hand) or not (state.gripPressed[hand] or waiting) then return end
    state.pendingGrab = nil
    local hands = emptyHands(player)
    if hands ~= nil then startGrab(self, hand, hands, target) end
end

-- ---- The off-hand -----------------------------------------------------------------------------------------------------------
-- While both hands are in use with empty hands, the hand that does not point is checked with a physics ray cast along its finger (the
-- Pointer Ray's ray, from the same place behind the fingertip as the look ray), ONLY to learn whether it points at something the
-- game's getCanPickUpNode accepts. While it does, its grip is taken from the pad, so that squeezing it does not reach the game first
-- (the held-input rule would hand that squeeze to the binding). The squeeze makes that hand the Pointer Hand (see
-- choosePointerHand), the targeter is run again with its ray, and the game's pickUpTarget picks up what the TARGETER finds. The check
-- never decides what is picked up: when the targeter finds nothing the game can pick up, nothing is.
-- The targeter's ray cast is asynchronous in the game, so its answer to the new ray may come a few updates later: the squeeze waits for
-- it (OBJECT_GRAB_ANSWER_WAIT_MS) for as long as the grip stays down.

local OFF_HAND_MINIMUM_DISTANCE = 0.5  -- the hands' target type finds nothing nearer to the ray's origin (HandToolHands:onHeldStart)

local checkPose = {}   -- scratch: the off-hand's finger in the world
local checkHit = {}    -- scratch: what the check's ray cast hit

-- The callback of the physics ray cast: the closest hit.
local function onCheckHit(self, node, x, y, z, distance)
    self.node, self.distance = node, distance
    return true
end
checkHit.onHit = onCheckHit

-- Whether the hand's finger points at a node the hands can pick up (the game's getCanPickUpNode says).
local function pointsAtGrabbable(self, hands, hand)
    local rayCast, key = engine("raycastClosest"), engine("HandToolHands")
    local spec = hands.spec_hands
    if type(rayCast) ~= "function" or type(key) ~= "table" or type(hands.getCanPickUpNode) ~= "function" or type(spec) ~= "table" then
        return false
    end
    local tx, ty, tz, tdx, tdy, tdz = self:getPointerRayInTrackingSpace(hand)
    if tx == nil then return false end
    -- tracking space to the world: a pose with no rotation of its own comes out as the rotation of the tracking space
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

-- Once per update, after the grips are tracked and the carry is settled: takes the grip of the off-hand that points at something
-- grabbable, and notes the one whose squeeze the pad let it have.
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

-- Asked by the Pointer Hand's choice: the hand's grip was squeezed this update as it points at something the hands can pick up, and
-- the pad let it have the grip. The hand becomes the Pointer Hand, and grabs there.
function VRCompanion:isObjectGrabSqueeze(hand)
    return state.offHandSqueezed == hand
end

-- Runs the targeter again, now that the Pointer Ray is the new Pointer Hand's. What it found for the hands before (with the ray of the
-- hand that pointed) is forgotten first, so that the squeeze can only pick up what the new ray finds.
local function runTargeterAgain(player)
    local targeter, key = player.targeter, engine("HandToolHands")
    if type(targeter) ~= "table" or type(targeter.update) ~= "function" then return end
    if type(key) == "table" and type(targeter.closestTargetsByKey) == "table" then targeter.closestTargetsByKey[key] = nil end
    pcall(targeter.update, targeter, state.dt)
end

-- The squeeze of `hand` that made it the Pointer Hand, called from the first look at the Pointer Ray after it.
local function startWaitingForTheTargeter(self, player, hand)
    local now = self:getCurrentTimeMs()
    state.pendingGrab = now ~= nil and { hand = hand, untilMs = now + self.OBJECT_GRAB_ANSWER_WAIT_MS } or nil
    runTargeterAgain(player)
end

-- Whether the hand's squeeze is still waiting for the targeter's answer: the hand is still the Pointer Hand, the grip is still down
-- and the wait is not over.
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

-- ---- The release ------------------------------------------------------------------------------------------------------------

-- The grip of the carrying hand was let go: what happens to the Held Object. The decision is this one function. (A drop through the
-- game's dropHeldItem; a swing will throw.)
local function releaseHeldObject(self, carry)
    local hands = carry.hands
    if type(hands.dropHeldItem) == "function" then pcall(hands.dropHeldItem, hands) end
end

-- ---- The carry --------------------------------------------------------------------------------------------------------------

-- Whether the hand is carrying a Held Object it grabbed.
function VRCompanion:isHandCarryingHeldObject(hand)
    return state.carry ~= nil and state.carry.hand == hand
end

-- Whether the carry has ended without the grip being let go: the game dropped the object itself (getting into a vehicle, taking a Hand
-- Tool, the object gone), or Object Grab or the Motion Controllers were switched off, or the hands are not the hands any more.
local function carryHasEnded(self, carry)
    if self.objectGrabEnabled == false or self.motionControllersEnabled == false or state.problem ~= nil then return true end
    return emptyHands(localPlayer()) ~= carry.hands or not holdsItem(carry.hands)
end

-- Whether the carrying hand let go of its grip. A hand that is not tracked has not (nothing is known of it until it is tracked again).
local function gripWasLetGo(self, carry)
    local pose = self:getHandState(carry.hand)
    return type(pose) == "table" and pose.tracked == true and not state.gripDown[carry.hand]
end

local function updateCarry(self)
    local carry = state.carry
    if carry == nil then return end
    if carryHasEnded(self, carry) then
        state.carry = nil  -- the grip goes back to the pad once it is let go (the Virtual Gamepad keeps what a use took off it)
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

-- Once per Motion Controller update, after the hands are polled and before the Pointer Hand is chosen.
function VRCompanion:updateObjectGrab(dt)
    state.dt = dt or 0
    trackGrips()
    if state.problem == nil then
        local hands = emptyHands(localPlayer())
        if hands ~= nil then wrapInstance(hands) end
    end
    updateCarry(self)
    updateOffHand(self)
end

-- ---- The Pointer Ray on an object -------------------------------------------------------------------------------------

-- The targeter's closest target for the hands when the hands can pick it up (the game decides): node, x, y, z. The target table is the
-- targeter's pooled one and is read here, never kept.
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

-- Called by updatePointerRay, once per update after the Pointer Ray is placed: whether the ray is on an object the hands can pick up,
-- and where. The Pointer Hand gets one light pulse as it lands, and squeezing its grip there grabs the object (see "The grab").
function VRCompanion:getObjectGrabRayTarget()
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
    if node ~= state.landedNode then
        state.landedNode = node
        self:pulsePointerHand(self.OBJECT_GRAB_LAND_PULSE_MS, self.OBJECT_GRAB_LAND_PULSE_STRENGTH)
    end
    tryGrab(self, player, target, waiting)
    return true, x, y, z
end

-- ---- No centre crosshair ----------------------------------------------------------------------------------------------
-- The hands tool's onDraw is read off the specialization class by the game's event dispatch at every call, so the wrap is on the class.

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

-- ---- Settings, persistence, console commands --------------------------------------------------------------------------

local SETTING_KEY_ENABLED = "vrSettings.objectGrab"

function VRCompanion:setObjectGrabEnabled(enabled, save)
    self.objectGrabEnabled = (enabled ~= false)
    if save and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
end

-- Called from the Motion Controller settings with the open settings file and the engine's setters (any may be nil).
function VRCompanion:saveObjectGrabSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY_ENABLED, self.objectGrabEnabled ~= false)
    end
end

-- Called from the Motion Controller settings with the open settings file and the engine's getters (any may be nil).
-- A key that is missing keeps the current value.
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
            mission:showBlinkingWarning(string.format("VR Object Grab: %s (Saved)", enabled and "on" or "off"), 2500)
        end)
    end
    print(string.format("[FS25_VR] Object Grab set to: %s", enabled and "on" or "off"))
    return string.format("Object Grab set to: %s (Saved)", enabled and "on" or "off")
end

-- The state of Object Grab as a list of fields: whether it is on, whether the hand aims now and if not why, the wraps.
local function diagnosticFields(self)
    local aiming, why = aimedRay(self, localPlayer())
    return {
        string.format("Object Grab: %s", self.objectGrabEnabled ~= false and "on" or "off"),
        aiming ~= nil and "aiming: the Pointer Hand" or ("aiming: no (" .. tostring(why) .. ")"),
        wrapStatus("getLookRay"),
        wrapStatus("handsOnDraw"),
        wrapStatus("handsUpdateKinematicNode"),
    }
end

-- One line (used by the Pointer diagnostics).
function VRCompanion:getObjectGrabDiagnostics()
    return table.concat(diagnosticFields(self), " | ")
end

function VRCompanion:registerObjectGrabConsoleCommands()
    wrapAll()    -- map load: the classes again (resetObjectGrab lifted the wraps with the last map)
    checkWraps()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrObjectGrab", "Turn Object Grab, picking up objects aimed by a Motion Controller, on or off (saved): vrObjectGrab [on|off]", "consoleCommandSetObjectGrab", self)
end

-- Map unload: the wraps come off and everything seen this session is forgotten. The saved setting stays as it is.
function VRCompanion:resetObjectGrab()
    liftWraps()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrObjectGrab")
    end
    state = newState()
end

wrapAll()  -- the game's classes are there as the files load

VRCompanion.ObjectGrab.getObjectGrabRayTarget = function(self, ...) return VRCompanion.getObjectGrabRayTarget(VRCompanion, ...) end
VRCompanion.ObjectGrab.consoleCommandSetObjectGrab = function(self, ...) return VRCompanion.consoleCommandSetObjectGrab(VRCompanion, ...) end

return VRCompanion.ObjectGrab
