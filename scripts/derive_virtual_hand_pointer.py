"""Derives VRCompanion.VIRTUAL_HAND_POINTER (the Pointer Ray's start and direction on the left Virtual Hand) from the
Gloved Hand Blender source. Read-only: it changes no file; it prints the numbers that are pasted into
mods/FS25_VRCompanion/src/VRCompanionVirtualHands.lua.

Run (Blender 4.4 or newer, headless):

    blender -b "Gloved Hand LP.blend" --python scripts/derive_virtual_hand_pointer.py

What it does
  * Takes the glove mesh exactly as modelled, which is the relaxed rest pose the .i3d bones compose to (all pose
    bones are identity; scripts/check_virtual_hand_rig.ps1 checks the .i3d against the same armature). It never uses
    the curled bones: the Pointer Ray must not move when the trigger curls the finger.
  * The index finger is the set of vertices whose strongest skin weight is one of the index finger's bones
    (Lower.001, Middle.001, Top.001).
  * The finger direction is from the index knuckle joint (the head of Lower.001) to the finger's tip, and the tip is
    the furthest point of that finger's mesh along that direction (found by iterating, since the direction depends
    on the tip). The tip point is the centre of the last 3 mm of mesh, so one stray vertex does not move it.
  * Everything is converted to the exporter's Y-up file axes (x, z, -y of Blender), scaled by 0.0875 m per model unit
    (the scale the shipped files carry), and carried onto the LEFT grip frame with the same rotation and origin as
    VRCompanion.VIRTUAL_HAND_MODEL_TO_GRIP.left (a model point p is at  R (p - o)  in the grip frame).
"""
import math

import bpy
import mathutils

METRES_PER_UNIT = 0.0875
INDEX_GROUPS = ("Lower.001", "Middle.001", "Top.001")
TIP_CAP_METRES = 0.003

# VRCompanion.VIRTUAL_HAND_MODEL_TO_GRIP.left, written in the glove's units as in the Lua file.
GRIP_ROTATION = mathutils.Quaternion((0.5, -0.5, 0.5, 0.5))  # Blender order (w, x, y, z)
GRIP_ORIGIN_UNITS = mathutils.Vector((0.04, -0.30, -0.40))

mesh_object = bpy.data.objects["HandModel.001"]
armature = bpy.data.objects["Hand.002"]
mesh = mesh_object.data
groups = {g.index: g.name for g in mesh_object.vertex_groups}


def to_file_axes(v):
    """Blender (Z up, fingers +y) to the exporter's Y-up axes: x, z, -y."""
    return mathutils.Vector((v.x, v.z, -v.y))


finger = []
for vertex in mesh.vertices:
    weights = {groups[g.group]: g.weight for g in vertex.groups}
    if not weights:
        continue
    strongest = max(weights, key=weights.get)
    if strongest in INDEX_GROUPS:
        finger.append(to_file_axes(vertex.co) * METRES_PER_UNIT)
print("index finger vertices:", len(finger))

knuckle = to_file_axes(armature.data.bones["Lower.001"].head_local) * METRES_PER_UNIT
top_joint = to_file_axes(armature.data.bones["Top.001"].head_local) * METRES_PER_UNIT
top_tail = to_file_axes(armature.data.bones["Top.001"].tail_local) * METRES_PER_UNIT

direction = (top_tail - knuckle).normalized()
for _ in range(20):
    best = max(v.dot(direction) - knuckle.dot(direction) for v in finger)
    cap = [v for v in finger if v.dot(direction) - knuckle.dot(direction) >= best - TIP_CAP_METRES]
    tip = sum(cap, mathutils.Vector()) / len(cap)
    new_direction = (tip - knuckle).normalized()
    if (new_direction - direction).length < 1e-9:
        break
    direction = new_direction
extreme = max(finger, key=lambda v: v.dot(direction))

rotation = GRIP_ROTATION
origin = GRIP_ORIGIN_UNITS * METRES_PER_UNIT


def to_grip(point):
    return rotation @ (point - origin)


def show(label, v):
    print("%-34s (%.4f, %.4f, %.4f)" % (label, v.x, v.y, v.z))


print("--- model (file axes, metres, left hand) ---")
show("index knuckle joint (Lower.001)", knuckle)
show("Top.001 joint", top_joint)
show("Top.001 bone tail", top_tail)
show("fingertip (centre of last 3 mm)", tip)
show("fingertip (single extreme vertex)", extreme)
show("finger direction (unit)", direction)
print("finger length knuckle to tip: %.4f m; tip cap vertices: %d" % ((tip - knuckle).length, len(cap)))
# How stable is the direction against the definition used?
for label, other in (("knuckle to Top.001 joint", top_joint), ("knuckle to Top.001 tail", top_tail),
                     ("knuckle to extreme vertex", extreme)):
    print("direction %-26s differs by %.2f deg" % (label, math.degrees(direction.angle((other - knuckle).normalized()))))
print("--- left grip frame (metres) ---")
grip_tip = to_grip(tip)
grip_dir = rotation @ direction
show("fingertip", grip_tip)
show("fingertip (single extreme vertex)", to_grip(extreme))
show("finger direction (unit)", grip_dir)
show("knuckle joint", to_grip(knuckle))
print("angle between finger direction and grip -Z (the tube axis): %.2f deg"
      % math.degrees(grip_dir.angle(mathutils.Vector((0, 0, -1)))))
print("angle between finger direction and grip -Y (toward the fingertips): %.2f deg"
      % math.degrees(grip_dir.angle(mathutils.Vector((0, -1, 0)))))
# Spread of the finger mesh about its axis: how far the tip point is from the finger's own centre line.
axis_offset = [((v - knuckle) - direction * (v - knuckle).dot(direction)).length for v in cap]
print("tip cap radius about the axis: max %.4f m" % max(axis_offset))
