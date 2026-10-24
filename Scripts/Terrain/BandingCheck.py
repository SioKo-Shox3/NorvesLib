# 表示の 8 bit の階調の段（同じ値が続く平らな帯が等高線のように並ぶ）を、同じ視点の前後の撮影で比べる（SCN-WL-CLOUD-BANDING）。
#
# 使い方:
#   python Scripts/Terrain/BandingCheck.py --before <前の PNG> --after <後の PNG> --rows 0:450 [--min-g 150]
#       [--hdr <後の .hdr.pfm>] [--out <出力先>] [--verify | --no-worse-bands]
# 量（G の 8 bit の値。縦の列を 4 画素おきに見る。--min-g 以上の画素だけ。空や雲の明るい所を選ぶため）:
#   - 平らな帯の長さ: 縦に同じ値が続く区間の長さの平均・95 % 点・最大（段が等高線に見えるのは長い帯の縁）
#   - 隣の差: 縦に隣り合う画素の差の絶対値が 1 段以下の割合と最大
#   - 局所の平均の差: 前後の 9 × 9 の区画の平均の差の平均・最大（雑音で平均の明るさが変わっていないこと）
#   - --hdr を与えると、HDR の縦の隣の相対差が 0 の割合（描画の段がシーンカラーにあるか）も書く
# --out には前後の同じ所（前の平らな帯が最も長い所）を拡大し、G の値の幅を引き伸ばした見比べの PNG（banding_zoom.png）と banding.json を書く。
# --verify: 後の平らな帯の長さの平均と 95 % 点が前より短く、局所の平均の差の平均が 0.5 段以下なら result=pass。
# --no-worse-bands: 後の平らな帯の長さの平均と 95 % 点が前の NO_WORSE_RATIO 倍以内なら result=pass（局所の平均の差は
#   書くだけで合否に使わない。雲の外形を変える直しで、段が増えていないことだけを見る。SCN-WL-CLOUD-SHAPE-NOISE）。
# 終了コード: 合格・書けたら 0、--verify・--no-worse-bands で不合格なら 1、入力が無ければ 2。
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

COLUMN_STEP = 4
BLOCK = 9
MEAN_DIFF_LIMIT = 0.5
NO_WORSE_RATIO = 1.1


def load_png(path):
    return np.asarray(Image.open(path).convert('RGB')).astype(np.float64)


def load_pfm(path):
    with open(path, 'rb') as f:
        f.readline()
        width, height = (int(v) for v in f.readline().split())
        f.readline()
        data = np.frombuffer(f.read(), dtype='<f4').reshape(height, width, 3)
    return data[::-1]


def flat_runs(green, min_g):
    runs = []
    for x in range(0, green.shape[1], COLUMN_STEP):
        column = green[:, x]
        mask = column >= min_g
        length = 0
        for y in range(column.shape[0]):
            if not mask[y]:
                if length > 0:
                    runs.append(length)
                length = 0
                continue
            if length > 0 and column[y] == column[y - 1]:
                length += 1
            else:
                if length > 0:
                    runs.append(length)
                length = 1
        if length > 0:
            runs.append(length)
    return np.array(runs if runs else [0])


def block_mean(values):
    h = values.shape[0] // BLOCK * BLOCK
    w = values.shape[1] // BLOCK * BLOCK
    return values[:h, :w].reshape(h // BLOCK, BLOCK, w // BLOCK, BLOCK).mean(axis=(1, 3))


def measure(green, min_g):
    runs = flat_runs(green, min_g)
    columns = green[:, ::COLUMN_STEP]
    diffs = np.abs(np.diff(columns, axis=0))
    mask = (columns[1:] >= min_g) & (columns[:-1] >= min_g)
    diffs = diffs[mask]
    return {
        'flat_run_mean': round(float(runs.mean()), 3),
        'flat_run_p95': float(np.percentile(runs, 95)),
        'flat_run_max': int(runs.max()),
        'adjacent_le1_ratio': round(float(np.mean(diffs <= 1.0)), 4) if diffs.size else 0.0,
        'adjacent_max': int(diffs.max()) if diffs.size else 0,
    }


def write_zoom(before, after, rows, min_g, out_path):
    # 段の見えやすい所: 前の撮影で平らな帯が最も長い所を中心に、幅 240 × 高さ 135 を 4 倍に拡大する。
    # G を中央値の ±6 段の幅で引き伸ばした灰色で並べる（1 段の平らな帯が縞として見える）。
    y0, y1 = rows
    green = before[y0:y1, :, 1]
    best = (0, 0, -1)
    for x in range(120, green.shape[1] - 120, 8):
        column = green[:, x]
        length = 1
        for y in range(1, column.shape[0]):
            if column[y] >= min_g and column[y] == column[y - 1]:
                length += 1
                if length > best[2]:
                    best = (x, y - length // 2, length)
            else:
                length = 1
    cx = best[0] - 120
    cy = int(np.clip(y0 + best[1] - 67, y0, max(y0, y1 - 135)))
    center = float(np.median(before[cy:cy + 135, cx:cx + 240, 1]))
    panels = []
    for image in (before, after):
        crop = image[cy:cy + 135, cx:cx + 240, 1]
        stretched = np.clip((crop - (center - 6.0)) / 12.0, 0.0, 1.0) * 255.0
        panels.append(Image.fromarray(stretched.astype(np.uint8)).resize((960, 540), Image.NEAREST))
    sheet = Image.new('L', (1920 + 16, 540), 0)
    sheet.paste(panels[0], (0, 0))
    sheet.paste(panels[1], (960 + 16, 0))
    sheet.save(out_path)
    return {'x': int(cx), 'y': int(cy), 'width': 240, 'height': 135, 'scale': 4, 'center_g': center, 'range': 12}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--before', required=True)
    parser.add_argument('--after', required=True)
    parser.add_argument('--rows', default='0:450')
    parser.add_argument('--min-g', type=float, default=150.0)
    parser.add_argument('--hdr')
    parser.add_argument('--out')
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--no-worse-bands', action='store_true')
    args = parser.parse_args()

    for path in (args.before, args.after):
        if not os.path.exists(path):
            print(f'BANDING_CHECK result=fail reason=missing path={path}')
            return 2
    y0, y1 = (int(v) for v in args.rows.split(':'))
    before = load_png(args.before)
    after = load_png(args.after)
    result = {
        'rows': [y0, y1],
        'min_g': args.min_g,
        'before': measure(before[y0:y1, :, 1], args.min_g),
        'after': measure(after[y0:y1, :, 1], args.min_g),
    }
    mean_diff = np.abs(block_mean(before[y0:y1, :, 1]) - block_mean(after[y0:y1, :, 1]))
    result['block_mean_diff'] = {'mean': round(float(mean_diff.mean()), 4), 'max': round(float(mean_diff.max()), 3)}
    if args.hdr and os.path.exists(args.hdr):
        hdr = load_pfm(args.hdr)[y0:y1]
        luminance = 0.2126 * hdr[..., 0] + 0.7152 * hdr[..., 1] + 0.0722 * hdr[..., 2]
        columns = luminance[:, ::COLUMN_STEP]
        relative = np.abs(np.diff(columns, axis=0)) / np.maximum(columns[1:], 1e-6)
        result['hdr_zero_step_ratio'] = round(float(np.mean(relative < 1e-4)), 4)
        result['hdr_median_relative_step'] = round(float(np.median(relative)), 5)
    if args.out:
        os.makedirs(args.out, exist_ok=True)
        result['zoom'] = write_zoom(before, after, (y0, y1), args.min_g, os.path.join(args.out, 'banding_zoom.png'))
        with open(os.path.join(args.out, 'banding.json'), 'w', encoding='utf-8') as f:
            json.dump(result, f, ensure_ascii=False, indent=2)

    for name in ('before', 'after'):
        m = result[name]
        print(f"BANDING_CHECK {name} flat_run_mean={m['flat_run_mean']} flat_run_p95={m['flat_run_p95']} "
              f"flat_run_max={m['flat_run_max']} adjacent_le1_ratio={m['adjacent_le1_ratio']} adjacent_max={m['adjacent_max']}")
    print(f"BANDING_CHECK block_mean_diff mean={result['block_mean_diff']['mean']} max={result['block_mean_diff']['max']}")
    if 'hdr_zero_step_ratio' in result:
        print(f"BANDING_CHECK hdr zero_step_ratio={result['hdr_zero_step_ratio']} "
              f"median_relative_step={result['hdr_median_relative_step']}")
    if args.no_worse_bands:
        passed = (result['after']['flat_run_mean'] <= result['before']['flat_run_mean'] * NO_WORSE_RATIO and
                  result['after']['flat_run_p95'] <= result['before']['flat_run_p95'] * NO_WORSE_RATIO)
        print(f"BANDING_CHECK no_worse_bands ratio={NO_WORSE_RATIO} "
              f"flat_run_mean_limit={round(result['before']['flat_run_mean'] * NO_WORSE_RATIO, 4)} "
              f"flat_run_p95_limit={round(result['before']['flat_run_p95'] * NO_WORSE_RATIO, 4)}")
        print(f"BANDING_CHECK result={'pass' if passed else 'fail'}")
        return 0 if passed else 1
    if not args.verify:
        return 0
    passed = (result['after']['flat_run_mean'] < result['before']['flat_run_mean'] and
              result['after']['flat_run_p95'] < result['before']['flat_run_p95'] and
              result['block_mean_diff']['mean'] <= MEAN_DIFF_LIMIT)
    print(f"BANDING_CHECK result={'pass' if passed else 'fail'}")
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
