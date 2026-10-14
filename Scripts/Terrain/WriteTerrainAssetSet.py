# 荒野の DEM の区画（AssetCook の --generate terrain-tile:dem:<x>,<z>）の項目を、格子の記述（height.json）の大きさと
# 区画の割り方（TerrainHeightField.h の WastelandTerrain::kDemTileCells）から求めて、一覧 Assets/AssetSets/WastelandTerrain.json に書く。
#
# 使い方:
#   python Scripts/Terrain/WriteTerrainAssetSet.py --write   # DEM の項目を書き直す（ほかの項目はそのまま）
#   python Scripts/Terrain/WriteTerrainAssetSet.py --verify  # 一覧の DEM の項目が、今の格子とヘッダから求めたものと同じか
#
# 区画の数は一辺ごとに「升目の数（頂点の数 - 1）を kDemTileCells で割って切り上げ」（WastelandTerrain::DemTileCount と同じ）。
# 各項目の source_path は格子の記述、extra_sources は格子のファイルと区画の割り方のヘッダで、CookAssets.ps1 はこれらの
# SHA-256 を変更の検出（印）に入れる。格子を切り出し直して大きさが変わったら --write で項目を書き直す。
# 終了コード: --write は書けたら 0、--verify は一致すれば 0・違えば 1。
import argparse
import json
import os
import re
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HEADER_PATH = "Library/Core/Public/Rendering/Terrain/TerrainHeightField.h"
ASSET_SET_PATH = "Assets/AssetSets/WastelandTerrain.json"
DEM_GENERATE = re.compile(r"^terrain-tile:dem:(\d+),(\d+)$")


def read_text(relative_path):
    with open(os.path.join(REPO_ROOT, relative_path), "r", encoding="utf-8-sig") as file:
        return file.read()


def header_value(header, name, pattern):
    match = re.search(r"\b" + name + r"\s*=\s*" + pattern, header)
    if not match:
        raise SystemExit(f"error: {HEADER_PATH} に {name} が見つかりません")
    return match.group(1)


def tile_count(vertex_count, tile_cells):
    # WastelandTerrain::DemTileCount と同じ式
    return 0 if vertex_count < 2 else (vertex_count - 2) // tile_cells + 1


def expected_entries():
    header = read_text(HEADER_PATH)
    tile_cells = int(header_value(header, "kDemTileCells", r"(\d+)u"))
    descriptor = header_value(header, "kDemGridDescriptorPath", r'"([^"]+)"')
    prefix = header_value(header, "kDemCookedMeshLogicalPathPrefix", r'"([^"]+)"')
    if not prefix.startswith("Assets/Models/"):
        raise SystemExit(f"error: kDemCookedMeshLogicalPathPrefix が Assets/Models/ で始まりません: {prefix}")

    descriptor_path = "Assets/" + descriptor
    grid = json.loads(read_text(descriptor_path))
    grid_file = os.path.dirname(descriptor_path) + "/" + grid["file"]
    width = int(grid["width"])
    height = int(grid["height"])
    tiles_x = tile_count(width, tile_cells)
    tiles_z = tile_count(height, tile_cells)

    package_prefix = prefix[len("Assets/Models/"):]
    entries = []
    for tile_z in range(tiles_z):
        for tile_x in range(tiles_x):
            name = f"{tile_x}_{tile_z}"
            entries.append({
                "logical_path": f"{prefix}{name}.generated",
                "source_path": descriptor_path,
                "extra_sources": [grid_file, HEADER_PATH],
                "generate": f"terrain-tile:dem:{tile_x},{tile_z}",
                "package_name": f"{package_prefix}{name}.nvpkg",
                "entry_name": f"{prefix}{name}.nvmesh",
            })
    summary = (f"grid={width}x{height} spacing={grid['vertex_spacing_m']} tile_cells={tile_cells} "
               f"tiles={tiles_x}x{tiles_z} entries={len(entries)} grid_sha256={grid['sha256']}")
    return entries, summary


def is_dem_entry(model):
    return isinstance(model, dict) and DEM_GENERATE.match(str(model.get("generate", ""))) is not None


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="荒野の DEM の区画の項目を一覧に書く・確かめる")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--verify", action="store_true")
    args = parser.parse_args()

    entries, summary = expected_entries()
    asset_set = json.loads(read_text(ASSET_SET_PATH))
    models = asset_set.get("models", [])
    print(summary)

    if args.write:
        kept = [model for model in models if not is_dem_entry(model)]
        asset_set["models"] = kept + entries
        text = json.dumps(asset_set, ensure_ascii=False, indent=2) + "\n"
        with open(os.path.join(REPO_ROOT, ASSET_SET_PATH), "w", encoding="utf-8", newline="\r\n") as file:
            file.write(text)
        print(f"wrote {ASSET_SET_PATH} dem_entries={len(entries)} other_models={len(kept)}")
        return 0

    actual = [model for model in models if is_dem_entry(model)]
    failures = []
    if len(actual) != len(entries):
        failures.append(f"DEM の項目の数が違います: 一覧 {len(actual)}・期待 {len(entries)}")
    for index, (got, want) in enumerate(zip(actual, entries)):
        if got != want:
            failures.append(f"DEM の項目 {index} が違います: 一覧 {got.get('generate')}・期待 {want['generate']}")
    for failure in failures[:20]:
        print("FAIL " + failure)
    print(f"dem_entries={len(actual)} expected={len(entries)} fail={len(failures)}")
    print("result=" + ("pass" if not failures else "fail"))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
