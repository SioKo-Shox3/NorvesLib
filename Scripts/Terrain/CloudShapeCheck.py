# 雲の形（塊・切れ間・縁）を全画素の雲だけの不透明度の画像で測る。ReferenceCheck.py --cloud-shape の本体。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --cloud-shape --engine <撮影先> [--views cloud_sky,ref_far_01] [--verify]
#                                            [--expect Scripts/Terrain/ExpectSpecs/cloud-shape.json]
#       SCN-WL-CLOUD-OPACITY-AOV の撮影（<視点>.cloud.opacity.json・.f32）を視点ごとに測り、結果を <撮影先>/cloud_shape_check.json、
#       量の画（塊の色分け・測れた縁）を <撮影先>/cloud_shape_<視点>.png に書く。--verify は入力のハッシュ（inputs.json）を
#       今のファイルと照らす。--expect は仕様の行（量ごとの下限・上限）を、行の views に入る視点で判定する。
#   python Scripts/Terrain/ReferenceCheck.py --cloud-shape --selftest [--expect <仕様>]
#       合成の画像を測り、仕様で一様な層・雲なし・細かなノイズ・粗い斑点・穴の開いた膜が落ち、既知の幅の縁が ±1 画素で
#       測れることを確かめる。独立の見本（Assets/Reference/SkySamples/ の HDRI。FetchSkySamples.py で取得）を cloud_sky の
#       向き・画角へ投影したマスクを測り、マスクで測れる行を見本がすべて通ることも確かめる。見本のマスクの画を
#       Assets/Reference/SkySamples/masks/ に書く。
#
# 測る画素: 地形のマスクの外で、視線の仰角が MIN_ELEVATION_DEG より上の画素（空の画素）。
# 量（Docs/RenderingValidation/WastelandCloudShape.md の「塊・切れ間・縁の量」）:
#   gap_fraction（不透明度 0.1 未満の割合）、film_fraction（0.1〜0.3）、
#   塊 = 0.3 以上の 8 連結の成分で、面積が空の画素の MIN_BLOB_FRACTION 以上の物:
#   blob_count、blob_area_median・blob_area_p90（空の画素に対する割合）とその比 blob_area_p90_over_median、
#   largest_blob_share（最大の塊 ÷ 0.3 以上の画素の全部）、edge_blobs（画面の端に触れる塊の数）、
#   thick_share（0.6 以上 ÷ 0.3 以上）、blob_opacity_std_median（塊ごとの不透明度の標準偏差の中央値）、
#   edge_width_median_px・edge_measured_fraction（縁の幅。下の measure_edges）、
#   straight_edge_fraction（画面の縦か横に沿って STRAIGHT_MIN_PX 以上まっすぐ続く縁の長さ ÷ 測れた縁の長さ。下の
#   measure_straight_edges。SCN-WL-CLOUD-CONTOUR-FIX で足した記録の量で、cloud-shape.json の行には無い）。
#   その横（行の間の縁）と縦（列の間の縁）の内訳を cloud_shape_straight の行と JSON の straight_edge_split に書く
#   （SCN-WL-CLOUD-SHAPE-NOISE。記録の量）。
import glob
import json
import math
import os

import cv2
import numpy as np

DEFAULT_EXPECT = os.path.join("Scripts", "Terrain", "ExpectSpecs", "cloud-shape.json")
FORMAT = "norves_cloud_opacity_v1"
MIN_ELEVATION_DEG = 2.0
GAP_OPACITY = 0.1
BLOB_OPACITY = 0.3
THICK_OPACITY = 0.6
# 数える塊の面積の下限（空の画素に対する割合）
MIN_BLOB_FRACTION = 0.0005
# 縁の幅: 0.1 から min(0.5, 塊の最大 × 0.8) まで
EDGE_LOW = 0.1
EDGE_HIGH = 0.5
EDGE_PEAK_SHARE = 0.8
EDGE_STEP_PX = 0.25
EDGE_REACH_PX = 64.0
# 縁を測る外周の画素の数の上限（超えたら等間隔に間引く）
EDGE_MAX_PIXELS = 12000
# まっすぐな縁: 不透明度 BLOB_OPACITY の等値線を、ぼかした（σ = STRAIGHT_BLUR_PX）画で画素の間の位置まで求め、
# 同じ行（列）に STRAIGHT_MIN_PX 以上続き、位置の幅が STRAIGHT_TOLERANCE_PX 以内の区間をまっすぐな縁とする
STRAIGHT_BLUR_PX = 1.5
STRAIGHT_MIN_PX = 20
STRAIGHT_TOLERANCE_PX = 0.5
# 見本のマスク: 赤 ÷ 青がこれより大きい画素を雲とする
SAMPLE_RB_THRESHOLD = 0.6
# 見本の太陽の周り（この角度より内）を測る画素から除く
SAMPLE_SUN_EXCLUDE_DEG = 45.0
# 見本の向き: 太陽の方位を撮影の太陽の方位へ合わせた向きと、そこから 90° ずつ回した向き
SAMPLE_YAWS_DEG = (120.0, 180.0, 240.0)

SAMPLE_DIR = os.path.join("Assets", "Reference", "SkySamples")
# マスクで測れる量（見本の幅から範囲を決める行）。ほかは不透明度でしか測れない
MASK_QUANTITIES = ("gap_fraction", "blob_count", "blob_area_median", "blob_area_p90", "blob_area_p90_over_median",
                   "largest_blob_share")
QUANTITIES = ("gap_fraction", "film_fraction", "blob_count", "blob_area_median", "blob_area_p90",
              "blob_area_p90_over_median", "largest_blob_share", "edge_blobs", "thick_share", "blob_opacity_std_median",
              "edge_width_median_px", "edge_measured_fraction", "straight_edge_fraction")
EDGE_REASONS = ("other_blob", "screen_edge", "outside_sky", "no_upper", "no_lower", "no_gradient")


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def pixel_directions(inverse_vp, camera, width, height):
    """画素の中心の視線の向き（行 0 = 画面の上。CloudOpacityCheck の契約と同じ逆の射影）"""
    inverse = np.asarray(inverse_vp, dtype=np.float64).reshape(4, 4).T
    xs, ys = np.meshgrid((np.arange(width) + 0.5) / width * 2.0 - 1.0, (np.arange(height) + 0.5) / height * 2.0 - 1.0)
    clip = np.stack([xs, ys, np.ones_like(xs), np.ones_like(xs)], axis=-1)
    world = clip @ inverse.T
    direction = world[..., :3] / world[..., 3:4] - np.asarray(camera, dtype=np.float64)
    return direction / np.linalg.norm(direction, axis=-1, keepdims=True)


def elevation_deg(directions):
    return np.degrees(np.arcsin(np.clip(directions[..., 1], -1.0, 1.0)))


def remap(image, xs, ys, interpolation, border):
    return cv2.remap(image, xs.astype(np.float32), ys.astype(np.float32), interpolation,
                     borderMode=cv2.BORDER_CONSTANT, borderValue=border)


def bilinear_taps_invalid(sample_x, sample_y, sky):
    """双線形の標本が使う画素（重みが 0 でない物）の全部を調べ、画面の外を含むか・空の外を含むかを返す。

    画面の外の値や空の外の 0 を補間に混ぜた標本は、閾値の交差を作り得るので交差に使わない。
    """
    height, width = sky.shape
    x0 = np.floor(sample_x)
    y0 = np.floor(sample_y)
    x1 = np.where(sample_x > x0, x0 + 1.0, x0)
    y1 = np.where(sample_y > y0, y0 + 1.0, y0)
    off_screen = (x0 < 0) | (x1 >= width) | (y0 < 0) | (y1 >= height)
    not_sky = np.zeros(sample_x.shape, dtype=bool)
    for tap_y in (y0, y1):
        for tap_x in (x0, x1):
            row = np.clip(tap_y, 0, height - 1).astype(np.int64)
            column = np.clip(tap_x, 0, width - 1).astype(np.int64)
            not_sky |= ~sky[row, column]
    return off_screen, not_sky & ~off_screen


def measure_edges(opacity, sky, labels, counted, upper):
    """塊の外周の画素から、不透明度の勾配の向きに 0.1 から上の閾値（塊ごと）まで上がる距離を測る。

    外周の画素 = 数える塊の画素で、8 近傍に塊の外の画素がある物。勾配（Sobel）の逆へ戻って 0.1 を切る位置、そこから
    勾配の向きへ進んで上の閾値に届く位置を、0.25 画素の歩の双線形の標本と線形の補間で求め、その差を幅とする。
    その区間がほかの塊・画面の外・空の外（地形か仰角の下限より下）を通る縁、64 画素の内に閾値に届かない縁は測れない縁。
    """
    counted_mask = counted[labels]
    eroded = cv2.erode(counted_mask.astype(np.uint8), np.ones((3, 3), np.uint8), borderType=cv2.BORDER_CONSTANT,
                       borderValue=0)
    perimeter = counted_mask & (eroded == 0)
    ys, xs = np.nonzero(perimeter)
    total = int(xs.size)
    reasons = {reason: 0 for reason in EDGE_REASONS}
    if total == 0:
        return {"perimeter_pixels": 0, "sampled": 0, "measured": 0, "widths": np.zeros(0), "points": [],
                "reasons": reasons}
    if total > EDGE_MAX_PIXELS:
        pick = np.linspace(0, total - 1, EDGE_MAX_PIXELS).astype(np.int64)
        ys, xs = ys[pick], xs[pick]
    gx = cv2.Sobel(opacity, cv2.CV_64F, 1, 0, ksize=3)[ys, xs]
    gy = cv2.Sobel(opacity, cv2.CV_64F, 0, 1, ksize=3)[ys, xs]
    norm = np.hypot(gx, gy)
    has_gradient = norm > 1e-9
    gx = np.where(has_gradient, gx / np.maximum(norm, 1e-12), 0.0)
    gy = np.where(has_gradient, gy / np.maximum(norm, 1e-12), 0.0)
    steps = np.arange(-EDGE_REACH_PX, EDGE_REACH_PX + EDGE_STEP_PX * 0.5, EDGE_STEP_PX)
    center = int(np.argmin(np.abs(steps)))
    sample_x = xs[:, None] + gx[:, None] * steps[None, :]
    sample_y = ys[:, None] + gy[:, None] * steps[None, :]
    values = remap(opacity.astype(np.float32), sample_x, sample_y, cv2.INTER_LINEAR, -1.0).astype(np.float64)
    # 補間に使う 4 画素のどれかが画面の外か空の外なら、その標本は交差に使わず、区間に入れば測れない縁にする
    outside, outside_sky = bilinear_taps_invalid(sample_x, sample_y, sky)
    invalid = outside | outside_sky
    sample_labels = remap(labels.astype(np.float32), sample_x, sample_y, cv2.INTER_NEAREST, -1.0).astype(np.int64)
    own = labels[ys, xs]
    other = (sample_labels > 0) & (sample_labels != own[:, None])
    index = np.arange(steps.size)[None, :]

    below = (values < EDGE_LOW) & (index <= center) & ~invalid
    has_low = below.any(axis=1)
    low_index = np.where(has_low, steps.size - 1 - np.argmax(below[:, ::-1], axis=1), 0)
    above = (values >= upper[own][:, None]) & (index > low_index[:, None]) & ~invalid
    has_high = above.any(axis=1)
    high_index = np.where(has_high, np.argmax(above, axis=1), steps.size - 1)
    span_end = np.maximum(high_index, center)
    span_start = np.where(has_low, low_index, 0)
    in_span = (index >= span_start[:, None]) & (index <= span_end[:, None])
    # 戻りで 0.1 を切れなかった縁は、中心から戻った全区間を調べる
    span_back = np.where(has_low[:, None], in_span, (index <= center) & in_span)
    hit_other = (other & span_back).any(axis=1)
    hit_screen = (outside & span_back).any(axis=1)
    hit_outside_sky = (outside_sky & span_back).any(axis=1)

    rows = np.arange(xs.size)
    low_value = values[rows, low_index]
    low_next = values[rows, np.minimum(low_index + 1, steps.size - 1)]
    low_t = steps[low_index] + np.clip((EDGE_LOW - low_value) / np.where(low_next - low_value > 1e-12,
                                                                        low_next - low_value, 1.0), 0.0, 1.0) * EDGE_STEP_PX
    high_value = values[rows, high_index]
    high_prev = values[rows, np.maximum(high_index - 1, 0)]
    target = upper[own]
    high_t = steps[high_index] - np.clip((high_value - target) / np.where(high_value - high_prev > 1e-12,
                                                                          high_value - high_prev, 1.0),
                                         0.0, 1.0) * EDGE_STEP_PX
    reason = np.full(xs.size, "", dtype=object)
    reason[~has_gradient] = "no_gradient"
    for flag, name in ((hit_other, "other_blob"), (hit_screen, "screen_edge"), (hit_outside_sky, "outside_sky"),
                       (~has_low, "no_lower"), (~has_high, "no_upper")):
        reason[(reason == "") & flag] = name
    measured = reason == ""
    for name in EDGE_REASONS:
        reasons[name] = int(np.count_nonzero(reason == name))
    widths = (high_t - low_t)[measured]
    points = [(int(x), int(y), bool(ok)) for x, y, ok in zip(xs, ys, measured)]
    return {"perimeter_pixels": total, "sampled": int(xs.size), "measured": int(np.count_nonzero(measured)),
            "widths": widths, "points": points, "reasons": reasons}


def straight_runs(crossings, offsets):
    """crossings[i, j] が真の所を j の向きに続く区間に分け、offsets の幅が STRAIGHT_TOLERANCE_PX を超えたら区間を切る。
    STRAIGHT_MIN_PX 以上の区間の長さの和を返す"""
    total = 0
    for row, offset_row in zip(crossings, offsets):
        columns = np.nonzero(row)[0]
        if columns.size < STRAIGHT_MIN_PX:
            continue
        length = 0
        previous = -2
        low = high = 0.0
        for column in columns:
            value = offset_row[column]
            if column == previous + 1 and max(high, value) - min(low, value) <= STRAIGHT_TOLERANCE_PX:
                length += 1
                low, high = min(low, value), max(high, value)
            else:
                if length >= STRAIGHT_MIN_PX:
                    total += length
                length = 1
                low = high = value
            previous = column
        if length >= STRAIGHT_MIN_PX:
            total += length
    return total


def measure_straight_edges(opacity, sky):
    """画面の縦か横に沿ってまっすぐ続く縁の長さの割合。

    ぼかした不透明度の BLOB_OPACITY の等値線が、縦に隣り合う 2 画素（ともに空）の間を横切る所を横向きの縁の 1 画素、
    横に隣り合う 2 画素の間を横切る所を縦向きの縁の 1 画素とする（勾配の大きい方の向きの横切りだけを数え、斜めの縁を
    2 重に数えない）。測れた縁の長さ = 数えた横切りの数、まっすぐな縁 = straight_runs の区間の長さの和。
    """
    blurred = cv2.GaussianBlur(np.where(sky, opacity, 0.0).astype(np.float64), (0, 0), STRAIGHT_BLUR_PX)
    gx = cv2.Sobel(blurred, cv2.CV_64F, 1, 0, ksize=3)
    gy = cv2.Sobel(blurred, cv2.CV_64F, 0, 1, ksize=3)
    above = blurred - BLOB_OPACITY
    # 横向きの縁: 行 y と y + 1 の間
    a, b = above[:-1, :], above[1:, :]
    pair_sky = sky[:-1, :] & sky[1:, :]
    horizontal = pair_sky & (a * b < 0.0) & (np.abs(gy[:-1, :] + gy[1:, :]) >= np.abs(gx[:-1, :] + gx[1:, :]))
    horizontal_offset = np.where(horizontal, a / np.where(a - b != 0.0, a - b, 1.0), 0.0)
    # 縦向きの縁: 列 x と x + 1 の間
    a, b = above[:, :-1], above[:, 1:]
    pair_sky = sky[:, :-1] & sky[:, 1:]
    vertical = pair_sky & (a * b < 0.0) & (np.abs(gx[:, :-1] + gx[:, 1:]) > np.abs(gy[:, :-1] + gy[:, 1:]))
    vertical_offset = np.where(vertical, a / np.where(a - b != 0.0, a - b, 1.0), 0.0)
    edge_length = int(np.count_nonzero(horizontal) + np.count_nonzero(vertical))
    straight_horizontal = straight_runs(horizontal, horizontal_offset)
    straight_vertical = straight_runs(vertical.T, vertical_offset.T)
    detail = {"edge_length_px": edge_length, "straight_horizontal_px": straight_horizontal,
              "straight_vertical_px": straight_vertical}
    if edge_length == 0:
        return None, detail
    return (straight_horizontal + straight_vertical) / edge_length, detail


def straight_edge_split(detail):
    """measure_straight_edges の内訳を、画面の横に沿う縁（行の間）と縦に沿う縁（列の間）の割合に分ける（記録の量）"""
    if not detail or not detail.get("edge_length_px"):
        return None
    length = detail["edge_length_px"]
    return {"horizontal_px": detail["straight_horizontal_px"], "vertical_px": detail["straight_vertical_px"],
            "edge_length_px": length, "horizontal_fraction": detail["straight_horizontal_px"] / length,
            "vertical_fraction": detail["straight_vertical_px"] / length}


def measure(opacity, sky, edges=True):
    """空の画素（sky が真）の不透明度から量を測る。値の求まらない量は None"""
    opacity = np.where(sky, np.nan_to_num(opacity.astype(np.float64), nan=0.0), 0.0)
    sky_pixels = int(np.count_nonzero(sky))
    values = {name: None for name in QUANTITIES}
    detail = {"sky_pixels": sky_pixels}
    if sky_pixels == 0:
        return values, detail, None
    sky_values = opacity[sky]
    cloud_pixels = int(np.count_nonzero(sky_values >= BLOB_OPACITY))
    values["gap_fraction"] = float(np.count_nonzero(sky_values < GAP_OPACITY) / sky_pixels)
    values["film_fraction"] = float(np.count_nonzero((sky_values >= GAP_OPACITY) & (sky_values < BLOB_OPACITY))
                                    / sky_pixels)
    values["thick_share"] = float(np.count_nonzero(sky_values >= THICK_OPACITY) / cloud_pixels) if cloud_pixels else None
    blob_mask = (sky & (opacity >= BLOB_OPACITY)).astype(np.uint8)
    count, labels, stats, _ = cv2.connectedComponentsWithStats(blob_mask, connectivity=8)
    areas = stats[:, cv2.CC_STAT_AREA].astype(np.int64)
    min_area = MIN_BLOB_FRACTION * sky_pixels
    counted = np.zeros(count, dtype=bool)
    counted[1:] = areas[1:] >= min_area
    kept = np.nonzero(counted)[0]
    flat = labels.ravel()
    sums = np.bincount(flat, weights=opacity.ravel(), minlength=count)
    squares = np.bincount(flat, weights=(opacity * opacity).ravel(), minlength=count)
    peaks = np.zeros(count)
    np.maximum.at(peaks, flat, opacity.ravel())
    height, width = opacity.shape
    left = stats[:, cv2.CC_STAT_LEFT]
    top = stats[:, cv2.CC_STAT_TOP]
    touches = (left == 0) | (top == 0) | (left + stats[:, cv2.CC_STAT_WIDTH] >= width) | \
              (top + stats[:, cv2.CC_STAT_HEIGHT] >= height)
    values["blob_count"] = int(kept.size)
    values["edge_blobs"] = int(np.count_nonzero(touches[kept]))
    detail.update({"cloud_pixels": cloud_pixels, "components": int(count - 1), "min_blob_pixels": float(min_area),
                   "dropped_small_components": int(count - 1 - kept.size)})
    if kept.size:
        kept_areas = areas[kept] / sky_pixels
        values["blob_area_median"] = float(np.median(kept_areas))
        values["blob_area_p90"] = float(np.percentile(kept_areas, 90))
        values["blob_area_p90_over_median"] = values["blob_area_p90"] / values["blob_area_median"]
        values["largest_blob_share"] = float(areas[kept].max() / cloud_pixels)
        means = sums[kept] / areas[kept]
        stds = np.sqrt(np.maximum(squares[kept] / areas[kept] - means * means, 0.0))
        values["blob_opacity_std_median"] = float(np.median(stds))
    edge = None
    if edges and kept.size:
        upper = np.minimum(EDGE_HIGH, EDGE_PEAK_SHARE * peaks)
        edge = measure_edges(opacity, sky, labels, counted, upper)
        if edge["sampled"]:
            values["edge_measured_fraction"] = edge["measured"] / edge["sampled"]
        if edge["measured"]:
            values["edge_width_median_px"] = float(np.median(edge["widths"]))
        detail["edges"] = {key: edge[key] for key in ("perimeter_pixels", "sampled", "measured", "reasons")}
        if edge["measured"]:
            detail["edges"]["width_percentiles_px"] = {str(p): float(np.percentile(edge["widths"], p))
                                                       for p in (10, 25, 50, 75, 90)}
    if edges:
        values["straight_edge_fraction"], detail["straight_edges"] = measure_straight_edges(opacity, sky)
    return values, detail, (labels, counted, edge)


def label_colors(count):
    rng = np.random.default_rng(12345)
    colors = rng.integers(70, 256, size=(max(count, 1), 3), dtype=np.int64).astype(np.uint8)
    colors[0] = 0
    return colors


def write_preview(path, opacity, sky, shape):
    """左: 不透明度（空の外は茶）。右: 塊の色分け（数えない小さな成分は暗い灰、薄い膜は灰、切れ間は紺）と縁の点
    （測れた縁は緑、測れない縁は赤）"""
    opacity = np.clip(np.where(sky, opacity, 0.0), 0.0, 1.0)
    gray = (opacity * 255.0 + 0.5).astype(np.uint8)
    left = np.stack([gray, gray, gray], axis=-1)
    left[~sky] = (40, 64, 96)
    right = np.zeros_like(left)
    right[sky] = (90, 30, 10)
    right[sky & (opacity >= GAP_OPACITY)] = (120, 120, 120)
    if shape is not None:
        labels, counted, edge = shape
        colors = label_colors(counted.size)
        blob = labels > 0
        right[blob] = (60, 60, 60)
        keep = counted[labels]
        right[keep] = colors[labels[keep]]
        if edge:
            for x, y, ok in edge["points"]:
                right[max(y - 1, 0):y + 2, max(x - 1, 0):x + 2] = (0, 230, 0) if ok else (0, 0, 255)
    right[~sky] = (40, 64, 96)
    cv2.imwrite(path, np.hstack([left, right]))


def evaluate_expect(values, spec, view=None):
    """仕様の行を判定する。(行の結果, 落ちた理由)"""
    rows = []
    failures = []
    for row in spec.get("rows", []):
        views = row.get("views")
        if view is not None and views and view not in views:
            continue
        quantity = row.get("quantity")
        if quantity not in QUANTITIES:
            failures.append(f"仕様の量 {quantity} を知らない")
            continue
        value = values.get(quantity)
        low = row.get("min")
        high = row.get("max")
        ok = value is not None and (low is None or value >= low) and (high is None or value <= high)
        rows.append({"quantity": quantity, "value": value, "min": low, "max": high, "pass": bool(ok)})
        if not ok:
            shown = "求まらない" if value is None else f"{value:.6g}"
            failures.append(f"{quantity} = {shown} が範囲 [{low}, {high}] の外")
    return rows, failures


def load_hdr(path):
    image = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if image is None:
        return None
    return image[:, :, ::-1].astype(np.float32)


def sample_masks(repo_root, view_meta, sun_direction):
    """見本の HDRI を撮影の視点の向き・画角へ投影し、赤 ÷ 青で雲のマスクを作る。[(名前, 不透明度, 空, 投影した色)]"""
    width = int(view_meta["target"]["width"])
    height = int(view_meta["target"]["height"])
    directions = pixel_directions(view_meta["camera"]["inverse_view_projection"], view_meta["camera"]["position"],
                                  width, height)
    elevation = elevation_deg(directions)
    azimuth = np.arctan2(directions[..., 2], directions[..., 0])
    sun = np.asarray(sun_direction, dtype=np.float64)
    sun_azimuth = math.atan2(sun[2], sun[0])
    results = []
    for path in sorted(glob.glob(os.path.join(repo_root, SAMPLE_DIR, "*.hdr"))):
        image = load_hdr(path)
        if image is None:
            continue
        rows, columns, _ = image.shape
        luminance = image.mean(axis=-1)
        peak_row, peak_column = np.unravel_index(int(np.argmax(luminance)), luminance.shape)
        hdri_sun_azimuth = (peak_column + 0.5) / columns * 2.0 * math.pi
        hdri_sun_elevation = math.radians(90.0 - (peak_row + 0.5) / rows * 180.0)
        name = os.path.basename(path).rsplit("_2k", 1)[0]
        for yaw_deg in SAMPLE_YAWS_DEG:
            yaw = hdri_sun_azimuth - sun_azimuth + math.radians(yaw_deg)
            map_x = ((azimuth + yaw) / (2.0 * math.pi)) % 1.0 * columns - 0.5
            map_y = (90.0 - elevation) / 180.0 * rows - 0.5
            color = cv2.remap(image, map_x.astype(np.float32), map_y.astype(np.float32), cv2.INTER_LINEAR,
                              borderMode=cv2.BORDER_WRAP)
            world_sun_azimuth = hdri_sun_azimuth - yaw
            world_sun = np.array([math.cos(hdri_sun_elevation) * math.cos(world_sun_azimuth),
                                  math.sin(hdri_sun_elevation),
                                  math.cos(hdri_sun_elevation) * math.sin(world_sun_azimuth)])
            near_sun = directions @ world_sun > math.cos(math.radians(SAMPLE_SUN_EXCLUDE_DEG))
            sky = (elevation > MIN_ELEVATION_DEG) & ~near_sun
            ratio = color[..., 0] / np.maximum(color[..., 2], 1e-6)
            opacity = (ratio > SAMPLE_RB_THRESHOLD).astype(np.float64)
            results.append((f"{name}@{int(yaw_deg)}", opacity, sky, color))
    return results


def write_sample_preview(path, color, opacity, sky, shape):
    scale = np.percentile(color[sky], 99) if np.any(sky) else 1.0
    ldr = np.clip(color / max(scale, 1e-6), 0.0, 1.0) ** (1.0 / 2.2)
    photo = (ldr[..., ::-1] * 255.0 + 0.5).astype(np.uint8)
    photo[~sky] = photo[~sky] // 3
    write_preview(path, opacity, sky, shape)
    preview = cv2.imread(path)
    cv2.imwrite(path, np.hstack([photo, preview[:, opacity.shape[1]:]]))


def summarize(values):
    return " ".join(f"{name}={'none' if values[name] is None else format(values[name], '.4g')}" for name in QUANTITIES)


def run(rc, capture_dir, views, verify, expect_path):
    failures = []
    inputs_ok, input_failures, record = rc.check_inputs(capture_dir)
    if verify and not inputs_ok:
        failures.extend(input_failures)
    metric_views = rc.views_from_metrics(capture_dir)
    if metric_views is None:
        failures.append("metrics.json が無い（CaptureScene.ps1 の出力先でない）")
        metric_views = []
    targets = views if views else metric_views
    if not targets:
        failures.append("測る視点が無い")
    spec = load_json(expect_path) if expect_path else None
    results = []
    for view in targets:
        result = {"view": view}
        meta_path = os.path.join(capture_dir, f"{view}.cloud.opacity.json")
        if view not in metric_views or not os.path.isfile(meta_path):
            failures.append(f"{view}: 撮影に雲の不透明度の画像が無い")
            continue
        meta = load_json(meta_path)
        if meta.get("format") != FORMAT:
            failures.append(f"{view}: 形式が違う（{meta.get('format')}）")
            continue
        image, reason = rc.atmos_read_floats(capture_dir, meta.get("image", {}))
        width = int(meta["target"]["width"])
        height = int(meta["target"]["height"])
        if reason or image is None or image.shape != (height, width, 4):
            failures.append(f"{view}: 不透明度の画像が読めない（{reason or 'shape'}）")
            continue
        if not np.all(np.isfinite(image)):
            failures.append(f"{view}: 不透明度の画像に NaN か Inf がある")
            continue
        directions = pixel_directions(meta["camera"]["inverse_view_projection"], meta["camera"]["position"],
                                      width, height)
        sky = (image[:, :, 2] < 0.5) & (elevation_deg(directions) > MIN_ELEVATION_DEG)
        values, detail, shape = measure(image[:, :, 0], sky)
        preview = os.path.join(capture_dir, f"cloud_shape_{view}.png")
        write_preview(preview, image[:, :, 0], sky, shape)
        result.update({"values": values, "detail": detail, "preview": os.path.basename(preview),
                       "terrain_pixels": int(np.count_nonzero(image[:, :, 2] >= 0.5))})
        if values["gap_fraction"] is None:
            failures.append(f"{view}: 空の画素が無い")
        if spec is not None:
            rows, row_failures = evaluate_expect(values, spec, view)
            result["expect"] = rows
            failures.extend(f"{view}: {f}" for f in row_failures)
        print(f"REFERENCE_CHECK cloud_shape view={view} sky_pixels={detail['sky_pixels']} {summarize(values)} "
              f"preview={os.path.basename(preview)}")
        straight = straight_edge_split(detail.get("straight_edges"))
        if straight is not None:
            result["straight_edge_split"] = straight
            print(f"REFERENCE_CHECK cloud_shape_straight view={view} "
                  f"horizontal_fraction={straight['horizontal_fraction']:.6g} vertical_fraction={straight['vertical_fraction']:.6g} "
                  f"horizontal_px={straight['horizontal_px']} vertical_px={straight['vertical_px']} "
                  f"edge_length_px={straight['edge_length_px']}")
        results.append(result)
    report = {
        "mode": "cloud_shape", "verify": bool(verify), "inputs_current": inputs_ok, "inputs_record": record,
        "expect": expect_path,
        "criteria": {"min_elevation_deg": MIN_ELEVATION_DEG, "gap_opacity": GAP_OPACITY, "blob_opacity": BLOB_OPACITY,
                     "thick_opacity": THICK_OPACITY, "min_blob_fraction": MIN_BLOB_FRACTION, "edge_low": EDGE_LOW,
                     "edge_high": EDGE_HIGH, "edge_peak_share": EDGE_PEAK_SHARE, "edge_step_px": EDGE_STEP_PX,
                     "edge_reach_px": EDGE_REACH_PX},
        "views": results, "failures": failures,
    }
    report_path = os.path.join(capture_dir, "cloud_shape_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False, default=float)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=cloud_shape views={len(results)} expect={int(spec is not None)} "
          f"inputs_current={int(bool(inputs_ok))} report={report_path}")
    return 0 if not failures else 1


# --- 合成の画像の試験 ---------------------------------------------------------------------------------------------

SELFTEST_SIZE = (1080, 1920)


def disk_distance(height, width, centers):
    ys, xs = np.mgrid[0:height, 0:width]
    best = np.full((height, width), np.inf)
    for cx, cy in centers:
        best = np.minimum(best, np.hypot(xs + 0.5 - cx, ys + 0.5 - cy))
    return best


def synthetic_cases():
    """(名前, 不透明度, 落ちるべきか)"""
    height, width = SELFTEST_SIZE
    rng = np.random.default_rng(20261024)
    cases = [("一様な層", np.full((height, width), 0.27), True),
             ("雲なし", np.zeros((height, width)), True),
             ("細かなノイズ", rng.uniform(0.0, 1.0, (height, width)), True)]
    # 粗い斑点: 不透明度 0.7 の独立した斑点 10 個（縁が急）。空の 30 % ほどを覆い、切れ間の割合は見本の幅に入る大きさ
    centers = [(200 + 380 * (i % 5), 280 + 520 * (i // 5)) for i in range(10)]
    spots = np.where(disk_distance(height, width, centers) <= 140.0, 0.7, 0.0)
    cases.append(("粗い斑点", spots, True))
    # 穴の開いた膜: 不透明度 0.2 の膜に、半径 60 画素の穴を 40 個
    holes = [(rng.uniform(0, width), rng.uniform(0, height)) for _ in range(40)]
    cases.append(("穴の開いた膜", np.where(disk_distance(height, width, holes) <= 60.0, 0.0, 0.2), True))
    return cases


def known_edge_case():
    """縁の不透明度が半径方向に直線で 0 から 1 まで上がる円盤（上がる幅 RAMP_PX）。0.1 から 0.5 までの幅は 0.4 × RAMP_PX"""
    height, width = SELFTEST_SIZE
    ramp = 50.0
    distance = disk_distance(height, width, [(width * 0.5, height * 0.5)])
    opacity = np.clip((300.0 - distance) / ramp, 0.0, 1.0)
    return opacity, 0.4 * ramp


def straight_edge_cases():
    """(名前, 不透明度, まっすぐな縁の割合の下限, 上限)。四角い雲（画面の縦横に沿った正方形）と丸い雲（円盤）で、
    縁は 12 画素で 0 から 0.9 まで直線で上がる"""
    height, width = SELFTEST_SIZE
    ys, xs = np.mgrid[0:height, 0:width]
    centers = [(220 + 370 * (i % 5), 270 + 540 * (i // 5)) for i in range(10)]
    square = np.full((height, width), np.inf)
    for cx, cy in centers:
        square = np.minimum(square, np.maximum(np.abs(xs + 0.5 - cx), np.abs(ys + 0.5 - cy)))
    squares = np.clip((110.0 - square) / 12.0, 0.0, 1.0) * 0.9
    disks = np.clip((60.0 - disk_distance(height, width, centers)) / 12.0, 0.0, 1.0) * 0.9
    return [("四角い雲", squares, 0.8, None), ("丸い雲", disks, None, 0.05)]


def run_selftest(rc, repo_root, expect_path):
    failures = []
    spec = load_json(expect_path)
    height, width = SELFTEST_SIZE
    sky = np.ones((height, width), dtype=bool)
    for name, opacity, low, high in straight_edge_cases():
        fraction, detail = measure_straight_edges(opacity, sky)
        ok = fraction is not None and (low is None or fraction >= low) and (high is None or fraction <= high)
        print(f"REFERENCE_CHECK cloud_shape_selftest case={name} straight_edge_fraction={fraction} "
              f"min={low} max={high} edge_length_px={detail['edge_length_px']} pass={int(ok)}")
        if not ok:
            failures.append(f"{name}: まっすぐな縁の割合 {fraction}（下限 {low}・上限 {high}）")
    for name, opacity, should_fail in synthetic_cases():
        values, _, _ = measure(opacity, sky)
        _, row_failures = evaluate_expect(values, spec)
        ok = bool(row_failures) == should_fail
        print(f"REFERENCE_CHECK cloud_shape_selftest case={name} expect_failures={len(row_failures)} pass={int(ok)} "
              f"{summarize(values)}")
        for failure in row_failures:
            print(f"REFERENCE_CHECK cloud_shape_selftest case={name} row_failure: {failure}")
        if not ok:
            failures.append(f"{name}: 仕様で{'落ちない' if should_fail else '落ちる'}")
    opacity, expected = known_edge_case()
    values, detail, _ = measure(opacity, sky)
    width_px = values["edge_width_median_px"]
    ok = width_px is not None and abs(width_px - expected) <= 1.0 and (values["edge_measured_fraction"] or 0) >= 0.9
    print(f"REFERENCE_CHECK cloud_shape_selftest case=既知の幅の縁 expected_px={expected} "
          f"measured_px={width_px} measured_fraction={values['edge_measured_fraction']} pass={int(ok)}")
    if not ok:
        failures.append(f"既知の幅の縁: {width_px} 画素（期待 {expected} ± 1、測れた縁の割合 {values['edge_measured_fraction']}）")

    # 見本のマスク: マスクで測れる行を見本がすべて通る
    # 投影の視点は仕様に写した cloud_sky のカメラと太陽の向き（撮影の置き場は git 管理外なので仕様に持つ）
    sample_view = spec.get("sample_view")
    samples = []
    if not sample_view:
        failures.append("仕様に見本を投影する視点（sample_view）が無い")
    else:
        samples = sample_masks(repo_root, sample_view, sample_view["sun_direction"])
    if not samples:
        failures.append(f"見本の HDRI が無い（python Scripts/Terrain/FetchSkySamples.py で {SAMPLE_DIR} へ取得する）")
    mask_spec = {"rows": [row for row in spec.get("rows", []) if row.get("quantity") in MASK_QUANTITIES]}
    mask_dir = os.path.join(repo_root, SAMPLE_DIR, "masks")
    os.makedirs(mask_dir, exist_ok=True)
    sample_values = {}
    for name, opacity, sample_sky, color in samples:
        values, detail, shape = measure(opacity, sample_sky, edges=False)
        sample_values[name] = {key: values[key] for key in MASK_QUANTITIES}
        _, row_failures = evaluate_expect(values, mask_spec)
        write_sample_preview(os.path.join(mask_dir, f"{name.replace('@', '_')}.png"), color, opacity, sample_sky, shape)
        print(f"REFERENCE_CHECK cloud_shape_sample name={name} sky_pixels={detail['sky_pixels']} "
              f"pass={int(not row_failures)} " + " ".join(f"{k}={sample_values[name][k]}" for k in MASK_QUANTITIES))
        failures.extend(f"見本 {name}: {f}" for f in row_failures)
    with open(os.path.join(mask_dir, "sample_values.json"), "w", encoding="utf-8") as f:
        json.dump(sample_values, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=cloud_shape_selftest samples={len(samples)} expect={expect_path}")
    return 0 if not failures else 1
