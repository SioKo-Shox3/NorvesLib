# 手本の視点で遠い雲が消える原因を、反実仮想で量に分ける（SCN-WL-CLOUD-FAR-CAUSE）。ReferenceCheck.py --cloud-far の本体。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --cloud-far --engine <撮影先> [--views ref_far_01,ref_far_02] [--verify]
#       CaptureScene.ps1 -GameArgs cloud-dump の撮影（<視点>.cloud.json と書き出しのファイル、<視点>.png）を視点ごとに読み、
#       結果を <撮影先>/cloud_far_check.json に書く。--verify は入力のハッシュ（inputs.json）を今のファイルと照らす。
#
# 同じ密度場・時刻・視線（画素と歩の端数）のまま、雲の式（Common/CloudLayerCommon.glsl）の診断の切り替えを段ごとに足す:
#   段 0 = 今の描画、段 1 = 45 km の打ち切りを外す、段 2 = さらに 30〜45 km の距離の溶かしを外す、
#   段 3 = さらに独自の 22 km の霞（kCloudHazeDistance）を外す。
# GPU の書き出しは格子の視線（横 16 × 縦 9）の後ろに、上の 7 行の視線を段 1・2・3 で評価した反実仮想の列を持つ。
# 写真と同じ画素の範囲（写真の地形の輪郭より上の空で、撮影でも空の画素）は、書き出しの格子と定数だけからエンジンの式の写し
# （CloudDumpCheck.CloudModel）で段ごとに求める。写しが GPU の反実仮想の列と合うことを合否で確かめる。
#
# 量（写真と同じ画素の範囲の視線の平均・割合。画素は撮影の 1920 × 1080 の画素の中心）:
#   - 地平線までの雲の密度の積分: 雲の層の区間の全部（打ち切りなし、透過率で止めない）の光学的深さ τ。1 − exp(−τ) が
#     CLOUD_OPACITY 以上の視線の割合を「その方向に雲がある」割合とする
#   - 段ごとの霞の前の雲の透過率（と不透明度が CLOUD_OPACITY 以上の割合）・雲の散乱光の Y（霞の前）・雲の放射輝度の Y（霞の後）
#   - 段ごとの最終の画素の雲のコントラスト: 合成の後の画素（雲の放射輝度 + 雲の後ろの空 × (1 − 不透明度 × 溶かす割合)）の Y を
#     雲の後ろの空の Y で割って 1 を引いた値。VISIBLE_CONTRAST 以上の視線の割合を「雲が見える」割合とする
#   - 段ごとの最終の画素の Y の 5 %・95 % 点 ÷ 中央値（空の明るさのばらつき。CloudPhotoStats.py の写真の値と同じ量）と、
#     切れ目（不透明度 0.5 未満）の割合。撮影の HDR（<視点>.hdr.pfm）の同じ画素の値を併記し、段 0 の写しが画と合うかを見る
#   - 原因の寄与（併存を認める）: (i) 中身 = 1 − 雲のある割合、(ii) 打ち切り = 見える割合の段 0 → 1 の増え、
#     (iii) 距離の溶かし = 段 1 → 2 の増え、(iv) 独自の霞 = 段 2 → 3 の増え、残り = 雲があるのに段 3 でも見えない割合
#     （雲の明るさが後ろの空に近い）。段の順に依らない見方として、打ち切りのまま霞だけ・溶かしと霞を外した値も書く
#   - 診断の併記: CloudPhotoStats.py の色の閾値（線形の R ÷ B ≥ 0.7）の被覆を、写真と撮影の PNG で求めた値
# 合否（--verify）:
#   1. 書き出しがあり、撮影の起動引数に --cloud-dump がある。反実仮想の列が段 1・2・3 のどれも 1 本以上ある
#   2. 格子の視線と反実仮想の列の全部で、写しの雲の透過率が GPU と CloudDumpCheck.TRANSMITTANCE_TOLERANCE 以内、歩数が同じ
#   3. 写真と同じ画素の範囲に MIN_RANGE_RAYS 本以上の視線がある
#   4. 入力のハッシュが今のファイルと合う（--verify のとき）
# 量の値そのもの（被覆・寄与）は合否にしない（写真は撮影時の処理を含む）。
import json
import os

import numpy as np
from PIL import Image

import CloudDumpCheck as CD
from CloudPhotoStats import measure as photo_measure
from CompareReference import PANEL_SIZE, PHOTO_DIR, PHOTOS, find_terrain_top, fit_photo

DEFAULT_VIEWS = ("ref_far_01", "ref_far_02")
# 段の名前と診断の切り替え（GLSL の kCloudDiagnostic* の和）
STAGES = (
    ("current", 0),
    ("no_march_limit", CD.DIAGNOSTIC_NO_MARCH_LIMIT),
    ("no_fade", CD.DIAGNOSTIC_NO_MARCH_LIMIT | CD.DIAGNOSTIC_NO_FADE),
    ("no_haze", CD.DIAGNOSTIC_NO_MARCH_LIMIT | CD.DIAGNOSTIC_NO_FADE | CD.DIAGNOSTIC_NO_HAZE),
)
# 段の順に依らない見方（打ち切りのまま霞だけを外す・打ち切りのまま溶かしと霞を外す）
SINGLE_STAGES = (
    ("haze_only_removed", CD.DIAGNOSTIC_NO_HAZE),
    ("fade_and_haze_removed", CD.DIAGNOSTIC_NO_FADE | CD.DIAGNOSTIC_NO_HAZE),
)
CLOUD_OPACITY = 0.05
VISIBLE_CONTRAST = 0.10
# 記録に併せて書く薄いコントラストの閾値
FAINT_CONTRAST = 0.02
# 写真の空の画素を間引く間隔（写真の絵の画素。1280 × 720 の絵で 10 画素 = 撮影の 15 画素）
RANGE_STRIDE = 10
MIN_RANGE_RAYS = 100
LUMA = np.array([0.2126, 0.7152, 0.0722])


def luminance(rgb):
    return float(np.asarray(rgb, dtype=np.float64) @ LUMA)


def evaluate(model, march, direction, flags):
    """1 本の視線の段の値。march は同じ視線を flags の打ち切りの扱いで進めた結果"""
    sky = CD.sample_sky(model.sky_lut, direction)
    sky_y = luminance(sky)
    if not march["marched"]:
        return {"transmittance": 1.0, "opacity": 0.0, "scattered_y": 0.0, "radiance_y": 0.0, "contrast": 0.0,
                "fade": 1.0, "haze": 1.0, "red_blue": float(sky[0] / max(sky[2], 1e-6)), "sky_y": sky_y,
                "final_y": sky_y}
    opacity = float(np.clip(1.0 - march["transmittance"], 0.0, 1.0))
    haze = 1.0 if flags & CD.DIAGNOSTIC_NO_HAZE else float(np.exp(-march["cloud_distance"] / CD.CLOUD_HAZE_DISTANCE))
    fade = 1.0 if flags & CD.DIAGNOSTIC_NO_FADE else \
        1.0 - float(CD.smoothstep(CD.FADE_START_DISTANCE, CD.MAX_MARCH_DISTANCE, march["start_distance"]))
    radiance = (march["scattered"] * haze + sky * opacity * (1.0 - haze)) * fade
    final = radiance + sky * (1.0 - opacity * fade)
    final_y = luminance(final)
    return {"transmittance": float(march["transmittance"]), "opacity": opacity,
            "scattered_y": luminance(march["scattered"]), "radiance_y": luminance(radiance),
            "contrast": final_y / max(sky_y, 1e-6) - 1.0, "fade": fade, "haze": haze,
            "red_blue": float(final[0] / max(final[2], 1e-6)), "sky_y": sky_y, "final_y": final_y}


def read_hdr_luminance(capture_dir, view, pixels):
    """撮影の HDR（pfm、下から上の行、cd/m²）の画素の Y。無ければ None"""
    path = os.path.join(capture_dir, f"{view}.hdr.pfm")
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as f:
        header = f.readline().strip()
        width, height = (int(v) for v in f.readline().split())
        scale = float(f.readline())
        data = np.fromfile(f, dtype="<f4" if scale < 0 else ">f4")
    if header != b"PF" or data.size != width * height * 3:
        return None
    image = data.reshape(height, width, 3)[::-1]
    values = [float(image[int(y), int(x)] @ LUMA) for x, y in pixels]
    return np.array(values)


def spread(values):
    """Y の 5 %・95 % 点 ÷ 中央値"""
    values = np.asarray(values, dtype=np.float64)
    median = max(float(np.median(values)), 1e-6)
    return {"median": float(np.median(values)), "p5_over_median": float(np.percentile(values, 5) / median),
            "p95_over_median": float(np.percentile(values, 95) / median)}


def range_pixels(view, capture_dir, target):
    """写真と同じ画素の範囲（写真の空で、撮影でも空の画素）を間引いた撮影の画素の中心と、範囲の数え"""
    entry = next((p for p in PHOTOS if p["view"] == view), None)
    if entry is None:
        return None, {"reason": f"{view} に対応する手本の写真が CompareReference.PHOTOS に無い"}
    photo_path = os.path.join(PHOTO_DIR, entry["id"] + ".jpg")
    capture_path = os.path.join(capture_dir, f"{view}.png")
    for path in (photo_path, capture_path):
        if not os.path.isfile(path):
            return None, {"reason": f"ファイルが無い {path}"}
    photo_panel, _, _ = fit_photo(Image.open(photo_path).convert("RGB"))
    capture_panel = Image.open(capture_path).convert("RGB").resize(PANEL_SIZE, Image.LANCZOS)
    photo_stats, photo_sky, _ = photo_measure(photo_panel)
    capture_stats, _, _ = photo_measure(capture_panel)
    capture_top = find_terrain_top(np.asarray(capture_panel))
    rows = np.arange(PANEL_SIZE[1])[:, None]
    capture_sky = rows < (capture_top[None, :] - 2)
    both = photo_sky & capture_sky
    ys, xs = np.where(both)
    keep = (ys % RANGE_STRIDE == RANGE_STRIDE // 2) & (xs % RANGE_STRIDE == RANGE_STRIDE // 2)
    scale_x = target[0] / PANEL_SIZE[0]
    scale_y = target[1] / PANEL_SIZE[1]
    pixels = [(np.floor((x + 0.5) * scale_x) + 0.5, np.floor((y + 0.5) * scale_y) + 0.5)
              for y, x in zip(ys[keep], xs[keep])]
    info = {
        "photo": entry["id"],
        "photo_sky_pixels": int(photo_sky.sum()),
        "range_pixels": int(both.sum()),
        "photo_sky_not_capture_sky": int((photo_sky & ~capture_sky).sum()),
        "range_rows_panel": [int(ys.min()), int(ys.max())] if ys.size else None,
        "stride_panel": RANGE_STRIDE,
        "color_threshold_coverage": {
            "photo": photo_stats["coverage"] if photo_stats else None,
            "photo_bands": {k: v["coverage"] for k, v in photo_stats["coverage_bands"].items()} if photo_stats else None,
            "photo_y_p5_p95_over_median": [photo_stats["luminance_percentiles_over_median"][k] for k in ("5", "95")]
            if photo_stats else None,
            "capture": capture_stats["coverage"] if capture_stats else None,
            "capture_bands": {k: v["coverage"] for k, v in capture_stats["coverage_bands"].items()} if capture_stats else None,
        },
    }
    return pixels, info


def check_probe(model, probe, pixels, target, failures):
    """格子の視線と反実仮想の列を写しで求め直し、GPU と照らす"""
    flags_all = CD.probe_diagnostic_flags(probe)
    stats = {}
    for flags in sorted(set(int(f) for f in flags_all)):
        columns = np.where(flags_all == flags)[0]
        worst = 0.0
        step_mismatch = 0
        marched_mismatch = 0
        marched = 0
        cloudy = 0
        radiance_rel = 0.0
        for index in columns:
            pixel = pixels[index]
            direction = model.ray_direction(pixel, target)
            march = model.march(direction, model.jitter(pixel), flags=flags)
            gpu_t = float(probe[0, index, 0])
            gpu_marched = bool(int(round(probe[7, index, 3])) & 1)
            if gpu_marched != march["marched"]:
                marched_mismatch += 1
                continue
            worst = max(worst, abs(float(march["transmittance"]) - gpu_t))
            if march["marched"]:
                marched += 1
                if march["step_count"] != int(round(probe[7, index, 1])):
                    step_mismatch += 1
                if 1.0 - gpu_t >= CLOUD_OPACITY:
                    cloudy += 1
                    radiance_rel = max(radiance_rel, CD.relative_error(march["radiance"], probe[2, index, :3],
                                                                       CD.RADIANCE_FLOOR))
        stats[str(flags)] = {"rays": int(len(columns)), "marched": marched, "cloudy": cloudy,
                             "transmittance_max_abs": worst, "step_mismatch": step_mismatch,
                             "marched_mismatch": marched_mismatch, "radiance_max_rel": radiance_rel}
        label = "格子の視線" if flags == 0 else f"反実仮想の列（切り替え {flags}）"
        if marched_mismatch:
            failures.append(f"{label}: 雲の層を進めたかが GPU と違う視線が {marched_mismatch} 本")
        if step_mismatch:
            failures.append(f"{label}: 歩数が GPU と違う視線が {step_mismatch} 本")
        if not worst <= CD.TRANSMITTANCE_TOLERANCE:
            failures.append(f"{label}: 写しの雲の透過率が GPU と {CD.TRANSMITTANCE_TOLERANCE} を超えて違う（最大の差 {worst:.4f}）")
    for _, flags in STAGES[1:]:
        if str(flags) not in stats:
            failures.append(f"反実仮想の列（切り替え {flags}）が書き出しに無い")
    return stats


def summarize(values):
    """段の値の一覧の平均と割合"""
    if not values:
        return {}
    opacity = np.array([v["opacity"] for v in values])
    contrast = np.array([v["contrast"] for v in values])
    cloudy = opacity >= CLOUD_OPACITY
    return {
        "transmittance_mean": float(np.mean([v["transmittance"] for v in values])),
        "cloud_fraction": float(np.mean(cloudy)),
        "scattered_y_mean_cloudy": float(np.mean([v["scattered_y"] for v, c in zip(values, cloudy) if c])) if cloudy.any() else None,
        "radiance_y_mean_cloudy": float(np.mean([v["radiance_y"] for v, c in zip(values, cloudy) if c])) if cloudy.any() else None,
        "sky_y_mean": float(np.mean([v["sky_y"] for v in values])),
        "contrast_mean": float(np.mean(contrast)),
        "contrast_p95": float(np.percentile(contrast, 95)),
        "visible_fraction": float(np.mean(contrast >= VISIBLE_CONTRAST)),
        "faint_fraction": float(np.mean(contrast >= FAINT_CONTRAST)),
        "fade_mean": float(np.mean([v["fade"] for v in values])),
        "haze_mean_cloudy": float(np.mean([v["haze"] for v, c in zip(values, cloudy) if c])) if cloudy.any() else None,
        "red_blue_cloud_fraction": float(np.mean([v["red_blue"] >= 0.7 for v in values])),
        "gap_fraction": float(np.mean(opacity < 0.5)),
        "final_y": spread([v["final_y"] for v in values]),
    }


def measure_view(rc, capture_dir, view, game_args):
    failures = []
    result = {"view": view}
    if CD.GAME_ARG not in (game_args or []):
        failures.append(f"撮影の起動引数に {CD.GAME_ARG} が無い")
    meta_path = os.path.join(capture_dir, f"{view}.cloud.json")
    if not os.path.isfile(meta_path):
        result.update({"pass": False, "failures": [f"{view}.cloud.json が無い"] + failures})
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    field = meta["density_field"]
    shape = CD.read_array(capture_dir, field["shape_noise"], np.uint8, failures, "基本の形の 3D ノイズ")
    detail = CD.read_array(capture_dir, field["detail_noise"], np.uint8, failures, "細部の 3D ノイズ")
    probe = CD.read_array(capture_dir, meta["probe"], "<f4", failures, "視線の値")
    lut = CD.read_array(capture_dir, meta["sky_radiance_lut"], "<f4", failures, "空の放射輝度 LUT")
    if failures:
        result.update({"pass": False, "failures": failures})
        return result
    model = CD.CloudModel(meta, shape.astype(np.float64) / 255.0, detail.astype(np.float64) / 255.0,
                          lut.astype(np.float64))
    target = (int(meta["target"]["width"]), int(meta["target"]["height"]))
    result["frame_number"] = meta.get("frame_number")
    result["time_seconds"] = meta.get("time_seconds")
    result["probe"] = check_probe(model, probe, meta["probe"]["pixels"], target, failures)

    pixels, range_info = range_pixels(view, capture_dir, target)
    result["range"] = range_info
    if pixels is None:
        failures.append(f"写真と同じ画素の範囲を決められない（{range_info['reason']}）")
        result.update({"pass": False, "failures": failures})
        return result
    if len(pixels) < MIN_RANGE_RAYS:
        failures.append(f"写真と同じ画素の範囲の視線が {len(pixels)} 本で {MIN_RANGE_RAYS} 本に足りない")

    stage_values = {name: [] for name, _ in STAGES + SINGLE_STAGES}
    full_depth = []
    start_distance = []
    for pixel in pixels:
        direction = model.ray_direction(pixel, target)
        jitter = model.jitter(pixel)
        limited = model.march(direction, jitter)
        unlimited = model.march(direction, jitter, flags=CD.DIAGNOSTIC_NO_MARCH_LIMIT)
        full_depth.append(unlimited.get("optical_depth_full", 0.0))
        start_distance.append(unlimited.get("start_distance", float("nan")))
        for name, flags in STAGES + SINGLE_STAGES:
            march = unlimited if flags & CD.DIAGNOSTIC_NO_MARCH_LIMIT else limited
            stage_values[name].append(evaluate(model, march, direction, flags))
    full_depth = np.array(full_depth)
    start_distance = np.array(start_distance)
    content_cloud = 1.0 - np.exp(-full_depth) >= CLOUD_OPACITY
    stages = {name: summarize(values) for name, values in stage_values.items()}
    hdr = read_hdr_luminance(capture_dir, view, pixels)
    if hdr is not None:
        result["hdr_final_y"] = spread(hdr)
        result["hdr_over_model_median"] = result["hdr_final_y"]["median"] / max(stages["current"]["final_y"]["median"], 1e-6)
    visible = [stages[name]["visible_fraction"] for name, _ in STAGES]
    cloud_fraction = float(np.mean(content_cloud))
    result.update({
        "rays": len(pixels),
        "content": {
            "cloud_fraction_to_horizon": cloud_fraction,
            "optical_depth_to_horizon_median": float(np.median(full_depth)),
            "optical_depth_to_horizon_p90": float(np.percentile(full_depth, 90)),
            "start_distance_km_median": float(np.nanmedian(start_distance) / 1000.0),
            "start_distance_km_p10_p90": [float(np.nanpercentile(start_distance, 10) / 1000.0),
                                          float(np.nanpercentile(start_distance, 90) / 1000.0)],
            "beyond_march_limit_fraction": float(np.mean(start_distance > CD.MAX_MARCH_DISTANCE)),
            "within_fade_fraction": float(np.mean((start_distance > CD.FADE_START_DISTANCE) &
                                                  (start_distance <= CD.MAX_MARCH_DISTANCE))),
        },
        "stages": stages,
        "attribution": {
            "content_no_cloud": 1.0 - cloud_fraction,
            "march_limit": visible[1] - visible[0],
            "distance_fade": visible[2] - visible[1],
            "custom_haze": visible[3] - visible[2],
            "visible_current": visible[0],
            "visible_all_removed": visible[3],
            "cloud_but_invisible_after_all": cloud_fraction - visible[3],
        },
    })
    result.update({"pass": not failures, "failures": failures})
    return result


def run(rc, capture_dir, views, verify):
    failures = []
    inputs_ok, input_failures, record = rc.check_inputs(capture_dir)
    if verify and not inputs_ok:
        failures.extend(input_failures)
    metric_views = rc.views_from_metrics(capture_dir)
    game_args = rc.plate_capture_args(capture_dir)
    if metric_views is None or game_args is None:
        failures.append("metrics.json が無い（CaptureScene.ps1 の出力先でない）")
        metric_views = []
    targets = views if views else list(DEFAULT_VIEWS)
    results = []
    for view in targets:
        if view not in metric_views:
            failures.append(f"{view}: metrics.json に無い視点")
            continue
        result = measure_view(rc, capture_dir, view, game_args)
        results.append(result)
        failures.extend(f"{view}: {failure}" for failure in result["failures"])
        for flags, stats in result.get("probe", {}).items():
            print(f"REFERENCE_CHECK cloud_far view={view} probe_flags={flags} rays={stats['rays']} "
                  f"marched={stats['marched']} cloudy={stats['cloudy']} "
                  f"transmittance_max_abs={stats['transmittance_max_abs']:.4f} step_mismatch={stats['step_mismatch']} "
                  f"radiance_max_rel={stats['radiance_max_rel']:.4f}")
        if "content" not in result:
            continue
        content = result["content"]
        coverage = result["range"]["color_threshold_coverage"]
        print(f"REFERENCE_CHECK cloud_far view={view} rays={result['rays']} "
              f"range_pixels={result['range']['range_pixels']} "
              f"cloud_to_horizon={content['cloud_fraction_to_horizon']:.3f} "
              f"tau_median={content['optical_depth_to_horizon_median']:.3f} "
              f"start_km_median={content['start_distance_km_median']:.1f} "
              f"beyond_45km={content['beyond_march_limit_fraction']:.3f} "
              f"photo_rb_coverage={coverage['photo']:.3f} capture_rb_coverage={coverage['capture']:.3f}")
        for name, stage in result["stages"].items():
            print(f"REFERENCE_CHECK cloud_far view={view} stage={name} "
                  f"transmittance_mean={stage['transmittance_mean']:.3f} cloud_fraction={stage['cloud_fraction']:.3f} "
                  f"scattered_y={stage['scattered_y_mean_cloudy']} radiance_y={stage['radiance_y_mean_cloudy']} "
                  f"sky_y={stage['sky_y_mean']:.0f} contrast_mean={stage['contrast_mean']:.4f} "
                  f"contrast_p95={stage['contrast_p95']:.4f} visible={stage['visible_fraction']:.3f} "
                  f"faint={stage['faint_fraction']:.3f} rb_cloud={stage['red_blue_cloud_fraction']:.3f} "
                  f"gap={stage['gap_fraction']:.3f} y_p5={stage['final_y']['p5_over_median']:.3f} "
                  f"y_p95={stage['final_y']['p95_over_median']:.3f}")
        if "hdr_final_y" in result:
            hdr = result["hdr_final_y"]
            photo_spread = coverage.get("photo_y_p5_p95_over_median") or [float("nan")] * 2
            print(f"REFERENCE_CHECK cloud_far view={view} hdr_y_p5={hdr['p5_over_median']:.3f} "
                  f"hdr_y_p95={hdr['p95_over_median']:.3f} hdr_over_model_median={result['hdr_over_model_median']:.3f} "
                  f"photo_y_p5={photo_spread[0]:.3f} photo_y_p95={photo_spread[1]:.3f}")
        attribution = result["attribution"]
        print(f"REFERENCE_CHECK cloud_far view={view} attribution " +
              " ".join(f"{key}={value:.3f}" for key, value in attribution.items()))
    report = {
        "mode": "cloud_far",
        "verify": bool(verify),
        "inputs_current": inputs_ok,
        "inputs_record": record,
        "game_args": game_args,
        "criteria": {
            "cloud_opacity": CLOUD_OPACITY, "visible_contrast": VISIBLE_CONTRAST, "faint_contrast": FAINT_CONTRAST,
            "range_stride_panel": RANGE_STRIDE, "min_range_rays": MIN_RANGE_RAYS,
            "transmittance_tolerance": CD.TRANSMITTANCE_TOLERANCE,
            "stages": {name: flags for name, flags in STAGES + SINGLE_STAGES},
        },
        "views": results,
        "failures": failures,
    }
    report_path = os.path.join(capture_dir, "cloud_far_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False, default=float)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=cloud_far views={len(targets)} inputs_current={int(bool(inputs_ok))} "
          f"report={report_path}")
    return 0 if not failures else 1
