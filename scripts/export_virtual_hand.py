"""Re-exports the Virtual Hand models (models/VirtualHandLeft.i3d and models/VirtualHandRight.i3d) from the Gloved
Hand Blender file, headless, with the GIANTS Blender exporter's bone transforms corrected, the model scaled to metres
and the right hand mirrored.

Why it exists

1. The rig. The GIANTS exporter (io_export_i3d_10_0_2) writes every bone as the parent-relative matrix with the
   Euler X angle reduced by 90 degrees (dccBlender.getBoneTranslationRotationScale). That is right for no bone but a
   root one: each child's translation already runs along its parent's local -Z, so the extra 90 degrees pile up down
   a chain, and the engine composes the skeleton into a heap about 1/5 the size of the mesh. The engine takes the
   bind pose of a skinned shape from the bone nodes as loaded (the .shapes file holds none), so the nodes must
   compose to the pose the mesh was modelled in. This script replaces that one function with the same matrix
   without the 90 degree subtraction.

2. The size. The Gloved Hand is modelled in arbitrary units (wrist crease to middle fingertip 2.2 units). The engine
   ignores a scale on any ancestor of a skinned mesh (it moves the bones by the scaled positions but draws the mesh
   unscaled), so no node above the model may carry one: the size has to be in the files. The script scales the
   mesh vertices and the armature's bones by METRES_PER_UNIT (0.0875, a hand 0.19 m long and 0.086 m across at the
   knuckles) directly in the Blender data, so the bone translations, the vertices and the bind pose the engine
   derives from the bones are all in metres, and every node's scale stays 1. Rotations are unchanged.

3. The handedness. The glove is a LEFT hand and a negative scale above a skinned mesh is ignored by the engine as
   well, so the right hand is a second file: the left model mirrored across the model's X = 0 plane (the thumb is
   on +X in the left model, -X in the right one; fingers still along -Z, palm still toward -Y). The mesh vertices
   are mirrored and the polygon windings reversed so no face turns inside out (the surface normals the exporter
   computes then point out). The armature is mirrored the same way with the SAME bone names, so Lua keeps one joint
   table. A mirrored bone's local frame is kept a proper right-handed rotation by negating its local X axis (side
   axis) while Y (along the bone) and Z map by the mirror: x' = -S x, y' = S y, z' = S z, with S = diag(-1, 1, 1).
   A joint's parent-relative rotation then becomes  D R D  with D = diag(-1, 1, 1): the Euler angles about X stay,
   those about Y and Z change sign, and a child's translation along the bone is unchanged. A curl about a joint's
   local X axis therefore folds the finger toward the palm in the right file by the SAME sign as in the left one.

Run (Blender 4.4 or newer with the GIANTS exporter add-on installed and enabled; the add-on's i3dConverter.exe
is used from its own folder; the add-on's files and the source .blend are not modified):

    blender -b "Gloved Hand LP.blend" --python scripts/export_virtual_hand.py -- <output folder>

Writes <output folder>/VirtualHandLeft.i3d and .i3d.shapes, then VirtualHandRight.i3d and .i3d.shapes. The two changes
made to the converter's text output by hand (castsShadows="false", the scene name) are made here too. Check the
result with scripts/check_virtual_hand_rig.ps1; this script also prints, per file, the hand's size and how many
triangles' winding agrees with its normals.
"""
import math
import os
import re
import subprocess
import sys

import bpy
import mathutils

METRES_PER_UNIT = 0.0875  # the model's units to metres: 0.188 m Hand bone to middle fingertip, 0.086 m across the knuckles

output_dir = os.path.abspath(sys.argv[sys.argv.index("--") + 1])
os.makedirs(output_dir, exist_ok=True)

import io_export_i3d_10_0_2.i3d_export as exporter  # noqa: E402  (available once Blender has loaded the add-on)
import io_export_i3d_10_0_2.dcc.dccBlender as blender_dcc  # noqa: E402

dcc = exporter.dcc

settings = bpy.context.scene.I3D_UIexportSettings
settings.i3D_exportUseSoftwareFileName = False
settings.i3D_binaryFiles = False  # the converter is run below, after the text is adjusted

# Headless Blender has no 3D viewport for the exporter's visibility query.
dcc.isNodeVisible = lambda name: not bpy.data.objects[name].hide_get() and not bpy.data.objects[name].hide_viewport

ARMATURE = "Hand.002"
MESH = "HandModel.001"


def bone_transform(bone_name, armature_name):
    """The exporter's own bone matrix (parent-relative, axes converted to Y-up), without its extra -90 on X."""
    bone = bpy.data.objects[armature_name].pose.bones[bone_name]
    bake = blender_dcc.bakeTransformMatrix
    matrix = bake(bone.matrix) if not bone.parent else bake(bone.parent.matrix).inverted() @ bake(bone.matrix)
    euler = matrix.to_euler("XYZ")
    return ("%g %g %g" % matrix.to_translation()[:],
            "%g %g %g" % (math.degrees(euler.x), math.degrees(euler.y), math.degrees(euler.z)),
            "%g %g %g" % matrix.to_scale()[:])


dcc.getBoneTranslationRotationScale = bone_transform
blender_dcc.getBoneTranslationRotationScale = bone_transform


def transform_model(matrix):
    """Applies a Blender-space transform (uniform scale, optionally with a mirror in X) to the mesh and the armature
    data directly, so no object carries a scale or a negative scale. A mirror reverses the polygon windings (faces
    keep facing out) and gives each mirrored bone the roll that keeps its frame a proper rotation (see the header)."""
    armature = bpy.data.objects[ARMATURE]
    mesh = bpy.data.objects[MESH].data
    mirrored = matrix.determinant() < 0

    mesh.transform(matrix)
    if mirrored:
        mesh.flip_normals()
    mesh.update()

    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode="EDIT")
    edit_bones = armature.data.edit_bones
    # Everything is read before any bone moves (moving a bone moves the heads of its connected children, so reading
    # a head afterwards would transform it twice). z_axis is the bone's roll axis in armature space.
    linear = matrix.to_3x3()
    planned = {bone.name: (matrix @ bone.head, matrix @ bone.tail, (linear @ bone.z_axis).normalized())
               for bone in edit_bones}
    for bone in edit_bones:
        bone.head, bone.tail = planned[bone.name][0], planned[bone.name][1]
    for bone in edit_bones:
        # the mirror maps Y (along the bone) and Z; the bone's X is then fixed by Y x Z = -S x, a proper rotation
        bone.align_roll(planned[bone.name][2])
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.update()


def export(name):
    """Exports the current scene as models/<name>.i3d (and .shapes) in the output folder."""
    i3d_path = os.path.join(output_dir, name + ".i3d")
    settings.i3D_exportFileLocation = i3d_path
    exporter.I3DExport(False)

    with open(i3d_path, "r", encoding="iso-8859-1") as handle:
        text = handle.read()
    text = text.replace('castsShadows="true"', 'castsShadows="false"')  # the hands cast no shadows
    with open(i3d_path, "w", encoding="iso-8859-1", newline="") as handle:
        handle.write(text)
    report_winding(name, text)

    converter = os.path.join(os.path.dirname(exporter.__file__), "util", "i3dConverter.exe")
    subprocess.run([converter, "-in", i3d_path, "-out", i3d_path], check=False)

    with open(i3d_path, "r", encoding="iso-8859-1") as handle:
        text = handle.read()
    text = text.replace('<i3D name="%s"' % name, '<i3D name="Gloved Hand LP"')  # keep the model's title
    with open(i3d_path, "w", encoding="iso-8859-1", newline="") as handle:
        handle.write(text)


def report_winding(name, text):
    """Looks at the exporter's text shape (before the converter packs it): per triangle, the geometric normal of its
    winding against the sum of its three vertex normals. A model whose faces face out agrees on (nearly) all of them
    one way or the other; the left file's agreement is what the right file must show too (a mirror with the windings
    left alone would show the opposite of it)."""
    vertices = [([float(v) for v in p.split()], [float(v) for v in n.split()])
                for p, n in re.findall(r'<v p="([^"]*)" n="([^"]*)"', text)]
    triangles = [[int(v) for v in vi.split()] for vi in re.findall(r'<t vi="([^"]*)"', text)]
    if not vertices or not triangles:
        print("%s: no text shape to check the winding of" % name)
        return
    agree = 0
    for a, b, c in triangles:
        pa, pb, pc = (mathutils.Vector(vertices[i][0]) for i in (a, b, c))
        geometric = (pb - pa).cross(pc - pa)
        vertex_normal = sum((mathutils.Vector(vertices[i][1]) for i in (a, b, c)), mathutils.Vector())
        if geometric.dot(vertex_normal) > 0:
            agree += 1
    print("%s: %d of %d triangles wind counter-clockwise around their vertex normals" % (name, agree, len(triangles)))

def report_size(name):
    armature = bpy.data.objects[ARMATURE]
    bones = {bone.name: bone.head_local for bone in armature.data.bones}
    # Blender Z-up: fingers along +Y; the metric of the file is the same (rotations only)
    length = (bones["Top.002"] - bones["Wrist"]).length
    hand_length = (bones["Top.002"] - bones["Hand"]).length
    knuckle = (bones["Lower.001"] - bones["Lower.004"]).length
    print("%s: Wrist to Top.001 %.4f m, Wrist to Top.002 %.4f m, Hand to Top.002 %.4f m, Lower.001 to Lower.004 %.4f m"
          % (name, (bones["Top.001"] - bones["Wrist"]).length, length, hand_length, knuckle))
    vertices = [vertex.co for vertex in bpy.data.objects[MESH].data.vertices]
    low = [min(v[axis] for v in vertices) for axis in range(3)]
    high = [max(v[axis] for v in vertices) for axis in range(3)]
    print("%s: mesh x %.4f .. %.4f, y %.4f .. %.4f, z %.4f .. %.4f m (Blender axes: fingers +y, palm -z, thumb at %s x)"
          % (name, low[0], high[0], low[1], high[1], low[2], high[2], "+" if bones["Lower"].x > 0 else "-"))


# The left hand: the glove as modelled, in metres.
transform_model(mathutils.Matrix.Scale(METRES_PER_UNIT, 4))
export("VirtualHandLeft")
report_size("VirtualHandLeft")

# The right hand: the left one mirrored across X = 0 (the model's own X plane).
mirror = mathutils.Matrix.Diagonal((-1.0, 1.0, 1.0, 1.0))
transform_model(mirror)
export("VirtualHandRight")
report_size("VirtualHandRight")

print("Virtual Hands exported to", output_dir)
