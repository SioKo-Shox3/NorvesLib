# 荒野の DEM の格子（CropDem.py が書く height.json と height.r32）から、地表の分類の重みの地図を焼く。
# 分類は露岩・崖錐・地表（細粒）・地表（礫）・砂（沢底と吹きだまり）の 5 つで、1 画素の RGBA8 に前の 4 つの重みを入れ、
# 砂は 255 から 4 つの和を引いた残りにする（5 つの合計が 255）。
#
# 使い方:
#   python Scripts/Terrain/TerrainMasks.py --build    地図・記述・色分けの図を Assets/Terrain/ValleyOfTheGods/ に書く
#   python Scripts/Terrain/TerrainMasks.py --verify   地図を作り直して書いたものと一致するか、崖に露岩・崖の下の裾に崖錐・
#                                                     平らな所に地表が出るか、崖錐が距離だけで決まっていないか、沢底が谷の底に
#                                                     沿って手本の沢の幅になるか、吹きだまりが風下の陰に寄るかを確かめる
# 出力は git 管理外（格子と同じ場所）。終了コード: すべて満たせば 0、どれかが欠ければ 1。
#
# 求め方（2 m の画素で計算する。1 m の格子をそのまま微分すると細かい凹凸を拾うので、2×2 の平均と数 m の幅でならす）:
#   - 傾斜: ならした高さ（2×2 の平均にガウス σ 3 m）の中心差分（4 m の幅）
#   - 相対の高さ: ならした高さと、周り（半径 10 m・50 m の正方形）の平均との差。正なら周りより高い（縁・尾根）、負なら低い（窪み）
#   - 起伏: 半径 12 m の正方形の中の最高と最低の差。沢の岸のような低い段（数 m）を露岩にしないために使う
#   - 露岩: 急な所（傾斜 38〜52°）で起伏の大きい所と、周りより高い縁の傾いた所
#   - 崖錐: 露岩の画素から崖の高さに比例した量の岩屑を落とし、下り斜面へ多方向に流す（急な下りほど多く、沢の切れ込みに集まる）。
#     落石の到達はエネルギー線（崖の上端から見下ろす角 32°）で切り、緩い所と周りより低い窪みほど多く溜まる（張り出した尾根では
#     溜まりにくい）。溜まった量を数 m ぼかして重みを決めるので、
#     崖より低い側・斜面の向き・沢の切れ込みに沿った扇状・舌状の塊になり、崖からの距離だけでは決まらない
#   - 地表: 残り。傾いた所・周りより高い所・崖錐の先の薄く散った所は礫、周りより低い平らな所は細粒
#   - 沢底: 流れの集まり（集水面積）を、地図の中は 2 m の画素、地図の外 5 km までは 10 m の升目で求める（どちらも窪地を
#     埋めて溢れ口へ流す Priority-Flood。描画用の高さは変えない）。外の升目から地図へ流れ込む面積は、流れ込む升目の中の
#     最も低い画素に足す。対数にした集水面積から沢底の幅を決め、流路の画素から幅の半分の内側（岸の上は除く）を砂にする
#   - 吹きだまり: 沢底の砂を風下へ運び（沢から遠い所にも薄く）、風上の高まりの陰に溜める。風向は手本の写真から推定し、
#     重みの上限は手本の足元の写真（砂は沢底と陰の薄い斑だけ）で抑える
# 地層の高さは地図に焼かない（シェーダーで共通の基準面からの高さとして計算する）。
import argparse
import hashlib
import json
import math
import os
import subprocess
import sys

import numpy

from ReferencePhotos import REPO_ROOT, label_font

GRID_DIR = os.path.join(REPO_ROOT, "Assets", "Terrain", "ValleyOfTheGods")
HEIGHT_JSON_PATH = os.path.join(GRID_DIR, "height.json")
MAP_PATH = os.path.join(GRID_DIR, "surface.png")
MAP_JSON_PATH = os.path.join(GRID_DIR, "surface.json")
PREVIEW_DIR = os.path.join(GRID_DIR, "preview")
PREVIEWS = ["surface_classes.png", "surface_battleship.png", "surface_franklin.png", "surface_wash.png"]

# 地図の画素の大きさ[m]（格子の 2×2 の頂点を 1 画素にまとめる）
TEXEL_M = 2.0
# 高さをならすガウスの σ[m]
SMOOTH_SIGMA_M = 3.0
# 相対の高さを見る半径[m]（小さい方は縁・段、大きい方は谷床と台地の区別）
TPI_SMALL_RADIUS_M = 10.0
TPI_MID_RADIUS_M = 50.0
# 起伏を見る半径[m]
RELIEF_RADIUS_M = 12.0

# 露岩: 傾斜の範囲[度]と起伏の範囲[m]（どちらも smoothstep の始まりと終わり）
ROCK_SLOPE_DEG = (38.0, 52.0)
ROCK_RELIEF_M = (4.0, 10.0)
# 周りより高い縁の露岩: 半径 10 m の相対の高さ[m]と傾斜[度]の範囲、重みの上限
RIM_TPI_M = (1.5, 4.0)
RIM_SLOPE_DEG = (18.0, 32.0)
RIM_WEIGHT = 0.8
# 崖錐の岩屑を出す露岩の重みの下限
SOURCE_MIN_ROCK = 0.5

# 崖錐: 落石の到達の角（崖の上端から見下ろす角、度）。Fahrböschung の 30〜35°の中ほど
REACH_ANGLE_DEG = 32.0
# 下り斜面への分け方の指数（大きいほど一番急な下りへ集まる）
FLOW_EXPONENT = 4.0
# 1 画素ごとに溜まる割合: 急な所（傾斜の上限より上）は DEPOSIT_STEEP、緩い所（下限より下）は DEPOSIT_GENTLE
DEPOSIT_SLOPE_DEG = (12.0, 30.0)
DEPOSIT_STEEP = 0.03
DEPOSIT_GENTLE = 0.3
# 周りより低い窪み（半径 10 m の相対の高さが負）ほど溜まりやすく、張り出した尾根では溜まりにくい: 割合に掛ける倍率の範囲と、
# 相対の高さ[m]の範囲（-HOLLOW_TPI_M[1] で最大、-HOLLOW_TPI_M[0] で最小）
HOLLOW_GAIN = (0.2, 1.8)
HOLLOW_TPI_M = (-1.0, 1.5)
# 溜まった量から重みへ: 1 - exp(-量 / TALUS_SCALE)
TALUS_SCALE = 0.6
# 崖錐の先の薄く散った所（礫を増やす）: 1 - exp(-量 / FRINGE_SCALE)
FRINGE_SCALE = 0.15
# 岩屑を流す面にさらに掛けるガウスの σ[m]（数 m の筋でなく、尾根の間の沢の切れ込みに沿わせる）と、溜まった量をぼかす σ[m]（筋を塊にする）
FLOW_SIGMA_M = 3.0
DEPOSIT_BLUR_M = 5.0
# 流れの反復の上限（2 m の画素で 1.2 km）
FLOW_MAX_ITERATIONS = 600

# 流れ（沢）: 地図の外へ広げる幅[m]と、広い範囲の升目の大きさ[m]（1 m の 4 枚を面積の平均で縮める）
WIDE_MARGIN_M = 5000.0
WIDE_TEXEL_M = 10.0
# 流す面: 2×2 の平均にさらに掛けるガウスの σ[m]（細かい凹凸で流れが切れないが、幅 5〜10 m の沢の底は消さない）
FLOW_SURFACE_SIGMA_M = 2.0
# 沢底（砂）: 集水面積[m²]がこの範囲で現れ始めて出そろう（小さな筋の雨裂は沢底にしない）
WASH_AREA_M2 = (5.0e4, 2.0e5)
# 沢底の幅[m] = 始まりの幅 + 集水面積が 10 倍になるごとの幅 × log10(面積 / WASH_AREA_M2[0])。上限あり。
# 手本の ground_02 の沢（集水面積 約 22 km²）は写真で幅 5〜10 m、1 m の DEM の流路の平らな底も 6〜8 m
WASH_WIDTH_M = (2.0, 2.3)
WASH_MAX_WIDTH_M = 12.0
# 沢底の縁をぼかす幅[m]（±）と、流路の中心より高い所を沢底にしない高さ[m]（岸を登らない）
WASH_EDGE_M = 1.0
WASH_BANK_M = 0.8
# 流路の傾斜[度]（この範囲で沢底が消える。崖錐や崖の雨裂は砂の流路にしない）
WASH_SLOPE_DEG = (6.0, 14.0)
# 砂の吹きだまり: 風の吹いてくる方位[度]。手本の far_02（西〜南西を向く）で、手前の高まりの東向きの面に滑らかな砂が溜まり、
# 西向きの面に礫が出る形から、西〜南西の風と読む（コロラド高原の春の卓越風の南西とも合う）
WIND_FROM_AZIMUTH_DEG = 235.0
# 風上を見る距離[m]と、風上が見下ろす角の正接の範囲（風下の陰ほど溜まる）
DRIFT_SHELTER_M = (4.0, 48.0)
DRIFT_SHELTER_TANGENT = (0.03, 0.12)
# 砂の出どころ: 沢底から風下へ運ぶ距離[m]と減り方の長さ[m]、沢から遠い細粒の地表にも薄く残る割合
DRIFT_REACH_M = 200.0
DRIFT_DECAY_M = 60.0
DRIFT_BASE_SUPPLY = 0.25
# 吹きだまりの重みの上限（手本の足元の写真では、谷床の大半は礫と土が出ていて、砂は沢底と陰の薄い斑だけ）と、
# 傾斜[度]の範囲（急な所には溜まらない）、ぼかす σ[m]
DRIFT_MAX = 0.6
DRIFT_SLOPE_DEG = (15.0, 25.0)
DRIFT_BLUR_M = 3.0

# 地表の礫の割合: 傾斜[度]・半径 50 m の相対の高さ[m]の範囲
GRAVEL_BASE = 0.25
GRAVEL_SLOPE_DEG = (3.0, 15.0)
GRAVEL_TPI_M = (0.5, 3.0)

# 色分けの図の色（露岩・崖錐・細粒・礫・砂）
CLASS_NAMES = ["露岩", "崖錐", "地表（細粒）", "地表（礫）", "砂（沢底と吹きだまり）"]
CLASS_COLORS = numpy.array([[205, 55, 45], [250, 175, 40], [150, 215, 240], [70, 165, 95], [150, 80, 230]],
                           dtype=numpy.float32)

# 確かめるときの基準
CHECK_STEEP_SLOPE_DEG = 55.0
CHECK_STEEP_RELIEF_M = 10.0
CHECK_STEEP_ROCK_SHARE = 0.90
CHECK_FLAT_SLOPE_DEG = 5.0
CHECK_FLAT_GROUND_SHARE = 0.98
CHECK_TALUS_SLOPE_DEG = (15.0, 38.0)
CHECK_NEAR_CLIFF_M = 150.0
CHECK_DISTANCE_ONLY_MAX = 0.6
CHECK_COVERAGE = {"露岩": (0.005, 0.15), "崖錐": (0.01, 0.20), "地表（細粒）": (0.10, 0.90), "地表（礫）": (0.10, 0.90),
                  "砂（沢底と吹きだまり）": (0.003, 0.10)}
# 沢: 手本の ground_02 の撮影位置（エンジンの X, Z。GPS E 603208・N 4128895）と、その周りで探す半径[m]、写真の沢の幅[m]
CHECK_WASH_PHOTO = (-1292.5, -1894.5)
CHECK_WASH_PHOTO_RADIUS_M = 30.0
CHECK_WASH_PHOTO_WIDTH_M = (5.0, 10.0)
# 沢底（重み 0.5 以上）のうち、周りより低い（半径 10 m の相対の高さが 0 以下）画素の割合の下限と、傾斜の中央値[度]の上限
CHECK_WASH_LOW_SHARE = 0.8
CHECK_WASH_SLOPE_DEG = 6.0
# 吹きだまり: 風下の陰（風上が見下ろす角の正接 0.08 以上）の平均の重みが、陰でない所（0 以下）の何倍以上か。
# 沢の風下 10〜60 m の平均の重みが、風上 10〜60 m の何倍以上か
CHECK_DRIFT_LEE_RATIO = 3.0
CHECK_DRIFT_DOWNWIND_RATIO = 1.5
CHECK_DRIFT_BAND_M = (10.0, 60.0)
# 主役の崖の断面（エンジンの座標 X, Z。Battleship Rock の頂 X -1420・Z -949 の近くを南北に横切る E 603100 の線を、
# 南の谷床（Z -250、Y 6 m 前後）から北の谷床（Z -1550、Y 20 m 前後）まで延ばしたもの）
PROFILE_START = (-1400.0, -250.0)
PROFILE_END = (-1400.0, -1550.0)
# 断面の分類の並びで数える区間の長さの下限[m]
PROFILE_MIN_RUN_M = 6.0
# 色分けの図の拡大（中心のエンジンの座標 X, Z と一辺[m]）
ZOOMS = {
    "surface_battleship.png": ("Battleship Rock", (-1420.0, -949.0), 1400.0),
    "surface_franklin.png": ("Franklin Butte", (-510.0, -319.0), 1000.0),
    "surface_wash.png": ("ground_02 の沢", (-1276.0, -1875.0), 800.0),
}


def smoothstep(value, edge0, edge1):
    t = numpy.clip((value - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def read_grid():
    with open(HEIGHT_JSON_PATH, encoding="utf-8-sig") as stream:
        descriptor = json.load(stream)
    raw_path = os.path.join(GRID_DIR, descriptor["file"])
    with open(raw_path, "rb") as stream:
        data = stream.read()
    sha = hashlib.sha256(data).hexdigest()
    heights = numpy.frombuffer(data, dtype="<f4").reshape(int(descriptor["height"]), int(descriptor["width"]))
    return descriptor, heights, sha


def downsample(heights):
    """2×2 の頂点の平均（奇数の端は最後の行・列を繰り返す）"""
    rows, columns = heights.shape
    padded = numpy.pad(heights.astype(numpy.float64), ((0, rows % 2), (0, columns % 2)), mode="edge")
    return padded.reshape(padded.shape[0] // 2, 2, padded.shape[1] // 2, 2).mean(axis=(1, 3))


def blur(values, sigma_px):
    radius = int(math.ceil(3.0 * sigma_px))
    taps = numpy.exp(-0.5 * (numpy.arange(-radius, radius + 1) / sigma_px) ** 2)
    taps /= taps.sum()
    result = values
    for axis in (0, 1):
        pad = [(0, 0), (0, 0)]
        pad[axis] = (radius, radius)
        padded = numpy.pad(result, pad, mode="edge")
        length = result.shape[axis]
        accumulated = numpy.zeros_like(result)
        for index, weight in enumerate(taps):
            accumulated += weight * (padded[index:index + length, :] if axis == 0 else padded[:, index:index + length])
        result = accumulated
    return result


def box_mean(values, radius):
    padded = numpy.pad(values, radius + 1, mode="edge")
    integral = padded.cumsum(axis=0).cumsum(axis=1)
    rows, columns = values.shape
    size = 2 * radius + 1
    total = (integral[size:size + rows, size:size + columns] - integral[0:rows, size:size + columns]
             - integral[size:size + rows, 0:columns] + integral[0:rows, 0:columns])
    return total / (size * size)


def extreme_filter(values, radius, function):
    result = values
    for axis in (0, 1):
        pad = [(0, 0), (0, 0)]
        pad[axis] = (radius, radius)
        padded = numpy.pad(result, pad, mode="edge")
        length = result.shape[axis]
        accumulated = result.copy()
        for index in range(2 * radius + 1):
            accumulated = function(accumulated, padded[index:index + length, :] if axis == 0
                                   else padded[:, index:index + length])
        result = accumulated
    return result


# 8 近傍の向き（行, 列）
NEIGHBORS = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]


def slices(offset, length):
    """配列の [source] の値を [target] へ動かす切り出し（target = source + offset）"""
    if offset >= 0:
        return slice(0, length - offset), slice(offset, length)
    return slice(-offset, length), slice(0, length + offset)


def flow_weights(smooth):
    """各画素から 8 近傍の下りへの分け方（合計 1。下りが無ければ 0）"""
    rows, columns = smooth.shape
    weights = numpy.zeros((len(NEIGHBORS), rows, columns), dtype=numpy.float64)
    for index, (dy, dx) in enumerate(NEIGHBORS):
        source_rows, target_rows = slices(dy, rows)
        source_cols, target_cols = slices(dx, columns)
        distance = TEXEL_M * math.hypot(dy, dx)
        drop = (smooth[source_rows, source_cols] - smooth[target_rows, target_cols]) / distance
        weights[index, source_rows, source_cols] = numpy.maximum(drop, 0.0) ** FLOW_EXPONENT
    total = weights.sum(axis=0)
    weights /= numpy.where(total > 0.0, total, 1.0)
    return weights, total > 0.0


def priority_flood(heights):
    """窪地を埋めた流れの向き（Priority-Flood。Barnes ほか 2014）。外周の画素と NaN（DEM の無い所）を出口にして、低い所から
    順に水位を上げ、各画素を最初に見つけた隣（それより低いか、窪地の中なら溢れ口へ向かう隣）へ流す。窪地の中と平らな所は
    先入れ先出しで広げるので、流れは溢れ口から放射状にたどる（並びの番号の順の筋にならない）。
    戻り値は (流れ先の平らな番号（出口は -1）, 処理の順の平らな番号)。番号は行 × 列数 + 列"""
    import heapq
    from collections import deque
    rows, columns = heights.shape
    width = columns + 2
    padded = numpy.full((rows + 2, width), numpy.inf)
    padded[1:-1, 1:-1] = heights
    missing = numpy.isnan(padded)
    padded[missing] = -numpy.inf
    levels = padded.ravel().tolist()
    closed_grid = numpy.zeros((rows + 2, width), dtype=numpy.uint8)
    closed_grid[0, :] = closed_grid[-1, :] = closed_grid[:, 0] = closed_grid[:, -1] = 1
    seed_grid = numpy.zeros_like(closed_grid, dtype=bool)
    seed_grid[1, 1:-1] = seed_grid[-2, 1:-1] = seed_grid[1:-1, 1] = seed_grid[1:-1, -2] = True
    seed_grid |= missing
    seeds = numpy.flatnonzero(seed_grid)
    closed_grid.ravel()[seeds] = 1
    closed = bytearray(closed_grid.tobytes())
    receiver = [-1] * padded.size
    heap = [(levels[index], index) for index in seeds.tolist()]
    heapq.heapify(heap)
    pit = deque()
    order = []
    offsets = (-width - 1, -width, -width + 1, -1, 1, width - 1, width, width + 1)
    heappop, heappush, append, popleft = heapq.heappop, heapq.heappush, order.append, pit.popleft
    while heap or pit:
        level, index = popleft() if pit else heappop(heap)
        append(index)
        for offset in offsets:
            neighbor = index + offset
            if closed[neighbor]:
                continue
            closed[neighbor] = 1
            receiver[neighbor] = index
            height = levels[neighbor]
            if height <= level:
                pit.append((level, neighbor))
            else:
                heappush(heap, (height, neighbor))
    # 枠を付けた番号から元の番号へ
    padded_receiver = numpy.array(receiver, dtype=numpy.int64).reshape(rows + 2, width)[1:-1, 1:-1].ravel()
    flat_receiver = numpy.where(padded_receiver >= 0,
                                (padded_receiver // width - 1) * columns + padded_receiver % width - 1, -1)
    padded_order = numpy.array(order, dtype=numpy.int64)
    flat_order = (padded_order // width - 1) * columns + padded_order % width - 1
    return flat_receiver, flat_order


def accumulate(receiver, order, area):
    """流れ先と処理の順（上流が後）から、各画素に集まる面積（自分を含む）"""
    total = area.ravel().astype(numpy.float64).tolist()
    targets = receiver.tolist()
    for index in reversed(order.tolist()):
        target = targets[index]
        if target >= 0:
            total[target] += total[index]
    return numpy.array(total).reshape(area.shape)


def read_wide_heights(grid):
    """地図の外へ WIDE_MARGIN_M 広げた範囲（1 m の 4 枚の内側）の、WIDE_TEXEL_M の升目の面積の平均の高さ（ワールドの y。
    DEM の無い所は NaN、行 0 が北）と、地図の外縁の升目の位置（列, 行）"""
    import rasterio
    from rasterio.enums import Resampling
    from rasterio.merge import merge
    from DemInput import DEM_PATH, NEIGHBOR_TILES, SOURCE_DIR
    edges = grid["grid_bounds_utm_edges"]
    tiles = [{"east_min": 600000, "east_max": 610000, "north_min": 4120000, "north_max": 4130000}] + NEIGHBOR_TILES
    mosaic = (min(tile["east_min"] for tile in tiles), min(tile["north_min"] for tile in tiles),
              max(tile["east_max"] for tile in tiles), max(tile["north_max"] for tile in tiles))
    bounds = (max(mosaic[0], edges["east_min"] - WIDE_MARGIN_M), max(mosaic[1], edges["north_min"] - WIDE_MARGIN_M),
              min(mosaic[2], edges["east_max"] + WIDE_MARGIN_M), min(mosaic[3], edges["north_max"] + WIDE_MARGIN_M))
    paths = [DEM_PATH] + [os.path.join(SOURCE_DIR, tile["file"]) for tile in NEIGHBOR_TILES]
    datasets = [rasterio.open(path) for path in paths]
    try:
        array, transform = merge(datasets, bounds=bounds, res=WIDE_TEXEL_M, nodata=-999999.0,
                                 resampling=Resampling.average)
    finally:
        for dataset in datasets:
            dataset.close()
    heights = array[0].astype(numpy.float64)
    heights[~(heights > -1000.0)] = numpy.nan
    heights -= float(grid["height_offset_m"])
    column0 = (edges["east_min"] - transform.c) / WIDE_TEXEL_M
    row0 = (transform.f - edges["north_max"]) / WIDE_TEXEL_M
    if abs(column0 - round(column0)) > 1e-6 or abs(row0 - round(row0)) > 1e-6:
        raise RuntimeError("広い範囲の升目が地図の外縁に揃っていない（列 %r・行 %r）" % (column0, row0))
    return heights, (int(round(column0)), int(round(row0))), bounds


def flow_accumulation(grid, flow_surface):
    """地図の画素ごとの集水面積[m²]。地図の中は 2 m の画素で窪地を埋めて流し、地図の外から入る流れは、広い範囲の 10 m の
    升目で同じように求めた集水面積を、地図の外の升目から地図の中の升目へ流れ込む所（その升目の中で最も低い画素）に足す"""
    rows, columns = flow_surface.shape
    wide, (column0, row0), bounds = read_wide_heights(grid)
    wide_receiver, wide_order = priority_flood(wide)
    ratio = int(round(WIDE_TEXEL_M / TEXEL_M))
    inside_rows = rows // ratio
    inside_columns = columns // ratio
    wide_rows, wide_columns = numpy.divmod(numpy.arange(wide.size), wide.shape[1])
    inside = ((wide_rows >= row0) & (wide_rows < row0 + inside_rows) &
              (wide_columns >= column0) & (wide_columns < column0 + inside_columns))
    # 地図の中の升目は面積を持たず、流れも先へ渡さない（地図の外へ出てまた入る流れを二重に数えない）
    cut_receiver = numpy.where(inside, -1, wide_receiver)
    wide_area = accumulate(cut_receiver, wide_order,
                           numpy.where(inside.reshape(wide.shape), 0.0, WIDE_TEXEL_M * WIDE_TEXEL_M))
    entering = numpy.flatnonzero(~inside & (wide_receiver >= 0))
    entering = entering[inside[wide_receiver[entering]]]
    area = numpy.full((rows, columns), TEXEL_M * TEXEL_M)
    injected = 0.0
    for source in entering.tolist():
        target = int(wide_receiver[source])
        block_row = (target // wide.shape[1] - row0) * ratio
        block_column = (target % wide.shape[1] - column0) * ratio
        # 外周の画素は出口なので、その 1 つ内側から選ぶ
        top, left = max(block_row, 1), max(block_column, 1)
        bottom, right = min(block_row + ratio, rows - 1), min(block_column + ratio, columns - 1)
        block = flow_surface[top:bottom, left:right]
        lowest = numpy.unravel_index(int(numpy.argmin(block)), block.shape)
        area[top + lowest[0], left + lowest[1]] += wide_area.ravel()[source]
        injected += float(wide_area.ravel()[source])
    receiver, order = priority_flood(flow_surface)
    accumulated = accumulate(receiver, order, area)
    info = {"wide_bounds_utm": [float(value) for value in bounds], "wide_shape": [int(wide.shape[1]), int(wide.shape[0])],
            "entering_cells": int(entering.size), "injected_km2": injected / 1.0e6,
            "outlets": int((receiver < 0).sum()), "drained": int(numpy.count_nonzero(receiver >= 0) + (receiver < 0).sum())}
    return accumulated, receiver, info


def wash_weight(accumulated, flow_surface, slope_deg, rock):
    """沢底（砂）の重み（0〜1）と、流路の画素ごとの幅[m]。流路の画素から幅の半分の内側を沢底にし、流路の中心より
    WASH_BANK_M 以上高い所（岸の上）へは広げない"""
    rows, columns = accumulated.shape
    log_area = numpy.log10(numpy.maximum(accumulated, 1.0))
    log_start = math.log10(WASH_AREA_M2[0])
    strength = (smoothstep(log_area, log_start, math.log10(WASH_AREA_M2[1]))
                * (1.0 - smoothstep(slope_deg, *WASH_SLOPE_DEG)) * (1.0 - rock))
    width = numpy.clip(WASH_WIDTH_M[0] + WASH_WIDTH_M[1] * (log_area - log_start), WASH_WIDTH_M[0], WASH_MAX_WIDTH_M)
    half = numpy.where(strength > 0.0, 0.5 * width, -numpy.inf)
    radius = int(math.ceil((0.5 * WASH_MAX_WIDTH_M + WASH_EDGE_M) / TEXEL_M))
    weight = numpy.zeros_like(accumulated)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            distance = TEXEL_M * math.hypot(dy, dx)
            if distance > radius * TEXEL_M:
                continue
            source_rows, target_rows = slices(dy, rows)
            source_cols, target_cols = slices(dx, columns)
            rise = flow_surface[target_rows, target_cols] - flow_surface[source_rows, source_cols]
            cover = (strength[source_rows, source_cols]
                     * smoothstep(half[source_rows, source_cols] - distance, -WASH_EDGE_M, WASH_EDGE_M)
                     * (1.0 - smoothstep(rise, 0.5 * WASH_BANK_M, WASH_BANK_M)))
            numpy.maximum(weight[target_rows, target_cols], cover, out=weight[target_rows, target_cols])
    return weight, numpy.where(strength > 0.0, width, 0.0)


def wind_offset(distance):
    """風下へ distance[m] 進む画素のずれ（行, 列）。行は南、列は東へ増える"""
    toward = math.radians(WIND_FROM_AZIMUTH_DEG + 180.0)
    return (int(round(-math.cos(toward) * distance / TEXEL_M)), int(round(math.sin(toward) * distance / TEXEL_M)))


def shifted_downwind(values, distance, fill):
    """各画素に、風上 distance[m] の画素の値を入れた配列（範囲の外は fill）"""
    rows, columns = values.shape
    dy, dx = wind_offset(distance)
    result = numpy.full_like(values, fill)
    source_rows, target_rows = slices(dy, rows)
    source_cols, target_cols = slices(dx, columns)
    result[target_rows, target_cols] = values[source_rows, source_cols]
    return result


def drift_weight(smooth, slope_deg, wash):
    """砂の吹きだまりの重み（0〜1）と、風上が見下ろす角の正接。砂は沢底から風下へ運ばれ（沢から遠い所にも薄く）、
    風上の高まりの陰（風上を見上げる角が大きい所）に溜まる"""
    shelter = numpy.full_like(smooth, -numpy.inf)
    for distance in numpy.arange(DRIFT_SHELTER_M[0], DRIFT_SHELTER_M[1] + 0.5 * TEXEL_M, 2.0 * TEXEL_M):
        upwind = shifted_downwind(smooth, float(distance), -numpy.inf)
        numpy.maximum(shelter, (upwind - smooth) / float(distance), out=shelter)
    supply = numpy.full_like(smooth, DRIFT_BASE_SUPPLY)
    for distance in numpy.arange(0.0, DRIFT_REACH_M + 0.5 * TEXEL_M, 2.0 * TEXEL_M):
        numpy.maximum(supply, shifted_downwind(wash, float(distance), 0.0) * math.exp(-distance / DRIFT_DECAY_M),
                      out=supply)
    drift = (DRIFT_MAX * supply * smoothstep(shelter, *DRIFT_SHELTER_TANGENT)
             * (1.0 - smoothstep(slope_deg, *DRIFT_SLOPE_DEG)))
    return numpy.clip(blur(drift, DRIFT_BLUR_M / TEXEL_M), 0.0, 1.0), shelter


def talus_deposit(smooth, slope_deg, rock, tpi_small):
    """露岩から落ちる岩屑の溜まる量と、落石の届く範囲（エネルギー線が地面より上の画素）"""
    rows, columns = smooth.shape
    weights, has_downhill = flow_weights(blur(smooth, FLOW_SIGMA_M / TEXEL_M))
    source = numpy.where(rock >= SOURCE_MIN_ROCK, rock * numpy.minimum(numpy.tan(numpy.radians(slope_deg)), 8.0), 0.0)
    # エネルギー線の始まりは崖の上端（3×3 の最高）
    top = extreme_filter(smooth, 1, numpy.maximum)
    energy_source = numpy.where(source > 0.0, top, -numpy.inf)
    deposit_rate = DEPOSIT_GENTLE + (DEPOSIT_STEEP - DEPOSIT_GENTLE) * smoothstep(slope_deg, *DEPOSIT_SLOPE_DEG)
    hollow = smoothstep(-tpi_small, *HOLLOW_TPI_M)
    deposit_rate = numpy.clip(deposit_rate * (HOLLOW_GAIN[0] + (HOLLOW_GAIN[1] - HOLLOW_GAIN[0]) * hollow), 0.0, 1.0)
    deposit_rate = numpy.where(has_downhill, deposit_rate, 1.0)
    reach_drop = math.tan(math.radians(REACH_ANGLE_DEG)) * TEXEL_M
    amount = source.copy()
    energy = energy_source.copy()
    iterations = 0
    for iterations in range(1, FLOW_MAX_ITERATIONS + 1):
        reachable = energy >= smooth
        passing = numpy.where(reachable, amount * (1.0 - deposit_rate), 0.0)
        inflow = numpy.zeros_like(amount)
        carried = energy_source.copy()
        for index, (dy, dx) in enumerate(NEIGHBORS):
            source_rows, target_rows = slices(dy, rows)
            source_cols, target_cols = slices(dx, columns)
            share = weights[index, source_rows, source_cols]
            inflow[target_rows, target_cols] += share * passing[source_rows, source_cols]
            candidate = numpy.where((share > 0.0) & reachable[source_rows, source_cols],
                                    energy[source_rows, source_cols] - reach_drop * math.hypot(dy, dx), -numpy.inf)
            numpy.maximum(carried[target_rows, target_cols], candidate, out=carried[target_rows, target_cols])
        new_amount = source + inflow
        settled = numpy.array_equal(carried, energy) and float(numpy.abs(new_amount - amount).max()) < 1e-9
        amount = new_amount
        energy = carried
        if settled:
            break
    reachable = energy >= smooth
    deposit = numpy.where(reachable, amount * deposit_rate, 0.0)
    # 筋の幅の量をならして塊にし、届かない所へははみ出させない
    deposit = numpy.where(reachable, blur(deposit, DEPOSIT_BLUR_M / TEXEL_M), 0.0)
    return deposit, reachable, energy, iterations


def compute(grid, heights):
    """格子から重み（5, 行, 列。露岩・崖錐・細粒・礫・砂。合計 1）と、確かめに使う途中の値を求める"""
    texel = int(TEXEL_M)
    base = downsample(heights)
    smooth = blur(base, SMOOTH_SIGMA_M / TEXEL_M)
    gradient_z, gradient_x = numpy.gradient(smooth, TEXEL_M)
    slope_deg = numpy.degrees(numpy.arctan(numpy.hypot(gradient_x, gradient_z)))
    tpi_small = smooth - box_mean(smooth, int(round(TPI_SMALL_RADIUS_M / texel)))
    tpi_mid = smooth - box_mean(smooth, int(round(TPI_MID_RADIUS_M / texel)))
    relief_radius = int(round(RELIEF_RADIUS_M / texel))
    relief = extreme_filter(smooth, relief_radius, numpy.maximum) - extreme_filter(smooth, relief_radius, numpy.minimum)

    steep_rock = smoothstep(slope_deg, *ROCK_SLOPE_DEG) * smoothstep(relief, *ROCK_RELIEF_M)
    rim_rock = RIM_WEIGHT * smoothstep(tpi_small, *RIM_TPI_M) * smoothstep(slope_deg, *RIM_SLOPE_DEG)
    rock = numpy.maximum(steep_rock, rim_rock)

    deposit, reachable, energy, iterations = talus_deposit(smooth, slope_deg, steep_rock, tpi_small)
    talus = (1.0 - numpy.exp(-deposit / TALUS_SCALE)) * (1.0 - rock)
    fringe = 1.0 - numpy.exp(-deposit / FRINGE_SCALE)
    ground = numpy.clip(1.0 - rock - talus, 0.0, 1.0)

    gravel_share = (GRAVEL_BASE + 0.45 * smoothstep(slope_deg, *GRAVEL_SLOPE_DEG)
                    + 0.35 * smoothstep(tpi_mid, *GRAVEL_TPI_M) - 0.25 * smoothstep(-tpi_mid, *GRAVEL_TPI_M)
                    + 0.6 * numpy.maximum(fringe - talus, 0.0))
    gravel_share = numpy.clip(gravel_share, 0.0, 1.0)
    weights = numpy.stack([rock, talus, ground * (1.0 - gravel_share), ground * gravel_share])
    weights /= weights.sum(axis=0, keepdims=True)

    # 沢底は 4 つの分類を置き換え、吹きだまりは地表（細粒と礫）の上にだけ載る
    flow_surface = blur(base, FLOW_SURFACE_SIGMA_M / TEXEL_M)
    accumulated, receiver, flow_info = flow_accumulation(grid, flow_surface)
    wash, wash_width = wash_weight(accumulated, flow_surface, slope_deg, rock)
    drift, shelter = drift_weight(smooth, slope_deg, wash)
    weights *= 1.0 - wash
    drift_taken = drift * (weights[2] + weights[3])
    weights[2:4] *= 1.0 - drift
    weights = numpy.concatenate([weights, (wash + drift_taken)[None]])
    fields = {"smooth": smooth, "slope_deg": slope_deg, "tpi_small": tpi_small, "tpi_mid": tpi_mid, "relief": relief,
              "steep_rock": steep_rock, "deposit": deposit, "reachable": reachable, "energy": energy,
              "iterations": iterations, "flow_surface": flow_surface, "accumulated": accumulated, "receiver": receiver,
              "flow_info": flow_info, "wash": wash, "wash_width": wash_width, "drift": drift, "shelter": shelter}
    return weights, fields


def quantize(weights):
    """重みを 8 ビットにし、画素ごとの合計をちょうど 255 にする（端数の大きい順に 1 を足す）"""
    scaled = weights * 255.0
    base = numpy.floor(scaled)
    remainder = (255 - base.sum(axis=0)).astype(numpy.int64)
    order = numpy.argsort(-(scaled - base), axis=0, kind="stable")
    rank = numpy.argsort(order, axis=0, kind="stable")
    base += rank < remainder[None, :, :]
    return base.astype(numpy.uint8)


def descriptor_for(grid, grid_sha, rows, columns, stats, iterations, flow_info):
    first_x = float(grid["first_vertex_engine"]["x"])
    first_z = float(grid["first_vertex_engine"]["z"])
    spacing = float(grid["vertex_spacing_m"])
    # 格子の頂点は DEM の画素の中心なので、地図の外縁は最初の頂点から半画素外
    edge_x = first_x - 0.5 * spacing
    edge_z = first_z - 0.5 * spacing
    return {
        "format": "PNG RGBA8, row 0 = north (engine z min), column 0 = west (engine x min)",
        "file": os.path.basename(MAP_PATH),
        "channels": {"r": "rock", "g": "talus", "b": "ground_fine", "a": "ground_gravel", "255-r-g-b-a": "sand"},
        "channels_note": "r 露岩・g 崖錐・b 地表（細粒）・a 地表（礫）。砂（沢底と吹きだまり）の重みは 255 から 4 つの和を引いた残り"
                         "（5 つの合計が 255）",
        "width": columns,
        "height": rows,
        "texel_size_m": TEXEL_M,
        "engine_edges": {"x_min": edge_x, "x_max": edge_x + columns * TEXEL_M,
                         "z_min": edge_z, "z_max": edge_z + rows * TEXEL_M},
        "uv_note": "u = (x - x_min) / (x_max - x_min)、v = (z - z_min) / (z_max - z_min)。画素 (列 i, 行 j) の中心は "
                   "x = x_min + (i + 0.5) × texel_size_m、z = z_min + (j + 0.5) × texel_size_m（格子の頂点 2i・2i+1 の中ほど）",
        "grid": {"descriptor": "height.json", "sha256": grid_sha},
        "parameters": {
            "smooth_sigma_m": SMOOTH_SIGMA_M, "tpi_radii_m": [TPI_SMALL_RADIUS_M, TPI_MID_RADIUS_M],
            "relief_radius_m": RELIEF_RADIUS_M, "rock_slope_deg": list(ROCK_SLOPE_DEG), "rock_relief_m": list(ROCK_RELIEF_M),
            "rim_tpi_m": list(RIM_TPI_M), "rim_slope_deg": list(RIM_SLOPE_DEG), "rim_weight": RIM_WEIGHT,
            "reach_angle_deg": REACH_ANGLE_DEG, "flow_exponent": FLOW_EXPONENT,
            "deposit_slope_deg": list(DEPOSIT_SLOPE_DEG), "deposit_steep": DEPOSIT_STEEP,
            "deposit_gentle": DEPOSIT_GENTLE, "hollow_gain": list(HOLLOW_GAIN), "hollow_tpi_m": list(HOLLOW_TPI_M),
            "flow_sigma_m": FLOW_SIGMA_M, "deposit_blur_m": DEPOSIT_BLUR_M,
            "talus_scale": TALUS_SCALE, "fringe_scale": FRINGE_SCALE,
            "gravel_base": GRAVEL_BASE, "gravel_slope_deg": list(GRAVEL_SLOPE_DEG), "gravel_tpi_m": list(GRAVEL_TPI_M),
            "wide_margin_m": WIDE_MARGIN_M, "wide_texel_m": WIDE_TEXEL_M, "flow_surface_sigma_m": FLOW_SURFACE_SIGMA_M,
            "wash_area_m2": list(WASH_AREA_M2), "wash_width_m": list(WASH_WIDTH_M), "wash_max_width_m": WASH_MAX_WIDTH_M,
            "wash_edge_m": WASH_EDGE_M, "wash_bank_m": WASH_BANK_M, "wash_slope_deg": list(WASH_SLOPE_DEG),
            "wind_from_azimuth_deg": WIND_FROM_AZIMUTH_DEG, "drift_shelter_m": list(DRIFT_SHELTER_M),
            "drift_shelter_tangent": list(DRIFT_SHELTER_TANGENT), "drift_reach_m": DRIFT_REACH_M,
            "drift_decay_m": DRIFT_DECAY_M, "drift_base_supply": DRIFT_BASE_SUPPLY, "drift_max": DRIFT_MAX,
            "drift_slope_deg": list(DRIFT_SLOPE_DEG), "drift_blur_m": DRIFT_BLUR_M,
        },
        "flow_iterations": iterations,
        "drainage": flow_info,
        "coverage": stats,
        "generator": "Scripts/Terrain/TerrainMasks.py",
    }


def coverage(quantized):
    dominant = quantized.argmax(axis=0)
    return {CLASS_NAMES[index]: round(float((dominant == index).mean()), 5) for index in range(len(CLASS_NAMES))}


def hillshade(smooth):
    gradient_z, gradient_x = numpy.gradient(smooth, TEXEL_M)
    # 北西（方位 315°・仰角 45°）から照らす。行が南へ進むので北向きの勾配は -gradient_z
    azimuth, altitude = math.radians(315.0), math.radians(45.0)
    light = (math.sin(azimuth) * math.cos(altitude), math.cos(azimuth) * math.cos(altitude), math.sin(altitude))
    shade = (-gradient_x * light[0] + gradient_z * light[1] + light[2]) / numpy.sqrt(
        gradient_x ** 2 + gradient_z ** 2 + 1.0)
    return numpy.clip(shade, 0.0, 1.0)


def class_image(quantized, shade):
    color = numpy.tensordot(quantized.astype(numpy.float32) / 255.0, CLASS_COLORS, axes=([0], [0]))
    lit = color * (0.35 + 0.65 * shade[:, :, None])
    return numpy.clip(lit + 0.5, 0, 255).astype(numpy.uint8)


def draw_legend(draw, font, x, y):
    for index, name in enumerate(CLASS_NAMES):
        top = y + index * 22
        draw.rectangle([x, top, x + 16, top + 16], fill=tuple(int(c) for c in CLASS_COLORS[index]), outline=(0, 0, 0))
        draw.text((x + 22, top - 1), name, fill=(255, 255, 255), font=font, stroke_width=2, stroke_fill=(0, 0, 0))


def write_previews(quantized, fields, descriptor):
    from PIL import Image, ImageDraw
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    font = label_font()
    image = Image.fromarray(class_image(quantized, hillshade(fields["smooth"])), mode="RGB")
    edges = descriptor["engine_edges"]

    whole = image.resize((image.width // 2, image.height // 2), Image.LANCZOS)
    draw = ImageDraw.Draw(whole)
    draw.text((8, 6), "地表の分類（%.0f m/画素、図は 1/2。北が上・東が右、北西から照らした陰影）" % TEXEL_M, fill=(255, 255, 255),
              font=font, stroke_width=2, stroke_fill=(0, 0, 0))
    draw_legend(draw, font, 8, 30)
    for name, (center, size) in ((v[0], (v[1], v[2])) for v in ZOOMS.values()):
        x0 = (center[0] - size / 2 - edges["x_min"]) / TEXEL_M / 2
        z0 = (center[1] - size / 2 - edges["z_min"]) / TEXEL_M / 2
        side = size / TEXEL_M / 2
        draw.rectangle([x0, z0, x0 + side, z0 + side], outline=(255, 255, 255), width=2)
        draw.text((x0 + 4, z0 + 2), name, fill=(255, 255, 255), font=font, stroke_width=2, stroke_fill=(0, 0, 0))
    whole.save(os.path.join(PREVIEW_DIR, PREVIEWS[0]))

    for file_name, (name, center, size) in ZOOMS.items():
        left = int(round((center[0] - size / 2 - edges["x_min"]) / TEXEL_M))
        top = int(round((center[1] - size / 2 - edges["z_min"]) / TEXEL_M))
        side = int(round(size / TEXEL_M))
        crop = image.crop((left, top, left + side, top + side)).resize((side * 2, side * 2), Image.NEAREST)
        draw = ImageDraw.Draw(crop)
        draw.text((8, 6), "%s の周り（一辺 %.0f m、%.0f m/画素を 2 倍）" % (name, size, TEXEL_M), fill=(255, 255, 255),
                  font=font, stroke_width=2, stroke_fill=(0, 0, 0))
        draw_legend(draw, font, 8, 30)
        if file_name == "surface_battleship.png":
            # 確かめに使う南北の断面の線
            points = [(((x - edges["x_min"]) / TEXEL_M - left) * 2, ((z - edges["z_min"]) / TEXEL_M - top) * 2)
                      for x, z in (PROFILE_START, PROFILE_END)]
            draw.line(points, fill=(255, 255, 255), width=1)
            draw.text((points[0][0] + 6, points[0][1] - 22), "断面", fill=(255, 255, 255), font=font, stroke_width=2,
                      stroke_fill=(0, 0, 0))
        if file_name == "surface_wash.png":
            # 手本の ground_02 の撮影位置
            px = ((CHECK_WASH_PHOTO[0] - edges["x_min"]) / TEXEL_M - left) * 2
            pz = ((CHECK_WASH_PHOTO[1] - edges["z_min"]) / TEXEL_M - top) * 2
            draw.ellipse([px - 8, pz - 8, px + 8, pz + 8], outline=(255, 255, 255), width=2)
            draw.text((px + 12, pz - 10), "ground_02 の撮影位置", fill=(255, 255, 255), font=font, stroke_width=2,
                      stroke_fill=(0, 0, 0))
        crop.save(os.path.join(PREVIEW_DIR, file_name))


def build():
    grid, heights, grid_sha = read_grid()
    weights, fields = compute(grid, heights)
    quantized = quantize(weights)
    stats = coverage(quantized)
    descriptor = descriptor_for(grid, grid_sha, quantized.shape[1], quantized.shape[2], stats, fields["iterations"],
                                fields["flow_info"])
    from PIL import Image
    Image.fromarray(numpy.ascontiguousarray(quantized[:4].transpose(1, 2, 0)), mode="RGBA").save(MAP_PATH)
    with open(MAP_JSON_PATH, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(descriptor, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    write_previews(quantized, fields, descriptor)
    print("書いた: %s（%d × %d、%.0f m/画素、流れの反復 %d 回）" % (
        os.path.relpath(MAP_PATH, REPO_ROOT), descriptor["width"], descriptor["height"], TEXEL_M, fields["iterations"]))
    print("主な分類の面積の割合: %s" % "、".join("%s %.1f %%" % (k, 100.0 * v) for k, v in stats.items()))
    return 0


def distance_to(mask, max_steps):
    """mask の画素からの水平の距離[m]（8 近傍の面取りの距離。max_steps 画素より遠い所は inf）"""
    rows, columns = mask.shape
    distance = numpy.where(mask, 0.0, numpy.inf)
    for _ in range(max_steps):
        updated = distance.copy()
        for dy, dx in NEIGHBORS:
            source_rows, target_rows = slices(dy, rows)
            source_cols, target_cols = slices(dx, columns)
            numpy.minimum(updated[target_rows, target_cols],
                          distance[source_rows, source_cols] + TEXEL_M * math.hypot(dy, dx),
                          out=updated[target_rows, target_cols])
        if numpy.array_equal(updated, distance):
            break
        distance = updated
    return distance


def profile_samples(quantized, fields, edges):
    """断面の 2 m ごとの（主な分類, ならした高さ, 傾斜）"""
    (x0, z0), (x1, z1) = PROFILE_START, PROFILE_END
    length = math.hypot(x1 - x0, z1 - z0)
    steps = int(length / TEXEL_M)
    samples = []
    for step in range(steps + 1):
        t = step / steps
        column = int((x0 + (x1 - x0) * t - edges["x_min"]) / TEXEL_M)
        row = int((z0 + (z1 - z0) * t - edges["z_min"]) / TEXEL_M)
        samples.append((int(quantized[:, row, column].argmax()), float(fields["smooth"][row, column]),
                        float(fields["slope_deg"][row, column])))
    return samples


def runs(classes):
    """同じ分類の続く区間 [分類, 始まりの番号, 長さ]（PROFILE_MIN_RUN_M より短い区間は前の区間に含める）"""
    result = []
    for index, value in enumerate(classes):
        if result and result[-1][0] == value:
            result[-1][2] += 1
        else:
            result.append([value, index, 1])
    minimum = int(PROFILE_MIN_RUN_M / TEXEL_M)
    merged = []
    for run in result:
        if merged and (run[2] < minimum or merged[-1][0] == run[0]):
            merged[-1][2] += run[2]
        else:
            merged.append(list(run))
    return merged


def run_at(sequence, index):
    return next(position for position, run in enumerate(sequence) if run[1] <= index < run[1] + run[2])


def verify():
    failures = []

    def check(condition, message):
        print("%s %s" % ("PASS" if condition else "FAIL", message))
        if not condition:
            failures.append(message)

    from PIL import Image
    check(os.path.isfile(MAP_PATH) and os.path.isfile(MAP_JSON_PATH), "地図と記述がある: %s・%s" % (
        os.path.relpath(MAP_PATH, REPO_ROOT), os.path.relpath(MAP_JSON_PATH, REPO_ROOT)))
    if failures:
        print("result=fail (%d)" % len(failures))
        return 1
    grid, heights, grid_sha = read_grid()
    with open(MAP_JSON_PATH, encoding="utf-8") as stream:
        descriptor = json.load(stream)
    written = numpy.asarray(Image.open(MAP_PATH))
    print("地図: %d × %d・%s、%.1f m/画素" % (written.shape[1], written.shape[0], Image.open(MAP_PATH).mode,
                                           descriptor["texel_size_m"]))

    # 格子との対応
    check(descriptor["grid"]["sha256"] == grid_sha == grid["sha256"], "記述の格子の SHA-256 が今の格子と同じ（%s）" % grid_sha[:16])
    check(1.0 <= descriptor["texel_size_m"] <= 2.0, "画素の大きさ %.1f m が 1〜2 m" % descriptor["texel_size_m"])
    edges = descriptor["engine_edges"]
    bounds = grid["engine_bounds"]
    spacing = float(grid["vertex_spacing_m"])
    edge_error = max(abs(edges["x_min"] - (bounds["x_min"] - 0.5 * spacing)),
                     abs(edges["z_min"] - (bounds["z_min"] - 0.5 * spacing)),
                     abs(edges["x_max"] - (bounds["x_max"] + 0.5 * spacing)),
                     abs(edges["z_max"] - (bounds["z_max"] + 0.5 * spacing)))
    check(edge_error < 1e-6, "地図の外縁が格子の画素の外縁と一致する（X %.1f〜%.1f・Z %.1f〜%.1f、ずれ %.3g m）" % (
        edges["x_min"], edges["x_max"], edges["z_min"], edges["z_max"], edge_error))

    # 作り直して一致
    weights, fields = compute(grid, heights)
    quantized = quantize(weights)
    rebuilt = numpy.ascontiguousarray(quantized[:4].transpose(1, 2, 0))
    check(written.shape == rebuilt.shape and numpy.array_equal(written, rebuilt),
          "格子から作り直した地図が書いた地図とビット単位で一致する（流れの反復 %d 回）" % fields["iterations"])
    sums = written.astype(numpy.int32).sum(axis=2)
    implicit = 255 - quantized[:4].astype(numpy.int32).sum(axis=0)
    check(bool((sums <= 255).all()) and numpy.array_equal(implicit, quantized[4].astype(numpy.int32)),
          "画素ごとの 4 つの重みの和が 255 以下で、残りが砂の重み（4 つの和の最小 %d・最大 %d）" % (sums.min(), sums.max()))
    check(fields["iterations"] < FLOW_MAX_ITERATIONS, "崖錐の流れが反復の上限の前に落ち着いた（%d 回 < %d）" % (
        fields["iterations"], FLOW_MAX_ITERATIONS))

    dominant = quantized.argmax(axis=0)
    slope_deg = fields["slope_deg"]
    stats = coverage(quantized)
    print("主な分類の面積の割合: %s" % "、".join("%s %.2f %%" % (k, 100.0 * v) for k, v in stats.items()))
    for name, (low, high) in CHECK_COVERAGE.items():
        check(low <= stats[name] <= high, "%s の面積の割合 %.2f %% が %.1f〜%.0f %%" % (name, 100.0 * stats[name],
                                                                                 100.0 * low, 100.0 * high))

    # 流れ（沢）: 窪地を埋めて、地図の中に行き止まりが無い（出口は外周だけ）
    rows, columns = slope_deg.shape
    info = fields["flow_info"]
    receiver = fields["receiver"]
    flow_surface = fields["flow_surface"].ravel()
    check(info["outlets"] == 2 * (rows + columns) - 4,
          "流れの出口は地図の外周の画素だけで、中に行き止まりの窪地が無い（出口 %d・外周 %d）" % (
              info["outlets"], 2 * (rows + columns) - 4))
    climbing = int(((receiver >= 0) & (flow_surface[numpy.maximum(receiver, 0)] > flow_surface)).sum())
    print("参考: 窪地の中で溢れ口へ向かって上る画素 %d（%.2f %%）" % (climbing, 100.0 * climbing / receiver.size))
    check(info["entering_cells"] > 0 and info["injected_km2"] > 0.0,
          "地図の外 %.0f km まで（%d × %d の %.0f m の升目）の流れが %d 升目で地図へ入り、集水面積 %.1f km² を足した" % (
              WIDE_MARGIN_M / 1000.0, info["wide_shape"][0], info["wide_shape"][1], WIDE_TEXEL_M,
              info["entering_cells"], info["injected_km2"]))
    # 手本の ground_02 の沢の幅
    photo_column = int((CHECK_WASH_PHOTO[0] - edges["x_min"]) / TEXEL_M)
    photo_row = int((CHECK_WASH_PHOTO[1] - edges["z_min"]) / TEXEL_M)
    reach = int(CHECK_WASH_PHOTO_RADIUS_M / TEXEL_M)
    window = (slice(photo_row - reach, photo_row + reach + 1), slice(photo_column - reach, photo_column + reach + 1))
    area_window = fields["accumulated"][window]
    peak = numpy.unravel_index(int(numpy.argmax(area_window)), area_window.shape)
    photo_width = float(fields["wash_width"][window][peak])
    photo_area = float(area_window[peak]) / 1.0e6
    check(CHECK_WASH_PHOTO_WIDTH_M[0] <= photo_width <= CHECK_WASH_PHOTO_WIDTH_M[1],
          "手本の ground_02 の撮影位置から %.0f m 以内の沢（集水面積 %.1f km²）の沢底の幅 %.1f m が写真の %.0f〜%.0f m" % (
              CHECK_WASH_PHOTO_RADIUS_M, photo_area, photo_width, *CHECK_WASH_PHOTO_WIDTH_M))
    # 沢底は谷の底に沿う
    wash_mask = fields["wash"] >= 0.5
    low_share = float((fields["tpi_small"][wash_mask] <= 0.0).mean())
    wash_slope = float(numpy.median(slope_deg[wash_mask]))
    check(low_share >= CHECK_WASH_LOW_SHARE, "沢底（重み 0.5 以上、%d 画素）の %.1f %% が周りより低い所（%.0f %% 以上）" % (
        int(wash_mask.sum()), 100.0 * low_share, 100.0 * CHECK_WASH_LOW_SHARE))
    check(wash_slope <= CHECK_WASH_SLOPE_DEG, "沢底の傾斜の中央値 %.1f°が %.0f°以下" % (wash_slope, CHECK_WASH_SLOPE_DEG))
    # 吹きだまりは風下の陰と、沢の風下に寄る
    drift = fields["drift"]
    open_ground = ~wash_mask & (slope_deg < DRIFT_SLOPE_DEG[0])
    lee = open_ground & (fields["shelter"] >= 0.08)
    exposed = open_ground & (fields["shelter"] <= 0.0)
    lee_ratio = float(drift[lee].mean()) / max(float(drift[exposed].mean()), 1e-6)
    check(lee_ratio >= CHECK_DRIFT_LEE_RATIO, "吹きだまりの平均の重みが、風下の陰（%d 画素）で陰でない所（%d 画素）の %.1f 倍（%.0f 倍以上）" % (
        int(lee.sum()), int(exposed.sum()), lee_ratio, CHECK_DRIFT_LEE_RATIO))
    downwind = numpy.zeros_like(wash_mask)
    upwind = numpy.zeros_like(wash_mask)
    for distance in numpy.arange(CHECK_DRIFT_BAND_M[0], CHECK_DRIFT_BAND_M[1] + 0.5 * TEXEL_M, TEXEL_M):
        downwind |= shifted_downwind(wash_mask, float(distance), False)
        upwind |= shifted_downwind(wash_mask, -float(distance), False)
    # 沢の風上の岸は沢へ下る風下の面（陰）が多いので、陰の画素どうしで比べて、砂を運ぶ向きの効きだけを見る
    band = ~wash_mask & ~(downwind & upwind) & lee
    downwind_ratio = float(drift[band & downwind].mean()) / max(float(drift[band & upwind].mean()), 1e-6)
    check(downwind_ratio >= CHECK_DRIFT_DOWNWIND_RATIO,
          "沢の風下 %.0f〜%.0f m の陰の吹きだまりの平均の重みが、風上の陰の %.1f 倍（%.1f 倍以上。風は方位 %.0f°から）" % (
              CHECK_DRIFT_BAND_M[0], CHECK_DRIFT_BAND_M[1], downwind_ratio, CHECK_DRIFT_DOWNWIND_RATIO,
              WIND_FROM_AZIMUTH_DEG))

    # 崖に露岩
    steep = (slope_deg >= CHECK_STEEP_SLOPE_DEG) & (fields["relief"] >= CHECK_STEEP_RELIEF_M)
    steep_rock = float((dominant[steep] == 0).mean())
    check(steep_rock >= CHECK_STEEP_ROCK_SHARE, "崖（傾斜 %.0f°以上・起伏 %.0f m 以上、%d 画素）の %.1f %% が露岩（%.0f %% 以上）" % (
        CHECK_STEEP_SLOPE_DEG, CHECK_STEEP_RELIEF_M, int(steep.sum()), 100.0 * steep_rock, 100.0 * CHECK_STEEP_ROCK_SHARE))

    # 平らな所に地表（落石の届かない所）
    flat = (slope_deg <= CHECK_FLAT_SLOPE_DEG) & ~fields["reachable"]
    flat_ground = float((dominant[flat] >= 2).mean())
    check(flat_ground >= CHECK_FLAT_GROUND_SHARE, "落石の届かない平らな所（傾斜 %.0f°以下、%d 画素）の %.2f %% が地表（%.0f %% 以上）" % (
        CHECK_FLAT_SLOPE_DEG, int(flat.sum()), 100.0 * flat_ground, 100.0 * CHECK_FLAT_GROUND_SHARE))
    flat_all = slope_deg <= CHECK_FLAT_SLOPE_DEG
    print("参考: 平らな所すべて（%d 画素）の地表 %.2f %%・崖錐 %.2f %%" % (
        int(flat_all.sum()), 100.0 * float((dominant[flat_all] >= 2).mean()),
        100.0 * float((dominant[flat_all] == 1).mean())))

    # 崖の下の裾に崖錐
    talus = dominant == 1
    rock_mask = fields["steep_rock"] >= SOURCE_MIN_ROCK
    talus_below = float((fields["energy"][talus] >= fields["smooth"][talus]).mean()) if talus.any() else 0.0
    check(talus_below == 1.0, "崖錐の画素（%d）はすべて落石の届く所（崖の上端から %.0f°の線より下）" % (
        int(talus.sum()), REACH_ANGLE_DEG))
    talus_slope = numpy.percentile(slope_deg[talus], [10, 50, 90])
    check(CHECK_TALUS_SLOPE_DEG[0] <= talus_slope[1] <= CHECK_TALUS_SLOPE_DEG[1],
          "崖錐の傾斜の中央値 %.1f°が %.0f〜%.0f°（10 %% %.1f°・90 %% %.1f°）" % (
              talus_slope[1], CHECK_TALUS_SLOPE_DEG[0], CHECK_TALUS_SLOPE_DEG[1], talus_slope[0], talus_slope[2]))
    distance = distance_to(rock_mask, int(CHECK_NEAR_CLIFF_M / TEXEL_M) + 1)
    talus_distance = numpy.percentile(distance[talus], [50, 90, 99])
    print("崖錐の崖からの距離: 中央値 %.0f m・90 %% %.0f m・99 %% %.0f m" % tuple(talus_distance))
    check(talus_distance[2] <= CHECK_NEAR_CLIFF_M, "崖錐の 99 %% が崖から %.0f m 以内（%.0f m）" % (
        CHECK_NEAR_CLIFF_M, talus_distance[2]))

    # 崖錐は距離だけで決まっていない（崖の近くの帯の中で、距離ごとの平均が崖錐の重みの分散を説明する割合）
    near = (distance > 0.0) & (distance <= CHECK_NEAR_CLIFF_M) & ~rock_mask & (fields["smooth"] < fields["energy"])
    talus_weight = quantized[1].astype(numpy.float64)[near] / 255.0
    bins = numpy.floor(distance[near] / (2.0 * TEXEL_M)).astype(numpy.int64)
    counts = numpy.bincount(bins)
    means = numpy.bincount(bins, weights=talus_weight) / numpy.maximum(counts, 1)
    explained = float(((means[bins] - talus_weight.mean()) ** 2).mean() / max(talus_weight.var(), 1e-12))
    check(explained <= CHECK_DISTANCE_ONLY_MAX, "崖から %.0f m 以内の崖より低い所で、崖からの距離が崖錐の重みのばらつきを説明する割合 %.2f が "
          "%.1f 以下（距離だけで決まっていない）" % (CHECK_NEAR_CLIFF_M, explained, CHECK_DISTANCE_ONLY_MAX))
    # 斜面の向き: 崖錐は崖より低い側にあり、崖の上の台地には出ない
    above = (fields["smooth"] > fields["energy"]) & (distance <= 20.0) & ~rock_mask
    above_talus = float((dominant[above] == 1).mean()) if above.any() else 0.0
    check(above_talus == 0.0, "崖の縁の上側（落石の線より上で崖から 20 m 以内、%d 画素）に崖錐が無い（%.2f %%）" % (
        int(above.sum()), 100.0 * above_talus))

    # 主役の崖の断面（南→北）: 両端の谷床は地表、頂の南と北でいちばん急な崖は露岩で、そのすぐ下（谷床の側）は崖錐
    samples = profile_samples(quantized, fields, edges)
    classes = [value for value, _, _ in samples]
    sequence = runs(classes)
    text = " → ".join("%s %d m" % (CLASS_NAMES[value], length * TEXEL_M) for value, _, length in sequence)
    print("Battleship Rock の南北の断面（X %.0f、Z %.0f → %.0f）の分類の並び（%.0f m 未満の区間は前に含める）: %s" % (
        PROFILE_START[0], PROFILE_START[1], PROFILE_END[1], PROFILE_MIN_RUN_M, text))
    summit = max(range(len(samples)), key=lambda index: samples[index][1])
    check(classes[0] >= 2 and classes[-1] >= 2, "断面の両端（谷床、Y %.0f m・%.0f m）が地表（%s・%s）" % (
        samples[0][1], samples[-1][1], CLASS_NAMES[classes[0]], CLASS_NAMES[classes[-1]]))
    for side, indices, downhill in (("南", range(0, summit), -1), ("北", range(summit + 1, len(samples)), 1)):
        cliff = max(indices, key=lambda index: samples[index][2])
        position = run_at(sequence, cliff)
        below = position + downhill
        below_class = sequence[below][0] if 0 <= below < len(sequence) else -1
        check(sequence[position][0] == 0 and below_class == 1,
              "頂の%sでいちばん急な崖（傾斜 %.0f°・Y %.0f m）が露岩で、そのすぐ下が崖錐（%s %d m → %s）" % (
                  side, samples[cliff][2], samples[cliff][1], CLASS_NAMES[sequence[position][0]],
                  sequence[position][2] * TEXEL_M, CLASS_NAMES[below_class] if below_class >= 0 else "なし"))

    # 図
    map_time = os.path.getmtime(MAP_PATH)
    for name in PREVIEWS:
        path = os.path.join(PREVIEW_DIR, name)
        check(os.path.isfile(path) and os.path.getmtime(path) >= map_time, "色分けの図がある（地図より新しい）: preview/%s" % name)

    ignored = all(subprocess.run(["git", "check-ignore", "-q", path], cwd=REPO_ROOT).returncode == 0
                  for path in (MAP_PATH, MAP_JSON_PATH))
    check(ignored, "地図と記述が git の無視対象（格子と同じ Assets/Terrain/ValleyOfTheGods/）")

    print("result=%s" % ("pass" if not failures else "fail (%d)" % len(failures)))
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser(description="荒野の DEM の格子から地表の分類の重みの地図を焼く")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--build", action="store_true", help="地図・記述・色分けの図を書く")
    group.add_argument("--verify", action="store_true", help="地図を作り直して一致と分類の置かれ方を確かめる")
    args = parser.parse_args()
    # 出力をファイルへ保存して読むので、端末の文字コードに依らず UTF-8 で書く
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if args.build:
        return build()
    return verify()


if __name__ == "__main__":
    sys.exit(main())
