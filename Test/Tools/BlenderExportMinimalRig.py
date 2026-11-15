"""2関節の骨格と三角形を作り、BlenderのglTF書き出しを再現する。"""

import math
from pathlib import Path
import sys


def main():
    """Blenderの「--」以降で指定されたパスにglTFを書き出す。"""
    arguments = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if len(arguments) != 1 or not arguments[0]:
        print('出力先を指定してください: -- <出力先.gltf>', file=sys.stderr)
        return 1

    try:
        import bpy
    except ImportError:
        print('bpyがありません。このスクリプトはBlenderの中で実行してください。', file=sys.stderr)
        return 1

    output = Path(arguments[0]).expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.name = 'Scene'
    scene.render.fps = 24
    scene.render.fps_base = 1.0
    scene.frame_start = 0
    scene.frame_end = 48
    scene.frame_set(0)

    armature_data = bpy.data.armatures.new('Armature')
    armature = bpy.data.objects.new('Armature', armature_data)
    scene.collection.objects.link(armature)
    bpy.context.view_layer.objects.active = armature
    armature.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    root = armature_data.edit_bones.new('Bone')
    root.head = (0, 0, 0)
    root.tail = (0, 0, 1)
    child = armature_data.edit_bones.new('Bone.001')
    child.head = (0, 0, 1)
    child.tail = (0, 0, 2)
    child.parent = root
    child.use_connect = True
    bpy.ops.object.mode_set(mode='OBJECT')
    armature.rotation_euler = (math.radians(90), 0, 0)
    armature.scale = (0.5, 0.5, 0.5)
    armature.location = (1, 2, 0)

    mesh = bpy.data.meshes.new('Triangle')
    mesh.from_pydata([(0, 0, 0), (1, 0, 0), (0, 0, 1)], [], [(0, 1, 2)])
    mesh.update()
    triangle = bpy.data.objects.new('Triangle', mesh)
    scene.collection.objects.link(triangle)
    triangle.parent = armature
    root_group = triangle.vertex_groups.new(name='Bone')
    child_group = triangle.vertex_groups.new(name='Bone.001')
    for index, (root_weight, child_weight) in enumerate(((0.75, 0.25), (0.5, 0.5), (1.0, 0.0))):
        root_group.add([index], root_weight, 'REPLACE')
        if child_weight > 0:
            child_group.add([index], child_weight, 'REPLACE')
    modifier = triangle.modifiers.new(name='Armature', type='ARMATURE')
    modifier.object = armature

    root_pose = armature.pose.bones['Bone']
    child_pose = armature.pose.bones['Bone.001']
    root_pose.rotation_mode = 'QUATERNION'
    # ポーズの位置は静止姿勢からの差分、quaternionはBlenderの(w, x, y, z)順。
    for frame, location, rotation in (
        (0, (0, 0, 0), (1, 0, 0, 0)),
        (48, (0, 2, 0), (0, 0, 0, 1)),
    ):
        scene.frame_set(frame)
        child_pose.location = location
        child_pose.keyframe_insert(data_path='location', frame=frame)
        root_pose.rotation_quaternion = rotation
        root_pose.keyframe_insert(data_path='rotation_quaternion', frame=frame)

    action = armature.animation_data.action
    action.name = 'ArmatureAction'
    # Blender 4.4以降の層つきActionを通して補間を指定する。
    for layer in action.layers:
        for strip in layer.strips:
            for channelbag in strip.channelbags:
                for curve in channelbag.fcurves:
                    for key in curve.keyframe_points:
                        key.interpolation = 'LINEAR'
    scene.frame_set(0)
    bpy.ops.export_scene.gltf(
        filepath=str(output),
        export_format='GLTF_SEPARATE',
        export_skins=True,
        export_animations=True,
        export_yup=True,
        export_force_sampling=False,
    )
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
