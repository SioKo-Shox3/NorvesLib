# エンジンの表示変換（露出・トーンマップ・グレーディング・ビネット・sRGB の出力）を再現する。
# 荒野の正解の絵の比較（SCN-WLR-*、計画は Docs/Plans/WastelandRebuild.md）で、エンジンの HDR と Cycles の EXR を
# 表示の値へ写す唯一の経路にする（Blender の AgX・Filmic は使わない）。
#
# 経路（Library/Core/Private/Rendering/SceneView.cpp の PostProcessStack と同じ順。値と式はシェーダーと同じ）:
#   1. 露出: 放射輝度 [cd/m²] × プリエクスポージャ。エンジンはライティングでシーンの色へ掛け、シーンの色は RGBA16F
#      （expose() は半精度へ丸める）。
#   2. ToneMappingPass（Assets/Shaders/tonemapping.frag）: トーンマップ → カラーフィルター・明度 → グレーディング
#      （式 0: View の設定のコントラスト・彩度・色温度、式 1: カメラの差し替えの知覚的な S 字のコントラスト・彩度・
#      輝度を保つ色温度）→ トーンマップの中のビネット。出力は RGBA16F。
#      ACES 2.0 SDR LUT（演算子 4）は表示変換そのものなので、グレーディングを掛けない。LUT は Scripts/BakeAcesOutputLut.py が
#      焼いた Assets/ColorManagement/Aces20SdrRec709.lut3d を、同じ log2 の shaper の座標で三線形補間する
#      （GPU の補間の重みは固定小数で、副テクセルの精度 8 bit に丸める）。
#   3. VignettePass（Assets/Shaders/vignette.frag）: ビネット。出力は RGBA16F。
#   4. 提示: sRGB の符号化と 8 bit への丸め（Assets/Shaders/blit.frag の符号化か、ハードウェアの sRGB の形式）。
#   ブルーム（レンズダートを含む）と FXAA はこの再現に入れない（空間の処理で、照合の撮影は Game の --display-check で外す）。
#   見た目の 3D LUT・フィルムグレイン・色収差は再現していない（掛かっていれば UnsupportedDisplay を投げる）。
#
# 値はシェーダーと同じく 32 bit の浮動小数で計算する。
#
# 使い方（ほかのスクリプトから import するほか、1 枚の HDR を PNG にできる）:
#   python Scripts/Terrain/DisplayTransform.py --hdr <視点>.hdr.pfm --out <出力>.png
#       <視点>.hdr.json（撮影が書く。プリエクスポージャと、ポストのパスが実際に使った値）の表示変換を掛ける。
import argparse
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ACES_LUT_PATH = os.path.join(REPO_ROOT, "Assets", "ColorManagement", "Aces20SdrRec709.lut3d")

# Rec.709 の輝度の重み（tonemapping.frag の ApplySaturation・ApplyWhiteBalance と同じ）
LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)

# tonemapping.frag の operatorType
OPERATOR_REINHARD = 0
OPERATOR_ACES_FILMIC = 1
OPERATOR_UNCHARTED2 = 2
OPERATOR_EXPOSURE = 3
OPERATOR_ACES20_LUT = 4
OPERATOR_NEUTRAL_LINEAR = 5

# ACES 2.0 LUT の shaper（tonemapping.frag・Scripts/BakeAcesOutputLut.py と同じ固定値）
ACES_LUT_MAGIC = b"NLUT3D01"
ACES_LUT_SHAPER_OFFSET = np.float32(0.00390625)
ACES_LUT_SHAPER_MAX = np.float32(256.0)
ACES_LUT_SHAPER_SPAN = np.float32(np.log2(256.0 / 0.00390625 + 1.0))
# GPU の三線形補間の重みの段数（VkPhysicalDeviceLimits::subTexelPrecisionBits = 8）
LUT_WEIGHT_STEPS = 256

# 表示変換の照合の色の階段（ToneMappingPass::SetDisplayRampInput と同じ並び）:
# 出力を横 12 × 縦 7 の区画に分け、列 i の明るさを 2^(i − 8)、行を R・G・B・C・M・Y・灰（各成分は 0 か明るさ）にする。
RAMP_COLUMNS = 12
RAMP_ROWS = 7
RAMP_EXPONENT_OFFSET = 8
RAMP_ROW_COLORS = np.array([
    [1, 0, 0],  # R
    [0, 1, 0],  # G
    [0, 0, 1],  # B
    [0, 1, 1],  # C
    [1, 0, 1],  # M
    [1, 1, 0],  # Y
    [1, 1, 1],  # 灰
], dtype=np.float32)
RAMP_ROW_NAMES = ["R", "G", "B", "C", "M", "Y", "W"]


class UnsupportedDisplay(ValueError):
    """撮影の表示変換に、この再現が持たない処理が掛かっている"""


def f32(value):
    return np.asarray(value, dtype=np.float32)


def to_half(color):
    """RGBA16F の描画先へ書いた値（半精度へ丸めて 32 bit へ戻す）"""
    return np.asarray(color, dtype=np.float32).astype(np.float16).astype(np.float32)


def read_pfm(path):
    """RGB の PFM を (高さ, 幅, 3) の float32 で読む（画像の上を 0 行目にする）"""
    with open(path, "rb") as f:
        data = f.read()
    # 見出しは 3 つの空白区切りの語（"PF"、幅 高さ、スケール）の後に 1 つの区切り文字
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
    if tokens[0] != "PF":
        raise ValueError(f"RGB の PFM でない（{tokens[0]}）")
    width, height, scale = int(tokens[1]), int(tokens[2]), float(tokens[3])
    dtype = "<f4" if scale < 0 else ">f4"
    count = width * height * 3
    if len(data) - pos != count * 4:
        raise ValueError(f"PFM の画素の量が見出しと合わない（{len(data) - pos} バイト、見出しから {count * 4} バイト）")
    pixels = np.frombuffer(data, dtype=dtype, count=count, offset=pos)
    # PFM は下の行から並ぶので、上下を返して画像の上を 0 行目にする
    return pixels.reshape(height, width, 3)[::-1].astype(np.float32)


def load_aces_lut(path=ACES_LUT_PATH):
    """ACES 2.0 SDR の 3D LUT を [B][G][R][RGB] の float32 で読む（ToneMappingPass の見出しの検査と同じ条件）"""
    with open(path, "rb") as f:
        data = f.read()
    magic = data[:8]
    size, channels, fmt, _reserved, offset, maximum = struct.unpack("<IIIIff", data[8:32])
    if magic != ACES_LUT_MAGIC or channels != 4 or fmt != 1 or size < 2:
        raise ValueError(f"ACES の LUT の見出しが想定と違う: {path}")
    if np.float32(offset) != ACES_LUT_SHAPER_OFFSET or np.float32(maximum) != ACES_LUT_SHAPER_MAX:
        raise ValueError(f"ACES の LUT の shaper が想定と違う（offset={offset} max={maximum}）")
    texels = np.frombuffer(data, dtype="<f2", offset=32, count=size * size * size * 4)
    return texels.reshape(size, size, size, 4)[..., :3].astype(np.float32)


# ========================================
# トーンマップ（tonemapping.frag と同じ式）
# ========================================

def tonemap_reinhard(color):
    return color / (f32(1.0) + color)


def tonemap_aces_filmic(color):
    a, b, c, d, e = (f32(v) for v in (2.51, 0.03, 2.43, 0.59, 0.14))
    return np.clip((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0).astype(np.float32)


def _uncharted2_helper(x):
    a, b, c, d, e, f = (f32(v) for v in (0.15, 0.50, 0.10, 0.20, 0.02, 0.30))
    return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f


def tonemap_uncharted2(color):
    white = f32(11.2)
    return _uncharted2_helper(color) * (f32(1.0) / _uncharted2_helper(white))


def tonemap_exposure(color):
    return f32(1.0) - np.exp(-color)


def tonemap_neutral_linear(color):
    start = f32(0.8)
    desaturation = f32(0.15)
    color = np.maximum(color, f32(0.0))
    peak = color.max(axis=-1, keepdims=True)
    d = f32(1.0) - start
    safe_peak = np.maximum(peak, f32(1e-30))
    new_peak = f32(1.0) - d * d / (peak + d - start)
    compressed = color * (new_peak / safe_peak)
    g = f32(1.0) - f32(1.0) / (desaturation * (peak - new_peak) + f32(1.0))
    compressed = compressed + (new_peak - compressed) * g
    return np.where(peak < start, color, compressed).astype(np.float32)


def tonemap_aces20_lut(color, lut, weight_steps=LUT_WEIGHT_STEPS):
    """log2 の shaper の座標で LUT を三線形補間する（格子点 0 と N−1 がテクセル中心。半テクセル内側の座標と同じ）"""
    size = lut.shape[0]
    clamped = np.clip(color, f32(0.0), ACES_LUT_SHAPER_MAX)
    shaped = np.clip(np.log2(clamped / ACES_LUT_SHAPER_OFFSET + f32(1.0)) / ACES_LUT_SHAPER_SPAN, 0.0, 1.0)
    position = (shaped * f32(size - 1)).reshape(-1, 3).astype(np.float64)
    base = np.minimum(np.floor(position).astype(np.int64), size - 2)
    fraction = position - base
    if weight_steps:
        fraction = np.round(fraction * weight_steps) / weight_steps
    result = np.zeros_like(position)
    for corner in range(8):
        offset = np.array([(corner >> 0) & 1, (corner >> 1) & 1, (corner >> 2) & 1])
        index = base + offset
        weight = np.prod(np.where(offset == 1, fraction, 1.0 - fraction), axis=1)
        result += weight[:, None] * lut[index[:, 2], index[:, 1], index[:, 0]]
    return result.reshape(color.shape).astype(np.float32)


# ========================================
# グレーディング（tonemapping.frag と同じ式）
# ========================================

def apply_temperature(color, temperature):
    out = color.copy()
    out[..., 0] += temperature * f32(0.1)
    out[..., 2] -= temperature * f32(0.1)
    return np.clip(out, 0.0, 1.0)


def apply_contrast(color, contrast):
    return np.clip((color - f32(0.5)) * contrast + f32(0.5), 0.0, 1.0)


def apply_saturation(color, saturation):
    luma = (color * LUMA).sum(axis=-1, keepdims=True, dtype=np.float32)
    return np.clip(luma + (color - luma) * saturation, 0.0, 1.0).astype(np.float32)


def apply_contrast_perceptual(color, contrast, pivot_linear):
    gamma = f32(1.0 / 2.2)
    pivot = np.clip(np.power(np.clip(f32(pivot_linear), 0.0, 1.0), gamma), 0.01, 0.99).astype(np.float32)
    x = np.power(np.clip(color, 0.0, 1.0), gamma)
    lower = pivot * np.power(x / pivot, contrast)
    upper = f32(1.0) - (f32(1.0) - pivot) * np.power(np.maximum(f32(1.0) - x, 0.0) / (f32(1.0) - pivot), contrast)
    y = np.where(x >= pivot, upper, lower)
    return np.power(np.clip(y, 0.0, 1.0), f32(2.2)).astype(np.float32)


def apply_white_balance(color, temperature):
    gain = np.array([1.0 + temperature * 0.1, 1.0, 1.0 - temperature * 0.1], dtype=np.float32)
    gain = gain / (gain * LUMA).sum(dtype=np.float32)
    return np.clip(color * gain, 0.0, 1.0).astype(np.float32)


def vignette_factor(height, width, intensity, radius, softness):
    """画素の中心の UV での ComputeVignette（tonemapping.frag・vignette.frag と同じ式）"""
    v = ((np.arange(height, dtype=np.float32) + f32(0.5)) / f32(height) - f32(0.5))
    u = ((np.arange(width, dtype=np.float32) + f32(0.5)) / f32(width) - f32(0.5))
    dist = np.sqrt(u[None, :] ** 2 + v[:, None] ** 2).astype(np.float32)
    edge0, edge1 = f32(radius) - f32(softness), f32(radius)
    t = np.clip((dist - edge0) / (edge1 - edge0), 0.0, 1.0)
    s = t * t * (f32(3.0) - f32(2.0) * t)
    return (f32(1.0) + ((f32(1.0) - s) - f32(1.0)) * f32(intensity))[..., None].astype(np.float32)


def encode_srgb(linear):
    linear = np.clip(linear, 0.0, 1.0).astype(np.float32)
    high = f32(1.055) * np.power(linear, f32(1.0 / 2.4)) - f32(0.055)
    return np.where(linear <= f32(0.0031308), linear * f32(12.92), high).astype(np.float32)


# ========================================
# 撮影の記録から表示変換の値を作る
# ========================================

def params_from_capture(meta):
    """撮影の <視点>.hdr.json から表示変換の値（dict）を作る。

    ポストのパスが実際に使った値（"post"）があればそれを使う。無い古い撮影は、カメラの差し替え（NeutralLinear と
    グレーディングの式 1）の値だけから作る（View の既定の表示変換では描かれていない荒野の撮影に限る）。
    """
    post = meta.get("post")
    display = meta.get("display", {})
    if post and post.get("tone_map_recorded"):
        params = {
            "operator": int(post["tone_map_operator"]),
            "bypass": bool(post.get("tone_map_bypass", False)),
            "grading_mode": int(post["grading_mode"]),
            "color_filter": [float(v) for v in post["color_filter"]],
            "contrast": float(post["contrast"]),
            "contrast_pivot": float(post["contrast_pivot"]),
            "saturation": float(post["saturation"]),
            "brightness": float(post["brightness"]),
            "temperature": float(post["temperature"]),
            "look_lut_intensity": float(post.get("look_lut_intensity", 0.0)),
            "tone_map_vignette": [float(v) for v in post["tone_map_vignette"]],
            "film_grain_strength": float(post.get("film_grain_strength", 0.0)),
            "vignette_pass": bool(post.get("vignette_recorded", False)),
            "vignette_enabled": bool(post.get("vignette_enabled", False)),
            "vignette": [float(v) for v in post.get("vignette", [0.0, 0.8, 0.5])],
            "chromatic_aberration_pixels": float(post.get("chromatic_aberration_pixels", 0.0)),
        }
    elif display.get("neutral_linear") and display.get("grading_override"):
        params = {
            "operator": OPERATOR_NEUTRAL_LINEAR,
            "bypass": False,
            "grading_mode": 1,
            "color_filter": [1.0, 1.0, 1.0, 1.0],
            "contrast": float(display["contrast"]),
            "contrast_pivot": float(display["contrast_pivot"]),
            "saturation": float(display["saturation"]),
            "brightness": 0.0,
            "temperature": float(display["temperature"]),
            "look_lut_intensity": 1.0 if display.get("look_lut") else 0.0,
            "tone_map_vignette": [0.0, 0.8, 0.5],
            "film_grain_strength": 0.0,
            "vignette_pass": True,
            "vignette_enabled": True,
            "vignette": [float(display["vignette_intensity"]), float(display["vignette_radius"]),
                         float(display["vignette_softness"])],
            "chromatic_aberration_pixels": float(display.get("chromatic_aberration_pixels", 0.0)),
        }
    else:
        raise UnsupportedDisplay("撮影にポストのパスの記録が無く、カメラの差し替えも無い（表示変換の値を決められない）")
    unsupported = [name for name, active in (
        ("見た目の 3D LUT", params["look_lut_intensity"] > 0.0 and params["operator"] != OPERATOR_ACES20_LUT),
        ("フィルムグレイン", params["film_grain_strength"] > 0.0),
        ("色収差", params["vignette_pass"] and params["chromatic_aberration_pixels"] > 0.0)) if active]
    if unsupported:
        raise UnsupportedDisplay(f"表示変換に再現していない処理（{'・'.join(unsupported)}）が掛かっている")
    return params


def expose(radiance, pre_exposure):
    """放射輝度 [cd/m²] にプリエクスポージャを掛けたシーンの色（RGBA16F へ丸める）"""
    return to_half(np.asarray(radiance, dtype=np.float32) * np.float32(pre_exposure))


def tone_mapping_pass(exposed, params, lut=None):
    """ToneMappingPass の出力（display-linear、RGBA16F へ丸める）"""
    exposed = np.asarray(exposed, dtype=np.float32)
    if params["bypass"]:
        return to_half(exposed)
    operator = params["operator"]
    if operator == OPERATOR_REINHARD:
        mapped = tonemap_reinhard(exposed)
    elif operator == OPERATOR_ACES_FILMIC:
        mapped = tonemap_aces_filmic(exposed)
    elif operator == OPERATOR_UNCHARTED2:
        mapped = tonemap_uncharted2(exposed)
    elif operator == OPERATOR_ACES20_LUT:
        mapped = tonemap_aces20_lut(exposed, lut if lut is not None else load_aces_lut())
    elif operator == OPERATOR_NEUTRAL_LINEAR:
        mapped = tonemap_neutral_linear(exposed)
    else:
        mapped = tonemap_exposure(exposed)
    result = mapped.astype(np.float32)
    if operator != OPERATOR_ACES20_LUT:
        color_filter = np.array(params["color_filter"], dtype=np.float32)
        result = result * (color_filter[:3] * color_filter[3])
        result = result + f32(params["brightness"])
        if params["grading_mode"] == 1:
            result = apply_contrast_perceptual(result, f32(params["contrast"]), params["contrast_pivot"])
            result = apply_saturation(result, f32(params["saturation"]))
            result = apply_white_balance(result, params["temperature"])
        else:
            result = apply_contrast(result, f32(params["contrast"]))
            result = apply_saturation(result, f32(params["saturation"]))
            result = apply_temperature(result, f32(params["temperature"]))
    height, width = exposed.shape[:2]
    intensity, radius, softness = params["tone_map_vignette"]
    result = result * vignette_factor(height, width, intensity, radius, softness)
    return to_half(result)


def vignette_pass(color, params):
    """VignettePass の出力（RGBA16F へ丸める。パスが走らない・無効なら入力のまま）"""
    if not params["vignette_pass"] or not params["vignette_enabled"]:
        return color
    height, width = color.shape[:2]
    intensity, radius, softness = params["vignette"]
    return to_half(color * vignette_factor(height, width, intensity, radius, softness))


def display_transform(exposed, params, lut=None):
    """露出後のシーンの色（H, W, 3）を表示の値へ写す。(8 bit の画像 uint8, sRGB の符号化値×255 の実数)"""
    display_linear = vignette_pass(tone_mapping_pass(exposed, params, lut), params)
    encoded = encode_srgb(display_linear) * f32(255.0)
    return np.clip(np.rint(encoded), 0, 255).astype(np.uint8), encoded


def display_ramp(width, height):
    """照合の色の階段（露出後の値）と、画素ごとの区画の列・行"""
    columns = (np.arange(width, dtype=np.int64) * RAMP_COLUMNS) // width
    rows = (np.arange(height, dtype=np.int64) * RAMP_ROWS) // height
    brightness = np.exp2(columns.astype(np.float32) - RAMP_EXPONENT_OFFSET).astype(np.float32)
    ramp = RAMP_ROW_COLORS[rows][:, None, :] * brightness[None, :, None]
    column_map = np.broadcast_to(columns[None, :], (height, width))
    row_map = np.broadcast_to(rows[:, None], (height, width))
    return ramp.astype(np.float32), column_map, row_map


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="エンジンの表示変換を HDR に掛けて PNG を書く")
    parser.add_argument("--hdr", required=True, help="撮影の <視点>.hdr.pfm（横に <視点>.hdr.json が要る）")
    parser.add_argument("--out", required=True, help="書き出す PNG")
    args = parser.parse_args()
    meta_path = args.hdr[:-len(".pfm")] + ".json" if args.hdr.endswith(".pfm") else args.hdr + ".json"
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    params = params_from_capture(meta)
    exposed = expose(read_pfm(args.hdr), float(meta["pre_exposure"]))
    image, _ = display_transform(exposed, params)
    Image.fromarray(image, "RGB").save(args.out)
    print(f"DISPLAY_TRANSFORM out={args.out} operator={params['operator']} grading_mode={params['grading_mode']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
