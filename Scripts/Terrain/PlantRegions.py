# 低木 1 株の比較（SCN-WLR-PLANT-REF-BASE）の領域の分け方・株の配置とメッシュの読み方。
# Blender の中で走る BlenderReference.py --plant（サンプルを増やすかの判定）と、ふつうの Python の ReferenceCheck.py --plant
# （検査と表）が同じ定義を使う。Blender の Python でも読めるよう numpy だけに頼る。
#
# 株: エンジンの撮影（Game の --wasteland-hide=all-but-plant）のログの WASTELAND_PLANT_PLACEMENT の reference=1 の行と、
#   同じ撮影のフォルダの reference_plant.ply（Game の WastelandFoliage::ExportReferencePlant。foliage.vert と同じ置き方の
#   ワールドの三角形と、頂点の線形のアルベド）。株の模型はアルファ抜きを使わない細い帯と角柱だけ（FoliageMesh.h）なので、
#   不透明度のマスクは全面 1。葉は 4 頂点の平たい帯で頂点の法線が 3 つとも同じ三角形、枝は三角の断面の角柱で頂点の法線が
#   断面の外向き（leaf_triangles）。
#
# 領域（画素の集合）は正解の側（Blender）の深度・物体の番号（地形 0、枝 1、葉 2）・法線・太陽の見え方から決め、エンジンと
# Cycles の同じ画素を比べる。株は細い枝と小さな葉で画素のほとんどが輪郭に掛かるので、建物（BuildingRegions）のように輪郭から
# 離れた画素だけを使うと残らない。代わりに、エンジンの深度と Blender の深度の差が DEPTH_MATCH_RELATIVE 以内の画素（両方の
# 描画で同じ面に当たった画素）だけを使う。地形の画素は、さらに空との境・深度の跳びから GrayRegions.EDGE_MARGIN_PX 画素より
# 離れたもの（GrayRegions.edge_mask）。深度から画素の位置（ワールド）を戻し、株の根元からの水平の距離 d[m] を測る。
#   日なた: GrayRegions と同じ n·l と見え方の基準。法線は Cycles の法線のパス（裏から当たった面はカメラの側へ向く）。
#   plant_sunlit（株の日なた）: 株の画素の日なた。
#   plant_shadow（株の陰）: 株の画素で、太陽へ向くが遮られる（見え方 ≤ SHADOW_MAX_VISIBILITY）か、枝の太陽の反対の面
#     （n·l ≤ 0）か、太陽を背にした葉で裏の見え方 ≤ SHADOW_MAX_VISIBILITY。
#   leaf_backlit（葉の透過）: 葉の画素で太陽を背にし（n·l ≤ −SUNLIT_MIN_COS）、裏の見え方（白い透過の面で太陽だけを描いた値
#     から求める）が SUNLIT_MIN_VISIBILITY 以上。透過の光だけで明るい葉。
#   ground_shadow（株の足元の影）: 地形の画素で d ≤ GROUND_SHADOW_M、太陽へ向くが遮られる。
#   ground_near（株の足元の遮蔽）: 地形の日なたの画素で d ≤ 樹冠の半径 + GROUND_NEAR_MARGIN_M（空の光の遮蔽で暗くなる所）。
#   ground_ref（離れた日なたの地面）: 地形の日なたの画素で GROUND_REF_M の範囲。
# 収束（乱数の種を変えた 2 本）は GrayRegions と同じ基準（平均の差・画素の差の RMS）で、画素の下限だけを MIN_REGION_PX にする。
import math

import numpy as np

import GrayRegions as GR

REGIONS = ("plant_sunlit", "plant_shadow", "leaf_backlit", "ground_shadow", "ground_near", "ground_ref")
REGION_LABELS = {
    "plant_sunlit": "株の日なた",
    "plant_shadow": "株の陰",
    "leaf_backlit": "太陽を背にした葉（透過）",
    "ground_shadow": "株の足元の影",
    "ground_near": "株の足元の日なた（遮蔽）",
    "ground_ref": "根元から 2.5〜5 m の日なたの地面",
}

DEPTH_MATCH_RELATIVE = 0.01
GROUND_SHADOW_M = 2.0
GROUND_NEAR_MARGIN_M = 0.5
GROUND_REF_M = (2.5, 5.0)
LEAF_NORMAL_COS = 0.9999
MIN_REGION_PX = 200

# 物体の番号（Cycles の Object Index のパス）
PLANT_BRANCH_PASS_INDEX = 1
PLANT_LEAF_PASS_INDEX = 2

PLY_VERTEX_PROPERTIES = ["property float x", "property float y", "property float z",
                         "property float nx", "property float ny", "property float nz",
                         "property float red", "property float green", "property float blue"]
PLY_FACE_PROPERTY = "property list uchar int vertex_indices"


def parse_placement(text):
    """Game のログから比べる株の配置（WASTELAND_PLANT_PLACEMENT の reference=1 の行）を読む。(配置の辞書か None, 行の数)"""
    found = []
    for line in text.splitlines():
        index = line.find("WASTELAND_PLANT_PLACEMENT ")
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
            "position": [float(fields["x"]), float(fields["y"]), float(fields["z"])],
            "yaw_rad": float(fields["yaw_rad"]),
            "width_scale": float(fields["width_scale"]),
            "height_scale": float(fields["height_scale"]),
            "color_variation": float(fields["color_variation"]),
            "wind_phase": float(fields["wind_phase"]),
            "wind_strength": float(fields["wind_strength"]),
            "roughness": float(fields["roughness"]),
            "ground_y": float(fields["ground"]),
            "ply": fields["ply"],
            "vertices": int(fields["vertices"]),
            "triangles": int(fields["triangles"]),
            "crown_radius": float(fields["crown_radius"]),
            "height": float(fields["height"]),
        })
    return (found[-1] if found else None), len(found)


def read_plant_ply(path):
    """Game の ExportReferencePlant が書いた PLY を読む。(位置, 法線, 線形のアルベド, 三角形)。並びが違えば例外"""
    with open(path, "rb") as f:
        data = f.read()
    marker = b"end_header\n"
    end = data.find(marker)
    if end < 0:
        raise ValueError(f"PLY の見出しが無い: {path}")
    header = data[:end].decode("ascii").splitlines()
    body = end + len(marker)
    if header[:2] != ["ply", "format binary_little_endian 1.0"]:
        raise ValueError(f"PLY の形式が違う: {path}")
    lines = [line for line in header[2:] if not line.startswith("comment")]
    if not (len(lines) == 12 and lines[0].startswith("element vertex ") and lines[1:10] == PLY_VERTEX_PROPERTIES and
            lines[10].startswith("element face ") and lines[11] == PLY_FACE_PROPERTY):
        raise ValueError(f"PLY の要素の並びが書き出しと違う: {path}")
    vertex_count = int(lines[0].split()[2])
    face_count = int(lines[10].split()[2])
    if len(data) - body != vertex_count * 36 + face_count * 13:
        raise ValueError(f"PLY の本体の大きさが見出しと合わない: {path}")
    vertices = np.frombuffer(data, dtype="<f4", count=vertex_count * 9, offset=body).reshape(vertex_count, 9)
    faces = np.frombuffer(data, dtype=np.dtype([("n", "u1"), ("i", "<i4", (3,))]), count=face_count,
                          offset=body + vertex_count * 36)
    if np.any(faces["n"] != 3):
        raise ValueError(f"三角形でない面がある: {path}")
    triangles = np.ascontiguousarray(faces["i"], dtype=np.int32)
    if triangles.size and (triangles.min() < 0 or triangles.max() >= vertex_count):
        raise ValueError(f"面の頂点の番号が範囲の外: {path}")
    return (np.ascontiguousarray(vertices[:, 0:3]), np.ascontiguousarray(vertices[:, 3:6]),
            np.ascontiguousarray(vertices[:, 6:9]), triangles)


def leaf_triangles(normals, triangles):
    """葉の三角形か（頂点の法線が 3 つとも同じ。枝の角柱は断面の外向きで頂点ごとに違う）"""
    n0, n1, n2 = (normals[triangles[:, k]] for k in range(3))
    d01 = np.einsum("ij,ij->i", n0, n1)
    d02 = np.einsum("ij,ij->i", n0, n2)
    return (d01 >= LEAF_NORMAL_COS) & (d02 >= LEAF_NORMAL_COS)


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


def root_distance(camera, depth, placement):
    """深度（カメラの前方向への距離 m、空は +∞）から戻した画素の位置の、株の根元からの水平の距離[m]（空は NaN）"""
    height, width = depth.shape
    safe = np.where(np.isfinite(depth), depth, np.nan)
    positions = np.asarray(camera["position"], dtype=np.float64)[None, None, :] + \
        pixel_rays(camera, width, height) * safe[..., None]
    x0, _, z0 = placement["position"]
    return np.hypot(positions[..., 0] - x0, positions[..., 2] - z0)


def same_surface(depth, engine_depth):
    """エンジンと Blender の深度の差が DEPTH_MATCH_RELATIVE 以内の画素（両方とも有限）"""
    finite = np.isfinite(depth) & np.isfinite(engine_depth)
    with np.errstate(invalid="ignore"):
        return finite & (np.abs(np.where(finite, engine_depth - depth, np.inf)) <=
                         DEPTH_MATCH_RELATIVE * np.where(finite, depth, 0.0))


def region_masks(camera, depth, engine_depth, plant_index, n_dot_l, visibility, back_visibility, surface, placement):
    """領域の名前 → 画素の真偽の配列（上の説明の定義）。engine_depth のほかは正解の側（Blender）の値。(領域, 記録)"""
    depth = np.asarray(depth, dtype=np.float64)
    engine_depth = np.asarray(engine_depth, dtype=np.float64)
    valid = surface & np.isfinite(depth)
    same = same_surface(depth, engine_depth)
    index = np.rint(plant_index).astype(np.int32)
    plant = valid & (index >= PLANT_BRANCH_PASS_INDEX)
    leaf = valid & (index == PLANT_LEAF_PASS_INDEX)
    branch = plant & ~leaf
    ground = valid & ~plant & ~GR.edge_mask(depth)
    distance = root_distance(camera, depth, placement)
    crown = float(placement["crown_radius"])
    facing = n_dot_l >= GR.SUNLIT_MIN_COS
    sunlit = facing & (visibility >= GR.SUNLIT_MIN_VISIBILITY)
    blocked = facing & (visibility <= GR.SHADOW_MAX_VISIBILITY)
    behind = n_dot_l <= -GR.SUNLIT_MIN_COS
    with np.errstate(invalid="ignore"):
        masks = {
            "plant_sunlit": plant & same & sunlit,
            "plant_shadow": plant & same & (blocked | (branch & (n_dot_l <= 0.0)) |
                                            (leaf & behind & (back_visibility <= GR.SHADOW_MAX_VISIBILITY))),
            "leaf_backlit": leaf & same & behind & (back_visibility >= GR.SUNLIT_MIN_VISIBILITY),
            "ground_shadow": ground & same & blocked & (distance <= GROUND_SHADOW_M),
            "ground_near": ground & same & sunlit & (distance <= crown + GROUND_NEAR_MARGIN_M),
            "ground_ref": ground & same & sunlit & (distance >= GROUND_REF_M[0]) & (distance <= GROUND_REF_M[1]),
        }
    record = {
        "plant_pixels": int(plant.sum()),
        "leaf_pixels": int(leaf.sum()),
        "plant_same_surface_ratio": float((plant & same).sum() / plant.sum()) if plant.any() else None,
        "ground_pixels_within_ref": int((ground & (distance <= GROUND_REF_M[1])).sum()),
        "ground_same_surface_ratio": float((ground & same).sum() / ground.sum()) if ground.any() else None,
        "crown_radius_m": crown,
    }
    return masks, record


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
        "depth_match_relative": DEPTH_MATCH_RELATIVE,
        "ground_shadow_m": GROUND_SHADOW_M,
        "ground_near_margin_m": GROUND_NEAR_MARGIN_M,
        "ground_ref_m": list(GROUND_REF_M),
        "leaf_normal_cos": LEAF_NORMAL_COS,
        "min_region_px": MIN_REGION_PX,
        "sunlit_min_cos": GR.SUNLIT_MIN_COS,
        "sunlit_min_visibility": GR.SUNLIT_MIN_VISIBILITY,
        "shadow_max_visibility": GR.SHADOW_MAX_VISIBILITY,
        "edge_margin_px": GR.EDGE_MARGIN_PX,
        "convergence_mean": GR.CONVERGENCE_MEAN,
        "convergence_pixel_rms": GR.CONVERGENCE_PIXEL_RMS,
    }
