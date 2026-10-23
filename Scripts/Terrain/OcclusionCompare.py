# 小さな遮蔽の比べ（SCN-WLR-OCCLUSION-*）の書き出しの契約の照合。ReferenceCheck.py --occlusion が呼ぶ。
#
#   python Scripts/Terrain/ReferenceCheck.py --occlusion [--trial] --engine-root <エンジンの撮影の置き場>
#          --reference <Cycles の参照の置き場> [--views contact_near] [--verify]
#
# 組: 参照の置き場の下の、inputs.json（kind が cycles_occlusion。BlenderOcclusionReference.py が書く）を持つフォルダが 1 組。
#   同じ名前のフォルダをエンジンの撮影の置き場の下から読む（例: <置き場>/rocks-sky と <参照>/rocks-sky）。
# 入力の照合（組ごと）:
#   - エンジンの撮影: 起動引数に共通の照明・灰色の材質・最も細かい段があり、照明の組（common-light-case）が参照と同じ。
#     --verify なら撮影の入力の記録（inputs.json）が今のファイルと合う。
#   - 参照: --verify なら台本（BlenderOcclusionReference.py・借りた BlenderReference.py）・共通の照明・地形のメッシュ
#     （元の入力・一覧・読んだ PLY）が今のファイルと合う。小物の形（PLY）と配置（placements.csv）のハッシュが、今のエンジンの
#     撮影のフォルダのものと合う。カメラがエンジンの aov.json と合う。--trial でない照合は試しの描画を拒む。
# 同じ面の画素（視点ごと）: エンジンと Cycles のどちらかが面に当たった画素のうち、両方の深度の空との境・深度の跳びから
#   GrayRegions.EDGE_MARGIN_PX 画素より離れた画素（輪郭の除外）を分母にし、PlantRegions.same_surface（深度の差が
#   DEPTH_MATCH_RELATIVE 以内）で、陰影の法線（エンジンの AOV の normal と Cycles の Normal のパス。どちらも頂点の補間）の
#   角の差が NORMAL_MATCH_DEGREES 以内の画素を同じ面とする。割合が MIN_SAME_SURFACE_FRACTION 以上で合格。物体の識別
#   （Cycles の Object Index。地形 0・崖錐の岩 1・崖の面の塊 2・株の枝 4・葉 5）ごとの割合も書く。
# 組の種類（参照の記録の set_kind。OcclusionSets.py の組。無い記録は岩塊の組）: 組のどれかの視点の分母に、種類の物体
#   （REQUIRED_OBJECTS。地形だけ = 地形、岩塊 = 地形と崖錐の岩かスキャンの崖の塊、低木 = 地形と株）が映ること。低木の組は株の PLY が
#   エンジンの撮影のものと合い、株の形が合うこと（株は枝と葉の間で深度が跳び、同じ面の分母にはほとんど残らないので、
#   輪郭を含めた Cycles の株の画素でエンジンの深度が合う割合が MIN_SAME_SURFACE_FRACTION 以上、Cycles の地形の画素で
#   エンジンだけが手前に面を見る画素が株の画素の MAX_PLANT_ENGINE_ONLY_RATIO 倍以下）。
# 組どうし: エンジンの撮影の Game.exe と実装のハッシュが全組で揃うこと。エンジンの置き場に OcclusionSets.py の
#   occlusion_sets.json があれば、そこに載った組のすべてに参照があり、組の種類が合うこと。
# 幾何の法線（Cycles の True Normal の AOV、aux.geometric_normal）: Cycles の深度から戻した位置の隣の画素との差の外積
#   （深度の面の向き）と比べる。輪郭から離れ上下左右の隣が面に当たり、その 5 画素の幾何の法線が PLANAR_DEGREES 以内で
#   そろう画素（三角形が画素より大きい所。MIN_PLANAR_PIXELS 以上）のうち、角の差が GEOMETRIC_MATCH_DEGREES 以内の
#   割合が MIN_GEOMETRIC_FRACTION 以上で、陰影の法線の同じ割合より高いこと（補間の法線を幾何の法線として出していないこと）。
# 材質: エンジンの AOV のアルベドと粗さが、面のどの画素でも CommonLight.json の gray_material と MATERIAL_AOV_TOLERANCE 以内
#   （地形も小物も灰色。Cycles は同じ値の Principled BSDF を全体に掛ける）。低木の組は株が模型の色のままなので、同じ面の
#   画素のうち Cycles の物体が灰色のもの（GRAY_OBJECTS）だけで確かめる。
# 書くもの: <参照の組>/occlusion_check.json と、並べた画 <参照の組>/occlusion_preview/<視点>.png
#   （上: エンジンの HDR と Cycles の HDR を同じ露出で、下: 同じ面の判定〔緑 一致、赤 不一致、灰 輪郭、黒 空〕と物体の識別）。
# --expect <定義>（--regions と一緒に。例 ExpectSpecs/occlusion-fix.json）: 定義の baseline_engine_root（直しの前のエンジンの撮影）を
#   同じ参照・同じ領域で測り直し、同じ画素で前後を比べる。空だけの組の層のうち、targets の層はエンジン ÷ Cycles が target_range の内、
#   ほかの層（くぼみ・対照の画素が領域の定義の下限以上）は 1 からの離れが前の値の worse_tolerance 倍を越えて増えない、対照の
#   エンジンの Y の平均が前の 1 ± control_y_tolerance の内。結果は <エンジンの置き場>/occlusion_expect.json。
import json
import math
import os

import numpy as np
from PIL import Image

import GrayRegions as GR
import PlantRegions as PR

MIN_SAME_SURFACE_FRACTION = 0.90
# エンジンの AOV の材質（灰色）の許容差（8 bit の GBuffer の 1.5 段）
MATERIAL_AOV_TOLERANCE = 1.5 / 255.0
NORMAL_MATCH_DEGREES = 10.0
GEOMETRIC_MATCH_DEGREES = 2.0
MIN_GEOMETRIC_FRACTION = 0.5
# 幾何の法線の判定に使う画素: 上下左右の隣の幾何の法線が中央とこの角度以内（同じ平面）。その画素の下限
PLANAR_DEGREES = 0.5
MIN_PLANAR_PIXELS = 10000
REQUIRED_GAME_ARGS = ("wasteland-common-light", "wasteland-gray-material", "wasteland-finest-lod")
SCRIPT = "Scripts/Terrain/BlenderOcclusionReference.py"
BLENDER_REFERENCE = "Scripts/Terrain/BlenderReference.py"
PROPS_MANIFEST = "occlusion_props.manifest.json"
PROPS_PLACEMENTS = "occlusion_props.placements.csv"
OBJECT_NAMES = {0: "terrain", 1: "talus", 2: "cliff_scan", 3: "other_prop", 4: "plant_branch", 5: "plant_leaf"}
GRAY_OBJECTS = (0, 1, 2, 3)
PLANT_OBJECTS = (4, 5)
# 低木の組の株の形: Cycles の地形の画素でエンジンだけが手前に面を見る画素の、株の画素に対する割合の上限
MAX_PLANT_ENGINE_ONLY_RATIO = 0.10
# 組の種類 → 組のどれかの視点に映るべき物体（並びの 1 つごとに、そのうちどれかが映ればよい）
REQUIRED_OBJECTS = {
    "terrain": (("terrain",),),
    "rocks": (("terrain",), ("talus", "cliff_scan")),
    "plant": (("terrain",), ("plant_branch", "plant_leaf")),
}
SETS_FILE = "occlusion_sets.json"
PLANT_PLY = "reference_plant.ply"
PREVIEW_DIR = "occlusion_preview"


def read_json(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def capture_case(game_args):
    for arg in game_args:
        if arg.startswith("common-light-case="):
            return arg.split("=", 1)[1]
    return None


def check_set_inputs(RC, engine_dir, reference_dir, record, verify, trial):
    """組の入力の照合。失敗の一覧"""
    failures = []
    if record.get("kind") != "cycles_occlusion":
        failures.append(f"参照の記録の種類が cycles_occlusion でない（{record.get('kind')}）")
    if bool(record.get("trial")) and not trial:
        failures.append("参照が試しの描画（--trial を付けない照合は本描画を求める）")
    game_args = RC.plate_capture_args(engine_dir)
    if game_args is None:
        return failures + ["エンジンの撮影の metrics.json が無い"]
    # 記録の起動引数は先頭の -- を持つ
    game_args = [arg[2:] if arg.startswith("--") else arg for arg in game_args]
    missing = [a for a in REQUIRED_GAME_ARGS if a not in game_args]
    if missing:
        failures.append(f"エンジンの撮影の起動引数に {', '.join(missing)} が無い")
    case = capture_case(game_args)
    if case != (record.get("lighting") or {}).get("case"):
        failures.append(f"照明の組が違う（エンジン {case}、参照 {(record.get('lighting') or {}).get('case')}）")
    for name in (PROPS_MANIFEST, PROPS_PLACEMENTS):
        if not os.path.isfile(os.path.join(engine_dir, name)):
            failures.append(f"エンジンの撮影に {name} が無い")
    if failures:
        return failures
    if RC.sha256_file(os.path.join(engine_dir, PROPS_PLACEMENTS)) != record.get("props_placements_sha256"):
        failures.append("小物の配置（placements.csv）が参照の描いたものと違う")
    if RC.sha256_file(os.path.join(engine_dir, PROPS_MANIFEST)) != record.get("props_manifest_sha256"):
        failures.append("小物の形の一覧（manifest）が参照の描いたものと違う")
    changed = [name for name, digest in (record.get("props_ply_sha256") or {}).items()
               if RC.sha256_file(os.path.join(engine_dir, f"occlusion_props.{name}.ply")) != digest]
    if changed:
        failures.append(f"小物の形（PLY）が参照の描いたものと違う: {', '.join(changed)}")
    if int(record.get("props_count") or 0) > 0 and not record.get("props_ply_sha256"):
        failures.append("小物の形の記録が無い")
    if record.get("set_kind") == "plant":
        plant_ply = os.path.join(engine_dir, PLANT_PLY)
        if not os.path.isfile(plant_ply) or RC.sha256_file(plant_ply) != record.get("plant_ply_sha256"):
            failures.append(f"比べる株（{PLANT_PLY}）が参照の描いたものと違う")
    if verify:
        inputs_ok, input_failures, _ = RC.check_inputs(engine_dir)
        if not inputs_ok:
            failures.extend(f"エンジンの撮影: {reason}" for reason in input_failures)
        ok, reasons, _ = RC.check_blender_inputs(reference_dir)
        failures.extend(reasons)
        for key, rel in (("blender_reference_sha256", BLENDER_REFERENCE),
                         ("common_light_sha256", RC.COMMON_LIGHT_PATH)):
            if RC.sha256_file(os.path.join(RC.REPO_ROOT, rel)) != record.get(key):
                failures.append(f"{rel} が描いた後に変わった（古い結果）")
    return failures


def same_surface_masks(engine_depth, engine_normal, cycles_depth, cycles_normal):
    """(分母の画素, 同じ面の画素, 輪郭の画素)"""
    engine_hit = np.isfinite(engine_depth)
    cycles_hit = np.isfinite(cycles_depth)
    edge = GR.edge_mask(engine_depth) | GR.edge_mask(cycles_depth)
    domain = (engine_hit | cycles_hit) & ~edge
    cos = np.sum(engine_normal * cycles_normal, axis=-1)
    normal_ok = cos >= math.cos(math.radians(NORMAL_MATCH_DEGREES))
    same = domain & PR.same_surface(cycles_depth, engine_depth) & normal_ok
    return domain, same, edge & (engine_hit | cycles_hit)


def depth_surface_normal(camera, depth):
    """(深度から戻した面の向き〔視線の側へ向けた単位の向き〕, 使える画素)。向きは上下・左右の隣の画素の位置の差の外積"""
    height, width = depth.shape
    rays = PR.pixel_rays(camera, width, height)
    finite = np.isfinite(depth)
    positions = np.asarray(camera["position"], dtype=np.float64)[None, None, :] +         rays * np.where(finite, depth, 0.0)[..., None]
    normal = np.zeros(positions.shape, dtype=np.float64)
    valid = np.zeros(depth.shape, dtype=bool)
    dx = positions[1:-1, 2:] - positions[1:-1, :-2]
    dy = positions[2:, 1:-1] - positions[:-2, 1:-1]
    normal[1:-1, 1:-1] = np.cross(dx, dy)
    valid[1:-1, 1:-1] = finite[1:-1, 1:-1] & finite[1:-1, 2:] & finite[1:-1, :-2] & finite[2:, 1:-1] & finite[:-2, 1:-1]
    length = np.linalg.norm(normal, axis=-1, keepdims=True)
    valid &= length[..., 0] > 0.0
    normal = normal / np.maximum(length, 1e-30)
    facing = np.sum(normal * rays, axis=-1) > 0.0
    normal[facing] *= -1.0
    return normal, valid & ~GR.edge_mask(depth)


def planar_pixels(geometric_normal, valid):
    """上下左右の隣の幾何の法線が中央と PLANAR_DEGREES 以内でそろう画素（5 画素が 1 つの平面に乗る。三角形が画素より
    大きい所）。遠い視点では三角形が画素ほどの大きさになり、隣との差から戻した向きが周りの三角形を均した向きになるので、
    判定はこの画素で行う"""
    cos_limit = math.cos(math.radians(PLANAR_DEGREES))
    center = geometric_normal[1:-1, 1:-1]
    planar = np.zeros(valid.shape, dtype=bool)
    same = np.ones(center.shape[:2], dtype=bool)
    for neighbor in (geometric_normal[1:-1, 2:], geometric_normal[1:-1, :-2], geometric_normal[2:, 1:-1],
                     geometric_normal[:-2, 1:-1]):
        same &= np.sum(center * neighbor, axis=-1) >= cos_limit
    planar[1:-1, 1:-1] = same
    return valid & planar


def geometric_normal_check(camera, cycles_depth, cycles_normal, geometric_normal, object_index):
    """幾何の法線の AOV が深度の面の向きと合うか（陰影の法線と並べる）。判定は 5 画素が 1 つの平面に乗る画素で行い、
    輪郭から離れた全画素の割合も参考に書く"""
    surface_normal, valid_all = depth_surface_normal(camera, cycles_depth)
    valid = planar_pixels(geometric_normal, valid_all)
    cos_limit = math.cos(math.radians(GEOMETRIC_MATCH_DEGREES))
    geometric_dot = np.sum(surface_normal * geometric_normal, axis=-1) >= cos_limit
    shading_dot = np.sum(surface_normal * cycles_normal, axis=-1) >= cos_limit
    geometric_ok = valid & geometric_dot
    shading_ok = valid & shading_dot
    pixels = max(int(valid.sum()), 1)
    all_pixels = max(int(valid_all.sum()), 1)
    result = {"pixels": int(valid.sum()),
              "geometric_fraction": round(float(geometric_ok.sum()) / pixels, 4),
              "shading_fraction": round(float(shading_ok.sum()) / pixels, 4),
              "planar_degrees": PLANAR_DEGREES, "min_planar_pixels": MIN_PLANAR_PIXELS,
              "all_pixels": {"pixels": int(valid_all.sum()),
                             "geometric_fraction": round(float((valid_all & geometric_dot).sum()) / all_pixels, 4),
                             "shading_fraction": round(float((valid_all & shading_dot).sum()) / all_pixels, 4)},
              "tolerance_degrees": GEOMETRIC_MATCH_DEGREES, "min_fraction": MIN_GEOMETRIC_FRACTION, "per_object": {}}
    cos_shading = np.clip(np.sum(geometric_normal * cycles_normal, axis=-1), -1.0, 1.0)
    hit = valid & (np.linalg.norm(geometric_normal, axis=-1) > 0.5)
    if hit.any():
        angles = np.degrees(np.arccos(cos_shading[hit]))
        result["geometric_vs_shading_degrees"] = {"median": round(float(np.median(angles)), 3),
                                                   "p99": round(float(np.percentile(angles, 99)), 3)}
    for value, name in OBJECT_NAMES.items():
        mask = valid & (object_index == value)
        if mask.any():
            count = float(mask.sum())
            result["per_object"][name] = {"pixels": int(mask.sum()),
                                          "geometric_fraction": round(float((geometric_ok & mask).sum()) / count, 4),
                                          "shading_fraction": round(float((shading_ok & mask).sum()) / count, 4)}
    failures = []
    if result["pixels"] < MIN_PLANAR_PIXELS:
        failures.append(f"幾何の法線を判定できる画素（5 画素が 1 つの平面に乗る）が {result['pixels']} で "
                        f"{MIN_PLANAR_PIXELS} に届かない")
    if result["geometric_fraction"] < MIN_GEOMETRIC_FRACTION:
        failures.append(f"幾何の法線が深度の面の向きと {GEOMETRIC_MATCH_DEGREES}° 以内で合う画素の割合 "
                        f"{result['geometric_fraction']:.4f} が {MIN_GEOMETRIC_FRACTION} に届かない")
    if result["geometric_fraction"] <= result["shading_fraction"]:
        failures.append(f"幾何の法線の割合 {result['geometric_fraction']:.4f} が陰影の法線の割合 "
                        f"{result['shading_fraction']:.4f} より高くない（補間の法線を出している）")
    return result, failures


def write_preview(path, engine_hdr, cycles_hdr, domain, same, edge, object_index):
    def display(image, scale):
        linear = np.clip(image * scale, 0.0, 1.0)
        srgb = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1.0 / 2.4) - 0.055)
        return (srgb * 255.0 + 0.5).astype(np.uint8)

    surface = np.isfinite(object_index) & (object_index >= 0)
    reference_y = (cycles_hdr @ GR.LUMA)[surface]
    scale = 0.5 / float(np.percentile(reference_y, 90)) if reference_y.size else 1.0
    judge = np.zeros(engine_hdr.shape, dtype=np.uint8)
    judge[edge] = (110, 110, 110)
    judge[domain & ~same] = (220, 50, 50)
    judge[same] = (60, 190, 80)
    colors = {0: (150, 120, 90), 1: (240, 160, 40), 2: (80, 140, 230), 3: (200, 80, 200), 4: (120, 70, 40),
              5: (90, 200, 90)}
    objects = np.zeros(engine_hdr.shape, dtype=np.uint8)
    for value, color in colors.items():
        objects[object_index == value] = color
    top = np.concatenate([display(engine_hdr, scale), display(cycles_hdr, scale)], axis=1)
    bottom = np.concatenate([judge, objects], axis=1)
    image = Image.fromarray(np.concatenate([top, bottom], axis=0))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    image.resize((image.width // 2, image.height // 2), Image.BILINEAR).save(path)


def check_view(RC, engine_dir, reference_dir, record, view, regions=None, set_name="", trial=False):
    result = {"view": view, "pass": False, "failures": []}
    failures = result["failures"]
    aov_path = os.path.join(engine_dir, f"{view}.aov.json")
    prefix = os.path.join(reference_dir, view)
    needed = [aov_path, os.path.join(engine_dir, f"{view}.hdr.pfm")] + \
        [f"{prefix}.aux.{part}.pfm" for part in ("depth", "normal", "geometric_normal", "object")] + [f"{prefix}.cycles.hdr.pfm"]
    missing = [os.path.basename(p) for p in needed if not os.path.isfile(p)]
    if missing:
        failures.append(f"無いファイル: {', '.join(missing)}")
        return result
    meta = (record.get("views") or {}).get(view)
    if meta is None:
        failures.append("参照の記録にこの視点が無い")
        return result
    aov = read_json(aov_path)
    failures.extend(RC.gray_camera_match(aov["camera"], meta["camera"]))
    engine_depth = RC.read_pfm_any(os.path.join(engine_dir, aov["depth"]["file"]))[..., 0].astype(np.float64)
    engine_normal = RC.read_pfm_any(os.path.join(engine_dir, aov["normal"]["file"]))[..., :3].astype(np.float64)
    engine_hdr = RC.read_pfm_any(os.path.join(engine_dir, f"{view}.hdr.pfm"))[..., :3].astype(np.float64)
    engine_albedo = RC.read_pfm_any(os.path.join(engine_dir, aov["albedo"]["file"]))[..., :3].astype(np.float64)
    engine_roughness = RC.read_pfm_any(os.path.join(engine_dir, aov["roughness"]["file"]))[..., 0].astype(np.float64)
    cycles_depth = RC.read_pfm_any(prefix + ".aux.depth.pfm")[..., 0].astype(np.float64)
    cycles_normal = RC.read_pfm_any(prefix + ".aux.normal.pfm")[..., :3].astype(np.float64)
    geometric_normal = RC.read_pfm_any(prefix + ".aux.geometric_normal.pfm")[..., :3].astype(np.float64)
    object_index = RC.read_pfm_any(prefix + ".aux.object.pfm")[..., 0]
    cycles_hdr = RC.read_pfm_any(prefix + ".cycles.hdr.pfm")[..., :3].astype(np.float64)
    if engine_depth.shape != cycles_depth.shape:
        failures.append(f"深度の大きさが違う（エンジン {engine_depth.shape}、Cycles {cycles_depth.shape}）")
        return result
    # 空の画素の深度を +∞ にそろえる（エンジンの空は +∞）
    engine_depth = np.where(np.isfinite(engine_depth) & (engine_depth > 0.0), engine_depth, np.inf)
    domain, same, edge = same_surface_masks(engine_depth, engine_normal, cycles_depth, cycles_normal)
    gray = read_json(os.path.join(RC.REPO_ROOT, RC.COMMON_LIGHT_PATH))["gray_material"]
    surface = np.isfinite(engine_depth)
    if record.get("set_kind") == "plant":
        surface = same & np.isin(object_index, GRAY_OBJECTS)
    albedo_error = float(np.abs(engine_albedo[surface] - float(gray["albedo"])).max()) if surface.any() else math.inf
    roughness_error = float(np.abs(engine_roughness[surface] - float(gray["roughness"])).max()) if surface.any()         else math.inf
    result["material_aov"] = {"albedo": float(gray["albedo"]), "roughness": float(gray["roughness"]),
                              "albedo_max_error": round(albedo_error, 6), "roughness_max_error": round(roughness_error, 6),
                              "tolerance": round(MATERIAL_AOV_TOLERANCE, 6)}
    if not (albedo_error <= MATERIAL_AOV_TOLERANCE and roughness_error <= MATERIAL_AOV_TOLERANCE):
        failures.append(f"エンジンの AOV の材質が灰色でない（アルベドの差 {albedo_error:.4f}・粗さの差 {roughness_error:.4f}）")
    fraction = float(same.sum()) / float(max(int(domain.sum()), 1))
    per_object = {}
    for value, name in OBJECT_NAMES.items():
        mask = domain & (object_index == value)
        if mask.any():
            per_object[name] = {"pixels": int(mask.sum()),
                                "same_surface_fraction": round(float((same & mask).sum()) / float(mask.sum()), 4)}
    engine_only = domain & np.isfinite(engine_depth) & ~np.isfinite(cycles_depth)
    cycles_only = domain & ~np.isfinite(engine_depth) & np.isfinite(cycles_depth)
    result.update({
        "domain_pixels": int(domain.sum()),
        "edge_pixels": int(edge.sum()),
        "same_surface_pixels": int(same.sum()),
        "same_surface_fraction": round(fraction, 4),
        "engine_only_pixels": int(engine_only.sum()),
        "cycles_only_pixels": int(cycles_only.sum()),
        "per_object": per_object,
        "tolerance": {"depth_relative": PR.DEPTH_MATCH_RELATIVE, "normal_degrees": NORMAL_MATCH_DEGREES,
                      "edge_margin_px": GR.EDGE_MARGIN_PX},
    })
    if record.get("set_kind") == "plant":
        # 株は枝と葉の間で深度が跳ぶので、同じ面の分母（輪郭を除く）にはほとんど残らない。輪郭を含めた株の画素で形を照らす
        plant = np.isin(object_index, PLANT_OBJECTS)
        plant_pixels = int(plant.sum())
        matched = float((PR.same_surface(cycles_depth, engine_depth) & plant).sum()) / float(max(plant_pixels, 1))
        nearer = (object_index == 0) & np.isfinite(engine_depth) & \
            (engine_depth < cycles_depth * (1.0 - PR.DEPTH_MATCH_RELATIVE))
        engine_only = float(nearer.sum()) / float(max(plant_pixels, 1))
        result["plant_silhouette"] = {"plant_pixels": plant_pixels, "depth_match_fraction": round(matched, 4),
                                      "engine_only_nearer_pixels": int(nearer.sum()),
                                      "engine_only_nearer_ratio": round(engine_only, 4),
                                      "min_depth_match": MIN_SAME_SURFACE_FRACTION,
                                      "max_engine_only_ratio": MAX_PLANT_ENGINE_ONLY_RATIO}
        if plant_pixels == 0 or matched < MIN_SAME_SURFACE_FRACTION:
            failures.append(f"株の画素（輪郭を含む {plant_pixels}）でエンジンの深度が合う割合 {matched:.4f} が "
                            f"{MIN_SAME_SURFACE_FRACTION} に届かない")
        if engine_only > MAX_PLANT_ENGINE_ONLY_RATIO:
            failures.append(f"Cycles の地形の画素でエンジンだけが手前に面を見る画素が株の画素の {engine_only:.4f} 倍で "
                            f"{MAX_PLANT_ENGINE_ONLY_RATIO} を超える")
    result["geometric_normal"], geometric_failures = geometric_normal_check(
        meta["camera"], cycles_depth, cycles_normal, geometric_normal, object_index)
    failures.extend(geometric_failures)
    if fraction < MIN_SAME_SURFACE_FRACTION:
        failures.append(f"同じ面の画素の割合 {fraction:.4f} が {MIN_SAME_SURFACE_FRACTION} に届かない")
    preview = os.path.join(reference_dir, PREVIEW_DIR, f"{view}.png")
    write_preview(preview, engine_hdr, cycles_hdr, domain, same, edge, object_index)
    result["preview"] = RC.rel_dir(preview)
    if regions is not None:
        import OcclusionMetric
        data = {"aov": aov, "same": same, "object_index": object_index, "engine_hdr": engine_hdr, "cycles_hdr": cycles_hdr,
                "cycles_depth": cycles_depth, "cycles_normal": cycles_normal, "cycles_geometric_normal": geometric_normal}
        result["metric"], metric_failures = OcclusionMetric.measure_view(
            RC, regions, set_name, record, engine_dir, reference_dir, view, data, trial)
        failures.extend(f"測り: {reason}" for reason in metric_failures)
    result["pass"] = not failures
    return result


def check_across_sets(engine_root, reference_root, sets):
    """組どうしの照合（エンジンの撮影の Game.exe と実装の揃い、OcclusionSets.py の組の一覧の揃い）。失敗の一覧"""
    failures = []
    for key in ("game_exe_sha256", "files_sha256"):
        values = set()
        for name in sets:
            path = os.path.join(engine_root, name, "inputs.json")
            if os.path.isfile(path):
                values.add(read_json(path).get(key))
        if len(values) > 1:
            failures.append(f"組ごとのエンジンの撮影の {key} が揃っていない（{len(values)} 通り）")
    listing = os.path.join(engine_root, SETS_FILE)
    if os.path.isfile(listing):
        listed = read_json(listing).get("sets") or {}
        missing = [name for name in listed if name not in sets]
        if missing:
            failures.append(f"{SETS_FILE} の組 {', '.join(missing)} の参照が無い")
        for name in sets:
            kind = read_json(os.path.join(reference_root, name, "inputs.json")).get("set_kind") or "rocks"
            if name in listed and kind != listed[name].get("kind"):
                failures.append(f"{name}: 参照の組の種類 {kind} が {SETS_FILE} の種類 {listed[name].get('kind')} と違う")
    return failures


def print_metric(name, view, metric):
    """測りの要点を 1 行ずつ出す"""
    if metric.get("kind") == "sun":
        print(f"REFERENCE_CHECK occlusion metric set={name} view={view} kind=sun iou={metric.get('shadow_iou')} "
              f"leak={metric.get('leak_fraction')} edge_width_m={json.dumps(metric.get('edge_width_m'))} "
              f"receiver_texel_m={metric.get('receiver_texel_m_median')}", flush=True)
        return
    for region, per_stratum in (metric.get("regions") or {}).items():
        for stratum, entry in per_stratum.items():
            print(f"REFERENCE_CHECK occlusion metric set={name} view={view} region={region} stratum={stratum} "
                  f"pixels={entry['region_pixels']}/{entry['control_pixels']} "
                  f"engine_over_cycles={entry.get('engine_over_cycles')} ratio_engine={entry.get('ratio_engine')} "
                  f"ratio_cycles={entry.get('ratio_cycles')} seed_difference={entry.get('seed_difference')}", flush=True)


def sky_layers(entry):
    """組の結果から空だけの組の層 {(視点, 領域, 層): 値} を取り出す"""
    layers = {}
    for view, result in (entry.get("views") or {}).items():
        metric = result.get("metric") or {}
        if metric.get("kind") != "sky":
            continue
        for region, per_stratum in (metric.get("regions") or {}).items():
            for stratum, values in per_stratum.items():
                if "engine_over_cycles" in values:
                    layers[(view, region, stratum)] = values
    return layers


def check_expect(RC, expect, regions, report, reference_root, views, trial):
    """直しの前の撮影を同じ参照・領域で測り直し、前後の層を比べる。(記録, 失敗の一覧)"""
    failures = []
    baseline_root = os.path.normpath(os.path.join(RC.REPO_ROOT, expect["baseline_engine_root"]))
    low, high = (float(v) for v in expect["target_range"])
    worse_tolerance = float(expect["worse_tolerance"])
    control_tolerance = float(expect["control_y_tolerance"])
    min_region = int(regions["limits"]["min_region_pixels"])
    min_control = int(regions["limits"]["min_control_pixels"])
    targets = {(t["set"], t["view"], t["region"], t["stratum"]) for t in expect["targets"]}
    record = {"baseline_engine_root": RC.rel_dir(baseline_root), "target_range": [low, high],
              "worse_tolerance": worse_tolerance, "control_y_tolerance": control_tolerance, "layers": [], "controls": []}
    seen_targets = set()
    for name, entry in report["sets"].items():
        reference_dir = os.path.join(reference_root, name)
        inputs = read_json(os.path.join(reference_dir, "inputs.json"))
        if (inputs.get("lighting") or {}).get("case") == "sun":
            continue
        baseline_dir = os.path.join(baseline_root, name)
        if not os.path.isdir(baseline_dir):
            failures.append(f"前の撮影 {RC.rel_dir(baseline_dir)} が無い")
            continue
        baseline = {"views": {}}
        for view in views or sorted((inputs.get("views") or {}).keys()):
            baseline["views"][view] = check_view(RC, baseline_dir, reference_dir, inputs, view, regions, name, trial)
        before_layers = sky_layers(baseline)
        after_layers = sky_layers(entry)
        controls = set()
        for key, after in sorted(after_layers.items()):
            view, region, stratum = key
            before = before_layers.get(key)
            if before is None:
                failures.append(f"{name}/{view} の層 {region} {stratum} が前の撮影で測れない")
                continue
            ratio_before = float(before["engine_over_cycles"])
            ratio_after = float(after["engine_over_cycles"])
            is_target = (name, view, region, stratum) in targets
            measured = after["region_pixels"] >= min_region and after["control_pixels"] >= min_control
            row = {"set": name, "view": view, "region": region, "stratum": stratum, "target": is_target,
                   "pixels": [after["region_pixels"], after["control_pixels"]], "measured": measured,
                   "before": ratio_before, "after": ratio_after}
            if is_target:
                seen_targets.add((name, view, region, stratum))
                row["pass"] = low <= ratio_after <= high
                if not row["pass"]:
                    failures.append(f"{name}/{view} の決めた層 {region} {stratum} のエンジン ÷ Cycles {ratio_after:.4f} が "
                                    f"{low}〜{high} の外（前 {ratio_before:.4f}）")
            elif measured:
                growth = abs(ratio_after - 1.0) - abs(ratio_before - 1.0)
                row["pass"] = growth <= worse_tolerance * ratio_before
                if not row["pass"]:
                    failures.append(f"{name}/{view} の層 {region} {stratum} のエンジン ÷ Cycles が {ratio_before:.4f} → "
                                    f"{ratio_after:.4f} で 1 から {growth:.4f} 離れた（許容 {worse_tolerance * ratio_before:.4f}）")
            record["layers"].append(row)
            # 対照は視点と層ごとに 1 つ（領域が違っても同じ画素）
            if after["control_pixels"] >= min_control and (view, stratum) not in controls:
                controls.add((view, stratum))
                change = float(after["engine_y_control"]) / max(float(before["engine_y_control"]), 1e-30)
                ok = abs(change - 1.0) <= control_tolerance
                record["controls"].append({"set": name, "view": view, "stratum": stratum,
                                           "pixels": after["control_pixels"], "before_y": before["engine_y_control"],
                                           "after_y": after["engine_y_control"], "after_over_before": round(change, 4),
                                           "pass": ok})
                if not ok:
                    failures.append(f"{name}/{view} の対照 {stratum} のエンジンの Y が前の {change:.4f} 倍で "
                                    f"1 ± {control_tolerance} の外")
    for missing in sorted(targets - seen_targets):
        failures.append(f"決めた層 {'/'.join(missing)} が測れない")
    return record, failures


def print_expect(record):
    """前後の比べを 1 行ずつ出す"""
    for row in record["layers"]:
        print(f"REFERENCE_CHECK occlusion expect set={row['set']} view={row['view']} region={row['region']} "
              f"stratum={row['stratum']} target={int(row['target'])} measured={int(row['measured'])} "
              f"pixels={row['pixels'][0]}/{row['pixels'][1]} before={row['before']} after={row['after']} "
              f"pass={int(row['pass']) if 'pass' in row else '-'}", flush=True)
    for row in record["controls"]:
        print(f"REFERENCE_CHECK occlusion expect control set={row['set']} view={row['view']} stratum={row['stratum']} "
              f"pixels={row['pixels']} before_y={row['before_y']:.3f} after_y={row['after_y']:.3f} "
              f"after_over_before={row['after_over_before']} pass={int(row['pass'])}", flush=True)


def run(RC, engine_root, reference_root, views, verify, trial, regions_path="", expect_path=None):
    regions = None
    if regions_path:
        import OcclusionMetric
        regions = OcclusionMetric.load_spec(regions_path)
    report = {"mode": "occlusion", "verify": bool(verify), "trial": bool(trial),
              "regions": RC.rel_dir(regions_path) if regions_path else "",
              "engine_root": RC.rel_dir(engine_root) if engine_root else "",
              "reference_root": RC.rel_dir(reference_root) if reference_root else "", "sets": {}}
    failures = []
    if not engine_root or not os.path.isdir(engine_root):
        failures.append("--engine-root の置き場が無い")
    if not reference_root or not os.path.isdir(reference_root):
        failures.append("--reference の置き場が無い")
    sets = []
    if not failures:
        for name in sorted(os.listdir(reference_root)):
            path = os.path.join(reference_root, name, "inputs.json")
            if os.path.isfile(path) and read_json(path).get("kind") == "cycles_occlusion":
                sets.append(name)
        if not sets:
            failures.append("参照の置き場に cycles_occlusion の組が無い（BlenderOcclusionReference.py を回していない）")
    for name in sets:
        reference_dir = os.path.join(reference_root, name)
        engine_dir = os.path.join(engine_root, name)
        record = read_json(os.path.join(reference_dir, "inputs.json"))
        entry = {"engine": RC.rel_dir(engine_dir), "reference": RC.rel_dir(reference_dir), "views": {}}
        entry["input_failures"] = check_set_inputs(RC, engine_dir, reference_dir, record, verify, trial) \
            if os.path.isdir(engine_dir) else [f"エンジンの撮影 {RC.rel_dir(engine_dir)} が無い"]
        failures.extend(f"{name}: {reason}" for reason in entry["input_failures"])
        set_views = views or sorted((record.get("views") or {}).keys())
        for view in set_views:
            result = check_view(RC, engine_dir, reference_dir, record, view, regions, name, trial) \
                if os.path.isdir(engine_dir) else {"view": view, "pass": False, "failures": ["エンジンの撮影が無い"]}
            entry["views"][view] = result
            failures.extend(f"{name}/{view}: {reason}" for reason in result["failures"])
            print(f"REFERENCE_CHECK occlusion set={name} view={view} same_surface={result.get('same_surface_fraction')} "
                  f"domain={result.get('domain_pixels')} per_object="
                  f"{json.dumps({k: v['same_surface_fraction'] for k, v in result.get('per_object', {}).items()})} "
                  f"geometric_normal={(result.get('geometric_normal') or {}).get('geometric_fraction')} "
                  f"shading_normal={(result.get('geometric_normal') or {}).get('shading_fraction')} "
                  f"pass={int(result['pass'])}", flush=True)
            if result.get("metric"):
                print_metric(name, view, result["metric"])
        kind = record.get("set_kind") or "rocks"
        seen = {obj for result in entry["views"].values() for obj in (result.get("per_object") or {})}
        for options in REQUIRED_OBJECTS.get(kind, ()):
            if not seen.intersection(options):
                reason = f"組（{kind}）のどの視点にも {'・'.join(options)} の画素が無い（配置かカメラが違う）"
                entry["input_failures"].append(reason)
                failures.append(f"{name}: {reason}")
        report["sets"][name] = entry
        with open(os.path.join(reference_dir, "occlusion_check.json"), "w", encoding="utf-8") as f:
            json.dump(entry, f, indent=2, ensure_ascii=False)
    if sets:
        failures.extend(check_across_sets(engine_root, reference_root, sets))
    if regions is not None:
        import OcclusionMetric
        measured = {(name, view) for name, entry in report["sets"].items() for view in entry["views"]}
        failures.extend(OcclusionMetric.missing_required(regions, measured))
    if expect_path:
        if regions is None:
            failures.append("--expect は --regions と一緒に使う")
        elif not os.path.isfile(expect_path):
            failures.append(f"--expect の定義 {expect_path} が無い")
        else:
            report["expect"], expect_failures = check_expect(RC, read_json(expect_path), regions, report,
                                                             reference_root, views, trial)
            print_expect(report["expect"])
            failures.extend(f"前後の比べ: {reason}" for reason in expect_failures)
            with open(os.path.join(engine_root, "occlusion_expect.json"), "w", encoding="utf-8") as f:
                json.dump(report["expect"], f, indent=2, ensure_ascii=False)
    report["failures"] = failures
    for reason in failures:
        print(f"REFERENCE_CHECK occlusion failure: {reason}")
    ok = not failures
    print(f"REFERENCE_CHECK mode=occlusion sets={len(sets)} result={'pass' if ok else 'fail'}")
    return 0 if ok else 1
