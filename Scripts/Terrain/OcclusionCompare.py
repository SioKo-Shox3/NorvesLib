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
#   DEPTH_MATCH_RELATIVE 以内）で、法線（エンジンの AOV の normal と Cycles の Normal のパス）の角の差が
#   NORMAL_MATCH_DEGREES 以内の画素を同じ面とする。割合が MIN_SAME_SURFACE_FRACTION 以上で合格。物体の識別
#   （Cycles の Object Index。地形 0・崖錐の岩 1・崖の面の塊 2）ごとの割合も書く。
# 材質: エンジンの AOV のアルベドと粗さが、面のどの画素でも CommonLight.json の gray_material と MATERIAL_AOV_TOLERANCE 以内
#   （地形も小物も灰色。Cycles は同じ値の Principled BSDF を全体に掛ける）。
# 書くもの: <参照の組>/occlusion_check.json と、並べた画 <参照の組>/occlusion_preview/<視点>.png
#   （上: エンジンの HDR と Cycles の HDR を同じ露出で、下: 同じ面の判定〔緑 一致、赤 不一致、灰 輪郭、黒 空〕と物体の識別）。
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
REQUIRED_GAME_ARGS = ("wasteland-common-light", "wasteland-gray-material", "wasteland-finest-lod")
SCRIPT = "Scripts/Terrain/BlenderOcclusionReference.py"
BLENDER_REFERENCE = "Scripts/Terrain/BlenderReference.py"
PROPS_MANIFEST = "occlusion_props.manifest.json"
PROPS_PLACEMENTS = "occlusion_props.placements.csv"
OBJECT_NAMES = {0: "terrain", 1: "talus", 2: "cliff_scan", 3: "other_prop"}
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
    if not record.get("props_ply_sha256"):
        failures.append("小物の形の記録が無い")
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
    colors = {0: (150, 120, 90), 1: (240, 160, 40), 2: (80, 140, 230), 3: (200, 80, 200)}
    objects = np.zeros(engine_hdr.shape, dtype=np.uint8)
    for value, color in colors.items():
        objects[object_index == value] = color
    top = np.concatenate([display(engine_hdr, scale), display(cycles_hdr, scale)], axis=1)
    bottom = np.concatenate([judge, objects], axis=1)
    image = Image.fromarray(np.concatenate([top, bottom], axis=0))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    image.resize((image.width // 2, image.height // 2), Image.BILINEAR).save(path)


def check_view(RC, engine_dir, reference_dir, record, view):
    result = {"view": view, "pass": False, "failures": []}
    failures = result["failures"]
    aov_path = os.path.join(engine_dir, f"{view}.aov.json")
    prefix = os.path.join(reference_dir, view)
    needed = [aov_path, os.path.join(engine_dir, f"{view}.hdr.pfm")] + \
        [f"{prefix}.aux.{part}.pfm" for part in ("depth", "normal", "object")] + [f"{prefix}.cycles.hdr.pfm"]
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
    if fraction < MIN_SAME_SURFACE_FRACTION:
        failures.append(f"同じ面の画素の割合 {fraction:.4f} が {MIN_SAME_SURFACE_FRACTION} に届かない")
    if "talus" not in per_object:
        failures.append("崖錐の岩の画素が無い（配置かカメラが違う）")
    preview = os.path.join(reference_dir, PREVIEW_DIR, f"{view}.png")
    write_preview(preview, engine_hdr, cycles_hdr, domain, same, edge, object_index)
    result["preview"] = RC.rel_dir(preview)
    result["pass"] = not failures
    return result


def run(RC, engine_root, reference_root, views, verify, trial):
    report = {"mode": "occlusion", "verify": bool(verify), "trial": bool(trial),
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
            result = check_view(RC, engine_dir, reference_dir, record, view) if os.path.isdir(engine_dir) else \
                {"view": view, "pass": False, "failures": ["エンジンの撮影が無い"]}
            entry["views"][view] = result
            failures.extend(f"{name}/{view}: {reason}" for reason in result["failures"])
            print(f"REFERENCE_CHECK occlusion set={name} view={view} same_surface={result.get('same_surface_fraction')} "
                  f"domain={result.get('domain_pixels')} per_object="
                  f"{json.dumps({k: v['same_surface_fraction'] for k, v in result.get('per_object', {}).items()})} "
                  f"pass={int(result['pass'])}", flush=True)
        report["sets"][name] = entry
        with open(os.path.join(reference_dir, "occlusion_check.json"), "w", encoding="utf-8") as f:
            json.dump(entry, f, indent=2, ensure_ascii=False)
    report["failures"] = failures
    for reason in failures:
        print(f"REFERENCE_CHECK occlusion failure: {reason}")
    ok = not failures
    print(f"REFERENCE_CHECK mode=occlusion sets={len(sets)} result={'pass' if ok else 'fail'}")
    return 0 if ok else 1
