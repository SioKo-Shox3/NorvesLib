#!/usr/bin/env python3
"""草原の距離スイープと近接画像から、草の被覆と明るさを判定する。"""

import argparse
import csv
import math
from pathlib import Path
import re
import sys

import cv2
import numpy as np
from PIL import Image


# 緑の画素は、暗すぎず G が R・B よりそれぞれ指定値以上高い画素とする。
GREEN_MIN = 35
GREEN_EXCESS = 8
# スイープの判定窓は画面幅の 10〜90%、高さの 55〜100%。空を避けて草と地面を含める。
GRASS_WINDOW = (0.10, 0.55, 0.90, 1.00)
# 表示された RGB の輝度係数。平均は緑の画素だけでなく判定窓全体で求める。
LUMA_WEIGHTS = (0.2126, 0.7152, 0.0722)
# 前の画像の輝度がゼロのときの相対差の分母。
LUMA_EPSILON = 1.0e-6
# 距離は台本の撮影名 grass-010〜grass-120 に対応する。
SWEEP_MIN_METERS = 10
SWEEP_MAX_METERS = 120
SWEEP_NAME = re.compile(r"^(?:[A-Za-z0-9_-]+\.)?grass-(\d{3})\.png$", re.IGNORECASE)
DEFAULT_THRESHOLDS = (Path(__file__).resolve().parents[2] / "Test/Core/Rendering/Thresholds"
                      / "RenderingValidation/GrassLodSweep.tsv")
THRESHOLD_KEYS = ("max_coverage_step", "max_relative_luminance_step", "max_near_component_fraction")


class ArgumentParser(argparse.ArgumentParser):
    def format_help(self):
        return super().format_help().replace("usage:", "使い方:", 1)

    def format_usage(self):
        return super().format_usage().replace("usage:", "使い方:", 1)

    def error(self, message):
        self.print_usage(sys.stderr)
        self.exit(1, f"エラー: {message}\n")


def read_thresholds(path):
    """キー・値・説明の TSV を読み、判定に使う値を確かめる。"""
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames != ["key", "value", "description"]:
            raise ValueError(f"しきい値の列が key・value・description ではありません: {path}")
        values = {}
        for row in reader:
            key = row.get("key")
            if not key or row.get("value") is None or row.get("description") is None:
                raise ValueError(f"しきい値の行が不正です: {path}")
            if key in values:
                raise ValueError(f"しきい値のキーが重複しています: {key}")
            try:
                value = float(row["value"])
            except ValueError as error:
                raise ValueError(f"しきい値が数値ではありません: {key}") from error
            if not math.isfinite(value) or value < 0:
                raise ValueError(f"しきい値は有限の 0 以上の値にしてください: {key}")
            values[key] = value
    missing = [key for key in THRESHOLD_KEYS if key not in values]
    if missing:
        raise ValueError(f"しきい値のキーがありません: {', '.join(missing)}")
    for key in ("max_coverage_step", "max_near_component_fraction"):
        if values[key] > 1:
            raise ValueError(f"割合のしきい値は 1 以下にしてください: {key}")
    return values


def load_image(path):
    """画像を RGB の配列として読む。"""
    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"), dtype=np.uint8)


def green_mask(image):
    """緑と見なす画素のマスクを返す。"""
    rgb = image.astype(np.int16)
    red, green, blue = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
    return ((green >= GREEN_MIN) & (green - red >= GREEN_EXCESS)
            & (green - blue >= GREEN_EXCESS))


def sweep_paths(directory):
    """距離ごとに 1 枚だけあることを確認し、距離の順に返す。"""
    if not directory.is_dir():
        raise ValueError(f"スイープのフォルダがありません: {directory}")
    paths = {}
    for path in directory.iterdir():
        match = SWEEP_NAME.fullmatch(path.name)
        if not path.is_file() or match is None:
            continue
        distance = int(match.group(1))
        if not SWEEP_MIN_METERS <= distance <= SWEEP_MAX_METERS:
            raise ValueError(f"撮影距離が範囲外です: {path.name}")
        if distance in paths:
            raise ValueError(f"撮影距離が重複しています: {distance} m")
        paths[distance] = path
    missing = [distance for distance in range(SWEEP_MIN_METERS, SWEEP_MAX_METERS + 1)
               if distance not in paths]
    if missing:
        example = ", ".join(str(distance) for distance in missing[:10])
        suffix = " など" if len(missing) > 10 else ""
        raise ValueError(f"スイープの画像が {len(missing)} 枚ありません: {example}{suffix} m")
    return sorted(paths.items())


def measure_sweep_image(image):
    """判定窓の緑の被覆率と、窓全体の表示輝度の平均を返す。"""
    height, width = image.shape[:2]
    left, top, right, bottom = GRASS_WINDOW
    window = image[int(height * top):int(height * bottom), int(width * left):int(width * right)]
    if window.size == 0:
        raise ValueError("画像が小さすぎて判定窓を作れません")
    coverage = float(np.mean(green_mask(window)))
    means = window.mean(axis=(0, 1)) / 255.0
    luminance = float(np.dot(means, LUMA_WEIGHTS))
    return coverage, luminance


def check_sweep(directory, thresholds):
    paths = sweep_paths(directory)
    measurements = []
    expected_shape = None
    for distance, path in paths:
        image = load_image(path)
        if expected_shape is None:
            expected_shape = image.shape
        elif image.shape != expected_shape:
            raise ValueError(f"スイープの画像の大きさが一致しません: {path.name}")
        coverage, luminance = measure_sweep_image(image)
        measurements.append((distance, coverage, luminance))

    print("\n距離スイープ（被覆率は判定窓に占める緑画素の割合）")
    print("距離[m] | 被覆率 | 平均輝度 | 被覆率の差 | 輝度の相対差")
    coverage_steps = []
    luminance_steps = []
    previous = None
    for distance, coverage, luminance in measurements:
        if previous is None:
            print(f"{distance:7d} | {coverage:6.4f} | {luminance:8.4f} |          - |            -")
        else:
            old_distance, old_coverage, old_luminance = previous
            coverage_delta = abs(coverage - old_coverage)
            luminance_delta = abs(luminance - old_luminance) / max(old_luminance, LUMA_EPSILON)
            coverage_steps.append((coverage_delta, old_distance, distance))
            luminance_steps.append((luminance_delta, old_distance, distance))
            print(f"{distance:7d} | {coverage:6.4f} | {luminance:8.4f} | "
                  f"{coverage_delta:10.4f} | {luminance_delta:12.4f}")
        previous = (distance, coverage, luminance)

    coverage_max, coverage_from, coverage_to = max(coverage_steps)
    luminance_max, luma_from, luma_to = max(luminance_steps)
    coverage_ok = coverage_max <= thresholds["max_coverage_step"]
    luminance_ok = luminance_max <= thresholds["max_relative_luminance_step"]
    has_green = any(coverage > 0 for _, coverage, _ in measurements)
    print("\n判定項目 | 最大値 | 上限 | 対象距離[m] | 結果")
    print(f"被覆率の差 | {coverage_max:.6f} | {thresholds['max_coverage_step']:.6f} | "
          f"{coverage_from}→{coverage_to} | {'合格' if coverage_ok else '不合格'}")
    print(f"輝度の相対差 | {luminance_max:.6f} | {thresholds['max_relative_luminance_step']:.6f} | "
          f"{luma_from}→{luma_to} | {'合格' if luminance_ok else '不合格'}")
    if not has_green:
        print("不合格: すべての判定窓で緑の画素が見つかりません")
    return coverage_ok and luminance_ok and has_green


def check_near(path, thresholds):
    image = load_image(path)
    # 斜めに接する画素も同じ成分とする 8 連結で、最大の緑の塊を数える。
    mask = green_mask(image).astype(np.uint8)
    count, _, stats, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
    largest = int(stats[1:, cv2.CC_STAT_AREA].max()) if count > 1 else 0
    fraction = largest / mask.size
    limit = thresholds["max_near_component_fraction"]
    passed = fraction <= limit
    print("\n近接画像（最大の緑の連結成分 ÷ 画面全体）")
    print("判定項目 | 画素数 | 割合 | 上限 | 結果")
    print(f"最大連結成分 | {largest} | {fraction:.6f} | {limit:.6f} | {'合格' if passed else '不合格'}")
    return passed


def main():
    parser = ArgumentParser(description="草の距離スイープと近接画像を判定する", add_help=False)
    options = parser.add_argument_group("オプション")
    options.add_argument("-h", "--help", action="help", help="使い方を表示して終了する")
    options.add_argument("--sweep", type=Path, help="grass-010〜grass-120 の PNG があるフォルダ")
    options.add_argument("--near", type=Path, help="近接の画像")
    options.add_argument("--thresholds", type=Path, default=DEFAULT_THRESHOLDS, help="しきい値の TSV")
    args = parser.parse_args()
    if args.sweep is None and args.near is None:
        parser.error("--sweep または --near を指定してください")
    try:
        thresholds = read_thresholds(args.thresholds)
        results = []
        if args.sweep is not None:
            results.append(check_sweep(args.sweep, thresholds))
        if args.near is not None:
            results.append(check_near(args.near, thresholds))
        passed = all(results)
    except (OSError, ValueError, csv.Error) as error:
        print(f"不合格: {error}", file=sys.stderr)
        passed = False
    print(f"\n総合判定: {'合格' if passed else '不合格'}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
