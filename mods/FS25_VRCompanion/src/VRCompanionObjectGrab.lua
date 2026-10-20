-- ====================================================================
-- FS25 VR Companion Mod: Object Grab
--
-- Lets the player pick up, carry and throw loose objects with the Motion Controllers (spec #156, ADR-0033). On foot with empty hands
-- the game carries one object at a time with its "hands" hand tool (HandToolHands): it targets what the look ray meets
-- (PlayerTargeter, fed by Player:getLookRay) and the player's pick-up button takes it. This file is built up ticket by ticket; so far
-- (#158) the Pointer Hand AIMS the empty hands.
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
-- THE GAME'S PICK-UP BUTTON is the game's: it picks up what the targeter found, which is now what the hand points at.
--
-- WRAPS: Player.getLookRay (shared with Hand Tools: Core owns the one wrap and each holds it, so that one giving up does not take the
-- look ray away from the other) and HandToolHands.onDraw; getCanPickUpNode is only checked. When one is missing or has another shape
-- at map load, Object Grab gives up: ONE warning ("Object Grab disabled: ..."), its wraps are lifted, and picking up is the
-- game's. Hand Tools are not affected. A game without HandToolHands (no hands to aim) is not a failure: nothing is wrapped.
--
-- PUBLIC SURFACE
--   VRCompanion.objectGrabEnabled              boolean, default true, saved (`vrObjectGrab on|off`)
--   VRCompanion.OBJECT_GRAB_REACH_BEHIND       how far (m) the look ray's origin is moved back along the finger
--   VRCompanion.OBJECT_GRAB_LAND_PULSE_MS / _STRENGTH   the light pulse on the Pointer Hand as the ray lands on something grabbable
--   VRCompanion:getObjectGrabAimedRay(player) -> x, y, z, dirX, dirY, dirZ | nil, why not   what Player:getLookRay answers
--   VRCompanion:getObjectGrabRayTarget() -> hasTarget, x, y, z   the Pointer Ray's object target (called by updatePointerRay)
--   VRCompanion:setObjectGrabEnabled(enabled, save)
--   VRCompanion:saveObjectGrabSetting(xmlFile, xml) / loadObjectGrabSetting(xmlFile, xml)   settings chain
--   VRCompanion:getObjectGrabDiagnostics() -> string          the state of Object Grab, one line
--   VRCompanion:registerObjectGrabConsoleCommands() / resetObjectGrab()   map load / unload
--   Console: vrObjectGrab [on|off]
--
-- Hooks into the rest of the mod (each a single guarded call):
--   Core               registerLookRayAimer("objectGrab", ...) and holdLookRayWrap("objectGrab")
--   MotionControllers  updatePointerRay -> getObjectGrabRayTarget() (after Interactive Control); the saved setting, the console
--                      commands, the diagnostics and the reset chain through the Motion Controller functions of the same names
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

-- Takes the wraps off again, each only when it is still the wrapper put there.
local function liftWraps()
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
local function aimedRay(self, player)
    if self.objectGrabEnabled == false then return nil, "Object Grab is off" end
    if self.motionControllersEnabled == false then return nil, "Motion Controllers are off" end
    if state.problem ~= nil then return nil, "Object Grab gave up (" .. state.problem .. ")" end
    if type(player) ~= "table" or player ~= localPlayer() then return nil, "not the local player" end
    if player.camera == nil or player.camera.isFirstPerson ~= true then return nil, "not in the first person view" end
    if self:getControlledVehicle() ~= nil then return nil, "in a vehicle" end
    if self:doesMenuTakeInput() then return nil, "a menu takes the input" end
    if emptyHands(player) == nil then return nil, "the hands are not empty" end
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
    state.carry = { hand = hand, hands = hands }
    self:pulseHand(hand, self.OBJECT_GRAB_GRAB_PULSE_MS, self.OBJECT_GRAB_GRAB_PULSE_STRENGTH)
end

-- The Pointer Hand is on an object the hands can pick up (`target`, the targeter's): a squeeze of its grip that the pad let the hand
-- have grabs it.
local function tryGrab(self, player, target)
    local hand = self.pointerHand
    if hand == nil or state.carry ~= nil then return end
    if not takeGrip(self, hand) or not state.gripPressed[hand] then return end
    local hands = emptyHands(player)
    if hands ~= nil then startGrab(self, hand, hands, target) end
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

local function updateCarry(self)
    local carry = state.carry
    if carry == nil then return end
    if carryHasEnded(self, carry) then
        state.carry = nil  -- the grip goes back to the pad once it is let go (the Virtual Gamepad keeps what a use took off it)
        return
    end
    takeGrip(self, carry.hand)
end

-- Once per Motion Controller update, after the hands are polled and before the Pointer Hand is chosen.
function VRCompanion:updateObjectGrab(dt)
    trackGrips()
    updateCarry(self)
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
    local node, x, y, z, target = grabbableTarget(player)
    if node == nil then
        state.landedNode = nil
        return false
    end
    if node ~= state.landedNode then
        state.landedNode = node
        self:pulsePointerHand(self.OBJECT_GRAB_LAND_PULSE_MS, self.OBJECT_GRAB_LAND_PULSE_STRENGTH)
    end
    tryGrab(self, player, target)
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
