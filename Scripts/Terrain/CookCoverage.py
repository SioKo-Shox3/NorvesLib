# 崖の生成の担当の範囲に掛かる地形の区画が、今の AssetCook（と、それに焼き込まれた崖の細部の版）で焼かれていることを
# 確かめる（ReferenceCheck.py --cook-coverage から呼ぶ。単独では動かさない）。
#
#   python Scripts/Terrain/ReferenceCheck.py --cook-coverage --manifest build/CookedAssets/manifest.json
#       --regions Scripts/Terrain/ShapeRegions.json --range-group cliff-gen [--verify]
#
# 区画: 範囲の source が kNearRegions の範囲は DEM の 1 m の区画（WastelandDem_<x>_<z>。DEM_TILE_CELLS 升目ごと）、
# kCastleRegions の範囲は遠景の castle の段の区画（WastelandHorizon_castle_<x>_<z>。horizon.json の bounds）のうち、範囲の
# 矩形と重なるもの。
# 印の照合は CookAssets.ps1 -CheckStamps（焼かずに、今の AssetCook・一覧の項目・元のファイルから印を求め、状態の印と比べる）。
# 合否: 範囲の組が分かり、どの範囲にも区画が 1 つ以上あり、区画がマニフェストにあって印が今の入力と同じ（match）。焼いた
# 区画の数（match）が区画の数と同じ。報告（<マニフェストの置き場>/cook_coverage.<組>.json）に AssetCook の SHA-256 と
# 崖の細部の版（Tools/AssetCook/TerrainCliffDetail.h の kTerrainCliffDetailVersion）を書く。
import json
import os
import re
import subprocess

import CheckCookedTerrain

ASSET_COOK = os.path.join("build", "Tools", "AssetCook", "RelWithDebInfo", "AssetCook.exe")
VERSION_HEADER = os.path.join("Tools", "AssetCook", "TerrainCliffDetail.h")
TERRAIN_PREFIX = "Models/WastelandTerrain/"
STAMP_LINE = re.compile(r"^COOK_STAMP logical=(\S+) package=(\S+) status=(\S+)")


def near_tiles(dem, region):
    """範囲の矩形と重なる DEM の区画 (x, z) の一覧"""
    tiles = []
    for tile_x in range((dem.columns - 2) // CheckCookedTerrain.DEM_TILE_CELLS + 1):
        first_column, last_column = dem.tile_vertex_range(tile_x, dem.columns)
        x0, _ = dem.position(first_column, 0)
        x1, _ = dem.position(last_column, 0)
        if x1 < region["x"][0] or x0 > region["x"][1]:
            continue
        for tile_z in range((dem.rows - 2) // CheckCookedTerrain.DEM_TILE_CELLS + 1):
            first_row, last_row = dem.tile_vertex_range(tile_z, dem.rows)
            _, z0 = dem.position(0, first_row)
            _, z1 = dem.position(0, last_row)
            if z1 < region["z"][0] or z0 > region["z"][1]:
                continue
            tiles.append((tile_x, tile_z))
    return tiles


def castle_tiles(region):
    return [tile for tile, (x0, x1, z0, z1) in sorted(CheckCookedTerrain.castle_tile_bounds().items())
            if not (x1 < region["x"][0] or x0 > region["x"][1] or z1 < region["z"][0] or z0 > region["z"][1])]


def cliff_detail_version(RC):
    with open(os.path.join(RC.REPO_ROOT, VERSION_HEADER), encoding="utf-8-sig") as f:
        match = re.search(r"kTerrainCliffDetailVersion\s*=\s*(\d+)u?;", f.read())
    return int(match.group(1)) if match else None


def check_stamps(RC, runtime_root, asset_cook):
    """CookAssets.ps1 -CheckStamps の地形の全項目の答え。(論理パス → (パッケージ, 状態), 失敗の一覧)"""
    command = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
               os.path.join(RC.REPO_ROOT, "Scripts", "CookAssets.ps1"), "-AssetCookExe", asset_cook,
               "-RuntimeRoot", runtime_root, "-CheckStamps", "-OnlyLogicalPath", "Assets/" + TERRAIN_PREFIX + "*"]
    done = subprocess.run(command, cwd=RC.REPO_ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    answers = {}
    for line in done.stdout.splitlines():
        match = STAMP_LINE.match(line.strip())
        if match:
            answers[match.group(1)] = (match.group(2), match.group(3))
    failures = []
    if done.returncode != 0:
        failures.append(f"CookAssets.ps1 -CheckStamps が失敗した（終了コード {done.returncode}）: "
                        f"{(done.stdout + done.stderr).strip()[-400:]}")
    if not answers:
        failures.append("CookAssets.ps1 -CheckStamps が地形の項目の印を 1 つも答えない")
    return answers, failures


def run(RC, manifest_path, regions_path, group_name, verify):
    failures = []
    if not os.path.isfile(manifest_path):
        print(f"REFERENCE_CHECK cook_coverage result=fail reason=manifest_missing path={manifest_path}")
        return 1
    with open(regions_path, encoding="utf-8-sig") as f:
        document = json.load(f)
    regions = {region["id"]: region for region in document.get("regions") or []}
    group = (document.get("range_groups") or {}).get(group_name) if group_name else list(regions)
    if not group:
        failures.append(f"範囲の組 {group_name} が {RC.rel_dir(regions_path)} の range_groups に無い")
        group = []
    with open(manifest_path, encoding="utf-8-sig") as f:
        assets = {asset["logical_path"]: asset for asset in json.load(f)["assets"]}
    dem = CheckCookedTerrain.Dem()
    tiles = {}
    report_regions = {}
    for region_id in group:
        region = regions.get(region_id)
        if region is None:
            failures.append(f"範囲の組の範囲 {region_id} が regions に無い")
            continue
        source = region.get("source", "")
        if "kNearRegions" in source:
            names = [f"{TERRAIN_PREFIX}WastelandDem_{x}_{z}.generated" for x, z in near_tiles(dem, region)]
        elif "kCastleRegions" in source:
            names = [f"{TERRAIN_PREFIX}WastelandHorizon_castle_{x}_{z}.generated" for x, z in castle_tiles(region)]
        else:
            failures.append(f"範囲 {region_id} の source {source} が崖の生成の範囲（kNearRegions・kCastleRegions）でない")
            continue
        if not names:
            failures.append(f"範囲 {region_id} に掛かる区画が無い")
        report_regions[region_id] = names
        for name in names:
            tiles.setdefault(name, []).append(region_id)
    asset_cook = os.path.join(RC.REPO_ROOT, ASSET_COOK)
    answers, stamp_failures = check_stamps(RC, os.path.dirname(manifest_path), asset_cook)
    failures.extend(stamp_failures)
    statuses = {}
    for name in sorted(tiles):
        if name not in assets:
            statuses[name] = "manifest_missing"
        else:
            statuses[name] = (answers.get(name) or (None, "no_answer"))[1]
        if statuses[name] != "match":
            failures.append(f"区画 {name}（範囲 {', '.join(tiles[name])}）の印が今の入力と合わない: {statuses[name]}")
    cooked = sum(1 for status in statuses.values() if status == "match")
    if cooked != len(tiles):
        failures.append(f"焼いた区画 {cooked} が範囲に掛かる区画 {len(tiles)} と合わない")
    report = {
        "manifest": RC.rel_dir(manifest_path),
        "regions": RC.rel_dir(regions_path),
        "range_group": group_name,
        "asset_cook": RC.rel_dir(asset_cook),
        "asset_cook_sha256": RC.sha256_file(asset_cook) if os.path.isfile(asset_cook) else None,
        "cliff_detail_version": cliff_detail_version(RC),
        "region_tiles": report_regions,
        "tiles": {name: {"regions": tiles[name], "status": statuses[name],
                         "source_hash": (assets.get(name) or {}).get("source_hash"),
                         "cooked_hash": (assets.get(name) or {}).get("cooked_hash")} for name in sorted(tiles)},
        "terrain_items_answered": len(answers),
        "terrain_items_stale": sorted(name for name, (_, status) in answers.items() if status != "match"),
        "cooked_tiles": cooked,
        "failures": failures,
        "pass": not failures,
    }
    out = os.path.join(os.path.dirname(manifest_path), f"cook_coverage.{group_name or 'all'}.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for region_id, names in report_regions.items():
        print(f"COOK_COVERAGE region={region_id} tiles={len(names)} "
              f"match={sum(1 for name in names if statuses.get(name) == 'match')}")
    print(f"COOK_COVERAGE asset_cook_sha256={report['asset_cook_sha256']} cliff_detail_version={report['cliff_detail_version']} "
          f"terrain_items={len(answers)} stale_terrain_items={len(report['terrain_items_stale'])}")
    for failure in failures:
        print("FAIL " + failure)
    print(f"REFERENCE_CHECK cook_coverage group={group_name} tiles={len(tiles)} cooked={cooked} "
          f"result={'pass' if not failures else 'fail'} report={RC.rel_dir(out)}")
    return 1 if (verify and failures) else 0
