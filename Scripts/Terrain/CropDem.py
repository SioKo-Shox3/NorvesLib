# 荒野（Valley of the Gods）の DEM から、手本で決めた範囲に余白を付けて切り出し、エンジンの座標の高さの格子を書く。
# 格子は float32 の生の配列（height.r32）と、座標の対応・高さの基準・処理の記録を持つ JSON（height.json）。
# 確かめるための陰影図（北西と南東から照らす 2 枚）と主役の崖を横切る断面の図も書く。
#
# 使い方:
#   python Scripts/Terrain/CropDem.py --build    格子・JSON・陰影図・断面の図を Assets/Terrain/ValleyOfTheGods/ に書く
#   python Scripts/Terrain/CropDem.py --verify   書いた格子を DEM から作り直して一致を確かめ、向き・高さの基準・余白・
#                                                NoData・前哨地の床・崖の急さ・格子の周期の凹凸を確かめる
# 入力は DemInput.py が落とした DEM（Assets/Terrain/Source/）。出力は git 管理外。
# 終了コード: すべて満たせば 0、どれかが欠ければ 1。
#
# 座標の約束（エンジンは右手系・Y 上。+Z を向いたとき画面の右は -X）:
#   X = 東 - 原点の東、Z = -(北 - 原点の北)、Y = NAVD88 の標高 - 高さのオフセット
#   東を +X にしたので北は -Z になる（北を向くと画面の右が東になり、地図と鏡に映した関係にならない）。
#   格子の頂点 (列 i, 行 j) は DEM の画素 (列, 行) の中心。行 0 が北端（Z が最小）、列 0 が西端（X が最小）。
import argparse
import collections
import datetime
import hashlib
import json
import math
import os
import subprocess
import sys

from DemInput import CROP_MARGIN, DEM_PATH, MANIFEST_PATH as SOURCE_MANIFEST_PATH
from ReferencePhotos import (CROP, FEATURES, OUTPOST_CANDIDATES, REPO_ROOT, label_font, sha256_of,
                             utm_from_latlon)

OUT_DIR = os.path.join(REPO_ROOT, "Assets", "Terrain", "ValleyOfTheGods")
RAW_PATH = os.path.join(OUT_DIR, "height.r32")
JSON_PATH = os.path.join(OUT_DIR, "height.json")
PREVIEW_DIR = os.path.join(OUT_DIR, "preview")
REFERENCE_MANIFEST_PATH = os.path.join(REPO_ROOT, "Assets", "Reference", "ValleyOfTheGods", "manifest.json")
DOC_PATH = os.path.join(REPO_ROOT, "Docs", "RenderingValidation", "WastelandDem.md")

# 格子の範囲（画素の外縁、UTM 12N の m）。手本の範囲に余白を付けた値
GRID = {
    "east_min": CROP["east_min"] - CROP_MARGIN["west"],
    "east_max": CROP["east_max"] + CROP_MARGIN["east"],
    "north_min": CROP["north_min"] - CROP_MARGIN["south"],
    "north_max": CROP["north_max"] + CROP_MARGIN["north"],
}
# 地域の原点（UTM 12N の m）。手本の範囲の中心に最も近い画素の中心にし、格子の頂点がエンジンの整数の座標に乗るようにする
ORIGIN = {"east": 604500.5, "north": 4127000.5}

# NoData の穴のうち、周りから埋めてよい大きさ（画素数）。これより大きい穴や格子の縁に触れる穴があれば範囲を変える
HOLE_FILL_MAX_PIXELS = 64
# 平らな所の異常: 周り 8 画素から求めた傾きがこの角度未満で、周り 8 画素の最大より上・最小より下へこの高さを越えて
# 飛び出した 1 画素だけを、周り 8 画素の中央値に置き換える（全体のならしはしない）
ANOMALY_FLAT_SLOPE_DEG = 10.0
ANOMALY_STEP_M = 0.5
# 前哨地の床の高さを求める半径[m]と、平らさを見る半径[m]・建物を置く範囲の半径[m]
OUTPOST_FLOOR_RADIUS = 30.0
OUTPOST_PLANE_RADIUS = 150.0
OUTPOST_SITE_RADIUS = 60.0
# 1 m の格子で前哨地に使える条件（半径 150 m の平面の傾き・平面からのずれの RMS、建物の範囲の平面からのずれの最大・
# 傾斜の 99 パーセンタイル）。建物の範囲は造成（SCN-WL-OUTPOST）で均すので、沢の切れ込みや段が 1.5 m を越えないことを見る。
# 傾斜は 1 m の差分だと礫や小さな起伏で 10°を越えるので、4 m 間隔の中心差分で測る
OUTPOST_MAX_TILT_DEG = 2.0
OUTPOST_MAX_RMS_M = 1.0
OUTPOST_MAX_SITE_RESIDUAL_M = 1.5
OUTPOST_MAX_SITE_SLOPE_DEG = 10.0
OUTPOST_SITE_SLOPE_STEP = 4

# 主役の崖（Battleship Rock）を横切る断面。南北の線は手本の文書で決めた線、東西の線は頂部を通る線
PROFILES = [
    {"id": "ns", "label": "南北 E 603100（南→北）", "start": (603100.5, 4127500.5), "end": (603100.5, 4128400.5)},
    {"id": "ew", "label": "東西 N 4127900（西→東）", "start": (602600.5, 4127900.5), "end": (603700.5, 4127900.5)},
]
# 主役の崖が丸まっていないことの目安。断面の 1 m 刻みの傾きの最大がこの角度を越える
CLIFF_MIN_MAX_SLOPE_DEG = 60.0
# 格子状の凹凸の目安。平らな所の 2 次差分の大きさを、列・行の番号を周期で割った余りごとに平均し、最大と最小の比がこれ未満
GRID_PATTERN_MAX_RATIO = 1.10

# 陰影図の光（方位は北から時計回り、仰角）
HILLSHADES = [
    {"id": "nw", "label": "北西から照らす（方位 315°・仰角 45°）", "azimuth": 315.0, "altitude": 45.0},
    {"id": "se", "label": "南東から照らす（方位 135°・仰角 45°）", "azimuth": 135.0, "altitude": 45.0},
]
# 手本の写真の撮影の向き（方位、度。WastelandReference.md の推定）。陰影図に矢印で描く
PHOTO_HEADINGS = {"far_01": 310.0, "far_02": 240.0, "cliff_01": 135.0, "cliff_02": 140.0, "ground_02": 145.0}
# 撮影位置の記録が無い cliff_01 は推定の範囲の中心に描く
PHOTO_ESTIMATED = {"cliff_01": (602225.0, 4128825.0)}


def grid_size():
    return GRID["east_max"] - GRID["east_min"], GRID["north_max"] - GRID["north_min"]


def utm_to_index(east, north):
    """UTM の座標を格子の (列, 行) の実数へ。頂点は画素の中心なので、整数が画素の中心に当たる"""
    return east - GRID["east_min"] - 0.5, GRID["north_max"] - north - 0.5


def utm_to_engine(east, north):
    return east - ORIGIN["east"], -(north - ORIGIN["north"])


def read_window(dataset):
    import numpy
    from rasterio.windows import from_bounds
    window = from_bounds(GRID["east_min"], GRID["north_min"], GRID["east_max"], GRID["north_max"],
                         dataset.transform).round_offsets().round_lengths()
    width, height = grid_size()
    if (window.width, window.height) != (width, height):
        raise RuntimeError("窓の大きさ %s が格子 %d x %d と違う" % (window, width, height))
    left, top = dataset.transform * (window.col_off, window.row_off)
    # DEM の変換の原点は 1e-5 m 程度の浮動小数の誤差を持つので、1 mm で比べる
    if abs(left - GRID["east_min"]) > 1e-3 or abs(top - GRID["north_max"]) > 1e-3:
        raise RuntimeError("窓の左上 (%r, %r) が格子の外縁と違う" % (left, top))
    return dataset.read(1, window=window).astype(numpy.float64)


def neighbor_stack(block):
    """各画素の周り 8 画素（端は端の値を延ばす）を (8, 行, 列) で返す"""
    import numpy
    pad = numpy.pad(block, 1, mode="edge")
    rows, cols = block.shape
    return numpy.stack([pad[1 + dy:1 + dy + rows, 1 + dx:1 + dx + cols]
                        for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dy, dx) != (0, 0)])


def fill_holes(heights, invalid):
    """孤立した小さな NoData の穴を周りの値から埋める。大きな穴・縁に触れる穴は例外にする（範囲を変える）"""
    import numpy
    rows, cols = heights.shape
    labels = numpy.zeros(heights.shape, dtype=numpy.int32)
    holes = []
    for start in map(tuple, numpy.argwhere(invalid)):
        if labels[start]:
            continue
        label = len(holes) + 1
        labels[start] = label
        queue = collections.deque([start])
        pixels = []
        while queue:
            r, c = queue.popleft()
            pixels.append((r, c))
            for dr in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    nr, nc = r + dr, c + dc
                    if 0 <= nr < rows and 0 <= nc < cols and invalid[nr, nc] and not labels[nr, nc]:
                        labels[nr, nc] = label
                        queue.append((nr, nc))
        holes.append(pixels)
    for pixels in holes:
        touches_edge = any(r in (0, rows - 1) or c in (0, cols - 1) for r, c in pixels)
        if len(pixels) > HOLE_FILL_MAX_PIXELS or touches_edge:
            r, c = pixels[0]
            raise RuntimeError("NoData の大きな穴（%d 画素、縁に触れる %s、行 %d・列 %d 付近）。範囲を変える" % (
                len(pixels), touches_edge, r, c))
    # 外側から 1 画素ずつ、有効な周りの値の平均で埋める
    remaining = invalid.copy()
    while remaining.any():
        filled_now = []
        for r, c in map(tuple, numpy.argwhere(remaining)):
            values = [heights[r + dr, c + dc] for dr in (-1, 0, 1) for dc in (-1, 0, 1)
                      if (dr or dc) and 0 <= r + dr < rows and 0 <= c + dc < cols and not remaining[r + dr, c + dc]]
            if values:
                filled_now.append((r, c, sum(values) / len(values)))
        for r, c, value in filled_now:
            heights[r, c] = value
            remaining[r, c] = False
    return [len(pixels) for pixels in holes]


def fix_flat_anomalies(heights):
    """平らな所で周りから飛び出した 1 画素だけを周りの中央値へ置き換え、置き換えた画素を返す"""
    import numpy
    fixes = []
    rows = heights.shape[0]
    strip = 512
    replacements = []
    for top in range(0, rows, strip):
        lo = max(0, top - 1)
        hi = min(rows, top + strip + 1)
        block = heights[lo:hi]
        neighbors = neighbor_stack(block)
        # 周り 8 画素だけから傾きを求める（Horn の差分。中心の画素の値を使わない）
        n = neighbors
        dzdx = ((n[2] + 2 * n[4] + n[7]) - (n[0] + 2 * n[3] + n[5])) / 8.0
        dzdy = ((n[5] + 2 * n[6] + n[7]) - (n[0] + 2 * n[1] + n[2])) / 8.0
        slope = numpy.degrees(numpy.arctan(numpy.hypot(dzdx, dzdy)))
        high = neighbors.max(axis=0)
        low = neighbors.min(axis=0)
        mask = (slope < ANOMALY_FLAT_SLOPE_DEG) & ((block > high + ANOMALY_STEP_M) | (block < low - ANOMALY_STEP_M))
        inner = slice(top - lo, top - lo + min(strip, rows - top))
        for r, c in map(tuple, numpy.argwhere(mask[inner])):
            median = float(numpy.median(neighbors[:, inner][:, r, c]))
            original = float(block[inner][r, c])
            replacements.append((top + int(r), int(c), original, median))
    for r, c, original, median in replacements:
        heights[r, c] = median
        fixes.append({"row": r, "col": c, "from": round(original, 3), "to": round(median, 3)})
    return fixes


def disc_values(heights, east, north, radius):
    """UTM の点の周り radius[m] の頂点の (東の差, 北の差, 高さ) を返す"""
    import numpy
    col, row = utm_to_index(east, north)
    r0, r1 = int(math.floor(row - radius)), int(math.ceil(row + radius)) + 1
    c0, c1 = int(math.floor(col - radius)), int(math.ceil(col + radius)) + 1
    block = heights[r0:r1, c0:c1]
    rr, cc = numpy.mgrid[r0:r1, c0:c1]
    de = cc - col
    dn = -(rr - row)
    inside = de * de + dn * dn <= radius * radius
    return de[inside], dn[inside], block[inside]


def evaluate_outpost(heights, candidate):
    import numpy
    de, dn, z = disc_values(heights, candidate["east"], candidate["north"], OUTPOST_PLANE_RADIUS)
    design = numpy.column_stack([de, dn, numpy.ones_like(de)])
    coeffs, *_ = numpy.linalg.lstsq(design, z, rcond=None)
    residual = z - design @ coeffs
    tilt = math.degrees(math.atan(math.hypot(coeffs[0], coeffs[1])))
    rms = float(numpy.sqrt(numpy.mean(residual ** 2)))
    # 建物を置く範囲の平面からのずれ
    de, dn, z = disc_values(heights, candidate["east"], candidate["north"], OUTPOST_SITE_RADIUS)
    design = numpy.column_stack([de, dn, numpy.ones_like(de)])
    site_coeffs, *_ = numpy.linalg.lstsq(design, z, rcond=None)
    site_residual = float(numpy.abs(z - design @ site_coeffs).max())
    # 建物を置く範囲の傾斜（4 m 間隔の中心差分）
    col, row = utm_to_index(candidate["east"], candidate["north"])
    s = OUTPOST_SITE_SLOPE_STEP
    r = int(OUTPOST_SITE_RADIUS) + s + 1
    r0, c0 = int(row) - r, int(col) - r
    block = heights[r0:r0 + 2 * r + 2, c0:c0 + 2 * r + 2]
    gx = (block[s:-s, 2 * s:] - block[s:-s, :-2 * s]) / (2.0 * s)
    gy = (block[2 * s:, s:-s] - block[:-2 * s, s:-s]) / (2.0 * s)
    slope = numpy.degrees(numpy.arctan(numpy.hypot(gx, gy)))
    rr, cc = numpy.mgrid[0:slope.shape[0], 0:slope.shape[1]]
    inside = (rr + r0 + s - row) ** 2 + (cc + c0 + s - col) ** 2 <= OUTPOST_SITE_RADIUS ** 2
    site_slope = float(numpy.percentile(slope[inside], 99))
    _, _, floor = disc_values(heights, candidate["east"], candidate["north"], OUTPOST_FLOOR_RADIUS)
    ok = (tilt <= OUTPOST_MAX_TILT_DEG and rms <= OUTPOST_MAX_RMS_M
          and site_residual <= OUTPOST_MAX_SITE_RESIDUAL_M and site_slope <= OUTPOST_MAX_SITE_SLOPE_DEG)
    return {
        "id": candidate["id"], "east": candidate["east"], "north": candidate["north"],
        "tilt_deg": round(tilt, 3),
        "rms_m": round(rms, 3),
        "max_residual_m": round(float(numpy.abs(residual).max()), 3),
        "site_max_residual_m": round(site_residual, 3),
        "site_slope_p99_deg": round(site_slope, 3),
        "floor_median_navd88_m": round(float(numpy.median(floor)), 3),
        "usable": bool(ok),
    }


def build_grid(dataset):
    """DEM の窓から、NoData を埋め・平らな所の異常を直し・オフセットを引いた float32 の格子と記録を作る"""
    import numpy
    heights = read_window(dataset)
    invalid = ~numpy.isfinite(heights)
    if dataset.nodata is not None:
        invalid |= heights == dataset.nodata
    nodata_count = int(invalid.sum())
    hole_sizes = fill_holes(heights, invalid) if nodata_count else []
    fixes = fix_flat_anomalies(heights)
    outposts = [evaluate_outpost(heights, candidate) for candidate in sorted(OUTPOST_CANDIDATES, key=lambda c: c["rank"])]
    usable = [entry for entry in outposts if entry["usable"]]
    if not usable:
        raise RuntimeError("1 m の格子で使える前哨地の候補が無い: %s" % outposts)
    chosen = usable[0]
    offset = round(chosen["floor_median_navd88_m"], 2)
    relative = (heights - offset).astype("<f4")
    record = {
        "nodata_value": dataset.nodata,
        "nodata_count": nodata_count,
        "hole_sizes": hole_sizes,
        "fixes": fixes,
        "outposts": outposts,
        "chosen": chosen,
        "offset": offset,
    }
    return relative, record


def raw_sha256(relative):
    return hashlib.sha256(relative.tobytes()).hexdigest()


def make_json(relative, record, source_manifest, input_sha):
    width, height = grid_size()
    x0, z0 = utm_to_engine(GRID["east_min"] + 0.5, GRID["north_max"] - 0.5)
    chosen = record["chosen"]
    outpost_x, outpost_z = utm_to_engine(chosen["east"], chosen["north"])
    return {
        "format": "float32 little-endian, row-major, no header",
        "file": os.path.basename(RAW_PATH),
        "sha256": raw_sha256(relative),
        "width": width,
        "height": height,
        "units": "m",
        "epsg": 26912,
        "crs": "NAD83 / UTM zone 12N",
        "vertical_datum": "NAVD88",
        "vertical_datum_note": "3DEP の 1 m DEM の標高は NAVD88（ファイルの CRS には垂直の基準が入っていない）",
        "pixel_size_m": 1.0,
        "vertex_spacing_m": 1.0,
        "sample_location": "pixel_center",
        "sample_location_note": "格子の頂点 (列 i, 行 j) は DEM の画素の中心（AREA_OR_POINT=Area の画素の中心）。"
                                "格子の外周は画素の外縁より半画素内側",
        "row_order": "north_to_south",
        "row_order_note": "行 0 が北端。行の番号が増えると南（エンジンの +Z）へ進む。列 0 が西端で、列の番号が増えると東（+X）",
        "axes": {"engine_x": "east", "engine_y": "up", "engine_z": "south", "handedness": "right"},
        "axes_note": "エンジンは右手系・Y 上。X = 東 - origin.east、Z = -(北 - origin.north)、Y = NAVD88 - height_offset_m。"
                     "北を向くと画面の右が東になる",
        "origin_utm": dict(ORIGIN),
        "origin_note": "手本の範囲（E 602500〜606500・N 4125000〜4129000）の中心に最も近い画素の中心",
        "first_vertex_engine": {"x": x0, "z": z0},
        "engine_bounds": {"x_min": x0, "x_max": x0 + (width - 1), "z_min": z0, "z_max": z0 + (height - 1)},
        "grid_bounds_utm_edges": dict(GRID),
        "core_crop_utm": dict(CROP),
        "margin_m": dict(CROP_MARGIN),
        "margin_note": "法線と解析のための余白。西は手本の写真 cliff_01 の推定の撮影位置、北は cliff_02 の撮影位置を DEM に乗せる",
        "height_offset_m": record["offset"],
        "height_offset_note": "前哨地の候補 %s（E %d・N %d）の半径 %d m の標高の中央値（NAVD88）。この床が Y = 0 付近になる" % (
            chosen["id"], chosen["east"], chosen["north"], OUTPOST_FLOOR_RADIUS),
        "outpost": dict(chosen, engine_x=outpost_x, engine_z=outpost_z),
        "outpost_candidates": record["outposts"],
        "height_range_m": {"min": float(relative.min()), "max": float(relative.max())},
        "nodata": {
            "source_value": record["nodata_value"],
            "count": record["nodata_count"],
            "filled_hole_sizes": record["hole_sizes"],
            "policy": "%d 画素以下の孤立した穴（格子の縁に触れない）だけ周りの値で外側から埋める。"
                      "それより大きい穴や縁に触れる穴があれば作らずに止まり、範囲を変える。0 m や負値として焼かない" % HOLE_FILL_MAX_PIXELS,
        },
        "corrections": {
            "count": len(record["fixes"]),
            "pixels": record["fixes"],
            "policy": "全体のならしはしない。周り 8 画素の傾きが %.0f°未満の平らな所で、周りの最大より上・最小より下へ %.1f m を越えて"
                      "飛び出した 1 画素だけを周り 8 画素の中央値に置き換える（行・列は格子の番号、値は NAVD88）" % (
                          ANOMALY_FLAT_SLOPE_DEG, ANOMALY_STEP_M),
        },
        "source": {
            "file": source_manifest.get("file"),
            "url": source_manifest.get("url"),
            "sha256": input_sha,
            "retrieved": source_manifest.get("retrieved"),
            "publication_date": source_manifest.get("publication_date"),
            "license": source_manifest.get("license"),
        },
        "generated": datetime.date.today().isoformat(),
        "generator": "Scripts/Terrain/CropDem.py",
    }


def hillshade(relative, azimuth, altitude):
    import numpy
    gy, gx = numpy.gradient(relative.astype(numpy.float32))
    dzde = gx
    dzdn = -gy
    az = math.radians(azimuth)
    alt = math.radians(altitude)
    light = (math.sin(az) * math.cos(alt), math.cos(az) * math.cos(alt), math.sin(alt))
    norm = numpy.sqrt(dzde * dzde + dzdn * dzdn + 1.0)
    shade = (-dzde * light[0] - dzdn * light[1] + light[2]) / norm
    return (numpy.clip(shade, 0.0, 1.0) * 255.0 + 0.5).astype(numpy.uint8)


def reference_points():
    """陰影図に描く点: 名前の付いた地形・前哨地の候補・手本の写真の撮影位置（UTM）"""
    features = []
    for name, feature in FEATURES.items():
        east, north = utm_from_latlon(feature["lat"], feature["lon"])
        features.append((name, east, north))
    cameras = []
    if os.path.isfile(REFERENCE_MANIFEST_PATH):
        with open(REFERENCE_MANIFEST_PATH, encoding="utf-8") as stream:
            manifest = json.load(stream)
        for photo in manifest["photos"]:
            camera = photo.get("camera", {})
            position = PHOTO_ESTIMATED.get(photo["id"])
            if camera.get("east") is not None:
                position = (camera["east"], camera["north"])
            if position and photo["id"] in PHOTO_HEADINGS:
                cameras.append((photo["id"], position[0], position[1], PHOTO_HEADINGS[photo["id"]]))
    return features, cameras


def draw_overview(shade, light, path):
    """陰影図を 1/4 に縮め、手本の範囲・地形・前哨地の候補・撮影位置と向き・断面の線を重ねる"""
    from PIL import Image, ImageDraw
    scale = 4
    image = Image.fromarray(shade, mode="L")
    image = image.resize((image.width // scale, image.height // scale), Image.LANCZOS).convert("RGB")
    draw = ImageDraw.Draw(image)
    font = label_font()

    def to_px(east, north):
        col, row = utm_to_index(east, north)
        return (col + 0.5) / scale, (row + 0.5) / scale

    x0, y0 = to_px(CROP["east_min"], CROP["north_max"])
    x1, y1 = to_px(CROP["east_max"], CROP["north_min"])
    draw.rectangle([x0, y0, x1, y1], outline=(255, 220, 60), width=2)
    for profile in PROFILES:
        draw.line([to_px(*profile["start"]), to_px(*profile["end"])], fill=(60, 230, 90), width=2)
    features, cameras = reference_points()
    for name, east, north in features:
        x, y = to_px(east, north)
        draw.ellipse([x - 4, y - 4, x + 4, y + 4], fill=(240, 60, 50))
        draw.text((x + 6, y - 8), name, fill=(255, 110, 100), font=font, stroke_width=2, stroke_fill=(0, 0, 0))
    for candidate in OUTPOST_CANDIDATES:
        x, y = to_px(candidate["east"], candidate["north"])
        draw.rectangle([x - 4, y - 4, x + 4, y + 4], outline=(60, 220, 255), width=2)
        draw.text((x + 6, y - 8), "前哨地 " + candidate["id"], fill=(80, 230, 255), font=font, stroke_width=2,
                  stroke_fill=(0, 0, 0))
    for photo_id, east, north, heading in cameras:
        x, y = to_px(east, north)
        h = math.radians(heading)
        tip = to_px(east + 350.0 * math.sin(h), north + 350.0 * math.cos(h))
        draw.line([(x, y), tip], fill=(240, 80, 240), width=2)
        draw.polygon([(x, y - 6), (x - 5, y + 4), (x + 5, y + 4)], fill=(240, 80, 240))
        draw.text((x + 6, y + 2), photo_id, fill=(250, 140, 250), font=font, stroke_width=2, stroke_fill=(0, 0, 0))
    # 方位と縮尺
    draw.rectangle([0, 0, image.width, 24], fill=(0, 0, 0))
    draw.text((6, 4), "%s  上が北・右が東  黄枠: 手本の範囲 4 km  緑: 断面  赤: 地形  水色: 前哨地の候補  紫: 写真の撮影位置と向き" % light,
              fill=(255, 255, 255), font=font)
    bar_y = image.height - 16
    draw.line([(10, bar_y), (10 + 1000 / scale, bar_y)], fill=(255, 255, 255), width=3)
    draw.text((14 + 1000 / scale, bar_y - 9), "1 km", fill=(255, 255, 255), font=font)
    image.save(path)


def draw_zoom(shades, east_min, north_max, size, magnify, title, path):
    """1 m の解像度の一部を、照らす向きごとに並べる（magnify 倍の最近傍で拡大）"""
    from PIL import Image, ImageDraw
    font = label_font()
    col0, row0 = utm_to_index(east_min + 0.5, north_max - 0.5)
    col0, row0 = int(round(col0)), int(round(row0))
    panels = []
    for shade in shades:
        block = shade[row0:row0 + size, col0:col0 + size]
        panel = Image.fromarray(block, mode="L").resize((size * magnify, size * magnify), Image.NEAREST)
        panels.append(panel.convert("RGB"))
    header = 26
    image = Image.new("RGB", (len(panels) * size * magnify + (len(panels) - 1) * 8, size * magnify + header), (0, 0, 0))
    draw = ImageDraw.Draw(image)
    for index, (panel, light) in enumerate(zip(panels, HILLSHADES)):
        x = index * (size * magnify + 8)
        image.paste(panel, (x, header))
        draw.text((x + 6, 4), "%s  %s" % (title, light["label"]), fill=(255, 255, 255), font=font)
    image.save(path)


def profile_samples(relative, profile):
    (e0, n0), (e1, n1) = profile["start"], profile["end"]
    length = int(round(math.hypot(e1 - e0, n1 - n0)))
    samples = []
    for step in range(length + 1):
        t = step / length
        col, row = utm_to_index(e0 + (e1 - e0) * t, n0 + (n1 - n0) * t)
        samples.append((float(step), float(relative[int(round(row)), int(round(col))])))
    return samples


def max_slope_deg(samples):
    return max(math.degrees(math.atan(abs(b[1] - a[1]) / (b[0] - a[0]))) for a, b in zip(samples, samples[1:]))


def draw_profiles(relative, path):
    """断面を縦横 1:1 で描き、最も急な 120 m は 1 m ごとの頂点を点で拡大して描く"""
    from PIL import Image, ImageDraw
    font = label_font()
    panels = []
    for profile in PROFILES:
        samples = profile_samples(relative, profile)
        heights = [h for _, h in samples]
        low = math.floor(min(heights) / 10.0) * 10.0 - 10.0
        high = math.ceil(max(heights) / 10.0) * 10.0 + 10.0
        steepest = max(range(len(samples) - 1), key=lambda i: abs(samples[i + 1][1] - samples[i][1]))
        detail_start = max(0, min(len(samples) - 121, steepest - 60))
        panels.append((profile, samples, low, high, detail_start))

    scale = 1.2
    detail_scale = 5.0
    margin = 50
    width = int(max(len(s) for _, s, _, _, _ in panels) * scale) + 2 * margin
    detail_width = int(120 * detail_scale) + 2 * margin
    heights_px = []
    for _, samples, low, high, start in panels:
        detail = samples[start:start + 121]
        d_low = min(h for _, h in detail) - 5.0
        d_high = max(h for _, h in detail) + 5.0
        heights_px.append((int((high - low) * scale) + 60, int((d_high - d_low) * detail_scale) + 60, d_low, d_high))
    total_h = sum(a + b for a, b, _, _ in heights_px) + 40
    image = Image.new("RGB", (max(width, detail_width), total_h), (250, 250, 250))
    draw = ImageDraw.Draw(image)
    draw.text((8, 8), "Battleship Rock の断面（縦横 1:1、高さはエンジンの Y = NAVD88 - オフセット、点は 1 m ごとの格子の頂点）",
              fill=(0, 0, 0), font=font)
    top = 40
    for (profile, samples, low, high, start), (h_main, h_detail, d_low, d_high) in zip(panels, heights_px):
        def plot(points, x_scale, y_scale, y_low, y_high, origin_y, distance0, dots):
            base = origin_y + 30 + (y_high - y_low) * y_scale
            for gy in range(int(math.ceil(y_low / 10.0)) * 10, int(y_high) + 1, 10):
                y = base - (gy - y_low) * y_scale
                draw.line([(margin, y), (margin + (points[-1][0] - distance0) * x_scale, y)],
                          fill=(215, 215, 215) if gy % 50 else (170, 170, 170))
                if gy % 50 == 0:
                    draw.text((4, y - 8), "%d" % gy, fill=(90, 90, 90), font=font)
            coords = [(margin + (d - distance0) * x_scale, base - (h - y_low) * y_scale) for d, h in points]
            draw.line(coords, fill=(150, 60, 20), width=2)
            if dots:
                for x, y in coords:
                    draw.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(20, 20, 160))
            return base

        draw.text((margin, top + 6), "%s  長さ %d m  傾きの最大 %.1f°（1 m 刻み）" % (
            profile["label"], len(samples) - 1, max_slope_deg(samples)), fill=(0, 0, 0), font=font)
        base = plot(samples, scale, scale, low, high, top, 0.0, False)
        d0 = samples[start][0]
        d1 = samples[min(start + 120, len(samples) - 1)][0]
        x_a = margin + d0 * scale
        x_b = margin + d1 * scale
        draw.rectangle([x_a, top + 30, x_b, base], outline=(60, 160, 60))
        top += h_main
        draw.text((margin, top + 6), "拡大（緑の枠、%d〜%d m、縦横 1:1）" % (d0, d1), fill=(0, 0, 0), font=font)
        plot(samples[start:start + 121], detail_scale, detail_scale, d_low, d_high, top, d0, True)
        top += h_detail
    image.save(path)


def grid_pattern_ratios(relative):
    """平らな所の 2 次差分の大きさを、列・行の番号の余りごとに平均した最大と最小の比（周期ごと）"""
    import numpy
    a = relative.astype(numpy.float64)
    pad = numpy.pad(a, 1, mode="edge")
    lap = numpy.abs(pad[1:-1, 2:] + pad[1:-1, :-2] + pad[2:, 1:-1] + pad[:-2, 1:-1] - 4.0 * a)
    gy, gx = numpy.gradient(a)
    flat = numpy.degrees(numpy.arctan(numpy.hypot(gx, gy))) < 3.0
    ratios = {}
    for period in (2, 3, 4, 5, 8, 16, 32):
        means = []
        for axis in (0, 1):
            for phase in range(period):
                if axis == 0:
                    values = lap[:, phase::period][flat[:, phase::period]]
                else:
                    values = lap[phase::period, :][flat[phase::period, :]]
                means.append(float(values.mean()))
            ratios["%s%d" % ("col" if axis == 0 else "row", period)] = max(means[-period:]) / min(means[-period:])
    return ratios


def build():
    import numpy
    import rasterio
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    with open(SOURCE_MANIFEST_PATH, encoding="utf-8") as stream:
        source_manifest = json.load(stream)
    input_sha = sha256_of(DEM_PATH)
    if input_sha != source_manifest.get("sha256"):
        raise RuntimeError("DEM の SHA-256 が manifest と違う")
    with rasterio.open(DEM_PATH) as dataset:
        relative, record = build_grid(dataset)
    with open(RAW_PATH, "wb") as stream:
        stream.write(relative.tobytes())
    document = make_json(relative, record, source_manifest, input_sha)
    with open(JSON_PATH, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(document, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print("格子: %s（%d x %d、%.1f MB）" % (RAW_PATH, document["width"], document["height"], os.path.getsize(RAW_PATH) / 1e6))
    print("JSON: %s" % JSON_PATH)
    print("前哨地: %s、高さのオフセット %.2f m、NoData %d、直した画素 %d" % (
        record["chosen"]["id"], record["offset"], record["nodata_count"], len(record["fixes"])))

    from PIL import Image
    shades = []
    for light in HILLSHADES:
        shade = hillshade(relative, light["azimuth"], light["altitude"])
        shades.append(shade)
        Image.fromarray(shade, mode="L").save(os.path.join(PREVIEW_DIR, "hillshade_%s_full.png" % light["id"]))
        draw_overview(shade, light["label"], os.path.join(PREVIEW_DIR, "hillshade_%s.png" % light["id"]))
    # 主役の崖の周り 1 km と、前哨地の周り 400 m（2 倍）を 1 m の解像度で
    draw_zoom(shades, 602600, 4128500, 1000, 1, "Battleship Rock の周り 1 km", os.path.join(PREVIEW_DIR, "zoom_battleship.png"))
    chosen = record["chosen"]
    draw_zoom(shades, chosen["east"] - 200, chosen["north"] + 200, 400, 2, "前哨地 %s の周り 400 m" % chosen["id"],
              os.path.join(PREVIEW_DIR, "zoom_outpost.png"))
    draw_profiles(relative, os.path.join(PREVIEW_DIR, "profile_battleship.png"))
    print("確かめる図: %s" % PREVIEW_DIR)
    return 0


def verify():
    import numpy
    import rasterio
    failures = []

    def check(condition, message):
        print(("OK   " if condition else "FAIL ") + message)
        if not condition:
            failures.append(message)

    for path in (DEM_PATH, SOURCE_MANIFEST_PATH, RAW_PATH, JSON_PATH):
        check(os.path.isfile(path), "ある: %s" % os.path.relpath(path, REPO_ROOT))
    if failures:
        print("result=fail (%d)" % len(failures))
        return 1

    with open(JSON_PATH, encoding="utf-8") as stream:
        document = json.load(stream)
    with open(SOURCE_MANIFEST_PATH, encoding="utf-8") as stream:
        source_manifest = json.load(stream)
    width, height = grid_size()

    # JSON の項目
    required = ("epsg", "origin_utm", "pixel_size_m", "row_order", "axes", "sample_location", "vertical_datum",
                "height_offset_m", "margin_m", "nodata", "source", "sha256", "width", "height", "first_vertex_engine")
    check(all(key in document for key in required), "JSON に %s がある" % "・".join(required))
    check(document.get("epsg") == 26912, "EPSG:26912（NAD83 / UTM 12N）")
    check(document.get("vertical_datum") == "NAVD88", "標高の基準が NAVD88")
    check(document.get("pixel_size_m") == 1.0 and document.get("vertex_spacing_m") == 1.0, "画素・頂点の間隔が 1 m")
    check(document.get("sample_location") == "pixel_center", "頂点が画素の中心")
    check(document.get("row_order") == "north_to_south", "行 0 が北端")
    check(document.get("axes") == {"engine_x": "east", "engine_y": "up", "engine_z": "south", "handedness": "right"},
          "東が +X・北が -Z（右手系・Y 上）")
    check((document.get("width"), document.get("height")) == (width, height), "寸法 %d x %d" % (width, height))
    check(all(document.get("margin_m", {}).get(side, 0) >= 100 for side in ("west", "east", "south", "north")),
          "余白が四方とも 100 m 以上（%s）" % document.get("margin_m"))
    x0, z0 = utm_to_engine(GRID["east_min"] + 0.5, GRID["north_max"] - 0.5)
    check(document.get("first_vertex_engine") == {"x": x0, "z": z0}, "最初の頂点のエンジンの座標 (%.1f, %.1f)" % (x0, z0))
    check(float(x0).is_integer() and float(z0).is_integer(), "頂点がエンジンの整数の座標に乗る")

    # 入力
    input_sha = sha256_of(DEM_PATH)
    check(input_sha == source_manifest.get("sha256") == document["source"].get("sha256"),
          "入力の SHA-256 が DEM・Source の manifest・JSON で一致（%s）" % input_sha)

    # 生の格子
    size = os.path.getsize(RAW_PATH)
    check(size == width * height * 4, "height.r32 の大きさ %d = %d x %d x 4" % (size, width, height))
    relative = numpy.fromfile(RAW_PATH, dtype="<f4").reshape(height, width)
    check(raw_sha256(relative) == document.get("sha256"), "height.r32 の SHA-256 が JSON と一致")
    check(bool(numpy.isfinite(relative).all()), "格子に NaN・無限大が無い")
    low, high = float(relative.min()), float(relative.max())
    print("高さの範囲（エンジンの Y）: %.3f〜%.3f m、NAVD88 では %.3f〜%.3f m" % (
        low, high, low + document["height_offset_m"], high + document["height_offset_m"]))
    check(document["height_range_m"] == {"min": low, "max": high}, "高さの範囲が JSON と一致")
    check(-100.0 < low and high < 300.0, "高さが前哨地の床から -100〜300 m の内側（0 m や大きな負値の焼き込みが無い）")

    # DEM から作り直して同じになるか（処理が決まった手順で、生の格子が DEM の値そのものか）
    with rasterio.open(DEM_PATH) as dataset:
        rebuilt, record = build_grid(dataset)
        check(raw_sha256(rebuilt) == document["sha256"], "DEM から作り直した格子が height.r32 とビット単位で一致")
        print("NoData: %d 画素、埋めた穴 %s、直した画素 %d" % (
            record["nodata_count"], record["hole_sizes"], len(record["fixes"])))
        check(document["nodata"]["count"] == record["nodata_count"], "NoData の数が JSON と一致")
        check(all(s <= HOLE_FILL_MAX_PIXELS for s in record["hole_sizes"]), "埋めた穴がすべて小さい")
        check(len(record["fixes"]) <= 100, "平らな所で直した画素が少数（%d、全体のならしをしていない）" % len(record["fixes"]))
        for fix in record["fixes"]:
            print("  直した画素: 行 %d・列 %d、%.3f → %.3f m" % (fix["row"], fix["col"], fix["from"], fix["to"]))
        fixed = {(fix["row"], fix["col"]) for fix in record["fixes"]}

        # 地形と撮影位置の点で、DEM を UTM で直接引いた値と格子の値が一致するか（向きの反転・ずれが無いか）
        features, cameras = reference_points()
        points = [(name, east, north) for name, east, north in features]
        points += [(photo_id, east, north) for photo_id, east, north, _ in cameras]
        points += [("前哨地 " + c["id"], c["east"], c["north"]) for c in OUTPOST_CANDIDATES]
        mismatch = 0
        for name, east, north in points:
            col, row = utm_to_index(east, north)
            col, row = int(round(col)), int(round(row))
            center_e, center_n = GRID["east_min"] + col + 0.5, GRID["north_max"] - row - 0.5
            value = next(dataset.sample([(center_e, center_n)]))[0]
            grid_value = float(relative[row, col]) + document["height_offset_m"]
            x, z = utm_to_engine(center_e, center_n)
            same = (row, col) in fixed or abs(grid_value - float(value)) < 1e-3
            mismatch += 0 if same else 1
            print("  %-18s E %.1f・N %.1f → X %7.1f・Z %7.1f、DEM %.3f m、格子 %.3f m" % (
                name, center_e, center_n, x, z, value, grid_value))
        check(mismatch == 0, "%d 点で DEM を UTM で引いた値と格子の値が一致" % len(points))

    # 向き: Battleship Rock は原点の北西なので X < 0・Z < 0 にあり、周りより 100 m 以上高い
    battleship = next((e, n) for name, e, n in reference_points()[0] if name == "Battleship Rock")
    _, _, top = disc_values(relative, battleship[0], battleship[1], 200.0)
    _, _, ring = disc_values(relative, battleship[0], battleship[1], 700.0)
    relief = float(top.max()) - float(numpy.percentile(ring, 20))
    bx, bz = utm_to_engine(*battleship)
    print("Battleship Rock: X %.0f・Z %.0f、頂部と周り 700 m の低い側 20%% の差 %.1f m" % (bx, bz, relief))
    check(bx < 0 and bz < 0, "Battleship Rock（原点の北西）が X < 0・Z < 0")
    check(relief > 100.0, "Battleship Rock の比高が 100 m を越える（%.1f m）" % relief)

    # 前哨地の床
    chosen = document["outpost"]
    _, _, floor = disc_values(relative, chosen["east"], chosen["north"], OUTPOST_FLOOR_RADIUS)
    floor_median = float(numpy.median(floor))
    for entry in document["outpost_candidates"]:
        print("  前哨地 %s: 半径 %d m の平面の傾き %.2f°・ずれ RMS %.2f m・最大 %.2f m、半径 %d m の平面からのずれの最大 %.2f m・"
              "4 m 間隔の傾斜の 99%% %.2f°、使える %s" % (
                  entry["id"], OUTPOST_PLANE_RADIUS, entry["tilt_deg"], entry["rms_m"], entry["max_residual_m"],
                  OUTPOST_SITE_RADIUS, entry["site_max_residual_m"], entry["site_slope_p99_deg"], entry["usable"]))
    print("前哨地 %s の床（半径 %d m の中央値）: Y %.3f m、高さのオフセット %.2f m" % (
        chosen["id"], OUTPOST_FLOOR_RADIUS, floor_median, document["height_offset_m"]))
    check(abs(floor_median) < 0.5, "前哨地の床が Y = 0 付近（|%.3f| < 0.5 m）" % floor_median)
    check(chosen["usable"], "前哨地 %s が 1 m の格子で平ら（傾き %.1f°以下・ずれ RMS %.1f m 以下・建物の範囲のずれ %.1f m 以下・"
          "傾斜の 99%% %.0f°以下）" % (chosen["id"], OUTPOST_MAX_TILT_DEG, OUTPOST_MAX_RMS_M,
                                     OUTPOST_MAX_SITE_RESIDUAL_M, OUTPOST_MAX_SITE_SLOPE_DEG))

    # 崖が丸まっていないか（断面の 1 m 刻みの傾きの最大）
    for profile in PROFILES:
        samples = profile_samples(relative, profile)
        steepest = max_slope_deg(samples)
        check(steepest > CLIFF_MIN_MAX_SLOPE_DEG, "断面 %s の傾きの最大 %.1f°が %.0f°を越える（崖が丸まっていない）" % (
            profile["label"], steepest, CLIFF_MIN_MAX_SLOPE_DEG))
    gy, gx = numpy.gradient(relative.astype(numpy.float32))
    steep = float((numpy.degrees(numpy.arctan(numpy.hypot(gx, gy))) > 60.0).mean())
    print("傾斜 60°超の面積: %.2f %%" % (100.0 * steep))

    # 格子状の凹凸
    ratios = grid_pattern_ratios(relative)
    print("平らな所の 2 次差分の周期ごとの最大/最小: %s" % "、".join("%s %.3f" % item for item in ratios.items()))
    check(max(ratios.values()) < GRID_PATTERN_MAX_RATIO, "格子状の凹凸が無い（最大/最小 %.3f < %.2f）" % (
        max(ratios.values()), GRID_PATTERN_MAX_RATIO))

    # 図
    figures = ["hillshade_%s.png" % light["id"] for light in HILLSHADES]
    figures += ["hillshade_%s_full.png" % light["id"] for light in HILLSHADES]
    figures += ["zoom_battleship.png", "zoom_outpost.png", "profile_battleship.png"]
    raw_time = os.path.getmtime(RAW_PATH)
    for name in figures:
        path = os.path.join(PREVIEW_DIR, name)
        check(os.path.isfile(path) and os.path.getmtime(path) >= raw_time, "図がある（格子より新しい）: preview/%s" % name)

    # git 管理外と文書
    ignored = all(subprocess.run(["git", "check-ignore", "-q", path], cwd=REPO_ROOT).returncode == 0
                  for path in (RAW_PATH, JSON_PATH))
    check(ignored, "Assets/Terrain/ValleyOfTheGods/ が git の無視対象")
    doc_ok = False
    if os.path.isfile(DOC_PATH):
        with open(DOC_PATH, encoding="utf-8-sig") as stream:
            text = stream.read()
        doc_ok = ("%.2f" % document["height_offset_m"]) in text and document["sha256"] in text
    check(doc_ok, "%s に高さのオフセットと格子の SHA-256 が書いてある" % os.path.relpath(DOC_PATH, REPO_ROOT))

    print("result=%s" % ("pass" if not failures else "fail (%d)" % len(failures)))
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser(description="荒野の DEM を余白付きで切り出し、エンジンの座標の高さの格子を書く")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--build", action="store_true", help="格子・JSON・陰影図・断面の図を書く")
    group.add_argument("--verify", action="store_true", help="格子を作り直して一致と向き・高さ・形を確かめる")
    args = parser.parse_args()
    # 出力をファイルへ保存して読むので、端末の文字コードに依らず UTF-8 で書く
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if args.build:
        return build()
    return verify()


if __name__ == "__main__":
    sys.exit(main())
