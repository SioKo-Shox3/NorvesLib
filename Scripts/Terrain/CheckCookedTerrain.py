# 焼いた地形の区画への問い合わせ（AssetCook の `--sample-cooked-terrain --manifest <manifest.json> --points <点の一覧>`）を
# 確かめる。問い合わせは点ごとに、その X/Z を含む DEM の区画のクック済みのパッケージ（最も細かい段の三角形）と鉛直の線の
# 交点をすべて返す。このスクリプトは交点の求め方を写さず、DEM の元の値（height.json・height.r32）と幾何の約束で照らす。
#
# 使い方:
#   python Scripts/Terrain/CheckCookedTerrain.py [--asset-cook <AssetCook.exe>] [--manifest <manifest.json>]
#                                                [--out <report.json>] [--verify]
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
# 終了コード: --verify で合格なら 0、不合格なら 1。--verify なしは値を出すだけで 0。
import argparse
import json
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_ASSET_COOK = os.path.join(REPO_ROOT, "build", "Tools", "AssetCook", "RelWithDebInfo", "AssetCook.exe")
DEFAULT_MANIFEST = os.path.join(REPO_ROOT, "build", "CookedAssets", "manifest.json")
DEFAULT_REPORT = os.path.join(REPO_ROOT, ".harness", "runs", "scene-capture", "SCN-WL-CLIFF-SURFACE-QUERY",
                              "cooked_terrain.json")
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


def query(asset_cook, manifest, points):
    """点の一覧を問い合わせ、結果の JSON を返す"""
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as handle:
        json.dump({"points": [[x, z] for x, z in points]}, handle)
        path = handle.name
    try:
        result = subprocess.run([asset_cook, "--sample-cooked-terrain", "--manifest", manifest, "--points", path],
                                capture_output=True, cwd=REPO_ROOT)
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


def main():
    parser = argparse.ArgumentParser(description="焼いた地形の区画への問い合わせ（--sample-cooked-terrain）を確かめる")
    parser.add_argument("--asset-cook", default=DEFAULT_ASSET_COOK)
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--out", default=DEFAULT_REPORT)
    parser.add_argument("--verify", action="store_true")
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
