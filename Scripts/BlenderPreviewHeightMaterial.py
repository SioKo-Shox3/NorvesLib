# -*- coding: utf-8 -*-
"""Blender のヘッドレスで、高さの地図を持つスキャンの材質を縦の壁へ変位させて描いた PNG を出す。

使い方: blender --background --factory-startup --python BlenderPreviewHeightMaterial.py -- <色> <高さ> <法線(GL)> <1枚の大きさ m> <変位の高さ m> <出力.png>
壁は 1 枚の大きさの正方形を 2×2 に並べた縦の面(繰り返しの見え方も分かる)で、太陽を横の低い角度から当てて層の段と割れ目の凹凸を見せる。
崖に使う素材を手本の写真と並べて選ぶための簡易描画で、ゲーム内の見え方ではない。
"""
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
color_path, height_path, normal_path = argv[0], argv[1], argv[2]
tile_size, displacement_height, output_path = float(argv[3]), float(argv[4]), argv[5]

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# 縦の壁(X が横、Z が上)。2×2 枚ぶんの大きさで、変位のために細かく分ける
bpy.ops.mesh.primitive_plane_add(size=tile_size * 2.0, location=(0.0, 0.0, tile_size), rotation=(1.5708, 0.0, 0.0))
wall = bpy.context.active_object
subdivide = wall.modifiers.new("Subdivide", "SUBSURF")
subdivide.subdivision_type = "SIMPLE"
subdivide.levels = 9
subdivide.render_levels = 9

height_texture = bpy.data.textures.new("Height", "IMAGE")
height_texture.image = bpy.data.images.load(height_path)
height_texture.image.colorspace_settings.name = "Non-Color"
height_texture.extension = "REPEAT"
height_texture.repeat_x = 2
height_texture.repeat_y = 2
displace = wall.modifiers.new("Displace", "DISPLACE")
displace.texture = height_texture
displace.texture_coords = "UV"
displace.strength = displacement_height
displace.mid_level = 0.5

material = bpy.data.materials.new("Scan")
material.use_nodes = True
nodes = material.node_tree.nodes
links = material.node_tree.links
shader = nodes["Principled BSDF"]
shader.inputs["Roughness"].default_value = 0.85
mapping = nodes.new("ShaderNodeMapping")
mapping.inputs["Scale"].default_value = (2.0, 2.0, 1.0)
coordinates = nodes.new("ShaderNodeTexCoord")
links.new(coordinates.outputs["UV"], mapping.inputs["Vector"])
color = nodes.new("ShaderNodeTexImage")
color.image = bpy.data.images.load(color_path)
links.new(mapping.outputs["Vector"], color.inputs["Vector"])
links.new(color.outputs["Color"], shader.inputs["Base Color"])
normal_image = nodes.new("ShaderNodeTexImage")
normal_image.image = bpy.data.images.load(normal_path)
normal_image.image.colorspace_settings.name = "Non-Color"
links.new(mapping.outputs["Vector"], normal_image.inputs["Vector"])
normal_map = nodes.new("ShaderNodeNormalMap")
links.new(normal_image.outputs["Color"], normal_map.inputs["Color"])
links.new(normal_map.outputs["Normal"], shader.inputs["Normal"])
wall.data.materials.append(material)

scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 32
scene.render.resolution_x = 640
scene.render.resolution_y = 640
world = bpy.data.worlds.new("PreviewWorld")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.45, 0.55, 0.7, 1.0)
world.node_tree.nodes["Background"].inputs[1].default_value = 0.6
scene.world = world

# 低い西日のように、左上の横から壁をかすめる太陽
sun = bpy.data.lights.new("Sun", "SUN")
sun.energy = 4.0
sun_object = bpy.data.objects.new("Sun", sun)
sun_object.rotation_euler = (1.2, 0.0, -1.1)
scene.collection.objects.link(sun_object)

camera_data = bpy.data.cameras.new("Cam")
camera_data.lens = 50
camera = bpy.data.objects.new("Cam", camera_data)
camera.location = (tile_size * 0.6, -tile_size * 3.6, tile_size * 0.8)
camera.rotation_euler = (1.5708 + 0.08, 0.0, 0.16)
scene.collection.objects.link(camera)
scene.camera = camera

scene.render.filepath = output_path
bpy.ops.render.render(write_still=True)
print("PREVIEW_WROTE " + output_path)
