# 荒野（Valley of the Gods）の遠景の地形を、1 m の DEM タイル 4 枚（E 590000〜610000・N 4120000〜4140000）と、
# その外を覆う 1/3 秒角（約 10 m 間隔）の DEM 2 枚から焼く。
# 近景（CropDem.py の 1 m の格子）の外側を、間隔の違う 4 つの段で覆い、段の間を縫い目の帯でつなぐ:
#   castle: Castle Butte と、その手前の谷床（手本の写真 far_01 の主役）。2 m
#   mid:    近景の周り 6 km（4 枚の内側）。8 m。穴は近景と castle
#   far:    20 km 四方の残り。16 m。穴は mid。4 枚の NoData（x60y413 の南東の角）は 1/3 秒角で埋める
#   outer:  20 km 四方の外（東 25 km・南 30 km・北 27 km。西は 1/3 秒角の西の端 西経 110° まで）。32 m。穴は far。
#           ridge・outpost から見える東・南の地平線と、北の Bears Ears を覆う
#   rim:    1/3 秒角の 2 枚の残り（西経 109〜110°・北緯 36〜38°）。128 m。穴は outer。高い視点（ridge）から 30 km より先に
#           見える地平線（Comb Ridge の東の台地、北の Abajo Mountains など）
# 縫い目の帯は、内側の段の外周の頂点と、外側の段の穴の縁の頂点（内側の外周から 1 升目以上離れた格子の線）を、
# 辺に沿った位置の順に交互に結んだ三角形の列。帯の両端の頂点は、どちらも DEM の画素の値そのもの（近景の側は近景の
# 格子の値）なので、帯の両端で高さの段差も隙間もできない。各段の高さは、縫い目から間隔の 4 倍の距離までは DEM の
# 画素の値そのもの（点）から、それより外は間隔ほどの箱の平均（折り返しを抑える）へ移す（近景の外周の高さへ寄せる帯）。
# 端の高さを外へ延ばす作り方はしない。1/3 秒角の 2 枚の外（西経 110° より西など）は、地形を作らない。
# 地球の丸み: 近景より外の段の高さから、近景の外周からの水平の距離 d で d^2 / 2R × (1 - 屈折係数) を引く（近景の中の視点から
# 見た地平線の下がり。30 km 先で約 60 m）。近景の外周で 0 なので、近景との縫い目に段差は出ない。
#
# 出力（git 管理外）: Assets/Terrain/ValleyOfTheGods/horizon.json（段・メッシュの一覧・SHA-256）と horizon/<名前>.bin。
# メッシュの .bin は頂点ごとに float32 × 8（位置 xyz・法線 xyz・UV）を並べ、続けて三角形ごとに uint32 × 3 を並べる。
# 法線が NaN の頂点は近景の外周の頂点で、クッカー（AssetCook）が近景の区画と同じ式（TerrainHeightField::SampleNormal）で埋める。
# 確かめる図: preview/horizon_levels.png（段の範囲と縫い目を陰影図に重ねた図）。
#
# 使い方:
#   python Scripts/Terrain/HorizonDem.py --build    遠景のメッシュ・記述・図を書く
#   python Scripts/Terrain/HorizonDem.py --verify   作り直して一致を確かめ、近景の外周を含めた全体で、開いた辺が
#                                                   DEM の外の縁にしか無いこと・同じ位置の頂点の高さが一致することを確かめる
# 入力: DemInput.py が落とした 1 m の 4 枚と 1/3 秒角の 2 枚（Assets/Terrain/Source/）と、CropDem.py の近景の格子。終了コード: 満たせば 0、欠ければ 1。
import argparse
import datetime
import hashlib
import json
import math
import os
import subprocess
import sys

from DemInput import DEM_PATH, MANIFEST_PATH as SOURCE_MANIFEST_PATH, NEIGHBOR_TILES, REGIONAL_TILES, SOURCE_DIR
from ReferencePhotos import FEATURES, REPO_ROOT, sha256_of, utm_from_latlon

OUT_DIR = os.path.join(REPO_ROOT, "Assets", "Terrain", "ValleyOfTheGods")
NEAR_JSON_PATH = os.path.join(OUT_DIR, "height.json")
DESCRIPTOR_PATH = os.path.join(OUT_DIR, "horizon.json")
MESH_FOLDER = "horizon"
MESH_DIR = os.path.join(OUT_DIR, MESH_FOLDER)
PREVIEW_PATH = os.path.join(OUT_DIR, "preview", "horizon_levels.png")
# 記述の中の道はアセットの根（Assets/）から
ASSET_PREFIX = "Terrain/ValleyOfTheGods/"

FORMAT = "norveslib.terrain-horizon.v1"
MESH_FORMAT = "float32 x8 per vertex (position xyz, normal xyz, uv), then uint32 x3 per triangle, little-endian, no header"

# 4 枚を並べた範囲（画素の外縁、UTM 12N）
MOSAIC = {"east_min": 590000, "east_max": 610000, "north_min": 4120000, "north_max": 4140000}
# castle の段の範囲（エンジンの座標の頂点の外周、m）。Castle Butte（E 601557・N 4130371、裾 E 601050〜601850・N 4130250〜4130650）と、
# far_01 の視点（E 603537・N 4128549）から北西を見た画角（方位 300.5〜323.5°）の、近景の北の縁から 4 km までの谷床を覆う。
# 南の縁は近景の北の縁から 16 m 北（間に mid の格子の線 1 本を挟む）
CASTLE_RECT = {"x0": -4100.0, "x1": -1500.0, "z0": -4199.0, "z1": -2515.0}
# mid の段は近景の外周から各方向にこの距離まで（4 枚の内側に、far の縫い目の帯が入る 2 升目を残して切る）
MID_EXPAND_M = 6000.0
# outer の段は 4 枚の外縁から各方向にこの距離まで[m]。東・南は ridge（南東を見る）と outpost（南を見る）の地平線、
# 北は Bears Ears（N 約 4164800）まで。西は 1/3 秒角の西の端（西経 110°、E 588300〜589000）で切れる
OUTER_EXPAND_M = {"west": 2000, "east": 25000, "south": 30000, "north": 27000}
# 段の間隔[m]
SPACINGS = {"castle": 2, "mid": 8, "far": 16, "outer": 32, "rim": 128}
# rim の段の範囲（1/3 秒角の 2 枚を合わせた経緯度の範囲。UTM では四隅を囲む長方形にし、外は NaN）
RIM_LONLAT = {"lon_min": -110.0, "lon_max": -109.0, "lat_min": 36.0, "lat_max": 38.0}
# 地球の半径[m]と、大気の屈折係数（測量の標準値。見かけの丸みを 13 % 減らす）
EARTH_RADIUS_M = 6371000.0
REFRACTION_COEFFICIENT = 0.13
# 縫い目から箱の平均へ移る距離（間隔の倍数）
BAND_SPACINGS = 4
# メッシュに分ける区画の一辺の升目の数（近景の区画と同じ）
TILE_CELLS = 500
# 検証で位置を整数の鍵にするときの X・Z のずらし[m]（rim の Z は -80 km〜+142 km）
KEY_OFFSET = 200000
# 法線を引く近景の格子の頂点の印
NEAR_NORMAL_FROM_GRID = float("nan")


def load_near():
    import numpy
    with open(NEAR_JSON_PATH, encoding="utf-8") as stream:
        document = json.load(stream)
    width, height = document["width"], document["height"]
    raw = numpy.fromfile(os.path.join(OUT_DIR, document["file"]), dtype="<f4").reshape(height, width)
    first = document["first_vertex_engine"]
    return {
        "document": document,
        "heights": raw,
        "x0": first["x"], "z0": first["z"],
        "x1": first["x"] + (width - 1), "z1": first["z"] + (height - 1),
        "spacing": 1,
        "origin": document["origin_utm"],
        "offset": document["height_offset_m"],
    }


class Level:
    """1 つの段: 格子の頂点の範囲（エンジンの座標）・間隔・高さ・法線・有効な頂点・穴（内側の段）"""

    def __init__(self, name, spacing, x0, x1, z0, z1):
        self.name = name
        self.spacing = spacing
        self.x0, self.x1, self.z0, self.z1 = x0, x1, z0, z1
        self.columns = int(round((x1 - x0) / spacing)) + 1
        self.rows = int(round((z1 - z0) / spacing)) + 1
        self.children = []
        self.parent = None
        self.heights = None
        self.normals = None

    def x_of(self, column):
        return self.x0 + column * self.spacing

    def z_of(self, row):
        return self.z0 + row * self.spacing


def lattice_floor(value, anchor, spacing):
    return anchor + math.floor((value - anchor) / spacing + 1e-9) * spacing


def lattice_ceil(value, anchor, spacing):
    return anchor + math.ceil((value - anchor) / spacing - 1e-9) * spacing


def make_levels(near):
    """段の範囲を、近景の最初の頂点を基点にした各段の間隔の格子に乗せて決める"""
    ax, az = near["x0"], near["z0"]
    origin = near["origin"]
    # 4 枚の画素の中心の範囲（エンジンの座標）
    mx0 = MOSAIC["east_min"] + 0.5 - origin["east"]
    mx1 = MOSAIC["east_max"] - 0.5 - origin["east"]
    mz0 = -(MOSAIC["north_max"] - 0.5 - origin["north"])
    mz1 = -(MOSAIC["north_min"] + 0.5 - origin["north"])

    near_level = Level("near", 1, near["x0"], near["x1"], near["z0"], near["z1"])
    castle = Level("castle", SPACINGS["castle"], CASTLE_RECT["x0"], CASTLE_RECT["x1"], CASTLE_RECT["z0"], CASTLE_RECT["z1"])
    for value, anchor in ((castle.x0, ax), (castle.x1, ax), (castle.z0, az), (castle.z1, az)):
        if (value - anchor) % castle.spacing:
            raise RuntimeError("castle の範囲が 2 m の格子に乗らない: %r" % CASTLE_RECT)
    s = SPACINGS["far"]
    far = Level("far", s, lattice_ceil(mx0, ax, s), lattice_floor(mx1, ax, s), lattice_ceil(mz0, az, s), lattice_floor(mz1, az, s))
    # mid は far の格子の 2 升目（縫い目の帯）を残して far の内側に収める
    keep = 2 * SPACINGS["far"]
    s = SPACINGS["mid"]
    mid = Level("mid", s,
                max(lattice_ceil(far.x0 + keep, ax, s), lattice_ceil(near["x0"] - MID_EXPAND_M, ax, s)),
                min(lattice_floor(far.x1 - keep, ax, s), lattice_floor(near["x1"] + MID_EXPAND_M, ax, s)),
                max(lattice_ceil(far.z0 + keep, az, s), lattice_ceil(near["z0"] - MID_EXPAND_M, az, s)),
                min(lattice_floor(far.z1 - keep, az, s), lattice_floor(near["z1"] + MID_EXPAND_M, az, s)))
    s = SPACINGS["outer"]
    # 東 = +X、北 = -Z
    outer = Level("outer", s,
                  lattice_ceil(mx0 - OUTER_EXPAND_M["west"], ax, s), lattice_floor(mx1 + OUTER_EXPAND_M["east"], ax, s),
                  lattice_ceil(mz0 - OUTER_EXPAND_M["north"], az, s), lattice_floor(mz1 + OUTER_EXPAND_M["south"], az, s))
    from rasterio.crs import CRS
    from rasterio.warp import transform as warp_transform
    lons = [RIM_LONLAT["lon_min"], RIM_LONLAT["lon_max"]] * 2
    lats = [RIM_LONLAT["lat_min"]] * 2 + [RIM_LONLAT["lat_max"]] * 2
    corner_e, corner_n = warp_transform(CRS.from_epsg(4269), CRS.from_epsg(26912), lons, lats)
    s = SPACINGS["rim"]
    rim = Level("rim", s,
                lattice_ceil(min(corner_e) - origin["east"], ax, s), lattice_floor(max(corner_e) - origin["east"], ax, s),
                lattice_ceil(origin["north"] - max(corner_n), az, s), lattice_floor(origin["north"] - min(corner_n), az, s))
    mid.children = [near_level, castle]
    far.children = [mid]
    outer.children = [far]
    rim.children = [outer]
    for child in mid.children:
        child.parent = mid
    mid.parent = far
    far.parent = outer
    outer.parent = rim
    return near_level, castle, mid, far, outer, rim


def ring_of(parent, child):
    """内側の段 child を囲む、外側の段 parent の格子の線（穴の縁）の番号の範囲。内側の外周から 1 升目以上離す。
    範囲が parent の外にはみ出すことがある（4 枚の端に接する辺。その辺の縫い目は作らない）"""
    s = parent.spacing
    c0 = int(math.floor((child.x0 - s - parent.x0) / s + 1e-9))
    c1 = int(math.ceil((child.x1 + s - parent.x0) / s - 1e-9))
    r0 = int(math.floor((child.z0 - s - parent.z0) / s + 1e-9))
    r1 = int(math.ceil((child.z1 + s - parent.z0) / s - 1e-9))
    return c0, c1, r0, r1


def read_mosaic(east_min, east_max, north_min, north_max):
    """4 枚を並べて、範囲（画素の外縁）の 1 m の高さを NaN 入りの float32 で返す（行 0 が北）"""
    import numpy
    import rasterio
    from rasterio.merge import merge
    paths = [DEM_PATH] + [os.path.join(SOURCE_DIR, tile["file"]) for tile in NEIGHBOR_TILES]
    datasets = [rasterio.open(path) for path in paths]
    try:
        array, transform = merge(datasets, bounds=(east_min, north_min, east_max, north_max), res=1, nodata=-999999.0)
    finally:
        for dataset in datasets:
            dataset.close()
    block = array[0].astype(numpy.float32)
    block[~(block > -1000.0)] = numpy.nan
    if abs(transform.c - east_min) > 1e-3 or abs(transform.f - north_max) > 1e-3:
        raise RuntimeError("並べた範囲の左上 (%r, %r) が求めた外縁と違う" % (transform.c, transform.f))
    return block


def sample_mosaic(level, near):
    """1 m の 4 枚から、段の頂点の点（画素の値）と箱の平均（有効な画素だけ）を返す"""
    import numpy
    origin = near["origin"]
    radius = level.spacing // 2
    margin = radius + 1
    # 頂点 (x, z) は画素の中心 E = x + origin.east、N = origin.north - z
    east_min = int(round(level.x0 + origin["east"] - 0.5)) - margin
    north_max = int(round(origin["north"] - level.z0 + 0.5)) + margin
    east_max = int(round(level.x1 + origin["east"] + 0.5)) + margin
    north_min = int(round(origin["north"] - level.z1 - 0.5)) - margin
    block = read_mosaic(east_min, east_max, north_min, north_max)
    cols = numpy.arange(level.columns) * level.spacing + margin
    rows = numpy.arange(level.rows) * level.spacing + margin
    point = block[numpy.ix_(rows, cols)].astype(numpy.float64)

    # 箱の平均（有効な画素だけ）。横の和を頂点の列だけ求め、縦に足す
    valid = numpy.isfinite(block)
    sums = numpy.zeros((block.shape[0], level.columns))
    counts = numpy.zeros((block.shape[0], level.columns))
    chunk = 2048
    for top in range(0, block.shape[0], chunk):
        part = block[top:top + chunk]
        part_valid = valid[top:top + chunk]
        cumulative = numpy.zeros((part.shape[0], part.shape[1] + 1))
        numpy.cumsum(numpy.where(part_valid, part, 0.0), axis=1, out=cumulative[:, 1:])
        sums[top:top + chunk] = cumulative[:, cols + radius + 1] - cumulative[:, cols - radius]
        numpy.cumsum(part_valid, axis=1, out=cumulative[:, 1:])
        counts[top:top + chunk] = cumulative[:, cols + radius + 1] - cumulative[:, cols - radius]
    del block, valid
    vertical = numpy.zeros((sums.shape[0] + 1, level.columns))
    numpy.cumsum(sums, axis=0, out=vertical[1:])
    box_sum = vertical[rows + radius + 1] - vertical[rows - radius]
    numpy.cumsum(counts, axis=0, out=vertical[1:])
    box_count = vertical[rows + radius + 1] - vertical[rows - radius]
    box = numpy.where(box_count > 0, box_sum / numpy.maximum(box_count, 1), numpy.nan)
    return point, box


def read_regional(lon_min, lon_max, lat_min, lat_max):
    """1/3 秒角の 2 枚を並べ、経緯度の範囲を元の画素の格子に合わせて外へ広げて読む。(NaN 入りの高さ, transform)"""
    import numpy
    import rasterio
    from rasterio.merge import merge
    datasets = [rasterio.open(os.path.join(SOURCE_DIR, tile["file"])) for tile in REGIONAL_TILES]
    try:
        grid = datasets[0].transform
        # 画素の格子に合わせる（ずらして読むと最近傍で半画素ずれる）
        left = grid.c + math.floor((lon_min - grid.c) / grid.a) * grid.a
        right = grid.c + math.ceil((lon_max - grid.c) / grid.a) * grid.a
        top = grid.f + math.floor((lat_max - grid.f) / grid.e) * grid.e
        bottom = grid.f + math.ceil((lat_min - grid.f) / grid.e) * grid.e
        array, transform = merge(datasets, bounds=(left, bottom, right, top), res=(grid.a, -grid.e), nodata=-999999.0)
    finally:
        for dataset in datasets:
            dataset.close()
    block = array[0].astype(numpy.float32)
    block[~(block > -1000.0)] = numpy.nan
    return block, transform


def sample_regional(level, near):
    """1/3 秒角の 2 枚から、段の頂点の点（双線形）と箱の平均（間隔の四方の面積の平均）を返す。2 枚の外は NaN"""
    import numpy
    from rasterio.crs import CRS
    from rasterio.enums import Resampling
    from rasterio.transform import Affine
    from rasterio.warp import reproject, transform as warp_transform
    origin = near["origin"]
    east = level.x0 + origin["east"] + numpy.arange(level.columns) * level.spacing
    north = origin["north"] - (level.z0 + numpy.arange(level.rows) * level.spacing)
    ee, nn = numpy.meshgrid(east, north)
    lon, lat = warp_transform(CRS.from_epsg(26912), CRS.from_epsg(4269), ee.ravel(), nn.ravel())
    lon = numpy.asarray(lon).reshape(ee.shape)
    lat = numpy.asarray(lat).reshape(ee.shape)
    del ee, nn
    pad = 0.002
    block, grid = read_regional(float(lon.min()) - pad, float(lon.max()) + pad, float(lat.min()) - pad, float(lat.max()) + pad)

    # 点: 画素の中心の格子で双線形。4 隣のどれかが無効なら NaN
    u = (lon - grid.c) / grid.a - 0.5
    v = (lat - grid.f) / grid.e - 0.5
    c0 = numpy.floor(u).astype(numpy.int64)
    r0 = numpy.floor(v).astype(numpy.int64)
    fu = u - c0
    fv = v - r0
    inside = (c0 >= 0) & (r0 >= 0) & (c0 + 1 < block.shape[1]) & (r0 + 1 < block.shape[0])
    c0 = numpy.clip(c0, 0, block.shape[1] - 2)
    r0 = numpy.clip(r0, 0, block.shape[0] - 2)
    b = block
    point = (b[r0, c0].astype(numpy.float64) * (1 - fu) * (1 - fv) + b[r0, c0 + 1].astype(numpy.float64) * fu * (1 - fv)
             + b[r0 + 1, c0].astype(numpy.float64) * (1 - fu) * fv + b[r0 + 1, c0 + 1].astype(numpy.float64) * fu * fv)
    point = numpy.where(inside, point, numpy.nan)
    del b

    # 箱の平均: 頂点を中心にした間隔の四方の UTM の画素へ面積の平均で写す
    box = numpy.full((level.rows, level.columns), numpy.nan, dtype=numpy.float32)
    half = level.spacing / 2.0
    destination = Affine(level.spacing, 0.0, east[0] - half, 0.0, -level.spacing, north[0] + half)
    reproject(block, box, src_transform=grid, src_crs=CRS.from_epsg(4269), src_nodata=numpy.nan,
              dst_transform=destination, dst_crs=CRS.from_epsg(26912), dst_nodata=numpy.nan,
              resampling=Resampling.average)
    return point, box.astype(numpy.float64)


def blend_level(level, point, box):
    """点と箱の平均を、縫い目からの距離で混ぜる（縫い目では点、間隔の BAND_SPACINGS 倍より外は箱）"""
    import numpy
    # 縫い目からの距離（自分の外周と、穴の縁）
    xs = level.x0 + numpy.arange(level.columns) * level.spacing
    zs = level.z0 + numpy.arange(level.rows) * level.spacing
    distance = numpy.full((level.rows, level.columns), numpy.inf)
    if level.parent is not None:
        inner = numpy.minimum.outer(numpy.minimum(zs - level.z0, level.z1 - zs), numpy.minimum(xs - level.x0, level.x1 - xs))
        distance = numpy.minimum(distance, inner)
    for child in level.children:
        c0, c1, r0, r1 = ring_of(level, child)
        rx0, rx1 = level.x_of(c0), level.x_of(c1)
        rz0, rz1 = level.z_of(r0), level.z_of(r1)
        dx = numpy.maximum(numpy.maximum(rx0 - xs, xs - rx1), 0.0)
        dz = numpy.maximum(numpy.maximum(rz0 - zs, zs - rz1), 0.0)
        distance = numpy.minimum(distance, numpy.maximum.outer(dz, dx))
    weight = numpy.clip(distance / (BAND_SPACINGS * level.spacing), 0.0, 1.0)
    mixed = numpy.where(numpy.isfinite(point), point + (numpy.nan_to_num(box) - point) * weight, numpy.nan)
    return numpy.where(numpy.isfinite(box) | (weight == 0.0), mixed, numpy.nan)


def curvature_drop(level, near):
    """地球の丸みで下がる高さ。近景の外周からの水平の距離 d で d^2 / 2R × (1 - 屈折係数)"""
    import numpy
    xs = level.x0 + numpy.arange(level.columns) * level.spacing
    zs = level.z0 + numpy.arange(level.rows) * level.spacing
    dx = numpy.maximum(numpy.maximum(near["x0"] - xs, xs - near["x1"]), 0.0)
    dz = numpy.maximum(numpy.maximum(near["z0"] - zs, zs - near["z1"]), 0.0)
    return numpy.add.outer(dz * dz, dx * dx) / (2.0 * EARTH_RADIUS_M) * (1.0 - REFRACTION_COEFFICIENT)


def sample_level(level, near):
    """段の頂点の高さ: 1 m の 4 枚の点と箱の平均を縫い目からの距離で混ぜ、4 枚の外と NoData は 1/3 秒角で同じように混ぜて
    埋める。地球の丸みの下がりと、近景の高さのオフセットを引く"""
    import numpy
    if level.name in ("outer", "rim"):
        mixed = blend_level(level, *sample_regional(level, near))
        level.filled_from_regional = 0
    else:
        mixed = blend_level(level, *sample_mosaic(level, near))
        missing = ~numpy.isfinite(mixed)
        level.filled_from_regional = int(missing.sum())
        if level.filled_from_regional:
            mixed = numpy.where(missing, blend_level(level, *sample_regional(level, near)), mixed)
    level.heights = (mixed - curvature_drop(level, near) - near["offset"]).astype(numpy.float32)
    level.normals = grid_normals(level.heights, float(level.spacing))


def grid_normals(heights, spacing):
    """格子の法線。中心差分で、片側が無効（NaN・範囲の外）ならその側は自分の高さを使う片側の差分にする"""
    import numpy
    h = heights.astype(numpy.float64)
    pad = numpy.pad(h, 1, constant_values=numpy.nan)

    def slope(minus, plus):
        span = numpy.full(h.shape, 2.0 * spacing)
        minus_missing = numpy.isnan(minus)
        plus_missing = numpy.isnan(plus)
        span -= minus_missing * spacing + plus_missing * spacing
        minus = numpy.where(minus_missing, h, minus)
        plus = numpy.where(plus_missing, h, plus)
        return numpy.where(span > 0, (plus - minus) / numpy.maximum(span, 1e-9), 0.0)

    sx = slope(pad[1:-1, :-2], pad[1:-1, 2:])
    sz = slope(pad[:-2, 1:-1], pad[2:, 1:-1])
    length = numpy.sqrt(sx * sx + 1.0 + sz * sz)
    normals = numpy.stack([-sx / length, 1.0 / length, -sz / length], axis=-1).astype(numpy.float32)
    normals[numpy.isnan(h)] = 0.0
    return normals


def removed_cells(level):
    """穴の縁の内側の升目（作らない）の印"""
    import numpy
    removed = numpy.zeros((level.rows - 1, level.columns - 1), dtype=bool)
    for child in level.children:
        c0, c1, r0, r1 = ring_of(level, child)
        removed[max(r0, 0):max(min(r1, level.rows - 1), 0), max(c0, 0):max(min(c1, level.columns - 1), 0)] = True
    return removed


def vertex_records(xs, ys, zs, normals):
    """頂点の float32 × 8 の並び（UV はワールドの XZ ÷ 4 m。近景の区画と同じ）"""
    import numpy
    records = numpy.empty((len(xs), 8), dtype="<f4")
    records[:, 0] = xs
    records[:, 1] = ys
    records[:, 2] = zs
    records[:, 3:6] = normals
    records[:, 6] = records[:, 0] / numpy.float32(4.0)
    records[:, 7] = records[:, 2] / numpy.float32(4.0)
    return records


def level_tile_mesh(level, removed, tile_x, tile_z):
    """段の区画 (tile_x, tile_z) のメッシュ。升目の対角線は近景と同じ規則（全体の番号の和が偶数なら (0,0)〜(1,1)）"""
    import numpy
    c0, r0 = tile_x * TILE_CELLS, tile_z * TILE_CELLS
    c1, r1 = min(c0 + TILE_CELLS, level.columns - 1), min(r0 + TILE_CELLS, level.rows - 1)
    h = level.heights[r0:r1 + 1, c0:c1 + 1]
    valid = numpy.isfinite(h)
    cells = ~removed[r0:r1, c0:c1] & valid[:-1, :-1] & valid[1:, :-1] & valid[:-1, 1:] & valid[1:, 1:]
    if not cells.any():
        return None
    used = numpy.zeros(h.shape, dtype=bool)
    used[:-1, :-1] |= cells
    used[1:, :-1] |= cells
    used[:-1, 1:] |= cells
    used[1:, 1:] |= cells
    index = numpy.full(h.shape, -1, dtype=numpy.int64)
    index[used] = numpy.arange(int(used.sum()))
    rr, cc = numpy.nonzero(used)
    records = vertex_records((level.x0 + (cc + c0) * level.spacing).astype(numpy.float32),
                             h[rr, cc], (level.z0 + (rr + r0) * level.spacing).astype(numpy.float32),
                             level.normals[r0:r1 + 1, c0:c1 + 1][rr, cc])
    cr, ccol = numpy.nonzero(cells)
    i00 = index[cr, ccol]
    i10 = index[cr, ccol + 1]
    i01 = index[cr + 1, ccol]
    i11 = index[cr + 1, ccol + 1]
    main = ((cr + r0 + ccol + c0) & 1) == 0
    triangles = numpy.empty((len(cr) * 2, 3), dtype="<u4")
    triangles[0::2] = numpy.where(main[:, None], numpy.stack([i00, i10, i11], 1), numpy.stack([i00, i10, i01], 1))
    triangles[1::2] = numpy.where(main[:, None], numpy.stack([i00, i11, i01], 1), numpy.stack([i10, i11, i01], 1))
    return records, triangles


def perimeter_sides(x0, x1, z0, z1, spacing):
    """長方形の外周の 4 辺を、北（z0）・東（x1）・南（z1）・西（x0）の順に、角から角まで時計回りに (x, z) の並びで返す"""
    n_x = int(round((x1 - x0) / spacing))
    n_z = int(round((z1 - z0) / spacing))
    north = [(x0 + i * spacing, z0) for i in range(n_x + 1)]
    east = [(x1, z0 + j * spacing) for j in range(n_z + 1)]
    south = [(x1 - i * spacing, z1) for i in range(n_x + 1)]
    west = [(x0, z1 - j * spacing) for j in range(n_z + 1)]
    return [north, east, south, west]


def seam_mesh(parent, child, near):
    """child の外周と parent の穴の縁を縫う帯。parent の外にある縁の頂点・無効な頂点に触れる三角形は作らない"""
    import numpy
    c0, c1, r0, r1 = ring_of(parent, child)
    inner_sides = perimeter_sides(child.x0, child.x1, child.z0, child.z1, child.spacing)
    outer_sides = perimeter_sides(parent.x_of(c0), parent.x_of(c1), parent.z_of(r0), parent.z_of(r1), parent.spacing)
    keys = {}
    positions = []
    normals = []

    def vertex(level, x, z):
        key = (level.name, x, z)
        if key in keys:
            return keys[key]
        column = int(round((x - level.x0) / level.spacing))
        row = int(round((z - level.z0) / level.spacing))
        if not (0 <= column < level.columns and 0 <= row < level.rows):
            keys[key] = -1
            return -1
        if level.name == "near":
            y = near["heights"][row, column]
            normal = (NEAR_NORMAL_FROM_GRID,) * 3
        else:
            y = level.heights[row, column]
            normal = tuple(level.normals[row, column])
        if not numpy.isfinite(y):
            keys[key] = -1
            return -1
        keys[key] = len(positions)
        positions.append((x, float(y), z))
        normals.append(normal)
        return keys[key]

    triangles = []
    for inner, outer in zip(inner_sides, outer_sides):
        def param(points):
            (ax, az), (bx, bz) = points[0], points[-1]
            length = math.hypot(bx - ax, bz - az)
            return [math.hypot(px - ax, pz - az) / length for px, pz in points]
        ti, to = param(inner), param(outer)
        i = j = 0
        while i < len(inner) - 1 or j < len(outer) - 1:
            advance_inner = j == len(outer) - 1 or (i < len(inner) - 1 and ti[i + 1] <= to[j + 1])
            if advance_inner:
                corners = [(child, inner[i]), (child, inner[i + 1]), (parent, outer[j])]
                i += 1
            else:
                corners = [(child, inner[i]), (parent, outer[j + 1]), (parent, outer[j])]
                j += 1
            ids = [vertex(level, x, z) for level, (x, z) in corners]
            if min(ids) >= 0:
                triangles.append(ids)
    if not triangles:
        return None
    pos = numpy.array(positions, dtype=numpy.float64)
    records = vertex_records(pos[:, 0].astype(numpy.float32), pos[:, 1].astype(numpy.float32),
                             pos[:, 2].astype(numpy.float32), numpy.array(normals, dtype=numpy.float32))
    tri = numpy.array(triangles, dtype=numpy.int64)
    # 巻き方を段の升目と同じ向き（XZ の外積の Y が負）にそろえる
    a, b, c = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
    cross_y = (b[:, 2] - a[:, 2]) * (c[:, 0] - a[:, 0]) - (b[:, 0] - a[:, 0]) * (c[:, 2] - a[:, 2])
    if (cross_y == 0).any():
        raise RuntimeError("縫い目 %s に面積 0 の三角形がある" % child.name)
    flip = cross_y > 0
    tri[flip] = tri[flip][:, [0, 2, 1]]
    return records, tri.astype("<u4")


def build_meshes():
    """段の格子を作り、メッシュの名前と (頂点, 三角形) の並びを返す"""
    near = load_near()
    near_level, castle, mid, far, outer, rim = make_levels(near)
    for level in (castle, mid, far, outer, rim):
        sample_level(level, near)
        print("段 %s: 間隔 %d m、%d x %d 頂点、X %.0f〜%.0f・Z %.0f〜%.0f、有効 %.1f %%、1/3 秒角で埋めた頂点 %d" % (
            level.name, level.spacing, level.columns, level.rows, level.x0, level.x1, level.z0, level.z1,
            100.0 * float((~numpy_isnan(level.heights)).mean()), level.filled_from_regional), flush=True)
    meshes = []
    for level in (castle, mid, far, outer, rim):
        removed = removed_cells(level)
        tiles_x = (level.columns - 2) // TILE_CELLS + 1
        tiles_z = (level.rows - 2) // TILE_CELLS + 1
        for tile_z in range(tiles_z):
            for tile_x in range(tiles_x):
                mesh = level_tile_mesh(level, removed, tile_x, tile_z)
                if mesh is not None:
                    meshes.append(("%s_%d_%d" % (level.name, tile_x, tile_z), level.name, mesh))
    for parent, child in ((mid, near_level), (mid, castle), (far, mid), (outer, far), (rim, outer)):
        mesh = seam_mesh(parent, child, near)
        if mesh is not None:
            meshes.append(("seam_%s" % child.name, "seam", mesh))
    return near, (near_level, castle, mid, far, outer, rim), meshes


def numpy_isnan(values):
    import numpy
    return numpy.isnan(values)


def mesh_bytes(mesh):
    records, triangles = mesh
    return records.tobytes() + triangles.tobytes()


def make_descriptor(near, levels, meshes, source_manifest):
    entries = []
    for name, level_name, mesh in meshes:
        records, triangles = mesh
        data = mesh_bytes(mesh)
        entries.append({
            "name": name,
            "level": level_name,
            "file": "%s.bin" % name,
            "sha256": hashlib.sha256(data).hexdigest(),
            "vertices": int(len(records)),
            "triangles": int(len(triangles)),
            "normals_from_grid": int(numpy_isnan(records[:, 3]).sum()),
            "bounds": {"x_min": float(records[:, 0].min()), "x_max": float(records[:, 0].max()),
                       "y_min": float(records[:, 1].min()), "y_max": float(records[:, 1].max()),
                       "z_min": float(records[:, 2].min()), "z_max": float(records[:, 2].max())},
        })

    def level_record(level):
        record = {"name": level.name, "spacing_m": level.spacing, "columns": level.columns, "rows": level.rows,
                  "x_min": level.x0, "x_max": level.x1, "z_min": level.z0, "z_max": level.z1}
        if level.heights is not None:
            # 段の頂点の高さ（float32、行優先、行 0 が z_min、無効は NaN）。照合（FitReferenceView.py）が引く
            record["file"] = "%s.r32" % level.name
            record["sha256"] = hashlib.sha256(level.heights.tobytes()).hexdigest()
        if level.parent is not None:
            c0, c1, r0, r1 = ring_of(level.parent, level)
            record["ring_in_parent"] = {"parent": level.parent.name, "x_min": level.parent.x_of(c0),
                                        "x_max": level.parent.x_of(c1), "z_min": level.parent.z_of(r0),
                                        "z_max": level.parent.z_of(r1)}
        return record

    sources = [{"file": source_manifest["file"], "sha256": source_manifest["sha256"]}]
    sources += [{"file": entry["file"], "sha256": entry["sha256"]} for entry in source_manifest.get("neighbors", [])]
    sources += [{"file": entry["file"], "sha256": entry["sha256"]} for entry in source_manifest.get("regional", [])]
    return {
        "format": FORMAT,
        "mesh_format": MESH_FORMAT,
        "mesh_folder": MESH_FOLDER,
        "normals_note": "法線が NaN の頂点は近景の外周の頂点。クッカーが近景の格子（grid_descriptor）の SampleNormal で埋める",
        "grid_descriptor": ASSET_PREFIX + "height.json",
        "grid_sha256": near["document"]["sha256"],
        "height_offset_m": near["offset"],
        "axes": near["document"]["axes"],
        "mosaic_utm_edges": dict(MOSAIC),
        "outer_expand_m": dict(OUTER_EXPAND_M),
        "curvature_note": "近景より外の段の高さから、近景の外周からの水平の距離 d で d^2 / 2R × (1 - k) を引いた"
                          "（R = %.0f m、屈折係数 k = %.2f）" % (EARTH_RADIUS_M, REFRACTION_COEFFICIENT),
        "levels": [level_record(level) for level in levels],
        "band_note": "各段の高さは、縫い目から間隔の %d 倍の距離までは DEM の画素の値、それより外は間隔ほどの箱の平均。"
                     "縫い目の帯の両端の頂点は DEM の画素の値（近景の側は近景の格子の値）" % BAND_SPACINGS,
        "tile_cells": TILE_CELLS,
        "meshes": entries,
        "sources": sources,
        "generated": datetime.date.today().isoformat(),
        "generator": "Scripts/Terrain/HorizonDem.py",
    }


def hillshade(heights, spacing):
    import numpy
    h = numpy.nan_to_num(heights.astype(numpy.float32), nan=float(numpy.nanmin(heights)))
    gz, gx = numpy.gradient(h, spacing)
    az, alt = math.radians(315.0), math.radians(45.0)
    light = (math.sin(az) * math.cos(alt), math.cos(az) * math.cos(alt), math.sin(alt))
    # 東 = +X、北 = -Z
    dzde, dzdn = gx, -gz
    shade = (-dzde * light[0] - dzdn * light[1] + light[2]) / numpy.sqrt(dzde * dzde + dzdn * dzdn + 1.0)
    shade = numpy.clip(shade, 0.0, 1.0) * 255.0
    shade[numpy.isnan(heights)] = 0.0
    return shade.astype(numpy.uint8)


def draw_preview(near, levels):
    """rim の段の陰影図に、各段の範囲と縫い目の縁、名前の付いた地形を重ねる"""
    from PIL import Image, ImageDraw
    from ReferencePhotos import label_font
    near_level, castle, mid, far, outer, rim = levels
    image = Image.fromarray(hillshade(rim.heights, rim.spacing), mode="L").convert("RGB")
    draw = ImageDraw.Draw(image)
    font = label_font()

    def px(x, z):
        return (x - rim.x0) / rim.spacing, (z - rim.z0) / rim.spacing

    colors = {"near": (255, 220, 60), "castle": (255, 80, 80), "mid": (80, 200, 255), "far": (120, 255, 120),
              "outer": (255, 160, 60)}
    for level in (near_level, castle, mid, far, outer):
        draw.rectangle([px(level.x0, level.z0), px(level.x1, level.z1)], outline=colors[level.name], width=2)
        draw.text((px(level.x0, level.z0)[0] + 4, px(level.x0, level.z0)[1] + 4),
                  "%s %d m" % (level.name, level.spacing), fill=colors[level.name], font=font, stroke_width=2,
                  stroke_fill=(0, 0, 0))
    origin = near["origin"]
    for name in ("Castle Butte", "Lady in the Bathtub", "Battleship Rock", "Bears Ears"):
        if name in FEATURES:
            east, north = utm_from_latlon(FEATURES[name]["lat"], FEATURES[name]["lon"])
        elif name == "Castle Butte":
            east, north = 601557.0, 4130371.0
        elif name == "Bears Ears":
            east, north = 600157.0, 4164815.0
        else:
            east, north = 599397.0, 4126740.0
        x, y = px(east - origin["east"], origin["north"] - north)
        draw.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(240, 60, 240))
        draw.text((x + 5, y - 6), name, fill=(250, 140, 250), font=font, stroke_width=2, stroke_fill=(0, 0, 0))
    draw.rectangle([0, 0, image.width, 22], fill=(0, 0, 0))
    draw.text((6, 3), "遠景の段（上が北・%d m/画素）  黄: 近景 1 m  赤: castle 2 m  水色: mid 8 m  緑: far 16 m  橙: outer 32 m  外: rim 128 m  黒: DEM の外" %
              rim.spacing, fill=(255, 255, 255), font=font)
    os.makedirs(os.path.dirname(PREVIEW_PATH), exist_ok=True)
    image.save(PREVIEW_PATH)


def build():
    with open(SOURCE_MANIFEST_PATH, encoding="utf-8") as stream:
        source_manifest = json.load(stream)
    if len(source_manifest.get("neighbors", [])) != len(NEIGHBOR_TILES):
        raise RuntimeError("manifest に隣のタイルが無い（DemInput.py --fetch-neighbors で落とす）")
    if len(source_manifest.get("regional", [])) != len(REGIONAL_TILES):
        raise RuntimeError("manifest に 1/3 秒角のタイルが無い（DemInput.py --fetch-regional で落とす）")
    near, levels, meshes = build_meshes()
    os.makedirs(MESH_DIR, exist_ok=True)
    for stale in os.listdir(MESH_DIR):
        if stale.endswith(".bin") and stale[:-4] not in {name for name, _, _ in meshes}:
            os.remove(os.path.join(MESH_DIR, stale))
    for name, _, mesh in meshes:
        with open(os.path.join(MESH_DIR, name + ".bin"), "wb") as stream:
            stream.write(mesh_bytes(mesh))
    for level in levels[1:]:
        with open(os.path.join(MESH_DIR, level.name + ".r32"), "wb") as stream:
            stream.write(level.heights.astype("<f4").tobytes())
    document = make_descriptor(near, levels, meshes, source_manifest)
    with open(DESCRIPTOR_PATH, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(document, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    total_vertices = sum(entry["vertices"] for entry in document["meshes"])
    total_triangles = sum(entry["triangles"] for entry in document["meshes"])
    for entry in document["meshes"]:
        print("  %-14s 頂点 %8d・三角形 %8d" % (entry["name"], entry["vertices"], entry["triangles"]))
    print("メッシュ %d 個、頂点 %d、三角形 %d" % (len(document["meshes"]), total_vertices, total_triangles))
    draw_preview(near, levels)
    print("記述: %s、図: %s" % (DESCRIPTOR_PATH, PREVIEW_PATH))
    return 0


def watertight_report(near, meshes, check):
    """近景の区画の外周の辺と遠景の全メッシュを合わせ、辺を共有する三角形の数を数える。開いた辺（1 つだけ）が
    DEM の外・NoData の縁にしか無いこと、3 つ以上が共有する辺が無いこと、同じ位置の頂点の高さが一致することを確かめる"""
    import numpy
    key_scale = 1 << 20

    def keys_of(xs, zs):
        return (numpy.round(xs).astype(numpy.int64) + KEY_OFFSET) * key_scale + (numpy.round(zs).astype(numpy.int64) + KEY_OFFSET)

    all_keys = []
    all_heights = []
    edges = []
    for name, _, (records, triangles) in meshes:
        k = keys_of(records[:, 0], records[:, 2])
        all_keys.append(k)
        all_heights.append(records[:, 1])
        tk = k[triangles.astype(numpy.int64)]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            pair = numpy.stack([tk[:, a], tk[:, b]], 1)
            edges.append(numpy.sort(pair, axis=1))
    # 近景の区画の外周の辺（区画は外周の頂点を動かさず、外周は 1 m ごとの辺）
    h = near["heights"]
    xs = near["x0"] + numpy.arange(h.shape[1], dtype=numpy.float64)
    zs = near["z0"] + numpy.arange(h.shape[0], dtype=numpy.float64)
    ring = [(xs, numpy.full_like(xs, zs[0]), h[0]), (xs, numpy.full_like(xs, zs[-1]), h[-1]),
            (numpy.full_like(zs, xs[0]), zs, h[:, 0]), (numpy.full_like(zs, xs[-1]), zs, h[:, -1])]
    near_edges = 0
    for rx, rz, rh in ring:
        k = keys_of(rx, rz)
        all_keys.append(k)
        all_heights.append(rh.astype(numpy.float32))
        edges.append(numpy.sort(numpy.stack([k[:-1], k[1:]], 1), axis=1))
        near_edges += len(k) - 1
    edges = numpy.concatenate(edges)
    # 位置の鍵を通し番号に置き換えて、辺を 1 つの整数にする
    vertex_keys, inverse = numpy.unique(edges.ravel(), return_inverse=True)
    ids = inverse.reshape(edges.shape).astype(numpy.int64)
    codes, counts = numpy.unique(ids[:, 0] * len(vertex_keys) + ids[:, 1], return_counts=True)
    open_codes = codes[counts == 1]
    open_edges = numpy.stack([vertex_keys[open_codes // len(vertex_keys)], vertex_keys[open_codes % len(vertex_keys)]], 1)
    unique_edges = codes
    shared3 = int((counts > 2).sum())
    # 同じ位置の頂点の高さ
    keys = numpy.concatenate(all_keys)
    heights = numpy.concatenate(all_heights)
    order = numpy.argsort(keys, kind="stable")
    keys, heights = keys[order], heights[order]
    same = keys[1:] == keys[:-1]
    height_mismatch = int((same & (heights[1:].view(numpy.uint32) != heights[:-1].view(numpy.uint32))).sum())
    print("辺: %d（近景の外周 %d を含む）、開いた辺 %d、3 つ以上が共有する辺 %d、同じ位置で高さが違う頂点の組 %d" % (
        len(unique_edges), near_edges, len(open_edges), shared3, height_mismatch))
    check(shared3 == 0, "3 つ以上の三角形が共有する辺が無い")
    check(height_mismatch == 0, "同じ位置の頂点の高さがビット単位で一致（縫い目・区画の境で段差が無い）")
    return open_edges, key_scale


def near_invalid_at(level, xs, zs):
    """位置 (xs, zs) の近く（段の格子の 2 升目以内）に、段の無効な頂点があるか。段の外は False"""
    import numpy
    invalid = numpy.isnan(level.heights)
    pad = numpy.pad(invalid, 2, constant_values=False)
    spread = numpy.zeros_like(invalid)
    for dr in range(-2, 3):
        for dc in range(-2, 3):
            spread |= pad[2 + dr:2 + dr + invalid.shape[0], 2 + dc:2 + dc + invalid.shape[1]]
    column = numpy.round((xs - level.x0) / level.spacing).astype(int)
    row = numpy.round((zs - level.z0) / level.spacing).astype(int)
    inside = (column >= 0) & (column < level.columns) & (row >= 0) & (row < level.rows)
    result = numpy.zeros(xs.shape, dtype=bool)
    result[inside] = spread[row[inside], column[inside]]
    return result


def verify():
    import numpy
    failures = []

    def check(condition, message):
        print(("OK   " if condition else "FAIL ") + message)
        if not condition:
            failures.append(message)

    for path in [SOURCE_MANIFEST_PATH, NEAR_JSON_PATH, DESCRIPTOR_PATH, PREVIEW_PATH]:
        check(os.path.isfile(path), "ある: %s" % os.path.relpath(path, REPO_ROOT))
    if failures:
        print("result=fail (%d)" % len(failures))
        return 1
    with open(SOURCE_MANIFEST_PATH, encoding="utf-8") as stream:
        source_manifest = json.load(stream)
    with open(DESCRIPTOR_PATH, encoding="utf-8") as stream:
        document = json.load(stream)

    # 元のタイル
    neighbors = {entry["file"]: entry for entry in source_manifest.get("neighbors", [])}
    for tile in NEIGHBOR_TILES:
        entry = neighbors.get(tile["file"])
        path = os.path.join(SOURCE_DIR, tile["file"])
        ok = entry is not None and os.path.isfile(path) and os.path.getsize(path) == tile["size"] == entry["size"]
        ok = ok and sha256_of(path) == entry["sha256"] and bool(entry.get("url")) and bool(entry.get("retrieved"))
        check(ok, "隣のタイル %s がある（大きさ %d・SHA-256・URL・取得日が manifest と一致）" % (tile["file"], tile["size"]))
    regional = {entry["file"]: entry for entry in source_manifest.get("regional", [])}
    for tile in REGIONAL_TILES:
        entry = regional.get(tile["file"])
        path = os.path.join(SOURCE_DIR, tile["file"])
        ok = entry is not None and os.path.isfile(path) and os.path.getsize(path) == tile["size"] == entry["size"]
        ok = ok and sha256_of(path) == entry["sha256"] and entry.get("url") == tile["url"] and bool(entry.get("retrieved"))
        check(ok, "1/3 秒角のタイル %s がある（大きさ %d・SHA-256・URL・取得日が manifest と一致）" % (tile["file"], tile["size"]))
    ignored = subprocess.run(["git", "check-ignore", "-q", DESCRIPTOR_PATH], cwd=REPO_ROOT).returncode == 0
    check(ignored, "horizon.json と horizon/ が git の無視対象")

    near, levels, meshes = build_meshes()
    rebuilt = make_descriptor(near, levels, meshes, source_manifest)
    check([m["sha256"] for m in rebuilt["meshes"]] == [m["sha256"] for m in document["meshes"]],
          "DEM から作り直したメッシュ %d 個が記述の SHA-256 と一致" % len(meshes))
    files_ok = all(os.path.isfile(os.path.join(MESH_DIR, m["file"])) and
                   sha256_of(os.path.join(MESH_DIR, m["file"])) == m["sha256"] for m in document["meshes"])
    check(files_ok, "horizon/*.bin が記述の SHA-256 と一致")
    check(document["grid_sha256"] == near["document"]["sha256"], "記述の近景の格子の SHA-256 が height.json と一致")

    near_level, castle, mid, far, outer, rim = levels
    # 段の入れ子と縫い目の帯の幅（outer の西の辺は 1/3 秒角の外なので縫い目を作らない）
    for level in (castle, mid, far, outer):
        c0, c1, r0, r1 = ring_of(level.parent, level)
        widths = (level.x0 - level.parent.x_of(c0), level.parent.x_of(c1) - level.x1,
                  level.z0 - level.parent.z_of(r0), level.parent.z_of(r1) - level.z1)
        # 穴の縁が外側の段の外にはみ出す辺（outer の西。1/3 秒角の外）は縫い目を作らないので見ない
        inside = (c0 >= 0, c1 <= level.parent.columns - 1, r0 >= 0, r1 <= level.parent.rows - 1)
        print("縫い目 %s→%s: 帯の幅 西 %.0f・東 %.0f・北 %.0f・南 %.0f m（外側の段の内にある辺 %d）" % (
            level.name, level.parent.name, *widths, sum(inside)))
        check(all(level.parent.spacing <= w < 2 * level.parent.spacing for w, ok in zip(widths, inside) if ok),
              "縫い目 %s の帯の幅が外側の間隔の 1〜2 倍" % level.name)
    c0, c1, r0, r1 = ring_of(mid, near_level)
    widths = (near_level.x0 - mid.x_of(c0), mid.x_of(c1) - near_level.x1, near_level.z0 - mid.z_of(r0), mid.z_of(r1) - near_level.z1)
    print("縫い目 near→mid: 帯の幅 西 %.0f・東 %.0f・北 %.0f・南 %.0f m" % widths)
    check(all(8 <= w < 16 for w in widths), "縫い目 near の帯の幅が 8〜16 m")
    _, _, cr0, cr1 = ring_of(mid, castle)
    check(mid.z_of(cr1) <= mid.z_of(r0), "castle の穴と近景の穴が重ならない")

    # 近景の外周の頂点の高さ（縫い目の内側の端）が近景の格子の値そのもの
    seam = next(mesh for name, _, mesh in meshes if name == "seam_near")
    records = seam[0]
    from_grid = numpy.isnan(records[:, 3])
    columns = numpy.round(records[from_grid, 0] - near["x0"]).astype(int)
    rows = numpy.round(records[from_grid, 2] - near["z0"]).astype(int)
    exact = bool((records[from_grid, 1] == near["heights"][rows, columns]).all())
    perimeter = 2 * (near["heights"].shape[0] + near["heights"].shape[1]) - 4
    check(exact and int(from_grid.sum()) == perimeter, "近景の縫い目の内側の頂点 %d（外周の全頂点 %d）が近景の格子の値そのもの" % (
        int(from_grid.sum()), perimeter))

    # 4 枚の NoData は 1/3 秒角で埋まり、far・mid・castle に無効な頂点が残らない
    for level in (castle, mid, far):
        check(not numpy.isnan(level.heights).any(), "段 %s に無効な頂点が無い（1/3 秒角で埋めた頂点 %d）" % (
            level.name, level.filled_from_regional))

    # 開いた辺は rim の外周と、1/3 秒角の外（西経 110° より西）の縁だけ
    open_edges, key_scale = watertight_report(near, meshes, check)
    ends = open_edges.ravel()
    ex = ends // key_scale - KEY_OFFSET
    ez = ends % key_scale - KEY_OFFSET
    margin = rim.spacing / 2
    at_outer_edge = ((ex < rim.x0 + margin) | (ex > rim.x1 - margin) |
                     (ez < rim.z0 + margin) | (ez > rim.z1 - margin))
    # データの外の縁: 近くに無効な頂点がある（outer と rim の格子で見る）
    at_nodata = numpy.zeros(ex.shape, dtype=bool)
    for level in (outer, rim):
        at_nodata |= near_invalid_at(level, ex, ez)
    stray = int((~(at_outer_edge | at_nodata)).sum())
    print("開いた辺の端点 %d: rim の外周 %d・1/3 秒角の外の縁 %d・それ以外 %d" % (
        len(ends), int(at_outer_edge.sum()), int((at_nodata & ~at_outer_edge).sum()), stray))
    check(stray == 0, "開いた辺が rim の外周・DEM の外の縁にしか無い（近景・段・縫い目の間に隙間が無い）")

    # 主役の地形が細かい段に入っている
    origin = near["origin"]
    for name, east_, north_, level in (("Castle Butte", 601557.0, 4130371.0, castle),):
        x, z = east_ - origin["east"], origin["north"] - north_
        inside = level.x0 + 300 <= x <= level.x1 - 300 and level.z0 + 300 <= z <= level.z1 - 300
        check(inside, "%s（X %.0f・Z %.0f）が %s の段（%d m）の 300 m 内側" % (name, x, z, level.name, level.spacing))
    total = sum(m["vertices"] for m in document["meshes"])
    print("メッシュ %d 個、頂点 %d、三角形 %d" % (len(document["meshes"]), total, sum(m["triangles"] for m in document["meshes"])))
    print("result=%s" % ("pass" if not failures else "fail (%d)" % len(failures)))
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser(description="荒野の遠景の地形を 4 枚の 1 m の DEM から焼く")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--build", action="store_true", help="遠景のメッシュ・記述・図を書く")
    group.add_argument("--verify", action="store_true", help="作り直して一致と、隙間・段差の無さを確かめる")
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if args.build:
        return build()
    return verify()


if __name__ == "__main__":
    sys.exit(main())
