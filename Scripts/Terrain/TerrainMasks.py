# 荒野の DEM の格子（CropDem.py が書く height.json と height.r32）から、地表の分類の重みの地図を焼く。
# 分類は露岩・崖錐・地表（細粒）・地表（礫）の 4 つで、1 画素に 4 つの重み（合計 255）を RGBA8 の PNG に入れる。
#
# 使い方:
#   python Scripts/Terrain/TerrainMasks.py --build    地図・記述・色分けの図を Assets/Terrain/ValleyOfTheGods/ に書く
#   python Scripts/Terrain/TerrainMasks.py --verify   地図を作り直して書いたものと一致するか、崖に露岩・崖の下の裾に崖錐・
#                                                     平らな所に地表が出るか、崖錐が距離だけで決まっていないかを確かめる
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
# 地層の高さは地図に焼かない（シェーダーで共通の基準面からの高さとして計算する）。流れ（沢）は別の項目で足す。
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
PREVIEWS = ["surface_classes.png", "surface_battleship.png", "surface_franklin.png"]

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

# 地表の礫の割合: 傾斜[度]・半径 50 m の相対の高さ[m]の範囲
GRAVEL_BASE = 0.25
GRAVEL_SLOPE_DEG = (3.0, 15.0)
GRAVEL_TPI_M = (0.5, 3.0)

# 色分けの図の色（露岩・崖錐・細粒・礫）
CLASS_NAMES = ["露岩", "崖錐", "地表（細粒）", "地表（礫）"]
CLASS_COLORS = numpy.array([[205, 55, 45], [250, 175, 40], [150, 215, 240], [70, 165, 95]], dtype=numpy.float32)

# 確かめるときの基準
CHECK_STEEP_SLOPE_DEG = 55.0
CHECK_STEEP_RELIEF_M = 10.0
CHECK_STEEP_ROCK_SHARE = 0.90
CHECK_FLAT_SLOPE_DEG = 5.0
CHECK_FLAT_GROUND_SHARE = 0.98
CHECK_TALUS_SLOPE_DEG = (15.0, 38.0)
CHECK_NEAR_CLIFF_M = 150.0
CHECK_DISTANCE_ONLY_MAX = 0.6
CHECK_COVERAGE = {"露岩": (0.005, 0.15), "崖錐": (0.01, 0.20), "地表（細粒）": (0.10, 0.90), "地表（礫）": (0.10, 0.90)}
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


def compute(heights):
    """格子から重み（4, 行, 列。合計 1）と、確かめに使う途中の値を求める"""
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
    fields = {"smooth": smooth, "slope_deg": slope_deg, "tpi_small": tpi_small, "tpi_mid": tpi_mid, "relief": relief,
              "steep_rock": steep_rock, "deposit": deposit, "reachable": reachable, "energy": energy,
              "iterations": iterations}
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


def descriptor_for(grid, grid_sha, rows, columns, stats, iterations):
    first_x = float(grid["first_vertex_engine"]["x"])
    first_z = float(grid["first_vertex_engine"]["z"])
    spacing = float(grid["vertex_spacing_m"])
    # 格子の頂点は DEM の画素の中心なので、地図の外縁は最初の頂点から半画素外
    edge_x = first_x - 0.5 * spacing
    edge_z = first_z - 0.5 * spacing
    return {
        "format": "PNG RGBA8, row 0 = north (engine z min), column 0 = west (engine x min)",
        "file": os.path.basename(MAP_PATH),
        "channels": {"r": "rock", "g": "talus", "b": "ground_fine", "a": "ground_gravel"},
        "channels_note": "画素ごとの 4 つの重みの合計は 255。r 露岩・g 崖錐・b 地表（細粒）・a 地表（礫）",
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
        },
        "flow_iterations": iterations,
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
        crop.save(os.path.join(PREVIEW_DIR, file_name))


def build():
    grid, heights, grid_sha = read_grid()
    weights, fields = compute(heights)
    quantized = quantize(weights)
    stats = coverage(quantized)
    descriptor = descriptor_for(grid, grid_sha, quantized.shape[1], quantized.shape[2], stats, fields["iterations"])
    from PIL import Image
    Image.fromarray(numpy.ascontiguousarray(quantized.transpose(1, 2, 0)), mode="RGBA").save(MAP_PATH)
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
    weights, fields = compute(heights)
    quantized = quantize(weights)
    rebuilt = numpy.ascontiguousarray(quantized.transpose(1, 2, 0))
    check(written.shape == rebuilt.shape and numpy.array_equal(written, rebuilt),
          "格子から作り直した地図が書いた地図とビット単位で一致する（流れの反復 %d 回）" % fields["iterations"])
    sums = written.astype(numpy.int32).sum(axis=2)
    check(bool((sums == 255).all()), "画素ごとの重みの合計が 255（最小 %d・最大 %d）" % (sums.min(), sums.max()))
    check(fields["iterations"] < FLOW_MAX_ITERATIONS, "崖錐の流れが反復の上限の前に落ち着いた（%d 回 < %d）" % (
        fields["iterations"], FLOW_MAX_ITERATIONS))

    dominant = quantized.argmax(axis=0)
    slope_deg = fields["slope_deg"]
    stats = coverage(quantized)
    print("主な分類の面積の割合: %s" % "、".join("%s %.2f %%" % (k, 100.0 * v) for k, v in stats.items()))
    for name, (low, high) in CHECK_COVERAGE.items():
        check(low <= stats[name] <= high, "%s の面積の割合 %.2f %% が %.1f〜%.0f %%" % (name, 100.0 * stats[name],
                                                                                 100.0 * low, 100.0 * high))

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
