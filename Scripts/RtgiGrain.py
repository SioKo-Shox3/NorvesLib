# 撮影の同じ領域で高周波輝度の標準偏差を平均輝度で割り、RTGI の粒の量の比を検査する。
# 画像は同じ視点・解像度で撮り、領域と比較する置き場は --expect の JSON で指定する。
import argparse
import json
import math
from pathlib import Path
import sys

import numpy as np
from PIL import Image


def box_blur(values, radius):
    """画像の端の値を延ばし、指定した半径の箱で平均する。"""
    # 画像の端では端の値を延ばす。半径 r の箱は (2r + 1) × (2r + 1) 画素。
    blurred = values
    if radius > 0:
        for axis in (0, 1):
            pad = [(0, 0), (0, 0)]
            pad[axis] = (radius + 1, radius)
            cumulative = np.cumsum(np.pad(blurred, pad, mode="edge"), axis=axis)
            indices = np.arange(blurred.shape[axis])
            blurred = (np.take(cumulative, indices + 2 * radius + 1, axis=axis)
                       - np.take(cumulative, indices, axis=axis)) / (2 * radius + 1)
    return blurred


def dilate_disk(mask, radius):
    """最も近い真の画素までのユークリッド距離が半径以内の画素を返す。"""
    height, width = mask.shape
    expanded = np.zeros_like(mask)
    # 円盤を行ごとの区間に分け、横の積分和で区間内の有無を調べる。
    cumulative = np.cumsum(np.pad(mask, ((0, 0), (radius + 1, radius))),
                           axis=1, dtype=np.int64)
    for dy in range(-min(radius, height - 1), min(radius, height - 1) + 1):
        dx = math.isqrt(radius * radius - dy * dy)
        start, end = radius - dx, radius + dx + 1
        occupied = cumulative[:, end:end + width] > cumulative[:, start:start + width]
        if dy >= 0:
            expanded[dy:] |= occupied[:height - dy]
        else:
            expanded[:height + dy] |= occupied[-dy:]
    return expanded


def select_floor_pixels(hsv, spec):
    """sRGB の HSV 色とバナナからの距離で、床と測定対象のマスクを返す。"""
    hue, saturation, value = hsv[:, :, 0] * 360.0, hsv[:, :, 1], hsv[:, :, 2]
    floor_spec, banana_spec = spec["floor_hsv"], spec["banana_hsv"]
    floor = ((saturation <= floor_spec["max_saturation"])
             & (value >= floor_spec["min_value"]) & (value <= floor_spec["max_value"]))
    banana = ((hue >= banana_spec["min_hue_degrees"])
              & (hue <= banana_spec["max_hue_degrees"])
              & (saturation >= banana_spec["min_saturation"])
              & (value >= banana_spec["min_value"]))
    selected = floor & ~dilate_disk(banana, spec["banana_exclusion_px"])
    outer = spec["banana_band_outer_px"]
    if outer is not None:
        selected &= dilate_disk(banana, outer)
    return floor, selected


def region_bounds(region, width, height):
    """左上を含み右下を含まない正規化矩形を画素の境界へ広げる。"""
    x0, y0, x1, y1 = region["rect"]
    return (math.floor(x0 * width), math.floor(y0 * height),
            math.ceil(x1 * width), math.ceil(y1 * height))


def measure_grain(png_path, spec):
    """sRGB の PNG を線形輝度へ戻し、選んだ床だけの粒の量と失敗理由を返す。"""
    with Image.open(png_path) as image:
        rgb = image.convert("RGB")
        encoded = np.asarray(rgb, dtype=np.float64) / 255.0
        # 色の分類は線形化せず、Pillow の 8 bit HSV を正規化して行う。
        hsv = np.asarray(rgb.convert("HSV"), dtype=np.float64) / 255.0
    linear = np.where(encoded <= 0.04045, encoded / 12.92,
                      ((encoded + 0.055) / 1.055) ** 2.4)
    luma = linear @ np.array([0.2126, 0.7152, 0.0722])
    height, width = luma.shape
    floor, selected = select_floor_pixels(hsv, spec)

    # 全画面の床だけで箱の平均を取り、隙間やバナナの輝度を混ぜない。
    # 帯の外の床もぼかしには使い、矩形や帯の境界に人工的な縁を作らない。
    radius = spec["blur_radius_px"]
    weights = box_blur(floor.astype(np.float64), radius)
    blurred = np.divide(box_blur(luma * floor, radius), weights,
                        out=np.zeros_like(luma), where=weights > 0)
    high_frequency = luma - blurred

    measurements = []
    for region in spec["regions"]:
        left, top, right, bottom = region_bounds(region, width, height)
        mask = selected[top:bottom, left:right]
        count = int(np.count_nonzero(mask))
        entry = {"name": region["name"], "pixel_count": count, "mean_luminance": None,
                 "high_frequency_stddev": None, "value": None}
        if count < spec["min_floor_pixels"]:
            entry["reason"] = f"測定する床の画素数が不足: {count} < {spec['min_floor_pixels']}"
            measurements.append(entry)
            continue
        mean = float(luma[top:bottom, left:right][mask].mean())
        stddev = float(high_frequency[top:bottom, left:right][mask].std())
        entry["mean_luminance"] = mean
        entry["high_frequency_stddev"] = stddev
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
    for key, defaults in (
            ("floor_hsv", {"max_saturation": 0.15, "min_value": 0.15, "max_value": 0.40}),
            ("banana_hsv", {"min_hue_degrees": 10, "max_hue_degrees": 80,
                            "min_saturation": 0.30, "min_value": 0.05})):
        color = spec.setdefault(key, defaults)
        if not isinstance(color, dict):
            raise ValueError(f"{key} は JSON オブジェクトで指定する")
        for channel, default in defaults.items():
            value = color.setdefault(channel, default)
            upper = 360 if channel.endswith("_hue_degrees") else 1
            if (type(value) not in (int, float) or not math.isfinite(value)
                    or not 0 <= value <= upper):
                raise ValueError(f"{key}.{channel} は 0 以上 {upper} 以下の有限な数値で指定する")
    if spec["floor_hsv"]["min_value"] > spec["floor_hsv"]["max_value"]:
        raise ValueError("floor_hsv の明度の下限が上限を超えている")
    if spec["banana_hsv"]["min_hue_degrees"] > spec["banana_hsv"]["max_hue_degrees"]:
        raise ValueError("banana_hsv の色相の下限が上限を超えている")
    # ぼかし窓の角までバナナを避けるため、除外距離は窓の対角半径以上にする。
    minimum_exclusion = math.ceil(math.sqrt(2) * radius)
    exclusion = spec.setdefault("banana_exclusion_px", max(radius + 1, minimum_exclusion))
    if type(exclusion) is not int or exclusion < minimum_exclusion:
        raise ValueError(f"banana_exclusion_px は {minimum_exclusion} 以上の整数で指定する")
    outer = spec.setdefault("banana_band_outer_px", None)
    if outer is not None and (type(outer) is not int or outer <= exclusion):
        raise ValueError("banana_band_outer_px は除外距離より大きい整数、または null で指定する")
    minimum = spec.setdefault("min_floor_pixels", 3000)
    if type(minimum) is not int or minimum < 1:
        raise ValueError("min_floor_pixels は 1 以上の整数で指定する")
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


def read_camera_path_data(root, capture, view):
    """撮影した視点のパスの時刻とモードを読み取る。"""
    try:
        with (root / capture / "metrics.json").open(encoding="utf-8") as stream:
            metrics = json.load(stream)
    except (OSError, ValueError):
        return None, None

    views = metrics.get("views") if isinstance(metrics, dict) else None
    if not isinstance(views, list):
        return None, None
    entry = next((item for item in views
                  if isinstance(item, dict) and item.get("view") == view), None)
    if entry is None:
        return None, None

    path_time = entry.get("camera_path_time")
    try:
        if type(path_time) not in (int, float) or not math.isfinite(path_time):
            path_time = None
    except OverflowError:
        path_time = None
    path_mode = entry.get("camera_path_mode")
    return path_time, path_mode if isinstance(path_mode, str) else None


def check_grain(root, spec):
    """仕様の置き場の組を読み、領域ごとの比と全体の判定を返す。"""
    validate_spec(spec)
    report = {"view": spec["view"], "numerator": spec["numerator"],
              "denominator": spec["denominator"],
              "camera_path_time": {"numerator": None, "denominator": None},
              "camera_path_mode": {"numerator": None, "denominator": None},
              "camera_path_time_difference": None,
              "max_camera_path_time_difference": 1 / 60,
              "blur_radius_px": spec["blur_radius_px"],
              "floor_hsv": spec["floor_hsv"], "banana_hsv": spec["banana_hsv"],
              "banana_distance_metric": "euclidean", "banana_exclusion_px": spec["banana_exclusion_px"],
              "banana_band_outer_px": spec["banana_band_outer_px"],
              "min_floor_pixels": spec["min_floor_pixels"],
              "regions": [], "result": "fail"}
    time_reasons = []
    for key in ("numerator", "denominator"):
        path_time, path_mode = read_camera_path_data(root, spec[key], spec["view"])
        report["camera_path_time"][key] = path_time
        report["camera_path_mode"][key] = path_mode
        if path_time is None:
            time_reasons.append(f"{spec[key]}: 保存フレームのパスの時刻が無い")
    if not time_reasons:
        numerator_time = report["camera_path_time"]["numerator"]
        denominator_time = report["camera_path_time"]["denominator"]
        difference = abs(numerator_time - denominator_time)
        if isinstance(difference, float) and not math.isfinite(difference):
            difference = abs(int(numerator_time) - int(denominator_time))
        report["camera_path_time_difference"] = difference
        if difference > report["max_camera_path_time_difference"]:
            time_reasons.append(
                f"保存フレームのパスの時刻差が上限を超えた: "
                f"{spec['numerator']}={numerator_time} 秒、{spec['denominator']}={denominator_time} 秒、"
                f"差={difference} 秒")
    if time_reasons:
        report["reasons"] = time_reasons

    measured = {}
    sizes = {}
    failures = {}
    for key in ("numerator", "denominator"):
        png_path = root / spec[key] / (spec["view"] + ".png")
        try:
            if not png_path.is_file():
                raise ValueError(f"画像ファイルが無い: {png_path}")
            measured[key], sizes[key] = measure_grain(png_path, spec)
        except (OSError, ValueError) as error:
            failures[key] = str(error)
    if len(sizes) == 2 and sizes["numerator"] != sizes["denominator"]:
        failures["size"] = "比較する画像の解像度が一致しない"

    for index, region in enumerate(spec["regions"]):
        row = {"name": region["name"], "rect": region["rect"], "numerator": None,
               "denominator": None, "ratio": None, "max_ratio": region["max_ratio"],
               "numerator_pixels": None, "denominator_pixels": None,
               "result": "fail"}
        reasons = list(failures.values())
        for key in ("numerator", "denominator"):
            if key in measured:
                measurement = measured[key][index]
                row[key] = measurement["value"]
                row[key + "_pixels"] = measurement["pixel_count"]
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
    if not time_reasons and all(row["result"] == "pass" for row in report["regions"]):
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
            if key.endswith("_pixels"):
                return "null" if value is None else str(value)
            return "null" if value is None else f"{value:.6f}" if key in ("numerator", "denominator") else f"{value:.3f}"
        print(f"RTGI_GRAIN region={row['name']} numerator={number('numerator')} "
              f"denominator={number('denominator')} ratio={number('ratio')} "
              f"max_ratio={number('max_ratio')} numerator_pixels={number('numerator_pixels')} "
              f"denominator_pixels={number('denominator_pixels')} result={row['result']}")
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
