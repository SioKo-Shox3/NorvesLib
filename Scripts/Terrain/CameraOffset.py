# 撮影の画と記録のカメラのずれの検査（SCN-WL-CAMERA-OFFSET。ReferenceCheck.py --camera-offset から呼ぶ。単独では動かさない）。
#
# エンジンの撮影（CaptureScene.ps1 -Aov の出力）の視点ごとに、記録のカメラ（<視点>.aov.json の camera）のまま補正なしで
# Blender の深度を描き（BlenderReference.py --depth。カメラをずらさない。BlenderReference.py はずらしが 0 でない描画を断る）、エンジンの深度との小数のずれを ReferenceCheck.depth_subpixel_shift で、エンジンの深度が MAX_DEPTH_M 未満の画素で
# 測る（全画面の値と画面の列ごとの値は記録だけ）。
#   落ちる: Blender の深度が無いか描いた後に入力が変わった（--verify では撮影の入力も照らす）、Blender の記録のカメラ・画素数が
#           撮影と違う、Blender のカメラの姿勢が記録のカメラからずれている（ずらし量が 0 でない）、
#           残りのずれ（右・下の画素の長さ）が MAX_RESIDUAL_PX を超える
#   記録する: 前の視線の作り方（注視点 = 位置 + 前 を float で足してから引く）が丸めで生んでいた向きのずれ（画素。原因の確認用）
# Blender の深度が無いか、記録のカメラ・画素数が撮影と違えば、その場で描いて <撮影>/blender に保存する。
# 結果は <撮影>/camera_offset_check.json に書く。
import json
import os
import subprocess
import sys

import numpy as np

VIEWS = ("ref_cliff_01", "ref_cliff_02")
MAX_RESIDUAL_PX = 0.05
# ずれを測る画素の深度の上限（法線・アルベドの照合の判定の範囲 NORMAL_MAX_DEPTH_M と同じ 3 km）と、記録する画面の列の数
MAX_DEPTH_M = 3000.0
COLUMNS = 4
# Blender の姿勢の行列と、記録のカメラから作った行列の食い違いの許容（成分の差）
MATRIX_TOLERANCE = 1e-9
MESH_DIR = os.path.join(".harness", "runs", "scene-capture", "SCN-WLR-BLENDER-TERRAIN-mesh")
BLENDER_DIR = "blender"
REPORT = "camera_offset_check.json"


def load_json(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def lookat_rounding_px(camera, width, height):
    """前の視線の作り方（CreateLookAt(位置, 位置 + 前, 上)）が float の丸めで生んでいた前の向きのずれを、画面の右・上の画素で返す"""
    position = np.asarray(camera["position"], dtype=np.float32)
    forward = np.asarray(camera["forward"], dtype=np.float32)
    rounded = ((position + forward).astype(np.float32) - position).astype(np.float64)
    exact = np.asarray(camera["forward"], dtype=np.float64)
    exact /= np.linalg.norm(exact)
    right = np.asarray(camera["right"], dtype=np.float64)
    up = np.cross(right, exact)
    up /= np.linalg.norm(up)
    focal = 0.5 * height / np.tan(np.radians(float(camera["vertical_fov_degrees"])) * 0.5)
    along = float(np.dot(rounded, exact))
    return [float(np.dot(rounded, right) / along * focal), float(np.dot(rounded, up) / along * focal)]


def blender_view_current(capture_dir, view):
    """保存した Blender の深度が、この撮影の記録のカメラ・画素数で描いたものか"""
    meta_path = os.path.join(capture_dir, BLENDER_DIR, f"{view}.json")
    depth_path = os.path.join(capture_dir, BLENDER_DIR, f"{view}.depth.pfm")
    if not (os.path.isfile(meta_path) and os.path.isfile(depth_path)):
        return False
    aov = load_json(os.path.join(capture_dir, f"{view}.aov.json"))
    meta = load_json(meta_path)
    return meta.get("camera_engine") == aov["camera"] and \
        (int(meta.get("width", 0)), int(meta.get("height", 0))) == (int(aov["width"]), int(aov["height"]))


def render_depth(rc, capture_dir, views):
    """BlenderReference.py --depth で深度を描く（カメラのずらしは渡さない）。(成功か, 出力の末尾)"""
    command = [sys.executable, os.path.join(rc.REPO_ROOT, "Scripts", "Terrain", "BlenderReference.py"), "--depth",
               "--mesh", os.path.join(rc.REPO_ROOT, MESH_DIR), "--capture", capture_dir, "--views", ",".join(views)]
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    for line in completed.stdout.splitlines():
        print(line, flush=True)
    return completed.returncode == 0, (completed.stdout + completed.stderr)[-2000:]


def check_view(rc, br, capture_dir, view):
    failures = []
    result = {"view": view, "failures": failures}
    aov = load_json(os.path.join(capture_dir, f"{view}.aov.json"))
    meta_path = os.path.join(capture_dir, BLENDER_DIR, f"{view}.json")
    if not os.path.isfile(meta_path):
        failures.append("Blender の深度が無い")
        result["pass"] = False
        return result
    meta = load_json(meta_path)
    width, height = int(aov["width"]), int(aov["height"])
    if meta.get("camera_engine") != aov["camera"]:
        failures.append("Blender の記録のカメラが撮影の記録のカメラと違う")
    if (int(meta.get("width", 0)), int(meta.get("height", 0))) != (width, height):
        failures.append(f"画素数が違う（エンジン {width}×{height}、Blender {meta.get('width')}×{meta.get('height')}）")
    # ずらし量: 深度の描画はカメラのずらしを持たない（記録に無ければ 0）。姿勢の行列が記録のカメラそのものかも照らす
    shift = [float(v) for v in (meta.get("camera_shift_px") or [0.0, 0.0])]
    result["camera_shift_px"] = shift
    if any(v != 0.0 for v in shift):
        failures.append(f"Blender のカメラが ({shift[0]:+.3f}, {shift[1]:+.3f}) 画素ずらしてある（ずらし 0 で描く）")
    expected_matrix, expected_fov = br.camera_to_blender(aov["camera"])
    matrix = np.asarray(meta.get("camera_blender_matrix_world") or np.zeros((4, 4)), dtype=np.float64)
    matrix_error = float(np.max(np.abs(matrix - np.asarray(expected_matrix, dtype=np.float64))))
    result["blender_matrix_max_error"] = matrix_error
    if matrix_error > MATRIX_TOLERANCE:
        failures.append(f"Blender のカメラの姿勢が記録のカメラと {matrix_error:.3g} 違う")
    if abs(float(meta.get("camera_vertical_fov_degrees", 0.0)) - float(np.degrees(expected_fov))) > 1e-9:
        failures.append("Blender の縦の画角が記録のカメラと違う")
    if aov.get("depth", {}).get("meaning") != "view_axis_distance" or meta.get("meaning") != "view_axis_distance":
        failures.append("深度の意味が view_axis_distance でない")
    engine_depth = rc.read_pfm_any(os.path.join(capture_dir, aov["depth"]["file"]))[..., 0].astype(np.float64)
    blender_depth = rc.read_pfm_any(os.path.join(capture_dir, BLENDER_DIR, f"{view}.depth.pfm"))[..., 0].astype(np.float64)
    # ずれは MAX_DEPTH_M 未満の画素で測る。遠くは地形の形がエンジンと書き出しで違い、画面の列ごとにずれが揃わない
    near = np.where(engine_depth < MAX_DEPTH_M, engine_depth, np.inf)
    fit = rc.depth_subpixel_shift(near, blender_depth)
    result["residual"] = fit
    result["residual_all_depths"] = rc.depth_subpixel_shift(engine_depth, blender_depth)
    width_px = engine_depth.shape[1]
    columns = []
    for index in range(COLUMNS):
        column = np.full(near.shape, np.inf)
        span = slice(index * width_px // COLUMNS, (index + 1) * width_px // COLUMNS)
        column[:, span] = near[:, span]
        columns.append(rc.depth_subpixel_shift(column, blender_depth))
    result["residual_columns"] = columns
    residual = float(np.hypot(fit["right_px"], fit["down_px"]))
    result["residual_px"] = residual
    if fit["pixels"] < 1000:
        failures.append(f"ずれを測れる画素が {fit['pixels']} しか無い")
    if not residual <= MAX_RESIDUAL_PX:
        failures.append(f"エンジンの深度と記録のカメラの Blender の深度のずれが ({fit['right_px']:+.3f}, "
                        f"{fit['down_px']:+.3f}) 画素（長さ {residual:.3f}。{MAX_RESIDUAL_PX} 超）")
    result["old_lookat_rounding_px"] = lookat_rounding_px(aov["camera"], width, height)
    result["pass"] = not failures
    return result


def run(rc, capture_dir, views, verify):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import BlenderReference as br  # noqa: E402  姿勢の行列の作り方を描画と同じ関数で照らす

    views = list(views) or list(VIEWS)
    failures = []
    report = {"mode": "camera_offset", "verify": bool(verify), "capture": os.path.relpath(capture_dir, rc.REPO_ROOT),
              "criteria": {"max_residual_px": MAX_RESIDUAL_PX, "max_depth_m": MAX_DEPTH_M, "camera_shift_px": [0.0, 0.0],
                           "matrix_tolerance": MATRIX_TOLERANCE},
              "views": []}
    metric_views = rc.views_from_metrics(capture_dir) or []
    missing = [v for v in views if v not in metric_views or not os.path.isfile(os.path.join(capture_dir, f"{v}.aov.json"))]
    for view in missing:
        failures.append(f"{view}: 撮影に無い視点（CaptureScene.ps1 -Aov の出力先でない）")
    targets = [v for v in views if v not in missing]
    inputs_ok, input_failures, record = rc.check_inputs(capture_dir)
    if verify:
        failures.extend(input_failures)
    report["inputs_current"] = inputs_ok
    blender_ok, blender_failures, _ = rc.check_blender_inputs(os.path.join(capture_dir, BLENDER_DIR))
    stale = [v for v in targets if not blender_view_current(capture_dir, v)]
    if targets and (stale or not blender_ok):
        print(f"REFERENCE_CHECK camera_offset render views={','.join(targets)} "
              f"reason={'stale_views=' + ','.join(stale) if stale else 'inputs_changed'}", flush=True)
        rendered, tail = render_depth(rc, capture_dir, targets)
        report["rendered"] = True
        if not rendered:
            failures.append(f"Blender の深度の描画が失敗した: {tail.strip().splitlines()[-1] if tail.strip() else ''}")
        blender_ok, blender_failures, _ = rc.check_blender_inputs(os.path.join(capture_dir, BLENDER_DIR))
    failures.extend(blender_failures)
    report["blender_inputs_current"] = blender_ok
    for view in targets:
        result = check_view(rc, br, capture_dir, view)
        report["views"].append(result)
        fit = result.get("residual") or {}
        rounding = result.get("old_lookat_rounding_px") or [float("nan"), float("nan")]
        everything = result.get("residual_all_depths") or {}
        columns = " ".join(f"{c['right_px']:+.3f}/{c['down_px']:+.3f}" for c in result.get("residual_columns") or [])
        print(f"REFERENCE_CHECK camera_offset view={view} pass={int(result['pass'])} "
              f"camera_shift_px={result.get('camera_shift_px')} "
              f"residual_right={fit.get('right_px', float('nan')):+.4f} residual_down={fit.get('down_px', float('nan')):+.4f} "
              f"residual_px={result.get('residual_px', float('nan')):.4f} pixels={fit.get('pixels')} "
              f"columns=[{columns}] all_depths=({everything.get('right_px', float('nan')):+.3f},"
              f"{everything.get('down_px', float('nan')):+.3f}) "
              f"old_lookat_rounding_px=({rounding[0]:+.3f},{rounding[1]:+.3f})")
        failures.extend(f"{view}: {f}" for f in result["failures"])
    report["failures"] = failures
    report_path = os.path.join(capture_dir, REPORT)
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=camera_offset views={len(targets)} "
          f"inputs_current={int(inputs_ok)} blender_inputs_current={int(blender_ok)} report={report_path}")
    return 0 if not failures else 1
