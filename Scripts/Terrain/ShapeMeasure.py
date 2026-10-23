# 崖の棚・角・外形を、写真の注釈（ShapeAnnotations.json）とエンジンの深度と幾何の法線で測る（CompareReference.py --shape。
# 単独では動かさない）。記録は Docs/RenderingValidation/WastelandShape.md の「形の物差し」。
#
#   python Scripts/Terrain/CompareReference.py --capture <-Aov の撮影> --photos far_01,... --shape [--expect <仕様>] [--verify]
#
# 撮影の側は <視点>.aov.depth.pfm と <視点>.aov.geometric_normal.pfm（可視バッファの解決が書く三角形の面の法線。ShapeCause.py と
# 同じ詰め方）だけを読み、輝度は使わない（材質の縞・影・露出で段が作れ、照明の向きで正しい棚にも陰が出ないため）。
# 折れの画素: 向き u（横・縦・2 つの斜め）に kFoldStepPx 画素ずつ前後の 2 点で、幾何の法線の角が kFoldDegrees 以上か、
#   深度の相対の差が kDepthStepRatio 以上か、片方だけが空（外形）。その向きだけで折れ、直交する向きでは折れない画素を
#   「u を横切る折れ」とし、さらに折れの線の向きに kLineMinPx 画素以上続く画素だけを残す（縦の溝は横を横切る折れだけになり
#   横の棚の判定に入らない。崖錐の岩の小さな折れも外す）。
# 写真と撮影の対応: 崖ごとに、写真の外形（多角形の列ごとの上の縁）と撮影の外形（列ごとに深度 depth_max_m 以下の最も上の画素）の
#   距離の中央値が最小になる平行移動を ±kSearchFraction（注釈の search_fraction で崖ごとに広げられる）の内で探し、棚と角の注釈をその分ずらして撮影に置く。
# 量（距離は撮影の 1920 幅の画素に直す）:
#   shelf_match: 棚の線分のうち、線分に垂直に ±kMatchRadiusPx の内に棚の向きを横切る折れがある長さの割合
#   corner_match: 角の点のうち、半径 kMatchRadiusPx の内に直交する 2 つの向きの折れ（横と縦、または 2 つの斜め）が
#     それぞれ kCornerMinPixels 画素以上ある点の割合
#   outline_distance_median_px: 写真の外形の点から撮影の外形への距離の中央値（ずらす前）。
#   outline_residual_median_px（ずらした後）・shift_px（ずらした量）も書く
# 写真ごとの値は崖をまとめた値（棚は長さ、角は点の数、外形は点の数で重み付け）。崖ごとの値は cliffs.<名前>。
# 出力: <出力先>/shape_<写真>.png（上: 写真と注釈、下: 撮影と折れ〔赤〕・撮影の外形〔水色〕・ずらした注釈〔緑 = 一致、
#   橙 = 不一致〕）、shape_metrics.json。
# 落ちる: 注釈が無い・崖に外形・棚・角が無い・撮影の PNG・深度・幾何の法線が無いか 1920×1080 未満・撮影の外形が取れない・
#   崖の外形の中で幾何の法線の読める画素が kGeometricCoverage 未満・--verify で所見の文書に「形の物差し」の節が無い。
import json
import os

import numpy as np
from PIL import Image, ImageDraw

from CompareLight import load_font
from CompareReference import MIN_CAPTURE_SIZE, PANEL_SIZE, PHOTO_DIR, PHOTOS, REPO_ROOT, fit_photo
from ReferenceCheck import read_pfm_any
from ShapeCause import decode_geometric_normal

ANNOTATIONS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ShapeAnnotations.json")
DOC = os.path.join("Docs", "RenderingValidation", "WastelandShape.md")
DOC_HEADING = "## 形の物差し"
REPORT = "shape_metrics.json"
# 画素の大きさの基準の幅（撮影がこれと違えば距離をこの幅に直す）
kReferenceWidth = 1920
kFoldDegrees = 30.0
kFoldStepPx = 3
kDepthStepRatio = 0.05
# 写真と撮影の位置の許容[1920 幅の画素]。今の撮影（SCN-WL-SHAPE-CAUSE の lod-default）で、崖ごとにずらした後の外形の距離の
# 距離の中央値（全部の崖の点をまとめて 7.1 画素）を切り上げて決めた（WastelandShape.md の「形の物差し」）
kMatchRadiusPx = 8
# 平行移動を探す範囲（幅・高さに対する割合）。構図の違いが大きい崖は注釈の search_fraction で広げる
kSearchFraction = (0.06, 0.08)
kCornerMinPixels = 3
# 折れの線として数える長さの下限[画素]
kLineMinPx = 10
kSampleStepPx = 2
kGeometricCoverage = 0.95
DIRECTIONS = {"x": (1, 0), "y": (0, 1), "d1": (1, 1), "d2": (1, -1)}
ORTHOGONAL = {"x": "y", "y": "x", "d1": "d2", "d2": "d1"}
# --photos を省いたときに測る写真（注釈はこの全部に要る）
SHAPE_PHOTOS = ("far_01", "far_02", "cliff_01", "cliff_02", "ground_02")


def fmt(value, digits=3):
    return "none" if value is None else f"{value:.{digits}f}"


def fmt_display(value, digits=2):
    """重ね画像の文字（人が読む）。値が無いときは「－」"""
    return "－" if value is None else f"{value:.{digits}f}"


def load_annotations():
    with open(ANNOTATIONS_PATH, encoding="utf-8-sig") as file:
        return json.load(file)["photos"]


def shifted(array, dx, dy, fill):
    """array[y + dy, x + dx]（範囲の外は fill）"""
    height, width = array.shape[:2]
    result = np.full_like(array, fill)
    ys = slice(max(0, -dy), min(height, height - dy))
    xs = slice(max(0, -dx), min(width, width - dx))
    yt = slice(max(0, dy), min(height, height + dy))
    xt = slice(max(0, dx), min(width, width + dx))
    result[ys, xs] = array[yt, xt]
    return result


def fold_maps(depth, normal, readable):
    """向きごとに「その向きだけで横切る折れ」の画素"""
    terrain = np.isfinite(depth) & (depth > 0.0)
    safe = np.where(terrain, depth, 0.0)
    cos_limit = np.cos(np.radians(kFoldDegrees))
    raw = {}
    for name, (ux, uy) in DIRECTIONS.items():
        dx, dy = ux * kFoldStepPx, uy * kFoldStepPx
        terrain_a, terrain_b = shifted(terrain, dx, dy, False), shifted(terrain, -dx, -dy, False)
        depth_a, depth_b = shifted(safe, dx, dy, 0.0), shifted(safe, -dx, -dy, 0.0)
        read_a, read_b = shifted(readable, dx, dy, False), shifted(readable, -dx, -dy, False)
        dot = np.sum(shifted(normal, dx, dy, 0.0) * shifted(normal, -dx, -dy, 0.0), axis=2)
        both = terrain_a & terrain_b
        bend = both & read_a & read_b & (dot <= cos_limit)
        step = both & depth_step(terrain, safe, dx, dy, depth_a, depth_b)
        silhouette = terrain_a ^ terrain_b
        raw[name] = bend | step | silhouette
    only = {name: raw[name] & ~raw[ORTHOGONAL[name]] for name in DIRECTIONS}
    return {name: line_filter(only[name], DIRECTIONS[name], kLineMinPx) for name in DIRECTIONS}


def depth_step(terrain, safe, dx, dy, depth_a, depth_b):
    """前後の 2 点 a・b の間の深度の段。連続した面の勾配（斜めに見た平面の深度の変化）は段にしない:
    外側の 2 点（a の先・b の先）から深度の逆数を直線で延ばして相手の点を予測し、どちらの側からの予測とも
    kDepthStepRatio 以上外れるときだけ段とする（平面の深度の逆数は画面の座標でほぼ一次なので、平面では外れない。
    折れも外れるが、折れは面の法線でも数える）。外側の点が地形でない側は使わず、両側とも使えないときは a・b の差だけで決める"""
    inverse_a = 1.0 / np.maximum(depth_a, 1e-6)
    inverse_b = 1.0 / np.maximum(depth_b, 1e-6)
    limit = kDepthStepRatio * np.minimum(inverse_a, inverse_b)
    outer_a = shifted(terrain, 2 * dx, 2 * dy, False)
    outer_b = shifted(terrain, -2 * dx, -2 * dy, False)
    inverse_outer_a = 1.0 / np.maximum(shifted(safe, 2 * dx, 2 * dy, 0.0), 1e-6)
    inverse_outer_b = 1.0 / np.maximum(shifted(safe, -2 * dx, -2 * dy, 0.0), 1e-6)
    # a と b の間は外側の間隔の 2 倍
    miss_from_a = np.abs(inverse_b - (inverse_a + 2.0 * (inverse_a - inverse_outer_a))) > limit
    miss_from_b = np.abs(inverse_a - (inverse_b + 2.0 * (inverse_b - inverse_outer_b))) > limit
    plain = np.abs(depth_a - depth_b) > kDepthStepRatio * np.maximum(np.minimum(depth_a, depth_b), 1e-6)
    return np.where(outer_a & outer_b, miss_from_a & miss_from_b,
                    np.where(outer_a, miss_from_a, np.where(outer_b, miss_from_b, plain)))


def line_filter(mask, crossing, length):
    """折れの画素のうち、横切る向きに直交する向き（折れの線の向き）に length 画素以上続く線に乗る画素だけを残す
    （縦の溝が棚の線分を 1 点で横切る・崖錐の岩の小さな折れを外す）。線の揺れを 1 画素まで許す"""
    cx, cy = crossing
    lx, ly = -cy, cx
    wide = mask | shifted(mask, cx, cy, False) | shifted(mask, -cx, -cy, False)
    runs = wide.copy()
    for k in range(1, length):
        runs &= shifted(wide, k * lx, k * ly, False)
    belongs = runs.copy()
    for k in range(1, length):
        belongs |= shifted(runs, -k * lx, -k * ly, False)
    return mask & belongs


def to_pixels(points, size, shift=(0.0, 0.0)):
    width, height = size
    return [(x * width + shift[0], y * height + shift[1]) for x, y in points]


def polygon_top(points, size):
    """多角形の列ごとの上の縁（列 → 行）"""
    image = Image.new("L", size, 0)
    ImageDraw.Draw(image).polygon(points, fill=255)
    mask = np.asarray(image) > 0
    columns = np.nonzero(mask.any(axis=0))[0]
    return columns, mask[:, columns].argmax(axis=0), mask


def curve_points(columns, tops):
    """列ごとの上の縁を、縦の段を埋めた点の並び（N × 2、x・y）にする"""
    points = []
    for index, (x, y) in enumerate(zip(columns, tops)):
        points.append((x, y))
        if index + 1 < len(columns) and columns[index + 1] == x + 1:
            following = tops[index + 1]
            for fill in range(min(y, following) + 1, max(y, following)):
                points.append((x if abs(fill - y) < abs(fill - following) else x + 1, fill))
    return np.asarray(points, dtype=np.float64)


def nearest_distances(source, target):
    result = np.empty(len(source))
    for start in range(0, len(source), 256):
        chunk = source[start:start + 256]
        result[start:start + 256] = np.sqrt(((chunk[:, None, :] - target[None, :, :]) ** 2).sum(axis=2).min(axis=1))
    return result


def best_shift(photo_points, capture_points, size, search):
    """外形の距離の中央値が最小になる平行移動（画素）と、その中央値"""
    limits = (int(search[0] * size[0]), int(search[1] * size[1]))
    coarse_source = photo_points[:: max(1, len(photo_points) // 80)]
    coarse_target = capture_points[:: max(1, len(capture_points) // 800)]
    best = (np.inf, 0, 0)
    for dy in range(-limits[1], limits[1] + 1, 12):
        for dx in range(-limits[0], limits[0] + 1, 12):
            value = np.median(nearest_distances(coarse_source + (dx, dy), coarse_target))
            if value < best[0]:
                best = (value, dx, dy)
    fine_source = photo_points[:: max(1, len(photo_points) // 400)]
    for step, radius in ((2, 12), (1, 2)):
        _, cx, cy = best
        best = (np.inf, cx, cy)
        for dy in range(cy - radius, cy + radius + 1, step):
            for dx in range(cx - radius, cx + radius + 1, step):
                value = np.median(nearest_distances(fine_source + (dx, dy), capture_points))
                if value < best[0]:
                    best = (value, dx, dy)
    return (best[1], best[2]), float(np.median(nearest_distances(photo_points + best[1:], capture_points)))


def match_segment(maps, start, end, radius):
    """棚の線分の標本ごとの一致（線分に垂直に ±radius の内に、棚の向きを横切る折れがあるか）"""
    start, end = np.asarray(start), np.asarray(end)
    length = float(np.linalg.norm(end - start))
    count = max(2, int(length / kSampleStepPx) + 1)
    samples = start[None, :] + (end - start)[None, :] * np.linspace(0.0, 1.0, count)[:, None]
    direction = (end - start) / max(length, 1e-6)
    normal = np.array([-direction[1], direction[0]])
    angle = np.degrees(np.arctan2(normal[1], normal[0])) % 180.0
    # 垂直の向きに近い折れの図（横 0°・斜め 45°・縦 90°・斜め 135°）
    name = ("x", "d1", "y", "d2")[int(((angle + 22.5) % 180.0) // 45.0)]
    fold = maps[name]
    height, width = fold.shape
    offsets = np.arange(-radius, radius + 1)
    points = samples[:, None, :] + offsets[None, :, None] * normal[None, None, :]
    xs = np.rint(points[..., 0]).astype(np.int64)
    ys = np.rint(points[..., 1]).astype(np.int64)
    inside = (xs >= 0) & (xs < width) & (ys >= 0) & (ys < height)
    hit = np.zeros(xs.shape, dtype=bool)
    hit[inside] = fold[ys[inside], xs[inside]]
    return samples, hit.any(axis=1), length


def match_corner(maps, point, radius):
    height, width = maps["x"].shape
    x0, x1 = int(max(0, point[0] - radius)), int(min(width, point[0] + radius + 1))
    y0, y1 = int(max(0, point[1] - radius)), int(min(height, point[1] + radius + 1))
    if x0 >= x1 or y0 >= y1:
        return False
    yy, xx = np.mgrid[y0:y1, x0:x1]
    disk = (xx - point[0]) ** 2 + (yy - point[1]) ** 2 <= radius * radius
    counts = {name: int(np.count_nonzero(maps[name][y0:y1, x0:x1] & disk)) for name in DIRECTIONS}
    return (counts["x"] >= kCornerMinPixels and counts["y"] >= kCornerMinPixels) or (
        counts["d1"] >= kCornerMinPixels and counts["d2"] >= kCornerMinPixels)


def capture_outline(depth, depth_max, columns_range):
    """撮影の外形（列ごとに深度 depth_max 以下の最も上の画素）の点の並び"""
    height, width = depth.shape
    c0, c1 = max(0, columns_range[0]), min(width, columns_range[1])
    near = np.isfinite(depth[:, c0:c1]) & (depth[:, c0:c1] > 0.0) & (depth[:, c0:c1] <= depth_max)
    present = near.any(axis=0)
    columns = np.arange(c0, c1)[present]
    tops = near[:, present].argmax(axis=0)
    return columns, tops


def measure_cliff(cliff, depth, maps, readable, size, scale):
    """崖ひとつの記録と描画の材料。失敗なら (None, 理由)"""
    width, height = size
    outline = to_pixels(cliff["outline"], size)
    photo_columns, photo_tops, _ = polygon_top(outline, size)
    if len(photo_columns) == 0:
        return None, "外形の多角形が空"
    photo_points = curve_points(photo_columns, photo_tops)
    search_fraction = tuple(cliff.get("search_fraction", kSearchFraction))
    search = int(search_fraction[0] * width)
    columns, tops = capture_outline(depth, float(cliff["depth_max_m"]), (photo_columns[0] - search, photo_columns[-1] + search + 1))
    if len(columns) < 2:
        return None, f"撮影の外形が取れない（深度 {cliff['depth_max_m']} m 以下の画素が無い）"
    capture_points = curve_points(columns, tops)
    raw = float(np.median(nearest_distances(photo_points, capture_points)))
    shift, residual = best_shift(photo_points, capture_points, size, search_fraction)
    radius = int(np.ceil(kMatchRadiusPx / scale))
    shelves = []
    matched_length = total_length = 0.0
    for start, end in cliff["shelves"]:
        a, b = to_pixels([start, end], size, shift)
        samples, hit, length = match_segment(maps, a, b, radius)
        shelves.append((samples, hit))
        matched_length += length * float(hit.mean())
        total_length += length
    corners = []
    for point in cliff["corners"]:
        p = to_pixels([point], size, shift)[0]
        corners.append((p, match_corner(maps, p, radius)))
    _, _, inside = polygon_top(to_pixels(cliff["outline"], size, shift), size)
    terrain = inside & np.isfinite(depth) & (depth > 0.0)
    coverage = float(np.count_nonzero(terrain & readable)) / max(1, int(np.count_nonzero(terrain)))
    record = {
        "shelf_match": matched_length / total_length if total_length else None,
        "shelf_length_px": total_length * scale,
        "shelves": len(cliff["shelves"]),
        "corner_match": (sum(1 for _, ok in corners if ok) / len(corners)) if corners else None,
        "corners": len(corners),
        "outline_distance_median_px": raw * scale,
        "outline_residual_median_px": residual * scale,
        "outline_points": int(len(photo_points)),
        "shift_px": [shift[0] * scale, shift[1] * scale],
        "geometric_coverage": coverage,
        "depth_max_m": cliff["depth_max_m"],
    }
    drawing = {"shelves": shelves, "corners": corners, "capture_outline": (columns, tops),
               "photo_outline_shifted": to_pixels(cliff["outline"], size, shift)}
    return (record, drawing, matched_length, total_length, photo_points, capture_points, shift), None


def draw_sheet(photo_panel, capture, maps, annotation, drawings, size, lines, font):
    photo_image = photo_panel.copy()
    draw = ImageDraw.Draw(photo_image)
    for cliff in annotation["cliffs"]:
        outline = to_pixels(cliff["outline"], PANEL_SIZE)
        draw.line(outline + [outline[0]], fill=(0, 230, 255), width=2)
        for start, end in cliff["shelves"]:
            draw.line(to_pixels([start, end], PANEL_SIZE), fill=(255, 230, 0), width=3)
        for x, y in to_pixels(cliff["corners"], PANEL_SIZE):
            draw.ellipse((x - 6, y - 6, x + 6, y + 6), outline=(255, 0, 220), width=3)
        x, y = to_pixels([cliff["outline"][1]], PANEL_SIZE)[0]
        draw.text((x + 4, y - 24), cliff["name"], fill=(0, 230, 255), font=font)
    folds = np.zeros(maps["x"].shape, dtype=bool)
    for value in maps.values():
        folds |= value
    base = np.asarray(capture, dtype=np.float32) * 0.55
    base[folds] = base[folds] * 0.3 + np.array([255.0, 40.0, 40.0]) * 0.7
    capture_image = Image.fromarray(base.astype(np.uint8)).resize(PANEL_SIZE, Image.BILINEAR)
    draw = ImageDraw.Draw(capture_image)
    sx, sy = PANEL_SIZE[0] / size[0], PANEL_SIZE[1] / size[1]
    for drawing in drawings:
        columns, tops = drawing["capture_outline"]
        draw.point([(x * sx, y * sy) for x, y in zip(columns, tops)], fill=(0, 230, 255))
        outline = [(x * sx, y * sy) for x, y in drawing["photo_outline_shifted"]]
        draw.line(outline + [outline[0]], fill=(255, 230, 0), width=1)
        for samples, hit in drawing["shelves"]:
            for (x, y), ok in zip(samples, hit):
                draw.rectangle((x * sx - 1, y * sy - 1, x * sx + 1, y * sy + 1), fill=(0, 255, 90) if ok else (255, 150, 0))
        for (x, y), ok in drawing["corners"]:
            color = (0, 255, 90) if ok else (255, 150, 0)
            draw.ellipse((x * sx - 7, y * sy - 7, x * sx + 7, y * sy + 7), outline=color, width=3)
    text_height = 12 + 28 * len(lines)
    sheet = Image.new("RGB", (PANEL_SIZE[0], PANEL_SIZE[1] * 2 + text_height), (24, 24, 24))
    sheet.paste(photo_image, (0, 0))
    sheet.paste(capture_image, (0, PANEL_SIZE[1]))
    draw = ImageDraw.Draw(sheet)
    for index, line in enumerate(lines):
        draw.text((10, PANEL_SIZE[1] * 2 + 6 + index * 28), line, fill=(235, 235, 235), font=font)
    return sheet


def check_doc():
    path = os.path.join(REPO_ROOT, DOC)
    if not os.path.isfile(path):
        return [f"所見の文書 {DOC} が無い"]
    with open(path, encoding="utf-8-sig") as file:
        text = file.read()
    if DOC_HEADING not in text:
        return [f"{DOC} に「{DOC_HEADING}」の節が無い"]
    return []


def run_shape(capture_dir, out_dir, wanted, verify, expect_path):
    """--shape の本体。終了コードを返す"""
    annotations = load_annotations()
    font = load_font(18)
    failures = []
    report = {"thresholds": {"fold_degrees": kFoldDegrees, "fold_step_px": kFoldStepPx, "depth_step_ratio": kDepthStepRatio,
                             "match_radius_px": kMatchRadiusPx, "search_fraction": list(kSearchFraction),
                             "corner_min_pixels": kCornerMinPixels, "reference_width": kReferenceWidth},
              "capture": os.path.relpath(capture_dir, REPO_ROOT), "photos": {}}
    # 既定の選択は注釈の有無と独立に決めた 5 枚（注釈が欠けた写真は下で落ちる）
    photos = [p for p in PHOTOS if p["id"] in (wanted or SHAPE_PHOTOS)]
    all_residuals = []
    for entry in photos:
        photo_id, view = entry["id"], entry["view"]
        annotation = annotations.get(photo_id)
        if not annotation or not annotation.get("cliffs"):
            failures.append(f"{photo_id} の注釈が ShapeAnnotations.json に無い")
            continue
        incomplete = [cliff.get("name", "?") for cliff in annotation["cliffs"]
                      if not cliff.get("outline") or "shelves" not in cliff or not cliff.get("corners")
                      or "depth_max_m" not in cliff]
        if incomplete:
            failures.append(f"{photo_id} の崖 {incomplete} に外形・角・深度の上限のどれかが無い")
            continue
        if not any(cliff["shelves"] for cliff in annotation["cliffs"]):
            failures.append(f"{photo_id} に棚の注釈が無い")
            continue
        paths = {
            "photo": os.path.join(PHOTO_DIR, photo_id + ".jpg"),
            "png": os.path.join(capture_dir, view + ".png"),
            "depth": os.path.join(capture_dir, view + ".aov.depth.pfm"),
            "geometric": os.path.join(capture_dir, view + ".aov.geometric_normal.pfm"),
        }
        missing = [path for path in paths.values() if not os.path.isfile(path)]
        if missing:
            failures.extend(f"ファイルが無い: {path}" for path in missing)
            continue
        capture = Image.open(paths["png"]).convert("RGB")
        size = (capture.width, capture.height)
        if capture.width < MIN_CAPTURE_SIZE[0] or capture.height < MIN_CAPTURE_SIZE[1]:
            failures.append(f"撮影が小さい: {paths['png']}（{capture.width}×{capture.height}）")
            continue
        depth = read_pfm_any(paths["depth"])[:, :, 0].astype(np.float64)
        normal, readable = decode_geometric_normal(read_pfm_any(paths["geometric"])[:, :, 0].astype(np.float64))
        if depth.shape != (capture.height, capture.width) or normal.shape[:2] != depth.shape:
            failures.append(f"{view} の深度・幾何の法線の大きさが PNG と違う")
            continue
        maps = fold_maps(depth, normal, readable)
        scale = kReferenceWidth / capture.width
        cliffs = {}
        drawings = []
        totals = {"matched": 0.0, "length": 0.0, "corners": 0, "corner_hits": 0}
        raw_distances, residual_distances = [], []
        for cliff in annotation["cliffs"]:
            result, reason = measure_cliff(cliff, depth, maps, readable, size, scale)
            if result is None:
                failures.append(f"{photo_id} の {cliff['name']}: {reason}")
                continue
            record, drawing, matched, length, photo_points, capture_points, shift = result
            if record["geometric_coverage"] < kGeometricCoverage:
                failures.append(f"{photo_id} の {cliff['name']} の幾何の法線の被覆率 {record['geometric_coverage']:.3f} が "
                                f"{kGeometricCoverage} 未満")
            cliffs[cliff["name"]] = record
            drawings.append(drawing)
            totals["matched"] += matched
            totals["length"] += length
            totals["corners"] += record["corners"]
            totals["corner_hits"] += sum(1 for _, ok in drawing["corners"] if ok)
            raw_distances.append(nearest_distances(photo_points, capture_points))
            residual_distances.append(nearest_distances(photo_points + shift, capture_points))
            all_residuals.append(residual_distances[-1] * scale)
            print(f"SHAPE_MEASURE photo={photo_id} cliff=\"{cliff['name']}\" shelf_match={fmt(record['shelf_match'])} "
                  f"corner_match={fmt(record['corner_match'])} outline_distance_median_px={record['outline_distance_median_px']:.1f} "
                  f"outline_residual_median_px={record['outline_residual_median_px']:.1f} "
                  f"shift_px={record['shift_px'][0]:.0f},{record['shift_px'][1]:.0f} coverage={record['geometric_coverage']:.3f}")
        if not cliffs:
            continue
        photo_record = {
            "view": view,
            "capture_size": list(size),
            "shelf_match": totals["matched"] / totals["length"] if totals["length"] else None,
            "corner_match": totals["corner_hits"] / totals["corners"] if totals["corners"] else None,
            "outline_distance_median_px": float(np.median(np.concatenate(raw_distances))) * scale,
            "outline_residual_median_px": float(np.median(np.concatenate(residual_distances))) * scale,
            "cliffs": cliffs,
        }
        report["photos"][photo_id] = photo_record
        print(f"SHAPE_MEASURE photo={photo_id} view={view} shelf_match={fmt(photo_record['shelf_match'])} "
              f"corner_match={fmt(photo_record['corner_match'])} "
              f"outline_distance_median_px={photo_record['outline_distance_median_px']:.1f} "
              f"outline_residual_median_px={photo_record['outline_residual_median_px']:.1f}")
        photo_panel, _, _ = fit_photo(Image.open(paths["photo"]).convert("RGB"))
        lines = [f"上: 写真 {photo_id}（水色 = 外形、黄 = 棚、桃 = 角）　下: 撮影 {view}（{size[0]}×{size[1]}）の面の法線と深度の折れ（赤）・"
                 f"撮影の外形（水色）・ずらした注釈（緑 = 一致、橙 = 不一致、黄 = 写真の外形）"]
        for name, record in cliffs.items():
            lines.append(f"{name}: 棚の一致 {fmt_display(record['shelf_match'])}（{record['shelves']} 本）　角の一致 {fmt_display(record['corner_match'])}"
                         f"（{record['corners']} 点）　外形の距離の中央値 {record['outline_distance_median_px']:.1f} 画素"
                         f"（ずらした後 {record['outline_residual_median_px']:.1f}、ずらした量 {record['shift_px'][0]:.0f}, "
                         f"{record['shift_px'][1]:.0f}）")
        lines.append(f"折れ: 面の法線の角 {kFoldDegrees:.0f}° 以上か深度の段 {kDepthStepRatio * 100:.0f} % 以上（{kFoldStepPx} 画素の前後）。"
                     f"許容 {kMatchRadiusPx} 画素（1920 幅）。輝度は使わない")
        draw_sheet(photo_panel, capture, maps, annotation, drawings, size, lines, font).save(
            os.path.join(out_dir, f"shape_{photo_id}.png"))
    if all_residuals:
        # 位置の許容（kMatchRadiusPx）を決め直すときの値: 選んだ写真の全部の崖の、ずらした後の外形の距離の中央値と 75 百分位
        merged = np.concatenate(all_residuals)
        report["outline_residual_all_px"] = {"median": float(np.median(merged)), "p75": float(np.percentile(merged, 75))}
        print(f"SHAPE_MEASURE outline_residual_all_median_px={np.median(merged):.1f} p75={np.percentile(merged, 75):.1f} "
              f"match_radius_px={kMatchRadiusPx}")
    if not photos:
        failures.append("測る写真が無い（空の選択）")
    if verify and len(report["photos"]) != len(photos):
        failures.append(f"測った写真が {len(report['photos'])} 枚（選んだ {len(photos)} 枚すべてが要る）")
    if verify:
        failures.extend(check_doc())
    if expect_path:
        from PhotoExpect import evaluate

        failures.extend(evaluate(expect_path, report["photos"], {p["id"] for p in photos}, {p["id"] for p in PHOTOS},
                                 "SHAPE_MEASURE_EXPECT", True))
    report["failures"] = failures
    with open(os.path.join(out_dir, REPORT), "w", encoding="utf-8") as file:
        json.dump(report, file, ensure_ascii=False, indent=2)
    if failures:
        for failure in failures:
            print(f"SHAPE_MEASURE failure={failure}")
        print(f"SHAPE_MEASURE result=fail failures={len(failures)}")
        return 1
    print(f"SHAPE_MEASURE result=pass photos={len(report['photos'])} out={out_dir}" + (" verify=1" if verify else "")
          + (f" expect={expect_path}" if expect_path else ""))
    return 0
