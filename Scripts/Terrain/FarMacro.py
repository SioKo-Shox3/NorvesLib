# 荒野の遠景の地形を描くための、前もって縮小した大きな地図（遠景の地図）を DEM と地表の分類から焼く。
# 遠くの地形は、模様の 1 枚も DEM の三角形も画面で数画素以下に縮み、画素ごとの幾何の法線と傾斜の式の分類が飛び飛びになって
# 斑点とちらつきになる。そこで 16 m の画素に、その画素の中の岩肌・土の割合と平均の法線を前もって入れておき、シェーダーは
# 遠くでこの地図（ミップで縮小済み）から分類と法線を引く（Assets/Shaders/Common/TerrainLayerBlend.glsl）。
#
# 範囲: 遠景の outer の段（HorizonDem.py。1 m の 4 枚の外へ西 2 km・東 25 km・南 30 km・北 27 km）。その外の rim の段は
# 地図の外で、シェーダーは従来の傾斜の式で描く（30 km より先で、DEM の 128 m の三角形が画面で数画素ある）。
# 求め方（8 m の升目で計算し、2×2 をまとめて 16 m の画素にする）:
#   - 高さ: 1 m の 4 枚と 1/3 秒角の 2 枚を 8 m の升目へ面積の平均で写す（4 枚のある所は 4 枚を使う）。近景の外の地球の丸みの
#     下がりと高さの基準（height.json の height_offset_m）を引いて、ワールドの y にする（HorizonDem.py と同じ）
#   - 分類: 中心差分の傾斜から、シェーダーの傾斜の式（ComputeTerrainSlopeWeights。揺らぎのノイズを除く）と同じ式・同じ値
#     （Game/GameModes/Wasteland/WastelandRoutine.cpp の kDirtSlopeDegrees・kRockSlopeDegrees・kDirtHeightRange・
#     kDirtHeightBiasDegrees）で砂・土・岩肌の重みを求める。地表の分類の地図（surface.png）の範囲は、その地図の重みを
#     シェーダーと同じ写し方（礫の半分を土へ）で使う
#   - 法線: 8 m の升目の法線の 2×2 の平均を正規化する
# 画素: R・G = 法線の X・Z（-1〜1 を 0〜255）、B = 岩肌の重み、A = 土の重み（砂は 1 - B - A）。線形の RGBA8。
# DEM の無い所（1/3 秒角の 2 枚の外）は真上の法線と砂にする（その位置に地形は無い）。
#
# 出力（git 管理外）: Assets/Terrain/ValleyOfTheGods/far.png・far.json（記述。surface.json と同じ並びの外縁・画素の広さ）と、
# 確かめる図 preview/far_map.png（分類の色に法線の陰影を掛けた縮小図）。
#
# 使い方:
#   python Scripts/Terrain/FarMacro.py --build    地図・記述・図を書く
#   python Scripts/Terrain/FarMacro.py --verify   作り直して書いたものと一致するか、重みと法線の範囲、主な崖（Castle Butte・
#                                                 Battleship Rock）に岩肌が出るか、平らな谷床が砂か、を確かめる
# 終了コード: すべて満たせば 0、どれかが欠ければ 1。
import argparse
import hashlib
import json
import math
import os
import sys

import numpy

from DemInput import DEM_PATH, NEIGHBOR_TILES, REGIONAL_TILES, SOURCE_DIR
from HorizonDem import EARTH_RADIUS_M, REFRACTION_COEFFICIENT, load_near, make_levels
from ReferencePhotos import REPO_ROOT

OUT_DIR = os.path.join(REPO_ROOT, "Assets", "Terrain", "ValleyOfTheGods")
MAP_PATH = os.path.join(OUT_DIR, "far.png")
MAP_JSON_PATH = os.path.join(OUT_DIR, "far.json")
SURFACE_PATH = os.path.join(OUT_DIR, "surface.png")
SURFACE_JSON_PATH = os.path.join(OUT_DIR, "surface.json")
PREVIEW_PATH = os.path.join(OUT_DIR, "preview", "far_map.png")

# エンジン（TerrainHeightField.cpp の LoadTerrainFarMapLayout）が照合する形式とチャンネルの名前
FORMAT = "PNG RGBA8, row 0 = north (engine z min), column 0 = west (engine x min)"
CHANNELS = {"r": "normal_x", "g": "normal_z", "b": "rock", "a": "dirt"}

# 地図の画素と、計算の升目の広さ[m]
TEXEL_M = 16
CELL_M = 8
# シェーダーの傾斜の式の値（WastelandRoutine.cpp と同じ）
DIRT_SLOPE_DEG = (24.0, 33.0)
ROCK_SLOPE_DEG = (37.0, 48.0)
DIRT_HEIGHT_RANGE_M = (12.0, 50.0)
DIRT_HEIGHT_BIAS_DEG = 10.0
# 地表の分類の地図の礫を土へ回す割合（WastelandRoutine.cpp の kSurfaceMaskGravelToDirt）
GRAVEL_TO_DIRT = 0.5

# 確かめる所（エンジンの座標 x・z、半径[m]）。崖は岩肌が、谷床は砂が多いはず。Castle Butte の塔は約 400 × 150 m と細いので、
# 半径 100 m（250 m では周りの平らな谷床が大半になり、塔の岩肌が平均で薄まる）
CHECK_CLIFFS = {
    "Castle Butte": (-2943.5, -3370.5, 100.0),
    "Battleship Rock": (-1420.0, -949.0, 150.0),
}
CHECK_FLOORS = {
    "前哨地の谷床": (1199.5, 150.5, 150.0),
}


def smoothstep(value, edge0, edge1):
    t = numpy.clip((value - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def map_edges(near):
    """地図の外縁（エンジンの座標）。outer の段の頂点の範囲を、画素の広さの格子で覆う"""
    outer = make_levels(near)[4]
    columns = int(math.ceil((outer.x1 - outer.x0) / TEXEL_M))
    rows = int(math.ceil((outer.z1 - outer.z0) / TEXEL_M))
    return {"x_min": float(outer.x0), "x_max": float(outer.x0 + columns * TEXEL_M),
            "z_min": float(outer.z0), "z_max": float(outer.z0 + rows * TEXEL_M)}, columns, rows


def read_heights(near, edges, columns, rows):
    """8 m の升目の標高（NAVD88。行 0 が北 = エンジンの z の最小）。1/3 秒角を写してから、4 枚のある所を 4 枚で上書きする"""
    import rasterio
    from rasterio.crs import CRS
    from rasterio.enums import Resampling
    from rasterio.transform import Affine
    from rasterio.warp import reproject
    origin = near["origin"]
    cell_columns = columns * (TEXEL_M // CELL_M)
    cell_rows = rows * (TEXEL_M // CELL_M)
    # 升目 (列 i, 行 j) の左上の角は E = origin.east + x_min + 8i、N = origin.north - z_min - 8j
    destination = Affine(CELL_M, 0.0, origin["east"] + edges["x_min"], 0.0, -CELL_M, origin["north"] - edges["z_min"])
    heights = numpy.full((cell_rows, cell_columns), numpy.nan, dtype=numpy.float32)
    utm = CRS.from_epsg(26912)
    for tile in REGIONAL_TILES:
        with rasterio.open(os.path.join(SOURCE_DIR, tile["file"])) as dataset:
            source = dataset.read(1).astype(numpy.float32)
            source[~(source > -1000.0)] = numpy.nan
            part = numpy.full_like(heights, numpy.nan)
            reproject(source, part, src_transform=dataset.transform, src_crs=dataset.crs, src_nodata=numpy.nan,
                      dst_transform=destination, dst_crs=utm, dst_nodata=numpy.nan, resampling=Resampling.average)
            heights = numpy.where(numpy.isfinite(part), part, heights)
            del source, part
    for path in [DEM_PATH] + [os.path.join(SOURCE_DIR, tile["file"]) for tile in NEIGHBOR_TILES]:
        with rasterio.open(path) as dataset:
            source = dataset.read(1).astype(numpy.float32)
            source[~(source > -1000.0)] = numpy.nan
            part = numpy.full_like(heights, numpy.nan)
            reproject(source, part, src_transform=dataset.transform, src_crs=dataset.crs, src_nodata=numpy.nan,
                      dst_transform=destination, dst_crs=utm, dst_nodata=numpy.nan, resampling=Resampling.average)
            heights = numpy.where(numpy.isfinite(part), part, heights)
            del source, part
    return heights


def world_heights(near, edges, heights):
    """標高からワールドの y へ: 近景の外周からの水平の距離で地球の丸みの下がりを引き、高さの基準を引く（HorizonDem.py と同じ）"""
    rows, columns = heights.shape
    xs = edges["x_min"] + (numpy.arange(columns) + 0.5) * CELL_M
    zs = edges["z_min"] + (numpy.arange(rows) + 0.5) * CELL_M
    dx = numpy.maximum(numpy.maximum(near["x0"] - xs, xs - near["x1"]), 0.0)
    dz = numpy.maximum(numpy.maximum(near["z0"] - zs, zs - near["z1"]), 0.0)
    drop = numpy.add.outer(dz * dz, dx * dx) / (2.0 * EARTH_RADIUS_M) * (1.0 - REFRACTION_COEFFICIENT)
    return (heights - drop - near["offset"]).astype(numpy.float32)


def cell_normals(y):
    """升目の法線（中心差分。端と無効な隣の側は自分の高さを使う片側の差分。無効な升目は真上）。行は +Z、列は +X"""
    h = y.astype(numpy.float64)

    def neighbor(shift, axis):
        shifted = numpy.roll(h, shift, axis=axis)
        edge = [slice(None)] * 2
        edge[axis] = 0 if shift > 0 else -1
        shifted[tuple(edge)] = numpy.nan
        valid = numpy.isfinite(shifted)
        return numpy.where(valid, shifted, h), valid.astype(numpy.float64)

    left, has_left = neighbor(1, 1)
    right, has_right = neighbor(-1, 1)
    up, has_up = neighbor(1, 0)
    down, has_down = neighbor(-1, 0)
    span_x = numpy.maximum(has_left + has_right, 1.0) * CELL_M
    span_z = numpy.maximum(has_up + has_down, 1.0) * CELL_M
    valid = numpy.isfinite(h)
    slope_x = numpy.where(valid, (right - left) / span_x, 0.0)
    slope_z = numpy.where(valid, (down - up) / span_z, 0.0)
    length = numpy.sqrt(slope_x * slope_x + slope_z * slope_z + 1.0)
    return -slope_x / length, 1.0 / length, -slope_z / length, valid


def slope_weights(y, ny, valid):
    """シェーダーの傾斜の式（揺らぎのノイズを除く）で、砂・土・岩肌の重み"""
    slope = numpy.arccos(numpy.clip(ny, -1.0, 1.0))
    bias = smoothstep(numpy.nan_to_num(y), *DIRT_HEIGHT_RANGE_M) * math.radians(DIRT_HEIGHT_BIAS_DEG)
    dirt = smoothstep(slope + bias, math.radians(DIRT_SLOPE_DEG[0]), math.radians(DIRT_SLOPE_DEG[1]))
    rock = smoothstep(slope, math.radians(ROCK_SLOPE_DEG[0]), math.radians(ROCK_SLOPE_DEG[1]))
    sand = (1.0 - dirt) * (1.0 - rock)
    dirt = dirt * (1.0 - rock)
    sand = numpy.where(valid, sand, 1.0)
    return sand, numpy.where(valid, dirt, 0.0), numpy.where(valid, rock, 0.0)


def apply_surface_mask(edges, sand, dirt, rock):
    """地表の分類の地図の範囲の升目を、その地図の重み（升目の中の画素の平均）で置き換える"""
    from PIL import Image
    with open(SURFACE_JSON_PATH, encoding="utf-8-sig") as stream:
        surface = json.load(stream)
    texel = float(surface["texel_size_m"])
    if CELL_M % texel:
        raise RuntimeError("地表の分類の地図の画素（%r m）が升目の約数でない" % texel)
    factor = int(CELL_M // texel)
    classes = numpy.asarray(Image.open(SURFACE_PATH).convert("RGBA"), dtype=numpy.float64) / 255.0
    se = surface["engine_edges"]
    # 地図の外縁が升目の境に乗る範囲だけ使う
    column0 = (se["x_min"] - edges["x_min"]) / CELL_M
    row0 = (se["z_min"] - edges["z_min"]) / CELL_M
    first_column = int(math.ceil(column0 - 1e-9))
    first_row = int(math.ceil(row0 - 1e-9))
    skip_x = int(round((first_column - column0) * factor))
    skip_z = int(round((first_row - row0) * factor))
    count_x = (classes.shape[1] - skip_x) // factor
    count_z = (classes.shape[0] - skip_z) // factor
    block = classes[skip_z:skip_z + count_z * factor, skip_x:skip_x + count_x * factor]
    block = block.reshape(count_z, factor, count_x, factor, 4).mean(axis=(1, 3))
    # 4 つの重みの和が 1 に足りない分は地図の砂（沢底と吹きだまり）で、砂の枠へ入れる
    r, g, b, a = (block[:, :, index] for index in range(4))
    wash = numpy.clip(1.0 - block.sum(axis=2), 0.0, 1.0)
    target = (slice(first_row, first_row + count_z), slice(first_column, first_column + count_x))
    sand[target] = b + a * (1.0 - GRAVEL_TO_DIRT) + wash
    dirt[target] = g + a * GRAVEL_TO_DIRT
    rock[target] = r
    return {"rows": [first_row, first_row + count_z], "columns": [first_column, first_column + count_x]}


def pool2(values):
    rows, columns = values.shape
    return values.reshape(rows // 2, 2, columns // 2, 2).mean(axis=(1, 3))


def compute():
    near = load_near()
    edges, columns, rows = map_edges(near)
    elevation = read_heights(near, edges, columns, rows)
    y = world_heights(near, edges, elevation)
    del elevation
    nx, ny, nz, valid = cell_normals(y)
    sand, dirt, rock = slope_weights(y, ny, valid)
    mask_cells = apply_surface_mask(edges, sand, dirt, rock)
    coverage = float(valid.mean())
    del sand
    nx, ny, nz = pool2(nx), pool2(ny), pool2(nz)
    length = numpy.maximum(numpy.sqrt(nx * nx + ny * ny + nz * nz), 1e-6)
    nx, nz = nx / length, nz / length
    dirt, rock = pool2(dirt), pool2(rock)
    pixels = numpy.empty((rows, columns, 4), dtype=numpy.uint8)
    pixels[:, :, 0] = numpy.clip(numpy.round((nx * 0.5 + 0.5) * 255.0), 0, 255)
    pixels[:, :, 1] = numpy.clip(numpy.round((nz * 0.5 + 0.5) * 255.0), 0, 255)
    pixels[:, :, 2] = numpy.clip(numpy.round(rock * 255.0), 0, 255)
    # 岩肌と土の和が 255 を超えないよう、土を残りで切る
    pixels[:, :, 3] = numpy.minimum(numpy.clip(numpy.round(dirt * 255.0), 0, 255), 255 - pixels[:, :, 2].astype(numpy.int32))
    stats = {"coverage": round(coverage, 4), "mask_cells": mask_cells}
    return near, edges, columns, rows, pixels, stats


def png_bytes(pixels):
    import io
    from PIL import Image
    stream = io.BytesIO()
    Image.fromarray(pixels, "RGBA").save(stream, format="PNG", compress_level=6)
    return stream.getvalue()


def descriptor_for(edges, columns, rows, sha, stats):
    return {
        "format": FORMAT,
        "file": os.path.basename(MAP_PATH),
        "channels": CHANNELS,
        "channels_note": "r・g = 16 m の画素の平均の法線の X・Z（-1〜1 を 0〜255）、b = 岩肌の重み、a = 土の重み（砂は 1 - b - a）",
        "width": columns,
        "height": rows,
        "texel_size_m": float(TEXEL_M),
        "engine_edges": edges,
        "uv_note": "u = (x - x_min) / (x_max - x_min)、v = (z - z_min) / (z_max - z_min)",
        "sha256": sha,
        "parameters": {
            "cell_m": CELL_M,
            "dirt_slope_deg": list(DIRT_SLOPE_DEG),
            "rock_slope_deg": list(ROCK_SLOPE_DEG),
            "dirt_height_range_m": list(DIRT_HEIGHT_RANGE_M),
            "dirt_height_bias_deg": DIRT_HEIGHT_BIAS_DEG,
            "gravel_to_dirt": GRAVEL_TO_DIRT,
        },
        "stats": stats,
    }


def write_preview(pixels):
    from PIL import Image
    data = pixels.astype(numpy.float64) / 255.0
    nx, nz = data[:, :, 0] * 2.0 - 1.0, data[:, :, 1] * 2.0 - 1.0
    ny = numpy.sqrt(numpy.clip(1.0 - nx * nx - nz * nz, 0.0, 1.0))
    # 西北西の上からの光
    light = numpy.array([-0.6, 0.6, -0.5])
    light /= numpy.linalg.norm(light)
    shade = numpy.clip(nx * light[0] + ny * light[1] + nz * light[2], 0.0, 1.0) * 0.8 + 0.2
    rock, dirt = data[:, :, 2], data[:, :, 3]
    sand = numpy.clip(1.0 - rock - dirt, 0.0, 1.0)
    color = (rock[..., None] * numpy.array([0.75, 0.3, 0.2]) + dirt[..., None] * numpy.array([0.75, 0.6, 0.4])
             + sand[..., None] * numpy.array([0.95, 0.85, 0.6])) * shade[..., None]
    image = Image.fromarray(numpy.clip(color * 255.0, 0, 255).astype(numpy.uint8), "RGB")
    image.thumbnail((1200, 1200))
    os.makedirs(os.path.dirname(PREVIEW_PATH), exist_ok=True)
    image.save(PREVIEW_PATH)


def build():
    near, edges, columns, rows, pixels, stats = compute()
    data = png_bytes(pixels)
    sha = hashlib.sha256(data).hexdigest()
    with open(MAP_PATH, "wb") as stream:
        stream.write(data)
    descriptor = descriptor_for(edges, columns, rows, sha, stats)
    with open(MAP_JSON_PATH, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(descriptor, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    write_preview(pixels)
    print("FAR_MACRO wrote %s size=%dx%d texel=%dm x=[%.1f,%.1f] z=[%.1f,%.1f] coverage=%.4f sha256=%s"
          % (os.path.relpath(MAP_PATH, REPO_ROOT), columns, rows, TEXEL_M, edges["x_min"], edges["x_max"],
             edges["z_min"], edges["z_max"], stats["coverage"], sha[:16]))
    return 0


def region_mean(pixels, edges, x, z, radius, channel):
    column = int((x - edges["x_min"]) / TEXEL_M)
    row = int((z - edges["z_min"]) / TEXEL_M)
    span = int(math.ceil(radius / TEXEL_M))
    block = pixels[max(row - span, 0):row + span + 1, max(column - span, 0):column + span + 1, channel]
    return float(block.mean()) / 255.0


def verify():
    failures = []

    def check(condition, message):
        print("FAR_MACRO %s %s" % ("ok" if condition else "FAIL", message))
        if not condition:
            failures.append(message)

    near, edges, columns, rows, pixels, stats = compute()
    data = png_bytes(pixels)
    sha = hashlib.sha256(data).hexdigest()
    with open(MAP_JSON_PATH, encoding="utf-8") as stream:
        descriptor = json.load(stream)
    with open(MAP_PATH, "rb") as stream:
        written = hashlib.sha256(stream.read()).hexdigest()
    check(descriptor.get("sha256") == sha == written, "作り直しが書いた地図と一致する（%s）" % sha[:16])
    check(descriptor.get("format") == FORMAT and descriptor.get("channels") == CHANNELS, "記述の形式とチャンネルが約束どおり")
    check(descriptor.get("width") == columns and descriptor.get("height") == rows and columns <= 16384 and rows <= 16384,
          "大きさ %dx%d が記述と同じで 16384 以下" % (columns, rows))
    check(abs((edges["x_max"] - edges["x_min"]) - columns * TEXEL_M) < 1e-6
          and abs((edges["z_max"] - edges["z_min"]) - rows * TEXEL_M) < 1e-6, "外縁の幅 = 画素の数 × 16 m")
    weight_sum = pixels[:, :, 2].astype(numpy.int32) + pixels[:, :, 3].astype(numpy.int32)
    check(int(weight_sum.max()) <= 255, "岩肌と土の重みの和が 1 以下（最大 %d/255）" % int(weight_sum.max()))
    nx = pixels[:, :, 0].astype(numpy.float64) / 127.5 - 1.0
    nz = pixels[:, :, 1].astype(numpy.float64) / 127.5 - 1.0
    horizontal = numpy.sqrt(nx * nx + nz * nz)
    check(float(horizontal.max()) <= 1.01, "法線の水平成分が 1 以下（最大 %.3f）" % float(horizontal.max()))
    check(stats["coverage"] > 0.98, "DEM のある升目の割合 %.4f > 0.98（西の端は 1/3 秒角の 2 枚の外）" % stats["coverage"])
    for name, (x, z, radius) in CHECK_CLIFFS.items():
        value = region_mean(pixels, edges, x, z, radius, 2)
        check(value > 0.15, "%s の周り %d m の岩肌の重みの平均 %.3f > 0.15" % (name, radius, value))
    for name, (x, z, radius) in CHECK_FLOORS.items():
        rock = region_mean(pixels, edges, x, z, radius, 2)
        check(rock < 0.05, "%s の周り %d m の岩肌の重みの平均 %.3f < 0.05" % (name, radius, rock))
    rock_share = float(pixels[:, :, 2].mean()) / 255.0
    check(0.01 < rock_share < 0.5, "地図全体の岩肌の重みの平均 %.3f が 0.01〜0.5" % rock_share)
    print("FAR_MACRO result=%s failures=%d" % ("pass" if not failures else "fail", len(failures)))
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser(description="荒野の遠景の地図（分類の重みと法線）を DEM と地表の分類から焼く")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--build", action="store_true", help="地図・記述・図を書く")
    group.add_argument("--verify", action="store_true", help="作り直して一致と値の範囲を確かめる")
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if args.build:
        return build()
    return verify()


if __name__ == "__main__":
    sys.exit(main())
