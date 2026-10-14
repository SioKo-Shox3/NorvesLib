# 荒野の撮影と手本の写真（既定は晴天の遠景 far_01）を左右に並べ、青空と雲の明るさ・日向と影の中の明るさの比・
# 遠景の霞の量・地面の明るさと色を同じ求め方で出す簡易の比較。基準の光（SCN-WL-LIGHT-BASE）を決めるときに開いて見るためのもので、
# 構図を合わせた正式な比較は SCN-WL-REFERENCE-CAPTURE の CompareReference.py が受け持つ。
#
# 使い方:
#   python Scripts/Terrain/CompareLight.py --capture-dir <CaptureScene.ps1 の OutDir> [--views overview,ground] [--photo far_01]
#                                          [--out <出力先>]
# 視点ごとに <出力先>/light_<視点>.png（上の段: 写真と撮影、下の段: 空・遠景の帯・近景の帯の塗り分け）と、
# <出力先>/light_metrics.json を書き、数値を 1 行ずつ出す。
#
# 求め方（画素は sRGB を線形に戻した輝度 Y = 0.2126R + 0.7152G + 0.0722B）:
#   - 地平線: 列ごとに上から見て、赤みのある（R - B が 8 bit で 10 を越える）画素が 4 行続いた所を地形の始まりとし、
#     列の並びで中央値をとってならす（青空と白い雲は R ≦ B 付近、霞んだ遠景を含む砂岩と砂は R > B）。
#     塗り分けの図で境が正しいかを目で確かめる。
#   - sky_terrain_ratio: 空の Y の平均 ÷ 地形の Y の平均（地面の材質に左右されるので記録だけにする）
#   - terrain_dark_ratio: 地形の Y の 5 パーセンタイル ÷ 中央値（影の濃さの目安。低いほど影が濃い）
#   - blue_sky_luminance・cloud_luminance・cloud_fraction: 画像の上 35 % の空を、彩度 0.25 未満の雲とそれ以外の青空に
#     分け、それぞれの Y の中央値と、雲が占める割合（露出を写真の記録に合わせた上での空と雲の画素の明るさ）
#   - shade_ratio: 材質のそろった領域の Y の 10 パーセンタイル ÷ 90 パーセンタイル（日向に対する影の中の明るさ）。
#     写真は近景（下 25 %）の赤い土（R > 1.15 G かつ R - B > 15）で低木の影と日向の土、撮影は SHADE_BOXES の
#     視点の岩肌の枠で、日の当たらない面・溝と日向の面。枠の無い視点は出さない
#   - ground_luminance・ground_srgb: 近景（下 25 %）の Y と sRGB の中央値（地面の材質の記録。光の合否には使わない）
#   - haze_far_to_sky: 地平線のすぐ下の帯（画像の高さの 6 %）の Y ÷ すぐ上の空の帯の Y（1 に近いほど遠景が空に溶ける）
#   - haze_saturation_ratio: その帯の彩度 ÷ 画像の下 25 % の近景の彩度（低いほど遠景の色が抜ける）
# 写真の加工とホワイトバランスは分からないので、値の一致は合否にせず、比べる目安にだけ使う。
# 終了コード: 書けたら 0、撮影や写真が無ければ 1。
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PHOTO_DIR = os.path.join(REPO_ROOT, "Assets", "Reference", "ValleyOfTheGods")

# 地形とみなす赤みのしきい値（8 bit の R - B）と、続く行の数
GROUND_RED_MINUS_BLUE = 10
GROUND_RUN_ROWS = 4
# 地平線をならす列の幅[px]（奇数）
HORIZON_MEDIAN_COLUMNS = 41
# 地平線の上下に取る帯の高さ（画像の高さに対する割合）と、近景の帯（画像の下からの割合）
HORIZON_BAND_FRACTION = 0.06
NEAR_BAND_FRACTION = 0.25
# 並べる絵の高さ[px]
PANEL_HEIGHT = 720
# 青空と雲を分ける所: 画像の上からの割合（地平線の霞を除く）と、雲とみなす彩度の上限
UPPER_SKY_FRACTION = 0.35
CLOUD_SATURATION_MAX = 0.25
# 影の比を測る材質のそろった枠（画像の幅・高さに対する割合 x0, y0, x1, y1）。写真は近景の赤い土を色で選ぶ
SHADE_BOXES = {"cliff": (0.45, 0.40, 0.92, 0.62)}
PHOTO_SHADE_BOX = (0.0, 1.0 - NEAR_BAND_FRACTION, 1.0, 1.0)
SOIL_RED_OVER_GREEN = 1.15
SOIL_RED_MINUS_BLUE = 15
# 比較の画像の文字（日本語を出せる Windows の書体。無ければ既定の書体）
LABEL_FONT_PATHS = ("C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/msgothic.ttc")


def srgb_to_linear(values):
    """8 bit の sRGB を 0〜1 の線形の値へ戻す"""
    c = values / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def find_horizon(rgb):
    """列ごとの地形の始まりの行（見つからない列は画像の高さ）"""
    height, width, _ = rgb.shape
    ground = (rgb[:, :, 0].astype(np.int32) - rgb[:, :, 2].astype(np.int32)) > GROUND_RED_MINUS_BLUE
    run = np.ones_like(ground)
    for offset in range(GROUND_RUN_ROWS):
        shifted = np.zeros_like(ground)
        shifted[: height - offset] = ground[offset:]
        run &= shifted
    horizon = np.full(width, height, dtype=np.int32)
    for column in range(width):
        rows = np.flatnonzero(run[:, column])
        if rows.size > 0:
            horizon[column] = rows[0]
    half = HORIZON_MEDIAN_COLUMNS // 2
    padded = np.pad(horizon, half, mode="edge")
    windows = np.lib.stride_tricks.sliding_window_view(padded, HORIZON_MEDIAN_COLUMNS)
    return np.median(windows, axis=1).astype(np.int32)


def box_pixels(values, box):
    """割合で与えた枠の中の画素"""
    height, width = values.shape[:2]
    x0, y0, x1, y1 = box
    return values[int(round(y0 * height)) : int(round(y1 * height)), int(round(x0 * width)) : int(round(x1 * width))]


def shade_ratio(rgb, luminance, box, soil_only):
    """枠の中の Y の 10 パーセンタイル ÷ 90 パーセンタイル。soil_only なら赤い土の画素だけを数える"""
    values = box_pixels(luminance, box)
    if soil_only:
        pixels = box_pixels(rgb, box).astype(np.int32)
        soil = (pixels[:, :, 0] > pixels[:, :, 1] * SOIL_RED_OVER_GREEN) & (
            pixels[:, :, 0] - pixels[:, :, 2] > SOIL_RED_MINUS_BLUE
        )
        values = values[soil]
    values = values.ravel()
    if values.size == 0:
        return float("nan")
    return float(np.percentile(values, 10) / np.percentile(values, 90))


def measure(rgb, shade_box=None, soil_only=False):
    """明るさの比・暗部の濃さ・霞の量・青空と雲・影の比・地面と、塗り分けに使う領域"""
    height, width, _ = rgb.shape
    linear = srgb_to_linear(rgb.astype(np.float64))
    luminance = linear @ np.array([0.2126, 0.7152, 0.0722])
    maximum = linear.max(axis=2)
    minimum = linear.min(axis=2)
    saturation = np.where(maximum > 1e-6, (maximum - minimum) / np.maximum(maximum, 1e-6), 0.0)

    horizon = find_horizon(rgb)
    rows = np.arange(height)[:, None]
    sky = rows < horizon[None, :]
    terrain = ~sky
    band = int(round(height * HORIZON_BAND_FRACTION))
    far = terrain & (rows < horizon[None, :] + band)
    sky_band = sky & (rows >= horizon[None, :] - band)
    near = terrain & (rows >= int(round(height * (1.0 - NEAR_BAND_FRACTION))))

    def mean(values, mask):
        return float(values[mask].mean()) if mask.any() else float("nan")

    upper = sky & (rows < int(round(height * UPPER_SKY_FRACTION)))
    cloud = upper & (saturation < CLOUD_SATURATION_MAX)
    blue = upper & ~cloud

    def median(values, mask):
        return float(np.median(values[mask])) if mask.any() else float("nan")

    terrain_values = luminance[terrain]
    metrics = {
        "sky_fraction": float(sky.mean()),
        "sky_luminance": mean(luminance, sky),
        "terrain_luminance": mean(luminance, terrain),
        "sky_terrain_ratio": mean(luminance, sky) / mean(luminance, terrain),
        "terrain_dark_ratio": float(np.percentile(terrain_values, 5) / np.percentile(terrain_values, 50))
        if terrain_values.size > 0
        else float("nan"),
        "haze_far_to_sky": mean(luminance, far) / mean(luminance, sky_band),
        "haze_saturation_ratio": mean(saturation, far) / mean(saturation, near),
        "blue_sky_luminance": median(luminance, blue),
        "cloud_luminance": median(luminance, cloud),
        "cloud_fraction": float(cloud.sum() / upper.sum()) if upper.any() else float("nan"),
        "shade_ratio": shade_ratio(rgb, luminance, shade_box, soil_only) if shade_box else float("nan"),
        "ground_luminance": median(luminance, near),
        "ground_srgb": [float(v) for v in np.median(rgb[near], axis=0)] if near.any() else [],
    }
    return metrics, {"sky": sky, "far": far, "near": near}


def overlay(rgb, regions):
    """空を青・遠景の帯を黄・近景の帯を緑で塗った図"""
    image = rgb.astype(np.float64)
    for name, color in (("sky", (60, 120, 255)), ("far", (255, 220, 0)), ("near", (0, 200, 80))):
        mask = regions[name]
        image[mask] = image[mask] * 0.45 + np.array(color) * 0.55
    return Image.fromarray(image.clip(0, 255).astype(np.uint8))


def fit_height(image, height):
    width = int(round(image.width * height / image.height))
    return image.resize((width, height), Image.LANCZOS)


def describe(label, metrics):
    """ログの 1 行（機械が拾うキーは英語）"""
    return (
        f"{label}: blue_sky={metrics['blue_sky_luminance']:.3f} cloud={metrics['cloud_luminance']:.3f} "
        f"cloud_fraction={metrics['cloud_fraction']:.2f} shade={metrics['shade_ratio']:.2f} "
        f"haze_far_to_sky={metrics['haze_far_to_sky']:.2f} haze_sat={metrics['haze_saturation_ratio']:.2f} "
        f"ground={metrics['ground_luminance']:.3f} sky_terrain={metrics['sky_terrain_ratio']:.2f}"
    )


def caption(label, metrics):
    """比較の画像に書く日本語の 1 行"""
    shade = "－" if np.isnan(metrics["shade_ratio"]) else f"{metrics['shade_ratio']:.2f}"
    return (
        f"{label}　青空 {metrics['blue_sky_luminance']:.3f}　雲 {metrics['cloud_luminance']:.3f}"
        f"（空の {metrics['cloud_fraction']:.0%}）　影÷日向 {shade}　"
        f"遠景÷空 {metrics['haze_far_to_sky']:.2f}　遠景÷近景の彩度 {metrics['haze_saturation_ratio']:.2f}　"
        f"地面 {metrics['ground_luminance']:.3f}"
    )


def load_font(size):
    for path in LABEL_FONT_PATHS:
        if os.path.isfile(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def main():
    parser = argparse.ArgumentParser(description="荒野の撮影と手本の写真を並べ、光の目安の数値を出す")
    parser.add_argument("--capture-dir", required=True, help="CaptureScene.ps1 の OutDir（<視点>.png がある）")
    parser.add_argument("--views", default="", help="比べる視点（カンマ区切り。省くと撮影の PNG すべて）")
    parser.add_argument("--photo", default="far_01", help="手本の写真の ID（Assets/Reference/ValleyOfTheGods/<ID>.jpg）")
    parser.add_argument("--out", default="", help="出力先（省くと --capture-dir）")
    args = parser.parse_args()

    photo_path = os.path.join(PHOTO_DIR, args.photo + ".jpg")
    if not os.path.isfile(photo_path):
        print(f"COMPARE_LIGHT result=fail reason=写真が無い path={photo_path}")
        return 1
    views = [v for v in args.views.split(",") if v] if args.views else sorted(
        os.path.splitext(name)[0] for name in os.listdir(args.capture_dir) if name.endswith(".png") and not name.startswith("light_")
    )
    if not views:
        print(f"COMPARE_LIGHT result=fail reason=撮影が無い dir={args.capture_dir}")
        return 1
    out_dir = args.out or args.capture_dir
    os.makedirs(out_dir, exist_ok=True)

    photo = fit_height(Image.open(photo_path).convert("RGB"), PANEL_HEIGHT)
    photo_rgb = np.asarray(photo)
    photo_metrics, photo_regions = measure(photo_rgb, PHOTO_SHADE_BOX, soil_only=True)
    print("COMPARE_LIGHT " + describe(f"photo={args.photo}", photo_metrics))
    report = {"photo": args.photo, "photo_metrics": photo_metrics, "views": {}}

    for view in views:
        capture_path = os.path.join(args.capture_dir, view + ".png")
        if not os.path.isfile(capture_path):
            print(f"COMPARE_LIGHT result=fail reason=撮影が無い path={capture_path}")
            return 1
        capture = fit_height(Image.open(capture_path).convert("RGB"), PANEL_HEIGHT)
        capture_rgb = np.asarray(capture)
        capture_metrics, capture_regions = measure(capture_rgb, SHADE_BOXES.get(view))
        report["views"][view] = capture_metrics
        print("COMPARE_LIGHT " + describe(f"view={view}", capture_metrics))

        text_height = 60
        sheet = Image.new("RGB", (photo.width + capture.width, PANEL_HEIGHT * 2 + text_height), (24, 24, 24))
        sheet.paste(photo, (0, 0))
        sheet.paste(capture, (photo.width, 0))
        sheet.paste(overlay(photo_rgb, photo_regions), (0, PANEL_HEIGHT))
        sheet.paste(overlay(capture_rgb, capture_regions), (photo.width, PANEL_HEIGHT))
        draw = ImageDraw.Draw(sheet)
        font = load_font(20)
        draw.text((10, PANEL_HEIGHT * 2 + 4), caption(f"写真 {args.photo}", photo_metrics), fill=(235, 235, 235), font=font)
        draw.text((10, PANEL_HEIGHT * 2 + 32), caption(f"撮影 {view}", capture_metrics), fill=(235, 235, 235), font=font)
        sheet.save(os.path.join(out_dir, f"light_{view}.png"))

    with open(os.path.join(out_dir, "light_metrics.json"), "w", encoding="utf-8") as file:
        json.dump(report, file, ensure_ascii=False, indent=2)
    print(f"COMPARE_LIGHT result=pass views={len(views)} out={out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
