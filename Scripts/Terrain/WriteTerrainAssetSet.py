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
# 遠景のメッシュ（Scripts/Terrain/HorizonDem.py の horizon.json）の項目も書く。遠景の項目は source_path が遠景の記述で、
# generate の terrain-tile:dem:<n>,0 が記述の n 番目のメッシュを表す（extra_sources はメッシュのファイル・ヘッダと、
# 縫い目の法線を引く近景の格子）。遠景を作り直したら --write で書き直す。
# 崖の細部を切った撮り比べの一覧 Assets/AssetSets/WastelandTerrainCliffOff.json も書く・確かめる。崖の細部
# （Tools/AssetCook/TerrainCliffDetail.cpp）を焼く範囲に掛かる近景の区画と遠景の castle の段のメッシュだけを、
# generate を terrain-tile:dem-plain:<x>,<z> にして論理パスの置き場 Assets/Models/WastelandTerrainCliffOff/ へ焼く。
# Game は --wasteland-cliff-detail=off のときだけ、この置き場にクック済みがある区画をこちらに差し替える。
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
HORIZON_DESCRIPTOR_PATH = "Assets/Terrain/ValleyOfTheGods/horizon.json"
CLIFF_OFF_ASSET_SET_PATH = "Assets/AssetSets/WastelandTerrainCliffOff.json"
CLIFF_OFF_MODEL_DIR = "Assets/Models/WastelandTerrainCliffOff/"
# Tools/AssetCook/TerrainCliffDetail.cpp の kNearRegions（エンジンの m: MinX, MaxX, MinZ, MaxZ）と窓の余白
# （(kWindowMargin + 1) 升目）。範囲を変えたらここも合わせる（ずれると差し替えの漏れた区画に崖の細部が残る）
CLIFF_NEAR_REGIONS = [
    (-2150.0, -900.0, -1650.0, -420.0),
    (-760.0, -240.0, -660.0, -140.0),
    (640.0, 1160.0, 560.0, 1160.0),
    (960.0, 1460.0, 1140.0, 1960.0),
]
CLIFF_WINDOW_MARGIN_CELLS = 4
CLIFF_HORIZON_LEVEL_PREFIX = "castle_"


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


def horizon_entries():
    header = read_text(HEADER_PATH)
    descriptor = "Assets/" + header_value(header, "kHorizonDescriptorPath", r'"([^"]+)"')
    prefix = header_value(header, "kHorizonCookedMeshLogicalPathPrefix", r'"([^"]+)"')
    grid_descriptor = "Assets/" + header_value(header, "kDemGridDescriptorPath", r'"([^"]+)"')
    if descriptor != HORIZON_DESCRIPTOR_PATH:
        raise SystemExit(f"error: kHorizonDescriptorPath が {HORIZON_DESCRIPTOR_PATH} と違います: {descriptor}")
    document = json.loads(read_text(HORIZON_DESCRIPTOR_PATH))
    grid = json.loads(read_text(grid_descriptor))
    grid_file = os.path.dirname(grid_descriptor) + "/" + grid["file"]
    folder = os.path.dirname(HORIZON_DESCRIPTOR_PATH) + "/" + document["mesh_folder"]
    package_prefix = prefix[len("Assets/Models/"):]
    entries = []
    for index, mesh in enumerate(document["meshes"]):
        name = mesh["name"]
        entries.append({
            "logical_path": f"{prefix}{name}.generated",
            "source_path": HORIZON_DESCRIPTOR_PATH,
            "extra_sources": [f"{folder}/{mesh['file']}", HEADER_PATH, grid_descriptor, grid_file],
            "generate": f"terrain-tile:dem:{index},0",
            "package_name": f"{package_prefix}{name}.nvpkg",
            "entry_name": f"{prefix}{name}.nvmesh",
        })
    summary = f"horizon_meshes={len(entries)} vertices={sum(mesh['vertices'] for mesh in document['meshes'])}"
    return entries, summary


def cliff_off_entries(dem, horizon):
    # 崖の細部を焼く範囲に掛かる近景の区画（TerrainCliffTileIntersects と同じ判定）と、遠景の castle の段の全メッシュ
    grid = json.loads(read_text(dem[0]["source_path"]))
    header = read_text(HEADER_PATH)
    tile_cells = int(header_value(header, "kDemTileCells", r"(\d+)u"))
    spacing = float(grid["vertex_spacing_m"])
    first_x = float(grid["first_vertex_engine"]["x"])
    first_z = float(grid["first_vertex_engine"]["z"])
    cells_x = int(grid["width"]) - 1
    cells_z = int(grid["height"]) - 1
    margin = CLIFF_WINDOW_MARGIN_CELLS * spacing
    picked = []
    for entry in dem:
        tile_x, tile_z = (int(value) for value in DEM_GENERATE.match(entry["generate"]).groups())
        x0 = tile_x * tile_cells
        z0 = tile_z * tile_cells
        min_x = first_x + x0 * spacing
        max_x = first_x + min(x0 + tile_cells, cells_x) * spacing
        min_z = first_z + z0 * spacing
        max_z = first_z + min(z0 + tile_cells, cells_z) * spacing
        if any(max_x >= rx0 - margin and min_x <= rx1 + margin and max_z >= rz0 - margin and min_z <= rz1 + margin
               for rx0, rx1, rz0, rz1 in CLIFF_NEAR_REGIONS):
            picked.append(entry)
    picked += [entry for entry in horizon
               if entry["logical_path"].split("/")[-1].startswith("WastelandHorizon_" + CLIFF_HORIZON_LEVEL_PREFIX)]
    models = []
    for entry in picked:
        name = entry["logical_path"].split("/")[-1].rsplit(".", 1)[0]
        model = dict(entry)
        model["logical_path"] = f"{CLIFF_OFF_MODEL_DIR}{name}.generated"
        model["generate"] = entry["generate"].replace("terrain-tile:dem:", "terrain-tile:dem-plain:")
        model["package_name"] = f"WastelandTerrainCliffOff/{name}.nvpkg"
        model["entry_name"] = f"{CLIFF_OFF_MODEL_DIR}{name}.nvmesh"
        models.append(model)
    return {"version": 1, "name": "WastelandTerrainCliffOff", "package_root": "Cooked/WastelandTerrainCliffOff",
            "default_variant": "default", "cook_assets": True, "textures": [], "models": models}


def is_horizon_entry(model):
    return isinstance(model, dict) and model.get("source_path") == HORIZON_DESCRIPTOR_PATH


def is_dem_entry(model):
    return (isinstance(model, dict) and DEM_GENERATE.match(str(model.get("generate", ""))) is not None
            and not is_horizon_entry(model))


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="荒野の DEM の区画と遠景の項目を一覧に書く・確かめる")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--verify", action="store_true")
    args = parser.parse_args()

    entries, summary = expected_entries()
    horizon, horizon_summary = horizon_entries()
    asset_set = json.loads(read_text(ASSET_SET_PATH))
    models = asset_set.get("models", [])
    print(summary)
    print(horizon_summary)

    if args.write:
        kept = [model for model in models if not is_dem_entry(model) and not is_horizon_entry(model)]
        asset_set["models"] = kept + entries + horizon
        text = json.dumps(asset_set, ensure_ascii=False, indent=2) + "\n"
        with open(os.path.join(REPO_ROOT, ASSET_SET_PATH), "w", encoding="utf-8", newline="\r\n") as file:
            file.write(text)
        print(f"wrote {ASSET_SET_PATH} dem_entries={len(entries)} horizon_entries={len(horizon)} other_models={len(kept)}")
        cliff_off = cliff_off_entries(entries, horizon)
        with open(os.path.join(REPO_ROOT, CLIFF_OFF_ASSET_SET_PATH), "w", encoding="utf-8", newline="\r\n") as file:
            file.write(json.dumps(cliff_off, ensure_ascii=False, indent=2) + "\n")
        print(f"wrote {CLIFF_OFF_ASSET_SET_PATH} models={len(cliff_off['models'])}")
        return 0

    failures = []
    actual = [model for model in models if is_dem_entry(model)]
    actual_horizon = [model for model in models if is_horizon_entry(model)]
    for label, got_entries, want_entries in (("DEM", actual, entries), ("遠景", actual_horizon, horizon)):
        if len(got_entries) != len(want_entries):
            failures.append(f"{label} の項目の数が違います: 一覧 {len(got_entries)}・期待 {len(want_entries)}")
        for index, (got, want) in enumerate(zip(got_entries, want_entries)):
            if got != want:
                failures.append(f"{label} の項目 {index} が違います: 一覧 {got.get('logical_path')}・期待 {want['logical_path']}")
    cliff_off_path = os.path.join(REPO_ROOT, CLIFF_OFF_ASSET_SET_PATH)
    if not os.path.exists(cliff_off_path) or json.loads(read_text(CLIFF_OFF_ASSET_SET_PATH)) != cliff_off_entries(entries, horizon):
        failures.append(f"{CLIFF_OFF_ASSET_SET_PATH} が今の格子と遠景から求めたものと違います（--write で書き直す）")
    for failure in failures[:20]:
        print("FAIL " + failure)
    print(f"dem_entries={len(actual)} expected={len(entries)} horizon_entries={len(actual_horizon)} "
          f"expected={len(horizon)} fail={len(failures)}")
    print("result=" + ("pass" if not failures else "fail"))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
