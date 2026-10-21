# 低木 1 株の比較（SCN-WLR-PLANT-REF-BASE）の照合。ReferenceCheck.py --plant の本体。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --plant --engine <エンジンの撮影> --reference <Cycles の書き先>
#          [--views plant_near] [--trial] [--verify]
#       <エンジンの撮影>: CaptureScene.ps1 -Hdr -Aov を Game の --wasteland-common-light・--wasteland-hide=all-but-plant・
#       --wasteland-finest-lod で撮ったもの（開発ビルド。株のメッシュ reference_plant.ply も同じフォルダに書かれる）。
#       <Cycles の書き先>: BlenderReference.py --plant が書いたフォルダ。結果を <Cycles の書き先>/plant_check.json と、
#       見比べの画像 <Cycles の書き先>/plant_<視点>.png（株の周りを切り出して拡大。左上エンジン・右上 Cycles（同じ露出）、
#       左下 log2(エンジン ÷ Cycles)、右下 領域）に書く。
#
# 比較が成り立つ条件（満たさなければ落ちる。エンジンと Cycles の値の一致は求めず、表に書くだけ）。建物 1 棟の比較
# （BuildingCheck.py）と同じ扱い:
#   1. エンジンの撮影: 起動引数とログの印（最細の LOD・共通の照明・株 1 つだけ）、株の配置の行が 1 行だけで、風で揺らさず
#      PLY を書いた（頂点・三角形の数が PLY と合う）、撮った時点のジオメトリのページの進行中 0・追い出しなし、描くクラスタが
#      溢れていない、HDR の単位と値の範囲。
#   2. Cycles の参照: 出力が記録のハッシュと合う、カメラ（ReferenceCheck.gray_camera_match）・画素数・株の配置がエンジンの
#      撮影の記録と同じ、株の PLY とエンジンの深度が描いたときのもの（ハッシュ）、光の内訳の和（GRAY_SPLIT_TOLERANCE）。
#   3. 深度の輪郭: 株の周り（Blender の株の画素の外接枠を OUTLINE_PAD_PX 広げた枠）で、エンジンと Blender の強い輪郭
#      （ReferenceCheck の PROJECTION_JUMP_STRONG）の画素から相手の弱い輪郭までが PROJECTION_RADIUS_PX（2 画素）以内の割合が
#      両方向とも PROJECTION_EDGE_RATIO 以上。株の画素のうち両方の深度が同じ面に当たった割合も記録する。
#   4. Cycles の収束: 1 本あたり BlenderReference.MIN_SPP_PER_SEED サンプル以上で、領域ごとに乱数の種を変えた 2 本の差が
#      PlantRegions の基準（GrayRegions と同じ平均の差・画素の差の RMS）に収まる。--trial はこの条件だけを外す。
#   5. 領域（PlantRegions.REGIONS）の画素がそれぞれ PlantRegions.MIN_REGION_PX 以上で、表の値（両方の平均）が有限。
#      未判定の領域・量が 1 つでもあれば落ちる。
#   --verify は、エンジンの撮影の入力（inputs.json）と、Cycles の参照の入力（台本・領域の定義・共通の照明・地形のメッシュ。
#   inputs.json）を今のファイルと照らす。
#
# 表（領域ごと）: エンジンと Cycles の線形の輝度 Y の平均（cd/m²）と RGB の平均、比（エンジン ÷ Cycles）、Cycles の内訳。
# 量（描画ごと）: 株の陰 ÷ 株の日なた、透過の葉 ÷ 株の日なた、株の日なた ÷ 離れた地面、足元の影 ÷ 離れた地面（影の比）、
#   足元の日なた ÷ 離れた地面（遮蔽の比）。
# --expect の plant の量（視点の検査の結果の plant_metrics）: 影の比のエンジンと Cycles の差の絶対値（shadow_ratio_abs_diff）、
#   株の日なたの画素のうちエンジンの太陽の見え方の AOV（<視点>.aov.sun_visibility.pfm。照明が求めた影の結果）が
#   SUNLIT_VISIBLE_MIN 以上の割合（sunlit_visible_fraction）、株の日なたのエンジン ÷ Cycles（plant_sunlit_ratio）。
#   株の陰の AOV の平均（shadow_visibility_mean）も記録する。株の日なたを Cycles の株の印で葉と枝に分けた同じ量
#   （leaf_sunlit_visible_fraction・leaf_sunlit_ratio、branch_sunlit_visible_fraction・branch_sunlit_ratio）も求める。
#   葉は収束と画素の数で判定し、枝は条件不一致 SUBPIXEL_BRANCH_CONDITION として値を記録するだけ（視点の検査の結果の
#   plant_sunlit_parts）。
import json
import math
import os
import re

import numpy as np
from PIL import Image

import GrayRegions as GR
import PlantRegions as PR

PLANT_VIEWS = ("plant_near",)
PLANT_GAME_ARGS = ("--wasteland-common-light", "--wasteland-finest-lod", "--wasteland-hide=all-but-plant")
PLANT_LOG_LINES = ("WASTELAND_FINEST_LOD ", "reference_plant_only=1", "reference_plant=1")
PLANT_PLY = "reference_plant.ply"
# 深度の輪郭を比べる枠の、株の画素の外接枠からの広げ[画素]
OUTLINE_PAD_PX = 48
# 1 本あたりのサンプル数の下限（BlenderReference.MIN_SPP_PER_SEED と同じ。--gray の収束の基準）
MIN_SPP_PER_SEED = 512
# 配置の値の照合の許し（ログの書式の桁）
PLACEMENT_TOLERANCE = 1e-4
# 見比べの画像の拡大
PREVIEW_SCALE = 2
# 株の日なたで「太陽が見える」とするエンジンの太陽の見え方の AOV の下限（Cycles の株の日なたの領域の見え方の下限と同じ 0.9）
SUNLIT_VISIBLE_MIN = 0.9
# 株の日なたの枝の条件不一致の分類（WastelandPlantRef.md。断面 0.17 cm の枝は 1 画素より細く、エンジンは最小の画素幅まで
# 広げた 1 標本、Cycles は画素の中の平均なので、比べる条件が揃わない）
SUBPIXEL_BRANCH_CONDITION = "plant_branch_subpixel"


def read_json(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def check_engine(RC, engine_dir, view, verify):
    """エンジンの撮影の検査。(記録, HDR, 深度, AOV, 配置, 失敗の一覧)"""
    failures = []
    entry = {"dir": RC.rel_dir(engine_dir)}
    inputs_ok, input_failures, record = RC.check_inputs(engine_dir)
    entry["inputs_current"] = inputs_ok
    entry["files_sha256"] = (record or {}).get("files_sha256")
    entry["game_exe_sha256"] = (record or {}).get("game_exe_sha256")
    if verify and not inputs_ok:
        failures.extend(input_failures)
    game_args = RC.plate_capture_args(engine_dir)
    entry["game_args"] = game_args
    if game_args is None:
        return entry, None, None, None, None, failures + ["metrics.json が無い"]
    missing = [a for a in PLANT_GAME_ARGS if a not in game_args]
    if missing:
        failures.append(f"撮影の起動引数に {', '.join(missing)} が無い")
    hdr_path = os.path.join(engine_dir, f"{view}.hdr.pfm")
    meta_path = os.path.join(engine_dir, f"{view}.hdr.json")
    aov_path = os.path.join(engine_dir, f"{view}.aov.json")
    for path in (hdr_path, meta_path, aov_path):
        if not os.path.isfile(path):
            failures.append(f"{os.path.basename(path)} が無い")
    log_name = RC.view_game_log(engine_dir, view)
    log_path = os.path.join(engine_dir, log_name) if log_name else None
    if not log_path or not os.path.isfile(log_path):
        failures.append("Game のログが無い")
    if failures and (not os.path.isfile(hdr_path) or not os.path.isfile(aov_path) or not log_path):
        return entry, None, None, None, None, failures
    meta = read_json(meta_path)
    if meta.get("units") != "cd/m2" or meta.get("source") != "scene_color_before_bloom":
        failures.append("hdr.json の単位・読み出し元が約束と違う")
    hdr = RC.DT.read_pfm(hdr_path).astype(np.float64)
    if not np.isfinite(hdr).all() or (hdr < 0.0).any():
        failures.append("HDR に負・NaN・Inf がある")
    aov = read_json(aov_path)
    depth = RC.read_pfm_any(os.path.join(engine_dir, aov["depth"]["file"]))[..., 0].astype(np.float64)
    depth = np.where(np.isfinite(depth) & (depth > 0.0), depth, np.inf)
    with open(log_path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    for line in PLANT_LOG_LINES + (f"WASTELAND_COMMON_LIGHT view={view} case=both ",):
        if line not in text:
            failures.append(f"Game のログに {line.strip()} が無い")
    placement, count = PR.parse_placement(text)
    entry.update({"placement": placement, "reference_placements": count})
    if placement is None or count != 1:
        failures.append(f"比べる株の配置の行が 1 行でない（{count} 行）")
    else:
        if placement["wind_strength"] != 0.0:
            failures.append(f"比べる株が風で揺れる（wind_strength {placement['wind_strength']}）")
        if placement["ply"] != PLANT_PLY:
            failures.append(f"株のメッシュを書いていない（ply={placement['ply']}。開発ビルドで撮る）")
        ply_path = os.path.join(engine_dir, PLANT_PLY)
        if not os.path.isfile(ply_path):
            failures.append(f"{PLANT_PLY} が無い")
        else:
            positions, _, _, triangles = PR.read_plant_ply(ply_path)
            entry["plant_ply_sha256"] = RC.sha256_file(ply_path)
            if len(positions) != placement["vertices"] or len(triangles) != placement["triangles"]:
                failures.append(f"{PLANT_PLY} の頂点・三角形の数がログと違う（PLY {len(positions)}・{len(triangles)}、"
                                f"ログ {placement['vertices']}・{placement['triangles']}）")
    single_view = view if log_name == f"{view}.Game.log" else None
    pages = RC.geometry_pages_at_capture(text.splitlines(), single_view).get(view)
    entry["geometry_pages_at_capture"] = pages
    if pages is None:
        failures.append("Game のログに撮影の保存の行か、その前の GEOMETRY_PAGES の行が無い")
    else:
        in_flight = {k: pages.get(k) for k in ("wanted", "reading", "ready", "uploading")}
        if any(in_flight.values()) or len(pages["settle_evicted"]) != 1:
            failures.append(f"撮った時点でジオメトリのページが常駐しきっていない（進行中 {in_flight}、"
                            f"追い出しの数 {pages['settle_evicted']}）")
    dropped = [int(v) for v in re.findall(r"MEGA_CAPACITY draw_dropped=(\d+)", text)]
    entry["mega_draw_dropped_max"] = max(dropped) if dropped else None
    if dropped and max(dropped) > 0:
        failures.append(f"描くクラスタが溢れた（draw_dropped 最大 {max(dropped)}）")
    return entry, hdr, depth, aov, placement, failures


def check_reference_inputs(RC, reference_dir, verify):
    """Cycles の参照の入力の記録を読み、verify なら今のファイルと照らす。(記録, 失敗の一覧)"""
    path = os.path.join(reference_dir, "inputs.json")
    if not os.path.isfile(path):
        return None, [f"{RC.rel_dir(reference_dir)}/inputs.json が無い（BlenderReference.py --plant を回していない）"]
    record = read_json(path)
    failures = []
    if record.get("kind") != "cycles_plant":
        failures.append(f"参照の記録の種類が cycles_plant でない（{record.get('kind')}）")
    if not verify:
        return record, failures
    ok, reasons, _ = RC.check_blender_inputs(reference_dir)
    failures.extend(reasons)
    for key, rel in (("plant_regions_sha256", "Scripts/Terrain/PlantRegions.py"),
                     ("gray_regions_sha256", "Scripts/Terrain/GrayRegions.py"),
                     ("common_light_sha256", RC.COMMON_LIGHT_PATH)):
        if RC.sha256_file(os.path.join(RC.REPO_ROOT, rel)) != record.get(key):
            failures.append(f"{rel} が描いた後に変わった（古い結果）")
    return record, failures


def placement_mismatch(engine, reference):
    """エンジンのログの配置と参照が置いた配置の食い違いの理由の一覧"""
    reasons = []
    if engine is None or reference is None:
        return ["配置の記録が無い"]
    for key in ("id", "ply", "vertices", "triangles"):
        if engine.get(key) != reference.get(key):
            reasons.append(f"株の {key} が違う（エンジン {engine.get(key)}、参照 {reference.get(key)}）")
    values = [("position", i) for i in range(3)] + [(k, None) for k in ("yaw_rad", "width_scale", "height_scale",
                                                                          "color_variation", "wind_phase", "roughness")]
    for key, index in values:
        a = engine[key][index] if index is not None else engine[key]
        b = reference[key][index] if index is not None else reference[key]
        if abs(float(a) - float(b)) > PLACEMENT_TOLERANCE:
            reasons.append(f"株の {key}{'' if index is None else f'[{index}]'} が違う（エンジン {a}、参照 {b}）")
    return reasons


def outline_check(RC, engine_depth, blender_depth, plant):
    """株の周りの枠で、深度の輪郭が両方向に 2 画素以内か。(記録, 失敗の一覧, 枠)"""
    rows = np.nonzero(plant.any(axis=1))[0]
    columns = np.nonzero(plant.any(axis=0))[0]
    height, width = plant.shape
    y0, y1 = max(0, int(rows[0]) - OUTLINE_PAD_PX), min(height, int(rows[-1]) + OUTLINE_PAD_PX + 1)
    x0, x1 = max(0, int(columns[0]) - OUTLINE_PAD_PX), min(width, int(columns[-1]) + OUTLINE_PAD_PX + 1)
    window = (slice(y0, y1), slice(x0, x1))
    engine = engine_depth[window]
    blender = blender_depth[window]
    to_blender = RC.distance_summary(RC.edge_distances(RC.depth_edges(engine, RC.PROJECTION_JUMP_STRONG),
                                                       RC.depth_edges(blender, RC.PROJECTION_JUMP_WEAK),
                                                       RC.PROJECTION_MAX_DISTANCE_PX), RC.PROJECTION_RADIUS_PX)
    to_engine = RC.distance_summary(RC.edge_distances(RC.depth_edges(blender, RC.PROJECTION_JUMP_STRONG),
                                                      RC.depth_edges(engine, RC.PROJECTION_JUMP_WEAK),
                                                      RC.PROJECTION_MAX_DISTANCE_PX), RC.PROJECTION_RADIUS_PX)
    same = PR.same_surface(blender_depth, engine_depth)
    record = {"window_px": [x0, y0, x1 - 1, y1 - 1], "radius_px": RC.PROJECTION_RADIUS_PX,
              "edge_ratio_min": RC.PROJECTION_EDGE_RATIO, "engine_to_blender": to_blender, "blender_to_engine": to_engine,
              "plant_same_surface_ratio": float((plant & same).sum() / plant.sum())}
    failures = []
    for label, summary in (("エンジン→Blender", to_blender), ("Blender→エンジン", to_engine)):
        if summary.get("count", 0) == 0:
            failures.append(f"深度の輪郭 {label}: 株の周りに輪郭の画素が無い")
        elif summary["within_ratio"] < RC.PROJECTION_EDGE_RATIO:
            failures.append(f"深度の輪郭 {label}: 相手の輪郭が {RC.PROJECTION_RADIUS_PX} 画素以内の画素が "
                            f"{summary['within_ratio']:.4f}（{RC.PROJECTION_EDGE_RATIO} 未満。p99 {summary['p99_px']} 画素）")
    return record, failures, window


def region_values(engine_hdr, cycles, parts, masks):
    """領域ごとの値の表"""
    luma = GR.LUMA
    engine_y = engine_hdr @ luma
    cycles_y = cycles @ luma
    table = {}
    for name in PR.REGIONS:
        mask = masks[name]
        entry = {"label": PR.REGION_LABELS[name], "pixels": int(mask.sum())}
        if mask.any():
            e_y = float(engine_y[mask].mean())
            c_y = float(cycles_y[mask].mean())
            entry.update({
                "engine_y": e_y, "cycles_y": c_y, "ratio": e_y / c_y if c_y > 0.0 else None,
                "engine_rgb": engine_hdr[mask].mean(axis=0).tolist(), "cycles_rgb": cycles[mask].mean(axis=0).tolist(),
                "cycles_share": {g: float((parts[g][mask] @ luma).mean() / c_y) if c_y > 0.0 else None for g in parts},
            })
        table[name] = entry
    return table


def sunlit_parts(engine_hdr, cycles, seed_a, seed_b, masks, leaf, trial, spp):
    """株の日なたを葉と枝に分けた表（PlantRegions の領域の定義は変えず、Cycles の株の印で分ける）。
    葉は収束と画素の数で判定・未判定を決め、枝は SCN-WLR-PLANT-REF-NEAR の分類どおり条件不一致（SUBPIXEL_BRANCH_CONDITION）"""
    engine_y, cycles_y = engine_hdr @ GR.LUMA, cycles @ GR.LUMA
    y_a, y_b = seed_a @ GR.LUMA, seed_b @ GR.LUMA
    parts = {}
    for name, mask in (("leaf", masks["plant_sunlit"] & leaf), ("branch", masks["plant_sunlit"] & ~leaf)):
        entry = {"pixels": int(mask.sum())}
        if mask.any():
            e_y, c_y = float(engine_y[mask].mean()), float(cycles_y[mask].mean())
            a, b = y_a[mask], y_b[mask]
            mean = float(0.5 * (a.mean() + b.mean()))
            entry.update({"engine_y": e_y, "cycles_y": c_y, "ratio": e_y / c_y if c_y > 0.0 else None,
                          "mean_diff": float(abs(a.mean() - b.mean()) / mean) if mean > 0.0 else None,
                          "pixel_rms": float(np.sqrt(np.mean((a - b) ** 2)) / mean) if mean > 0.0 else None})
        converged = (entry.get("mean_diff") is not None and entry["mean_diff"] <= GR.CONVERGENCE_MEAN and
                     entry["pixel_rms"] <= GR.CONVERGENCE_PIXEL_RMS and spp >= MIN_SPP_PER_SEED)
        if name == "branch":
            entry["status"] = f"条件不一致（{SUBPIXEL_BRANCH_CONDITION}）"
        elif entry["pixels"] < PR.MIN_REGION_PX:
            entry["status"] = "未判定（画素が足りない）"
        elif entry.get("ratio") is None:
            entry["status"] = "未判定（値が求まらない）"
        elif trial:
            entry["status"] = "判定（試し: 収束を見ない）"
        else:
            entry["status"] = "判定" if converged else "未判定（収束しない）"
        parts[name] = entry
    return parts


def plant_metrics(RC, engine_dir, aov, masks, table, ratios, leaf, parts):
    """--expect の plant の量（エンジンの太陽の見え方の AOV と、表・比から求める値）。leaf_* は株の日なたの葉の画素だけ、
    branch_* は枝の画素だけ（条件不一致の記録）。葉が未判定なら leaf_* は None（--expect で未判定になる）"""
    metrics = {"sunlit_visible_min": SUNLIT_VISIBLE_MIN}
    engine_ratio = (ratios.get("engine") or {}).get("ground_shadow_over_ref")
    cycles_ratio = (ratios.get("cycles") or {}).get("ground_shadow_over_ref")
    metrics["shadow_ratio_abs_diff"] = (abs(engine_ratio - cycles_ratio)
                                        if engine_ratio is not None and cycles_ratio is not None else None)
    metrics["plant_sunlit_ratio"] = table["plant_sunlit"].get("ratio")
    leaf_judged = parts["leaf"]["status"].startswith("判定")
    metrics["leaf_sunlit_ratio"] = parts["leaf"].get("ratio") if leaf_judged else None
    metrics["branch_sunlit_ratio"] = parts["branch"].get("ratio")
    metrics["branch_condition"] = SUBPIXEL_BRANCH_CONDITION
    entry = aov.get("sun_visibility") or {}
    path = os.path.join(engine_dir, entry.get("file", ""))
    if not entry.get("file") or not os.path.isfile(path):
        metrics.update({"sunlit_visible_fraction": None, "shadow_visibility_mean": None,
                        "leaf_sunlit_visible_fraction": None, "branch_sunlit_visible_fraction": None})
        return metrics
    visibility = RC.read_pfm_any(path)[..., 0].astype(np.float64)
    readable = np.isfinite(visibility) & (visibility >= 0.0)
    for name, key in (("plant_sunlit", "sunlit"), ("plant_shadow", "shadow")):
        values = visibility[masks[name] & readable]
        metrics[f"{key}_visibility_pixels"] = int(values.size)
        metrics[f"{key}_visibility_mean"] = float(values.mean()) if values.size else None
    for key, mask in (("sunlit", masks["plant_sunlit"]), ("leaf_sunlit", masks["plant_sunlit"] & leaf),
                      ("branch_sunlit", masks["plant_sunlit"] & ~leaf)):
        values = visibility[mask & readable]
        metrics[f"{key}_visible_fraction"] = float((values >= SUNLIT_VISIBLE_MIN).mean()) if values.size else None
    if not leaf_judged:
        metrics["leaf_sunlit_visible_fraction"] = None
    return metrics


def derived_ratios(table, key):
    """描画（key = engine_y か cycles_y）ごとの、領域どうしの比"""
    def value(name):
        v = table[name].get(key)
        return v if v is not None and math.isfinite(v) and v > 0.0 else None

    def ratio(a, b):
        return a / b if a is not None and b is not None else None
    sunlit, ground = value("plant_sunlit"), value("ground_ref")
    return {
        "plant_shadow_over_sunlit": ratio(value("plant_shadow"), sunlit),
        "leaf_backlit_over_plant_sunlit": ratio(value("leaf_backlit"), sunlit),
        "plant_sunlit_over_ground_ref": ratio(sunlit, ground),
        "ground_shadow_over_ref": ratio(value("ground_shadow"), ground),
        "ground_near_over_ref": ratio(value("ground_near"), ground),
    }


def write_preview(path, window, engine_hdr, cycles, masks, valid):
    """株の周りを切り出して拡大: 左上エンジン、右上 Cycles（同じ露出）、左下 log2(エンジン ÷ Cycles)（±1 段で飽和）、右下 領域"""
    def display(image, scale):
        x = np.clip(image * scale, 0.0, 1.0)
        srgb = np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1.0 / 2.4) - 0.055)
        return (srgb * 255.0 + 0.5).astype(np.uint8)
    engine = engine_hdr[window]
    reference = cycles[window]
    inside = valid[window]
    reference_y = (reference @ GR.LUMA)[inside]
    scale = 0.5 / float(np.percentile(reference_y, 90)) if reference_y.size else 1.0
    ratio = np.log2(np.maximum(engine @ GR.LUMA, 1e-6) / np.maximum(reference @ GR.LUMA, 1e-6))
    t = np.clip(ratio, -1.0, 1.0)
    heat = np.stack([np.where(t > 0, 1.0, 1.0 + t), 1.0 - np.abs(t), np.where(t < 0, 1.0, 1.0 - t)], axis=-1)
    heat = np.where(inside[..., None], heat, 0.15)
    colors = {"plant_sunlit": (240, 140, 40), "plant_shadow": (70, 90, 210), "leaf_backlit": (120, 220, 80),
              "ground_shadow": (200, 60, 60), "ground_near": (230, 200, 60), "ground_ref": (80, 170, 90)}
    regions = np.full(engine.shape, 30, dtype=np.uint8)
    for name, color in colors.items():
        regions[masks[name][window]] = color
    top = np.concatenate([display(engine, scale), display(reference, scale)], axis=1)
    bottom = np.concatenate([(heat * 255.0).astype(np.uint8), regions], axis=1)
    image = Image.fromarray(np.concatenate([top, bottom], axis=0))
    image.resize((image.width * PREVIEW_SCALE, image.height * PREVIEW_SCALE), Image.NEAREST).save(path)


def check_view(RC, engine_dir, reference_dir, view, record, verify, trial):
    failures = []
    result = {"view": view}
    engine_entry, engine_hdr, engine_depth, aov, placement, engine_failures = check_engine(RC, engine_dir, view, verify)
    result["engine"] = engine_entry
    failures.extend(f"エンジン: {f}" for f in engine_failures)
    meta_path = os.path.join(reference_dir, f"{view}.cycles.json")
    if engine_hdr is None:
        return dict(result, failures=failures, **{"pass": False})
    if not os.path.isfile(meta_path):
        failures.append(f"{view}.cycles.json が無い")
        return dict(result, failures=failures, **{"pass": False})
    meta = read_json(meta_path)
    for name, digest in (meta.get("outputs_sha256") or {}).items():
        path = os.path.join(reference_dir, name)
        if not os.path.isfile(path) or RC.sha256_file(path) != digest:
            failures.append(f"Cycles の出力 {name} が無いか、記録のハッシュと違う")
    if failures:
        return dict(result, failures=failures, **{"pass": False})
    view_record = ((record or {}).get("views") or {}).get(view) or {}
    plant_meta = meta.get("plant") or {}
    result["cycles"] = {k: meta.get(k) for k in ("renderer", "device", "triangles", "spp_per_seed", "stop_reason",
                                                 "load_seconds", "render_seconds")}
    result["plant"] = {k: plant_meta.get(k) for k in ("id", "ply", "parts", "material", "opacity_mask")}
    failures.extend(RC.gray_camera_match(aov["camera"], meta["camera_engine"]))
    if (int(aov["width"]), int(aov["height"])) != (int(meta["width"]), int(meta["height"])):
        failures.append("画素数がエンジンと違う")
        return dict(result, failures=failures, **{"pass": False})
    for label, other in (("cycles.json", plant_meta.get("placement")), ("inputs.json", view_record.get("placement"))):
        failures.extend(f"{label}: {r}" for r in placement_mismatch(placement, other))
    if engine_entry.get("plant_ply_sha256") != (record or {}).get("plant_ply_sha256"):
        failures.append("Cycles が読んだ株の PLY がエンジンの撮影の PLY と違う（撮り直した後に描いていない）")
    depth_digest = RC.sha256_file(os.path.join(engine_dir, aov["depth"]["file"]))
    if depth_digest != view_record.get("engine_depth_sha256"):
        failures.append("Cycles が領域に使ったエンジンの深度が今の撮影の深度と違う（撮り直した後に描いていない）")
    shifted = RC.shifted_reference_failure(meta)
    if shifted:
        failures.append(shifted)

    prefix = os.path.join(reference_dir, view)
    cycles = RC.read_pfm_any(prefix + ".cycles.hdr.pfm").astype(np.float64)
    seed_a = RC.read_pfm_any(prefix + ".cycles.seed_a.pfm").astype(np.float64)
    seed_b = RC.read_pfm_any(prefix + ".cycles.seed_b.pfm").astype(np.float64)
    parts = {g: RC.read_pfm_any(prefix + f".cycles.{g}.pfm").astype(np.float64) for g in ("sun", "sky", "direct",
                                                                                         "indirect")}
    visibility = RC.read_pfm_any(prefix + ".aux.visibility.pfm")[..., 0]
    back_visibility = RC.read_pfm_any(prefix + ".aux.back_visibility.pfm")[..., 0]
    n_dot_l = RC.read_pfm_any(prefix + ".aux.n_dot_l.pfm")[..., 0]
    alpha = RC.read_pfm_any(prefix + ".aux.alpha.pfm")[..., 0]
    blender_depth = RC.read_pfm_any(prefix + ".aux.depth.pfm")[..., 0].astype(np.float64)
    plant_index = RC.read_pfm_any(prefix + ".aux.plant.pfm")[..., 0]
    if cycles.shape != engine_hdr.shape or blender_depth.shape != engine_depth.shape:
        failures.append("Cycles の大きさがエンジンと違う")
        return dict(result, failures=failures, **{"pass": False})
    for name, image in (("合成", cycles), ("A", seed_a), ("B", seed_b)):
        if not np.isfinite(image).all() or (image < 0.0).any():
            failures.append(f"Cycles の{name}に負・NaN・Inf がある")
    plant = (np.rint(plant_index) >= PR.PLANT_BRANCH_PASS_INDEX) & (alpha > 0.5)
    if not plant.any():
        failures.append("Cycles に株の画素が無い")
        return dict(result, failures=failures, **{"pass": False})

    # 光の内訳の和（直接 + 間接、太陽の組 + 空の組 が合成に合う）
    surface = alpha > 0.5
    combined_sum = float((cycles[surface] @ GR.LUMA).sum())
    consistency = {
        "direct_plus_indirect": float(((parts["direct"] + parts["indirect"])[surface] @ GR.LUMA).sum() / combined_sum),
        "sun_plus_sky": float(((parts["sun"] + parts["sky"])[surface] @ GR.LUMA).sum() / combined_sum),
    }
    result["cycles_split_consistency"] = consistency
    for key, value in consistency.items():
        if abs(value - 1.0) > RC.GRAY_SPLIT_TOLERANCE:
            failures.append(f"Cycles の {key} の和が合成の {value:.4f} 倍（±{RC.GRAY_SPLIT_TOLERANCE}）")

    # 3. 深度の輪郭
    outline, outline_failures, window = outline_check(RC, engine_depth, blender_depth, plant)
    result["depth_outline"] = outline
    failures.extend(outline_failures)

    # 領域と収束
    pp = plant_meta.get("placement") or placement
    masks, region_record = PR.region_masks(aov["camera"], blender_depth, engine_depth, plant_index, n_dot_l, visibility,
                                           back_visibility, surface, pp)
    result["region_geometry"] = region_record
    stats, _ = PR.convergence(seed_a @ GR.LUMA, seed_b @ GR.LUMA, masks)
    spp = int(meta.get("spp_per_seed") or 0)
    result["convergence_required"] = not trial
    if not trial and spp < MIN_SPP_PER_SEED:
        failures.append(f"Cycles の 1 本あたりのサンプル数 {spp} が {MIN_SPP_PER_SEED} に届かない（収束の基準に足りない）")
    table = region_values(engine_hdr, cycles, parts, masks)
    for name in PR.REGIONS:
        entry = table[name]
        entry["convergence"] = stats[name]
        finite = all(entry.get(k) is not None and math.isfinite(entry[k]) for k in ("engine_y", "cycles_y"))
        if entry["pixels"] < PR.MIN_REGION_PX:
            entry["status"] = "未判定（画素が足りない）"
        elif not finite:
            entry["status"] = "未判定（値が求まらない）"
        elif trial:
            entry["status"] = "判定（試し: 収束を見ない）"
        elif stats[name]["converged"] and spp >= MIN_SPP_PER_SEED:
            entry["status"] = "判定"
        else:
            entry["status"] = "未判定（収束しない）"
        if entry["status"].startswith("未判定"):
            failures.append(f"領域 {name}（{entry['label']}）が{entry['status']}（画素 {entry['pixels']}、"
                            f"下限 {PR.MIN_REGION_PX}）")
    result["regions"] = table
    result["ratios"] = {"engine": derived_ratios(table, "engine_y"), "cycles": derived_ratios(table, "cycles_y")}
    for source, ratios in result["ratios"].items():
        for key, value in ratios.items():
            if value is None:
                failures.append(f"{source} の {key} が求まらない")
    leaf = np.rint(plant_index).astype(np.int32) == PR.PLANT_LEAF_PASS_INDEX
    parts_sunlit = sunlit_parts(engine_hdr, cycles, seed_a, seed_b, masks, leaf, trial, spp)
    result["plant_sunlit_parts"] = parts_sunlit
    result["plant_metrics"] = plant_metrics(RC, engine_dir, aov, masks, table, result["ratios"], leaf, parts_sunlit)
    valid = plant.copy()
    for name in PR.REGIONS:
        valid |= masks[name]
    preview = os.path.join(reference_dir, f"plant_{view}.png")
    write_preview(preview, window, engine_hdr, cycles, masks, valid)
    result["preview"] = RC.rel_dir(preview)
    result.update({"pass": not failures, "failures": failures})
    return result


def run(RC, engine_dirs, reference_dir, views, verify, trial, expect=None):
    failures = []
    views = views or list(PLANT_VIEWS)
    if not engine_dirs or not reference_dir:
        print("REFERENCE_CHECK result=fail reason=dirs_missing（--engine と --reference を指定する）")
        return 1
    engine_dir = engine_dirs[0]
    report = {"mode": "plant", "verify": bool(verify), "trial": bool(trial), "views": views,
              "engine": RC.rel_dir(engine_dir), "reference": RC.rel_dir(reference_dir),
              "region_criteria": PR.criteria(), "min_spp_per_seed": MIN_SPP_PER_SEED, "outline_pad_px": OUTLINE_PAD_PX}
    record, reference_failures = check_reference_inputs(RC, reference_dir, verify)
    report["reference_inputs"] = {k: (record or {}).get(k) for k in ("kind", "script_sha256", "terrain_material",
                                                                     "leaf_transmittance_ratio", "mesh_dir", "range",
                                                                     "trial", "git_commit")}
    failures.extend(f"参照: {f}" for f in reference_failures)
    report["view_results"] = []
    for view in views:
        result = check_view(RC, engine_dir, reference_dir, view, record, verify, trial)
        report["view_results"].append(result)
        failures.extend(f"{view}: {f}" for f in result["failures"])
        cycles = result.get("cycles") or {}
        outline = result.get("depth_outline") or {}
        print(f"REFERENCE_CHECK plant view={view} pass={int(result['pass'])} spp_per_seed={cycles.get('spp_per_seed')} "
              f"stop={cycles.get('stop_reason')} outline_e2b={(outline.get('engine_to_blender') or {}).get('within_ratio')} "
              f"outline_b2e={(outline.get('blender_to_engine') or {}).get('within_ratio')} "
              f"plant_same_surface={outline.get('plant_same_surface_ratio')} "
              f"split={result.get('cycles_split_consistency')}")
        for name, entry in (result.get("regions") or {}).items():
            conv = entry["convergence"]
            print(f"REFERENCE_CHECK plant view={view} region={name} status={entry['status']} pixels={entry['pixels']} "
                  f"engine_y={entry.get('engine_y', float('nan')):.1f} cycles_y={entry.get('cycles_y', float('nan')):.1f} "
                  f"ratio={entry.get('ratio') or float('nan'):.3f} "
                  f"mean_diff={conv.get('mean_diff', float('nan')):.4f} pixel_rms={conv.get('pixel_rms', float('nan')):.3f}")
        for source, ratios in (result.get("ratios") or {}).items():
            print(f"REFERENCE_CHECK plant view={view} ratios source={source} " +
                  " ".join(f"{k}={v if v is None else round(v, 4)}" for k, v in ratios.items()))
        for name, entry in (result.get("plant_sunlit_parts") or {}).items():
            print(f"REFERENCE_CHECK plant view={view} sunlit_part={name} status={entry['status']} pixels={entry['pixels']} "
                  f"engine_y={entry.get('engine_y', float('nan')):.1f} cycles_y={entry.get('cycles_y', float('nan')):.1f} "
                  f"ratio={entry.get('ratio') or float('nan'):.3f} "
                  f"mean_diff={entry.get('mean_diff') or float('nan'):.4f} pixel_rms={entry.get('pixel_rms') or float('nan'):.3f}")
        metrics = result.get("plant_metrics") or {}
        print(f"REFERENCE_CHECK plant view={view} metrics " +
              " ".join(f"{k}={v if v is None or isinstance(v, (int, str)) else round(v, 4)}" for k, v in metrics.items()))
    RC.apply_expect(report, expect, "plant", failures)
    report["failures"] = failures
    report_path = os.path.join(reference_dir, "plant_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    engine_current = all((r.get("engine") or {}).get("inputs_current") for r in report["view_results"])
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=plant views={len(views)} "
          f"trial={int(bool(trial))} inputs_current={int(engine_current)} "
          f"reference_inputs_checked={int(bool(verify))} report={report_path}")
    return 0 if not failures else 1
