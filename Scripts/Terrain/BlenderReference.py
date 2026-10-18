# 荒野の正解の絵の比較（SCN-WLR-*、計画は Docs/Plans/WastelandRebuild.md）の Blender 側の台本。
# エンジンが描く地形のメッシュ（Game の --wasteland-export-terrain が書いた区画ごとの LOD 0 の PLY）を Blender へ読み込み、
# エンジンの撮影と同じカメラを置いて、Cycles で描く。
#
# 使い方（ふつうの Python から。視点ごとに Blender をヘッドレスで起動し、この台本を --python で渡す）:
#   python Scripts/Terrain/BlenderReference.py --depth --mesh <書き出しのフォルダ> --capture <エンジンの撮影>
#          [--views a,b,...] [--range view|all] [--blender <blender.exe>]
#       エンジンの撮影（CaptureScene.ps1 -Aov）の <視点>.aov.json のカメラ（位置・向き・縦の画角・縦横比・画素数）を置き、
#       地形の深度（カメラの前方向への距離 m。エンジンの AOV の depth と同じ意味）を Cycles の 1 サンプルで描いて、
#       <撮影>/blender/<視点>.depth.pfm（下の行から並ぶ float32 の 1 チャンネル。空は +∞）と <視点>.json を書く。
#       最後に入力の記録 <撮影>/blender/inputs.json（台本・メッシュの一覧と PLY・地形のメッシュの元の入力のハッシュ、視点のカメラ）
#       を書く。照合は Scripts/Terrain/ReferenceCheck.py --projection。
#
# 座標の変換（m・軸・標高の基準・原点）はこの台本の「座標の変換」の節だけで行う。
# 読む区画（--range）: frustum（既定。深度に要るのは見える物だけ）は視錐台に掛かる区画、shadow はそれに加えてカメラから水平に
#   SHADOW_RANGE_M（影の届く 4 km）以内に掛かる区画（照明を比べるとき）、all は全区画。
#   書き出しは全区画（近景の DEM・遠景の段の全部）なので、影・空の遮蔽の届く範囲はすべて書き出しに入っている。
#   全量（LOD 0 で約 1.5 億三角形）を Blender に読むとメモリが 60 GB を超えるので、視点ごとに読む範囲を絞る。
# 読み方: PLY は Blender の読み込み（bpy.ops.wm.ply_import）でも読めるが、法線を角ごとの法線として持ち、1 億三角形で数十 GB に
#   なるので、深度では書き出しの決まった並びを numpy で読み、位置と三角形だけでメッシュを作る（read_terrain_ply）。
#
# 深度の描き方: 地形の全区画に、Cycles の Camera Data の View Z Depth（カメラの前方向への距離）をそのまま放射する材質を掛け、
#   放射だけを 1 サンプルで描く（跳ね返り 0・デノイズなし）。画素の中の標本の位置は箱のフィルタの幅 PIXEL_FILTER_WIDTH 画素で
#   画素の中心に寄せる（エンジンの GBuffer は画素の中心の値）。空（何にも当たらない画素）は透明の背景で見分ける。
#   Blender 5.2 の多層 EXR には Z のパスが書かれないので、パスは使わない。
import argparse
import hashlib
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

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT_BLENDER = r"Y:\AI\blender\blender-5.2.2-windows-x64\blender.exe"
HEIGHT_DESCRIPTOR = "Assets/Terrain/ValleyOfTheGods/height.json"
# 影の届く範囲[m]（太陽の低い視点で、崖の影が届く距離として計画で決めた値）
SHADOW_RANGE_M = 4000.0
# 視錐台で区画を選ぶときの余裕（画角の正接に掛ける）
FRUSTUM_MARGIN = 1.02
# 画素の中の標本を中心へ寄せる箱のフィルタの幅[画素]（Cycles の最小）
PIXEL_FILTER_WIDTH = 0.01
# Blender のカメラの遠い面[m]（エンジンの撮影の far 400 km より遠く）
CLIP_END_M = 1.0e6
RESULT_DIR = "blender"


# ---- 座標の変換（この台本でここだけ） ----
# エンジンのワールド: m、+X 東・+Y 上・+Z 南の右手系。原点は height.json の origin_utm（UTM 12N の east・north）で、
#   Y = 0 は NAVD88 の標高 height_offset_m（X = east − origin.east、Z = −(north − origin.north)、Y = NAVD88 − height_offset_m）。
# Blender: m、+X 東・+Y 北・+Z 上の右手系。原点と標高の基準はエンジンと同じにする（x = X、y = −Z、z = Y）。
#   UTM と NAVD88 へは east = x + origin.east、north = y + origin.north、NAVD88 = z + height_offset_m。
ENGINE_TO_BLENDER = ((1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0))


def engine_to_blender(v):
    """エンジンのワールドの位置か向き（3 成分）を Blender の座標へ"""
    return tuple(sum(ENGINE_TO_BLENDER[row][col] * float(v[col]) for col in range(3)) for row in range(3))


def engine_to_blender_matrix4():
    """エンジンの座標のままの頂点を持つ物体に掛ける 4×4 の変換（行の並び）"""
    return [list(ENGINE_TO_BLENDER[0]) + [0.0], list(ENGINE_TO_BLENDER[1]) + [0.0],
            list(ENGINE_TO_BLENDER[2]) + [0.0], [0.0, 0.0, 0.0, 1.0]]


def camera_to_blender(camera):
    """エンジンのカメラ（aov.json の camera）を Blender のカメラの姿勢（4×4 の行の並び）と縦の画角[ラジアン]へ。
    Blender のカメラは自分の −Z を向き、+Y が上、+X が右"""
    position = engine_to_blender(camera["position"])
    right = engine_to_blender(camera["right"])
    up = engine_to_blender(camera["up"])
    back = tuple(-c for c in engine_to_blender(camera["forward"]))
    matrix = [[right[i], up[i], back[i], position[i]] for i in range(3)] + [[0.0, 0.0, 0.0, 1.0]]
    return matrix, math.radians(float(camera["vertical_fov_degrees"]))


def coordinate_record():
    """記録に書く座標の約束（height.json の原点と標高の基準を添える）"""
    with open(os.path.join(REPO_ROOT, HEIGHT_DESCRIPTOR), encoding="utf-8-sig") as f:
        height = json.load(f)
    return {
        "engine": "m, +X east, +Y up, +Z south (right-handed)",
        "blender": "m, +X east, +Y north, +Z up (right-handed); x = X, y = -Z, z = Y",
        "origin_utm": height.get("origin_utm"),
        "crs": height.get("crs"),
        "height_offset_m": height.get("height_offset_m"),
        "vertical_datum": height.get("vertical_datum"),
    }


# ---- ふつうの Python の側（区画を選び、視点ごとに Blender を起動して、記録を書く） ----

def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 24), b""):
            digest.update(block)
    return digest.hexdigest()


def load_manifest(mesh_dir):
    path = os.path.join(mesh_dir, "manifest.json")
    with open(path, encoding="utf-8-sig") as f:
        manifest = json.load(f)
    if manifest.get("failed_tiles"):
        raise RuntimeError(f"書き出しに失敗した区画がある（{manifest['failed_tiles']}）")
    return manifest


def tile_in_range(tile, camera, range_mode):
    """区画（manifest の境界の箱。エンジンの座標）を読むか。(読むか, 理由)"""
    if range_mode == "all":
        return True, "all"
    lo = np.asarray(tile["bounds_min"], dtype=np.float64)
    hi = np.asarray(tile["bounds_max"], dtype=np.float64)
    eye = np.asarray(camera["position"], dtype=np.float64)
    if range_mode == "shadow":
        # 水平の距離（X・Z の箱への最短）
        dx = max(lo[0] - eye[0], 0.0, eye[0] - hi[0])
        dz = max(lo[2] - eye[2], 0.0, eye[2] - hi[2])
        if math.hypot(dx, dz) <= SHADOW_RANGE_M:
            return True, "shadow_range"
    # 視錐台: 箱の 8 隅がすべて同じ面の外なら外
    forward = np.asarray(camera["forward"], dtype=np.float64)
    up = np.asarray(camera["up"], dtype=np.float64)
    right = np.asarray(camera["right"], dtype=np.float64)
    tan_v = math.tan(math.radians(float(camera["vertical_fov_degrees"])) / 2.0) * FRUSTUM_MARGIN
    tan_h = tan_v * float(camera["aspect_ratio"])
    planes = [forward, forward * tan_h - right, forward * tan_h + right, forward * tan_v - up, forward * tan_v + up]
    corners = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]) - eye
    for normal in planes:
        if np.all(corners @ normal < 0.0):
            return False, "outside"
    return True, "frustum"


def read_engine_camera(capture_dir, view):
    path = os.path.join(capture_dir, f"{view}.aov.json")
    with open(path, encoding="utf-8-sig") as f:
        record = json.load(f)
    return record["camera"], int(record["width"]), int(record["height"])


def views_from_metrics(capture_dir):
    with open(os.path.join(capture_dir, "metrics.json"), encoding="utf-8-sig") as f:
        metrics = json.load(f)
    views = metrics.get("views") or []
    if isinstance(views, dict):
        views = [views]
    return [v["view"] for v in views if isinstance(v, dict) and v.get("view")]


def run_depth(args):
    mesh_dir = os.path.abspath(args.mesh)
    capture_dir = os.path.abspath(args.capture)
    out_dir = os.path.join(capture_dir, RESULT_DIR)
    os.makedirs(out_dir, exist_ok=True)
    manifest = load_manifest(mesh_dir)
    views = [v for v in args.views.split(",") if v] if args.views else views_from_metrics(capture_dir)
    if not views:
        print("BLENDER_REFERENCE error 視点が無い")
        return 1

    used_tiles = set()
    view_records = {}
    for view in views:
        camera, width, height = read_engine_camera(capture_dir, view)
        tiles = []
        reasons = {}
        for tile in manifest["tiles"]:
            take, reason = tile_in_range(tile, camera, args.range)
            if take:
                tiles.append(tile)
                reasons[reason] = reasons.get(reason, 0) + 1
        triangles = sum(int(t["triangles"]) for t in tiles)
        job = {
            "view": view,
            "camera": camera,
            "width": width,
            "height": height,
            "tiles": [os.path.join(mesh_dir, t["file"]) for t in tiles],
            "out_pfm": os.path.join(out_dir, f"{view}.depth.pfm"),
            "out_json": os.path.join(out_dir, f"{view}.json"),
            "exr": os.path.join(out_dir, f"{view}.depth.exr"),
        }
        job_path = os.path.join(out_dir, f"{view}.job.json")
        with open(job_path, "w", encoding="utf-8") as f:
            json.dump(job, f, indent=2, ensure_ascii=False)
        for stale in (job["out_pfm"], job["out_json"]):
            if os.path.exists(stale):
                os.remove(stale)
        print(f"BLENDER_REFERENCE view={view} tiles={len(tiles)} triangles={triangles} range={args.range} "
              f"reasons={json.dumps(reasons, sort_keys=True)}", flush=True)
        started = time.time()
        command = [args.blender, "-b", "--factory-startup", "--python-exit-code", "1",
                   "--python", os.path.abspath(__file__), "--",
                   "--render-job", job_path]
        completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
        with open(os.path.join(out_dir, f"{view}.blender.log"), "w", encoding="utf-8") as f:
            f.write(completed.stdout)
            f.write(completed.stderr)
        if completed.returncode != 0 or not os.path.isfile(job["out_pfm"]) or not os.path.isfile(job["out_json"]):
            print(f"BLENDER_REFERENCE error view={view} Blender が失敗した（終了コード {completed.returncode}。"
                  f"{RESULT_DIR}/{view}.blender.log）")
            return 1
        print(f"BLENDER_REFERENCE view={view} done seconds={time.time() - started:.1f}", flush=True)
        os.remove(job_path)
        used_tiles.update(t["file"] for t in tiles)
        view_records[view] = {"camera": camera, "width": width, "height": height,
                              "tiles": sorted(t["file"] for t in tiles), "triangles": triangles}

    # 入力の記録（ReferenceCheck.py --projection --verify が今のファイルと照らす）
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ReferenceCheck as RC  # noqa: E402  ふつうの Python の側だけで使う
    record = {
        "kind": "blender_depth",
        "script": normalize_rel(os.path.abspath(__file__)),
        "script_sha256": sha256_file(os.path.abspath(__file__)),
        "mesh_dir": normalize_rel(mesh_dir),
        "manifest_sha256": sha256_file(os.path.join(mesh_dir, "manifest.json")),
        "ply_sha256": {name: sha256_file(os.path.join(mesh_dir, name)) for name in sorted(used_tiles)},
        "terrain_mesh_inputs": RC.hash_inputs("terrain_mesh"),
        "git_commit": (RC.git_output(["rev-parse", "HEAD"]) or "").strip(),
        "range": args.range,
        "shadow_range_m": SHADOW_RANGE_M,
        "coordinates": coordinate_record(),
        "views": view_records,
    }
    with open(os.path.join(out_dir, "inputs.json"), "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_REFERENCE record views={len(views)} ply={len(used_tiles)} "
          f"terrain_mesh_sha256={record['terrain_mesh_inputs']['files_sha256']}")
    return 0


def normalize_rel(path):
    rel = os.path.relpath(path, REPO_ROOT)
    return rel.replace("\\", "/")


# ---- Blender の中の側（1 視点を描く） ----

def write_pfm(path, image):
    """1 チャンネルの float32 を PFM（下の行から並ぶ、little endian）で書く。image は上の行が 0 行目"""
    height, width = image.shape
    with open(path, "wb") as f:
        f.write(f"Pf\n{width} {height}\n-1.0\n".encode("ascii"))
        f.write(np.ascontiguousarray(image[::-1], dtype="<f4").tobytes())


PLY_VERTEX_PROPERTIES = ["property float x", "property float y", "property float z",
                         "property float nx", "property float ny", "property float nz"]
PLY_FACE_PROPERTY = "property list uchar int vertex_indices"


def read_terrain_ply(path):
    """Game の WastelandTerrainExport が書いた PLY（binary_little_endian、頂点は位置と法線の float 6 つ、面は 3 角の int）を読む。
    (位置 (頂点数, 3) float32, 三角形の頂点の番号 (三角形数, 3) int32)。並びが違えば例外"""
    with open(path, "rb") as f:
        data = f.read()
    marker = b"end_header\n"
    end = data.find(marker)
    if end < 0:
        raise ValueError(f"PLY の見出しが無い: {path}")
    header = data[:end].decode("ascii").splitlines()
    body = end + len(marker)
    if header[:2] != ["ply", "format binary_little_endian 1.0"]:
        raise ValueError(f"PLY の形式が違う: {path}")
    lines = [line for line in header[2:] if not line.startswith("comment")]
    if not (len(lines) == 9 and lines[0].startswith("element vertex ") and lines[1:7] == PLY_VERTEX_PROPERTIES and
            lines[7].startswith("element face ") and lines[8] == PLY_FACE_PROPERTY):
        raise ValueError(f"PLY の要素の並びが書き出しと違う: {path}")
    vertex_count = int(lines[0].split()[2])
    face_count = int(lines[7].split()[2])
    if len(data) - body != vertex_count * 24 + face_count * 13:
        raise ValueError(f"PLY の本体の大きさが見出しと合わない: {path}")
    vertices = np.frombuffer(data, dtype="<f4", count=vertex_count * 6, offset=body).reshape(vertex_count, 6)
    faces = np.frombuffer(data, dtype=np.dtype([("n", "u1"), ("i", "<i4", (3,))]), count=face_count,
                          offset=body + vertex_count * 24)
    if np.any(faces["n"] != 3):
        raise ValueError(f"三角形でない面がある: {path}")
    indices = np.ascontiguousarray(faces["i"], dtype=np.int32)
    if indices.size and (indices.min() < 0 or indices.max() >= vertex_count):
        raise ValueError(f"面の頂点の番号が範囲の外: {path}")
    return np.ascontiguousarray(vertices[:, :3], dtype=np.float32), indices


def build_mesh(name, positions, triangles):
    """位置と三角形から Blender のメッシュを作る（法線・UV は持たない）"""
    import bpy
    mesh = bpy.data.meshes.new(name)
    mesh.vertices.add(len(positions))
    mesh.vertices.foreach_set("co", positions.ravel())
    mesh.loops.add(triangles.size)
    mesh.loops.foreach_set("vertex_index", triangles.ravel())
    mesh.polygons.add(len(triangles))
    mesh.polygons.foreach_set("loop_start", np.arange(0, triangles.size, 3, dtype=np.int32))
    mesh.update(calc_edges=True)
    return mesh


def render_job(job_path):
    import bpy
    import mathutils
    import OpenImageIO as oiio

    with open(job_path, encoding="utf-8") as f:
        job = json.load(f)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene

    # 深度を放射する材質（View Z Depth はカメラの前方向への距離 m）
    material = bpy.data.materials.new("EngineViewDepth")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    camera_data = nodes.new("ShaderNodeCameraData")
    emission = nodes.new("ShaderNodeEmission")
    output = nodes.new("ShaderNodeOutputMaterial")
    material.node_tree.links.new(camera_data.outputs["View Z Depth"], emission.inputs["Color"])
    emission.inputs["Strength"].default_value = 1.0
    material.node_tree.links.new(emission.outputs["Emission"], output.inputs["Surface"])

    # 区画を読み、エンジンの座標から Blender の座標へ変換を掛ける
    started = time.time()
    to_blender = mathutils.Matrix(engine_to_blender_matrix4())
    vertex_count = 0
    triangle_count = 0
    for path in job["tiles"]:
        name = os.path.splitext(os.path.basename(path))[0]
        positions, triangles = read_terrain_ply(path)
        mesh = build_mesh(name, positions, triangles)
        mesh.materials.append(material)
        obj = bpy.data.objects.new(name, mesh)
        scene.collection.objects.link(obj)
        obj.matrix_world = to_blender
        vertex_count += len(positions)
        triangle_count += len(triangles)
        del positions, triangles
    load_seconds = time.time() - started

    # カメラ（エンジンの撮影と同じ位置・向き・縦の画角・縦横比）
    matrix, vertical_fov = camera_to_blender(job["camera"])
    camera = bpy.data.cameras.new("EngineCamera")
    camera.type = "PERSP"
    camera.sensor_fit = "VERTICAL"
    camera.angle_y = vertical_fov
    camera.shift_x = 0.0
    camera.shift_y = 0.0
    camera.clip_start = float(job["camera"].get("near", 0.1))
    camera.clip_end = CLIP_END_M
    camera_object = bpy.data.objects.new("EngineCamera", camera)
    scene.collection.objects.link(camera_object)
    camera_object.matrix_world = mathutils.Matrix(matrix)
    scene.camera = camera_object

    render = scene.render
    render.engine = "CYCLES"
    render.resolution_x = int(job["width"])
    render.resolution_y = int(job["height"])
    render.resolution_percentage = 100
    render.pixel_aspect_x = 1.0
    render.pixel_aspect_y = 1.0
    render.film_transparent = True
    cycles = scene.cycles
    cycles.device = "CPU"
    cycles.samples = 1
    cycles.use_adaptive_sampling = False
    cycles.use_denoising = False
    cycles.pixel_filter_type = "BOX"
    cycles.filter_width = PIXEL_FILTER_WIDTH
    cycles.max_bounces = 0
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    render.image_settings.media_type = "MULTI_LAYER_IMAGE"
    render.image_settings.file_format = "OPEN_EXR_MULTILAYER"
    render.image_settings.color_depth = "32"
    render.filepath = job["exr"]
    started = time.time()
    bpy.ops.render.render(write_still=True)
    render_seconds = time.time() - started

    # EXR（上の行から）の Combined の R が深度、A が当たり（空は 0）
    image = oiio.ImageInput.open(job["exr"])
    if image is None:
        raise RuntimeError(f"EXR を開けない: {job['exr']}")
    spec = image.spec()
    names = list(spec.channelnames)
    pixels = np.asarray(image.read_image(0, 0, 0, spec.nchannels, "float")).reshape(spec.height, spec.width, spec.nchannels)
    image.close()
    depth = pixels[:, :, names.index("ViewLayer.Combined.R")].astype(np.float32)
    alpha = pixels[:, :, names.index("ViewLayer.Combined.A")]
    depth = np.where(alpha > 0.5, depth, np.float32(np.inf))
    write_pfm(job["out_pfm"], depth)
    os.remove(job["exr"])

    with open(job["out_json"], "w", encoding="utf-8") as f:
        json.dump({
            "view": job["view"],
            "format": "pfm_f32_le_bottom_up",
            "meaning": "view_axis_distance",
            "units": "m",
            "sky_value": "inf",
            "renderer": f"Blender {bpy.app.version_string} Cycles CPU",
            "samples": 1,
            "pixel_filter": {"type": "BOX", "width_px": PIXEL_FILTER_WIDTH},
            "depth_source": "emission of Camera Data View Z Depth",
            "width": int(job["width"]),
            "height": int(job["height"]),
            "camera_engine": job["camera"],
            "camera_blender_matrix_world": matrix,
            "camera_vertical_fov_degrees": math.degrees(vertical_fov),
            "tiles": len(job["tiles"]),
            "vertices": vertex_count,
            "triangles": triangle_count,
            "surface_pixels": int(np.count_nonzero(alpha > 0.5)),
            "load_seconds": load_seconds,
            "render_seconds": render_seconds,
        }, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_REFERENCE_JOB view={job['view']} tiles={len(job['tiles'])} triangles={triangle_count} "
          f"load={load_seconds:.1f}s render={render_seconds:.1f}s")


def main():
    if IN_BLENDER:
        argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
        parser = argparse.ArgumentParser(description="Blender の中で 1 視点を描く")
        parser.add_argument("--render-job", required=True)
        args = parser.parse_args(argv)
        render_job(args.render_job)
        return 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--depth", action="store_true", help="地形の深度を描く")
    parser.add_argument("--mesh", required=True, help="Game の --wasteland-export-terrain が書いたフォルダ")
    parser.add_argument("--capture", required=True, help="エンジンの撮影（CaptureScene.ps1 -Aov の -OutDir）")
    parser.add_argument("--views", default="", help="描く視点（カンマ区切り。省くと metrics.json の全視点）")
    parser.add_argument("--range", default="frustum", choices=("frustum", "shadow", "all"), help="読む区画の範囲")
    parser.add_argument("--blender", default=DEFAULT_BLENDER, help="blender.exe")
    args = parser.parse_args()
    if not args.depth:
        parser.error("--depth を指定してください")
    return run_depth(args)


if __name__ == "__main__":
    status = main()
    if not IN_BLENDER:
        sys.exit(status)
