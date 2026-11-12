-- Model loading
-- Run after scene_graph.luau. The engine functions the Virtual Hands call to bring in their model, with a
-- stand-ins for the shipped Gloved Hand files (VirtualHandLeft.i3d, VirtualHandRight.i3d): each a file root whose
-- first child is the hand's root node, with the Wrist > Hand > finger bone chains below it, one shape with the real
-- mesh's bounds, and the real bone names and (roughly) positions in the files' own units: METRES, like the shipped
-- files, which carry their real-world size and their handedness themselves. The right file is the left one mirrored
-- across the model's X = 0 plane (positions with x negated, the Euler Y and Z angles of every bone negated), with the
-- same node names. The stub is served by the file name asked for: a name containing "VirtualHandRight" is the right
-- hand's.
--
--   StubModel.fileExists (default true)   what fileExists() answers for the model files
--   StubModel.missingFiles                set of file names (e.g. { ["VirtualHandRight.i3d"] = true }) fileExists() says
--                                         are not there, whatever fileExists is
--   StubModel.failLoad (default false)    loadI3DFile answers 0 (and a failure reason) although the files exist
--   StubModel.failFiles                   set of file names loadI3DFile fails for
--   StubModel.loads                       { { filename, root } } per loadI3DFile call that reached the "engine"
--   StubModel.clones                      node ids of every clone() of a node (the hands), in order
--   StubModel.deleted                     node id -> true for every deleted node
--   StubModel.hands() -> { left = node, right = node }   the hands' holder nodes (named virtualHand_left and
--                         virtualHand_right by the mod): each holder is placed at its grip pose, so its local space is the
--                         grip pose frame and everything of the hand hangs below it; a missing one is nil
--   StubModel.model(hand) -> node         the cloned model below a hand's holder
--   StubModel.scaledNodes(node) -> list   the names of the nodes among the node's ancestors and descendants whose scale
--                                         is not exactly 1 (none may be: the engine ignores the scale above a skinned mesh)
--   StubModel.setScaleCalls               how many times anything called setScale
--   StubModel.pointInAncestor(node, ancestor, x, y, z) -> x, y, z   a point of a node's local space in an ancestor's
--   StubModel.jointInGripFrame(hand, "index1") -> x, y, z   where a joint is, in the grip pose frame of the hand
--   StubModel.meshBoundsInGripFrame(hand) -> minX, minY, minZ, maxX, maxY, maxZ   the box around the glove mesh there
--   StubModel.isShown(node)               the node and every ancestor are visible (and it exists)
--   StubModel.castsShadow(node) -> true when any shape below casts one
--   StubModel.isLinkedToScene(node)       the node's ancestors lead up to the scene root
--   StubModel.missingJoints               set of node names of the model's file (e.g. { ["Middle.001"] = true }) the
--                                         next loaded model lacks
--   StubModel.joint(hand, name) -> node   the finger joint below a hand node, or nil ("thumb1".."little3": finger
--                                         and curl segment, looked up in that finger's own subtree)
--   StubModel.curl(hand, name) -> radians how far the joint is curled from its rest pose, TOWARD THE PALM, which for
--                                         this model (as for the shipped one) is a NEGATIVE rotation about the joint's own
--                                         X axis; errors when the joint turned about any other axis
-- Every joint of the stub model rests in a slightly bent pose that is NOT the identity rotation (each segment a
-- different X angle, plus a small Y and Z twist), so a curl that ignored the model's rest pose would show. Like the
-- shipped model's, the stub's joint rotations are small bends relative to their parent: a bone's rotation is NOT a
-- 90 degree turn of the bone axis (the GIANTS exporter wrote those, and the bones composed to a heap).
-- Nodes are visible, unscaled and (for shapes) casting shadows by default, like freshly loaded ones.

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
