# ACES 2.0 SDR の出力変換を 3D LUT へ焼き込み、HDR 試験チャートと OCIO 厳密変換の基準画像を作る。
#
# 変換は OCIO の組み込み ACES 2.0 studio config で、scene-linear Rec.709 (D65) から
# display「sRGB - Display」・view「ACES 2.0 - SDR 100 nits (Rec.709)」へ向かう。
# LUT の値は sRGB の符号化を外した display-linear で、提示側の既存の sRGB 符号化をそのまま使う。
#
# 使い方:
#   python Scripts/BakeAcesOutputLut.py           # LUT・チャート・基準画像を書き出す
#   python Scripts/BakeAcesOutputLut.py --verify  # 再生成の byte 一致と LUT 補間の誤差を検査する
import argparse
import hashlib
import struct
import sys
import zlib
from pathlib import Path

import numpy as np
import PyOpenColorIO as ocio

# 変換の固定値。変えたら LUT と基準画像を作り直し、記録文書も更新する。
OCIO_MINIMUM_VERSION = (2, 4)
OCIO_CONFIG_NAME = "studio-config-v4.0.0_aces-v2.0_ocio-v2.5"
SOURCE_COLOR_SPACE = "Linear Rec.709 (sRGB)"
DISPLAY_NAME = "sRGB - Display"
VIEW_NAME = "ACES 2.0 - SDR 100 nits (Rec.709)"

# log2 shaper: 各成分を u = log2(x / OFFSET + 1) / log2(MAX / OFFSET + 1) で [0,1] へ写す。
# 0 は u=0 に一致し、OFFSET より十分明るい値では log2 の等間隔になる。範囲 [0, MAX] の外は端へ寄せる。
# OFFSET=2^-8・MAX=2^8 は、チャートでの補間誤差を範囲の候補ごとに測って選んだ（記録文書に表を残す）。
LUT_SIZE = 65
SHAPER_OFFSET_LOG2 = -8.0
SHAPER_MAX_LOG2 = 8.0
SHAPER_OFFSET = 2.0 ** SHAPER_OFFSET_LOG2
SHAPER_MAX = 2.0 ** SHAPER_MAX_LOG2
SHAPER_SPAN = float(np.log2(SHAPER_MAX / SHAPER_OFFSET + 1.0))

# 参考の頑健性検査（合否に使わない）: 固定の種で作った無作為な色の誤差を表示する。
ROBUSTNESS_SEED = 20260926
ROBUSTNESS_SAMPLES = 200000

# 試験チャートは 16×16 画素の区画を横 16 個並べた行で作る。
PATCH_SIZE = 16
PATCHES_PER_ROW = 16
CHART_WIDTH = PATCH_SIZE * PATCHES_PER_ROW

# 合格の閾値: sRGB 符号化後の差（0〜1 の尺度）。
MAX_ENCODED_DIFFERENCE = 2.0 / 255.0

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
LUT_PATH = REPOSITORY_ROOT / "Assets/ColorManagement/Aces20SdrRec709.lut3d"
BASELINE_DIRECTORY = REPOSITORY_ROOT / "Test/Core/Rendering/Baselines/RenderingValidation"
CHART_PATH = BASELINE_DIRECTORY / "R8AcesChart.rgba16f"
REFERENCE_PATH = BASELINE_DIRECTORY / "R8AcesReference.rgba32f"
REFERENCE_PREVIEW_PATH = BASELINE_DIRECTORY / "R8AcesReference.png"

# ファイルの見出し。すべて little-endian。
# LUT: magic(8) size(u32) channels(u32) format(u32: 1=RGBA16F) reserved(u32) shaperOffset(f32) shaperMax(f32)、
#      続けて size^3 個の RGBA half を R が最も速く変わる順（x=R, y=G, z=B）で並べる。
# 画像: magic(8) width(u32) height(u32)、続けて上の行から RGBA を並べる。
LUT_MAGIC = b"NLUT3D01"
LUT_FORMAT_RGBA16F = 1
CHART_MAGIC = b"NRGBA16F"
REFERENCE_MAGIC = b"NRGBA32F"

# ColorChecker 系の色票（linear sRGB の近似値）。肌・空・葉などの記憶色の代表として使う固定値。
MEMORY_COLOR_PATCHES = (
    (0.173, 0.084, 0.058),  # dark skin
    (0.558, 0.300, 0.230),  # light skin
    (0.114, 0.198, 0.335),  # blue sky
    (0.106, 0.151, 0.053),  # foliage
    (0.235, 0.217, 0.434),  # blue flower
    (0.132, 0.515, 0.408),  # bluish green
    (0.745, 0.203, 0.024),  # orange
    (0.061, 0.103, 0.389),  # purplish blue
    (0.559, 0.082, 0.117),  # moderate red
    (0.109, 0.042, 0.144),  # purple
    (0.334, 0.504, 0.046),  # yellow green
    (0.787, 0.358, 0.017),  # orange yellow
    (0.018, 0.049, 0.285),  # blue
    (0.070, 0.296, 0.069),  # green
    (0.444, 0.030, 0.040),  # red
    (0.846, 0.577, 0.008),  # yellow
)

# 原色・補色と中間の色相（最大成分を 1 とした linear sRGB）。
HUE_PATCHES = (
    (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0),
    (0.0, 1.0, 1.0), (1.0, 0.0, 1.0), (1.0, 1.0, 0.0),
    (1.0, 0.5, 0.5), (0.5, 1.0, 0.5), (0.5, 0.5, 1.0),
    (0.5, 1.0, 1.0), (1.0, 0.5, 1.0), (1.0, 1.0, 0.5),
    (1.0, 0.5, 0.0), (0.5, 1.0, 0.0), (0.0, 0.5, 1.0), (0.5, 0.0, 1.0),
)


def require_ocio():
    version = tuple(int(part) for part in ocio.__version__.split(".")[:2])
    if version < OCIO_MINIMUM_VERSION:
        raise SystemExit(f"OCIO {ocio.__version__} is older than required {OCIO_MINIMUM_VERSION}")
    names = list(ocio.BuiltinConfigRegistry())
    if OCIO_CONFIG_NAME not in names:
        raise SystemExit(f"builtin config {OCIO_CONFIG_NAME} is not available: {names}")
    config = ocio.Config.CreateFromBuiltinConfig(OCIO_CONFIG_NAME)
    if VIEW_NAME not in list(config.getViews(DISPLAY_NAME)):
        raise SystemExit(f"view '{VIEW_NAME}' is not available for display '{DISPLAY_NAME}'")
    return config


def make_exact_processor(config):
    transform = ocio.DisplayViewTransform(src=SOURCE_COLOR_SPACE, display=DISPLAY_NAME, view=VIEW_NAME)
    return config.getProcessor(transform).getOptimizedCPUProcessor(ocio.OPTIMIZATION_LOSSLESS)


def srgb_decode(encoded):
    # 「sRGB - Display」の符号化（区分的 sRGB、負は鏡映）を外して display-linear にする。
    magnitude = np.abs(encoded)
    linear = np.where(magnitude <= 0.04045, magnitude / 12.92, ((magnitude + 0.055) / 1.055) ** 2.4)
    return np.sign(encoded) * linear


def srgb_encode_clamped(linear):
    # 提示の sRGB 形式と同じく [0,1] へ飽和させてから区分的 sRGB で符号化する。
    clamped = np.clip(linear, 0.0, 1.0)
    return np.where(clamped <= 0.0031308, clamped * 12.92, 1.055 * clamped ** (1.0 / 2.4) - 0.055)


def apply_exact(processor, rgb):
    # scene-linear の (N,3) を OCIO で厳密に変換し、display-linear の float32 で返す。
    pixels = np.ascontiguousarray(rgb, dtype=np.float32).copy()
    processor.applyRGB(pixels)
    return srgb_decode(pixels.astype(np.float64)).astype(np.float32)


def shaper_coordinate(scene_linear):
    return np.clip(np.log2(np.maximum(scene_linear, 0.0) / SHAPER_OFFSET + 1.0) / SHAPER_SPAN, 0.0, 1.0)


def bake_lut(processor):
    # 格子点 i の入力は shaper の逆 OFFSET * (2^(SPAN * i/(N-1)) - 1)。index 0 は 0、最後は MAX。
    steps = np.arange(LUT_SIZE, dtype=np.float64) / (LUT_SIZE - 1)
    axis = SHAPER_OFFSET * (np.exp2(SHAPER_SPAN * steps) - 1.0)
    blue, green, red = np.meshgrid(axis, axis, axis, indexing="ij")
    lattice = np.stack([red, green, blue], axis=-1).reshape(-1, 3)
    display_linear = apply_exact(processor, lattice)
    rgba = np.ones((display_linear.shape[0], 4), dtype=np.float16)
    rgba[:, :3] = display_linear.astype(np.float16)
    header = LUT_MAGIC + struct.pack("<IIIIff", LUT_SIZE, 4, LUT_FORMAT_RGBA16F, 0, SHAPER_OFFSET, SHAPER_MAX)
    return header + rgba.astype("<f2").tobytes(), rgba[:, :3].astype(np.float32).reshape(LUT_SIZE, LUT_SIZE, LUT_SIZE, 3)


def sample_lut_trilinear(lut, scene_linear, weight_steps=None):
    # lut は [B][G][R] の順。shaper の座標から格子の index 空間で三線形補間する。
    # weight_steps を与えると補間の重みをその段数へ丸める（GPU の固定小数の重みの近似）。
    position = shaper_coordinate(scene_linear.astype(np.float64)) * (LUT_SIZE - 1)
    base = np.minimum(np.floor(position).astype(np.int64), LUT_SIZE - 2)
    fraction = position - base
    if weight_steps is not None:
        fraction = np.round(fraction * weight_steps) / weight_steps
    result = np.zeros_like(position)
    for corner in range(8):
        offset = np.array([(corner >> 0) & 1, (corner >> 1) & 1, (corner >> 2) & 1])
        index = base + offset
        weight = np.prod(np.where(offset == 1, fraction, 1.0 - fraction), axis=1)
        result += weight[:, None] * lut[index[:, 2], index[:, 1], index[:, 0]]
    return result


def hsv_row(hue_count, value):
    hue = (np.arange(hue_count, dtype=np.float64) + 0.5) / hue_count * 6.0
    x = 1.0 - np.abs(np.mod(hue, 2.0) - 1.0)
    sector = np.floor(hue).astype(np.int64)
    # 成分は (最大=1, 中間=x, 最小=0)。区間ごとに R・G・B へ割り当てる。
    table = np.array([[0, 1, 2], [1, 0, 2], [2, 0, 1], [2, 1, 0], [1, 2, 0], [0, 2, 1]])
    components = np.stack([np.ones_like(x), x, np.zeros_like(x)], axis=-1)
    selected = table[sector]
    rgb = np.take_along_axis(components, selected, axis=1)
    return rgb * value


def build_chart():
    # 行ごとの色を (16 区画) または (横 256 画素の連続変化) で作り、16 画素の高さへ伸ばす。
    rows = []

    def patch_row(colors):
        colors = np.asarray(colors, dtype=np.float64)
        return np.repeat(colors, PATCH_SIZE, axis=0)

    # 露出段のグレー: 18% を中心に 2^-8〜2^7、および 0 と 2^-14〜2^7 の 1.5 段刻み。
    rows.append(patch_row([[0.18 * 2.0 ** e] * 3 for e in range(-8, 8)]))
    rows.append(patch_row([[0.0] * 3] + [[2.0 ** (-14.0 + 1.5 * i)] * 3 for i in range(15)]))
    # 原色・補色・中間色相を 5 つの露出で。
    for exposure in (-3, 0, 2, 4, 6):
        level = 0.18 * 2.0 ** exposure
        rows.append(patch_row([[c * level for c in color] for color in HUE_PATCHES]))
    # 肌・空などの記憶色を 3 つの露出で。
    for scale in (0.25, 1.0, 4.0):
        rows.append(patch_row([[c * scale for c in color] for color in MEMORY_COLOR_PATCHES]))
    # 明るい空の色（青みの強い高輝度）。
    rows.append(patch_row([[0.5 * s, 0.8 * s, 1.6 * s] for s in (2.0 ** (i * 0.5 - 2.0) for i in range(16))]))
    # 連続変化: log グレーの傾斜と、彩度 1 の色相の一巡を 3 つの明るさで。
    ramp = np.exp2(-14.0 + 23.0 * (np.arange(CHART_WIDTH, dtype=np.float64) + 0.5) / CHART_WIDTH)
    rows.append(np.stack([ramp] * 3, axis=-1))
    for value in (0.18, 1.0, 8.0):
        rows.append(hsv_row(CHART_WIDTH, value))

    image = np.concatenate([np.repeat(row[None, :, :], PATCH_SIZE, axis=0) for row in rows], axis=0)
    # SceneColor は RGBA16F なので、基準は half へ丸めた値から求める。
    return image.astype(np.float16)


def encode_image(magic, pixels_rgb, dtype):
    height, width, _ = pixels_rgb.shape
    rgba = np.ones((height, width, 4), dtype=dtype)
    rgba[:, :, :3] = pixels_rgb.astype(dtype)
    little = "<f2" if dtype == np.float16 else "<f4"
    return magic + struct.pack("<II", width, height) + rgba.astype(little).tobytes()


def encode_png_rgb8(pixels_rgb8):
    height, width, _ = pixels_rgb8.shape
    raw = b"".join(b"\x00" + pixels_rgb8[y].tobytes() for y in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def generate():
    config = require_ocio()
    processor = make_exact_processor(config)
    lut_bytes, lut = bake_lut(processor)
    chart = build_chart()
    height, width, _ = chart.shape
    chart_linear = chart.astype(np.float32).reshape(-1, 3)
    reference = apply_exact(processor, chart_linear).reshape(height, width, 3)
    preview = np.round(srgb_encode_clamped(reference.astype(np.float64)) * 255.0).astype(np.uint8)
    outputs = {
        LUT_PATH: lut_bytes,
        CHART_PATH: encode_image(CHART_MAGIC, chart, np.float16),
        REFERENCE_PATH: encode_image(REFERENCE_MAGIC, reference, np.float32),
        REFERENCE_PREVIEW_PATH: encode_png_rgb8(preview),
    }
    return outputs, lut, chart_linear, reference.reshape(-1, 3)


def report_robustness(lut):
    # 露出 2^-10〜2^7・最大成分で正規化した一様な色の無作為標本での差。合否には使わない。
    config = require_ocio()
    processor = make_exact_processor(config)
    generator = np.random.default_rng(ROBUSTNESS_SEED)
    level = np.exp2(generator.uniform(-10.0, 7.0, ROBUSTNESS_SAMPLES))
    color = generator.uniform(0.0, 1.0, (ROBUSTNESS_SAMPLES, 3))
    color /= color.max(axis=1, keepdims=True)
    samples = (color * level[:, None]).astype(np.float16).astype(np.float32)
    exact = apply_exact(processor, samples).astype(np.float64)
    difference = np.abs(srgb_encode_clamped(sample_lut_trilinear(lut, samples)) - srgb_encode_clamped(exact)).max(axis=1)
    print(f"info random_samples={ROBUSTNESS_SAMPLES} seed={ROBUSTNESS_SEED} "
          f"max={difference.max() * 255.0:.3f}/255 p99.9={np.quantile(difference, 0.999) * 255.0:.3f}/255 "
          f"over_threshold={np.count_nonzero(difference > MAX_ENCODED_DIFFERENCE)}")


def describe(outputs):
    print(f"ocio_version={ocio.__version__}")
    print(f"config={OCIO_CONFIG_NAME}")
    print(f"source={SOURCE_COLOR_SPACE} display={DISPLAY_NAME} view={VIEW_NAME}")
    print(f"lut_size={LUT_SIZE} shaper=log2(x/offset+1)/log2(max/offset+1) "
          f"offset=2^{SHAPER_OFFSET_LOG2:g} max=2^{SHAPER_MAX_LOG2:g}")
    for path, data in outputs.items():
        relative = path.relative_to(REPOSITORY_ROOT).as_posix()
        print(f"sha256 {hashlib.sha256(data).hexdigest().upper()} bytes={len(data)} {relative}")


def verify():
    outputs, lut, chart_linear, reference = generate()
    describe(outputs)
    failed = False
    for path, data in outputs.items():
        relative = path.relative_to(REPOSITORY_ROOT).as_posix()
        if not path.is_file():
            print(f"FAIL missing {relative}")
            failed = True
        elif path.read_bytes() != data:
            print(f"FAIL regenerated bytes differ {relative}")
            failed = True
        else:
            print(f"OK byte-identical {relative}")

    interpolated = sample_lut_trilinear(lut, chart_linear)
    difference = np.abs(srgb_encode_clamped(interpolated) - srgb_encode_clamped(reference.astype(np.float64)))
    per_pixel = difference.max(axis=1)
    worst = int(np.argmax(per_pixel))
    print(f"chart_pixels={per_pixel.size} max_encoded_difference={per_pixel.max():.6f} "
          f"({per_pixel.max() * 255.0:.3f}/255) mean={per_pixel.mean() * 255.0:.4f}/255 "
          f"threshold={MAX_ENCODED_DIFFERENCE * 255.0:.1f}/255")
    print(f"worst_pixel index={worst} x={worst % CHART_WIDTH} y={worst // CHART_WIDTH} "
          f"scene={chart_linear[worst].tolist()}")
    over = int(np.count_nonzero(per_pixel > MAX_ENCODED_DIFFERENCE))
    if over:
        print(f"FAIL {over} pixels exceed the threshold")
        failed = True
    else:
        print("OK all chart pixels within threshold")

    # GPU の三線形補間は重みを 8 bit の小数で持つ実装がある。その丸めでの差も参考に表示する。
    quantized = sample_lut_trilinear(lut, chart_linear, weight_steps=256)
    quantized_difference = np.abs(srgb_encode_clamped(quantized) - srgb_encode_clamped(reference.astype(np.float64)))
    print(f"info chart_8bit_weights max_encoded_difference={quantized_difference.max() * 255.0:.3f}/255")
    report_robustness(lut)
    print("RESULT=" + ("FAIL" if failed else "PASS"))
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description="ACES 2.0 SDR の出力変換を 3D LUT へ焼き、基準画像を作る")
    parser.add_argument("--verify", action="store_true", help="再生成の byte 一致と LUT 補間の誤差を検査する")
    arguments = parser.parse_args()
    if arguments.verify:
        return verify()
    outputs, _, _, _ = generate()
    for path, data in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    describe(outputs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
