-- ====================================================================
-- FS25 VR Companion Mod: Virtual Hands
--
-- Draws a gloved hand in the 3D world where each Motion Controller is held (spec #82). The player alone
-- sees them: nothing here is synchronised in multiplayer, and the hands have no collision, physics or
-- shadow.
--
-- A Virtual Hand is SHOWN exactly while all of these hold, otherwise it is hidden (never deleted, so it
-- is back at once):
--   * Motion Controllers are on (`vrMotionControllers`, there is no separate setting),
--   * that hand is in use (VRCompanion:isHandInUse: tracked and moved within the in-use timeout),
--   * its grip pose is tracked (the aim pose alone being tracked is not enough),
--   * the head is placed in the world, the check the Pointer Ray uses (no menu, no 2D Floating Window
--     Mode, the tracked camera is shown, no Pass-Through Mode). The engine's own head-tracking setting
--     is not a gate: it reads false in the real game.
--
-- WHERE: the hand's GRIP pose (not its aim pose, which is where the Pointer Ray starts), carried into the
-- world by VRCompanion:headRelativePoseToWorld, the same transform the Pointer Ray uses. It is placed once
-- per update, so under Sequential Dual-Pass both eye passes see the same placement and under Alternate
-- Frame Rendering each rendered frame places it again.
--
-- THE MODEL (ticket #84): one right-hand work glove i3d at VRCompanion.modDirectory ..
-- VRCompanion.VIRTUAL_HAND_MODEL. The file's first child is the hand's root node, whose origin and axes are
-- the OpenXR grip pose of a right hand, with joints thumb1..3, index1..3, middle1..3, ring1..3 and
-- little1..3 below it. It is loaded once per map load (on the first update with Motion Controllers on),
-- cloned per hand, and every clone sits in a holder transform group linked to the scene root; the holder
-- carries the placement and the visibility. The left clone is the right model mirrored along X (the grip
-- frame has the same axes for both hands, so mirroring across X turns a right hand into a left hand).
-- Shadow casting is off on every shape of the clones. Everything is deleted again when the map unloads.
-- A missing or failing model logs one `[FS25_VR] Virtual Hands: ...` line and leaves Virtual Hands off; the
-- Pointer Ray, Click Points and menus do not depend on it.
--
-- FINGER CURL: each time a hand is shown, its index finger joints (index1..3) are rotated in proportion to
-- the trigger and its middle, ring and little finger joints (middle1..3, ring1..3, little1..3) in
-- proportion to the grip, both 0..1 as VRBridge.getHandState reports them. The thumb joints (thumb1..3)
-- are never touched. A joint is found by name below the hand's clone; the rotation it has when found is
-- the model's relaxed rest pose, and the curl is applied on top of it: the joint's rest rotation turned
-- about the joint's own X axis by value * VRCompanion.FINGER_CURL_DEGREES[segment] (positive = toward the
-- palm, ticket #84). 0 is therefore exactly the rest pose. The left hand is the mirrored clone, so the same
-- local rotation curls its mirrored fingers the same way. A hand that is hidden is not updated: its fingers
-- are set again the moment it is shown. A joint the model lacks keeps that finger segment still; the
-- missing names are logged once per map, and the other fingers still curl.
--
-- LOG: a change-only `[FS25_VR] Virtual Hands: <reason>` line says why Virtual Hands are unavailable
-- (Motion Controllers off, model not loaded, head not placed in the world) and when they become
-- available. Whether one hand is shown or why not is in the diagnostics only.
--
-- PUBLIC SURFACE
--   VRCompanion.VIRTUAL_HAND_MODEL           model path inside the Companion Mod
--   VRCompanion.modDirectory                 the Companion Mod's folder (set when the mod is loaded)
--   VRCompanion.FINGER_CURL_DEGREES          full curl (value 1) of segment 1, 2 and 3 of a finger, in degrees
--   VRCompanion:updateVirtualHands(dt)       once per update, called by updateMotionControllers
--   VRCompanion:resetVirtualHands()          map unload: deletes the hands and the model, forgets everything
--   VRCompanion:getVirtualHandsDiagnostics() -> string   the reason and each hand's state, one line
--   Console: vrVirtualHandsDiagnostics       prints the same line (also part of vrPointerDiagnostics)
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.VirtualHands = VRCompanion.VirtualHands or {}

-- The mod's own folder, known only while the engine loads the mod's scripts.
VRCompanion.modDirectory = VRCompanion.modDirectory or g_currentModDirectory or ""
-- The right-hand model, relative to the Companion Mod's folder.
VRCompanion.VIRTUAL_HAND_MODEL = "models/virtualHandRight.i3d"

-- The curl of one finger joint at a full pull, base to tip, in degrees. The three together fold a finger
-- into a fist the way a real one does (knuckle about 80, middle joint about 100, tip about 65).
VRCompanion.FINGER_CURL_DEGREES = { 80, 100, 65 }

local HANDS = { 0, 1 }
local HAND_NAMES = { [0] = "left", [1] = "right" }
-- The fingers that curl and the analog input of the Motion Controller that drives each. The thumb is left out
-- on purpose: it stays as the model rests.
local FINGERS = {
    { name = "index", input = "trigger" },
    { name = "middle", input = "grip" },
    { name = "ring", input = "grip" },
    { name = "little", input = "grip" },
}

local function newState()
    return {
        modelTried = false,      -- the model was loaded (or failed to) this map
        modelFailure = nil,      -- why the model is not available, when it is not
        fileRoot = nil,          -- the loaded file's root node (never linked into the scene)
        hands = {},              -- [hand] = { holder, model, shown, status, world = {} }
        reason = nil,            -- the last reason logged
        missingJointsLogged = false,  -- the line about joints the model lacks was logged this map
    }
end

local state = newState()

local function engine(name)
    return _G and _G[name]
end

local function deleteNode(node)
    local deleteFn = engine("delete")
    if node == nil or node == 0 or type(deleteFn) ~= "function" then return end
    local exists = engine("entityExists")
    if type(exists) == "function" and not exists(node) then return end
    pcall(deleteFn, node)
end

-- Drops everything this module put into the scene.
local function discardScene()
    for _, hand in ipairs(HANDS) do
        local entry = state.hands[hand]
        if entry ~= nil then deleteNode(entry.holder) end
    end
    state.hands = {}
    deleteNode(state.fileRoot)
    state.fileRoot = nil
end

-- Casting shadows is off for every shape below the node.
local function disableShadowCasting(node)
    local getHasClassId, setCast = engine("getHasClassId"), engine("setShapeCastShadowmap")
    local classIds = engine("ClassIds")
    local getNumOfChildren, getChildAt = engine("getNumOfChildren"), engine("getChildAt")
    if type(setCast) ~= "function" then return end
    local isShape = true
    if type(getHasClassId) == "function" and type(classIds) == "table" and classIds.SHAPE ~= nil then
        isShape = getHasClassId(node, classIds.SHAPE) == true
    end
    if isShape then pcall(setCast, node, false) end
    if type(getNumOfChildren) == "function" and type(getChildAt) == "function" then
        for index = 0, getNumOfChildren(node) - 1 do
            disableShadowCasting(getChildAt(node, index))
        end
    end
end

-- The first node named `name` at or below `node`, or nil.
local function findNode(node, name)
    local getName, getNumOfChildren, getChildAt = engine("getName"), engine("getNumOfChildren"), engine("getChildAt")
    if getName(node) == name then return node end
    for index = 0, getNumOfChildren(node) - 1 do
        local found = findNode(getChildAt(node, index), name)
        if found ~= nil then return found end
    end
    return nil
end

-- Finds the finger joints below one hand clone and remembers each one's rest rotation. Returns the names of
-- the joints the model does not have.
local function findJoints(entry)
    local getQuaternion = engine("getQuaternion")
    entry.joints = {}
    local missing = {}
    for _, finger in ipairs(FINGERS) do
        for segment = 1, 3 do
            local name = finger.name .. segment
            local node = findNode(entry.model, name)
            if node == nil then
                table.insert(missing, name)
            else
                local qx, qy, qz, qw = getQuaternion(node)
                table.insert(entry.joints, {
                    node = node, input = finger.input, segment = segment,
                    restX = qx, restY = qy, restZ = qz, restW = qw,
                })
            end
        end
    end
    return missing
end

-- Loads the model and makes both hands, hidden. Returns nil on success, otherwise why not.
local function buildHands(self)
    for _, name in ipairs({ "loadI3DFile", "clone", "link", "createTransformGroup", "getRootNode", "getChildAt",
                            "setVisibility", "setScale", "setWorldTranslation", "setWorldQuaternion", "getName",
                            "getNumOfChildren", "getQuaternion", "setQuaternion" }) do
        if type(engine(name)) ~= "function" then return name .. " is not available" end
    end
    local path = (self.modDirectory or "") .. self.VIRTUAL_HAND_MODEL
    local fileExists = engine("fileExists")
    if type(fileExists) == "function" and not fileExists(path) then
        return "the model file is missing: " .. path
    end
    local ok, root, failedReason = pcall(engine("loadI3DFile"), path, false, false)
    if not ok then return "loading " .. path .. " raised: " .. tostring(root) end
    if root == nil or root == 0 then
        return string.format("%s failed to load (reason %s)", path, tostring(failedReason))
    end
    state.fileRoot = root
    local template = engine("getChildAt")(root, 0)
    if template == nil or template == 0 then return path .. " holds no hand node" end

    local missing = {}
    for _, hand in ipairs(HANDS) do
        local holder = engine("createTransformGroup")("virtualHand_" .. HAND_NAMES[hand])
        local entry = { holder = holder, shown = false, status = "hidden", world = {} }
        state.hands[hand] = entry
        engine("link")(engine("getRootNode")(), holder)
        local model = engine("clone")(template, false, false)
        if model == nil or model == 0 then return "cloning the hand node of " .. path .. " failed" end
        entry.model = model
        engine("link")(holder, model)
        if hand == self.MOTION_HAND_LEFT then
            engine("setScale")(model, -1, 1, 1)  -- the right-hand model mirrored
        end
        disableShadowCasting(model)
        engine("setVisibility")(holder, false)
        for _, name in ipairs(findJoints(entry)) do missing[name] = true end
    end
    if next(missing) ~= nil and not state.missingJointsLogged then
        state.missingJointsLogged = true
        local names = {}
        for _, finger in ipairs(FINGERS) do
            for segment = 1, 3 do
                if missing[finger.name .. segment] then table.insert(names, finger.name .. segment) end
            end
        end
        print("[FS25_VR] Virtual Hands: the model lacks joints, those fingers stay still: " .. table.concat(names, ", "))
    end
    return nil
end

local function loadModel(self)
    state.modelTried = true
    local ok, failure = pcall(buildHands, self)
    if not ok then failure = "building the hands raised: " .. tostring(failure) end
    if failure ~= nil then
        discardScene()
        state.modelFailure = failure
    end
end

-- Why Virtual Hands are unavailable as a whole, or nil when they are available.
local function unavailableReason(self)
    if self.motionControllersEnabled == false then return "Motion Controllers off" end
    if state.modelFailure ~= nil then return "model not loaded (" .. state.modelFailure .. ")" end
    if not self:isHeadPlacedInTheWorld() then
        return "head not placed in the world (" .. self:describeHeadPlacement() .. ")"
    end
    return nil
end

local function setShown(entry, shown)
    if entry.shown == shown then return end
    entry.shown = shown
    engine("setVisibility")(entry.holder, shown)
end

-- Sets every finger joint of a shown hand: its rest rotation turned about the joint's own X axis by the curl
-- its trigger or grip asks for.
local function curlFingers(entry, pose)
    local setQuaternion = engine("setQuaternion")
    local degrees = VRCompanion.FINGER_CURL_DEGREES
    for _, joint in ipairs(entry.joints or {}) do
        local value = math.max(0, math.min(1, tonumber(pose[joint.input]) or 0))
        local half = math.rad(degrees[joint.segment]) * value * 0.5
        local bx, bw = math.sin(half), math.cos(half)  -- the turn about X, as a quaternion
        local ax, ay, az, aw = joint.restX, joint.restY, joint.restZ, joint.restW
        -- rest * turn: the turn happens in the joint's own frame
        setQuaternion(joint.node, aw * bx + ax * bw, ay * bw + az * bx, az * bw - ay * bx, aw * bw - ax * bx)
    end
end

-- Places one hand at its grip pose, or hides it; returns the hand's status for the diagnostics.
local function updateHand(self, hand, unavailable)
    local entry = state.hands[hand]
    if entry == nil then return "no hand" end
    local status
    if unavailable ~= nil then
        status = "hidden (" .. unavailable .. ")"
    elseif not self:isHandInUse(hand) then
        status = "hidden (Motion Controller not in use)"
    else
        local pose = self:getHandState(hand)
        if pose.gripPoseTracked ~= true then
            status = "hidden (grip pose not tracked)"
        else
            local world = entry.world
            local ok, why = self:headRelativePoseToWorld(pose.gripPoseX, pose.gripPoseY, pose.gripPoseZ,
                pose.gripPoseQx, pose.gripPoseQy, pose.gripPoseQz, pose.gripPoseQw, world)
            if ok then
                engine("setWorldTranslation")(entry.holder, world.x, world.y, world.z)
                engine("setWorldQuaternion")(entry.holder, world.qx, world.qy, world.qz, world.qw)
                curlFingers(entry, pose)
                status = "shown"
            else
                status = "hidden (" .. why .. ")"
            end
        end
    end
    setShown(entry, status == "shown")
    entry.status = status
    return status
end

function VRCompanion:updateVirtualHands(dt)
    if self.motionControllersEnabled ~= false and not state.modelTried then
        loadModel(self)
    end
    local unavailable = unavailableReason(self)
    -- Logged when it changes, so a session without hands says why in the game log.
    local reason = unavailable or "available"
    if reason ~= state.reason then
        state.reason = reason
        print("[FS25_VR] Virtual Hands: " .. reason)
    end
    for _, hand in ipairs(HANDS) do
        updateHand(self, hand, unavailable)
    end
end

-- One line: why Virtual Hands are unavailable (or that they are available) and each hand's state.
function VRCompanion:getVirtualHandsDiagnostics()
    local function describe(hand)
        local entry = state.hands[hand]
        return entry ~= nil and entry.status or "no hand"
    end
    return string.format("Virtual Hands: %s | Left Virtual Hand: %s | Right Virtual Hand: %s",
        tostring(state.reason or "not updated yet"), describe(0), describe(1))
end

function VRCompanion:consoleCommandVirtualHandsDiagnostics()
    return self:getVirtualHandsDiagnostics()
end

function VRCompanion:registerVirtualHandsConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrVirtualHandsDiagnostics", "Report why the Virtual Hands are shown or hidden: vrVirtualHandsDiagnostics", "consoleCommandVirtualHandsDiagnostics", self)
end

-- Map unload: the hands and the model go, and the next map loads the model again.
function VRCompanion:resetVirtualHands()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrVirtualHandsDiagnostics")
    end
    discardScene()
    state = newState()
end

VRCompanion.VirtualHands.updateVirtualHands = function(self, ...) return VRCompanion.updateVirtualHands(VRCompanion, ...) end
VRCompanion.VirtualHands.resetVirtualHands = function(self, ...) return VRCompanion.resetVirtualHands(VRCompanion, ...) end
VRCompanion.VirtualHands.getVirtualHandsDiagnostics = function(self, ...) return VRCompanion.getVirtualHandsDiagnostics(VRCompanion, ...) end
VRCompanion.VirtualHands.consoleCommandVirtualHandsDiagnostics = function(self, ...) return VRCompanion.consoleCommandVirtualHandsDiagnostics(VRCompanion, ...) end

return VRCompanion.VirtualHands
