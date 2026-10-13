-- ====================================================================
-- FS25 VR Companion Mod: Motion Controller Subsystem
--
-- Makes the player's Motion Controllers visible to the rest of the mod: it polls each hand's state
-- from the Injector once per update, decides which hands are in use, picks the Pointer Hand, and owns
-- the `vrMotionControllers` setting and the status console command. Controller input is optional;
-- everything here is inert when VRBridge.getHandState is missing or the runtime offers no controllers.
--
-- PUBLIC SURFACE (frozen for the Motion Controller tickets that build on this module; see ADR-0025)
--
--   Hand ids          VRCompanion.MOTION_HAND_LEFT = 0, VRCompanion.MOTION_HAND_RIGHT = 1.
--                     The same ids VRBridge.getHandState (and the later haptics call) use. Anywhere a
--                     hand is a parameter it is one of these two numbers.
--
--   VRBridge.getHandState(hand [, out]) -> table   (the bridge call, filled in place like getHeadPose)
--     tracked            boolean  aim pose valid and the controller active (false when the runtime
--                                 has no controllers, refused the action set, or the controller is off)
--     x, y, z            metres   aim pose position, tracking space, SAME convention, reference space
--     pitch, yaw, roll   radians  and predicted display time as VRBridge.getHeadPose (follows the
--     qx, qy, qz, qw              Recenter Action). Compose it into the world exactly like the head.
--     isValid, isPositionTracked, isOrientationTracked   booleans, as in getHeadPose
--     trigger, grip      0..1     analog value (Khronos simple controller: select/click as trigger)
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
--                                               Untracked defaults when motion controllers are off.
--     VRCompanion:isHandInUse(hand) -> boolean  tracked and moved within MOTION_CONTROLLER_IN_USE_MS
--     VRCompanion:getPointerHand() -> 0 | 1 | nil
--     VRCompanion.pointerHand                    mirror of getPointerHand() (read-only for others)
--       Pointer Hand rule of this module: exactly one hand in use -> that hand; none in use -> nil;
--       both in use -> the previous Pointer Hand if it is still in use, otherwise nil (choosing
--       between two hands in use is the Pointer Hand switching rule, which owns pointerHand then).
--
--   VRCompanion.motionControllersEnabled  boolean, default true, saved (`vrMotionControllers on|off`).
--     When false the mod ignores controller input: hands report untracked, none is in use, there is
--     no Pointer Hand. The Injector session and its action set are never touched by this setting.
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
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.MotionControllers = VRCompanion.MotionControllers or {}

VRCompanion.MOTION_HAND_LEFT = 0
VRCompanion.MOTION_HAND_RIGHT = 1
VRCompanion.pointerHand = nil

-- rMotionControllers on|off: saved, default on. Off makes the mod ignore controller input; it never
-- touches the Injector's session or action set.
VRCompanion.DEFAULT_MOTION_CONTROLLERS_ENABLED = true
VRCompanion.motionControllersEnabled = VRCompanion.DEFAULT_MOTION_CONTROLLERS_ENABLED

-- A hand is in use while it is tracked and has moved within this long (ms).
VRCompanion.MOTION_CONTROLLER_IN_USE_MS = 3000
-- "Moved" means the aim pose left the position (metres) or orientation (radians) it was last at by
-- more than this. Measured from the last pose that counted, so a slow drift adds up to a move while
-- a controller lying on a desk (sensor noise well below a millimetre) never does.
VRCompanion.MOTION_CONTROLLER_MOVE_DISTANCE = 0.01
VRCompanion.MOTION_CONTROLLER_MOVE_ANGLE = 0.035

local HANDS = { 0, 1 }

-- State of the subsystem. Rebuilt by resetMotionControllers().
local function newState()
    return {
        hands = {},      -- [hand] = table filled in place by VRBridge.getHandState
        motion = {},     -- [hand] = { ref = {x,y,z,qx,qy,qz,qw} | nil, lastMovedMs = number | nil }
        inUse = {},      -- [hand] = boolean
        pointerHand = nil,
        ignoredCleared = false,  -- controller input was ignored and the cached hands were already cleared
        bridgeMissing = false,   -- the Injector offers no VRBridge.getHandState
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
        trigger = 0, grip = 0, thumbX = 0, thumbY = 0,
        thumbClick = false, buttonLower = false, buttonUpper = false, menu = false,
    }
end

local function initHands()
    for _, hand in ipairs(HANDS) do
        state.hands[hand] = untrackedHand()
        state.motion[hand] = {}
        state.inUse[hand] = false
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

-- The Pointer Hand: the one hand in use; with both in use the previous one while its hand is still in
-- use (choosing between two hands in use belongs to the Pointer Hand switching rule); otherwise none.
local function choosePointerHand()
    local left, right = state.inUse[0], state.inUse[1]
    if left and not right then return 0 end
    if right and not left then return 1 end
    if left and right and state.pointerHand ~= nil and state.inUse[state.pointerHand] then
        return state.pointerHand
    end
    return nil
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
    end
    state.pointerHand = nil
    VRCompanion.pointerHand = nil
end

function VRCompanion:updateMotionControllers(dt)
    if self.motionControllersEnabled == false then
        -- Controller input is ignored: nothing is polled, and the hands read as untracked.
        if not state.ignoredCleared then
            clearHands()
            state.ignoredCleared = true
        end
        return
    end
    state.ignoredCleared = false

    local getHandState = bridgeCall()
    state.bridgeMissing = (getHandState == nil)
    if getHandState == nil then
        return
    end
    local now = self:getCurrentTimeMs()
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
            if movedFromReference(motion, current) and now ~= nil then
                motion.lastMovedMs = now
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
    end
    state.pointerHand = choosePointerHand()
    self.pointerHand = state.pointerHand
    if type(self.processControllerButtons) == "function" then self:processControllerButtons() end
end

-- A Recenter Action moves the origin every pose is measured from. Hands lying still must not look
-- moved, so the movement references are dropped and taken again at the next update.
function VRCompanion:resetMotionControllerMovement()
    for _, hand in ipairs(HANDS) do
        state.motion[hand].ref = nil
    end
end
-- ---- Settings, persistence, console commands ------------------------------------------------

local SETTING_KEY_ENABLED = "vrSettings.motionControllers"

local function parseOnOff(arg)
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
        if str ~= nil then loaded = parseOnOff(str) end
    end
    if loaded ~= nil then
        self.motionControllersEnabled = loaded
    end
end

function VRCompanion:consoleCommandSetMotionControllers(arg)
    local enabled = parseOnOff(arg)
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
    if self.motionControllersEnabled == false then
        return "Motion Controllers: off (controller input is ignored) | Left: ignored | Right: ignored | Pointer Hand: none"
    end
    if state.bridgeMissing then
        return "Motion Controllers: on | VRBridge.getHandState is not available (no Injector, or an older one) | Pointer Hand: none"
    end
    local function describe(hand)
        return string.format("%s, %s",
            state.hands[hand].tracked == true and "tracked" or "not tracked",
            state.inUse[hand] and "in use" or "not in use")
    end
    local pointer = state.pointerHand
    return string.format("Motion Controllers: on | Left: %s | Right: %s | Pointer Hand: %s",
        describe(0), describe(1), pointer == 0 and "left" or pointer == 1 and "right" or "none")
end

function VRCompanion:consoleCommandMotionControllerStatus()
    return self:getMotionControllerStatus()
end

function VRCompanion:registerMotionControllerConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrMotionControllers", "Turn Motion Controller input on or off (saved; the Injector's session is untouched): vrMotionControllers [on|off]", "consoleCommandSetMotionControllers", self)
    addConsoleCommand("vrMotionControllerStatus", "Report each Motion Controller's tracked and in-use state and the Pointer Hand: vrMotionControllerStatus", "consoleCommandMotionControllerStatus", self)
end

-- Map unload: forget everything seen this session. The saved setting stays as it is.
function VRCompanion:resetMotionControllers()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrMotionControllers")
        removeConsoleCommand("vrMotionControllerStatus")
    end
    state = newState()
    initHands()
    self.pointerHand = nil
end
-- Export to MotionControllers subsystem table
VRCompanion.MotionControllers.getHandState = function(self, ...) return VRCompanion.getHandState(VRCompanion, ...) end
VRCompanion.MotionControllers.isHandInUse = function(self, ...) return VRCompanion.isHandInUse(VRCompanion, ...) end
VRCompanion.MotionControllers.getPointerHand = function(self, ...) return VRCompanion.getPointerHand(VRCompanion, ...) end
VRCompanion.MotionControllers.consoleCommandSetMotionControllers = function(self, ...) return VRCompanion.consoleCommandSetMotionControllers(VRCompanion, ...) end
VRCompanion.MotionControllers.consoleCommandMotionControllerStatus = function(self, ...) return VRCompanion.consoleCommandMotionControllerStatus(VRCompanion, ...) end
VRCompanion.MotionControllers.updateMotionControllers = function(self, ...) return VRCompanion.updateMotionControllers(VRCompanion, ...) end
VRCompanion.MotionControllers.resetMotionControllerMovement = function(self, ...) return VRCompanion.resetMotionControllerMovement(VRCompanion, ...) end

return VRCompanion.MotionControllers
