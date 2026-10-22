# 雲の書き出し（SCN-WLR-CLOUD-REF-DUMP）の検査。ReferenceCheck.py --cloud-dump の本体。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --cloud-dump --engine <撮影先> [--views cloud_sky] [--verify]
#       CaptureScene.ps1 -GameArgs cloud-dump の撮影（<視点>.cloud.json と、そこに書かれたファイル）を視点ごとに検査し、
#       結果を <撮影先>/cloud_dump_check.json に書く。--verify は入力のハッシュ（inputs.json）を今のファイルと照らす。
#
# 入力の契約は Docs/RenderingValidation/WastelandCloudDump.md。書き出した格子（3D ノイズ）と定数だけから、エンジンの式
# （Assets/Shaders/Common/CloudLayerCommon.glsl の SampleCloudDensity・MarchCloudLayer・ComposeCloudRadiance）を numpy で
# 写して、書き出しの視線（画面の決めた画素）ごとに雲の透過率を求め直し、同じ視線を GPU が描画と同じ式で評価した値と比べる。
# 合否:
#   1. 書き出しの形式・ファイルの形・値が有限。撮影の起動引数に --cloud-dump があり、Game のログに書き出しのパスを足した印と
#      保存の印がある。3D ノイズのファイルの FNV-1a が JSON の値と、Game のログの CLOUD_NOISE（描画のパスと書き出しのパスが
#      GPU へ上げた texel）の値と同じ。
#   2. 定数が契約どおり: 周期の逆数・風のずれ・天気の模様のずれ・フレームの位相を、JSON の雲の層・時刻・フレーム番号から
#      求め直した値と合う（CONSTANT_TOLERANCE）。
#   3. 視線がすべて空の画素で、雲のある視線（不透明度 CLOUD_OPACITY 以上）が MIN_CLOUD_RAYS 本以上、厚い視線（THICK_OPACITY
#      以上）が MIN_THICK_RAYS 本以上ある（検査が成り立つ）。
#   4. 視線ごとに、画素から求め直した向き・歩の端数・雲の層の区間・歩数・1 歩の長さが GPU の値と合う。
#   5. 雲の透過率の差の絶対値が全部の視線で TRANSMITTANCE_TOLERANCE（1 %）以内。
# 記録（合否に使わない）: 光学的深さの差、散乱光・雲の放射輝度・上の空の平均・雲の後ろの空の相対差（同じ式の写しの照明）。
import json
import os
import re

import numpy as np

GAME_ARG = "--cloud-dump"
LOG_LINES = ("CLOUD_DUMP ",)
DEFAULT_VIEW = "cloud_sky"
FORMAT = "norves_cloud_dump_v1"
PROBE_ROWS = 8
# 透過率の差の絶対値の上限（1 %）
TRANSMITTANCE_TOLERANCE = 0.01
# 検査が成り立つ雲の視線の数
CLOUD_OPACITY = 0.05
MIN_CLOUD_RAYS = 10
THICK_OPACITY = 0.5
MIN_THICK_RAYS = 3
# 定数を契約から求め直した値との差（単精度に丸めた値との比べ）
CONSTANT_TOLERANCE = 1e-4
# 画素から求め直した向き・歩の端数・区間の差。GPU は単精度で、遠方面の点の w が 2 に近い値どうしの差（逆射影の行列の桁落ち）
# になり、向きが 1e-4（荒野の cloud_sky で 0.011°、0.2 画素）ずれる。区間は惑星の半径（6.36e6 m）の 2 乗どうしの差で求めるので、
# 同じく相対 1e-4 ほどずれる。向きは 1 画素（0.056°）より小さい 1e-3、区間は 2e-3 まで許す
DIRECTION_TOLERANCE = 1e-3
JITTER_TOLERANCE = 0.01
DISTANCE_RELATIVE_TOLERANCE = 2e-3
# 放射輝度の相対差の分母の下限（cd/m²）
RADIANCE_FLOOR = 1.0

# cloud_layer の GLSL の定数（Common/CloudLayerCommon.glsl。契約の文書と同じ）
PI = 3.14159265359
MAX_MARCH_DISTANCE = 45000.0
FADE_START_DISTANCE = 30000.0
MIN_PRIMARY_STEPS = 32
MAX_PRIMARY_STEPS = 96
PRIMARY_STEP_TARGET = 90.0
LIGHT_STEPS = 6
LIGHT_FIRST_STEP = 70.0
LIGHT_STEP_GROWTH = 1.8
DETAIL_MAX_DISTANCE = 18000.0
# 雲までの区間の空気遠近を積む歩数（kCloudAerialSteps。VolumetricsCommon.glsl の kAtmosphereSteps と同じ中点則）
AERIAL_STEPS = 32
MIN_TRANSMITTANCE = 0.01
DENSITY_THRESHOLD = 0.001
# 遠い雲の消える原因を分ける診断の切り替え（GLSL の kCloudDiagnostic*）。書き出しの反実仮想の列（SCN-WL-CLOUD-FAR-CAUSE）が
# 視線の値の 7 行目の w の 4 の位より上に持つ
DIAGNOSTIC_NO_MARCH_LIMIT = 1
DIAGNOSTIC_NO_FADE = 2
# 雲までの区間の空気遠近を外す（SCN-WL-CLOUD-FAR-COMPOSITE の前は独自の 22 km の霞を外す切り替えだった）
DIAGNOSTIC_NO_HAZE = 4
DIAGNOSTIC_MAX_PRIMARY_STEPS = 1024


def fnv1a64(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return value


def fnv1a64_fast(data):
    """FNV-1a（64 bit）。numpy で 1 バイトずつ回すと遅いので、Python の整数で回す"""
    return fnv1a64(memoryview(data).tobytes())


def clamp01(value):
    return np.clip(value, 0.0, 1.0)


def remap(value, low, high, new_low, new_high):
    return new_low + (value - low) / np.maximum(high - low, 1.0e-5) * (new_high - new_low)


def smoothstep(edge0, edge1, value):
    t = clamp01((value - edge0) / (edge1 - edge0))
    return t * t * (3.0 - 2.0 * t)


def mix(a, b, t):
    return a * (1.0 - t) + b * t


def probe_diagnostic_flags(probe):
    """視線ごとの診断の切り替え（格子の視線は 0）"""
    return np.rint(probe[7, :, 3]).astype(np.int64) >> 2


def grid_columns(probe):
    """格子の視線（診断の切り替えを立てていない列）の番号"""
    return [int(i) for i in np.where(probe_diagnostic_flags(probe) == 0)[0]]


def sample_trilinear(texture, coords):
    """GPU の 3 次元の線形の標本（texel の中心が (i + 0.5) / 大きさ、3 方向とも繰り返す）。texture は [z, y, x, c]、coords は (..., 3) の (u, v, w)"""
    depth, height, width = texture.shape[:3]
    size = np.array([width, height, depth], dtype=np.float64)
    position = coords * size - 0.5
    base = np.floor(position)
    fraction = position - base
    base = base.astype(np.int64)
    result = 0.0
    for corner in range(8):
        offset = np.array([corner & 1, (corner >> 1) & 1, (corner >> 2) & 1])
        index = base + offset
        x = np.mod(index[..., 0], width)
        y = np.mod(index[..., 1], height)
        z = np.mod(index[..., 2], depth)
        weight = np.prod(np.where(offset == 1, fraction, 1.0 - fraction), axis=-1)
        result = result + weight[..., None] * texture[z, y, x]
    return result


def sample_sky(lut, direction):
    """cloud_layer の SampleSky（正距円筒の空の放射輝度 LUT。横は繰り返し、縦は端で止める線形の標本）"""
    direction = np.asarray(direction, dtype=np.float64)
    u = np.arctan2(direction[2], direction[0]) * 0.15915494 + 0.5
    v = np.arcsin(np.clip(-direction[1], -1.0, 1.0)) * 0.31830989 + 0.5
    height, width = lut.shape[:2]
    x = u * width - 0.5
    y = v * height - 0.5
    x0 = int(np.floor(x))
    y0 = int(np.floor(y))
    fx = x - x0
    fy = y - y0

    def at(row, column):
        return lut[min(max(row, 0), height - 1), column % width, :3]

    value = ((1 - fx) * (1 - fy) * at(y0, x0) + fx * (1 - fy) * at(y0, x0 + 1) +
             (1 - fx) * fy * at(y0 + 1, x0) + fx * fy * at(y0 + 1, x0 + 1))
    return np.maximum(value, 0.0)


class CloudModel:
    """書き出しの JSON と格子から組み立てた、エンジンの雲の式の写し"""

    def __init__(self, meta, shape_noise, detail_noise, sky_lut, aerial_profile=None):
        """aerial_profile は書き出しの空気遠近の高度ごとの表（[2, 段, 4]。<視点>.cloud.aerial_profile.f32）"""
        params = meta["shader_params"]
        field = meta["density_field"]
        self.shape_noise = shape_noise
        self.detail_noise = detail_noise
        self.sky_lut = sky_lut
        self.inverse_view_projection = np.array(params["inverse_view_projection"], dtype=np.float64).reshape(4, 4).T
        self.camera = np.array(params["camera_position_and_pre_exposure"][:3], dtype=np.float64)
        self.frame_phase = float(params["sun_direction_and_frame_index"][3])
        sun = np.array(params["sun_direction_and_frame_index"][:3], dtype=np.float64)
        self.sun_direction = sun / np.linalg.norm(sun)
        self.sun_illuminance = np.array(params["sun_illuminance"][:3], dtype=np.float64)
        layer = params["layer_altitudes_and_coverage"]
        self.planet_radius = float(layer[0])
        self.bottom = float(layer[1])
        self.top = float(layer[2])
        self.coverage = float(layer[3])
        scales = params["extinction_and_inverse_scales"]
        self.extinction = float(scales[0])
        self.inverse_shape = float(scales[1])
        self.inverse_detail = float(scales[2])
        self.inverse_weather = float(scales[3])
        offsets = params["shape_and_weather_offset"]
        self.shape_offset = np.array([offsets[0], 0.0, offsets[1]], dtype=np.float64)
        self.weather_offset = np.array(offsets[2:4], dtype=np.float64)
        self.detail_offset = np.array(params["detail_offset"][:3], dtype=np.float64)
        self.weather_high_offset = np.array(params["weather_high_offset"][:2], dtype=np.float64)
        self.ground_albedo = np.array(params["ground_albedo"][:3], dtype=np.float64)
        # 雲までの区間の空気遠近（GLSL の IntegrateCloudAerial。地面の VolumetricsPass と同じ媒質と表）
        aerial = params.get("aerial_perspective", [0.0, 1.0, 0.0, 0.0])
        self.aerial_enabled = float(aerial[0]) > 0.5 and aerial_profile is not None
        self.aerial_distance_scale = float(aerial[1])
        self.aerial_start_distance = max(float(aerial[2]), 0.0)
        self.aerial_origin_altitude = float(aerial[3])
        rayleigh = params.get("rayleigh_extinction_and_scale_height", [0.0, 0.0, 0.0, 1.0])
        self.rayleigh_extinction = np.array(rayleigh[:3], dtype=np.float64)
        self.rayleigh_scale_height = float(rayleigh[3])
        mie = params.get("mie_extinction_and_scattering", [0.0, 1.0, 0.0, 0.0])
        self.mie_extinction = float(mie[0])
        self.mie_scale_height = float(mie[1])
        self.mie_scattering = float(mie[2])
        self.mie_anisotropy = float(mie[3])
        self.aerial_profile_max_altitude = float(params.get("aerial_profile_range", [1.0])[0])
        self.aerial_profile = None if aerial_profile is None else np.asarray(aerial_profile, dtype=np.float64)
        weather = field["weather"]
        self.rotation_cos, self.rotation_sin = (float(v) for v in weather["rotation_cos_sin"])
        self.high_scale = float(weather["high_scale"])

    # ---- 雲までの空気遠近（IntegrateCloudAerial） ----
    def aerial_profile_lookup(self, row, altitude):
        """SampleCloudAerialProfile（高度ごとの表を海面からの高度で線形に引く）。altitude は配列、戻りは [n, 3]"""
        size = self.aerial_profile.shape[1]
        position = np.clip(altitude / self.aerial_profile_max_altitude, 0.0, 1.0) * (size - 1)
        index = np.minimum(position.astype(np.int64), size - 2)
        weight = (position - index)[:, None]
        table = self.aerial_profile[row, :, :3]
        return table[index] * (1.0 - weight) + table[index + 1] * weight

    def aerial(self, direction, distance, steps=AERIAL_STEPS):
        """カメラから direction の向きに distance m の区間の空気遠近（RGB の透過率, 散乱光）。steps 歩の中点則"""
        direction = np.asarray(direction, dtype=np.float64)
        step_length = distance / steps
        sample_distance = (np.arange(steps) + 0.5) * step_length
        origin_height = self.camera[1] + self.aerial_origin_altitude
        from_center = np.stack([direction[0] * sample_distance,
                                self.planet_radius + origin_height + direction[1] * sample_distance,
                                direction[2] * sample_distance], axis=1)
        altitude = np.linalg.norm(from_center, axis=1) - self.planet_radius
        weight = np.where(sample_distance >= self.aerial_start_distance, self.aerial_distance_scale, 0.0)
        rayleigh_density = np.exp(np.clip(-altitude / self.rayleigh_scale_height, -80.0, 80.0))
        mie_density = np.exp(np.clip(-altitude / self.mie_scale_height, -80.0, 80.0))
        rayleigh = self.rayleigh_extinction[None, :] * (rayleigh_density * weight)[:, None]
        mie = (self.mie_scattering * mie_density * weight)[:, None]
        step_depth = (rayleigh + (self.mie_extinction * mie_density * weight)[:, None]) * step_length
        # その歩の中点までの透過率
        sample_transmittance = np.exp(-(np.cumsum(step_depth, axis=0) - 0.5 * step_depth))
        cosine = float(np.clip(np.dot(direction, self.sun_direction), -1.0, 1.0))
        rayleigh_phase = 0.0596831 * (1.0 + cosine * cosine)
        g = self.mie_anisotropy
        mie_phase = (1.0 - g * g) / (12.5663706 * max(1.0 + g * g - 2.0 * g * cosine, 1.0e-4) ** 1.5)
        sun = self.aerial_profile_lookup(0, altitude)
        psi = self.aerial_profile_lookup(1, altitude)
        scattering = np.sum(sample_transmittance * ((rayleigh * rayleigh_phase + mie * mie_phase) * sun + (rayleigh + mie) * psi),
                            axis=0) * step_length
        transmittance = np.exp(-np.clip(np.sum(step_depth, axis=0), 0.0, 80.0))
        return transmittance, np.maximum(scattering, 0.0)

    # ---- 密度（SampleCloudDensity） ----
    def coverage_mask(self, x, z):
        """天気の模様の覆いの印（雲の座標の x・z だけで決まる）"""
        x = np.atleast_1d(x).astype(np.float64)
        z = np.atleast_1d(z).astype(np.float64)
        weather_u = (self.rotation_cos * x - self.rotation_sin * z) * self.inverse_weather
        weather_v = (self.rotation_sin * x + self.rotation_cos * z) * self.inverse_weather
        low = sample_trilinear(self.shape_noise, np.stack(
            [weather_u + self.weather_offset[0], np.full_like(weather_u, 0.37), weather_v + self.weather_offset[1]], axis=-1))
        high = sample_trilinear(self.shape_noise, np.stack(
            [weather_v * self.high_scale + self.weather_high_offset[0], np.full_like(weather_u, 0.71),
             weather_u * self.high_scale + self.weather_high_offset[1]], axis=-1))
        weather_value = clamp01(low[:, 0] * 0.55 + low[:, 1] * 0.2 + high[:, 0] * 0.25)
        return smoothstep(1.0 - self.coverage - 0.08, 1.0 - self.coverage + 0.22, weather_value)

    def density(self, position, height_fraction, detail_mask, mask=None):
        """mask は coverage_mask の値（省くと位置から求める）"""
        position = np.atleast_2d(position)
        height_fraction = np.atleast_1d(height_fraction).astype(np.float64)
        detail_mask = np.broadcast_to(np.atleast_1d(detail_mask), height_fraction.shape)
        if mask is None:
            mask = self.coverage_mask(position[:, 0], position[:, 2])
        top_limit = mix(0.35, 1.0, mask)
        profile = (clamp01(remap(height_fraction, 0.0, 0.07, 0.0, 1.0)) *
                   clamp01(remap(height_fraction, top_limit * 0.55, top_limit, 1.0, 0.0)))
        shape = sample_trilinear(self.shape_noise, (position + self.shape_offset) * self.inverse_shape)
        low_fbm = shape[:, 1] * 0.625 + shape[:, 2] * 0.25 + shape[:, 3] * 0.125
        base = clamp01(remap(shape[:, 0], low_fbm - 1.0, 1.0, 0.0, 1.0))
        base = clamp01(remap(base * profile, 1.0 - mask, 1.0, 0.0, 1.0)) * mask
        detail = sample_trilinear(self.detail_noise, (position + self.detail_offset) * self.inverse_detail)
        detail_fbm = detail[:, 0] * 0.625 + detail[:, 1] * 0.25 + detail[:, 2] * 0.125
        modifier = mix(detail_fbm, 1.0 - detail_fbm, clamp01(height_fraction * 4.0))
        eroded = clamp01(remap(base, modifier * 0.35, 1.0, 0.0, 1.0))
        value = np.where(detail_mask & (base > 0.0), eroded, base)
        value = np.where(profile <= 0.0, 0.0, value)
        return np.where(mask <= 0.001, 0.0, value)

    # ---- 視線の向きと歩の端数 ----
    def ray_direction(self, pixel, target_size):
        uv = np.asarray(pixel, dtype=np.float64) / np.asarray(target_size, dtype=np.float64)
        clip = np.array([uv[0] * 2.0 - 1.0, uv[1] * 2.0 - 1.0, 1.0, 1.0])
        far = self.inverse_view_projection @ clip
        direction = far[:3] / far[3] - self.camera
        return direction / np.linalg.norm(direction)

    def jitter(self, pixel):
        """InterleavedGradientNoise を単精度で（GPU と同じ順の演算）"""
        f32 = np.float32
        px = f32(pixel[0]) + f32(5.588238) * f32(self.frame_phase)
        py = f32(pixel[1]) + f32(5.588238) * f32(self.frame_phase)
        dot = f32(px * f32(0.06711056)) + f32(py * f32(0.00583715))
        inner = f32(dot - np.floor(dot))
        product = f32(f32(52.9829189) * inner)
        return float(product - np.floor(product))

    # ---- 視線を進める（MarchCloudLayer と ComposeCloudRadiance） ----
    @staticmethod
    def intersect_sphere(origin_y, direction, radius):
        b = origin_y * direction[1]
        c = origin_y * origin_y - radius * radius
        discriminant = b * b - c
        if discriminant < 0.0:
            return 1.0, -1.0
        root = np.sqrt(discriminant)
        return -b - root, -b + root

    def march(self, direction, jitter, with_light=True, flags=0):
        """flags は診断の切り替え（DIAGNOSTIC_*。GLSL の MarchCloudLayerWithDiagnostics と ComposeCloudRadianceWithDiagnostics）"""
        result = {"marched": False, "transmittance": 1.0, "optical_depth": 0.0, "steps_taken": 0}
        camera = self.camera
        origin_y = self.planet_radius + max(camera[1], 1.0)
        bottom_radius = self.planet_radius + self.bottom
        top_radius = self.planet_radius + self.top
        planet = self.intersect_sphere(origin_y, direction, self.planet_radius)
        if planet[0] <= planet[1] and planet[0] > 0.0:
            return result
        top_hit = self.intersect_sphere(origin_y, direction, top_radius)
        bottom_hit = self.intersect_sphere(origin_y, direction, bottom_radius)
        if origin_y < bottom_radius:
            start, end = bottom_hit[1], top_hit[1]
        elif origin_y <= top_radius:
            start, end = 0.0, top_hit[1]
            if bottom_hit[0] <= bottom_hit[1] and bottom_hit[0] > 0.0:
                end = min(end, bottom_hit[0])
        else:
            if top_hit[0] > top_hit[1] or top_hit[1] <= 0.0:
                return result
            start = max(top_hit[0], 0.0)
            end = bottom_hit[0] if (bottom_hit[0] <= bottom_hit[1] and bottom_hit[0] > 0.0) else top_hit[1]
        no_limit = bool(flags & DIAGNOSTIC_NO_MARCH_LIMIT)
        max_steps = DIAGNOSTIC_MAX_PRIMARY_STEPS if no_limit else MAX_PRIMARY_STEPS
        if not (end > start) or (not no_limit and start > MAX_MARCH_DISTANCE):
            return result
        if not no_limit:
            end = min(end, start + MAX_MARCH_DISTANCE)
        segment = end - start
        step_count = int(np.clip(int(segment / PRIMARY_STEP_TARGET), MIN_PRIMARY_STEPS, max_steps))
        step_length = segment / step_count

        thickness = self.top - self.bottom
        indices = np.arange(step_count, dtype=np.float64)
        distances = start + (indices + jitter) * step_length
        positions = camera[None, :] + direction[None, :] * distances[:, None]
        from_center = np.stack([positions[:, 0] - camera[0], origin_y + direction[1] * distances,
                                positions[:, 2] - camera[2]], axis=-1)
        altitudes = np.linalg.norm(from_center, axis=-1) - self.planet_radius
        heights = (altitudes - self.bottom) / thickness
        inside = (heights >= 0.0) & (heights <= 1.0)
        noise_positions = np.stack([positions[:, 0], altitudes, positions[:, 2]], axis=-1)
        densities = np.zeros(step_count)
        if np.any(inside):
            densities[inside] = self.density(noise_positions[inside], heights[inside],
                                             distances[inside] < DETAIL_MAX_DISTANCE)
        active = inside & (densities > DENSITY_THRESHOLD)

        sun_terms = np.zeros(step_count)
        sky_above = (sample_sky(self.sky_lut, (0.0, 1.0, 0.0)) * 0.2 +
                     (sample_sky(self.sky_lut, (0.866, 0.5, 0.0)) + sample_sky(self.sky_lut, (-0.866, 0.5, 0.0)) +
                      sample_sky(self.sky_lut, (0.0, 0.5, 0.866)) + sample_sky(self.sky_lut, (0.0, 0.5, -0.866))) * 0.2)
        ground = self.ground_albedo * (self.sun_illuminance * max(self.sun_direction[1], 0.0) / PI + sky_above)
        if with_light and np.any(active):
            sun_terms[active] = self.sun_term(noise_positions[active], direction)

        transmittance = 1.0
        optical_depth = 0.0
        scattered = np.zeros(3)
        weighted_distance = 0.0
        distance_weight = 0.0
        steps_taken = 0
        for index in range(step_count):
            if transmittance < MIN_TRANSMITTANCE:
                break
            steps_taken = index + 1
            if not active[index]:
                continue
            sigma = densities[index] * self.extinction
            step_transmittance = np.exp(-sigma * step_length)
            height = heights[index]
            ambient = sky_above * mix(0.6, 1.0, height) + 0.5 * ground * mix(1.0, 0.3, height)
            source = sigma * (self.sun_illuminance * sun_terms[index] + ambient)
            integrated = (source - source * step_transmittance) / max(sigma, 1.0e-7)
            absorbed = transmittance * (1.0 - step_transmittance)
            scattered = scattered + transmittance * integrated
            weighted_distance += absorbed * distances[index]
            distance_weight += absorbed
            transmittance *= step_transmittance
            optical_depth += sigma * step_length
        cloud_distance = weighted_distance / distance_weight if distance_weight > 0.0 else start
        opacity = float(np.clip(1.0 - transmittance, 0.0, 1.0))
        # 雲までの区間の大気: 雲の光を透過率で減らし、区間の散乱光を雲が覆う割合だけ足す（ComposeCloudRadianceWithDiagnostics）
        aerial_transmittance = np.ones(3)
        aerial_scattering = np.zeros(3)
        if not flags & DIAGNOSTIC_NO_HAZE and self.aerial_enabled and cloud_distance > 0.0:
            aerial_transmittance, aerial_scattering = self.aerial(direction, cloud_distance)
        haze = float(np.mean(aerial_transmittance))
        fade = 1.0 if flags & DIAGNOSTIC_NO_FADE else 1.0 - float(smoothstep(FADE_START_DISTANCE, MAX_MARCH_DISTANCE, start))
        radiance = (scattered * aerial_transmittance + aerial_scattering * opacity) * fade
        result.update({
            "marched": True, "start_distance": start, "end_distance": end, "step_count": step_count,
            "step_length": step_length, "steps_taken": steps_taken, "transmittance": transmittance,
            "optical_depth": optical_depth, "scattered": scattered, "cloud_distance": cloud_distance,
            "sky_above": sky_above, "ground_radiance": ground, "radiance": radiance, "fade": fade, "haze": haze,
            "aerial_transmittance": aerial_transmittance, "aerial_scattering": aerial_scattering,
            # 区間の全部の密度の積分（透過率の打ち切りの前。消散係数を掛けた光学的深さ）
            "optical_depth_full": float(np.sum(densities[active]) * step_length * self.extinction),
        })
        return result

    def phases(self, cosine):
        def henyey_greenstein(g):
            g2 = g * g
            denominator = max(1.0 + g2 - 2.0 * g * cosine, 1.0e-4)
            return (1.0 - g2) / (4.0 * PI * denominator * np.sqrt(denominator))
        return [mix(henyey_greenstein(0.8 * 0.5 ** octave), henyey_greenstein(-0.3 * 0.5 ** octave), 0.3)
                for octave in range(3)]

    def sun_term(self, noise_positions, direction):
        """太陽へ向かう光学的深さ（6 回、歩幅は 1.8 倍ずつ）と多重散乱の近似の和"""
        thickness = self.top - self.bottom
        count = noise_positions.shape[0]
        light_depth = np.zeros(count)
        alive = np.ones(count, dtype=bool)
        light_distance = 0.0
        light_step = LIGHT_FIRST_STEP
        for light_index in range(LIGHT_STEPS):
            sample_distance = light_distance + light_step * 0.5
            positions = noise_positions + self.sun_direction[None, :] * sample_distance
            heights = (positions[:, 1] - self.bottom) / thickness
            alive &= ~(heights > 1.0)
            use = alive & (heights >= 0.0)
            if np.any(use):
                light_depth[use] += self.density(positions[use], heights[use], light_index < 3) * light_step
            light_distance += light_step
            light_step *= LIGHT_STEP_GROWTH
        light_depth *= self.extinction
        phases = self.phases(float(np.dot(direction, self.sun_direction)))
        term = np.zeros(count)
        contribution = 1.0
        attenuation = 1.0
        for octave in range(3):
            term += contribution * np.exp(-light_depth * attenuation) * phases[octave]
            contribution *= 0.5
            attenuation *= 0.5
        return term


def read_array(capture_dir, entry, dtype, failures, label):
    path = os.path.join(capture_dir, entry.get("file", ""))
    if not os.path.isfile(path):
        failures.append(f"{label}: {entry.get('file')} が無い")
        return None
    data = np.fromfile(path, dtype=dtype)
    shape = tuple(int(n) for n in entry.get("shape", []))
    if data.size != int(np.prod(shape)):
        failures.append(f"{label}: 値の数 {data.size} が形 {shape} と合わない")
        return None
    return data.reshape(shape)


def relative_error(a, b, floor):
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    return float(np.max(np.abs(a - b) / np.maximum(np.abs(b), floor)))


def expected_constants(meta):
    """雲の層・時刻・フレーム番号から、契約の規則で定数を求め直す（CloudLayerPass.inl の BuildCloudLayerParams と同じ規則）"""
    cloud = meta["cloud_layer"]
    field = meta["density_field"]
    wind = field["wind"]
    weather = field["weather"]
    time = float(meta["time_seconds"])
    shape_scale = float(cloud["shape_scale_m"])
    detail_scale = shape_scale * float(field["detail_scale_ratio"])
    weather_scale = shape_scale * float(field["weather_scale_ratio"])
    wind_x, wind_z = (float(v) for v in cloud["wind_velocity_m_s"])
    ratio = float(wind["detail_wind_ratio"])
    rotation_cos, rotation_sin = (float(v) for v in weather["rotation_cos_sin"])
    high_scale = float(weather["high_scale"])
    phase_u, phase_w = (float(v) for v in weather["high_phase_uw"])

    def wrap_wind(velocity, period):
        return np.fmod(velocity * time, period)

    def wrap_unit(value):
        return value - np.floor(value)

    weather_u = (rotation_cos * wind_x * time - rotation_sin * wind_z * time) / weather_scale
    weather_v = (rotation_sin * wind_x * time + rotation_cos * wind_z * time) / weather_scale
    return {
        "inverse_scales": [1.0 / shape_scale, 1.0 / detail_scale, 1.0 / weather_scale],
        "shape_offset": [wrap_wind(wind_x, shape_scale), wrap_wind(wind_z, shape_scale)],
        "weather_offset": [wrap_unit(weather_u), wrap_unit(weather_v)],
        "weather_high_offset": [wrap_unit(weather_v * high_scale + phase_u), wrap_unit(weather_u * high_scale + phase_w)],
        "detail_offset": [wrap_wind(wind_x * ratio, detail_scale), wrap_wind(float(wind["detail_rise_velocity_m_s"]), detail_scale),
                          wrap_wind(wind_z * ratio, detail_scale)],
        "frame_phase": float(int(meta["temporal_frame_index"]) % 1024),
        "layer": [float(meta["sky"]["planet_radius_m"]), float(cloud["bottom_altitude_m"]), float(cloud["top_altitude_m"]),
                  float(cloud["coverage"])],
        "extinction": float(cloud["extinction_per_m"]),
    }


def check_constants(meta, failures):
    params = meta["shader_params"]
    expected = expected_constants(meta)
    worst = {}

    def compare(label, actual, wanted, periodic=False):
        actual = np.asarray(actual, dtype=np.float64)
        wanted = np.asarray(wanted, dtype=np.float64)
        difference = np.abs(actual - wanted)
        if periodic:
            # 0〜1 に折り返した値は、0 と 1 の際で折り返しの側が違っても同じ位置
            difference = np.minimum(difference, np.abs(1.0 - difference))
        error = float(np.max(difference / np.maximum(np.abs(wanted), 1.0)))
        worst[label] = error
        if not error <= CONSTANT_TOLERANCE:
            failures.append(f"定数 {label} が契約から求め直した値と合わない（{actual.tolist()} / {wanted.tolist()}）")

    scales = params["extinction_and_inverse_scales"]
    compare("inverse_scales", np.asarray(scales[1:4]) * 1.0e4, np.asarray(expected["inverse_scales"]) * 1.0e4)
    compare("extinction", scales[0] * 1.0e3, expected["extinction"] * 1.0e3)
    compare("shape_offset", params["shape_and_weather_offset"][:2], expected["shape_offset"])
    compare("weather_offset", params["shape_and_weather_offset"][2:4], expected["weather_offset"], periodic=True)
    compare("weather_high_offset", params["weather_high_offset"][:2], expected["weather_high_offset"], periodic=True)
    compare("detail_offset", params["detail_offset"][:3], expected["detail_offset"])
    compare("frame_phase", params["sun_direction_and_frame_index"][3], expected["frame_phase"])
    compare("layer", params["layer_altitudes_and_coverage"], expected["layer"])
    return worst


def noise_log_hashes(log_text):
    """Game のログの CLOUD_NOISE の行（描画のパスと書き出しのパス）の FNV-1a の組"""
    pairs = set()
    for match in re.finditer(r"CLOUD_NOISE shape=\d+ detail=\d+ generate_ms=[0-9.]+ shape_fnv1a64=([0-9a-f]{16}) "
                             r"detail_fnv1a64=([0-9a-f]{16})", log_text):
        pairs.add((match.group(1), match.group(2)))
    return pairs


def check_view(rc, capture_dir, view, game_args):
    failures = []
    result = {"view": view}
    meta_path = os.path.join(capture_dir, f"{view}.cloud.json")
    if not os.path.isfile(meta_path):
        return {"view": view, "pass": False, "failures": [f"{view}.cloud.json が無い"]}
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    if meta.get("format") != FORMAT:
        failures.append(f"形式が違う（{meta.get('format')}）")
    if GAME_ARG not in (game_args or []):
        failures.append(f"撮影の起動引数に {GAME_ARG} が無い")
    log_name = rc.view_game_log(capture_dir, view)
    log_text = ""
    if log_name and os.path.isfile(os.path.join(capture_dir, log_name)):
        with open(os.path.join(capture_dir, log_name), encoding="utf-8", errors="replace") as f:
            log_text = f.read()
    for line in LOG_LINES + (f"CAPTURE_VIEWS view={view} clouds saved ",):
        if line not in log_text:
            failures.append(f"Game のログに「{line.strip()}」が無い")

    # 1. ファイルと格子の照合
    field = meta.get("density_field", {})
    shape_noise = read_array(capture_dir, field.get("shape_noise", {}), np.uint8, failures, "基本の形の 3D ノイズ")
    detail_noise = read_array(capture_dir, field.get("detail_noise", {}), np.uint8, failures, "細部の 3D ノイズ")
    aerial_profile = read_array(capture_dir, meta.get("aerial_profile", {}), "<f4", failures, "空気遠近の高度ごとの表")
    probe = None
    sky_lut = None
    probe_entry = meta.get("probe", {})
    lut_entry = meta.get("sky_radiance_lut", {})
    for entry, label in ((probe_entry, "視線の値"), (lut_entry, "空の放射輝度 LUT")):
        values, reason = rc.atmos_read_floats(capture_dir, entry)
        if reason:
            failures.append(f"{label}: {reason}")
            continue
        if not np.all(np.isfinite(values)):
            failures.append(f"{label}: NaN か Inf がある")
        if label == "視線の値":
            probe = values
        else:
            sky_lut = values
    log_pairs = noise_log_hashes(log_text)
    result["noise_log_hashes"] = sorted(list(pair) for pair in log_pairs)
    if shape_noise is not None and detail_noise is not None:
        file_pair = (f"{fnv1a64_fast(shape_noise):016x}", f"{fnv1a64_fast(detail_noise):016x}")
        json_pair = (field["shape_noise"].get("fnv1a64"), field["detail_noise"].get("fnv1a64"))
        result["noise_file_hashes"] = list(file_pair)
        if file_pair != json_pair:
            failures.append(f"3D ノイズのファイルの FNV-1a {file_pair} が JSON の値 {json_pair} と違う")
        if not log_pairs:
            failures.append("Game のログに CLOUD_NOISE の FNV-1a が無い")
        elif log_pairs != {file_pair}:
            failures.append(f"Game のログの CLOUD_NOISE の FNV-1a {sorted(log_pairs)} が書き出しの格子 {file_pair} と違う")
    if probe is None or shape_noise is None or detail_noise is None or sky_lut is None:
        result.update({"pass": False, "failures": failures})
        return result
    if probe.shape[0] != PROBE_ROWS or probe.shape[1] != len(probe_entry.get("pixels", [])):
        failures.append(f"視線の値の形 {list(probe.shape)} が約束（{PROBE_ROWS} 行 × 視線 {len(probe_entry.get('pixels', []))}）と違う")
        result.update({"pass": False, "failures": failures})
        return result

    # 2. 定数が契約どおり
    result["constants_max_relative_error"] = check_constants(meta, failures)

    # 3〜5. 視線ごとに求め直す
    model = CloudModel(meta, shape_noise.astype(np.float64) / 255.0, detail_noise.astype(np.float64) / 255.0, sky_lut,
                       aerial_profile)
    target = (int(meta["target"]["width"]), int(meta["target"]["height"]))
    rays = []
    worst = {"transmittance_abs": 0.0, "direction": 0.0, "jitter": 0.0, "distance_rel": 0.0, "optical_depth_rel": 0.0,
             "scattered_rel": 0.0, "radiance_rel": 0.0, "sky_above_rel": 0.0, "ground_rel": 0.0, "background_rel": 0.0}
    cloud_rays = 0
    thick_rays = 0
    non_sky = 0
    # 反実仮想の列（診断の切り替えを立てた列）は ReferenceCheck.py --cloud-far が見る
    for index in grid_columns(probe):
        pixel = probe_entry["pixels"][index]
        gpu = {"transmittance": probe[0, index, 0], "alpha": probe[0, index, 1], "fade": probe[0, index, 2],
               "steps_taken": int(round(probe[0, index, 3])), "scattered": probe[1, index, :3],
               "cloud_distance": probe[1, index, 3], "radiance": probe[2, index, :3], "direction": probe[3, index, :3],
               "jitter": probe[3, index, 3], "sky_above": probe[4, index, :3], "start": probe[4, index, 3],
               "ground": probe[5, index, :3], "end": probe[5, index, 3], "background": probe[6, index, :3],
               "depth": probe[6, index, 3], "optical_depth": probe[7, index, 0], "step_count": int(round(probe[7, index, 1])),
               "step_length": probe[7, index, 2], "flags": int(round(probe[7, index, 3]))}
        direction = model.ray_direction(pixel, target)
        jitter = model.jitter(pixel)
        march = model.march(direction, jitter)
        sky_pixel = not (np.isfinite(gpu["depth"]) and gpu["depth"] < 0.999999)
        if not sky_pixel:
            non_sky += 1
        gpu_marched = bool(gpu["flags"] & 1)
        opacity = float(np.clip(1.0 - gpu["transmittance"], 0.0, 1.0))
        cloud_rays += int(opacity >= CLOUD_OPACITY)
        thick_rays += int(opacity >= THICK_OPACITY)
        transmittance_error = abs(float(march["transmittance"]) - float(gpu["transmittance"]))
        worst["transmittance_abs"] = max(worst["transmittance_abs"], transmittance_error)
        worst["direction"] = max(worst["direction"], float(np.max(np.abs(direction - gpu["direction"]))))
        worst["jitter"] = max(worst["jitter"], abs(jitter - float(gpu["jitter"])))
        entry = {"pixel": pixel, "sky_pixel": sky_pixel, "gpu_transmittance": float(gpu["transmittance"]),
                 "python_transmittance": float(march["transmittance"]), "transmittance_abs_error": transmittance_error,
                 "gpu_alpha": float(gpu["alpha"]), "gpu_steps_taken": gpu["steps_taken"],
                 "python_steps_taken": int(march["steps_taken"])}
        if gpu_marched != march["marched"]:
            failures.append(f"視線 {index}（画素 {pixel}）: 雲の層を進めたか（GPU {int(gpu_marched)}・Python {int(march['marched'])}）が違う")
        if march["marched"] and gpu_marched:
            if march["step_count"] != gpu["step_count"]:
                failures.append(f"視線 {index}: 歩数 {march['step_count']} が GPU の {gpu['step_count']} と違う")
            distance_error = max(abs(march["start_distance"] - gpu["start"]) / max(abs(gpu["start"]), 1.0),
                                 abs(march["end_distance"] - gpu["end"]) / max(abs(gpu["end"]), 1.0),
                                 abs(march["step_length"] - gpu["step_length"]) / max(abs(gpu["step_length"]), 1.0))
            worst["distance_rel"] = max(worst["distance_rel"], distance_error)
            worst["optical_depth_rel"] = max(worst["optical_depth_rel"],
                                             abs(march["optical_depth"] - gpu["optical_depth"]) / max(gpu["optical_depth"], 0.1))
            # 照明の写し（記録）
            worst["sky_above_rel"] = max(worst["sky_above_rel"], relative_error(march["sky_above"], gpu["sky_above"], RADIANCE_FLOOR))
            worst["ground_rel"] = max(worst["ground_rel"], relative_error(march["ground_radiance"], gpu["ground"], RADIANCE_FLOOR))
            if opacity >= CLOUD_OPACITY:
                worst["scattered_rel"] = max(worst["scattered_rel"],
                                             relative_error(march["scattered"], gpu["scattered"], RADIANCE_FLOOR))
                worst["radiance_rel"] = max(worst["radiance_rel"],
                                            relative_error(march["radiance"], gpu["radiance"], RADIANCE_FLOOR))
            entry.update({"start_distance_m": float(gpu["start"]), "step_count": gpu["step_count"],
                          "gpu_optical_depth": float(gpu["optical_depth"]), "python_optical_depth": float(march["optical_depth"]),
                          "gpu_scattered": gpu["scattered"].tolist(), "python_scattered": march["scattered"].tolist()})
        worst["background_rel"] = max(worst["background_rel"],
                                      relative_error(sample_sky(model.sky_lut, direction), gpu["background"], RADIANCE_FLOOR))
        rays.append(entry)

    if non_sky:
        failures.append(f"空でない画素の視線が {non_sky} 本ある（空だけを写す視点でない）")
    if cloud_rays < MIN_CLOUD_RAYS or thick_rays < MIN_THICK_RAYS:
        failures.append(f"雲のある視線が足りない（不透明度 {CLOUD_OPACITY} 以上 {cloud_rays} 本 / 下限 {MIN_CLOUD_RAYS}、"
                        f"{THICK_OPACITY} 以上 {thick_rays} 本 / 下限 {MIN_THICK_RAYS}）。検査が成り立たない")
    if not worst["direction"] <= DIRECTION_TOLERANCE:
        failures.append(f"画素から求め直した視線の向きが GPU と合わない（最大の差 {worst['direction']:.3g}）")
    if not worst["jitter"] <= JITTER_TOLERANCE:
        failures.append(f"画素とフレームから求め直した歩の端数が GPU と合わない（最大の差 {worst['jitter']:.3g}）")
    if not worst["distance_rel"] <= DISTANCE_RELATIVE_TOLERANCE:
        failures.append(f"雲の層の区間・1 歩の長さが GPU と合わない（最大の相対差 {worst['distance_rel']:.3g}）")
    if not worst["transmittance_abs"] <= TRANSMITTANCE_TOLERANCE:
        failures.append(f"雲の透過率が GPU と {TRANSMITTANCE_TOLERANCE} を超えて違う（最大の差 {worst['transmittance_abs']:.4f}）")
    result.update({
        "rays": len(rays), "cloud_rays": cloud_rays, "thick_rays": thick_rays, "worst": worst,
        "gpu_transmittance_range": [min(r["gpu_transmittance"] for r in rays), max(r["gpu_transmittance"] for r in rays)],
        "frame_number": meta.get("frame_number"), "time_seconds": meta.get("time_seconds"),
        "temporal_frame_index": meta.get("temporal_frame_index"), "ray_results": rays,
        "pass": not failures, "failures": failures,
    })
    return result


def run(rc, capture_dir, views, verify):
    failures = []
    inputs_ok, input_failures, record = rc.check_inputs(capture_dir)
    if verify and not inputs_ok:
        failures.extend(input_failures)
    metric_views = rc.views_from_metrics(capture_dir)
    game_args = rc.plate_capture_args(capture_dir)
    if metric_views is None or game_args is None:
        failures.append("metrics.json が無い（CaptureScene.ps1 の出力先でない）")
        metric_views = []
    targets = views if views else metric_views
    if not targets:
        failures.append("検査する視点が無い")
    results = []
    for view in targets:
        if view not in metric_views:
            failures.append(f"{view}: metrics.json に無い視点")
            continue
        result = check_view(rc, capture_dir, view, game_args)
        results.append(result)
        worst = result.get("worst", {})
        print(f"REFERENCE_CHECK cloud_dump view={view} pass={int(result['pass'])} rays={result.get('rays')} "
              f"cloud_rays={result.get('cloud_rays')} thick_rays={result.get('thick_rays')} "
              f"transmittance_max_abs={worst.get('transmittance_abs')} direction={worst.get('direction')} "
              f"jitter={worst.get('jitter')} distance_rel={worst.get('distance_rel')} "
              f"optical_depth_rel={worst.get('optical_depth_rel')} scattered_rel={worst.get('scattered_rel')} "
              f"radiance_rel={worst.get('radiance_rel')} sky_above_rel={worst.get('sky_above_rel')} "
              f"ground_rel={worst.get('ground_rel')} background_rel={worst.get('background_rel')} "
              f"noise={result.get('noise_file_hashes')} inputs_current={int(bool(inputs_ok))}")
        failures.extend(f"{view}: {failure}" for failure in result["failures"])
    report = {
        "mode": "cloud_dump",
        "verify": bool(verify),
        "inputs_current": inputs_ok,
        "inputs_record": record,
        "game_args": game_args,
        "criteria": {
            "transmittance_tolerance": TRANSMITTANCE_TOLERANCE, "cloud_opacity": CLOUD_OPACITY,
            "min_cloud_rays": MIN_CLOUD_RAYS, "thick_opacity": THICK_OPACITY, "min_thick_rays": MIN_THICK_RAYS,
            "constant_tolerance": CONSTANT_TOLERANCE, "direction_tolerance": DIRECTION_TOLERANCE,
            "jitter_tolerance": JITTER_TOLERANCE, "distance_relative_tolerance": DISTANCE_RELATIVE_TOLERANCE,
        },
        "views": results,
        "failures": failures,
    }
    report_path = os.path.join(capture_dir, "cloud_dump_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False, default=float)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=cloud_dump views={len(targets)} inputs_current={int(bool(inputs_ok))} "
          f"report={report_path}")
    return 0 if not failures else 1
