# -*- coding: utf-8 -*-
"""Blender のヘッドレスで GLB を読み、正面・側面・上面の 3 方向から描いた PNG を出す。

使い方: blender --background --factory-startup --python BlenderPreviewGlb.py -- <in.glb> <出力の接頭辞>
出力: <接頭辞>_front.png・<接頭辞>_side.png・<接頭辞>_top.png(512x512)
GenerateSceneModels.py の --preview から呼ばれる。形と色を目で確かめるための簡易描画で、ゲーム内の見え方ではない。
"""
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
glb_path, prefix = argv[0], argv[1]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb_path)

meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
if not meshes:
    raise SystemExit("GLB にメッシュがありません: " + glb_path)

# 全体の範囲(ワールド座標)
corners = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
low = Vector((min(c.x for c in corners), min(c.y for c in corners), min(c.z for c in corners)))
high = Vector((max(c.x for c in corners), max(c.y for c in corners), max(c.z for c in corners)))
center = (low + high) / 2
radius = max((high - low).length / 2, 1e-4)
print("PREVIEW_BOUNDS low=%s high=%s" % (tuple(low), tuple(high)))

scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 48
scene.render.resolution_x = 512
scene.render.resolution_y = 512
scene.render.film_transparent = False
world = bpy.data.worlds.new("PreviewWorld")
world.use_nodes = True
background = world.node_tree.nodes["Background"]
background.inputs[0].default_value = (0.55, 0.57, 0.6, 1.0)
background.inputs[1].default_value = 1.0
scene.world = world

sun = bpy.data.lights.new("Sun", "SUN")
sun.energy = 3.0
sun_object = bpy.data.objects.new("Sun", sun)
sun_object.rotation_euler = (0.9, 0.2, 0.6)
scene.collection.objects.link(sun_object)

camera_data = bpy.data.cameras.new("Cam")
camera_data.lens = 50
camera = bpy.data.objects.new("Cam", camera_data)
scene.collection.objects.link(camera)
scene.camera = camera

# glTF は +Y 上だが、Blender への取り込みで +Z 上になる。正面は -Y から、側面は +X から、上面は +Z から見る。
distance = radius * 3.2
views = {
    "front": Vector((0, -1, 0.15)),
    "side": Vector((1, 0, 0.15)),
    "top": Vector((0, -0.001, 1)),
}
for name, direction in views.items():
    camera.location = center + direction.normalized() * distance
    look = center - camera.location
    camera.rotation_euler = look.to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = "%s_%s.png" % (prefix, name)
    bpy.ops.render.render(write_still=True)
    print("PREVIEW_WROTE " + scene.render.filepath)
