# 荒野の撮影と手本の写真（既定は晴天の遠景 far_01）を左右に並べ、空と地形の明るさの比・地形の暗部の濃さ・
# 遠景の霞の量を同じ求め方で出す簡易の比較。基準の光（SCN-WL-LIGHT-BASE）を決めるときに開いて見るためのもので、
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
#   - sky_terrain_ratio: 空の Y の平均 ÷ 地形の Y の平均
#   - terrain_dark_ratio: 地形の Y の 5 パーセンタイル ÷ 中央値（影の濃さの目安。低いほど影が濃い）
#   - haze_far_to_sky: 地平線のすぐ下の帯（画像の高さの 6 %）の Y ÷ すぐ上の空の帯の Y（1 に近いほど遠景が空に溶ける）
#   - haze_saturation_ratio: その帯の彩度 ÷ 画像の下 25 % の近景の彩度（低いほど遠景の色が抜ける）
# 写真の加工とホワイトバランスは分からないので、値の一致は合否にせず、比べる目安にだけ使う。
# 終了コード: 書けたら 0、撮影や写真が無ければ 1。
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

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


def measure(rgb):
    """明るさの比・暗部の濃さ・霞の量と、塗り分けに使う領域"""
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
    return (
        f"{label}: sky/terrain={metrics['sky_terrain_ratio']:.2f} dark p5/p50={metrics['terrain_dark_ratio']:.2f} "
        f"haze far/sky={metrics['haze_far_to_sky']:.2f} haze sat far/near={metrics['haze_saturation_ratio']:.2f}"
    )


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
    photo_metrics, photo_regions = measure(photo_rgb)
    print("COMPARE_LIGHT " + describe(f"photo={args.photo}", photo_metrics))
    report = {"photo": args.photo, "photo_metrics": photo_metrics, "views": {}}

    for view in views:
        capture_path = os.path.join(args.capture_dir, view + ".png")
        if not os.path.isfile(capture_path):
            print(f"COMPARE_LIGHT result=fail reason=撮影が無い path={capture_path}")
            return 1
        capture = fit_height(Image.open(capture_path).convert("RGB"), PANEL_HEIGHT)
        capture_rgb = np.asarray(capture)
        capture_metrics, capture_regions = measure(capture_rgb)
        report["views"][view] = capture_metrics
        print("COMPARE_LIGHT " + describe(f"view={view}", capture_metrics))

        text_height = 60
        sheet = Image.new("RGB", (photo.width + capture.width, PANEL_HEIGHT * 2 + text_height), (24, 24, 24))
        sheet.paste(photo, (0, 0))
        sheet.paste(capture, (photo.width, 0))
        sheet.paste(overlay(photo_rgb, photo_regions), (0, PANEL_HEIGHT))
        sheet.paste(overlay(capture_rgb, capture_regions), (photo.width, PANEL_HEIGHT))
        draw = ImageDraw.Draw(sheet)
        draw.text((10, PANEL_HEIGHT * 2 + 8), describe(f"photo={args.photo}", photo_metrics), fill=(235, 235, 235))
        draw.text((10, PANEL_HEIGHT * 2 + 32), describe(f"view={view}", capture_metrics), fill=(235, 235, 235))
        sheet.save(os.path.join(out_dir, f"light_{view}.png"))

    with open(os.path.join(out_dir, "light_metrics.json"), "w", encoding="utf-8") as file:
        json.dump(report, file, ensure_ascii=False, indent=2)
    print(f"COMPARE_LIGHT result=pass views={len(views)} out={out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
