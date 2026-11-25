# Decisions behind Virtual Hands and the Shipped Hand Models

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0025](0025-motion-controller-input-reaches-lua-as-raw-per-hand-state.md).

## Context and Decision
This record holds the non-obvious decisions behind the Virtual Hands: where they are drawn and why there, the finger curl, the fitted numbers, and the rig and export of the shipped models. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `mods/FS25_VRCompanion/src/VRCompanionVirtualHands.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Virtual Hands

  Draws a gloved hand in the 3D world where each Motion Controller is held. The player alone
  sees them: nothing here is synchronised in multiplayer, and the hands have no collision, physics or
  shadow.

  A Virtual Hand is SHOWN exactly while all of these hold, otherwise it is hidden (never deleted, so it
  is back at once):
    * Motion Controllers are on (`vrMotionControllers`, there is no separate setting),
    * that hand is in use (VRCompanion:isHandInUse: tracked and moved within the in-use timeout),
    * its grip pose is tracked (the aim pose alone being tracked is not enough),
    * the head is placed in the world, the check the Pointer Ray uses (no menu, no 2D Floating Window
      Mode, the tracked camera is shown, no Pass-Through Mode). The engine's own head-tracking setting
      is not a gate: it reads false in the real game.

  WHERE: the hand's GRIP pose (not its aim pose, which is only the Pointer Ray's fallback: the ray starts at this
  hand's index fingertip, VRCompanion.VIRTUAL_HAND_POINTER), carried into the world by
  VRCompanion:headRelativePoseToWorld, the same transform the Pointer Ray uses. That world pose is then taken into the
  space of the ANCHOR and the hand's holder is linked below it, so the hand rides with whatever carries the head: the
  game moves a vehicle, and the camera chain with it, after the mod's update, and a hand left at a world position was
  drawn a frame's travel behind the cab (a ghost trailing the direction of travel). The anchor is the topmost node
  above the Cyclopean Audio Listener Node (the head) below the scene root: in a cab the vehicle's component (the game
  links components to the root), on foot the player's root; the scene root itself when that node is hidden. NOT the
  head or its camera nodes: hung below the head the hands vanished in the cab (headset check), and the nodes below a
  component are moved by camera logic (rotate nodes, seat suspension, the mod's own head placement) and may sit in a
  group a vehicle file marks invisible, which hides every shape below it but no camera. A change-only
  `[FS25_VR] Virtual Hands ride on <node>` line names the anchor (and why, when it is the scene root), and each hand
  logs its geometry again on a new one.
  It is placed in the update and AGAIN BEFORE EVERY RENDERED FRAME (VRCompanion:placeVirtualHandsForRender, called by
  the camera path once it placed the head), from the grip pose relative to the head as the update found it and the
  head's world pose then: the game renders some frames with no update before them (under Alternate Frame Rendering one
  update serves both eyes, a headset trace showed), so a hand placed in the update alone was a sharp copy lagging
  behind in the second eye. Placing it in the draw was too late: the hand was right at draw time and still lagged.
  Under Sequential Dual-Pass both eye passes see the same placement.
  A hand that holds the Steering Wheel (Wheel Grab, VRCompanion:getWheelGrabHandPose) is not placed at its grip pose but
  pinned to the rim point where it grabbed, turning with the wheel node's actual rotation (taken into the anchor's
  space like the grip pose, so it rides with the cab too); it stays shown however long it
  is held still (the Motion Controller module keeps a holding hand in use), and its fingers still curl with the real grip.
  A hand that holds a Hand Tool (Hand Tools, VRCompanion:isHandHoldingHandTool) has the tool's root linked below its holder
  (the tool rides along on every placement above, including the one before a rendered frame with no update), and is drawn
  closed around the handle: the middle, ring and little fingers at VRCompanion.HELD_TOOL_GRIP_CURL whatever the real grip is,
  the index finger still following the trigger. The holder is deleted only with the hands, and the tool is given back to the
  game's placement first (VRCompanion:releaseHandToolsFromHands).
  A holder deleted with the node it hung below (a vehicle deleted with the player in it) is made again, model and all.

  THE MODELS: the Gloved Hand work glove (CC BY, see models/VirtualHand_ATTRIBUTION.txt), two i3d files at
  VRCompanion.modDirectory .. VRCompanion.VIRTUAL_HAND_MODEL_LEFT / VIRTUAL_HAND_MODEL_RIGHT: the glove as a left
  hand and the same glove mirrored as a right hand (scripts/export_virtual_hand.py makes both). Both are rigged
  the same way: Wrist > Hand > one base bone per finger (VRCompanion.VIRTUAL_HAND_FINGERS), each with three curl
  joints below it, the same bone names in both. The file's first child is the hand's root node. Each file is
  loaded once per map load (on the first update with Motion Controllers on) and cloned for its hand. Each hand is
  a chain  holder -> offset node -> model clone  with the holder linked to the scene root until it is first placed,
  and below the anchor from then on (see WHERE):
    * the holder carries the placement (the grip pose) and the visibility;
    * the offset node carries the model onto the grip pose frame: VRCompanion.VIRTUAL_HAND_MODEL_TO_GRIP (a
      rotation and a translation, derived and documented there, one for each hand).
  NO NODE ABOVE OR ON THE MODEL CARRIES A SCALE. The engine skins a mesh by its bones' positions but ignores a
  scale (or a negative scale, a mirror) on an ancestor of the skinned mesh: it drew the glove at its raw size,
  about 2.5 m, while the bones were scaled. So the real-world size (the models are in metres) and the handedness
  (a mirrored second file) are in the files, and this module never calls setScale.
  Shadow casting is off on every shape of the clones. Everything is deleted again when the map unloads.
  A missing or failing model file logs one `[FS25_VR] Virtual Hands: ...` line (naming the first file that
  failed) and leaves Virtual Hands off; the Pointer Ray, Click Points and menus do not depend on it.

  FINGER CURL: each time a hand is shown, its index finger joints are rotated in proportion to the trigger and
  its middle, ring and little finger joints in proportion to the grip, both 0..1 as VRBridge.getHandState
  reports them. The thumb is never touched. The joints are those of VRCompanion.VIRTUAL_HAND_FINGERS, each found
  below its finger's base bone. The rotation a joint has when found is the model's relaxed rest pose, and the
  curl is applied on top of it: the joint's rest rotation turned about the joint's own X axis by
  FINGER_CURL_DIRECTION * value * VRCompanion.FINGER_CURL_DEGREES[segment]. FINGER_CURL_DIRECTION is -1 for this
  model: toward the palm is a NEGATIVE rotation about local X. 0 is therefore exactly the rest pose. The right
  file's bones are the left file's mirrored with their local frames kept proper rotations (the local X axis is
  negated by the mirror, so a joint's X rotation is unchanged and its Y and Z rotations change sign), so the same
  local rotation about X curls the right hand's fingers toward its palm the same way (checked offline on both
  files by scripts/check_virtual_hand_rig.ps1 -Curl). A hand that is hidden is not updated: its fingers are set again the moment it is shown. A bone the model lacks keeps its
  finger segment still (a finger without its base bone stays still altogether); the missing names are logged
  once per map, and the other fingers still curl.

  LOG: a change-only `[FS25_VR] Virtual Hands: <reason>` line says why Virtual Hands are unavailable
  (Motion Controllers off, model not loaded, head not placed in the world) and when they become
  available. Whether one hand is shown or why not is in the diagnostics only.

  PUBLIC SURFACE
    VRCompanion.VIRTUAL_HAND_MODEL_LEFT      left hand model path inside the Companion Mod
    VRCompanion.VIRTUAL_HAND_MODEL_RIGHT     right hand model path inside the Companion Mod
    VRCompanion.modDirectory                 the Companion Mod's folder (set when the mod is loaded)
    VRCompanion.VIRTUAL_HAND_FINGERS         the joint table: per curling finger its input, base bone and three joints
    VRCompanion.VIRTUAL_HAND_MODEL_TO_GRIP   { left = , right = } rotation and grip origin (metres) that carry each model onto the grip frame
    VRCompanion.VIRTUAL_HAND_POINTER         { left = , right = } index fingertip and finger direction in the grip frame: where the Pointer Ray starts and runs
    VRCompanion.FINGER_CURL_DEGREES          full curl (value 1) of segment 1, 2 and 3 of a finger, in degrees
    VRCompanion.FINGER_CURL_DIRECTION        -1: toward the palm is a negative rotation about a joint's local X
    VRCompanion.HELD_TOOL_GRIP_CURL          0..1: how far the grip fingers close around a Hand Tool's handle
    VRCompanion:getVirtualHandHolder(hand)   the node that carries a hand's placement (the grip frame), or nil: what a Hand Tool is linked below
    VRCompanion:updateVirtualHands(dt)       once per update, called by updateMotionControllers
    VRCompanion:placeVirtualHandsForRender() before every rendered frame, called by placeAndBindEyes: places the shown hands again
                       (and then placeHandToolsForRender, so the flashlight's light node follows its hand)
    VRCompanion:drawVirtualHands()           called by VRCompanion:draw: the vrVirtualHandsTrace line at draw time
    VRCompanion:resetVirtualHands()          map unload: deletes the hands and the model, forgets everything
    VRCompanion:getVirtualHandsDiagnostics() -> string   the reason and each hand's state, one line
    Console: vrVirtualHandsDiagnostics       prints the same line (also part of vrPointerDiagnostics)
    Console: vrVirtualHandsTrace [frames]    logs each shown hand relative to the rendering camera, after the update and
                                             at draw time (in VRCompanion:drawVirtualHands)
  ```

- **`VRCompanion.FINGER_CURL_DEGREES`**: The curl of one finger joint at a full pull, base to tip, in degrees, ADDED to the glove's relaxed pose (which already flexes the joints by about 3 to 9, 15 to 21 and 14 to 21 degrees). A real fist flexes a finger about 80, 100 and 65 degrees from straight; on top of the relaxed pose that would drive the fingertips into the palm, so these are less. Checked offline on the composed bones of the corrected rig (models/VirtualHandLeft.i3d and VirtualHandRight.i3d, scripts/check_virtual_hand_rig.ps1 -Curl, identical for both): at 1 the bone through each fingertip ends 0.007 to 0.026 m (0.08 to 0.30 of the glove's original units) in front of the knuckle line, and the palm plane is 0.015 m (0.17 units) in front of it, so the fingertips come to rest on the palm. The earlier 80, 100, 65 put the index tip end 0.03 BEHIND the knuckle line, 0.2 inside the hand.
- **`VRCompanion.HELD_TOOL_GRIP_CURL`**: THE HELD-TOOL POSE: a hand that holds a Hand Tool (VRCompanion:isHandHoldingHandTool) has its middle, ring and little fingers closed around the handle by this much of a full grip (0..1), whatever the real grip is; the index finger still follows the trigger. Full: the fingers rest on the palm as for a squeezed grip.
- **`METRES_PER_MODEL_UNIT`**:

  ```text
  THE MODEL-TO-GRIP OFFSET: how the model as the file holds it is carried onto the OpenXR grip pose frame (origin
  at the centre of the tube the closed non-thumb fingers form, -Z along that tube from the little finger toward
  the thumb, +X the palm normal: into the palm for a right hand, out of the palm for a left hand, so -Y runs
  toward the fingertips). One rotation and one model-space point (in metres) that must land on the grip origin,
  for each hand. No scale: the models are already real-world size (see THE MODELS above). A node between the
  holder and the model carries it (see buildHands).

  Derived from the Blender source (Gloved Hand LP.blend, inspected headless with Blender 5.2), in the
  file's own axes (the exporter's Y-up axes: x right, y up, z = -Blender y). The numbers below are in the
  glove's ORIGINAL units, the arbitrary units it was modelled in; the exported files are those units times
  0.0875 m per unit (scripts/export_virtual_hand.py, METRES_PER_UNIT), which is why the table at the end holds the
  origin as 0.0875 times the units written here:
    * AXES. The model is a LEFT hand. Its fingers run along -Z, the palm faces -Y (the back of the hand is +Y) and
      the thumb is on the +X side, so little finger -> thumb is +X. With the grip frame of a LEFT hand (+X out of
      the palm, -Z little finger -> thumb, so Y = Z x X points toward the wrist): grip X = model -Y, grip Y =
      model +Z, grip Z = model -X. That rotation is the quaternion below (qx, qy, qz, qw = -0.5, 0.5, 0.5, 0.5);
      its determinant is +1.
    * THE RIGHT HAND. Both hands' grip frames have the same axes; the right hand is the mirror image of the left
      across the grip frame's X plane (grip x -> -x). The right MODEL file is the left model mirrored across the
      model's own X = 0 plane (S = diag(-1, 1, 1), thumb on -X, palm still toward -Y, fingers still along -Z). So a
      point p of the right model is the point S p of the left model, which is at  r = R (S p - o)  in the left grip
      frame (R the rotation above, o the left grip origin), and at  S r  in the right grip frame (the grip mirror
      is the same S). The right hand therefore carries  p -> S R S (p - S o): the rotation  R' = S R S  and the
      grip origin  o' = S o  (x negated). S R S is a proper rotation (determinant (-1)(+1)(-1) = +1), and for a
      quaternion (x, y, z, w) it is (x, -y, -z, w): here (-0.5, -0.5, -0.5, 0.5), the cyclic permutation
      model (x, y, z) -> grip (y, z, x): grip X = model +Y (into the palm, the palm faces -Y), grip Y = model +Z
      (toward the wrist), grip Z = model +X (the thumb is on -X, so -Z runs little finger -> thumb). The node
      translation is -R' o' = S (-R o): the left node's translation with x negated, "the left offset mirrored
      across the grip X plane".
    * SCALE. The file is in arbitrary units: from the Hand bone (the wrist crease, z 0.525) to the tip of the middle
      finger (mesh, z -1.622) it is 2.147 units; the knuckle row of the four fingers is about 0.99 units across
      (x -0.458 .. 0.527 at the metacarpals). A real hand is about 0.19 m long and 0.085 m across, which gives
      0.0885 m and 0.0863 m per unit. 0.0875 m per unit makes it 0.188 m long and 0.086 m across. The glove mesh
      is then 0.245 m long including 0.03 m of cuff and arm stub, 0.149 m wide with the thumb and 0.057 m thick.
    * ORIGIN. The model-space point that becomes the grip origin is (x 0.04, y -0.30, z -0.40):
        - across the hand, x 0.04, the middle of the four knuckles (x -0.458 .. 0.527 at the metacarpals);
        - out of the palm, y -0.30: the palm surface is at about y -0.115 in the middle of the palm (-0.147 average
          over the palm; the back of the hand is at about +0.22), and a controller handle about 33 mm across
          (radius about 0.19 units) lies in front of it, so the tube centre is the palm surface minus 0.19;
        - along the hand, z -0.40: the knuckle joints are at z -0.62 .. -0.73 and the wrist crease at z 0.525; the
          tube lies under the proximal finger segments, about 0.25 units (2 cm) on the wrist side of the knuckles.
      The grip origin is then 0.081 m from the wrist crease and 0.02 to 0.03 m behind the knuckle row.
  UNCERTAINTY: the scale is good to about 5 percent (hands differ). The origin is an estimate of where the tube is
  for a Touch, Index or Vive controller: expect +-0.02 m along the hand (z) and +-0.01 m out of the palm (y); the
  headset check tunes it. Written below as original units times the 0.0875 m per unit the files were scaled by,
  so it can be nudged in the glove's units without redoing the arithmetic.
  ```

- **`LEFT_POINTER`**: THE POINTER RAY ON THE HAND: the Pointer Ray starts at the index fingertip of the Virtual Hand and runs along the index finger. Both are a FIXED offset in the grip frame of a hand (so they ride on the GRIP pose, through the same head-relative-to-world transform as the hand itself): the tip is the end of the glove's index finger and the direction is the unit vector from the index knuckle joint (the Lower.001 bone) to that tip. It is the RELAXED rest pose of the glove that is measured, never the curled bones, so the ray does not twitch when the trigger curls the finger. Metres, in the grip frame of the LEFT hand; the right hand is the left with grip-frame X negated, the same mirror as VIRTUAL_HAND_MODEL_TO_GRIP (grip X = out of a left palm, -Y toward the fingertips, -Z little finger to thumb). The ray applies this whenever it is placed, whether or not Virtual Hands are shown or their model loaded.

  Derived by scripts/derive_virtual_hand_pointer.py (Blender 5.2, headless, on Gloved Hand LP.blend, the same file and the same model-to-grip rotation and origin as above; re-run it when VIRTUAL_HAND_MODEL_TO_GRIP changes):

  - the index finger is the set of mesh vertices whose strongest skin weight is Lower.001, Middle.001 or Top.001 (188 vertices); every pose bone is the identity, so the mesh is the relaxed rest pose the .i3d bones compose to;
    - the tip is the centre of the last 3 mm of that mesh along the finger (9 vertices; the single furthest vertex is 1.3 mm from it), at model (0.0476, -0.0210, -0.1255) m in the file's axes; the knuckle joint (Lower.001) is at model (0.0330, 0.0048, -0.0543) m, so the finger from knuckle to tip is 0.077 m long (a real index finger from the knuckle joint is 0.07 to 0.08 m);
    - both carried onto the grip frame by R (p - o) with the LEFT_MODEL_TO_GRIP rotation and origin below: the tip at (-0.0052, -0.0905, -0.0441) and the knuckle at (-0.0311, -0.0193, -0.0295) (the model's bone, as the Virtual Hand puts it there), the direction (0.3349, -0.9232, -0.1885). The finger therefore runs 22.6 degrees off the grip frame's -Y toward +X, because the relaxed glove finger is curled a little toward the palm.

  UNCERTAINTY: the direction does not depend on how the tip is defined to within about 1.4 degrees (to the furthest vertex 0.7, to the end of the Top.001 bone 1.4); measured to the Top.001 joint origin instead it would be 6.4 degrees off, which is why the joint is not used. The tip is good to about 5 mm (the glove finger is 10 mm across). Whether the glove's fingertip sits where the player's real fingertip is depends on the same estimate of the grip origin as the hand itself (+-0.02 m along the hand, +-0.01 m out of the palm, see above), which the headset check tunes. A test checks this table against the model's knuckle bone in the grip frame.

- **`VRCompanion.FINGER_CURL_DIRECTION`**: Which way a curl turns a joint about its own X axis for THIS model: -1, a NEGATIVE rotation about local X is toward the palm (+1 would be the opposite model). The Gloved Hand's bones run along their local -Z, each bone's X axis is the side axis of its finger (perpendicular to the segment, in the plane of the palm), and its relaxed rest pose already flexes every finger joint a little in the NEGATIVE direction (index Lower.001 -5.9, Middle.001 -14.8, Top.001 -21.2 degrees about X relative to the parent). The Blender file agrees: the same bends are toward the side the palm faces (-Z in Blender, -Y in the file). Checked offline on the composed bone transforms of the file: turning a joint by a NEGATIVE angle about its local X moves the rest of the finger toward -Y, the palm side, for all four fingers (the local X axes lie within 25 degrees of the file's X axis, the most for the splayed little finger, and are exactly perpendicular to their segments). Flip this one number if a headset check shows fingers bending back. The right file's bones are the left's mirrored with proper-rotation frames, so the sign is the same for both hands: the offline check folds the fingers of both files and both go toward the palm.
- **`VRCompanion.VIRTUAL_HAND_FINGERS`**: THE JOINT TABLE: the fingers that curl, the analog input of the Motion Controller that drives each, and the model's own names for the bones. `base` is the finger's base bone and `joints` are its three curl joints, base to tip (segment 1, 2 and 3). The thumb is left out on purpose: it stays as the model rests. The thumb's own bones are called Lower, Middle and Top, so a joint is only ever looked for below its finger's base bone, never by its name across the whole hand. A test checks this table against models/VirtualHandLeft.i3d.
- **`discardScene`**: Drops everything this module put into the scene. A Hand Tool hangs below a holder (VRCompanionHandTools.lua): it goes back to the game's placement first, so that deleting the holder does not delete the tool with it.
- **`applyModelToGrip`**: Sets a node to carry a hand's model onto the grip pose frame (VRCompanion.VIRTUAL_HAND_MODEL_TO_GRIP): the model point gripOrigin ends at the node's origin and the model's axes are turned onto the grip axes. The node carries only a rotation and a translation, never a scale: the models are in metres.
- **`applyModelToGrip`, `local cx, cy, cz = q.y * o.z - q.z * o.y + q.w * o.x, q.z * o.x - q.x * o.z +...`**: a point of the model is carried to rotation \* (point - gripOrigin) in the grip frame, so the node's own translation is -rotation \* gripOrigin
- **`buildHands`, `offset`**: holder (the grip pose) -> offset node -> model clone. No node carries a scale (see THE MODELS): the offset node only turns and moves the model onto the grip frame, with the offset of this hand.
- **`curlFingers`, `if holdsTool and joint.input == "grip" then value = VRCompanion.HELD_TOOL_GRI...`**: Closed around a Hand Tool's handle whatever the real grip is; the index finger still follows the trigger.
- **`bodyAnchor`**: The node the hands ride on (see WHERE): the topmost node above `head` (the listener) below the scene root, a vehicle's component in a cab and the player's root on foot; the scene root when that node is hidden or there is none. Also why it is the scene root, or nil.
- **`pinToWheel`**: A hand holding the Steering Wheel is drawn pinned to the rim where it grabbed (Wheel Grab): its world pose replaces the grip pose's in `world`.
- **`updateHand`, `relative`**: The grip pose relative to the head, for the draws until the next update (VRCompanion:drawVirtualHands).
- **`VRCompanion:getVirtualHandHolder`**: The node of a hand's Virtual Hand that carries its placement (the grip pose frame): what a Hand Tool is linked below (VRCompanionHandTools.lua). nil without the hand (the model is not loaded, or the node is gone). The node is there whether or not the hand is shown.
- **`describeEngineGeometry`**: What the engine itself reports about one hand's nodes (world positions and scale), for a headset check that finds the hand the wrong size or in the wrong place. Needs the engine's node query functions; says so when it has none.
- **`traceHands`**: vrVirtualHandsTrace [frames] For the next frames (default 16), logs each shown hand's position in the space of the camera rendering the frame, at the end of the update, after the camera path placed it for a rendered frame (placeVirtualHandsForRender) and at draw time (VRCompanion:drawVirtualHands), with the eye the frame renders, the camera's position in the anchor's space and the frame counter. A hand drawn stale in one eye shows as a hand-in-camera position that differs between the eyes, or between the update and the draw.
- **`VRCompanion:placeVirtualHandsForRender`**: Called by VRCompanion:placeAndBindEyes once it has placed the head (the Cyclopean Audio Listener Node) for a frame, which happens before every frame the engine renders. The game may render a frame with no update before it (under AFR the mod's update runs once per eye pair, so the second eye is rendered after the vehicle moved again: a hand placed only in the update was a sharp copy lagging behind in that eye; placed in the draw it was right at draw time but still lagged, so the draw comes too late for the frame). So each shown hand is placed again here, from its grip pose relative to the head as the update found it (head and hand poses are one snapshot per eye pair) and from where the head is in the world now; a hand holding the Steering Wheel from the rim as it is now.
- **`VRCompanion:placeVirtualHandsForRender`, `self:placeHandToolsForRender()`**: What rides in a hand and is placed by the game's update (the flashlight's light node) follows the hand for this frame too.
- **`registerSubsystem "virtualHands"`**: Registered with the core (the registry is documented in VRCompanionCore.lua). No update hook: updateVirtualHands runs from the Motion Controller poll (after the Menu Pointer, before the Virtual Gamepad's uses). Unload runs in reverse file order: after the Virtual Gamepad and Wheel Grab, which do not touch the hands, and after Hand Tools (their file follows this one, so they reset first), so a tool is given back before the hands it hangs below are deleted; discardScene gives it back too (releaseHandToolsFromHands).

### `scripts/check_virtual_hand_rig.ps1`

- **the file**:

  ```text
  .SYNOPSIS
      Offline check of the Virtual Hand models' skeletons: composes the bone transforms of both .i3d files the way the
      engine does and compares them with the armature of the source Blender file, scaled to metres and, for the right
      hand, mirrored.

  .DESCRIPTION
      The engine takes the bind pose of a skinned shape from the world transforms of its bone nodes as loaded
      (the .shapes file holds no bind matrices), so the skeleton the .i3d composes to must be the skeleton the mesh
      was modelled around. The first export of the Gloved Hand was not: the GIANTS Blender exporter wrote every
      bone with 90 degrees too little about X, the 90 degree turns piled up down each finger, and the composed
      wrist-to-fingertip chain was 0.46 model units long instead of 2.25 (0.040 m instead of 0.197 m in the game).

      The engine also ignores a scale (or a mirror) on a node above a skinned mesh, so the models carry their size
      and handedness themselves: VirtualHandLeft.i3d is the glove scaled to metres (0.0875 m per model unit) and
      VirtualHandRight.i3d is that mirrored across the model's X = 0 plane, with the same bone names.

      This script reads each model, composes every bone's world matrix (Euler angles in degrees, R = Rz * Ry * Rx, the
      convention the exporter writes; the check also composes R = Ry * Rx * Rz and reports how far the two disagree),
      and checks:
   every bone is within -Tolerance metres of where the Blender armature has it at rest (the numbers are the
          bone heads of "Gloved Hand LP.blend" in the exporter's Y-up axes: x, z, -y, times 0.0875, with x negated
          for the right hand),
   the wrist to the first joint of the index and middle fingertip segments ("Top.001", "Top.002") is about
          0.197 and 0.204 m,
   no finger spirals: its chain runs nearly straight from the Hand bone to its Top joint,
   every bone's local rotation is a proper rotation with small bends: the right file's rotations are the left
          file's with the Euler Y and Z angles negated (a mirrored rig), and no rotation is a 90 degree pile-up.
      It exits with 1 when a check fails. With -Curl it also folds the fingers by VRCompanion.FINGER_CURL_DEGREES
      (read from VRCompanionVirtualHands.lua) and prints where the fingertips end up relative to the palm for each
      hand; a curl that does not move the fingertips toward the palm (-Y) fails.

      Run it from the repository root:  powershell -File scripts\check_virtual_hand_rig.ps1 [-Curl] [-Quiet]
      The same expectations are asserted by CompanionModTest (test_virtual_hands.cpp), which runs in CI.
  ```

- **`$limit`**: the thumb base is turned about 106 degrees about Y by the glove's modelling
- **`$expectedDeg`**: the mirror D R D: Euler X stays, Y and Z change sign
- **`Check (($tips[0.2][1] - $tips[0.0][1]) -lt -0.0005 -and ($tips[0.2][2] - $tip...`**: a fifth of a curl is a small bend: the tip must move toward the palm (-Y) and toward the wrist (+Z)

### `scripts/export_virtual_hand.py`

- **`METRES_PER_UNIT`**: the model's units to metres: 0.188 m Hand bone to middle fingertip, 0.086 m across the knuckles
- **`settings.i3D_binaryFiles = False`**: the converter is run below, after the text is adjusted
- **`dcc.isNodeVisible = lambda name: not bpy.data.objects[name].hide_get() and no...`**: Headless Blender has no 3D viewport for the exporter's visibility query.
- **`transform_model`, `linear = matrix.to_3x3()`**: Everything is read before any bone moves (moving a bone moves the heads of its connected children, so reading a head afterwards would transform it twice). z_axis is the bone's roll axis in armature space.
- **`transform_model`, `bone.align_roll(planned[bone.name][2])`**: the mirror maps Y (along the bone) and Z; the bone's X is then fixed by Y x Z = -S x, a proper rotation
- **`report_size`, `length = (bones["Top.002"] - bones["Wrist"]).length`**: Blender Z-up: fingers along +Y; the metric of the file is the same (rotations only)

### `tests/test_virtual_hands.cpp`

- **`CompanionModTest.AVirtualHandRidesWithTheCabWhenTheVehicleMovesAfterTheModsUpdate`**: In the game the vehicle moves AFTER the Companion Mod's update in a frame, and its camera (and the head with it) follows before the frame is drawn. A hand placed at a world position in the update would then be drawn where the cab was, a frame's travel behind: a ghost trailing along the direction of travel. The hand must ride with the cab.
- **`CompanionModTest.AVirtualHandIsShownAndRidesWithTheCabWhenTheCabCamerasHangInAnInvisibleGroup`**: A vehicle file may keep its cameras in a transform group marked invisible: a camera there still renders, but a shape below it is hidden. The head hangs below the cab camera's parent, so a hand linked below the head would vanish in such a cab. It hangs below the deepest visible node above the head instead, and still rides with the cab.
- **`CompanionModTest.AVirtualHandIsStillShownWhenTheNodeCarryingTheHeadIsHidden`**: A component hidden as a whole leaves the hands nothing visible to ride on in the vehicle: they stay at the scene root.
- **`CompanionModTest.AVirtualHandIsPlacedAgainWhenAFrameIsRenderedWithoutAnUpdateAfterTheVehicleMoved`**: The game runs the mod's update once per AFR pair but renders each eye: the second eye's frame is rendered after the vehicle moved again with no update between (vrVirtualHandsTrace, headset check). A hand placed only in the update was a sharp copy lagging behind in that eye, and placing it in the draw came too late for the frame (the hand was right at draw time in both eyes, the copy stayed). The camera update, which places the eyes before every rendered frame, places the hands too.
- **`CompanionModTest.VrVirtualHandsTraceLogsEachShownHandRelativeToTheRenderingCameraAfterTheUpdateAndAtDrawTime`**: A hand drawn stale in one eye only (a headset check under AFR) is found by where it is relative to the camera that renders each frame: vrVirtualHandsTrace logs that after the update and again at draw time, with the eye, for the frames asked for, then stops by itself.
- **`CompanionModTest.TheRightVirtualHandIsTheLeftHandMirroredAcrossTheGripFrameWithNoScaleAnywhereAndNeitherCastsShadows`**: The engine ignores a scale (a size or a mirror) on a node above a skinned mesh, so the hands are real-world size and handed in the FILES: the left Virtual Hand is the left file, the right one the right file (the left model mirrored across the model's X plane). The result in the grip pose frame is still the mirror image across the grip frame's X plane (both hands' grip frames have the same axes), which turns a left hand into a right hand and leaves the finger and thumb directions of the frame alone. Nothing at or above a model is scaled, and nothing is mirrored by a negative scale.
- **`CompanionModTest.BothVirtualHandsSitInTheGripPoseFrameAtRealHandSize`**: The grip pose frame (OpenXR, as ticket #84 states it): origin at the centre of the tube the closed non-thumb fingers form, -Z from the little finger toward the thumb, +X the palm normal pointing INTO the palm for a right hand (so out of the palm for a left hand). The fingers then point along -Y. The hand is real-world size.
- **`ComposedBonePositions`**: The world position of every bone of an .i3d's scene, composed the way the engine composes node transforms: translation and rotation in degrees (Euler, R = Rz \* Ry \* Rx when zyx is true, the convention the GIANTS exporter writes, else R = Ry \* Rx \* Rz) of each TransformGroup below its parent. The scene's first group (the hand's root) is the identity. The skinned mesh's bind pose is this composed skeleton (the .shapes file holds no bind matrices), so it is the skeleton the mesh was modelled around or the hand is distorted in the game. extraXDegrees turns the named bones further about their own X axis (a finger curl). Besides each bone's position the result holds the point endLength along its -Z axis (the way the bones run) under "\<bone>@end". The models are in metres, so positions and lengths are metres.
- **`kMetresPerUnit`**: The shipped models are the Gloved Hand in metres: its original units times this (VRCompanion's scale is gone, the files carry the size). The right file is the left one mirrored across X = 0.
- **`CompanionModTest.TheShippedModelsBoneTransformsComposeToTheSkeletonTheMeshWasModelledAroundInMetres`**: The Gloved Hand's bones as the Blender armature has them at rest ("Gloved Hand LP.blend": each bone's head, in the exporter's Y-up axes x, z, -y), in the mesh's original units. The models' bone transforms must compose to these times 0.0875 m per unit, mirrored across X = 0 for the right hand.
- **`CompanionModTest.TheShippedModelsAreRealHandSizeWristToFingertipChainAsLongAsTheMeshAndNoFingerSpirals`, `EXPECT_NEAR(Distance(bones.at("Wrist"), bones.at("Top.001")), 0.197, 0.005);`**: 2.25 and 2.33 units times 0.0875 m per unit: 0.197 and 0.204 m, the length of a real hand from the wrist to the middle fingertip joint. The first export of the model composed to 0.46 units (0.040 m): a fifth of the mesh.
- **`CompanionModTest.TheRightModelFileIsTheLeftOneMirroredWithTheSameBonesAndNoNodeOfEitherCarriesAScale`**: The size and the handedness are in the files: no node of either model carries a scale (the engine ignores one above a skinned mesh), and the right model is the left one mirrored across X = 0 with the same bone names and the same parents, each bone's rotation D R D (Euler X as it was, Euler Y and Z negated: a proper rotation, a small bend like the left one's, never a 90 degree pile-up) and each position with its x negated.
- **`CompanionModTest.AFullCurlOfEachShippedModelFoldsEveryFingerTowardThePalmAndRestsItsTipOnIt`**: The curl the mod applies (VRCompanion.FINGER_CURL_DIRECTION \* FINGER_CURL_DEGREES about each joint's own X), played on the shipped models' composed bones, for both hands. The models' axes: fingers run along -Z, the palm faces -Y in BOTH files (the right file is mirrored across X, which leaves Y and Z alone). The knuckle line of the fingers is 0.17 units (0.015 m) behind the palm plane (the palm surface is at y -0.115, the knuckles at 0.055). One direction number serves both hands because the right file's bone frames are proper-rotation mirrors of the left's.
- **`CompanionModTest.AFullCurlOfEachShippedModelFoldsEveryFingerTowardThePalmAndRestsItsTipOnIt`, `tipFront`**: the tip rests on the palm: 0.05 to 0.45 units (0.004 to 0.039 m) in front of the knuckle line (the palm plane is at 0.17 units), not through the back of the hand (an earlier 80, 100, 65 degrees ended the index tip behind the line)
- **`CompanionModTest.TheRaysFingertipAndDirectionAreTheIndexFingerOfTheGloveFromItsKnuckleInBothGripFrames`**: The Pointer Ray starts at the index fingertip of the glove and runs along its finger from the knuckle (VRCompanion.VIRTUAL_HAND_POINTER). The table is derived offline from the glove's mesh with the same model-to-grip offset the hand is drawn with, so it must still agree with the knuckle bone, as the hand puts it into the grip frame, and with the glove's mesh. Nudging VIRTUAL_HAND_MODEL_TO_GRIP without deriving the table again fails here.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 46 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
