# 撮影の AOV の地形の印（<視点>.aov.terrain_id.pfm）の検査（SCN-WL-SHAPE-TERRAIN-AOV。ReferenceCheck.py --aov-terrain-id から呼ぶ。
# 単独では動かさない）。
#
# 入力: CaptureScene.ps1 -Aov で 2 視点以上を 1 回の起動（--capture-views）で撮った出力先（--engine）と、同じ視点・同じ起動引数を
# -Aov なしで撮った出力先（--plain）。
#   落ちる: 印の画像・記録が無い、値が 0・1・255 以外、描いた画素（深度が有限）に書かれていない画素（255）がある、空の画素が 0 でない、
#           印の記録のフレームの番号・印が深度・面の法線の記録と違う、面の法線が書かれた画素と印が書かれた画素が食い違う、
#           同じ起動の視点どうしでフレームの番号か印が同じ（前の視点の値が残っても見分けられない）、
#           -Aov の有無の PNG の差が 1 段以内の画素が 99.9 % に届かない、--verify で入力のハッシュが撮影の後に変わった
#   記録する: 視点ごとの地形・ほかの物・空の画素数、フレームの番号と印、PNG の差の割合と最大
# 印の画（<engine>/terrain_id_<視点>.png）: 地形 = 緑、ほかの物 = 橙、空 = 灰の PNG のまま、書かれていない画素 = 赤紫を PNG に重ねる。
# 結果は <engine>/aov_terrain_id_check.json に書く。
import json
import os

import numpy as np
from PIL import Image

REPORT = "aov_terrain_id_check.json"
# 印の値（gbuffer_aov.frag・FrameCaptureHdrWriter の terrain_id と同じ）
ID_OTHER = 0.0
ID_TERRAIN = 1.0
ID_UNWRITTEN = 255.0
# -Aov の有無で通常の描画が変わらないことの基準: 8 bit の差が 1 段以内の画素の割合
PLAIN_MAX_STEP = 1
PLAIN_MIN_FRACTION = 0.999
# 面の法線と同じ印の作り方（VisibilityResolvePass::GetGeometricNormalTag）
TAG_PERIOD = 2048


def frame_tag(frame_number):
    return float(frame_number % TAG_PERIOD) + 1.0


def load_json(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def load_png(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"), dtype=np.int16)


def write_overlay(path, png, terrain_id, drawn):
    """PNG に印を重ねた画（地形 = 緑・ほかの物 = 橙・書かれていない画素 = 赤紫。空は PNG のまま）"""
    base = png.astype(np.float32)
    colors = np.zeros_like(base)
    alpha = np.zeros(terrain_id.shape, dtype=np.float32)
    terrain = drawn & (terrain_id == ID_TERRAIN)
    other = drawn & (terrain_id == ID_OTHER)
    unwritten = terrain_id == ID_UNWRITTEN
    colors[terrain] = (40.0, 200.0, 60.0)
    colors[other] = (255.0, 140.0, 0.0)
    colors[unwritten] = (255.0, 0.0, 255.0)
    alpha[terrain | other] = 0.55
    alpha[unwritten] = 1.0
    out = base * (1.0 - alpha[..., None]) + colors * alpha[..., None]
    Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8)).save(path)


def check_view(rc, engine_dir, plain_dir, view):
    result = {"view": view, "failures": []}
    failures = result["failures"]
    meta_path = os.path.join(engine_dir, f"{view}.aov.json")
    if not os.path.isfile(meta_path):
        failures.append("AOV の記録（.aov.json）が無い")
        return result
    meta = load_json(meta_path)
    entry = meta.get("terrain_id")
    normal_entry = meta.get("geometric_normal") or {}
    if not isinstance(entry, dict) or not entry.get("file"):
        failures.append("AOV の記録に terrain_id が無い（印を書く前の Game.exe の撮影）")
        return result
    frame_number = meta.get("frame_number")
    result["frame_number"] = frame_number
    result["frame_tag"] = entry.get("frame_tag")
    if not isinstance(frame_number, int):
        failures.append("AOV の記録にフレームの番号が無い")
        return result
    # 同じフレームの値: 印・面の法線の記録のフレームの番号が、深度を含む AOV 全体の番号と同じで、印がその番号から作った値
    expected_tag = frame_tag(frame_number)
    for name, item in (("terrain_id", entry), ("geometric_normal", normal_entry)):
        if item.get("frame_number") != frame_number:
            failures.append(f"{name} のフレームの番号 {item.get('frame_number')} が AOV の {frame_number} と違う")
        if item.get("frame_tag") != expected_tag:
            failures.append(f"{name} の印 {item.get('frame_tag')} がフレームの番号から作る {expected_tag:g} と違う")
    if entry.get("unwritten_value") != ID_UNWRITTEN or entry.get("sky_value") != ID_OTHER:
        failures.append(f"terrain_id の書かれていない値・空の値の記録が約束と違う（{entry.get('unwritten_value')}・{entry.get('sky_value')}）")

    terrain_id = rc.read_pfm_any(os.path.join(engine_dir, entry["file"]))[..., 0]
    depth = rc.read_pfm_any(os.path.join(engine_dir, meta["depth"]["file"]))[..., 0]
    normal = rc.read_pfm_any(os.path.join(engine_dir, normal_entry["file"]))[..., 0] if normal_entry.get("file") else None
    if terrain_id.shape != depth.shape:
        failures.append(f"印と深度の大きさが違う（{terrain_id.shape}・{depth.shape}）")
        return result
    drawn = np.isfinite(depth)
    sky = ~drawn
    values, counts = np.unique(terrain_id, return_counts=True)
    result["values"] = {f"{v:g}": int(c) for v, c in zip(values.tolist(), counts.tolist())}
    unknown = [v for v in values.tolist() if v not in (ID_OTHER, ID_TERRAIN, ID_UNWRITTEN)]
    if unknown:
        failures.append(f"印に 0・1・255 以外の値がある（{', '.join(f'{v:g}' for v in unknown[:8])}）")
    unwritten_drawn = int(np.count_nonzero(drawn & (terrain_id == ID_UNWRITTEN)))
    sky_nonzero = int(np.count_nonzero(sky & (terrain_id != ID_OTHER)))
    result.update({
        "drawn_pixels": int(np.count_nonzero(drawn)),
        "sky_pixels": int(np.count_nonzero(sky)),
        "terrain_pixels": int(np.count_nonzero(drawn & (terrain_id == ID_TERRAIN))),
        "other_pixels": int(np.count_nonzero(drawn & (terrain_id == ID_OTHER))),
        "unwritten_drawn_pixels": unwritten_drawn,
        "sky_nonzero_pixels": sky_nonzero,
    })
    if unwritten_drawn:
        failures.append(f"描いた画素に書かれていない印（255）が {unwritten_drawn} 画素ある")
    if sky_nonzero:
        failures.append(f"空の画素の印が 0 でない画素が {sky_nonzero} 画素ある")
    if result["terrain_pixels"] == 0:
        failures.append("地形の画素が 1 つも無い（印が書かれていない）")
    if normal is not None:
        # 面の法線が読めた画素（> 0）と印が書かれた画素（0・1）は、同じ画素・同じフレームで解決が書いた値なので一致する
        normal_written = drawn & (normal > 0.0)
        id_written = drawn & (terrain_id != ID_UNWRITTEN)
        mismatch = int(np.count_nonzero(normal_written != id_written))
        result["normal_id_mismatch_pixels"] = mismatch
        if mismatch:
            failures.append(f"面の法線が書かれた画素と印が書かれた画素が {mismatch} 画素で食い違う")
    else:
        failures.append("AOV の記録に面の法線が無い")

    # 通常の描画に影響しない: 同じ視点の -Aov ありと無しの PNG の差
    png_path = os.path.join(engine_dir, f"{view}.png")
    plain_path = os.path.join(plain_dir, f"{view}.png") if plain_dir else ""
    if not os.path.isfile(png_path) or not plain_path or not os.path.isfile(plain_path):
        failures.append("-Aov の有無の PNG のどちらかが無い")
        return result
    png = load_png(png_path)
    plain = load_png(plain_path)
    if png.shape != plain.shape:
        failures.append(f"-Aov の有無の PNG の大きさが違う（{png.shape}・{plain.shape}）")
        return result
    step = np.abs(png - plain).max(axis=2)
    within = float(np.count_nonzero(step <= PLAIN_MAX_STEP)) / float(step.size)
    result["plain_within_fraction"] = within
    result["plain_max_step"] = int(step.max())
    if within < PLAIN_MIN_FRACTION:
        failures.append(f"-Aov の有無の PNG の差が {PLAIN_MAX_STEP} 段以内の画素が {within:.5f}（基準 {PLAIN_MIN_FRACTION}）")
    if png.shape[:2] == terrain_id.shape:
        overlay = os.path.join(engine_dir, f"terrain_id_{view}.png")
        write_overlay(overlay, png, terrain_id, drawn)
        result["overlay"] = os.path.relpath(overlay, rc.REPO_ROOT)
    return result


def run(rc, engine_dir, plain_dir, views, verify):
    failures = []
    report = {"mode": "aov_terrain_id", "verify": bool(verify),
              "engine": os.path.relpath(engine_dir, rc.REPO_ROOT) if engine_dir else "",
              "plain": os.path.relpath(plain_dir, rc.REPO_ROOT) if plain_dir else "",
              "criteria": {"values": [ID_OTHER, ID_TERRAIN, ID_UNWRITTEN], "plain_max_step": PLAIN_MAX_STEP,
                           "plain_min_fraction": PLAIN_MIN_FRACTION},
              "views": []}
    if not engine_dir or not os.path.isdir(engine_dir):
        print(f"REFERENCE_CHECK result=fail reason=engine_dir_missing（--engine に -Aov の撮影先を指定する） path={engine_dir}")
        return 1
    if not plain_dir or not os.path.isdir(plain_dir):
        print(f"REFERENCE_CHECK result=fail reason=plain_dir_missing（--plain に -Aov なしの撮影先を指定する） path={plain_dir}")
        return 1
    metric_views = rc.views_from_metrics(engine_dir) or []
    plain_views = rc.views_from_metrics(plain_dir) or []
    views = list(views) or list(metric_views)
    if not views:
        failures.append("検査する視点が無い（metrics.json に視点が無い）")
    for view in views:
        if view not in metric_views:
            failures.append(f"{view}: -Aov の撮影に無い視点")
        if view not in plain_views:
            failures.append(f"{view}: -Aov なしの撮影に無い視点")
    for name, directory in (("engine", engine_dir), ("plain", plain_dir)):
        inputs_ok, input_failures, _ = rc.check_inputs(directory)
        report[f"inputs_current_{name}"] = inputs_ok
        if verify:
            failures.extend(f"{name}: {f}" for f in input_failures)
    targets = [v for v in views if v in metric_views]
    for view in targets:
        result = check_view(rc, engine_dir, plain_dir, view)
        report["views"].append(result)
        print(f"REFERENCE_CHECK aov_terrain_id view={view} pass={int(not result['failures'])} "
              f"frame={result.get('frame_number')} tag={result.get('frame_tag')} "
              f"terrain={result.get('terrain_pixels')} other={result.get('other_pixels')} sky={result.get('sky_pixels')} "
              f"unwritten_drawn={result.get('unwritten_drawn_pixels')} sky_nonzero={result.get('sky_nonzero_pixels')} "
              f"normal_mismatch={result.get('normal_id_mismatch_pixels')} values={result.get('values')} "
              f"plain_within={result.get('plain_within_fraction', float('nan')):.6f} "
              f"plain_max_step={result.get('plain_max_step')}")
        failures.extend(f"{view}: {f}" for f in result["failures"])
    # 同じ起動の視点どうしは別のフレームの印で書かれる（資源を使い回しても、前の視点の印は今の印と合わず読まれない）
    stamped = [(r["view"], r.get("frame_number"), r.get("frame_tag")) for r in report["views"] if r.get("frame_number") is not None]
    if len(targets) >= 2 and len(stamped) < 2:
        failures.append("2 視点のフレームの番号が読めない")
    for index in range(1, len(stamped)):
        previous, current = stamped[index - 1], stamped[index]
        if current[1] == previous[1] or current[2] == previous[2]:
            failures.append(f"{current[0]}: 前の視点 {previous[0]} とフレームの番号か印が同じ（{current[1]}・{current[2]}）")
    if len(targets) < 2:
        failures.append("同じ起動の 2 視点が無い（前の視点の値が残らないことを確かめられない）")
    report["failures"] = failures
    report_path = os.path.join(engine_dir, REPORT)
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=aov_terrain_id views={len(targets)} "
          f"inputs_current={int(bool(report.get('inputs_current_engine')) and bool(report.get('inputs_current_plain')))} "
          f"report={report_path}")
    return 0 if not failures else 1
