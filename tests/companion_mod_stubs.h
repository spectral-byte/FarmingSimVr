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
//   kStubInputBinding        a stand-in for g_inputBinding and Binding (StubInput), shaped after the finding of spike #99: the\r\n//                            actions and their bindings per device, addBinding, saveToXMLFile, with variants that lack the API or raise\r\n//   kStubSteeringVehicle     a cab vehicle with a Steering Wheel and the game's analog steering (StubSteer): the
//                            Drivable.setSteeringInput that records every call, a Steering Wheel node and the
//                            per-frame tick that moves the steering toward the input and turns the node (ADR-0027)
//   kStubHandTool            a player on foot who holds Hand Tools (StubHandTool): the game's HandTool, Player and
//                            PlayerTargeter, the pressure washer lance and another mod's tool (which the Companion Mod
//                            does not support), with attachToolToCamera / getLookRay install variants and extension
//                            points for the flashlight, the chainsaw, the spray can and the look ray (ADR-0029)
//   kStubHandToolFlashlight  the flashlight for StubHandTool: the game's HandToolFlashlight class with
//                            onUpdate install variants, StubHandTool.flashlight (light node, lens, the flashlight's own
//                            switch) and setDirection. Run after kStubHandTool and kStubModelLoading.
//   kStubHandToolChainsaw    the chainsaw for StubHandTool: HandToolChainsaw with its update, cut handler and
//                            roll handler, the cut-state machine, the model, camera-rotation and split plane nodes, trees to
//                            target, and the install variants (ADR-0029)
//   kStubHandToolSprayCan    the spray can for StubHandTool: HandToolSprayCan with its update (the rising-edge marker, the
//                            timed spraying state), activateSpraying, marker types and the change action, a targeter that
//                            honours each target type's minimum and maximum distance from the look ray's origin, and the
//                            trees to mark (ADR-0030)
//   kStubHandToolHorseBrush  the horse brush for StubHandTool: HandToolHorseBrush with its update (the animal through the class's
//                            lookup, the input help's text and active state, the sway, the sound, a clean event every half
//                            second), the use handler on the class only, the draw, install variants for the handler and the
//                            lookup, animals with farm access, and an animal target type in the targeter (ADR-0030)
//
// They model only the names and shapes the Companion Mod depends on (ADR-0026, ADR-0027). Nothing here is
// copied from Interactive Control or from the game.
//
// Each constant stays well under the compiler's limit for one string literal.

// ---- Scene graph ---------------------------------------------------------------------------------
// Nodes are plain tables. Euler angles use the engine convention R = Ry * Rx * Rz.
//
//   StubScene.newNode(name, parent, x, y, z, rx, ry, rz) -> node id   (parent 0 = world root)
//   StubScene.worldPosition(node) -> x, y, z      StubScene.worldDirection(node, x, y, z) -> x, y, z
//   The engine's localToWorld(node, x, y, z) and worldToLocal(node, x, y, z) carry a point between a node's space and the world.
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
    _G.localToWorld = function(node, x, y, z)
        local nx, ny, nz, qx, qy, qz, qw = world(node)
        local rx, ry, rz = qrotate(qx, qy, qz, qw, x, y, z)
        return nx + rx, ny + ry, nz + rz
    end
    _G.worldToLocal = function(node, x, y, z)
        local nx, ny, nz, qx, qy, qz, qw = world(node)
        return qrotate(-qx, -qy, -qz, qw, x - nx, y - ny, z - nz)
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
        local parent = StubScene.newNode("cab", rootNode, options.x, options.y, options.z, 0, options.yaw or 0, 0)
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
        local cameraNode = StubScene.newNode("outsideCamera", rootNode, options.x, options.y, options.z, 0, options.yaw or 0, 0)
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
        local root = StubScene.newNode("player", rootNode, options.x, options.y, options.z)
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
// stand-ins for the shipped Gloved Hand files (VirtualHandLeft.i3d, VirtualHandRight.i3d): each a file root whose
// first child is the hand's root node, with the Wrist > Hand > finger bone chains below it, one shape with the real
// mesh's bounds, and the real bone names and (roughly) positions in the files' own units: METRES, like the shipped
// files, which carry their real-world size and their handedness themselves. The right file is the left one mirrored
// across the model's X = 0 plane (positions with x negated, the Euler Y and Z angles of every bone negated), with the
// same node names. The stub is served by the file name asked for: a name containing "VirtualHandRight" is the right
// hand's.
//
//   StubModel.fileExists (default true)   what fileExists() answers for the model files
//   StubModel.missingFiles                set of file names (e.g. { ["VirtualHandRight.i3d"] = true }) fileExists() says
//                                         are not there, whatever fileExists is
//   StubModel.failLoad (default false)    loadI3DFile answers 0 (and a failure reason) although the files exist
//   StubModel.failFiles                   set of file names loadI3DFile fails for
//   StubModel.loads                       { { filename, root } } per loadI3DFile call that reached the "engine"
//   StubModel.clones                      node ids of every clone() of a node (the hands), in order
//   StubModel.deleted                     node id -> true for every deleted node
//   StubModel.hands() -> { left = node, right = node }   the hands' holder nodes (named virtualHand_left and
//                         virtualHand_right by the mod): each holder is placed at its grip pose, so its local space is the
//                         grip pose frame and everything of the hand hangs below it; a missing one is nil
//   StubModel.model(hand) -> node         the cloned model below a hand's holder
//   StubModel.scaledNodes(node) -> list   the names of the nodes among the node's ancestors and descendants whose scale
//                                         is not exactly 1 (none may be: the engine ignores the scale above a skinned mesh)
//   StubModel.setScaleCalls               how many times anything called setScale
//   StubModel.pointInAncestor(node, ancestor, x, y, z) -> x, y, z   a point of a node's local space in an ancestor's
//   StubModel.jointInGripFrame(hand, "index1") -> x, y, z   where a joint is, in the grip pose frame of the hand
//   StubModel.meshBoundsInGripFrame(hand) -> minX, minY, minZ, maxX, maxY, maxZ   the box around the glove mesh there
//   StubModel.isShown(node)               the node and every ancestor are visible (and it exists)
//   StubModel.castsShadow(node) -> true when any shape below casts one
//   StubModel.isLinkedToScene(node)       the node's ancestors lead up to the scene root
//   StubModel.missingJoints               set of node names of the model's file (e.g. { ["Middle.001"] = true }) the
//                                         next loaded model lacks
//   StubModel.joint(hand, name) -> node   the finger joint below a hand node, or nil ("thumb1".."little3": finger
//                                         and curl segment, looked up in that finger's own subtree)
//   StubModel.curl(hand, name) -> radians how far the joint is curled from its rest pose, TOWARD THE PALM, which for
//                                         this model (as for the shipped one) is a NEGATIVE rotation about the joint's own
//                                         X axis; errors when the joint turned about any other axis
// Every joint of the stub model rests in a slightly bent pose that is NOT the identity rotation (each segment a
// different X angle, plus a small Y and Z twist), so a curl that ignored the model's rest pose would show. Like the
// shipped model's, the stub's joint rotations are small bends relative to their parent: a bone's rotation is NOT a
// 90 degree turn of the bone axis (the GIANTS exporter wrote those, and the bones composed to a heap).
// Nodes are visible, unscaled and (for shapes) casting shadows by default, like freshly loaded ones.
inline constexpr const char* kStubModelLoading = R"luau(
    StubModel = { fileExists = true, failLoad = false, missingFiles = {}, failFiles = {}, setScaleCalls = 0,
                  missingJoints = {}, loads = {}, clones = {}, deleted = {} }
    local function baseName(filename) return (tostring(filename):gsub("^.*[/\\]", "")) end
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
    _G.fileExists = function(filename) return StubModel.fileExists and not StubModel.missingFiles[baseName(filename)] end
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
    _G.setScale = function(id, x, y, z)
        StubModel.setScaleCalls = StubModel.setScaleCalls + 1
        if nodes[id] then nodes[id].sx, nodes[id].sy, nodes[id].sz = x, y, z end
    end
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

    -- The shipped LEFT model's geometry, in the glove's original units (about 2.1 units from the wrist to the middle
    -- fingertip) and in the file's own axes: fingers along -Z, the palm facing -Y (the back of the hand +Y), the
    -- thumb on the +X side. The numbers are the model's bone positions and mesh bounds as the Blender file gives
    -- them, in the exporter's Y-up axes. The shipped files are these times 0.0875 m per unit (a real hand is about
    -- 0.19 m long), so buildHand scales them; the right hand is mirrored across X = 0.
    local STUB_METRES_PER_UNIT = 0.0875
    local STUB_WRIST = { 0.0630, 0.0551, 0.9691 }                  -- the Wrist bone
    local STUB_PALM = { 0.0630, 0.0551, 0.5246 }                   -- the Hand bone, the wrist crease of the glove
    local STUB_FINGER_BASE = { 0.0695, 0.0551, -0.1343 }           -- where all five base bones start
    local STUB_KNUCKLE = {                                         -- the first curl joint of each finger
        index = { 0.3777, 0.0551, -0.6210 }, middle = { 0.1051, 0.0551, -0.7313 },
        ring = { -0.1674, 0.0551, -0.6794 }, little = { -0.3816, 0.0551, -0.6307 },
        thumb = { 0.5854, 0.0551, 0.0182 },
    }
    local STUB_MESH_BOUNDS = { min = { -0.6331, -0.2843, -1.6223 }, max = { 1.0737, 0.3675, 1.1760 } }  -- glove and arm stub
    -- the rest flexion about X of the three curl joints of a finger, relative to their parent, in radians (the shipped
    -- index finger: -5.9, -14.8 and -21.2 degrees)
    local STUB_REST_FLEX = { -0.10, -0.26, -0.37 }

    -- the left file (mirror = 1) or the right one (mirror = -1: x negated, the Euler Y and Z angles negated), in metres
    local function buildHand(mirror)
        local k = STUB_METRES_PER_UNIT
        local function at(x, y, z) return mirror * x * k, y * k, z * k end
        local function sub3(a, b) return mirror * (a[1] - b[1]) * k, (a[2] - b[2]) * k, (a[3] - b[3]) * k end
        local fileRoot = StubScene.newNode("virtualHandFile", 0)
        local root = StubScene.newNode("Hand.002", fileRoot)  -- the file's first child, the hand's root node
        local mesh = StubScene.newNode("HandModel.001", root)
        nodes[mesh].isShape = true
        nodes[mesh].castShadow = true
        local lo, hi = STUB_MESH_BOUNDS.min, STUB_MESH_BOUNDS.max
        local x0, x1 = mirror * lo[1] * k, mirror * hi[1] * k
        nodes[mesh].bounds = { min = { math.min(x0, x1), lo[2] * k, lo[3] * k }, max = { math.max(x0, x1), hi[2] * k, hi[3] * k } }
        local wrist = StubScene.newNode("Wrist", root, at(STUB_WRIST[1], STUB_WRIST[2], STUB_WRIST[3]))
        local palm = StubScene.newNode("Hand", wrist, sub3(STUB_PALM, STUB_WRIST))
        for _, finger in ipairs(STUB_FINGER_ORDER) do
            local parent = palm  -- a base bone the model lacks leaves its joints hanging off the palm
            if not StubModel.missingJoints[STUB_FINGERS[finger][1]] then
                parent = StubScene.newNode(STUB_FINGERS[finger][1], palm, sub3(STUB_FINGER_BASE, STUB_PALM))
            end
            for index = 1, 3 do
                local name = STUB_FINGERS[finger][index + 1]
                if not StubModel.missingJoints[name] then
                    -- the model's relaxed pose, like the shipped one's: each joint flexed a little relative to its
                    -- parent (a NEGATIVE turn about X is toward the palm in this model; the shipped file has -3 to
                    -- -9, -15 to -21 and -14 to -21 degrees for the three segments), with a small twist. The
                    -- bones run along their local -Z, as the shipped ones do. The first joint sits at the finger's knuckle.
                    local x, y, z = 0, 0, 0
                    if index == 1 then x, y, z = sub3(STUB_KNUCKLE[finger], STUB_FINGER_BASE) end
                    parent = StubScene.newNode(name, parent, x, y, z, STUB_REST_FLEX[index], 0.1 * mirror, -0.05 * mirror)
                    local n = nodes[parent]
                    n.restQx, n.restQy, n.restQz, n.restQw = n.qx, n.qy, n.qz, n.qw
                end
            end
        end
        return fileRoot
    end
    _G.loadI3DFile = function(filename, addPhysics, callOnCreate)
        if not StubModel.fileExists or StubModel.failLoad or StubModel.failFiles[baseName(filename)] then return 0, 1 end
        local root = buildHand(tostring(filename):find("VirtualHandRight", 1, true) and -1 or 1)
        table.insert(StubModel.loads, { filename = filename, root = root, addPhysics = addPhysics, callOnCreate = callOnCreate })
        return root, 0
    end
    local function copyTree(id, parent)
        local source = nodes[id]
        local copy = StubScene.newNode(source.name, parent, source.x, source.y, source.z)
        local n = nodes[copy]
        n.qx, n.qy, n.qz, n.qw = source.qx, source.qy, source.qz, source.qw
        n.restQx, n.restQy, n.restQz, n.restQw = source.restQx, source.restQy, source.restQz, source.restQw
        n.isShape, n.castShadow, n.visible, n.bounds = source.isShape, source.castShadow, source.visible, source.bounds
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
    -- the node of a clone's ancestors that hangs off the scene root: the hand's holder (the grip pose frame)
    -- the hand's holder above a node (the first ancestor the mod named virtualHand_*), wherever the holder itself hangs
    local function holderOf(id)
        local node = id
        while nodes[node] ~= nil do
            local name = nodes[node].name
            if name == "virtualHand_left" or name == "virtualHand_right" then return node end
            node = nodes[node].parent
        end
        return id
    end
    -- The Virtual Hands, told apart by their holder's name (the mod names them virtualHand_left / virtualHand_right).
    -- A hand is its holder: that node is placed at the grip pose, and everything of the hand hangs below it.
    function StubModel.hands()
        local found = {}
        for _, id in ipairs(StubModel.clones) do
            if nodes[id] ~= nil then
                local holder = holderOf(id)
                local name = nodes[holder].name
                if name == "virtualHand_left" then found.left = holder end
                if name == "virtualHand_right" then found.right = holder end
            end
        end
        return found
    end
    -- the cloned model of a hand (its Hand.002 root node), wherever the mod hung it below the holder
    function StubModel.model(hand)
        for _, id in ipairs(StubModel.clones) do
            if nodes[id] ~= nil and holderOf(id) == hand then return id end
        end
        return nil
    end
    -- the names of the nodes among a node's ancestors, itself and descendants with a scale other than exactly 1
    function StubModel.scaledNodes(id)
        local names = {}
        local function check(node)
            local sx, sy, sz = getScale(node)
            if sx ~= 1 or sy ~= 1 or sz ~= 1 then table.insert(names, nodes[node].name) end
        end
        local function down(node)
            check(node)
            for _, childId in ipairs(childrenOf(node)) do down(childId) end
        end
        local up = nodes[id] and nodes[id].parent
        while up ~= nil and up ~= 0 and nodes[up] ~= nil do check(up); up = nodes[up].parent end
        down(id)
        return names
    end
    -- a point given in the local space of a node, in the local space of one of its ancestors (scale, then
    -- rotation, then translation, node by node)
    function StubModel.pointInAncestor(id, ancestor, x, y, z)
        while id ~= ancestor do
            local n = nodes[id]
            assert(n ~= nil and id ~= 0, "the second node is not an ancestor of the first")
            x, y, z = qrotate(n.qx, n.qy, n.qz, n.qw, x * (n.sx or 1), y * (n.sy or 1), z * (n.sz or 1))
            x, y, z = x + n.x, y + n.y, z + n.z
            id = n.parent
        end
        return x, y, z
    end
    -- where a finger joint (see StubModel.joint) is, in the local space of the hand's holder: the grip pose frame
    function StubModel.jointInGripFrame(hand, joint)
        local id = StubModel.joint(hand, joint)
        assert(id ~= nil, joint .. " is not in the model")
        return StubModel.pointInAncestor(id, hand, 0, 0, 0)
    end
    -- where a bone of the model (by the node's own name in the file, e.g. "Hand" for the wrist crease) is, in the grip frame
    function StubModel.boneInGripFrame(hand, nodeName)
        local id = findByName(hand, nodeName)
        assert(id ~= nil, nodeName .. " is not in the model")
        return StubModel.pointInAncestor(id, hand, 0, 0, 0)
    end
    -- the box around the glove mesh, in the grip pose frame: min x, y, z then max x, y, z (the box of its eight corners)
    function StubModel.meshBoundsInGripFrame(hand)
        local function findMesh(id)
            if nodes[id].bounds then return id end
            for _, childId in ipairs(childrenOf(id)) do
                local found = findMesh(childId)
                if found then return found end
            end
        end
        local mesh = findMesh(hand)
        assert(mesh ~= nil, "the hand has a mesh")
        local b = nodes[mesh].bounds
        local lo, hi = { math.huge, math.huge, math.huge }, { -math.huge, -math.huge, -math.huge }
        for _, x in ipairs({ b.min[1], b.max[1] }) do
            for _, y in ipairs({ b.min[2], b.max[2] }) do
                for _, z in ipairs({ b.min[3], b.max[3] }) do
                    local gx, gy, gz = StubModel.pointInAncestor(mesh, hand, x, y, z)
                    for i, v in ipairs({ gx, gy, gz }) do
                        lo[i], hi[i] = math.min(lo[i], v), math.max(hi[i], v)
                    end
                end
            end
        end
        return lo[1], lo[2], lo[3], hi[1], hi[2], hi[3]
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

// ---- A vehicle with a Steering Wheel ----------------------------------------------------------------------------
// Run after kStubSceneGraph. The vehicle side of Wheel Grab (ADR-0027): what the game does with a steering input, as
// the real Drivable and Wheels code does it (Drivable.lua setSteeringInput / onUpdate / updateSteeringWheel,
// Wheels.lua updateVehiclePhysics), so a test sees what the player would: the calls that reached the vehicle and how
// the Steering Wheel turned.
//
// StubSteer.install(variant)  defines the globals the game defines: InputDevice.CATEGORY and the Drivable class whose
//   setSteeringInput records every call that reaches a vehicle in StubSteer.calls as { value, analog, category }
//   and then stores the input like the game (the analog flag and category only change on a non-zero input).
//   variant: nil ("ok"), "missing" (a game update renamed it: the class has setSteeringAxis, same arguments, and no
//   setSteeringInput) or "reshaped" (setSteeringInput(vehicle, { value, isAnalog, category }) takes one table).
//   or "raises" (the "ok" function, but a call in the Wheel Peripheral's category -- the one Wheel Grab makes -- raises
//   an error and records nothing; the other categories work). StubSteer.failNextWheelCalls(n) makes only the next n
//   such calls raise, in any variant.
//   Run it BEFORE loading the Companion Mod to have Drivable there at file load, like the game; StubSteer.original is
//   the unwrapped function of the "ok" and "raises" variants.
// StubSteer.keyboard / gamepad / wheelPeripheral (vehicle, value)  another device steering: the call it makes (by the
//   variant's entry point); StubSteer.send(vehicle, value, isAnalog, category) is the general form.
// StubSteer.equip(vehicle, options) -> vehicle   makes a StubScene.cab vehicle steerable. Like the game it COPIES
//   Drivable.setSteeringInput onto the vehicle instance, so wrapping only the class afterwards would not reach it.
//   options:
//     parent            the node the Steering Wheel hangs in (the cab's parent from StubScene.cab); default the world root
//     position          { x, y, z } of the Steering Wheel's centre in that node (default 0, 1.2, 0.5: in front of
//                       and below the head of StubScene.cab)
//     tilt              radians the wheel's base leans about X (default -0.6: the axis points up and toward the driver).
//                       The tilt is on a node ABOVE the wheel: the game sets the wheel node's own rotation to (0, angle, 0)
//     rotationDeg       the in-cab rotation at full lock, in degrees (default 540: vanilla); outdoorRotation is half
//     maxRotTime        (default 1) and minRotTime (default -maxRotTime): the vehicle's steering times
//     steeringDuration  seconds of the vehicle's wheelSteeringDuration (default 1)
//     rotatedTime       where the steering starts (default 0, straight ahead)
//     noWheel           true: the vehicle has no Steering Wheel node
//     powered           false: the motor is off, the vehicle does not steer
//     controlAllowed    false: getIsPlayerVehicleControlAllowed says no (an attached tool blocks the steering)
//     turnedSeat        true: the driver's seat is turned to drive backwards. Like ReverseDriving it flips BOTH the direction
//                       the game turns the Steering Wheel node in (-1) and getSteeringDirection() (-1)
//     turnedSeatWheel   with turnedSeat: the vehicle has a Steering Wheel of its own for the turned seat (another node,
//                       spec_reverseDriving.steeringWheel), which the game turns instead of the drivable's
//     helper            true: a hired helper is driving (getIsAIActive says yes), as in StubSteer.setHelper
//     copyOfOriginal    true: the vehicle's copy of setSteeringInput is the unwrapped StubSteer.original, as if it was
//                       copied before the Companion Mod wrapped the class
// StubSteer.tick(dtMs)  one frame of the game's vehicle update for every equipped vehicle: the steering input of the
//   last frame moves the steering value toward it (analog input at 2.5 steering durations per full range, keyboard
//   input at 1), the input is cleared (so it must be sent again every frame), the wheels' rotated time follows, and the
//   Steering Wheel node is set to (0, angle, 0). With no input the value goes back to centre at the rate of the last
//   non-zero input, which stays "analog" after a Wheel Peripheral or Wheel Grab (the game's analog steering back).
// StubSteer.calls                every setSteeringInput that reached a vehicle, in order
// StubSteer.setHelper(vehicle, driving)  a hired helper starts (true) or stops driving the vehicle
// StubSteer.changeCamera(vehicle, cam)  the vehicle's active camera becomes cam (e.g. { isInside = false }, an Outside
//   Camera, or { isInside = true, isPassengerCamera = true })
// StubSteer.wheelAngle(vehicle)  the node's angle in radians, unwrapped (rotatedTime * in-cab rotation), 0 without a wheel
// StubSteer.wheelNode(vehicle)   the Steering Wheel node the game turns now; StubSteer.wheelBase(vehicle) the node above it
// StubSteer.roadWheelAngle(vehicle)  rotatedTime: how far the vehicle's wheels on the ground are turned (positive and
//   negative are the two ways the vehicle steers)
// StubSteer.rimPoint(vehicle, radius, angle, lift) -> x, y, z   the world position of the point on the wheel at
//   `radius` from its axis and `lift` along it (default 0) when the wheel is turned to `angle` radians
inline constexpr const char* kStubSteeringVehicle = R"luau(
    StubSteer = {}
    StubSteer.calls = {}
    local equipped = {}

    function StubSteer.install(variant)
        _G.InputDevice = { CATEGORY = { UNKNOWN = 0, KEYBOARD_MOUSE = 1, GAMEPAD = 2, WHEEL = 3 } }
        StubSteer.calls = {}
        StubSteer.variant = variant or "ok"
        StubSteer.original = nil
        equipped = {}
        _G.Drivable = {}
        StubSteer.wheelCallFailures = StubSteer.variant == "raises" and math.huge or 0
        local function steer(self, inputValue, isAnalog, deviceCategory)
            if deviceCategory == InputDevice.CATEGORY.WHEEL and StubSteer.wheelCallFailures > 0 then
                StubSteer.wheelCallFailures = StubSteer.wheelCallFailures - 1
                error("setSteeringInput: the Wheel Peripheral branch is broken")  -- before the call is recorded
            end
            table.insert(StubSteer.calls, { value = inputValue, analog = isAnalog, category = deviceCategory })
            local last = self.spec_drivable.lastInputValues
            last.axisSteer = inputValue
            if inputValue ~= 0 then
                last.axisSteerIsAnalog = isAnalog
                last.axisSteerDeviceCategory = deviceCategory
            end
        end
        if StubSteer.variant == "missing" then
            Drivable.setSteeringAxis = steer
        elseif StubSteer.variant == "reshaped" then
            function Drivable.setSteeringInput(self, input) steer(self, input.value, input.isAnalog, input.category) end
        else
            Drivable.setSteeringInput = steer
            StubSteer.original = steer
        end
    end

    function StubSteer.failNextWheelCalls(count) StubSteer.wheelCallFailures = count end

    -- Another device steering: the call the keyboard, a gamepad or a Wheel Peripheral makes, through whatever the
    -- installed game variant calls its steering function.
    function StubSteer.send(vehicle, value, isAnalog, category)
        if StubSteer.variant == "missing" then
            vehicle:setSteeringAxis(value, isAnalog, category)
        elseif StubSteer.variant == "reshaped" then
            vehicle:setSteeringInput({ value = value, isAnalog = isAnalog, category = category })
        else
            vehicle:setSteeringInput(value, isAnalog, category)
        end
    end
    function StubSteer.keyboard(vehicle, value) StubSteer.send(vehicle, value, false, InputDevice.CATEGORY.KEYBOARD_MOUSE) end
    function StubSteer.gamepad(vehicle, value) StubSteer.send(vehicle, value, true, InputDevice.CATEGORY.GAMEPAD) end
    function StubSteer.wheelPeripheral(vehicle, value) StubSteer.send(vehicle, value, true, InputDevice.CATEGORY.WHEEL) end

    function StubSteer.equip(vehicle, options)
        options = options or {}
        local position = options.position or { x = 0, y = 1.2, z = 0.5 }
        local rotation = math.rad(options.rotationDeg or 540)
        vehicle.maxRotTime = options.maxRotTime or 1
        vehicle.minRotTime = options.minRotTime or -vehicle.maxRotTime
        vehicle.wheelSteeringDuration = options.steeringDuration or 1
        vehicle.rotatedTime = options.rotatedTime or 0
        vehicle.stubPowered = options.powered ~= false
        vehicle.stubControlAllowed = options.controlAllowed ~= false
        local spec = { axisSide = 0, lastInputValues = { axisSteer = 0, axisSteerIsAnalog = false } }
        if vehicle.rotatedTime >= 0 then
            spec.axisSide = vehicle.maxRotTime ~= 0 and -vehicle.rotatedTime / vehicle.maxRotTime or 0
        else
            spec.axisSide = vehicle.minRotTime ~= 0 and vehicle.rotatedTime / vehicle.minRotTime or 0
        end
        local function newWheel()
            local base = StubScene.newNode("steeringWheelBase", options.parent or 0, position.x, position.y, position.z,
                options.tilt or -0.6, 0, 0)
            local node = StubScene.newNode("steeringWheel", base, 0, 0, 0)
            return { node = node, base = base, indoorRotation = rotation, outdoorRotation = rotation / 2 }
        end
        if not options.noWheel then
            spec.steeringWheel = newWheel()
        end
        vehicle.spec_drivable = spec
        if options.turnedSeat then
            vehicle.spec_reverseDriving = { isReverseDriving = true, steeringWheel = options.turnedSeatWheel and newWheel() or nil }
        end
        vehicle.stubHelper = options.helper == true
        vehicle.setSteeringInput = Drivable.setSteeringInput  -- Vehicle:load copies the class functions onto the instance
        vehicle.setSteeringAxis = Drivable.setSteeringAxis    -- (the "missing" variant renames it)
        if options.copyOfOriginal then
            vehicle.setSteeringInput = StubSteer.original      -- a copy taken before anything wrapped the class
        end
        function vehicle:getSteeringDirection()
            local reverse = self.spec_reverseDriving  -- ReverseDriving:getSteeringDirection (settled: the seat is not turning)
            return (reverse ~= nil and reverse.isReverseDriving) and -1 or 1
        end
        function vehicle:getIsAIActive() return self.stubHelper end
        function vehicle:getIsEntered() return true end
        function vehicle:getIsPowered() return self.stubPowered end
        function vehicle:getIsPlayerVehicleControlAllowed() return self.stubControlAllowed end
        table.insert(equipped, vehicle)
        StubSteer.step(vehicle, 0)  -- puts the Steering Wheel node at its starting angle
        return vehicle
    end

    -- The wheel the game turns now, and the direction it turns the node in (ReverseDriving:updateSteeringWheel).
    local function activeWheel(vehicle)
        local wheel = vehicle.spec_drivable.steeringWheel
        local reverse = vehicle.spec_reverseDriving
        if reverse ~= nil and reverse.isReverseDriving then
            return reverse.steeringWheel or wheel, -1
        end
        return wheel, 1
    end

    function StubSteer.wheelNode(vehicle)
        local wheel = activeWheel(vehicle)
        return wheel and wheel.node or nil
    end
    function StubSteer.wheelBase(vehicle)
        local wheel = activeWheel(vehicle)
        return wheel and wheel.base or nil
    end
    function StubSteer.setHelper(vehicle, driving) vehicle.stubHelper = driving == true end
    function StubSteer.changeCamera(vehicle, cam) vehicle.getActiveCamera = function() return cam end end
    function StubSteer.wheelAngle(vehicle) return vehicle.stubWheelAngle or 0 end
    function StubSteer.roadWheelAngle(vehicle) return vehicle.rotatedTime end

    function StubSteer.rimPoint(vehicle, radius, angle, lift)
        -- the wheel node is turned about its Y axis: a point at (radius, lift, 0) goes to (r cos a, lift, -r sin a)
        return localToWorld(StubSteer.wheelBase(vehicle), radius * math.cos(angle), lift or 0, -radius * math.sin(angle))
    end

    -- One frame of the vehicle update for one vehicle (dt in ms).
    function StubSteer.step(vehicle, dt)
        local spec = vehicle.spec_drivable
        local last = spec.lastInputValues
        if vehicle:getIsPowered() then
            local speedFactor = last.axisSteerIsAnalog and 2.5 or 1
            local delta = dt / (vehicle.wheelSteeringDuration * 1000) * speedFactor
            if last.axisSteer > spec.axisSide then
                spec.axisSide = math.min(last.axisSteer, spec.axisSide + delta)
            elseif last.axisSteer < spec.axisSide then
                spec.axisSide = math.max(last.axisSteer, spec.axisSide - delta)
            end
        end
        last.axisSteer = 0
        -- Wheels:updateVehiclePhysics
        local axisSide = vehicle:getSteeringDirection() * spec.axisSide
        if axisSide < 0 then
            vehicle.rotatedTime = math.min(-vehicle.maxRotTime * axisSide, vehicle.maxRotTime)
        else
            vehicle.rotatedTime = math.max(vehicle.minRotTime * axisSide, vehicle.minRotTime)
        end
        -- Drivable:updateSteeringWheel: the in-cab rotation in an inside camera that is not a passenger's
        local wheel, direction = activeWheel(vehicle)
        if wheel ~= nil then
            local camera = vehicle:getActiveCamera()
            local maxRotation = wheel.outdoorRotation
            if camera ~= nil and camera.isInside and not camera.isPassengerCamera then maxRotation = wheel.indoorRotation end
            vehicle.stubWheelAngle = vehicle.rotatedTime * maxRotation * direction
            setRotation(wheel.node, 0, vehicle.stubWheelAngle, 0)
        end
    end

    function StubSteer.tick(dt)
        for _, vehicle in ipairs(equipped) do StubSteer.step(vehicle, dt) end
    end
)luau";

// ---- The game's input binding -------------------------------------------------------------------------
// A stand-in for g_inputBinding and Binding, shaped after the finding of spike #99 (spec #98): the game's InputBinding Lua is
// stripped in the readable source, so this models only what the spike saw it do.
//   - g_inputBinding:getActionByName(name) -> action | nil. action.bindings is an array of Binding objects (deviceId,
//     inputString, axisComponent, neutralInput, index, id), those of devices that are not plugged in included. The methods
//     are on the metatable, so pairs(g_inputBinding) does not list them.
//   - Binding.new(deviceId, { inputs }, axisComponent, negative) as the game measured it: the sign of an axis input comes only
//     from `negative` (any non-nil value), a "-" inside the name is dropped ({"AXIS_3-"} becomes "AXIS_3+"), and the
//     argument after the list is the axisComponent. Then the fields set by hand and b:makeId(); g_inputBinding:addBinding(action, b)
//     is true when added, false (after the game's "occupied slot" log line) when the device already has a binding of that
//     action at b.index.
//   - g_inputBinding:getDeviceById(deviceId) is the device table while the pad is connected and nil for a pad that is only saved (an unplugged
//     one); g_inputBinding:getMissingDeviceById(deviceId) is the other way round (the spike's finding).
//   - g_inputBinding:deleteBinding(...) says true and removes nothing. Taking a binding out of action.bindings does remove it,
//     but not from action.activeBindings, which is what the game acts on (the Controls screen shows .bindings): a removed
//     binding keeps acting until g_inputBinding:resetActiveActionBindings() rebuilds activeBindings from bindings.
//     addBinding adds to .bindings only.
//   - g_inputBinding:saveToXMLFile() takes no path; StubInput.saves counts the calls and StubInput.saved is the listing at the
//     last one.
//
//   StubInput.install(options)
//     options.devices  the pads the game has applied GAMEPAD_DEFAULT to, as device ids (default 0_ and 3_XINPUT_GAMEPAD); a
//                      pad that is not plugged in counts. Each gets, at index slot + 1 as in the saved file, the Companion Mod's
//                      GAMEPAD_DEFAULT bindings (VR_RECENTER_VIEW BUTTON_11, VR_HUD_TOGGLE BUTTON_10, VR_SNAP_TURN_LEFT BUTTON_5,
//                      VR_SNAP_TURN_RIGHT BUTTON_6, VR_TOGGLE_VIEW BUTTON_9) and the game's own (smooth look on the right
//                      stick X on foot and in vehicles, JUMP and look up / down), next to the keyboard's.
//     options.unplugged  a list of device ids in devices whose pad is saved but not connected: getDeviceById says nil for them
//                      until StubInput.plug(deviceId)
//     options.variant  "ok" (default); "missing": no g_inputBinding; "reshaped": no addBinding; "raises": addBinding raises
//                      on its second call, after the Companion Mod has taken bindings out; "saveRaises": saveToXMLFile raises;
//                      "noReset": no resetActiveActionBindings; "resetRaises": resetActiveActionBindings raises
//     options.omit     a list of "ACTION@device" default bindings left out
//     options.extra    a list of { action, device, input, index } bindings added to the defaults
//     options.state    a string from StubInput.serialize(): the bindings of an earlier game session, in place of the defaults
//   StubInput.addDevice(deviceId)      the game lists one more pad (connected) and applies GAMEPAD_DEFAULT to it
//   StubInput.plug(deviceId)           the game attaches a listed pad as a connected device
//   StubInput.list(action, deviceId) -> string   the device's bindings of the action as "input/axisComponent@index", sorted,
//                      separated by ' ; ' ("" when there are none)
//   StubInput.listActive(action, deviceId) / StubInput.dumpActive(deviceId)   the same for action.activeBindings
//   StubInput.get(action, deviceId) -> Binding   the device's first binding of the action
//   StubInput.dump(deviceId) -> string   all of the device's bindings as "ACTION: list" lines (every device when nil)
//   StubInput.add(action, deviceId, input, index)   one more binding on the device (in bindings and activeBindings)
//   StubInput.change(action, deviceId, from, to)   the player changes a binding in the Controls screen: its input becomes `to`
//   StubInput.serialize() -> string    every binding, for a later game session
//   StubInput.saves, StubInput.saved   see above
inline constexpr const char* kStubInputBinding = R"luau(
    StubInput = {}
    local actions = {}
    local actionOrder = {}
    local variant = "ok"
    local addCalls = 0
    local connected = {}

    local Binding = {}
    Binding.__index = Binding
    -- The game's Binding.new(deviceId, inputNames, axisComponent, negative), as measured in the game: the second argument after
    -- the list is the axisComponent, and ANY non-nil fourth argument makes the input negative. The sign of an axis input
    -- comes from that flag alone: a "-" (or "+") given inside the name is dropped, so {"AXIS_3-"} comes out "AXIS_3+".
    -- inputComponent is "-" for a negative input, otherwise "+".
    function Binding.new(deviceId, inputs, axisComponent, negative)
        local b = setmetatable({}, Binding)
        local names = {}
        for i, name in ipairs(inputs) do
            if string.sub(name, 1, 5) == "AXIS_" then name = (string.gsub(name, "[%+%-]$", "")) end
            names[i] = name
        end
        local isNegative = negative ~= nil
        local last = names[#names]
        if last ~= nil and string.sub(last, 1, 5) == "AXIS_" then
            names[#names] = last .. (isNegative and "-" or "+")
        end
        b.deviceId = deviceId
        b.inputString = table.concat(names, " ")
        b.inputComponent = isNegative and "-" or "+"
        b.axisComponent = axisComponent or "+"
        b.neutralInput = 0
        b.index = 1
        return b
    end
    function Binding:makeId()
        self.id = table.concat({ self.deviceId, self.inputString, self.axisComponent, tostring(self.neutralInput), tostring(self.index) }, "|")
    end

    local function actionNamed(name)
        local action = actions[name]
        if action == nil then
            action = { name = name, bindings = {}, activeBindings = {} }
            actions[name] = action
            table.insert(actionOrder, name)
        end
        return action
    end

    local function split(text, separator)
        local parts = {}
        for part in string.gmatch(text, "[^" .. separator .. "]+") do table.insert(parts, part) end
        return parts
    end

    -- The game loading or applying a binding: it is in action.bindings and in action.activeBindings (what the game acts on).
    -- A trailing "-" on an axis input is the binding's negative flag.
    local function put(name, deviceId, input, axisComponent, index)
        local negative = nil
        if string.match(input, "AXIS_[%w_]*%-$") then negative = true end
        local b = Binding.new(deviceId, split(input, " "), axisComponent or "+", negative)
        b.index = index
        b:makeId()
        local action = actionNamed(name)
        table.insert(action.bindings, b)
        table.insert(action.activeBindings, b)
    end

    local methods = {}
    methods.__index = methods
    function methods:getActionByName(name) return actions[name] end
    local function addBindingImpl(self, action, b)
        addCalls = addCalls + 1
        if variant == "raises" and addCalls >= 2 then error("InputBinding:addBinding: attempt to index nil") end
        for _, existing in ipairs(action.bindings) do
            if existing.deviceId == b.deviceId and existing.index == b.index then
                print("Warning: InputBinding:addBinding: occupied slot " .. tostring(b.index) .. " of " .. action.name)
                return false
            end
        end
        table.insert(action.bindings, b)
        return true
    end
    methods.addBinding = addBindingImpl
    function methods:getDeviceById(deviceId) if connected[deviceId] then return { id = deviceId } end return nil end
    function methods:getMissingDeviceById(deviceId) if not connected[deviceId] then return { id = deviceId } end return nil end
    function methods:deleteBinding(...) return true end
    -- Rebuilds every action's activeBindings (what the game acts on) from its bindings (what the Controls screen shows).
    local function resetActiveImpl(self)
        if variant == "resetRaises" then error("InputBinding:resetActiveActionBindings: attempt to index nil") end
        for _, name in ipairs(actionOrder) do
            local action = actions[name]
            action.activeBindings = {}
            for i, b in ipairs(action.bindings) do action.activeBindings[i] = b end
        end
    end
    methods.resetActiveActionBindings = resetActiveImpl
    function methods:saveToXMLFile()
        if variant == "saveRaises" then error("InputBinding:saveToXMLFile: attempt to call a nil value") end
        StubInput.saves = StubInput.saves + 1
        StubInput.saved = StubInput.dump(nil)
        return true
    end

    local function slotIndex(deviceId) return (tonumber(string.match(deviceId, "^(%d+)_")) or 0) + 1 end

    local function omitted(options, name, deviceId)
        for _, key in ipairs(options.omit or {}) do
            if key == name .. "@" .. deviceId then return true end
        end
        return false
    end

    local pendingOptions = {}
    local function isUnplugged(deviceId)
        for _, id in ipairs(pendingOptions.unplugged or {}) do
            if id == deviceId then return true end
        end
        return false
    end

    function StubInput.plug(deviceId) connected[deviceId] = true end

    function StubInput.addDevice(deviceId)
        connected[deviceId] = not isUnplugged(deviceId)
        local index = slotIndex(deviceId)
        local function pad(name, input, axisComponent)
            if not omitted(pendingOptions, name, deviceId) then put(name, deviceId, input, axisComponent, index) end
        end
        pad("JUMP", "BUTTON_2")
        pad("VR_RECENTER_VIEW", "BUTTON_11")
        pad("VR_HUD_TOGGLE", "BUTTON_10")
        pad("VR_SNAP_TURN_LEFT", "BUTTON_5")
        pad("VR_SNAP_TURN_RIGHT", "BUTTON_6")
        pad("VR_TOGGLE_VIEW", "BUTTON_9")
        pad("AXIS_LOOK_LEFTRIGHT_PLAYER", "AXIS_3+", "+")
        pad("AXIS_LOOK_LEFTRIGHT_PLAYER", "AXIS_3-", "-")
        pad("AXIS_LOOK_LEFTRIGHT_VEHICLE", "AXIS_3+", "+")
        pad("AXIS_LOOK_LEFTRIGHT_VEHICLE", "AXIS_3-", "-")
        pad("AXIS_LOOK_UPDOWN_PLAYER", "AXIS_4+", "+")
        pad("AXIS_LOOK_UPDOWN_PLAYER", "AXIS_4-", "-")
    end

    function StubInput.install(options)
        options = options or {}
        pendingOptions = options
        variant = options.variant or "ok"
        actions, actionOrder, addCalls, connected = {}, {}, 0, {}
        StubInput.saves, StubInput.saved = 0, nil
        _G.g_inputBinding = nil
        if variant == "missing" then return end

        _G.Binding = Binding
        local inputBinding = setmetatable({}, methods)
        methods.addBinding = (variant ~= "reshaped") and addBindingImpl or nil
        methods.resetActiveActionBindings = (variant ~= "noReset") and resetActiveImpl or nil
        _G.g_inputBinding = inputBinding

        if options.state ~= nil then
            for _, line in ipairs(split(options.state, "\n")) do
                local f = split(line, "|")
                put(f[1], f[2], f[3], f[4], tonumber(f[5]))
                if not isUnplugged(f[2]) then connected[f[2]] = true end
            end
        else
            for _, name in ipairs({ "VR_RECENTER_VIEW", "VR_HUD_TOGGLE", "VR_SNAP_TURN_LEFT", "VR_SNAP_TURN_RIGHT", "VR_TOGGLE_VIEW" }) do
                put(name, "KB_MOUSE_DEFAULT", "KEY_numPad5", "+", 1)
            end
            -- The Seat Set Action: a keyboard default and, as in modDesc.xml, no gamepad default
            put("VR_SET_SEAT", "KB_MOUSE_DEFAULT", "KEY_rctrl KEY_numPad5", "+", 1)
            for _, deviceId in ipairs(options.devices or { "0_XINPUT_GAMEPAD", "3_XINPUT_GAMEPAD" }) do
                StubInput.addDevice(deviceId)
            end
            for _, extra in ipairs(options.extra or {}) do
                put(extra.action, extra.device, extra.input, extra.axisComponent, extra.index)
            end
        end
    end

    local function describe(b) return b.inputString .. "/" .. b.axisComponent .. "@" .. tostring(b.index) end

    local function listField(field, name, deviceId)
        local action = actions[name]
        local parts = {}
        for _, b in ipairs(action and action[field] or {}) do
            if b.deviceId == deviceId then table.insert(parts, describe(b)) end
        end
        table.sort(parts)
        return table.concat(parts, " ; ")
    end
    function StubInput.list(name, deviceId) return listField("bindings", name, deviceId) end
    -- the same for action.activeBindings: what the game acts on
    function StubInput.listActive(name, deviceId) return listField("activeBindings", name, deviceId) end

    local function dumpField(field, deviceId)
        local lines = {}
        for _, name in ipairs(actionOrder) do
            local devices = {}
            for _, b in ipairs(actions[name][field]) do
                if deviceId == nil or b.deviceId == deviceId then devices[b.deviceId] = true end
            end
            local ids = {}
            for id in pairs(devices) do table.insert(ids, id) end
            table.sort(ids)
            for _, id in ipairs(ids) do table.insert(lines, name .. " " .. id .. ": " .. listField(field, name, id)) end
        end
        return table.concat(lines, "\n")
    end
    function StubInput.dump(deviceId) return dumpField("bindings", deviceId) end
    function StubInput.dumpActive(deviceId) return dumpField("activeBindings", deviceId) end

    -- the device's first binding of the action (the Binding object), for the fields list() does not show
    function StubInput.get(name, deviceId)
        for _, b in ipairs(actions[name].bindings) do
            if b.deviceId == deviceId then return b end
        end
        return nil
    end

    -- the player (or a pad profile an earlier build left behind) has one more binding on the device
    function StubInput.add(name, deviceId, input, index) put(name, deviceId, input, "+", index) end

    function StubInput.change(name, deviceId, from, to)
        for _, b in ipairs(actions[name].bindings) do
            if b.deviceId == deviceId and b.inputString == from then
                b.inputString = to
                b:makeId()
                return
            end
        end
        error("no such binding: " .. name .. " " .. deviceId .. " " .. from)
    end

    function StubInput.serialize()
        local lines = {}
        for _, name in ipairs(actionOrder) do
            for _, b in ipairs(actions[name].bindings) do
                table.insert(lines, table.concat({ name, b.deviceId, b.inputString, b.axisComponent, tostring(b.index) }, "|"))
            end
        end
        return table.concat(lines, "\n")
    end

    StubInput.saves, StubInput.saved = 0, nil
)luau";

// ---- Hand Tools (ADR-0029) ---------------------------------------------------------------------------------
// A player on foot who can hold a Hand Tool, shaped after the game's HandTool, Player and PlayerTargeter. Run after
// kStubSceneGraph (and after kStubModelLoading when the Virtual Hands are needed: a hand tool goes into a Virtual Hand).
// Run StubHandTool.install BEFORE loading the Companion Mod to have HandTool and Player there at file load, like the game.
//
// StubHandTool.install(variant, options)   defines the globals the game defines: HandTool, Player and PlayerTargeter.
//   variant   how HandTool.attachToolToCamera is in this "game version": nil / "ok", "missing" (a game update renamed it
//             to attachToolToCameraNode: the class has no attachToolToCamera, the game attaches through the new name) or
//             "reshaped" (it takes no parameters at all, and attaches the tool being held)
//   options   getLookRay  "ok" (default), "missing" (Player has lookRay instead) or "reshaped" (Player.getLookRay takes no
//             parameters): for the look ray wrap. The targeter reads Player.lookRay in the "missing" game.
//             StubHandTool.originalGetLookRay is the game's own function ("ok" game only: nil in the others).
// StubHandTool.onFoot(options) -> { player, cam, parent, yawNode, firstPersonCamera }   StubScene.onFoot(options) with a
//   Player: g_currentMission.player and g_localPlayer are it. player.camera is the camera of StubScene.onFoot (isFirstPerson,
//   pitchNode: the node the game links a held tool to). Player:getLookRay() -> x, y, z, dirX, dirY, dirZ is the first
//   person camera's position and its -Z axis in the world. player.targeter is a PlayerTargeter.
//   StubHandTool.thirdPerson(player, on)  the camera is (not) in first person; a held tool attaches again.
// PlayerTargeter:update(dt)   reads player:getLookRay() once, like the game, and keeps it in targeter.lastRayX .. lastRayDirectionZ
//   (the fields the game keeps) and in StubHandTool.rays, in order, as { x, y, z, dirX, dirY, dirZ }.
//   StubHandTool.lastRay() -> x, y, z, dirX, dirY, dirZ   of the last update, nil before any
// StubHandTool.tick(dt)   one frame of the game's player update: the targeter reads the look ray, then every registered
//   updater (StubHandTool.updaters, below) runs
// StubHandTool.lance(player, options) -> tool   the pressure washer lance (spec_highPressureWasherLance), not yet held.
//   tool.rootNode (the node the game links), tool.firstPersonNode, tool.spec_highPressureWasherLance.raycastNode (the
//   nozzle: it washes from the node's origin along its +Z axis) and tool.activateAction, the function the game's
//   ACTIVATE_HANDTOOL binding calls (HandToolHPWLance:onWashAction(_, inputValue)).
//   StubHandTool.nozzleRay(tool) -> x, y, z, dirX, dirY, dirZ   where the lance washes from and along, in the world
// StubHandTool.otherModTool(player, options) -> tool   another mod's Hand Tool (spec_otherModTool), which the Companion Mod
//   does not support: placed like any other by the game. Its use handler is OtherModHandTool.onUseAction(tool, _, inputValue),
//   on the specialization class only (recorded in StubHandTool.useCalls as tool = "otherMod"), and tool.actionEvents lists
//   its registered events { actionName, target, callback, triggerUp, triggerDown, triggerAlways, isActive }: ACTIVATE_HANDTOOL
//   (up and down, not always) first; tool.activateAction, the game's binding, calls it on those edges. The tool also carries
//   what a Hand Tool of the game has to be read from outside (config file, type, specializations, nodes): see the comment
//   above the function; options shape it (extra fields, nodes, a spec that cannot be read)
// StubHandTool.hold(tool) / StubHandTool.drop(tool)   the player takes the tool in hand (the game's startHolding: isHeld,
//   the carrying player, attachTool) and puts it away (detachTool, not held)
// StubHandTool.pressUseBinding(tool, inputValue)   the game's own binding (X on a pad, the mouse, the keyboard) calling the
//   tool's use handler with inputValue (1 pressed or held, 0 released)
// StubHandTool.draw()   one frame of the game's draw: every held tool draws what it draws (the spray can's and the chainsaw's
//   onDraw are in their own chunks; another mod's tool draws "otherModGauge")
// StubHandTool.rendering(tool) -> text   what the tool rendered in the last draw, in order, joined by "+" ("" for nothing)
// StubHandTool.drawers   a list of function(tool) the draw runs for each held tool: a tool type's own onDraw goes in here;
//   StubHandTool.render(tool, overlay) records one thing rendered
// StubHandTool.useCalls   every call of a tool's use handler, in order, as { tool = <name>, value = <inputValue>, self = tool }
// StubHandTool.cameraAttaches   how many times the game's own attachToolToCamera ran (whatever placed the tool then)
// StubHandTool.CAMERA_PLACEMENT   { x, y, z } where the game puts a tool's root below the pitch node (a tool placed the
//   game's way is there; the lance's firstPersonNode is its root's origin)
// StubHandTool.tools   every tool made, in order
// Extension points for the later Hand Tools tickets (each adds its own chunk, run after this one, and touches nothing above):
//   StubHandTool.updaters   a list of function(dt) the tick runs after the targeter: a tool's own onUpdate goes in here
//   StubHandTool.makeTool(player, name, specName, options) -> tool   the common tool: a root node, a firstPersonNode at its
//     origin, spec table `specName`, the HandTool methods through the class; a tool type adds its own nodes and handlers
//   (the flashlight and the chainsaw add StubHandTool.flashlight / chainsaw, in their own chunks)
inline constexpr const char* kStubHandTool = R"luau(
    StubHandTool = { useCalls = {}, rays = {}, tools = {}, updaters = {}, drawers = {}, rendered = {}, cameraAttaches = 0 }
    StubHandTool.CAMERA_PLACEMENT = { x = 0.2, y = -0.15, z = 0.5 }
    local nodes = StubScene.nodes

    local function placeAtCamera(tool)
        local camera = tool.carryingPlayer.camera
        link(camera.pitchNode, tool.rootNode)
        local p = StubHandTool.CAMERA_PLACEMENT
        setTranslation(tool.rootNode, p.x, p.y, p.z)
        setRotation(tool.rootNode, 0, 0, 0)
    end

    function StubHandTool.install(variant, options)
        variant = variant or "ok"
        options = options or {}
        StubHandTool.variant = variant
        StubHandTool.useCalls, StubHandTool.rays, StubHandTool.tools, StubHandTool.cameraAttaches = {}, {}, {}, 0
        StubHandTool.heldTool = nil

        local HandTool = {}
        _G.HandTool = HandTool
        function HandTool.getIsCarried(self) return self.carryingPlayer ~= nil end
        function HandTool.getIsHeld(self) return self:getIsCarried() and self.isHeld == true end
        function HandTool.getCarryingPlayer(self) return self.carryingPlayer end
        function HandTool.attachToolToHand(self)
            link(self.carryingPlayer.handNode, self.rootNode)
            setTranslation(self.rootNode, 0, 0, 0)
            setRotation(self.rootNode, 0, 0, 0)
            return true
        end
        function HandTool.attachTool(self)
            if not self:getIsHeld() then return end
            if self.carryingPlayer.camera.isFirstPerson then
                if StubHandTool.variant == "missing" then
                    self:attachToolToCameraNode()
                elseif StubHandTool.variant == "reshaped" then
                    HandTool.attachToolToCamera(self)
                else
                    self:attachToolToCamera()
                end
            else
                self:attachToolToHand()
            end
            setVisibility(self.rootNode, true)
        end
        function HandTool.detachTool(self)
            setVisibility(self.rootNode, false)
            unlink(self.rootNode)
        end
        -- the game's placement in front of the face, whatever the version calls the function
        local function attachToCamera(tool)
            if not tool:getIsHeld() or tool.rootNode == nil then return end
            StubHandTool.cameraAttaches = StubHandTool.cameraAttaches + 1
            placeAtCamera(tool)
        end
        if variant == "missing" then
            function HandTool.attachToolToCameraNode(self) attachToCamera(self) end
        elseif variant == "reshaped" then
            function HandTool.attachToolToCamera() attachToCamera(StubHandTool.heldTool) end
        else
            function HandTool.attachToolToCamera(self) attachToCamera(self) end
        end
        StubHandTool.originalAttachToolToCamera = HandTool.attachToolToCamera

        local Player = {}
        _G.Player = Player
        local lookRay = function(self)
            local x, y, z = getWorldTranslation(self.camera.firstPersonCamera)
            local dx, dy, dz = StubScene.worldDirection(self.camera.firstPersonCamera, 0, 0, -1)
            return x, y, z, dx, dy, dz
        end
        -- the game's own: the tool the player holds now (Player.currentHandTool, set by Player:setCurrentHandTool)
        function Player.getHeldHandTool(self) return self.currentHandTool end
        StubHandTool.originalGetLookRay = nil
        if options.getLookRay == "missing" then
            Player.lookRay = lookRay
        elseif options.getLookRay == "reshaped" then
            Player.getLookRay = function() return lookRay(StubHandTool.player) end
        else
            Player.getLookRay = lookRay
            StubHandTool.originalGetLookRay = lookRay
        end

        local PlayerTargeter = {}
        PlayerTargeter.__index = PlayerTargeter
        _G.PlayerTargeter = PlayerTargeter
        function PlayerTargeter.new(player) return setmetatable({ player = player }, PlayerTargeter) end
        function PlayerTargeter:update(dt)
            -- a game whose getLookRay was renamed (options.getLookRay == "missing") reads it by the new name
            local read = self.player.getLookRay or self.player.lookRay
            self.lastRayX, self.lastRayY, self.lastRayZ, self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ =
                read(self.player)
            table.insert(StubHandTool.rays, { self.lastRayX, self.lastRayY, self.lastRayZ,
                self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ })
        end
    end

    function StubHandTool.onFoot(options)
        local scene = StubScene.onFoot(options)
        local camera = scene.cam
        local player = setmetatable({ camera = camera, isOwner = true }, { __index = _G.Player })
        player.handNode = StubScene.newNode("thirdPersonRightHand", scene.yawNode, 0.3, 1.2, 0.2)
        player.targeter = _G.PlayerTargeter.new(player)
        StubHandTool.player = player
        _G.g_currentMission.player = player
        _G.g_localPlayer = player
        return { player = player, cam = camera, parent = scene.parent, yawNode = scene.yawNode,
                 firstPersonCamera = scene.firstPersonCamera }
    end

    function StubHandTool.thirdPerson(player, on)
        player.camera.isFirstPerson = not on
        if StubHandTool.heldTool ~= nil then StubHandTool.heldTool:attachTool() end
    end

    function StubHandTool.tick(dt)
        local player = _G.g_localPlayer
        if player ~= nil and player.targeter ~= nil then player.targeter:update(dt or 16.667) end
        for _, updater in ipairs(StubHandTool.updaters) do updater(dt or 16.667) end
    end

    function StubHandTool.lastRay()
        local ray = StubHandTool.rays[#StubHandTool.rays]
        if ray == nil then return nil end
        return table.unpack(ray)
    end

    -- One frame of the game's draw: StubHandTool.rendered is emptied, then every held tool draws (each registered drawer, in
    -- StubHandTool.drawers, runs function(tool) for the tools of its kind, reading the class's onDraw at that moment, like the
    -- game's event dispatch). A tool's onDraw records what it rendered with StubHandTool.render(tool, overlay).
    function StubHandTool.render(tool, overlay) table.insert(StubHandTool.rendered, { tool = tool, overlay = overlay }) end
    function StubHandTool.draw()
        StubHandTool.rendered, StubHandTool.screen = {}, {}
        for _, tool in ipairs(StubHandTool.tools) do
            if tool.isHeld and tool.carryingPlayer ~= nil then
                for _, drawer in ipairs(StubHandTool.drawers) do drawer(tool) end
            end
        end
    end
    -- what the tool rendered in the last StubHandTool.draw(), in order, as one text ("" for nothing): "markerTypeIcon+treeIcon"
    function StubHandTool.rendering(tool)
        local names = {}
        for _, entry in ipairs(StubHandTool.rendered) do
            if entry.tool == tool then table.insert(names, entry.overlay) end
        end
        return table.concat(names, "+")
    end

    -- ---- the common tool ----------------------------------------------------------------------------------------
    -- A tool is what the game makes of a hand tool's i3d file: a root node, the first person node (where the game links
    -- the tool to the camera: here at the root's origin), the class's methods and the specialization's table.
    function StubHandTool.makeTool(player, name, specName, options)
        options = options or {}
        local tool = setmetatable({ name = name, isHeld = false, carryingPlayer = nil }, { __index = _G.HandTool })
        tool.rootNode = StubScene.newNode(name .. "Root", 0)
        tool.firstPersonNode = StubScene.newNode(name .. "FirstPerson", tool.rootNode)
        tool[specName] = {}
        tool.player = player
        setVisibility(tool.rootNode, false)
        table.insert(StubHandTool.tools, tool)
        return tool
    end

    local function useHandler(tool, name)
        return function(self, _, inputValue)
            table.insert(StubHandTool.useCalls, { tool = name, value = inputValue, self = self })
        end
    end

    function StubHandTool.hold(tool)
        tool.isHeld = true
        tool.carryingPlayer = tool.player
        tool.player.currentHandTool = tool
        StubHandTool.heldTool = tool
        tool:attachTool()
    end

    function StubHandTool.drop(tool)
        tool:detachTool()
        tool.isHeld = false
        tool.carryingPlayer = nil
        if tool.player.currentHandTool == tool then tool.player.currentHandTool = nil end
        if StubHandTool.heldTool == tool then StubHandTool.heldTool = nil end
        StubHandTool.restoreNameReading(tool)  -- a node name the tool made unreadable (otherModTool) reads again
    end

    function StubHandTool.pressUseBinding(tool, inputValue)
        tool:activateAction(nil, inputValue)
    end

    -- ---- the pressure washer lance ------------------------------------------------------------------------------
    function StubHandTool.lance(player, options)
        local tool = StubHandTool.makeTool(player, "lance", "spec_highPressureWasherLance", options)
        local spec = tool.spec_highPressureWasherLance
        spec.raycastNode = StubScene.newNode("lanceNozzle", tool.rootNode, 0, 0, 0.5)
        spec.isActivated = false
        local record = useHandler(tool, "lance")
        tool.onWashAction = function(self, a, inputValue)
            self.spec_highPressureWasherLance.isActivated = inputValue > 0
            record(self, a, inputValue)
        end
        tool.activateAction = tool.onWashAction
        return tool
    end

    function StubHandTool.nozzleRay(tool)
        local node = tool.spec_highPressureWasherLance.raycastNode
        local x, y, z = getWorldTranslation(node)
        local dx, dy, dz = StubScene.worldDirection(node, 0, 0, 1)
        return x, y, z, dx, dy, dz
    end

    -- A node whose name cannot be read (a node that is gone while a tool is being read): getName raises for it. The engine's
    -- getName is replaced only while a tool has such a node, kept in StubHandTool.engineGetName, and put back by drop(tool).
    local unreadableNames = {}  -- node -> the tool it belongs to
    function StubHandTool.makeNameUnreadable(tool, node)
        if StubHandTool.engineGetName == nil then
            local plainGetName = _G.getName
            StubHandTool.engineGetName = plainGetName
            _G.getName = function(id)
                if unreadableNames[id] ~= nil then error("the node " .. tostring(id) .. " is gone") end
                return plainGetName(id)
            end
        end
        unreadableNames[node] = tool
    end

    function StubHandTool.restoreNameReading(tool)
        for node, owner in pairs(unreadableNames) do
            if owner == tool then unreadableNames[node] = nil end
        end
        if StubHandTool.engineGetName ~= nil and next(unreadableNames) == nil then
            _G.getName = StubHandTool.engineGetName
            StubHandTool.engineGetName = nil
        end
    end

    -- ---- another mod's tool ---------------------------------------------------------------------------------------
    -- A Hand Tool the Companion Mod has never heard of, shaped like the tools mods add to the game: its own specialization
    -- (spec_otherModTool, which no SUPPORTED_TOOLS entry names), a use handler on its specialization class only
    -- (OtherModHandTool.onUseAction, called with the tool as `self`, like HandToolChainsaw.onCutAction) and an
    -- ACTIVATE_HANDTOOL action event registered for it (tool.actionEvents), as the game's registerActionEvent keeps one:
    -- called for trigger down (inputValue 1) and trigger up (0), not on every update the input is held.
    --
    -- It is also what the game gives any Hand Tool to be read from outside (HandTool.lua, SpecializationUtil.lua):
    --   tool.configFileName ("mods/FS25_OtherMod/handTools/otherModTool.xml"), tool.typeName
    --   tool.specializations / tool.specializationNames   the specialization classes and their names, in step (OtherModHandTool
    --     "otherModTool"); the specialization tables are tool.spec_<name>. A third table, spec_otherModLine, has no class.
    --   tool.type = { name, functions }   the tool's type and the functions it gives the instance (copied onto it)
    --   tool.actionEvents   three registered events: ACTIVATE_HANDTOOL (up and down, callback on the class), AXIS_ROTATE_HANDTOOL
    --     (every frame, callback on the class) and AXIS_OTHERMOD_NUDGE (up only, a callback that is on no class)
    --   the nodes: root -> FirstPerson, Rod -> Guide -> Tip -> Float (four levels below the root)
    -- options (all optional): nestedTables = true adds spec_otherModTool.nested ({ inner = { deep }, speed, name }) and an empty
    -- table; bigTable = N adds a table of N entries; pathNamedNode = true adds a field naming an entity whose name is a file path
    -- (as the engine's images and sounds are); extraSpecFields = N more fields in spec_otherModTool; extraSpecTables = N more specializations of
    -- 20 fields each; extraNodes = N more children of the root; extraClassFunctions = N more functions on OtherModHandTool (the
    -- first tool made in a game decides); brokenSpec = true adds spec_otherModBroken, which has a field
    -- naming a node whose getName raises (a node that is gone while the tool is being read); brokenNode = true adds a last child of
    -- the root whose name cannot be read the same way; both are the tool's, and StubHandTool.drop(tool) makes the names readable
    -- again (see makeNameUnreadable); configFileName replaces the name.
    function StubHandTool.otherModTool(player, options)
        options = options or {}
        local class = _G.OtherModHandTool
        if class == nil then
            class = {}
            _G.OtherModHandTool = class
            function class.onUseAction(self, _, inputValue)
                table.insert(StubHandTool.useCalls, { tool = "otherMod", value = inputValue, self = self })
            end
            function class.onRollAction(self, _, inputDelta) end
            for i = 1, options.extraClassFunctions or 0 do class["extraFunction" .. i] = function() end end
        end
        local tool = StubHandTool.makeTool(player, "otherMod", "spec_otherModTool", options)
        tool.configFileName = options.configFileName or "mods/FS25_OtherMod/handTools/otherModTool.xml"
        tool.typeName = "otherModTool"
        local spec = tool.spec_otherModTool
        spec.charge = 5
        spec.isReeling = false
        spec.label = "A rather long label that is cut short when it is written to the log file"
        spec.bobberNode = StubScene.newNode("otherModBobber", 0)
        spec.lines = { 1, 2, 3 }
        spec.onBite = function() end
        for i = 1, options.extraSpecFields or 0 do spec["extra" .. i] = i end
        if options.nestedTables then
            spec.nested = { inner = { deep = 1 }, speed = 2.5, name = "reel" }
            spec.emptyTable = {}
        end
        if options.bigTable then
            local big = {}
            for i = 1, options.bigTable do big["entry" .. i] = i end
            spec.bigTable = big
        end
        if options.pathNamedNode then spec.guiImage = StubScene.newNode("dataS/menu/gui.png", 0) end
        tool.spec_otherModLine = { length = 12.5 }
        tool.specializationNames = { "otherModTool" }
        tool.specializations = { class }
        for i = 1, options.extraSpecTables or 0 do
            local name = "otherModExtra" .. i
            local extra = {}
            for j = 1, 20 do extra["field" .. j] = j end
            tool["spec_" .. name] = extra
            table.insert(tool.specializationNames, name)
            table.insert(tool.specializations, {})
        end
        if options.brokenSpec then
            local brokenNode = StubScene.newNode("otherModBroken", 0)
            tool.spec_otherModBroken = { first = 1, gone = brokenNode, last = 2 }
            StubHandTool.makeNameUnreadable(tool, brokenNode)
            table.insert(tool.specializationNames, "otherModBroken")
            table.insert(tool.specializations, {})
        end
        -- the type's functions, which the game copies onto the instance
        local typeFunctions = {
            getFloatDepth = function(self) return self.spec_otherModTool.charge end,
            isOtherModReeling = function(self) return self.spec_otherModTool.isReeling end,
        }
        tool.type = { name = "otherModTool", functions = typeFunctions }
        for name, fn in pairs(typeFunctions) do tool[name] = fn end
        -- its own overlay, drawn by the game's draw (StubHandTool.draw) and never wrapped by anything of the Companion Mod
        tool.drawOverlay = function(self) StubHandTool.render(self, "otherModGauge") end
        -- the nodes below the root
        local rod = StubScene.newNode("otherModRod", tool.rootNode, 0, 0, 0.2)
        local guide = StubScene.newNode("otherModGuide", rod, 0, 0, 0.3)
        local tip = StubScene.newNode("otherModTip", guide, 0, 0, 0.3)
        StubScene.newNode("otherModFloat", tip, 0, 0, 0.1)
        for i = 1, options.extraNodes or 0 do StubScene.newNode("otherModExtra" .. i, tool.rootNode, 0, 0, 0) end
        if options.brokenNode then  -- the last child of the root: its name cannot be read
            StubHandTool.makeNameUnreadable(tool, StubScene.newNode("otherModGone", tool.rootNode, 0, 0, 0))
        end
        -- the registered action events
        local event = { actionName = "ACTIVATE_HANDTOOL", target = tool, triggerUp = true, triggerDown = true,
                        triggerAlways = false, isActive = true, isDown = false }
        event.callback = class.onUseAction  -- the function itself, as the game's registerActionEvent keeps it
        tool.actionEvents = {
            event,
            { actionName = "AXIS_ROTATE_HANDTOOL", target = tool, callback = class.onRollAction, triggerUp = false, triggerDown = false,
              triggerAlways = true, isActive = true },
            { actionName = "AXIS_OTHERMOD_NUDGE", target = tool, callback = function() end, triggerUp = true, triggerDown = false,
              triggerAlways = false, isActive = true },
        }
        -- the game's input binding: the registered event is called on the edges it was registered for
        tool.activateAction = function(self, _, inputValue)
            if not event.isActive then return end
            local down = inputValue > 0
            if down and not event.isDown and event.triggerDown then
                event.callback(event.target, event.actionName, inputValue)
            elseif down and event.isDown and event.triggerAlways then
                event.callback(event.target, event.actionName, inputValue)
            elseif not down and event.isDown and event.triggerUp then
                event.callback(event.target, event.actionName, inputValue)
            end
            event.isDown = down
        end
        return tool
    end

    table.insert(StubHandTool.drawers, function(tool)
        if tool.spec_otherModTool ~= nil then tool:drawOverlay() end
    end)

    -- ---- The chainsaw and the look ray: their own chunks add StubHandTool.chainsaw / ... with
    -- StubHandTool.makeTool and StubHandTool.updaters; nothing above changes. The flashlight is kStubHandToolFlashlight.
)luau";

// ---- Hand Tools: the flashlight ---------------------------------------------------------------------------
// Run after kStubHandTool (and after kStubModelLoading: it uses setVisibility and setWorldQuaternion). Its own chunk, so that
// the other Hand Tools tickets do not edit the same lines. Shaped after the game's HandToolFlashlight (the readable source in
// the SDK's gameSource.zip), with the game's placement of the light as ADR-0029 describes it: at the first person camera.
//
// StubHandTool.installFlashlight(variant)   defines the global HandToolFlashlight, the class the game's flashlight is made of.
//   Run it BEFORE loading the Companion Mod to have it there at file load, or any time before the map is loaded.
//   variant   how the class's event HandToolFlashlight.onUpdate(tool, dt) is in this "game version": nil / "ok",
//             "missing" (a game update renamed it to onUpdateBeam: the class has no onUpdate, the game updates through
//             the new name) or "reshaped" (it takes no parameters at all, and updates the flashlight being held)
//   StubHandTool.originalFlashlightOnUpdate   is the game's own onUpdate ("ok" game only: nil in the others)
//   HandToolFlashlight.updateTransform(tool, isHeld)   the game's placement of the light node: at the camera of the carrying
//     player, along its view. (Each flashlight keeps its own copy of the function, as the game's tools do, so that a wrap of
//     the class does not reach it.)
//   The game updates each carried flashlight once per StubHandTool.tick, after the targeter, through the class's onUpdate
//   looked up at that moment, as the game raises its events.
// StubHandTool.flashlight(player, options) -> tool   the flashlight (spec_flashlight), not yet held. The spec is the game's:
//   lightNode (the light, linked to the world root; visible only while the flashlight is on), lightNodeParent (the node the
//   light was below in the model: the lens, 0.15 m along the tool's +Z), lightNodeOffsetX/Y/Z (where the light sits in the lens)
//   and lightNodeForwardX/Y/Z (the way it shines in the lens, +Z), isActive.
//   tool:setFlashlightIsActive(isActive, noEventSend)   the flashlight's own switch; every call is in
//     StubHandTool.flashlightCalls as { tool = <the flashlight>, isActive = <the argument>, noEventSend = <the argument> }
//   player.isFlashlightActive   the player's own flag for the light, false at first (the game restores the light from it)
//   StubHandTool.beam(tool) -> x, y, z, dirX, dirY, dirZ   where the light node is now and the way it shines, in the world
//   StubHandTool.lensRay(tool) -> x, y, z, dirX, dirY, dirZ   where the light leaves the lens and the way it shines, in the world
//   StubHandTool.flashlights   every flashlight made, in order
//   setDirection(node, dirX, dirY, dirZ, upX, upY, upZ)   the engine's call: the node's +Z axis along the direction
inline constexpr const char* kStubHandToolFlashlight = R"luau(
    StubHandTool.flashlightCalls = {}
    StubHandTool.flashlights = {}
    local nodes = StubScene.nodes

    _G.setDirection = function(node, dx, dy, dz, ux, uy, uz)
        local function norm(x, y, z) local l = math.sqrt(x * x + y * y + z * z); return x / l, y / l, z / l end
        local zx, zy, zz = norm(dx, dy, dz)
        local xx, xy, xz = norm(uy * zz - uz * zy, uz * zx - ux * zz, ux * zy - uy * zx)
        local yx, yy, yz = zy * xz - zz * xy, zz * xx - zx * xz, zx * xy - zy * xx
        -- the rotation matrix with columns x, y, z as a quaternion
        local trace = xx + yy + zz
        local qx, qy, qz, qw
        if trace > 0 then
            local s = math.sqrt(trace + 1) * 2
            qw, qx, qy, qz = s / 4, (yz - zy) / s, (zx - xz) / s, (xy - yx) / s
        elseif xx > yy and xx > zz then
            local s = math.sqrt(1 + xx - yy - zz) * 2
            qw, qx, qy, qz = (yz - zy) / s, s / 4, (yx + xy) / s, (zx + xz) / s
        elseif yy > zz then
            local s = math.sqrt(1 + yy - xx - zz) * 2
            qw, qx, qy, qz = (zx - xz) / s, (yx + xy) / s, s / 4, (zy + yz) / s
        else
            local s = math.sqrt(1 + zz - xx - yy) * 2
            qw, qx, qy, qz = (xy - yx) / s, (zx + xz) / s, (zy + yz) / s, s / 4
        end
        setWorldQuaternion(node, qx, qy, qz, qw)
    end

    function StubHandTool.installFlashlight(variant)
        variant = variant or "ok"
        StubHandTool.flashlightVariant = variant
        StubHandTool.flashlightCalls, StubHandTool.flashlights = {}, {}
        local HandToolFlashlight = {}
        _G.HandToolFlashlight = HandToolFlashlight

        -- the game's placement of the light: at the camera, along the view
        function HandToolFlashlight.updateTransform(self, isHeld)
            local player = self:getCarryingPlayer()
            if player == nil then return end
            local camera = player.camera.firstPersonCamera
            local x, y, z = getWorldTranslation(camera)
            local dx, dy, dz = StubScene.worldDirection(camera, 0, 0, -1)
            setWorldTranslation(self.spec_flashlight.lightNode, x, y, z)
            setDirection(self.spec_flashlight.lightNode, dx, dy, dz, 0, 1, 0)
        end
        if variant == "missing" then
            function HandToolFlashlight.onUpdateBeam(self, dt) self:updateTransform(self:getIsHeld()) end
        elseif variant == "reshaped" then
            function HandToolFlashlight.onUpdate()
                local held = StubHandTool.heldTool
                if held ~= nil and held.spec_flashlight ~= nil then held:updateTransform(true) end
            end
        else
            function HandToolFlashlight.onUpdate(self, dt) self:updateTransform(self:getIsHeld()) end
        end
        StubHandTool.originalFlashlightOnUpdate = variant == "ok" and HandToolFlashlight.onUpdate or nil

        if not StubHandTool.flashlightUpdaterAdded then
            StubHandTool.flashlightUpdaterAdded = true
            table.insert(StubHandTool.updaters, function(dt)
                for _, tool in ipairs(StubHandTool.flashlights) do
                    if tool:getIsCarried() then
                        local class = _G.HandToolFlashlight
                        local update = class.onUpdate or class.onUpdateBeam
                        update(tool, dt)
                    end
                end
            end)
        end
    end

    function StubHandTool.flashlight(player, options)
        local tool = StubHandTool.makeTool(player, "flashlight", "spec_flashlight", options)
        local spec = tool.spec_flashlight
        spec.isActive = false
        spec.lightNodeParent = StubScene.newNode("flashlightLens", tool.rootNode, 0, 0, 0.15)
        spec.lightNodeOffsetX, spec.lightNodeOffsetY, spec.lightNodeOffsetZ = 0, 0.01, 0.02
        spec.lightNodeForwardX, spec.lightNodeForwardY, spec.lightNodeForwardZ = 0, 0, 1
        spec.lightNode = StubScene.newNode("flashlightLight", getRootNode())
        setVisibility(spec.lightNode, false)
        player.isFlashlightActive = false
        -- a tool's functions are copies made when it is made
        tool.updateTransform = _G.HandToolFlashlight.updateTransform
        tool.setFlashlightIsActive = function(self, isActive, noEventSend)
            self.spec_flashlight.isActive = isActive
            setVisibility(self.spec_flashlight.lightNode, isActive)
            table.insert(StubHandTool.flashlightCalls, { tool = self, isActive = isActive, noEventSend = noEventSend })
        end
        -- the game's own binding for the flashlight is not the use binding: the tool has no activateAction
        table.insert(StubHandTool.flashlights, tool)
        return tool
    end

    function StubHandTool.beam(tool)
        local node = tool.spec_flashlight.lightNode
        local x, y, z = getWorldTranslation(node)
        local dx, dy, dz = StubScene.worldDirection(node, 0, 0, 1)
        return x, y, z, dx, dy, dz
    end

    function StubHandTool.lensRay(tool)
        local spec = tool.spec_flashlight
        local x, y, z = localToWorld(spec.lightNodeParent, spec.lightNodeOffsetX, spec.lightNodeOffsetY, spec.lightNodeOffsetZ)
        local dx, dy, dz = StubScene.worldDirection(spec.lightNodeParent, spec.lightNodeForwardX, spec.lightNodeForwardY, spec.lightNodeForwardZ)
        return x, y, z, dx, dy, dz
    end
)luau";

// ---- Hand Tools: the chainsaw (ADR-0029) -------------------------------------------------------
// The chainsaw for StubHandTool, shaped after the game's HandToolChainsaw. Run after kStubHandTool (and after the Companion
// Mod is loaded or before it: HandToolChainsaw is read at map load too), then call StubHandTool.installChainsaw(variant).
//
// StubHandTool.installChainsaw(variant)   defines the globals the game defines: ChainsawCutState, HandToolChainsaw,
//   PlayerCamera and g_cameraManager, and makes the targeter honour target types: PlayerTargeter:addTargetType(key, mask,
//   minDistance, maxDistance) and :removeTargetType(key) (the spray can's chunk's, when it has added them), and each update fills
//   closestTargetsByKey[HandToolChainsaw] with the closest trunk the look ray meets, measured from the ray's origin and only between
//   the type's minimum and maximum distance (the game's tryAddTargetWithMask), as { node, x, y, z, distance }. Holding a chainsaw
//   adds its target type (trees, from HandToolChainsaw.MINIMUM_CUT_DISTANCE, or 0 when the class has none, to 3 m), as the game's
//   onHeldStart does; putting it away removes it. The minimum is read at that moment, so a test sets it before the player takes the
//   saw in hand.
//   variant   how HandToolChainsaw.onUpdate is in this "game version": nil / "ok", "missing" (a game update renamed it to
//             onUpdateTick, which the event dispatch calls instead) or "reshaped" (it takes no parameters, and updates the
//             chainsaw being held), or "rollMissing" / "rollReshaped" (the roll action's handler,
//             HandToolChainsaw.onRollAction, is gone / takes no parameters; onUpdate is as "ok"), or "drawMissing" /
//             "drawReshaped" (the draw event, HandToolChainsaw.onDraw, is gone: a game update renamed it to onDrawOverlays,
//             which the draw calls instead / takes no parameters and draws the chainsaw being held; onUpdate is as "ok"), or
//             "minimumMissing" (HandToolChainsaw.MINIMUM_CUT_DISTANCE is gone: a game update dropped or renamed it; onUpdate is as "ok")
// The game keeps the chainsaw's handlers on the specialization class and its event dispatch calls them as
// Class.handler(tool, ...) (SpecializationUtil.raiseEvent: spec[eventName](object, ...)). So they are here:
//   HandToolChainsaw.onUpdate(tool, dt)       the chainsaw's update (run each tick by StubHandTool.tick): targets the tree the
//                                             targeter found, aligns the split plane (updateSplitPlane), turns the model to
//                                             face the camera, validates the cut, shows the ring marker, advances a cut
//   HandToolChainsaw.onDraw(tool)             the chainsaw's draw (StubHandTool.draw): renders "crosshair", the game's default one at
//                                             the centre of the view, while the saw is idle
//   HandToolChainsaw.onCutAction(tool, _, v)  the ACTIVATE_HANDTOOL handler (v > 0 starts a cut on the targeted tree, or
//                                             delimbing without one; v = 0 stops); recorded in StubHandTool.useCalls
//   HandToolChainsaw.onRollAction(tool, _, d) the AXIS_ROTATE_HANDTOOL handler (rollInput += d, applied by the next update
//                                             when not cutting, to spec.currentCutRoll, clamped to +-pi/2)
//   HandToolChainsaw.MINIMUM_CUT_DISTANCE     the nearest a trunk can be to the targeter's ray start and still be found (0 here, so
//                                             existing tests find a trunk anywhere along the ray; a test sets the game's own)
//   HandToolChainsaw.MAXIMUM_CUT_DISTANCE     a cut stops when the targeter's last ray starts this far from the cut node
// and, as the game's registerFunction does, on the tool itself: updateSplitPlane, calculateCutPlane, updateRingSelector,
// testIfCutValid, beginCutting, stopCutting, beginDelimbing, stopDelimbing, updateCutting, setCurrentCutState.
// PlayerCamera.getRotation(camera) -> pitch, yaw, roll   the player camera's rotation from its look direction (the game's):
//   updateSplitPlane takes the cut plane's yaw from it. The player's camera gets it as a method when a chainsaw is made.
//
// StubHandTool.chainsaw(player, options) -> tool   the chainsaw (spec_chainsaw), not yet held. options.cutTime: ms a cut takes
//   (default 600). tool.rootNode, tool.graphicalNodeParent, tool.graphicalNode (the model), and in tool.spec_chainsaw:
//   currentCutState (ChainsawCutState.IDLE / CUTTING / DELIMBING), currentCutRoll, cameraRotationNode (the node the cut plane
//   hangs from: at the targeted tree's hit point while idle), splitPlaneNode, cutGuideNode (the model rides it while cutting),
//   ringNode (the marker, visible while a tree is targeted), cutNode (a child of the model 0.4 m along the bar and 4 cm below its
//   centre line, as in the game's models), targetedTree ({ node, x, y, z, distance }), cuttingTreeNode, lastDelimbPosX .. lastDelimbNormalZ.
// StubHandTool.tree(x, z, radius, y0, y1) -> tree   a vertical trunk: node, x, z, radius, y0, y1. A held chainsaw's targeter
//   hits the closest trunk its look ray meets between the class's minimum cut distance and 3 m. StubHandTool.trees lists them.
// StubHandTool.treeAcross(ray, distance, radius) -> tree   a tall trunk whose surface the ray meets, horizontally, at `distance`;
//   ray = { x, y, z, dirX, dirY, dirZ }
// StubHandTool.barRay(tool) -> x, y, z, dirX, dirY, dirZ   where the chainsaw's bar points now: the root's origin and +Z
// StubHandTool.modelDirection(tool) -> dirX, dirY, dirZ   the way the model's +Z points in the world
// StubHandTool.cuts   every tree the game cut through, in order: { tree, x, y, z, normalX, normalY, normalZ, upX, upY, upZ }
//   where the plane was (calculateCutPlane); StubHandTool.inputLocks: how many times the cut locked the player's input minus unlocks
inline constexpr const char* kStubHandToolChainsaw = R"luau(
    local nodes = StubScene.nodes
    StubHandTool.trees, StubHandTool.cuts, StubHandTool.inputLocks = {}, {}, 0

    -- ---- engine functions the scene stub lacks ----------------------------------------------------------------------
    local function qmul(ax, ay, az, aw, bx, by, bz, bw)
        return aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
               aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz
    end
    local function eulerToQuat(rx, ry, rz)
        local cx, sx = math.cos(rx * 0.5), math.sin(rx * 0.5)
        local cy, sy = math.cos(ry * 0.5), math.sin(ry * 0.5)
        local cz, sz = math.cos(rz * 0.5), math.sin(rz * 0.5)
        local x, y, z, w = qmul(0, sy, 0, cy, sx, 0, 0, cx)
        return qmul(x, y, z, w, 0, 0, sz, cz)
    end
    local function normalize(x, y, z)
        local length = math.sqrt(x * x + y * y + z * z)
        if length < 1e-9 then return 0, 0, 1 end
        return x / length, y / length, z / length
    end
    _G.setWorldRotation = function(node, rx, ry, rz)
        local qx, qy, qz, qw = eulerToQuat(rx, ry, rz)
        local px, py, pz, pw = getWorldQuaternion(getParent(node))
        setQuaternion(node, qmul(-px, -py, -pz, pw, qx, qy, qz, qw))
    end
    -- the direction is the node's +Z, in the space of its parent; the up vector keeps the node's +Y near it. (The flashlight's
    -- chunk, kStubHandToolFlashlight, defines one for nodes below the world root; a game that loads both keeps that one.)
    _G.setDirection = _G.setDirection or function(node, dx, dy, dz, ux, uy, uz)
        local zx, zy, zz = normalize(dx, dy, dz)
        local xx, xy, xz = normalize(uy * zz - uz * zy, uz * zx - ux * zz, ux * zy - uy * zx)
        local yx, yy, yz = zy * xz - zz * xy, zz * xx - zx * xz, zx * xy - zy * xx
        local trace = xx + yy + zz
        local qx, qy, qz, qw
        if trace > 0 then
            local s = math.sqrt(trace + 1) * 2
            qw, qx, qy, qz = 0.25 * s, (yz - zy) / s, (zx - xz) / s, (xy - yx) / s
        elseif xx > yy and xx > zz then
            local s = math.sqrt(1 + xx - yy - zz) * 2
            qw, qx, qy, qz = (yz - zy) / s, 0.25 * s, (yx + xy) / s, (zx + xz) / s
        elseif yy > zz then
            local s = math.sqrt(1 + yy - xx - zz) * 2
            qw, qx, qy, qz = (zx - xz) / s, (yx + xy) / s, 0.25 * s, (zy + yz) / s
        else
            local s = math.sqrt(1 + zz - xx - yy) * 2
            qw, qx, qy, qz = (xy - yx) / s, (zx + xz) / s, (zy + yz) / s, 0.25 * s
        end
        setQuaternion(node, qx, qy, qz, qw)
    end
    _G.rotateAboutLocalAxis = function(node, angle, ax, ay, az)
        ax, ay, az = normalize(ax, ay, az)
        local s = math.sin(angle * 0.5)
        local qx, qy, qz, qw = getQuaternion(node)
        setQuaternion(node, qmul(qx, qy, qz, qw, ax * s, ay * s, az * s, math.cos(angle * 0.5)))
    end
    _G.localDirectionToLocal = function(from, to, x, y, z)
        local wx, wy, wz = StubScene.worldDirection(from, x, y, z)
        local tx, ty, tz = getWorldTranslation(to)
        return worldToLocal(to, tx + wx, ty + wy, tz + wz)
    end

    -- ---- trees and the targeter ---------------------------------------------------------------------------------------
    local MAX_TARGET_DISTANCE = 3.0

    function StubHandTool.tree(x, z, radius, y0, y1)
        local node = StubScene.newNode("tree", 0, x, y0, z)
        local tree = { node = node, x = x, z = z, radius = radius, y0 = y0, y1 = y1 }
        table.insert(StubHandTool.trees, tree)
        return tree
    end

    function StubHandTool.treeAcross(ray, distance, radius)
        local x, y, z, dx, dy, dz = table.unpack(ray)
        local hx, _, hz = normalize(dx, 0, dz)
        return StubHandTool.tree(x + hx * (distance + radius), z + hz * (distance + radius), radius, y - 10, y + 10)
    end

    -- the closest trunk a ray meets between a minimum and a maximum distance of its origin, as the targeter reports a target:
    -- { node, x, y, z, distance }
    local function castAtTrees(x, y, z, dx, dy, dz, minDistance, maxDistance)
        local best = nil
        for _, tree in ipairs(StubHandTool.trees) do
            local ox, oz = x - tree.x, z - tree.z
            local a = dx * dx + dz * dz
            if a > 1e-9 then
                local b = ox * dx + oz * dz
                local disc = b * b - a * (ox * ox + oz * oz - tree.radius ^ 2)
                if disc >= 0 then
                    local t = (-b - math.sqrt(disc)) / a
                    local hitY = y + dy * t
                    if t >= minDistance and t <= maxDistance and hitY >= tree.y0 and hitY <= tree.y1 and (best == nil or t < best.distance) then
                        best = { node = tree.node, x = x + dx * t, y = hitY, z = z + dz * t, distance = t }
                    end
                end
            end
        end
        return best
    end

    local function treeOfNode(node)
        for _, tree in ipairs(StubHandTool.trees) do
            if tree.node == node then return tree end
        end
        return nil
    end

    -- the game's testSplitShape: how far the tree reaches in the plane, around the node the plane hangs from
    _G.testSplitShape = function(treeNode, x, y, z, nx, ny, nz, yx, yy, yz, width, depth)
        local tree = treeOfNode(treeNode)
        if tree == nil then return nil end
        return -tree.radius, tree.radius, -tree.radius, tree.radius
    end

    -- ---- the chainsaw -------------------------------------------------------------------------------------------------
    function StubHandTool.installChainsaw(variant)
        variant = variant or "ok"
        StubHandTool.trees, StubHandTool.cuts, StubHandTool.inputLocks = {}, {}, 0
        local State = { IDLE = 0, CUTTING = 1, DELIMBING = 2 }
        _G.ChainsawCutState = State
        -- (a minimum of 0 leaves a trunk anywhere along the look ray found; a test sets it to the game's own)
        local Class = { MINIMUM_CUT_DISTANCE = 0, MAXIMUM_CUT_DISTANCE = 1.5 }
        if variant == "minimumMissing" then Class.MINIMUM_CUT_DISTANCE = nil end
        _G.HandToolChainsaw = Class
        _G.g_cameraManager = { getActiveCamera = function() return getCamera() end }

        local PlayerCamera = {}
        _G.PlayerCamera = PlayerCamera
        function PlayerCamera.getRotation(self)
            local dx, dy, dz = StubScene.worldDirection(self.cameraRootNode, 0, 0, -1)
            return -math.atan2(dy, math.sqrt(dx * dx + dz * dz)), math.atan2(dx, dz), 0
        end

        -- the targeter: target types with a minimum and a maximum distance (the spray can's chunk may have added them)
        local PlayerTargeter = _G.PlayerTargeter
        if PlayerTargeter.addTargetType == nil then
            function PlayerTargeter:addTargetType(key, mask, minDistance, maxDistance)
                self.targetedMasks = self.targetedMasks or {}
                if self.targetedMasks[key] ~= nil then return end
                self.targetedMasks[key] = { key = key, mask = mask, minDistance = minDistance or 0, maxDistance = maxDistance }
            end
            function PlayerTargeter:removeTargetType(key)
                if self.targetedMasks ~= nil then self.targetedMasks[key] = nil end
                if self.closestTargetsByKey ~= nil then self.closestTargetsByKey[key] = nil end
            end
        end
        local baseUpdate = PlayerTargeter.update
        function PlayerTargeter:update(dt)
            baseUpdate(self, dt)
            self.closestTargetsByKey = self.closestTargetsByKey or {}
            local targetedMask = (self.targetedMasks or {})[Class]
            self.closestTargetsByKey[Class] = nil
            if targetedMask ~= nil and self.lastRayX ~= nil then
                self.closestTargetsByKey[Class] = castAtTrees(self.lastRayX, self.lastRayY, self.lastRayZ,
                    self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ, targetedMask.minDistance, targetedMask.maxDistance)
            end
        end

        -- a chainsaw taken in hand targets trees, one put away stops what it is doing and targets none (the game's onHeldStart)
        local baseHold, baseDrop = StubHandTool.hold, StubHandTool.drop
        function StubHandTool.hold(tool)
            baseHold(tool)
            if tool.spec_chainsaw ~= nil and tool.carryingPlayer.isOwner then
                tool.carryingPlayer.targeter:addTargetType(Class, 0, Class.MINIMUM_CUT_DISTANCE or 0, MAX_TARGET_DISTANCE)
            end
        end
        function StubHandTool.drop(tool)
            if tool.spec_chainsaw ~= nil and tool.spec_chainsaw.currentCutState == State.CUTTING then tool:stopCutting(false) end
            if tool.spec_chainsaw ~= nil and tool.carryingPlayer ~= nil and tool.carryingPlayer.targeter ~= nil then
                tool.carryingPlayer.targeter:removeTargetType(Class)
            end
            baseDrop(tool)
        end

        -- ---- the functions the game registers on the tool
        local functions = {}
        function functions.setCurrentCutState(self, cutState, isVerticalCut)
            self.spec_chainsaw.currentCutState = cutState
            self.spec_chainsaw.isVerticalCut = isVerticalCut
        end

        function functions.calculateCutPlane(self)
            local spec = self.spec_chainsaw
            local x, y, z = getWorldTranslation(spec.splitPlaneNode)
            local downX, downY, downZ = localDirectionToWorld(spec.splitPlaneNode, 0, -1, 0)
            local leftX, leftY, leftZ = localDirectionToWorld(spec.splitPlaneNode, 1, 0, 0)
            return x, y, z, downX, downY, downZ, leftX, leftY, leftZ, spec.maximumCutDiameter, spec.maximumCutDiameter
        end

        function functions.updateSplitPlane(self)
            local spec = self.spec_chainsaw
            if spec.currentCutState == State.DELIMBING then
                local x, y, z = localToWorld(self.graphicalNode, 0, -spec.maximumDelimbDiameter * 0.5, 0)
                setWorldTranslation(spec.cameraRotationNode, x, y, z)
                local cameraNode = g_cameraManager:getActiveCamera()
                local upX, upY, upZ = localDirectionToWorld(cameraNode, 0, 1, 0)
                local dirX, dirY, dirZ = localDirectionToWorld(cameraNode, 0, 0, -1)
                setDirection(spec.cameraRotationNode, dirX, dirY, dirZ, upX, upY, upZ)
            else
                local _, playerYaw = self:getCarryingPlayer().camera:getRotation()
                setWorldRotation(spec.cameraRotationNode, 0, playerYaw, 0)
            end
            rotateAboutLocalAxis(spec.cameraRotationNode, spec.currentCutRoll, 0, 0, 1)

            spec.cutMinimumY, spec.cutMaximumY, spec.cutMinimumZ, spec.cutMaximumZ = nil, nil, nil, nil
            if spec.currentCutState == State.CUTTING then
                if spec.cuttingTreeNode == nil or not entityExists(spec.cuttingTreeNode) then return end
                spec.cutMinimumY, spec.cutMaximumY, spec.cutMinimumZ, spec.cutMaximumZ =
                    testSplitShape(spec.cuttingTreeNode, self:calculateCutPlane())
            elseif spec.currentCutState == State.IDLE then
                if spec.targetedTree == nil or not entityExists(spec.targetedTree.node) then return end
                setWorldTranslation(spec.cameraRotationNode, spec.targetedTree.x, spec.targetedTree.y, spec.targetedTree.z)
                spec.cutMinimumY, spec.cutMaximumY, spec.cutMinimumZ, spec.cutMaximumZ =
                    testSplitShape(spec.targetedTree.node, self:calculateCutPlane())
            end
        end

        function functions.testIfCutValid(self, treeNode, cutX, cutZ, minY, maxY, minZ, maxZ)
            local spec = self.spec_chainsaw
            if minY == nil or maxY == nil or minZ == nil or maxZ == nil then return false end
            return not (maxY - minY >= spec.maximumCutDiameter or maxZ - minZ >= spec.maximumCutDiameter)
        end

        -- the ring marker sits on the targeted trunk at the height of the cut node
        function functions.updateRingSelector(self, targetedTree, isCutPossible)
            local spec = self.spec_chainsaw
            setVisibility(spec.ringNode, targetedTree ~= nil)
            if targetedTree == nil then return end
            local tree = treeOfNode(targetedTree.node)
            local _, cutY = getWorldTranslation(spec.cameraRotationNode)
            local x, y, z = worldToLocal(getParent(spec.ringNode), tree.x, cutY, tree.z)
            setTranslation(spec.ringNode, x, y, z)
            spec.ringIsValid = isCutPossible
        end

        function functions.beginDelimbing(self)
            self:setCurrentCutState(State.DELIMBING, true)
        end
        function functions.stopDelimbing(self)
            self:setCurrentCutState(State.IDLE, true)
        end

        function functions.beginCutting(self)
            local spec = self.spec_chainsaw
            setVisibility(spec.ringNode, false)
            if spec.possibleTreeNode == nil then return end
            self:setCurrentCutState(State.CUTTING, true)
            spec.cuttingTreeNode = spec.possibleTreeNode
            spec.currentCutTime = 0
            spec.cutStartPositionX, spec.cutStartPositionY, spec.cutStartPositionZ =
                localToWorld(spec.splitPlaneNode, spec.cutMinimumY, 0, spec.cutMinimumZ)
            spec.cutEndPositionX, spec.cutEndPositionY, spec.cutEndPositionZ =
                localToWorld(spec.splitPlaneNode, spec.cutMaximumY, 0, spec.cutMinimumZ)
            -- the model rides the cut guide, which moves along the cut in the world
            link(spec.cutGuideNode, self.graphicalNode)
            setTranslation(self.graphicalNode, 0, 0, 0)
            setRotation(self.graphicalNode, 0, 0, 0)
            StubHandTool.inputLocks = StubHandTool.inputLocks + 1
        end

        function functions.stopCutting(self, sliceTree)
            local spec = self.spec_chainsaw
            local cuttingTreeNode = spec.cuttingTreeNode
            spec.cuttingTreeNode = nil
            self:setCurrentCutState(State.IDLE, true)
            spec.currentCutTime = 0
            link(self.graphicalNodeParent, self.graphicalNode)
            setTranslation(self.graphicalNode, 0, 0, 0)
            StubHandTool.inputLocks = StubHandTool.inputLocks - 1
            if sliceTree and cuttingTreeNode ~= nil then
                local x, y, z, nx, ny, nz, ux, uy, uz = self:calculateCutPlane()
                table.insert(StubHandTool.cuts, { tree = cuttingTreeNode, x = x, y = y, z = z,
                    normalX = nx, normalY = ny, normalZ = nz, upX = ux, upY = uy, upZ = uz })
            end
        end

        function functions.updateCutting(self, dt)
            local spec = self.spec_chainsaw
            local cutX, cutY, cutZ = getWorldTranslation(spec.cameraRotationNode)
            local targeter = self:getCarryingPlayer().targeter
            local dx, dy, dz = targeter.lastRayX - cutX, targeter.lastRayY - cutY, targeter.lastRayZ - cutZ
            spec.currentCutTime = spec.currentCutTime + dt
            if math.sqrt(dx * dx + dy * dy + dz * dz) >= Class.MAXIMUM_CUT_DISTANCE then
                self:stopCutting(false)
                return
            end
            local progress = math.clamp(spec.currentCutTime / spec.cutTime, 0, 1)
            setWorldTranslation(spec.cutGuideNode,
                spec.cutStartPositionX + (spec.cutEndPositionX - spec.cutStartPositionX) * progress,
                spec.cutStartPositionY + (spec.cutEndPositionY - spec.cutStartPositionY) * progress,
                spec.cutStartPositionZ + (spec.cutEndPositionZ - spec.cutStartPositionZ) * progress)
            if spec.currentCutTime >= spec.cutTime then self:stopCutting(true) end
        end

        -- ---- the specialization's own handlers: on the class, not on the tool
        local function update(self, dt)
            local spec = self.spec_chainsaw
            local carryingPlayer = self:getCarryingPlayer()
            if carryingPlayer == nil or not carryingPlayer.isOwner or not self:getIsHeld() then return end
            spec.targetedTree = carryingPlayer.targeter.closestTargetsByKey and carryingPlayer.targeter.closestTargetsByKey[Class] or nil
            spec.possibleTreeNode = nil
            self:updateSplitPlane()

            if spec.currentCutState ~= State.CUTTING then
                if spec.rollInput ~= 0 then
                    spec.currentCutRoll = math.clamp(spec.currentCutRoll + 0.001 * spec.rollInput * dt, -math.pi * 0.5, math.pi * 0.5)
                    spec.rollInput = 0
                end
                -- the model faces the camera
                local upX, upY, upZ = localDirectionToLocal(spec.cameraRotationNode, getParent(self.graphicalNode), 0, 1, 0)
                local dirX, dirY, dirZ = localDirectionToLocal(getCamera(), getParent(self.graphicalNode), 0, 0, -1)
                setDirection(self.graphicalNode, dirX, dirY, dirZ, upX, upY, upZ)
            end

            local isCutPossible = false
            if spec.currentCutState == State.IDLE or spec.currentCutState == State.CUTTING then
                local treeNode = spec.cuttingTreeNode
                if treeNode == nil and spec.targetedTree ~= nil then treeNode = spec.targetedTree.node end
                if treeNode ~= nil then
                    local cutX, _, cutZ = getWorldTranslation(spec.cameraRotationNode)
                    if not self:testIfCutValid(treeNode, cutX, cutZ, spec.cutMinimumY, spec.cutMaximumY, spec.cutMinimumZ, spec.cutMaximumZ) then
                        if spec.cuttingTreeNode ~= nil then self:stopCutting(false) end
                    else
                        if spec.cuttingTreeNode == nil then spec.possibleTreeNode = treeNode end
                        isCutPossible = true
                    end
                end
            end

            if spec.currentCutState == State.IDLE then
                self:updateRingSelector(spec.targetedTree, isCutPossible)
            elseif spec.currentCutState == State.CUTTING then
                self:updateCutting(dt)
            end

            if spec.currentCutState == State.DELIMBING then
                local x, y, z, nx, ny, nz, ux, uy, uz = self:calculateCutPlane()
                spec.lastDelimbPosX, spec.lastDelimbPosY, spec.lastDelimbPosZ = x, y, z
                spec.lastDelimbNormalX, spec.lastDelimbNormalY, spec.lastDelimbNormalZ = nx, ny, nz
                spec.lastDelimbUpX, spec.lastDelimbUpY, spec.lastDelimbUpZ = ux, uy, uz
            end
        end
        if variant == "missing" then
            Class.onUpdateTick = update
        elseif variant == "reshaped" then
            Class.onUpdate = function() update(StubHandTool.heldTool, 16.667) end
        else
            Class.onUpdate = update
        end

        function Class.onCutAction(self, _, inputValue)
            local spec = self.spec_chainsaw
            table.insert(StubHandTool.useCalls, { tool = "chainsaw", value = inputValue, self = self })
            if not (inputValue > 0) then
                if spec.currentCutState == State.CUTTING then
                    self:stopCutting(false)
                elseif spec.currentCutState == State.DELIMBING then
                    self:stopDelimbing()
                else
                    spec.currentCutState = State.IDLE
                end
                return
            end
            if spec.targetedTree == nil then
                self:beginDelimbing()
            else
                self:beginCutting()
                if spec.currentCutState ~= State.CUTTING then self:beginDelimbing() end
            end
        end

        function Class.onRollAction(self, _, inputDelta)
            self.spec_chainsaw.rollInput = self.spec_chainsaw.rollInput + inputDelta
        end
        -- the roll action's handler gone, or reshaped to take no parameters
        if variant == "rollMissing" then
            Class.onRollAction = nil
        elseif variant == "rollReshaped" then
            Class.onRollAction = function() end
        end

        -- the game's event dispatch: spec[eventName](object, ...), the class read at every call
        table.insert(StubHandTool.updaters, function(dt)
            local held = StubHandTool.heldTool
            if held ~= nil and held.spec_chainsaw ~= nil then
                local handler = variant == "missing" and Class.onUpdateTick or Class.onUpdate
                handler(held, dt)
            end
        end)

        -- the draw: the default crosshair at the centre of the view while the saw is idle
        local function draw(self)
            if self.spec_chainsaw.currentCutState == State.IDLE then StubHandTool.render(self, "crosshair") end
        end
        if variant == "drawMissing" then
            Class.onDrawOverlays = draw
        elseif variant == "drawReshaped" then
            Class.onDraw = function() draw(StubHandTool.heldTool) end
        else
            Class.onDraw = draw
        end
        table.insert(StubHandTool.drawers, function(tool)
            if tool.spec_chainsaw ~= nil then
                local handler = variant == "drawMissing" and Class.onDrawOverlays or Class.onDraw
                handler(tool)
            end
        end)

        StubHandTool.chainsawFunctions = functions
    end

    function StubHandTool.chainsaw(player, options)
        options = options or {}
        local tool = StubHandTool.makeTool(player, "chainsaw", "spec_chainsaw", options)
        local spec = tool.spec_chainsaw
        if getmetatable(player.camera) == nil then setmetatable(player.camera, { __index = _G.PlayerCamera }) end

        tool.graphicalNodeParent = StubScene.newNode("chainsawModelParent", tool.rootNode)
        tool.graphicalNode = StubScene.newNode("chainsawModel", tool.graphicalNodeParent)
        -- below the bar's centre line, as in the game's models (at the lower edge of the chain)
        spec.cutNode = StubScene.newNode("chainsawCutNode", tool.graphicalNode, 0, -0.04, 0.4)
        spec.cutGuideNode = createTransformGroup("chainsawCutGuideNode")
        link(getRootNode(), spec.cutGuideNode)
        spec.cameraRotationNode = createTransformGroup("chainsawCameraRotationNode")
        link(getRootNode(), spec.cameraRotationNode)
        spec.splitPlaneNode = createTransformGroup("chainsawSplitPlaneNode")
        link(spec.cameraRotationNode, spec.splitPlaneNode)
        setRotation(spec.splitPlaneNode, 0, 0, -math.pi * 0.5)
        spec.ringNode = StubScene.newNode("chainsawRing", spec.cameraRotationNode)
        setVisibility(spec.ringNode, false)

        spec.currentCutState = _G.ChainsawCutState.IDLE
        spec.isVerticalCut = true
        spec.currentCutRoll, spec.rollInput = 0, 0
        spec.maximumCutDiameter, spec.maximumDelimbDiameter = 1, 1
        spec.cutTime, spec.currentCutTime = options.cutTime or 600, 0
        spec.targetedTree, spec.cuttingTreeNode, spec.possibleTreeNode = nil, nil, nil

        for name, fn in pairs(StubHandTool.chainsawFunctions) do tool[name] = fn end
        tool.activateAction = function(self, a, inputValue) return _G.HandToolChainsaw.onCutAction(self, a, inputValue) end
        return tool
    end

    function StubHandTool.barRay(tool)
        local x, y, z = getWorldTranslation(tool.rootNode)
        local dx, dy, dz = StubScene.worldDirection(tool.rootNode, 0, 0, 1)
        return x, y, z, dx, dy, dz
    end

    function StubHandTool.modelDirection(tool)
        return StubScene.worldDirection(tool.graphicalNode, 0, 0, 1)
    end
)luau";

// ---- Hand Tools: the spray can (ADR-0030) --------------------------------------------------------------
// The spray can for StubHandTool, shaped after the game's HandToolSprayCan (the readable source in the SDK's
// gameSource.zip). Run after kStubHandTool (it needs kStubModelLoading only through the Virtual Hands), then call
// StubHandTool.installSprayCan(variant) BEFORE loading the Companion Mod, or any time before the map is loaded.
//
// StubHandTool.installSprayCan(variant)   defines the global HandToolSprayCan (the class the game's spray can is made of) and makes the
//   targeter honour target types: PlayerTargeter:addTargetType(key, mask, minDistance, maxDistance) and :removeTargetType(key),
//   and each update fills closestTargetsByKey[key] with the closest trunk the look ray meets, measured from the ray's origin and
//   only between the type's minimum and maximum distance (the game's tryAddTargetWithMask), as { node, x, y, z, distance }.
//   Holding a can adds its target type (trees, 0.5 m to spec.sprayDetectionDistance, 1.5 m), as onHeldStart does; putting it
//   away removes it.
//   HandToolSprayCan.onUpdate(tool, dt)   the can's update, run each tick (StubHandTool.updaters) for a held can, looked up on
//     the class at every call: reads the found tree, acts on the RISING edge of spec.activatePressed (a marker event, or the
//     game's blinking warning when the can is empty or the tree is not the player's), places a delayed marker, shakes the can
//     node while spraying, ends the spray after its duration (500 ms on g_time) and clears spec.activatePressed.
//   HandToolSprayCan.onDraw(tool)   the can's draw (StubHandTool.draw), looked up on the class at every call: renders
//     "markerTypeIcon" and, while a tree is found, "treeIcon" (both at the centre of the view); nothing for an empty can.
//   variant   how that onDraw is in this "game version": nil / "ok", "missing" (a game update renamed it to onDrawOverlays,
//     which the draw calls instead) or "reshaped" (it takes no parameters, and draws the can being held)
//   The functions the game registers on the tool: activateSpraying(_, inputValue) (spec.activatePressed = inputValue ~= 0; the
//     handler the game's ACTIVATE_HANDTOOL binding calls every frame; recorded in StubHandTool.useCalls as tool = "sprayCan"),
//     tryToAddTreeMarker, processDelayedMarker, setIsSpraying(isSpraying, force), changeTreeMarkerType(_, inputValue) and
//     getIsSprayingAllowed(shape).
//
// StubHandTool.sprayCan(player, options) -> tool   the spray can (spec_sprayCan), not yet held. options.markers: how many
//   markers it holds (default 50). tool.rootNode, and in tool.spec_sprayCan: canNode (sprayCan_vis, the game's shake node: the
//   can's visual node, whose +Z axis is the way it sprays), remainingNumMarkers, activatePressed, wasActivatePressed, isSpraying,
//   sprayStopTime, sprayDuration, sprayDetectionDistance, foundTargetTree ({ node, x, y, z, distance } or nil), treeMarkerTypeIndex,
//   treeMarkerType ({ name, title, iconFilename }, see the marker types below).
//   tool.activateAction is activateSpraying: the game's binding. g_currentMission also gets showBlinkingWarning (recorded in
//   StubHandTool.warnings as { text, ms }, text being what the game was asked to show) and a treeMarkerSystem with three marker types (CROSS, ARROW, CIRCLE)
//   when it has none, and a g_i18n (StubHandTool.texts) when there is none.
// StubHandTool.canRay(tool) -> x, y, z, dirX, dirY, dirZ   where the can sprays from and along, in the world: the origin of canNode
//   and its +Z axis
// StubHandTool.treeAt(ray, distance, radius) -> tree   a tall trunk the ray (x, y, z, dirX, dirY, dirZ in a table) meets at
//   `distance` metres along the ray (a vertical trunk: the horizontal distance of its surface is distance times the horizontal part of the
//   direction). StubHandTool.trees lists them. StubHandTool.forbid(tree): the player may not mark it (getIsSprayingAllowed).
// StubHandTool.markers   every marker event the can sent (the game's SprayCanEvent), in order, as { tool, typeIndex, shape, x, y, z,
//   hitX, hitY, hitZ } with x, y, z the look ray's origin the game passed; StubHandTool.placedMarkers: the markers the game then
//   placed on the tree (addTreeMarkerCameraBased), same fields; StubHandTool.warnings: the blinking warnings
inline constexpr const char* kStubHandToolSprayCan = R"luau(
    StubHandTool.markers, StubHandTool.placedMarkers, StubHandTool.warnings = {}, {}, {}
    StubHandTool.sprayCans, StubHandTool.forbiddenTrees = {}, {}
    StubHandTool.trees = StubHandTool.trees or {}
    local function normalize(x, y, z)
        local length = math.sqrt(x * x + y * y + z * z)
        if length < 1e-9 then return 0, 0, 1 end
        return x / length, y / length, z / length
    end

    -- ---- trees --------------------------------------------------------------------------------------------------------
    -- (the chainsaw's chunk has the same: a game with both keeps the later definition, which is the same)
    function StubHandTool.tree(x, z, radius, y0, y1)
        local node = StubScene.newNode("tree", 0, x, y0, z)
        local tree = { node = node, x = x, z = z, radius = radius, y0 = y0, y1 = y1 }
        table.insert(StubHandTool.trees, tree)
        return tree
    end

    function StubHandTool.treeAt(ray, distance, radius)
        local x, y, z, dx, dy, dz = table.unpack(ray)
        local hx, _, hz = normalize(dx, 0, dz)
        local horizontal = distance * math.sqrt(dx * dx + dz * dz)
        return StubHandTool.tree(x + hx * (horizontal + radius), z + hz * (horizontal + radius), radius, y - 10, y + 10)
    end

    function StubHandTool.forbid(tree) StubHandTool.forbiddenTrees[tree.node] = true end

    -- the closest trunk a ray meets between a minimum and a maximum distance of its origin
    local function castAtTrees(x, y, z, dx, dy, dz, minDistance, maxDistance)
        local best = nil
        for _, tree in ipairs(StubHandTool.trees) do
            local ox, oz = x - tree.x, z - tree.z
            local a = dx * dx + dz * dz
            if a > 1e-9 then
                local b = ox * dx + oz * dz
                local disc = b * b - a * (ox * ox + oz * oz - tree.radius ^ 2)
                if disc >= 0 then
                    local t = (-b - math.sqrt(disc)) / a
                    local hitY = y + dy * t
                    if t >= minDistance and t <= maxDistance and hitY >= tree.y0 and hitY <= tree.y1 and (best == nil or t < best.distance) then
                        best = { node = tree.node, x = x + dx * t, y = hitY, z = z + dz * t, distance = t }
                    end
                end
            end
        end
        return best
    end

    function StubHandTool.installSprayCan(variant)
        variant = variant or "ok"
        StubHandTool.markers, StubHandTool.placedMarkers, StubHandTool.warnings = {}, {}, {}
        StubHandTool.sprayCans, StubHandTool.forbiddenTrees = {}, {}
        local Class = {}
        _G.HandToolSprayCan = Class

        -- ---- the targeter: target types with a minimum and a maximum distance
        local PlayerTargeter = _G.PlayerTargeter
        local baseUpdate = PlayerTargeter.update
        function PlayerTargeter:addTargetType(key, mask, minDistance, maxDistance)
            self.targetedMasks = self.targetedMasks or {}
            if self.targetedMasks[key] ~= nil then return end
            self.targetedMasks[key] = { key = key, mask = mask, minDistance = minDistance or 0, maxDistance = maxDistance }
        end
        function PlayerTargeter:removeTargetType(key)
            if self.targetedMasks ~= nil then self.targetedMasks[key] = nil end
            if self.closestTargetsByKey ~= nil then self.closestTargetsByKey[key] = nil end
        end
        function PlayerTargeter:update(dt)
            baseUpdate(self, dt)
            self.closestTargetsByKey = self.closestTargetsByKey or {}
            for key, targetedMask in pairs(self.targetedMasks or {}) do
                self.closestTargetsByKey[key] = nil
                if key == Class and self.lastRayX ~= nil then
                    self.closestTargetsByKey[key] = castAtTrees(self.lastRayX, self.lastRayY, self.lastRayZ,
                        self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ, targetedMask.minDistance, targetedMask.maxDistance)
                end
            end
        end

        -- a can taken in hand targets trees, one put away stops (the game's onHeldStart)
        local baseHold, baseDrop = StubHandTool.hold, StubHandTool.drop
        function StubHandTool.hold(tool)
            baseHold(tool)
            local spec = tool.spec_sprayCan
            if spec ~= nil and tool.carryingPlayer.isOwner then
                tool.carryingPlayer.targeter:addTargetType(Class, 0, 0.5, spec.sprayDetectionDistance)
            end
        end
        function StubHandTool.drop(tool)
            if tool.spec_sprayCan ~= nil and tool.carryingPlayer ~= nil and tool.carryingPlayer.targeter ~= nil then
                tool.carryingPlayer.targeter:removeTargetType(Class)
            end
            baseDrop(tool)
        end

        -- ---- the marker system and the player's rights
        -- The game's data/maps/maps_treeMarkerTypes.xml gives each type a name (an identifier) and a title (an l10n key written
        -- "$l10n_treeMarker_cross"), an icon and a texture; the icon's filename is the one field the readable Lua reads off the
        -- type (iconFilename). That the native type keeps the title as `title`, and unresolved, is a guess (headset-only).
        local markerTypes = {
            { name = "CROSS", title = "$l10n_treeMarker_cross", iconFilename = "data/shared/treeMarker/markerTreeCross_icon.png" },
            { name = "ARROW", title = "$l10n_treeMarker_arrow", iconFilename = "data/shared/treeMarker/markerTreeArrow_icon.png" },
            { name = "CIRCLE", title = "$l10n_treeMarker_circle", iconFilename = "data/shared/treeMarker/markerTreeCircle_icon.png" },
        }
        StubHandTool.markerTypes = markerTypes
        local treeMarkerSystem = {
            getNumOfTreeMarkerTypes = function(self) return #markerTypes end,
            getTreeMarkerTypeByIndex = function(self, index) return markerTypes[index] end,
            addTreeMarkerCameraBased = function(self, shape, typeIndex, r, g, b, a, x, y, z, hitX, hitY, hitZ, isOwner)
                table.insert(StubHandTool.placedMarkers, { shape = shape, typeIndex = typeIndex, x = x, y = y, z = z,
                    hitX = hitX, hitY = hitY, hitZ = hitZ })
            end,
        }

        -- ---- the functions the game registers on the tool
        local functions = {}
        function functions.activateSpraying(self, _, inputValue)
            table.insert(StubHandTool.useCalls, { tool = "sprayCan", value = inputValue, self = self })
            self.spec_sprayCan.activatePressed = inputValue ~= 0
        end

        function functions.getIsSprayingAllowed(self, shape)
            if shape == nil or shape == 0 then return true end
            return StubHandTool.forbiddenTrees[shape] == nil
        end

        function functions.setIsSpraying(self, isSpraying, force)
            local spec = self.spec_sprayCan
            if spec.isSpraying == isSpraying then return end
            if isSpraying then
                if self:getIsHeld() then
                    spec.shakeEndTime = (_G.g_time or 0) + spec.shakeDuration
                    spec.isSpraying = true
                end
            else
                spec.sprayStopTime = 0
                spec.isSpraying = false
            end
        end

        function functions.processDelayedMarker(self)
            local spec = self.spec_sprayCan
            local marker = spec.delayedMarker
            if marker == nil then return end
            if entityExists(marker.shape) then
                g_currentMission.treeMarkerSystem:addTreeMarkerCameraBased(marker.shape, marker.typeIndex, 1, 1, 1, 1,
                    marker.x, marker.y, marker.z, marker.hitX, marker.hitY, marker.hitZ, true)
            end
            spec.delayedMarker = nil
        end

        function functions.tryToAddTreeMarker(self, typeIndex, shape, x, y, z, hitX, hitY, hitZ)
            local spec = self.spec_sprayCan
            self:setIsSpraying(true, false)
            spec.sprayStopTime = (_G.g_time or 0) + spec.sprayDuration
            spec.remainingNumMarkers = spec.remainingNumMarkers - 1
            if shape ~= nil and self:getIsSprayingAllowed(shape) then
                spec.delayedMarker = { time = (_G.g_time or 0), shape = shape, typeIndex = typeIndex,
                    x = x, y = y, z = z, hitX = hitX, hitY = hitY, hitZ = hitZ }
            end
        end

        function functions.changeTreeMarkerType(self, _, inputValue)
            local spec = self.spec_sprayCan
            spec.treeMarkerTypeIndex = spec.treeMarkerTypeIndex + 1
            if spec.treeMarkerTypeIndex > g_currentMission.treeMarkerSystem:getNumOfTreeMarkerTypes() then spec.treeMarkerTypeIndex = 1 end
            spec.treeMarkerType = g_currentMission.treeMarkerSystem:getTreeMarkerTypeByIndex(spec.treeMarkerTypeIndex)
        end

        -- ---- the specialization's own update: on the class, read at every call (the game's event dispatch)
        function Class.onUpdate(self, dt)
            local spec = self.spec_sprayCan
            local player = self:getCarryingPlayer()
            local allowInput = player ~= nil and player.isOwner or false
            local now = _G.g_time or 0

            if allowInput then
                local targetTree = player.targeter.closestTargetsByKey[Class]
                spec.foundTargetTree = targetTree

                if spec.activatePressed then
                    if not spec.wasActivatePressed and not spec.isSpraying then
                        local mission = g_currentMission
                        if spec.remainingNumMarkers > 0 then
                            local x, y, z = player:getLookRay()
                            local typeIndex = spec.treeMarkerTypeIndex
                            local shape, hitX, hitY, hitZ = nil, nil, nil, nil
                            if targetTree ~= nil then
                                shape = targetTree.node
                                hitX, hitY, hitZ = targetTree.x, targetTree.y, targetTree.z
                            end
                            if self:getIsSprayingAllowed(shape) then
                                -- the game's SprayCanEvent, sent to the server (this client) and run there
                                table.insert(StubHandTool.markers, { tool = self, typeIndex = typeIndex, shape = shape,
                                    x = x, y = y, z = z, hitX = hitX, hitY = hitY, hitZ = hitZ })
                                self:tryToAddTreeMarker(typeIndex, shape, x, y, z, hitX, hitY, hitZ)
                            else
                                mission:showBlinkingWarning("warning_youAreNotAllowedToMarkThisTree", 2000)
                            end
                        else
                            mission:showBlinkingWarning("warning_sprayCanIsEmpty", 2000)
                        end
                        spec.wasActivatePressed = true
                    end
                end
            end

            if spec.delayedMarker ~= nil and spec.delayedMarker.time <= now then self:processDelayedMarker() end

            if spec.isSpraying then
                if allowInput then
                    -- the shake: the can node moves a few centimetres while the can sprays
                    local x, y, z = spec.originalPos[1], spec.originalPos[2], spec.originalPos[3]
                    local timeLeft = math.max(0, spec.shakeEndTime - now)
                    local factor = 1 - (timeLeft / spec.shakeDuration)
                    local animValue = spec.numShakes * factor
                    x = x + math.sin(animValue * math.pi) * 0.01
                    y = y + math.sin(animValue * math.pi) * 0.05
                    z = z + math.sin(animValue * math.pi) * 0.01
                    setTranslation(spec.canNode, x, y, z)
                end
                if now > spec.sprayStopTime then self:setIsSpraying(false, false) end
            end

            if not spec.activatePressed then spec.wasActivatePressed = false end
            spec.activatePressed = false
        end

        -- the draw (the game's HandToolSprayCan:onDraw): the marker type icon and, with a tree found, the tree icon, both at the
        -- centre of the view; nothing for a can that has no marker type or is empty
        local function draw(self)
            local spec = self.spec_sprayCan
            if spec.treeMarkerType == nil or spec.remainingNumMarkers == 0 then return end
            StubHandTool.render(self, "markerTypeIcon")
            if spec.foundTargetTree ~= nil then StubHandTool.render(self, "treeIcon") end
        end
        if variant == "missing" then
            Class.onDrawOverlays = draw
        elseif variant == "reshaped" then
            Class.onDraw = function() draw(StubHandTool.heldTool) end
        else
            Class.onDraw = draw
        end
        StubHandTool.sprayCanDrawVariant = variant

        -- the game's event dispatch: spec[eventName](object, ...), the class read at every call
        if not StubHandTool.sprayCanUpdaterAdded then
            StubHandTool.sprayCanUpdaterAdded = true
            table.insert(StubHandTool.updaters, function(dt)
                local held = StubHandTool.heldTool
                if held ~= nil and held.spec_sprayCan ~= nil then _G.HandToolSprayCan.onUpdate(held, dt) end
            end)
            table.insert(StubHandTool.drawers, function(tool)
                if tool.spec_sprayCan == nil then return end
                local class = _G.HandToolSprayCan
                local handler = StubHandTool.sprayCanDrawVariant == "missing" and class.onDrawOverlays or class.onDraw
                handler(tool)
            end)
        end

        StubHandTool.sprayCanFunctions, StubHandTool.treeMarkerSystem = functions, treeMarkerSystem
    end

    function StubHandTool.sprayCan(player, options)
        options = options or {}
        local tool = StubHandTool.makeTool(player, "sprayCan", "spec_sprayCan", options)
        local spec = tool.spec_sprayCan
        -- the can stands upright in its i3d: the visual node, a little up and forward of the root, sprays along its +Z
        spec.canNode = StubScene.newNode("sprayCan_vis", tool.rootNode, 0, 0.12, 0.06)
        spec.originalPos = { getTranslation(spec.canNode) }
        spec.remainingNumMarkers = options.markers or 50
        spec.sprayDetectionDistance = 1.5
        spec.sprayDuration, spec.sprayStopTime = 500, 0
        spec.wasActivatePressed, spec.activatePressed, spec.isSpraying = false, false, false
        spec.numShakes, spec.shakeDuration, spec.shakeEndTime = 3, 200, 0
        spec.delayedMarker, spec.foundTargetTree = nil, nil

        -- what the game's mission offers the can
        local mission = _G.g_currentMission
        mission.treeMarkerSystem = mission.treeMarkerSystem or StubHandTool.treeMarkerSystem
        if mission.showBlinkingWarning == nil then
            mission.showBlinkingWarning = function(self, text, ms) table.insert(StubHandTool.warnings, { text = text, ms = ms }) end
        end
        spec.treeMarkerTypeIndex = 1
        spec.treeMarkerType = mission.treeMarkerSystem:getTreeMarkerTypeByIndex(1)

        -- the game's texts: the marker types' titles (the game's l10n) and the Companion Mod's own (its modDesc.xml l10n). Added
        -- when the test has no i18n of its own; hasText/getText as the game's I18N has them (a missing text reads "Missing ...").
        if _G.g_i18n == nil then
            local texts = {
                treeMarker_cross = "Cross", treeMarker_arrow = "Arrow", treeMarker_circle = "Circle",
                vrHandTools_markerType = "Marker type: %s",
            }
            StubHandTool.texts = texts
            _G.g_i18n = {
                hasText = function(self, name) return texts[name] ~= nil end,
                getText = function(self, name) return texts[name] or ("Missing '" .. tostring(name) .. "' in l10n") end,
            }
        end

        for name, fn in pairs(StubHandTool.sprayCanFunctions) do tool[name] = fn end
        tool.activateAction = tool.activateSpraying
        table.insert(StubHandTool.sprayCans, tool)
        return tool
    end

    function StubHandTool.canRay(tool)
        local node = tool.spec_sprayCan.canNode
        local x, y, z = getWorldTranslation(node)
        local dx, dy, dz = StubScene.worldDirection(node, 0, 0, 1)
        return x, y, z, dx, dy, dz
    end
)luau";

// ---- Hand Tools: the horse brush (ADR-0030) ------------------------------------------------------------
// The horse brush for StubHandTool, shaped after the game's HandToolHorseBrush (the readable source in the SDK's gameSource.zip). Run
// after kStubHandTool, then call StubHandTool.installHorseBrush(handlerVariant, lookupVariant) BEFORE loading the Companion Mod, or any
// time before the map is loaded. A game with the spray can too: install the horse brush AFTER the spray can (both fill the targeter).
//
// StubHandTool.installHorseBrush(handlerVariant, lookupVariant)   defines the global HandToolHorseBrush (the class the game's horse
//   brush is made of), AnimalCleanEvent and the game's farm, husbandry and network objects the brush reads, and makes the targeter
//   honour the animal target type (PlayerTargeter:addTargetType / :removeTargetType / :getClosestTargetedNodeFromType when the spray
//   can has not added them): each update fills closestTargetsByKey[HandToolHorseBrush] with the closest animal the look ray meets,
//   measured from the ray's origin and only between the type's minimum and maximum distance (the game's tryAddTargetWithMask), as
//   { node, x, y, z, distance }. Holding a brush adds its target type (animals, 0.5 m to 3 m, as onHeldStart does); putting it away
//   removes it.
//   handlerVariant   how the use handler HandToolHorseBrush.onBrushAction is in this "game version": nil / "ok", "missing" (a game
//                    update renamed it to onBrushUse, which the game's binding calls instead) or "reshaped" (it takes no parameters)
//   lookupVariant    how the animal lookup HandToolHorseBrush.getHusbandryAndClusterFromNode(player, node) is: nil / "ok", "missing"
//                    (renamed to getAnimalFromNode, which the brush's update calls) or "reshaped" (it takes only the node)
//   HandToolHorseBrush.onUpdate(tool, dt)   the brush's update, run each tick (StubHandTool.updaters) for a held brush, looked up on
//     the class at every call: asks the lookup (on the class) for the animal and cluster of the targeter's closest animal, sets the
//     action event's text ("Clean <name>") and active state to match, and while the brush is cleaning an animal (spec.isCleaning with
//     one targeted) sways the brush node, plays the cleaning sound (spec.cleaningSoundPlaying) and sends an AnimalCleanEvent every
//     half second (+5 dirt cleaned, the game's cleaningDeltaPerMs of 0.01).
//   HandToolHorseBrush.onDraw(tool)   the brush's draw (StubHandTool.draw), looked up on the class at every call: renders
//     "brushCrosshair" while an animal is targeted, "defaultCrosshair" otherwise (both at the centre of the view)
//   HandToolHorseBrush.onBrushAction(tool, _, inputValue)   the ACTIVATE_HANDTOOL handler, on the class ONLY (not on the tool): the
//     game's binding calls it with the tool as self; held (spec.isCleaning = inputValue > 0); recorded in StubHandTool.useCalls as
//     tool = "horseBrush". The binding itself, tool.activateAction, calls whatever the variant calls it.
//   HandToolHorseBrush.getHusbandryAndClusterFromNode(player, node) -> husbandry, cluster   nil, nil without a valid node, an
//     animal, an animal of a farm the player may access (the game's accessHandler:canFarmAccess)
//
// StubHandTool.horseBrush(player, options) -> tool   the horse brush (spec_horseBrush), not yet held. tool.rootNode, and in
//   tool.spec_horseBrush: brushNode (`brush`, whose -Y axis runs toward the bristles), originalPos, animalDetectionDistance (3),
//   isCleaning, targetedHusbandry, targetedHusbandryId, targetedClusterId, deltaSum, brushText ("Clean %s"), cleaningSoundPlaying.
//   The player gets farmId 1 when it has none.
// StubHandTool.bristleRay(tool) -> x, y, z, dirX, dirY, dirZ   where the bristles are and the way they point, in the world: the origin
//   of the brush node and its -Y axis
// StubHandTool.animalAt(ray, distance, radius, options) -> animal   a spherical animal the ray (x, y, z, dirX, dirY, dirZ in a table)
//   meets `distance` metres along it. options: name ("Silver"), farmId (1: the player's), dirt (50). animal = { node, husbandry,
//   cluster, ... }, cluster.dirt the animal's dirt; StubHandTool.animals lists them
// StubHandTool.inputHelp(tool) -> text   what the input help offers for the brush now: the action event's text while it is active
//   (e.g. "Clean Silver"), "" while it is not. Together with StubHandTool.actionEvents[tool.spec_horseBrush.activateActionEventId]
// StubHandTool.cleanEvents   every clean event the brush sent (the game's AnimalCleanEvent), in order, as { husbandry, clusterId, delta }
inline constexpr const char* kStubHandToolHorseBrush = R"luau(
    StubHandTool.animals, StubHandTool.cleanEvents, StubHandTool.actionEvents = {}, {}, {}
    local function normalize(x, y, z)
        local length = math.sqrt(x * x + y * y + z * z)
        if length < 1e-9 then return 0, 0, 1 end
        return x / length, y / length, z / length
    end

    -- the closest animal (a sphere) a ray meets between a minimum and a maximum distance of its origin
    local function castAtAnimals(x, y, z, dx, dy, dz, minDistance, maxDistance)
        local best = nil
        for _, animal in ipairs(StubHandTool.animals) do
            local cx, cy, cz = getWorldTranslation(animal.node)
            local ox, oy, oz = x - cx, y - cy, z - cz
            local b = ox * dx + oy * dy + oz * dz
            local disc = b * b - (ox * ox + oy * oy + oz * oz - animal.radius ^ 2)
            if disc >= 0 then
                local t = -b - math.sqrt(disc)
                if t >= minDistance and t <= maxDistance and (best == nil or t < best.distance) then
                    best = { node = animal.node, x = x + dx * t, y = y + dy * t, z = z + dz * t, distance = t }
                end
            end
        end
        return best
    end

    -- ---- animals: the game's husbandry, cluster and farm objects the brush reads ---------------------------------------------
    function StubHandTool.animalAt(ray, distance, radius, options)
        options = options or {}
        local x, y, z, dx, dy, dz = table.unpack(ray)
        dx, dy, dz = normalize(dx, dy, dz)
        local reach = distance + radius
        local index = #StubHandTool.animals + 1
        local node = StubScene.newNode("animal" .. index, 0, x + dx * reach, y + dy * reach, z + dz * reach)
        local cluster = { id = index, name = options.name or "Silver", dirt = options.dirt or 50 }
        function cluster:getName() return self.name end
        function cluster:changeDirt(delta) self.dirt = math.max(0, math.min(100, self.dirt + delta)) end
        local husbandry = { networkId = 100 + index, farmId = options.farmId or 1, clusters = { [cluster.id] = cluster } }
        function husbandry:getClusterById(id) return self.clusters[id] end
        local animal = { node = node, radius = radius, husbandry = husbandry, cluster = cluster, husbandryId = 200 + index, animalId = index }
        table.insert(StubHandTool.animals, animal)
        return animal
    end

    local function installTheGamesFarmAndNetwork()
        _G.getAnimalFromCollisionNode = function(node)
            for _, animal in ipairs(StubHandTool.animals) do
                if animal.node == node then return animal.husbandryId, animal.animalId end
            end
            return nil, nil
        end
        local mission = _G.g_currentMission or {}
        _G.g_currentMission = mission
        mission.husbandrySystem = {
            getClusterHusbandryById = function(self, husbandryId)
                for _, animal in ipairs(StubHandTool.animals) do
                    if animal.husbandryId == husbandryId then
                        return {
                            getPlaceable = function() return animal.husbandry end,
                            getClusterByAnimalId = function(_, animalId) return animal.cluster end,
                        }
                    end
                end
                return nil
            end,
        }
        mission.accessHandler = { canFarmAccess = function(self, farmId, husbandry) return farmId == husbandry.farmId end }
        _G.NetworkUtil = {
            getObjectId = function(object) return object.networkId end,
            getObject = function(id)
                for _, animal in ipairs(StubHandTool.animals) do
                    if animal.husbandry.networkId == id then return animal.husbandry end
                end
                return nil
            end,
        }
        _G.AnimalCleanEvent = {
            new = function(husbandry, clusterId, delta)
                return { husbandry = husbandry, clusterId = clusterId, delta = math.abs(math.floor(delta)) }
            end,
        }
        -- the server connection of a client that is also the server: the event is kept and run at once
        _G.g_client = {
            getServerConnection = function()
                return {
                    sendEvent = function(_, event)
                        table.insert(StubHandTool.cleanEvents, event)
                        local cluster = event.husbandry:getClusterById(event.clusterId)
                        if cluster ~= nil and cluster.changeDirt ~= nil then cluster:changeDirt(-event.delta) end
                    end,
                }
            end,
        }
    end

    -- the game's input binding, for the brush's action event only: the text and active state the brush sets
    local inputBinding = {
        setActionEventText = function(_, id, text) StubHandTool.actionEvents[id].text = text end,
        setActionEventActive = function(_, id, active) StubHandTool.actionEvents[id].active = active end,
    }
    function StubHandTool.inputHelp(tool)
        local event = StubHandTool.actionEvents[tool.spec_horseBrush.activateActionEventId]
        return event.active and event.text or ""
    end

    function StubHandTool.installHorseBrush(handlerVariant, lookupVariant)
        handlerVariant, lookupVariant = handlerVariant or "ok", lookupVariant or "ok"
        StubHandTool.animals, StubHandTool.cleanEvents, StubHandTool.actionEvents = {}, {}, {}
        StubHandTool.horseBrushes = {}
        StubHandTool.horseBrushHandlerName = handlerVariant == "missing" and "onBrushUse" or "onBrushAction"
        StubHandTool.horseBrushLookupName = lookupVariant == "missing" and "getAnimalFromNode" or "getHusbandryAndClusterFromNode"
        StubHandTool.horseBrushLookupVariant = lookupVariant
        local Class = {}
        _G.HandToolHorseBrush = Class
        installTheGamesFarmAndNetwork()

        -- ---- the targeter: target types with a minimum and a maximum distance (the spray can's chunk may have added them)
        local PlayerTargeter = _G.PlayerTargeter
        if PlayerTargeter.addTargetType == nil then
            function PlayerTargeter:addTargetType(key, mask, minDistance, maxDistance)
                self.targetedMasks = self.targetedMasks or {}
                if self.targetedMasks[key] ~= nil then return end
                self.targetedMasks[key] = { key = key, mask = mask, minDistance = minDistance or 0, maxDistance = maxDistance }
            end
            function PlayerTargeter:removeTargetType(key)
                if self.targetedMasks ~= nil then self.targetedMasks[key] = nil end
                if self.closestTargetsByKey ~= nil then self.closestTargetsByKey[key] = nil end
            end
        end
        if PlayerTargeter.getClosestTargetedNodeFromType == nil then
            function PlayerTargeter:getClosestTargetedNodeFromType(key)
                local target = (self.closestTargetsByKey or {})[key]
                return target ~= nil and target.node or nil
            end
        end
        local previousUpdate = PlayerTargeter.update
        function PlayerTargeter:update(dt)
            previousUpdate(self, dt)
            self.closestTargetsByKey = self.closestTargetsByKey or {}
            local targetedMask = (self.targetedMasks or {})[Class]
            self.closestTargetsByKey[Class] = nil
            if targetedMask ~= nil and self.lastRayX ~= nil then
                self.closestTargetsByKey[Class] = castAtAnimals(self.lastRayX, self.lastRayY, self.lastRayZ,
                    self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ, targetedMask.minDistance, targetedMask.maxDistance)
            end
        end

        -- a brush taken in hand targets animals, one put away stops (the game's onHeldStart)
        local baseHold, baseDrop = StubHandTool.hold, StubHandTool.drop
        function StubHandTool.hold(tool)
            baseHold(tool)
            local spec = tool.spec_horseBrush
            if spec ~= nil and tool.carryingPlayer.isOwner then
                tool.carryingPlayer.targeter:addTargetType(Class, 0, 0.5, spec.animalDetectionDistance)
            end
        end
        function StubHandTool.drop(tool)
            if tool.spec_horseBrush ~= nil and tool.carryingPlayer ~= nil and tool.carryingPlayer.targeter ~= nil then
                tool.carryingPlayer.targeter:removeTargetType(Class)
            end
            baseDrop(tool)
        end

        -- ---- the lookup: on the class, called through it by the brush's update
        local function lookup(player, node)
            if node == nil or not entityExists(node) then return nil, nil end
            local husbandryId, animalId = getAnimalFromCollisionNode(node)
            if husbandryId ~= nil and husbandryId ~= 0 then
                local mission = g_currentMission
                local clusterHusbandry = mission.husbandrySystem:getClusterHusbandryById(husbandryId)
                if clusterHusbandry ~= nil then
                    local husbandry = clusterHusbandry:getPlaceable()
                    local cluster = clusterHusbandry:getClusterByAnimalId(animalId)
                    if cluster ~= nil and mission.accessHandler:canFarmAccess(player.farmId, husbandry) then
                        if cluster.changeDirt ~= nil and cluster.getName ~= nil then return husbandry, cluster end
                    end
                end
            end
            return nil, nil
        end
        if lookupVariant == "reshaped" then
            Class.getHusbandryAndClusterFromNode = function(node) return lookup(StubHandTool.player, node) end
        else
            Class[StubHandTool.horseBrushLookupName] = lookup
        end
        local function askTheLookup(player, node)
            if lookupVariant == "reshaped" then return Class.getHusbandryAndClusterFromNode(node) end
            return Class[StubHandTool.horseBrushLookupName](player, node)
        end

        -- ---- the use handler: on the class only, held
        local function setCleaning(self, inputValue)
            table.insert(StubHandTool.useCalls, { tool = "horseBrush", value = inputValue, self = self })
            self.spec_horseBrush.isCleaning = inputValue > 0
        end
        if handlerVariant == "reshaped" then
            Class.onBrushAction = function() setCleaning(StubHandTool.heldTool, 1) end
        else
            Class[StubHandTool.horseBrushHandlerName] = function(self, _, inputValue) setCleaning(self, inputValue) end
        end

        -- ---- the specialization's own update, on the class and read at every call (the game's event dispatch)
        function Class.onUpdate(self, dt)
            local player = self:getCarryingPlayer()
            if player == nil then return end
            local spec = self.spec_horseBrush
            spec.targetedHusbandryId, spec.targetedClusterId, spec.targetedHusbandry = nil, nil, nil

            if player.isOwner then
                local targetNode = player.targeter:getClosestTargetedNodeFromType(Class)
                local husbandry, cluster = askTheLookup(player, targetNode)
                spec.targetedHusbandry = husbandry

                if husbandry ~= nil then
                    inputBinding:setActionEventText(spec.activateActionEventId, string.format(spec.brushText, cluster:getName()))
                end
                inputBinding:setActionEventActive(spec.activateActionEventId, husbandry ~= nil)

                if husbandry ~= nil and spec.isCleaning then
                    spec.targetedHusbandryId = NetworkUtil.getObjectId(husbandry)
                    spec.targetedClusterId = cluster.id
                    if spec.brushNode ~= nil then
                        local x, y, z = spec.originalPos[1], spec.originalPos[2], spec.originalPos[3]
                        local now = _G.g_time or 0
                        x = x + math.sin(now * 0.25 * math.pi) * 0.005
                        y = y + math.sin(now * 0.25 * math.pi) * 0.001
                        z = z + math.sin(now * 0.25 * math.pi) * 0.05
                        setTranslation(spec.brushNode, x, y, z)
                    end
                end
            end

            spec.cleaningSoundPlaying = spec.targetedHusbandryId ~= nil

            if spec.targetedHusbandryId ~= nil then
                local husbandry = NetworkUtil.getObject(spec.targetedHusbandryId)
                if husbandry ~= nil then
                    spec.deltaSum = spec.deltaSum + spec.cleaningDeltaPerMs * dt
                    if spec.deltaSum > 5 then
                        local delta = math.floor(spec.deltaSum)
                        spec.deltaSum = spec.deltaSum - delta
                        if husbandry:getClusterById(spec.targetedClusterId) ~= nil then
                            g_client:getServerConnection():sendEvent(AnimalCleanEvent.new(husbandry, spec.targetedClusterId, delta))
                        end
                    end
                end
            else
                spec.deltaSum = 0
            end
        end

        -- the draw (the game's HandToolHorseBrush:onDraw): the crosshairs at the centre of the view
        function Class.onDraw(self)
            local spec = self.spec_horseBrush
            StubHandTool.render(self, spec.targetedHusbandry ~= nil and "brushCrosshair" or "defaultCrosshair")
        end

        -- the game's event dispatch: spec[eventName](object, ...), the class read at every call
        if not StubHandTool.horseBrushUpdaterAdded then
            StubHandTool.horseBrushUpdaterAdded = true
            table.insert(StubHandTool.updaters, function(dt)
                local held = StubHandTool.heldTool
                if held ~= nil and held.spec_horseBrush ~= nil then _G.HandToolHorseBrush.onUpdate(held, dt) end
            end)
            table.insert(StubHandTool.drawers, function(tool)
                if tool.spec_horseBrush ~= nil then _G.HandToolHorseBrush.onDraw(tool) end
            end)
        end
    end

    function StubHandTool.horseBrush(player, options)
        options = options or {}
        local tool = StubHandTool.makeTool(player, "horseBrush", "spec_horseBrush", options)
        local spec = tool.spec_horseBrush
        player.farmId = player.farmId or 1
        spec.animalDetectionDistance = 3
        -- the brush node (`brush`) is the root's origin; the bristles (`fibers`, 4 cm along its -Y) are what touches the animal
        spec.brushNode = StubScene.newNode("brush", tool.rootNode, 0, 0, 0)
        spec.originalPos = { getTranslation(spec.brushNode) }
        spec.cleaningDeltaPerMs = 10 / 1000
        spec.deltaSum = 0
        spec.brushText = "Clean %s"
        spec.cleaningSoundPlaying = false
        -- the action event the game registers when the tool is held: no text, not active
        local eventId = #StubHandTool.horseBrushes + 1
        StubHandTool.actionEvents[eventId] = { text = "", active = false }
        spec.activateActionEventId = eventId
        -- the game's own binding calls the handler off the class, with the tool as self
        tool.activateAction = function(self, a, inputValue) return _G.HandToolHorseBrush[StubHandTool.horseBrushHandlerName](self, a, inputValue) end
        table.insert(StubHandTool.horseBrushes, tool)
        return tool
    end

    function StubHandTool.bristleRay(tool)
        local node = tool.spec_horseBrush.brushNode
        local x, y, z = getWorldTranslation(node)
        local dx, dy, dz = StubScene.worldDirection(node, 0, -1, 0)
        return x, y, z, dx, dy, dz
    end
)luau";

// ---- Hand Tools: the fishing rod (ADR-0031) ------------------------------------------------------------
// The Highlands Fishing DLC's fishing rod for StubHandTool. The DLC's scripts cannot be read: this is shaped after the probe dump
// of the real rod (#125: its specializations, class functions, action events, nodes and spec fields, and how spec.state changed
// while fishing) and after play, not after source. Run after kStubHandTool, then call StubHandTool.installFishingRod(variant) BEFORE
// loading the Companion Mod, or any time before the map is loaded. A game without the DLC simply never calls it.
//
// StubHandTool.installFishingRod(variant)   makes the DLC's specialization class. It has NO global name (the DLC's scripts run in
//   their own environment): it is StubHandTool.fishingRodClass, and the game reaches it only through each rod's
//   tool.specializations, next to its name "pdlc_highlandsFishingPack.fishingRod" in tool.specializationNames (with the storable
//   specialization before it, as in the dump). Every handler is on the class only, never on the tool, called with the rod as self.
//   variant   how the class's ACTIVATE_HANDTOOL handler is in this "DLC version": nil / "ok" (onActivateAction(rod, actionName,
//             inputValue)), "missing" (a DLC update renamed it to onPrimaryAction, which the game's binding calls instead) or
//             "reshaped" (onActivateAction takes no parameters, and acts as a press on the rod being held)
//   Class functions (the dump's): onActivateAction, onActivateSecondaryAction, onUpdate, onDraw, onHeldStart, onHeldEnd,
//     onRegisterActionEvents, onInputModeChanged, setState, getBobberPosition, getTipPosition, getAngleScale, getRodBendFactor,
//     getCurrentItem, ... The type's functions (getBobberPosition, getTipPosition, getAngleScale, getRodBendFactor, getCurrentItem,
//     setState, ...) are also copied onto each rod, as the game does; the action handlers, onUpdate and onDraw are not.
//   The class's onUpdate runs each StubHandTool.tick for the held rod, and its onDraw in StubHandTool.draw, both looked up on the
//   class at every call (the game's event dispatch).
//
// StubHandTool.fishingRod(player, options) -> tool   a rod, not yet held. Its spec table is tool["spec_pdlc_highlandsFishingPack.fishingRod"]
//   (also StubHandTool.rodSpec(tool)); tool.spec_storable is the storable specialization's. In the spec, as in the dump:
//     state          the current state: ONE OF spec.states (11 tables), recognised only by its keys (each has `fishingRod`, the rod).
//                    nil when the game attaches the rod: the rod's first update while held sets it to idle (the headset showed
//                    the real rod's state missing at the attach, 2026-10-18):
//       idle        { fishingRod }                                   (1 key)
//       charging     angleScale (0 to 1 while held), angleScaleIncreasePerMs (0.0007), inputValue, castingChargeOverlay, ... (10 keys)
//       waiting      biteTime, biteTimeMin (10000), biteTimeMax (25000), biteHintTimeThreshold, waitForBiteOverlay, ... (10 keys)
//       bite         catchTime, duration (880, the hook window), minCatchTimeThreshold (700), glyphButtonText ("Hook fish"),
//                    hookFishOverlay, ... (12 keys)
//       reeling      the minigame's layout (48 keys; the #140 headset dump named them): posX, posY, width, height, fishYMin,
//                    fishYMax, cursorPos, targetLineXStart/End, hookLineX, hookStatusLineXStart/End, progressBarOverlay,
//                    fishTargetBarOverlay, fishOverlay, hookOverlay, ... None of them tells the state by itself; the reeling
//                    shows in rodBendFactor and fishingLine instead.
//       caught       { fishingRod, angleScaleDecreasePerMs, bendResetRate }  (3 keys)
//       (and five more states the dump never showed the rod in)
//     states         the 11 state tables
//     fishingLine    { isInitialized (true from the cast until the line is in), ropeLength, numActiveNodes, minLength (2),
//                    maxLength (40), visibility, ... }
//     rodBendFactor  0 unless reeling: while reeling, the fish's pull (options.fishPull, 0.5; StubHandTool.setFishPull) while the
//                    reel is held, a third of it while it is not
//     angleScale, angleMax (2.27), bendMax (0.2), item (the catch, nil until one), the nodes bobber, bobberPhysicsHelper, tipLine,
//     tipLinePhysicsHelper, handle (handleJoint), rotor (yokeJoint), spool (spoolJoint), bail (bailJoint), mesh,
//     activateActionId / activateActionSecondaryId
//   The nodes: fishingRodRoot -> fishingRod -> bodyBoneJoint -> handleJoint, yokeJoint -> bailJoint, spoolJoint, segment01Joint
//   -> tipLine (the rod's tip, 1.9 m along the root's +X; the reel below, at -Y); fishingRod -> body, handNode (the game's third person hold, a little up
//   the handle from the root), firstPersonNode; fishingRodRoot -> ropeReferenceFrame -> rope_10_100 ...; the bobber is linked to
//   the world. tool.handNode is the handNode (the stub's own guess: the i3d is in the packed DLC).
//   tool.actionEvents: ACTIVATE_HANDTOOL -> onActivateAction and ACTIVATE_HANDTOOL_SECONDARY -> onActivateSecondaryAction, both
//   on the class, both firing on press and release (not every frame). tool.activateAction(self, _, inputValue) and
//   tool.activateSecondaryAction(self, _, inputValue) are the game's bindings (X on a pad, the mouse): they call the event's
//   callback on those edges only.
//   options: fishPull (0.5)
//
// What the rod does when its ACTIVATE_HANDTOOL handler is called (from play: hold to charge, let go to throw; the same action hooks
// and reels). Each call is in StubHandTool.useCalls as { tool = "fishingRod", action = "ACTIVATE_HANDTOOL", value, self }:
//   idle, pressed      -> charging; each update the charge grows (angleScale += 0.0007 per ms, to 1)
//   charging, released -> the cast: the line goes out (isInitialized, ropeLength 2 + 20 * angleScale), the bobber lands ropeLength
//                         along the player's look ray as the game answers it now (Player:getLookRay: whether the real DLC aims
//                         that way is not known), and the rod waits. Each cast is in StubHandTool.casts as
//                         { angleScale, ray = { x, y, z, dirX, dirY, dirZ }, bobber = { x, y, z } }
//   waiting            -> a bite comes only when the test says: StubHandTool.biteRod(tool) (not by the clock)
//   bite, pressed      -> reeling (hooked); not pressed within the hook window (880 ms of updates) -> waiting again (got away)
//   reeling            -> while the reel is held (pressed, not yet released) each update takes 10 m per second off the line;
//                         at minLength the fish is caught: item set, the line in (isInitialized false), state caught
//   caught, pressed    -> idle (the catch dialog is closed), item nil
// The secondary handler only records itself, as { tool = "fishingRod", action = "ACTIVATE_HANDTOOL_SECONDARY", value, self }:
// what it does is not known yet.
// The draw (onDraw, at the centre of the view): "castingChargeOverlay" while charging, "waitForBiteOverlay" while waiting and
// "hookFishOverlay" in the hook window; nothing otherwise.
//
// StubHandTool.rodState(tool) -> "idle" | "charging" | "waiting" | "bite" | "reeling" | "caught"   what the DLC is doing (the stub
//   knows its own state tables; the Companion Mod has only the keys)
// StubHandTool.setRodState(tool, name)   puts the rod straight into a state, with the line and the bend that go with it
// StubHandTool.biteRod(tool)   a fish bites (waiting -> bite)
// StubHandTool.setFishPull(tool, pull)   how hard the fish pulls while reeling (0..1)
// StubHandTool.rodFaceRay(tool) -> x, y, z, dirX, dirY, dirZ   the rod's root and the way its face points, in the world: square
//   to the rod, toward the side its reel hangs on (the way the tip swings in a cast)
// StubHandTool.pressSecondaryBinding(tool, inputValue)   the game's own ACTIVATE_HANDTOOL_SECONDARY binding
// StubHandTool.fishingRods   every rod made; StubHandTool.casts   every cast
inline constexpr const char* kStubHandToolFishingRod = R"luau(
    local SPEC_NAME = "pdlc_highlandsFishingPack.fishingRod"
    local SPEC = "spec_" .. SPEC_NAME
    StubHandTool.fishingRods, StubHandTool.casts = {}, {}
    local stateNames = {}  -- a state table -> its name (the stub's own knowledge; the tables carry no name)

    -- What reached the screen in a draw: { what (the overlay's file, or the text), x, y, width (0 for text) }.
    StubHandTool.screen = {}
    -- The game's Overlay (dataS/scripts/gui/base/Overlay.lua), as far as the rod's draw uses it: Overlay.new(filename, x, y, width,
    -- height), and overlay:render(), looked up on the class at every call, which puts it on the screen at x + offsetX, y + offsetY.
    -- Defined here unless another stub has.
    if _G.Overlay == nil then
        local Overlay = {}
        local Overlay_mt = { __index = Overlay }
        function Overlay.new(filename, x, y, width, height)
            return setmetatable({ filename = filename, x = x, y = y, width = width, height = height, offsetX = 0, offsetY = 0,
                                  visible = true }, Overlay_mt)
        end
        function Overlay:render()
            if not self.visible then return end
            table.insert(StubHandTool.screen, { what = self.filename, x = self.x + self.offsetX, y = self.y + self.offsetY, width = self.width })
        end
        function Overlay:setPosition(x, y) self.x, self.y = x or self.x, y or self.y end
        _G.Overlay = Overlay
    end
    -- The game's natives for text and filled rectangles: the engine resolves natives at load, so a mod cannot wrap them (locals
    -- here, not globals). Each notes what reached the screen, a rectangle by the name the stub gives it.
    local function renderTextNative(x, y, size, text) table.insert(StubHandTool.screen, { what = tostring(text), x = x, y = y, width = 0 }) end
    local function drawRectNative(name, x, y, width, height) table.insert(StubHandTool.screen, { what = name, x = x, y = y, width = width }) end
    local reelHeld = {}    -- a rod -> its reel is held (pressed and not yet released)

    function StubHandTool.rodSpec(tool) return tool[SPEC] end
    function StubHandTool.rodState(tool) return stateNames[tool[SPEC].state] end

    local function normalize(x, y, z)
        local length = math.sqrt(x * x + y * y + z * z)
        if length < 1e-9 then return 0, 0, 1 end
        return x / length, y / length, z / length
    end

    -- the 11 state tables of a rod, by the stub's names; each has the keys the dump showed for it
    local function makeStates(tool)
        local overlay = function() return { width = 0.08, height = 0.08, visible = true } end  -- one the stub's draw does not render
        local states = {
            idle = { fishingRod = tool },
            charging = { fishingRod = tool, angleScale = 0, angleScaleDecreasePerMs = 0.006, angleScaleIncreasePerMs = 0.0007,
                         castingAngle = 0, castingChargeOverlay = Overlay.new("castingCharge.dds", 0.45, 0.45, 0.10, 0.02),
                         inputValue = 0, maxAngleScale = 1,
                         rotBackSpeed = 0.0014, rotForwardSpeed = 0.0084 },
            waiting = { fishingRod = tool, biteHintTimeThreshold = 3500, biteTime = 0, biteTimeMax = 25000, biteTimeMin = 10000,
                        multiplier = 1.584, overlayDefaultWidth = 0.078125, overlayWidthVariation = 0.0015625, playedSample = false,
                        waitForBiteOverlay = overlay() },
            bite = { fishingRod = tool, bobberDepthBit = 0.08, catchTime = 0, duration = 880, glyphButtonHeight = 0.05,
                     glyphButtonOverlay = overlay(), glyphButtonText = "Hook fish",
                     hookFishOverlay = Overlay.new("hookFish.dds", 0.47, 0.45, 0.06, 0.04),
                     hookFishOverlayFadeInDuration = 200, hookFishOverlayWidthMax = 0.4, hookFishOverlayWidthMin = 0.3,
                     minCatchTimeThreshold = 700 },
            -- the minigame's layout, by the keys the #140 headset dump showed (its numbers rounded, and some of its keys left out)
            reeling = { fishingRod = tool, posX = 0.65, posY = 0.40, width = 0.10, height = 0.32,
                        fishYMin = 0.41, fishYMax = 0.69, cursorPos = 0.5, cursorSpeed = 0, cursorInTargetRange = true,
                        targetPosPercentage = 0.5, threshold = 0.2, targetLineXStart = 0.69, targetLineXEnd = 0.73,
                        targetLineWidth = 0.0026, hookLineX = 0.76, hookLineOffsetY = 0.0213, hookLineWidth = 0.0026,
                        hookStatusLineXStart = 0.746, hookStatusLineXEnd = 0.7474, hookStatusLineWidth = 0.0056, progress = 0.1,
                        reelingStartTime = 0, lastInputValue = 0, glyphButtonHeight = 0.03, glyphButtonOverlay = overlay(),
                        progressBarOverlay = Overlay.new("progressBar.png", 0.655, 0.41, 0.02, 0.30),
                        fishTargetBarOverlay = Overlay.new("fishTargetBar.png", 0.685, 0.41, 0.05, 0.30),
                        fishOverlay = Overlay.new("fish.png", 0.69, 0.41, 0.05, 0.04),
                        hookOverlay = Overlay.new("hook.png", 0.755, 0.37, 0.015, 0.026),
                        caughtOverlay = Overlay.new("caught.png", 0.657, 0.672, 0.02, 0.04),
                        lostOverlay = Overlay.new("lost.png", 0.658, 0.419, 0.019, 0.022) },
            caught = { fishingRod = tool, angleScaleDecreasePerMs = 0.006, bendResetRate = 0.001 },
        }
        local list = { states.idle, states.charging, states.waiting, states.bite, states.reeling, states.caught }
        for i = 1, 5 do table.insert(list, { fishingRod = tool, unseenValue = i }) end
        for name, stateTable in pairs(states) do stateNames[stateTable] = name end
        return states, list
    end

    local function stateTable(tool, name) return tool.rodStates[name] end

    local function setState(tool, name)
        local spec = tool[SPEC]
        spec.state = stateTable(tool, name)
        local line = spec.fishingLine
        if name == "idle" or name == "charging" or name == "caught" then
            line.isInitialized, line.visibility, line.ropeLength, line.numActiveNodes = false, false, 0, 0
            spec.rodBendFactor = 0
            reelHeld[tool] = nil
        elseif not line.isInitialized then  -- waiting, bite, reeling: the line is out
            line.isInitialized, line.visibility, line.ropeLength, line.numActiveNodes = true, true, 22.4, 46
        end
        if name ~= "reeling" then spec.rodBendFactor = 0 end
        if name == "charging" then
            spec.state.angleScale, spec.state.inputValue, spec.angleScale = 0, 1, 0
        elseif name == "waiting" then
            spec.state.biteTime = (_G.g_time or 0) + spec.state.biteTimeMin
        elseif name == "bite" then
            spec.state.catchTime = (_G.g_time or 0) + spec.state.duration
            tool.hookWindowMs = 0
        end
    end

    function StubHandTool.setRodState(tool, name) setState(tool, name) end
    function StubHandTool.biteRod(tool)
        assert(StubHandTool.rodState(tool) == "waiting", "a fish bites only while the rod waits")
        setState(tool, "bite")
    end
    function StubHandTool.setFishPull(tool, pull) tool.fishPull = pull end

    -- the cast: the line out, the bobber ropeLength along the look ray the game answers now
    local function cast(tool)
        local spec = tool[SPEC]
        local angleScale = spec.state.angleScale
        setState(tool, "waiting")
        spec.fishingLine.ropeLength = 2 + 20 * angleScale
        local player = tool:getCarryingPlayer()
        local ray = nil
        if player ~= nil then
            local read = player.getLookRay or player.lookRay
            ray = { read(player) }
        end
        local bobber = nil
        if ray ~= nil and ray[1] ~= nil then
            local dx, dy, dz = normalize(ray[4], ray[5], ray[6])
            local length = spec.fishingLine.ropeLength
            setTranslation(spec.bobber, ray[1] + dx * length, ray[2] + dy * length, ray[3] + dz * length)
            bobber = { getWorldTranslation(spec.bobber) }
        end
        table.insert(StubHandTool.casts, { angleScale = angleScale, ray = ray, bobber = bobber })
    end

    -- what the ACTIVATE_HANDTOOL action does, by state
    local function activate(tool, inputValue)
        local name = StubHandTool.rodState(tool)
        if inputValue > 0 then
            if name == "idle" then
                setState(tool, "charging")
            elseif name == "bite" then
                setState(tool, "reeling")
                reelHeld[tool] = true
            elseif name == "reeling" then
                reelHeld[tool] = true
            elseif name == "caught" then
                tool[SPEC].item = nil
                setState(tool, "idle")
            end
        else
            if name == "charging" then
                cast(tool)
            elseif name == "reeling" then
                reelHeld[tool] = nil
            end
        end
    end

    function StubHandTool.installFishingRod(variant)
        variant = variant or "ok"
        StubHandTool.fishingRods, StubHandTool.casts = {}, {}
        StubHandTool.fishingRodHandlerName = variant == "missing" and "onPrimaryAction" or "onActivateAction"
        local Class = {}
        StubHandTool.fishingRodClass = Class  -- no global name
        StubHandTool.storableClass = {
            getHolsterNodeByType = function() return nil end, onLoad = function() end,
            prerequisitesPresent = function() return true end, registerEventListeners = function() end,
            registerFunctions = function() end, registerXMLPaths = function() end,
        }

        local function record(self, action, inputValue)
            table.insert(StubHandTool.useCalls, { tool = "fishingRod", action = action, value = inputValue, self = self })
        end
        if variant == "reshaped" then
            Class.onActivateAction = function()
                local rod = StubHandTool.heldTool
                record(rod, "ACTIVATE_HANDTOOL", 1)
                activate(rod, 1)
            end
        else
            Class[StubHandTool.fishingRodHandlerName] = function(self, actionName, inputValue)
                record(self, "ACTIVATE_HANDTOOL", inputValue)
                activate(self, inputValue)
            end
        end
        function Class.onActivateSecondaryAction(self, actionName, inputValue) record(self, "ACTIVATE_HANDTOOL_SECONDARY", inputValue) end

        -- the type's functions, copied onto each rod by StubHandTool.fishingRod
        Class.setState = function(self, name) setState(self, name) end
        Class.getBobberPosition = function(self) return getWorldTranslation(self[SPEC].bobber) end
        Class.getTipPosition = function(self) return getWorldTranslation(self[SPEC].tipLine) end
        Class.getAngleScale = function(self) return self[SPEC].angleScale end
        Class.getRodBendFactor = function(self) return self[SPEC].rodBendFactor end
        Class.setRodBendFactor = function(self, factor) self[SPEC].rodBendFactor = factor end
        Class.getCurrentItem = function(self) return self[SPEC].item end
        Class.setCurrentItem = function(self, item) self[SPEC].item = item end
        Class.getExtendState = function(self) return 1 end
        for _, name in ipairs({ "setPrimaryAction", "setSecondaryAction", "placeBobber", "reelInBobber", "rotateReel", "createLine",
                                "destroyLine", "setAngleScale", "setBobberVisibility", "setTipLineVisibility", "setMovementDisabled",
                                "playSample", "stopSample", "getSample", "playExtendAnimation", "updateGraphicalNode",
                                "getInputOverlays" }) do
            Class[name] = function() end
        end
        -- the specialization's events and registration (on the class only)
        for _, name in ipairs({ "onHeldStart", "onHeldEnd", "onRegisterActionEvents", "onInputModeChanged", "onLoad", "onDelete",
                                "onPlayerPreTeleport", "prerequisitesPresent", "registerEventListeners", "registerFunctions",
                                "registerXMLPaths" }) do
            Class[name] = function() end
        end

        -- the update: the charge grows, a missed hook window lets the fish go, a held reel takes the line in
        function Class.onUpdate(self, dt)
            local spec = self[SPEC]
            if spec.state == nil then spec.state = self.rodStates.idle end
            local name = stateNames[spec.state]
            if name == "charging" then
                spec.state.angleScale = math.min(spec.state.maxAngleScale, spec.state.angleScale + spec.state.angleScaleIncreasePerMs * dt)
                spec.angleScale = spec.state.angleScale
            elseif name == "bite" then
                self.hookWindowMs = (self.hookWindowMs or 0) + dt
                if self.hookWindowMs > spec.state.duration then setState(self, "waiting") end
            elseif name == "reeling" then
                local held = reelHeld[self] == true
                spec.rodBendFactor = (self.fishPull or 0.5) * (held and 1 or 1 / 3)
                if held then
                    local line = spec.fishingLine
                    line.ropeLength = math.max(line.minLength, line.ropeLength - 0.01 * dt)
                    line.numActiveNodes = math.floor(line.ropeLength / line.nodeDistance)
                    if line.ropeLength <= line.minLength then
                        spec.item = { name = "fishingItem_trout", dialogText = "You've caught a trout!", dialogTitle = "Well done!",
                                      sellPrice = 100, distance = 1.15 }
                        setState(self, "caught")
                    end
                end
            end
        end

        -- the draw: the charge meter, the waiting hint and the "Hook fish" prompt, at the centre of the view, each also put on the
        -- screen (StubHandTool.screen) from the state's Overlays or as text; and while reeling a minigame far to the right (the
        -- headset showed one there; its shape here is made up: a tension bar and its fill, Overlays, and a hint the native puts
        -- below the bar, at a place taken from the bar's Overlay, as the real minigame's panel and lines are)
        function Class.onDraw(self)
            local current = self[SPEC].state
            local name = stateNames[current]
            if name == "charging" then
                StubHandTool.render(self, "castingChargeOverlay")
                current.castingChargeOverlay:render()
            elseif name == "waiting" then
                StubHandTool.render(self, "waitForBiteOverlay")
                renderTextNative(0.5, 0.40, 0.02, "Wait for a bite")
            elseif name == "bite" then
                StubHandTool.render(self, "hookFishOverlay")
                current.hookFishOverlay:render()
            elseif name == "reeling" then
                -- as the real minigame (#140 dump): the panel and the lines with the natives at the layout's numbers, the bars
                -- from the state's Overlays, and the fish and the hook placed again at every draw from where they are
                local s = current
                local function fishY(share) return s.fishYMin + share * (s.fishYMax - s.fishYMin) end
                drawRectNative("panel", s.posX, s.posY, s.width, s.height)
                s.progressBarOverlay:render()
                s.fishTargetBarOverlay:render()
                s.fishOverlay:setPosition(s.fishOverlay.x, fishY(s.cursorPos))
                s.fishOverlay:render()
                drawRectNative("targetLine", s.targetLineXStart, fishY(s.targetPosPercentage), s.targetLineXEnd - s.targetLineXStart, 0.002)
                s.hookOverlay:setPosition(s.hookOverlay.x, s.hookOverlay.y)
                s.hookOverlay:render()
                drawRectNative("hookLine", s.hookLineX, s.posY, s.hookLineWidth, s.height)
            end
        end
        StubHandTool.fishingRodOriginalOnDraw = Class.onDraw

        if not StubHandTool.fishingRodUpdaterAdded then
            StubHandTool.fishingRodUpdaterAdded = true
            table.insert(StubHandTool.updaters, function(dt)
                local held = StubHandTool.heldTool
                if held ~= nil and held[SPEC] ~= nil then StubHandTool.fishingRodClass.onUpdate(held, dt) end
            end)
            table.insert(StubHandTool.drawers, function(tool)
                if tool[SPEC] ~= nil then StubHandTool.fishingRodClass.onDraw(tool) end
            end)
        end
    end

    function StubHandTool.fishingRod(player, options)
        options = options or {}
        local Class = StubHandTool.fishingRodClass
        assert(Class ~= nil, "StubHandTool.installFishingRod first: the game has no fishing rod without the DLC")
        local tool = StubHandTool.makeTool(player, "fishingRod", SPEC, options)
        tool.configFileName = "C:/Program Files (x86)/Steam/steamapps/common/Farming Simulator 25/pdlc/highlandsFishingPack/handTools/fishingRod/fishingRod.xml"
        tool.typeName = SPEC_NAME
        tool.fishPull = options.fishPull or 0.5
        tool.spec_storable = { holsterNodes = {} }
        tool.specializationNames = { "storable", SPEC_NAME }
        tool.specializations = { StubHandTool.storableClass, Class }

        -- the nodes: the rod lies along its +X, root to tip, the reel below it at -Y (the headset, #140: the rod's +Z is its side)
        local root = tool.rootNode
        local model = StubScene.newNode("fishingRod", root, 0, 0, 0)
        local bones = StubScene.newNode("bodyBoneJoint", model, 0, 0, 0)
        local handle = StubScene.newNode("handleJoint", bones, 0.25, -0.06, 0.05)
        local yoke = StubScene.newNode("yokeJoint", bones, 0.25, -0.05, 0)
        local bail = StubScene.newNode("bailJoint", yoke, 0, -0.02, 0)
        local spool = StubScene.newNode("spoolJoint", bones, 0.27, -0.06, 0)
        local segment = StubScene.newNode("segment01Joint", bones, 0.6, 0, 0)
        local tip = StubScene.newNode("tipLine", segment, 1.3, 0, 0)
        StubScene.newNode("body", model, 0, 0, 0)
        tool.handNode = StubScene.newNode("handNode", model, 0.12, -0.01, 0)
        delete(tool.firstPersonNode)  -- the common tool's; the rod's is below its model
        tool.firstPersonNode = StubScene.newNode("firstPersonNode", model, 0, 0, 0)
        local ropeFrame = StubScene.newNode("ropeReferenceFrame", root, 0, 0, 0)
        for _, name in ipairs({ "rope_10_100", "rope_20_200", "rope_40_400", "rope_60_600" }) do StubScene.newNode(name, ropeFrame, 0, 0, 0) end

        -- the spec, as the dump showed it with the rod held and idle
        local spec = tool[SPEC]
        spec.bobber = StubScene.newNode("bobber", 0, 0, 0, 0)
        spec.bobberPhysicsHelper = StubScene.newNode("bobberPhysicsHelper", spec.bobber, 0, 0, 0)
        spec.tipLine = tip
        spec.tipLinePhysicsHelper = StubScene.newNode("fishingLineTipPhysicsHelper", tip, 0, 0, 0)
        spec.tipLineRopeVisualNode = StubScene.newNode("tipLineRopeVisualNode", tip, 0, 0, 0)
        spec.handle, spec.rotor, spec.spool, spec.bail, spec.mesh = handle, yoke, spool, bail, model
        spec.angleMax, spec.bendMax, spec.angleScale, spec.rodBendFactor = 2.2689280275926285, 0.2, 0, 0
        spec.reelInSoundSpeed, spec.spoolRecipricationValue = 0, 0
        spec.activateActionId, spec.activateActionSecondaryId = "activate", "activateSecondary"
        spec.actionEvents = {}
        spec.fishingLine = { isInitialized = false, visibility = false, ropeLength = 0, ropeLengthSumUp = 0, numActiveNodes = 0,
                             minLength = 2, maxLength = 40, nodeDistance = 0.5, diameter = 0.003, linkNode = root,
                             referenceFrame = ropeFrame }
        spec.item = nil
        local states, list = makeStates(tool)
        tool.rodStates = states
        spec.states = list
        spec.state = nil  -- until the rod's first update while held

        -- the type's functions, copied onto the rod as the game does (not the handlers, the update or the draw)
        local typeFunctions = {}
        for _, name in ipairs({ "setState", "getBobberPosition", "getTipPosition", "getAngleScale", "getRodBendFactor",
                                "setRodBendFactor", "getCurrentItem", "setCurrentItem", "getExtendState" }) do
            typeFunctions[name] = Class[name]
            tool[name] = Class[name]
        end
        tool.type = { name = SPEC_NAME, functions = typeFunctions }

        -- the registered action events, both on the class, both on press and release
        local function event(actionName, callback)
            return { actionName = actionName, target = tool, callback = callback, triggerUp = true, triggerDown = true,
                     triggerAlways = false, isActive = true, isDown = false }
        end
        local primary = event("ACTIVATE_HANDTOOL", Class[StubHandTool.fishingRodHandlerName] or Class.onActivateAction)
        local secondary = event("ACTIVATE_HANDTOOL_SECONDARY", Class.onActivateSecondaryAction)
        tool.actionEvents = { primary, secondary }
        local function binding(entry)
            return function(self, _, inputValue)
                if not entry.isActive then return end
                local down = inputValue > 0
                if down ~= entry.isDown then entry.callback(entry.target, entry.actionName, inputValue) end
                entry.isDown = down
            end
        end
        tool.activateAction = binding(primary)
        tool.activateSecondaryAction = binding(secondary)

        table.insert(StubHandTool.fishingRods, tool)
        return tool
    end

    function StubHandTool.pressSecondaryBinding(tool, inputValue) tool:activateSecondaryAction(nil, inputValue) end

    function StubHandTool.rodFaceRay(tool)
        local x, y, z = getWorldTranslation(tool.rootNode)
        local tx, ty, tz = getWorldTranslation(tool[SPEC].tipLine)
        local ax, ay, az = normalize(tx - x, ty - y, tz - z)
        local sx, sy, sz = getWorldTranslation(tool[SPEC].spool)
        local vx, vy, vz = sx - x, sy - y, sz - z
        local along = vx * ax + vy * ay + vz * az
        local dx, dy, dz = normalize(vx - along * ax, vy - along * ay, vz - along * az)
        return x, y, z, dx, dy, dz
    end
)luau";
