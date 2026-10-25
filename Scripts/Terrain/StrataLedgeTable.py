# 地層の境の高さの表（SCN-WL-CLIFF-GEN-LEDGES）。写真の注釈の棚の線（ShapeAnnotations.json）を、形の物差しと同じ外形の
# 対応でずらして撮影に置き、線の上の画素の深度とカメラから世界の点に戻す。棚の線ごとの高さから、谷の地層の緩い傾き
# （平面 y = 高さ + 傾き x・z）と境の高さを当てはめ、崖の生成（Tools/AssetCook/TerrainCliffDetail.cpp の kStrataLedges）へ
# 写す表を書く。
#
#   python Scripts/Terrain/StrataLedgeTable.py --capture <photo>=<-Aov の撮影の置き場> [...] [--out <表の JSON>]
#
# 撮影は形の物差しの撮影（CaptureScene.ps1 -Hdr -Aov）。<photo> は far_01・far_02・cliff_01・cliff_02・ground_02。

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from CompareReference import PHOTOS, REPO_ROOT  # noqa: E402
from ReferenceCheck import read_pfm_any  # noqa: E402
from ShapeMeasure import align_cliff, load_annotations, to_pixels  # noqa: E402

# 同じ境とみなす高さの差（m。傾きを除いた後）
kClusterMeters = 3.0
# 傾きの大きさの上限（m/km）。谷の地層は緩く傾くだけなので、点が少なくても傾きが暴れないようにする
kMaxDipPerKm = 15.0
# 傾きの探し方の刻み（m/km）
kDipStepPerKm = 0.5


def world_points(depth, camera, pixels):
    """画素の並び（x, y）の世界の点（N × 3）。深度は視線の軸に沿った距離"""
    height, width = depth.shape
    forward = np.asarray(camera["forward"], dtype=np.float64)
    right = np.asarray(camera["right"], dtype=np.float64)
    up = np.asarray(camera["up"], dtype=np.float64)
    position = np.asarray(camera["position"], dtype=np.float64)
    tan_half = np.tan(np.radians(camera["vertical_fov_degrees"]) * 0.5)
    aspect = float(camera["aspect_ratio"])
    points = []
    for px, py in pixels:
        ix, iy = int(round(px)), int(round(py))
        if not (0 <= ix < width and 0 <= iy < height):
            continue
        d = float(depth[iy, ix])
        if not np.isfinite(d) or d <= 0.0:
            continue
        nx = (2.0 * (ix + 0.5) / width - 1.0) * tan_half * aspect
        ny = (1.0 - 2.0 * (iy + 0.5) / height) * tan_half
        points.append(position + (forward + right * nx + up * ny) * d)
    return np.asarray(points).reshape(-1, 3)


def sample_segment(start, end):
    length = max(1, int(np.ceil(np.hypot(end[0] - start[0], end[1] - start[1]))))
    t = np.linspace(0.0, 1.0, length + 1)
    return [(start[0] + (end[0] - start[0]) * s, start[1] + (end[1] - start[1]) * s) for s in t]


def measure_lines(captures):
    """棚の線ごとの記録（写真・崖・世界の点の中央値・高さの散らばり）"""
    annotations = load_annotations()
    views = {p["id"]: p["view"] for p in PHOTOS}
    lines = []
    for photo_id, capture_dir in captures.items():
        view = views[photo_id]
        depth = read_pfm_any(os.path.join(capture_dir, view + ".aov.depth.pfm"))[:, :, 0].astype(np.float64)
        with open(os.path.join(capture_dir, view + ".aov.json"), encoding="utf-8") as file:
            camera = json.load(file)["camera"]
        terrain_path = os.path.join(capture_dir, view + ".aov.terrain_id.pfm")
        terrain = None
        if os.path.isfile(terrain_path):
            terrain = np.rint(read_pfm_any(terrain_path)[:, :, 0]) == 1
        size = (depth.shape[1], depth.shape[0])
        for cliff in annotations[photo_id]["cliffs"]:
            if not cliff.get("shelves"):
                continue
            alignment, reason = align_cliff(cliff, depth, size)
            if alignment is None:
                print(f"STRATA_SKIP photo={photo_id} cliff=\"{cliff['name']}\" reason={reason}")
                continue
            shift = alignment["shift"]
            limited = np.where(depth <= float(cliff["depth_max_m"]), depth, np.inf)
            if terrain is not None:
                limited = np.where(terrain, limited, np.inf)
            for index, (start, end) in enumerate(cliff["shelves"]):
                a, b = to_pixels([start, end], size, shift)
                points = world_points(limited, camera, sample_segment(a, b))
                if len(points) < 4:
                    print(f"STRATA_SKIP photo={photo_id} cliff=\"{cliff['name']}\" shelf={index} reason=点が {len(points)}")
                    continue
                centre = np.median(points, axis=0)
                lines.append({"photo": photo_id, "cliff": cliff["name"], "shelf": index, "points": int(len(points)),
                              "x": float(centre[0]), "z": float(centre[2]), "height_m": float(centre[1]),
                              "height_iqr_m": float(np.subtract(*np.percentile(points[:, 1], [75, 25]))),
                              "shift_px": [float(shift[0]), float(shift[1])]})
    return lines


def cluster(values, tolerance):
    """1 次元の値の並びを、隣どうしの差が tolerance 以下の塊に分ける（添字の塊の並び）"""
    order = np.argsort(values)
    groups = [[int(order[0])]]
    for i in order[1:]:
        if values[i] - values[groups[-1][-1]] <= tolerance:
            groups[-1].append(int(i))
        else:
            groups.append([int(i)])
    return groups


def fit_plane(lines, x0, z0):
    """傾き（m/km の x・z）を探し、傾きを除いた高さの塊の散らばりが最小のものを採る。崖ごとの差の自由度を
    減らすため、2 つ以上の崖から来た点を含む塊の数が多いものを先に選ぶ"""
    xs = np.array([line["x"] - x0 for line in lines])
    zs = np.array([line["z"] - z0 for line in lines])
    ys = np.array([line["height_m"] for line in lines])
    cliffs = [line["photo"] + "/" + line["cliff"] for line in lines]
    best = None
    steps = np.arange(-kMaxDipPerKm, kMaxDipPerKm + 1e-9, kDipStepPerKm)
    for bx in steps:
        for bz in steps:
            flat = ys - (bx * xs + bz * zs) * 1.0e-3
            groups = cluster(flat, kClusterMeters)
            shared = sum(1 for g in groups if len({cliffs[i] for i in g}) >= 2)
            spread = sum(float(np.sum((flat[g] - flat[g].mean()) ** 2)) for g in groups)
            key = (-shared, len(groups), spread, abs(bx) + abs(bz))
            if best is None or key < best[0]:
                best = (key, bx, bz, groups, flat)
    return best


def main():
    parser = argparse.ArgumentParser(description="写真の棚の線から地層の境の高さの表を作る")
    parser.add_argument("--capture", action="append", required=True, help="<photo>=<撮影の置き場>")
    parser.add_argument("--out", default=os.path.join(REPO_ROOT, "Scripts", "Terrain", "StrataLedges.json"))
    args = parser.parse_args()
    captures = {}
    for item in args.capture:
        photo_id, _, path = item.partition("=")
        captures[photo_id] = path
    lines = measure_lines(captures)
    for line in lines:
        print(f"STRATA_LINE photo={line['photo']} cliff=\"{line['cliff']}\" shelf={line['shelf']} points={line['points']} "
              f"x={line['x']:.0f} z={line['z']:.0f} height={line['height_m']:.2f} iqr={line['height_iqr_m']:.2f}")
    x0 = float(np.mean([line["x"] for line in lines]))
    z0 = float(np.mean([line["z"] for line in lines]))
    key, bx, bz, groups, flat = fit_plane(lines, x0, z0)
    boundaries = []
    for group in sorted(groups, key=lambda g: float(flat[g].mean())):
        heights = flat[group]
        boundaries.append({"height_m": round(float(heights.mean()), 2),
                           "residual_max_m": round(float(np.max(np.abs(heights - heights.mean()))), 2),
                           "lines": [f"{lines[i]['photo']}/{lines[i]['cliff']}/{lines[i]['shelf']}" for i in group]})
    table = {"origin_xz": [round(x0, 1), round(z0, 1)], "dip_m_per_km": [bx, bz], "cluster_m": kClusterMeters,
             "captures": {k: os.path.relpath(v, REPO_ROOT).replace("\\", "/") for k, v in captures.items()},
             "boundaries": boundaries, "lines": lines}
    print(f"STRATA_PLANE origin=({x0:.1f}, {z0:.1f}) dip_x={bx:.1f} dip_z={bz:.1f} m/km shared={-key[0]} groups={len(groups)}")
    for boundary in boundaries:
        print(f"STRATA_BOUNDARY height={boundary['height_m']:.2f} residual_max={boundary['residual_max_m']:.2f} "
              f"lines={','.join(boundary['lines'])}")
    with open(args.out, "w", encoding="utf-8-sig", newline="\r\n") as file:
        json.dump(table, file, ensure_ascii=False, indent=2)
        file.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
