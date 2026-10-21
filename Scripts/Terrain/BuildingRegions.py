# 建物 1 棟の比較（SCN-WLR-BUILDING-REF-BASE）の領域の分け方・建物の配置の読み方・深度から求める建物の下端と地面の高さの差。
# Blender の中で走る BlenderReference.py --building（サンプルを増やすかの判定）と、ふつうの Python の
# ReferenceCheck.py --building（検査と表）が同じ定義を使う。Blender の Python でも読めるよう numpy だけに頼る。
#
# 建物の配置: エンジンの撮影のログの WASTELAND_BUILDING_PLACEMENT の reference=1 の行（Game の --wasteland-hide=all-but-building
#   が残した 1 棟）。glTF・Entity の位置[m]・Y 軸まわりの回転 yaw[rad]・一様な拡大率。ローカルの (x, z) はワールドの
#   (x cos − z sin, z cos + x sin) へ写る（Game/GameModes/Wasteland/WastelandLayout.cpp の RotateXZ）。足元の範囲は glTF の
#   POSITION の最小・最大（エンジンの PropMeshShape と同じ、クック済みのメッシュの頂点の範囲）。
#
# 領域（画素の集合）は正解の側（Blender）の深度・物体の番号（建物 1、地形 0）・法線・太陽の見え方から決め、エンジンと Cycles の
# 同じ画素を比べる。共通: Blender で表面（α ≥ 0.5）かつ深度が有限で、空との境・深度の跳びから GrayRegions.EDGE_MARGIN_PX 画素
# より離れた画素（GrayRegions.edge_mask）。深度から画素の位置（ワールド）を戻し、
#   足元の距離 d: 水平の面で、建物の足元の範囲（ローカルの XZ の箱 × 拡大率）の外への距離[m]（中は 0）。
#   地面の高さ: 地形の画素で 0 < d ≤ GROUND_LEVEL_BAND_M の位置の高さの中央値（建物の周りの地面）。
#   ground_band（足元から 0〜2 m の地面）: 地形の画素で GROUND_BAND_M[0] < d ≤ GROUND_BAND_M[1]。日なたと影を分けない。
#   ground_ref（離れた日なたの地面）: 地形の画素で GROUND_REF_M の範囲、日なた（GrayRegions と同じ n·l と見え方の基準）。
#     足元の帯との比で、地面の材質に依らない足元の暗さを見る。
#   壁: 建物の画素で法線の上向きの成分の大きさが WALL_MAX_UP 以下（傾き 72.5° 以上）。
#   wall_bottom（壁の下端）: 壁で、地面の高さからの高さが WALL_BOTTOM_M 以下。
#   wall_sunlit・wall_shadow（壁の日なた・陰）: 壁で地面の高さから WALL_BOTTOM_M より上の、日なた・影（GrayRegions と同じ基準）。
# 収束（乱数の種を変えた 2 本）は GrayRegions と同じ基準（平均の差・画素の差の RMS）で、画素の下限だけを MIN_REGION_PX にする
# （outpost の視点では建物まで約 37 m、足元の帯は地面をかすめて見るので数百画素）。
#
# 建物の下端と地面の高さの差（深度から）: 描画ごとに、その描画の深度から戻した位置で、
#   下端: 建物の画素（Blender の物体の番号）で足元の範囲の中（d ≤ FOOTPRINT_INSIDE_M）の高さの BOTTOM_PERCENTILE % 点、
#   地面: 地形の画素で FOOTPRINT_INSIDE_M < d ≤ GROUND_LEVEL_BAND_M の高さの中央値、差 = 下端 − 地面（負は埋まり、正は浮き）。
import math

import numpy as np

import GrayRegions as GR

REGIONS = ("ground_band", "ground_ref", "wall_bottom", "wall_sunlit", "wall_shadow")
REGION_LABELS = {
    "ground_band": "足元から 0〜2 m の地面",
    "ground_ref": "足元から 4〜8 m の日なたの地面",
    "wall_bottom": "壁の下端",
    "wall_sunlit": "壁の日なた",
    "wall_shadow": "壁の陰",
}

GROUND_BAND_M = (0.0, 2.0)
GROUND_REF_M = (4.0, 8.0)
GROUND_LEVEL_BAND_M = 1.0
WALL_MAX_UP = 0.3
WALL_BOTTOM_M = 0.5
FOOTPRINT_INSIDE_M = 0.05
BOTTOM_PERCENTILE = 2.0
MIN_REGION_PX = 200

# 物体の番号（Cycles の Object Index のパス）
BUILDING_PASS_INDEX = 1


def parse_placement(text):
    """Game のログから比べる建物の配置（WASTELAND_BUILDING_PLACEMENT の reference=1 の行）を読む。(配置の辞書か None, 行の数)"""
    found = []
    for line in text.splitlines():
        index = line.find("WASTELAND_BUILDING_PLACEMENT ")
        if index < 0:
            continue
        fields = {}
        for token in line[index:].split()[1:]:
            if "=" in token:
                key, value = token.split("=", 1)
                fields[key] = value
        if fields.get("reference") != "1":
            continue
        found.append({
            "id": fields["id"],
            "gltf": fields["gltf"],
            "position": [float(fields["x"]), float(fields["y"]), float(fields["z"])],
            "yaw_rad": float(fields["yaw_rad"]),
            "scale": float(fields["scale"]),
            "mesh_min_y": float(fields["mesh_min_y"]),
        })
    return (found[-1] if found else None), len(found)


def gltf_bounds(path):
    """glTF（.glb か .gltf）の POSITION の最小・最大（ローカル。全プリミティブの和。ノードの変換は恒等のものだけ受ける）"""
    import json
    import struct
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] == b"glTF":
        length = struct.unpack("<I", data[12:16])[0]
        document = json.loads(data[20:20 + length])
    else:
        document = json.loads(data.decode("utf-8-sig"))
    for node in document.get("nodes", []):
        for key in ("matrix", "translation", "rotation", "scale"):
            if key in node:
                raise ValueError(f"glTF のノードに変換がある（{key}）。足元の範囲を頂点の範囲から求められない")
    lo = np.full(3, np.inf)
    hi = np.full(3, -np.inf)
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            accessor = document["accessors"][primitive["attributes"]["POSITION"]]
            lo = np.minimum(lo, np.asarray(accessor["min"], dtype=np.float64))
            hi = np.maximum(hi, np.asarray(accessor["max"], dtype=np.float64))
    if not np.isfinite(lo).all():
        raise ValueError("glTF に POSITION が無い")
    return lo.tolist(), hi.tolist()


def pixel_rays(camera, width, height):
    """画素の中心を通る、カメラの前方向の成分が 1 の向き（ワールド。(高さ, 幅, 3)）。深度を掛けると位置になる"""
    forward = np.asarray(camera["forward"], dtype=np.float64)
    up = np.asarray(camera["up"], dtype=np.float64)
    right = np.asarray(camera["right"], dtype=np.float64)
    tan_half = math.tan(math.radians(float(camera["vertical_fov_degrees"])) / 2.0)
    aspect = float(camera["aspect_ratio"])
    xs = ((np.arange(width) + 0.5) / width * 2.0 - 1.0) * tan_half * aspect
    ys = (1.0 - (np.arange(height) + 0.5) / height * 2.0) * tan_half
    return forward[None, None, :] + xs[None, :, None] * right[None, None, :] + ys[:, None, None] * up[None, None, :]


def world_positions(camera, depth):
    """深度（カメラの前方向への距離 m、空は +∞）から画素の位置（ワールド）。空は NaN"""
    height, width = depth.shape
    rays = pixel_rays(camera, width, height)
    safe = np.where(np.isfinite(depth), depth, np.nan)
    return np.asarray(camera["position"], dtype=np.float64)[None, None, :] + rays * safe[..., None]


def footprint_distance(positions, placement, bounds):
    """位置（ワールド）の、建物の足元の範囲の外への水平の距離[m]（中は 0）"""
    x0, y0, z0 = placement["position"]
    yaw = float(placement["yaw_rad"])
    scale = float(placement["scale"])
    c, s = math.cos(yaw), math.sin(yaw)
    dx = positions[..., 0] - x0
    dz = positions[..., 2] - z0
    # RotateXZ の逆: ローカル = (dx cos + dz sin, dz cos − dx sin) ÷ 拡大率
    lx = (dx * c + dz * s) / scale
    lz = (dz * c - dx * s) / scale
    lo, hi = bounds
    cx, hx = 0.5 * (lo[0] + hi[0]), 0.5 * (hi[0] - lo[0])
    cz, hz = 0.5 * (lo[2] + hi[2]), 0.5 * (hi[2] - lo[2])
    ox = np.maximum(np.abs(lx - cx) - hx, 0.0)
    oz = np.maximum(np.abs(lz - cz) - hz, 0.0)
    return np.hypot(ox, oz) * scale


def ground_level(positions, distance, ground):
    """建物の周りの地面の高さ（地形の画素で 0 < d ≤ GROUND_LEVEL_BAND_M の高さの中央値。無ければ NaN）"""
    band = ground & (distance > FOOTPRINT_INSIDE_M) & (distance <= GROUND_LEVEL_BAND_M) & np.isfinite(positions[..., 1])
    return float(np.median(positions[..., 1][band])) if band.any() else float("nan"), int(band.sum())


def region_masks(camera, depth, building, up, n_dot_l, visibility, surface, placement, bounds):
    """領域の名前 → 画素の真偽の配列（上の説明の定義）。すべて正解の側（Blender）の値。(領域, 記録)"""
    depth = np.asarray(depth, dtype=np.float64)
    valid = surface & np.isfinite(depth) & ~GR.edge_mask(depth)
    positions = world_positions(camera, depth)
    distance = footprint_distance(positions, placement, bounds)
    ground = valid & ~building
    level, level_pixels = ground_level(positions, distance, ground)
    height = positions[..., 1] - level
    sunlit = (n_dot_l >= GR.SUNLIT_MIN_COS) & (visibility >= GR.SUNLIT_MIN_VISIBILITY)
    shadow = (n_dot_l <= 0.0) | ((n_dot_l >= GR.SUNLIT_MIN_COS) & (visibility <= GR.SHADOW_MAX_VISIBILITY))
    wall = valid & building & (np.abs(up) <= WALL_MAX_UP)
    with np.errstate(invalid="ignore"):
        masks = {
            "ground_band": ground & (distance > GROUND_BAND_M[0]) & (distance <= GROUND_BAND_M[1]),
            "ground_ref": ground & (distance >= GROUND_REF_M[0]) & (distance <= GROUND_REF_M[1]) & sunlit,
            "wall_bottom": wall & (height <= WALL_BOTTOM_M),
            "wall_sunlit": wall & (height > WALL_BOTTOM_M) & sunlit,
            "wall_shadow": wall & (height > WALL_BOTTOM_M) & shadow,
        }
    return masks, {"ground_level_y": level, "ground_level_pixels": level_pixels,
                   "building_pixels": int((valid & building).sum())}


def bottom_gap(camera, depth, building, placement, bounds):
    """その描画の深度から求めた建物の下端と地面の高さの差（上の説明）。記録の辞書"""
    depth = np.asarray(depth, dtype=np.float64)
    positions = world_positions(camera, depth)
    finite = np.isfinite(depth)
    distance = footprint_distance(positions, placement, bounds)
    inside = finite & building & (distance <= FOOTPRINT_INSIDE_M)
    level, level_pixels = ground_level(positions, distance, finite & ~building)
    record = {"building_pixels": int(inside.sum()), "ground_pixels": level_pixels, "ground_y": level,
              "placement_bottom_y": float(placement["position"][1] + placement["mesh_min_y"] * placement["scale"])}
    if inside.any() and math.isfinite(level):
        bottom = float(np.percentile(positions[..., 1][inside], BOTTOM_PERCENTILE))
        record.update({"bottom_y": bottom, "gap_m": bottom - level})
    else:
        record.update({"bottom_y": None, "gap_m": None})
    return record


def convergence(y_a, y_b, masks):
    """領域ごとの収束の値（GrayRegions と同じ基準、画素の下限は MIN_REGION_PX）。(領域 → 値, すべて収束したか)"""
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
                entry["converged"] = bool(count >= MIN_REGION_PX and entry["mean_diff"] <= GR.CONVERGENCE_MEAN and
                                          entry["pixel_rms"] <= GR.CONVERGENCE_PIXEL_RMS)
        stats[name] = entry
    return stats, all(stats[name]["converged"] for name in REGIONS)


def criteria():
    """記録に書く基準"""
    return {
        "ground_band_m": list(GROUND_BAND_M),
        "ground_ref_m": list(GROUND_REF_M),
        "ground_level_band_m": GROUND_LEVEL_BAND_M,
        "wall_max_up": WALL_MAX_UP,
        "wall_bottom_m": WALL_BOTTOM_M,
        "footprint_inside_m": FOOTPRINT_INSIDE_M,
        "bottom_percentile": BOTTOM_PERCENTILE,
        "min_region_px": MIN_REGION_PX,
        "sunlit_min_cos": GR.SUNLIT_MIN_COS,
        "sunlit_min_visibility": GR.SUNLIT_MIN_VISIBILITY,
        "shadow_max_visibility": GR.SHADOW_MAX_VISIBILITY,
        "edge_margin_px": GR.EDGE_MARGIN_PX,
        "convergence_mean": GR.CONVERGENCE_MEAN,
        "convergence_pixel_rms": GR.CONVERGENCE_PIXEL_RMS,
    }
