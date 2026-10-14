-- ====================================================================
-- FS25 VR Companion Mod: Virtual Hands
--
-- Draws a gloved hand in the 3D world where each Motion Controller is held. The player alone
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
-- WHERE: the hand's GRIP pose (not its aim pose, which is only the Pointer Ray's fallback: the ray starts at this
-- hand's index fingertip, VRCompanion.VIRTUAL_HAND_POINTER), carried into the
-- world by VRCompanion:headRelativePoseToWorld, the same transform the Pointer Ray uses. It is placed once
-- per update, so under Sequential Dual-Pass both eye passes see the same placement and under Alternate
-- Frame Rendering each rendered frame places it again.
--
-- THE MODELS: the Gloved Hand work glove (CC BY, see models/VirtualHand_ATTRIBUTION.txt), two i3d files at
-- VRCompanion.modDirectory .. VRCompanion.VIRTUAL_HAND_MODEL_LEFT / VIRTUAL_HAND_MODEL_RIGHT: the glove as a left
-- hand and the same glove mirrored as a right hand (scripts/export_virtual_hand.py makes both). Both are rigged
-- the same way: Wrist > Hand > one base bone per finger (VRCompanion.VIRTUAL_HAND_FINGERS), each with three curl
-- joints below it, the same bone names in both. The file's first child is the hand's root node. Each file is
-- loaded once per map load (on the first update with Motion Controllers on) and cloned for its hand. Each hand is
-- a chain  holder -> offset node -> model clone  with the holder linked to the scene root:
--   * the holder carries the placement (the grip pose) and the visibility;
--   * the offset node carries the model onto the grip pose frame: VRCompanion.VIRTUAL_HAND_MODEL_TO_GRIP (a
--     rotation and a translation, derived and documented there, one for each hand).
-- NO NODE ABOVE OR ON THE MODEL CARRIES A SCALE. The engine skins a mesh by its bones' positions but ignores a
-- scale (or a negative scale, a mirror) on an ancestor of the skinned mesh: it drew the glove at its raw size,
-- about 2.5 m, while the bones were scaled. So the real-world size (the models are in metres) and the handedness
-- (a mirrored second file) are in the files, and this module never calls setScale.
-- Shadow casting is off on every shape of the clones. Everything is deleted again when the map unloads.
-- A missing or failing model file logs one `[FS25_VR] Virtual Hands: ...` line (naming the first file that
-- failed) and leaves Virtual Hands off; the Pointer Ray, Click Points and menus do not depend on it.
--
-- FINGER CURL: each time a hand is shown, its index finger joints are rotated in proportion to the trigger and
-- its middle, ring and little finger joints in proportion to the grip, both 0..1 as VRBridge.getHandState
-- reports them. The thumb is never touched. The joints are those of VRCompanion.VIRTUAL_HAND_FINGERS, each found
-- below its finger's base bone. The rotation a joint has when found is the model's relaxed rest pose, and the
-- curl is applied on top of it: the joint's rest rotation turned about the joint's own X axis by
-- FINGER_CURL_DIRECTION * value * VRCompanion.FINGER_CURL_DEGREES[segment]. FINGER_CURL_DIRECTION is -1 for this
-- model: toward the palm is a NEGATIVE rotation about local X. 0 is therefore exactly the rest pose. The right
-- file's bones are the left file's mirrored with their local frames kept proper rotations (the local X axis is
-- negated by the mirror, so a joint's X rotation is unchanged and its Y and Z rotations change sign), so the same
-- local rotation about X curls the right hand's fingers toward its palm the same way (checked offline on both
-- files by scripts/check_virtual_hand_rig.ps1 -Curl). A hand that is hidden is not updated: its fingers are set again the moment it is shown. A bone the model lacks keeps its
-- finger segment still (a finger without its base bone stays still altogether); the missing names are logged
-- once per map, and the other fingers still curl.
--
-- LOG: a change-only `[FS25_VR] Virtual Hands: <reason>` line says why Virtual Hands are unavailable
-- (Motion Controllers off, model not loaded, head not placed in the world) and when they become
-- available. Whether one hand is shown or why not is in the diagnostics only.
--
-- PUBLIC SURFACE
--   VRCompanion.VIRTUAL_HAND_MODEL_LEFT      left hand model path inside the Companion Mod
--   VRCompanion.VIRTUAL_HAND_MODEL_RIGHT     right hand model path inside the Companion Mod
--   VRCompanion.modDirectory                 the Companion Mod's folder (set when the mod is loaded)
--   VRCompanion.VIRTUAL_HAND_FINGERS         the joint table: per curling finger its input, base bone and three joints
--   VRCompanion.VIRTUAL_HAND_MODEL_TO_GRIP   { left = , right = } rotation and grip origin (metres) that carry each model onto the grip frame
--   VRCompanion.VIRTUAL_HAND_POINTER         { left = , right = } index fingertip and finger direction in the grip frame: where the Pointer Ray starts and runs
--   VRCompanion.FINGER_CURL_DEGREES          full curl (value 1) of segment 1, 2 and 3 of a finger, in degrees
--   VRCompanion.FINGER_CURL_DIRECTION        -1: toward the palm is a negative rotation about a joint's local X
--   VRCompanion:updateVirtualHands(dt)       once per update, called by updateMotionControllers
--   VRCompanion:resetVirtualHands()          map unload: deletes the hands and the model, forgets everything
--   VRCompanion:getVirtualHandsDiagnostics() -> string   the reason and each hand's state, one line
--   Console: vrVirtualHandsDiagnostics       prints the same line (also part of vrPointerDiagnostics)
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.VirtualHands = VRCompanion.VirtualHands or {}

-- The mod's own folder, known only while the engine loads the mod's scripts.
VRCompanion.modDirectory = VRCompanion.modDirectory or g_currentModDirectory or ""
-- The hand models, relative to the Companion Mod's folder: the glove as a left hand and the same glove mirrored as a
-- right hand, both in metres.
VRCompanion.VIRTUAL_HAND_MODEL_LEFT = "models/VirtualHandLeft.i3d"
VRCompanion.VIRTUAL_HAND_MODEL_RIGHT = "models/VirtualHandRight.i3d"

-- The curl of one finger joint at a full pull, base to tip, in degrees, ADDED to the glove's relaxed pose (which
-- already flexes the joints by about 3 to 9, 15 to 21 and 14 to 21 degrees). A real fist flexes a finger about
-- 80, 100 and 65 degrees from straight; on top of the relaxed pose that would drive the fingertips into the
-- palm, so these are less. Checked offline on the composed bones of the corrected rig (models/VirtualHandLeft.i3d
-- and VirtualHandRight.i3d, scripts/check_virtual_hand_rig.ps1 -Curl, identical for both): at 1 the bone through
-- each fingertip ends 0.007 to 0.026 m (0.08 to 0.30 of the glove's original units) in front of the knuckle line,
-- and the palm plane is 0.015 m (0.17 units) in front of it, so the fingertips come to rest on the palm.
-- The earlier 80, 100, 65 put the index tip end 0.03 BEHIND the knuckle line, 0.2 inside the hand.
VRCompanion.FINGER_CURL_DEGREES = { 60, 75, 40 }

-- THE MODEL-TO-GRIP OFFSET: how the model as the file holds it is carried onto the OpenXR grip pose frame (origin
-- at the centre of the tube the closed non-thumb fingers form, -Z along that tube from the little finger toward
-- the thumb, +X the palm normal: into the palm for a right hand, out of the palm for a left hand, so -Y runs
-- toward the fingertips). One rotation and one model-space point (in metres) that must land on the grip origin,
-- for each hand. No scale: the models are already real-world size (see THE MODELS above). A node between the
-- holder and the model carries it (see buildHands).
--
-- Derived from the Blender source (Gloved Hand LP.blend, inspected headless with Blender 5.2), in the
-- file's own axes (the exporter's Y-up axes: x right, y up, z = -Blender y). The numbers below are in the
-- glove's ORIGINAL units, the arbitrary units it was modelled in; the exported files are those units times
-- 0.0875 m per unit (scripts/export_virtual_hand.py, METRES_PER_UNIT), which is why the table at the end holds the
-- origin as 0.0875 times the units written here:
--   * AXES. The model is a LEFT hand. Its fingers run along -Z, the palm faces -Y (the back of the hand is +Y) and
--     the thumb is on the +X side, so little finger -> thumb is +X. With the grip frame of a LEFT hand (+X out of
--     the palm, -Z little finger -> thumb, so Y = Z x X points toward the wrist): grip X = model -Y, grip Y =
--     model +Z, grip Z = model -X. That rotation is the quaternion below (qx, qy, qz, qw = -0.5, 0.5, 0.5, 0.5);
--     its determinant is +1.
--   * THE RIGHT HAND. Both hands' grip frames have the same axes; the right hand is the mirror image of the left
--     across the grip frame's X plane (grip x -> -x). The right MODEL file is the left model mirrored across the
--     model's own X = 0 plane (S = diag(-1, 1, 1), thumb on -X, palm still toward -Y, fingers still along -Z). So a
--     point p of the right model is the point S p of the left model, which is at  r = R (S p - o)  in the left grip
--     frame (R the rotation above, o the left grip origin), and at  S r  in the right grip frame (the grip mirror
--     is the same S). The right hand therefore carries  p -> S R S (p - S o): the rotation  R' = S R S  and the
--     grip origin  o' = S o  (x negated). S R S is a proper rotation (determinant (-1)(+1)(-1) = +1), and for a
--     quaternion (x, y, z, w) it is (x, -y, -z, w): here (-0.5, -0.5, -0.5, 0.5), the cyclic permutation
--     model (x, y, z) -> grip (y, z, x): grip X = model +Y (into the palm, the palm faces -Y), grip Y = model +Z
--     (toward the wrist), grip Z = model +X (the thumb is on -X, so -Z runs little finger -> thumb). The node
--     translation is -R' o' = S (-R o): the left node's translation with x negated, "the left offset mirrored
--     across the grip X plane".
--   * SCALE. The file is in arbitrary units: from the Hand bone (the wrist crease, z 0.525) to the tip of the middle
--     finger (mesh, z -1.622) it is 2.147 units; the knuckle row of the four fingers is about 0.99 units across
--     (x -0.458 .. 0.527 at the metacarpals). A real hand is about 0.19 m long and 0.085 m across, which gives
--     0.0885 m and 0.0863 m per unit. 0.0875 m per unit makes it 0.188 m long and 0.086 m across. The glove mesh
--     is then 0.245 m long including 0.03 m of cuff and arm stub, 0.149 m wide with the thumb and 0.057 m thick.
--   * ORIGIN. The model-space point that becomes the grip origin is (x 0.04, y -0.30, z -0.40):
--       - across the hand, x 0.04, the middle of the four knuckles (x -0.458 .. 0.527 at the metacarpals);
--       - out of the palm, y -0.30: the palm surface is at about y -0.115 in the middle of the palm (-0.147 average
--         over the palm; the back of the hand is at about +0.22), and a controller handle about 33 mm across
--         (radius about 0.19 units) lies in front of it, so the tube centre is the palm surface minus 0.19;
--       - along the hand, z -0.40: the knuckle joints are at z -0.62 .. -0.73 and the wrist crease at z 0.525; the
--         tube lies under the proximal finger segments, about 0.25 units (2 cm) on the wrist side of the knuckles.
--     The grip origin is then 0.081 m from the wrist crease and 0.02 to 0.03 m behind the knuckle row.
-- UNCERTAINTY: the scale is good to about 5 percent (hands differ). The origin is an estimate of where the tube is
-- for a Touch, Index or Vive controller: expect +-0.02 m along the hand (z) and +-0.01 m out of the palm (y); the
-- headset check tunes it. Written below as original units times the 0.0875 m per unit the files were scaled by,
-- so it can be nudged in the glove's units without redoing the arithmetic.
local METRES_PER_MODEL_UNIT = 0.0875

local function mirroredAcrossX(left)
    -- S R S and S o for S = diag(-1, 1, 1), see THE RIGHT HAND above
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

-- THE POINTER RAY ON THE HAND: the Pointer Ray starts at the index fingertip of the Virtual Hand and runs along the
-- index finger. Both are a FIXED offset in the grip frame of a hand (so they ride on the GRIP pose, through the same
-- head-relative-to-world transform as the hand itself): the tip is the end of the glove's index finger and the
-- direction is the unit vector from the index knuckle joint (the Lower.001 bone) to that tip. It is the RELAXED rest
-- pose of the glove that is measured, never the curled bones, so the ray does not twitch when the trigger curls the
-- finger. Metres, in the grip frame of the LEFT hand; the right hand is the left with grip-frame X negated, the same
-- mirror as VIRTUAL_HAND_MODEL_TO_GRIP (grip X = out of a left palm, -Y toward the fingertips, -Z little finger to thumb).
-- The ray applies this whenever it is placed, whether or not Virtual Hands are shown or their model loaded.
--
-- Derived by scripts/derive_virtual_hand_pointer.py (Blender 5.2, headless, on Gloved Hand LP.blend, the same file and
-- the same model-to-grip rotation and origin as above; re-run it when VIRTUAL_HAND_MODEL_TO_GRIP changes):
--   * the index finger is the set of mesh vertices whose strongest skin weight is Lower.001, Middle.001 or Top.001
--     (188 vertices); every pose bone is the identity, so the mesh is the relaxed rest pose the .i3d bones compose to;
--   * the tip is the centre of the last 3 mm of that mesh along the finger (9 vertices; the single furthest vertex is
--     1.3 mm from it), at model (0.0476, -0.0210, -0.1255) m in the file's axes; the knuckle joint (Lower.001) is at
--     model (0.0330, 0.0048, -0.0543) m, so the finger from knuckle to tip is 0.077 m long (a real index finger from
--     the knuckle joint is 0.07 to 0.08 m);
--   * both carried onto the grip frame by  R (p - o)  with the LEFT_MODEL_TO_GRIP rotation and origin below: the tip at
--     (-0.0052, -0.0905, -0.0441) and the knuckle at (-0.0311, -0.0193, -0.0295) (the model's bone, as the Virtual Hand
--     puts it there), the direction (0.3349, -0.9232, -0.1885). The finger therefore runs 22.6 degrees off the grip
--     frame's -Y toward +X, because the relaxed glove finger is curled a little toward the palm.
-- UNCERTAINTY: the direction does not depend on how the tip is defined to within about 1.4 degrees (to the furthest
-- vertex 0.7, to the end of the Top.001 bone 1.4); measured to the Top.001 joint origin instead it would be 6.4 degrees
-- off, which is why the joint is not used. The tip is good to about 5 mm (the glove finger is 10 mm across). Whether the
-- glove's fingertip sits where the player's real fingertip is depends on the same estimate of the grip origin as the
-- hand itself (+-0.02 m along the hand, +-0.01 m out of the palm, see above), which the headset check tunes.
-- A test checks this table against the model's knuckle bone in the grip frame.
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

-- Which way a curl turns a joint about its own X axis for THIS model: -1, a NEGATIVE rotation about local X is
-- toward the palm (+1 would be the opposite model). The Gloved Hand's bones run along their local -Z, each bone's
-- X axis is the side axis of its finger (perpendicular to the segment, in the plane of the palm), and its relaxed
-- rest pose already flexes every finger joint a little in the NEGATIVE direction (index Lower.001 -5.9,
-- Middle.001 -14.8, Top.001 -21.2 degrees about X relative to the parent). The Blender file agrees: the same bends
-- are toward the side the palm faces (-Z in Blender, -Y in the file). Checked offline on the composed bone
-- transforms of the file: turning a joint by a NEGATIVE angle about its local X moves the rest of the finger toward
-- -Y, the palm side, for all four fingers (the local X axes lie within 25 degrees of the file's X axis, the most for
-- the splayed little finger, and are exactly perpendicular to their segments). Flip this one number if a headset
-- check shows fingers bending back. The right file's bones are the left's mirrored with proper-rotation frames, so
-- the sign is the same for both hands: the offline check folds the fingers of both files and both go toward the palm.
VRCompanion.FINGER_CURL_DIRECTION = -1

local HANDS = { 0, 1 }
local HAND_NAMES = { [0] = "left", [1] = "right" }

-- THE JOINT TABLE: the fingers that curl, the analog input of the Motion Controller that drives each, and the
-- model's own names for the bones. `base` is the finger's base bone and `joints` are its three curl joints, base
-- to tip (segment 1, 2 and 3). The thumb is left out on purpose: it stays as the model rests. The thumb's own
-- bones are called Lower, Middle and Top, so a joint is only ever looked for below its finger's base bone, never
-- by its name across the whole hand. A test checks this table against models/VirtualHandLeft.i3d.
VRCompanion.VIRTUAL_HAND_FINGERS = {
    { name = "index",  input = "trigger", base = "Index Finger",  joints = { "Lower.001", "Middle.001", "Top.001" } },
    { name = "middle", input = "grip",    base = "Middle Finger", joints = { "Lower.002", "Middle.002", "Top.002" } },
    { name = "ring",   input = "grip",    base = "Ring Finger",   joints = { "Lower.003", "Middle.003", "Top.003" } },
    { name = "little", input = "grip",    base = "Pinky",         joints = { "Lower.004", "Middle.004", "Top.004" } },
}
local FINGERS = VRCompanion.VIRTUAL_HAND_FINGERS

local function newState()
    return {
        modelTried = false,      -- the model was loaded (or failed to) this map
        modelFailure = nil,      -- why the model is not available, when it is not
        fileRoots = {},          -- [hand] = the root node of the model file loaded for that hand (never linked into the scene)
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
    for _, hand in ipairs(HANDS) do deleteNode(state.fileRoots[hand]) end
    state.fileRoots = {}
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

-- Finds the finger joints below one hand clone and remembers each one's rest rotation. Each finger's base bone
-- is found first and its joints below it; a finger whose base bone is missing has no joints at all. Returns the
-- model's names of what it does not have, as a set.
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

-- Sets a node to carry a hand's model onto the grip pose frame (VRCompanion.VIRTUAL_HAND_MODEL_TO_GRIP): the model
-- point gripOrigin ends at the node's origin and the model's axes are turned onto the grip axes. The node carries
-- only a rotation and a translation, never a scale: the models are in metres.
local function applyModelToGrip(node, offset)
    local q, o = offset.rotation, offset.gripOrigin
    -- a point of the model is carried to  rotation * (point - gripOrigin)  in the grip frame, so the node's own
    -- translation is  -rotation * gripOrigin
    local cx, cy, cz = q.y * o.z - q.z * o.y + q.w * o.x, q.z * o.x - q.x * o.z + q.w * o.y, q.x * o.y - q.y * o.x + q.w * o.z
    local tx = o.x + 2 * (q.y * cz - q.z * cy)  -- the quaternion rotation of gripOrigin
    local ty = o.y + 2 * (q.z * cx - q.x * cz)
    local tz = o.z + 2 * (q.x * cy - q.y * cx)
    engine("setQuaternion")(node, q.x, q.y, q.z, q.w)
    engine("setTranslation")(node, -tx, -ty, -tz)
end

-- The model file of a hand: the left hand's from the left file, the right hand's from the right one.
local function modelPath(self, hand)
    local file = hand == self.MOTION_HAND_RIGHT and self.VIRTUAL_HAND_MODEL_RIGHT or self.VIRTUAL_HAND_MODEL_LEFT
    return (self.modDirectory or "") .. file
end

-- Loads both model files (each once) and makes both hands, hidden. Returns nil on success, otherwise why not (the
-- first file that failed).
local function buildHands(self)
    for _, name in ipairs({ "loadI3DFile", "clone", "link", "createTransformGroup", "getRootNode", "getChildAt",
                            "setVisibility", "setWorldTranslation", "setWorldQuaternion", "getName",
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
        local entry = { holder = holder, shown = false, status = "hidden", world = {} }
        state.hands[hand] = entry
        engine("link")(engine("getRootNode")(), holder)
        local model = engine("clone")(template, false, false)
        if model == nil or model == 0 then return "cloning the hand node of " .. path .. " failed" end
        entry.model = model
        -- holder (the grip pose) -> offset node -> model clone. No node carries a scale (see THE MODELS): the
        -- offset node only turns and moves the model onto the grip frame, with the offset of this hand.
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
        local names = {}  -- in table order: each finger's base bone, then its joints
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
        local half = VRCompanion.FINGER_CURL_DIRECTION * math.rad(degrees[joint.segment]) * value * 0.5
        local bx, bw = math.sin(half), math.cos(half)  -- the turn about X, as a quaternion
        -- rest * turn: the turn happens in the joint's own frame
        setQuaternion(joint.node, VRCompanion.multiplyQuaternions(joint.restX, joint.restY, joint.restZ, joint.restW,
            bx, 0, 0, bw))
    end
end

local describeEngineGeometry  -- defined below with the diagnostics

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
                if not entry.geometryLogged then
                    entry.geometryLogged = true
                    print(string.format("[FS25_VR] Virtual Hand geometry (%s): %s", HAND_NAMES[hand], describeEngineGeometry(entry)))
                end
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

-- What the engine itself reports about one hand's nodes (world positions and scale), for a headset check that finds
-- the hand the wrong size or in the wrong place. Needs the engine's node query functions; says so when it has none.
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
    return string.format("holder %s, model %s (local scale %s, expect 1.0000: the file is already in metres), Wrist %s, Wrist to Top.001 %s m, Wrist to Top.002 %s m (expect about 0.20: 0.197 and 0.204)",
        at(entry.holder), at(entry.model), scale, at(wrist), distance(wrist, indexTip), distance(wrist, middleTip))
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

-- The engine's own numbers for both hands, one line each.
function VRCompanion:consoleCommandVirtualHandsGeometry()
    return string.format("Left Virtual Hand: %s | Right Virtual Hand: %s",
        describeEngineGeometry(state.hands[0]), describeEngineGeometry(state.hands[1]))
end

function VRCompanion:consoleCommandVirtualHandsDiagnostics()
    return self:getVirtualHandsDiagnostics()
end

function VRCompanion:registerVirtualHandsConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrVirtualHandsDiagnostics", "Report why the Virtual Hands are shown or hidden: vrVirtualHandsDiagnostics", "consoleCommandVirtualHandsDiagnostics", self)
    addConsoleCommand("vrVirtualHandsGeometry", "Report where the engine puts the Virtual Hands and how big they are: vrVirtualHandsGeometry", "consoleCommandVirtualHandsGeometry", self)
end

-- Map unload: the hands and the model go, and the next map loads the model again.
function VRCompanion:resetVirtualHands()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrVirtualHandsDiagnostics")
        removeConsoleCommand("vrVirtualHandsGeometry")
    end
    discardScene()
    state = newState()
end

VRCompanion.VirtualHands.updateVirtualHands = function(self, ...) return VRCompanion.updateVirtualHands(VRCompanion, ...) end
VRCompanion.VirtualHands.resetVirtualHands = function(self, ...) return VRCompanion.resetVirtualHands(VRCompanion, ...) end
VRCompanion.VirtualHands.getVirtualHandsDiagnostics = function(self, ...) return VRCompanion.getVirtualHandsDiagnostics(VRCompanion, ...) end
VRCompanion.VirtualHands.consoleCommandVirtualHandsDiagnostics = function(self, ...) return VRCompanion.consoleCommandVirtualHandsDiagnostics(VRCompanion, ...) end

return VRCompanion.VirtualHands
