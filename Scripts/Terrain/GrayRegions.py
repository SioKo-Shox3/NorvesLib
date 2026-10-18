# 灰色の地形の比較（SCN-WLR-CYCLES-GRAY）の領域の分け方と、Cycles の収束の測り方。
# Blender の中で走る BlenderReference.py --gray（サンプルを増やすかの判定）と、ふつうの Python の ReferenceCheck.py --gray
# （検査と表）が同じ定義を使う。Blender の Python でも読めるよう numpy だけに頼る。
#
# 領域（画素の集合）: エンジンの深度（AOV、カメラの前方向への距離 m、空は +∞）・Cycles のシェーディングの法線（エンジンの座標）・
# Cycles で描いた太陽の見え方（0〜1。白いランバート面を太陽だけで直接光だけ描き、E⊥ × max(n·l, 0) / π で割った値）から決める。
#   共通: 両方の描画で表面（エンジンの深度が有限かつ Cycles の α が 0.5 以上）で、空との境・深度の跳び（隣の画素との相対差が
#     EDGE_JUMP を超える）から EDGE_MARGIN_PX 画素より離れた画素（輪郭の 2 画素以内の投影のずれが値に混ざらないように）。
#   far（遠景）: 深度が FAR_MIN_M より奥。日なたと影を分けない。
#   近景（深度が FAR_MIN_M 以下）を、法線の上向きの成分と太陽で 4 つに分ける:
#     ground（地面）: 上向きの成分 ≥ GROUND_MIN_UP（傾斜 30° 以下）、cliff（崖）: ≤ CLIFF_MAX_UP（傾斜 50° 以上）。間は使わない。
#     sunlit（日なた）: n·l ≥ SUNLIT_MIN_COS かつ見え方 ≥ SUNLIT_MIN_VISIBILITY。
#     shadow（影）: n·l ≤ 0（太陽に背を向けた面）か、n·l ≥ SUNLIT_MIN_COS かつ見え方 ≤ SHADOW_MAX_VISIBILITY（落ちた影）。
#     半影と、太陽をかすめる面（0 < n·l < SUNLIT_MIN_COS）は使わない。
#
# 収束（乱数の種を変えた 2 本、A と B）: 領域ごとに、
#   平均の差 |平均(A) − 平均(B)| / 平均((A + B) / 2) が CONVERGENCE_MEAN 以下、
#   画素の差の RMS √平均((A − B)²) / 平均((A + B) / 2) が CONVERGENCE_PIXEL_RMS 以下（2 本の平均の画素の雑音はこの約半分）、
#   画素の数が MIN_REGION_PX 以上なら収束。値は輝度 Y（線形の Rec.709）で測る。
import math

import numpy as np

LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)

REGIONS = ("sunlit_ground", "shadow_ground", "sunlit_cliff", "shadow_cliff", "far")
REGION_LABELS = {
    "sunlit_ground": "日なたの地面",
    "shadow_ground": "影の中の地面",
    "sunlit_cliff": "日なたの崖",
    "shadow_cliff": "影の崖",
    "far": "遠景",
}

FAR_MIN_M = 2000.0
GROUND_MIN_UP = math.cos(math.radians(30.0))
CLIFF_MAX_UP = math.cos(math.radians(50.0))
SUNLIT_MIN_COS = 0.1
SUNLIT_MIN_VISIBILITY = 0.9
SHADOW_MAX_VISIBILITY = 0.1
EDGE_JUMP = 0.04
EDGE_MARGIN_PX = 2
MIN_REGION_PX = 2000

CONVERGENCE_MEAN = 0.01
CONVERGENCE_PIXEL_RMS = 0.10


def read_pfm(path):
    """RGB（"PF"）か 1 チャンネル（"Pf"）の PFM を (高さ, 幅, チャンネル数) の float32 で読む（画像の上を 0 行目にする）"""
    with open(path, "rb") as f:
        data = f.read()
    tokens = []
    pos = 0
    while len(tokens) < 4:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        if start == pos:
            raise ValueError("PFM の見出しが短い")
        tokens.append(data[start:pos].decode("ascii"))
    pos += 1
    if tokens[0] not in ("PF", "Pf"):
        raise ValueError(f"PFM でない（{tokens[0]}）")
    channels = 3 if tokens[0] == "PF" else 1
    width, height, scale = int(tokens[1]), int(tokens[2]), float(tokens[3])
    dtype = "<f4" if scale < 0 else ">f4"
    count = width * height * channels
    if len(data) - pos != count * 4:
        raise ValueError(f"PFM の画素の量が見出しと合わない（{len(data) - pos} バイト、見出しから {count * 4} バイト）")
    pixels = np.frombuffer(data, dtype=dtype, count=count, offset=pos)
    return pixels.reshape(height, width, channels)[::-1].astype(np.float32)


def write_pfm(path, image):
    """(高さ, 幅) か (高さ, 幅, 3) の画像（上の行が 0 行目）を PFM（下の行から並ぶ、little endian）で書く"""
    image = np.asarray(image, dtype=np.float32)
    height, width = image.shape[:2]
    kind = "PF" if image.ndim == 3 else "Pf"
    with open(path, "wb") as f:
        f.write(f"{kind}\n{width} {height}\n-1.0\n".encode("ascii"))
        f.write(np.ascontiguousarray(image[::-1], dtype="<f4").tobytes())


def luminance(rgb):
    return np.asarray(rgb, dtype=np.float64) @ LUMA


def dilate(mask, steps):
    out = mask.copy()
    for _ in range(steps):
        grown = out.copy()
        grown[1:, :] |= out[:-1, :]
        grown[:-1, :] |= out[1:, :]
        grown[:, 1:] |= out[:, :-1]
        grown[:, :-1] |= out[:, 1:]
        out = grown
    return out


def edge_mask(depth):
    """空との境と深度の跳びの画素から EDGE_MARGIN_PX 画素以内"""
    finite = np.isfinite(depth)
    edge = np.zeros(depth.shape, dtype=bool)
    safe = np.where(finite, depth, 0.0)
    for axis in (0, 1):
        a = np.take(safe, np.arange(depth.shape[axis] - 1), axis=axis)
        b = np.take(safe, np.arange(1, depth.shape[axis]), axis=axis)
        fa = np.take(finite, np.arange(depth.shape[axis] - 1), axis=axis)
        fb = np.take(finite, np.arange(1, depth.shape[axis]), axis=axis)
        jump = (fa != fb) | (fa & fb & (np.abs(a - b) > EDGE_JUMP * np.minimum(a, b)))
        if axis == 0:
            edge[:-1, :] |= jump
            edge[1:, :] |= jump
        else:
            edge[:, :-1] |= jump
            edge[:, 1:] |= jump
    return dilate(edge, EDGE_MARGIN_PX)


def region_masks(depth, up, n_dot_l, visibility, surface):
    """領域の名前 → 画素の真偽の配列（上の説明の定義）。depth はエンジンの深度、up・n_dot_l・visibility・surface は Cycles の側"""
    depth = np.asarray(depth, dtype=np.float64)
    valid = surface & np.isfinite(depth) & ~edge_mask(depth)
    far = valid & (depth > FAR_MIN_M)
    near = valid & ~far
    sunlit = (n_dot_l >= SUNLIT_MIN_COS) & (visibility >= SUNLIT_MIN_VISIBILITY)
    shadow = (n_dot_l <= 0.0) | ((n_dot_l >= SUNLIT_MIN_COS) & (visibility <= SHADOW_MAX_VISIBILITY))
    ground = up >= GROUND_MIN_UP
    cliff = up <= CLIFF_MAX_UP
    return {
        "sunlit_ground": near & ground & sunlit,
        "shadow_ground": near & ground & shadow,
        "sunlit_cliff": near & cliff & sunlit,
        "shadow_cliff": near & cliff & shadow,
        "far": far,
    }


def convergence(y_a, y_b, masks):
    """領域ごとの収束の値（上の説明）。(領域の名前 → 値の辞書, すべて収束したか)"""
    stats = {}
    for name in REGIONS:
        mask = masks[name]
        count = int(mask.sum())
        entry = {"pixels": count, "converged": False}
        if count > 0:
            a = y_a[mask].astype(np.float64)
            b = y_b[mask].astype(np.float64)
            mean = float(0.5 * (a.mean() + b.mean()))
            if mean > 0.0:
                entry["mean_diff"] = float(abs(a.mean() - b.mean()) / mean)
                entry["pixel_rms"] = float(np.sqrt(np.mean((a - b) ** 2)) / mean)
                entry["converged"] = bool(count >= MIN_REGION_PX and entry["mean_diff"] <= CONVERGENCE_MEAN and
                                          entry["pixel_rms"] <= CONVERGENCE_PIXEL_RMS)
        stats[name] = entry
    return stats, all(stats[name]["converged"] or stats[name]["pixels"] < MIN_REGION_PX for name in REGIONS)


def criteria():
    """記録に書く基準"""
    return {
        "far_min_m": FAR_MIN_M,
        "ground_min_up": GROUND_MIN_UP,
        "cliff_max_up": CLIFF_MAX_UP,
        "sunlit_min_cos": SUNLIT_MIN_COS,
        "sunlit_min_visibility": SUNLIT_MIN_VISIBILITY,
        "shadow_max_visibility": SHADOW_MAX_VISIBILITY,
        "edge_jump": EDGE_JUMP,
        "edge_margin_px": EDGE_MARGIN_PX,
        "min_region_px": MIN_REGION_PX,
        "convergence_mean": CONVERGENCE_MEAN,
        "convergence_pixel_rms": CONVERGENCE_PIXEL_RMS,
    }
