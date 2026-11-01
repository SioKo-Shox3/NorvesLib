# 撮影の同じ領域で高周波輝度の標準偏差を平均輝度で割り、RTGI の粒の量の比を検査する。
# 画像は同じ視点・解像度で撮り、領域と比較する置き場は --expect の JSON で指定する。
import argparse
import json
import math
from pathlib import Path
import sys

import numpy as np
from PIL import Image


def measure_grain(png_path, regions, blur_radius_px=2):
    """sRGB の PNG を線形輝度へ戻し、各領域の粒の量と失敗理由を返す。"""
    with Image.open(png_path) as image:
        encoded = np.asarray(image.convert("RGB"), dtype=np.float64) / 255.0
    linear = np.where(encoded <= 0.04045, encoded / 12.92,
                      ((encoded + 0.055) / 1.055) ** 2.4)
    luma = linear @ np.array([0.2126, 0.7152, 0.0722])
    height, width = luma.shape

    # 全画面を箱でぼかしてから切り出し、領域の境界に人工的な縁を作らない。
    # 画像の端では端の値を延ばす。半径 r の箱は (2r + 1) × (2r + 1) 画素。
    blurred = luma
    radius = blur_radius_px
    if radius > 0:
        for axis in (0, 1):
            pad = [(0, 0), (0, 0)]
            pad[axis] = (radius + 1, radius)
            cumulative = np.cumsum(np.pad(blurred, pad, mode="edge"), axis=axis)
            indices = np.arange(blurred.shape[axis])
            blurred = (np.take(cumulative, indices + 2 * radius + 1, axis=axis)
                       - np.take(cumulative, indices, axis=axis)) / (2 * radius + 1)
    high_frequency = luma - blurred

    measurements = []
    for region in regions:
        x0, y0, x1, y1 = region["rect"]
        # 左上を含み右下を含まない。正規化の端は床・天井で画素の境界へ広げる。
        left, top = math.floor(x0 * width), math.floor(y0 * height)
        right, bottom = math.ceil(x1 * width), math.ceil(y1 * height)
        mean = float(luma[top:bottom, left:right].mean())
        stddev = float(high_frequency[top:bottom, left:right].std())
        entry = {"name": region["name"], "mean_luminance": mean,
                 "high_frequency_stddev": stddev, "value": None}
        if mean < 1e-4:
            entry["reason"] = "領域の平均輝度が 1e-4 未満"
        else:
            entry["value"] = stddev / mean
        measurements.append(entry)
    return measurements, (width, height)


def validate_spec(spec):
    """空の検査や領域の切り詰めによって誤って合格しないよう、仕様を検査する。"""
    if not isinstance(spec, dict):
        raise ValueError("仕様は JSON オブジェクトで指定する")
    for key, default in (("view", "horizon"), ("numerator", "path"), ("denominator", "static")):
        value = spec.setdefault(key, default)
        if not isinstance(value, str) or not value.strip():
            raise ValueError(f"{key} は空でない文字列で指定する")
    radius = spec.setdefault("blur_radius_px", 2)
    if type(radius) is not int or radius < 0:
        raise ValueError("blur_radius_px は 0 以上の整数で指定する")
    regions = spec.get("regions")
    if not isinstance(regions, list) or not regions:
        raise ValueError("regions に領域を 1 つ以上指定する")
    names = set()
    for region in regions:
        if not isinstance(region, dict):
            raise ValueError("領域は JSON オブジェクトで指定する")
        name = region.get("name")
        if not isinstance(name, str) or not name or any(c.isspace() for c in name) or name in names:
            raise ValueError("領域名は空白を含まない重複のない文字列で指定する")
        names.add(name)
        rect = region.get("rect")
        if (not isinstance(rect, list) or len(rect) != 4
                or any(type(v) not in (int, float) or not math.isfinite(v) for v in rect)):
            raise ValueError(f"領域 {name}: rect は有限な数値 4 つで指定する")
        x0, y0, x1, y1 = rect
        if not (0 <= x0 < x1 <= 1 and 0 <= y0 < y1 <= 1):
            raise ValueError(f"領域 {name}: 矩形が画像の外、または幅・高さが正でない")
        limit = region.get("max_ratio")
        if type(limit) not in (int, float) or not math.isfinite(limit) or limit < 0:
            raise ValueError(f"領域 {name}: max_ratio は 0 以上の有限な数値で指定する")


def check_grain(root, spec):
    """仕様の置き場の組を読み、領域ごとの比と全体の判定を返す。"""
    validate_spec(spec)
    report = {"view": spec["view"], "numerator": spec["numerator"],
              "denominator": spec["denominator"], "blur_radius_px": spec["blur_radius_px"],
              "regions": [], "result": "fail"}
    measured = {}
    sizes = {}
    failures = {}
    for key in ("numerator", "denominator"):
        png_path = root / spec[key] / (spec["view"] + ".png")
        try:
            if not png_path.is_file():
                raise ValueError(f"画像ファイルが無い: {png_path}")
            measured[key], sizes[key] = measure_grain(png_path, spec["regions"], spec["blur_radius_px"])
        except (OSError, ValueError) as error:
            failures[key] = str(error)
    if len(sizes) == 2 and sizes["numerator"] != sizes["denominator"]:
        failures["size"] = "比較する画像の解像度が一致しない"

    for index, region in enumerate(spec["regions"]):
        row = {"name": region["name"], "rect": region["rect"], "numerator": None,
               "denominator": None, "ratio": None, "max_ratio": region["max_ratio"],
               "result": "fail"}
        reasons = list(failures.values())
        for key in ("numerator", "denominator"):
            if key in measured:
                measurement = measured[key][index]
                row[key] = measurement["value"]
                if "reason" in measurement:
                    reasons.append(f"{key}: {measurement['reason']}")
        if not reasons:
            if row["denominator"] <= 0:
                reasons.append("比較元の粒の量が 0 のため比を計算できない")
            else:
                row["ratio"] = row["numerator"] / row["denominator"]
                if row["ratio"] <= row["max_ratio"]:
                    row["result"] = "pass"
                else:
                    reasons.append("粒の量の比が max_ratio を超えた")
        if reasons:
            row["reasons"] = reasons
        report["regions"].append(row)
    if all(row["result"] == "pass" for row in report["regions"]):
        report["result"] = "pass"
    return report


def main():
    parser = argparse.ArgumentParser(description="撮影の領域ごとに RTGI の粒の量の比を検査する")
    parser.add_argument("--root", required=True, type=Path, help="比較する撮影の組の置き場")
    parser.add_argument("--expect", required=True, type=Path, help="領域と比の上限を指定する JSON")
    args = parser.parse_args()
    try:
        with args.expect.open(encoding="utf-8-sig") as stream:
            spec = json.load(stream)
        report = check_grain(args.root, spec)
    except (OSError, ValueError) as error:
        report = {"regions": [], "result": "fail", "reasons": [str(error)]}

    for row in report["regions"]:
        def number(key):
            value = row[key]
            return "null" if value is None else f"{value:.6f}" if key in ("numerator", "denominator") else f"{value:.3f}"
        print(f"RTGI_GRAIN region={row['name']} numerator={number('numerator')} "
              f"denominator={number('denominator')} ratio={number('ratio')} "
              f"max_ratio={number('max_ratio')} result={row['result']}")
        for reason in row.get("reasons", []):
            print(f"RTGI_GRAIN reason={reason}")
    for reason in report.get("reasons", []):
        print(f"RTGI_GRAIN reason={reason}")
    try:
        with (args.root / "rtgi-grain.json").open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
    except (OSError, ValueError) as error:
        report["result"] = "fail"
        print(f"RTGI_GRAIN reason=結果の JSON を保存できない: {error}")
    print(f"RTGI_GRAIN result={report['result']}")
    return 0 if report["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
