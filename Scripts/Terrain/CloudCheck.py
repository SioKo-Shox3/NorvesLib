# 雲の比較（SCN-WLR-CLOUD-REF-BLENDER）の照合。ReferenceCheck.py --cloud の本体。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --cloud --engine <雲の書き出しの撮影> --reference <Cycles の書き先>
#          [--views cloud_sky] [--trial] [--verify]
#       <雲の書き出しの撮影>: CaptureScene.ps1 -Hdr -Views cloud_sky -GameArgs cloud-dump の撮影。<Cycles の書き先>:
#       BlenderReference.py --cloud が書いたフォルダ。結果を <Cycles の書き先>/cloud_check.json と、見比べの画像
#       <Cycles の書き先>/cloud_<視点>.png（左 エンジンの HDR、右 Cycles の雲の散乱光 + 透過率の画 × 空だけの画。同じ露出。視線の画素に
#       領域の色の枠）に書く。比べる値は画面の決めた画素の視線（書き出しの視線）ごとの値で、エンジンは GPU が描画と同じ式で評価した
#       霞の前の散乱光と透過率、Cycles は視線の画素の窓（CloudVolume.WINDOW_RADIUS_PX）の平均。
#
# 比較が成り立つ条件（満たさなければ落ちる。エンジンと Cycles の値の一致は求めず、表に書くだけ）:
#   1. エンジンの書き出し: ReferenceCheck.py --cloud-dump の検査（CloudDumpCheck.check_view。格子・定数・視線の透過率）に合格する。
#   2. Cycles の参照: 記録の種類、書き出しのファイルのハッシュが今の撮影と同じ。カメラ（書き出しの逆の視線行列から求め直した
#      向き・縦の画角）・格子・視線の領域が記録と同じ。対称な透視の投影で書き出しの視線を PROJECTION_TOLERANCE_PX 画素以内で
#      表せる（Blender のカメラが同じ視線を描く）。出力の画の大きさが撮影と同じ。
#   3. 空: 雲を隠して描いた空だけの画の、視線の画素の値が、書き出しの空の LUT を CloudDumpCheck.sample_sky で引いた値と相対
#      SKY_TOLERANCE 以内（同じ空・同じ向き）。
#   4. 体積: 視線ごとに、窓の Cycles の透過率の画（同じ格子の吸収だけの体積を光線の歩で積んだ 1 − α）が、書き出しの式を 4 m ごとに
#      積んだ透過率（窓の画素の視線の平均）と TRANSMITTANCE_TOLERANCE 以内（同じ密度場・同じ置き方・同じ消散係数）。本描画の
#      1 − α は散乱した標本で透明が切れるので粒が粗く、記録だけにする。
#   5. Cycles の収束: 1 本あたり MIN_SPP_PER_SEED サンプル以上で、領域ごとに乱数の種を変えた 2 本の差が GrayRegions の基準
#      （平均の差・画素の差の RMS）に収まる。--trial はこの条件だけを外す。
#   6. 領域（CloudVolume.REGIONS）にそれぞれ CloudVolume.MIN_REGION_RAYS 本以上の視線があり、表の値と量が有限。未判定の領域・量が
#      1 つでもあれば落ちる。
#   --verify は、エンジンの撮影の入力（inputs.json）と、Cycles の参照の入力（台本・CloudVolume.py・CloudDumpCheck.py・
#   GrayRegions.py。inputs.json）を今のファイルと照らす。
#
# 表（視線ごと）: エンジンの雲の透過率（GPU）・Cycles の透過率・式を積んだ透過率、エンジンの散乱光と Cycles の放射輝度の輝度 Y と
#   比（エンジン ÷ Cycles）、太陽との角・太陽の透過率・領域。
# 表（領域ごと）: 両方の輝度 Y の平均・比・不透明度の平均・Y / 不透明度の平均。
# 量（描画ごと）: 縁の明るさ = 縁の Y / 不透明度 ÷ 厚い雲の Y / 不透明度、銀の縁 = 銀の縁の Y / 不透明度 ÷ 縁の Y / 不透明度、
#   影の側の暗さ = 影の側の Y ÷ 日の当たる側の Y（それぞれ領域の平均の比）。
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw

import CloudDumpCheck as CD
import CloudVolume as CV
import GrayRegions as GR

CLOUD_VIEWS = ("cloud_sky",)
REFERENCE_KIND = "cycles_cloud"
# 1 本あたりのサンプル数の下限（BlenderReference.MIN_SPP_PER_SEED と同じ。--gray の収束の基準）
MIN_SPP_PER_SEED = 512
# 空だけの画と LUT の相対差の上限（同じ texel を同じ線形の標本で引くので、差は画像の標本の精度だけ）
SKY_TOLERANCE = 0.01
# 透過率の差の上限（透過率の画は吸収だけの体積を光線の歩で積むので、標本の揺らぎはほとんど無い）: 16 m の格子で書き出しの視線の
# 透過率が式から最大 0.004 ずれる（CloudVolume の説明）。密度・消散係数が 5 % 違うと光学的深さ 1 前後の視線で 0.02 近く違う
TRANSMITTANCE_TOLERANCE = 0.01
# 対称な透視の投影の視線と書き出しの視線の差の上限[画素]（逆の視線行列の単精度の丸めで 0.2 画素ほどずれる）
PROJECTION_TOLERANCE_PX = 0.5
# 記録のカメラ・格子と求め直した値の差の上限（同じ計算なので丸めだけ）
RECORD_TOLERANCE = 1e-9
# 見比べの画像の縮小と、領域の枠の色
PREVIEW_SCALE = 0.5
REGION_COLORS = {"cloud_body": (255, 255, 255), "cloud_edge": (80, 200, 255), "silver_lining": (255, 210, 0),
                 "body_sunlit": (0, 220, 120), "body_shadow": (255, 60, 60)}


def read_json(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def check_reference_inputs(RC, reference_dir, engine_dir, verify):
    """Cycles の参照の入力の記録を読み、書き出しのハッシュを照らす（verify なら台本も）。(記録, 失敗の一覧)"""
    path = os.path.join(reference_dir, "inputs.json")
    if not os.path.isfile(path):
        return None, [f"{RC.rel_dir(reference_dir)}/inputs.json が無い（BlenderReference.py --cloud を回していない）"]
    record = read_json(path)
    failures = []
    if record.get("kind") != REFERENCE_KIND:
        failures.append(f"参照の記録の種類が {REFERENCE_KIND} でない（{record.get('kind')}）")
    for name, digest in (record.get("dump_sha256") or {}).items():
        current = os.path.join(engine_dir, name)
        if not os.path.isfile(current) or RC.sha256_file(current) != digest:
            failures.append(f"参照が読んだ書き出し {name} が今の撮影 {RC.rel_dir(engine_dir)} に無いか、違う")
    if not record.get("dump_sha256"):
        failures.append("参照の記録に書き出しのハッシュが無い")
    if not verify:
        return record, failures
    script = os.path.join(RC.REPO_ROOT, record.get("script", ""))
    if not os.path.isfile(script) or not RC.blender_script_matches(RC.sha256_file(script), record.get("script_sha256")):
        failures.append(f"Blender の台本 {record.get('script')} が描いた後に変わった（古い結果）")
    for key, rel in (("cloud_volume_sha256", "Scripts/Terrain/CloudVolume.py"),
                     ("cloud_dump_check_sha256", "Scripts/Terrain/CloudDumpCheck.py"),
                     ("gray_regions_sha256", "Scripts/Terrain/GrayRegions.py")):
        if RC.sha256_file(os.path.join(RC.REPO_ROOT, rel)) != record.get(key):
            failures.append(f"{rel} が描いた後に変わった（古い結果）")
    return record, failures


def max_difference(a, b):
    """入れ子の数の並びの差の絶対値の最大（形が違えば無限大）"""
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    if a.shape != b.shape:
        return float("inf")
    return float(np.max(np.abs(a - b))) if a.size else 0.0


def camera_grid_mismatch(frame, grid, recorded_frame, recorded_grid):
    """求め直したカメラと格子が記録と違う理由の一覧"""
    reasons = []
    for key in ("forward", "right", "up", "vertical_fov_degrees", "position_frame"):
        if max_difference(frame[key], recorded_frame.get(key, [])) > RECORD_TOLERANCE:
            reasons.append(f"カメラの {key} が記録と違う")
    for key in ("voxel_size_m", "origin_frame", "dims"):
        if max_difference(grid[key], recorded_grid.get(key, [])) > RECORD_TOLERANCE:
            reasons.append(f"格子の {key} が記録と違う")
    for axis in ("i", "j", "k"):
        if max_difference(grid["axes"][axis], (recorded_grid.get("axes") or {}).get(axis, [])) > RECORD_TOLERANCE:
            reasons.append(f"格子の軸 {axis} が記録と違う")
    return reasons


def window_mean(image, pixel):
    height, width = image.shape[:2]
    window = image[CV.window_pixels(pixel, width, height)]
    return window.reshape(-1, *image.shape[2:]).astype(np.float64).mean(axis=0)


def region_mean(rays, indices, key):
    values = [rays[i][key] for i in indices if rays[i].get(key) is not None]
    return float(np.mean(values)) if values else None


def finite(*values):
    return all(v is not None and math.isfinite(v) for v in values)


def ratio(a, b):
    return a / b if finite(a, b) and b != 0.0 else None


def srgb(linear):
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1.0 / 2.4) - 0.055)


def write_preview(path, engine_hdr, cycles_composite, rays, members):
    """左 エンジン、右 Cycles（同じ露出。中間の灰色をエンジンの輝度の中央値に合わせ、x / (1 + x) で丸める）"""
    exposure = 0.18 / max(float(np.median(GR.luminance(engine_hdr))), 1e-6)
    panels = []
    for image in (engine_hdr, cycles_composite):
        scaled = np.asarray(image, dtype=np.float64) * exposure
        panels.append((srgb(scaled / (1.0 + scaled)) * 255.0 + 0.5).astype(np.uint8))
    height, width = engine_hdr.shape[:2]
    canvas = Image.new("RGB", (width * 2, height))
    canvas.paste(Image.fromarray(panels[0]), (0, 0))
    canvas.paste(Image.fromarray(panels[1]), (width, 0))
    draw = ImageDraw.Draw(canvas)
    region_of = {}
    # 細かい領域（影の側・日の当たる側・銀の縁）の色を厚い雲・縁より優先する
    for name in reversed(CV.REGIONS):
        for index in members.get(name, []):
            region_of.setdefault(index, name)
    for ray in rays:
        name = region_of.get(ray["index"])
        color = REGION_COLORS.get(name, (128, 128, 128))
        x, y = ray["pixel"]
        for offset in (0, width):
            draw.rectangle([x + offset - 8, y - 8, x + offset + 8, y + 8], outline=color, width=2)
    canvas = canvas.resize((int(width * 2 * PREVIEW_SCALE), int(height * PREVIEW_SCALE)), Image.LANCZOS)
    canvas.save(path)


def check_view(RC, engine_dir, reference_dir, view, record, game_args, trial):
    failures = []
    result = {"view": view}
    # 1. エンジンの書き出し
    dump = CD.check_view(RC, engine_dir, view, game_args)
    result["dump_check"] = {"pass": dump["pass"], "worst": dump.get("worst"), "failures": dump["failures"]}
    if not dump["pass"]:
        failures.append("雲の書き出しの検査（--cloud-dump）が落ちた: " + "; ".join(dump["failures"][:3]))
    try:
        meta, model, probe = CV.load_dump(engine_dir, view)
    except ValueError as error:
        return {"view": view, "pass": False, "failures": failures + [f"雲の書き出しを読めない（{error}）"]}
    target = CV.target_size(meta)
    width, height = target
    pixels = meta["probe"]["pixels"]

    # 2. カメラ・格子・領域が記録と同じ
    view_record = ((record or {}).get("views") or {}).get(view)
    if view_record is None:
        return {"view": view, "pass": False, "failures": failures + [f"参照の記録に視点 {view} が無い"]}
    frame = CV.camera_frame(model, target)
    grid = CV.grid_spec(model, target, frame)
    failures.extend(camera_grid_mismatch(frame, grid, view_record.get("camera") or {}, view_record.get("grid") or {}))
    projection_px = frame["projection_error_rad"] / frame["pixel_rad"]
    result["projection_error_px"] = projection_px
    if not projection_px <= PROJECTION_TOLERANCE_PX:
        failures.append(f"対称な透視の投影で書き出しの視線を表せない（最大 {projection_px:.3f} 画素 / {PROJECTION_TOLERANCE_PX}）")
    rays = CV.ray_table(model, probe, pixels, target)
    members = CV.region_members(rays)
    recorded_members = {name: [int(i) for i in v] for name, v in (view_record.get("regions") or {}).items()}
    if members != recorded_members:
        failures.append("視線の領域の分け方が記録と違う")

    # Cycles の出力
    cycles_meta_path = os.path.join(reference_dir, f"{view}.cycles.json")
    images = {}
    for part in ("seed_a", "seed_b", "alpha_a", "alpha_b"):
        path = os.path.join(reference_dir, f"{view}.cycles.{part}.pfm")
        if os.path.isfile(path):
            images[part] = GR.read_pfm(path)
    for part in ("sky", "transmittance"):
        path = os.path.join(reference_dir, f"{view}.aux.{part}.pfm")
        if os.path.isfile(path):
            images[part] = GR.read_pfm(path)
    missing = [p for p in ("seed_a", "seed_b", "alpha_a", "alpha_b", "sky", "transmittance") if p not in images]
    if missing or not os.path.isfile(cycles_meta_path):
        return {"view": view, "pass": False,
                "failures": failures + [f"Cycles の出力が無い（{', '.join(missing) or f'{view}.cycles.json'}）"]}
    cycles_meta = read_json(cycles_meta_path)
    for part, image in images.items():
        if image.shape[:2] != (height, width):
            failures.append(f"Cycles の {part} の大きさ {image.shape[1]}×{image.shape[0]} が撮影 {width}×{height} と違う")
    if failures and any("大きさ" in f for f in failures):
        return {"view": view, "pass": False, "failures": failures}
    rgb_a, rgb_b = images["seed_a"], images["seed_b"]
    alpha_a, alpha_b = images["alpha_a"][..., 0], images["alpha_b"][..., 0]
    rgb = 0.5 * (rgb_a + rgb_b)
    alpha = 0.5 * (alpha_a + alpha_b)

    # 3〜4. 空と体積
    worst_sky = 0.0
    worst_t = 0.0
    for ray in rays:
        pixel = ray["pixel"]
        direction = np.asarray(ray["direction"])
        expected_sky = CD.sample_sky(model.sky_lut, direction)
        # 空だけの画は視線の画素そのもの（窓で均すと空の勾配が混ざる）
        cycles_sky = images["sky"][int(math.floor(pixel[1])), int(math.floor(pixel[0]))].astype(np.float64)
        sky_error = float(np.max(np.abs(cycles_sky - expected_sky) / np.maximum(np.abs(expected_sky), CD.RADIANCE_FLOOR)))
        worst_sky = max(worst_sky, sky_error)
        fine = float(np.mean([CV.fine_transmittance(model, model.ray_direction(center, target))
                              for center in CV.window_centers(pixel, width, height)]))
        cycles_t = float(window_mean(images["transmittance"][..., 0], pixel))
        render_t = 1.0 - float(window_mean(alpha, pixel))
        cycles_rgb = window_mean(rgb, pixel)
        engine_y = float(GR.luminance(np.asarray(ray["engine_scattered_rgb"])))
        cycles_y = float(GR.luminance(cycles_rgb))
        t_error = abs(cycles_t - fine)
        worst_t = max(worst_t, t_error)
        ray.update({"sky_relative_error": sky_error, "fine_transmittance": fine, "cycles_transmittance": cycles_t,
                    "transmittance_error": t_error, "cycles_render_transmittance": render_t, "engine_y": engine_y, "cycles_y": cycles_y,
                    "cycles_rgb": cycles_rgb.tolist(), "cycles_opacity": float(np.clip(1.0 - cycles_t, 0.0, 1.0)),
                    "ratio": ratio(engine_y, cycles_y)})
        ray["engine_y_per_opacity"] = engine_y / ray["opacity"] if ray["opacity"] >= CV.CLOUD_OPACITY else None
        ray["cycles_y_per_opacity"] = cycles_y / ray["cycles_opacity"] if ray["cycles_opacity"] >= CV.CLOUD_OPACITY else None
    result["sky_max_relative_error"] = worst_sky
    result["transmittance_max_error"] = worst_t
    if not worst_sky <= SKY_TOLERANCE:
        failures.append(f"空だけの画が書き出しの空の LUT と合わない（最大の相対差 {worst_sky:.4f} / {SKY_TOLERANCE}）")
    if not worst_t <= TRANSMITTANCE_TOLERANCE:
        failures.append(f"Cycles の雲の透過率が書き出しの式を積んだ透過率と合わない（最大の差 {worst_t:.4f} / "
                        f"{TRANSMITTANCE_TOLERANCE}）")

    # 5. 収束
    spp = int(cycles_meta.get("spp_per_seed", 0))
    stats, all_converged = CV.convergence(GR.luminance(rgb_a), GR.luminance(rgb_b), pixels, members)
    result["convergence"] = {"spp_per_seed": spp, "regions": stats, "all_converged": all_converged,
                             "stop_reason": cycles_meta.get("stop_reason"), "trial": bool(cycles_meta.get("trial"))}
    if not trial:
        if spp < MIN_SPP_PER_SEED:
            failures.append(f"Cycles のサンプル数が足りない（1 本 {spp} / {MIN_SPP_PER_SEED}）")
        unconverged = [name for name in CV.REGIONS if not stats[name]["converged"]]
        if unconverged:
            failures.append(f"Cycles が収束していない領域: {', '.join(unconverged)}")

    # 6. 領域の表と量
    table = {}
    for name in CV.REGIONS:
        indices = members[name]
        entry = {"label": CV.REGION_LABELS[name], "rays": len(indices)}
        if len(indices) < CV.MIN_REGION_RAYS:
            failures.append(f"領域 {name}（{CV.REGION_LABELS[name]}）の視線が {len(indices)} 本（下限 {CV.MIN_REGION_RAYS}）で未判定")
        else:
            entry.update({
                "engine_y": region_mean(rays, indices, "engine_y"), "cycles_y": region_mean(rays, indices, "cycles_y"),
                "engine_opacity": region_mean(rays, indices, "opacity"),
                "cycles_opacity": region_mean(rays, indices, "cycles_opacity"),
                "engine_y_per_opacity": region_mean(rays, indices, "engine_y_per_opacity"),
                "cycles_y_per_opacity": region_mean(rays, indices, "cycles_y_per_opacity"),
            })
            entry["ratio"] = ratio(entry["engine_y"], entry["cycles_y"])
            if not finite(entry["engine_y"], entry["cycles_y"], entry["engine_y_per_opacity"],
                          entry["cycles_y_per_opacity"], entry["ratio"]):
                failures.append(f"領域 {name}（{CV.REGION_LABELS[name]}）の値が求まらない（未判定）")
        table[name] = entry

    def quantity(numerator, denominator, key):
        return {side: ratio((table[numerator] or {}).get(f"{side}_{key}"), (table[denominator] or {}).get(f"{side}_{key}"))
                for side in ("engine", "cycles")}

    quantities = {
        "edge_brightness": quantity("cloud_edge", "cloud_body", "y_per_opacity"),
        "silver_lining": quantity("silver_lining", "cloud_edge", "y_per_opacity"),
        "shadow_darkness": quantity("body_shadow", "body_sunlit", "y"),
    }
    for name, values in quantities.items():
        if not finite(values["engine"], values["cycles"]):
            failures.append(f"量 {name} が求まらない（未判定）")
    result.update({"regions": table, "quantities": quantities, "rays": rays, "members": members,
                   "cycles": {"spp_per_seed": spp, "stop_reason": cycles_meta.get("stop_reason"),
                              "device": cycles_meta.get("device"), "settings": cycles_meta.get("settings"),
                              "grid_record": cycles_meta.get("grid_record"),
                              "render_seconds": cycles_meta.get("render_seconds"),
                              "load_seconds": cycles_meta.get("load_seconds")}})

    # 見比べの画像
    engine_hdr_path = os.path.join(engine_dir, f"{view}.hdr.pfm")
    if os.path.isfile(engine_hdr_path):
        engine_hdr = GR.read_pfm(engine_hdr_path)
        composite = rgb + images["transmittance"] * images["sky"]
        preview = os.path.join(reference_dir, f"cloud_{view}.png")
        write_preview(preview, engine_hdr, composite, rays, members)
        result["preview"] = RC.rel_dir(preview)
    result["pass"] = not failures
    result["failures"] = failures
    return result


def run(RC, engine_dirs, reference_dir, views, verify, trial):
    failures = []
    if len(engine_dirs) != 1:
        print("REFERENCE_CHECK result=fail reason=engine_missing（--engine に雲の書き出しの撮影を 1 つ指定する）")
        return 1
    engine_dir = engine_dirs[0]
    if not reference_dir or not os.path.isdir(reference_dir):
        print(f"REFERENCE_CHECK result=fail reason=reference_missing（--reference に BlenderReference.py --cloud の書き先を指定する）"
              f" path={reference_dir}")
        return 1
    inputs_ok, input_failures, engine_record = RC.check_inputs(engine_dir)
    if verify and not inputs_ok:
        failures.extend(f"エンジンの撮影: {f}" for f in input_failures)
    game_args = RC.plate_capture_args(engine_dir)
    metric_views = RC.views_from_metrics(engine_dir)
    if game_args is None or metric_views is None:
        failures.append("エンジンの撮影に metrics.json が無い（CaptureScene.ps1 の出力先でない）")
        metric_views = []
    record, record_failures = check_reference_inputs(RC, reference_dir, engine_dir, verify)
    failures.extend(f"Cycles の参照: {f}" for f in record_failures)
    targets = views or list(CLOUD_VIEWS)
    results = []
    for view in targets:
        if view not in metric_views:
            failures.append(f"{view}: エンジンの撮影の metrics.json に無い視点")
            continue
        result = check_view(RC, engine_dir, reference_dir, view, record, game_args, trial)
        results.append(result)
        failures.extend(f"{view}: {f}" for f in result["failures"])
        conv = result.get("convergence", {})
        print(f"REFERENCE_CHECK cloud view={view} pass={int(result['pass'])} "
              f"dump_pass={int(bool((result.get('dump_check') or {}).get('pass')))} "
              f"projection_px={result.get('projection_error_px')} sky_max_rel={result.get('sky_max_relative_error')} "
              f"transmittance_max_err={result.get('transmittance_max_error')} spp_per_seed={conv.get('spp_per_seed')} "
              f"all_converged={int(bool(conv.get('all_converged')))} trial={int(bool(trial))}")
        for name, entry in (result.get("regions") or {}).items():
            print(f"REFERENCE_CHECK cloud view={view} region={name} rays={entry['rays']} engine_y={entry.get('engine_y')} "
                  f"cycles_y={entry.get('cycles_y')} ratio={entry.get('ratio')} engine_opacity={entry.get('engine_opacity')} "
                  f"cycles_opacity={entry.get('cycles_opacity')} engine_y_per_opacity={entry.get('engine_y_per_opacity')} "
                  f"cycles_y_per_opacity={entry.get('cycles_y_per_opacity')} "
                  f"converged={int(bool(conv.get('regions', {}).get(name, {}).get('converged')))}")
        for name, values in (result.get("quantities") or {}).items():
            print(f"REFERENCE_CHECK cloud view={view} quantity={name} engine={values['engine']} cycles={values['cycles']}")
    report = {
        "mode": "cloud",
        "verify": bool(verify),
        "trial": bool(trial),
        "engine_dir": RC.rel_dir(engine_dir),
        "reference_dir": RC.rel_dir(reference_dir),
        "inputs_current": inputs_ok,
        "engine_inputs": engine_record,
        "reference_inputs": {k: (record or {}).get(k) for k in ("kind", "script_sha256", "cloud_volume_sha256",
                                                                "cloud_dump_check_sha256", "dump_sha256", "git_commit",
                                                                "trial")},
        "criteria": dict(CV.criteria(), min_spp_per_seed=MIN_SPP_PER_SEED, sky_tolerance=SKY_TOLERANCE,
                         transmittance_tolerance=TRANSMITTANCE_TOLERANCE, projection_tolerance_px=PROJECTION_TOLERANCE_PX),
        "views": results,
        "failures": failures,
    }
    report_path = os.path.join(reference_dir, "cloud_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False, default=float)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=cloud views={len(targets)} trial={int(bool(trial))} "
          f"inputs_current={int(bool(inputs_ok))} report={report_path}")
    return 0 if not failures else 1
