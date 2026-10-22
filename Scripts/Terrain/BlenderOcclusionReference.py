# 小さな遮蔽の比べ（SCN-WLR-OCCLUSION-*）の Blender 側の台本。BlenderReference.py の関数（地形の PLY の読み方・座標の変換・
# カメラ・共通の照明・灰色の材質・深度の描き方）を借り、BlenderReference.py 自体は変えない（参照の記録が script_sha256 を持つ）。
#
# 使い方（ふつうの Python から。視点ごとに Blender をヘッドレスで起動し、この台本を --python で渡す）:
#   python Scripts/Terrain/BlenderOcclusionReference.py --mesh <地形の書き出しのフォルダ> --capture <エンジンの撮影>
#          [--views contact_near] [--case sky|sun] [--range shadow] [--trial] [--spp N] [--out <書き先>]
#
# 入力（書き出しの契約。Docs/RenderingValidation/WastelandOcclusion.md の「比べの書き出しの契約」）:
#   - 地形: Game の --wasteland-export-terrain が書いた区画ごとの LOD 0 の PLY と manifest.json（--mesh。BlenderReference.py と同じ）
#   - 小物: エンジンの撮影（開発ビルドの --wasteland-common-light・--wasteland-gray-material・--wasteland-finest-lod を
#     CaptureScene.ps1 -Hdr -Aov で撮ったもの）のフォルダに Game が書いた occlusion_props.*（WastelandTerrainExport.h）。
#     形はメッシュのローカルの座標の LOD 0 の PLY（occlusion_props.<名前>.ply と occlusion_props.manifest.json）、配置は
#     occlusion_props.placements.csv（mesh, 位置, 四元数 (x, y, z, w), 拡大率, 影を落とすか）。ワールドの位置 =
#     位置 + (四元数の共役の回転)(拡大率 ⊙ ローカルの位置)。崖錐の岩は撮影の視点から 300 m 以内だけが置かれる（Game の
#     kGrayTalusRadius）ので、書き出しの全行をそのまま置く。
#   - カメラ: エンジンの撮影の <視点>.aov.json。照明: CommonLight.json の空（--case sky）か太陽（--case sun）だけ。
#   - 材質: 地形も小物も CommonLight.json の gray_material（Principled BSDF、法線は頂点の補間した法線。エンジンの
#     --wasteland-gray-material と同じ）。
#
# 出力（<書き先>/。既定は <撮影>/cycles-occlusion、--trial なら <撮影>/cycles-occlusion-trial）:
#   <視点>.aux.depth.pfm   カメラの前方向への距離 m（空は +∞。BlenderReference.render_view_depth と同じ描き方）
#   <視点>.aux.normal.pfm  頂点の補間した法線（陰影の法線。エンジンの座標の向き。Cycles の Normal のパス。エンジンの AOV の
#                          normal と同じ意味なので、同じ面の判定の法線の許容差はこちらで比べる）
#   <視点>.aux.geometric_normal.pfm  三角形の面の法線（幾何の法線。エンジンの座標の向き。Geometry の True Normal を
#                          シェーダの AOV で出す。視線の側へ向く。照合は深度から戻した面の向きと合うかを確かめる）
#   <視点>.aux.object.pfm  物体の識別（地形 0、崖錐の岩 1、崖の面の塊 2、そのほかの小物 3、空 −1。Object Index のパス）
#   <視点>.aux.alpha.pfm   面に当たった画素 1
#   <視点>.cycles.hdr.pfm  本描画（線形の RGB。エンジンの HDR と同じ尺度）。--trial は低いサンプル数の試し
#   <視点>.cycles.json     設定・物体の数・時間
#   inputs.json            入力の記録（台本・借りた台本・共通の照明・地形のメッシュ・小物の形と配置のハッシュ、カメラ・照明）
# 照合は Scripts/Terrain/ReferenceCheck.py --occlusion（OcclusionCompare.py）。
import argparse
import csv
import json
import math
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import BlenderReference as BR  # noqa: E402

import numpy as np  # noqa: E402

PROPS_PREFIX = "occlusion_props."
PROPS_MANIFEST = PROPS_PREFIX + "manifest.json"
PROPS_PLACEMENTS = PROPS_PREFIX + "placements.csv"
RESULT_DIR = "cycles-occlusion"
TRIAL_DIR = "cycles-occlusion-trial"
TRIAL_SPP = 64
FULL_SPP = 1024
CASES = ("sky", "sun")
# 照合の画の AOV の部分（<視点>.aux.<部分>.pfm）
AUX_PARTS = ("depth", "normal", "geometric_normal", "object", "alpha")
# 幾何の法線を出すシェーダの AOV の名前
GEOMETRIC_NORMAL_AOV = "GeometricNormal"

# 物体の識別（Object Index のパス）
TERRAIN_INDEX = 0
TALUS_INDEX = 1
CLIFF_SCAN_INDEX = 2
OTHER_PROP_INDEX = 3
TALUS_MESHES = ("boulder_01_4k", "rock_09_4k")
CLIFF_SCAN_PREFIX = "rock_face_"


def object_index_for_mesh(name):
    if name in TALUS_MESHES:
        return TALUS_INDEX
    if name.startswith(CLIFF_SCAN_PREFIX):
        return CLIFF_SCAN_INDEX
    return OTHER_PROP_INDEX


def read_placements(path):
    """occlusion_props.placements.csv を読む。行の辞書の並び"""
    rows = []
    with open(path, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            rows.append({
                "mesh": row["mesh"],
                "position": [float(row["x"]), float(row["y"]), float(row["z"])],
                "rotation": [float(row["qx"]), float(row["qy"]), float(row["qz"]), float(row["qw"])],
                "scale": [float(row["sx"]), float(row["sy"]), float(row["sz"])],
                "cast_shadow": row["cast_shadow"] == "1",
            })
    return rows


def prop_world_matrix(row):
    """エンジンのワールド行列（列ベクトルの 4×4）: 位置 + (四元数の共役の回転)(拡大率 ⊙ ローカル)"""
    x, y, z, w = row["rotation"]
    # 共役 (−x, −y, −z, w) の標準の回転行列
    qx, qy, qz, qw = -x, -y, -z, w
    rotation = np.array([
        [1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw)],
        [2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw)],
        [2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy)],
    ], dtype=np.float64)
    matrix = np.eye(4, dtype=np.float64)
    matrix[:3, :3] = rotation * np.asarray(row["scale"], dtype=np.float64)[None, :]
    matrix[:3, 3] = row["position"]
    return matrix


# ---- ふつうの Python の側 ----

def run(args):
    import ReferenceCheck as RC  # noqa: E402  ふつうの Python の側だけで使う
    mesh_dir = os.path.abspath(args.mesh)
    capture_dir = os.path.abspath(args.capture)
    out_dir = os.path.abspath(args.out) if args.out else os.path.join(capture_dir, TRIAL_DIR if args.trial else RESULT_DIR)
    os.makedirs(out_dir, exist_ok=True)
    light = BR.load_common_light()
    lighting = {
        "case": args.case,
        "sky_radiance_rgb": [float(v) for v in light["sky_radiance_rgb"]] if args.case == "sky" else [0.0, 0.0, 0.0],
        "sun_illuminance_normal_lux": float(light["sun_illuminance_normal_lux"]) if args.case == "sun" else 0.0,
        "sun_angle_degrees": BR.SUN_ANGLE_DEGREES,
    }
    props_manifest_path = os.path.join(capture_dir, PROPS_MANIFEST)
    placements_path = os.path.join(capture_dir, PROPS_PLACEMENTS)
    if not os.path.isfile(props_manifest_path) or not os.path.isfile(placements_path):
        print(f"OCCLUSION_REFERENCE error 撮影のフォルダに {PROPS_MANIFEST}・{PROPS_PLACEMENTS} が無い"
              "（開発ビルドの --wasteland-gray-material で撮る）")
        return 1
    with open(props_manifest_path, encoding="utf-8-sig") as f:
        props_manifest = json.load(f)
    placements = read_placements(placements_path)
    used_meshes = sorted({row["mesh"] for row in placements})
    mesh_files = {tile["name"]: tile["file"] for tile in props_manifest["tiles"]}
    missing = [name for name in used_meshes if name not in mesh_files]
    if missing:
        print(f"OCCLUSION_REFERENCE error 配置の形 {missing} が {PROPS_MANIFEST} に無い")
        return 1
    manifest = BR.load_manifest(mesh_dir)
    spp = args.spp if args.spp else (TRIAL_SPP if args.trial else FULL_SPP)
    views = [v for v in args.views.split(",") if v]
    view_records = {}
    used_tiles = set()
    for view in views:
        camera, width, height = BR.read_engine_camera(capture_dir, view)
        tiles = [t for t in manifest["tiles"] if BR.tile_in_range(t, camera, args.range)[0]]
        altitude, azimuth = BR.common_light_sun(light, view)
        prefix = os.path.join(out_dir, view)
        job = {
            "kind": "occlusion",
            "view": view,
            "camera": camera,
            "width": width,
            "height": height,
            "tiles": [os.path.join(mesh_dir, t["file"]) for t in tiles],
            "props": {name: os.path.join(capture_dir, mesh_files[name]) for name in used_meshes},
            "placements": placements_path,
            "lighting": lighting,
            "to_sun_engine": BR.engine_sun_direction(altitude, azimuth),
            "gray_material": light["gray_material"],
            "spp": spp,
            "prefix": prefix,
        }
        job["required_outputs"] = [f"{prefix}.cycles.json", f"{prefix}.cycles.hdr.pfm"] + \
            [f"{prefix}.aux.{part}.pfm" for part in AUX_PARTS]
        for stale in job["required_outputs"]:
            if os.path.exists(stale):
                os.remove(stale)
        triangles = sum(int(t["triangles"]) for t in tiles)
        print(f"OCCLUSION_REFERENCE view={view} case={args.case} tiles={len(tiles)} triangles={triangles} "
              f"props={len(placements)} meshes={len(used_meshes)} spp={spp} trial={int(args.trial)}", flush=True)
        if not run_blender_job(args, out_dir, view, job):
            print(f"OCCLUSION_REFERENCE error view={view} Blender が失敗した（{os.path.basename(out_dir)}/{view}.blender.log）")
            return 1
        used_tiles.update(t["file"] for t in tiles)
        with open(os.path.join(capture_dir, f"{view}.aov.json"), encoding="utf-8-sig") as f:
            engine_depth_pfm = os.path.join(capture_dir, json.load(f)["depth"]["file"])
        view_records[view] = {"camera": camera, "width": width, "height": height,
                              "tiles": sorted(t["file"] for t in tiles), "triangles": triangles,
                              "sun_altitude_degrees": altitude, "sun_azimuth_degrees": azimuth,
                              "engine_depth_sha256": BR.sha256_file(engine_depth_pfm), "spp": spp,
                              "trial": bool(args.trial)}

    script_dir = os.path.dirname(os.path.abspath(__file__))
    record = {
        "kind": "cycles_occlusion",
        "script": BR.normalize_rel(os.path.abspath(__file__)),
        "script_sha256": BR.sha256_file(os.path.abspath(__file__)),
        "blender_reference_sha256": BR.sha256_file(os.path.join(script_dir, "BlenderReference.py")),
        "common_light_sha256": BR.sha256_file(os.path.join(BR.REPO_ROOT, BR.COMMON_LIGHT)),
        "lighting": lighting,
        "terrain_material": "gray_material（CommonLight.json）",
        "prop_material": "gray_material（CommonLight.json）",
        "git_commit": (RC.git_output(["rev-parse", "HEAD"]) or "").strip(),
        "mesh_dir": BR.normalize_rel(mesh_dir),
        "manifest_sha256": BR.sha256_file(os.path.join(mesh_dir, "manifest.json")),
        "ply_sha256": {name: BR.sha256_file(os.path.join(mesh_dir, name)) for name in sorted(used_tiles)},
        "terrain_mesh_inputs": RC.hash_inputs("terrain_mesh"),
        "capture_dir": BR.normalize_rel(capture_dir),
        "props_manifest_sha256": BR.sha256_file(props_manifest_path),
        "props_placements_sha256": BR.sha256_file(placements_path),
        "props_ply_sha256": {name: BR.sha256_file(os.path.join(capture_dir, mesh_files[name])) for name in used_meshes},
        "props_count": len(placements),
        "object_index": {"terrain": TERRAIN_INDEX, "talus": TALUS_INDEX, "cliff_scan": CLIFF_SCAN_INDEX,
                         "other_prop": OTHER_PROP_INDEX, "sky": -1},
        "range": args.range,
        "coordinates": BR.coordinate_record(),
        "trial": bool(args.trial),
        "views": view_records,
    }
    with open(os.path.join(out_dir, "inputs.json"), "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    print(f"OCCLUSION_REFERENCE record views={len(view_records)} out={BR.normalize_rel(out_dir)}")
    return 0


def run_blender_job(args, out_dir, name, job):
    """job を書いて Blender をヘッドレスで 1 回起動する。成否"""
    job_path = os.path.join(out_dir, f"{name}.job.json")
    with open(job_path, "w", encoding="utf-8") as f:
        json.dump(job, f, indent=2, ensure_ascii=False)
    started = time.time()
    command = [args.blender, "-b", "--factory-startup", "--python-exit-code", "1",
               "--python", os.path.abspath(__file__), "--", "--render-job", job_path]
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    with open(os.path.join(out_dir, f"{name}.blender.log"), "w", encoding="utf-8") as f:
        f.write(completed.stdout)
        f.write(completed.stderr)
    for line in completed.stdout.splitlines():
        if line.startswith("BLENDER_OCCLUSION"):
            print(line, flush=True)
    ok = completed.returncode == 0 and all(os.path.isfile(p) for p in job["required_outputs"])
    print(f"OCCLUSION_REFERENCE job={name} ok={int(ok)} exit={completed.returncode} "
          f"seconds={time.time() - started:.1f}", flush=True)
    if ok:
        os.remove(job_path)
    return ok


# ---- Blender の中の側 ----

def make_aux_material(name):
    """照合の画の材質: 白いランバート面に、Geometry の True Normal（三角形の面の法線）をシェーダの AOV へ書く出力を足す"""
    material = BR.make_diffuse_material(name, 1.0)
    nodes = material.node_tree.nodes
    geometry = nodes.new("ShaderNodeNewGeometry")
    aov = nodes.new("ShaderNodeOutputAOV")
    aov.aov_name = GEOMETRIC_NORMAL_AOV
    material.node_tree.links.new(geometry.outputs["True Normal"], aov.inputs["Color"])
    return material


def blender_to_engine_normal(normal_blender):
    """Blender の向き (x, y, z) をエンジンの向き (x, z, −y) へ直し、長さを 1 にする（面に当たらない画素は 0）"""
    normal = np.stack([normal_blender[..., 0], normal_blender[..., 2], -normal_blender[..., 1]], axis=-1)
    length = np.linalg.norm(normal, axis=-1, keepdims=True)
    return np.where(length > 1e-6, normal / np.maximum(length, 1e-6), 0.0).astype(np.float32)


def render_job(job_path):
    import bpy
    import mathutils

    with open(job_path, encoding="utf-8") as f:
        job = json.load(f)
    GR = BR.load_gray_regions()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    device = BR.setup_cycles_device(scene)
    material, ior = BR.make_gray_material(job["gray_material"])

    started = time.time()
    to_blender = mathutils.Matrix(BR.engine_to_blender_matrix4())
    terrain_triangles = 0
    for path in job["tiles"]:
        name = os.path.splitext(os.path.basename(path))[0]
        positions, triangles, normals = BR.read_terrain_ply(path, with_normals=True)
        triangles, _ = BR.orient_triangles(positions, triangles, normals)
        mesh = BR.build_mesh(name, positions, triangles, normals)
        mesh.materials.append(material)
        obj = bpy.data.objects.new(name, mesh)
        scene.collection.objects.link(obj)
        obj.matrix_world = to_blender
        obj.pass_index = TERRAIN_INDEX
        terrain_triangles += len(triangles)
        del positions, triangles, normals

    # 小物: 形ごとにメッシュを 1 つ作り、配置ごとにそのメッシュを共有する物体を置く（Cycles は同じメッシュを instancing する）
    meshes = {}
    prop_triangles = {}
    for name, path in job["props"].items():
        positions, triangles, normals = BR.read_terrain_ply(path, with_normals=True)
        triangles, _ = BR.orient_triangles(positions, triangles, normals)
        mesh = BR.build_mesh("prop_" + name, positions, triangles, normals)
        mesh.materials.append(material)
        meshes[name] = mesh
        prop_triangles[name] = int(len(triangles))
    placements = read_placements(job["placements"])
    per_index = {}
    for number, row in enumerate(placements):
        obj = bpy.data.objects.new(f"prop_{number}", meshes[row["mesh"]])
        scene.collection.objects.link(obj)
        obj.matrix_world = to_blender @ mathutils.Matrix(prop_world_matrix(row).tolist())
        obj.pass_index = object_index_for_mesh(row["mesh"])
        per_index[obj.pass_index] = per_index.get(obj.pass_index, 0) + 1
    load_seconds = time.time() - started
    print(f"BLENDER_OCCLUSION view={job['view']} loaded terrain_triangles={terrain_triangles} props={len(placements)} "
          f"seconds={load_seconds:.1f}", flush=True)

    matrix, vertical_fov = BR.camera_to_blender(job["camera"])
    camera = bpy.data.cameras.new("EngineCamera")
    camera.type = "PERSP"
    camera.sensor_fit = "VERTICAL"
    camera.angle_y = vertical_fov
    camera.clip_start = float(job["camera"].get("near", 0.1))
    camera.clip_end = BR.CLIP_END_M
    camera_object = bpy.data.objects.new("EngineCamera", camera)
    scene.collection.objects.link(camera_object)
    camera_object.matrix_world = mathutils.Matrix(matrix)
    scene.camera = camera_object

    lighting = job["lighting"]
    world, background = BR.make_uniform_world(scene, lighting["sky_radiance_rgb"])
    if lighting["case"] == "sun":
        BR.make_sun_lamp(scene, lighting["sun_illuminance_normal_lux"], job["to_sun_engine"],
                         lighting["sun_angle_degrees"])
    view_layer = scene.view_layers[0]
    view_layer.use_pass_normal = True
    view_layer.use_pass_object_index = True
    geometric_aov = view_layer.aovs.add()
    geometric_aov.name = GEOMETRIC_NORMAL_AOV
    geometric_aov.type = "COLOR"

    prefix = job["prefix"]
    exr_tmp = prefix + ".render.exr"
    width, height = int(job["width"]), int(job["height"])
    BR.configure_gray_cycles(scene, width, height, exr_tmp)

    # 1. 深度（空は +∞）
    aux_started = time.time()
    depth = BR.render_view_depth(scene, exr_tmp, background)
    # 2. 法線と物体の識別: 白いランバート面を 1 サンプル・画素の中心に寄せたフィルタで描き、Normal（陰影の法線）・
    #    幾何の法線の AOV・Object Index のパスを読む
    view_layer.material_override = make_aux_material("OcclusionAuxWhite")
    background_strength = background.inputs["Strength"].default_value
    BR.set_gray_bounces(scene, 0)
    scene.cycles.samples = 1
    scene.cycles.seed = 0
    scene.cycles.filter_width = BR.PIXEL_FILTER_WIDTH
    channels = BR.render_and_read(scene, exr_tmp)
    view_layer.material_override = None
    background.inputs["Strength"].default_value = background_strength
    BR.set_gray_bounces(scene, BR.GRAY_MAX_BOUNCES)
    scene.cycles.filter_width = BR.GRAY_PIXEL_FILTER_WIDTH
    alpha = channels["ViewLayer.Combined.A"].astype(np.float32)
    normal = blender_to_engine_normal(BR.pass_rgb(channels, "Normal", ("X", "Y", "Z")))
    geometric_normal = blender_to_engine_normal(BR.pass_rgb(channels, GEOMETRIC_NORMAL_AOV))
    index_keys = [key for key in channels if ".Object Index." in key or ".IndexOB." in key]
    if not index_keys:
        raise RuntimeError(f"EXR に Object Index のパスが無い（あるもの: {sorted(channels)}）")
    object_index = np.where(alpha > 0.5, np.rint(channels[index_keys[0]]), -1.0).astype(np.float32)
    aux_seconds = time.time() - aux_started
    GR.write_pfm(prefix + ".aux.depth.pfm", depth)
    GR.write_pfm(prefix + ".aux.normal.pfm", normal)
    GR.write_pfm(prefix + ".aux.geometric_normal.pfm", geometric_normal)
    GR.write_pfm(prefix + ".aux.object.pfm", object_index)
    GR.write_pfm(prefix + ".aux.alpha.pfm", alpha)
    counts = {str(int(v)): int((object_index == v).sum()) for v in np.unique(object_index)}
    print(f"BLENDER_OCCLUSION view={job['view']} aux object_pixels={json.dumps(counts)} seconds={aux_seconds:.1f}",
          flush=True)

    # 3. 本描画（job の spp を 1 回。デノイズなし）
    render_started = time.time()
    scene.cycles.samples = int(job["spp"])
    scene.cycles.seed = 0
    channels = BR.render_and_read(scene, exr_tmp)
    hdr = BR.pass_rgb(channels, "Combined")
    GR.write_pfm(prefix + ".cycles.hdr.pfm", hdr.astype(np.float32))
    render_seconds = time.time() - render_started
    print(f"BLENDER_OCCLUSION view={job['view']} render spp={job['spp']} seconds={render_seconds:.1f}", flush=True)

    record = {
        "view": job["view"],
        "device": device,
        "lighting": lighting,
        "material": {"type": "Principled BSDF", "base_color": float(job["gray_material"]["albedo"]),
                     "roughness": float(job["gray_material"]["roughness"]), "metallic": 0.0, "ior": ior},
        "spp": int(job["spp"]),
        "max_bounces": BR.GRAY_MAX_BOUNCES,
        "pixel_filter_width": BR.GRAY_PIXEL_FILTER_WIDTH,
        "aux_pixel_filter_width": BR.PIXEL_FILTER_WIDTH,
        "aux_normals": {"normal": "陰影の法線（頂点の補間。Normal のパス）",
                        "geometric_normal": f"幾何の法線（三角形の面。True Normal のシェーダの AOV {GEOMETRIC_NORMAL_AOV}）"},
        "terrain_tiles": len(job["tiles"]),
        "terrain_triangles": terrain_triangles,
        "props": len(placements),
        "props_per_object_index": {str(k): v for k, v in sorted(per_index.items())},
        "prop_mesh_triangles": prop_triangles,
        "object_pixels": counts,
        "seconds": {"load": load_seconds, "aux": aux_seconds, "render": render_seconds},
    }
    with open(prefix + ".cycles.json", "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    parser = argparse.ArgumentParser(description="小さな遮蔽の比べの Cycles の正解")
    parser.add_argument("--render-job", help="（Blender の中）job の JSON")
    parser.add_argument("--mesh", help="地形の書き出しのフォルダ（--wasteland-export-terrain）")
    parser.add_argument("--capture", help="エンジンの撮影（--wasteland-gray-material）")
    parser.add_argument("--views", default="contact_near")
    parser.add_argument("--case", choices=CASES, default="sky")
    parser.add_argument("--range", choices=("frustum", "shadow", "all"), default="shadow")
    parser.add_argument("--trial", action="store_true", help=f"試しの描画（既定 {TRIAL_SPP} サンプル）")
    parser.add_argument("--spp", type=int, default=0)
    parser.add_argument("--out", default="")
    parser.add_argument("--blender", default=BR.DEFAULT_BLENDER)
    args = parser.parse_args(argv)
    if args.render_job:
        render_job(args.render_job)
        return 0
    if not args.mesh or not args.capture:
        parser.error("--mesh と --capture を指定する")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
