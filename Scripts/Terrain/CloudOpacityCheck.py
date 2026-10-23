# 雲だけの不透明度の画像（SCN-WL-CLOUD-OPACITY-AOV）の検査。ReferenceCheck.py --cloud-opacity の本体。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --cloud-opacity --engine <撮影先> [--views cloud_sky,ref_far_01] [--verify]
#       CaptureScene.ps1 -GameArgs wasteland-cloud-opacity,cloud-dump の撮影（<視点>.cloud.opacity.json・.f32 と、同じフレームの
#       雲の書き出し <視点>.cloud.json）を視点ごとに検査し、結果を <撮影先>/cloud_opacity_check.json に、見える形の画像を
#       <撮影先>/cloud_opacity_<視点>.png に書く。--verify は入力のハッシュ（inputs.json）を今のファイルと照らす。
#
# 画像の契約（Library/Core/Private/Rendering/FrameCaptureHdrWriter.cpp の SaveCapturedCloudOpacity、Assets/Shaders/cloud_layer_opacity.frag）:
#   [行, 列, RGBA] の 32 bit 浮動小数。行 0 が画面の上。r = 雲だけの不透明度（1 − 透過率に溶かす割合を掛けた後。cloud_layer.frag が
#   重ねるアルファ）、g = 1 − 透過率（掛ける前）、b = 地形のマスク（深度が空でない画素は 1）、a = 溶かす割合。
# 合否:
#   1. 形式・ファイルの形が JSON と合い、撮影の起動引数に --wasteland-cloud-opacity と --cloud-dump があり、Game のログに
#      不透明度の画像のパスを足した印と保存の印がある。雲の書き出しと同じフレームの番号・時刻・画面の大きさ・逆の射影の行列。
#   2. 値がすべて有限で、r・g・a が 0〜1、地形のマスクが 0 か 1。
#   3. 地形のマスクの外だけに値がある（マスクが 1 の画素の r・g が 0）。
#   4. 雲の書き出しの視線（診断の切り替えの無い格子の視線）ごとに、書き出しの視線の向きを記録の逆の射影の行列から求めた投影で
#      画素へ戻し（書き出しの画素の中心との差が POSITION_TOLERANCE 画素以内）、その画素の r と視線の重ねる割合（行 0 の y）、
#      空の画素では g と 1 − 透過率（行 0 の x）の差が OPACITY_TOLERANCE（0.01）以内。地形のマスクと視線の深度（行 6 の w）の
#      空かどうかが合う。
#   5. 全視点で雲のある視線（不透明度 CLOUD_OPACITY 以上）が MIN_CLOUD_RAYS 本以上ある（検査が成り立つ）。
# 記録（合否に使わない）: 反実仮想の列（診断の切り替えを立てた視線）の差、空の画素の不透明度の平均・分位、雲の画素の割合。
import json
import os

import numpy as np
from PIL import Image

GAME_ARGS = ("--wasteland-cloud-opacity", "--cloud-dump")
LOG_LINES = ("CLOUD_OPACITY ",)
FORMAT = "norves_cloud_opacity_v1"
DUMP_FORMAT = "norves_cloud_dump_v1"
PROBE_ROWS = 8
# 画像の値と書き出しの視線の値の差の上限
OPACITY_TOLERANCE = 0.01
# 投影で戻した画素と書き出しの画素の中心の差の上限（画素）。書き出しの視線の向きは GPU が単精度の逆の射影から求めた値で、
# 倍精度で求め直した向きと画角に関係なく 2.5〜2.8e-4 rad 違う（SCN-WL-CLOUD-OPACITY-AOV の撮影。cloud_sky の横の画角 118° で
# 0.26 画素、ref_far_01 の 22.8° で 1.2 画素）。向きや上下の取り違えは数百画素ずれるので、1.5 画素で投影の契約を確かめる
POSITION_TOLERANCE = 1.5
# 値の範囲の丸めの許し
RANGE_EPSILON = 1e-6
# 検査が成り立つ雲の視線の数（全視点の合計）
CLOUD_OPACITY = 0.05
MIN_CLOUD_RAYS = 10
# 視線の深度が空の画素かの閾値（cloud_layer.frag と同じ）
SKY_DEPTH = 0.999999


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def project_direction(view_projection, camera, direction, target):
    """視線の向きを、記録のカメラの射影で画素の座標（gl_FragCoord と同じ、左上が原点）へ戻す"""
    point = np.append(camera + direction * 1000.0, 1.0)
    clip = view_projection @ point
    if not np.isfinite(clip[3]) or abs(clip[3]) < 1e-12:
        return None
    ndc = clip[:2] / clip[3]
    uv = (ndc + 1.0) * 0.5
    return uv * np.asarray(target, dtype=np.float64)


def write_preview(path, image, rays):
    """不透明度を灰（空）、地形のマスクを茶、視線の位置を合否の色の十字で描く"""
    opacity = np.clip(image[:, :, 0], 0.0, 1.0)
    terrain = image[:, :, 2] > 0.5
    gray = (opacity * 255.0 + 0.5).astype(np.uint8)
    rgb = np.stack([gray, gray, gray], axis=-1)
    rgb[terrain] = (96, 64, 40)
    height, width = opacity.shape
    for ray in rays:
        if ray.get("projected_pixel") is None:
            continue
        x, y = (int(v) for v in np.floor(ray["projected_pixel"]))
        color = (40, 220, 60) if ray["pass"] else (240, 30, 30)
        for d in range(-6, 7):
            if 0 <= y < height and 0 <= x + d < width:
                rgb[y, x + d] = color
            if 0 <= y + d < height and 0 <= x < width:
                rgb[y + d, x] = color
    Image.fromarray(rgb).save(path)


def check_view(rc, capture_dir, view, game_args):
    failures = []
    result = {"view": view}
    meta_path = os.path.join(capture_dir, f"{view}.cloud.opacity.json")
    dump_path = os.path.join(capture_dir, f"{view}.cloud.json")
    for path in (meta_path, dump_path):
        if not os.path.isfile(path):
            return {"view": view, "pass": False, "failures": [f"{os.path.basename(path)} が無い"]}
    meta = load_json(meta_path)
    dump = load_json(dump_path)
    if meta.get("format") != FORMAT:
        failures.append(f"形式が違う（{meta.get('format')}）")
    if dump.get("format") != DUMP_FORMAT:
        failures.append(f"雲の書き出しの形式が違う（{dump.get('format')}）")
    for arg in GAME_ARGS:
        if arg not in (game_args or []):
            failures.append(f"撮影の起動引数に {arg} が無い")
    log_name = rc.view_game_log(capture_dir, view)
    log_text = ""
    if log_name and os.path.isfile(os.path.join(capture_dir, log_name)):
        with open(os.path.join(capture_dir, log_name), encoding="utf-8", errors="replace") as f:
            log_text = f.read()
    for line in LOG_LINES + (f"CAPTURE_VIEWS view={view} cloud opacity saved ",):
        if line not in log_text:
            failures.append(f"Game のログに「{line.strip()}」が無い")

    # 1. 同じフレーム・同じカメラ
    target = (int(meta["target"]["width"]), int(meta["target"]["height"]))
    dump_target = (int(dump["target"]["width"]), int(dump["target"]["height"]))
    for key in ("frame_number", "time_seconds", "temporal_frame_index"):
        if meta.get(key) != dump.get(key):
            failures.append(f"{key} が雲の書き出しと違う（{meta.get(key)} / {dump.get(key)}）")
    if target != dump_target:
        failures.append(f"画面の大きさ {target} が雲の書き出し {dump_target} と違う")
    inverse_vp = np.array(meta["camera"]["inverse_view_projection"], dtype=np.float64)
    dump_inverse_vp = np.array(dump["shader_params"]["inverse_view_projection"], dtype=np.float64)
    if inverse_vp.shape != (16,) or not np.array_equal(inverse_vp, dump_inverse_vp):
        failures.append("逆の射影の行列が雲の書き出しと違う")
    camera = np.array(meta["camera"]["position"], dtype=np.float64)
    if not np.allclose(camera, np.array(dump["shader_params"]["camera_position_and_pre_exposure"][:3], dtype=np.float64)):
        failures.append("カメラの位置が雲の書き出しと違う")

    image_entry = meta.get("image", {})
    image, reason = rc.atmos_read_floats(capture_dir, image_entry)
    if reason:
        failures.append(f"不透明度の画像: {reason}")
    probe, probe_reason = rc.atmos_read_floats(capture_dir, dump.get("probe", {}))
    if probe_reason:
        failures.append(f"雲の書き出しの視線の値: {probe_reason}")
    if image is None or probe is None:
        result.update({"pass": False, "failures": failures})
        return result
    if image.shape != (target[1], target[0], 4):
        failures.append(f"画像の形 {list(image.shape)} が画面（{target[1]} × {target[0]} × 4）と違う")
        result.update({"pass": False, "failures": failures})
        return result
    pixels = dump["probe"].get("pixels", [])
    if probe.shape[0] != PROBE_ROWS or probe.shape[1] != len(pixels):
        failures.append(f"視線の値の形 {list(probe.shape)} が約束と違う")
        result.update({"pass": False, "failures": failures})
        return result

    # 2. 有限・範囲
    finite = np.isfinite(image)
    non_finite = int(np.count_nonzero(~finite))
    if non_finite:
        failures.append(f"NaN か Inf の値が {non_finite} 個ある")
    safe = np.where(finite, image, 0.0)
    for channel, name in ((0, "不透明度（溶かした後）"), (1, "1 − 透過率"), (3, "溶かす割合")):
        values = safe[:, :, channel]
        low = float(values.min())
        high = float(values.max())
        if low < -RANGE_EPSILON or high > 1.0 + RANGE_EPSILON:
            failures.append(f"{name} が 0〜1 の外（{low:.6g}〜{high:.6g}）")
    mask = safe[:, :, 2]
    if not np.all((mask == 0.0) | (mask == 1.0)):
        failures.append("地形のマスクに 0 と 1 以外の値がある")
    terrain = mask > 0.5

    # 3. 地形のマスクの外だけに値
    leaked = int(np.count_nonzero(terrain & ((safe[:, :, 0] != 0.0) | (safe[:, :, 1] != 0.0))))
    if leaked:
        failures.append(f"地形のマスクの中に雲の値のある画素が {leaked} 個ある")

    # 4. 雲の書き出しの視線と照らす
    view_projection = np.linalg.inv(inverse_vp.reshape(4, 4).T)
    rays = []
    worst = {"position_px": 0.0, "opacity_abs": 0.0, "opacity_before_fade_abs": 0.0}
    diagnostic_worst = 0.0
    cloud_rays = 0
    sky_rays = 0
    for index, pixel in enumerate(pixels):
        flags = int(round(probe[7, index, 3]))
        diagnostic = flags >> 2
        direction = probe[3, index, :3].astype(np.float64)
        depth = float(probe[6, index, 3])
        sky_pixel = not (np.isfinite(depth) and depth < SKY_DEPTH)
        alpha = float(probe[0, index, 1])
        before_fade = float(np.clip(1.0 - probe[0, index, 0], 0.0, 1.0)) if sky_pixel and (flags & 1) else 0.0
        projected = None
        if flags & 2:
            projected = project_direction(view_projection, camera, direction, target)
        entry = {"index": index, "pixel": pixel, "diagnostic": diagnostic, "sky_pixel": sky_pixel,
                 "probe_alpha": alpha, "probe_opacity_before_fade": before_fade,
                 "projected_pixel": None if projected is None else projected.tolist()}
        ray_failures = []
        if projected is None:
            ray_failures.append("投影で画素へ戻せない")
        else:
            position_error = float(np.max(np.abs(projected - np.asarray(pixel, dtype=np.float64))))
            entry["position_error_px"] = position_error
            column, row = (int(v) for v in np.floor(projected))
            if not (0 <= column < target[0] and 0 <= row < target[1]):
                ray_failures.append(f"投影した画素 {projected.tolist()} が画面の外")
            else:
                value = safe[row, column]
                entry.update({"image_opacity": float(value[0]), "image_opacity_before_fade": float(value[1]),
                              "image_terrain": bool(value[2] > 0.5)})
                opacity_error = abs(float(value[0]) - alpha)
                # 記録: 書き出しの画素の中心の値との差（投影が隣の画素へ落ちたときの比べ）
                center = safe[int(pixel[1]), int(pixel[0])]
                entry["center_opacity_abs_error"] = abs(float(center[0]) - alpha)
                before_error = abs(float(value[1]) - before_fade)
                entry.update({"opacity_abs_error": opacity_error, "opacity_before_fade_abs_error": before_error})
                if diagnostic:
                    diagnostic_worst = max(diagnostic_worst, opacity_error)
                else:
                    worst["position_px"] = max(worst["position_px"], position_error)
                    worst["opacity_abs"] = max(worst["opacity_abs"], opacity_error)
                    worst["opacity_before_fade_abs"] = max(worst["opacity_before_fade_abs"], before_error)
                    if position_error > POSITION_TOLERANCE:
                        ray_failures.append(f"投影した画素が書き出しの画素の中心から {position_error:.3f} 画素ずれる")
                    if opacity_error > OPACITY_TOLERANCE:
                        ray_failures.append(f"不透明度 {value[0]:.4f} が視線の重ねる割合 {alpha:.4f} と違う")
                    if before_error > OPACITY_TOLERANCE:
                        ray_failures.append(f"1 − 透過率 {value[1]:.4f} が視線の値 {before_fade:.4f} と違う")
                    if bool(value[2] > 0.5) == sky_pixel:
                        ray_failures.append(f"地形のマスク {int(value[2] > 0.5)} が視線の深度（空 {int(sky_pixel)}）と合わない")
        if not diagnostic:
            sky_rays += int(sky_pixel)
            cloud_rays += int(sky_pixel and alpha >= CLOUD_OPACITY)
            failures.extend(f"視線 {index}（画素 {pixel}）: {f}" for f in ray_failures)
        entry["pass"] = not ray_failures
        entry["failures"] = ray_failures
        rays.append(entry)

    sky = ~terrain
    sky_values = safe[:, :, 0][sky]
    summary = {
        "terrain_pixels": int(np.count_nonzero(terrain)), "sky_pixels": int(np.count_nonzero(sky)),
        "sky_mean_opacity": float(sky_values.mean()) if sky_values.size else 0.0,
        "sky_opacity_percentiles": {str(p): float(np.percentile(sky_values, p)) for p in (5, 25, 50, 75, 95)}
        if sky_values.size else {},
        "sky_cloud_fraction_0_05": float(np.count_nonzero(sky_values >= CLOUD_OPACITY) / sky_values.size)
        if sky_values.size else 0.0,
        "fade_min": float(safe[:, :, 3][sky].min()) if sky_values.size else 1.0,
        "writer_summary": meta.get("summary"),
    }
    preview = os.path.join(capture_dir, f"cloud_opacity_{view}.png")
    write_preview(preview, safe, [r for r in rays if not r["diagnostic"]])
    grid_rays = sum(1 for r in rays if not r["diagnostic"])
    result.update({
        "rays": grid_rays, "sky_rays": sky_rays, "cloud_rays": cloud_rays, "worst": worst,
        "diagnostic_rays": len(rays) - grid_rays, "diagnostic_opacity_max_abs": diagnostic_worst,
        "frame_number": meta.get("frame_number"), "time_seconds": meta.get("time_seconds"),
        "summary": summary, "preview": os.path.basename(preview), "ray_results": rays,
        "pass": not failures, "failures": failures,
    })
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
    targets = views if views else metric_views
    if not targets:
        failures.append("検査する視点が無い")
    results = []
    total_cloud_rays = 0
    for view in targets:
        if view not in metric_views:
            failures.append(f"{view}: metrics.json に無い視点")
            continue
        result = check_view(rc, capture_dir, view, game_args)
        results.append(result)
        total_cloud_rays += int(result.get("cloud_rays") or 0)
        worst = result.get("worst", {})
        summary = result.get("summary", {})
        print(f"REFERENCE_CHECK cloud_opacity view={view} pass={int(result['pass'])} rays={result.get('rays')} "
              f"sky_rays={result.get('sky_rays')} cloud_rays={result.get('cloud_rays')} "
              f"opacity_max_abs={worst.get('opacity_abs')} opacity_before_fade_max_abs={worst.get('opacity_before_fade_abs')} "
              f"position_max_px={worst.get('position_px')} diagnostic_opacity_max_abs={result.get('diagnostic_opacity_max_abs')} "
              f"terrain_pixels={summary.get('terrain_pixels')} sky_pixels={summary.get('sky_pixels')} "
              f"sky_mean_opacity={summary.get('sky_mean_opacity')} sky_cloud_fraction={summary.get('sky_cloud_fraction_0_05')} "
              f"frame={result.get('frame_number')} preview={result.get('preview')} inputs_current={int(bool(inputs_ok))}")
        failures.extend(f"{view}: {failure}" for failure in result["failures"])
    if results and total_cloud_rays < MIN_CLOUD_RAYS:
        failures.append(f"雲のある視線が足りない（全視点で {total_cloud_rays} 本 / 下限 {MIN_CLOUD_RAYS}）。検査が成り立たない")
    report = {
        "mode": "cloud_opacity",
        "verify": bool(verify),
        "inputs_current": inputs_ok,
        "inputs_record": record,
        "game_args": game_args,
        "criteria": {
            "opacity_tolerance": OPACITY_TOLERANCE, "position_tolerance_px": POSITION_TOLERANCE,
            "range_epsilon": RANGE_EPSILON, "cloud_opacity": CLOUD_OPACITY, "min_cloud_rays": MIN_CLOUD_RAYS,
            "value": "r = opacity after fade (cloud_layer.frag alpha), g = 1 - transmittance before fade",
        },
        "views": results,
        "failures": failures,
    }
    report_path = os.path.join(capture_dir, "cloud_opacity_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False, default=float)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=cloud_opacity views={len(targets)} cloud_rays={total_cloud_rays} "
          f"inputs_current={int(bool(inputs_ok))} report={report_path}")
    return 0 if not failures else 1
