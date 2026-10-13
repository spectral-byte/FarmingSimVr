#pragma once

// Shared engine and mod stubs for Companion Mod tests, as Luau chunks to run with RunLuauChunk
// (see companion_mod_fixture.h). Per-feature test files include this header and reuse them.
//
//   kStubSceneGraph          a small scene graph (nodes, parents, local and world transforms) with the
//                            engine's node functions, and StubScene.cab / StubScene.onFoot building a
//                            camera chain the real Companion Mod drives
//   kStubModelLoading        the engine's model loading and node functions the Virtual Hands use (StubModel), on top of
//                            the scene graph: a loadable hand model with finger joints, clones, visibility, scale, world
//                            transforms, joint rotations, shadow flags, and a model that is missing, failing or lacking joints
//   kStubInteractiveControl  a stand-in for the Interactive Control mod's Click Point class and manager
//                            (StubIC), with a mismatched variant
//
// They model only the names and shapes the Companion Mod depends on (ADR-0026). Nothing here is
// copied from Interactive Control or from the game.
//
// Each constant stays well under the compiler's limit for one string literal.

// ---- Scene graph ---------------------------------------------------------------------------------
// Nodes are plain tables. Euler angles use the engine convention R = Ry * Rx * Rz.
//
//   StubScene.newNode(name, parent, x, y, z, rx, ry, rz) -> node id   (parent 0 = world root)
//   StubScene.worldPosition(node) -> x, y, z      StubScene.worldDirection(node, x, y, z) -> x, y, z
//   StubScene.cab(options) -> { vehicle, cam, cameraNode, headTrackingNode, parent }
//       options: x, y, z, yaw  = the cab's pose in the world (default 0, 0, 0, 0)
//     A vehicle in the cab view: camParent -> cameraNode, headTrackingNode (1.5 m up, turned by pi like
//     the game's cab cameras). g_currentMission.controlledVehicle is the vehicle and
//     StubScene.updateCamera() runs VRCompanion:onVehicleCameraUpdate for it.
//   StubScene.outside(options) -> { cam, cameraNode }   an Outside Camera of the controlled vehicle at the world
//       pose x, y, z, yaw (the engine only reads it); StubScene.updateCamera() runs onVehicleCameraUpdate
//   StubScene.onFoot(options) -> { parent, yawNode, firstPersonCamera }   (a player on foot)
//       options: x, y, z, yaw. StubScene.updateCamera() then runs onPlayerCameraUpdate.
inline constexpr const char* kStubSceneGraph = R"luau(
    StubScene = {}
    local nodes = {}
    StubScene.nodes = nodes
    local nextId = 1000

    local function qmul(ax, ay, az, aw, bx, by, bz, bw)
        return aw * bx + ax * bw + ay * bz - az * by,
               aw * by - ax * bz + ay * bw + az * bx,
               aw * bz + ax * by - ay * bx + az * bw,
               aw * bw - ax * bx - ay * by - az * bz
    end
    local function qrotate(qx, qy, qz, qw, x, y, z)
        local cx, cy, cz = qy * z - qz * y + qw * x, qz * x - qx * z + qw * y, qx * y - qy * x + qw * z
        return x + 2 * (qy * cz - qz * cy), y + 2 * (qz * cx - qx * cz), z + 2 * (qx * cy - qy * cx)
    end
    local function eulerToQuat(rx, ry, rz)
        local cx, sx = math.cos(rx * 0.5), math.sin(rx * 0.5)
        local cy, sy = math.cos(ry * 0.5), math.sin(ry * 0.5)
        local cz, sz = math.cos(rz * 0.5), math.sin(rz * 0.5)
        local x, y, z, w = qmul(0, sy, 0, cy, sx, 0, 0, cx)
        return qmul(x, y, z, w, 0, 0, sz, cz)
    end
    local function quatToEuler(qx, qy, qz, qw)
        local sinPitch = math.max(-1, math.min(1, 2 * (qw * qx - qy * qz)))
        return math.asin(sinPitch),
            math.atan2(2 * (qw * qy + qx * qz), 1 - 2 * (qx * qx + qy * qy)),
            math.atan2(2 * (qw * qz + qx * qy), 1 - 2 * (qx * qx + qz * qz))
    end
    local function world(id)
        local n = nodes[id]
        if n == nil then return 0, 0, 0, 0, 0, 0, 1 end
        if n.parent == 0 or nodes[n.parent] == nil then
            return n.x, n.y, n.z, n.qx, n.qy, n.qz, n.qw
        end
        local px, py, pz, pqx, pqy, pqz, pqw = world(n.parent)
        local rx, ry, rz = qrotate(pqx, pqy, pqz, pqw, n.x, n.y, n.z)
        local qx, qy, qz, qw = qmul(pqx, pqy, pqz, pqw, n.qx, n.qy, n.qz, n.qw)
        return px + rx, py + ry, pz + rz, qx, qy, qz, qw
    end

    function StubScene.newNode(name, parent, x, y, z, rx, ry, rz)
        nextId = nextId + 1
        local qx, qy, qz, qw = eulerToQuat(rx or 0, ry or 0, rz or 0)
        nodes[nextId] = { name = name, parent = parent or 0, x = x or 0, y = y or 0, z = z or 0, qx = qx, qy = qy, qz = qz, qw = qw }
        return nextId
    end
    function StubScene.worldPosition(node) local x, y, z = world(node); return x, y, z end
    function StubScene.worldDirection(node, x, y, z)
        local _, _, _, qx, qy, qz, qw = world(node)
        return qrotate(qx, qy, qz, qw, x, y, z)
    end

    _G.createTransformGroup = function(name) return StubScene.newNode(name, 0) end
    _G.createCamera = function(name) return StubScene.newNode(name, 0) end
    local rootNode = StubScene.newNode("root", 0)
    _G.getRootNode = function() return rootNode end
    _G.getParent = function(node) return nodes[node] and nodes[node].parent or 0 end
    _G.link = function(parent, child) if nodes[child] then nodes[child].parent = parent end end
    _G.setTranslation = function(node, x, y, z) local n = nodes[node]; if n then n.x, n.y, n.z = x, y, z end end
    _G.getTranslation = function(node) local n = nodes[node]; return n.x, n.y, n.z end
    _G.setRotation = function(node, rx, ry, rz)
        local n = nodes[node]
        if n then n.qx, n.qy, n.qz, n.qw = eulerToQuat(rx, ry, rz) end
    end
    _G.getRotation = function(node) local n = nodes[node]; return quatToEuler(n.qx, n.qy, n.qz, n.qw) end
    _G.setQuaternion = function(node, qx, qy, qz, qw)
        local n = nodes[node]
        if n then n.qx, n.qy, n.qz, n.qw = qx, qy, qz, qw end
    end
    _G.getWorldTranslation = function(node) local x, y, z = world(node); return x, y, z end
    _G.getWorldQuaternion = function(node) local _, _, _, qx, qy, qz, qw = world(node); return qx, qy, qz, qw end
    _G.localToLocal = function(from, to, x, y, z)
        local fx, fy, fz, fqx, fqy, fqz, fqw = world(from)
        local rx, ry, rz = qrotate(fqx, fqy, fqz, fqw, x, y, z)
        local tx, ty, tz, tqx, tqy, tqz, tqw = world(to)
        return qrotate(-tqx, -tqy, -tqz, tqw, fx + rx - tx, fy + ry - ty, fz + rz - tz)
    end
    _G.localDirectionToWorld = function(node, x, y, z) return StubScene.worldDirection(node, x, y, z) end
    _G.getNearClip = function() return 0.1 end
    _G.getFarClip = function() return 1000 end
    _G.getFovY = function() return 1.0 end
    _G.setFovY = function() end
    _G.setProjectionOffset = function() end
    _G.getProjectionOffset = function() return 0, 0 end
    _G.setCamera = function(node) _G.activeCamera = node end
    _G.getCamera = function() return _G.activeCamera or 0 end
    _G.setAudioListener = function() end
    _G.getAudioListener = function() return 0 end
    _G.activeCamera = 0

    function StubScene.cab(options)
        options = options or {}
        local parent = StubScene.newNode("cab", 0, options.x, options.y, options.z, 0, options.yaw or 0, 0)
        local cameraNode = StubScene.newNode("cabCamera", parent, 0, 1.5, 0, 0, math.pi, 0)
        local headTrackingNode = StubScene.newNode("headTracking", parent, 0, 1.5, 0, 0, math.pi, 0)
        local cam = { isInside = true, cameraNode = cameraNode, headTrackingNode = headTrackingNode,
                      origHeadTrackingRotX = 0, origHeadTrackingRotY = math.pi, origHeadTrackingRotZ = 0,
                      origRotY = math.pi, rotY = math.pi }
        local vehicle = { getActiveCamera = function() return cam end }
        _G.g_currentMission = _G.g_currentMission or {}
        _G.g_currentMission.controlledVehicle = vehicle
        _G.activeCamera = cameraNode
        StubScene.updateCamera = function() VRCompanion:onVehicleCameraUpdate(cam, 16.667) end
        VRCompanion:onEnterVehicle(vehicle, true, nil)
        return { vehicle = vehicle, cam = cam, cameraNode = cameraNode, headTrackingNode = headTrackingNode, parent = parent }
    end

    function StubScene.outside(options)
        options = options or {}
        local cameraNode = StubScene.newNode("outsideCamera", 0, options.x, options.y, options.z, 0, options.yaw or 0, 0)
        local cam = { isInside = false, cameraNode = cameraNode }
        local vehicle = { getActiveCamera = function() return cam end }
        _G.g_currentMission = _G.g_currentMission or {}
        _G.g_currentMission.controlledVehicle = vehicle
        _G.activeCamera = cameraNode
        StubScene.updateCamera = function() VRCompanion:onVehicleCameraUpdate(cam, 16.667) end
        VRCompanion:onEnterVehicle(vehicle, true, nil)
        return { cam = cam, cameraNode = cameraNode }
    end

    function StubScene.onFoot(options)
        options = options or {}
        local root = StubScene.newNode("player", 0, options.x, options.y, options.z)
        local yawNode = StubScene.newNode("playerYaw", root, 0, 0, 0, 0, options.yaw or 0, 0)
        local pitchNode = StubScene.newNode("playerPitch", yawNode, 0, 1.7, 0)
        local firstPersonCamera = StubScene.newNode("playerCamera", pitchNode, 0, 0, 0)
        local cam = { isFirstPerson = true, firstPersonCamera = firstPersonCamera, yawNode = yawNode, pitchNode = pitchNode,
                      cameraRootNode = pitchNode, setRotation = function(self, pitch, yaw, roll) end }
        _G.g_currentMission = _G.g_currentMission or {}
        _G.g_currentMission.controlledVehicle = nil
        _G.g_currentMission.player = { camera = cam }
        _G.activeCamera = firstPersonCamera
        VRCompanion:onPlayerCameraMakeCurrent(cam)
        StubScene.updateCamera = function() VRCompanion:onPlayerCameraUpdate(cam, 16.667) end
        return { cam = cam, parent = root, yawNode = yawNode, firstPersonCamera = firstPersonCamera }
    end
)luau";

// ---- Model loading ---------------------------------------------------------------------------------------
// Run after kStubSceneGraph. The engine functions the Virtual Hands call to bring in their model, with a
// hand model in the shape the model contract describes (a file root whose first child is the hand's root
// node, with the finger joints below it and one shape).
//
//   StubModel.fileExists (default true)   what fileExists() answers for the model file
//   StubModel.failLoad (default false)    loadI3DFile answers 0 (and a failure reason) although the file exists
//   StubModel.loads                       { { filename, root } } per loadI3DFile call that reached the "engine"
//   StubModel.clones                      node ids of every clone() of a node (the hands), in order
//   StubModel.deleted                     node id -> true for every deleted node
//   StubModel.hands() -> { left = node, right = node }   the cloned hand nodes, told apart by mirroring
//                         (the left clone is the right model scaled -1 along X); a missing one is nil
//   StubModel.isShown(node)               the node and every ancestor are visible (and it exists)
//   StubModel.scale(node) -> x, y, z      StubModel.castsShadow(node) -> true when any shape below casts one
//   StubModel.isLinkedToScene(node)       the node's ancestors lead up to the scene root
//   StubModel.missingJoints               set of node names of the model's file (e.g. { ["Middle.001"] = true }) the
//                                         next loaded model lacks
//   StubModel.joint(hand, name) -> node   the finger joint below a hand node, or nil ("thumb1".."little3": finger
//                                         and curl segment, looked up in that finger's own subtree)
//   StubModel.curl(hand, name) -> radians how far the joint is curled from its rest pose, TOWARD THE PALM, which for
//                                         this model (as for the shipped one) is a NEGATIVE rotation about the joint's own
//                                         X axis; errors when the joint turned about any other axis
// Every joint of the stub model rests in a slightly bent pose that is NOT the identity rotation (each segment a
// different X angle around the model's -90 degree bone-axis turn, plus a small Y and Z twist), so a curl that
// ignored the model's rest pose would show.
// Nodes are visible, unscaled and (for shapes) casting shadows by default, like freshly loaded ones.
inline constexpr const char* kStubModelLoading = R"luau(
    StubModel = { fileExists = true, failLoad = false, missingJoints = {}, loads = {}, clones = {}, deleted = {} }
    local nodes = StubScene.nodes
    local function qmul(ax, ay, az, aw, bx, by, bz, bw)
        return aw * bx + ax * bw + ay * bz - az * by,
               aw * by - ax * bz + ay * bw + az * bx,
               aw * bz + ax * by - ay * bx + az * bw,
               aw * bw - ax * bx - ay * by - az * bz
    end
    local function qrotate(qx, qy, qz, qw, x, y, z)
        local cx, cy, cz = qy * z - qz * y + qw * x, qz * x - qx * z + qw * y, qx * y - qy * x + qw * z
        return x + 2 * (qy * cz - qz * cy), y + 2 * (qz * cx - qx * cz), z + 2 * (qx * cy - qy * cx)
    end
    local function childrenOf(id)
        local list = {}
        for childId, node in pairs(nodes) do
            if node.parent == id then table.insert(list, childId) end
        end
        table.sort(list)
        return list
    end

    _G.ClassIds = { SHAPE = 1 }
    _G.fileExists = function(filename) return StubModel.fileExists end
    _G.entityExists = function(id) return nodes[id] ~= nil end
    _G.getHasClassId = function(id, classId) return nodes[id] ~= nil and classId == 1 and nodes[id].isShape == true end
    _G.getNumOfChildren = function(id) return #childrenOf(id) end
    _G.getChildAt = function(id, index) return childrenOf(id)[index + 1] or 0 end
    _G.getName = function(id) return nodes[id] and nodes[id].name or "" end
    _G.getChild = function(id, name)
        for _, childId in ipairs(childrenOf(id)) do
            if nodes[childId].name == name then return childId end
        end
        return 0
    end
    _G.unlink = function(id) if nodes[id] then nodes[id].parent = 0 end end
    _G.setVisibility = function(id, visible) if nodes[id] then nodes[id].visible = visible end end
    _G.getVisibility = function(id) return nodes[id] ~= nil and nodes[id].visible ~= false end
    _G.setScale = function(id, x, y, z) if nodes[id] then nodes[id].sx, nodes[id].sy, nodes[id].sz = x, y, z end end
    _G.getScale = function(id) local n = nodes[id]; return n.sx or 1, n.sy or 1, n.sz or 1 end
    _G.setShapeCastShadowmap = function(id, cast) if nodes[id] then nodes[id].castShadow = cast end end
    _G.setWorldTranslation = function(id, x, y, z)
        local n = nodes[id]
        if n == nil then return end
        if n.parent == 0 or nodes[n.parent] == nil then n.x, n.y, n.z = x, y, z return end
        n.x, n.y, n.z = localToLocal(0, n.parent, x, y, z)
    end
    _G.setWorldQuaternion = function(id, qx, qy, qz, qw)
        local n = nodes[id]
        if n == nil then return end
        if n.parent == 0 or nodes[n.parent] == nil then n.qx, n.qy, n.qz, n.qw = qx, qy, qz, qw return end
        local px, py, pz, pw = getWorldQuaternion(n.parent)
        n.qx, n.qy, n.qz, n.qw = qmul(-px, -py, -pz, pw, qx, qy, qz, qw)
    end

    -- The joint names of the shipped model (VirtualHandLeft.i3d), written out here on purpose: the stub stands for
    -- the file, the mod must find these by its own table. Per finger: the base bone, then the three curl joints.
    -- The thumb's joints are called Lower, Middle and Top, like the fingers' without the .00N suffix.
    local STUB_FINGERS = {
        index  = { "Index Finger", "Lower.001", "Middle.001", "Top.001" },
        middle = { "Middle Finger", "Lower.002", "Middle.002", "Top.002" },
        ring   = { "Ring Finger", "Lower.003", "Middle.003", "Top.003" },
        little = { "Pinky", "Lower.004", "Middle.004", "Top.004" },
        thumb  = { "Thumb", "Lower", "Middle", "Top" },
    }
    -- the order the file lists the fingers below the Hand bone
    local STUB_FINGER_ORDER = { "index", "middle", "little", "ring", "thumb" }

    local function buildHand()
        local fileRoot = StubScene.newNode("virtualHandFile", 0)
        local root = StubScene.newNode("Hand.002", fileRoot)  -- the file's first child, the hand's root node
        local mesh = StubScene.newNode("HandModel.001", root)
        nodes[mesh].isShape = true
        nodes[mesh].castShadow = true
        local wrist = StubScene.newNode("Wrist", root)
        local palm = StubScene.newNode("Hand", wrist)
        for _, finger in ipairs(STUB_FINGER_ORDER) do
            local parent = palm
            for index, name in ipairs(STUB_FINGERS[finger]) do
                if not StubModel.missingJoints[name] then
                    -- the model's relaxed pose, like the shipped one's: the bone-axis turn of -90 degrees about X
                    -- (every bone has it), flexed a little more at each segment (a NEGATIVE turn about X is toward
                    -- the palm in this model), with a small twist
                    parent = StubScene.newNode(name, parent, 0, 0, 0, -1.5708 - 0.1 * index, 0.1, -0.05)
                    local n = nodes[parent]
                    n.restQx, n.restQy, n.restQz, n.restQw = n.qx, n.qy, n.qz, n.qw
                end
            end
        end
        return fileRoot
    end
    _G.loadI3DFile = function(filename, addPhysics, callOnCreate)
        if not StubModel.fileExists or StubModel.failLoad then return 0, 1 end
        local root = buildHand()
        table.insert(StubModel.loads, { filename = filename, root = root, addPhysics = addPhysics, callOnCreate = callOnCreate })
        return root, 0
    end
    local function copyTree(id, parent)
        local source = nodes[id]
        local copy = StubScene.newNode(source.name, parent, source.x, source.y, source.z)
        local n = nodes[copy]
        n.qx, n.qy, n.qz, n.qw = source.qx, source.qy, source.qz, source.qw
        n.restQx, n.restQy, n.restQz, n.restQw = source.restQx, source.restQy, source.restQz, source.restQw
        n.isShape, n.castShadow, n.visible = source.isShape, source.castShadow, source.visible
        n.sx, n.sy, n.sz = source.sx, source.sy, source.sz
        for _, childId in ipairs(childrenOf(id)) do copyTree(childId, copy) end
        return copy
    end
    _G.clone = function(id, groupUnderParent, callOnCreate)
        if nodes[id] == nil then return 0 end
        local copy = copyTree(id, groupUnderParent and nodes[id].parent or 0)
        table.insert(StubModel.clones, copy)
        return copy
    end
    _G.delete = function(id)
        if nodes[id] == nil then return end
        for _, childId in ipairs(childrenOf(id)) do _G.delete(childId) end
        nodes[id] = nil
        StubModel.deleted[id] = true
    end

    _G.getQuaternion = function(id) local n = nodes[id]; return n.qx, n.qy, n.qz, n.qw end

    local function findByName(id, name)
        if nodes[id] == nil then return nil end
        for _, childId in ipairs(childrenOf(id)) do
            if nodes[childId].name == name then return childId end
            local found = findByName(childId, name)
            if found ~= nil then return found end
        end
        return nil
    end
    -- "index2" -> the middle curl joint of the index finger of the hand node, looked up inside that finger's own
    -- subtree (the thumb's joints have names of their own too); nil when the model lacks it
    function StubModel.joint(id, joint)
        local finger, segment = joint:match("^(%a+)(%d)$")
        local names = STUB_FINGERS[finger]
        assert(names ~= nil, joint .. " is not a finger joint of the stub model")
        local base = findByName(id, names[1])
        if base == nil then return nil end
        return findByName(base, names[tonumber(segment) + 1])
    end
    function StubModel.curl(hand, name)
        local id = StubModel.joint(hand, name)
        assert(id ~= nil, name .. " is not in the model")
        return StubModel.curlOfNode(id, name)
    end
    -- the same for a node found by its own name in the file, wherever it hangs (for a finger the mod cannot reach)
    function StubModel.curlOfNamed(hand, nodeName)
        local id = findByName(hand, nodeName)
        assert(id ~= nil, nodeName .. " is not in the model")
        return StubModel.curlOfNode(id, nodeName)
    end
    function StubModel.curlOfNode(id, name)
        local n = nodes[id]
        -- rotation from the rest pose to now, in the joint's own frame: conj(rest) * current
        local x, y, z, w = qmul(-n.restQx, -n.restQy, -n.restQz, n.restQw, n.qx, n.qy, n.qz, n.qw)
        assert(math.abs(y) < 1e-4 and math.abs(z) < 1e-4,
            string.format("%s turned about an axis other than its local X (%.4f %.4f %.4f %.4f)", name, x, y, z, w))
        -- a negative turn about X flexes this model's fingers toward the palm, so that is a positive curl
        return -2 * math.atan2(x, w)
    end
    function StubModel.scale(id) return getScale(id) end
    function StubModel.isShown(id)
        while id ~= nil and id ~= 0 do
            local n = nodes[id]
            if n == nil or n.visible == false then return false end
            id = n.parent
        end
        return true
    end
    function StubModel.isLinkedToScene(id)
        while id ~= nil and id ~= 0 do
            if nodes[id] == nil then return false end
            if id == getRootNode() then return true end
            id = nodes[id].parent
        end
        return false
    end
    function StubModel.castsShadow(id)
        local n = nodes[id]
        if n == nil then return false end
        if n.isShape and n.castShadow then return true end
        for _, childId in ipairs(childrenOf(id)) do
            if StubModel.castsShadow(childId) then return true end
        end
        return false
    end
    function StubModel.hands()
        local found = {}
        for _, id in ipairs(StubModel.clones) do
            if nodes[id] ~= nil then
                local sx = getScale(id)
                if sx < 0 then found.left = id else found.right = id end
            end
        end
        return found
    end
)luau";
// ---- Interactive Control ------------------------------------------------------------------------------
// StubIC.install(variant)  installs the globals the Companion Mod looks at, in the release-1.2.0.1 shape
//   variants: nil / "ok", "renamedUpdateClickable" (the Click Point class lacks updateClickable),
//             "missingExecute" (the manager lacks onActionEventExecute)
//   Without a call there is no Interactive Control at all (g_modIsLoaded has no entry).
// StubIC.addPoint(name, x, y, z, options) -> Click Point at a world position
//   options: analog (bool), inRange (default true: IC activates it), screenX, screenY (where the stub's
//   mouse test finds it; default 0.5, 0.5), facesAway (bool: like a static outdoor Click Point facing away
//   from the camera, IC tests it with no mouse position, so it is never clickable)
// StubIC.equip(vehicle, state) / StubIC.stateSetting (1 toggle, 2 always on, 3 always off) /
//   StubIC.stateWrites / StubIC.changeCamera(vehicle, cam)  the vehicle side, see below
// StubIC.tick()  one frame of IC's own vehicle update: every Click Point updates (activation, the mouse
//   hit test through updateClickable, hover blink), as IC does each frame
// StubIC.executed  names of the Click Points IC executed through its controller, in order
// StubIC.blinks[name]  hover blinks IC drew on a Click Point     StubIC.mouseX / mouseY  the desk mouse
// StubIC.selected()  name of the Click Point IC currently treats as the clickable one, or nil
inline constexpr const char* kStubInteractiveControl = R"luau(
    StubIC = {}
    StubIC.stateWrites = {}
    function StubIC.install(variant)
        variant = variant or "ok"
        local Point = {}
        Point.__index = Point
        local Manager = {}
        Manager.__index = Manager

        StubIC.points, StubIC.executed, StubIC.blinks = {}, {}, {}
        StubIC.mouseX, StubIC.mouseY = 0.9, 0.9
        StubIC.Point = Point

        function Point:isActivated() return self.activated end
        function Point:setActivated(activated) self.activated = activated end
        function Point:isClickable() return self.clickable end
        function Point:setClickable(clickable) self.clickable = clickable end
        function Point:isExecutable() return self.activated and self.clickable end
        local function hitTest(self, mx, my)
            if mx == nil or my == nil then self:setClickable(false) return end
            local half = self.size * 0.5
            local over = math.abs(mx - self.screenPosX) < half and math.abs(my - self.screenPosY) < half
            if over then StubIC.blinks[self.name] = (StubIC.blinks[self.name] or 0) + 1 end
            self:setClickable(over)
        end
        if variant == "renamedUpdateClickable" then
            Point.updateClickableRenamed = hitTest
        else
            Point.updateClickable = hitTest
        end
        function Point:updateScreenPosition(mx, my)
            if self.facesAway then mx, my = nil, nil end
            (self.updateClickable or self.updateClickableRenamed)(self, mx, my)
        end
        function Point:update(isIndoor, isOutdoor, hasInput)
            self:setActivated(self.inRange)
            if self.activated then
                local mx, my = _G.g_inputBinding:getMousePosition()
                self:updateScreenPosition(mx, my, isIndoor, isOutdoor)
            else
                self:setClickable(false)
            end
        end

        function Manager:getActiveInteractiveController()
            for _, point in ipairs(StubIC.points) do
                if point:isExecutable() then return point.interactiveController end
            end
            return nil
        end
        if variant ~= "missingExecute" then
            function Manager:onActionEventExecute()
                local controller = self:getActiveInteractiveController()
                if controller ~= nil then controller:execute() end
            end
        end

        -- IC's own settings: the state setting (toggle key, always on or always off).
        Manager.SETTING_STATE_TOGGLE, Manager.SETTING_STATE_ALWAYS_ON, Manager.SETTING_STATE_OFF = 1, 2, 3
        StubIC.stateSetting = Manager.SETTING_STATE_TOGGLE
        StubIC.stateWrites = {}
        Manager.settings = {
            getSetting = function(_, name)
                if name == "IC_STATE" then return StubIC.stateSetting end
                return nil
            end,
        }

        _G.g_inputBinding = _G.g_inputBinding or {}
        _G.g_inputBinding.getMousePosition = function() return StubIC.mouseX, StubIC.mouseY end
        _G.g_modIsLoaded = _G.g_modIsLoaded or {}
        _G.g_modIsLoaded["FS25_interactiveControl"] = true
        _G.FS25_interactiveControl = { InteractiveClickPoint = Point, InteractiveControlManager = Manager }
        _G.g_currentMission = _G.g_currentMission or {}
        _G.g_currentMission.interactiveControl = setmetatable({}, Manager)
    end

    function StubIC.addPoint(name, x, y, z, options)
        options = options or {}
        local controller = { analog = options.analog == true, point = nil }
        function controller:isAnalog() return self.analog end
        function controller:execute() table.insert(StubIC.executed, self.point.name) end
        local node = StubScene.newNode(name, 0, x, y, z)
        local point = setmetatable({
            name = name, node = node, activated = false, clickable = false, size = 0.04,
            inRange = options.inRange ~= false, facesAway = options.facesAway == true,
            screenPosX = options.screenX or 0.5, screenPosY = options.screenY or 0.5,
            interactiveController = controller,
        }, StubIC.Point)
        controller.point = point
        table.insert(StubIC.points, point)
        return point
    end

    -- StubIC.equip(vehicle, state) gives a vehicle Interactive Control's per-vehicle state: the synced
    -- on/off state (spec_interactiveControl.state), isInteractiveControlActivated and
    -- activateInteractiveControl(state, noEventSend), which records every call in StubIC.stateWrites
    -- as { state, noEventSend } and, like IC, changes the state only when it differs.
    function StubIC.equip(vehicle, initialState)
        vehicle.spec_interactiveControl = { state = initialState == true, interactiveControllers = { {} } }
        function vehicle:isInteractiveControlActivated() return self.spec_interactiveControl.state end
        function vehicle:activateInteractiveControl(newState, noEventSend)
            table.insert(StubIC.stateWrites, { state = newState, noEventSend = noEventSend })
            self.spec_interactiveControl.state = newState
        end
        return vehicle
    end

    -- StubIC.changeCamera(vehicle, cam): the vehicle's active camera becomes `cam`. Like IC (unless it
    -- keeps itself alive) the vehicle switches itself off when the new camera is an inside camera.
    function StubIC.changeCamera(vehicle, cam)
        vehicle.getActiveCamera = function() return cam end
        if cam.isInside and not StubIC.keepAlive then vehicle:activateInteractiveControl(false) end
    end

    function StubIC.tick()
        for _, point in ipairs(StubIC.points or {}) do point:update(true, false, true) end
    end

    function StubIC.selected()
        for _, point in ipairs(StubIC.points or {}) do
            if point:isExecutable() then return point.name end
        end
        return nil
    end
)luau";
