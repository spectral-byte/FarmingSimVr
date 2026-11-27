VRCompanion = VRCompanion or {}

VRCompanion.modDirectory = VRCompanion.modDirectory or g_currentModDirectory or ""
VRCompanion.VIRTUAL_HAND_MODEL_LEFT = "models/VirtualHandLeft.i3d"
VRCompanion.VIRTUAL_HAND_MODEL_RIGHT = "models/VirtualHandRight.i3d"

VRCompanion.FINGER_CURL_DEGREES = { 60, 75, 40 }

VRCompanion.HELD_TOOL_GRIP_CURL = 1.0

local METRES_PER_MODEL_UNIT = 0.0875

local function mirroredAcrossX(left)
    return {
        rotation = { x = left.rotation.x, y = -left.rotation.y, z = -left.rotation.z, w = left.rotation.w },
        gripOrigin = { x = -left.gripOrigin.x, y = left.gripOrigin.y, z = left.gripOrigin.z },
    }
end

local LEFT_MODEL_TO_GRIP = {
    rotation = { x = -0.5, y = 0.5, z = 0.5, w = 0.5 },  -- model axes -> grip axes (see AXES above)
    gripOrigin = {                                       -- the model point that becomes the grip origin, in metres
        x = 0.04 * METRES_PER_MODEL_UNIT, y = -0.30 * METRES_PER_MODEL_UNIT, z = -0.40 * METRES_PER_MODEL_UNIT,
    },
}
VRCompanion.VIRTUAL_HAND_MODEL_TO_GRIP = {
    left = LEFT_MODEL_TO_GRIP,
    right = mirroredAcrossX(LEFT_MODEL_TO_GRIP),
}

local LEFT_POINTER = {
    tip = { x = -0.0052, y = -0.0905, z = -0.0441 },
    direction = { x = 0.3349, y = -0.9232, z = -0.1885 },
}
VRCompanion.VIRTUAL_HAND_POINTER = {
    left = LEFT_POINTER,
    right = {
        tip = { x = -LEFT_POINTER.tip.x, y = LEFT_POINTER.tip.y, z = LEFT_POINTER.tip.z },
        direction = { x = -LEFT_POINTER.direction.x, y = LEFT_POINTER.direction.y, z = LEFT_POINTER.direction.z },
    },
}

VRCompanion.FINGER_CURL_DIRECTION = -1

local HANDS = { 0, 1 }
local HAND_NAMES = { [0] = "left", [1] = "right" }

VRCompanion.VIRTUAL_HAND_FINGERS = {
    { name = "index",  input = "trigger", base = "Index Finger",  joints = { "Lower.001", "Middle.001", "Top.001" } },
    { name = "middle", input = "grip",    base = "Middle Finger", joints = { "Lower.002", "Middle.002", "Top.002" } },
    { name = "ring",   input = "grip",    base = "Ring Finger",   joints = { "Lower.003", "Middle.003", "Top.003" } },
    { name = "little", input = "grip",    base = "Pinky",         joints = { "Lower.004", "Middle.004", "Top.004" } },
}
local FINGERS = VRCompanion.VIRTUAL_HAND_FINGERS

local function newState()
    return {
        modelTried = false,
        modelFailure = nil,
        fileRoots = {},          -- [hand] = the root node of the model file loaded for that hand (never linked into the scene)
        hands = {},              -- [hand] = { holder, model, shown, status, placement = {} (the anchor's space),
                                 --            world = {} (scratch), relative = {} (the grip pose in the head's space, from the update) }
        reason = nil,
        missingJointsLogged = false,
        anchor = nil,
    }
end

local state = newState()

local engine = VRCompanion.getEngineGlobal

local function deleteNode(node)
    local deleteFn = engine("delete")
    if node == nil or node == 0 or type(deleteFn) ~= "function" then return end
    local exists = engine("entityExists")
    if type(exists) == "function" and not exists(node) then return end
    pcall(deleteFn, node)
end

local function discardScene()
    VRCompanion:releaseHandToolsFromHands()
    for _, hand in ipairs(HANDS) do
        local entry = state.hands[hand]
        if entry ~= nil then deleteNode(entry.holder) end
    end
    state.hands = {}
    for _, hand in ipairs(HANDS) do deleteNode(state.fileRoots[hand]) end
    state.fileRoots = {}
end

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

local function findNode(node, name)
    local getName, getNumOfChildren, getChildAt = engine("getName"), engine("getNumOfChildren"), engine("getChildAt")
    if getName(node) == name then return node end
    for index = 0, getNumOfChildren(node) - 1 do
        local found = findNode(getChildAt(node, index), name)
        if found ~= nil then return found end
    end
    return nil
end

local function findJoints(entry)
    local getQuaternion = engine("getQuaternion")
    entry.joints = {}
    local missing = {}
    for _, finger in ipairs(FINGERS) do
        local base = findNode(entry.model, finger.base)
        if base == nil then missing[finger.base] = true end
        for segment, name in ipairs(finger.joints) do
            local node = base ~= nil and findNode(base, name) or nil
            if node == nil then
                missing[name] = true
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

local function applyModelToGrip(node, offset)
    local q, o = offset.rotation, offset.gripOrigin
    local tx, ty, tz = VRCompanion.rotateVectorByQuaternion(q.x, q.y, q.z, q.w, o.x, o.y, o.z)
    engine("setQuaternion")(node, q.x, q.y, q.z, q.w)
    engine("setTranslation")(node, -tx, -ty, -tz)
end

local function modelPath(self, hand)
    local file = hand == self.MOTION_HAND_RIGHT and self.VIRTUAL_HAND_MODEL_RIGHT or self.VIRTUAL_HAND_MODEL_LEFT
    return (self.modDirectory or "") .. file
end

local function buildHands(self)
    for _, name in ipairs({ "loadI3DFile", "clone", "link", "getParent", "createTransformGroup", "getRootNode", "getChildAt",
                            "setVisibility", "worldToLocal", "getWorldQuaternion", "getName",
                            "getNumOfChildren", "getQuaternion", "setQuaternion", "setTranslation" }) do
        if type(engine(name)) ~= "function" then return name .. " is not available" end
    end
    local fileExists = engine("fileExists")
    if type(fileExists) == "function" then
        for _, hand in ipairs(HANDS) do
            local path = modelPath(self, hand)
            if not fileExists(path) then return "the model file is missing: " .. path end
        end
    end

    local missing = {}
    for _, hand in ipairs(HANDS) do
        local path = modelPath(self, hand)
        local ok, root, failedReason = pcall(engine("loadI3DFile"), path, false, false)
        if not ok then return "loading " .. path .. " raised: " .. tostring(root) end
        if root == nil or root == 0 then
            return string.format("%s failed to load (reason %s)", path, tostring(failedReason))
        end
        state.fileRoots[hand] = root
        local template = engine("getChildAt")(root, 0)
        if template == nil or template == 0 then return path .. " holds no hand node" end

        local holder = engine("createTransformGroup")("virtualHand_" .. HAND_NAMES[hand])
        local entry = { holder = holder, shown = false, status = "hidden", placement = {}, world = {}, relative = {} }
        state.hands[hand] = entry
        engine("link")(engine("getRootNode")(), holder)
        local model = engine("clone")(template, false, false)
        if model == nil or model == 0 then return "cloning the hand node of " .. path .. " failed" end
        entry.model = model
        local offset = engine("createTransformGroup")("virtualHandModelToGrip_" .. HAND_NAMES[hand])
        engine("link")(holder, offset)
        applyModelToGrip(offset, hand == self.MOTION_HAND_RIGHT and self.VIRTUAL_HAND_MODEL_TO_GRIP.right
            or self.VIRTUAL_HAND_MODEL_TO_GRIP.left)
        engine("link")(offset, model)
        disableShadowCasting(model)
        engine("setVisibility")(holder, false)
        for name in pairs(findJoints(entry)) do missing[name] = true end
    end    if next(missing) ~= nil and not state.missingJointsLogged then
        state.missingJointsLogged = true
        local names = {}
        for _, finger in ipairs(FINGERS) do
            if missing[finger.base] then table.insert(names, finger.base) end
            for _, name in ipairs(finger.joints) do
                if missing[name] then table.insert(names, name) end
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

local function curlFingers(entry, pose, holdsTool)
    local setQuaternion = engine("setQuaternion")
    local degrees = VRCompanion.FINGER_CURL_DEGREES
    for _, joint in ipairs(entry.joints or {}) do
        local value = math.max(0, math.min(1, tonumber(pose[joint.input]) or 0))
        if holdsTool and joint.input == "grip" then value = VRCompanion.HELD_TOOL_GRIP_CURL end
        local half = VRCompanion.FINGER_CURL_DIRECTION * math.rad(degrees[joint.segment]) * value * 0.5
        local bx, bw = math.sin(half), math.cos(half)
        setQuaternion(joint.node, VRCompanion.multiplyQuaternions(joint.restX, joint.restY, joint.restZ, joint.restW,
            bx, 0, 0, bw))
    end
end

local function nodeExists(node)
    if node == nil or node == 0 then return false end
    local exists = engine("entityExists")
    return type(exists) ~= "function" or exists(node)
end

local function worldToAnchorSpace(anchor, aqx, aqy, aqz, aqw, x, y, z, qx, qy, qz, qw, out)
    local worldToLocal = engine("worldToLocal")
    if type(worldToLocal) ~= "function" then
        return false
    end
    local okL, lx, ly, lz = pcall(worldToLocal, anchor, x, y, z)
    if not okL or type(lx) ~= "number" then return false end
    out.x, out.y, out.z = lx, ly, lz
    out.qx, out.qy, out.qz, out.qw = VRCompanion.multiplyQuaternions(-aqx, -aqy, -aqz, aqw, qx, qy, qz, qw)
    return true
end

local function nodeName(node)
    local getName = engine("getName")
    if type(getName) ~= "function" or not nodeExists(node) then return "?" end
    local ok, name = pcall(getName, node)
    return ok and tostring(name) or "?"
end

local function bodyAnchor(head)
    local getParent, getRootNode = engine("getParent"), engine("getRootNode")
    if type(getRootNode) ~= "function" then return 0, "there is no root node" end
    local root = getRootNode()
    if not nodeExists(head) or type(getParent) ~= "function" then return root, "there is no head node" end
    local node = head
    while true do
        local okP, parent = pcall(getParent, node)
        if not okP then return root, "the head node failed" end
        if parent == root then
            local getVisibility = engine("getVisibility")
            if type(getVisibility) == "function" then
                local okV, visible = pcall(getVisibility, node)
                if okV and not visible then
                    return root, nodeName(node) .. " is hidden"
                end
            end
            return node, nil
        end
        if parent == nil or parent == 0 then
            if node == head then return root, "the head hangs on nothing" end
            return root, nodeName(node) .. " is not below the scene root"
        end
        node = parent
    end
end

local function noteAnchor(anchor, why)
    if anchor == state.anchor then return end
    state.anchor = anchor
    local suffix = why ~= nil and (" (" .. why .. ")") or ""
    print(string.format("[FS25_VR] Virtual Hands ride on %s%s", nodeName(anchor), suffix))
    for _, hand in ipairs(HANDS) do
        if state.hands[hand] ~= nil then state.hands[hand].geometryLogged = false end
    end
end

local function pinToWheel(self, hand, world)
    local x, y, z, qx, qy, qz, qw = self:getWheelGrabHandPose(hand)
    if x ~= nil then
        world.x, world.y, world.z, world.qx, world.qy, world.qz, world.qw = x, y, z, qx, qy, qz, qw
    end
end

local function resolveAnchorPose(listener)
    local anchor, why = bodyAnchor(listener)
    if not nodeExists(anchor) then return nil end
    noteAnchor(anchor, why)
    local getWorldQuaternion = engine("getWorldQuaternion")
    if type(getWorldQuaternion) ~= "function" then return nil end
    local okQ, aqx, aqy, aqz, aqw = pcall(getWorldQuaternion, anchor)
    if not okQ or type(aqw) ~= "number" then return nil end
    return anchor, aqx, aqy, aqz, aqw
end

local function placeHolder(entry, world, anchor, aqx, aqy, aqz, aqw)
    if anchor == nil or not nodeExists(entry.holder) then return false end
    local placement = entry.placement
    if not worldToAnchorSpace(anchor, aqx, aqy, aqz, aqw, world.x, world.y, world.z, world.qx, world.qy, world.qz, world.qw, placement) then
        return false
    end
    local getParent, link = engine("getParent"), engine("link")
    local setTranslation, setQuaternion = engine("setTranslation"), engine("setQuaternion")
    if type(getParent) == "function" and type(link) == "function" then
        local okP, currentParent = pcall(getParent, entry.holder)
        if okP and currentParent ~= anchor then pcall(link, anchor, entry.holder) end
    end
    if type(setTranslation) == "function" then pcall(setTranslation, entry.holder, placement.x, placement.y, placement.z) end
    if type(setQuaternion) == "function" then pcall(setQuaternion, entry.holder, placement.qx, placement.qy, placement.qz, placement.qw) end
    return true
end

local function handsWereDeleted()
    local exists = engine("entityExists")
    if type(exists) ~= "function" then return false end
    for _, hand in ipairs(HANDS) do
        local entry = state.hands[hand]
        if entry ~= nil and not exists(entry.holder) then return true end
    end
    return false
end

local describeEngineGeometry
local traceHands

local function updateHand(self, hand, unavailable, anchor, aqx, aqy, aqz, aqw)
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
            local world, relative = entry.world, entry.relative
            local ok, why = self:headRelativePoseToWorld(pose.gripPoseX, pose.gripPoseY, pose.gripPoseZ,
                pose.gripPoseQx, pose.gripPoseQy, pose.gripPoseQz, pose.gripPoseQw, world, relative)
            if ok then
                if relative.valid then
                    pinToWheel(self, hand, world)
                    if placeHolder(entry, world, anchor, aqx, aqy, aqz, aqw) then
                        local holdsTool = self:isHandHoldingHandTool(hand) == true
                        curlFingers(entry, pose, holdsTool)
                        status = "shown"
                        if not entry.geometryLogged then
                            entry.geometryLogged = true
                            print(string.format("[FS25_VR] Virtual Hand geometry (%s): %s", HAND_NAMES[hand], describeEngineGeometry(entry)))
                        end
                    else
                        status = "hidden (holder placement failed)"
                    end
                else
                    relative.valid = false
                    status = "hidden (the listener node has no pose)"
                end
            else
                relative.valid = false
                status = "hidden (" .. why .. ")"
            end
        end
    end
    setShown(entry, status == "shown")
    entry.status = status
    return status
end

function VRCompanion:getVirtualHandHolder(hand)
    local entry = state.hands[hand]
    if entry == nil or entry.holder == nil then return nil end
    local exists = engine("entityExists")
    if type(exists) == "function" and not exists(entry.holder) then return nil end
    return entry.holder
end

function VRCompanion:updateVirtualHands(dt)
    if state.modelTried and state.modelFailure == nil and handsWereDeleted() then
        discardScene()
        state.modelTried = false
    end
    if self.motionControllersEnabled ~= false and not state.modelTried then
        loadModel(self)
    end
    local unavailable = unavailableReason(self)
    local reason = unavailable or "available"
    if reason ~= state.reason then
        state.reason = reason
        print("[FS25_VR] Virtual Hands: " .. reason)
    end
    local anchor, aqx, aqy, aqz, aqw = nil, nil, nil, nil, nil
    if unavailable == nil then
        anchor, aqx, aqy, aqz, aqw = resolveAnchorPose(self.cyclopeanAudioListenerNode)
    end
    for _, hand in ipairs(HANDS) do
        updateHand(self, hand, unavailable, anchor, aqx, aqy, aqz, aqw)
    end
    traceHands(self, "update")
end

function describeEngineGeometry(entry)
    local getWorldTranslation, getScale = engine("getWorldTranslation"), engine("getScale")
    if type(getWorldTranslation) ~= "function" or entry == nil or entry.model == nil then
        return "no engine geometry"
    end
    local function at(node)
        if node == nil then return "missing" end
        local x, y, z = getWorldTranslation(node)
        return string.format("(%.3f, %.3f, %.3f)", x, y, z)
    end
    local function distance(a, b)
        if a == nil or b == nil then return "?" end
        local ax, ay, az = getWorldTranslation(a)
        local bx, by, bz = getWorldTranslation(b)
        return string.format("%.3f", math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2 + (az - bz) ^ 2))
    end
    local wrist, indexTip, middleTip = findNode(entry.model, "Wrist"), findNode(entry.model, "Top.001"), findNode(entry.model, "Top.002")
    local scale = "?"
    if type(getScale) == "function" then
        local sx, sy, sz = getScale(entry.model)
        scale = string.format("%.4f %.4f %.4f", sx, sy, sz)
    end
    local getParent, getName = engine("getParent"), engine("getName")
    local parent = type(getParent) == "function" and getParent(entry.holder) or nil
    local parentName = (parent ~= nil and parent ~= 0 and type(getName) == "function") and tostring(getName(parent)) or "?"
    return string.format("holder %s on %s %s, model %s (local scale %s, expect 1.0000: the file is already in metres), Wrist %s, Wrist to Top.001 %s m, Wrist to Top.002 %s m (expect about 0.20: 0.197 and 0.204)",
        at(entry.holder), parentName, parent ~= nil and parent ~= 0 and at(parent) or "", at(entry.model), scale, at(wrist),
        distance(wrist, indexTip), distance(wrist, middleTip))
end

function traceHands(self, moment)
    if (state.traceFrames or 0) <= 0 then return end
    local getCamera, getWorldTranslation = engine("getCamera"), engine("getWorldTranslation")
    if type(getCamera) ~= "function" or type(getWorldTranslation) ~= "function" then return end
    local camera = getCamera()
    if camera == nil or camera == 0 then return end
    local worldToLocal = engine("worldToLocal")
    local parts = {}
    for _, hand in ipairs(HANDS) do
        local entry = state.hands[hand]
        if entry ~= nil and entry.shown then
            local x, y, z = worldToLocal(camera, getWorldTranslation(entry.holder))
            table.insert(parts, string.format("%s in camera (%.4f, %.4f, %.4f)", HAND_NAMES[hand], x, y, z))
        end
    end
    if #parts == 0 then return end
    local cameraInAnchor = "?"
    if state.anchor ~= nil then
        cameraInAnchor = string.format("(%.4f, %.4f, %.4f)", worldToLocal(state.anchor, getWorldTranslation(camera)))
    end
    local parity = self:getFrameParity() or "?"
    print(string.format("[FS25_VR] Virtual Hands trace %s: frame %s eye %s | %s | camera in anchor %s",
        moment, tostring(self.frameTicks), tostring(parity), table.concat(parts, " | "), cameraInAnchor))
end

function VRCompanion:placeVirtualHandsForRender()
    local listener = self.cyclopeanAudioListenerNode
    local localToWorld, getWorldQuaternion = engine("localToWorld"), engine("getWorldQuaternion")
    if not nodeExists(listener) or type(localToWorld) ~= "function" or type(getWorldQuaternion) ~= "function" then
        return
    end
    local okQ, lqx, lqy, lqz, lqw = pcall(getWorldQuaternion, listener)
    if not okQ or type(lqw) ~= "number" then return end
    local anchor, aqx, aqy, aqz, aqw = resolveAnchorPose(listener)
    for _, hand in ipairs(HANDS) do
        local entry = state.hands[hand]
        if entry ~= nil and entry.shown and entry.relative.valid then
            local relative, world = entry.relative, entry.world
            local okP, wx, wy, wz = pcall(localToWorld, listener, relative.x, relative.y, relative.z)
            if okP and type(wx) == "number" then
                world.x, world.y, world.z = wx, wy, wz
                world.qx, world.qy, world.qz, world.qw = VRCompanion.multiplyQuaternions(lqx, lqy, lqz, lqw,
                    relative.qx, relative.qy, relative.qz, relative.qw)
                pinToWheel(self, hand, world)
                placeHolder(entry, world, anchor, aqx, aqy, aqz, aqw)
            end
        end
    end
    self:placeHandToolsForRender()
    traceHands(self, "camera")
end

function VRCompanion:drawVirtualHands()
    if (state.traceFrames or 0) > 0 then
        traceHands(self, "draw")
        state.traceFrames = state.traceFrames - 1
    end
end

function VRCompanion:consoleCommandVirtualHandsTrace(arg)
    local frames = 16
    if arg ~= nil and arg ~= "" then
        frames = VRCompanion.parseFiniteNumber(arg, 1, nil, true)
        if frames == nil then return "Usage: vrVirtualHandsTrace [frames, default 16]" end
    end
    state.traceFrames = frames
    return string.format("Tracing the Virtual Hands for %d frames into the log", frames)
end

function VRCompanion:getVirtualHandsDiagnostics()
    local function describe(hand)
        local entry = state.hands[hand]
        return entry ~= nil and entry.status or "no hand"
    end
    return string.format("Virtual Hands: %s | Left Virtual Hand: %s | Right Virtual Hand: %s",
        tostring(state.reason or "not updated yet"), describe(0), describe(1))
end

function VRCompanion:consoleCommandVirtualHandsGeometry()
    return string.format("Left Virtual Hand: %s | Right Virtual Hand: %s",
        describeEngineGeometry(state.hands[0]), describeEngineGeometry(state.hands[1]))
end

function VRCompanion:consoleCommandVirtualHandsDiagnostics()
    return self:getVirtualHandsDiagnostics()
end

function VRCompanion:resetVirtualHands()
    discardScene()
    state = newState()
end

VRCompanion:registerSubsystem("virtualHands", {
    mapUnload = function(self) self:resetVirtualHands() end,
    consoleCommands = {
        { name = "vrVirtualHandsDiagnostics", description = "Report why the Virtual Hands are shown or hidden: vrVirtualHandsDiagnostics", method = "consoleCommandVirtualHandsDiagnostics" },
        { name = "vrVirtualHandsGeometry", description = "Report where the engine puts the Virtual Hands and how big they are: vrVirtualHandsGeometry", method = "consoleCommandVirtualHandsGeometry" },
        { name = "vrVirtualHandsTrace", description = "Log where each Virtual Hand is relative to the rendering camera, after the update and at draw time, for some frames: vrVirtualHandsTrace [frames]", method = "consoleCommandVirtualHandsTrace" },
    },
})
