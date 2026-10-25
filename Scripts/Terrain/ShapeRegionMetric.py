# 地形の形の量を世界座標の範囲ごとに地形の画素だけで測る（SCN-WL-SHAPE-REGION-METRIC。ReferenceCheck.py --shape-cause --regions から
# 呼ぶ。単独では動かさない）。
#
#   python Scripts/Terrain/ReferenceCheck.py --shape-cause --capture-root <置き場> --regions Scripts/Terrain/ShapeRegions.json
#       [--views <視点,...>] [--expect <仕様>] [--baseline <前の基準の置き場>] [--verify]
#   python Scripts/Terrain/ReferenceCheck.py --shape-cause --capture-root <基準の置き場> --baseline-record
#   python Scripts/Terrain/ReferenceCheck.py --shape-cause --capture-root <基準の置き場> --regions <範囲> --baseline-integrity [--verify]
#   python Scripts/Terrain/ReferenceCheck.py --shape-cause --selftest
#
# 置き場の組は ShapeCause.SETS の 1〜3 組のどれでも（lod-default・lod-finest・cliff-detail-off。どれも -Hdr -Aov）。組ごとに
# 折れ目の密度・材質の陰影の差・格子の限界を測り、通常の LOD と最も細かい LOD の組があれば LOD の差、最も細かい LOD と崖の変形なしの
# 組があれば崖の生成の差も測る。範囲（ShapeRegions.json）は世界座標の X・Z の矩形で、範囲に入る画素のうち地形の印
# （<視点>.aov.terrain_id.pfm が 1）の画素だけを数える。折れ目は隣の画素も地形の印のときだけ比べる（岩の縁を地形の折れに数えない）。
# 範囲の中は 32 × 32 画素の区画に分け、主の組（lod-default が無ければ並びの最初の組）の折れ目を区画ごとに記録する。
# --baseline を付けると、前の基準の置き場（基準の記録が合うことを先に確かめる）を同じ範囲で測り、範囲ごとの差と、折れ目が
# 前の基準より kLocalDropPoints 以上下がった区画の割合（局所の退行）を出す。
# 基準の記録（<置き場>/baseline_manifest.json）は、撮影の出力のファイルごとのハッシュと当時の入力の記録（inputs.json）を持つ。
# --baseline-integrity はこの記録とファイルだけを照らし、今の実装（Game.exe・シェーダー・台本）との一致は求めない。
# 結果は <置き場>/shape_regions.json、範囲の重ね画は <置き場>/shape_regions/<視点>.png。
# 落ちる: 範囲の定義の誤り・未知の視点・空の選択・組の撮影や AOV の欠け・起動引数が組の条件と違う・描いた画素に地形の印が
#   書かれていない（255）・幾何の法線の被覆の不足・範囲の地形の画素が最低の数に足りない・組の Game.exe が揃わない・
#   --expect の行が落ちる（未知の範囲・視点、範囲の視点に無い視点、値が無い、範囲の外、選んだ行が無い）・基準の記録が合わない・
#   --verify で入力が撮影の後に変わった（基準の検査では照らさない）。
import datetime
import json
import os
import subprocess

import numpy as np
from PIL import Image, ImageDraw

import ShapeCause

REPORT = "shape_regions.json"
PREVIEW_DIR = "shape_regions"
MANIFEST = "baseline_manifest.json"
MANIFEST_FORMAT = "shape_baseline_manifest.v1"
ID_TERRAIN = 1
ID_UNWRITTEN = 255
# 局所の値の区画（画素）と、区画の画素のうち範囲の地形の画素がこの割合に満たない区画は局所の値を出さない
kBlockPixels = 32
kBlockMinShare = 0.25
# 局所の退行: 区画の折れ目が前の基準よりこれ以上下がった区画を数える（割合の差。10 ポイント）
kLocalDropPoints = 0.10
REGION_KEYS = ("id", "name", "owner", "source", "x", "z", "views", "note")
OWNERS = ("cliff_generation", "scan", "valley_floor")
EXPECT_KEYS = ("region", "view", "quantity", "min", "max", "note")
REGION_COLORS = [(255, 70, 70), (60, 200, 255), (255, 210, 40), (170, 90, 255), (60, 230, 120), (255, 130, 0),
                 (255, 80, 200), (120, 255, 230), (200, 255, 60), (90, 120, 255), (255, 170, 150), (180, 180, 180)]


def load_regions(path, views_known):
    """範囲の定義を読む。(範囲の一覧, 失敗の一覧)"""
    if not path or not os.path.isfile(path):
        return None, [f"範囲の定義 {path} が無い"]
    try:
        with open(path, encoding="utf-8-sig") as f:
            document = json.load(f)
    except (OSError, ValueError) as error:
        return None, [f"範囲の定義 {path} が読めない: {error}"]
    regions = document.get("regions") if isinstance(document, dict) else None
    if not isinstance(regions, list) or not regions:
        return None, [f"範囲の定義 {path} に regions が無い"]
    failures = []
    seen = set()
    for index, region in enumerate(regions):
        label = f"範囲 {index + 1}"
        if not isinstance(region, dict):
            failures.append(f"{label} が JSON のオブジェクトでない")
            continue
        unknown = [key for key in region if key not in REGION_KEYS]
        if unknown:
            failures.append(f"{label} に知らない鍵 {unknown}")
        identifier = region.get("id")
        if not isinstance(identifier, str) or not identifier or identifier in seen:
            failures.append(f"{label} の id {identifier} が無いか重なる")
        seen.add(identifier)
        if region.get("owner") not in OWNERS:
            failures.append(f"{label}（{identifier}）の owner {region.get('owner')} が {OWNERS} のどれでもない")
        for axis in ("x", "z"):
            bounds = region.get(axis)
            if (not isinstance(bounds, list) or len(bounds) != 2 or
                    not all(isinstance(v, (int, float)) for v in bounds) or not bounds[0] < bounds[1]):
                failures.append(f"{label}（{identifier}）の {axis} が [最小, 最大] でない: {bounds}")
        views = region.get("views")
        if not isinstance(views, dict) or not views:
            failures.append(f"{label}（{identifier}）に views（視点 → 最低の地形の画素数）が無い")
            continue
        for view, minimum in views.items():
            if view not in views_known:
                failures.append(f"{label}（{identifier}）の視点 {view} が未知")
            if not isinstance(minimum, int) or isinstance(minimum, bool) or minimum <= 0:
                failures.append(f"{label}（{identifier}）の視点 {view} の最低の画素数 {minimum} が正の整数でない")
    return (regions if not failures else None), failures


def load_terrain_id(RC, set_dir, view, depth_finite):
    """地形の印（1 = 地形）。描いた画素に書かれていない画素（255）があれば None と理由"""
    path = os.path.join(set_dir, f"{view}.aov.terrain_id.pfm")
    if not os.path.isfile(path):
        return None, f"{os.path.basename(path)} が無い（-Aov の地形の印）"
    terrain_id = np.rint(RC.read_pfm_any(path)[:, :, 0]).astype(np.int64)
    if terrain_id.shape != depth_finite.shape:
        return None, f"{os.path.basename(path)} の大きさ {terrain_id.shape} が深度 {depth_finite.shape} と違う"
    unwritten = int(np.count_nonzero(depth_finite & (terrain_id == ID_UNWRITTEN)))
    if unwritten:
        return None, f"{os.path.basename(path)} の描いた画素 {unwritten} に地形の印が書かれていない（255）"
    unknown = int(np.count_nonzero(~np.isin(terrain_id, (0, ID_TERRAIN, ID_UNWRITTEN))))
    if unknown:
        return None, f"{os.path.basename(path)} に 0・1・255 のほかの値が {unknown} 画素ある"
    return terrain_id == ID_TERRAIN, None


def load_set_view(RC, set_dir, view):
    """組の視点の配列。(辞書, 理由)"""
    geometry, reason = ShapeCause.view_geometry(RC, set_dir, view)
    if geometry is None:
        return None, reason
    position, geometric, shading, valid, footprint, camera, coverage = geometry
    if coverage < ShapeCause.kGeometricCoverage:
        return None, f"幾何の法線の被覆率 {coverage:.3f} が {ShapeCause.kGeometricCoverage:.2f} 未満"
    depth = RC.read_pfm_any(os.path.join(set_dir, f"{view}.aov.depth.pfm"))[:, :, 0]
    terrain, reason = load_terrain_id(RC, set_dir, view, np.isfinite(depth) & (depth > 0.0))
    if terrain is None:
        return None, reason
    return {"position": position, "geometric": geometric, "shading": shading, "valid": valid, "footprint": footprint,
            "camera": camera, "coverage": coverage, "terrain": terrain}, None


def region_rect(position, region):
    x = position[:, :, 0]
    z = position[:, :, 2]
    return (x >= region["x"][0]) & (x <= region["x"][1]) & (z >= region["z"][0]) & (z <= region["z"][1])


def block_sums(mask):
    """32 × 32 画素の区画ごとの数（端の欠けた区画は除く）"""
    rows = mask.shape[0] // kBlockPixels
    columns = mask.shape[1] // kBlockPixels
    cut = mask[:rows * kBlockPixels, :columns * kBlockPixels].astype(np.int64)
    return cut.reshape(rows, kBlockPixels, columns, kBlockPixels).sum(axis=(1, 3))


def fraction(mask, within):
    count = int(np.count_nonzero(within))
    return (float(np.count_nonzero(mask & within)) / count) if count else None


def primary_set(names):
    return "lod-default" if "lod-default" in names else names[0]


def measure_regions(levels, view, regions, data):
    """1 視点の範囲ごとの量。data は 組 → load_set_view の辞書（組の並びは ShapeCause.SETS の順）。(記録, 失敗の一覧)"""
    names = [name for name in ShapeCause.SETS if name in data]
    primary = primary_set(names)
    base = data[primary]
    position = base["position"]
    # 地形の印の画素だけを有効にし、折れ目は両隣も地形の印のときだけ比べる
    terrain_valid = {name: data[name]["valid"] & data[name]["terrain"] for name in names}
    creases = {name: ShapeCause.crease_mask(data[name]["geometric"], terrain_valid[name]) for name in names}
    spacing, _ = ShapeCause.level_spacing(levels, position)
    grid_limited = terrain_valid[primary] & (base["footprint"] * ShapeCause.kGridPixels <= spacing)
    shading_diff = terrain_valid[primary] & (
        ShapeCause.angle_degrees(base["shading"], base["geometric"]) > ShapeCause.kShadingDegrees)
    pair_diffs = {}
    for key, first, second in (("lod_diff", "lod-default", "lod-finest"), ("cliff_diff", "lod-finest", "cliff-detail-off")):
        if first in data and second in data:
            common = terrain_valid[first] & terrain_valid[second]
            pair_diffs[key] = (common & (ShapeCause.angle_degrees(data[first]["geometric"], data[second]["geometric"])
                                         > ShapeCause.kDiffDegrees), common)
    drawn = base["valid"]
    camera_position = np.asarray(base["camera"]["position"], dtype=np.float64)
    records = {}
    failures = []
    for region in regions:
        minimum = region["views"].get(view)
        if minimum is None:
            continue
        rect = region_rect(position, region)
        inside = rect & terrain_valid[primary]
        count = int(np.count_nonzero(inside))
        record = {
            "terrain_pixels": count,
            "min_pixels": minimum,
            "other_pixels": int(np.count_nonzero(rect & drawn & ~base["terrain"])),
            "primary_set": primary,
        }
        records[region["id"]] = record
        if count < minimum:
            record["insufficient"] = True
            failures.append(f"範囲 {region['id']} の視点 {view} の地形の画素 {count} が最低の {minimum} に足りない")
            continue
        xs = position[:, :, 0][inside]
        zs = position[:, :, 2][inside]
        record.update({
            "world_x": [round(float(np.percentile(xs, 5)), 1), round(float(np.percentile(xs, 95)), 1)],
            "world_z": [round(float(np.percentile(zs, 5)), 1), round(float(np.percentile(zs, 95)), 1)],
            "distance_median_m": round(float(np.median(np.linalg.norm(position[inside] - camera_position[None, :],
                                                                      axis=1))), 1),
            "crease": {name: fraction(creases[name][0], rect & terrain_valid[name] & creases[name][1]) for name in names},
            "shading_diff": fraction(shading_diff, inside),
            "grid_limited": fraction(grid_limited, inside),
        })
        for key, (mask, common) in pair_diffs.items():
            record[key] = fraction(mask, rect & common)
        # 32 × 32 画素の区画ごとの主の組の折れ目
        crease, pairs = creases[primary]
        block_inside = block_sums(inside)
        block_pairs = block_sums(inside & pairs)
        block_crease = block_sums(inside & pairs & crease)
        usable = (block_inside >= kBlockMinShare * kBlockPixels * kBlockPixels) & (block_pairs > 0)
        blocks = []
        for row, column in zip(*np.nonzero(usable)):
            blocks.append([int(row), int(column), int(block_inside[row, column]),
                           round(float(block_crease[row, column]) / float(block_pairs[row, column]), 5)])
        record["blocks"] = blocks
        record["block_count"] = len(blocks)
    return records, failures


def compare_baseline(current, baseline):
    """範囲の記録を前の基準と比べる（どちらも measure_regions の記録）。差の辞書"""
    result = {"delta": {}}
    for key in ("shading_diff", "grid_limited", "lod_diff", "cliff_diff"):
        if current.get(key) is not None and baseline.get(key) is not None:
            result["delta"][key] = current[key] - baseline[key]
    crease_delta = {}
    for name, value in (current.get("crease") or {}).items():
        before = (baseline.get("crease") or {}).get(name)
        if value is not None and before is not None:
            crease_delta[name] = value - before
    result["delta"]["crease"] = crease_delta
    if current.get("primary_set") != baseline.get("primary_set"):
        result["local_regression"] = None
        result["local_note"] = f"主の組が違う（今 {current.get('primary_set')}・基準 {baseline.get('primary_set')}）"
        return result
    before_blocks = {(row, column): value for row, column, _, value in baseline.get("blocks") or []}
    compared = 0
    dropped = 0
    for row, column, _, value in current.get("blocks") or []:
        before = before_blocks.get((row, column))
        if before is None:
            continue
        compared += 1
        if before - value >= kLocalDropPoints - 1e-12:
            dropped += 1
    result["blocks_compared"] = compared
    result["blocks_dropped"] = dropped
    result["local_regression"] = (float(dropped) / compared) if compared else None
    return result


def lookup(entry, path):
    value = entry
    for key in path.split("."):
        if not isinstance(value, dict) or key not in value:
            return None
        value = value[key]
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) else None


def evaluate_expect(spec_path, report_views, regions, selected_views, views_known):
    """--expect の行を範囲の記録で判定する。失敗の一覧"""
    if not os.path.isfile(spec_path):
        return [f"仕様 {spec_path} が無い"]
    try:
        with open(spec_path, encoding="utf-8-sig") as f:
            spec = json.load(f)
    except (OSError, ValueError) as error:
        return [f"仕様 {spec_path} が読めない: {error}"]
    rows = spec.get("rows") if isinstance(spec, dict) else spec
    if not isinstance(rows, list) or not rows:
        return [f"仕様 {spec_path} に行が無い"]
    by_id = {region["id"]: region for region in regions}
    failures = []
    evaluated = 0
    for index, row in enumerate(rows):
        label = f"行 {index + 1}"
        if not isinstance(row, dict):
            failures.append(f"{label} が JSON のオブジェクトでない")
            continue
        unknown = [key for key in row if key not in EXPECT_KEYS]
        if unknown:
            failures.append(f"{label} に知らない鍵 {unknown}")
            continue
        region_id, view, quantity = row.get("region"), row.get("view"), row.get("quantity")
        if region_id not in by_id:
            failures.append(f"{label} の範囲 {region_id} が未知")
            continue
        if view not in views_known:
            failures.append(f"{label} の視点 {view} が未知")
            continue
        if view not in by_id[region_id]["views"]:
            failures.append(f"{label} の視点 {view} は範囲 {region_id} の視点に無い")
            continue
        if not quantity or ("min" not in row and "max" not in row):
            failures.append(f"{label} に quantity か min・max が無い")
            continue
        if view not in selected_views:
            continue
        evaluated += 1
        record = (report_views.get(view) or {}).get(region_id)
        if record is None or record.get("insufficient"):
            failures.append(f"{label}: 範囲 {region_id} の視点 {view} の記録が無いか画素が足りない")
            continue
        value = lookup(record, quantity)
        if value is None:
            failures.append(f"{label}: 範囲 {region_id} の視点 {view} の {quantity} の値が無い")
            continue
        low, high = row.get("min"), row.get("max")
        ok = (low is None or value >= low) and (high is None or value <= high)
        print(f"SHAPE_REGION_EXPECT region={region_id} view={view} quantity={quantity} value={value:.5f} "
              f"min={low} max={high} result={'pass' if ok else 'fail'}")
        if not ok:
            failures.append(f"{label}: 範囲 {region_id} の視点 {view} の {quantity} = {value:.5f} が [{low}, {high}] の外")
    if not evaluated:
        failures.append(f"仕様 {spec_path} に選んだ視点の行が無い（空の選択）")
    return failures


def capture_sets(root):
    return [name for name in ShapeCause.SETS if os.path.isfile(os.path.join(root, name, "metrics.json"))]


def manifest_files(set_dir):
    files = {}
    for directory, _, names in os.walk(set_dir):
        for name in names:
            path = os.path.join(directory, name)
            files[os.path.relpath(path, set_dir).replace("\\", "/")] = path
    return files


def record_baseline(RC, root):
    """基準の記録を書く（既にあれば書き換えない）"""
    path = os.path.join(root, MANIFEST)
    if os.path.isfile(path):
        print(f"REFERENCE_CHECK result=fail reason=baseline_manifest_exists path={RC.rel_dir(path)}"
              "（基準は撮り直さない。撮り直すときは置き場ごと替える）")
        return 1
    sets = capture_sets(root)
    if not sets:
        print(f"REFERENCE_CHECK result=fail reason=no_sets path={RC.rel_dir(root)}")
        return 1
    try:
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=RC.REPO_ROOT, capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        head = None
    manifest = {"format": MANIFEST_FORMAT, "recorded_at": datetime.datetime.now().isoformat(timespec="seconds"),
                "git_head": head, "sets": {}}
    for name in sets:
        set_dir = os.path.join(root, name)
        with open(os.path.join(set_dir, "inputs.json"), encoding="utf-8") as f:
            inputs = json.load(f)
        files = {rel: RC.sha256_file(full) for rel, full in sorted(manifest_files(set_dir).items())}
        manifest["sets"][name] = {"files": files, "inputs": inputs}
        print(f"SHAPE_BASELINE_RECORD set={name} files={len(files)} game_exe_sha256={inputs.get('game_exe_sha256')}")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
    print(f"REFERENCE_CHECK shape_baseline_record result=pass manifest={RC.rel_dir(path)}")
    return 0


def check_baseline(RC, root):
    """基準の記録とファイルを照らす。(組の一覧, 失敗の一覧)"""
    path = os.path.join(root, MANIFEST)
    if not os.path.isfile(path):
        return [], [f"基準の記録 {RC.rel_dir(path)} が無い（--baseline-record で書く）"]
    with open(path, encoding="utf-8") as f:
        manifest = json.load(f)
    if manifest.get("format") != MANIFEST_FORMAT:
        return [], [f"基準の記録の形式 {manifest.get('format')} を知らない"]
    failures = []
    sets = list((manifest.get("sets") or {}).keys())
    if not sets:
        failures.append("基準の記録に組が無い")
    for name in sets:
        entry = manifest["sets"][name]
        set_dir = os.path.join(root, name)
        present = manifest_files(set_dir) if os.path.isdir(set_dir) else {}
        recorded = entry.get("files") or {}
        mismatched = [rel for rel, digest in recorded.items()
                      if rel not in present or RC.sha256_file(present[rel]) != digest]
        extra = sorted(set(present) - set(recorded))
        if mismatched:
            failures.append(f"基準の組 {name} の撮影の出力が記録と違うか無い: {', '.join(sorted(mismatched)[:8])}"
                            + (f" ほか {len(mismatched) - 8}" if len(mismatched) > 8 else ""))
        if extra:
            failures.append(f"基準の組 {name} に記録の無いファイルがある: {', '.join(extra[:8])}")
        inputs_path = os.path.join(set_dir, "inputs.json")
        if os.path.isfile(inputs_path):
            with open(inputs_path, encoding="utf-8") as f:
                if json.load(f) != entry.get("inputs"):
                    failures.append(f"基準の組 {name} の inputs.json が当時の入力の記録と違う")
        print(f"SHAPE_BASELINE_INTEGRITY set={name} files={len(recorded)} mismatched={len(mismatched)} extra={len(extra)} "
              f"game_exe_sha256={(entry.get('inputs') or {}).get('game_exe_sha256')}")
    return sets, failures


def check_sets(RC, root, sets, verify):
    """組の撮影の条件（起動引数・失敗の記録・崖錐の岩の制限を外した印・Game.exe）。(組の記録, 失敗の一覧)"""
    failures = []
    records = {}
    for name in sets:
        set_dir = os.path.join(root, name)
        inputs_path = os.path.join(set_dir, "inputs.json")
        if not os.path.isfile(inputs_path):
            failures.append(f"組 {name} の inputs.json が無い")
            continue
        with open(os.path.join(set_dir, "metrics.json"), encoding="utf-8-sig") as f:
            metrics = json.load(f)
        with open(inputs_path, encoding="utf-8") as f:
            inputs = json.load(f)
        records[name] = {"game_args": metrics.get("game_args"), "game_exe_sha256": inputs.get("game_exe_sha256")}
        if sorted(metrics.get("game_args") or []) != sorted(ShapeCause.SETS[name]):
            failures.append(f"組 {name} の起動引数 {metrics.get('game_args')} が組の条件 {ShapeCause.SETS[name]} と違う")
        if metrics.get("failures"):
            failures.append(f"組 {name} の撮影が失敗を記録している: {metrics.get('failures')}")
        if verify:
            ok, reasons, _ = RC.check_inputs(set_dir)
            if not ok:
                failures.extend(f"組 {name}: {reason}" for reason in reasons)
        talus_off = None
        for view in ShapeCause.VIEWS:
            log_name = RC.view_game_log(set_dir, view)
            log_path = os.path.join(set_dir, log_name) if log_name else ""
            if log_path and os.path.isfile(log_path):
                with open(log_path, encoding="utf-8", errors="replace") as f:
                    log_text = f.read()
                talus_off = "WASTELAND_TALUS_GRAY_LIMIT off" in log_text and "WASTELAND_TALUS_GRAY_LIMIT radius_m" not in log_text
                break
        records[name]["talus_limit_off"] = talus_off
        if not talus_off:
            failures.append(f"組 {name} のログに崖錐の岩の制限を外した印（WASTELAND_TALUS_GRAY_LIMIT off）が無い")
    exes = {entry.get("game_exe_sha256") for entry in records.values()}
    if len(exes) != 1 or None in exes:
        failures.append(f"組の Game.exe が揃っていない: {sorted(str(value) for value in exes)}")
    return records, failures


def measure_root(RC, levels, root, regions, views):
    """置き場の全視点を測る。(視点 → 範囲の記録, 視点 → 主の組の PNG と配列, 失敗の一覧)"""
    sets = capture_sets(root)
    failures = []
    report = {}
    previews = {}
    for view in views:
        data = {}
        for name in sets:
            entry, reason = load_set_view(RC, os.path.join(root, name), view)
            if entry is None:
                failures.append(f"{RC.rel_dir(root)} の組 {name} の視点 {view}: {reason}")
                continue
            data[name] = entry
        if len(data) != len(sets):
            continue
        shapes = {entry["terrain"].shape for entry in data.values()}
        if len(shapes) != 1:
            failures.append(f"視点 {view} の組の大きさが違う: {sorted(shapes)}")
            continue
        records, view_failures = measure_regions(levels, view, regions, data)
        failures.extend(view_failures)
        report[view] = records
        previews[view] = (os.path.join(root, primary_set([n for n in ShapeCause.SETS if n in data]), f"{view}.png"),
                          data[primary_set([n for n in ShapeCause.SETS if n in data])])
    return report, previews, failures


def write_overlay(path, png_path, entry, regions, view, records):
    """範囲の重ね画: 範囲の地形の画素を範囲の色で塗り、範囲の中の地形でない画素は暗くする。範囲の ID を画素の重心に書く"""
    image = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.float32)
    out = image * 0.55
    labels = []
    for index, region in enumerate(regions):
        if view not in region["views"]:
            continue
        color = np.asarray(REGION_COLORS[index % len(REGION_COLORS)], dtype=np.float32)
        rect = region_rect(entry["position"], region)
        terrain = rect & entry["valid"] & entry["terrain"]
        other = rect & entry["valid"] & ~entry["terrain"]
        out[terrain] = image[terrain] * 0.35 + color * 0.65
        out[other] = image[other] * 0.15
        if np.any(terrain):
            rows, columns = np.nonzero(terrain)
            record = records.get(region["id"]) or {}
            labels.append((int(np.median(columns)), int(np.median(rows)), tuple(int(c) for c in color),
                           f"{region['id']} {record.get('terrain_pixels', 0)}px"))
    sheet = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
    draw = ImageDraw.Draw(sheet)
    try:
        from CompareLight import load_font
        font = load_font(22)
    except Exception:
        font = None
    for x, y, color, text in labels:
        draw.rectangle((x - 4, y - 4, x + 12 * len(text), y + 26), fill=(0, 0, 0))
        draw.text((x, y), text, fill=color, font=font)
    draw.rectangle((0, 0, 1100, 30), fill=(0, 0, 0))
    draw.text((6, 3), f"{view}: 範囲の地形の画素 = 範囲の色、範囲の中の地形でない画素 = 暗く", fill=(255, 255, 255), font=font)
    sheet.save(path)


def run(RC, root, regions_path, views_arg, verify, expect, baseline, integrity):
    views_known = ShapeCause.VIEWS
    failures = []
    if not root or not os.path.isdir(root):
        print(f"REFERENCE_CHECK result=fail reason=capture_root_missing path={root}")
        return 1
    regions, region_failures = load_regions(regions_path, views_known)
    if regions is None:
        for failure in region_failures:
            print("FAIL " + failure)
        print("REFERENCE_CHECK shape_regions result=fail reason=regions_invalid")
        return 1
    region_views = [view for view in views_known if any(view in region["views"] for region in regions)]
    views = [v.strip() for v in views_arg.split(",") if v.strip()] if views_arg else region_views
    unknown = [view for view in views if view not in views_known]
    if unknown:
        failures.append(f"未知の視点 {unknown}")
    views = [view for view in views if view in views_known]
    if not views:
        failures.append("選んだ視点が無い（空の選択）")
    levels = ShapeCause.load_levels(RC)
    report = {"regions_file": RC.rel_dir(regions_path), "regions_sha256": RC.sha256_file(regions_path), "views": {},
              "thresholds": {"block_pixels": kBlockPixels, "block_min_share": kBlockMinShare,
                             "local_drop_points": kLocalDropPoints, "crease_degrees": ShapeCause.kCreaseDegrees,
                             "diff_degrees": ShapeCause.kDiffDegrees, "shading_degrees": ShapeCause.kShadingDegrees}}
    if integrity:
        sets, integrity_failures = check_baseline(RC, root)
        failures.extend(integrity_failures)
        report["baseline_integrity"] = {"sets": sets, "pass": not integrity_failures}
        # 基準の検査は今の実装との一致を求めないので、入力のハッシュ（inputs.json と今のファイル）は照らさない
        set_records, set_failures = check_sets(RC, root, capture_sets(root), False)
    else:
        set_records, set_failures = check_sets(RC, root, capture_sets(root), verify)
    failures.extend(set_failures)
    report["sets"] = set_records
    if not set_records:
        failures.append(f"置き場 {RC.rel_dir(root)} に組（{', '.join(ShapeCause.SETS)}）の撮影が無い")
    baseline_report = None
    if baseline:
        _, baseline_failures = check_baseline(RC, baseline)
        failures.extend(f"前の基準: {failure}" for failure in baseline_failures)
        baseline_sets, baseline_set_failures = check_sets(RC, baseline, capture_sets(baseline), False)
        failures.extend(f"前の基準: {failure}" for failure in baseline_set_failures)
        if not baseline_failures:
            baseline_report, _, measure_failures = measure_root(RC, levels, baseline, regions, views)
            failures.extend(f"前の基準: {failure}" for failure in measure_failures)
        report["baseline"] = {"root": RC.rel_dir(baseline), "sets": baseline_sets}
    if not failures or (set_records and views):
        current, previews, measure_failures = measure_root(RC, levels, root, regions, views)
        failures.extend(measure_failures)
        os.makedirs(os.path.join(root, PREVIEW_DIR), exist_ok=True)
        for view, records in current.items():
            if baseline_report is not None:
                for region_id, record in records.items():
                    before = (baseline_report.get(view) or {}).get(region_id)
                    if before and not before.get("insufficient") and not record.get("insufficient"):
                        record["baseline"] = compare_baseline(record, before)
            png_path, entry = previews[view]
            preview = os.path.join(root, PREVIEW_DIR, f"{view}.png")
            write_overlay(preview, png_path, entry, regions, view, records)
            report["views"][view] = {"preview": RC.rel_dir(preview), "regions": records}
            for region_id, record in records.items():
                if record.get("insufficient"):
                    continue
                crease = " ".join(f"crease_{name}={value * 100.0:.1f}%" for name, value in record["crease"].items()
                                  if value is not None)
                extra = ""
                if "baseline" in record:
                    regression = record["baseline"].get("local_regression")
                    extra = (f" local_regression={'none' if regression is None else f'{regression * 100.0:.1f}%'}"
                             f" blocks_compared={record['baseline'].get('blocks_compared')}")
                print(f"SHAPE_REGION view={view} region={region_id} terrain={record['terrain_pixels']} "
                      f"other={record['other_pixels']} blocks={record['block_count']} {crease} "
                      f"shading_diff={record['shading_diff'] * 100.0:.1f}% grid_limited={record['grid_limited'] * 100.0:.1f}%"
                      + extra)
        if expect:
            failures.extend(evaluate_expect(expect, {view: entry["regions"] for view, entry in report["views"].items()},
                                            regions, views, views_known))
    report["failures"] = failures
    report["pass"] = not failures
    with open(os.path.join(root, REPORT), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print("FAIL " + failure)
    print(f"REFERENCE_CHECK shape_regions views={len(report['views'])} regions={len(regions)} "
          f"sets={len(set_records)} result={'pass' if not failures else 'fail'} report={RC.rel_dir(os.path.join(root, REPORT))}")
    return 0 if not failures else 1


def synthetic_entry(size, terrain, normal):
    """合成の視点の配列（位置は X = 列 × 0.25 m・Z = 行 × 0.25 m の平らな面、輪郭なし）"""
    rows, columns = np.mgrid[0:size, 0:size].astype(np.float64)
    position = np.stack([columns * 0.25, np.zeros_like(rows), rows * 0.25], axis=2)
    valid = np.ones((size, size), dtype=bool)
    valid[:ShapeCause.kStride, :] = valid[-ShapeCause.kStride:, :] = False
    valid[:, :ShapeCause.kStride] = valid[:, -ShapeCause.kStride:] = False
    return {"position": position, "geometric": normal, "shading": normal.copy(), "valid": valid,
            "footprint": np.full((size, size), 0.01), "camera": {"position": [0.0, 100.0, 0.0]}, "coverage": 1.0,
            "terrain": terrain}


def sawtooth(size, columns_mask):
    """列ごとに ±40° で傾く法線（4 画素ごとに入れ替わり、2 × kStride 画素離れた法線が 80° 違う。折れ目になる）。columns_mask の外は上向き"""
    normal = np.zeros((size, size, 3))
    normal[:, :, 1] = 1.0
    tilt = np.radians(40.0)
    sign = np.where((np.arange(size) // 4) % 2 == 0, 1.0, -1.0)[None, :].repeat(size, axis=0)
    normal[columns_mask, 0] = np.sin(tilt) * sign[columns_mask]
    normal[columns_mask, 1] = np.cos(tilt)
    return normal


def run_selftest():
    """地形の印の外の画素を数えないこと・画素の不足で落ちることを合成の配列で確かめる"""
    size = 128
    levels = [("near", 1.0, -1e9, 1e9, -1e9, 1e9)]
    columns = np.arange(size)[None, :].repeat(size, axis=0)
    terrain = columns < 64
    region = {"id": "selftest", "x": [0.0, 31.75], "z": [0.0, 31.75], "views": {"ref_cliff_01": 1}}
    failures = []
    expected_terrain = int(np.count_nonzero(synthetic_entry(size, terrain, sawtooth(size, ~terrain))["valid"] & terrain))

    def measure(normal, minimum):
        entry = synthetic_entry(size, terrain, normal)
        probe = dict(region, views={"ref_cliff_01": minimum})
        return measure_regions(levels, "ref_cliff_01", [probe], {"lod-default": entry})

    # (1) 地形でない画素（右半分）だけを鋸歯の折れにする: 範囲の折れ目は 0、地形の画素の数は地形の印の数だけ
    records, measure_failures = measure(sawtooth(size, ~terrain), 1)
    record = records["selftest"]
    crease = record["crease"]["lod-default"]
    ok_outside = (not measure_failures and crease == 0.0 and record["terrain_pixels"] == expected_terrain and
                  record["other_pixels"] > 0 and all(block[3] == 0.0 for block in record["blocks"]))
    print(f"SHAPE_REGION_SELFTEST case=non_terrain_creases_ignored crease={crease} terrain={record['terrain_pixels']} "
          f"expected_terrain={expected_terrain} other={record['other_pixels']} result={'pass' if ok_outside else 'fail'}")
    if not ok_outside:
        failures.append("地形の印の外の画素の折れが範囲の量に数えられた")
    # (2) 対照: 地形の画素（左半分）を鋸歯にすると折れ目が出る（測りが折れを拾えること）
    records, _ = measure(sawtooth(size, terrain), 1)
    crease_terrain = records["selftest"]["crease"]["lod-default"]
    ok_control = crease_terrain is not None and crease_terrain > 0.5
    print(f"SHAPE_REGION_SELFTEST case=terrain_creases_counted crease={crease_terrain} result={'pass' if ok_control else 'fail'}")
    if not ok_control:
        failures.append("地形の画素の折れが範囲の量に数えられない（対照）")
    # (3) 最低の画素数がちょうどなら通り、1 画素多いと落ちる
    _, exact_failures = measure(sawtooth(size, ~terrain), expected_terrain)
    records, short_failures = measure(sawtooth(size, ~terrain), expected_terrain + 1)
    ok_short = (not exact_failures and len(short_failures) == 1 and "足りない" in short_failures[0] and
                records["selftest"].get("insufficient") is True)
    print(f"SHAPE_REGION_SELFTEST case=insufficient_pixels exact_failures={len(exact_failures)} "
          f"short_failures={len(short_failures)} result={'pass' if ok_short else 'fail'}")
    if not ok_short:
        failures.append("範囲の地形の画素の不足で落ちない（か、ちょうどで落ちる）")
    for failure in failures:
        print("FAIL " + failure)
    print(f"REFERENCE_CHECK shape_regions_selftest cases=3 result={'pass' if not failures else 'fail'}")
    return 0 if not failures else 1
