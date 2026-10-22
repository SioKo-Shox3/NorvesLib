# 手本の写真と、写真の構図に寄せた撮影（SceneCatalog.h の ref_*）の空の雲の被覆と明るさの分布を並べる（SCN-WLR-CLOUD-REF）。
# 雲の形（密度場の作り方）を写真と比べる材料で、Cycles との比べ（ReferenceCheck.py --cloud）とは別に書く。
#
# 使い方:
#   python Scripts/Terrain/CloudPhotoStats.py --capture <CaptureScene.ps1 の OutDir> [--photo far_01] [--out <出力先>]
# <出力先>/cloud_photo_<写真>.json と、写真と撮影の空の雲の印を重ねて並べた <出力先>/cloud_photo_<写真>.png を書き、数値を 1 行ずつ出す。
#
# 並べ方と領域は CompareReference.py と同じ（写真は横幅を撮影にそろえて 16:9 に切り、地形の輪郭より上を空とする）。
# 輪郭の近くの混ざった画素を外すため、空は輪郭より SKY_MARGIN_ROWS 行上まで。
# 雲の印: 線形の R ÷ B が CLOUD_RED_BLUE_RATIO 以上の空の画素（写真の空の R ÷ B は青空 0.4〜0.6 と雲 0.8〜1.0 の二峰で、
# 谷は 0.6〜0.7）。白さだけで決めるので、青みの残る薄い雲は青空に数える。
# 量（画素は sRGB を線形に戻した値。Y = 0.2126R + 0.7152G + 0.0722B）:
#   - 被覆: 空の画素のうち雲の印の割合。仰角の帯（空の行を上・中・下に 3 等分）ごとにも書く
#   - R ÷ B の分布: 0.1 刻みの割合
#   - 明るさの分布: 空の Y の 5・25・50・75・95 % 点を空の Y の中央値で割った値（露出をそろえる）
#   - 雲と青空の比: 雲の印の Y の中央値 ÷ 青空（印の無い画素）の Y の中央値（どちらも空の 1 % 以上あるときだけ）
#   - 縁の鋭さ: 空の中の隣の画素との Y の差 ÷ Y の 95 % 点（雲の縁が締まっていれば大きい）
# 写真の現像・露出・天気・時刻は撮影と違うので、値の一致を合否にしない（差の向きと大きさを見るためだけに使う）。
# 終了コード: 書けたら 0、写真・撮影が無いときは 1。
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

from CompareLight import load_font, srgb_to_linear
from CompareReference import PANEL_SIZE, PHOTO_DIR, PHOTOS, find_terrain_top, fit_photo

SKY_MARGIN_ROWS = 8
CLOUD_RED_BLUE_RATIO = 0.7
MIN_CLASS_FRACTION = 0.01
LUMA = np.array([0.2126, 0.7152, 0.0722])
RATIO_BINS = np.round(np.arange(0.0, 1.31, 0.1), 2)
BAND_NAMES = ("upper", "middle", "lower")
CLOUD_TINT = np.array([255, 60, 200], dtype=np.float64)


def measure(panel):
    """空の画素の雲の印と量。panel は PANEL_SIZE の RGB の画"""
    rgb = np.asarray(panel)
    linear = srgb_to_linear(rgb.astype(np.float64))
    horizon = find_terrain_top(rgb)
    rows = np.arange(rgb.shape[0])[:, None]
    sky = rows < (horizon[None, :] - SKY_MARGIN_ROWS)
    if not sky.any():
        return None, sky, sky
    red_blue = linear[..., 0] / np.maximum(linear[..., 2], 1e-6)
    luminance = linear @ LUMA
    cloud = sky & (red_blue >= CLOUD_RED_BLUE_RATIO)
    clear = sky & ~cloud
    sky_count = int(sky.sum())
    sky_luminance = luminance[sky]
    median = float(np.median(sky_luminance))

    sky_rows = np.where(sky.any(axis=1))[0]
    edges = np.linspace(sky_rows.min(), sky_rows.max() + 1, len(BAND_NAMES) + 1).astype(int)
    bands = {}
    for name, start, stop in zip(BAND_NAMES, edges[:-1], edges[1:]):
        band = sky[start:stop]
        bands[name] = {"rows": [int(start), int(stop)],
                       "coverage": float(cloud[start:stop][band].mean()) if band.any() else None}

    histogram, _ = np.histogram(np.clip(red_blue[sky], 0.0, RATIO_BINS[-1] - 1e-6), bins=RATIO_BINS)
    cloud_to_clear = None
    if cloud.sum() >= MIN_CLASS_FRACTION * sky_count and clear.sum() >= MIN_CLASS_FRACTION * sky_count:
        cloud_to_clear = float(np.median(luminance[cloud]) / np.median(luminance[clear]))
    # 縁の鋭さ: 横と縦の隣が両方とも空の画素の組の差
    dx = np.abs(np.diff(luminance, axis=1))[sky[:, 1:] & sky[:, :-1]]
    dy = np.abs(np.diff(luminance, axis=0))[sky[1:, :] & sky[:-1, :]]
    gradient = np.concatenate([dx, dy]) / max(median, 1e-6)
    stats = {
        "sky_pixels": sky_count,
        "coverage": float(cloud.sum() / sky_count),
        "coverage_bands": bands,
        "red_blue_histogram": {f"{lo:.1f}-{hi:.1f}": float(n / sky_count)
                               for lo, hi, n in zip(RATIO_BINS[:-1], RATIO_BINS[1:], histogram)},
        "red_blue_median": {"cloud": float(np.median(red_blue[cloud])) if cloud.any() else None,
                            "clear": float(np.median(red_blue[clear])) if clear.any() else None},
        "luminance_median": median,
        "luminance_percentiles_over_median": {str(p): float(v / median)
                                              for p, v in zip((5, 25, 50, 75, 95),
                                                              np.percentile(sky_luminance, (5, 25, 50, 75, 95)))},
        "cloud_to_clear_luminance": cloud_to_clear,
        "edge_gradient_p95_over_median": float(np.percentile(gradient, 95)),
    }
    return stats, sky, cloud


def overlay(panel, sky, cloud):
    """空の外を暗くし、雲の印に色を重ねた画"""
    image = np.asarray(panel).astype(np.float64)
    image[~sky] *= 0.35
    image[cloud] = 0.55 * image[cloud] + 0.45 * CLOUD_TINT
    return Image.fromarray(np.clip(image, 0, 255).astype(np.uint8))


def main():
    parser = argparse.ArgumentParser(description="手本の写真と撮影の空の雲の被覆と明るさの分布を並べる")
    parser.add_argument("--capture", required=True, help="CaptureScene.ps1 の OutDir（<視点>.png がある）")
    parser.add_argument("--photo", default="far_01", help="手本の写真の ID（CompareReference.PHOTOS）")
    parser.add_argument("--out", default="", help="出力先（省くと --capture）")
    args = parser.parse_args()

    entry = next((p for p in PHOTOS if p["id"] == args.photo), None)
    if entry is None:
        print(f"CLOUD_PHOTO error 写真 {args.photo} は CompareReference.PHOTOS に無い")
        return 1
    photo_path = os.path.join(PHOTO_DIR, entry["id"] + ".jpg")
    capture_path = os.path.join(args.capture, entry["view"] + ".png")
    for path in (photo_path, capture_path):
        if not os.path.isfile(path):
            print(f"CLOUD_PHOTO error ファイルが無い {path}")
            return 1
    out_dir = args.out or args.capture
    os.makedirs(out_dir, exist_ok=True)

    photo_panel, _, _ = fit_photo(Image.open(photo_path).convert("RGB"))
    capture_panel = Image.open(capture_path).convert("RGB").resize(PANEL_SIZE, Image.LANCZOS)
    report = {"photo": entry["id"], "view": entry["view"], "capture": os.path.abspath(capture_path),
              "cloud_red_blue_ratio": CLOUD_RED_BLUE_RATIO, "sky_margin_rows": SKY_MARGIN_ROWS}
    panels = []
    for label, panel in (("photo", photo_panel), ("engine", capture_panel)):
        stats, sky, cloud = measure(panel)
        if stats is None:
            print(f"CLOUD_PHOTO error {label} に空の画素が無い")
            return 1
        report[label] = stats
        panels.append(overlay(panel, sky, cloud))
        bands = " ".join(f"{name}={band['coverage']:.2f}" for name, band in stats["coverage_bands"].items()
                         if band["coverage"] is not None)
        ratio = stats["cloud_to_clear_luminance"]
        percentiles = stats["luminance_percentiles_over_median"]
        print(f"CLOUD_PHOTO {label} sky_px={stats['sky_pixels']} coverage={stats['coverage']:.3f} bands=[{bands}] "
              f"rb_cloud={stats['red_blue_median']['cloud']} rb_clear={stats['red_blue_median']['clear']} "
              f"Y_p5={percentiles['5']:.3f} Y_p95={percentiles['95']:.3f} "
              f"cloud_to_clear={'none' if ratio is None else f'{ratio:.3f}'} "
              f"edge_p95={stats['edge_gradient_p95_over_median']:.4f}")

    width, height = PANEL_SIZE
    label_height = 34
    sheet = Image.new("RGB", (width * 2, height + label_height), (20, 20, 20))
    draw = ImageDraw.Draw(sheet)
    font = load_font(22)
    for index, (panel, title) in enumerate(zip(panels, (f"写真 {entry['id']}", f"撮影 {entry['view']}"))):
        sheet.paste(panel, (index * width, label_height))
        stats = report["photo" if index == 0 else "engine"]
        draw.text((index * width + 10, 4), f"{title}  雲の被覆 {stats['coverage']:.2f}（R÷B ≥ {CLOUD_RED_BLUE_RATIO}）",
                  fill=(255, 255, 255), font=font)
    image_path = os.path.join(out_dir, f"cloud_photo_{entry['id']}.png")
    sheet.save(image_path)
    report_path = os.path.join(out_dir, f"cloud_photo_{entry['id']}.json")
    with open(report_path, "w", encoding="utf-8") as file:
        json.dump(report, file, ensure_ascii=False, indent=2)
    print(f"CLOUD_PHOTO result=written image={image_path} report={report_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
