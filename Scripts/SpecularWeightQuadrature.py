#!/usr/bin/env python3
# RTGI の鏡面光線の平均重みが DFG LUT の Ess と一致するかを独立な求積で確かめる。
# 対象は PbrMaterialEvaluation.glsl の EvaluateGGXVisibleNormalSampleWeight と
# DfgLut.cpp の BuildDfgLutHalfData。使い方: python Scripts/SpecularWeightQuadrature.py --check
# 判定は LUT のセル中心の範囲に粗さを切り詰め、G の一致を調べる。粗さ 1.0 は
# 最後のセル中心 255.5/256 を参照するため、元の粗さで積分するエンジンの比には
# LUT の端によるずれが残る。その比は参考値として分けて出し、判定には使わない。

import argparse
import functools
import math

import numpy as np


LUT_SIZE = 256
LUT_SAMPLE_COUNT = 4096
RATIO_TOLERANCE = 0.005
CONVERGENCE_TOLERANCE = 0.0001


@functools.lru_cache(maxsize=None)
def hammersley_samples():
    """LUT と同じ順序の Hammersley 列を単精度で返す。"""
    indices = np.arange(LUT_SAMPLE_COUNT, dtype=np.uint32)
    bits = (indices << 16) | (indices >> 16)
    for shift, mask in ((1, 0x55555555), (2, 0x33333333),
                        (4, 0x0F0F0F0F), (8, 0x00FF00FF)):
        bits = ((bits & np.uint32(mask)) << shift) | (
            (bits >> shift) & np.uint32(mask)
        )
    u = indices.astype(np.float32) / np.float32(LUT_SAMPLE_COUNT)
    v = bits.astype(np.float32) * np.float32(2.0 ** -32)
    return u, v


@functools.lru_cache(maxsize=None)
def dfg_lut_cell(x, y):
    """必要なセルだけ生成し、A と B をそれぞれ半精度に丸める。"""
    f = np.float32
    n_dot_v = (f(x) + f(0.5)) / f(LUT_SIZE)
    roughness = (f(y) + f(0.5)) / f(LUT_SIZE)
    u, v = hammersley_samples()
    a = roughness * roughness
    phi = f(2.0) * f(math.pi) * u
    n_dot_h = np.sqrt((f(1.0) - v) / (f(1.0) + (a * a - f(1.0)) * v))
    sin_theta = np.sqrt(f(1.0) - n_dot_h * n_dot_h)
    h_x = sin_theta * np.cos(phi)
    v_x = np.sqrt(f(1.0) - n_dot_v * n_dot_v)
    raw_v_dot_h = v_x * h_x + n_dot_v * n_dot_h
    n_dot_l = np.maximum(f(2.0) * raw_v_dot_h * n_dot_h - n_dot_v, f(0.0))
    v_dot_h = np.maximum(raw_v_dot_h, f(0.0))
    k = roughness * roughness / f(2.0)
    g_v = n_dot_v / (n_dot_v * (f(1.0) - k) + k)
    g_l = n_dot_l / (n_dot_l * (f(1.0) - k) + k)
    g_vis = (g_v * g_l * v_dot_h) / (n_dot_h * n_dot_v + f(0.0001))
    fresnel = np.power(f(1.0) - v_dot_h, f(5.0))

    # C++ と同じ標本順で単精度に加算し、最後に各成分を半精度へ変換する。
    a_sum = np.cumsum((f(1.0) - fresnel) * g_vis, dtype=np.float32)[-1]
    b_sum = np.cumsum(fresnel * g_vis, dtype=np.float32)[-1]
    a_half = np.float16(np.clip(a_sum / f(LUT_SAMPLE_COUNT), f(0.0), f(1.0)))
    b_half = np.float16(np.clip(b_sum / f(LUT_SAMPLE_COUNT), f(0.0), f(1.0)))
    return float(a_half), float(b_half)


def dfg_lut_ess(n_dot_v, roughness):
    """テクスチャ座標を端でクランプし、半精度の A と B を双線形補間する。"""
    x = min(max(n_dot_v * LUT_SIZE - 0.5, 0.0), LUT_SIZE - 1.0)
    y = min(max(roughness * LUT_SIZE - 0.5, 0.0), LUT_SIZE - 1.0)
    x0, y0 = int(x), int(y)
    x1, y1 = min(x0 + 1, LUT_SIZE - 1), min(y0 + 1, LUT_SIZE - 1)
    tx, ty = x - x0, y - y0
    ess = 0.0
    for cell_x, weight_x in ((x0, 1.0 - tx), (x1, tx)):
        for cell_y, weight_y in ((y0, 1.0 - ty), (y1, ty)):
            a, b = dfg_lut_cell(cell_x, cell_y)
            ess += (a + b) * weight_x * weight_y
    return ess


@functools.lru_cache(maxsize=None)
def hemisphere_nodes(resolution):
    """天頂方向を Gauss–Legendre、方位角を等間隔で離散化する。"""
    nodes, weights = np.polynomial.legendre.leggauss(resolution)
    n_dot_l = (nodes[:, None] + 1.0) / 2.0
    elevation_weights = weights[:, None] / 2.0
    azimuth_count = 2 * resolution
    phi = (np.arange(azimuth_count) + 0.5) * (2.0 * math.pi / azimuth_count)
    return n_dot_l, elevation_weights, np.cos(phi), 2.0 * math.pi / azimuth_count


def smith_lambda(n_dot_x, alpha_squared):
    """GGX の可視法線分布に用いる Smith の Lambda。"""
    return (np.sqrt(1.0 + alpha_squared * (1.0 - n_dot_x ** 2) / n_dot_x ** 2) - 1.0) / 2.0


def integrate_weights(n_dot_v, roughness, resolution):
    """上半球における新旧の重み × VNDF の pdf を別々に積分する。"""
    n_dot_l, elevation_weights, cos_phi, azimuth_step = hemisphere_nodes(resolution)
    l_x = np.sqrt(1.0 - n_dot_l ** 2) * cos_phi
    v_dot_l = math.sqrt(1.0 - n_dot_v ** 2) * l_x + n_dot_v * n_dot_l
    n_dot_h = (n_dot_v + n_dot_l) / np.sqrt(2.0 + 2.0 * v_dot_l)

    alpha = roughness * roughness
    alpha_squared = alpha * alpha
    distribution = alpha_squared / (
        math.pi * (n_dot_h ** 2 * (alpha_squared - 1.0) + 1.0) ** 2
    )
    lambda_v = smith_lambda(n_dot_v, alpha_squared)
    lambda_l = smith_lambda(n_dot_l, alpha_squared)
    g1_v = 1.0 / (1.0 + lambda_v)
    pdf = distribution * g1_v / (4.0 * n_dot_v)

    k = roughness * roughness / 2.0
    schlick_v = n_dot_v / (n_dot_v * (1.0 - k) + k)
    schlick_l = n_dot_l / (n_dot_l * (1.0 - k) + k)
    weight = schlick_v * schlick_l * (1.0 + lambda_v)
    old_weight = (1.0 / (1.0 + lambda_v + lambda_l)) * (1.0 + lambda_v)

    mean_weight = float(np.sum(weight * pdf * elevation_weights) * azimuth_step)
    mean_weight_old = float(np.sum(old_weight * pdf * elevation_weights) * azimuth_step)
    return mean_weight, mean_weight_old


def main():
    parser = argparse.ArgumentParser(description="RTGI の鏡面重みと DFG LUT の Ess を比較する。")
    parser.add_argument("--check", action="store_true", help="判定が失敗した場合に終了コード 1 を返す")
    args = parser.parse_args()

    all_passed = True
    max_abs_ratio_error = 0.0
    max_abs_ratio_engine_error = 0.0
    case_count = 0
    for roughness in (0.5, 0.8, 1.0):
        roughness_lut = min(max(roughness, 0.5 / LUT_SIZE),
                            (LUT_SIZE - 0.5) / LUT_SIZE)
        for n_dot_v in (0.1, 0.3, 0.5, 0.9):
            resolution = 192 if roughness <= 0.5 and n_dot_v <= 0.3 else 96
            low_engine = integrate_weights(n_dot_v, roughness, resolution)
            high_engine = integrate_weights(n_dot_v, roughness, 2 * resolution)
            if roughness_lut == roughness:
                low_lut, high_lut = low_engine, high_engine
            else:
                low_lut = integrate_weights(n_dot_v, roughness_lut, resolution)
                high_lut = integrate_weights(n_dot_v, roughness_lut, 2 * resolution)

            # 判定用の新重み・エンジンの新重み・従来の重みのすべてで収束を確認する。
            convergence_delta = max(
                abs(high_lut[0] - low_lut[0]),
                abs(high_engine[0] - low_engine[0]),
                abs(high_engine[1] - low_engine[1]),
            )
            mean_weight = high_lut[0]
            mean_weight_engine, mean_weight_old = high_engine
            ess = dfg_lut_ess(n_dot_v, roughness)
            ratio = mean_weight / ess
            ratio_engine = mean_weight_engine / ess
            ratio_old = mean_weight_old / ess
            ratio_error = abs(ratio - 1.0)
            passed = (all(math.isfinite(value) for value in
                          (ratio, ratio_engine, ratio_old, convergence_delta))
                      and ratio_error <= RATIO_TOLERANCE
                      and convergence_delta <= CONVERGENCE_TOLERANCE)
            all_passed = all_passed and passed
            max_abs_ratio_error = max(max_abs_ratio_error, ratio_error)
            max_abs_ratio_engine_error = max(max_abs_ratio_engine_error,
                                             abs(ratio_engine - 1.0))
            case_count += 1
            print(
                f"SPEC_WEIGHT_QUADRATURE roughness={roughness:.2f} NdotV={n_dot_v:.2f} "
                f"roughness_lut={roughness_lut:.9f} "
                f"mean_weight={mean_weight:.8f} Ess_LUT={ess:.8f} ratio={ratio:.8f} "
                f"mean_weight_engine={mean_weight_engine:.8f} ratio_engine={ratio_engine:.8f} "
                f"mean_weight_old={mean_weight_old:.8f} ratio_old={ratio_old:.8f} "
                f"convergence_delta={convergence_delta:.8g} result={'pass' if passed else 'fail'}"
            )

    print(
        f"SPEC_WEIGHT_QUADRATURE_SUMMARY result={'pass' if all_passed else 'fail'} "
        f"max_abs_ratio_error={max_abs_ratio_error:.8f} "
        f"max_abs_ratio_engine_error={max_abs_ratio_engine_error:.8f} "
        f"tolerance={RATIO_TOLERANCE} cases={case_count}"
    )
    return 1 if args.check and not all_passed else 0


if __name__ == "__main__":
    raise SystemExit(main())
