# 崖の棚・角・外形を、写真の注釈（ShapeAnnotations.json）とエンジンの深度と幾何の法線で測る（CompareReference.py --shape。
# 単独では動かさない）。記録は Docs/RenderingValidation/WastelandShape.md の「形の物差し」。
#
#   python Scripts/Terrain/CompareReference.py --capture <-Aov の撮影> --photos far_01,... --shape [--terrain-only] [--expect <仕様>] [--verify]
#   python Scripts/Terrain/CompareReference.py --shape --selftest [--out <出力先>]
#
# 撮影の側は <視点>.aov.depth.pfm と <視点>.aov.geometric_normal.pfm（可視バッファの解決が書く三角形の面の法線。ShapeCause.py と
# 同じ詰め方）だけを読み、輝度は使わない（材質の縞・影・露出で段が作れ、照明の向きで正しい棚にも陰が出ないため）。
# --terrain-only は <視点>.aov.terrain_id.pfm（地形 1・ほか 0・書かれていない 255）も読み、地形の画素どうしの折れと、地形と空の
# 外形の折れだけを数える（崖錐の岩の縁・岩と地形の境を棚に数えない）。
# 折れの画素: 横・縦・2 つの斜めの向きに 1 画素ずつ前後の 2 点で、幾何の法線の角が kFoldDegrees 以上か、深度の段（depth_step）か、
#   片方だけが空（外形）。三角形の継ぎ目の両側の 2 画素の幅の線になる。
# 折れの向き: 面の法線・地形の印・深度の対数の Sobel の勾配の構造テンソルを kTensorRadius の箱で均し、主の向きに直交する向きを
#   折れの線の向き（0〜180°）とする。向きのまとまり（coherence）が kCoherenceMin 未満の画素（交わり・点の集まり）は向きを持たない。
#   さらに線の向きに ±kLineHalfPx の点のうち kLineMinHits 以上が向きを持つ折れの画素だけを「折れの線」に残す（点の雑音を外す）。
# 棚の折れ: 折れの線に、遠くで数画素の斜面に丸まる段の縁を足す（rounded_folds）。4 つの向きの ±kShelfSpanHalfPx（区間 6 画素）で
#   隣どうしの法線の角を積み上げた曲がりが kFoldDegrees 以上か両端の深度の段で、積み上げが向きに沿って前後以上の画素（曲がりの中心）。
#   向きと点の雑音の外し方は折れの線と同じ。棚の一致・局所の欠け・余分な折れはこの棚の折れで数える。
# 角の折れ: 折れの線に、空との縁（外形の輪郭。地形の画素で 4 近傍に空がある画素）を足す（contour_orientation）。輪郭の画素の向きは
#   法線や深度の勾配でなく、半径 kContourRadiusPx の箱の輪郭の画素の位置の主成分の軸（1 m の格子の段で輪郭が細かく折れても、
#   輪郭が角の点の近くで注釈の二辺の向きに曲がればその辺を支える）。角の一致はこの角の折れで数える。
# 写真と撮影の対応: 崖ごとに、写真の外形（多角形の列ごとの上の縁）と撮影の外形（列ごとに深度 depth_max_m 以下の最も上の画素）の
#   距離の中央値が最小になる平行移動を ±kSearchFraction（注釈の search_fraction で崖ごとに広げられる）の内で探し、棚と角の注釈をその分ずらして撮影に置く。
# 量（距離と長さは撮影の 1920 幅の画素に直す）:
#   shelf_match: 棚の線分の 1 画素ごとの標本のうち、線分に垂直に ±kMatchRadiusPx の内に、向きが線分と kOrientationDegrees 以内の
#     折れの線がある標本が、線分に沿って kRunMinPx 以上続く所の長さの割合
#   shelf_gap_max_px: 棚の線分に沿った一致の欠け（上の続きに数えない所）の最長（局所の欠け。崖・写真では全部の棚の最大）
#   shelf_extra_ratio: 棚の線分の周り（線分に垂直に ±kExtraBandFactor × 許容、線分の長さの内）の折れの線のうち、どの注釈の線
#     （棚・外形の辺・角の二辺）とも対応しない（許容の内で向きが kOrientationDegrees 以内でない）長さ ÷ 棚の線分の長さ。
#     長さは画素の数 ÷ 折れの線の幅（kFoldThicknessPx）
#   corner_match: 角のうち、角の点から半径 kCornerSearchFactor × 許容の内で、注釈の二辺の向きとそれぞれ kOrientationDegrees 以内の
#     折れの線が kCornerEdgeMinPixels 画素以上あり、二辺の線（画素の重心と主の向き）の交点が角の点から kCornerMeetFactor × 許容の内で、
#     交点を中心とした同じ半径の円の内で、二辺の向きの画素のそれぞれ kCornerRayFraction 以上が交点から注釈の辺の側にある
#     （角は二本の半直線。縦の溝と横の折れが交わる十字は、交点が角の点からずれていても通らない）割合
#   outline_distance_median_px: 写真の外形の点から撮影の外形への距離の中央値（ずらす前）。
#     outline_residual_median_px（ずらした後）・shift_px（ずらした量）も書く
# 内部の折れ線（注釈の崖の folds。種類 kind は FOLD_KINDS の壁の足〔壁の足元から崖錐へ移る境〕と塊の稜線〔崩れた塊の稜線〕、
#   region は ShapeRegions.json の崖の生成の担当の範囲の ID、points は折れ線の頂点）: 棚と同じ折れ（棚の折れ = 面の法線の向きの
#   変化と深度の段）と同じ量で対応づける。fold_match（折れ線の 1 画素ごとの標本の一致を、頂点をまたいで続く kRunMinPx 以上の
#   所だけ数えた長さの割合）・fold_gap_max_px（折れ線に沿った一致の欠けの最長）・fold_extra_ratio（折れ線の周りの帯の、どの
#   注釈の線〔棚・外形の辺・角の二辺・内部の折れ線〕とも対応しない折れの線の長さ ÷ 折れ線の長さ。頂点で重なる帯の画素は 1 回）。
#   崖・写真ごとの全部（fold_*）、種類ごと（fold_kinds.<種類>）、範囲ごと（写真の fold_regions.<ID>）、線ごと（崖の fold_lines）に書く。
#   棚の余分な折れの対応の線には内部の折れ線を入れない（棚の量は内部の折れ線の注釈で変わらない）
# 写真ごとの値は崖をまとめた値（棚は長さ、角は点の数、外形は点の数で重み付け。局所の欠けは最大）。崖ごとの値は cliffs.<名前>。
# 出力: <出力先>/shape_<写真>.png（上: 写真と注釈、下: 撮影と折れの線〔赤〕・撮影の外形〔水色〕・ずらした注釈〔緑 = 一致、
#   橙 = 不一致〕）、shape_metrics.json。
# --expect の仕様（PhotoExpect.py）は注釈の版 annotations_sha256（ShapeAnnotations.json から内部の折れ線〔崖の folds と
#   _folds_comment〕を除き、鍵を並べた正規の JSON の sha256。annotations_hash() が求める）を持ち、無いか今の注釈と違えば落ちる。
#   内部の折れ線の量（fold_ で始まる量）の行を持つ仕様は、さらに折れ線の版 fold_annotations_sha256（folds_hash()）が要る。
# --selftest: 合成の深度と法線（1920×1080 の正面の壁、棚 3 本、上の 2 つの角）で量を求め、SELFTEST_ROWS で判定する。
#   正しい形を許容の内でずらしたものと 5 画素の幅で丸まった段の縁・両端の面が平行に戻る丸まった段は通り、壁一面の鋸歯・格子・棚を 1 本消した形・角の点で縦の溝と
#   横の折れが十字に交わる形・滑らかな斜面・平らな面は落ちる。角の判定だけの例（CORNER_PROBE_CASES）で、角の点から左上へずれた十字と
#   まっすぐな空との縁は角に数えず、同じだけずれた正しい角と、細かく段になった空との縁で曲がる角は数えることも確かめる。
# 落ちる: 注釈が無い・崖に外形・棚・角（二辺の向き）が無い・撮影の PNG・深度・幾何の法線（--terrain-only は地形の印）が無いか
#   1920×1080 未満・地形の印に描いた画素の 255 がある・撮影の外形が取れない・崖の外形の中で幾何の法線の読める画素が
#   kGeometricCoverage 未満・--verify で所見の文書に「形の物差し」の節が無い・--expect の仕様の注釈の版が無いか違う・
#   内部の折れ線の種類が FOLD_KINDS に無いか範囲が崖の生成の担当の範囲の ID でないか頂点が 2 つ未満・--verify で FOLD_PHOTOS の
#   写真に内部の折れ線が無い。
import hashlib
import json
import os

import numpy as np
from PIL import Image, ImageDraw

from CompareLight import load_font
from CompareReference import MIN_CAPTURE_SIZE, PANEL_SIZE, PHOTO_DIR, PHOTOS, REPO_ROOT, fit_photo
from ReferenceCheck import read_pfm_any
from ShapeCause import decode_geometric_normal

ANNOTATIONS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ShapeAnnotations.json")
REGIONS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ShapeRegions.json")
# 内部の折れ線の種類（壁の足: 壁の足元から崖錐へ移る境、塊の稜線: 崩れた塊の稜線）と、置く範囲の担当
FOLD_KINDS = ("wall_foot", "block_ridge")
FOLD_KIND_LABELS = {"wall_foot": "壁の足", "block_ridge": "塊の稜線"}
# 重ね画の写真の側の内部の折れ線の色（壁の足 = 白、塊の稜線 = 薄い紫）
FOLD_COLORS = {"wall_foot": (255, 255, 255), "block_ridge": (200, 160, 255)}
FOLD_REGION_OWNER = "cliff_generation"
# --verify で内部の折れ線の注釈が要る写真
FOLD_PHOTOS = ("far_01", "far_02", "ground_02")
DOC = os.path.join("Docs", "RenderingValidation", "WastelandShape.md")
DOC_HEADING = "## 形の物差し"
REPORT = "shape_metrics.json"
SELFTEST_DEFAULT_OUT = os.path.join(".harness", "runs", "shape-selftest")
# 画素の大きさの基準の幅（撮影がこれと違えば距離をこの幅に直す）
kReferenceWidth = 1920
kFoldDegrees = 30.0
kDepthStepRatio = 0.05
# 写真と撮影の位置の許容[1920 幅の画素]。今の撮影（SCN-WL-SHAPE-CAUSE の lod-default）で、崖ごとにずらした後の外形の距離の
# 距離の中央値（全部の崖の点をまとめて 7.1 画素）を切り上げて決めた（WastelandShape.md の「形の物差し」）
kMatchRadiusPx = 8
# 注釈の線と撮影の折れの線の向きの許容[°]
kOrientationDegrees = 20.0
# 棚の一致に数える続きの長さの下限[1920 幅の画素]
kRunMinPx = 8
# 余分な折れを数える帯の半幅（許容の倍数）
kExtraBandFactor = 3
# 折れの線の幅[画素]（1 画素の前後で測るので継ぎ目の両側の 2 画素）。余分な折れの長さ = 画素の数 ÷ この幅
kFoldThicknessPx = 2.0
kTensorRadius = 2
kCoherenceMin = 0.5
kLineHalfPx = 5
kLineMinHits = 8
# 深度の対数の勾配の大きさの上限（外形の飛びが向きを支配しないように）
kDepthGradientClip = 2.0
kCornerSearchFactor = 2.0
kCornerMeetFactor = 1.5
kCornerEdgeMinPixels = 6
kCornerRayFraction = 0.75
# 交点の手前に許す画素（線の幅と丸めの分）
kCornerRaySlackPx = 2.0
# 平行移動を探す範囲（幅・高さに対する割合）。構図の違いが大きい崖は注釈の search_fraction で広げる
kSearchFraction = (0.06, 0.08)
kGeometricCoverage = 0.95
# 棚の折れに足す丸まった段の縁の区間の半分[画素]（区間 6 画素。540 m 先の 1 m の格子で 1 升目の斜面に丸まる 2.5 m の段の縁は約 5 画素）
kShelfSpanHalfPx = 3
# 空との縁の向きを決める輪郭の画素の箱の半径[画素]
kContourRadiusPx = 4
DIRECTIONS = {"x": (1, 0), "y": (0, 1), "d1": (1, 1), "d2": (1, -1)}
# --photos を省いたときに測る写真（注釈はこの全部に要る）
SHAPE_PHOTOS = ("far_01", "far_02", "cliff_01", "cliff_02", "ground_02")
# --selftest の判定の行（量・下限・上限）。棚と角は shape-target.json の絶対の基準、余分な折れは棚の長さと同じまで、局所の欠けは
# 余分な折れの帯の幅（許容の 3 倍）まで
SELFTEST_ROWS = (
    ("shelf_match", 0.6, None),
    ("corner_match", 0.5, None),
    ("shelf_extra_ratio", None, 1.0),
    ("shelf_gap_max_px", None, 24.0),
)


def fmt(value, digits=3):
    return "none" if value is None else f"{value:.{digits}f}"


def fmt_display(value, digits=2):
    """重ね画像の文字（人が読む）。値が無いときは「－」"""
    return "－" if value is None else f"{value:.{digits}f}"


def load_annotations():
    with open(ANNOTATIONS_PATH, encoding="utf-8-sig") as file:
        return json.load(file)["photos"]


def canonical_hash(data):
    canonical = json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def annotations_hash():
    """注釈の版: ShapeAnnotations.json から内部の折れ線（崖の folds と _folds_comment）を除き、鍵を並べた正規の JSON の
    sha256（行末・字下げに依らない。外形・棚・角の注釈の仕様は折れ線の注釈を足しても版が変わらない）"""
    with open(ANNOTATIONS_PATH, encoding="utf-8-sig") as file:
        data = json.load(file)
    data.pop("_folds_comment", None)
    for photo in data["photos"].values():
        for cliff in photo.get("cliffs", []):
            cliff.pop("folds", None)
    return canonical_hash(data)


def folds_hash():
    """内部の折れ線の版: 写真・崖の名前ごとの folds の正規の JSON の sha256"""
    folds = {photo_id: {cliff["name"]: cliff["folds"] for cliff in photo.get("cliffs", []) if cliff.get("folds")}
             for photo_id, photo in load_annotations().items()}
    return canonical_hash({photo_id: entry for photo_id, entry in folds.items() if entry})


def fold_regions():
    """内部の折れ線を置ける範囲の ID（ShapeRegions.json の崖の生成の担当）"""
    with open(REGIONS_PATH, encoding="utf-8-sig") as file:
        regions = json.load(file)["regions"]
    return {region["id"] for region in regions if region.get("owner") == FOLD_REGION_OWNER}


def check_folds(annotation, photo_id, allowed_regions):
    """注釈の内部の折れ線の形の確かめ（種類・範囲・頂点）。失敗の一覧"""
    failures = []
    for cliff in annotation["cliffs"]:
        for index, fold in enumerate(cliff.get("folds", [])):
            where = f"{photo_id} の {cliff.get('name', '?')} の内部の折れ線 {index}（{fold.get('label', '?') if isinstance(fold, dict) else '?'}）"
            if not isinstance(fold, dict):
                failures.append(f"{where} が辞書でない")
                continue
            if fold.get("kind") not in FOLD_KINDS:
                failures.append(f"{where} の種類 {fold.get('kind')} が {list(FOLD_KINDS)} に無い")
            if fold.get("region") not in allowed_regions:
                failures.append(f"{where} の範囲 {fold.get('region')} が崖の生成の担当の範囲（ShapeRegions.json）に無い")
            points = fold.get("points")
            if not isinstance(points, list) or len(points) < 2 or not all(
                    isinstance(p, list) and len(p) == 2 and all(isinstance(v, (int, float)) and 0.0 <= v <= 1.0 for v in p)
                    for p in points):
                failures.append(f"{where} の頂点が 2 つ未満か、0〜1 の [x, y] でない")
    return failures


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


def box_mean(array, radius):
    """(2 radius + 1) 四方の箱の平均（端は内側だけ）"""
    result = array
    for axis in (0, 1):
        padded = np.concatenate([np.zeros_like(np.take(result, [0], axis=axis)), np.cumsum(result, axis=axis)], axis=axis)
        size = result.shape[axis]
        upper = np.clip(np.arange(size) + radius + 1, 0, size)
        lower = np.clip(np.arange(size) - radius, 0, size)
        result = (np.take(padded, upper, axis=axis) - np.take(padded, lower, axis=axis)) / np.expand_dims(
            (upper - lower).astype(np.float64), axis=1 - axis)
    return result


def sobel(channel):
    gx = (shifted(channel, 1, 0, 0.0) - shifted(channel, -1, 0, 0.0))
    gy = (shifted(channel, 0, 1, 0.0) - shifted(channel, 0, -1, 0.0))
    gx = gx * 2.0 + shifted(gx, 0, 1, 0.0) + shifted(gx, 0, -1, 0.0)
    gy = gy * 2.0 + shifted(gy, 1, 0, 0.0) + shifted(gy, -1, 0, 0.0)
    return gx, gy


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


def fold_field(depth, normal, readable, ground=None):
    """折れの線の画素と、その線の向き[°、0〜180。画像の座標で x 右・y 下]。ground があれば地形の画素だけで数える"""
    terrain = np.isfinite(depth) & (depth > 0.0)
    surface = terrain if ground is None else terrain & ground
    sky = ~terrain
    safe = np.where(terrain, depth, 0.0)
    cos_limit = np.cos(np.radians(kFoldDegrees))
    raw = np.zeros(depth.shape, dtype=bool)
    for ux, uy in DIRECTIONS.values():
        surface_a, surface_b = shifted(surface, ux, uy, False), shifted(surface, -ux, -uy, False)
        depth_a, depth_b = shifted(safe, ux, uy, 0.0), shifted(safe, -ux, -uy, 0.0)
        read_a, read_b = shifted(readable, ux, uy, False), shifted(readable, -ux, -uy, False)
        dot = np.sum(shifted(normal, ux, uy, 0.0) * shifted(normal, -ux, -uy, 0.0), axis=2)
        both = surface_a & surface_b
        bend = both & read_a & read_b & (dot <= cos_limit)
        step = both & depth_step(terrain, safe, ux, uy, depth_a, depth_b)
        silhouette = (surface_a & shifted(sky, -ux, -uy, False)) | (surface_b & shifted(sky, ux, uy, False))
        raw |= bend | step | silhouette
    # 向き: 面の法線（読めない・数えない画素は 0）・地形の印・深度の対数の勾配の構造テンソル
    valid = surface & readable
    j11 = np.zeros(depth.shape)
    j12 = np.zeros(depth.shape)
    j22 = np.zeros(depth.shape)
    channels = [np.where(valid, normal[:, :, k], 0.0) for k in range(3)] + [surface.astype(np.float64)]
    for channel in channels:
        gx, gy = sobel(channel)
        j11 += gx * gx
        j12 += gx * gy
        j22 += gy * gy
    gx, gy = sobel(np.where(surface, np.log(np.maximum(safe, 1e-3)), 0.0))
    gx, gy = np.where(surface, gx, 0.0), np.where(surface, gy, 0.0)
    magnitude = np.hypot(gx, gy)
    clip = np.minimum(1.0, kDepthGradientClip / np.maximum(magnitude, 1e-9))
    gx, gy = gx * clip, gy * clip
    j11 += gx * gx
    j12 += gx * gy
    j22 += gy * gy
    j11, j12, j22 = (box_mean(value, kTensorRadius) for value in (j11, j12, j22))
    trace = j11 + j22
    coherence = np.sqrt((j11 - j22) ** 2 + 4.0 * j12 * j12) / np.maximum(trace, 1e-12)
    crossing = 0.5 * np.degrees(np.arctan2(2.0 * j12, j11 - j22))
    angle = (crossing + 90.0) % 180.0
    coherent = (coherence >= kCoherenceMin) & (trace > 1e-9)
    fold = line_pixels(raw & coherent, angle)
    soft = rounded_folds(terrain, surface, safe, normal, readable)
    contour, contour_angle = contour_orientation(surface, sky)
    return {"fold": fold, "angle": angle, "raw": raw,
            "shelf_fold": fold | line_pixels((raw | soft) & coherent, angle),
            "corner_fold": fold | contour, "corner_angle": np.where(contour, contour_angle, angle)}


def line_pixels(oriented, angle):
    """線の向きに ±kLineHalfPx の点のうち kLineMinHits 以上が向きを持つ折れの画素だけを残す"""
    height, width = oriented.shape
    ys, xs = np.nonzero(oriented)
    radians = np.radians(angle[ys, xs])
    hits = np.zeros(len(ys), dtype=np.int32)
    for k in range(-kLineHalfPx, kLineHalfPx + 1):
        sx = np.rint(xs + k * np.cos(radians)).astype(np.int64)
        sy = np.rint(ys + k * np.sin(radians)).astype(np.int64)
        inside = (sx >= 0) & (sx < width) & (sy >= 0) & (sy < height)
        hit = np.zeros(len(ys), dtype=bool)
        hit[inside] = oriented[sy[inside], sx[inside]]
        hits += hit
    result = np.zeros(oriented.shape, dtype=bool)
    keep = hits >= kLineMinHits
    result[ys[keep], xs[keep]] = True
    return result


def rounded_folds(terrain, surface, safe, normal, readable):
    """遠くで数画素の斜面に丸まる段の縁（棚の折れに足す）。4 つの向きの短い区間（±kShelfSpanHalfPx）で、隣どうしの画素の
    法線の角を区間に沿って積み上げた曲がりが kFoldDegrees 以上（区間の全部の画素が読める所。両端が平行に戻る丸まった段も
    数える）か、両端の深度の段（depth_step）の画素のうち、積み上げ（法線は曲がりの角、深度は区間を 1〜kShelfSpanHalfPx 画素に
    広げた両端の対数の比の和）が向きに沿って前後の画素以上の所だけを残す（曲がりの中心の 1〜2 画素の線にする）"""
    span = kShelfSpanHalfPx
    log_depth = np.log(np.maximum(safe, 1e-6))
    soft = np.zeros(safe.shape, dtype=bool)
    for ux, uy in DIRECTIONS.values():
        bend_score = np.zeros(safe.shape)
        depth_score = np.zeros(safe.shape)
        read = surface & readable
        for k in range(-span, span):
            read &= shifted(surface & readable, (k + 1) * ux, (k + 1) * uy, False)
            if k == -span:
                read &= shifted(surface & readable, k * ux, k * uy, False)
            dot = np.sum(shifted(normal, k * ux, k * uy, 0.0) * shifted(normal, (k + 1) * ux, (k + 1) * uy, 0.0), axis=2)
            bend_score += np.degrees(np.arccos(np.clip(dot, -1.0, 1.0)))
        bend_score = np.where(read, bend_score, 0.0)
        for k in range(1, span + 1):
            usable = surface & shifted(surface, k * ux, k * uy, False) & shifted(surface, -k * ux, -k * uy, False)
            depth_score += np.where(usable, np.abs(shifted(log_depth, k * ux, k * uy, 0.0) - shifted(log_depth, -k * ux, -k * uy, 0.0)), 0.0)
        ends = surface & shifted(surface, span * ux, span * uy, False) & shifted(surface, -span * ux, -span * uy, False)
        bend = read & (bend_score >= kFoldDegrees)
        step = ends & depth_step(terrain, safe, span * ux, span * uy, shifted(safe, span * ux, span * uy, 0.0),
                                 shifted(safe, -span * ux, -span * uy, 0.0))
        for flags, score in ((bend, bend_score), (step, depth_score)):
            peak = (score >= shifted(score, ux, uy, 0.0)) & (score >= shifted(score, -ux, -uy, 0.0))
            soft |= flags & peak
    return soft


def contour_orientation(surface, sky):
    """空との縁（外形の輪郭）の画素と、その向き[°、0〜180]。向きは輪郭の画素の位置を半径 kContourRadiusPx の箱で
    主成分に分けた主の軸（法線の折れと違い、1 m の格子の段で輪郭が細かく折れても位置の並びで決まる）。位置のまとまりが
    kCoherenceMin 未満の画素（角の頂・点の集まり）は向きを持たないので数えない"""
    contour = surface & (shifted(sky, 1, 0, False) | shifted(sky, -1, 0, False) | shifted(sky, 0, 1, False) | shifted(sky, 0, -1, False))
    height, width = contour.shape
    mask = contour.astype(np.float64)
    count = box_mean(mask, kContourRadiusPx)
    xs = np.arange(width, dtype=np.float64)[None, :] * np.ones((height, 1))
    ys = np.arange(height, dtype=np.float64)[:, None] * np.ones((1, width))
    safe = np.maximum(count, 1e-12)
    mx, my = box_mean(mask * xs, kContourRadiusPx) / safe, box_mean(mask * ys, kContourRadiusPx) / safe
    cxx = box_mean(mask * xs * xs, kContourRadiusPx) / safe - mx * mx
    cxy = box_mean(mask * xs * ys, kContourRadiusPx) / safe - mx * my
    cyy = box_mean(mask * ys * ys, kContourRadiusPx) / safe - my * my
    trace = cxx + cyy
    coherence = np.sqrt((cxx - cyy) ** 2 + 4.0 * cxy * cxy) / np.maximum(trace, 1e-12)
    angle = (0.5 * np.degrees(np.arctan2(2.0 * cxy, cxx - cyy))) % 180.0
    return contour & (coherence >= kCoherenceMin) & (trace > 1e-9), angle


def angle_difference(a, b):
    """線の向き（180° 周期）の差[°]"""
    difference = np.abs((np.asarray(a) - b) % 180.0)
    return np.minimum(difference, 180.0 - difference)


def line_angle(start, end):
    return float(np.degrees(np.arctan2(end[1] - start[1], end[0] - start[0])) % 180.0)


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


def shift_score(distances):
    """平行移動の良さ: 距離の中央値、同じなら平均（真っ直ぐな縁では中央値が縁に沿ったずれで変わらないため）"""
    return float(np.median(distances)), float(np.mean(distances))


def best_shift(photo_points, capture_points, size, search):
    """外形の距離の中央値が最小になる平行移動（画素）と、その中央値"""
    limits = (int(search[0] * size[0]), int(search[1] * size[1]))
    coarse_source = photo_points[:: max(1, len(photo_points) // 80)]
    coarse_target = capture_points[:: max(1, len(capture_points) // 800)]
    best = ((np.inf, np.inf), 0, 0)
    for dy in range(-limits[1], limits[1] + 1, 12):
        for dx in range(-limits[0], limits[0] + 1, 12):
            value = shift_score(nearest_distances(coarse_source + (dx, dy), coarse_target))
            if value < best[0]:
                best = (value, dx, dy)
    fine_source = photo_points[:: max(1, len(photo_points) // 400)]
    for step, radius in ((2, 12), (1, 2)):
        _, cx, cy = best
        best = ((np.inf, np.inf), cx, cy)
        for dy in range(cy - radius, cy + radius + 1, step):
            for dx in range(cx - radius, cx + radius + 1, step):
                value = shift_score(nearest_distances(fine_source + (dx, dy), capture_points))
                if value < best[0]:
                    best = (value, dx, dy)
    return (best[1], best[2]), float(np.median(nearest_distances(photo_points + best[1:], capture_points)))


def runs_of(flags):
    """真偽の並びの続き（始まり・長さ・値）"""
    runs = []
    start = 0
    for index in range(1, len(flags) + 1):
        if index == len(flags) or flags[index] != flags[start]:
            runs.append((start, index - start, bool(flags[start])))
            start = index
    return runs


def segment_hits(field, start, end, radius):
    """線分の 1 画素ごとの標本と、垂直に ±radius の内に向きが kOrientationDegrees 以内の折れの線がある標本の印"""
    start, end = np.asarray(start, dtype=np.float64), np.asarray(end, dtype=np.float64)
    length = float(np.linalg.norm(end - start))
    count = max(2, int(length) + 1)
    samples = start[None, :] + (end - start)[None, :] * np.linspace(0.0, 1.0, count)[:, None]
    direction = (end - start) / max(length, 1e-6)
    normal = np.array([-direction[1], direction[0]])
    fold, angle = field["shelf_fold"], field["angle"]
    height, width = fold.shape
    offsets = np.arange(-radius, radius + 1)
    points = samples[:, None, :] + offsets[None, :, None] * normal[None, None, :]
    xs = np.rint(points[..., 0]).astype(np.int64)
    ys = np.rint(points[..., 1]).astype(np.int64)
    inside = (xs >= 0) & (xs < width) & (ys >= 0) & (ys < height)
    hit = np.zeros(xs.shape, dtype=bool)
    aligned = fold[ys[inside], xs[inside]] & (angle_difference(angle[ys[inside], xs[inside]], line_angle(start, end))
                                             <= kOrientationDegrees)
    hit[inside] = aligned
    return samples, hit.any(axis=1), length


def kept_runs(raw_hit, spacing, run_min):
    """標本の一致の印のうち、続きの長さ（標本ごとの間隔 spacing の和）が run_min 以上の所と、一致の欠けの最長"""
    kept = np.zeros(len(raw_hit), dtype=bool)
    for begin, size, value in runs_of(raw_hit):
        if value and float(spacing[begin:begin + size].sum()) >= run_min:
            kept[begin:begin + size] = True
    gap = max([float(spacing[begin:begin + size].sum()) for begin, size, value in runs_of(kept) if not value], default=0.0)
    return kept, gap


def match_segment(field, start, end, radius, run_min):
    """棚の線分の 1 画素ごとの標本の一致（垂直に ±radius の内に、向きが kOrientationDegrees 以内の折れの線がある標本が
    run_min 画素以上続く所）と、一致の欠けの最長[撮影の画素]"""
    samples, raw_hit, length = segment_hits(field, start, end, radius)
    spacing = length / (len(raw_hit) - 1)
    kept = np.zeros(len(raw_hit), dtype=bool)
    for begin, size, value in runs_of(raw_hit):
        if value and size * spacing >= run_min:
            kept[begin:begin + size] = True
    gap = max([size * spacing for _, size, value in runs_of(kept) if not value], default=0.0)
    return samples, kept, length, gap


def match_polyline(field, points, radius, run_min):
    """内部の折れ線の一致。線分ごとの標本の印を頂点をまたいでつなぎ（頂点の重なる標本は後の線分の始点を外す）、続きと欠けを
    折れ線に沿って数える。(標本, 一致, 長さ, 欠けの最長)"""
    all_samples, all_hits, spacings, total = [], [], [], 0.0
    for index, (start, end) in enumerate(zip(points[:-1], points[1:])):
        samples, raw_hit, length = segment_hits(field, start, end, radius)
        first = 0 if index == 0 else 1
        all_samples.append(samples[first:])
        all_hits.append(raw_hit[first:])
        spacings.append(np.full(len(raw_hit) - first, length / (len(raw_hit) - 1)))
        total += length
    raw_hit = np.concatenate(all_hits)
    kept, gap = kept_runs(raw_hit, np.concatenate(spacings), run_min)
    return np.concatenate(all_samples), kept, total, gap


def annotation_lines(cliff, size, shift):
    """対応を取る注釈の線（棚・外形の辺・角の二辺）の (始点, 終点) の並び（撮影の画素、ずらした後）"""
    lines = [tuple(to_pixels([a, b], size, shift)) for a, b in cliff["shelves"]]
    outline = to_pixels(cliff["outline"], size, shift)
    lines += [(outline[i], outline[(i + 1) % len(outline)]) for i in range(len(outline))]
    for corner in cliff["corners"]:
        at = to_pixels([corner["at"]], size, shift)[0]
        lines += [(at, point) for point in to_pixels(corner["edges"], size, shift)]
    return lines


def line_coordinates(xs, ys, start, end):
    """画素の、線分に沿った位置（始点から）と線分からの垂直の距離"""
    start, end = np.asarray(start, dtype=np.float64), np.asarray(end, dtype=np.float64)
    length = max(float(np.linalg.norm(end - start)), 1e-6)
    direction = (end - start) / length
    rx, ry = xs - start[0], ys - start[1]
    return rx * direction[0] + ry * direction[1], np.abs(-rx * direction[1] + ry * direction[0]), length


def extra_folds(field, start, end, radius, all_lines):
    """棚の線分の周りの帯の、どの注釈の線とも対応しない折れの線の長さ[撮影の画素]"""
    return float(len(extra_fold_pixels(field, start, end, radius, all_lines))) / kFoldThicknessPx


def extra_fold_pixels(field, start, end, radius, all_lines):
    """線分の周りの帯の、どの注釈の線とも対応しない折れの線の画素（行 × 幅 + 列 の並び）"""
    band = kExtraBandFactor * radius
    fold, angle = field["shelf_fold"], field["angle"]
    height, width = fold.shape
    x0 = int(max(0, np.floor(min(start[0], end[0]) - band)))
    x1 = int(min(width, np.ceil(max(start[0], end[0]) + band) + 1))
    y0 = int(max(0, np.floor(min(start[1], end[1]) - band)))
    y1 = int(min(height, np.ceil(max(start[1], end[1]) + band) + 1))
    if x0 >= x1 or y0 >= y1:
        return np.zeros(0, dtype=np.int64)
    ys, xs = np.nonzero(fold[y0:y1, x0:x1])
    xs, ys = xs + x0, ys + y0
    along, across, length = line_coordinates(xs, ys, start, end)
    inside = (along >= 0.0) & (along <= length) & (across <= band)
    xs, ys = xs[inside], ys[inside]
    angles = angle[ys, xs]
    matched = np.zeros(len(xs), dtype=bool)
    for line_start, line_end in all_lines:
        along, across, length = line_coordinates(xs, ys, line_start, line_end)
        near = (along >= -radius) & (along <= length + radius) & (across <= radius)
        matched |= near & (angle_difference(angles, line_angle(line_start, line_end)) <= kOrientationDegrees)
    return ys[~matched].astype(np.int64) * width + xs[~matched].astype(np.int64)


def measure_fold(field, fold, size, shift, radius, run_min, fold_all_lines):
    """内部の折れ線ひとつ。(一致の長さ, 長さ, 余分な折れの長さ, 欠けの最長, 標本, 一致)[撮影の画素]"""
    points = to_pixels(fold["points"], size, shift)
    samples, kept, length, gap = match_polyline(field, points, radius, run_min)
    extra = np.unique(np.concatenate([extra_fold_pixels(field, a, b, radius, fold_all_lines)
                                      for a, b in zip(points[:-1], points[1:])]))
    return length * float(kept.mean()), length, float(len(extra)) / kFoldThicknessPx, gap, samples, kept


def fold_record(matched, length, extra, gap, count, scale):
    """内部の折れ線の量の記録（長さは撮影の画素、scale で 1920 幅に直す）"""
    return {"fold_match": matched / length if length else None,
            "fold_gap_max_px": gap * scale if length else None,
            "fold_extra_ratio": extra / length if length else None,
            "fold_length_px": length * scale, "folds": count}


def add_fold_totals(totals, key, matched, length, extra, gap):
    entry = totals.setdefault(key, [0.0, 0.0, 0.0, 0.0, 0])
    entry[0] += matched
    entry[1] += length
    entry[2] += extra
    entry[3] = max(entry[3], gap)
    entry[4] += 1


def match_corner(field, corner, size, shift, radius):
    """角の一致: 二辺の向きの折れの線が角の点の近くで交わる二本の半直線になっているか"""
    at = np.asarray(to_pixels([corner["at"]], size, shift)[0])
    fold, angle = field["corner_fold"], field["corner_angle"]
    height, width = fold.shape
    search = kCornerSearchFactor * radius
    x0, x1 = int(max(0, at[0] - search)), int(min(width, at[0] + search + 1))
    y0, y1 = int(max(0, at[1] - search)), int(min(height, at[1] + search + 1))
    if x0 >= x1 or y0 >= y1:
        return False, at
    ys, xs = np.nonzero(fold[y0:y1, x0:x1])
    xs, ys = (xs + x0).astype(np.float64), (ys + y0).astype(np.float64)
    disk = (xs - at[0]) ** 2 + (ys - at[1]) ** 2 <= search * search
    xs, ys = xs[disk], ys[disk]
    angles = angle[ys.astype(np.int64), xs.astype(np.int64)]
    edges = []
    for point in to_pixels(corner["edges"], size, shift):
        outward = np.asarray(point) - at
        outward /= max(np.linalg.norm(outward), 1e-6)
        pick = angle_difference(angles, line_angle(at, at + outward)) <= kOrientationDegrees
        if np.count_nonzero(pick) < kCornerEdgeMinPixels:
            return False, at
        coords = np.stack([xs[pick], ys[pick]], axis=1)
        center = coords.mean(axis=0)
        _, _, vectors = np.linalg.svd(coords - center, full_matrices=False)
        principal = vectors[0] if np.count_nonzero(pick) >= 2 else outward
        edges.append((center, principal, outward))
    (c1, v1, _), (c2, v2, _) = edges
    cross = v1[0] * v2[1] - v1[1] * v2[0]
    if abs(cross) < np.sin(np.radians(kOrientationDegrees)):
        return False, at
    # c1 + s v1 = c2 + t v2
    s = ((c2[0] - c1[0]) * v2[1] - (c2[1] - c1[1]) * v2[0]) / cross
    meet = c1 + s * v1
    if np.linalg.norm(meet - at) > kCornerMeetFactor * radius:
        return False, meet
    # 半直線の判定は交点を中心とした円で行う（角の点を中心にすると、交点がずれた十字の片側だけが多く入って通る）
    x0, x1 = int(max(0, np.floor(meet[0] - search))), int(min(width, np.ceil(meet[0] + search) + 1))
    y0, y1 = int(max(0, np.floor(meet[1] - search))), int(min(height, np.ceil(meet[1] + search) + 1))
    if x0 >= x1 or y0 >= y1:
        return False, meet
    ys, xs = np.nonzero(fold[y0:y1, x0:x1])
    xs, ys = (xs + x0).astype(np.float64), (ys + y0).astype(np.float64)
    disk = (xs - meet[0]) ** 2 + (ys - meet[1]) ** 2 <= search * search
    xs, ys = xs[disk], ys[disk]
    angles = angle[ys.astype(np.int64), xs.astype(np.int64)]
    for _, _, outward in edges:
        pick = angle_difference(angles, line_angle(meet, meet + outward)) <= kOrientationDegrees
        if np.count_nonzero(pick) < kCornerEdgeMinPixels:
            return False, meet
        coords = np.stack([xs[pick], ys[pick]], axis=1)
        ahead = (coords - meet) @ outward >= -kCornerRaySlackPx
        if np.count_nonzero(ahead) < kCornerRayFraction * len(coords):
            return False, meet
    return True, meet


def capture_outline(depth, depth_max, columns_range):
    """撮影の外形（列ごとに深度 depth_max 以下の最も上の画素）の点の並び"""
    height, width = depth.shape
    c0, c1 = max(0, columns_range[0]), min(width, columns_range[1])
    near = np.isfinite(depth[:, c0:c1]) & (depth[:, c0:c1] > 0.0) & (depth[:, c0:c1] <= depth_max)
    present = near.any(axis=0)
    columns = np.arange(c0, c1)[present]
    tops = near[:, present].argmax(axis=0)
    return columns, tops


def align_cliff(cliff, depth, size):
    """崖の外形の対応（ずらした量）。失敗なら (None, 理由)"""
    width, _ = size
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
    return {"shift": shift, "raw": raw, "residual": residual, "photo_points": photo_points,
            "capture_points": capture_points, "capture_outline": (columns, tops)}, None


def measure_cliff(cliff, alignment, depth, field, readable, size, scale, all_lines, fold_all_lines):
    """崖ひとつの記録と描画の材料。棚の余分な折れは all_lines、内部の折れ線の余分な折れは fold_all_lines（内部の折れ線を足した線）で数える"""
    shift = alignment["shift"]
    radius = int(np.ceil(kMatchRadiusPx / scale))
    run_min = kRunMinPx / scale
    shelves = []
    matched_length = total_length = extra_length = 0.0
    gap_max = 0.0
    for start, end in cliff["shelves"]:
        a, b = to_pixels([start, end], size, shift)
        samples, kept, length, gap = match_segment(field, a, b, radius, run_min)
        shelves.append((samples, kept))
        matched_length += length * float(kept.mean())
        total_length += length
        gap_max = max(gap_max, gap)
        extra_length += extra_folds(field, a, b, radius, all_lines)
    corners = [match_corner(field, corner, size, shift, radius) for corner in cliff["corners"]]
    # 内部の折れ線: 全部（"all"）・種類（"kind:<種類>"）・範囲（"region:<ID>"）ごとの [一致の長さ, 長さ, 余分, 欠け, 本数]
    fold_totals, fold_lines, fold_drawings = {}, [], []
    for fold in cliff.get("folds", []):
        matched, length, extra, gap, samples, kept = measure_fold(field, fold, size, shift, radius, run_min, fold_all_lines)
        for key in ("all", "kind:" + fold["kind"], "region:" + fold["region"]):
            add_fold_totals(fold_totals, key, matched, length, extra, gap)
        fold_lines.append({"label": fold.get("label", ""), "kind": fold["kind"], "region": fold["region"],
                           **fold_record(matched, length, extra, gap, 1, scale)})
        fold_drawings.append((samples, kept))
    _, _, inside = polygon_top(to_pixels(cliff["outline"], size, shift), size)
    terrain = inside & np.isfinite(depth) & (depth > 0.0)
    coverage = float(np.count_nonzero(terrain & readable)) / max(1, int(np.count_nonzero(terrain)))
    record = {
        "shelf_match": matched_length / total_length if total_length else None,
        "shelf_gap_max_px": gap_max * scale if total_length else None,
        "shelf_extra_ratio": extra_length / total_length if total_length else None,
        "shelf_length_px": total_length * scale,
        "shelves": len(cliff["shelves"]),
        "corner_match": (sum(1 for ok, _ in corners if ok) / len(corners)) if corners else None,
        "corners": len(corners),
        "outline_distance_median_px": alignment["raw"] * scale,
        "outline_residual_median_px": alignment["residual"] * scale,
        "outline_points": int(len(alignment["photo_points"])),
        "shift_px": [shift[0] * scale, shift[1] * scale],
        "geometric_coverage": coverage,
        "depth_max_m": cliff["depth_max_m"],
    }
    record.update(fold_records(fold_totals, scale))
    if fold_lines:
        record["fold_lines"] = fold_lines
    drawing = {"shelves": shelves, "corners": [(to_pixels([c["at"]], size, shift)[0], ok) for c, (ok, _) in zip(cliff["corners"], corners)],
               "capture_outline": alignment["capture_outline"], "folds": fold_drawings,
               "photo_outline_shifted": to_pixels(cliff["outline"], size, shift)}
    totals = {"matched": matched_length, "length": total_length, "extra": extra_length, "gap": gap_max,
              "corners": len(corners), "corner_hits": sum(1 for ok, _ in corners if ok), "folds": fold_totals}
    return record, drawing, totals


def fold_records(fold_totals, scale):
    """内部の折れ線の合計から、全部（fold_*）・種類ごと（fold_kinds）・範囲ごと（fold_regions）の記録。折れ線が無ければ空"""
    if "all" not in fold_totals:
        return {}
    result = fold_record(*fold_totals["all"][:4], fold_totals["all"][4], scale)
    result["fold_kinds"] = {key[5:]: fold_record(*value[:4], value[4], scale)
                            for key, value in fold_totals.items() if key.startswith("kind:")}
    result["fold_regions"] = {key[7:]: fold_record(*value[:4], value[4], scale)
                              for key, value in fold_totals.items() if key.startswith("region:")}
    return result


def measure_photo(annotation, depth, normal, readable, ground, size):
    """写真ひとつの量。(写真の記録, 崖の記録, 描画の材料, 外形の距離の材料, 失敗の一覧, 折れの場)"""
    field = fold_field(depth, normal, readable, ground)
    scale = kReferenceWidth / size[0]
    failures = []
    alignments = {}
    for cliff in annotation["cliffs"]:
        alignment, reason = align_cliff(cliff, depth, size)
        if alignment is None:
            failures.append(f"{cliff['name']}: {reason}")
            continue
        alignments[cliff["name"]] = alignment
    all_lines, fold_all_lines = [], []
    for cliff in annotation["cliffs"]:
        if cliff["name"] in alignments:
            shift = alignments[cliff["name"]]["shift"]
            all_lines += annotation_lines(cliff, size, shift)
            for fold in cliff.get("folds", []):
                points = to_pixels(fold["points"], size, shift)
                fold_all_lines += list(zip(points[:-1], points[1:]))
    fold_all_lines = all_lines + fold_all_lines
    cliffs, drawings, distances = {}, [], []
    totals = {"matched": 0.0, "length": 0.0, "extra": 0.0, "gap": 0.0, "corners": 0, "corner_hits": 0}
    fold_totals = {}
    for cliff in annotation["cliffs"]:
        alignment = alignments.get(cliff["name"])
        if alignment is None:
            continue
        record, drawing, cliff_totals = measure_cliff(cliff, alignment, depth, field, readable, size, scale, all_lines, fold_all_lines)
        cliffs[cliff["name"]] = record
        drawings.append(drawing)
        for key in ("matched", "length", "extra", "corners", "corner_hits"):
            totals[key] += cliff_totals[key]
        totals["gap"] = max(totals["gap"], cliff_totals["gap"])
        for key, (matched, length, extra, gap, count) in cliff_totals["folds"].items():
            entry = fold_totals.setdefault(key, [0.0, 0.0, 0.0, 0.0, 0])
            entry[0] += matched
            entry[1] += length
            entry[2] += extra
            entry[3] = max(entry[3], gap)
            entry[4] += count
        distances.append((nearest_distances(alignment["photo_points"], alignment["capture_points"]),
                          nearest_distances(alignment["photo_points"] + alignment["shift"], alignment["capture_points"])))
    if not cliffs:
        return None, cliffs, drawings, distances, failures, field
    photo_record = {
        "capture_size": list(size),
        "shelf_match": totals["matched"] / totals["length"] if totals["length"] else None,
        "shelf_gap_max_px": totals["gap"] * scale if totals["length"] else None,
        "shelf_extra_ratio": totals["extra"] / totals["length"] if totals["length"] else None,
        "corner_match": totals["corner_hits"] / totals["corners"] if totals["corners"] else None,
        "outline_distance_median_px": float(np.median(np.concatenate([raw for raw, _ in distances]))) * scale,
        "outline_residual_median_px": float(np.median(np.concatenate([res for _, res in distances]))) * scale,
        "cliffs": cliffs,
    }
    photo_record.update(fold_records(fold_totals, scale))
    return photo_record, cliffs, drawings, distances, failures, field


def draw_sheet(photo_panel, capture, field, annotation, drawings, size, lines, font):
    photo_image = photo_panel.copy()
    draw = ImageDraw.Draw(photo_image)
    for cliff in annotation["cliffs"]:
        outline = to_pixels(cliff["outline"], PANEL_SIZE)
        draw.line(outline + [outline[0]], fill=(0, 230, 255), width=2)
        for start, end in cliff["shelves"]:
            draw.line(to_pixels([start, end], PANEL_SIZE), fill=(255, 230, 0), width=3)
        for corner in cliff["corners"]:
            x, y = to_pixels([corner["at"]], PANEL_SIZE)[0]
            for ex, ey in to_pixels(corner["edges"], PANEL_SIZE):
                length = max(np.hypot(ex - x, ey - y), 1e-6)
                draw.line((x, y, x + (ex - x) / length * 18, y + (ey - y) / length * 18), fill=(255, 0, 220), width=2)
            draw.ellipse((x - 6, y - 6, x + 6, y + 6), outline=(255, 0, 220), width=3)
        for fold in cliff.get("folds", []):
            points = to_pixels(fold["points"], PANEL_SIZE)
            draw.line(points, fill=FOLD_COLORS[fold["kind"]], width=3)
            draw.text((points[-1][0] + 4, points[-1][1] - 10), FOLD_KIND_LABELS[fold["kind"]], fill=FOLD_COLORS[fold["kind"]], font=font)
        x, y = to_pixels([cliff["outline"][1]], PANEL_SIZE)[0]
        draw.text((x + 4, y - 24), cliff["name"], fill=(0, 230, 255), font=font)
    base = np.asarray(capture, dtype=np.float32) * 0.55
    folds = field["shelf_fold"] | field["corner_fold"]
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
            for (x, y), ok in list(zip(samples, hit))[::2]:
                draw.rectangle((x * sx - 1, y * sy - 1, x * sx + 1, y * sy + 1), fill=(0, 255, 90) if ok else (255, 150, 0))
        for (x, y), ok in drawing["corners"]:
            color = (0, 255, 90) if ok else (255, 150, 0)
            draw.ellipse((x * sx - 7, y * sy - 7, x * sx + 7, y * sy + 7), outline=color, width=3)
        # 内部の折れ線の標本は丸（白 = 一致、紫 = 不一致）で棚の四角と分ける
        for samples, hit in drawing.get("folds", []):
            for (x, y), ok in list(zip(samples, hit))[::2]:
                draw.ellipse((x * sx - 2, y * sy - 2, x * sx + 2, y * sy + 2), fill=(255, 255, 255) if ok else (190, 90, 255))
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


def check_spec_version(expect_path):
    """--expect の仕様の注釈の版（annotations_sha256）が今の注釈と同じか"""
    try:
        with open(expect_path, encoding="utf-8-sig") as file:
            spec = json.load(file)
    except (OSError, ValueError) as error:
        return [f"仕様 {expect_path} が読めない: {error}"]
    version = spec.get("annotations_sha256") if isinstance(spec, dict) else None
    current = annotations_hash()
    if not version:
        return [f"仕様 {expect_path} に注釈の版 annotations_sha256 が無い（今の注釈 {current}）"]
    if version != current:
        return [f"仕様 {expect_path} の注釈の版 {version} が今の注釈 {current} と違う"]
    print(f"SHAPE_MEASURE_EXPECT annotations_sha256={current} result=pass")
    rows = spec.get("rows", []) if isinstance(spec, dict) else []
    if any(isinstance(row, dict) and str(row.get("quantity", "")).startswith("fold_") for row in rows):
        fold_version = spec.get("fold_annotations_sha256")
        fold_current = folds_hash()
        if fold_version != fold_current:
            return [f"仕様 {expect_path} の内部の折れ線の版 {fold_version} が今の折れ線 {fold_current} と違うか無い"]
        print(f"SHAPE_MEASURE_EXPECT fold_annotations_sha256={fold_current} result=pass")
    return []


def describe(record):
    return (f"shelf_match={fmt(record['shelf_match'])} shelf_gap_max_px={fmt(record['shelf_gap_max_px'], 1)} "
            f"shelf_extra_ratio={fmt(record['shelf_extra_ratio'])} corner_match={fmt(record['corner_match'])}")


def describe_fold(record):
    return (f"fold_match={fmt(record['fold_match'])} fold_gap_max_px={fmt(record['fold_gap_max_px'], 1)} "
            f"fold_extra_ratio={fmt(record['fold_extra_ratio'])} fold_length_px={record['fold_length_px']:.0f} folds={record['folds']}")


def run_shape(capture_dir, out_dir, wanted, verify, expect_path, terrain_only=False):
    """--shape の本体。終了コードを返す"""
    annotations = load_annotations()
    font = load_font(18)
    failures = []
    report = {"thresholds": {"fold_degrees": kFoldDegrees, "depth_step_ratio": kDepthStepRatio,
                             "match_radius_px": kMatchRadiusPx, "orientation_degrees": kOrientationDegrees,
                             "run_min_px": kRunMinPx, "extra_band_factor": kExtraBandFactor,
                             "fold_thickness_px": kFoldThicknessPx, "coherence_min": kCoherenceMin,
                             "line_half_px": kLineHalfPx, "line_min_hits": kLineMinHits,
                             "corner_search_factor": kCornerSearchFactor, "corner_meet_factor": kCornerMeetFactor,
                             "corner_edge_min_pixels": kCornerEdgeMinPixels, "corner_ray_fraction": kCornerRayFraction,
                             "search_fraction": list(kSearchFraction), "reference_width": kReferenceWidth},
              "capture": os.path.relpath(capture_dir, REPO_ROOT), "terrain_only": terrain_only,
              "annotations_sha256": annotations_hash(), "fold_annotations_sha256": folds_hash(), "photos": {}}
    allowed_regions = fold_regions()
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
                      or "depth_max_m" not in cliff
                      or not all(isinstance(c, dict) and "at" in c and len(c.get("edges", [])) == 2 for c in cliff["corners"])]
        if incomplete:
            failures.append(f"{photo_id} の崖 {incomplete} に外形・角（点と二辺の向き）・深度の上限のどれかが無い")
            continue
        if not any(cliff["shelves"] for cliff in annotation["cliffs"]):
            failures.append(f"{photo_id} に棚の注釈が無い")
            continue
        fold_failures = check_folds(annotation, photo_id, allowed_regions)
        if fold_failures:
            failures.extend(fold_failures)
            continue
        if verify and photo_id in FOLD_PHOTOS and not any(cliff.get("folds") for cliff in annotation["cliffs"]):
            failures.append(f"{photo_id} に内部の折れ線（folds）の注釈が無い")
        paths = {
            "photo": os.path.join(PHOTO_DIR, photo_id + ".jpg"),
            "png": os.path.join(capture_dir, view + ".png"),
            "depth": os.path.join(capture_dir, view + ".aov.depth.pfm"),
            "geometric": os.path.join(capture_dir, view + ".aov.geometric_normal.pfm"),
        }
        if terrain_only:
            paths["terrain_id"] = os.path.join(capture_dir, view + ".aov.terrain_id.pfm")
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
        ground = None
        if terrain_only:
            terrain_id = np.rint(read_pfm_any(paths["terrain_id"])[:, :, 0]).astype(np.int64)
            drawn = np.isfinite(depth) & (depth > 0.0)
            if terrain_id.shape != depth.shape:
                failures.append(f"{view} の地形の印の大きさが深度と違う")
                continue
            unwritten = int(np.count_nonzero(drawn & (terrain_id == 255)))
            if unwritten:
                failures.append(f"{view} の地形の印に、このフレームに書かれていない描いた画素（255）が {unwritten} ある")
                continue
            ground = terrain_id == 1
        scale = kReferenceWidth / capture.width
        photo_record, cliffs, drawings, distances, cliff_failures, field = measure_photo(annotation, depth, normal, readable, ground, size)
        failures.extend(f"{photo_id} の {failure}" for failure in cliff_failures)
        for name, record in cliffs.items():
            if record["geometric_coverage"] < kGeometricCoverage:
                failures.append(f"{photo_id} の {name} の幾何の法線の被覆率 {record['geometric_coverage']:.3f} が {kGeometricCoverage} 未満")
            print(f"SHAPE_MEASURE photo={photo_id} cliff=\"{name}\" {describe(record)} "
                  f"outline_distance_median_px={record['outline_distance_median_px']:.1f} "
                  f"outline_residual_median_px={record['outline_residual_median_px']:.1f} "
                  f"shift_px={record['shift_px'][0]:.0f},{record['shift_px'][1]:.0f} coverage={record['geometric_coverage']:.3f}")
            for line in record.get("fold_lines", []):
                print(f"SHAPE_MEASURE_FOLD photo={photo_id} cliff=\"{name}\" kind={line['kind']} region={line['region']} "
                      f"label=\"{line['label']}\" {describe_fold(line)}")
        if photo_record is None:
            continue
        for kind, entry in photo_record.get("fold_kinds", {}).items():
            print(f"SHAPE_MEASURE_FOLD photo={photo_id} kind={kind} {describe_fold(entry)}")
        for region, entry in photo_record.get("fold_regions", {}).items():
            print(f"SHAPE_MEASURE_FOLD photo={photo_id} region={region} {describe_fold(entry)}")
        photo_record["view"] = view
        all_residuals.extend(res * scale for _, res in distances)
        report["photos"][photo_id] = photo_record
        print(f"SHAPE_MEASURE photo={photo_id} view={view} {describe(photo_record)} "
              f"outline_distance_median_px={photo_record['outline_distance_median_px']:.1f} "
              f"outline_residual_median_px={photo_record['outline_residual_median_px']:.1f}")
        photo_panel, _, _ = fit_photo(Image.open(paths["photo"]).convert("RGB"))
        lines = [f"上: 写真 {photo_id}（水色 = 外形、黄 = 棚、桃 = 角と二辺）　下: 撮影 {view}（{size[0]}×{size[1]}）の折れの線（赤）・"
                 f"撮影の外形（水色）・ずらした注釈（緑 = 一致、橙 = 不一致、黄 = 写真の外形）" + ("　地形の画素だけ" if terrain_only else "")]
        for name, record in cliffs.items():
            lines.append(f"{name}: 棚の一致 {fmt_display(record['shelf_match'])}（{record['shelves']} 本、局所の欠け "
                         f"{fmt_display(record['shelf_gap_max_px'], 0)} 画素、余分な折れ {fmt_display(record['shelf_extra_ratio'])}）　"
                         f"角の一致 {fmt_display(record['corner_match'])}（{record['corners']} 点）　外形の距離の中央値 "
                         f"{record['outline_distance_median_px']:.1f}（ずらした後 {record['outline_residual_median_px']:.1f}）")
            if record.get("fold_lines"):
                lines.append(f"{name} の内部の折れ線: 一致 {fmt_display(record['fold_match'])}（{record['folds']} 本、局所の欠け "
                             f"{fmt_display(record['fold_gap_max_px'], 0)} 画素、余分な折れ {fmt_display(record['fold_extra_ratio'])}）　" + "　".join(
                                 f"{FOLD_KIND_LABELS[kind]} {fmt_display(entry['fold_match'])}" for kind, entry in record["fold_kinds"].items()))
        if any(record.get("fold_lines") for record in cliffs.values()):
            lines.append("内部の折れ線: 写真の白 = 壁の足、薄い紫 = 塊の稜線。撮影の丸: 白 = 一致、紫 = 不一致（量は棚と同じ折れ・許容・向き・続き）")
        lines.append(f"折れ: 面の法線の角 {kFoldDegrees:.0f}° 以上か深度の段 {kDepthStepRatio * 100:.0f} % 以上（1 画素の前後。棚は "
                     f"{2 * kShelfSpanHalfPx} 画素の区間の丸まった縁も、角は空との縁の向きも数える）。"
                     f"許容 {kMatchRadiusPx} 画素・向き {kOrientationDegrees:.0f}°・続き {kRunMinPx} 画素（1920 幅）。輝度は使わない")
        draw_sheet(photo_panel, capture, field, annotation, drawings, size, lines, font).save(
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

        failures.extend(check_spec_version(expect_path))
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
          + (" terrain_only=1" if terrain_only else "") + (f" expect={expect_path}" if expect_path else ""))
    return 0


# --selftest の合成の場面: 1920×1080 の正面の壁（深度 500 m、x 600〜1319・y 300〜1079、ほかは空）。棚は y 500・700・900 の
# x 650〜1270 の線分、角は壁の上の左右（二辺は上の縁と横の縁）。壁の面の法線は (0, 0, 1)
kSelftestSize = (1920, 1080)
kSelftestWall = (600, 1320, 300)
kSelftestShelves = (500, 700, 900)
kSelftestShelfSpan = (650, 1270)
# 正しい形の棚: 注釈の線より kSelftestShelfOffset 画素下から 4 画素の踏み面（上へ 40° 傾けた法線）。折れは +3 と +7 画素で許容 8 の内
kSelftestShelfOffset = 3
kSelftestTreadPx = 4
# 丸まった段の縁: 注釈の線の kSelftestRoundStart 画素上から、法線が 1 画素に 9° ずつ 45° まで上へ傾き（5 画素）、また 0° へ戻る
# （5 画素）。1 画素の前後の差は 18° で、前の折れの判定（kFoldDegrees）では折れにならない
kSelftestRoundStart = 4
kSelftestRoundAngles = (9.0, 18.0, 27.0, 36.0, 45.0, 36.0, 27.0, 18.0, 9.0)
# 両端の面が平行に戻る丸まった段: 注釈の線の kSelftestStepStart 画素上から、法線が 1 画素に 8° ずつ 24° まで上へ傾き、また 0° へ
# 戻る。6 画素の区間の両端の法線の角はどこでも 24° 以下で、隣どうしの角の積み上げは中心で 48°
kSelftestStepStart = 2
kSelftestStepAngles = (8.0, 16.0, 24.0, 16.0, 8.0)
# 滑らかな斜面: 法線の上下の傾きが周期 kSelftestSlopePeriod 画素・振幅 kSelftestSlopeDegrees の正弦で変わる（6 画素の区間の
# 隣どうしの角の積み上げは最大 20°）
kSelftestSlopePeriod = 48
kSelftestSlopeDegrees = 25.0


def selftest_annotation():
    width, height = kSelftestSize
    left, right, top = kSelftestWall
    f = lambda x, y: [x / width, y / height]
    shelves = [[f(kSelftestShelfSpan[0], y), f(kSelftestShelfSpan[1], y)] for y in kSelftestShelves]
    corners = [{"at": f(left, top), "edges": [f(right, top), f(left, height)]},
               {"at": f(right, top), "edges": [f(left, top), f(right, height)]}]
    return {"cliffs": [{"name": "合成の壁", "depth_max_m": 1000,
                        "outline": [f(left, top), f(right, top), f(right, height), f(left, height)],
                        "shelves": shelves, "corners": corners}]}


def tilted(degrees_up):
    radians = np.radians(degrees_up)
    return np.array([0.0, -np.sin(radians), np.cos(radians)])


def selftest_scene(kind):
    """合成の深度・法線・読める印"""
    width, height = kSelftestSize
    left, right, top = kSelftestWall
    depth = np.full((height, width), np.inf)
    depth[top:, left:right] = 500.0
    normal = np.zeros((height, width, 3))
    normal[top:, left:right] = (0.0, 0.0, 1.0)
    wall = np.zeros((height, width), dtype=bool)
    wall[top:, left:right] = True
    rows = np.arange(height)[:, None] * np.ones((1, width), dtype=np.int64)
    cols = np.ones((height, 1), dtype=np.int64) * np.arange(width)[None, :]
    inner = wall & (cols >= left + 20) & (cols < right - 20)
    if kind in ("shifted", "missing_shelf", "cross_corner"):
        for index, y in enumerate(kSelftestShelves):
            if kind == "missing_shelf" and index == 1:
                continue
            first = y + kSelftestShelfOffset
            tread = inner & (rows >= first) & (rows < first + kSelftestTreadPx)
            normal[tread] = tilted(40.0)
    if kind == "rounded_shelf":
        for y in kSelftestShelves:
            for step, degrees in enumerate(kSelftestRoundAngles):
                normal[inner & (rows == y - kSelftestRoundStart + step)] = tilted(degrees)
    if kind == "rounded_step":
        for y in kSelftestShelves:
            for step, degrees in enumerate(kSelftestStepAngles):
                normal[inner & (rows == y - kSelftestStepStart + step)] = tilted(degrees)
    if kind == "smooth_slope":
        waves = kSelftestSlopeDegrees * np.sin(2.0 * np.pi * (rows - top) / kSelftestSlopePeriod)
        radians = np.radians(waves)
        normal[wall] = np.stack([np.zeros_like(radians), -np.sin(radians), np.cos(radians)], axis=2)[wall]
    if kind == "sawtooth":
        # 壁一面の鋸歯: 4 画素ごとに上と下へ 20° 傾いた面が交互に並ぶ（水平の折れが 4 画素おき）
        teeth = wall & (rows >= top + 8)
        normal[teeth & ((rows // 4) % 2 == 0)] = tilted(20.0)
        normal[teeth & ((rows // 4) % 2 == 1)] = tilted(-20.0)
    if kind == "grid":
        # 格子: 12 画素ごとに幅 2 画素の縦と横の溝（35° 傾いた面）
        grooves = wall & (rows >= top + 8) & (cols >= left + 8) & (cols < right - 8)
        normal[grooves & (rows % 12 < 2)] = tilted(35.0)
        side = np.array([np.sin(np.radians(35.0)), 0.0, np.cos(np.radians(35.0))])
        normal[grooves & (cols % 12 < 2)] = side
    if kind == "cross_corner":
        # 壁の上に奥の帯（深度 2000 m。外形を取る深度の上限 1000 m より奥なので外形は動かない）を左右へ 60 画素張り出して置き、
        # 壁の上の縁の深度の段が角の点の左右へ続く横の折れになる。角の点の上下へ続く縦の溝を足して十字に交わる形にする
        band = (rows >= top - 24) & (rows < top) & (cols >= left - 60) & (cols < right + 60)
        depth[band] = 2000.0
        normal[band] = (0.0, 0.0, 1.0)
        for x in (left, right):
            groove = (rows >= top - 24) & (rows < top + 40) & (cols >= x - 1) & (cols < x + 1)
            normal[groove] = (np.sin(np.radians(35.0)), 0.0, np.cos(np.radians(35.0)))
    readable = np.isfinite(depth)
    return depth, normal, readable


SELFTEST_CASES = (
    ("shifted", "正しい形を許容の内（棚を 3〜7 画素下）にずらした形", True),
    ("sawtooth", "壁一面の鋸歯（4 画素おきの水平の折れ）", False),
    ("grid", "格子（12 画素おきの縦と横の溝）", False),
    ("missing_shelf", "正しい形から棚を 1 本消した形（局所の欠け）", False),
    ("cross_corner", "角の点で縦の溝と横の折れが十字に交わる形（上の縁は外形でない）", False),
    ("rounded_shelf", "5 画素の幅で丸まった段の縁（1 画素の前後では 18° で前の折れの判定に掛からない）", True),
    ("rounded_step", "両端の面が平行に戻る丸まった段（法線が 0・8・16・24・16・8・0°。区間の両端の角は 24° 以下で、積み上げは 48°）", True),
    ("smooth_slope", "滑らかな斜面（法線の傾きが周期 48 画素で ±25° に波打つ。棚が出ない）", False),
    ("flat_face", "平らな面（棚が出ない）", False),
)


# 角の判定だけを小さな合成の場（kCornerProbeSize 四方、深度 500 m・正面の法線）で確かめる例。注釈の角は中央で二辺は右と下。
# 折れの交わりは角の点から左上へ kCornerProbeOffset 画素ずらす（交点は許容 kMatchRadiusPx × kCornerMeetFactor の内）
kCornerProbeSize = 128
kCornerProbeOffset = 6
CORNER_PROBE_CASES = (
    ("cross_offset", "左上へずれた十字（縦の溝と、その上が奥の深度の段）", False),
    ("corner_offset", "左上へずれた正しい角（右へ続く棚と下へ続く溝の二本の半直線）", True),
    ("sky_corner", "左上へずれた、空との縁で曲がる角（縁は 2〜4 画素の段で細かく折れ、面の法線の折れは無い。前の折れの向きでは数えない）", True),
    ("straight_sky", "まっすぐな空との縁（注釈の角の下の辺に折れも縁も無い）", False),
)
# 空との縁の段の高さ（列・行ごとに 0〜kCornerProbeJagPx 画素。決まった並び）
kCornerProbeJagPx = 4
kCornerProbeJag = (0, 2, 4, 2, 0, 0, 4, 4, 2, 0, 2, 4, 0, 2, 2, 4)


def corner_probe(kind):
    """角の判定の例ひとつ。(一致したか, 交点)"""
    size = kCornerProbeSize
    at = size // 2 - kCornerProbeOffset
    depth = np.full((size, size), 500.0)
    normal = np.zeros((size, size, 3))
    normal[:] = (0.0, 0.0, 1.0)
    side = (np.sin(np.radians(35.0)), 0.0, np.cos(np.radians(35.0)))
    if kind == "cross_offset":
        depth[:at, :] = 2000.0
        normal[:, at - 1:at + 1] = side
    elif kind == "corner_offset":
        normal[at - 1:at + 1, at - 1:] = tilted(40.0)
        normal[at - 1:, at - 1:at + 1] = side
    elif kind == "sky_corner":
        # 右下の壁（面の法線は一様）。上の縁は列ごとに、左の縁は行ごとに段の高さだけ壁の側へ下がる
        jag = np.asarray(kCornerProbeJag)
        index = np.arange(size)
        top = at + jag[(index // 2) % len(jag)]
        left = at + jag[(index // 3 + 5) % len(jag)]
        wall = (index[:, None] >= top[None, :]) & (index[None, :] >= left[:, None]) & (index[None, :] >= at) & (index[:, None] >= at)
        depth[~wall] = np.inf
    else:
        depth[:size // 2, :] = np.inf
    field = fold_field(depth, normal, np.isfinite(depth))
    corner = {"at": [0.5, 0.5], "edges": [[0.8, 0.5], [0.5, 0.8]]}
    matched, meet = match_corner(field, corner, (size, size), (0.0, 0.0), kMatchRadiusPx)
    return matched, meet


def selftest_rows(record):
    """SELFTEST_ROWS の判定。(全部の行が通ったか, 落ちた行の量の一覧, 行ごとの文字)"""
    failed, texts = [], []
    for quantity, low, high in SELFTEST_ROWS:
        value = record.get(quantity)
        ok = value is not None and (low is None or value >= low) and (high is None or value <= high)
        texts.append(f"{quantity}={fmt(value)}[{low},{high}]{'' if ok else '×'}")
        if not ok:
            failed.append(quantity)
    return not failed, failed, texts


def run_selftest(out_dir):
    """--shape --selftest の本体。終了コードを返す"""
    os.makedirs(out_dir, exist_ok=True)
    annotation = selftest_annotation()
    font = load_font(18)
    failures = []
    report = {}
    for kind, label, should_pass in SELFTEST_CASES:
        depth, normal, readable = selftest_scene(kind)
        record, _, drawings, _, cliff_failures, field = measure_photo(annotation, depth, normal, readable, None, kSelftestSize)
        if record is None:
            failures.append(f"{kind}: 量が求まらない {cliff_failures}")
            continue
        passed, failed, texts = selftest_rows(record)
        report[kind] = {"label": label, "expected_pass": should_pass, "passed": passed, "failed_rows": failed,
                        **{quantity: record.get(quantity) for quantity, _, _ in SELFTEST_ROWS},
                        "outline_residual_median_px": record["outline_residual_median_px"]}
        result = "pass" if passed == should_pass else "fail"
        print(f"SHAPE_SELFTEST case={kind} expected={'pass' if should_pass else 'fail'} measured={'pass' if passed else 'fail'} "
              f"failed_rows={','.join(failed) or '-'} " + " ".join(texts) + f" result={result}")
        if passed != should_pass:
            failures.append(f"{kind}（{label}）は {'通る' if should_pass else '落ちる'}はずが {'通った' if passed else '落ちた'}（{' '.join(texts)}）")
        gray = np.where(np.isfinite(depth), 90 + 60 * normal[:, :, 2], 30).astype(np.uint8)
        capture = Image.fromarray(np.stack([gray] * 3, axis=2))
        draw_sheet(capture.resize(PANEL_SIZE), capture, field, annotation, drawings, kSelftestSize,
                   [f"{kind}: {label}　期待 {'通る' if should_pass else '落ちる'}", " ".join(texts)], font).save(
            os.path.join(out_dir, f"shape_selftest_{kind}.png"))
    corner_report = {}
    for kind, label, should_match in CORNER_PROBE_CASES:
        matched, meet = corner_probe(kind)
        center = kCornerProbeSize / 2
        distance = float(np.hypot(meet[0] - center, meet[1] - center))
        corner_report[kind] = {"label": label, "expected_match": should_match, "matched": bool(matched),
                               "meet_px": [float(meet[0]), float(meet[1])], "meet_distance_px": distance}
        result = "pass" if matched == should_match else "fail"
        print(f"SHAPE_SELFTEST corner_case={kind} expected={'match' if should_match else 'no-match'} "
              f"measured={'match' if matched else 'no-match'} meet_distance_px={distance:.2f} result={result}")
        if matched != should_match:
            failures.append(f"{kind}（{label}）は角に{'数える' if should_match else '数えない'}はずが{'数えた' if matched else '数えなかった'}")
    with open(os.path.join(out_dir, "shape_selftest.json"), "w", encoding="utf-8") as file:
        json.dump({"rows": [list(row) for row in SELFTEST_ROWS], "cases": report, "corner_cases": corner_report,
                   "failures": failures}, file, ensure_ascii=False, indent=2)
    if failures:
        for failure in failures:
            print(f"SHAPE_SELFTEST failure={failure}")
        print(f"SHAPE_SELFTEST result=fail failures={len(failures)}")
        return 1
    print(f"SHAPE_SELFTEST result=pass cases={len(SELFTEST_CASES) + len(CORNER_PROBE_CASES)} out={out_dir}")
    return 0
