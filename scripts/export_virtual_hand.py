"""Re-exports the Virtual Hand model (models/VirtualHandLeft.i3d) from the Gloved Hand Blender file, headless,
with the GIANTS Blender exporter's bone transforms corrected.

Why it exists: the GIANTS exporter (io_export_i3d_10_0_2) writes every bone as the parent-relative matrix with the
Euler X angle reduced by 90 degrees (dccBlender.getBoneTranslationRotationScale). That is right for no bone but a
root one: each child's translation already runs along its parent's local -Z, so the extra 90 degrees pile up down a
chain, and the engine composes the skeleton into a heap about 1/5 the size of the mesh. The engine takes the bind
pose of a skinned shape from the bone nodes as loaded (the .shapes file holds none), so the nodes must compose to
the pose the mesh was modelled in. This script replaces that one function with the same matrix without the 90
degree subtraction, then runs the exporter and GIANTS' own i3dConverter on the result.

Run (Blender 4.4 or newer with the GIANTS exporter add-on installed and enabled; the add-on's i3dConverter.exe
is used from its own folder):

    blender -b "Gloved Hand LP.blend" --python scripts/export_virtual_hand.py -- <output folder>

Writes <output folder>/VirtualHandLeft.i3d and VirtualHandLeft.i3d.shapes. The two changes made to the converter's
text output by hand (castsShadows="false", the scene name) are made here too. The .shapes file holds only the mesh,
so it is byte-for-byte reusable when only the bone transforms change. Check the result with
scripts/check_virtual_hand_rig.ps1.
"""
import math
import os
import subprocess
import sys

import bpy

output_dir = os.path.abspath(sys.argv[sys.argv.index("--") + 1])
os.makedirs(output_dir, exist_ok=True)
i3d_path = os.path.join(output_dir, "VirtualHandLeft.i3d")

import io_export_i3d_10_0_2.i3d_export as exporter  # noqa: E402  (available once Blender has loaded the add-on)
import io_export_i3d_10_0_2.dcc.dccBlender as blender_dcc  # noqa: E402

dcc = exporter.dcc

settings = bpy.context.scene.I3D_UIexportSettings
settings.i3D_exportUseSoftwareFileName = False
settings.i3D_exportFileLocation = i3d_path
settings.i3D_binaryFiles = False  # the converter is run below, after the text is adjusted

# Headless Blender has no 3D viewport for the exporter's visibility query.
dcc.isNodeVisible = lambda name: not bpy.data.objects[name].hide_get() and not bpy.data.objects[name].hide_viewport


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

exporter.I3DExport(False)

with open(i3d_path, "r", encoding="iso-8859-1") as handle:
    text = handle.read()
text = text.replace('castsShadows="true"', 'castsShadows="false"')  # the hands cast no shadows
with open(i3d_path, "w", encoding="iso-8859-1", newline="") as handle:
    handle.write(text)

converter = os.path.join(os.path.dirname(exporter.__file__), "util", "i3dConverter.exe")
subprocess.run([converter, "-in", i3d_path, "-out", i3d_path], check=False)

with open(i3d_path, "r", encoding="iso-8859-1") as handle:
    text = handle.read()
text = text.replace('<i3D name="VirtualHandLeft"', '<i3D name="Gloved Hand LP"')  # keep the model's title
with open(i3d_path, "w", encoding="iso-8859-1", newline="") as handle:
    handle.write(text)
print("Virtual Hand exported to", output_dir)
