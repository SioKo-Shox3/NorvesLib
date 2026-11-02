# 崖の生成の範囲に掛かる地形の印と、代表区画を今の AssetCook で焼き直した出力を確かめる。
# ReferenceCheck.py --cook-coverage から呼ぶ（単独では動かさない）。
#
#   python Scripts/Terrain/ReferenceCheck.py --cook-coverage --manifest build/CookedAssets/manifest.json
#       --regions Scripts/Terrain/ShapeRegions.json --range-group cliff-gen [--verify]
#
# kNearRegions は DEM の 1 m の区画、kCastleRegions は遠景の castle の区画のうち範囲の矩形と重なるものを検査する。
# 印は CookAssets.ps1 -CheckStamps で照合する。印の元は崖の細部の版と地形の経路のソースで、exe のハッシュは含まない。
# 近景の代表区画を一時の置き場で同じ CookAssets.ps1 により焼き直し、共有マニフェストの cooked_hash と比べる。
# 合否: 範囲の組が分かり、各範囲に区画があり、全区画の印が match で、代表区画の焼き直しのハッシュが一致すること。
# 報告 <マニフェストの置き場>/cook_coverage.<組>.json の cliff_detail_version・terrain_sources_sha256・terrain_sources は印の元。
# recook は代表区画・共有と焼き直しの cooked_hash・一致・秒数と、焼き比べに使った AssetCook のパス・SHA-256 を記録する。
# 一時の置き場は、焼き直しが失敗した場合も削除する。
import json
import os
import re
import subprocess
import shutil
import tempfile
import time

import CheckCookedTerrain

ASSET_COOK = os.path.join("build", "Tools", "AssetCook", "RelWithDebInfo", "AssetCook.exe")
TERRAIN_PREFIX = "Models/WastelandTerrain/"
TERRAIN_STAMP_LINE = re.compile(r"^COOK_STAMP_TERRAIN cliff_detail_version=(\d+) sources_sha256=([0-9a-fA-F]{64}) sources=(\d+)$")
TERRAIN_SOURCE_LINE = re.compile(r"^COOK_STAMP_TERRAIN_SOURCE path=(\S+) sha256=([0-9a-fA-F]{64})$")
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


def check_stamps(RC, runtime_root, asset_cook):
    """地形の全項目の印の状態、印の元、失敗の一覧を返す。"""
    command = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
               os.path.join(RC.REPO_ROOT, "Scripts", "CookAssets.ps1"), "-AssetCookExe", asset_cook,
               "-RuntimeRoot", runtime_root, "-CheckStamps", "-OnlyLogicalPath", "Assets/" + TERRAIN_PREFIX + "*"]
    done = subprocess.run(command, cwd=RC.REPO_ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    answers = {}
    stamp = {}
    sources = []
    for line in done.stdout.splitlines():
        line = line.strip()
        terrain_match = TERRAIN_STAMP_LINE.fullmatch(line)
        source_match = TERRAIN_SOURCE_LINE.fullmatch(line)
        if terrain_match:
            stamp = {"cliff_detail_version": int(terrain_match.group(1)),
                     "terrain_sources_sha256": terrain_match.group(2),
                     "source_count": int(terrain_match.group(3))}
        if source_match:
            sources.append({"path": source_match.group(1), "sha256": source_match.group(2)})
        match = STAMP_LINE.match(line)
        if match:
            answers[match.group(1)] = (match.group(2), match.group(3))
    failures = []
    if done.returncode != 0:
        failures.append(f"CookAssets.ps1 -CheckStamps が失敗した（終了コード {done.returncode}）: "
                        f"{(done.stdout + done.stderr).strip()[-400:]}")
    if not answers:
        failures.append("CookAssets.ps1 -CheckStamps が地形の項目の印を 1 つも答えない")
    if not stamp:
        failures.append("COOK_STAMP_TERRAIN が無い。地形の印の元を出力する CookAssets.ps1 が必要")
    elif len(sources) != stamp["source_count"] or len({source["path"] for source in sources}) != len(sources):
        failures.append("COOK_STAMP_TERRAIN_SOURCE の一覧が印の元の件数と合わないか、パスが重複している")
    stamp["terrain_sources"] = sources
    return answers, stamp, failures


def recook_tile(RC, manifest_path, asset_cook, name, shared_hash):
    """代表区画を一時の置き場で焼き、共有の出力と照合する。"""
    result = {"tile": name, "shared_cooked_hash": shared_hash, "recooked_cooked_hash": None,
              "match": False, "seconds": 0.0, "asset_cook": RC.rel_dir(asset_cook),
              "asset_cook_sha256": RC.sha256_file(asset_cook) if os.path.isfile(asset_cook) else None}
    failures = []
    temporary_root = None
    started = time.perf_counter()
    try:
        temporary_root = tempfile.mkdtemp(prefix="terrain-cook-coverage-")
        temporary_manifest = os.path.join(temporary_root, "manifest.json")
        shutil.copyfile(manifest_path, temporary_manifest)
        command = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                   os.path.join(RC.REPO_ROOT, "Scripts", "CookAssets.ps1"), "-AssetCookExe", asset_cook,
                   "-RuntimeRoot", temporary_root, "-OnlyLogicalPath", "Assets/" + name,
                   "-Force", "-SkipPlacementGround"]
        done = subprocess.run(command, cwd=RC.REPO_ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        result["exit_code"] = done.returncode
        if done.returncode != 0:
            failures.append(f"代表区画 {name} の焼き直しが失敗した（終了コード {done.returncode}）: "
                            f"{(done.stdout + done.stderr).strip()[-400:]}")
        else:
            with open(temporary_manifest, encoding="utf-8-sig") as f:
                assets = {asset["logical_path"]: asset for asset in json.load(f)["assets"]}
            result["recooked_cooked_hash"] = (assets.get(name) or {}).get("cooked_hash")
            result["match"] = bool(shared_hash and result["recooked_cooked_hash"] == shared_hash)
            if not result["match"]:
                failures.append(f"代表区画 {name} の cooked_hash が焼き直しと一致しない。"
                                "地形の印の一覧に無い入力が出力を変えた見込みがあるため、一覧を見直す")
    except (OSError, ValueError, KeyError, TypeError) as error:
        failures.append(f"代表区画 {name} の焼き直しの検査に失敗した: {error}")
    finally:
        if temporary_root is not None:
            try:
                shutil.rmtree(temporary_root)
            except OSError as error:
                failures.append(f"焼き直しの一時の置き場を削除できない: {temporary_root}: {error}")
        result["seconds"] = round(time.perf_counter() - started, 3)
    return result, failures


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
    near_tile_regions = {}
    report_regions = {}
    for region_id in group:
        region = regions.get(region_id)
        if region is None:
            failures.append(f"範囲の組の範囲 {region_id} が regions に無い")
            continue
        source = region.get("source", "")
        if "kNearRegions" in source:
            names = [f"{TERRAIN_PREFIX}WastelandDem_{x}_{z}.generated" for x, z in near_tiles(dem, region)]
            for name in names:
                near_tile_regions.setdefault(name, set()).add(region_id)
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
    answers, stamp, stamp_failures = check_stamps(RC, os.path.dirname(manifest_path), asset_cook)
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
    recook = None
    if not near_tile_regions:
        failures.append("範囲の組に代表の焼き比べを行う kNearRegions の区画が無い")
    else:
        # 掛かる近景範囲の数が最多の区画を選び、同数なら論理パス順の先頭にする。
        representative = min(near_tile_regions, key=lambda name: (-len(near_tile_regions[name]), name))
        recook, recook_failures = recook_tile(RC, manifest_path, asset_cook, representative,
                                              (assets.get(representative) or {}).get("cooked_hash"))
        failures.extend(recook_failures)
    report = {
        "manifest": RC.rel_dir(manifest_path),
        "regions": RC.rel_dir(regions_path),
        "range_group": group_name,
        "cliff_detail_version": stamp.get("cliff_detail_version"),
        "terrain_sources_sha256": stamp.get("terrain_sources_sha256"),
        "terrain_sources": stamp["terrain_sources"],
        "recook": recook,
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
    print(f"COOK_COVERAGE cliff_detail_version={report['cliff_detail_version']} "
          f"terrain_sources_sha256={report['terrain_sources_sha256']} terrain_sources={len(report['terrain_sources'])}")
    if recook is not None:
        print(f"COOK_COVERAGE recook tile={recook['tile']} match={str(recook['match']).lower()} seconds={recook['seconds']}")
    for failure in failures:
        print("FAIL " + failure)
    print(f"REFERENCE_CHECK cook_coverage group={group_name} tiles={len(tiles)} cooked={cooked} "
          f"result={'pass' if not failures else 'fail'} report={RC.rel_dir(out)}")
    return 1 if (verify and failures) else 0
