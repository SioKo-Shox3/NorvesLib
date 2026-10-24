# Battleship Rock の尾根の刻み（Tools/AssetCook/TerrainCliffDetail.cpp）の、刻みの割合（マスク）の境で近景の面の高さが
# 跳ばないかを確かめる。面の高さは AssetCook の `--sample-terrain-cliff-surface <points-file>`（焼きが頂点に使うのと
# 同じ式）で引き、このスクリプトは式を写さない。
#
# 使い方:
#   python Scripts/Terrain/CheckCarveContinuity.py [--asset-cook <AssetCook.exe>] [--out <report.json>] [--verify]
# 1. 刻みの届く範囲の中心から放射状に粗く（COARSE_STEP_M）マスクを引き、マスクが 1 から 1 未満へ変わる所を境とする
# 2. 境ごとに、放射の向きに ±LINE_HALF_M の線を FINE_STEP_M の間隔で引く（境を横切る線）
# 3. 線の上でマスク 0.99 以上 1 未満の点と、隣の標本（FINE_STEP_M 先。NEIGHBOR_M 以内）のマスク 1 の点の高さの差を測る。
#    自然の急な崖（1 m あたり 10 m を超える）では NEIGHBOR_M や 6.25 cm の幅でも高さが 0.5 m 以上変わるので、段差と傾きを
#    分けるため細かい間隔の隣の標本どうしで見る（式の切り替わりの段差は間隔を細かくしても残る）。
#    参考に NEIGHBOR_M 離れた点との差と、内側の同じ幅の差（面の傾きの分）を並べて出す
# 合否（--verify）: 線が MIN_LINES 本以上、差の最大が MAX_STEP_M 以下、刻みの版 7 で段差の出ていた 2 点（EXAMPLE_POINTS）の差が MAX_STEP_M 以下。
# 高さは押し出す前の面（刻んだ高さ）で見る。地層の押し出した後の高さの差は参考に出す。
# 終了コード: --verify で合格なら 0、不合格なら 1。--verify なしは値を出すだけで 0。
import argparse
import json
import math
import os
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_ASSET_COOK = os.path.join(REPO_ROOT, "build", "Tools", "AssetCook", "RelWithDebInfo", "AssetCook.exe")
DEFAULT_REPORT = os.path.join(REPO_ROOT, ".harness", "runs", "scene-capture", "SCN-WL-CLIFF-CARVE-CONTINUITY",
                              "continuity.json")

# 放射の本数と、粗く引く間隔[m]
RAY_COUNT = 360
COARSE_STEP_M = 1.0
# 境を横切る線の片側の長さと、線の上の間隔[m]
LINE_HALF_M = 0.75
FINE_STEP_M = 1.0 / 64.0
# マスクの境の近くとみなす下限と、比べる隣の点までの水平の距離[m]
MASK_LOW = 0.99
NEIGHBOR_M = 0.25
# 合否の基準
MIN_LINES = 200
MAX_STEP_M = 0.5
# 刻みの版 7 でマスクの境の段差（約 20.29 m）が出ていた 2 点（世界座標 X, Z）
EXAMPLE_POINTS = [(-1875.25, -1245.1875), (-1875.25, -1245.125)]


def query(asset_cook, points):
    """点の一覧を AssetCook に渡し、範囲と点ごとの (mask, base, px, py, pz) を返す（格子の外は None）"""
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as handle:
        for x, z in points:
            handle.write(f"{x!r} {z!r}\n")
        path = handle.name
    try:
        result = subprocess.run([asset_cook, "--sample-terrain-cliff-surface", path], capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
    finally:
        os.remove(path)
    if result.returncode != 0:
        raise RuntimeError(f"AssetCook が失敗しました（{result.returncode}）: {result.stderr.strip()}")
    extent = None
    samples = []
    for line in result.stdout.splitlines():
        if line.startswith("carve_extent="):
            extent = [float(value) for value in line.split("=", 1)[1].split(",")]
        elif line.startswith("point="):
            fields = line.split("=", 1)[1].split(",")
            if fields[2] == "out_of_range":
                samples.append(None)
            else:
                samples.append(tuple(float(value) for value in fields[2:7]))
    if extent is None or len(samples) != len(points):
        raise RuntimeError("AssetCook の出力に範囲か点の数が足りません")
    return extent, samples


def find_crossings(asset_cook):
    """放射の上でマスクが 1 から 1 未満へ変わる所（境の点と放射の向き）の一覧"""
    extent, _ = query(asset_cook, [EXAMPLE_POINTS[0]])
    min_x, max_x, min_z, max_z = extent
    center_x = 0.5 * (min_x + max_x)
    center_z = 0.5 * (min_z + max_z)
    reach = math.hypot(max_x - min_x, max_z - min_z) * 0.5
    steps = int(math.ceil(reach / COARSE_STEP_M)) + 1
    points = []
    rays = []
    for ray in range(RAY_COUNT):
        angle = 2.0 * math.pi * ray / RAY_COUNT
        dx = math.cos(angle)
        dz = math.sin(angle)
        first = len(points)
        for step in range(steps):
            x = center_x + dx * step * COARSE_STEP_M
            z = center_z + dz * step * COARSE_STEP_M
            if min_x <= x <= max_x and min_z <= z <= max_z:
                points.append((x, z))
        rays.append((first, len(points), dx, dz))
    _, samples = query(asset_cook, points)
    crossings = []
    for first, end, dx, dz in rays:
        for index in range(first, end - 1):
            a = samples[index]
            b = samples[index + 1]
            if a is None or b is None:
                continue
            if (a[0] >= 1.0) != (b[0] >= 1.0):
                x = 0.5 * (points[index][0] + points[index + 1][0])
                z = 0.5 * (points[index][1] + points[index + 1][1])
                crossings.append((x, z, dx, dz))
    return extent, crossings


def measure(asset_cook, crossings):
    """境を横切る線ごとに、マスク 0.99〜1 未満の点と、隣（線の上で次の標本）のマスク 1 の点の高さの差を測る。
    参考に、マスク 0.99〜1 未満の点から NEIGHBOR_M 離れたマスク 1 の点との差と、そのマスク 1 の点からさらに内側へ
    NEIGHBOR_M の点との差（同じ幅の面の傾きの分）も出す"""
    count = int(round(LINE_HALF_M / FINE_STEP_M))
    offsets = [index * FINE_STEP_M for index in range(-count, count + 1)]
    reach = int(round(NEIGHBOR_M / FINE_STEP_M))
    points = []
    for x, z, dx, dz in crossings:
        for offset in offsets:
            points.append((x + dx * offset, z + dz * offset))
    _, samples = query(asset_cook, points)
    lines = []
    for line_index, (x, z, dx, dz) in enumerate(crossings):
        base = line_index * len(offsets)
        line = samples[base:base + len(offsets)]
        worst = None
        wide = None
        for index in range(len(line) - 1):
            for edge, inner, direction in ((index, index + 1, 1), (index + 1, index, -1)):
                sample = line[edge]
                neighbor = line[inner]
                if sample is None or neighbor is None or not (MASK_LOW <= sample[0] < 1.0) or neighbor[0] < 1.0:
                    continue
                step = abs(sample[1] - neighbor[1])
                if worst is None or step > worst["step_m"]:
                    worst = {
                        "x": points[base + edge][0],
                        "z": points[base + edge][1],
                        "mask": sample[0],
                        "neighbor_mask": neighbor[0],
                        "distance_m": FINE_STEP_M,
                        "step_m": step,
                        "displaced_step_m": abs(sample[3] - neighbor[3]),
                    }
                # 参考: NEIGHBOR_M 離れたマスク 1 の点との差と、内側の同じ幅の差
                far = edge + direction * reach
                further = far + direction * reach
                if 0 <= further < len(line) and all(line[i] is not None for i in (far, further)) and                         line[far][0] >= 1.0 and line[further][0] >= 1.0:
                    across = abs(sample[1] - line[far][1])
                    inside = abs(line[far][1] - line[further][1])
                    if wide is None or across > wide["across_m"]:
                        wide = {"across_m": across, "inside_m": inside}
        if worst is not None:
            lines.append({"crossing": [x, z], "worst": worst, "wide": wide})
    return lines


def main():
    parser = argparse.ArgumentParser(description="Battleship Rock の刻みのマスクの境の高さの連続を確かめる")
    parser.add_argument("--asset-cook", default=DEFAULT_ASSET_COOK)
    parser.add_argument("--out", default=DEFAULT_REPORT)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    asset_cook = os.path.abspath(args.asset_cook)
    if not os.path.isfile(asset_cook):
        print(f"error: AssetCook がありません: {asset_cook}")
        return 1

    extent, crossings = find_crossings(asset_cook)
    lines = measure(asset_cook, crossings)
    _, example = query(asset_cook, EXAMPLE_POINTS)
    if example[0] is None or example[1] is None:
        print("error: 例の 2 点が格子の外です")
        return 1
    example_step = abs(example[0][1] - example[1][1])
    steps = sorted(line["worst"]["step_m"] for line in lines)
    max_step = steps[-1] if steps else float("nan")
    worst_lines = sorted(lines, key=lambda line: line["worst"]["step_m"], reverse=True)[:10]
    wides = [line["wide"] for line in lines if line["wide"] is not None]
    wide_worst = max(wides, key=lambda wide: wide["across_m"], default=None)
    wide_over = sum(1 for wide in wides if wide["across_m"] > MAX_STEP_M)
    wide_over_flat = sum(1 for wide in wides if wide["across_m"] > MAX_STEP_M and wide["inside_m"] <= MAX_STEP_M)

    failures = []
    if len(lines) < MIN_LINES:
        failures.append(f"境を横切る線が {len(lines)} 本（要る {MIN_LINES} 本以上）")
    if not (max_step <= MAX_STEP_M):
        over = sum(1 for step in steps if step > MAX_STEP_M)
        failures.append(f"マスクの境の高さの差の最大 {max_step:.3f} m（許容 {MAX_STEP_M} m、超えた線 {over} 本）")
    if not (example_step <= MAX_STEP_M):
        failures.append(f"例の 2 点の高さの差 {example_step:.3f} m（許容 {MAX_STEP_M} m）")

    report = {
        "carve_extent": extent,
        "crossings": len(crossings),
        "lines": len(lines),
        "max_step_m": max_step,
        "median_step_m": steps[len(steps) // 2] if steps else None,
        "max_displaced_step_m": max((line["worst"]["displaced_step_m"] for line in lines), default=None),
        "example": [
            {"x": EXAMPLE_POINTS[index][0], "z": EXAMPLE_POINTS[index][1], "mask": example[index][0],
             "height": example[index][1]} for index in range(2)
        ],
        "example_step_m": example_step,
        "wide_reference": {"lines": len(wides), "max": wide_worst, "over_limit": wide_over,
                           "over_limit_where_inside_within_limit": wide_over_flat},
        "worst_lines": worst_lines,
        "failures": failures,
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)

    print(f"carve_extent={extent[0]:.3f},{extent[1]:.3f},{extent[2]:.3f},{extent[3]:.3f}")
    print(f"crossings={len(crossings)} lines={len(lines)}")
    print(f"max_step_m={max_step:.4f} median_step_m={report['median_step_m']:.4f}")
    print(f"max_displaced_step_m={report['max_displaced_step_m']:.4f}")
    for index in range(2):
        print(f"example[{index}] x={EXAMPLE_POINTS[index][0]} z={EXAMPLE_POINTS[index][1]} mask={example[index][0]:.6f} "
              f"height={example[index][1]:.3f}")
    print(f"example_step_m={example_step:.4f}")
    if wide_worst is not None:
        print(f"reference_{NEIGHBOR_M}m: lines={len(wides)} max_across_m={wide_worst['across_m']:.4f} "
              f"inside_m={wide_worst['inside_m']:.4f} over_limit={wide_over} over_limit_where_inside_within_limit={wide_over_flat}")
    for line in worst_lines[:5]:
        worst = line["worst"]
        print(f"worst x={worst['x']:.4f} z={worst['z']:.4f} mask={worst['mask']:.6f} distance_m={worst['distance_m']:.4f} "
              f"step_m={worst['step_m']:.4f}")
    print(f"report={os.path.abspath(args.out)}")
    for failure in failures:
        print(f"failure: {failure}")
    if args.verify:
        print(f"result={'pass' if not failures else 'fail'}")
        return 0 if not failures else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
