# 室内の光のシーン（interior-light）の正解の絵（Blender Cycles のパストレース）と、手本の写真と並べた比較画像（1920×1080）を作る。
# タスク SCN-INTERIOR-ROOM。部屋・材質・家具の並び・太陽は Game/GameModes/InteriorLight/InteriorLightRoutine.cpp と同じ値で
# 組み直し、カメラはエンジンの撮影の <視点>.aov.json から取る。比べる経路は荒野の正解の絵と同じく、エンジンの HDR と Cycles の
# 放射輝度に同じ表示変換（Scripts/Terrain/DisplayTransform.py。エンジンの撮影の <視点>.hdr.json の露出とトーンマップ）を掛ける。
#
# 使い方:
#   1. エンジンを 1920×1080 で HDR と AOV つきで撮る:
#      powershell -File Scripts/CaptureScene.ps1 -Scene interior-light -Configuration RelWithDebInfo -RequireAssets
#          -Width 1920 -Height 1080 -Hdr -Aov -OutDir <撮影>
#   2. python Scripts/InteriorLightReference.py --render --capture <撮影> [--samples 1024] [--blender <blender.exe>]
#      全視点を 1 回の Blender で描き、<撮影>/cycles/<視点>.cycles.pfm（放射輝度 cd/m²、線形の Rec.709）と記録 cycles.json を書く。
#   3. python Scripts/InteriorLightReference.py --compare --capture <撮影> [--reference <正解の置き場>] [--expect <仕様>]
#      <撮影>/compare/<視点>.compare.png（左上 エンジン・右上 Cycles・下 2 枚が手本の写真）と、領域ごとの明るさと色の比
#      compare.json を書く。--reference は Cycles の絵（<置き場>/cycles/）を描いた撮影のフォルダ（既定は --capture）で、
#      カメラが今の撮影と同じことを確かめる。--expect は判定の仕様（Scripts/Terrain/ExpectSpecs/interior-*.json）で、
#      行ごとに視点・領域・量（luminance_ratio = 輝度の中央値のエンジン ÷ Cycles、chroma_bg_ratio = 青 ÷ 緑の比の
#      エンジン ÷ Cycles、sunlit_iou = 領域の印と正解の置き場の撮影の印の IoU）と min・max を持ち、外れたら fail にする。
#
# 光の決め方（エンジンと同じ物理の単位。Blender の放射の量をそのまま cd/m²・lux として扱う）:
#   太陽: 高さ 15°・方位 240°。大気の外の照度（SkyAtmosphereParameters の既定の太陽の輝度 × 円盤の立体角）に、
#         Library/Core/Private/Rendering/SkyAtmosphere.cpp の IntegrateOpticalDepth と同じ式の地表までの透過率を掛けた照度の
#         平行光（円盤の視直径も同じ）。
#   空:   Blender の空のテクスチャ（多重散乱の式、太陽の円盤なし）を、エンジンの撮影で窓の外に見える空の画素の輝度に
#         合わせて一様に倍する（倍率は空の画素の輝度の比の中央値。Cycles の試しの描画で測る）。空の明るさの分布の差は残る。
#         --engine-sky <撮影> を付けると、Blender の空の式の代わりに、エンジンが -GameArgs atmosphere-dump で書いた空の放射輝度の
#         LUT（<視点>.atmos.radiance_lut.f32、正距円筒・cd/m²）を荒野の正解（Scripts/Terrain/BlenderReference.py の
#         make_lut_world）と同じ引き方で World に張る。太陽の円盤の texel は周りの空の平均で埋め、円盤は平行光だけが受け持つ。
#         倍率と方位の合わせは要らない（空の明るさの分布も色もエンジンと同じ）。
import argparse
import json
import math
import os
import subprocess
import sys
import time

try:
    import bpy  # noqa: F401  Blender の中で動くときだけある
    IN_BLENDER = True
except ImportError:
    IN_BLENDER = False

import numpy as np

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_BLENDER = r"Y:\AI\blender\blender-5.2.2-windows-x64\blender.exe"
RESULT_DIR = "cycles"
COMPARE_DIR = "compare"
DEFAULT_VIEWS = ("default", "window", "corner")
DEFAULT_SAMPLES = 1024
# 空の倍率を測る試しの描画の標本数（カメラから見える空の画素は背景そのものなので少なくてよい）
CALIBRATION_SAMPLES = 4

# ---- 太陽（InteriorLightRoutine.cpp の kSunAltitudeDegrees・kSunAzimuthDegrees と SkyAtmosphere.h の既定） ----
SUN_ALTITUDE_DEGREES = 15.0
SUN_AZIMUTH_DEGREES = 240.0
SUN_LUMINANCE_NITS = 1.6e9
SOLAR_DISK_SOLID_ANGLE_SR = 6.87e-5
PLANET_RADIUS_M = 6360000.0
ATMOSPHERE_HEIGHT_M = 80000.0
RAYLEIGH_SCALE_HEIGHT_M = 8000.0
MIE_SCALE_HEIGHT_M = 1200.0
MIE_DENSITY_SCALE = 1.0
RAYLEIGH_SCATTERING = (5.802e-6, 13.558e-6, 33.100e-6)
MIE_SCATTERING = 1.0e-4
MIE_ABSORPTION = 1.11e-5
OZONE_ABSORPTION = (0.650e-6, 1.881e-6, 0.085e-6)
OZONE_CENTER_M = 25000.0
OZONE_HALF_WIDTH_M = 15000.0
OPTICAL_DEPTH_STEPS = 64

# ---- 部屋（InteriorLightRoutine.cpp と同じ値。x は窓の壁に沿って北へ、z は窓の壁から東へ、y は上へ。m） ----
ROOM_X = 12.0
ROOM_Z = 8.0
ROOM_HEIGHT = 5.0
WALL_THICKNESS = 0.3
WINDOW_CENTERS_X = (2.5, 6.0, 9.5)
WINDOW_HALF_WIDTH = 1.0
WINDOW_SILL = 0.45
WINDOW_TOP = 3.65
WINDOW_PANE_ROWS = 4
MULLION_HALF_WIDTH = 0.035
MULLION_MIN_Z = -0.2
MULLION_MAX_Z = -0.1
BEAM_COUNT = 5
BEAM_PITCH_X = 2.0
BEAM_HALF_WIDTH = 0.125
BEAM_DEPTH = 0.35
GROUND_SIZE = 600.0
GROUND_HEIGHT = -0.02

# 材質（InteriorLightRoutine.cpp の RoomMaterialSpec と同じ値）。unit_albedo は単位のアルベド（Scripts/MakeInteriorLightAlbedo.py）を
# 張り color を掛ける。roughness_map が偽なら粗さは定数
ROOM_MATERIALS = {
    "plaster": {"asset": "beige_wall_001", "tile": 3.0, "unit_albedo": False, "roughness_map": True, "roughness": 0.0,
                "color": (0.74, 0.69, 0.60)},
    "floor": {"asset": "wood_floor", "tile": 1.7, "unit_albedo": True, "roughness_map": False, "roughness": 0.3,
              "color": (0.3185, 0.1746, 0.0823)},
    "brick": {"asset": "red_brick", "tile": 1.4, "unit_albedo": True, "roughness_map": True, "roughness": 0.0,
              "color": (0.4452, 0.3095, 0.2423)},
    "timber": {"asset": "dark_wood", "tile": 2.0, "unit_albedo": True, "roughness_map": True, "roughness": 0.0,
               "color": (0.2423, 0.0666, 0.0203)},
    "ground": {"asset": "forrest_ground_01", "tile": 2.0, "unit_albedo": True, "roughness_map": True, "roughness": 0.0,
               "color": (0.6514, 0.5333, 0.3712)},
}

# 家具（InteriorLightRoutine.cpp の kFurnitureKinds・kFurniturePlacements と同じ並び。(種類, x, z, 向き[度])）。
# GBuffer の色は単位のアルベド × 倍率で、元のアルベド（GLB の色の画像）を 99 百分位で丸めたものなので、Cycles は GLB の材質のまま使う
FURNITURE_PLACEMENTS = (
    ("rug", 7.75, 3.45, 0.0),
    ("sofa", 8.7, 6.05, 180.0),
    ("floor_lamp", 10.3, 6.3, 0.0),
    ("wooden_table", 2.9, 2.95, 0.0),
    ("wooden_chair", 2.9, 2.33, 0.0),
    ("wooden_chair", 2.9, 3.57, 180.0),
    ("bookshelf", 1.2, 7.68, 180.0),
    ("potted_plant", 11.2, 1.2, 0.0),
)

# 比較画像の下の段に並べる手本の写真（Docs/RenderingValidation/InteriorLightReference.md の「比べる所」）
VIEW_PHOTOS = {
    "default": ("window_01", "window_02"),
    "window": ("gloss_01", "window_02"),
    "corner": ("bleed_01", "window_02"),
}
VIEW_LABELS = {"default": "overview", "window": "window", "corner": "corner"}
PHOTO_DIR = os.path.join(REPO_ROOT, "Assets", "Reference", "InteriorLight")
FONT_PATH = r"C:\Windows\Fonts\meiryo.ttc"
COMPARE_WIDTH = 1920
COMPARE_HEIGHT = 1080

# エンジンの座標（+Y が上、右手系）から Blender の座標（+Z が上）へ: x = X, y = -Z, z = Y
ENGINE_TO_BLENDER = ((1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0))


def engine_to_blender(v):
    return tuple(sum(ENGINE_TO_BLENDER[row][col] * float(v[col]) for col in range(3)) for row in range(3))


def engine_sun_direction(altitude_degrees, azimuth_degrees):
    """太陽へ向かう向き（エンジンの座標。SkyAtmosphere.cpp の MakeSunDirection と同じ: 方位角は +X から +Z へ）"""
    altitude = math.radians(altitude_degrees)
    azimuth = math.radians(azimuth_degrees)
    return (math.cos(altitude) * math.cos(azimuth), math.sin(altitude), math.cos(altitude) * math.sin(azimuth))


def sun_transmittance(altitude_degrees):
    """地表（高度 0）から太陽の向きの大気の上端までの透過率（RGB）。SkyAtmosphere.cpp の IntegrateOpticalDepth と同じ式"""
    radius = PLANET_RADIUS_M
    cosine = math.sin(math.radians(altitude_degrees))
    top = PLANET_RADIUS_M + ATMOSPHERE_HEIGHT_M
    discriminant = radius * radius * (cosine * cosine - 1.0) + top * top
    length = -radius * cosine + math.sqrt(max(discriminant, 0.0))
    rayleigh = mie = ozone = 0.0
    previous = 0.0
    for step in range(OPTICAL_DEPTH_STEPS):
        fraction = (step + 1) / OPTICAL_DEPTH_STEPS
        current = length * fraction * fraction
        middle = 0.5 * (previous + current)
        segment = current - previous
        previous = current
        sample_radius = math.sqrt(radius * radius + middle * middle + 2.0 * radius * middle * cosine)
        altitude = max(sample_radius - PLANET_RADIUS_M, 0.0)
        rayleigh += math.exp(-altitude / RAYLEIGH_SCALE_HEIGHT_M) * segment
        mie += math.exp(-altitude / MIE_SCALE_HEIGHT_M) * segment
        ozone += max(0.0, 1.0 - abs(altitude - OZONE_CENTER_M) / OZONE_HALF_WIDTH_M) * segment
    mie_extinction = (MIE_SCATTERING + MIE_ABSORPTION) * MIE_DENSITY_SCALE * mie
    return tuple(math.exp(-(RAYLEIGH_SCATTERING[i] * rayleigh + mie_extinction + OZONE_ABSORPTION[i] * ozone))
                 for i in range(3))


def sun_light():
    """太陽の平行光の照度（RGB、lux）と視直径[度]"""
    top_lux = SUN_LUMINANCE_NITS * SOLAR_DISK_SOLID_ANGLE_SR
    transmittance = sun_transmittance(SUN_ALTITUDE_DEGREES)
    angular_radius = math.acos(1.0 - SOLAR_DISK_SOLID_ANGLE_SR / (2.0 * math.pi))
    return [top_lux * t for t in transmittance], math.degrees(2.0 * angular_radius), list(transmittance)


def read_pfm_any(path):
    """1 チャンネル（Pf）か 3 チャンネル（PF）の PFM を、画像の上を 0 行目にした float32 で読む"""
    with open(path, "rb") as f:
        data = f.read()
    tokens = []
    pos = 0
    while len(tokens) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos].decode("ascii"))
    pos += 1
    channels = 3 if tokens[0] == "PF" else 1
    width, height, scale = int(tokens[1]), int(tokens[2]), float(tokens[3])
    pixels = np.frombuffer(data, dtype="<f4" if scale < 0 else ">f4", count=width * height * channels, offset=pos)
    shape = (height, width, channels) if channels == 3 else (height, width)
    return pixels.reshape(shape)[::-1].astype(np.float32)


def write_pfm_rgb(path, image):
    height, width, _ = image.shape
    with open(path, "wb") as f:
        f.write(f"PF\n{width} {height}\n-1.0\n".encode("ascii"))
        f.write(np.ascontiguousarray(image[::-1], dtype="<f4").tobytes())


def luminance(image):
    return image[..., 0] * 0.2126 + image[..., 1] * 0.7152 + image[..., 2] * 0.0722


# ---------------------------------------------------------------------------------------------------------------------
# 部屋の形（InteriorLightRoutine.cpp の AppendWorldQuad・AppendBox と同じ面と UV）
# ---------------------------------------------------------------------------------------------------------------------

def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def append_world_quad(mesh, center, axis_u, axis_v, normal, half_u, half_v, uv_u, uv_v, tile):
    """四角形を 1 枚足す（頂点はエンジンの座標、UV はワールドの位置の投影 ÷ 実寸）。面の向きは normal に合わせる"""
    signs = ((-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0))
    corners = [_add(center, _add(_scale(axis_u, su * half_u), _scale(axis_v, sv * half_v))) for su, sv in signs]
    edge1 = tuple(corners[1][i] - corners[0][i] for i in range(3))
    edge2 = tuple(corners[2][i] - corners[0][i] for i in range(3))
    cross = (edge1[1] * edge2[2] - edge1[2] * edge2[1], edge1[2] * edge2[0] - edge1[0] * edge2[2],
             edge1[0] * edge2[1] - edge1[1] * edge2[0])
    if _dot(cross, normal) < 0.0:
        corners.reverse()
    base = len(mesh["positions"])
    for corner in corners:
        mesh["positions"].append(corner)
        # エンジンの UV は画像の上が v = 0、Blender は下が v = 0 なので v を返す
        mesh["uvs"].append((_dot(corner, uv_u) / tile, -_dot(corner, uv_v) / tile))
    mesh["faces"].append((base, base + 1, base + 2, base + 3))


def append_box(mesh, lo, hi, tile):
    center = _scale(_add(lo, hi), 0.5)
    half = _scale(_add(hi, _scale(lo, -1.0)), 0.5)
    unit_x, unit_y, unit_z, down_y = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (0.0, -1.0, 0.0)
    for sign in (-1.0, 1.0):
        append_world_quad(mesh, _add(center, _scale(unit_x, sign * half[0])), unit_z, unit_y, _scale(unit_x, sign),
                          half[2], half[1], unit_z, down_y, tile)
        append_world_quad(mesh, _add(center, _scale(unit_y, sign * half[1])), unit_x, unit_z, _scale(unit_y, sign),
                          half[0], half[2], unit_x, unit_z, tile)
        append_world_quad(mesh, _add(center, _scale(unit_z, sign * half[2])), unit_x, unit_y, _scale(unit_z, sign),
                          half[0], half[1], unit_x, down_y, tile)


def room_meshes():
    """材質の名前 → {positions, uvs, faces}（エンジンの座標）"""
    t = WALL_THICKNESS
    meshes = {name: {"positions": [], "uvs": [], "faces": []} for name in ROOM_MATERIALS}
    plaster = meshes["plaster"]
    tile = ROOM_MATERIALS["plaster"]["tile"]
    append_box(plaster, (-t, 0.0, -t), (ROOM_X + t, WINDOW_SILL, 0.0), tile)
    append_box(plaster, (-t, WINDOW_TOP, -t), (ROOM_X + t, ROOM_HEIGHT, 0.0), tile)
    pier_start = -t
    for center_x in WINDOW_CENTERS_X:
        append_box(plaster, (pier_start, WINDOW_SILL, -t), (center_x - WINDOW_HALF_WIDTH, WINDOW_TOP, 0.0), tile)
        pier_start = center_x + WINDOW_HALF_WIDTH
    append_box(plaster, (pier_start, WINDOW_SILL, -t), (ROOM_X + t, WINDOW_TOP, 0.0), tile)
    append_box(plaster, (-t, 0.0, 0.0), (0.0, ROOM_HEIGHT, ROOM_Z), tile)
    append_box(plaster, (-t, 0.0, ROOM_Z), (ROOM_X + t, ROOM_HEIGHT, ROOM_Z + t), tile)
    append_box(plaster, (-t, ROOM_HEIGHT, -t), (ROOM_X + t, ROOM_HEIGHT + t, ROOM_Z + t), tile)

    append_box(meshes["floor"], (-t, -t, -t), (ROOM_X + t, 0.0, ROOM_Z + t), ROOM_MATERIALS["floor"]["tile"])
    append_box(meshes["brick"], (ROOM_X, 0.0, 0.0), (ROOM_X + t, ROOM_HEIGHT, ROOM_Z), ROOM_MATERIALS["brick"]["tile"])

    timber = meshes["timber"]
    tile = ROOM_MATERIALS["timber"]["tile"]
    for beam in range(1, BEAM_COUNT + 1):
        x = beam * BEAM_PITCH_X
        append_box(timber, (x - BEAM_HALF_WIDTH, ROOM_HEIGHT - BEAM_DEPTH, 0.0), (x + BEAM_HALF_WIDTH, ROOM_HEIGHT, ROOM_Z),
                   tile)
    pane_height = (WINDOW_TOP - WINDOW_SILL) / WINDOW_PANE_ROWS
    for center_x in WINDOW_CENTERS_X:
        append_box(timber, (center_x - MULLION_HALF_WIDTH, WINDOW_SILL, MULLION_MIN_Z),
                   (center_x + MULLION_HALF_WIDTH, WINDOW_TOP, MULLION_MAX_Z), tile)
        for row in range(1, WINDOW_PANE_ROWS):
            y = WINDOW_SILL + row * pane_height
            append_box(timber, (center_x - WINDOW_HALF_WIDTH, y - MULLION_HALF_WIDTH, MULLION_MIN_Z),
                       (center_x + WINDOW_HALF_WIDTH, y + MULLION_HALF_WIDTH, MULLION_MAX_Z), tile)

    append_world_quad(meshes["ground"], (0.5 * ROOM_X, GROUND_HEIGHT, 0.5 * ROOM_Z), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0),
                      (0.0, 1.0, 0.0), 0.5 * GROUND_SIZE, 0.5 * GROUND_SIZE, (1.0, 0.0, 0.0), (0.0, 0.0, 1.0),
                      ROOM_MATERIALS["ground"]["tile"])
    return meshes


# ---------------------------------------------------------------------------------------------------------------------
# Blender の中
# ---------------------------------------------------------------------------------------------------------------------

def blender_texture_path(asset, suffix):
    return os.path.join(REPO_ROOT, "Assets", "Textures", "PolyHaven", asset, f"{asset}_{suffix}")


def blender_room_material(name, spec):
    import bpy
    material = bpy.data.materials.new(f"interior_{name}")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    bsdf = nodes.get("Principled BSDF")
    bsdf.inputs["Metallic"].default_value = 0.0
    uv = nodes.new("ShaderNodeTexCoord")
    color = tuple(spec["color"]) + (1.0,)
    if spec["unit_albedo"]:
        albedo = nodes.new("ShaderNodeTexImage")
        albedo.image = bpy.data.images.load(blender_texture_path(spec["asset"], "diff_unit_4k.png"))
        albedo.image.colorspace_settings.name = "sRGB"
        links.new(uv.outputs["UV"], albedo.inputs["Vector"])
        multiply = nodes.new("ShaderNodeVectorMath")
        multiply.operation = "MULTIPLY"
        links.new(albedo.outputs["Color"], multiply.inputs[0])
        multiply.inputs[1].default_value = tuple(spec["color"])
        links.new(multiply.outputs[0], bsdf.inputs["Base Color"])
    else:
        bsdf.inputs["Base Color"].default_value = color
    if spec["roughness_map"]:
        rough = nodes.new("ShaderNodeTexImage")
        rough.image = bpy.data.images.load(blender_texture_path(spec["asset"], "rough_4k.jpg"))
        rough.image.colorspace_settings.name = "Non-Color"
        links.new(uv.outputs["UV"], rough.inputs["Vector"])
        links.new(rough.outputs["Color"], bsdf.inputs["Roughness"])
    else:
        bsdf.inputs["Roughness"].default_value = spec["roughness"]
    # 法線の画像は DirectX の向き（緑が下）なので、緑を返して OpenGL の向きにする
    normal_image = nodes.new("ShaderNodeTexImage")
    normal_image.image = bpy.data.images.load(blender_texture_path(spec["asset"], "nor_dx_4k.jpg"))
    normal_image.image.colorspace_settings.name = "Non-Color"
    links.new(uv.outputs["UV"], normal_image.inputs["Vector"])
    separate = nodes.new("ShaderNodeSeparateColor")
    links.new(normal_image.outputs["Color"], separate.inputs["Color"])
    invert = nodes.new("ShaderNodeMath")
    invert.operation = "SUBTRACT"
    invert.inputs[0].default_value = 1.0
    links.new(separate.outputs["Green"], invert.inputs[1])
    combine = nodes.new("ShaderNodeCombineColor")
    links.new(separate.outputs["Red"], combine.inputs["Red"])
    links.new(invert.outputs["Value"], combine.inputs["Green"])
    links.new(separate.outputs["Blue"], combine.inputs["Blue"])
    normal_map = nodes.new("ShaderNodeNormalMap")
    links.new(combine.outputs["Color"], normal_map.inputs["Color"])
    links.new(normal_map.outputs["Normal"], bsdf.inputs["Normal"])
    return material


def blender_build_room():
    import bpy
    for name, mesh_data in room_meshes().items():
        mesh = bpy.data.meshes.new(f"interior_{name}")
        positions = [engine_to_blender(p) for p in mesh_data["positions"]]
        # エンジンの座標から Blender の座標への写しは回転（行列式 +1）なので、面の巡回はそのまま
        mesh.from_pydata(positions, [], mesh_data["faces"])
        uv_layer = mesh.uv_layers.new(name="UVMap")
        for polygon in mesh.polygons:
            for loop_index in polygon.loop_indices:
                uv_layer.data[loop_index].uv = mesh_data["uvs"][mesh.loops[loop_index].vertex_index]
        mesh.validate()
        mesh.update()
        obj = bpy.data.objects.new(f"interior_{name}", mesh)
        obj.data.materials.append(blender_room_material(name, ROOM_MATERIALS[name]))
        bpy.context.scene.collection.objects.link(obj)


def blender_place_furniture():
    """GLB を置き場所ごとに読み、模型の底を床（高さ 0）に載せて置く。置いた数を返す"""
    import bpy
    from mathutils import Vector
    placed = 0
    for kind, x, z, yaw in FURNITURE_PLACEMENTS:
        path = os.path.join(REPO_ROOT, "Assets", "Models", "Generated", kind, f"{kind}.glb")
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=path)
        imported = [obj for obj in bpy.data.objects if obj not in before]
        roots = [obj for obj in imported if obj.parent is None]
        bpy.context.view_layer.update()
        min_z = min((obj.matrix_world @ Vector(corner)).z for obj in imported if obj.type == "MESH"
                    for corner in obj.bound_box)
        holder = bpy.data.objects.new(f"furniture_{kind}_{placed}", None)
        bpy.context.scene.collection.objects.link(holder)
        for root in roots:
            root.parent = holder
        # エンジンは模型の底（ローカルの最も低い頂点）を床に載せ、+Y の周りに回す。Blender では +Z の周り
        holder.location = engine_to_blender((x, -min_z, z))
        holder.rotation_euler = (0.0, 0.0, math.radians(yaw))
        placed += 1
    return placed


def blender_setup_device(scene):
    import bpy
    preferences = bpy.context.preferences.addons["cycles"].preferences
    used = []
    try:
        preferences.compute_device_type = "OPTIX"
        preferences.get_devices()
        for device in preferences.devices:
            device.use = device.type == "OPTIX"
            if device.use:
                used.append(device.name)
    except (TypeError, ValueError):
        used = []
    scene.cycles.device = "GPU" if used else "CPU"
    return f"OPTIX {', '.join(used)}" if used else "CPU"


def blender_sky_world(sun_direction_blender):
    """多重散乱の空のテクスチャ（太陽の円盤なし）。返り値は (world, 背景の節, 空の式の名前)"""
    import bpy
    world = bpy.data.worlds.new("interior_sky")
    bpy.context.scene.world = world
    world.use_nodes = True
    nodes = world.node_tree.nodes
    background = nodes.get("Background")
    sky = nodes.new("ShaderNodeTexSky")
    sky_type = None
    for candidate in ("MULTIPLE_SCATTERING", "NISHITA", "SINGLE_SCATTERING"):
        try:
            sky.sky_type = candidate
            sky_type = candidate
            break
        except TypeError:
            continue
    sky.sun_elevation = math.radians(SUN_ALTITUDE_DEGREES)
    world.node_tree.links.new(sky.outputs["Color"], background.inputs["Color"])
    return world, background, sky, sky_type


def blender_render(scene, exr_path, samples, denoise):
    """描いて、合成（RGB）と深度を (H, W, 3)・(H, W) の float32（上の行が 0 行目）で返す"""
    import bpy
    import OpenImageIO as oiio
    for view_layer in scene.view_layers:
        view_layer.use_pass_z = True
    scene.cycles.samples = samples
    scene.cycles.use_denoising = denoise
    scene.render.filepath = exr_path
    bpy.ops.render.render(write_still=True)
    image = oiio.ImageInput.open(exr_path)
    if image is None:
        raise RuntimeError(f"EXR を開けない: {exr_path}")
    # Blender 5.2 の多層 EXR は多パートで、パスごとに別のサブ画像に入る
    channels = {}
    subimage = 0
    while image.seek_subimage(subimage, 0):
        spec = image.spec()
        pixels = np.asarray(image.read_image(subimage, 0, 0, spec.nchannels, "float"), dtype=np.float32).reshape(
            spec.height, spec.width, spec.nchannels)
        for index, name in enumerate(spec.channelnames):
            channels[name] = pixels[..., index]
        subimage += 1
    image.close()

    def channel(suffix):
        for name, values in channels.items():
            if name.endswith(suffix):
                return values
        raise RuntimeError(f"EXR に {suffix} が無い（{sorted(channels)}）")
    rgb = np.stack([channel("Combined.R"), channel("Combined.G"), channel("Combined.B")], axis=-1)
    depth = channel("Depth.Z")
    return rgb, depth


def blender_sky_rotation(scene, sky, sun_direction_blender, device):
    """空のテクスチャの太陽の方位の約束を試しの描画で決める。太陽へ向けた狭い画角のカメラで、候補ごとに中心の明るさを比べる"""
    import bpy
    from mathutils import Vector
    camera_data = bpy.data.cameras.new("sky_probe")
    camera_data.angle = math.radians(20.0)
    camera = bpy.data.objects.new("sky_probe", camera_data)
    scene.collection.objects.link(camera)
    target = Vector(sun_direction_blender)
    camera.location = (0.0, 0.0, 0.0)
    camera.rotation_euler = target.to_track_quat("-Z", "Y").to_euler()
    previous_camera = scene.camera
    scene.camera = camera
    hidden = [obj for obj in scene.objects if obj.type in ("MESH", "EMPTY") and not obj.hide_render]
    for obj in hidden:
        obj.hide_render = True
    width, height = scene.render.resolution_x, scene.render.resolution_y
    scene.render.resolution_x = scene.render.resolution_y = 64
    sky.sun_disc = True
    base = math.atan2(sun_direction_blender[1], sun_direction_blender[0])
    candidates = [base, base - 0.5 * math.pi, base + 0.5 * math.pi, 0.5 * math.pi - base]
    best = None
    for rotation in candidates:
        sky.sun_rotation = rotation % (2.0 * math.pi)
        rgb, _ = blender_render(scene, os.path.join(bpy.app.tempdir, "sky_probe.exr"), 4, False)
        y = luminance(rgb)
        center = float(y[28:36, 28:36].mean())
        edge = float(np.median(y))
        print(f"INTERIOR_REFERENCE sky_probe rotation={math.degrees(rotation):.1f} center={center:.4g} median={edge:.4g}",
              flush=True)
        if best is None or center / max(edge, 1e-12) > best[1]:
            best = (rotation, center / max(edge, 1e-12))
    sky.sun_disc = False
    sky.sun_rotation = best[0] % (2.0 * math.pi)
    for obj in hidden:
        obj.hide_render = False
    scene.render.resolution_x, scene.render.resolution_y = width, height
    scene.camera = previous_camera
    bpy.data.objects.remove(camera)
    return best[0], best[1]


def blender_main(job_path):
    import bpy
    from mathutils import Matrix, Vector
    with open(job_path, encoding="utf-8") as f:
        job = json.load(f)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    device = blender_setup_device(scene)
    scene.cycles.max_bounces = 16
    scene.cycles.diffuse_bounces = 12
    scene.cycles.glossy_bounces = 12
    scene.cycles.transparent_max_bounces = 16
    scene.cycles.sample_clamp_direct = 0.0
    scene.cycles.sample_clamp_indirect = 0.0
    scene.cycles.blur_glossy = 0.0
    scene.cycles.caustics_reflective = True
    scene.cycles.caustics_refractive = True
    scene.cycles.use_adaptive_sampling = True
    scene.cycles.adaptive_threshold = 0.005
    scene.cycles.seed = 1
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    scene.render.film_transparent = False
    scene.render.image_settings.media_type = "MULTI_LAYER_IMAGE"
    scene.render.image_settings.file_format = "OPEN_EXR_MULTILAYER"
    scene.render.image_settings.color_depth = "32"
    scene.view_layers[0].use_pass_z = True
    scene.view_layers[0].use_pass_combined = True

    blender_build_room()
    placed = blender_place_furniture()

    sun_blender = Vector(engine_to_blender(job["sun_direction_engine"])).normalized()
    sun_data = bpy.data.lights.new("sun", "SUN")
    illuminance = job["sun_illuminance_lux"]
    strength = max(illuminance)
    sun_data.energy = strength
    sun_data.color = tuple(c / strength for c in illuminance)
    sun_data.angle = math.radians(job["sun_angle_degrees"])
    sun = bpy.data.objects.new("sun", sun_data)
    # 太陽の光は自分の −Z へ進むので、+Z を太陽へ向ける
    sun.rotation_euler = sun_blender.to_track_quat("Z", "Y").to_euler()
    scene.collection.objects.link(sun)

    if job.get("sky_lut"):
        # エンジンの空の LUT をそのまま張る（倍率と方位の合わせをしない）
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "Terrain"))
        import BlenderReference as BR
        BR.make_lut_world(scene, np.load(job["sky_lut"]))
        sky_type, rotation, contrast = "ENGINE_LUT", 0.0, None
    else:
        world, background, sky, sky_type = blender_sky_world(tuple(sun_blender))
        rotation, contrast = blender_sky_rotation(scene, sky, tuple(sun_blender), device)

    camera_data = bpy.data.cameras.new("view")
    camera_data.clip_start = 0.05
    camera_data.clip_end = 5000.0
    camera_data.sensor_fit = "VERTICAL"
    camera = bpy.data.objects.new("view", camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera

    def set_view(view):
        cam = view["camera"]
        position = engine_to_blender(cam["position"])
        right = engine_to_blender(cam["right"])
        up = engine_to_blender(cam["up"])
        back = tuple(-c for c in engine_to_blender(cam["forward"]))
        camera.matrix_world = Matrix([[right[i], up[i], back[i], position[i]] for i in range(3)] + [[0.0, 0.0, 0.0, 1.0]])
        camera_data.angle = math.radians(float(cam["vertical_fov_degrees"]))
        scene.render.resolution_x = int(view["width"])
        scene.render.resolution_y = int(view["height"])
        scene.render.resolution_percentage = 100

    # 空の倍率: エンジンと Cycles のどちらでも空（深度が無限）の画素の、輝度の比の中央値
    ratios = []
    for view in ([] if job.get("sky_lut") else job["views"]):
        background.inputs["Strength"].default_value = 1.0
        engine_hdr = read_pfm_any(view["engine_hdr"])
        engine_depth = read_pfm_any(view["engine_depth"])
        set_view(view)
        rgb, depth = blender_render(scene, os.path.join(job["out_dir"], f"{view['name']}.calibration.exr"),
                                    CALIBRATION_SAMPLES, False)
        mask = ~np.isfinite(engine_depth) & (depth > 1.0e9)
        engine_y = luminance(engine_hdr)[mask]
        cycles_y = luminance(rgb)[mask]
        keep = cycles_y > 0.0
        if keep.sum() > 0:
            ratios.append(engine_y[keep] / cycles_y[keep])
        print(f"INTERIOR_REFERENCE calibration view={view['name']} sky_pixels={int(keep.sum())}", flush=True)
    if job.get("sky_lut"):
        sky_scale = 1.0
    elif not ratios:
        raise RuntimeError("空の画素がどの視点にも無いので空の倍率を決められない")
    else:
        sky_scale = float(np.median(np.concatenate(ratios)))
        background.inputs["Strength"].default_value = sky_scale
    print(f"INTERIOR_REFERENCE sky_scale={sky_scale:.6g} sky_type={sky_type} rotation={math.degrees(rotation):.2f}",
          flush=True)

    records = []
    for view in job["views"]:
        set_view(view)
        started = time.time()
        rgb, depth = blender_render(scene, os.path.join(job["out_dir"], f"{view['name']}.cycles.exr"), job["samples"], True)
        seconds = time.time() - started
        write_pfm_rgb(os.path.join(job["out_dir"], f"{view['name']}.cycles.pfm"), rgb)
        records.append({"view": view["name"], "width": int(view["width"]), "height": int(view["height"]),
                        "render_seconds": round(seconds, 1)})
        print(f"INTERIOR_REFERENCE render view={view['name']} seconds={seconds:.1f}", flush=True)
    with open(os.path.join(job["out_dir"], "blender_result.json"), "w", encoding="utf-8") as f:
        json.dump({"device": device, "sky_type": sky_type, "sky_rotation_degrees": math.degrees(rotation),
                   "sky_probe_contrast": contrast, "sky_scale": sky_scale, "furniture_placed": placed,
                   "blender_version": bpy.app.version_string, "views": records}, f, ensure_ascii=False, indent=2)


# ---------------------------------------------------------------------------------------------------------------------
# 外（システムの Python）
# ---------------------------------------------------------------------------------------------------------------------

def write_engine_sky_lut(sky_capture, view, out_dir):
    """エンジンの空の放射輝度 LUT（<視点>.atmos.radiance_lut.f32）を読み、太陽の円盤の texel（a = 1）を周りの空の平均で
    埋めて <out_dir>/engine_sky_lut.npy に書く。返り値はそのパス"""
    with open(os.path.join(sky_capture, f"{view}.atmos.json"), encoding="utf-8-sig") as f:
        entry = json.load(f)["radiance_lut"]
    lut = np.fromfile(os.path.join(sky_capture, entry["file"]), dtype="<f4").reshape(entry["shape"]).copy()
    disk = lut[..., 3] > 0.5
    rows, columns = np.nonzero(disk)
    height, width = disk.shape
    for row, column in zip(rows, columns):
        r0, r1 = max(row - 4, 0), min(row + 5, height)
        window_columns = np.arange(column - 4, column + 5) % width
        block = lut[r0:r1][:, window_columns]
        outside = block[..., 3] <= 0.5
        if outside.any():
            lut[row, column, :3] = block[outside][:, :3].mean(axis=0)
    lut[..., 3] = 1.0
    if not np.isfinite(lut).all():
        raise RuntimeError("空の LUT に有限でない値がある")
    path = os.path.join(out_dir, "engine_sky_lut.npy")
    np.save(path, lut.astype(np.float32))
    print(f"INTERIOR_REFERENCE engine_sky lut={entry['shape']} disk_texels={int(disk.sum())} "
          f"zenith_rgb={[round(float(v), 1) for v in lut[0].mean(axis=0)[:3]]}", flush=True)
    return path


def run_render(args):
    capture = os.path.abspath(args.capture)
    out_dir = os.path.join(capture, RESULT_DIR)
    os.makedirs(out_dir, exist_ok=True)
    illuminance, angle, transmittance = sun_light()
    views = []
    for name in args.views.split(","):
        with open(os.path.join(capture, f"{name}.aov.json"), encoding="utf-8-sig") as f:
            aov = json.load(f)
        views.append({"name": name, "camera": aov["camera"], "width": aov["width"], "height": aov["height"],
                      "engine_hdr": os.path.join(capture, f"{name}.hdr.pfm"),
                      "engine_depth": os.path.join(capture, f"{name}.aov.depth.pfm")})
    job = {"views": views, "out_dir": out_dir, "samples": args.samples,
           "sun_direction_engine": engine_sun_direction(SUN_ALTITUDE_DEGREES, SUN_AZIMUTH_DEGREES),
           "sun_illuminance_lux": illuminance, "sun_angle_degrees": angle}
    if args.engine_sky:
        job["sky_lut"] = write_engine_sky_lut(os.path.abspath(args.engine_sky), views[0]["name"], out_dir)
    job_path = os.path.join(out_dir, "job.json")
    with open(job_path, "w", encoding="utf-8") as f:
        json.dump(job, f, ensure_ascii=False, indent=2)
    print(f"INTERIOR_REFERENCE sun_lux={[round(v, 1) for v in illuminance]} transmittance="
          f"{[round(v, 4) for v in transmittance]} angle={angle:.3f} views={len(views)} samples={args.samples}", flush=True)
    started = time.time()
    command = [args.blender, "-b", "--factory-startup", "--python-exit-code", "1",
               "--python", os.path.abspath(__file__), "--", "--blender-job", job_path]
    log_path = os.path.join(out_dir, "blender.log")
    with open(log_path, "w", encoding="utf-8") as log:
        completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    with open(log_path, encoding="utf-8", errors="replace") as log:
        for line in log:
            if line.startswith("INTERIOR_REFERENCE"):
                print(line.rstrip(), flush=True)
    if completed.returncode != 0:
        print(f"INTERIOR_REFERENCE result=fail reason=blender_exit_{completed.returncode} log={log_path}")
        return 1
    with open(os.path.join(out_dir, "blender_result.json"), encoding="utf-8") as f:
        result = json.load(f)
    result.update({"sun_illuminance_lux": illuminance, "sun_transmittance": transmittance, "sun_angle_degrees": angle,
                   "sun_altitude_degrees": SUN_ALTITUDE_DEGREES, "sun_azimuth_degrees": SUN_AZIMUTH_DEGREES,
                   "samples": args.samples, "denoise": "OpenImageDenoise", "seconds": round(time.time() - started, 1)})
    with open(os.path.join(out_dir, "cycles.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    print(f"INTERIOR_REFERENCE result=pass views={len(result['views'])} furniture={result['furniture_placed']} "
          f"sky_scale={result['sky_scale']:.6g} seconds={result['seconds']}")
    return 0


def region_masks(capture, view):
    """領域の印（エンジンの AOV から）: 日向の床・日陰の床・壁・天井・空"""
    normal = read_pfm_any(os.path.join(capture, f"{view}.aov.normal.pfm"))
    depth = read_pfm_any(os.path.join(capture, f"{view}.aov.depth.pfm"))
    sun = read_pfm_any(os.path.join(capture, f"{view}.aov.sun_visibility.pfm"))
    finite = np.isfinite(depth) & (depth < 50.0)
    up = normal[..., 1]
    floor = finite & (up > 0.95)
    return {
        "floor_sunlit": floor & (sun > 0.95),
        "floor_shade": floor & (sun >= 0.0) & (sun < 0.05),
        "wall_shade": finite & (np.abs(up) < 0.1) & (sun >= 0.0) & (sun < 0.05),
        "ceiling": finite & (up < -0.95),
        "interior_all": finite,
    }


def region_stats(image, mask):
    if mask.sum() < 100:
        return None
    pixels = image[mask]
    mean = pixels.mean(axis=0)
    return {"pixels": int(mask.sum()), "median_luminance": float(np.median(luminance(pixels))),
            "mean_rgb": [float(v) for v in mean], "chroma_rg": float(mean[0] / max(mean[1], 1e-9)),
            "chroma_bg": float(mean[2] / max(mean[1], 1e-9))}


def fit_photo(path, width, height):
    """写真を width × height に切り抜いて縮める（中央、縦横比を保つ）"""
    from PIL import Image
    image = Image.open(path).convert("RGB")
    target = width / height
    w, h = image.size
    if w / h > target:
        new_w = int(h * target)
        image = image.crop(((w - new_w) // 2, 0, (w - new_w) // 2 + new_w, h))
    else:
        new_h = int(w / target)
        image = image.crop((0, (h - new_h) // 2, w, (h - new_h) // 2 + new_h))
    return image.resize((width, height), Image.LANCZOS)


def draw_caption(draw, font, xy, text):
    x, y = xy
    box = draw.textbbox((x, y), text, font=font)
    draw.rectangle((box[0] - 6, box[1] - 4, box[2] + 6, box[3] + 4), fill=(0, 0, 0))
    draw.text((x, y), text, font=font, fill=(255, 255, 255))


def camera_mismatch(capture, reference, view):
    """今の撮影と正解の置き場の撮影のカメラの違い（位置 1 mm・向き 1e-4・画角 0.01° を超える項目の名前。同じなら空）"""
    cameras = []
    for folder in (capture, reference):
        with open(os.path.join(folder, f"{view}.aov.json"), encoding="utf-8-sig") as f:
            cameras.append(json.load(f)["camera"])
    differs = []
    for key, tolerance in (("position", 1e-3), ("forward", 1e-4), ("up", 1e-4)):
        if max(abs(a - b) for a, b in zip(cameras[0][key], cameras[1][key])) > tolerance:
            differs.append(key)
    if abs(cameras[0]["vertical_fov_degrees"] - cameras[1]["vertical_fov_degrees"]) > 0.01:
        differs.append("vertical_fov_degrees")
    return differs


def evaluate_expect(spec, results, sunlit_iou):
    """判定の仕様の行を compare の結果に当てる。返すのは（行ごとの記録, 外れの説明の並び）"""
    rows = []
    failures = []
    for row in spec["rows"]:
        view, region, quantity = row["view"], row["region"], row["quantity"]
        value = None
        entry = results.get(view, {}).get("regions", {}).get(region)
        if quantity == "luminance_ratio" and entry:
            value = entry["luminance_ratio"]
        elif quantity == "chroma_bg_ratio" and entry:
            value = entry["engine"]["chroma_bg"] / max(entry["cycles"]["chroma_bg"], 1e-9)
        elif quantity == "sunlit_iou":
            value = sunlit_iou.get((view, region))
        record = {"view": view, "region": region, "quantity": quantity, "value": value,
                  "min": row.get("min"), "max": row.get("max")}
        if value is None:
            record["pass"] = False
            failures.append(f"{view}/{region}/{quantity}: 測れない（領域の画素が足りないか量の名前が違う）")
        else:
            ok = (row.get("min") is None or value >= row["min"]) and (row.get("max") is None or value <= row["max"])
            record["pass"] = bool(ok)
            if not ok:
                failures.append(f"{view}/{region}/{quantity}={value:.4f} が範囲 [{row.get('min')}, {row.get('max')}] の外")
        rows.append(record)
        print(f"INTERIOR_EXPECT view={view} region={region} quantity={quantity} "
              f"value={'n/a' if value is None else f'{value:.4f}'} min={row.get('min')} max={row.get('max')} "
              f"result={'pass' if record['pass'] else 'fail'}")
    return rows, failures


def run_compare(args):
    sys.path.insert(0, os.path.join(REPO_ROOT, "Scripts", "Terrain"))
    import DisplayTransform
    from PIL import Image, ImageDraw, ImageFont
    capture = os.path.abspath(args.capture)
    reference = os.path.abspath(args.reference) if args.reference else capture
    cycles_dir = os.path.join(reference, RESULT_DIR)
    spec = None
    if args.expect:
        with open(args.expect, encoding="utf-8-sig") as f:
            spec = json.load(f)
    out_dir = os.path.join(capture, COMPARE_DIR)
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(cycles_dir, "cycles.json"), encoding="utf-8") as f:
        cycles_record = json.load(f)
    with open(os.path.join(PHOTO_DIR, "manifest.json"), encoding="utf-8-sig") as f:
        photos = {p["id"]: p for p in json.load(f)["photos"]}
    font = ImageFont.truetype(FONT_PATH, 22)
    small = ImageFont.truetype(FONT_PATH, 15)
    half_w, half_h = COMPARE_WIDTH // 2, COMPARE_HEIGHT // 2
    results = {}
    failures = []
    sunlit_iou = {}
    for view in args.views.split(","):
        if reference != capture:
            differs = camera_mismatch(capture, reference, view)
            if differs:
                failures.append(f"{view}: カメラが正解の置き場の撮影と違う（{', '.join(differs)}）")
                continue
        with open(os.path.join(capture, f"{view}.hdr.json"), encoding="utf-8") as f:
            meta = json.load(f)
        params = DisplayTransform.params_from_capture(meta)
        pre_exposure = float(meta["pre_exposure"])
        engine = DisplayTransform.read_pfm(os.path.join(capture, f"{view}.hdr.pfm"))
        cycles = read_pfm_any(os.path.join(cycles_dir, f"{view}.cycles.pfm"))
        if engine.shape != cycles.shape:
            failures.append(f"{view}: 大きさが違う（エンジン {engine.shape}、Cycles {cycles.shape}）")
            continue
        engine_png, _ = DisplayTransform.display_transform(DisplayTransform.expose(engine, pre_exposure), params)
        cycles_png, _ = DisplayTransform.display_transform(DisplayTransform.expose(cycles, pre_exposure), params)
        Image.fromarray(engine_png, "RGB").save(os.path.join(out_dir, f"{view}.engine.png"))
        Image.fromarray(cycles_png, "RGB").save(os.path.join(out_dir, f"{view}.cycles.png"))

        masks = region_masks(capture, view)
        if reference != capture:
            # 正解の置き場の撮影の印と重ねる（窓の日だまりの形・位置が正解を描いたときと同じか）
            reference_masks = region_masks(reference, view)
            for name in ("floor_sunlit", "floor_shade"):
                union = (masks[name] | reference_masks[name]).sum()
                sunlit_iou[(view, name)] = float((masks[name] & reference_masks[name]).sum() / union) if union else None
        regions = {}
        for name, mask in masks.items():
            e = region_stats(engine, mask)
            c = region_stats(cycles, mask)
            if e is None or c is None:
                continue
            regions[name] = {"engine": e, "cycles": c,
                             "luminance_ratio": e["median_luminance"] / max(c["median_luminance"], 1e-9)}
        clip = {"engine_white": float((engine_png.min(axis=-1) >= 254).mean()),
                "engine_black": float((engine_png.max(axis=-1) <= 2).mean()),
                "cycles_white": float((cycles_png.min(axis=-1) >= 254).mean()),
                "cycles_black": float((cycles_png.max(axis=-1) <= 2).mean())}
        results[view] = {"pre_exposure": pre_exposure, "regions": regions, "clip": clip,
                         "mask_iou": {name: value for (v, name), value in sunlit_iou.items() if v == view}}

        canvas = Image.new("RGB", (COMPARE_WIDTH, COMPARE_HEIGHT), (0, 0, 0))
        canvas.paste(Image.fromarray(engine_png, "RGB").resize((half_w, half_h), Image.LANCZOS), (0, 0))
        canvas.paste(Image.fromarray(cycles_png, "RGB").resize((half_w, half_h), Image.LANCZOS), (half_w, 0))
        draw = ImageDraw.Draw(canvas)
        label = VIEW_LABELS.get(view, view)
        draw_caption(draw, font, (12, 10), f"エンジン（interior-light / {label}）")
        draw_caption(draw, font, (half_w + 12, 10),
                     f"Cycles パストレース（{cycles_record['samples']} サンプル・デノイズ、同じ露出と表示変換）")
        for index, photo_id in enumerate(VIEW_PHOTOS.get(view, ())):
            photo = photos[photo_id]
            x = index * half_w
            canvas.paste(fit_photo(os.path.join(PHOTO_DIR, photo["file"]), half_w, half_h), (x, half_h))
            draw_caption(draw, font, (x + 12, half_h + 10), f"手本 {photo_id}")
            draw_caption(draw, small, (x + 12, COMPARE_HEIGHT - 28),
                         f"{photo['credit']} / {photo['author']} / {photo['license']}（Wikimedia Commons）")
        path = os.path.join(out_dir, f"{view}.compare.png")
        canvas.save(path)
        summary = " ".join(f"{name}={value['luminance_ratio']:.3f}" for name, value in regions.items())
        print(f"INTERIOR_COMPARE view={view} out={os.path.relpath(path, REPO_ROOT)} ratio_engine_over_cycles {summary}")
        for name, value in regions.items():
            print(f"INTERIOR_COMPARE view={view} region={name} chroma_bg engine={value['engine']['chroma_bg']:.3f} "
                  f"cycles={value['cycles']['chroma_bg']:.3f} chroma_rg engine={value['engine']['chroma_rg']:.3f} "
                  f"cycles={value['cycles']['chroma_rg']:.3f}")
        print(f"INTERIOR_COMPARE view={view} clip engine_white={clip['engine_white']:.4f} "
              f"engine_black={clip['engine_black']:.4f} cycles_white={clip['cycles_white']:.4f} "
              f"cycles_black={clip['cycles_black']:.4f}")
    expect_rows = None
    if spec is not None:
        expect_rows, expect_failures = evaluate_expect(spec, results, sunlit_iou)
        failures.extend(expect_failures)
        with open(os.path.join(out_dir, "expect.json"), "w", encoding="utf-8") as f:
            json.dump({"expect": os.path.relpath(os.path.abspath(args.expect), REPO_ROOT), "reference": reference,
                       "rows": expect_rows, "failures": expect_failures}, f, ensure_ascii=False, indent=2)
    with open(os.path.join(out_dir, "compare.json"), "w", encoding="utf-8") as f:
        json.dump({"cycles": cycles_record, "reference": reference, "views": results, "expect": expect_rows,
                   "failures": failures}, f, ensure_ascii=False, indent=2)
    for failure in failures:
        print(f"INTERIOR_COMPARE failure: {failure}")
    print(f"INTERIOR_COMPARE result={'fail' if failures else 'pass'} views={len(results)}")
    return 1 if failures else 0


def main():
    if IN_BLENDER:
        argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
        blender_main(argv[argv.index("--blender-job") + 1])
        return 0
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="室内の光のシーンの Cycles の正解の絵と、手本の写真と並べた比較画像を作る")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--render", action="store_true", help="Cycles で全視点を描く")
    group.add_argument("--compare", action="store_true", help="エンジン・Cycles・手本の写真を並べた比較画像を書く")
    parser.add_argument("--capture", required=True, help="エンジンの撮影（-Hdr -Aov つき）のフォルダ")
    parser.add_argument("--views", default=",".join(DEFAULT_VIEWS), help="視点（カンマ区切り）")
    parser.add_argument("--samples", type=int, default=DEFAULT_SAMPLES, help="Cycles の標本数")
    parser.add_argument("--blender", default=DEFAULT_BLENDER, help="blender.exe")
    parser.add_argument("--engine-sky", help="--render: 空をエンジンの空の LUT で描く。-GameArgs atmosphere-dump で撮った置き場")
    parser.add_argument("--reference", help="--compare: Cycles の絵（cycles/）を描いた撮影のフォルダ（既定は --capture）")
    parser.add_argument("--expect", help="--compare: 判定の仕様（領域の比の範囲。Scripts/Terrain/ExpectSpecs/interior-*.json）")
    args = parser.parse_args()
    return run_render(args) if args.render else run_compare(args)


if __name__ == "__main__":
    sys.exit(main())
