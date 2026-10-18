# 空と霞の数値の正解（SCN-WLR-ATMOS-REF）。エンジンのコードを写さず、Hillaire 2020「A Scalable and Production Ready Sky and
# Atmosphere Rendering Technique」の定義（惑星と大気の同心球、Rayleigh・Mie の指数分布、オゾンの三角形の層）から、
# 空の放射輝度・透過率・多重散乱・地表の照度を numpy で数値積分する。
#
# 使い方:
#   python Scripts/Terrain/AtmosphereReference.py --verify [--dump <視点>.atmos.json] [--out <出力先>]
#       論文の設定と、荒野の書き出し（--dump。省くと SCN-WLR-ATMOS-DUMP の ref_ground_02）の設定の 2 組で、
#       正解を段 1 と、歩数・方向の数・表の細かさをすべて倍にした段 2 で求め、比べる量がすべて CONVERGENCE_TOLERANCE 以内で
#       一致することを確かめる。ほかに、鉛直の光学的深さの解析解との一致・多重散乱の級数が収束することを見る。
#       結果を <出力先>/atmosphere_reference_verify.json に書く。
#   ほかのスクリプト（ReferenceCheck.py --atmos-ref）は import して使う。
#
# 媒質（高度 h の m、1/m）:
#   Rayleigh: 散乱 β_R × exp(-h / H_R)（吸収なし）。Mie: 散乱 β_M・吸収 α_M × exp(-h / H_M)。
#   オゾン: 吸収 β_O × max(0, 1 - |h - 25 km| / 15 km)。大気の上端より上は真空。
#   位相関数: Rayleigh は 3 (1 + cos²θ) / (16π)、Mie は Henyey-Greenstein（論文の公開実装と同じ）。
#   cosθ は視線の向き（観測点から離れる向き）と太陽の向き（観測点から太陽へ）の内積（太陽を見る向きで前方散乱）。
#   太陽は大気の外の照度 1 lux の点光源とみなす（値はすべて太陽の照度 1 lux あたり。エンジンと比べるときに掛ける）。
#
# 多重散乱（2 次以上）は 2 つの版で求める:
#   A（論文の近似）: 2 次以上の散乱を等方とし、点 x の周りの点がすべて x と同じ光を受けるとみなす。
#      L_2nd(x) = 球の平均 ∫ T(x,p) σs(p) T_sun(p) / (4π) dt（＋地表で返る太陽）、f_ms(x) = 球の平均 ∫ T(x,p) σs(p) dt、
#      Ψ_ms = L_2nd / (1 - f_ms)。視線の上では σs(p) Ψ_ms(p) を散乱の源に足す。
#   B（散乱の回数で足す版）: 2 次以上の散乱を等方とする所は同じで、周りの点が同じ光を受けるとはみなさない。
#      高度 × 太陽の天頂角の余弦の表の上で、n 回目の散乱の光の球の平均 L̄_n と地表の散乱光の照度 E_n を求め、
#      L̄_{n+1}(x) = 球の平均 [∫ T(x,p) σs(p) L̄_n(p) dt ＋ 地表に当たれば T × albedo / π × E_n]、1 回目は本当の位相関数で
#      太陽の光を数える。L̄_n の和が Ψ（σs Ψ を視線の源に足す）。回ごとの比を論文の f_ms（等比の仮定）と並べて記録する。
#
# 数値の積分: 視線は中点則（空への視線は観測点の近くを細かく取る 2 乗の並び、地表や区間の視線は等間隔）。
# 太陽の透過率は Bruneton 2017 の (r, μ) の並び（地平線の近くを細かく取る）の表を線形に引く。
# 段 2 は段 1 の歩数・方向の数・表の細かさをすべて倍にしたもので、比べる量の差が CONVERGENCE_TOLERANCE 以内なら収束とする。
import argparse
import json
import math
import os
import sys
import time

import numpy as np

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_DUMP = os.path.join(REPO_ROOT, ".harness", "runs", "scene-capture", "SCN-WLR-ATMOS-DUMP", "ref_ground_02.atmos.json")
DEFAULT_OUT = os.path.join(REPO_ROOT, ".harness", "runs", "scene-capture", "SCN-WLR-ATMOS-REF")

# 段 2 と段 1 の相対差の上限（0.5 %）と、相対差の分母の下限（太陽 1 lux あたりの cd/m²・透過率）
CONVERGENCE_TOLERANCE = 0.005
CONVERGENCE_RADIANCE_FLOOR = 1e-4
# 鉛直の光学的深さの解析解との相対差の上限
ANALYTIC_TOLERANCE = 1e-3
# 散乱の回数で足す版を打ち切る、その回の光の和に対する割合と、回数の上限
ORDER_STOP_FRACTION = 1e-9
ORDER_LIMIT = 200
# B の 1 回目で、方位の 1 つの区画の中の Mie の位相関数を平均する分け数
PHASE_AZIMUTH_SUBDIVISIONS = 16

# Hillaire 2020 の論文と公開実装（github.com/sebh/UnrealEngineSkyAtmosphere の SkyAtmosphereCommon.cpp の既定値）の地球の大気。
# Mie は散乱 3.996e-3 /km・消散 4.440e-3 /km（吸収は差の 0.444e-3 /km）、オゾンは 10〜40 km の三角形の層。
PAPER_SETTINGS = {
    "name": "hillaire2020",
    "planet_radius_m": 6360000.0,
    "atmosphere_height_m": 100000.0,
    "rayleigh_scattering_per_m": [5.802e-6, 13.558e-6, 33.100e-6],
    "rayleigh_scale_height_m": 8000.0,
    "mie_scattering_per_m": 3.996e-6,
    "mie_absorption_per_m": 0.444e-6,
    "mie_scale_height_m": 1200.0,
    "mie_anisotropy": 0.8,
    "ozone_absorption_per_m": [0.650e-6, 1.881e-6, 0.085e-6],
    "ozone_center_m": 25000.0,
    "ozone_half_width_m": 15000.0,
    "ground_albedo": [0.0, 0.0, 0.0],
}
# エンジンの設定のうち書き出しの JSON に無いもの（SkyAtmosphere.cpp の設定値）: Mie の散乱と吸収の分け方（散乱 1.0e-4・吸収 1.11e-5 /m）。
# オゾンは論文と同じ値を使っている
ENGINE_MIE_SCATTERING_FRACTION = 1.0e-4 / (1.0e-4 + 1.11e-5)

# 計算の細かさ（段 1。段 2 はすべて倍）
LEVEL1 = {
    "table_radius": 64, "table_cosine": 128, "table_steps": 256,
    "field_altitude": 24, "field_cosine": 24, "field_polar": 16, "field_azimuth": 4, "field_steps": 96,
    "view_steps": 256, "hemisphere_elevation": 24, "hemisphere_azimuth": 48,
}
LUMINANCE_WEIGHTS = np.array([0.2126, 0.7152, 0.0722])
# Ψ の値を比べる点（高度 m・太陽の天頂角の余弦）。荒野の視点の空と霞の視線が通る 10 km より下と、太陽の高度 38° の前後
# （余弦 0.616。1000 km 先の視線の点では球の丸みで ±0.15 ずれる）に限る。15 km より上と太陽の低い所は段 2 でも段の差が
# 1〜2 % 残る（空の放射輝度はその高さの Ψ の寄与が小さく、0.5 % 以内に収まる）
PSI_PROBE_ALTITUDES = (0.0, 233.0, 1866.0, 6297.0, 10000.0)
PSI_PROBE_COSINES = (0.3, 0.45, 0.6, 0.75, 0.9)


def level_parameters(level):
    scale = 2 ** (level - 1)
    return {key: value * scale for key, value in LEVEL1.items()}


def settings_from_dump(meta, mie_density_scale=1.0):
    """書き出しの JSON（<視点>.atmos.json）の空のパラメータを、この台本の設定の形にする"""
    sky = meta["sky"]
    mie_extinction = float(sky["ground_extinction_mie_per_m"]) * mie_density_scale
    return {
        "name": "engine",
        "planet_radius_m": float(sky["planet_radius_m"]),
        "atmosphere_height_m": float(sky["atmosphere_height_m"]),
        # Rayleigh は散乱だけなので、地表の消散係数がそのまま散乱係数
        "rayleigh_scattering_per_m": [float(v) for v in sky["ground_extinction_rayleigh_per_m"]],
        "rayleigh_scale_height_m": float(sky["rayleigh_scale_height_m"]),
        "mie_scattering_per_m": mie_extinction * ENGINE_MIE_SCATTERING_FRACTION,
        "mie_absorption_per_m": mie_extinction * (1.0 - ENGINE_MIE_SCATTERING_FRACTION),
        "mie_scale_height_m": float(sky["mie_scale_height_m"]),
        "mie_anisotropy": float(sky["mie_anisotropy"]),
        "ozone_absorption_per_m": list(PAPER_SETTINGS["ozone_absorption_per_m"]),
        "ozone_center_m": PAPER_SETTINGS["ozone_center_m"],
        "ozone_half_width_m": PAPER_SETTINGS["ozone_half_width_m"],
        "ground_albedo": [float(v) for v in sky["ground_albedo"]],
    }


def with_changes(settings, name, **changes):
    result = dict(settings)
    result.update(changes)
    result["name"] = name
    return result


class Medium:
    """高度ごとの散乱係数と消散係数"""

    def __init__(self, settings):
        self.settings = settings
        self.bottom = settings["planet_radius_m"]
        self.top = self.bottom + settings["atmosphere_height_m"]
        self.rayleigh = np.array(settings["rayleigh_scattering_per_m"], dtype=np.float64)
        self.mie_scattering = float(settings["mie_scattering_per_m"])
        self.mie_extinction = self.mie_scattering + float(settings["mie_absorption_per_m"])
        self.ozone = np.array(settings["ozone_absorption_per_m"], dtype=np.float64)
        self.albedo = np.array(settings["ground_albedo"], dtype=np.float64)
        self.g = float(settings["mie_anisotropy"])

    def densities(self, altitude):
        h = np.maximum(altitude, 0.0)
        inside = altitude <= self.settings["atmosphere_height_m"]
        rayleigh = np.where(inside, np.exp(-h / self.settings["rayleigh_scale_height_m"]), 0.0)
        mie = np.where(inside, np.exp(-h / self.settings["mie_scale_height_m"]), 0.0)
        ozone = np.where(inside, np.maximum(
            0.0, 1.0 - np.abs(h - self.settings["ozone_center_m"]) / self.settings["ozone_half_width_m"]), 0.0)
        return rayleigh, mie, ozone

    def coefficients(self, altitude, mie_scale=1.0):
        """(Rayleigh の散乱 [...,3], Mie の散乱 [...], 消散 [...,3])"""
        rayleigh, mie, ozone = self.densities(altitude)
        mie = mie * mie_scale
        rayleigh_scattering = rayleigh[..., None] * self.rayleigh
        mie_scattering = mie * self.mie_scattering
        extinction = rayleigh_scattering + (mie * self.mie_extinction)[..., None] + ozone[..., None] * self.ozone
        return rayleigh_scattering, mie_scattering, extinction

    def rayleigh_phase(self, cosine):
        return 3.0 * (1.0 + cosine * cosine) / (16.0 * math.pi)

    def mie_phase(self, cosine, g=None):
        g = self.g if g is None else g
        denominator = np.maximum(1.0 + g * g - 2.0 * g * cosine, 1e-12)
        return (1.0 - g * g) / (4.0 * math.pi * denominator * np.sqrt(denominator))


def ray_end(radius, cosine, bottom, top):
    """半径 radius の点から天頂との余弦 cosine の向きに進み、地表か大気の上端に着くまでの距離と、地表に当たるか"""
    discriminant_top = radius * radius * (cosine * cosine - 1.0) + top * top
    to_top = -radius * cosine + np.sqrt(np.maximum(discriminant_top, 0.0))
    discriminant_ground = radius * radius * (cosine * cosine - 1.0) + bottom * bottom
    hits_ground = (cosine < 0.0) & (discriminant_ground >= 0.0)
    to_ground = np.maximum(-radius * cosine - np.sqrt(np.maximum(discriminant_ground, 0.0)), 0.0)
    return np.where(hits_ground, to_ground, np.maximum(to_top, 0.0)), hits_ground


def ray_step_bounds(length, hits_ground, cosine, steps):
    """視線の歩の境（[..., steps + 1]）。空へ上がる視線は始まりの近く（濃い所）を細かく取る 2 乗の並び、地表に当たる視線は
    終わりの近くを細かく取る並び、下向きに出て地表をかすめて上がる視線は、いちばん低い所が途中にあるので等間隔"""
    edges = np.arange(steps + 1) / steps
    shape = np.where(hits_ground[..., None], 1.0 - (1.0 - edges) ** 2,
                     np.where((cosine < 0.0)[..., None], edges, edges ** 2))
    return shape * length[..., None]


class TransmittanceTable:
    """大気の上端までの光学的深さの表。並びは Bruneton 2017 の (x_r, x_μ)（地平線の近くを細かく取る）"""

    def __init__(self, medium, radius_count, cosine_count, steps):
        self.medium = medium
        bottom, top = medium.bottom, medium.top
        self.horizon = math.sqrt(top * top - bottom * bottom)
        x_r = np.linspace(0.0, 1.0, radius_count)
        x_mu = np.linspace(0.0, 1.0, cosine_count)
        rho = self.horizon * x_r[:, None]
        radius = np.sqrt(rho * rho + bottom * bottom)
        d_min = top - radius
        d_max = rho + self.horizon
        distance = d_min + x_mu[None, :] * (d_max - d_min)
        with np.errstate(divide="ignore", invalid="ignore"):
            cosine = np.where(distance <= 0.0, 1.0,
                              (self.horizon ** 2 - rho * rho - distance * distance) / (2.0 * radius * distance))
        cosine = np.clip(cosine, -1.0, 1.0)
        fractions = (np.arange(steps) + 0.5) / steps
        depth = np.zeros(distance.shape + (3,))
        for k in range(steps):
            t = distance * fractions[k]
            r = np.sqrt(radius * radius + t * t + 2.0 * radius * cosine * t)
            _, _, extinction = medium.coefficients(r - bottom)
            depth += extinction * (distance / steps)[..., None]
        self.depth = depth
        self.shape = depth.shape[:2]

    def optical_depth(self, radius, cosine):
        """大気の上端までの光学的深さ（RGB）。地表に当たる向きは inf"""
        bottom, top = self.medium.bottom, self.medium.top
        radius = np.clip(radius, bottom, top)
        cosine = np.clip(cosine, -1.0, 1.0)
        rho = np.sqrt(np.maximum(radius * radius - bottom * bottom, 0.0))
        discriminant = radius * radius * (cosine * cosine - 1.0) + top * top
        distance = np.maximum(-radius * cosine + np.sqrt(np.maximum(discriminant, 0.0)), 0.0)
        d_min = top - radius
        d_max = rho + self.horizon
        x_mu = np.clip((distance - d_min) / np.maximum(d_max - d_min, 1e-9), 0.0, 1.0)
        x_r = np.clip(rho / self.horizon, 0.0, 1.0)
        fr = x_r * (self.shape[0] - 1)
        fm = x_mu * (self.shape[1] - 1)
        i0 = np.minimum(np.floor(fr).astype(np.int64), self.shape[0] - 2)
        j0 = np.minimum(np.floor(fm).astype(np.int64), self.shape[1] - 2)
        a = (fr - i0)[..., None]
        b = (fm - j0)[..., None]
        value = ((1 - a) * (1 - b) * self.depth[i0, j0] + (1 - a) * b * self.depth[i0, j0 + 1] +
                 a * (1 - b) * self.depth[i0 + 1, j0] + a * b * self.depth[i0 + 1, j0 + 1])
        discriminant_ground = radius * radius * (cosine * cosine - 1.0) + bottom * bottom
        blocked = (cosine < 0.0) & (discriminant_ground >= 0.0)
        return np.where(blocked[..., None], np.inf, value)

    def transmittance(self, radius, cosine):
        return np.exp(-self.optical_depth(radius, cosine))


def horizon_split_directions(radius, bottom, polar_count, azimuth_count):
    """半径 radius の点の球の求積。天頂角の余弦を地平線（地表に当たるかの境）で 2 つに分け、それぞれをガウス・ルジャンドル
    （polar_count 個ずつ）で取り、方位は太陽の方位（+x）からの半周（0〜π）を等間隔で取る（太陽を含む面について対称なので
    半周の 2 倍）。戻り値は向き [N,D,3] と重み [N,D]（球の平均の重みで、和が 1）"""
    nodes, weights = np.polynomial.legendre.leggauss(polar_count)
    horizon = -np.sqrt(np.maximum(1.0 - (bottom / radius) ** 2, 0.0))
    low = np.stack([np.full_like(horizon, -1.0), horizon], -1)
    high = np.stack([horizon, np.ones_like(horizon)], -1)
    mu_parts = []
    weight_parts = []
    for a, b in ((low[:, 0], low[:, 1]), (high[:, 0], high[:, 1])):
        mu_parts.append(0.5 * (b - a)[:, None] * nodes[None, :] + 0.5 * (a + b)[:, None])
        weight_parts.append((b - a)[:, None] * weights[None, :] / (4.0 * azimuth_count))
    mu = np.concatenate(mu_parts, axis=1)
    mu_weight = np.concatenate(weight_parts, axis=1)
    azimuth = (np.arange(azimuth_count) + 0.5) * math.pi / azimuth_count
    mu = np.repeat(mu, azimuth_count, axis=1)
    weight = np.repeat(mu_weight, azimuth_count, axis=1)
    phi = np.tile(azimuth, 2 * polar_count)[None, :]
    ring = np.sqrt(np.maximum(1.0 - mu * mu, 0.0))
    directions = np.stack([ring * np.cos(phi), mu, ring * np.sin(phi)], -1)
    return directions, weight


class ScatteringField:
    """多重散乱の表（高度 × 太陽の天頂角の余弦）。A は論文の近似の Ψ_ms、B は散乱の回数で足した Ψ"""

    def __init__(self, medium, table, altitude_count, cosine_count, polar_count, azimuth_count, steps):
        self.medium = medium
        self.table = table
        self.altitude_count = altitude_count
        self.cosine_count = cosine_count
        self.polar_count = polar_count
        self.azimuth_count = azimuth_count
        self.steps = steps
        height = medium.settings["atmosphere_height_m"]
        u = np.linspace(0.0, 1.0, altitude_count)
        self.altitudes = height * u * u
        self.cosines = np.linspace(-1.0, 1.0, cosine_count)
        points = altitude_count * cosine_count
        self.points = points
        self.states = points + cosine_count

        l_2nd = np.zeros((points, 3))
        f_ms = np.zeros((points, 3))
        first = np.zeros((self.states, 3))
        matrix = np.zeros((3, self.states, self.states))
        batch = self.batch_size()
        for start in range(0, points, batch):
            index = np.arange(start, min(start + batch, points))
            rows = self.point_rows(self.altitudes[index // cosine_count], self.cosines[index % cosine_count])
            l_2nd[index] = rows["l_2nd"]
            f_ms[index] = rows["f_ms"]
            first[index] = rows["first"]
            matrix[:, index] = rows["rows"]
            # 地表の点の行は、地表の散乱光の照度の欄にもなる
            for local_index in np.nonzero(rows["on_ground"])[0]:
                j = index[local_index] % cosine_count
                first[points + j] = rows["first_ground"][local_index]
                matrix[:, points + j] = rows["ground_rows"][:, local_index]

        self.f_ms = f_ms
        self.l_2nd = l_2nd
        self.psi_a = l_2nd / np.maximum(1.0 - f_ms, 1e-6)
        # 回数で足す
        total = first.copy()
        current = first.copy()
        ratios = []
        orders = 1
        while orders < ORDER_LIMIT:
            following = np.stack([matrix[c] @ current[:, c] for c in range(3)], axis=-1)
            orders += 1
            total += following
            ratio = float(np.sum(following[:points] @ LUMINANCE_WEIGHTS) /
                          max(np.sum(current[:points] @ LUMINANCE_WEIGHTS), 1e-30))
            ratios.append(ratio)
            current = following
            if np.max(np.abs(following)) <= ORDER_STOP_FRACTION * np.max(np.abs(total)):
                break
        self.orders = orders
        self.order_ratios = ratios
        self.total = total
        self.psi_b = total[:points]
        self.ground_diffuse_b = total[points:]
        self.first_order = first[:points]
        # 同じ線形の式を直接解いた値（回数で足した和の確かめ）
        solved = np.stack([np.linalg.solve(np.eye(self.states) - matrix[c], first[:, c]) for c in range(3)], axis=-1)
        self.series_vs_solve = float(np.max(np.abs(solved - total) / np.maximum(np.abs(solved), 1e-12)))

    def batch_size(self):
        return max(1, 4096 * 64 // (2 * self.polar_count * self.azimuth_count * self.steps))

    def point_rows(self, altitude, sun_cosine):
        """点（高度 altitude、太陽の天頂角の余弦 sun_cosine。どちらも [N]）の、A の L_2nd と f_ms、B の 1 回目の光と
        次の回への行（表の状態への線形の重み）。地表の点は、上向きの余弦の重みの付いた積分（地表の散乱光の照度）の値と行も返す"""
        medium = self.medium
        bottom = medium.bottom
        steps = self.steps
        states = self.states
        points = self.points
        albedo_over_pi = medium.albedo / math.pi
        uniform_phase = 1.0 / (4.0 * math.pi)
        count = len(altitude)
        radius = bottom + altitude
        sun = np.stack([np.sqrt(np.maximum(1.0 - sun_cosine ** 2, 0.0)), sun_cosine, np.zeros_like(sun_cosine)], -1)
        directions, direction_weight = horizon_split_directions(radius, bottom, self.polar_count, self.azimuth_count)
        length, hits_ground = ray_end(radius[:, None], directions[..., 1], bottom, medium.top)
        bounds = ray_step_bounds(length, hits_ground, directions[..., 1], steps)
        t = 0.5 * (bounds[..., 1:] + bounds[..., :-1])
        dt = bounds[..., 1:] - bounds[..., :-1]
        px = t * directions[..., 0, None]
        py = radius[:, None, None] + t * directions[..., 1, None]
        pz = t * directions[..., 2, None]
        r = np.sqrt(px * px + py * py + pz * pz)
        p_sun_cosine = (px * sun[:, None, None, 0] + py * sun[:, None, None, 1]) / r
        rayleigh_s, mie_s, extinction = medium.coefficients(r - bottom)
        step_depth = extinction * dt[..., None]
        depth_mid = np.cumsum(step_depth, axis=2) - 0.5 * step_depth
        view_t = np.exp(-depth_mid)
        end_t = np.exp(-np.sum(step_depth, axis=2))
        sun_t = self.table.transmittance(r, p_sun_cosine)
        scattering = rayleigh_s + mie_s[..., None]
        weight = view_t * scattering * dt[..., None]
        # 地表に当たる視線: 当たった点の太陽の余弦と、そこで返る太陽の光
        gx = length * directions[..., 0]
        gy = radius[:, None] + length * directions[..., 1]
        gz = length * directions[..., 2]
        ground_cosine = (gx * sun[:, None, 0] + gy * sun[:, None, 1]) / np.sqrt(gx * gx + gy * gy + gz * gz)
        ground_sun = np.where(hits_ground[..., None],
                              end_t * albedo_over_pi * self.table.transmittance(np.full_like(ground_cosine, bottom),
                                                                                ground_cosine) *
                              np.maximum(ground_cosine, 0.0)[..., None], 0.0)
        mean = direction_weight[..., None]
        result = {"on_ground": altitude == 0.0}
        # A: 論文の近似
        ray_l2 = np.sum(weight * sun_t, axis=2) * uniform_phase + ground_sun
        result["l_2nd"] = np.sum(ray_l2 * mean, axis=1)
        result["f_ms"] = np.sum(np.sum(weight, axis=2) * mean, axis=1)
        # B: 1 回目（本当の位相関数）。Mie の前方の山（g = 0.8）は方向の求積では粗いので、太陽の向きの視線の値 F_s を
        # 差し引いて積み（被積分関数が山の所で 0 になる）、位相関数の球の積分が 1 であることを使って F_s を足し戻す
        view_cosine = np.sum(directions * sun[:, None, :], axis=-1)
        rayleigh_ray = np.sum(view_t * rayleigh_s * dt[..., None] * sun_t, axis=2) * \
            medium.rayleigh_phase(view_cosine)[..., None]
        mie_ray = np.sum(view_t * (mie_s * dt)[..., None] * sun_t, axis=2)
        mie_phase = self.cell_mean_mie_phase(directions, sun)[..., None]
        mie_sun = self.mie_toward_sun(radius, sun, steps)
        result["first"] = (np.sum((rayleigh_ray + mie_phase * (mie_ray - mie_sun[:, None, :]) + ground_sun) * mean, axis=1) +
                           mie_sun / (4.0 * math.pi))
        # B: 次の回への行（表の 4 隅への線形の重み。地表に当たる視線は地表の照度の欄へ）
        corner_index, corner_weight = self.corners(r - bottom, p_sun_cosine)
        ground_index, ground_weight = self.ground_corners(ground_cosine)
        local = np.arange(count)[:, None, None]
        local_flat = np.arange(count)[:, None]
        # 地表の点では、上向きの余弦の重み（球の平均の重みに 4π を掛けて照度にする）
        upward = np.maximum(directions[..., 1], 0.0) * direction_weight * (4.0 * math.pi)
        rows = np.zeros((3, count, states))
        ground_rows = np.zeros((3, count, states))
        for c in range(3):
            ground_value = np.where(hits_ground, end_t[..., c] * albedo_over_pi[c], 0.0)
            for target, direction_factor in ((rows, direction_weight), (ground_rows, upward)):
                for k in range(4):
                    flat = (local * states + corner_index[k]).ravel()
                    values = (weight[..., c] * corner_weight[k] * direction_factor[..., None]).ravel()
                    target[c] += np.bincount(flat, values, count * states).reshape(count, states)
                for k in range(2):
                    flat = (local_flat * states + points + ground_index[k]).ravel()
                    values = (ground_value * ground_weight[k] * direction_factor).ravel()
                    target[c] += np.bincount(flat, values, count * states).reshape(count, states)
        result["rows"] = rows
        result["ground_rows"] = ground_rows
        # 余弦の重みの付いた積分にも同じ差し引きを使う（太陽の向きの余弦 × F_s を引いて足し戻す）
        sun_up = np.maximum(sun[:, 1], 0.0)[:, None]
        cosine_weight = np.maximum(directions[..., 1], 0.0)[..., None]
        result["first_ground"] = (
            np.sum((rayleigh_ray * cosine_weight + mie_phase * (mie_ray * cosine_weight - sun_up[:, None, :] *
                                                                mie_sun[:, None, :])) *
                   (direction_weight * 4.0 * math.pi)[..., None], axis=1) + sun_up * mie_sun)
        return result

    def evaluate(self, altitude, sun_cosine):
        """任意の点の Ψ（A・B）を、表の補間でなく、その点から視線を積んで求める（B は収束した表の光を次の回の源に使う）。
        戻り値は (Ψ_A [N,3], Ψ_B [N,3])"""
        altitude = np.asarray(altitude, dtype=np.float64).ravel()
        sun_cosine = np.asarray(sun_cosine, dtype=np.float64).ravel()
        psi_a = np.zeros((len(altitude), 3))
        psi_b = np.zeros((len(altitude), 3))
        batch = self.batch_size()
        for start in range(0, len(altitude), batch):
            index = np.arange(start, min(start + batch, len(altitude)))
            rows = self.point_rows(altitude[index], sun_cosine[index])
            psi_a[index] = rows["l_2nd"] / np.maximum(1.0 - rows["f_ms"], 1e-6)
            psi_b[index] = rows["first"] + np.stack([rows["rows"][c] @ self.total[:, c] for c in range(3)], axis=-1)
        return psi_a, psi_b

    def cell_mean_mie_phase(self, directions, sun):
        """方位の 1 つの区画（幅 π / 方位の数）の中で平均した Mie の位相関数。視線の光は方位でなめらかに変わるが、位相関数の
        前方の山は区画より細いので、区画の中を PHASE_AZIMUTH_SUBDIVISIONS 個に分けて平均する（積の求積）"""
        ring = np.sqrt(np.maximum(1.0 - directions[..., 1] ** 2, 0.0))
        phi = np.arctan2(directions[..., 2], directions[..., 0])
        width = math.pi / self.azimuth_count
        total = np.zeros_like(phi)
        for k in range(PHASE_AZIMUTH_SUBDIVISIONS):
            sub = phi + ((k + 0.5) / PHASE_AZIMUTH_SUBDIVISIONS - 0.5) * width
            cosine = ring * np.cos(sub) * sun[:, None, 0] + directions[..., 1] * sun[:, None, 1]
            total += self.medium.mie_phase(cosine)
        return total / PHASE_AZIMUTH_SUBDIVISIONS

    def mie_toward_sun(self, radius, sun, steps):
        """点 (0, radius, 0) から太陽の向きの視線の ∫ T σs_M T_sun dt（RGB）"""
        medium = self.medium
        bottom = medium.bottom
        length, hits_ground = ray_end(radius, sun[:, 1], bottom, medium.top)
        bounds = ray_step_bounds(length, hits_ground, sun[:, 1], steps)
        t = 0.5 * (bounds[:, 1:] + bounds[:, :-1])
        dt = bounds[:, 1:] - bounds[:, :-1]
        px = t * sun[:, 0, None]
        py = radius[:, None] + t * sun[:, 1, None]
        r = np.sqrt(px * px + py * py)
        p_sun_cosine = (px * sun[:, 0, None] + py * sun[:, 1, None]) / r
        _, mie_s, extinction = medium.coefficients(r - bottom)
        step_depth = extinction * dt[..., None]
        view_t = np.exp(-(np.cumsum(step_depth, axis=1) - 0.5 * step_depth))
        return np.sum(view_t * (mie_s * dt)[..., None] * self.table.transmittance(r, p_sun_cosine), axis=1)

    def corners(self, altitude, sun_cosine):
        height = self.medium.settings["atmosphere_height_m"]
        u = np.sqrt(np.clip(altitude / height, 0.0, 1.0)) * (self.altitude_count - 1)
        m = (np.clip(sun_cosine, -1.0, 1.0) + 1.0) * 0.5 * (self.cosine_count - 1)
        i0 = np.minimum(np.floor(u).astype(np.int64), self.altitude_count - 2)
        j0 = np.minimum(np.floor(m).astype(np.int64), self.cosine_count - 2)
        a = u - i0
        b = m - j0
        n = self.cosine_count
        index = (i0 * n + j0, i0 * n + j0 + 1, (i0 + 1) * n + j0, (i0 + 1) * n + j0 + 1)
        weight = ((1 - a) * (1 - b), (1 - a) * b, a * (1 - b), a * b)
        return index, weight

    def ground_corners(self, sun_cosine):
        m = (np.clip(sun_cosine, -1.0, 1.0) + 1.0) * 0.5 * (self.cosine_count - 1)
        j0 = np.minimum(np.floor(m).astype(np.int64), self.cosine_count - 2)
        b = m - j0
        return (j0, j0 + 1), (1 - b, b)

    def psi(self, altitude, sun_cosine, version):
        values = self.psi_a if version == "A" else self.psi_b
        index, weight = self.corners(altitude, sun_cosine)
        return sum(values[index[k]] * weight[k][..., None] for k in range(4))

    def ground_diffuse(self, sun_cosine):
        index, weight = self.ground_corners(sun_cosine)
        return sum(self.ground_diffuse_b[index[k]] * weight[k][..., None] for k in range(2))


class Atmosphere:
    """1 組の設定と 1 つの段の正解。値は太陽の照度 1 lux あたり"""

    def __init__(self, settings, level):
        self.settings = settings
        self.level = level
        self.parameters = level_parameters(level)
        p = self.parameters
        started = time.time()
        self.medium = Medium(settings)
        self.table = TransmittanceTable(self.medium, p["table_radius"], p["table_cosine"], p["table_steps"])
        self.field = ScatteringField(self.medium, self.table, p["field_altitude"], p["field_cosine"],
                                     p["field_polar"], p["field_azimuth"], p["field_steps"])
        self.seconds = time.time() - started

    def sun_transmittance(self, altitude, sun_direction):
        return self.table.transmittance(self.medium.bottom + np.asarray(altitude, dtype=np.float64),
                                        np.asarray(sun_direction[1], dtype=np.float64))

    def march(self, origin_altitude, directions, sun_direction, version, start=None, end=None, steps=None,
              mie_scale=1.0, fog=None, origin_world_y=0.0, air_start=0.0):
        """観測点（高度 origin_altitude）から directions（[N,3]、+Y が上）へ進む視線の散乱光と透過率。
        start・end を与えると、その区間（m）だけを数える（与えないと 0 から地表か大気の上端まで、地表に当たれば地表の光も足す）。
        mie_scale は視線の上の Mie の密度の倍率（太陽の透過率と多重散乱の表は倍率 1 の大気のまま）。
        fog は高さフォグの媒質（dict: density_at_base_per_m・base_height_m・height_falloff_per_m・phase_anisotropy。
        ワールドの y = origin_world_y + t × 向きの y で数える。散乱のみ）。air_start より手前では空の媒質（Rayleigh・Mie・オゾン）を
        数えない（空気遠近の数え始めの距離）。
        戻り値は成分ごとの散乱光（dict。値は [N,3]）と透過率 [N,3]"""
        medium = self.medium
        steps = steps or self.parameters["view_steps"]
        directions = np.asarray(directions, dtype=np.float64)
        sun = np.asarray(sun_direction, dtype=np.float64)
        bottom = medium.bottom
        radius = bottom + origin_altitude
        length, hits_ground = ray_end(radius, directions[:, 1], bottom, medium.top)
        segment = start is not None
        if segment:
            first = np.full(len(directions), float(start))
            last = np.minimum(np.asarray(end, dtype=np.float64) * np.ones(len(directions)), length)
            hits_ground = np.zeros(len(directions), dtype=bool)
            fractions = (np.arange(steps) + 0.5) / steps
            t = first[:, None] + (last - first)[:, None] * fractions
            dt = np.broadcast_to(((last - first) / steps)[:, None], t.shape)
        else:
            bounds = ray_step_bounds(length, hits_ground, directions[:, 1], steps)
            t = 0.5 * (bounds[:, 1:] + bounds[:, :-1])
            dt = bounds[:, 1:] - bounds[:, :-1]
        px = t * directions[:, 0, None]
        py = radius + t * directions[:, 1, None]
        pz = t * directions[:, 2, None]
        r = np.sqrt(px * px + py * py + pz * pz)
        altitude = r - bottom
        sun_cosine = (px * sun[0] + py * sun[1] + pz * sun[2]) / r
        rayleigh_s, mie_s, extinction = medium.coefficients(altitude, mie_scale)
        if air_start > 0.0:
            counted = t >= air_start
            rayleigh_s = rayleigh_s * counted[..., None]
            mie_s = mie_s * counted
            extinction = extinction * counted[..., None]
        fog_s = np.zeros_like(altitude)
        if fog is not None:
            world_y = origin_world_y + t * directions[:, 1, None]
            fog_s = fog["density_at_base_per_m"] * np.exp(-fog["height_falloff_per_m"] * (world_y - fog["base_height_m"]))
        step_depth = (extinction + fog_s[..., None]) * dt[..., None]
        depth_mid = np.cumsum(step_depth, axis=1) - 0.5 * step_depth
        view_t = np.exp(-depth_mid)
        sun_t = self.table.transmittance(r, sun_cosine)
        view_cosine = directions @ sun
        psi = self.field.psi(altitude, sun_cosine, version)
        air_sun = (rayleigh_s * medium.rayleigh_phase(view_cosine)[:, None, None] +
                   (mie_s * medium.mie_phase(view_cosine)[:, None])[..., None]) * sun_t
        air_multiple = (rayleigh_s + mie_s[..., None]) * psi
        weight = view_t * dt[..., None]
        result = {
            "air_sun": np.sum(weight * air_sun, axis=1),
            "air_multiple": np.sum(weight * air_multiple, axis=1),
        }
        if fog is not None:
            fog_phase = medium.mie_phase(view_cosine, fog["phase_anisotropy"])
            result["fog_sun"] = np.sum(weight * fog_s[..., None] * fog_phase[:, None, None] * sun_t, axis=1)
            result["fog_multiple"] = np.sum(weight * fog_s[..., None] * psi, axis=1)
        transmittance = np.exp(-np.sum(step_depth, axis=1))
        ground = np.zeros((len(directions), 3))
        if np.any(hits_ground):
            gx = length * directions[:, 0]
            gy = radius + length * directions[:, 1]
            gz = length * directions[:, 2]
            ground_cosine = (gx * sun[0] + gy * sun[1] + gz * sun[2]) / np.sqrt(gx * gx + gy * gy + gz * gz)
            irradiance = self.table.transmittance(np.full_like(ground_cosine, bottom), ground_cosine) * \
                np.maximum(ground_cosine, 0.0)[:, None]
            if version == "B":
                irradiance = irradiance + self.field.ground_diffuse(ground_cosine)
            ground = np.where(hits_ground[:, None], transmittance * medium.albedo / math.pi * irradiance, 0.0)
        result["ground"] = ground
        result["total"] = sum(result.values())
        return result, transmittance

    def sky_radiance(self, origin_altitude, directions, sun_direction, version):
        result, _ = self.march(origin_altitude, directions, sun_direction, version)
        return result["total"]

    def sky_horizontal_irradiance(self, origin_altitude, sun_direction, version):
        """上向きの水平面の空の照度（太陽の直射を除く）。仰角は余弦の重みのガウス・ルジャンドル、方位は等間隔"""
        p = self.parameters
        nodes, weights = np.polynomial.legendre.leggauss(p["hemisphere_elevation"])
        mu = 0.5 * (nodes + 1.0)
        mu_weight = 0.5 * weights
        azimuth = (np.arange(p["hemisphere_azimuth"]) + 0.5) * 2.0 * math.pi / p["hemisphere_azimuth"]
        mu_grid, azimuth_grid = np.meshgrid(mu, azimuth, indexing="ij")
        ring = np.sqrt(1.0 - mu_grid ** 2)
        directions = np.stack([ring * np.cos(azimuth_grid), mu_grid, ring * np.sin(azimuth_grid)], -1).reshape(-1, 3)
        radiance = self.sky_radiance(origin_altitude, directions, sun_direction, version).reshape(
            len(mu), len(azimuth), 3)
        weight = (mu_grid * mu_weight[:, None] * (2.0 * math.pi / len(azimuth)))[..., None]
        return np.sum(radiance * weight, axis=(0, 1))


def sun_direction_from_angles(altitude_degrees, azimuth_degrees):
    altitude = math.radians(altitude_degrees)
    azimuth = math.radians(azimuth_degrees)
    return np.array([math.cos(altitude) * math.cos(azimuth), math.sin(altitude), math.cos(altitude) * math.sin(azimuth)])


def direction_from_sun_offset(sun_direction, elevation_degrees, azimuth_offset_degrees):
    """太陽の方位からの方位差と仰角で決めた向き（+Y が上）"""
    sun_azimuth = math.atan2(sun_direction[2], sun_direction[0])
    elevation = np.radians(np.asarray(elevation_degrees, dtype=np.float64))
    azimuth = sun_azimuth + np.radians(np.asarray(azimuth_offset_degrees, dtype=np.float64))
    return np.stack([np.cos(elevation) * np.cos(azimuth), np.sin(elevation), np.cos(elevation) * np.sin(azimuth)], -1)


def vertical_optical_depth(settings):
    """地表から天頂への光学的深さの解析解（鉛直の視線では高度 = 距離なので球の形に依らない）"""
    height = settings["atmosphere_height_m"]
    h_r = settings["rayleigh_scale_height_m"]
    h_m = settings["mie_scale_height_m"]
    rayleigh = np.array(settings["rayleigh_scattering_per_m"]) * h_r * (1.0 - math.exp(-height / h_r))
    mie = (settings["mie_scattering_per_m"] + settings["mie_absorption_per_m"]) * h_m * (1.0 - math.exp(-height / h_m))
    center = settings["ozone_center_m"]
    half = settings["ozone_half_width_m"]
    low = max(center - half, 0.0)
    high = min(center + half, height)
    xs = np.linspace(low, high, 200001)
    ozone_area = float(np.trapezoid(np.maximum(0.0, 1.0 - np.abs(xs - center) / half), xs)) if high > low else 0.0
    return rayleigh + mie + np.array(settings["ozone_absorption_per_m"]) * ozone_area


def standard_quantities(atmosphere, sun_altitude_degrees, observer_altitude_m):
    """収束を見る量: 方向別の空の放射輝度（A・B）、Ψ の表の値、地表の太陽の透過率、水平面の空の照度"""
    sun = sun_direction_from_angles(sun_altitude_degrees, 0.0)
    elevations = np.array([0.0, 2.0, 5.0, 10.0, 20.0, 30.0, 45.0, 60.0, 90.0])
    offsets = np.array([0.0, 45.0, 90.0, 135.0, 180.0])
    el, az = np.meshgrid(elevations, offsets, indexing="ij")
    directions = direction_from_sun_offset(sun, el.ravel(), az.ravel())
    quantities = {}
    for version in ("A", "B"):
        quantities[f"sky_{version}"] = atmosphere.sky_radiance(observer_altitude_m, directions, sun, version)
        quantities[f"sky_irradiance_{version}"] = atmosphere.sky_horizontal_irradiance(observer_altitude_m, sun, version)
    probe_altitudes = np.array(PSI_PROBE_ALTITUDES)
    probe_cosines = np.array(PSI_PROBE_COSINES)
    ha, ca = np.meshgrid(probe_altitudes, probe_cosines, indexing="ij")
    quantities["psi_A"], quantities["psi_B"] = atmosphere.field.evaluate(ha.ravel(), ca.ravel())
    quantities["sun_transmittance"] = atmosphere.sun_transmittance(0.0, sun)
    # 地表の近くの水平の 10 km の区間（空気遠近の形）
    horizontal = direction_from_sun_offset(sun, np.zeros(4), np.array([0.0, 90.0, 180.0, 270.0]))
    segment, segment_t = atmosphere.march(1500.0, horizontal, sun, "B", start=300.0, end=10000.0)
    quantities["segment_scattering"] = segment["total"]
    quantities["segment_transmittance"] = segment_t
    return quantities


def compare_levels(coarse, fine):
    worst = {}
    for key, value in fine.items():
        reference = np.asarray(value, dtype=np.float64)
        other = np.asarray(coarse[key], dtype=np.float64)
        worst[key] = float(np.max(np.abs(other - reference) / np.maximum(np.abs(reference), CONVERGENCE_RADIANCE_FLOOR)))
    return worst


def verify(dump_path, out_dir):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    failures = []
    sets = [("paper", PAPER_SETTINGS, 38.0, 100.0)]
    if os.path.isfile(dump_path):
        with open(dump_path, encoding="utf-8") as f:
            meta = json.load(f)
        sky = meta["sky"]
        sets.append(("engine", settings_from_dump(meta), float(sky["sun_altitude_degrees"]), float(sky["observer_altitude_m"])))
    else:
        failures.append(f"書き出しの JSON が無い: {dump_path}")
    report = {"criteria": {"convergence_tolerance": CONVERGENCE_TOLERANCE, "radiance_floor": CONVERGENCE_RADIANCE_FLOOR,
                           "analytic_tolerance": ANALYTIC_TOLERANCE, "level1": LEVEL1},
              "sets": {}}
    for name, settings, sun_altitude, observer in sets:
        coarse = Atmosphere(settings, 1)
        fine = Atmosphere(settings, 2)
        q_coarse = standard_quantities(coarse, sun_altitude, observer)
        q_fine = standard_quantities(fine, sun_altitude, observer)
        worst = compare_levels(q_coarse, q_fine)
        converged = max(worst.values()) <= CONVERGENCE_TOLERANCE
        if not converged:
            failures.append(f"{name}: 段 1 と段 2 の差が {CONVERGENCE_TOLERANCE} を超える: " +
                            ", ".join(f"{k}={v:.4f}" for k, v in worst.items() if v > CONVERGENCE_TOLERANCE))
        analytic = vertical_optical_depth(settings)
        table_vertical = fine.table.optical_depth(np.array(fine.medium.bottom), np.array(1.0))
        analytic_error = float(np.max(np.abs(table_vertical - analytic) / analytic))
        if analytic_error > ANALYTIC_TOLERANCE:
            failures.append(f"{name}: 鉛直の光学的深さが解析解と {analytic_error:.2e} 違う")
        f_max = float(np.max(fine.field.f_ms))
        if not f_max < 1.0:
            failures.append(f"{name}: f_ms の最大 {f_max} が 1 以上（論文の級数が収束しない）")
        if fine.field.orders >= ORDER_LIMIT:
            failures.append(f"{name}: 散乱の回数で足す版が {ORDER_LIMIT} 回で収束しない")
        if fine.field.series_vs_solve > 1e-3:
            failures.append(f"{name}: 回数で足した和と直接解いた値が {fine.field.series_vs_solve:.2e} 違う")
        sun = sun_direction_from_angles(sun_altitude, 0.0)
        # 地表・太陽の余弦 0.6 の点（荒野の太陽の高度 38° に近い）
        ground_index = PSI_PROBE_COSINES.index(0.6)
        ratio_a = q_fine["psi_A"][ground_index] / np.maximum(q_fine["psi_B"][ground_index], 1e-30)
        record = {
            "settings": settings, "sun_altitude_degrees": sun_altitude, "observer_altitude_m": observer,
            "seconds": {"level1": coarse.seconds, "level2": fine.seconds},
            "level2_parameters": fine.parameters,
            "max_relative_difference_level1_vs_level2": worst,
            "converged": converged,
            "vertical_optical_depth": {"table": table_vertical.tolist(), "analytic": analytic.tolist(),
                                       "max_relative_error": analytic_error},
            "f_ms_max": f_max,
            "orders": fine.field.orders,
            "order_ratios_first10": fine.field.order_ratios[:10],
            "series_vs_solve": fine.field.series_vs_solve,
            "sun_transmittance_ground": q_fine["sun_transmittance"].tolist(),
            "sky_horizontal_irradiance_per_lux": {"A": q_fine["sky_irradiance_A"].tolist(),
                                                  "B": q_fine["sky_irradiance_B"].tolist()},
            "psi_ground_A_over_B": ratio_a.tolist(),
            "sun_direction": sun.tolist(),
        }
        report["sets"][name] = record
        print(f"ATMOSPHERE_REFERENCE set={name} converged={int(converged)} "
              f"max_level_diff={max(worst.values()):.5f} worst={max(worst, key=worst.get)} "
              f"analytic_vertical={analytic_error:.2e} f_ms_max={f_max:.4f} orders={fine.field.orders} "
              f"order_ratio_2={fine.field.order_ratios[0]:.4f} series_vs_solve={fine.field.series_vs_solve:.1e} "
              f"sun_T={np.round(q_fine['sun_transmittance'], 4).tolist()} "
              f"sky_E_A={np.round(q_fine['sky_irradiance_A'], 4).tolist()} "
              f"sky_E_B={np.round(q_fine['sky_irradiance_B'], 4).tolist()} "
              f"seconds={coarse.seconds:.1f}+{fine.seconds:.1f}")
        for key, value in worst.items():
            print(f"ATMOSPHERE_REFERENCE set={name} quantity={key} level1_vs_level2={value:.5f}")
    report["failures"] = failures
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "atmosphere_reference_verify.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"ATMOSPHERE_REFERENCE failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"ATMOSPHERE_REFERENCE result={status} sets={len(sets)} report={path}")
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser(description="空と霞の数値の正解（Hillaire 2020 の定義から独立に積分する）")
    parser.add_argument("--verify", action="store_true", help="収束と解析解との一致を確かめる")
    parser.add_argument("--dump", default=DEFAULT_DUMP, help="エンジンの書き出しの JSON（<視点>.atmos.json）")
    parser.add_argument("--out", default=DEFAULT_OUT, help="結果の JSON の出力先")
    args = parser.parse_args()
    if args.verify:
        return verify(args.dump, args.out)
    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
