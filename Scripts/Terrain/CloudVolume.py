# 雲の正解の絵（SCN-WLR-CLOUD-REF-BLENDER）の、書き出した雲の密度場から Blender の体積を作る計算と、視線の分け方。
# Blender の中で走る BlenderReference.py --cloud（格子の焼き・カメラ・収束の判定）と、ふつうの Python の CloudCheck.py
# （ReferenceCheck.py --cloud の検査と表）が同じ定義を使う。Blender の Python でも読めるよう numpy だけに頼る。
#
# 入力の契約は Docs/RenderingValidation/WastelandCloudDump.md、式は CloudDumpCheck.CloudModel（エンジンの雲の式の写し）。
# 雲の枠（Blender へ渡す座標）: カメラの真下の惑星の表面を原点とする、エンジンの向きの座標（m、+X 東・+Y 上・+Z 南）。
#   カメラは (0, max(カメラの y, 1), 0)（雲の式の高度の下限と同じ）、惑星の中心は (0, −惑星の半径, 0)。枠の点 p の雲の座標は
#   (カメラの x + p.x, |p − 中心| − 惑星の半径, カメラの z + p.z) で、エンジンが視線の歩の位置から雲の座標を求めるのと同じ。
# 格子: カメラの水平の右・前と上を軸とする VOXEL_SIZE_M の格子の点（OpenVDB と同じく値を格子の点に置く）で密度を評価する。
#   範囲は、画面の縁の視線が雲の層の底と頂の球殻と交わる点の水平の外接箱を MARGIN_M 広げた箱（太陽への影と横への散乱の分。
#   箱の外の雲は無いものとする）、高さは雲の層の全体（箱の隅までの地球の丸みの分を下へ足す）。細部で縁を削るのは、エンジンの
#   視線と同じくカメラから DETAIL_MAX_DISTANCE 以内の点。格子の大きさ 16 m は細部の 3D ノイズの texel（約 17 m）より細かく、
#   書き出しの視線で、格子を通した透過率と式を 4 m ごとに積んだ透過率の差は最大 0.004（12 m で 0.0026、8 m で 0.0013）。
#
# 視線の領域（画面の決めた画素の視線の分け方。エンジンの書き出しの値で決め、両方の描画に同じ分け方を使う）:
#   不透明度 = 1 − エンジンの雲の透過率。太陽との角 = 視線と太陽へ向かう向きのなす角。太陽の透過率 = エンジンの光を返した所の
#   距離の平均（書き出しの行 1 の w）の点から太陽へ、式を SUN_STEP_M ごとに積んだ透過率。
#   cloud_body（厚い雲）: 不透明度 ≥ THICK_OPACITY。
#   cloud_edge（縁）: CLOUD_OPACITY ≤ 不透明度 < THICK_OPACITY で、太陽との角 ≥ EDGE_MIN_SUN_ANGLE_DEG（銀の縁を除く）。
#   silver_lining（銀の縁）: CLOUD_OPACITY ≤ 不透明度 < THICK_OPACITY で、太陽との角 ≤ SILVER_MAX_SUN_ANGLE_DEG（前への散乱）。
#   body_sunlit（日の当たる側）: 厚い雲で、太陽の透過率 ≥ SUNLIT_MIN_SUN_T。
#   body_shadow（影の側）: 厚い雲で、太陽の透過率 ≤ SHADOW_MAX_SUN_T（太陽の光の半分以上を雲自身が遮る）。
#   各領域に MIN_REGION_RAYS 本以上の視線が要る（足りなければ未判定）。
# Cycles の値: 視線の画素を中心とする (2 WINDOW_RADIUS_PX + 1)² 画素の平均（画素の中の標本はフィルタの幅で画素の中心に寄せる）。
# 収束（乱数の種を変えた 2 本、A と B）: 領域ごとに、窓の画素の輝度 Y で GrayRegions と同じ平均の差・画素の差の RMS を測る。
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import CloudDumpCheck as CD  # noqa: E402
import GrayRegions as GR  # noqa: E402

# 格子の大きさ[m]と、画面の視線の外接箱からの広げ[m]
VOXEL_SIZE_M = 16.0
MARGIN_M = 2000.0
# 外接箱を求める画面の縁の視線の数（1 辺あたり）
FOOTPRINT_SAMPLES = 40
# 焼くときに一度に評価する右の軸の格子の数
BAKE_CHUNK = 48
# 式を積む歩[m]（視線の透過率・太陽の透過率）と、太陽へ積む距離の上限[m]
FINE_STEP_M = 4.0
SUN_STEP_M = 8.0
SUN_MAX_DISTANCE_M = 12000.0
# 視線の領域
CLOUD_OPACITY = CD.CLOUD_OPACITY
THICK_OPACITY = CD.THICK_OPACITY
SILVER_MAX_SUN_ANGLE_DEG = 20.0
EDGE_MIN_SUN_ANGLE_DEG = 40.0
SUNLIT_MIN_SUN_T = 0.7
SHADOW_MAX_SUN_T = 0.5
MIN_REGION_RAYS = 3
WINDOW_RADIUS_PX = 1
REGIONS = ("cloud_body", "cloud_edge", "silver_lining", "body_sunlit", "body_shadow")
REGION_LABELS = {
    "cloud_body": "厚い雲",
    "cloud_edge": "縁",
    "silver_lining": "銀の縁",
    "body_sunlit": "日の当たる側",
    "body_shadow": "影の側",
}


def dump_files(meta, view):
    """書き出しのファイルの一覧（入力の記録のハッシュに使う）"""
    field = meta["density_field"]
    return [f"{view}.cloud.json", field["shape_noise"]["file"], field["detail_noise"]["file"],
            meta["probe"]["file"], meta["sky_radiance_lut"]["file"]]


def load_dump(capture_dir, view):
    """書き出しを読む。(meta, CloudModel, 視線の値 [8, 視線, 4])。足りなければ ValueError"""
    meta_path = os.path.join(capture_dir, f"{view}.cloud.json")
    if not os.path.isfile(meta_path):
        raise ValueError(f"{view}.cloud.json が無い")
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    if meta.get("format") != CD.FORMAT:
        raise ValueError(f"形式が違う（{meta.get('format')}）")
    failures = []
    field = meta["density_field"]
    shape = CD.read_array(capture_dir, field["shape_noise"], np.uint8, failures, "基本の形の 3D ノイズ")
    detail = CD.read_array(capture_dir, field["detail_noise"], np.uint8, failures, "細部の 3D ノイズ")
    probe = CD.read_array(capture_dir, meta["probe"], "<f4", failures, "視線の値")
    lut = CD.read_array(capture_dir, meta["sky_radiance_lut"], "<f4", failures, "空の放射輝度 LUT")
    if failures:
        raise ValueError("; ".join(failures))
    model = CD.CloudModel(meta, shape.astype(np.float64) / 255.0, detail.astype(np.float64) / 255.0,
                          lut.astype(np.float64))
    # 格子の視線だけを残す（反実仮想の列は ReferenceCheck.py --cloud-far が見る）
    columns = CD.grid_columns(probe)
    if len(columns) != probe.shape[1]:
        meta["probe"]["pixels"] = [meta["probe"]["pixels"][i] for i in columns]
        probe = probe[:, columns, :]
    return meta, model, probe


def target_size(meta):
    return int(meta["target"]["width"]), int(meta["target"]["height"])


def camera_height(model):
    """雲の式のカメラの高度（max(カメラの y, 1)）"""
    return max(float(model.camera[1]), 1.0)


def normalize(v):
    v = np.asarray(v, dtype=np.float64)
    return v / np.linalg.norm(v)


def angle_between(a, b):
    return float(np.arccos(np.clip(np.dot(normalize(a), normalize(b)), -1.0, 1.0)))


def camera_frame(model, target):
    """書き出しの逆の視線行列から、カメラの向き（エンジンの座標）・縦の画角と、対称な透視の投影で画素の視線を表せるかの誤差"""
    width, height = target
    forward = model.ray_direction((width * 0.5, height * 0.5), target)
    top = model.ray_direction((width * 0.5, 0.0), target)
    bottom = model.ray_direction((width * 0.5, float(height)), target)
    left = model.ray_direction((0.0, height * 0.5), target)
    right_edge = model.ray_direction((float(width), height * 0.5), target)
    right = right_edge - left
    right = normalize(right - forward * np.dot(right, forward))
    up = top - bottom
    up = up - forward * np.dot(up, forward) - right * np.dot(up, right)
    up = normalize(up)
    vertical_fov = angle_between(top, bottom)
    tan_v = math.tan(vertical_fov * 0.5)
    tan_h = tan_v * width / height
    # 画面の 9 × 5 の点で、対称な透視の投影の視線と書き出しの逆の視線行列の視線のなす角の最大
    worst = 0.0
    for x in np.linspace(0.0, width, 9):
        for y in np.linspace(0.0, height, 5):
            ndc_x = 2.0 * x / width - 1.0
            ndc_y = 1.0 - 2.0 * y / height
            expected = normalize(forward + right * ndc_x * tan_h + up * ndc_y * tan_v)
            worst = max(worst, angle_between(expected, model.ray_direction((x, y), target)))
    return {
        "forward": forward.tolist(), "right": right.tolist(), "up": up.tolist(),
        "vertical_fov_degrees": math.degrees(vertical_fov),
        "height_m": camera_height(model), "position_frame": [0.0, camera_height(model), 0.0],
        "projection_error_rad": worst, "pixel_rad": vertical_fov / height,
    }


def grid_spec(model, target, frame):
    """格子の軸・原点（雲の枠）・数"""
    width, height = target
    forward = np.asarray(frame["forward"], dtype=np.float64)
    up_axis = np.array([0.0, 1.0, 0.0])
    forward_h = normalize([forward[0], 0.0, forward[2]])
    right_h = normalize(np.cross(forward_h, up_axis))
    if np.dot(right_h, frame["right"]) <= 0.0:
        raise ValueError("カメラの右と格子の右の向きが逆（左手系の座標）")
    h0 = camera_height(model)
    origin_y = model.planet_radius + h0
    radii = (model.planet_radius + model.bottom, model.planet_radius + model.top)
    edge = np.linspace(0.0, 1.0, FOOTPRINT_SAMPLES + 1)
    pixels = [(u * width, 0.0) for u in edge] + [(u * width, float(height)) for u in edge] + \
        [(0.0, v * height) for v in edge] + [(float(width), v * height) for v in edge]
    a_values = []
    b_values = []
    for pixel in pixels:
        direction = model.ray_direction(pixel, target)
        for radius in radii:
            near, far = model.intersect_sphere(origin_y, direction, radius)
            if far > 0.0:
                point = direction * far
                a_values.append(float(np.dot(point, right_h)))
                b_values.append(float(np.dot(point, forward_h)))
    if not a_values:
        raise ValueError("画面の視線が雲の層と交わらない")
    a_min, a_max = min(a_values) - MARGIN_M, max(a_values) + MARGIN_M
    b_min, b_max = min(b_values) - MARGIN_M, max(b_values) + MARGIN_M
    corner = max(math.hypot(a, b) for a in (a_min, a_max) for b in (b_min, b_max))
    y_min = model.bottom - corner * corner / (2.0 * model.planet_radius) - VOXEL_SIZE_M
    y_max = model.top + VOXEL_SIZE_M
    size = VOXEL_SIZE_M
    dims = [int(math.ceil((a_max - a_min) / size)) + 1, int(math.ceil((b_max - b_min) / size)) + 1,
            int(math.ceil((y_max - y_min) / size)) + 1]
    origin = right_h * a_min + forward_h * b_min + up_axis * y_min
    return {
        "voxel_size_m": size, "margin_m": MARGIN_M, "camera_height_m": h0,
        "axes": {"i": right_h.tolist(), "j": forward_h.tolist(), "k": up_axis.tolist()},
        "origin_frame": origin.tolist(), "dims": dims,
        "footprint_m": {"right": [a_min + MARGIN_M, a_max - MARGIN_M], "forward": [b_min + MARGIN_M, b_max - MARGIN_M]},
        "frame_y_range_m": [y_min, y_min + (dims[2] - 1) * size],
    }


def frame_density(model, points, mask=None):
    """雲の枠の点（(N, 3)）の密度。細部はカメラからの距離で決める"""
    points = np.atleast_2d(points)
    h0 = camera_height(model)
    altitude = np.linalg.norm(np.stack([points[:, 0], points[:, 1] + model.planet_radius, points[:, 2]], axis=-1),
                              axis=-1) - model.planet_radius
    heights = (altitude - model.bottom) / (model.top - model.bottom)
    inside = (heights >= 0.0) & (heights <= 1.0)
    values = np.zeros(points.shape[0])
    if np.any(inside):
        distance = np.linalg.norm(points[inside] - np.array([0.0, h0, 0.0]), axis=-1)
        noise = np.stack([model.camera[0] + points[inside, 0], altitude[inside], model.camera[2] + points[inside, 2]],
                         axis=-1)
        values[inside] = model.density(noise, heights[inside], distance < CD.DETAIL_MAX_DISTANCE,
                                       None if mask is None else mask[inside])
    return values


def bake_density(model, grid):
    """格子の点の密度（float32、[i, j, k]）"""
    size = float(grid["voxel_size_m"])
    ni, nj, nk = (int(n) for n in grid["dims"])
    axis_i = np.asarray(grid["axes"]["i"], dtype=np.float64) * size
    axis_j = np.asarray(grid["axes"]["j"], dtype=np.float64) * size
    axis_k = np.asarray(grid["axes"]["k"], dtype=np.float64) * size
    origin = np.asarray(grid["origin_frame"], dtype=np.float64)
    out = np.zeros((ni, nj, nk), dtype=np.float32)
    rows_j = np.arange(nj, dtype=np.float64)
    levels = np.arange(nk, dtype=np.float64)
    for start in range(0, ni, BAKE_CHUNK):
        rows_i = np.arange(start, min(ni, start + BAKE_CHUNK), dtype=np.float64)
        columns = origin[None, None, :] + rows_i[:, None, None] * axis_i + rows_j[None, :, None] * axis_j
        # 覆いの印は雲の座標の x・z だけで決まるので列ごとに 1 回求め、印の無い列は飛ばす
        mask = model.coverage_mask(model.camera[0] + columns[..., 0].ravel(),
                                   model.camera[2] + columns[..., 2].ravel()).reshape(columns.shape[:2])
        live_i, live_j = np.nonzero(mask > CD.DENSITY_THRESHOLD)
        if live_i.size == 0:
            continue
        points = columns[live_i, live_j][:, None, :] + levels[None, :, None] * axis_k
        column_mask = np.repeat(mask[live_i, live_j], nk)
        values = frame_density(model, points.reshape(-1, 3), column_mask).reshape(live_i.size, nk)
        out[start + live_i, live_j, :] = values.astype(np.float32)
    return out


def density_along(model, direction, distances):
    """カメラからの視線の距離の点の密度"""
    points = np.asarray(direction, dtype=np.float64)[None, :] * np.asarray(distances, dtype=np.float64)[:, None]
    points[:, 1] += camera_height(model)
    return frame_density(model, points)


def fine_transmittance(model, direction):
    """視線の雲の層の区間（エンジンと同じ区間）で、式を FINE_STEP_M ごとに積んだ雲の透過率"""
    march = model.march(np.asarray(direction, dtype=np.float64), 0.5, with_light=False)
    if not march["marched"]:
        return 1.0
    start, end = float(march["start_distance"]), float(march["end_distance"])
    count = max(1, int(math.ceil((end - start) / FINE_STEP_M)))
    step = (end - start) / count
    distances = start + (np.arange(count) + 0.5) * step
    return float(np.exp(-model.extinction * step * density_along(model, direction, distances).sum()))


def sun_transmittance(model, direction, distance):
    """視線の距離 distance の点から太陽へ、式を SUN_STEP_M ごとに積んだ透過率"""
    count = int(SUN_MAX_DISTANCE_M / SUN_STEP_M)
    steps = (np.arange(count) + 0.5) * SUN_STEP_M
    points = np.asarray(direction, dtype=np.float64)[None, :] * float(distance) + \
        model.sun_direction[None, :] * steps[:, None]
    points[:, 1] += camera_height(model)
    return float(np.exp(-model.extinction * SUN_STEP_M * frame_density(model, points).sum()))


def window_pixels(pixel, width, height):
    """視線の画素を中心とする窓の画素の (行, 列) の範囲（画面の内に切る）"""
    column = int(math.floor(float(pixel[0])))
    row = int(math.floor(float(pixel[1])))
    return (slice(max(0, row - WINDOW_RADIUS_PX), min(height, row + WINDOW_RADIUS_PX + 1)),
            slice(max(0, column - WINDOW_RADIUS_PX), min(width, column + WINDOW_RADIUS_PX + 1)))


def window_centers(pixel, width, height):
    """窓の画素の中心（画素の座標）"""
    rows, columns = window_pixels(pixel, width, height)
    return [(x + 0.5, y + 0.5) for y in range(rows.start, rows.stop) for x in range(columns.start, columns.stop)]


def ray_table(model, probe, pixels, target):
    """視線ごとのエンジンの値と、領域を決める量"""
    rays = []
    for index, pixel in enumerate(pixels):
        direction = model.ray_direction(pixel, target)
        transmittance = float(probe[0, index, 0])
        opacity = float(np.clip(1.0 - transmittance, 0.0, 1.0))
        sun_angle = math.degrees(angle_between(direction, model.sun_direction))
        cloud_distance = float(probe[1, index, 3])
        sun_t = sun_transmittance(model, direction, cloud_distance) if opacity >= CLOUD_OPACITY else None
        rays.append({"index": index, "pixel": [float(pixel[0]), float(pixel[1])], "direction": direction.tolist(),
                     "engine_transmittance": transmittance, "opacity": opacity, "sun_angle_degrees": sun_angle,
                     "cloud_distance_m": cloud_distance, "sun_transmittance": sun_t,
                     "engine_scattered_rgb": [float(v) for v in probe[1, index, :3]]})
    return rays


def region_members(rays):
    """領域の名前 → 視線の番号の一覧"""
    members = {name: [] for name in REGIONS}
    for ray in rays:
        opacity = ray["opacity"]
        if opacity >= THICK_OPACITY:
            members["cloud_body"].append(ray["index"])
            sun_t = ray["sun_transmittance"]
            if sun_t is not None and sun_t >= SUNLIT_MIN_SUN_T:
                members["body_sunlit"].append(ray["index"])
            if sun_t is not None and sun_t <= SHADOW_MAX_SUN_T:
                members["body_shadow"].append(ray["index"])
        elif opacity >= CLOUD_OPACITY:
            if ray["sun_angle_degrees"] >= EDGE_MIN_SUN_ANGLE_DEG:
                members["cloud_edge"].append(ray["index"])
            if ray["sun_angle_degrees"] <= SILVER_MAX_SUN_ANGLE_DEG:
                members["silver_lining"].append(ray["index"])
    return members


def convergence(y_a, y_b, pixels, members):
    """領域ごとの収束（窓の画素の輝度 Y の 2 本の平均の差と画素の差の RMS）。(領域 → 値, すべて収束したか)"""
    height, width = y_a.shape
    stats = {}
    for name in REGIONS:
        indices = members.get(name, [])
        entry = {"rays": len(indices), "converged": False}
        if indices:
            a = np.concatenate([y_a[window_pixels(pixels[i], width, height)].ravel() for i in indices]).astype(np.float64)
            b = np.concatenate([y_b[window_pixels(pixels[i], width, height)].ravel() for i in indices]).astype(np.float64)
            mean = float(0.5 * (a.mean() + b.mean()))
            entry["pixels"] = int(a.size)
            if mean > 0.0:
                entry["mean_diff"] = float(abs(a.mean() - b.mean()) / mean)
                entry["pixel_rms"] = float(np.sqrt(np.mean((a - b) ** 2)) / mean)
                entry["converged"] = bool(len(indices) >= MIN_REGION_RAYS and
                                          entry["mean_diff"] <= GR.CONVERGENCE_MEAN and
                                          entry["pixel_rms"] <= GR.CONVERGENCE_PIXEL_RMS)
        stats[name] = entry
    return stats, all(stats[name]["converged"] for name in REGIONS)


def criteria():
    """記録に書く基準"""
    return {
        "voxel_size_m": VOXEL_SIZE_M, "margin_m": MARGIN_M, "fine_step_m": FINE_STEP_M, "sun_step_m": SUN_STEP_M,
        "sun_max_distance_m": SUN_MAX_DISTANCE_M, "cloud_opacity": CLOUD_OPACITY, "thick_opacity": THICK_OPACITY,
        "silver_max_sun_angle_degrees": SILVER_MAX_SUN_ANGLE_DEG, "edge_min_sun_angle_degrees": EDGE_MIN_SUN_ANGLE_DEG,
        "sunlit_min_sun_transmittance": SUNLIT_MIN_SUN_T, "shadow_max_sun_transmittance": SHADOW_MAX_SUN_T,
        "min_region_rays": MIN_REGION_RAYS, "window_radius_px": WINDOW_RADIUS_PX,
        "convergence_mean": GR.CONVERGENCE_MEAN, "convergence_pixel_rms": GR.CONVERGENCE_PIXEL_RMS,
    }
