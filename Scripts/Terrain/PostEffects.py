# エンジンの空間のポスト処理（ブルームと FXAA）を再現する。DisplayTransform.py の表示変換の前後に挟み、既定の撮影
# （ブルームと FXAA が乗った PNG）を HDR から作るのに使う（ReferenceCheck.py --grading）。
#
# 経路（Library/Core/Private/Rendering/SceneView.cpp の PostProcessStack と同じ順。値と式はシェーダーと同じ）:
#   1. BloomPass（Assets/Shaders/bloom_downsample.frag・bloom_upsample.frag・bloom.frag）: 露出後のシーンの色を 6 段
#      （縦横を 1 段ごとに半分、端数は切り捨て）へ 13 タップで縮小し（最初の段だけ Karis の重み）、最下段から上へ
#      テントで広げて各段の縮小結果へ足し、最上段をテントで元の大きさへ広げて段数で割り、しきい値なしで元の色へ
#      BLOOM_INTENSITY の割合で混ぜる。中間はすべて RGBA16F。
#   2. ToneMappingPass・VignettePass（DisplayTransform.py）
#   3. FXAAPass（Assets/Shaders/fxaa.frag）: display-linear の RGBA16F の上で、輝度（0.299・0.587・0.114）の差から縁を
#      見つけ、縁に沿って最大 12 歩探して、ずらした位置を双線形で読み直す（シェーダーの探索の向きの決め方もそのまま写す）。
#   4. 提示: sRGB の符号化と 8 bit への丸め（DisplayTransform.py）
# 設定の値は SceneView の既定（ブルーム: しきい値 0・強さ 0.04・半径 1・6 段、FXAA: 閾値 0.0312・最小 0.0625・
# サブピクセル 0.75）。撮影の記録（hdr.json の post）にはブルームと FXAA の有無しか無いので、この値は固定で持つ。
# レンズダートは再現していない（掛かっていれば UnsupportedDisplay を投げる）。
#
# 双線形の読み出しは、クランプのアドレスで、副テクセルの重みを GPU と同じ 8 bit に丸める。
import numpy as np

import DisplayTransform as DT

BLOOM_INTENSITY = 0.04
BLOOM_RADIUS = 1.0
BLOOM_MIP_COUNT = 6
FXAA_EDGE_THRESHOLD = 0.0312
FXAA_EDGE_THRESHOLD_MIN = 0.0625
FXAA_SUBPIXEL_QUALITY = 0.75
FXAA_SUBPIXEL_CAP = 0.75
FXAA_SEARCH_STEPS = (1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0)
FXAA_LUMA = np.array([0.299, 0.587, 0.114], dtype=np.float32)
SUBTEXEL_STEPS = 256.0


def bilinear(texture, px, py):
    """texture（H, W, 3）を画素の座標（画素の中心が k + 0.5）で双線形に読む（クランプ）"""
    height, width = texture.shape[:2]
    xs = np.asarray(px, dtype=np.float64) - 0.5
    ys = np.asarray(py, dtype=np.float64) - 0.5
    x0 = np.floor(xs)
    y0 = np.floor(ys)
    fx = (np.round((xs - x0) * SUBTEXEL_STEPS) / SUBTEXEL_STEPS).astype(np.float32)[..., None]
    fy = (np.round((ys - y0) * SUBTEXEL_STEPS) / SUBTEXEL_STEPS).astype(np.float32)[..., None]
    x0 = x0.astype(np.int64)
    y0 = y0.astype(np.int64)
    xa = np.clip(x0, 0, width - 1)
    xb = np.clip(x0 + 1, 0, width - 1)
    ya = np.clip(y0, 0, height - 1)
    yb = np.clip(y0 + 1, 0, height - 1)
    top = texture[ya, xa] * (1 - fx) + texture[ya, xb] * fx
    bottom = texture[yb, xa] * (1 - fx) + texture[yb, xb] * fx
    return (top * (1 - fy) + bottom * fy).astype(np.float32)


def target_grid(width, height, source_width, source_height):
    """出力（width, height）の画素の中心を、読む側のテクスチャの画素の座標で"""
    u = (np.arange(width) + 0.5) / width
    v = (np.arange(height) + 0.5) / height
    return np.meshgrid(u * source_width, v * source_height)


def bloom_downsample(source, width, height, first_level):
    """bloom_downsample.frag（しきい値 0）"""
    source_height, source_width = source.shape[:2]
    gx, gy = target_grid(width, height, source_width, source_height)

    def tap(dx, dy):
        return np.maximum(np.nan_to_num(bilinear(source, gx + dx, gy + dy), nan=0.0, posinf=0.0, neginf=0.0), 0.0)

    a, b, c = tap(-2, -2), tap(0, -2), tap(2, -2)
    d, e, f = tap(-2, 0), tap(0, 0), tap(2, 0)
    g, h, i = tap(-2, 2), tap(0, 2), tap(2, 2)
    j, k, l, m = tap(-1, -1), tap(1, -1), tap(-1, 1), tap(1, 1)
    boxes = [(j + k + l + m) * 0.25, (a + b + d + e) * 0.25, (b + c + e + f) * 0.25,
             (d + e + g + h) * 0.25, (e + f + h + i) * 0.25]
    weights = [0.5, 0.125, 0.125, 0.125, 0.125]
    if first_level:
        weights = [w / (1.0 + box @ DT.LUMA)[..., None] for w, box in zip(weights, boxes)]
    total = sum(weights)
    result = sum(box * w for box, w in zip(boxes, weights)) / np.maximum(total, 0.00001)
    return DT.to_half(result)


def bloom_tent(source, width, height, radius):
    """bloom_upsample.frag・bloom.frag の SampleTent（半径は読む側のテクセル単位）"""
    source_height, source_width = source.shape[:2]
    gx, gy = target_grid(width, height, source_width, source_height)

    def tap(dx, dy):
        return bilinear(source, gx + dx * radius, gy + dy * radius)

    total = tap(0, 0) * 4.0
    total += (tap(-1, 0) + tap(1, 0) + tap(0, -1) + tap(0, 1)) * 2.0
    total += tap(-1, -1) + tap(1, -1) + tap(-1, 1) + tap(1, 1)
    return (total * np.float32(1.0 / 16.0)).astype(np.float32)


def bloom_pass(scene):
    """BloomPass の出力（露出後のシーンの色、RGBA16F）"""
    height, width = scene.shape[:2]
    sizes = []
    level_width, level_height = width, height
    for _ in range(BLOOM_MIP_COUNT):
        if level_width <= 1 and level_height <= 1:
            break
        level_width = max(level_width // 2, 1)
        level_height = max(level_height // 2, 1)
        sizes.append((level_width, level_height))
    downs = []
    source = scene
    for level, (level_width, level_height) in enumerate(sizes):
        source = bloom_downsample(source, level_width, level_height, level == 0)
        downs.append(source)
    upper = downs[-1]
    for level in range(len(sizes) - 2, -1, -1):
        level_width, level_height = sizes[level]
        upper = DT.to_half(downs[level] + bloom_tent(upper, level_width, level_height, BLOOM_RADIUS))
    glow = bloom_tent(upper, width, height, BLOOM_RADIUS) * np.float32(1.0 / len(sizes))
    return DT.to_half(scene + (glow - scene) * np.float32(BLOOM_INTENSITY))


def fxaa_pass(color):
    """FXAAPass の出力（display-linear、RGBA16F）"""
    height, width = color.shape[:2]
    padded = np.pad(color, ((1, 1), (1, 1), (0, 0)), mode="edge")

    def luma(rgb):
        return rgb @ FXAA_LUMA

    def neighbor(dx, dy):
        return luma(padded[1 + dy:1 + dy + height, 1 + dx:1 + dx + width])

    luma_m = luma(color)
    luma_n, luma_s, luma_e, luma_w = neighbor(0, -1), neighbor(0, 1), neighbor(1, 0), neighbor(-1, 0)
    luma_min = np.minimum(luma_m, np.minimum(np.minimum(luma_n, luma_s), np.minimum(luma_e, luma_w)))
    luma_max = np.maximum(luma_m, np.maximum(np.maximum(luma_n, luma_s), np.maximum(luma_e, luma_w)))
    luma_range = luma_max - luma_min
    edge = ~(luma_range < np.maximum(np.float32(FXAA_EDGE_THRESHOLD_MIN), luma_max * np.float32(FXAA_EDGE_THRESHOLD)))
    out = color.copy()
    index = np.nonzero(edge)
    if not index[0].size:
        return DT.to_half(out)
    px = index[1] + 0.5
    py = index[0] + 0.5
    m, n, s, e, w = (v[index] for v in (luma_m, luma_n, luma_s, luma_e, luma_w))
    nw, ne, sw, se = (neighbor(dx, dy)[index] for dx, dy in ((-1, -1), (1, -1), (-1, 1), (1, 1)))
    span = luma_range[index]

    # サブピクセルの混ぜ
    luma_l = (n + s + e + w) * np.float32(0.25)
    blend_l = np.maximum(0.0, np.abs(luma_l - m) / span - np.float32(FXAA_SUBPIXEL_QUALITY))
    blend_l = np.minimum(np.minimum(blend_l / np.float32(1.0 - FXAA_SUBPIXEL_QUALITY), 1.0), FXAA_SUBPIXEL_CAP)

    # 縁の向きと、縁を挟む側
    edge_h = np.abs(nw + ne - 2.0 * n) + 2.0 * np.abs(w + e - 2.0 * m) + np.abs(sw + se - 2.0 * s)
    edge_v = np.abs(nw + sw - 2.0 * w) + 2.0 * np.abs(n + s - 2.0 * m) + np.abs(ne + se - 2.0 * e)
    horizontal = edge_h >= edge_v
    gradient1 = np.where(horizontal, n, w) - m
    gradient2 = np.where(horizontal, s, e) - m
    steepest = np.abs(gradient1) >= np.abs(gradient2)
    gradient_scaled = 0.25 * np.maximum(np.abs(gradient1), np.abs(gradient2))
    # fxaa.frag は bSteepest のとき stepLength を正（S か E の向き）のまま使う
    step = np.where(steepest, 1.0, -1.0)
    center_x = px + np.where(horizontal, 0.0, step * 0.5)
    center_y = py + np.where(horizontal, step * 0.5, 0.0)
    dir_x = np.where(horizontal, 1.0, 0.0)
    dir_y = np.where(horizontal, 0.0, 1.0)

    # 縁に沿って両方向へ探す
    x1, y1 = center_x - dir_x, center_y - dir_y
    x2, y2 = center_x + dir_x, center_y + dir_y
    end1 = luma(bilinear(color, x1, y1)) - m
    end2 = luma(bilinear(color, x2, y2)) - m
    reached1 = np.abs(end1) >= gradient_scaled
    reached2 = np.abs(end2) >= gradient_scaled
    for quality in FXAA_SEARCH_STEPS:
        searching = ~(reached1 & reached2)
        if not searching.any():
            break
        more1 = searching & ~reached1
        if more1.any():
            x1 = np.where(more1, x1 - dir_x * quality, x1)
            y1 = np.where(more1, y1 - dir_y * quality, y1)
            pick = np.nonzero(more1)
            end1[pick] = luma(bilinear(color, x1[pick], y1[pick])) - m[pick]
            reached1 = reached1 | (more1 & (np.abs(end1) >= gradient_scaled))
        more2 = searching & ~reached2
        if more2.any():
            x2 = np.where(more2, x2 + dir_x * quality, x2)
            y2 = np.where(more2, y2 + dir_y * quality, y2)
            pick = np.nonzero(more2)
            end2[pick] = luma(bilinear(color, x2[pick], y2[pick])) - m[pick]
            reached2 = reached2 | (more2 & (np.abs(end2) >= gradient_scaled))

    # 近い端までの距離からずらす量を決め、サブピクセルの混ぜと大きい方を採る
    dist1 = np.where(horizontal, px - x1, py - y1)
    dist2 = np.where(horizontal, x2 - px, y2 - py)
    direction1 = dist1 < dist2
    pixel_offset = -np.minimum(dist1, dist2) / (dist1 + dist2) + 0.5
    luma_end_correct = (np.where(direction1, end1, end2) < 0.0) != ((m - luma_l) < 0.0)
    final_offset = np.maximum(np.where(luma_end_correct, pixel_offset, 0.0), blend_l)
    final_x = px + np.where(horizontal, 0.0, final_offset * step)
    final_y = py + np.where(horizontal, final_offset * step, 0.0)
    out[index] = bilinear(color, final_x, final_y)
    return DT.to_half(out)


def scene_with_bloom(radiance, meta):
    """放射輝度（H, W, 3）に露出と、撮影の記録どおりのブルームを掛けたシーンの色（トーンマップの入力）"""
    post = meta.get("post") or {}
    if post.get("bloom") and float((meta.get("display") or {}).get("lens_dirt_intensity", 0.0)) > 0.0:
        raise DT.UnsupportedDisplay("ブルームにレンズダートが掛かっている（再現していない）")
    scene = DT.expose(radiance, float(meta["pre_exposure"]))
    return bloom_pass(scene) if post.get("bloom") else scene


def display_with_post(scene, meta, params, lut=None):
    """scene_with_bloom の結果に、表示変換・撮影の記録どおりの FXAA・sRGB を掛けた 8 bit の画像（uint8）"""
    post = meta.get("post") or {}
    display_linear = DT.vignette_pass(DT.tone_mapping_pass(scene, params, lut), params)
    if post.get("fxaa"):
        display_linear = fxaa_pass(display_linear)
    encoded = DT.encode_srgb(display_linear) * np.float32(255.0)
    return np.clip(np.rint(encoded), 0, 255).astype(np.uint8)
