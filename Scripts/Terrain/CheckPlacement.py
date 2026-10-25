# 置いた物（崖錐の岩・草と低木・石と礫）の浮きと埋もれを、焼いた地形の面（支持面）で測る。
# 置き方（Game/GameModes/Wasteland の WastelandTalus.cpp・WastelandFoliage.cpp・WastelandGravel.cpp）は 1 m の格子の高さ
# （TerrainHeightField::SampleHeight）を読むので、崖の生成が面を押し出した範囲では焼いた面とずれうる。
#
# 入力: Game の --wasteland-export-placement=<path> が書く配置の JSON（WastelandPlacementExport.h）。崖錐の岩は置いた全部、
# 草と低木・石と礫は書き出しの sampling が示す範囲の物。
# 足元の点（どれも回転・拡大率を掛けたワールドの位置）:
#   - 岩: 書き出したメッシュの頂点を置いた向きに回し、高さの下 5 % の帯に入る頂点の全部
#   - 草と低木・石: 根元の高さ（置き方が地面と見なした高さ）の水平の円の 6 点。半径はメッシュの根元の円の半径 × 横の拡大率
# 支持面: 足元の点ごとに、焼いた面の問い合わせ（AssetCook --sample-cooked-terrain）の交点のうち上を向く面（法線の上の成分
# SUPPORT_MIN_NORMAL_Y 以上。張り出しの下面は支持面にしない）で、足元の高さとの差が max(0.5 m, 物の高さの 20 %) 以内で
# 最も近い面。差 d = 支持面の高さ − 足元の高さ（正なら点は面の下）。
# 物ごとの値は足元の点ごとの量の最大: 浮き = 点ごとの max(0, −d) の最大、埋もれ = 点ごとの max(0, d) の最大。
# 支持面の無い足元の点が 1 つでもあれば「支持面なし」の違反（窓を外した最も近い上を向く面で「窓より深く埋まる」
# 「窓より高く浮く」「上を向く面が無い」に分けて数える。分けるのは最も悪い点。埋まる > 浮く > 面が無い の順）。
# 規則: 岩は浮き < 0.05 m・埋もれ ≤ 物の高さの 50 %。草と低木・石は浮き ≤ 0.05 m・埋もれ ≤ 0.1 m。
# 落とす（測り切れない）もの: 足元の点の問い合わせが ok でない物（DEM の格子の外・区画が無い・メッシュが壊れている・
# 交点が無い）、種類ごとの物が 0 個、形の欠け（岩の頂点が無い・下の帯に頂点が無い・草と石のメッシュの頂点が 0）。
#
# 使い方:
#   python Scripts/Terrain/CheckPlacement.py --placement <placement.json> [--asset-cook <AssetCook.exe>]
#       [--manifest <manifest.json>] --regions Scripts/Terrain/ShapeRegions.json (--record | --require-compliant)
#       [--removed <removed.json>] [--out <report.json>] [--workers N] [--verify]
#   --record: 全部の物を測り切れたら通る（違反の数は範囲ごとに書くだけ）
#   --require-compliant: 加えて違反 0。物を消して通すなら、消した物の一覧（--removed。{"removed": [{"type", "x", "z"}, ...]}）を
#     渡し、範囲ごと（名前の付いた範囲・どの範囲にも入らない所・全体）に消した数が、その範囲の物（残した物 + 消した物）の
#     REMOVED_MAX_SHARE 以下
# 終了コード: --verify で合格なら 0、不合格なら 1。--verify なしは値を出すだけで 0。
import argparse
import concurrent.futures
import json
import math
import os
import subprocess
import sys
import tempfile

import numpy as np

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_ASSET_COOK = os.path.join(REPO_ROOT, "build", "Tools", "AssetCook", "RelWithDebInfo", "AssetCook.exe")
DEFAULT_MANIFEST = os.path.join(REPO_ROOT, "build", "CookedAssets", "manifest.json")
DEM_DESCRIPTOR = os.path.join(REPO_ROOT, "Assets", "Terrain", "ValleyOfTheGods", "height.json")
# DEM の区画の一辺の升目の数（TerrainHeightField.h の kDemTileCells）。問い合わせを区画ごとにまとめるのに使う
DEM_TILE_CELLS = 500

TYPES = ("talus", "plant", "gravel")
ROCK_BOTTOM_BAND = 0.05
ROOT_RING_POINTS = 6
SUPPORT_MIN_NORMAL_Y = 0.3
SUPPORT_WINDOW_MIN_M = 0.5
SUPPORT_WINDOW_SHARE = 0.2
ROCK_MAX_FLOAT_M = 0.05
ROCK_MAX_BURIAL_SHARE = 0.5
SCATTER_MAX_FLOAT_M = 0.05
SCATTER_MAX_BURIAL_M = 0.1
REMOVED_MAX_SHARE = 0.02
# 問い合わせ 1 回の点の数の上限と、並べて走らせる AssetCook の数
QUERY_CHUNK_POINTS = 300000
DEFAULT_WORKERS = 8
# 範囲ごとに書く値の悪い物の数と、報告に書く測り切れない物の数
WORST_PER_REGION = 10
UNMEASURED_LISTED = 200
ROCK_CHUNK = 4000
# 足元の点の支持面なしの分類（大きいほど悪い。物ごとに最も悪い点で分ける）
NO_SUPPORT_NONE, NO_SUPPORT_NO_UPWARD, NO_SUPPORT_FLOATING, NO_SUPPORT_BURIED = 0, 1, 2, 3


def load_json(path):
    with open(path, encoding="utf-8-sig") as handle:
        return json.load(handle)


def rotate_conjugate(q, v):
    """v（..., 3）を四元数 q（..., 4。x y z w）の共役で回す（崖錐の岩の散布の写し方）"""
    u = -q[..., :3]
    w = q[..., 3:4]
    t = 2.0 * np.cross(u, v)
    return v + w * t + np.cross(u, t)


def rock_foot_points(points, q, s, pos):
    """岩の足元の点（下の帯の頂点の全部。M, 3）・持ち主の行（M。q の行の番号）・回した高さ（N）"""
    offsets = rotate_conjugate(q[:, None, :], points[None, :, :] * s[:, None, :])
    y = offsets[..., 1]
    min_y = y.min(axis=1)
    height = y.max(axis=1) - min_y
    in_band = y <= (min_y + ROCK_BOTTOM_BAND * height)[:, None]
    rows, cols = np.nonzero(in_band)
    return offsets[rows, cols] + pos[rows], rows, height


def ring_points(objects, meshes):
    """草と石の根元の円の点（N, 6, 3）と物の高さ（N）"""
    mesh_index = objects[:, 0].astype(np.int64)
    radius = np.array([m.get("root_radius", 0.0) for m in meshes])[mesh_index] * objects[:, 8]
    height = np.array([m.get("height", 0.0) for m in meshes])[mesh_index] * objects[:, 9]
    # 四元数 (0, −sin(yaw/2), 0, cos(yaw/2)) から yaw を戻す
    yaw = 2.0 * np.arctan2(-objects[:, 5], objects[:, 7])
    angle = yaw[:, None] + np.arange(ROOT_RING_POINTS)[None, :] * (2.0 * math.pi / ROOT_RING_POINTS)
    foot = np.empty((len(objects), ROOT_RING_POINTS, 3))
    foot[..., 0] = objects[:, 1:2] + radius[:, None] * np.cos(angle)
    foot[..., 1] = objects[:, 11:12]
    foot[..., 2] = objects[:, 3:4] + radius[:, None] * np.sin(angle)
    return foot, height


def run_query(asset_cook, manifest, xz):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as handle:
        json.dump({"points": xz.tolist()}, handle)
        path = handle.name
    try:
        result = subprocess.run([asset_cook, "--sample-cooked-terrain", "--manifest", manifest, "--points", path],
                                capture_output=True, cwd=REPO_ROOT)
    finally:
        os.remove(path)
    if result.returncode != 0:
        raise RuntimeError(f"AssetCook が失敗しました（{result.returncode}）: "
                           f"{result.stderr.decode('mbcs', errors='replace').strip()}")
    data = json.loads(result.stdout.decode("utf-8", errors="strict"))
    if data.get("format") != "cooked_terrain_samples.v1" or len(data["points"]) != len(xz):
        raise RuntimeError("問い合わせの結果の形が違います")
    return data["points"]


def query_chunks(foot):
    """問い合わせを DEM の区画ごとにまとめた点の番号の組"""
    dem = load_json(DEM_DESCRIPTOR)
    first_x = float(dem["first_vertex_engine"]["x"])
    first_z = float(dem["first_vertex_engine"]["z"])
    spacing = float(dem["vertex_spacing_m"])
    tile_x = np.floor((foot[:, 0] - first_x) / (spacing * DEM_TILE_CELLS)).astype(np.int64)
    tile_z = np.floor((foot[:, 2] - first_z) / (spacing * DEM_TILE_CELLS)).astype(np.int64)
    order = np.lexsort((tile_x, tile_z))
    keys = tile_z[order] * 100000 + tile_x[order]
    chunks = []
    start = 0
    while start < len(order):
        end = min(start + QUERY_CHUNK_POINTS, len(order))
        # 区画の切れ目で切る（1 回の問い合わせが読む区画を減らす）
        boundary = np.searchsorted(keys, keys[start], side="right")
        if boundary > start and boundary < end:
            end = boundary
        chunks.append(order[start:end])
        start = end
    return chunks


def query_support(asset_cook, manifest, foot, window, workers):
    """足元の点（P, 3）と窓（P）から、点ごとの差 d（窓の内の支持面。無ければ NaN）・窓を外した最も近い上を向く面との差
    （上を向く面が無ければ NaN）・問い合わせの状態（P。文字列）を返す"""
    diff = np.full(len(foot), np.nan)
    nearest = np.full(len(foot), np.nan)
    status = np.empty(len(foot), dtype=object)

    def work(index):
        return index, run_query(asset_cook, manifest, foot[index][:, [0, 2]])

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for index, points in pool.map(work, query_chunks(foot)):
            for local, point in enumerate(points):
                row = index[local]
                status[row] = point["status"]
                if point["status"] != "ok":
                    continue
                best = None
                for hit in point["hits"]:
                    if hit["normal"][1] < SUPPORT_MIN_NORMAL_Y:
                        continue
                    d = hit["height"] - foot[row, 1]
                    if best is None or abs(d) < abs(best):
                        best = d
                if best is not None:
                    nearest[row] = best
                    if abs(best) <= window[row]:
                        diff[row] = best
    return diff, nearest, status


def percentiles(values):
    if len(values) == 0:
        return None
    p = np.percentile(values, [50, 90, 99])
    return {"p50": round(float(p[0]), 4), "p90": round(float(p[1]), 4), "p99": round(float(p[2]), 4),
            "max": round(float(np.max(values)), 4)}


def collect_foot_points(objects, meshes, obj_type, failures):
    """足元の点（P, 3）・持ち主の物の番号（P）・物の高さ（物の数）"""
    foot_list, owner_list = [], []
    obj_height = np.zeros(len(objects))
    for i, t in enumerate(TYPES):
        rows = np.nonzero(obj_type == i)[0]
        if len(rows) == 0:
            continue
        if t == "talus":
            for start in range(0, len(rows), ROCK_CHUNK):
                part = rows[start:start + ROCK_CHUNK]
                for mesh_index in np.unique(objects[part, 0].astype(np.int64)):
                    sub = part[objects[part, 0].astype(np.int64) == mesh_index]
                    points = np.array(meshes[mesh_index].get("points") or [], dtype=np.float64).reshape(-1, 3)
                    if len(points) == 0:
                        continue
                    foot, local, height = rock_foot_points(points, objects[sub, 4:8], objects[sub, 8:11],
                                                           objects[sub, 1:4])
                    foot_list.append(foot)
                    owner_list.append(sub[local])
                    obj_height[sub] = height
        else:
            foot, height = ring_points(objects[rows], meshes)
            foot_list.append(foot.reshape(-1, 3))
            owner_list.append(np.repeat(rows, ROOT_RING_POINTS))
            obj_height[rows] = height
    if not foot_list:
        return np.zeros((0, 3)), np.zeros(0, dtype=np.int64), obj_height
    foot = np.concatenate(foot_list)
    owner = np.concatenate(owner_list)
    points_per_object = np.bincount(owner, minlength=len(objects))
    missing = int((points_per_object == 0).sum())
    if missing:
        failures.append(f"足元の点が無い物が {missing} 個あります（形の頂点の欠け）")
    return foot, owner, obj_height


def per_object_max(owner, values, count):
    """点ごとの値（NaN は数えない）の物ごとの最大。値のある点が無い物は NaN"""
    out = np.full(count, -np.inf)
    valid = ~np.isnan(values)
    np.maximum.at(out, owner[valid], values[valid])
    out[np.isinf(out)] = np.nan
    return out


def region_masks(regions, center_x, center_z, count):
    """（範囲の名前, 物の選び, 消した物の選びの関数）の組。名前の付いた範囲・どの範囲にも入らない所・全体"""
    def inside_box(region, x, z):
        return (x >= region["x"][0]) & (x <= region["x"][1]) & (z >= region["z"][0]) & (z <= region["z"][1])

    rows = [("all", np.ones(count, dtype=bool), lambda x, z: np.ones(len(x), dtype=bool))]
    inside_any = np.zeros(count, dtype=bool)
    for region in regions:
        inside = inside_box(region, center_x, center_z)
        inside_any |= inside
        rows.append((region["id"], inside, lambda x, z, region=region: inside_box(region, x, z)))

    def outside_all(x, z):
        hit = np.zeros(len(x), dtype=bool)
        for region in regions:
            hit |= inside_box(region, x, z)
        return ~hit

    rows.append(("outside_regions", ~inside_any, outside_all))
    return rows


def main():
    parser = argparse.ArgumentParser(description="置いた物の浮きと埋もれを焼いた面の支持面で測る")
    parser.add_argument("--placement", required=True)
    parser.add_argument("--asset-cook", default=DEFAULT_ASSET_COOK)
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--regions", required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--record", action="store_true")
    mode.add_argument("--require-compliant", action="store_true")
    parser.add_argument("--removed")
    parser.add_argument("--out")
    parser.add_argument("--workers", type=int, default=DEFAULT_WORKERS)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()

    failures = []
    placement = load_json(args.placement)
    meshes = placement["meshes"]
    objects = np.array(placement["objects"], dtype=np.float64).reshape(-1, 12)
    regions = load_json(args.regions)["regions"]
    mesh_type = np.array([TYPES.index(m["type"]) for m in meshes])
    obj_type = mesh_type[objects[:, 0].astype(np.int64)] if len(objects) else np.zeros(0, dtype=np.int64)

    # 形の欠けと物の数
    for mesh in meshes:
        if mesh["type"] == "talus":
            if not mesh.get("available", False) or len(mesh.get("points", [])) == 0:
                failures.append(f"岩の形 {mesh['name']} の頂点がありません")
        elif mesh.get("vertices", 0) == 0 or mesh.get("height", 0.0) <= 0.0:
            failures.append(f"形 {mesh['name']}（{mesh['type']}）のメッシュがありません")
    counts = {t: int((obj_type == i).sum()) for i, t in enumerate(TYPES)}
    for name, count in counts.items():
        if count == 0:
            failures.append(f"{name} の物が 0 個です")

    # 足元の点と問い合わせ
    foot, owner, obj_height = collect_foot_points(objects, meshes, obj_type, failures)
    if len(foot) == 0:
        failures.append("足元の点がありません")
        return finish(args, failures, {"objects": counts}, None)
    window = np.maximum(SUPPORT_WINDOW_MIN_M, SUPPORT_WINDOW_SHARE * obj_height[owner])
    diff, nearest, status = query_support(os.path.abspath(args.asset_cook), os.path.abspath(args.manifest), foot, window,
                                          args.workers)
    statuses = {}
    for value in status:
        statuses[value] = statuses.get(value, 0) + 1
    point_ok = status == "ok"

    # 物ごとの値（足元の点ごとの量の最大）
    n = len(objects)
    point_count = np.bincount(owner, minlength=n)
    measured = (np.bincount(owner, weights=point_ok.astype(np.float64), minlength=n) == point_count) & (point_count > 0)
    float_m = per_object_max(owner, np.maximum(0.0, -diff), n)
    burial_m = per_object_max(owner, np.maximum(0.0, diff), n)
    point_class = np.where(~point_ok | ~np.isnan(diff), NO_SUPPORT_NONE,
                           np.where(np.isnan(nearest), NO_SUPPORT_NO_UPWARD,
                                    np.where(nearest > 0, NO_SUPPORT_BURIED, NO_SUPPORT_FLOATING)))
    obj_class = np.zeros(n, dtype=np.int64)
    np.maximum.at(obj_class, owner, point_class)
    center_x = np.bincount(owner, weights=foot[:, 0], minlength=n) / np.maximum(point_count, 1)
    center_z = np.bincount(owner, weights=foot[:, 2], minlength=n) / np.maximum(point_count, 1)

    is_rock = obj_type == 0
    burial_limit = np.where(is_rock, ROCK_MAX_BURIAL_SHARE * obj_height, SCATTER_MAX_BURIAL_M)
    with np.errstate(invalid="ignore"):
        v_float = measured & np.where(is_rock, float_m >= ROCK_MAX_FLOAT_M, float_m > SCATTER_MAX_FLOAT_M)
        v_burial = measured & (burial_m > burial_limit)
    v_support = measured & (obj_class != NO_SUPPORT_NONE)
    v_support_buried = v_support & (obj_class == NO_SUPPORT_BURIED)
    v_support_floating = v_support & (obj_class == NO_SUPPORT_FLOATING)
    v_support_none = v_support & (obj_class == NO_SUPPORT_NO_UPWARD)
    violation = v_float | v_burial | v_support

    # 測り切れない物（足元の点の問い合わせが ok でない物）は落とす
    unmeasured_rows = np.nonzero(~measured)[0]
    unmeasured_statuses = {}
    for value in status[~point_ok]:
        unmeasured_statuses[value] = unmeasured_statuses.get(value, 0) + 1
    if len(unmeasured_rows):
        failures.append(f"測り切れない物が {len(unmeasured_rows)} 個あります（ok でない足元の点の状態 {unmeasured_statuses}）")
    bad_status = {}
    for row, value in zip(owner[~point_ok], status[~point_ok]):
        bad_status.setdefault(int(row), value)
    unmeasured_list = [{"index": int(row), "type": TYPES[int(obj_type[row])], "x": round(float(objects[row, 1]), 2),
                        "z": round(float(objects[row, 3]), 2), "status": bad_status.get(int(row), "no_foot_points")}
                       for row in unmeasured_rows[:UNMEASURED_LISTED]]

    # 範囲ごとの値と消した物
    removed = load_json(args.removed)["removed"] if args.removed else []
    for item in removed:
        if item.get("type") not in TYPES:
            failures.append(f"消した物の種類 {item.get('type')} が分かりません")
    removed_x = np.array([float(r["x"]) for r in removed])
    removed_z = np.array([float(r["z"]) for r in removed])
    removed_type = np.array([r.get("type") for r in removed], dtype=object)
    report_regions = {}
    for region_id, inside, removed_inside in region_masks(regions, center_x, center_z, n):
        removed_sel = removed_inside(removed_x, removed_z) if len(removed) else np.zeros(0, dtype=bool)
        entry = {}
        for i, t in enumerate(TYPES):
            sel = inside & (obj_type == i)
            sel_m = sel & measured & (obj_class == NO_SUPPORT_NONE)
            worst = []
            with np.errstate(invalid="ignore"):
                score = np.where(sel & violation,
                                 np.nan_to_num(np.fmax(float_m, burial_m - burial_limit), nan=0.0) + v_support * 1e9, -1.0)
            for row in np.argsort(-score)[:WORST_PER_REGION]:
                if score[row] < 0:
                    break
                worst.append({"index": int(row), "mesh": meshes[int(objects[row, 0])]["name"],
                              "x": round(float(center_x[row]), 2), "z": round(float(center_z[row]), 2),
                              "y": round(float(objects[row, 2]), 2), "height_m": round(float(obj_height[row]), 3),
                              "float_m": None if np.isnan(float_m[row]) else round(float(float_m[row]), 3),
                              "burial_m": None if np.isnan(burial_m[row]) else round(float(burial_m[row]), 3),
                              "no_support": bool(v_support[row])})
            gone = int((removed_sel & (removed_type == t)).sum()) if len(removed) else 0
            item = {
                "objects": int(sel.sum()),
                "measured": int((sel & measured).sum()),
                "unmeasured": int((sel & ~measured).sum()),
                "violations": int((sel & violation).sum()),
                "float_violations": int((sel & v_float).sum()),
                "burial_violations": int((sel & v_burial).sum()),
                "no_support": int((sel & v_support).sum()),
                "no_support_buried": int((sel & v_support_buried).sum()),
                "no_support_floating": int((sel & v_support_floating).sum()),
                "no_support_no_upward": int((sel & v_support_none).sum()),
                "float_m": percentiles(float_m[sel_m]),
                "burial_m": percentiles(burial_m[sel_m]),
                "removed": gone,
                "worst": worst,
            }
            if t == "talus":
                item["foot_points"] = percentiles(point_count[sel].astype(np.float64))
                item["burial_share"] = percentiles(burial_m[sel_m] / np.maximum(obj_height[sel_m], 1e-6))
            total = item["objects"] + gone
            item["removed_share"] = round(gone / total, 5) if total else 0.0
            if args.require_compliant and gone > REMOVED_MAX_SHARE * total:
                failures.append(f"{region_id} の {t} は消した物 {gone} 個が範囲の物 {total} 個の "
                                f"{REMOVED_MAX_SHARE:.0%} を超えます")
            entry[t] = item
        report_regions[region_id] = entry

    total_violations = int(violation.sum())
    if args.require_compliant and total_violations:
        failures.append(f"違反が {total_violations} 個あります（遵守の形は違反 0 を求めます）")
    summary = {
        "objects": counts,
        "foot_points": int(len(foot)),
        "query_statuses": statuses,
        "sampling": placement.get("sampling"),
        "violations": total_violations,
        "unmeasured": int(len(unmeasured_rows)),
        "unmeasured_objects": unmeasured_list,
        "removed": len(removed),
    }
    return finish(args, failures, summary, report_regions)


def finish(args, failures, summary, regions):
    mode = "record" if args.record else "require_compliant"
    report = {
        "format": "placement_check.v2",
        "mode": mode,
        "placement": os.path.abspath(args.placement),
        "rules": {
            "rock_bottom_band": ROCK_BOTTOM_BAND, "rock_foot_points": "all vertices in the bottom band",
            "root_ring_points": ROOT_RING_POINTS, "support_min_normal_y": SUPPORT_MIN_NORMAL_Y,
            "support_window": f"max({SUPPORT_WINDOW_MIN_M} m, {SUPPORT_WINDOW_SHARE} * height)",
            "per_object": "max over foot points", "unsupported_point": "violation",
            "rock_max_float_m": ROCK_MAX_FLOAT_M, "rock_max_burial_share": ROCK_MAX_BURIAL_SHARE,
            "scatter_max_float_m": SCATTER_MAX_FLOAT_M, "scatter_max_burial_m": SCATTER_MAX_BURIAL_M,
            "removed_max_share": REMOVED_MAX_SHARE,
        },
        "summary": summary,
        "regions": regions,
        "failures": failures,
        "result": "pass" if not failures else "fail",
    }
    out = args.out or os.path.join(os.path.dirname(os.path.abspath(args.placement)), f"placement_check.{mode}.json")
    with open(out, "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=1)
    print(f"report={out}")
    print(f"summary={json.dumps({k: v for k, v in summary.items() if k != 'unmeasured_objects'}, ensure_ascii=False)}")
    for region_id, entry in (regions or {}).items():
        for t, item in entry.items():
            if item["objects"] == 0 and item["removed"] == 0:
                continue
            print(f"region={region_id} type={t} objects={item['objects']} measured={item['measured']} "
                  f"unmeasured={item['unmeasured']} violations={item['violations']} float={item['float_violations']} "
                  f"burial={item['burial_violations']} no_support={item['no_support']}(buried={item['no_support_buried']} "
                  f"floating={item['no_support_floating']} no_upward={item['no_support_no_upward']}) "
                  f"removed={item['removed']}({item['removed_share']}) float_m={item['float_m']} "
                  f"burial_m={item['burial_m']}" + (f" burial_share={item['burial_share']}" if "burial_share" in item else ""))
    for failure in failures:
        print(f"FAIL {failure}")
    print(f"result={report['result']} mode={mode} failures={len(failures)}")
    return 1 if (args.verify and failures) else 0


if __name__ == "__main__":
    sys.exit(main())
