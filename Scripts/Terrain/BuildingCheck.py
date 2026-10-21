# 建物 1 棟の比較（SCN-WLR-BUILDING-REF-BASE）の照合。ReferenceCheck.py --building の本体。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --building --engine <エンジンの撮影> --reference <Cycles の書き先>
#          [--views outpost] [--trial] [--verify]
#       <エンジンの撮影>: CaptureScene.ps1 -Hdr -Aov を Game の --wasteland-common-light・--wasteland-hide=all-but-building・
#       --wasteland-finest-lod で撮ったもの。<Cycles の書き先>: BlenderReference.py --building が書いたフォルダ。
#       結果を <Cycles の書き先>/building_check.json と、見比べの画像 <Cycles の書き先>/building_<視点>.png（建物の周りを
#       切り出して拡大。左上エンジン・右上 Cycles（同じ露出）、左下 log2(エンジン ÷ Cycles)、右下 領域）に書く。
#
# 比較が成り立つ条件（満たさなければ落ちる。エンジンと Cycles の値の一致は求めず、表に書くだけ）:
#   1. エンジンの撮影: 起動引数とログの印（最細の LOD・共通の照明・建物 1 棟だけ）、建物の配置の行が 1 行だけ、撮った時点の
#      ジオメトリのページの進行中 0・追い出しなし、描くクラスタが溢れていない、HDR の単位と値の範囲。
#   2. Cycles の参照: 出力が記録のハッシュと合う、カメラ（ReferenceCheck.gray_camera_match）・画素数・建物の配置（glTF・位置・
#      回転・拡大率）がエンジンの撮影の記録と同じ、光の内訳の和（--gray と同じ GRAY_SPLIT_TOLERANCE）。
#   3. 深度の輪郭: 建物の周り（Blender の建物の画素の外接枠を OUTLINE_PAD_PX 広げた枠）で、エンジンと Blender の強い輪郭
#      （ReferenceCheck の PROJECTION_JUMP_STRONG）の画素から相手の弱い輪郭までが PROJECTION_RADIUS_PX（2 画素）以内の割合が
#      両方向とも PROJECTION_EDGE_RATIO 以上。
#   4. Cycles の収束: 1 本あたり BlenderReference.MIN_SPP_PER_SEED サンプル以上で、領域ごとに乱数の種を変えた 2 本の差が
#      BuildingRegions の基準（GrayRegions と同じ平均の差・画素の差の RMS）に収まる。--trial はこの条件だけを外す。
#   5. 領域（BuildingRegions.REGIONS）の画素がそれぞれ BuildingRegions.MIN_REGION_PX 以上で、表の値（両方の平均）が有限。
#      深度から求めた建物の下端と地面の高さの差が両方の描画で求まる。未判定の領域・量が 1 つでもあれば落ちる。
#   --verify は、エンジンの撮影の入力（inputs.json）と、Cycles の参照の入力（台本・領域の定義・共通の照明・glTF・地形の
#   メッシュ。inputs.json）を今のファイルと照らす。
#
# 表（領域ごと）: エンジンと Cycles の線形の輝度 Y の平均（cd/m²）と RGB の平均、比（エンジン ÷ Cycles）、Cycles の内訳。
# 量: 壁の日なた ÷ 壁の陰、壁の下端 ÷ 壁の日なたと陰の平均、足元の帯 ÷ 離れた日なたの地面（描画ごと）、建物の下端と地面の
#   高さの差（描画ごと。BuildingRegions.bottom_gap）。
import json
import math
import os
import re

import numpy as np
from PIL import Image

import BuildingRegions as BR
import GrayRegions as GR

BUILDING_VIEWS = ("outpost",)
BUILDING_GAME_ARGS = ("--wasteland-common-light", "--wasteland-finest-lod", "--wasteland-hide=all-but-building")
BUILDING_LOG_LINES = ("WASTELAND_FINEST_LOD ", "reference_building_only=1")
# 深度の輪郭を比べる枠の、建物の画素の外接枠からの広げ[画素]
OUTLINE_PAD_PX = 48
# 1 本あたりのサンプル数の下限（BlenderReference.MIN_SPP_PER_SEED と同じ。--gray の収束の基準）
MIN_SPP_PER_SEED = 512
# 配置の値の照合の許し（ログの書式の桁）
PLACEMENT_TOLERANCE = 1e-4
# 見比べの画像の拡大
PREVIEW_SCALE = 3


def read_json(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def check_engine(RC, engine_dir, view, verify):
    """エンジンの撮影の検査。(記録, HDR, 深度, カメラ, 配置, 失敗の一覧)"""
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
    missing = [a for a in BUILDING_GAME_ARGS if a not in game_args]
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
    for line in BUILDING_LOG_LINES + (f"WASTELAND_COMMON_LIGHT view={view} case=both ",):
        if line not in text:
            failures.append(f"Game のログに {line.strip()} が無い")
    placement, count = BR.parse_placement(text)
    placed = len(re.findall(r"WASTELAND_BUILDING_PLACEMENT ", text))
    entry.update({"placement": placement, "reference_placements": count, "building_placements": placed})
    if placement is None or count != 1 or placed != 1:
        failures.append(f"置いた建物が比べる 1 棟だけでない（比べる建物の行 {count}、建物の配置の行 {placed}）")
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
        return None, [f"{RC.rel_dir(reference_dir)}/inputs.json が無い（BlenderReference.py --building を回していない）"]
    record = read_json(path)
    failures = []
    if record.get("kind") != "cycles_building":
        failures.append(f"参照の記録の種類が cycles_building でない（{record.get('kind')}）")
    if not verify:
        return record, failures
    ok, reasons, _ = RC.check_blender_inputs(reference_dir)
    failures.extend(reasons)
    for key, rel in (("building_regions_sha256", "Scripts/Terrain/BuildingRegions.py"),
                     ("gray_regions_sha256", "Scripts/Terrain/GrayRegions.py"),
                     ("common_light_sha256", RC.COMMON_LIGHT_PATH)):
        if RC.sha256_file(os.path.join(RC.REPO_ROOT, rel)) != record.get(key):
            failures.append(f"{rel} が描いた後に変わった（古い結果）")
    for rel, digest in (record.get("gltf_sha256") or {}).items():
        gltf = os.path.join(RC.REPO_ROOT, rel)
        if not os.path.isfile(gltf) or RC.sha256_file(gltf) != digest:
            failures.append(f"建物の glTF {rel} が無いか、描いた後に変わった（古い結果）")
    return record, failures


def placement_mismatch(engine, reference):
    """エンジンのログの配置と参照が置いた配置の食い違いの理由の一覧"""
    reasons = []
    if engine is None or reference is None:
        return ["配置の記録が無い"]
    for key in ("id", "gltf"):
        if engine.get(key) != reference.get(key):
            reasons.append(f"建物の {key} が違う（エンジン {engine.get(key)}、参照 {reference.get(key)}）")
    values = [("position", i) for i in range(3)] + [("yaw_rad", None), ("scale", None), ("mesh_min_y", None)]
    for key, index in values:
        a = engine[key][index] if index is not None else engine[key]
        b = reference[key][index] if index is not None else reference[key]
        if abs(float(a) - float(b)) > PLACEMENT_TOLERANCE:
            reasons.append(f"建物の {key}{'' if index is None else f'[{index}]'} が違う（エンジン {a}、参照 {b}）")
    return reasons


def outline_check(RC, engine_depth, blender_depth, building):
    """建物の周りの枠で、深度の輪郭が両方向に 2 画素以内か。(記録, 失敗の一覧)"""
    rows = np.nonzero(building.any(axis=1))[0]
    columns = np.nonzero(building.any(axis=0))[0]
    height, width = building.shape
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
    record = {"window_px": [x0, y0, x1 - 1, y1 - 1], "radius_px": RC.PROJECTION_RADIUS_PX,
              "edge_ratio_min": RC.PROJECTION_EDGE_RATIO, "engine_to_blender": to_blender, "blender_to_engine": to_engine}
    failures = []
    for label, summary in (("エンジン→Blender", to_blender), ("Blender→エンジン", to_engine)):
        if summary.get("count", 0) == 0:
            failures.append(f"深度の輪郭 {label}: 建物の周りに輪郭の画素が無い")
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
    for name in BR.REGIONS:
        mask = masks[name]
        entry = {"label": BR.REGION_LABELS[name], "pixels": int(mask.sum())}
        if mask.any():
            e_y = float(engine_y[mask].mean())
            c_y = float(cycles_y[mask].mean())
            e_rgb = engine_hdr[mask].mean(axis=0)
            c_rgb = cycles[mask].mean(axis=0)
            entry.update({
                "engine_y": e_y, "cycles_y": c_y, "ratio": e_y / c_y if c_y > 0.0 else None,
                "engine_rgb": e_rgb.tolist(), "cycles_rgb": c_rgb.tolist(),
                "cycles_share": {g: float((parts[g][mask] @ luma).mean() / c_y) if c_y > 0.0 else None for g in parts},
            })
        table[name] = entry
    return table


def derived_ratios(table, key):
    """描画（key = engine_y か cycles_y）ごとの、領域どうしの比"""
    def value(name):
        v = table[name].get(key)
        return v if v is not None and math.isfinite(v) and v > 0.0 else None

    def ratio(a, b):
        return a / b if a is not None and b is not None else None
    sunlit, shadow = value("wall_sunlit"), value("wall_shadow")
    walls = 0.5 * (sunlit + shadow) if sunlit is not None and shadow is not None else None
    return {
        "wall_sunlit_over_shadow": ratio(sunlit, shadow),
        "wall_bottom_over_walls": ratio(value("wall_bottom"), walls),
        "ground_band_over_ref": ratio(value("ground_band"), value("ground_ref")),
    }


def write_preview(path, window, engine_hdr, cycles, masks, valid):
    """建物の周りを切り出して拡大: 左上エンジン、右上 Cycles（同じ露出）、左下 log2(エンジン ÷ Cycles)（±1 段で飽和）、右下 領域"""
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
    colors = {"ground_band": (230, 200, 60), "ground_ref": (80, 170, 90), "wall_bottom": (200, 60, 60),
              "wall_sunlit": (240, 140, 40), "wall_shadow": (70, 90, 210)}
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
    building_meta = meta.get("building") or {}
    result["cycles"] = {k: meta.get(k) for k in ("renderer", "device", "triangles", "spp_per_seed", "stop_reason",
                                                 "load_seconds", "render_seconds")}
    result["building"] = {k: building_meta.get(k) for k in ("id", "gltf", "triangles", "materials", "material_source",
                                                            "blender_bottom_z", "placement_bottom_y")}
    failures.extend(RC.gray_camera_match(aov["camera"], meta["camera_engine"]))
    if (int(aov["width"]), int(aov["height"])) != (int(meta["width"]), int(meta["height"])):
        failures.append("画素数がエンジンと違う")
        return dict(result, failures=failures, **{"pass": False})
    for label, other in (("cycles.json", building_meta.get("placement")), ("inputs.json", view_record.get("placement"))):
        failures.extend(f"{label}: {r}" for r in placement_mismatch(placement, other))
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
    n_dot_l = RC.read_pfm_any(prefix + ".aux.n_dot_l.pfm")[..., 0]
    normal = RC.read_pfm_any(prefix + ".aux.normal.pfm")
    alpha = RC.read_pfm_any(prefix + ".aux.alpha.pfm")[..., 0]
    blender_depth = RC.read_pfm_any(prefix + ".aux.depth.pfm")[..., 0].astype(np.float64)
    building = RC.read_pfm_any(prefix + ".aux.building.pfm")[..., 0] > 0.5
    if cycles.shape != engine_hdr.shape or blender_depth.shape != engine_depth.shape:
        failures.append("Cycles の大きさがエンジンと違う")
        return dict(result, failures=failures, **{"pass": False})
    for name, image in (("合成", cycles), ("A", seed_a), ("B", seed_b)):
        if not np.isfinite(image).all() or (image < 0.0).any():
            failures.append(f"Cycles の{name}に負・NaN・Inf がある")
    if not building.any():
        failures.append("Cycles に建物の画素が無い")
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
    outline, outline_failures, window = outline_check(RC, engine_depth, blender_depth, building)
    result["depth_outline"] = outline
    failures.extend(outline_failures)

    # 領域と収束
    bp = building_meta.get("placement") or placement
    bounds = building_meta.get("bounds") or view_record.get("bounds")
    masks, region_record = BR.region_masks(aov["camera"], blender_depth, building, normal[..., 1], n_dot_l, visibility,
                                           surface, bp, bounds)
    result["region_geometry"] = region_record
    stats, _ = BR.convergence(seed_a @ GR.LUMA, seed_b @ GR.LUMA, masks)
    spp = int(meta.get("spp_per_seed") or 0)
    result["convergence_required"] = not trial
    if not trial and spp < MIN_SPP_PER_SEED:
        failures.append(f"Cycles の 1 本あたりのサンプル数 {spp} が {MIN_SPP_PER_SEED} に届かない（収束の基準に足りない）")
    table = region_values(engine_hdr, cycles, parts, masks)
    for name in BR.REGIONS:
        entry = table[name]
        entry["convergence"] = stats[name]
        finite = all(entry.get(k) is not None and math.isfinite(entry[k]) for k in ("engine_y", "cycles_y"))
        if entry["pixels"] < BR.MIN_REGION_PX:
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
                            f"下限 {BR.MIN_REGION_PX}）")
    result["regions"] = table
    result["ratios"] = {"engine": derived_ratios(table, "engine_y"), "cycles": derived_ratios(table, "cycles_y")}
    for source, ratios in result["ratios"].items():
        for key, value in ratios.items():
            if value is None:
                failures.append(f"{source} の {key} が求まらない")

    # 深度から求めた建物の下端と地面の高さの差（描画ごと）
    gaps = {"engine": BR.bottom_gap(aov["camera"], engine_depth, building, bp, bounds),
            "cycles": BR.bottom_gap(aov["camera"], blender_depth, building, bp, bounds)}
    result["bottom_gap"] = gaps
    for source, gap in gaps.items():
        if gap.get("gap_m") is None:
            failures.append(f"{source} の深度から建物の下端と地面の高さの差が求まらない（建物の画素 {gap['building_pixels']}、"
                            f"地面の画素 {gap['ground_pixels']}）")
    valid = np.zeros(building.shape, dtype=bool)
    for name in BR.REGIONS:
        valid |= masks[name]
    preview = os.path.join(reference_dir, f"building_{view}.png")
    write_preview(preview, window, engine_hdr, cycles, masks, valid | building)
    result["preview"] = RC.rel_dir(preview)
    result.update({"pass": not failures, "failures": failures})
    return result


def run(RC, engine_dirs, reference_dir, views, verify, trial):
    failures = []
    views = views or list(BUILDING_VIEWS)
    if not engine_dirs or not reference_dir:
        print("REFERENCE_CHECK result=fail reason=dirs_missing（--engine と --reference を指定する）")
        return 1
    engine_dir = engine_dirs[0]
    report = {"mode": "building", "verify": bool(verify), "trial": bool(trial), "views": views,
              "engine": RC.rel_dir(engine_dir), "reference": RC.rel_dir(reference_dir),
              "region_criteria": BR.criteria(), "min_spp_per_seed": MIN_SPP_PER_SEED, "outline_pad_px": OUTLINE_PAD_PX}
    record, reference_failures = check_reference_inputs(RC, reference_dir, verify)
    report["reference_inputs"] = {k: (record or {}).get(k) for k in ("kind", "script_sha256", "terrain_material",
                                                                     "mesh_dir", "range", "trial", "git_commit")}
    failures.extend(f"参照: {f}" for f in reference_failures)
    report["view_results"] = []
    for view in views:
        result = check_view(RC, engine_dir, reference_dir, view, record, verify, trial)
        report["view_results"].append(result)
        failures.extend(f"{view}: {f}" for f in result["failures"])
        cycles = result.get("cycles") or {}
        outline = result.get("depth_outline") or {}
        print(f"REFERENCE_CHECK building view={view} pass={int(result['pass'])} spp_per_seed={cycles.get('spp_per_seed')} "
              f"stop={cycles.get('stop_reason')} outline_e2b={(outline.get('engine_to_blender') or {}).get('within_ratio')} "
              f"outline_b2e={(outline.get('blender_to_engine') or {}).get('within_ratio')} "
              f"split={result.get('cycles_split_consistency')}")
        for name, entry in (result.get("regions") or {}).items():
            conv = entry["convergence"]
            print(f"REFERENCE_CHECK building view={view} region={name} status={entry['status']} pixels={entry['pixels']} "
                  f"engine_y={entry.get('engine_y', float('nan')):.1f} cycles_y={entry.get('cycles_y', float('nan')):.1f} "
                  f"ratio={entry.get('ratio') or float('nan'):.3f} "
                  f"mean_diff={conv.get('mean_diff', float('nan')):.4f} pixel_rms={conv.get('pixel_rms', float('nan')):.3f}")
        for source, ratios in (result.get("ratios") or {}).items():
            print(f"REFERENCE_CHECK building view={view} ratios source={source} " +
                  " ".join(f"{k}={v if v is None else round(v, 4)}" for k, v in ratios.items()))
        for source, gap in (result.get("bottom_gap") or {}).items():
            print(f"REFERENCE_CHECK building view={view} bottom_gap source={source} gap_m={gap.get('gap_m')} "
                  f"bottom_y={gap.get('bottom_y')} ground_y={gap.get('ground_y')} "
                  f"placement_bottom_y={gap.get('placement_bottom_y')}")
    report["failures"] = failures
    report_path = os.path.join(reference_dir, "building_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    engine_current = all((r.get("engine") or {}).get("inputs_current") for r in report["view_results"])
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=building views={len(views)} "
          f"trial={int(bool(trial))} inputs_current={int(engine_current)} "
          f"reference_inputs_checked={int(bool(verify))} report={report_path}")
    return 0 if not failures else 1
