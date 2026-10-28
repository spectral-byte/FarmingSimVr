-- ====================================================================
-- FS25 VR Companion Mod: Motion Controller Subsystem
--
-- Makes the player's Motion Controllers visible to the rest of the mod: it polls each hand's state
-- from the Injector once per update, decides which hands are in use, picks the Pointer Hand, and owns
-- the `vrMotionControllers` setting and the status console command. Motion Controller input is optional;
-- everything here is inert when VRBridge.getHandState is missing or the runtime offers no Motion Controllers.
--
-- PUBLIC SURFACE (other modules build on it; see ADR-0025)
--
--   Hand ids          VRCompanion.MOTION_HAND_LEFT = 0, VRCompanion.MOTION_HAND_RIGHT = 1.
--                     The same ids VRBridge.getHandState and VRBridge.pulseHaptic use. Anywhere a
--                     hand is a parameter it is one of these two numbers.
--
--   VRBridge.getHandState(hand [, out]) -> table   (the bridge call, filled in place like getHeadPose)
--     tracked            boolean  aim pose valid and the Motion Controller active (false when the
--                                 runtime has no Motion Controllers, refused the action set, or it is off)
--     x, y, z            metres   aim pose position, tracking space, SAME convention, reference space
--     pitch, yaw, roll   radians  and predicted display time as VRBridge.getHeadPose (follows the
--     qx, qy, qz, qw              Recenter Action). Compose it into the world exactly like the head.
--     isValid, isPositionTracked, isOrientationTracked   booleans, as in getHeadPose
--     gripPoseTracked    boolean  the grip pose (where a Virtual Hand is drawn) is located; its own flag, independent
--                                 of the aim pose's: false when the hand is untracked, when the runtime refuses the grip pose
--                                 or when it can not locate it
--     gripPoseX/Y/Z, gripPosePitch/Yaw/Roll, gripPoseQx/Qy/Qz/Qw   the grip pose, same conventions, reference
--                                 space and predicted display time as the aim pose above (identity when untracked)
--     trigger, grip      0..1     analog value (Khronos simple controller profile: select/click as trigger)
--     thumbX, thumbY     -1..1    thumbstick (Vive: trackpad); +X right, +Y up
--     thumbClick         boolean  thumbstick (Vive: trackpad) click
--     buttonLower        boolean  lower face button: Touch X (left) / A (right), Index A
--     buttonUpper        boolean  upper face button: Touch Y (left) / B (right), Index B
--     menu               boolean  menu button (Touch: left hand only; Index has none)
--     An untracked hand reports tracked = false, isValid = false, an identity aim pose and zeroed inputs.
--
--   Per-frame cached state (updated once per VRCompanion:update by updateMotionControllers(dt)):
--     VRCompanion:getHandState(hand) -> table   the bridge fields above, the same table every frame
--                                               (filled in place; read it, never keep or edit it).
--                                               Untracked defaults when Motion Controllers are off.
--     VRCompanion:isHandInUse(hand) -> boolean  tracked and moved within MOTION_CONTROLLER_IN_USE_MS
--     VRCompanion:getPointerHand() -> 0 | 1 | nil
--     VRCompanion.pointerHand                    mirror of getPointerHand() (read-only for others)
--       Pointer Hand rule of this module: exactly one hand in use -> that hand; none in use -> nil;
--       both in use -> the previous Pointer Hand, until a trigger pull on the other hand moves it
--       there (with no previous Pointer Hand, a pull on either hand picks that hand). That switching
--       pull is consumed: getPointerTrigger() reports it as neither pressed nor held until the
--       trigger is let go, so it never clicks in a menu either.
--
--   VRCompanion.motionControllersEnabled  boolean, default true, saved (`vrMotionControllers on|off`).
--     When false the mod ignores Motion Controller input: hands report untracked, none is in use, there is
--     no Pointer Hand. The Injector session and its action set are never touched by this setting.
--
--   Who controls the pointer (read by menus and by Interactive Control selection)
--     The pointer is the cursor / selection the player aims: either the desk mouse or the Pointer Ray.
--     Whichever moved last controls it. Starts with the mouse; the ray takes control when the Pointer
--     Hand moves (the same "moved" that puts a hand in use), the mouse takes it back when it moves.
--     VRCompanion:getPointerOwner() -> "mouse" | "ray"   (VRCompanion.POINTER_CONTROL_MOUSE / _RAY)
--       "ray" only while the ray is the last mover AND a Pointer Hand is in use (so Motion Controllers
--       off, an untracked hand and the 3 s idle timeout all read as "mouse").
--     VRCompanion:isPointerRayInControl() -> boolean   (getPointerOwner() == "ray")
--     VRCompanion:notePointerRayMoved() / VRCompanion:notePointerMouseMoved()   hand the control over;
--       called by this module (hand moves; mouseEvent listener; sampling g_inputBinding:getMousePosition)
--       and callable from tests or other modules that see either one move.
--
--   The Pointer Ray (see "The Pointer Ray" below, and VRCompanionInteractiveControl.lua)
--     VRCompanion:getPointerRay() -> table | nil   world origin / direction of the ray this update: it starts at the
--                                                  Pointer Hand's index fingertip and runs along the finger
--     VRCompanion:getPointerRayInTrackingSpace(hand) -> ox, oy, oz, dx, dy, dz, source | nil   the same ray before
--                                                  the head carries it into the world (for the menu / 2D panel hit)
--     VRCompanion.POINTER_RAY_FOLLOWS_FINGER_DIRECTION  true (default): along the finger; false: keeps the aim direction
--     VRCompanion:getPointerTrigger() -> pressed, held   the Pointer Hand's trigger (hysteresis 0.6 / 0.4)
--     VRCompanion.pointerConeDegrees               saved by `vrPointerCone <degrees>` (default 5)
--     VRCompanion:draw()                           the line and dot, drawn from the update's end points
--
--   Haptics (VRBridge.pulseHaptic(hand, durationMs, strength) -> boolean)
--     VRCompanion:pulseHand(hand, durationMs, strength) -> boolean   one hand; false when off or the runtime refuses
--     VRCompanion:pulsePointerHand(durationMs, strength) -> boolean   Pointer Hand only; false when off
--     VRCompanion:onClickPointEvent(kind, clickPoint)   "selected" (non-nil point) and "clicked" pulse
--
--   Time: every time in this module is VRCompanion:getCurrentTimeMs(), i.e. the global g_time in
--   milliseconds (a wall clock only when g_time is nil). Tests drive it by assigning _G.g_time.
--
--   Hooks into the rest of the mod (each a single guarded call):
--     Core.lua    loadMap -> registerMotionControllerConsoleCommands()
--                 runUpdate -> updateMotionControllers(dt)
--                 deleteMap -> resetMotionControllers()
--     Config.lua  saveConfiguration -> saveMotionControllerSettings(...)
--                 loadConfiguration -> loadMotionControllerSettings(...)
--     Locomotion  onRecenterAction -> resetMotionControllerMovement() (a recenter moves every pose)
--     MenuPointer updateMotionControllers -> updateMenuPointer(dt) at the end of every update, and
--                 resetMotionControllers -> resetMenuPointer() (VRCompanionMenuPointer.lua)
--     VirtualHands updateMotionControllers -> updateVirtualHands(dt) at the end of every update (also when input is
--                 ignored, so the hands hide), resetMotionControllers -> resetVirtualHands(), and the console
--                 command registers next to the others (VRCompanionVirtualHands.lua). The Virtual Hands place the grip
--                 pose through VRCompanion:headRelativePoseToWorld, the transform the Pointer Ray uses too.
--     WheelGrab   updateMotionControllers -> updateWheelGrab(dt) after the hands are polled and before the Pointer Hand is chosen
--                 (also when input is ignored, or the Injector offers no hand states, so a held Steering Wheel is let go), the saved setting rides on
--                 save/loadMotionControllerSettings (saveWheelGrabSetting / loadWheelGrabSetting), the console commands register
--                 next to the others and resetMotionControllers -> resetWheelGrab() (VRCompanionWheelGrab.lua). A hand
--                 holding the Steering Wheel (isHandHoldingSteeringWheel) stays in use but is never a Pointer Hand
--                 candidate, so it casts no Pointer Ray; Interactive Control's auto-on keeps its state while it holds.
--     HandTools   updateMotionControllers -> updateHandTools(dt) on every path, after the hands are polled and before the Pointer Hand
--                 is chosen (also when input is ignored, or the Injector offers no hand states, so a Hand Tool goes back to
--                 where the game puts it), the saved setting rides on save/loadMotionControllerSettings (saveHandToolsSetting /
--                 loadHandToolsSetting), the console commands register next to the others and resetMotionControllers ->
--                 resetHandTools() before the Virtual Hands go (VRCompanionHandTools.lua, ADR-0029). The Tool Hand holding a
--                 supported Hand Tool (isHandHoldingHandTool) is never a Pointer Hand candidate in the world, as a hand on the
--                 Steering Wheel is not; in a menu either hand can. While its Hand Tool keeps it in use
--                 (isHandKeptInUseByHandTool) the Tool Hand stays in use however still it is held, so a held-still cut or spray
--                 does not drop the tool.
--     ObjectGrab  updateMotionControllers -> updateObjectGrab(dt) on every path (the grip edges and the carry), updatePointerRay ->
--                 peekObjectGrabRayTarget() and settleObjectGrabRayTarget() (VRCompanionObjectGrab.lua, ADR-0033; the object is looked
--                 at first, settled once the nearer of it and an Animated Object is known). A hand carrying a Held Object
--                 (isHandCarryingHeldObject) stays in use however still it is held and is never a Pointer Hand candidate.
--     AnimatedObjects registerMotionControllerConsoleCommands -> loadAnimatedObjectsSupport() at map load (wraps the game's trigger
--                 callback to follow the zones), updatePointerRay -> updateAnimatedObjects(dt, suppressed, rivalDistance) after
--                 Interactive Control, between Object Grab's peek and settle (the nearer wins; also, as suppressed, on the paths
--                 where input is ignored, so the selection clears), getPointerDiagnostics -> getAnimatedObjectsDiagnostics(),
--                 resetMotionControllers -> resetAnimatedObjectsSupport() (VRCompanionAnimatedObjects.lua, ADR-0034). While the
--                 ray is on one, the Pointer Hand's trigger is taken off the Virtual Gamepad and a pull runs the game's own toggle.
--     VirtualGamepad updateMotionControllers -> updateVirtualGamepad(dt) first of all (also when input is ignored), the saved
--                 setting rides on save/loadMotionControllerSettings, the status command ends with the pad's state and
--                 the console command registers next to the others (VRCompanionVirtualGamepad.lua, ADR-0028)
--     The buttons, triggers and thumbsticks do nothing here beyond the uses above (pointing, Click Points, menus, Wheel Grab):
--                 Snap Turn, the Recenter Action and the HUD Toggle Action from the Motion Controllers arrive only as game actions
--                 bound on the Virtual Gamepad (ADR-0028). The old button mapping and its settings are gone.
-- ====================================================================

VRCompanion = VRCompanion or {}

VRCompanion.MOTION_HAND_LEFT = 0
VRCompanion.MOTION_HAND_RIGHT = 1
VRCompanion.pointerHand = nil

-- The two things that can control the pointer (see "Who controls the pointer" above).
VRCompanion.POINTER_CONTROL_MOUSE = "mouse"
VRCompanion.POINTER_CONTROL_RAY = "ray"
-- The desk mouse counts as moved when its normalized position changes by more than this (about a pixel).
VRCompanion.POINTER_MOUSE_MOVE_EPSILON = 0.0005

-- vrMotionControllers on|off: saved, default on. Off makes the mod ignore Motion Controller input; it never
-- touches the Injector's session or action set.
VRCompanion.DEFAULT_MOTION_CONTROLLERS_ENABLED = true
VRCompanion.motionControllersEnabled = VRCompanion.DEFAULT_MOTION_CONTROLLERS_ENABLED

-- A hand is in use while it is tracked and has moved within this long (ms).
VRCompanion.MOTION_CONTROLLER_IN_USE_MS = 3000
-- "Moved" means the aim pose left the position (metres) or orientation (radians) it was last at by
-- more than this. Measured from the last pose that counted, so a slow drift adds up to a move while
-- a Motion Controller lying on a desk (sensor noise well below a millimetre) never does.
VRCompanion.MOTION_CONTROLLER_MOVE_DISTANCE = 0.01
VRCompanion.MOTION_CONTROLLER_MOVE_ANGLE = 0.035

-- vrPointerCone <degrees>: saved. A Click Point is selected only within this angle of the Pointer Ray's
-- centre, measured at the hand.
VRCompanion.DEFAULT_POINTER_CONE_DEGREES = 5
VRCompanion.MIN_POINTER_CONE_DEGREES = 0.5
VRCompanion.MAX_POINTER_CONE_DEGREES = 45
VRCompanion.pointerConeDegrees = VRCompanion.DEFAULT_POINTER_CONE_DEGREES

-- The trigger counts as pulled above PRESS and as released below RELEASE (hysteresis against a
-- trigger that rests half-way).
VRCompanion.POINTER_TRIGGER_PRESS = 0.6
VRCompanion.POINTER_TRIGGER_RELEASE = 0.4
-- Length of the drawn Pointer Ray (m) while it ends on nothing.
VRCompanion.POINTER_RAY_LENGTH = 3

local HANDS = { 0, 1 }

-- State of the subsystem. Rebuilt by resetMotionControllers().
local function newState()
    return {
        hands = {},      -- [hand] = table filled in place by VRBridge.getHandState
        motion = {},     -- [hand] = { ref = {x,y,z,qx,qy,qz,qw} | nil, lastMovedMs = number | nil }
        inUse = {},      -- [hand] = boolean
        triggerDown = {},     -- [hand] = trigger is pulled (with hysteresis)
        triggerPressed = {},  -- [hand] = it went down this update
        triggerConsumed = {}, -- [hand] = a Pointer Hand switch took this pull: it counts for nothing until let go
        ray = { valid = false, ox = 0, oy = 0, oz = 0, dx = 0, dy = 0, dz = -1 },
        fingerLogged = {},    -- [hand] = the line about the ray starting at the index finger was logged this map
        headScratch = nil,    -- table VRBridge.getHeadPose fills in place
        pointerHand = nil,
        ignoredCleared = false,  -- Motion Controller input was ignored and the cached hands were already cleared
        bridgeMissing = false,   -- the Injector offers no VRBridge.getHandState
        pointerOwner = VRCompanion.POINTER_CONTROL_MOUSE,  -- see "Who controls the pointer"
        mousePosition = nil,     -- {x, y}: where the desk mouse was last seen
        movedThisUpdate = {},    -- [hand] = true when the hand's aim moved far enough to count this update
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

-- An untracked, zeroed hand: what everyone sees while there is nothing to report.
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

-- Whether the aim pose moved away from the reference pose far enough to count; the pose becomes the
-- new reference when it did.
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

-- The Pointer Hand: the one hand in use. With both in use it is the previous one, until the trigger is
-- pulled on the other hand: that pull moves the Pointer Hand there and is consumed (it clicks nothing,
-- see getPointerTrigger). With both in use and no previous Pointer Hand, a pull on either picks it.
-- A hand holding the Steering Wheel is in use but never a candidate (Wheel Grab, see VRCompanionWheelGrab.lua): the
-- Pointer Hand that starts holding falls to the other hand in use, or to nobody, and can point again once it lets go.
local function isHoldingSteeringWheel(self, hand)
    return type(self.isHandHoldingSteeringWheel) == "function" and self:isHandHoldingSteeringWheel(hand) == true
end

-- The Tool Hand holding a supported Hand Tool is never a candidate either, as it uses the tool with its trigger (Hand Tools,
-- ADR-0029). That holds in the world only: in a menu the tool is back where the game puts it and either hand can point.
local function isHoldingHandTool(self, hand)
    return not self:isMenuQuadShown() and type(self.isHandHoldingHandTool) == "function" and self:isHandHoldingHandTool(hand) == true
end

-- The hand carrying a Held Object (Object Grab, ADR-0033) is never a candidate either: no ray sticks out of the object it holds.
local function isCarryingHeldObject(self, hand)
    return type(self.isHandCarryingHeldObject) == "function" and self:isHandCarryingHeldObject(hand) == true
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
            state.triggerConsumed[hand] = true  -- until the trigger is let go
            return hand
        end
        -- So does squeezing the grip of a hand that points at something it can grab (Object Grab): it grabs in that squeeze.
        if hand ~= previous and type(self.isObjectGrabSqueeze) == "function" and self:isObjectGrabSqueeze(hand) == true then
            return hand
        end
    end
    return previous
end

-- Puts every cached hand back to untracked and nobody in use; the tables stay the ones the bridge fills.
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
    state.ray.visible = false  -- the update that draws it does not run now: a ray left drawn would stay on its last target
    VRCompanion.pointerHand = nil
end

-- ---- Who controls the pointer ---------------------------------------------------------------
-- One place for "desk mouse or Pointer Ray: whichever moved last" (see the header for the API).

-- Logs who took the pointer (the first few times only, so a mouse that never settles cannot flood the log).
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

-- The desk mouse was seen at (x, y), either reported by the engine (isEvent: any event is the player
-- using the mouse) or sampled (a first sample only learns where it is). It took control when it moved.
local function noteDeskMouse(self, x, y, isEvent)
    if type(x) ~= "number" or type(y) ~= "number" then return end
    local last = state.mousePosition
    state.mousePosition = { x = x, y = y }
    local moved = isEvent
    if last ~= nil then
        local epsilon = self.POINTER_MOUSE_MOVE_EPSILON
        moved = moved or math.abs(x - last.x) > epsilon or math.abs(y - last.y) > epsilon
    end
    if moved then
        self:notePointerMouseMoved(isEvent and "mouse event" or "mouse position sampled")
    end
end

-- The engine tells mod listeners about the desk mouse. The mod's own synthetic GUI events never come
-- through here, so they never look like the mouse moving.
function VRCompanion:mouseEvent(posX, posY, isDown, isUp, button)
    noteDeskMouse(self, posX, posY, true)
end

-- Sampling covers an engine that does not deliver mouseEvent to the mod in this screen.
local function sampleDeskMouse(self)
    local inputBinding = g_inputBinding or (_G and _G.g_inputBinding)
    if inputBinding == nil or type(inputBinding.getMousePosition) ~= "function" then return end
    local ok, x, y = pcall(inputBinding.getMousePosition, inputBinding)
    if ok then
        noteDeskMouse(self, x, y, false)
    end
end

-- Interactive Control still has to see a Pointer Hand go away (its state is put back); a single guarded call.
local function settleInteractiveControl(self, dt)
    if type(self.updateInteractiveControl) == "function" then self:updateInteractiveControl(dt) end
    -- No Animated Object stays selected either (suppressed: nothing is selected, the trigger is not taken).
    if type(self.updateAnimatedObjects) == "function" then self:updateAnimatedObjects(dt, true) end
end

-- What reads the pointer after the hands were polled (menus); a single guarded call.
-- The Virtual Hands read the settled hands too (VRCompanionVirtualHands.lua), so they follow every exit
-- of the update, including the ones where Motion Controller input is ignored.
local function updatePointerUsers(self, dt)
    if type(self.updateMenuPointer) == "function" then
        self:updateMenuPointer(dt)
    end
    if type(self.updateVirtualHands) == "function" then
        self:updateVirtualHands(dt)
    end
    -- Last: which Motion Controller inputs the uses above (and Interactive Control's click) took off the Virtual Gamepad.
    if type(self.updateVirtualGamepadUse) == "function" then
        self:updateVirtualGamepadUse(dt)
    end
end

function VRCompanion:updateMotionControllers(dt)
    if type(self.updateVirtualGamepad) == "function" then self:updateVirtualGamepad(dt) end
    sampleDeskMouse(self)
    if self.motionControllersEnabled == false then
        -- Motion Controller input is ignored: nothing is polled, and the hands read as untracked.
        if not state.ignoredCleared then
            clearHands()
            state.ignoredCleared = true
        end
        if type(self.updateWheelGrab) == "function" then self:updateWheelGrab(dt) end  -- lets go of the Steering Wheel
        if type(self.updateHandTools) == "function" then self:updateHandTools(dt) end  -- the Hand Tool goes back to the game's placement
        if type(self.updateObjectGrab) == "function" then self:updateObjectGrab(dt) end  -- the Held Object goes back to the game's carry
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
            -- The cached hands are stale from here on: read as untracked, so that a hand holding the Steering Wheel is no
            -- longer located and Wheel Grab lets go (and stops dropping the other devices' steering).
            clearHands()
        end
        if type(self.updateWheelGrab) == "function" then self:updateWheelGrab(dt) end
        if type(self.updateHandTools) == "function" then self:updateHandTools(dt) end
        if type(self.updateObjectGrab) == "function" then self:updateObjectGrab(dt) end
        settleInteractiveControl(self, dt)
        updatePointerUsers(self, dt)
        return
    end
    local now = self:getCurrentTimeMs()
    state.movedThisUpdate = {}
    for _, hand in ipairs(HANDS) do
        -- The bridge fills a table in place once it holds every field, and otherwise hands back a
        -- complete one that is adopted here, so nothing is allocated per frame.
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
                motion.lastMovedMs = nil  -- the clock went backwards: forget the old move
            else
                inUse = elapsed < self.MOTION_CONTROLLER_IN_USE_MS
            end
        end
        state.inUse[hand] = inUse

        -- Trigger: pulled above PRESS, released below RELEASE; "pressed" is the update it went down.
        local wasDown = state.triggerDown[hand]
        local value = current.trigger or 0
        local isDown = value >= (wasDown and self.POINTER_TRIGGER_RELEASE or self.POINTER_TRIGGER_PRESS)
        if current.tracked ~= true then isDown = false end
        state.triggerDown[hand] = isDown
        if not isDown then state.triggerConsumed[hand] = false end
        state.triggerPressed[hand] = isDown and not wasDown
    end
    -- Wheel Grab decides which hands hold the Steering Wheel from the hands just polled (VRCompanionWheelGrab.lua).
    if type(self.updateWheelGrab) == "function" then self:updateWheelGrab(dt) end
    -- Object Grab lets go of a Held Object whose grip is let go, from the hands just polled (VRCompanionObjectGrab.lua).
    if type(self.updateObjectGrab) == "function" then self:updateObjectGrab(dt) end
    -- A hand holding the Steering Wheel stays in use (it keeps its Virtual Hand), however still it is held. So does the Tool
    -- Hand while its Hand Tool keeps it in use (Hand Tools says when): a cut or a spray held with a still hand must not drop the tool.
    -- And the hand carrying a Held Object (Object Grab), which is however still it is held.
    for _, hand in ipairs(HANDS) do
        if isHoldingSteeringWheel(self, hand) or isCarryingHeldObject(self, hand)
            or (type(self.isHandKeptInUseByHandTool) == "function" and self:isHandKeptInUseByHandTool(hand) == true) then
            state.inUse[hand] = true
            if now ~= nil then state.motion[hand].lastMovedMs = now end
        end
    end
    -- Hand Tools put the Hand Tool the Tool Hand holds into its Virtual Hand, from the hands just polled (VRCompanionHandTools.lua).
    if type(self.updateHandTools) == "function" then self:updateHandTools(dt) end
    state.pointerHand = choosePointerHand(self)
    self.pointerHand = state.pointerHand
    -- The Pointer Hand moving is the ray moving: it takes the pointer from the desk mouse.
    if state.pointerHand ~= nil and state.movedThisUpdate[state.pointerHand] then
        self:notePointerRayMoved()
    end
    self:updatePointerRay(dt)
    updatePointerUsers(self, dt)
end

-- A Recenter Action moves the origin every pose is measured from. Hands lying still must not look
-- moved, so the movement references are dropped and taken again at the next update.
function VRCompanion:resetMotionControllerMovement()
    for _, hand in ipairs(HANDS) do
        state.motion[hand].ref = nil
    end
end

-- ---- The Pointer Ray ------------------------------------------------------------------------------
-- The ray starts at the index fingertip of the Pointer Hand in the world and runs along the finger (below: "The
-- Pointer Ray on the index finger"); the aim pose is only the fallback. A hand pose is in the head pose's
-- tracking space, so it is carried into the world the way the head is: the Cyclopean Audio Listener
-- Node sits at the head's centre with the head's full world rotation (all three camera paths place it,
-- through Snap Turn and the Recenter Action), so the hand's offset and direction relative to the head
-- are rotated by the listener's world rotation and added to its world position. Anchoring at the head
-- centre keeps the ray the same in both eyes.
--
--   VRCompanion:getPointerRay() -> table | nil   the ray this update, or nil when there is none:
--     ox, oy, oz (origin, world), dx, dy, dz (unit direction), visible (drawn), ex, ey, ez (end point)
--     Read it, never keep or edit it.
--   VRCompanion:getPointerTrigger() -> pressed, held   the Pointer Hand's trigger this update
--   VRCompanion:draw()                           the engine calls it per rendered frame (per eye under AFR)

-- Rotates (x, y, z) by the unit quaternion (qx, qy, qz, qw).
local function qrotate(qx, qy, qz, qw, x, y, z)
    local cx, cy, cz = qy * z - qz * y + qw * x, qz * x - qx * z + qw * y, qx * y - qy * x + qw * z
    return x + 2 * (qy * cz - qz * cy), y + 2 * (qz * cx - qx * cz), z + 2 * (qx * cy - qy * cx)
end

-- Whether the head is currently placed in the world by the mod's own camera chain; otherwise the
-- listener node says nothing about where the player's head is. (The engine's own head-tracking setting,
-- VRCompanion:isHeadTrackingActive, is not a gate: the mod places the head itself whatever it says, and in
-- the game it reads off while the head is in fact placed.)
local function headIsPlacedInTheWorld(self)
    if self:isMenuQuadShown() or self.is2DViewActive or self.trackedCameraShown == false then return false end
    if type(self.isPassThroughMode) == "function" and self:isPassThroughMode() then return false end
    return true
end

-- ---- Head-relative poses in the world (shared by the Pointer Ray and the Virtual Hands) --------------
-- A hand pose from VRBridge is in the head pose's tracking space. Carrying it into the world is one
-- transform, whatever the pose is for: take the pose relative to the head, then apply the Cyclopean
-- Audio Listener Node's world pose (the head's centre with the head's full world rotation).
--
--   VRCompanion:isHeadPlacedInTheWorld() -> boolean
--   VRCompanion:headRelativePoseToWorld(x, y, z, qx, qy, qz, qw, out) -> true | false, reason
--     On true, out.x, out.y, out.z (world position) and out.qx, out.qy, out.qz, out.qw (unit world
--     rotation) are filled in place. On false, out is untouched and reason says why in one line.
--   VRCompanion.multiplyQuaternions(ax, ay, az, aw, bx, by, bz, bw) -> x, y, z, w   (a then b is applied as a * b)

-- Multiplies two quaternions: a then b is applied as a * b.
local function qmultiply(ax, ay, az, aw, bx, by, bz, bw)
    return aw * bx + ax * bw + ay * bz - az * by,
           aw * by - ax * bz + ay * bw + az * bx,
           aw * bz + ax * by - ay * bx + az * bw,
           aw * bw - ax * bx - ay * by - az * bz
end

-- Shared with the Virtual Hands, which compose a joint's rest rotation with a curl.
VRCompanion.multiplyQuaternions = qmultiply

function VRCompanion:isHeadPlacedInTheWorld()
    return headIsPlacedInTheWorld(self)
end

-- The state behind isHeadPlacedInTheWorld, for log lines and diagnostics.
function VRCompanion:describeHeadPlacement()
    return string.format("menu=%s, 2D=%s, trackedCameraShown=%s, passThrough=%s",
        tostring(self:isMenuQuadShown()), tostring(self.is2DViewActive), tostring(self.trackedCameraShown),
        tostring(type(self.isPassThroughMode) == "function" and self:isPassThroughMode()))
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

    -- The pose relative to the head, in the head's frame ...
    local hx, hy, hz, hw = -head.qx, -head.qy, -head.qz, head.qw
    local offX, offY, offZ = qrotate(hx, hy, hz, hw, x - head.x, y - head.y, z - head.z)
    local rx, ry, rz, rw = qmultiply(hx, hy, hz, hw, qx, qy, qz, qw)

    -- ... and in the world, through the head's world placement.
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

-- ---- The Pointer Ray on the index finger -------------------------------------------------------------------
-- The ray starts at the index fingertip of the hand and runs along the finger: a fixed offset in the grip frame
-- (VRCompanion.VIRTUAL_HAND_POINTER, VRCompanionVirtualHands.lua, the relaxed finger of the glove, so the ray does not
-- twitch as the trigger curls the finger) applied to the GRIP pose. It does not depend on Virtual Hands being shown or
-- their model loaded. Only when the grip pose is not tracked (a runtime that refuses it) does the ray start at the aim
-- pose and run along its -Z axis, as it did before the ray moved to the finger.
--
-- VRCompanion.POINTER_RAY_FOLLOWS_FINGER_DIRECTION  true (default): the ray runs along the finger. false: it keeps the
--     aim pose's direction (-Z) and only starts at the fingertip.
--   VRCompanion:getPointerRayInTrackingSpace(hand) -> ox, oy, oz, dx, dy, dz, source | nil   the same ray in the
--     tracking space the hand poses are in (before the head carries it into the world), for the Injector's menu and 2D
--     panel hit test; source is "index finger" or "aim pose"; nil while the hand is not tracked.
VRCompanion.POINTER_RAY_FOLLOWS_FINGER_DIRECTION = true

local SOURCE_FINGER = "index finger"
local SOURCE_AIM = "aim pose"

-- The fingertip offset of a hand (VRCompanion.VIRTUAL_HAND_POINTER), or nil when the table is not there.
local function pointerOffset(self, hand)
    local offsets = self.VIRTUAL_HAND_POINTER
    if type(offsets) ~= "table" then return nil end
    local offset = hand == self.MOTION_HAND_RIGHT and offsets.right or offsets.left
    if type(offset) ~= "table" or type(offset.tip) ~= "table" or type(offset.direction) ~= "table" then return nil end
    return offset
end

-- The ray of a hand whose grip pose (gx..gqw) and aim rotation (aqx..aqw) are given in one frame (tracking space or
-- the world): the fingertip as origin and the finger (or the aim -Z axis) as direction. Not normalized.
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

-- The angle in degrees between the finger's direction and the aim pose's direction (-Z) of a hand, or nil when the
-- hand has no tracked grip pose and aim pose.
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

-- Fills state.ray.o* / d* for the Pointer Hand; true and where it starts from ("index finger" or "aim pose"), or
-- false and why it cannot be placed.
local function placePointerRay(self, hand)
    local ray = state.ray
    local pose = state.hands[hand]
    if pose.tracked ~= true then return false, "the Pointer Hand is not tracked" end
    local offset = pointerOffset(self, hand)
    local fromFinger = offset ~= nil and pose.gripPoseTracked == true
    local ok, reason

    -- The aim pose in the world: where the ray starts and runs when there is no grip pose, and the direction it
    -- keeps when it is not to follow the finger.
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
        -- The ray runs along the aim pose's -Z axis.
        ox, oy, oz = aim.x, aim.y, aim.z
        dx, dy, dz = qrotate(aim.qx, aim.qy, aim.qz, aim.qw, 0, 0, -1)
    end
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 0.5 then return false, "the hand's aim direction is degenerate" end
    ray.ox, ray.oy, ray.oz = ox, oy, oz
    ray.dx, ray.dy, ray.dz = dx / length, dy / length, dz / length
    return true, fromFinger and SOURCE_FINGER or SOURCE_AIM
end

-- Called at the end of every updateMotionControllers. Places the ray, lets Interactive Control select
-- and click, and settles what is drawn.
function VRCompanion:updatePointerRay(dt)
    local ray = state.ray
    ray.valid = false
    ray.visible = false
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
    -- Logged when it changes, so a session where pointing does not work says why in the game log.
    if reason ~= state.rayReason then
        state.rayReason = reason
        print("[FS25_VR] Pointer Ray: " .. reason)
    end
    -- Once per hand, the first time the ray is placed from its index finger: how far the finger points from where the
    -- runtime aims the controller, so a headset session shows whether pointing along the finger feels sane.
    if source == SOURCE_FINGER and not state.fingerLogged[hand] then
        state.fingerLogged[hand] = true
        local angle = fingerAimAngleDegrees(self, hand)
        print(string.format("[FS25_VR] Pointer Ray: from the index finger (%s hand), the finger points %.1f degrees from the aim direction; the ray %s",
            hand == self.MOTION_HAND_RIGHT and "right" or "left", angle or 0,
            self.POINTER_RAY_FOLLOWS_FINGER_DIRECTION ~= false and "runs along the finger"
                or "keeps the aim direction (POINTER_RAY_FOLLOWS_FINGER_DIRECTION is false)"))
    end
    local hasTarget, hitX, hitY, hitZ = false, nil, nil, nil
    if type(self.updateInteractiveControl) == "function" then
        hasTarget, hitX, hitY, hitZ = self:updateInteractiveControl(dt)
    end
    -- A Click Point within the cone wins over everything: Interactive Control answers a hit point only when it selected one (it answers
    -- hasTarget, with no hit point, merely for "Click Points exist", which an Animated Object may replace).
    -- Object Grab: the ray is on an object the empty hands can pick up. Only looked at here: it pulses and grabs once it is the target.
    local grabbable, grabX, grabY, grabZ, grabDistance = false, nil, nil, nil, nil
    if not hasTarget and type(self.peekObjectGrabRayTarget) == "function" then
        grabbable, grabX, grabY, grabZ, grabDistance = self:peekObjectGrabRayTarget()
    end
    -- Animated Objects: the ray is on a door or gate in whose zone the player stands. Of the two the ray meets first, the nearer wins.
    if type(self.updateAnimatedObjects) == "function" then
        local onAnimatedObject, animatedX, animatedY, animatedZ = self:updateAnimatedObjects(dt, hitX ~= nil, grabbable and grabDistance or nil)
        if onAnimatedObject then
            hasTarget, hitX, hitY, hitZ = true, animatedX, animatedY, animatedZ
            grabbable = false
        end
    end
    if type(self.settleObjectGrabRayTarget) == "function" then self:settleObjectGrabRayTarget(grabbable) end
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
    end
end

function VRCompanion:getPointerRay()
    if state.ray.valid then return state.ray end
    return nil
end

-- The Pointer Hand's trigger: pressed (it went down this update), held (it is down). A pull that
-- switched the Pointer Hand to this hand is neither, however long it is held on: it is let go first.
function VRCompanion:getPointerTrigger()
    local hand = state.pointerHand
    if hand == nil or state.triggerConsumed[hand] then return false, false end
    return state.triggerPressed[hand] == true, state.triggerDown[hand] == true
end

-- ---- Haptic pulses on the Pointer Hand -----------------------------------------------

-- Pulse lengths (ms) and strengths (0..1): a light tick when the ray lands on a different Click Point,
-- a firmer one for a click so the two can be told apart by feel.
VRCompanion.HAPTIC_SELECT_MS = 25
VRCompanion.HAPTIC_SELECT_STRENGTH = 0.25
VRCompanion.HAPTIC_CLICK_MS = 45
VRCompanion.HAPTIC_CLICK_STRENGTH = 0.6

-- Vibrates one hand (never the other). Nothing happens, and false is returned, while Motion Controllers are
-- off, without a hand, or when the Injector has no haptics call or the runtime refuses it.
function VRCompanion:pulseHand(hand, durationMs, strength)
    if hand == nil or self.motionControllersEnabled == false then return false end
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge == nil or type(bridge.pulseHaptic) ~= "function" then return false end
    local ok, played = pcall(bridge.pulseHaptic, hand, durationMs, strength)
    return ok and played == true
end

-- Vibrates the Pointer Hand (never the other hand); false without a Pointer Hand (see pulseHand).
function VRCompanion:pulsePointerHand(durationMs, strength)
    return self:pulseHand(state.pointerHand, durationMs, strength)
end

-- Interactive Control's Click Point events (see VRCompanionInteractiveControl.lua): "selected" when the
-- ray lands on a different Click Point (clickPoint is nil when it left every one: silent), "clicked"
-- for each click that was executed.
function VRCompanion:onClickPointEvent(kind, clickPoint)
    if kind == "selected" then
        if clickPoint ~= nil then
            self:pulsePointerHand(self.HAPTIC_SELECT_MS, self.HAPTIC_SELECT_STRENGTH)
        end
    elseif kind == "clicked" then
        self:pulsePointerHand(self.HAPTIC_CLICK_MS, self.HAPTIC_CLICK_STRENGTH)
    end
end

-- A line from the hand with a dot where it lands, drawn with the engine's debug drawing from the end
-- points settled in the update: under AFR the update and the draw both run per rendered frame, under
-- Sequential Dual-Pass the one update's ray is drawn in each pass.
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
    if type(self.drawVirtualHands) == "function" then self:drawVirtualHands() end
    local ray = state.ray
    if not ray.visible then return end
    local drawLine = drawDebugLine or (_G and _G.drawDebugLine)
    local drawPoint = drawDebugPoint or (_G and _G.drawDebugPoint)
    local r, g, b = 0.25, 0.85, 1.0
    if type(drawLine) == "function" then
        drawLine(ray.ox, ray.oy, ray.oz, r, g, b, ray.ex, ray.ey, ray.ez, r, g, b, false)
    end
    if type(drawPoint) == "function" then
        drawPoint(ray.ex, ray.ey, ray.ez, 1.0, 1.0, 1.0, 1.0, false)
    end
end

-- ---- Settings, persistence, console commands ------------------------------------------------

local SETTING_KEY_ENABLED = "vrSettings.motionControllers"

-- The on/off a console argument or a saved string says (on/true/1, off/false/0), or nil when it says neither. Shared
-- with Wheel Grab's `vrWheelGrab` command and setting.
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
    if save and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
end

-- Called from saveConfiguration with the open settings file and the engine's setters (any may be nil).
function VRCompanion:saveMotionControllerSettings(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY_ENABLED, self.motionControllersEnabled ~= false)
    end
    self:savePointerConeSetting(xmlFile, xml)
    if type(self.saveWheelGrabSetting) == "function" then self:saveWheelGrabSetting(xmlFile, xml) end
    if type(self.saveHandToolsSetting) == "function" then self:saveHandToolsSetting(xmlFile, xml) end
    if type(self.saveObjectGrabSetting) == "function" then self:saveObjectGrabSetting(xmlFile, xml) end
    if type(self.saveVirtualGamepadSetting) == "function" then self:saveVirtualGamepadSetting(xmlFile, xml) end
end

-- Called from loadConfiguration with the open settings file and the engine's getters (any may be nil).
-- A key that is missing keeps the current value.
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
    if type(self.loadWheelGrabSetting) == "function" then self:loadWheelGrabSetting(xmlFile, xml) end
    if type(self.loadHandToolsSetting) == "function" then self:loadHandToolsSetting(xmlFile, xml) end
    if type(self.loadObjectGrabSetting) == "function" then self:loadObjectGrabSetting(xmlFile, xml) end
    if type(self.loadVirtualGamepadSetting) == "function" then self:loadVirtualGamepadSetting(xmlFile, xml) end
end

-- vrPointerCone: the cone (degrees) inside which a Click Point is selected.
local SETTING_KEY_CONE = "vrSettings.pointerConeDegrees"

local function validPointerCone(self, degrees)
    return type(degrees) == "number" and degrees >= self.MIN_POINTER_CONE_DEGREES and degrees <= self.MAX_POINTER_CONE_DEGREES
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
    local degrees = tonumber(arg)
    if not validPointerCone(self, degrees) then
        return usage
    end
    self.pointerConeDegrees = degrees
    if type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
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
            mission:showBlinkingWarning(string.format("VR Motion Controllers: %s (Saved)", enabled and "on" or "off"), 2500)
        end)
    end
    print(string.format("[FS25_VR] Motion Controllers set to: %s", enabled and "on" or "off"))
    return string.format("Motion Controllers set to: %s (Saved)", enabled and "on" or "off")
end

function VRCompanion:getMotionControllerStatus()
    local virtualGamepad = ""
    if type(self.getVirtualGamepadStatus) == "function" then
        virtualGamepad = " | Virtual Gamepad: " .. self:getVirtualGamepadStatus()
    end
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

-- One hand's aim pose and grip pose with whether each is tracked, and when both are, how far the index finger points
-- from the aim direction, e.g. "aim (0.30, 1.20, -0.40) tracked, grip (0.28, 1.15, -0.30) tracked, finger 23.4 degrees
-- from aim". Positions are tracking-space metres.
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

-- Why pointing may not work, in one line: the ray, who controls the pointer and Interactive Control support,
-- then each hand's aim pose and grip pose.
function VRCompanion:getPointerDiagnostics()
    local icText = "Interactive Control support: not available"
    if type(self.getInteractiveControlDiagnostics) == "function" then
        icText = self:getInteractiveControlDiagnostics()
    end
    local virtualHandsText = ""
    if type(self.getVirtualHandsDiagnostics) == "function" then
        virtualHandsText = " | " .. self:getVirtualHandsDiagnostics()
    end
    if type(self.getWheelGrabDiagnostics) == "function" then
        virtualHandsText = virtualHandsText .. " | " .. self:getWheelGrabDiagnostics()
    end
    if type(self.getHandToolsDiagnostics) == "function" then
        virtualHandsText = virtualHandsText .. " | " .. self:getHandToolsDiagnostics()
    end
    if type(self.getObjectGrabDiagnostics) == "function" then
        virtualHandsText = virtualHandsText .. " | " .. self:getObjectGrabDiagnostics()
    end
    if type(self.getAnimatedObjectsDiagnostics) == "function" then
        virtualHandsText = virtualHandsText .. " | " .. self:getAnimatedObjectsDiagnostics()
    end
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

function VRCompanion:registerMotionControllerConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrPointerDiagnostics", "Report why the Pointer Ray may not select Click Points: vrPointerDiagnostics", "consoleCommandPointerDiagnostics", self)
    addConsoleCommand("vrMotionControllers", "Turn Motion Controller input on or off (saved; the Injector's session is untouched): vrMotionControllers [on|off]", "consoleCommandSetMotionControllers", self)
    addConsoleCommand("vrMotionControllerStatus", "Report each Motion Controller's tracked and in-use state and the Pointer Hand: vrMotionControllerStatus", "consoleCommandMotionControllerStatus", self)
    addConsoleCommand("vrPointerCone", "How wide (degrees) the cone around the Pointer Ray is inside which the nearest Click Point is selected (saved): vrPointerCone [degrees]", "consoleCommandSetPointerCone", self)
    if type(self.registerVirtualHandsConsoleCommands) == "function" then self:registerVirtualHandsConsoleCommands() end
    if type(self.registerWheelGrabConsoleCommands) == "function" then self:registerWheelGrabConsoleCommands() end
    if type(self.registerHandToolsConsoleCommands) == "function" then self:registerHandToolsConsoleCommands() end
    if type(self.registerObjectGrabConsoleCommands) == "function" then self:registerObjectGrabConsoleCommands() end
    if type(self.loadAnimatedObjectsSupport) == "function" then self:loadAnimatedObjectsSupport() end  -- wraps the game's trigger callback
    if type(self.registerVirtualGamepadConsoleCommands) == "function" then self:registerVirtualGamepadConsoleCommands() end
end

-- Map unload: forget everything seen this session. The saved setting stays as it is.
function VRCompanion:resetMotionControllers()
    if type(self.resetMenuPointer) == "function" then
        self:resetMenuPointer()
    end
    if type(self.resetHandTools) == "function" then
        self:resetHandTools()  -- before the Virtual Hands go: a tool hangs below one
    end
    if type(self.resetObjectGrab) == "function" then
        self:resetObjectGrab()
    end
    if type(self.resetVirtualHands) == "function" then
        self:resetVirtualHands()
    end
    if type(self.resetWheelGrab) == "function" then
        self:resetWheelGrab()
    end
    if type(self.resetVirtualGamepad) == "function" then
        self:resetVirtualGamepad()
    end
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrMotionControllers")
        removeConsoleCommand("vrMotionControllerStatus")
        removeConsoleCommand("vrPointerDiagnostics")
        removeConsoleCommand("vrPointerCone")
    end
    if type(self.resetInteractiveControlSupport) == "function" then self:resetInteractiveControlSupport() end
    if type(self.resetAnimatedObjectsSupport) == "function" then self:resetAnimatedObjectsSupport() end
    state = newState()
    initHands()
    self.pointerHand = nil
end
