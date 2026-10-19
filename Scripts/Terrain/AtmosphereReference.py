# 空と霞の数値の正解（SCN-WLR-ATMOS-REF）。エンジンのコードを写さず、Hillaire 2020「A Scalable and Production Ready Sky and
# Atmosphere Rendering Technique」の定義（惑星と大気の同心球、Rayleigh・Mie の指数分布、オゾンの三角形の層）から、
# 空の放射輝度・透過率・多重散乱・地表の照度を numpy で数値積分する。
#
# 使い方:
#   python Scripts/Terrain/AtmosphereReference.py --solve [--dump <視点>.atmos.json] [--out <出力先>]
#       論文の設定と、荒野の書き出し（--dump。省くと SCN-WLR-ATMOS-DUMP の ref_ground_02）の設定の 2 組で、歩数・方向の数・
#       表の細かさをすべて倍にした段 2 の正解（1 組数分）と下の検査を求め、この台本と書き出しの中身のハッシュつきで
#       <出力先>/atmosphere_reference_level2.json に保存する。
#   python Scripts/Terrain/AtmosphereReference.py --verify [--dump ...] [--out ...]
#       同じ 2 組で段 1 をその場で解き、保存した段 2 と比べる量がすべて CONVERGENCE_TOLERANCE 以内で一致することを確かめる
#       （保存した段 2 のハッシュが今の入力と同じことも見る）。ほかに、鉛直の光学的深さの解析解との一致・多重散乱の級数が
#       収束すること・B の 2 回目の散乱の源が位相関数の角度の積分と合うこと・1 回目だけの値が A と B で一致することを、
#       段 1 はその場で、段 2 は保存した値で見る。結果を <出力先>/atmosphere_reference_verify.json に書く。
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
#   B（散乱の回数で足す版）: 等方とも、周りの点が同じ光を受けるともみなさない。各回の散乱で本当の位相関数（Rayleigh・HG）を
#      使い、方向別の放射輝度を次の回へ引き継ぐ（DirectionalField）。点（高度 × 太陽の天頂角の余弦）ごとに、入ってくる光の
#      放射輝度 L_n(p, w) を w の求積の向きで持ち、散乱の源 G_X(p, u) = ∫ p_X(u·w) L_n(p, w) dw（X = Rayleigh・Mie）を
#      u の格子（点の天頂角の余弦 × 太陽からの方位差）の表にする。L_{n+1}(p, w) = ∫ T(p,q) [σ_R G_R + σ_M G_M](q, w) dt
#      ＋ 地表に当たれば T × albedo / π × E_n（E_n は地表の n 回目の光の照度）。視線の源は σ_R G_R + σ_M G_M（全回の和）。
#      1 回目の光は L_1(p, w) = p_R(s·w) F_R(p, w) + p_M(s·w) F_M(p, w) + 地表で返る太陽 で、F = ∫ T σ T_sun dt は
#      向きでなめらかなので、Mie の前方の山どうしの積は球の上の畳み込み（ルジャンドル展開の係数の積）で解析的に積む。
#      散乱の源の表は、その解析的な部分 Σ_Y F_Y(p, s) (p_X ⊛ p_Y)(u·s) を引いた残りを持ち、引くときに足し戻す。
#
# 数値の積分: 視線は中点則（空への視線は観測点の近くを細かく取る 2 乗の並び、地表や区間の視線は等間隔）。
# 太陽の透過率は Bruneton 2017 の (r, μ) の並び（地平線の近くを細かく取る）の表を線形に引く。
# 段 2 は段 1 の歩数・方向の数・表の細かさをすべて倍にしたもので、比べる量の差が CONVERGENCE_TOLERANCE 以内なら収束とする。
# 段 2 では B の区画の中の位相関数の標本の分け数も倍にする。
# B の 2 回目の散乱の源は、点の周りの細かい向きの求積で 1 回目の光を直接積んだ値とも比べる（SECOND_ORDER_TOLERANCE）。
# 比べる点は評価者の最小再現（書き出しの係数・空気遠近の起点の高度 + カメラの y・書き出しの太陽の高度）と観測点で、
# 太陽の向きの σ_R G_R + σ_M G_M が等方とした源の何倍かも記録する。1 回目だけの値（A の視線の 1 回目の項と、B が次の回へ
# 渡す 1 回目の光）は FIRST_ORDER_TOLERANCE で一致を見る。
import argparse
import hashlib
import json
import math
import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_DUMP = os.path.join(REPO_ROOT, ".harness", "runs", "scene-capture", "SCN-WLR-ATMOS-DUMP", "ref_ground_02.atmos.json")
DEFAULT_OUT = os.path.join(REPO_ROOT, ".harness", "runs", "scene-capture", "SCN-WLR-ATMOS-REF")
# --solve が段 2 の正解と検査を保存する先（--out の下）
SOLVE_FILE = "atmosphere_reference_level2.json"

# 段 2 と段 1 の相対差の上限（0.5 %）と、相対差の分母の下限（太陽 1 lux あたりの cd/m²・透過率）
CONVERGENCE_TOLERANCE = 0.005
CONVERGENCE_RADIANCE_FLOOR = 1e-4
# 鉛直の光学的深さの解析解との相対差の上限
ANALYTIC_TOLERANCE = 1e-3
# 散乱の回数で足す版を打ち切る、その回の散乱の源の和（全回）に対する割合と、回数の上限
ORDER_STOP_FRACTION = 1e-5
ORDER_LIMIT = 100
# B の 2 回目の散乱の源と、細かい向きの求積で直接積んだ値の相対差の上限
SECOND_ORDER_TOLERANCE = 0.01
# 1 回目だけの値（A の視線の 1 回目の項と、B が次の回へ渡す 1 回目の光）の相対差の上限
FIRST_ORDER_TOLERANCE = 1e-6
# 評価者の最小再現（書き出しの係数・空気遠近の起点の高度＋カメラの y = 1474.223367 m・太陽の高度 38°）で、太陽の向きの
# 2 回目の散乱の源 σ_R G_R + σ_M G_M が、2 回目以降を等方とした源 (σ_R + σ_M) × 入ってくる光の球の平均の何倍か（RGB）
EVALUATOR_SUN_RATIO = (3.3694, 2.9392, 2.3542)
REPRO_ALTITUDE_M = 1474.223367
# 直接積む求積の細かさ（太陽の向きを軸にした天頂角 0〜SECOND_ORDER_SPLIT rad・その先・方位）と視線の歩数
SECOND_ORDER_SPLIT = 0.35
SECOND_ORDER_COUNTS = (96, 192, 360)
SECOND_ORDER_STEPS = 256
# 点ごとの計算を並べるスレッドの数（numpy の配列の計算は GIL を離す）
WORKERS = max(1, min(16, os.cpu_count() or 1))

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
    # B（DirectionalField）: 点の高度・太陽の余弦、w の求積（地平線の上下それぞれの天頂角・半周の方位）、u の格子、視線の歩数、
    # 求積の 1 つの区画の中の位相関数を平均する標本の分け数（天頂角の余弦と方位のそれぞれ）
    "orders_altitude": 16, "orders_cosine": 16, "orders_polar": 8, "orders_azimuth": 8,
    "orders_view_cosine": 16, "orders_view_azimuth": 12, "orders_steps": 64, "orders_phase_subdivisions": 4,
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


def parallel_map(function, items):
    with ThreadPoolExecutor(WORKERS) as pool:
        return list(pool.map(function, items))


class ScatteringField:
    """論文の近似（A）の多重散乱の表（高度 × 太陽の天頂角の余弦）の Ψ_ms"""

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
        batch = self.batch_size()
        batches = [np.arange(start, min(start + batch, points)) for start in range(0, points, batch)]
        rows = parallel_map(lambda index: self.point_rows(self.altitudes[index // cosine_count],
                                                          self.cosines[index % cosine_count]), batches)
        self.l_2nd = np.concatenate([row[0] for row in rows])
        self.f_ms = np.concatenate([row[1] for row in rows])
        self.psi_a = self.l_2nd / np.maximum(1.0 - self.f_ms, 1e-6)

    def batch_size(self):
        return max(1, 4096 * 64 // (2 * self.polar_count * self.azimuth_count * self.steps))

    def point_rows(self, altitude, sun_cosine):
        """点（高度 altitude、太陽の天頂角の余弦 sun_cosine。どちらも [N]）の L_2nd と f_ms"""
        medium = self.medium
        bottom = medium.bottom
        radius = bottom + altitude
        sun = np.stack([np.sqrt(np.maximum(1.0 - sun_cosine ** 2, 0.0)), sun_cosine, np.zeros_like(sun_cosine)], -1)
        directions, direction_weight = horizon_split_directions(radius, bottom, self.polar_count, self.azimuth_count)
        length, hits_ground = ray_end(radius[:, None], directions[..., 1], bottom, medium.top)
        bounds = ray_step_bounds(length, hits_ground, directions[..., 1], self.steps)
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
        weight = view_t * (rayleigh_s + mie_s[..., None]) * dt[..., None]
        # 地表に当たる視線: 当たった点の太陽の余弦と、そこで返る太陽の光
        gx = length * directions[..., 0]
        gy = radius[:, None] + length * directions[..., 1]
        gz = length * directions[..., 2]
        ground_cosine = (gx * sun[:, None, 0] + gy * sun[:, None, 1]) / np.sqrt(gx * gx + gy * gy + gz * gz)
        ground_sun = np.where(hits_ground[..., None],
                              end_t * medium.albedo / math.pi *
                              self.table.transmittance(np.full_like(ground_cosine, bottom), ground_cosine) *
                              np.maximum(ground_cosine, 0.0)[..., None], 0.0)
        mean = direction_weight[..., None]
        ray_l2 = np.sum(weight * sun_t, axis=2) / (4.0 * math.pi) + ground_sun
        return np.sum(ray_l2 * mean, axis=1), np.sum(np.sum(weight, axis=2) * mean, axis=1)

    def evaluate(self, altitude, sun_cosine):
        """任意の点の Ψ_A を、表の補間でなく、その点から視線を積んで求める（[N,3]）"""
        altitude = np.asarray(altitude, dtype=np.float64).ravel()
        sun_cosine = np.asarray(sun_cosine, dtype=np.float64).ravel()
        l_2nd, f_ms = self.point_rows(altitude, sun_cosine)
        return l_2nd / np.maximum(1.0 - f_ms, 1e-6)

    def psi(self, altitude, sun_cosine):
        height = self.medium.settings["atmosphere_height_m"]
        u = np.sqrt(np.clip(altitude / height, 0.0, 1.0)) * (self.altitude_count - 1)
        m = (np.clip(sun_cosine, -1.0, 1.0) + 1.0) * 0.5 * (self.cosine_count - 1)
        i0 = np.minimum(np.floor(u).astype(np.int64), self.altitude_count - 2)
        j0 = np.minimum(np.floor(m).astype(np.int64), self.cosine_count - 2)
        a = (u - i0)[..., None]
        b = (m - j0)[..., None]
        n = self.cosine_count
        values = self.psi_a
        return ((1 - a) * (1 - b) * values[i0 * n + j0] + (1 - a) * b * values[i0 * n + j0 + 1] +
                a * (1 - b) * values[(i0 + 1) * n + j0] + a * b * values[(i0 + 1) * n + j0 + 1])


# 位相関数の指定: ("rayleigh",)・("isotropic",)・("hg", g)
RAYLEIGH = ("rayleigh",)
ISOTROPIC = ("isotropic",)


def henyey_greenstein(g):
    return ("hg", float(g))


def phase_value(spec, cosine):
    """位相関数（球の上の積分が 1）"""
    cosine = np.asarray(cosine, dtype=np.float64)
    if spec[0] == "rayleigh":
        return 3.0 * (1.0 + cosine * cosine) / (16.0 * math.pi)
    if spec[0] == "isotropic":
        return np.full_like(cosine, 1.0 / (4.0 * math.pi))
    g = spec[1]
    denominator = np.maximum(1.0 + g * g - 2.0 * g * cosine, 1e-12)
    return (1.0 - g * g) / (4.0 * math.pi * denominator * np.sqrt(denominator))


def phase_convolution(spec_x, spec_y, cosine):
    """球の上の畳み込み ∫ p_X(u·w) p_Y(s·w) dw を、u·s の関数として返す。位相関数をルジャンドル展開
    p = Σ (2l+1) / (4π) a_l P_l とすると、畳み込みの係数は a_l b_l（Rayleigh は a = 1・0・1/10、等方は a = 1、HG は a_l = g^l）。
    HG どうしは g の積の HG、片方が Rayleigh か等方なら l = 2 までで切れる"""
    if spec_x[0] == "hg" and spec_y[0] == "hg":
        return phase_value(("hg", spec_x[1] * spec_y[1]), cosine)
    heads = {"rayleigh": lambda spec: (1.0, 0.0, 0.1), "isotropic": lambda spec: (1.0, 0.0, 0.0),
             "hg": lambda spec: (1.0, spec[1], spec[1] ** 2)}
    a = heads[spec_x[0]](spec_x)
    b = heads[spec_y[0]](spec_y)
    p2 = 0.5 * (3.0 * cosine * cosine - 1.0)
    return (a[0] * b[0] + 3.0 * a[1] * b[1] * cosine + 5.0 * a[2] * b[2] * p2) / (4.0 * math.pi)


def relative_azimuth(cosine_between, view_up, sun_up):
    """点の水平面に落とした視線と太陽の向きの方位差（0〜π）。どちらかが鉛直なら 0"""
    denominator = np.sqrt(np.maximum((1.0 - view_up * view_up) * (1.0 - sun_up * sun_up), 1e-12))
    return np.arccos(np.clip((cosine_between - view_up * sun_up) / denominator, -1.0, 1.0))


def sun_vectors(sun_cosines):
    """太陽の天頂角の余弦 [J] の太陽の向き [J,3]（方位は +x）"""
    sun_cosines = np.asarray(sun_cosines, dtype=np.float64)
    return np.stack([np.sqrt(np.maximum(1.0 - sun_cosines ** 2, 0.0)), sun_cosines, np.zeros_like(sun_cosines)], -1)


def cubic_taps(coordinate, size):
    """格子の座標 coordinate（0〜size − 1）の Catmull-Rom の 4 点の起点と重み [..., 4]。起点は pad_for_cubic で両端に 1 つずつ
    足した格子の添字で、4 点は起点から連続する"""
    index = np.clip(np.floor(coordinate).astype(np.int64), 0, size - 2)
    t = coordinate - index
    t2 = t * t
    t3 = t2 * t
    weights = np.stack([0.5 * (-t3 + 2.0 * t2 - t), 0.5 * (3.0 * t3 - 5.0 * t2 + 2.0),
                        0.5 * (-3.0 * t3 + 4.0 * t2 + t), 0.5 * (t3 - t2)], -1)
    return index, weights


def pad_for_cubic(values, axis, mirror=False):
    """Catmull-Rom の端の外の点を 1 つずつ足す。mirror なら端で折り返し（方位差の 0・π のように偶関数の軸）、
    そうでなければ端の 2 点からの直線の外挿"""
    first, second = np.take(values, [0], axis), np.take(values, [1], axis)
    last, before = np.take(values, [-1], axis), np.take(values, [-2], axis)
    if mirror:
        low, high = second, before
    else:
        low, high = 2.0 * first - second, 2.0 * last - before
    return np.concatenate([low, values, high], axis)


def cubic_1d(values, coordinate):
    """値の並び values [S, C] を、座標 coordinate [...] で Catmull-Rom で引く [..., C]"""
    padded = pad_for_cubic(values, 0)
    index, weights = cubic_taps(coordinate, len(values))
    return sum(weights[..., k, None] * padded[index + k] for k in range(4))


def trace_rays(medium, radius, directions, steps):
    """点 (0, radius, 0) から directions [N,3] へ、地表か大気の上端までの視線の歩（中点則）。
    weight は観測点からの透過率 × 歩の長さ [N,K,3]、ground は当たった点（か上端の点）の向き [N,3]"""
    bottom = medium.bottom
    radius = np.broadcast_to(np.asarray(radius, dtype=np.float64), directions.shape[:1])
    length, hits_ground = ray_end(radius, directions[:, 1], bottom, medium.top)
    bounds = ray_step_bounds(length, hits_ground, directions[:, 1], steps)
    t = 0.5 * (bounds[:, 1:] + bounds[:, :-1])
    dt = bounds[:, 1:] - bounds[:, :-1]
    position = np.stack([t * directions[:, 0, None], radius[:, None] + t * directions[:, 1, None],
                         t * directions[:, 2, None]], -1)
    r = np.linalg.norm(position, axis=-1)
    rayleigh_s, mie_s, extinction = medium.coefficients(r - bottom)
    step_depth = extinction * dt[..., None]
    depth = np.cumsum(step_depth, axis=1)
    end = np.stack([length * directions[:, 0], radius + length * directions[:, 1], length * directions[:, 2]], -1)
    return {"position": position, "r": r, "rayleigh": rayleigh_s, "mie": mie_s,
            "weight": np.exp(-(depth - 0.5 * step_depth)) * dt[..., None], "end_t": np.exp(-depth[:, -1]),
            "hits_ground": hits_ground, "ground": end / np.linalg.norm(end, axis=-1, keepdims=True)}


def first_order_rays(medium, table, radius, directions, sun, steps):
    """点 (0, radius, 0) から directions [N,3] への視線の、太陽 sun [J,3] の 1 回目の光の部品:
    F_R・F_M = ∫ T σ_R・σ_M T_sun dt（[J,N,3]。位相関数は掛けない）と、地表で返る太陽の放射輝度 [J,N,3]"""
    ray = trace_rays(medium, radius, directions, steps)
    sun_up = np.einsum("jc,nkc->jnk", sun, ray["position"]) / ray["r"][None]
    weighted = ray["weight"][None] * table.transmittance(ray["r"][None], sun_up)
    rayleigh = np.sum(weighted * ray["rayleigh"][None], axis=2)
    mie = np.sum(weighted * ray["mie"][None, ..., None], axis=2)
    ground_cosine = sun @ ray["ground"].T
    irradiance = table.transmittance(np.full_like(ground_cosine, medium.bottom), ground_cosine) * \
        np.maximum(ground_cosine, 0.0)[..., None]
    ground = np.where(ray["hits_ground"][None, :, None], ray["end_t"][None] * medium.albedo / math.pi * irradiance, 0.0)
    return rayleigh, mie, ground


class DirectionalField:
    """散乱の回数で足す版（B）。各回の散乱で本当の位相関数を使い、方向別の放射輝度を次の回へ引き継ぐ。
    点の格子（高度 × 太陽の天頂角の余弦）ごとに、入ってくる光 L(p, w) を w の求積（地平線で分けたガウス・ルジャンドル ×
    方位の半周。太陽を含む面について対称）の向きで持つ。散乱の源 G_X(p, u) = ∫ p_X(u·w) L(p, w) dw は、位相関数を w の区画の
    中で平均した重み（区画と、面に映した区画の和。行の和を 1 にそろえる）で積み、u の格子（天頂角の余弦 × 太陽からの方位差）の
    表 [高度, 太陽の余弦, u, Rayleigh 3 + Mie 3] にする。次の回の L は視線の上で σ_R G_R + σ_M G_M を表から 4 次元の 3 次補間
    （Catmull-Rom。prepare の形）で引いて積む（σ はその点の高度の値）。1 回目の光の Mie の前方の山は phase_convolution で解析的に積む（sources_at）"""

    def __init__(self, medium, table, altitude_count, cosine_count, polar_count, azimuth_count,
                 view_cosine_count, view_azimuth_count, steps, subdivisions):
        self.medium = medium
        self.table = table
        self.subdivisions = subdivisions
        self.polar_count = polar_count
        self.azimuth_count = azimuth_count
        self.steps = steps
        self.height = medium.settings["atmosphere_height_m"]
        u = np.linspace(0.0, 1.0, altitude_count)
        self.altitudes = self.height * u * u
        self.cosines = np.linspace(-1.0, 1.0, cosine_count)
        view_cosines = np.linspace(-1.0, 1.0, view_cosine_count)
        view_azimuths = np.linspace(0.0, math.pi, view_azimuth_count)
        mu, phi = np.meshgrid(view_cosines, view_azimuths, indexing="ij")
        ring = np.sqrt(np.maximum(1.0 - mu * mu, 0.0))
        self.targets = np.stack([ring * np.cos(phi), mu, ring * np.sin(phi)], -1).reshape(-1, 3)
        self.shape = (altitude_count, cosine_count, view_cosine_count, view_azimuth_count)
        self.mie = henyey_greenstein(medium.g)
        self.quadratures = [self.quadrature(medium.bottom + altitude) for altitude in self.altitudes]
        self.extra_tables = {}
        started = time.time()
        self.solve()
        self.seconds = time.time() - started

    # --- 求積と位相関数の区画の平均 ---

    def quadrature(self, radius):
        """半径 radius の点の w の求積: 向き [N,3]、球の平均の重み [N]、区画を subdivisions² に分けた向きと面に映した向き
        [N, 2Q², 3]（cell_mean が使う）と、cell_integral が使う補間の基底（天頂角の余弦は地平線の上下の区間ごとの GL の点の
        ラグランジュ補間、方位は半周の点の余弦級数の補間）を、細かい標本（区間ごとの GL・全周の等間隔）で引いた値"""
        bottom = self.medium.bottom
        polar, azimuth, split = self.polar_count, self.azimuth_count, self.subdivisions
        directions, weight = horizon_split_directions(np.array([radius]), bottom, polar, azimuth)
        _, gauss = np.polynomial.legendre.leggauss(polar)
        horizon = -math.sqrt(max(1.0 - (bottom / radius) ** 2, 0.0))
        segments = ((-1.0, horizon), (horizon, 1.0))
        edges = []
        for low, high in segments:
            cumulative = low + 0.5 * (high - low) * np.concatenate([[0.0], np.cumsum(gauss)])
            edges.append(np.stack([cumulative[:-1], cumulative[1:]], -1))
        mu_cells = np.repeat(np.concatenate(edges), azimuth, axis=0)
        k = np.arange(azimuth)
        phi_cells = np.tile(np.stack([k * math.pi / azimuth, (k + 1) * math.pi / azimuth], -1), (2 * polar, 1))
        fractions = (np.arange(split) + 0.5) / split
        mu = mu_cells[:, :1] + (mu_cells[:, 1:] - mu_cells[:, :1]) * fractions
        phi = phi_cells[:, :1] + (phi_cells[:, 1:] - phi_cells[:, :1]) * fractions
        mu = np.repeat(mu, split, axis=1)
        phi = np.tile(phi, (1, split))
        ring = np.sqrt(np.maximum(1.0 - mu * mu, 0.0))
        samples = np.stack([ring * np.cos(phi), mu, ring * np.sin(phi)], -1)
        samples = np.concatenate([samples, samples * np.array([1.0, 1.0, -1.0])], axis=1)
        # 天頂角の余弦: 区間ごとに polar × split 点の GL の標本と、その区間の GL の点のラグランジュ基底（区間の外は 0）
        fine_nodes, fine_weights = np.polynomial.legendre.leggauss(polar * split)
        segment_nodes = directions[0, ::azimuth, 1].reshape(2, polar)
        mu_samples = []
        mu_weights = []
        lagrange = np.zeros((2 * polar * split, 2 * polar))
        for index, (low, high) in enumerate(segments):
            values = 0.5 * (high - low) * fine_nodes + 0.5 * (high + low)
            mu_samples.append(values)
            mu_weights.append(0.5 * (high - low) * fine_weights)
            nodes = segment_nodes[index]
            rows = slice(index * polar * split, (index + 1) * polar * split)
            for m in range(polar):
                others = np.delete(nodes, m)
                lagrange[rows, index * polar + m] = np.prod(
                    (values[:, None] - others[None, :]) / (nodes[m] - others[None, :]), axis=1)
        # 方位: 全周の等間隔の標本と、半周の点 (a + 0.5)π/A の余弦級数の基底（偶関数として補間する）
        phi_samples = (np.arange(2 * azimuth * split) + 0.5) * math.pi / (azimuth * split)
        nodes_phi = (np.arange(azimuth) + 0.5) * math.pi / azimuth
        orders = np.arange(1, azimuth)
        cardinal = (1.0 + 2.0 * np.sum(np.cos(orders[None, None, :] * nodes_phi[None, :, None]) *
                                       np.cos(orders[None, None, :] * phi_samples[:, None, None]), axis=-1)) / azimuth
        return {"radius": float(radius), "directions": directions[0], "weight": weight[0], "samples": samples,
                "mu_samples": np.concatenate(mu_samples), "mu_weights": np.concatenate(mu_weights), "lagrange": lagrange,
                "phi_samples": phi_samples, "phi_weight": math.pi / (azimuth * split), "cardinal": cardinal, "cache": {}}

    def cell_mean(self, quadrature, spec, targets):
        """向き targets [T,3] から見た位相関数 p(target·w) の、w の区画（と映した区画）の中の平均 [T,N]"""
        result = np.empty((len(targets), len(quadrature["directions"])))
        chunk = 64
        for start in range(0, len(targets), chunk):
            cosine = np.einsum("tc,nsc->tns", targets[start:start + chunk], quadrature["samples"])
            result[start:start + chunk] = phase_value(spec, cosine).mean(axis=-1)
        return result

    def cell_integral(self, quadrature, spec, targets):
        """求積の向きの光の重み ∫ p(target·w) b_m(w) dw [T,N]。b_m は求積の向きの間の補間の基底（天頂角の余弦のラグランジュ
        補間 × 方位の余弦級数の補間）で、位相関数が定数なら重みは GL の求積の重みに一致する。積分は細かい標本で取り、行の和を
        位相関数の球の積分 1 にそろえる"""
        mu = quadrature["mu_samples"]
        ring = np.sqrt(np.maximum(1.0 - mu * mu, 0.0))
        phi = quadrature["phi_samples"]
        scale = quadrature["mu_weights"][:, None] * quadrature["phi_weight"]
        lagrange = quadrature["lagrange"]
        cardinal = quadrature["cardinal"]
        result = np.empty((len(targets), len(quadrature["directions"])))
        chunk = 32
        for start in range(0, len(targets), chunk):
            part = targets[start:start + chunk]
            across = part[:, 0, None] * np.cos(phi)[None, :] + part[:, 2, None] * np.sin(phi)[None, :]
            cosine = part[:, 1, None, None] * mu[None, :, None] + ring[None, :, None] * across[:, None, :]
            value = phase_value(spec, cosine) * scale[None]
            value = np.einsum("tij,ja->tia", value, cardinal)
            result[start:start + chunk] = np.einsum("tia,im->tma", value, lagrange).reshape(len(part), -1)
        return result / np.sum(result, axis=1, keepdims=True)

    def grid_integral(self, quadrature, spec):
        """u の格子への cell_integral（求積ごとに残す）"""
        if spec not in quadrature["cache"]:
            quadrature["cache"][spec] = self.cell_integral(quadrature, spec, self.targets)
        return quadrature["cache"][spec]

    # --- 1 回目の光と、散乱の源 ---

    def first_order_at(self, quadrature, sun_cosines):
        """点の 1 回目の光の部品: w の向きの F_R・F_M・地表で返る太陽 [J,N,3] と、太陽の向きの F_R(s)・F_M(s) [J,3]"""
        sun = sun_vectors(sun_cosines)
        radius = quadrature["radius"]
        rayleigh, mie, ground = first_order_rays(self.medium, self.table, radius, quadrature["directions"], sun,
                                                 self.steps)
        toward_rayleigh, toward_mie, _ = first_order_rays(self.medium, self.table, radius, sun, sun, self.steps)
        diagonal = np.arange(len(sun))
        return {"sun": sun, "F_R": rayleigh, "F_M": mie, "ground_sun": ground,
                "Fs_R": toward_rayleigh[diagonal, diagonal], "Fs_M": toward_mie[diagonal, diagonal]}

    def sources_at(self, quadrature, first, higher, spec, targets=None):
        """点の散乱の源 ∫ p_X(u·w) L(p, w) dw [J,T,3]。L は 1 回目の光（first）と 2 回目以降の光（higher [J,N,3] か None）。
        1 回目の光の p_Y(s·w) F_Y(w) は F_Y(w) = F_Y(s) + (F_Y(w) − F_Y(s)) と分け、前を p_X ⊛ p_Y で解析的に、後を区画の
        平均の積で積む（差が太陽の向きで 0 になるので、前方の山どうしの積の求積の誤差が残らない）"""
        if targets is None:
            weights = self.grid_integral(quadrature, spec)
            targets = self.targets
        else:
            weights = self.cell_integral(quadrature, spec, targets)
        smooth = first["ground_sun"] if higher is None else first["ground_sun"] + higher
        result = np.einsum("tn,jnc->jtc", weights, smooth)
        between = first["sun"] @ targets.T
        for spec_y, values, toward in ((RAYLEIGH, first["F_R"], first["Fs_R"]), (self.mie, first["F_M"], first["Fs_M"])):
            sun_mean = self.cell_mean(quadrature, spec_y, first["sun"])
            result += toward[:, None, :] * phase_convolution(spec, spec_y, between)[..., None]
            result += np.einsum("tn,jn,jnc->jtc", weights, sun_mean, values - toward[:, None, :])
        return result

    def ground_irradiance(self, quadrature, first, higher):
        """地表の点の、空と大気から来る光の水平面の照度 [J,3]（first が None なら higher だけ）"""
        up = np.maximum(quadrature["directions"][:, 1], 0.0)
        result = np.einsum("n,jnc->jc", 4.0 * math.pi * quadrature["weight"] * up, higher)
        if first is not None:
            result += np.einsum("n,jnc->jc", 4.0 * math.pi * quadrature["weight"] * up, first["ground_sun"])
            for spec_y, values in ((RAYLEIGH, first["F_R"]), (self.mie, first["F_M"])):
                weights = self.cell_integral(quadrature, spec_y, first["sun"])
                result += np.einsum("jn,jnc->jc", weights * up, values)
        return result

    # --- 次の回の光 ---

    def prepare(self, values):
        """散乱の源の表（[高度 × 太陽の余弦 × u, C] か [高度, 太陽の余弦, u, C]）を lookup が引く形にする: 4 つの軸の両端に
        Catmull-Rom の外の点を足し（方位差は 0・π で折り返し、ほかは直線の外挿）、視線の余弦 × 方位差の 4 × 4 の窓を並べた
        [高度 + 2, 太陽の余弦 + 2, 視線の余弦 − 1, 方位差 − 1, C, 4, 4] の見方"""
        values = np.asarray(values, dtype=np.float64).reshape(self.shape + (-1,))
        for axis in range(4):
            values = pad_for_cubic(values, axis, mirror=axis == 3)
        return np.lib.stride_tricks.sliding_window_view(values, (4, 4), axis=(2, 3))

    def lookup(self, prepared, altitude, sun_up, view_up, azimuth):
        """散乱の源の表（prepare の形）を 4 次元の Catmull-Rom で引く"""
        altitude, sun_up, view_up, azimuth = np.broadcast_arrays(altitude, sun_up, view_up, azimuth)
        shape = altitude.shape
        sizes = self.shape
        coordinates = (np.sqrt(np.clip(altitude / self.height, 0.0, 1.0)).ravel() * (sizes[0] - 1),
                       (np.clip(sun_up, -1.0, 1.0).ravel() + 1.0) * 0.5 * (sizes[1] - 1),
                       (np.clip(view_up, -1.0, 1.0).ravel() + 1.0) * 0.5 * (sizes[2] - 1),
                       np.clip(azimuth, 0.0, math.pi).ravel() / math.pi * (sizes[3] - 1))
        result = np.empty((coordinates[0].size, prepared.shape[4]))
        chunk = 16384
        for start in range(0, len(result), chunk):
            part = slice(start, start + chunk)
            (ia, wa), (ic, wc), (iv, wv), (iz, wz) = [cubic_taps(value[part], size)
                                                      for value, size in zip(coordinates, sizes)]
            corner = wv[:, :, None] * wz[:, None, :]
            value = 0.0
            for da in range(4):
                for dc in range(4):
                    block = prepared[ia + da, ic + dc, iv, iz]
                    value = value + (wa[:, da] * wc[:, dc])[:, None] * np.einsum("mcvz,mvz->mc", block, corner)
            result[part] = value
        return result.reshape(shape + (prepared.shape[4],))

    def interpolate_point(self, values, altitude, sun_up):
        """点の格子（高度 × 太陽の余弦）の値 [A, C, 3] を 2 次元の Catmull-Rom で引く（0 より下は 0 にする）"""
        sizes = self.shape[:2]
        u = np.sqrt(np.clip(altitude / self.height, 0.0, 1.0)) * (sizes[0] - 1)
        m = (np.clip(sun_up, -1.0, 1.0) + 1.0) * 0.5 * (sizes[1] - 1)
        padded = pad_for_cubic(pad_for_cubic(values, 0), 1)
        ia, wa = cubic_taps(u, sizes[0])
        ic, wc = cubic_taps(m, sizes[1])
        result = 0.0
        for da in range(4):
            for dc in range(4):
                result = result + (wa[..., da] * wc[..., dc])[..., None] * padded[ia + da, ic + dc]
        return np.maximum(result, 0.0)

    def analytic_sources(self, spec, altitude, sun_up, between):
        """1 回目の光のうち太陽の向きの値 F_Y(s) で置いた部分の散乱の源 Σ_Y F_Y(s) (p_X ⊛ p_Y)(u·s)。表はこれを引いた残りを
        持ち、引くときに足し戻す（Mie の前方の山どうしの積の鋭い形を u の格子の線形補間に任せない）。between は u·s"""
        result = 0.0
        for spec_y, key in ((RAYLEIGH, "Fs_R"), (self.mie, "Fs_M")):
            result = result + self.interpolate_point(self.first[key], altitude, sun_up) * \
                phase_convolution(spec, spec_y, between)[..., None]
        return result

    def node_analytic(self, spec):
        """analytic_sources を表の格子の点で求めた値 [A, C, T, 3]"""
        between = sun_vectors(self.cosines) @ self.targets.T
        result = 0.0
        for spec_y, key in ((RAYLEIGH, "Fs_R"), (self.mie, "Fs_M")):
            result = result + self.first[key][:, :, None, :] * phase_convolution(spec, spec_y, between)[None, ..., None]
        return result

    def source_lookup(self, tables, specs, altitude, sun_up, view_up, azimuth, between, analytic):
        """散乱の源の表を引き、analytic なら 1 回目の光の解析的な部分を足し戻す（列は specs の順に 3 つずつ）"""
        value = self.lookup(tables, altitude, sun_up, view_up, azimuth)
        if analytic:
            altitude, sun_up, between = np.broadcast_arrays(altitude, sun_up, between)
            value = value + np.concatenate([self.analytic_sources(spec, altitude, sun_up, between) for spec in specs],
                                           axis=-1)
        return value

    def interpolate_ground(self, values, cosine):
        """太陽の余弦の格子の地表の照度 [S, 3] を Catmull-Rom で引く（0 より下は 0 にする）"""
        m = (np.clip(cosine, -1.0, 1.0) + 1.0) * 0.5 * (len(self.cosines) - 1)
        return np.maximum(cubic_1d(values, m), 0.0)

    def scattered_at(self, quadrature, sun_cosines, tables, ground, analytic):
        """散乱の源の表 tables（prepare の形）と地表の照度 ground（太陽の余弦の格子 [S,3]）から、点の w の向きに入ってくる次の回の
        光 [J,N,3]。analytic なら tables は 1 回目の光の解析的な部分を引いた残り"""
        medium = self.medium
        if "ray" not in quadrature["cache"]:
            quadrature["cache"]["ray"] = trace_rays(medium, quadrature["radius"], quadrature["directions"], self.steps)
        ray = quadrature["cache"]["ray"]
        sun = sun_vectors(sun_cosines)
        directions = quadrature["directions"]
        sun_up = np.einsum("jc,nkc->jnk", sun, ray["position"]) / ray["r"][None]
        view_up = np.einsum("nc,nkc->nk", directions, ray["position"]) / ray["r"]
        between = (sun @ directions.T)[..., None]
        azimuth = relative_azimuth(between, view_up[None], sun_up)
        sources = self.source_lookup(tables, (RAYLEIGH, self.mie), (ray["r"] - medium.bottom)[None], sun_up,
                                     view_up[None], azimuth, between, analytic)
        emitted = ray["rayleigh"][None] * sources[..., :3] + ray["mie"][None, ..., None] * sources[..., 3:]
        result = np.sum(ray["weight"][None] * emitted, axis=2)
        diffuse = self.interpolate_ground(ground, sun @ ray["ground"].T)
        return result + np.where(ray["hits_ground"][None, :, None],
                                 ray["end_t"][None] * medium.albedo / math.pi * diffuse, 0.0)

    # --- 回数で足す ---

    def solve(self):
        altitude_count, cosine_count = self.shape[:2]
        chunk = 4
        tasks = [(i, np.arange(start, min(start + chunk, cosine_count)))
                 for i in range(altitude_count) for start in range(0, cosine_count, chunk)]
        specs = (RAYLEIGH, self.mie)
        for quadrature in self.quadratures:
            for spec in specs:
                self.grid_integral(quadrature, spec)
        firsts = parallel_map(lambda task: self.first_order_at(self.quadratures[task[0]], self.cosines[task[1]]), tasks)
        self.first = {}
        for key in ("F_R", "F_M", "ground_sun", "Fs_R", "Fs_M"):
            shape = (altitude_count, cosine_count) + firsts[0][key].shape[1:]
            self.first[key] = np.zeros(shape)
            for (i, js), first in zip(tasks, firsts):
                self.first[key][i, js] = first[key]
        directions_count = len(self.quadratures[0]["directions"])
        target_count = len(self.targets)

        def first_slice(i, js):
            values = {key: self.first[key][i, js] for key in self.first}
            values["sun"] = sun_vectors(self.cosines[js])
            return values

        def first_sources(task):
            i, js = task
            first = first_slice(i, js)
            return np.concatenate([self.sources_at(self.quadratures[i], first, None, spec) for spec in specs], axis=-1)

        tables = np.zeros((altitude_count, cosine_count, target_count, 6))
        for (i, js), values in zip(tasks, parallel_map(first_sources, tasks)):
            tables[i, js] = values
        all_cosines = np.arange(cosine_count)
        ground = self.ground_irradiance(self.quadratures[0], first_slice(0, all_cosines),
                                        np.zeros((cosine_count, directions_count, 3)))
        # 1 回目の光の解析的な部分（表の格子の点の値）。表はこれを引いた残りを持つ
        node_analytic = np.concatenate([self.node_analytic(spec) for spec in specs], axis=-1)
        total_tables = tables.copy()
        total_ground = ground.copy()
        higher = np.zeros((altitude_count, cosine_count, directions_count, 3))
        ratios = []
        orders = 1
        fraction = 1.0
        while orders < ORDER_LIMIT:
            analytic = orders == 1
            prepared = self.prepare(tables - node_analytic if analytic else tables)
            radiance = np.zeros_like(higher)
            for (i, js), values in zip(tasks, parallel_map(
                    lambda task: self.scattered_at(self.quadratures[task[0]], self.cosines[task[1]], prepared, ground,
                                                   analytic), tasks)):
                radiance[i, js] = values
            higher += radiance
            following = np.zeros_like(tables)
            for i, quadrature in enumerate(self.quadratures):
                following[i] = np.concatenate([np.einsum("tn,snc->stc", self.grid_integral(quadrature, spec), radiance[i])
                                               for spec in specs], axis=-1)
            ground = self.ground_irradiance(self.quadratures[0], None, radiance[0])
            orders += 1
            luminance = np.concatenate([LUMINANCE_WEIGHTS, LUMINANCE_WEIGHTS])
            ratios.append(float(np.sum(following @ luminance) / max(np.sum(tables @ luminance), 1e-30)))
            total_tables += following
            total_ground += ground
            tables = following
            fraction = float(np.max(np.abs(following)) / max(np.max(np.abs(total_tables)), 1e-30))
            if fraction <= ORDER_STOP_FRACTION:
                break
        self.orders = orders
        self.order_ratios = ratios
        self.last_order_fraction = fraction
        self.tables = self.prepare(total_tables - node_analytic)
        self.ground = total_ground
        self.higher = higher

    # --- 引く ---

    def ground_diffuse(self, sun_cosine):
        """地表の、空と大気から来る光の水平面の照度（全回の和）"""
        return self.interpolate_ground(self.ground, sun_cosine)

    def table_for(self, spec):
        """位相関数 spec の散乱の源の表（全回の和から 1 回目の光の解析的な部分を引いた残り、prepare の形で 3 列）。高さフォグの
        HG のように表に無い位相関数のときに作る"""
        if spec == RAYLEIGH:
            return self.tables[:, :, :, :, :3]
        if spec == self.mie:
            return self.tables[:, :, :, :, 3:]
        if spec not in self.extra_tables:
            altitude_count, cosine_count = self.shape[:2]
            values = np.zeros((altitude_count, cosine_count, len(self.targets), 3))

            def task_sources(i):
                first = {key: self.first[key][i] for key in self.first}
                first["sun"] = sun_vectors(self.cosines)
                return self.sources_at(self.quadratures[i], first, self.higher[i], spec)

            for i, value in enumerate(parallel_map(task_sources, range(altitude_count))):
                values[i] = value
            self.extra_tables[spec] = self.prepare(values - self.node_analytic(spec))
        return self.extra_tables[spec]

    def evaluate(self, altitude, sun_cosine, spec=ISOTROPIC, targets=None):
        """任意の点の散乱の源を、表の補間でなく、その点から視線を積んで求める（2 回目以降の光は収束した表から）。
        既定は等方の源 = 入ってくる光の球の平均（Ψ と同じ意味）[N,3]。targets を与えると [N,T,3]"""
        altitude = np.asarray(altitude, dtype=np.float64).ravel()
        sun_cosine = np.asarray(sun_cosine, dtype=np.float64).ravel()
        single = targets is None
        targets = np.array([[0.0, 1.0, 0.0]]) if single else np.asarray(targets, dtype=np.float64)

        def point(index):
            quadrature = self.quadrature(self.medium.bottom + altitude[index])
            first = self.first_order_at(quadrature, sun_cosine[index:index + 1])
            higher = self.scattered_at(quadrature, sun_cosine[index:index + 1], self.tables, self.ground, True)
            return self.sources_at(quadrature, first, higher, spec, targets)[0]

        values = np.array(parallel_map(point, range(len(altitude))))
        return values[:, 0] if single else values


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
        self.directional = DirectionalField(self.medium, self.table, p["orders_altitude"], p["orders_cosine"],
                                            p["orders_polar"], p["orders_azimuth"], p["orders_view_cosine"],
                                            p["orders_view_azimuth"], p["orders_steps"],
                                            p["orders_phase_subdivisions"])
        self.seconds = time.time() - started

    def evaluate_psi(self, altitude, sun_cosine):
        """任意の点の Ψ（A は論文の Ψ_ms、B は 2 回目以降の散乱の源になる光の球の平均）。戻り値は (Ψ_A [N,3], Ψ_B [N,3])"""
        return self.field.evaluate(altitude, sun_cosine), self.directional.evaluate(altitude, sun_cosine)

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
        air_sun = (rayleigh_s * medium.rayleigh_phase(view_cosine)[:, None, None] +
                   (mie_s * medium.mie_phase(view_cosine)[:, None])[..., None]) * sun_t
        if version == "A":
            psi = self.field.psi(altitude, sun_cosine)
            air_multiple = (rayleigh_s + mie_s[..., None]) * psi
        else:
            # 点での視線の天頂角の余弦と、太陽からの方位差で散乱の源の表を引く
            view_up = (px * directions[:, 0, None] + py * directions[:, 1, None] + pz * directions[:, 2, None]) / r
            azimuth = relative_azimuth(view_cosine[:, None], view_up, sun_cosine)
            field = self.directional
            between = view_cosine[:, None]
            sources = field.source_lookup(field.tables, (RAYLEIGH, field.mie), altitude, sun_cosine, view_up, azimuth,
                                          between, True)
            air_multiple = rayleigh_s * sources[..., :3] + mie_s[..., None] * sources[..., 3:]
        weight = view_t * dt[..., None]
        result = {
            "air_sun": np.sum(weight * air_sun, axis=1),
            "air_multiple": np.sum(weight * air_multiple, axis=1),
        }
        if fog is not None:
            fog_phase = medium.mie_phase(view_cosine, fog["phase_anisotropy"])
            result["fog_sun"] = np.sum(weight * fog_s[..., None] * fog_phase[:, None, None] * sun_t, axis=1)
            if version == "A":
                fog_sources = psi
            else:
                fog_spec = henyey_greenstein(fog["phase_anisotropy"])
                fog_sources = field.source_lookup(field.table_for(fog_spec), (fog_spec,), altitude, sun_cosine, view_up,
                                                  azimuth, between, True)
            result["fog_multiple"] = np.sum(weight * fog_s[..., None] * fog_sources, axis=1)
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
                irradiance = irradiance + self.directional.ground_diffuse(ground_cosine)
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
    quantities["psi_A"], quantities["psi_B"] = atmosphere.evaluate_psi(ha.ravel(), ca.ravel())
    quantities["sun_transmittance"] = atmosphere.sun_transmittance(0.0, sun)
    # 地表の近くの水平の 10 km の区間（空気遠近の形）
    horizontal = direction_from_sun_offset(sun, np.zeros(4), np.array([0.0, 90.0, 180.0, 270.0]))
    segment, segment_t = atmosphere.march(1500.0, horizontal, sun, "B", start=300.0, end=10000.0)
    quantities["segment_scattering"] = segment["total"]
    quantities["segment_transmittance"] = segment_t
    return quantities


def second_order_check(atmosphere, altitude, sun_altitude_degrees):
    """B の 2 回目の散乱の源 ∫ p_X(u·w) L_1(p, w) dw（DirectionalField.sources_at）を、点の周りの細かい向きの求積で 1 回目の光
    L_1 を直接積んだ値と比べる。細かい求積は太陽の向きを軸にした天頂角（0〜SECOND_ORDER_SPLIT rad とその先をそれぞれ
    ガウス・ルジャンドル）× 方位（等間隔）で、Mie の前方の山の積も細かく取る。比べる向き u は太陽・天頂・太陽から方位 90° の
    地平線・太陽と反対の地平線"""
    medium = atmosphere.medium
    field = atmosphere.directional
    radius = medium.bottom + altitude
    sun = sun_direction_from_angles(sun_altitude_degrees, 0.0)
    # 横と反対の向きは仰角 10°（地平線の上では Mie の山が空と地表の境に掛かり、太陽を軸にした細かい求積の方が粗くなる）
    rise = math.radians(10.0)
    targets = np.array([sun, [0.0, 1.0, 0.0], [0.0, math.sin(rise), math.cos(rise)], [-math.cos(rise), math.sin(rise), 0.0]])
    near, far, azimuth_count = SECOND_ORDER_COUNTS
    thetas = []
    theta_weights = []
    for low, high, count in ((0.0, SECOND_ORDER_SPLIT, near), (SECOND_ORDER_SPLIT, math.pi, far)):
        nodes, weights = np.polynomial.legendre.leggauss(count)
        theta = 0.5 * (high - low) * nodes + 0.5 * (high + low)
        thetas.append(theta)
        theta_weights.append(0.5 * (high - low) * weights * np.sin(theta))
    theta, phi = np.meshgrid(np.concatenate(thetas), (np.arange(azimuth_count) + 0.5) * 2.0 * math.pi / azimuth_count,
                             indexing="ij")
    side = np.array([-sun[1], sun[0], 0.0])
    across = np.array([0.0, 0.0, 1.0])
    directions = (np.cos(theta)[..., None] * sun + (np.sin(theta) * np.cos(phi))[..., None] * side +
                  (np.sin(theta) * np.sin(phi))[..., None] * across).reshape(-1, 3)
    solid_angle = np.repeat(np.concatenate(theta_weights) * 2.0 * math.pi / azimuth_count, azimuth_count)
    chunk = 2048

    def first_radiance(start):
        part = directions[start:start + chunk]
        rayleigh, mie, ground = first_order_rays(medium, atmosphere.table, radius, part, sun[None], SECOND_ORDER_STEPS)
        cosine = part @ sun
        return (rayleigh[0] * phase_value(RAYLEIGH, cosine)[:, None] + mie[0] * phase_value(field.mie, cosine)[:, None] +
                ground[0])

    radiance = np.concatenate(parallel_map(first_radiance, range(0, len(directions), chunk)))
    quadrature = field.quadrature(radius)
    first = field.first_order_at(quadrature, np.array([sun[1]]))
    result = {"altitude_m": altitude, "sun_altitude_degrees": sun_altitude_degrees, "targets": targets.tolist()}
    worst = 0.0
    for name, spec in (("rayleigh", RAYLEIGH), ("mie", field.mie), ("isotropic", ISOTROPIC)):
        direct = np.einsum("tn,n,nc->tc", phase_value(spec, targets @ directions.T), solid_angle, radiance)
        method = field.sources_at(quadrature, first, None, spec, targets)[0]
        error = float(np.max(np.abs(method - direct) / np.maximum(np.abs(direct), 1e-30)))
        worst = max(worst, error)
        result[name] = {"direct": direct.tolist(), "field": method.tolist(), "max_relative": error}
    # 太陽の向きの Mie の源 ÷ 等方の源（入ってくる光の球の平均。2 回目以降を等方とする近似が使う値）
    result["mie_toward_sun_over_isotropic"] = (np.array(result["mie"]["direct"][0]) /
                                              np.array(result["isotropic"]["direct"][0])).tolist()
    # 視線の上の 2 回目の散乱の源 σ_R G_R + σ_M G_M（その高度の係数）と、等方とした源 (σ_R + σ_M) × 球の平均
    rayleigh_s, mie_s, _ = medium.coefficients(np.array(altitude))
    combined = {}
    for key in ("direct", "field"):
        combined[key] = (rayleigh_s * np.array(result["rayleigh"][key]) + mie_s * np.array(result["mie"][key]))
    isotropic = (rayleigh_s + mie_s) * np.array(result["isotropic"]["direct"])
    combined_error = np.abs(combined["field"] - combined["direct"]) / np.maximum(np.abs(combined["direct"]), 1e-30)
    result["combined"] = {"direct": combined["direct"].tolist(), "field": combined["field"].tolist(),
                          "isotropic": isotropic.tolist(), "max_relative": float(np.max(combined_error)),
                          "toward_sun_relative": combined_error[0].tolist()}
    result["combined_toward_sun_over_isotropic"] = (combined["direct"][0] / isotropic[0]).tolist()
    result["max_relative"] = max(worst, float(np.max(combined_error)))
    return result


def first_order_check(atmosphere, altitude, sun_altitude_degrees):
    """1 回目だけの値を A と B で比べる。A は視線の上で太陽の光を位相関数で散乱させて積む march の 1 回目の項（air_sun）と、
    地表で返る太陽（A の地表は直射だけ）。B は DirectionalField が 2 回目の散乱の源に渡す 1 回目の光
    L_1(p, w) = p_R(s·w) F_R(w) + p_M(s·w) F_M(w) + 地表で返る太陽（first_order_rays）。同じ歩数で、地表へ下りる向きも含める"""
    medium = atmosphere.medium
    field = atmosphere.directional
    sun = sun_direction_from_angles(sun_altitude_degrees, 0.0)
    elevations = np.array([-30.0, -10.0, -2.0, 0.0, 2.0, 5.0, 10.0, 20.0, 45.0, 90.0])
    offsets = np.array([0.0, 45.0, 90.0, 135.0, 180.0])
    el, az = np.meshgrid(elevations, offsets, indexing="ij")
    directions = direction_from_sun_offset(sun, el.ravel(), az.ravel())
    steps = atmosphere.parameters["view_steps"]
    components, _ = atmosphere.march(altitude, directions, sun, "A", steps=steps)
    value_a = components["air_sun"] + components["ground"]
    rayleigh, mie, ground = first_order_rays(medium, atmosphere.table, medium.bottom + altitude, directions, sun[None],
                                             steps)
    cosine = directions @ sun
    value_b = (rayleigh[0] * phase_value(RAYLEIGH, cosine)[:, None] + mie[0] * phase_value(field.mie, cosine)[:, None] +
               ground[0])
    error = float(np.max(np.abs(value_b - value_a) / np.maximum(np.abs(value_a), 1e-30)))
    return {"altitude_m": altitude, "sun_altitude_degrees": sun_altitude_degrees, "directions": len(directions),
            "max_relative": error, "ground_directions": int(np.sum(np.any(components["ground"] > 0.0, axis=1)))}


def compare_levels(coarse, fine):
    worst = {}
    for key, value in fine.items():
        reference = np.asarray(value, dtype=np.float64)
        other = np.asarray(coarse[key], dtype=np.float64)
        worst[key] = float(np.max(np.abs(other - reference) / np.maximum(np.abs(reference), CONVERGENCE_RADIANCE_FLOOR)))
    return worst


def reference_sets(dump_path):
    """比べる組（名前・設定・太陽の高度・観測点の高度）と、最小再現の高度（空気遠近の起点の高度 + カメラの y。書き出しが
    無いときは評価者の値）と、見つかった問題"""
    failures = []
    repro_altitude = REPRO_ALTITUDE_M
    sets = [("paper", PAPER_SETTINGS, 38.0, 100.0)]
    if os.path.isfile(dump_path):
        with open(dump_path, encoding="utf-8") as f:
            meta = json.load(f)
        sky = meta["sky"]
        repro_altitude = float(meta["aerial_perspective"]["origin_altitude_m"]) + float(meta["camera_position"][1])
        sets.append(("engine", settings_from_dump(meta), float(sky["sun_altitude_degrees"]), float(sky["observer_altitude_m"])))
    else:
        failures.append(f"書き出しの JSON が無い: {dump_path}")
    return sets, repro_altitude, failures


def inputs_sha256(dump_path):
    """段 2 の保存した結果が今の入力のものかを見るハッシュ（この台本と書き出しの JSON の中身）"""
    digest = hashlib.sha256()
    for path in (os.path.abspath(__file__), dump_path):
        digest.update(os.path.basename(path).encode("utf-8"))
        if os.path.isfile(path):
            with open(path, "rb") as f:
                digest.update(f.read())
    return digest.hexdigest()


def correctness_checks(atmosphere, settings, sun_altitude, observer, repro_altitude):
    """収束以外の検査: 鉛直の光学的深さの解析解、f_ms < 1、B の回数の打ち切り、(1) 2 回目の散乱の源と位相関数の角度の積分
    （最小再現の高度と観測点）、(3) 1 回目だけの値の A と B の一致（観測点と最小再現の高度）"""
    analytic = vertical_optical_depth(settings)
    table_vertical = atmosphere.table.optical_depth(np.array(atmosphere.medium.bottom), np.array(1.0))
    second_order = [second_order_check(atmosphere, altitude, sun_altitude) for altitude in (repro_altitude, observer)]
    first_order = [first_order_check(atmosphere, altitude, sun_altitude) for altitude in (observer, repro_altitude)]
    directional = atmosphere.directional
    return {
        "level": atmosphere.level,
        "seconds": atmosphere.seconds,
        "seconds_directional": directional.seconds,
        "vertical_optical_depth": {"table": table_vertical.tolist(), "analytic": analytic.tolist(),
                                   "max_relative_error": float(np.max(np.abs(table_vertical - analytic) / analytic))},
        "f_ms_max": float(np.max(atmosphere.field.f_ms)),
        "orders": directional.orders,
        "order_ratios_first10": directional.order_ratios[:10],
        "last_order_fraction": directional.last_order_fraction,
        "second_order_check": second_order,
        "second_order_max_relative": max(check["max_relative"] for check in second_order),
        "first_order_check": first_order,
        "first_order_max_relative": max(check["max_relative"] for check in first_order),
    }


def check_failures(name, checks):
    failures = []
    level = checks["level"]
    # 解析解との一致は段 2 の透過率の表で見る（段 1 の表は荒野の設定で 1.2e-3）
    if level >= 2 and checks["vertical_optical_depth"]["max_relative_error"] > ANALYTIC_TOLERANCE:
        failures.append(f"{name} 段 {level}: 鉛直の光学的深さが解析解と "
                        f"{checks['vertical_optical_depth']['max_relative_error']:.2e} 違う")
    if not checks["f_ms_max"] < 1.0:
        failures.append(f"{name} 段 {level}: f_ms の最大 {checks['f_ms_max']} が 1 以上（論文の級数が収束しない）")
    if checks["last_order_fraction"] > ORDER_STOP_FRACTION:
        failures.append(f"{name} 段 {level}: 散乱の回数で足す版が {ORDER_LIMIT} 回で収束しない")
    if checks["second_order_max_relative"] > SECOND_ORDER_TOLERANCE:
        failures.append(f"{name} 段 {level}: B の 2 回目の散乱の源が、細かい求積で直接積んだ値と "
                        f"{checks['second_order_max_relative']:.2e} 違う")
    if checks["first_order_max_relative"] > FIRST_ORDER_TOLERANCE:
        failures.append(f"{name} 段 {level}: 1 回目だけの値が A と B で {checks['first_order_max_relative']:.2e} 違う")
    return failures


def solve(dump_path, out_dir):
    """段 2（重い。1 組数分）の収束を見る量と検査を求め、入力のハッシュつきで保存する。--verify はこれと段 1 を比べる"""
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sets, repro_altitude, failures = reference_sets(dump_path)
    result = {"inputs_sha256": inputs_sha256(dump_path), "dump": dump_path, "repro_altitude_m": repro_altitude,
              "level": 2, "sets": {}}
    for name, settings, sun_altitude, observer in sets:
        fine = Atmosphere(settings, 2)
        quantities = standard_quantities(fine, sun_altitude, observer)
        checks = correctness_checks(fine, settings, sun_altitude, observer, repro_altitude)
        failures += check_failures(name, checks)
        result["sets"][name] = {"parameters": fine.parameters, "sun_altitude_degrees": sun_altitude,
                                "observer_altitude_m": observer, "checks": checks,
                                "quantities": {key: np.asarray(value).tolist() for key, value in quantities.items()}}
        print(f"ATMOSPHERE_REFERENCE_SOLVE set={name} level=2 seconds={fine.seconds:.1f} orders={checks['orders']} "
              f"second_order_max_rel={checks['second_order_max_relative']:.2e} "
              f"first_order_A_vs_B={checks['first_order_max_relative']:.1e}", flush=True)
    result["failures"] = failures
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, SOLVE_FILE)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=1, ensure_ascii=False)
    for failure in failures:
        print(f"ATMOSPHERE_REFERENCE_SOLVE failure: {failure}")
    print(f"ATMOSPHERE_REFERENCE_SOLVE result={'pass' if not failures else 'fail'} inputs_sha256={result['inputs_sha256']} "
          f"report={path}")
    return 0 if not failures else 1


def verify(dump_path, out_dir):
    """段 1 をその場で解き、--solve が保存した段 2 と比べる（入力のハッシュが今と同じことも見る）。収束以外の検査は段 1 を
    その場で、段 2 は保存した値で見る"""
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sets, repro_altitude, failures = reference_sets(dump_path)
    saved_path = os.path.join(out_dir, SOLVE_FILE)
    saved = None
    if os.path.isfile(saved_path):
        with open(saved_path, encoding="utf-8") as f:
            saved = json.load(f)
    else:
        failures.append(f"段 2 の保存した結果が無い（--solve で作る）: {saved_path}")
    current_hash = inputs_sha256(dump_path)
    inputs_current = saved is not None and saved.get("inputs_sha256") == current_hash
    if saved is not None and not inputs_current:
        failures.append("段 2 の保存した結果の入力のハッシュが今の台本・書き出しと違う（--solve をやり直す）")
    report = {"criteria": {"convergence_tolerance": CONVERGENCE_TOLERANCE, "radiance_floor": CONVERGENCE_RADIANCE_FLOOR,
                           "analytic_tolerance": ANALYTIC_TOLERANCE, "second_order_tolerance": SECOND_ORDER_TOLERANCE,
                           "first_order_tolerance": FIRST_ORDER_TOLERANCE, "level1": LEVEL1},
              "inputs_sha256": current_hash, "inputs_current": inputs_current, "solve_report": saved_path, "sets": {}}
    for name, settings, sun_altitude, observer in sets:
        coarse = Atmosphere(settings, 1)
        q_coarse = standard_quantities(coarse, sun_altitude, observer)
        checks = {"level1": correctness_checks(coarse, settings, sun_altitude, observer, repro_altitude)}
        failures += check_failures(name, checks["level1"])
        fine_record = (saved or {}).get("sets", {}).get(name)
        worst = {}
        if fine_record is None:
            failures.append(f"{name}: 段 2 の保存した結果に組が無い")
        else:
            checks["level2"] = fine_record["checks"]
            failures += check_failures(name, fine_record["checks"])
            worst = compare_levels(q_coarse, fine_record["quantities"])
        converged = bool(worst) and max(worst.values()) <= CONVERGENCE_TOLERANCE
        if worst and not converged:
            failures.append(f"{name}: 段 1 と段 2 の差が {CONVERGENCE_TOLERANCE} を超える: " +
                            ", ".join(f"{k}={v:.4f}" for k, v in worst.items() if v > CONVERGENCE_TOLERANCE))
        q_fine = {key: np.asarray(value) for key, value in fine_record["quantities"].items()} if fine_record else q_coarse
        repro = checks["level1"]["second_order_check"][0]
        repro_ratio = np.array(repro["combined_toward_sun_over_isotropic"])
        # 地表・太陽の余弦 0.6 の点（荒野の太陽の高度 38° に近い）
        ground_index = PSI_PROBE_COSINES.index(0.6)
        ratio_a = q_fine["psi_A"][ground_index] / np.maximum(q_fine["psi_B"][ground_index], 1e-30)
        sun = sun_direction_from_angles(sun_altitude, 0.0)
        record = {
            "settings": settings, "sun_altitude_degrees": sun_altitude, "observer_altitude_m": observer,
            "max_relative_difference_level1_vs_level2": worst,
            "converged": converged,
            "checks": checks,
            "repro": {"altitude_m": repro_altitude, "sun_altitude_degrees": sun_altitude,
                      "toward_sun_relative": repro["combined"]["toward_sun_relative"],
                      "combined_toward_sun_over_isotropic": repro_ratio.tolist(),
                      "evaluator_ratio": list(EVALUATOR_SUN_RATIO)},
            "sun_transmittance_ground": q_fine["sun_transmittance"].tolist(),
            "sky_horizontal_irradiance_per_lux": {"A": q_fine["sky_irradiance_A"].tolist(),
                                                  "B": q_fine["sky_irradiance_B"].tolist()},
            "psi_ground_A_over_B": ratio_a.tolist(),
            "sun_direction": sun.tolist(),
        }
        report["sets"][name] = record
        level2 = checks.get("level2", {})
        print(f"ATMOSPHERE_REFERENCE set={name} converged={int(converged)} "
              f"max_level_diff={max(worst.values(), default=float('nan')):.5f} "
              f"worst={max(worst, key=worst.get) if worst else '-'} "
              f"analytic_vertical={checks['level1']['vertical_optical_depth']['max_relative_error']:.2e} "
              f"f_ms_max={checks['level1']['f_ms_max']:.4f} orders={checks['level1']['orders']}/{level2.get('orders')} "
              f"order_ratio_2={checks['level1']['order_ratios_first10'][0]:.4f} "
              f"second_order_max_rel={checks['level1']['second_order_max_relative']:.2e}/"
              f"{level2.get('second_order_max_relative', float('nan')):.2e} "
              f"repro_altitude={repro_altitude:.6f} "
              f"repro_toward_sun_rel={np.round(repro['combined']['toward_sun_relative'], 5).tolist()} "
              f"repro_sun_over_isotropic={np.round(repro_ratio, 4).tolist()} "
              f"first_order_A_vs_B={checks['level1']['first_order_max_relative']:.1e}/"
              f"{level2.get('first_order_max_relative', float('nan')):.1e} "
              f"sun_T={np.round(q_fine['sun_transmittance'], 4).tolist()} "
              f"sky_E_A={np.round(q_fine['sky_irradiance_A'], 4).tolist()} "
              f"sky_E_B={np.round(q_fine['sky_irradiance_B'], 4).tolist()} "
              f"seconds={coarse.seconds:.1f}+{level2.get('seconds', float('nan')):.1f}(saved)")
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
    print(f"ATMOSPHERE_REFERENCE result={status} sets={len(sets)} inputs_current={int(inputs_current)} report={path}")
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser(description="空と霞の数値の正解（Hillaire 2020 の定義から独立に積分する）")
    parser.add_argument("--solve", action="store_true", help="段 2 の正解と検査を求めて保存する（重い）")
    parser.add_argument("--verify", action="store_true", help="段 1 を解き、保存した段 2 との収束と解析解との一致を確かめる")
    parser.add_argument("--dump", default=DEFAULT_DUMP, help="エンジンの書き出しの JSON（<視点>.atmos.json）")
    parser.add_argument("--out", default=DEFAULT_OUT, help="結果の JSON の出力先")
    args = parser.parse_args()
    if args.solve:
        status = solve(args.dump, args.out)
        if status or not args.verify:
            return status
    if args.verify:
        return verify(args.dump, args.out)
    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
