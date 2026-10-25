# 焼いた地形の区画への問い合わせ（AssetCook の `--sample-cooked-terrain --manifest <manifest.json> --points <点の一覧>`）を
# 確かめる。問い合わせは点ごとに、その X/Z を含む DEM の区画のクック済みのパッケージ（最も細かい段の三角形）と鉛直の線の
# 交点をすべて返す。このスクリプトは交点の求め方を写さず、DEM の元の値（height.json・height.r32）と幾何の約束で照らす。
#
# 使い方:
#   python Scripts/Terrain/CheckCookedTerrain.py [--asset-cook <AssetCook.exe>] [--manifest <manifest.json>]
#                                                [--out <report.json>] [--verify] [--seams]
# 1. 崖の生成の範囲の外の区画（FLAT_TILES。問い合わせが cliff_detail=false と答えること）は DEM の格子のまま焼かれるので、
#    格子の頂点の真上の点（区画ごとに乱数の RANDOM_VERTICES 点と、区画の最初の頂点・隣の区画との境の頂点・格子の最後の
#    頂点）で、交点がちょうど 1 つで高さが height.r32 の値と MAX_FLAT_ERROR_M 以内
# 2. 崖の範囲の中（Battleship Rock の刻みの範囲 CLIFF_SCAN を SCAN_STEP_M の間隔で走査）の全部の点で交点が 1 つ以上、
#    交点が 2 つ以上の点（張り出し）が MIN_OVERHANG_POINTS 点以上あり、どの点も交点が上から順に（高さが真に減る順に）並ぶ。
#    張り出しの点は、線が面を下から通る交点（法線の Y が負）を少なくとも 1 つ持つ割合が MIN_UNDERSIDE_SHARE 以上
#    （同じ X/Z に面が 2 枚以上あれば、どれかは下を向く）。決めた張り出しの点（OVERHANG_POINTS）は交点が 2 つ以上
# 3. 範囲の外の点は out_of_range、区画がマニフェストに無い・パッケージのファイルが無いと package_missing、
#    パッケージが壊れている（中ほどのバイトを反転・半分で切る）と mesh_invalid を返し、どれも交点を返さない。
#    パッケージとしては正しくメッシュの本体だけが切れている組（mesh_short）は、メッシュの解析の失敗として mesh_invalid を返す。
#    失敗の区画は理由を持ち、結果の JSON は全体を UTF-8 として厳密に読めること（読めないバイトを置き換えない）
#    同じ置き方の正しい写しは ok で DEM の値を返す（失敗が置き方のせいでないことの対照）
# --seams: 上の 1〜3 の代わりに、崖の細部と刻みの範囲（近景の 4 範囲と遠景の castle の段。TerrainCliffDetail.cpp の
#    kNearRegions・kCastleRegions）に掛かる区画の継ぎ目を確かめる。押し出しは区画の境の頂点も水平へ動かす（境の頂点は両側の
#    区画で同じ位置）ので、継ぎ目の近くの面は隣の区画のメッシュにもある（問い合わせは隣の区画の三角形とも交わりを調べる）。
#    a. 継ぎ目ごとに、まず ±SEAM_COARSE_BAND_M の粗い帯で隣の区画の面だけに交わる点の継ぎ目からの距離の最大（境の動く幅）を
#       求め、それに SEAM_COARSE_PAD_M を足した幅（SEAM_BAND_M 以上）の帯を継ぎ目に垂直に SEAM_ACROSS_STEP_M、沿って
#       SEAM_ALONG_STEP_M おきに引く。全部の点で交点が 1 つ以上（面に交わらない点 0）。どちらの帯でも、隣の区画の面だけに交わる点の
#       距離の最大が帯の幅より SEAM_BAND_MARGIN_M 以上狭い（細かい帯で縁に届いた継ぎ目は、測った距離に SEAM_WIDEN_PAD_M を
#       足した幅で SEAM_WIDEN_ROUNDS 回まで走査し直す）
#    b. 近景の継ぎ目の上の格子の頂点（--sample-terrain-cliff-surface で押し出した位置）で、両側の区画の交点の高さの差が
#       MAX_SEAM_STEP_M 以内（両側の区画の交点がそろわない頂点も落ちる）。継ぎ目の線の上の帯の点で両側の区画が交わる所の差は
#       値を出すだけ（線が張り出しの別の層で両側の面を通ると、差は層の間の高さになる）
# 終了コード: --verify で合格なら 0、不合格なら 1。--verify なしは値を出すだけで 0。
import argparse
import json
import math
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_ASSET_COOK = os.path.join(REPO_ROOT, "build", "Tools", "AssetCook", "RelWithDebInfo", "AssetCook.exe")
DEFAULT_MANIFEST = os.path.join(REPO_ROOT, "build", "CookedAssets", "manifest.json")
DEFAULT_REPORT = os.path.join(REPO_ROOT, ".harness", "runs", "scene-capture", "SCN-WL-CLIFF-SURFACE-QUERY",
                              "cooked_terrain.json")
DEFAULT_SEAM_REPORT = os.path.join(REPO_ROOT, ".harness", "runs", "scene-capture", "SCN-WL-TERRAIN-SEAM-FIX",
                                   "cooked_terrain_seams.json")
DEM_DESCRIPTOR = os.path.join(REPO_ROOT, "Assets", "Terrain", "ValleyOfTheGods", "height.json")
# DEM の区画の一辺の升目の数（Library/Core/Public/Rendering/Terrain/TerrainHeightField.h の kDemTileCells）
DEM_TILE_CELLS = 500

# 1. 崖の生成の範囲の外の区画（格子の四隅と北の端の中ほど。前哨地の造成の区画 (7, 5) は避ける）
FLAT_TILES = [(0, 0), (9, 0), (0, 9), (9, 9), (4, 0)]
RANDOM_VERTICES = 60
RANDOM_SEED = 20261025
MAX_FLAT_ERROR_M = 1.0e-4

# 2. 崖の範囲の中の走査（Battleship Rock の刻みの範囲のうち区画 (1, 2) に入る部分。X・Z の範囲[m]と間隔）。
#    格子の頂点・升目の対角線に乗らないよう端数をずらす
CLIFF_SCAN = {"tile": (1, 2), "x": (-2099.0, -1722.0), "z": (-1367.0, -1115.0), "offset": (0.37, 0.61)}
SCAN_STEP_M = 1.0
MIN_OVERHANG_POINTS = 100
MIN_UNDERSIDE_SHARE = 0.95
# 走査で見つけた張り出しの点（上を向く面・下を向く面・上を向く面の 3 つの交点を持ち、上の 2 つの差が 3 m を超える。
# 崖の細部の版 5・刻みの版 8 の焼き）
OVERHANG_POINTS = [(-1776.63, -1220.39), (-1967.63, -1210.39)]
# 交点の高さが真に減るとみなす差[m]（問い合わせが同じ高さとしてまとめる差と同じ桁）
MIN_HIT_GAP_M = 1.0e-4

# --seams: 崖の細部の範囲（TerrainCliffDetail.cpp の kNearRegions・kCastleRegions の X 最小・最大・Z 最小・最大）。
#    範囲の外では押し出しが 0 なので、継ぎ目は範囲の中だけを調べる
NEAR_REGIONS = {
    "battleship_rock": (-2150.0, -900.0, -1650.0, -420.0),
    "franklin_butte": (-760.0, -240.0, -660.0, -140.0),
    "rooster_butte": (640.0, 1160.0, 560.0, 1160.0),
    "setting_hen_butte": (960.0, 1460.0, 1140.0, 1960.0),
}
CASTLE_REGIONS = {
    "castle_main_butte": (-3450.0, -2100.0, -4450.0, -3950.0),
    "castle_far_cliffs": (-3650.0, -1900.0, -6340.0, -4650.0),
    "castle_left_far": (-6250.0, -5150.0, -8250.0, -6250.0),
    "castle_left_near": (-5350.0, -4250.0, -7000.0, -5500.0),
    "castle_left_edge": (-6875.0, -6150.0, -8625.0, -7875.0),
}
HORIZON_DESCRIPTOR = os.path.join(REPO_ROOT, "Assets", "Terrain", "ValleyOfTheGods", "horizon.json")
# 細かい帯の幅の下限と刻み。帯の幅は継ぎ目ごとに粗い走査（SEAM_COARSE_*）で求めた境の動く幅に余白を足して決める
SEAM_BAND_M = 1.0
SEAM_ACROSS_STEP_M = 0.05
SEAM_ALONG_STEP_M = 0.1
SEAM_BAND_MARGIN_M = 0.1
SEAM_COARSE_BAND_M = 4.0
SEAM_COARSE_ACROSS_STEP_M = 0.25
SEAM_COARSE_ALONG_STEP_M = 1.0
SEAM_COARSE_PAD_M = 0.3
SEAM_WIDEN_PAD_M = 0.5
SEAM_WIDEN_ROUNDS = 3
MAX_SEAM_STEP_M = 0.05
# 1 回の問い合わせに渡す点の数の上限（結果の JSON を読む記憶の量を抑える）
SEAM_CHUNK_POINTS = 400000
SEAM_WORKERS = 4

# 3. 範囲の外の点（格子の西・東・北・南の外と、遠い点）と、壊したパッケージを置く区画
OUT_OF_RANGE_POINTS = [(-2600.5, 0.0), (2099.5, 0.0), (0.0, -2499.5), (0.0, 2100.5), (1.0e6, -1.0e6)]
BROKEN_TILE = (9, 9)
# 3. パッケージは正しくメッシュだけが切れた組で、メッシュの項目に残すバイト数（メッシュの見出しより長く本体より短い）
SHORT_MESH_BYTES = 4096


class Dem:
    """height.json の格子の記述と height.r32 の頂点の値"""

    def __init__(self):
        with open(DEM_DESCRIPTOR, encoding="utf-8-sig") as handle:
            descriptor = json.load(handle)
        self.columns = int(descriptor["width"])
        self.rows = int(descriptor["height"])
        self.first_x = float(descriptor["first_vertex_engine"]["x"])
        self.first_z = float(descriptor["first_vertex_engine"]["z"])
        self.spacing = float(descriptor["vertex_spacing_m"])
        self.path = os.path.join(os.path.dirname(DEM_DESCRIPTOR), descriptor["file"])

    def tile_vertex_range(self, tile, axis_count):
        first = tile * DEM_TILE_CELLS
        last = min(first + DEM_TILE_CELLS, axis_count - 1)
        return first, last

    def heights(self, vertices):
        """(列, 行) の一覧の高さ（行は北から南、列は西から東。float32 の行優先）"""
        values = []
        with open(self.path, "rb") as handle:
            for column, row in vertices:
                handle.seek((row * self.columns + column) * 4)
                values.append(struct.unpack("<f", handle.read(4))[0])
        return values

    def position(self, column, row):
        return self.first_x + column * self.spacing, self.first_z + row * self.spacing


def query(asset_cook, manifest, points, horizon_level=None):
    """点の一覧を問い合わせ、結果の JSON を返す。horizon_level を渡すと遠景のその段の区画を読む"""
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as handle:
        json.dump({"points": [[x, z] for x, z in points]}, handle)
        path = handle.name
    command = [asset_cook, "--sample-cooked-terrain", "--manifest", manifest, "--points", path]
    if horizon_level is not None:
        command += ["--horizon-level", horizon_level]
    try:
        result = subprocess.run(command, capture_output=True, cwd=REPO_ROOT)
    finally:
        os.remove(path)
    if result.returncode != 0:
        # 標準エラーは人が読む文言（コンソールのコードページ）なので、読めない字は置き換えて見せるだけにする
        raise RuntimeError(f"AssetCook が失敗しました（{result.returncode}）: "
                           f"{result.stderr.decode('mbcs', errors='replace').strip()}")
    # 結果の JSON は UTF-8 の約束なので、読めないバイトは置き換えずに失敗にする
    data = json.loads(result.stdout.decode("utf-8", errors="strict"))
    if data.get("format") != "cooked_terrain_samples.v1" or len(data["points"]) != len(points):
        raise RuntimeError("問い合わせの結果の形が違います")
    return data


def check_flat(asset_cook, manifest, dem, failures):
    rng = random.Random(RANDOM_SEED)
    vertices = []
    for tile_x, tile_z in FLAT_TILES:
        first_column, last_column = dem.tile_vertex_range(tile_x, dem.columns)
        first_row, last_row = dem.tile_vertex_range(tile_z, dem.rows)
        chosen = {(first_column, first_row), (last_column, last_row), (first_column, last_row),
                  (last_column, first_row)}
        while len(chosen) < RANDOM_VERTICES + 4:
            chosen.add((rng.randint(first_column, last_column), rng.randint(first_row, last_row)))
        vertices.extend(sorted(chosen))
    expected = dem.heights(vertices)
    data = query(asset_cook, manifest, [dem.position(c, r) for c, r in vertices])
    tiles = {tuple(tile["tile"]): tile for tile in data["tiles"]}
    for tile in FLAT_TILES:
        info = tiles.get(tile)
        if info is None or info["status"] != "ok" or info["cliff_detail"]:
            failures.append(f"flat: 区画 {tile} が崖の範囲の外の読める区画ではありません（{info}）")
    max_error = 0.0
    bad = []
    for (column, row), height, point in zip(vertices, expected, data["points"]):
        if point["status"] != "ok" or len(point["hits"]) != 1:
            bad.append({"vertex": [column, row], "status": point["status"], "hits": len(point["hits"])})
            continue
        error = abs(point["hits"][0]["height"] - height)
        max_error = max(max_error, error)
        if error > MAX_FLAT_ERROR_M:
            bad.append({"vertex": [column, row], "height": point["hits"][0]["height"], "dem": height})
    if bad:
        failures.append(f"flat: {len(bad)} 点が DEM の値と合いません（例 {bad[:3]}）")
    print(f"flat: tiles={len(FLAT_TILES)} vertices={len(vertices)} max_error_m={max_error:.3g} bad={len(bad)}")
    return {"tiles": [list(t) for t in FLAT_TILES], "vertices": len(vertices), "max_error_m": max_error,
            "bad": bad[:20]}


def hits_descending(hits):
    return all(hits[i]["height"] - hits[i + 1]["height"] > MIN_HIT_GAP_M for i in range(len(hits) - 1))


def check_cliff(asset_cook, manifest, failures):
    (x0, x1), (z0, z1) = CLIFF_SCAN["x"], CLIFF_SCAN["z"]
    ox, oz = CLIFF_SCAN["offset"]
    points = []
    x = x0
    while x <= x1:
        z = z0
        while z <= z1:
            points.append((round(x + ox, 4), round(z + oz, 4)))
            z += SCAN_STEP_M
        x += SCAN_STEP_M
    points.extend(OVERHANG_POINTS)
    data = query(asset_cook, manifest, points)
    tiles = {tuple(tile["tile"]): tile for tile in data["tiles"]}
    info = tiles.get(CLIFF_SCAN["tile"])
    if info is None or info["status"] != "ok" or not info["cliff_detail"]:
        failures.append(f"cliff: 区画 {CLIFF_SCAN['tile']} が崖の範囲の中の読める区画ではありません（{info}）")
    no_hit = [p for p in data["points"] if p["status"] != "ok" or not p["hits"]]
    unordered = [p for p in data["points"] if not hits_descending(p["hits"])]
    multi = [p for p in data["points"][:len(points) - len(OVERHANG_POINTS)] if len(p["hits"]) >= 2]
    underside = [p for p in multi if any(h["normal"][1] < 0.0 for h in p["hits"])]
    underside_share = len(underside) / len(multi) if multi else 0.0
    histogram = {}
    for p in data["points"]:
        histogram[len(p["hits"])] = histogram.get(len(p["hits"]), 0) + 1
    if no_hit:
        failures.append(f"cliff: 交点の無い点が {len(no_hit)} 点あります（例 {no_hit[:2]}）")
    if unordered:
        failures.append(f"cliff: 交点が上から順に並ばない点が {len(unordered)} 点あります（例 {unordered[:1]}）")
    if len(multi) < MIN_OVERHANG_POINTS:
        failures.append(f"cliff: 交点が 2 つ以上の点が {len(multi)} 点で、{MIN_OVERHANG_POINTS} 点に届きません")
    if underside_share < MIN_UNDERSIDE_SHARE:
        failures.append(f"cliff: 下を向く交点を持つ張り出しの点の割合 {underside_share:.3f} が {MIN_UNDERSIDE_SHARE} 未満です")
    examples = data["points"][len(points) - len(OVERHANG_POINTS):]
    for point in examples:
        if point["status"] != "ok" or len(point["hits"]) < 2:
            failures.append(f"cliff: 張り出しの点 ({point['x']}, {point['z']}) の交点が 2 つ未満です（{point}）")
    print(f"cliff: points={len(points)} hits_histogram={dict(sorted(histogram.items()))} overhang={len(multi)} "
          f"underside_share={underside_share:.3f} no_hit={len(no_hit)} unordered={len(unordered)}")
    for point in examples:
        heights = ", ".join(f"{h['height']:.3f}({h['normal'][1]:+.2f})" for h in point["hits"])
        print(f"cliff: example ({point['x']}, {point['z']}) hits={len(point['hits'])} heights(normal_y)={heights}")
    return {"tile": list(CLIFF_SCAN["tile"]), "points": len(points), "hits_histogram": histogram,
            "overhang_points": len(multi), "underside_share": underside_share, "no_hit": len(no_hit),
            "unordered": len(unordered), "examples": examples}


def fnv1a64(data):
    """FNV-1a 64 ビット（パッケージの項目とマニフェストの cooked_hash が使うハッシュ）"""
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def shorten_mesh_entry(package, payload_hash):
    """パッケージの中のメッシュの項目を先頭 SHORT_MESH_BYTES に縮め、項目の格納長とハッシュを合わせ直す。
    パッケージの検証は通り、メッシュの本体だけが途中で切れた形になる。返り値は (パッケージのバイト, 新しいハッシュ)。
    配置は Library/Core/Public/Asset/AssetPackageFormat.h の AssetPackageFormatV1（見出し・項目の表の位置）に従う"""
    entry_count = struct.unpack_from("<I", package, 32)[0]
    entry_table = struct.unpack_from("<Q", package, 40)[0]
    for index in range(entry_count):
        record = entry_table + index * 64
        data_offset, stored_size, _, stored_hash = struct.unpack_from("<QQQQ", package, record + 24)
        if stored_hash != payload_hash:
            continue
        if stored_size <= SHORT_MESH_BYTES:
            raise RuntimeError("メッシュの項目が短すぎて縮められません")
        new_hash = fnv1a64(package[data_offset:data_offset + SHORT_MESH_BYTES])
        patched = bytearray(package)
        struct.pack_into("<QQQ", patched, record + 32, SHORT_MESH_BYTES, SHORT_MESH_BYTES, new_hash)
        return bytes(patched), f"{new_hash:016x}"
    raise RuntimeError("パッケージにマニフェストのハッシュの項目がありません")


def check_failures(asset_cook, manifest, dem, failures):
    report = {}
    data = query(asset_cook, manifest, OUT_OF_RANGE_POINTS)
    for point in data["points"]:
        if point["status"] != "out_of_range" or point["hits"] or "tile" in point:
            failures.append(f"failure: 範囲の外の点が out_of_range を返しません（{point}）")
    report["out_of_range"] = [p["status"] for p in data["points"]]

    with open(manifest, encoding="utf-8-sig") as handle:
        source = json.load(handle)
    suffix = f"WastelandDem_{BROKEN_TILE[0]}_{BROKEN_TILE[1]}.generated"
    entry = next(a for a in source["assets"] if a["logical_path"].endswith(suffix))
    package = os.path.join(os.path.dirname(os.path.abspath(manifest)), entry["cooked_package"])
    column = BROKEN_TILE[0] * DEM_TILE_CELLS + 7
    row = BROKEN_TILE[1] * DEM_TILE_CELLS + 11
    point = dem.position(column, row)
    expected = dem.heights([(column, row)])[0]
    with open(package, "rb") as handle:
        original = handle.read()
    flipped = bytearray(original)
    for index in range(len(flipped) // 2, len(flipped) // 2 + 4096):
        flipped[index] ^= 0xFF
    short_mesh = shorten_mesh_entry(original, int(entry["cooked_hash"], 16))
    # (名前, マニフェストに項目を置くか, パッケージのバイト（None は置かない）, 期待する状態, マニフェストの cooked_hash)
    cases = [
        ("control", True, original, "ok", None),
        ("entry_missing", False, original, "package_missing", None),
        ("file_missing", True, None, "package_missing", None),
        ("flipped", True, bytes(flipped), "mesh_invalid", None),
        ("truncated", True, original[:len(original) // 2], "mesh_invalid", None),
        # パッケージとしては正しく（項目のハッシュも合う）、メッシュの本体だけが途中で切れている
        ("mesh_short", True, short_mesh[0], "mesh_invalid", short_mesh[1]),
    ]
    root = tempfile.mkdtemp(prefix="cooked_terrain_check_")
    try:
        for name, with_entry, payload, expected_status, cooked_hash in cases:
            directory = os.path.join(root, name)
            os.makedirs(os.path.join(directory, os.path.dirname(entry["cooked_package"])), exist_ok=True)
            if payload is not None:
                with open(os.path.join(directory, entry["cooked_package"]), "wb") as handle:
                    handle.write(payload)
            case_manifest = os.path.join(directory, "manifest.json")
            case_entry = dict(entry, cooked_hash=cooked_hash) if cooked_hash else entry
            with open(case_manifest, "w", encoding="utf-8") as handle:
                json.dump({"version": source["version"], "assets": [case_entry] if with_entry else []}, handle)
            data = query(asset_cook, case_manifest, [point])
            result = data["points"][0]
            reason = next((t["reason"] for t in data["tiles"] if t["tile"] == list(BROKEN_TILE)), None)
            ok = result["status"] == expected_status
            if expected_status == "ok":
                ok = ok and len(result["hits"]) == 1 and abs(result["hits"][0]["height"] - expected) <= MAX_FLAT_ERROR_M
            else:
                # 失敗の区画は理由を必ず持つ（JSON は query で UTF-8 として厳密に読んでいる）
                ok = ok and not result["hits"] and bool(reason)
            if name == "mesh_short":
                # パッケージの検証を通り、メッシュの解析で落ちた（解析の失敗の理由が読める字で届く）
                ok = ok and reason.startswith("メッシュを解析できません status=")
            if not ok:
                failures.append(f"failure: {name} が {expected_status} を返しません（{result}、理由 {reason!r}）")
            report[name] = {"status": result["status"], "reason": reason}
            print(f"failure: {name} status={result['status']} hits={len(result['hits'])} expected={expected_status}"
                  f" reason={reason!r}")
    finally:
        shutil.rmtree(root, ignore_errors=True)
    print(f"failure: out_of_range={report['out_of_range']}")
    return report


def seam_lines(dem):
    """崖の範囲に掛かる区画の継ぎ目の一覧。要素は (範囲の名前, 段, 'x' か 'z', 継ぎ目の座標, 沿う向きの始め, 終わり)。
    'x' は X が一定の継ぎ目（Z に沿う）。段は None（近景の DEM）か 'castle'"""
    seams = []
    dem_x = (dem.first_x, dem.first_x + (dem.columns - 1) * dem.spacing)
    dem_z = (dem.first_z, dem.first_z + (dem.rows - 1) * dem.spacing)
    cuts_x = [dem.first_x + k * DEM_TILE_CELLS * dem.spacing for k in range(1, (dem.columns - 2) // DEM_TILE_CELLS + 1)]
    cuts_z = [dem.first_z + k * DEM_TILE_CELLS * dem.spacing for k in range(1, (dem.rows - 2) // DEM_TILE_CELLS + 1)]
    for name, (x0, x1, z0, z1) in NEAR_REGIONS.items():
        seams += [(name, None, "x", x, max(z0, dem_z[0]), min(z1, dem_z[1])) for x in cuts_x if x0 < x < x1]
        seams += [(name, None, "z", z, max(x0, dem_x[0]), min(x1, dem_x[1])) for z in cuts_z if z0 < z < z1]
    bounds = castle_tile_bounds()
    edges_x = sorted({b[0] for b in bounds.values()} | {b[1] for b in bounds.values()})
    edges_z = sorted({b[2] for b in bounds.values()} | {b[3] for b in bounds.values()})
    for name, (x0, x1, z0, z1) in CASTLE_REGIONS.items():
        seams += [(name, "castle", "x", x, max(z0, edges_z[0]), min(z1, edges_z[-1]))
                  for x in edges_x[1:-1] if x0 < x < x1]
        seams += [(name, "castle", "z", z, max(x0, edges_x[0]), min(x1, edges_x[-1]))
                  for z in edges_z[1:-1] if z0 < z < z1]
    return seams


def castle_tile_bounds():
    """遠景の castle の段の区画 (x, z) ごとの (X 最小, X 最大, Z 最小, Z 最大)"""
    with open(HORIZON_DESCRIPTOR, encoding="utf-8-sig") as handle:
        descriptor = json.load(handle)
    bounds = {}
    for mesh in descriptor["meshes"]:
        if mesh.get("level") != "castle":
            continue
        tile_x, tile_z = (int(value) for value in mesh["name"].split("_")[1:3])
        b = mesh["bounds"]
        bounds[(tile_x, tile_z)] = (b["x_min"], b["x_max"], b["z_min"], b["z_max"])
    return bounds


def tile_centers(dem):
    """段（None か 'castle'）ごとの、区画の中心の (X, Z)"""
    near = {}
    for tile_x in range((dem.columns - 2) // DEM_TILE_CELLS + 1):
        for tile_z in range((dem.rows - 2) // DEM_TILE_CELLS + 1):
            near[(tile_x, tile_z)] = dem.position(tile_x * DEM_TILE_CELLS + DEM_TILE_CELLS / 2,
                                                  tile_z * DEM_TILE_CELLS + DEM_TILE_CELLS / 2)
    castle = {tile: ((b[0] + b[1]) / 2, (b[2] + b[3]) / 2) for tile, b in castle_tile_bounds().items()}
    return {None: near, "castle": castle}


def seam_points(axis, coordinate, start, end, band=SEAM_BAND_M, across_step=SEAM_ACROSS_STEP_M,
                along_step=SEAM_ALONG_STEP_M):
    """継ぎ目の帯の格子の点 ((x, z), 継ぎ目からの符号つきの距離)"""
    across = [round(-band + j * across_step, 4) for j in range(int(round(2 * band / across_step)) + 1)]
    points = []
    count = int((end - start) / along_step)
    for i in range(count + 1):
        along = round(start + i * along_step, 4)
        for offset in across:
            value = round(coordinate + offset, 4)
            points.append(((value, along) if axis == "x" else (along, value), offset))
    return points


def side_of(center, axis, coordinate):
    return (center[0] if axis == "x" else center[1]) > coordinate


def both_side_step(hits, centers, axis, coordinate):
    """両側の区画の交点の高さの差（共有する交点があれば 0、両側の交点が無ければ None）"""
    if any(hit.get("shared") for hit in hits):
        return 0.0
    plus = [h["height"] for h in hits if side_of(centers[tuple(h["hit_tile"])], axis, coordinate)]
    minus = [h["height"] for h in hits if not side_of(centers[tuple(h["hit_tile"])], axis, coordinate)]
    if not plus or not minus:
        return None
    return min(abs(a - b) for a in plus for b in minus)


def float32_candidates(value):
    """value に最も近い float と、その前後の float（標本の出力は 10 桁なので、float の中間に近い値はどちらへ丸めたか決まらない）"""
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    return [struct.unpack("<f", struct.pack("<I", bits + delta))[0] for delta in (-1, 0, 1)]


def seam_vertex_probes(asset_cook, axis, coordinate, start, end, dem):
    """近景の継ぎ目の上の格子の頂点を、焼きと同じ式で押し出した位置（焼いた頂点と同じ float の X/Z）"""
    origin = dem.first_z if axis == "x" else dem.first_x
    lines = []
    for index in range(math.ceil((start - origin) / dem.spacing), math.floor((end - origin) / dem.spacing) + 1):
        along = origin + index * dem.spacing
        lines.append(f"{coordinate} {along}" if axis == "x" else f"{along} {coordinate}")
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
        path = handle.name
    try:
        result = subprocess.run([asset_cook, "--sample-terrain-cliff-surface", path], capture_output=True, cwd=REPO_ROOT)
    finally:
        os.remove(path)
    if result.returncode != 0:
        raise RuntimeError(f"--sample-terrain-cliff-surface が失敗しました（{result.returncode}）")
    probes = []
    for line in result.stdout.decode("utf-8", errors="replace").splitlines():
        if not line.startswith("point=") or line.endswith("out_of_range"):
            continue
        values = line[len("point="):].split(",")
        probes.append([(x, z) for x in float32_candidates(float(values[4])) for z in float32_candidates(float(values[6]))])
    return probes


def scan_seam_chunk(asset_cook, manifest, centers, seam, chunk):
    """継ぎ目の帯の点のひと塊を問い合わせ、面に交わらない点・隣の区画だけに交わる点の距離・線の上の両側の差を数える"""
    name, level, axis, coordinate, start, end = seam
    data = query(asset_cook, manifest, [p for p, _ in chunk], level)
    row = {"points": len(chunk), "bad": [], "cross_max_m": 0.0, "on_line": 0, "on_line_both": 0,
           "on_line_step_max_m": 0.0, "tiles_bad": [t for t in data["tiles"] if t["status"] != "ok"]}
    for (point, offset), result in zip(chunk, data["points"]):
        if result["status"] != "ok" or not result["hits"]:
            row["bad"].append(point)
            continue
        if offset != 0.0 and all(side_of(centers[tuple(h["hit_tile"])], axis, coordinate) != (offset > 0)
                                 for h in result["hits"]):
            row["cross_max_m"] = max(row["cross_max_m"], abs(offset))
        if offset == 0.0:
            row["on_line"] += 1
            step = both_side_step(result["hits"], centers, axis, coordinate)
            if step is not None:
                row["on_line_both"] += 1
                row["on_line_step_max_m"] = max(row["on_line_step_max_m"], step)
    return row


def scan_seams(asset_cook, manifest, centers, seams, bands, across_step, along_step):
    """継ぎ目ごとの帯（幅 bands[seam]）の点を並べて問い合わせ、塊ごとの数を返す"""
    jobs = []
    for seam in seams:
        points = seam_points(*seam[2:], band=bands[seam], across_step=across_step, along_step=along_step)
        for first in range(0, len(points), SEAM_CHUNK_POINTS):
            jobs.append((seam, points[first:first + SEAM_CHUNK_POINTS]))
    with ThreadPoolExecutor(max_workers=SEAM_WORKERS) as pool:
        return list(pool.map(lambda job: (job[0], scan_seam_chunk(asset_cook, manifest, centers[job[0][1]], *job)),
                             jobs))


def check_seams(asset_cook, manifest, dem, failures):
    centers = tile_centers(dem)
    seams = seam_lines(dem)
    # 粗い帯で境の動く幅を測り、細かい帯の幅を決める
    coarse = {}
    for seam, chunk in scan_seams(asset_cook, manifest, centers, seams, {seam: SEAM_COARSE_BAND_M for seam in seams},
                                  SEAM_COARSE_ACROSS_STEP_M, SEAM_COARSE_ALONG_STEP_M):
        row = coarse.setdefault(seam, {"cross_max_m": 0.0, "no_hit": 0})
        row["cross_max_m"] = max(row["cross_max_m"], chunk["cross_max_m"])
        row["no_hit"] += len(chunk["bad"])
    bands = {}
    for seam in seams:
        width = coarse[seam]["cross_max_m"] + SEAM_COARSE_PAD_M
        bands[seam] = max(SEAM_BAND_M, math.ceil(width / SEAM_ACROSS_STEP_M - 1e-9) * SEAM_ACROSS_STEP_M)
    # 細かい帯で境の動く幅が帯の縁に届いた継ぎ目は、幅を広げて走査し直す（粗い走査の点の間にある山を拾う）
    rows = {}
    pending = list(seams)
    for _ in range(SEAM_WIDEN_ROUNDS):
        for seam in pending:
            rows.pop(seam, None)
        for seam, chunk in scan_seams(asset_cook, manifest, centers, pending, bands, SEAM_ACROSS_STEP_M,
                                      SEAM_ALONG_STEP_M):
            row = rows.setdefault(seam, {"points": 0, "bad": [], "cross_max_m": 0.0, "on_line": 0, "on_line_both": 0,
                                         "on_line_step_max_m": 0.0, "tiles_bad": []})
            for key in ("points", "bad", "on_line", "on_line_both", "tiles_bad"):
                row[key] += chunk[key]
            for key in ("cross_max_m", "on_line_step_max_m"):
                row[key] = max(row[key], chunk[key])
        pending = [seam for seam in pending if rows[seam]["cross_max_m"] > bands[seam] - SEAM_BAND_MARGIN_M]
        if not pending:
            break
        for seam in pending:
            bands[seam] = round(rows[seam]["cross_max_m"] + SEAM_WIDEN_PAD_M, 4)

    report = []
    for seam, row in rows.items():
        name, level, axis, coordinate, start, end = seam
        label = f"{name} {level or 'near'} {axis}={coordinate:g} [{start:g}, {end:g}]"
        band = bands[seam]
        entry = {"region": name, "level": level or "near", "axis": axis, "coordinate": coordinate,
                 "range": [start, end], "coarse_cross_max_m": coarse[seam]["cross_max_m"],
                 "coarse_no_hit": coarse[seam]["no_hit"], "band_m": band, "points": row["points"], "no_hit": len(row["bad"]),
                 "no_hit_examples": row["bad"][:5], "cross_max_m": row["cross_max_m"],
                 "on_line_points": row["on_line"], "on_line_both_sides": row["on_line_both"],
                 "on_line_step_max_m": row["on_line_step_max_m"]}
        if row["tiles_bad"]:
            failures.append(f"seams: {label} の区画が読めません（{row['tiles_bad'][:2]}）")
        if row["bad"]:
            failures.append(f"seams: {label} で面に交わらない点が {len(row['bad'])} 点あります（例 {row['bad'][:3]}）")
        if coarse[seam]["no_hit"]:
            failures.append(f"seams: {label} の粗い帯で面に交わらない点が {coarse[seam]['no_hit']} 点あります")
        if coarse[seam]["cross_max_m"] > SEAM_COARSE_BAND_M - SEAM_BAND_MARGIN_M:
            failures.append(f"seams: {label} で隣の区画の面だけに交わる点が継ぎ目から {coarse[seam]['cross_max_m']} m にあり、"
                            f"粗い帯（{SEAM_COARSE_BAND_M} m）が狭すぎます")
        if row["cross_max_m"] > band - SEAM_BAND_MARGIN_M:
            failures.append(f"seams: {label} で隣の区画の面だけに交わる点が継ぎ目から {row['cross_max_m']} m にあり、"
                            f"帯（{band} m）が狭すぎます")
        vertex = ""
        if level is None:
            probes = seam_vertex_probes(asset_cook, axis, coordinate, start, end, dem)
            data = query(asset_cook, manifest, [point for candidates in probes for point in candidates])
            steps = []
            unmatched = []
            for index, candidates in enumerate(probes):
                results = data["points"][index * len(candidates):(index + 1) * len(candidates)]
                found = [both_side_step(result["hits"], centers[None], axis, coordinate) for result in results
                         if result["status"] == "ok"]
                found = [step for step in found if step is not None]
                if not found:
                    unmatched.append(candidates[len(candidates) // 2])
                else:
                    steps.append(min(found))
            entry["vertex_probes"] = len(probes)
            entry["vertex_unmatched"] = len(unmatched)
            entry["vertex_step_max_m"] = max(steps) if steps else None
            if not probes or unmatched:
                failures.append(f"seams: {label} の境の頂点 {len(unmatched)}/{len(probes)} 点で両側の区画の交点が"
                                f"そろいません（例 {unmatched[:3]}）")
            if steps and max(steps) > MAX_SEAM_STEP_M:
                failures.append(f"seams: {label} の境の頂点で両側の区画の高さの差が {max(steps):.4f} m あります")
            vertex = (f" vertex_probes={len(probes)} vertex_unmatched={len(unmatched)} "
                      f"vertex_step_max_m={entry['vertex_step_max_m']}")
        print(f"SEAM {label} coarse_cross_max_m={coarse[seam]['cross_max_m']:.2f} coarse_no_hit={coarse[seam]['no_hit']} "
              f"band_m={band:.2f} points={row['points']} no_hit={len(row['bad'])} cross_max_m={row['cross_max_m']:.2f} "
              f"on_line={row['on_line']} on_line_both={row['on_line_both']} "
              f"on_line_step_max_m={row['on_line_step_max_m']:.5f}{vertex}")
        report.append(entry)
    if not report:
        failures.append("seams: 崖の範囲に掛かる継ぎ目がありません")
    return report


def main():
    parser = argparse.ArgumentParser(description="焼いた地形の区画への問い合わせ（--sample-cooked-terrain）を確かめる")
    parser.add_argument("--asset-cook", default=DEFAULT_ASSET_COOK)
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--out", default=DEFAULT_REPORT)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--seams", action="store_true", help="崖の範囲に掛かる区画の継ぎ目だけを確かめる")
    args = parser.parse_args()
    args.asset_cook = os.path.abspath(args.asset_cook)
    args.manifest = os.path.abspath(args.manifest)
    for path in (args.asset_cook, args.manifest):
        if not os.path.isfile(path):
            print(f"見つかりません: {path}")
            print("result=fail")
            return 1

    dem = Dem()
    failures = []
    if args.seams:
        if args.out == DEFAULT_REPORT:
            args.out = DEFAULT_SEAM_REPORT
        report = {
            "asset_cook": args.asset_cook,
            "manifest": args.manifest,
            "seams": check_seams(args.asset_cook, args.manifest, dem, failures),
            "failures": failures,
        }
    else:
        report = {
            "asset_cook": args.asset_cook,
            "manifest": args.manifest,
            "flat": check_flat(args.asset_cook, args.manifest, dem, failures),
            "cliff": check_cliff(args.asset_cook, args.manifest, failures),
            "failure": check_failures(args.asset_cook, args.manifest, dem, failures),
            "failures": failures,
        }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
    print(f"report={args.out}")
    for failure in failures:
        print(f"FAIL {failure}")
    if args.verify:
        print(f"result={'pass' if not failures else 'fail'}")
        return 0 if not failures else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
