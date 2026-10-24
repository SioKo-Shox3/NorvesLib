# Battleship Rock の尾根を写真の段（平らな頂・縦の壁・右の段・左の縁）に削る、地形の焼き込みの刻みの表を書く台本
# （SCN-WL-CLIFF-SCAN-NEAR）。削る前の ref_cliff_01 の撮影（深度と視点）と、写真 cliff_01 の注釈の外形
# （Scripts/Terrain/ShapeAnnotations.json）から、列ごとに撮影の外形の最も上の画素の世界の位置（尾根の点）と、同じ水平の距離で
# 写真の外形の行を通る視線の高さ（目標の高さ）を求める。尾根の点から視点の反対へ RADIUS 寄せた点を中心とする半径 RADIUS の
# 円（前の縁が尾根の点を通る）を 1 つの刻みとし、目標が上なら高さを目標まで上げ（max）、下なら目標まで下げる（min）。
# TerrainCliffDetail.cpp は中心を列の順に並べた折れ線（前後を均す）として扱い、格子の点は最も近い線分の両端の刻みを
# 線分に沿って補間して受ける（円ごとの丸い柱と最も近い円の境の縦の段を作らない）。
# 表は Tools/AssetCook/TerrainCliffCarveBattleship.h に書き、TerrainCliffDetail.cpp が近景の範囲 0 の高さに掛ける。
#
# --shelves を付けると、写真の注釈の棚の線（ずらした量を足した位置）を撮影の深度で世界へ戻した点の並びを
# Tools/AssetCook/TerrainCliffShelfBattleship.h に書く（尾根の刻みの表は書かない）。TerrainCliffDetail.cpp は、
# 棚の点の高さから蹴上げの高さまでの斜面を棚の高さへ削り、平らな踏み面と縦の蹴上げの水平の段を作る。
#
# 使い方: python Scripts/Terrain/MakeBattleshipCarve.py --capture <削る前の撮影の置き場> [--shelves]
#   標準出力の最後の行: BATTLESHIP_CARVE stamps=<数> raise=<数> lower=<数> max_raise_m=<m> max_lower_m=<m>
#   （--shelves では BATTLESHIP_SHELVES lines=<数> points=<数> shift_px=<x>,<y>）
import argparse
import json
import os
import sys

import numpy as np

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "Scripts", "Terrain"))
import BuildingRegions as BRg  # noqa: E402
import ShapeMeasure as SM  # noqa: E402

OUTPUT = os.path.join(REPO_ROOT, "Tools", "AssetCook", "TerrainCliffCarveBattleship.h")
SIZE = (1920, 1080)
# 刻む列の範囲（写真の幅に対する割合）。外の裾の斜面は地形のまま
COLUMNS = (0.185, 0.835)
# 刻みを置く列の間隔（画素）と、刻みの円の半径（m）
COLUMN_STEP = 3
RADIUS = 18.0
# これより小さい差（m）の列は刻まない
MIN_DELTA = 0.5
# 尾根の点が隣の列より遠い（奥の尾根が外形を作る）列は、前後 NEIGHBOR 列の最も近い水平の距離に置き換える
NEIGHBOR = 30
FAR_JUMP = 60.0
SHELF_OUTPUT = os.path.join(REPO_ROOT, "Tools", "AssetCook", "TerrainCliffShelfBattleship.h")
# 棚の線を世界へ戻す画素の間隔と、続いた点の並びを切る水平の跳び（m）と、残す並びの最小の点の数
SHELF_STEP = 2
SHELF_BREAK = 8.0
SHELF_MIN_POINTS = 8
# 並びの両端で効きを 0 へ落とす割合（並びの長さに対する）
SHELF_TAPER = 0.15


def read_pfm(path):
    with open(path, "rb") as f:
        f.readline()
        width, height = (int(v) for v in f.readline().split())
        f.readline()
        data = np.fromfile(f, dtype="<f4")
    return data.reshape(height, width)[::-1]


def write_shelves(capture):
    camera = json.load(open(os.path.join(capture, "ref_cliff_01.aov.json"), encoding="utf-8-sig"))["camera"]
    depth = read_pfm(os.path.join(capture, "ref_cliff_01.aov.depth.pfm"))
    positions = BRg.world_positions(camera, depth)
    origin = np.asarray(camera["position"], dtype=np.float64)
    annotations = json.load(open(os.path.join(REPO_ROOT, "Scripts", "Terrain", "ShapeAnnotations.json"), encoding="utf-8-sig"))
    cliff = [c for c in annotations["photos"]["cliff_01"]["cliffs"] if c["name"] == "Battleship Rock"][0]
    alignment, error = SM.align_cliff(cliff, depth, SIZE)
    if alignment is None:
        raise SystemExit(f"外形の対応が取れない: {error}")
    shift = alignment["shift"]
    groups = []
    for start, end in (SM.to_pixels([a, b], SIZE, shift) for a, b in cliff["shelves"]):
        count = int(max(abs(end[0] - start[0]), abs(end[1] - start[1])) // SHELF_STEP) + 1
        run = []
        for t in np.linspace(0.0, 1.0, count):
            x = int(round(start[0] + (end[0] - start[0]) * t))
            y = int(round(start[1] + (end[1] - start[1]) * t))
            point = positions[y, x] if 0 <= x < SIZE[0] and 0 <= y < SIZE[1] else None
            near = point is not None and np.all(np.isfinite(point)) and                 np.hypot(point[0] - origin[0], point[2] - origin[2]) <= float(cliff["depth_max_m"])
            if near and run and np.hypot(point[0] - run[-1][0], point[2] - run[-1][2]) > SHELF_BREAK:
                groups.append(run)
                run = []
            if near:
                run.append(point)
            elif run:
                groups.append(run)
                run = []
        if run:
            groups.append(run)
    groups = [g for g in groups if len(g) >= SHELF_MIN_POINTS]
    lines = [
        "﻿#pragma once",
        "",
        "// Battleship Rock の崖錐の斜面を写真の棚の位置で水平の段に削る、棚の点の並び（Scripts/Terrain/MakeBattleshipCarve.py",
        "// --shelves が書く。手で直さない）。1 行が 1 点: 世界の X・Z（m）、棚の高さ（m）、効きの割合（並びの両端で 0）、並びの番号",
        "",
        "namespace NorvesLib::Tools::AssetCook",
        "{",
        "    struct TerrainCliffShelfPoint",
        "    {",
        "        double X;",
        "        double Z;",
        "        float Height;",
        "        float Weight;",
        "        uint32_t Group;",
        "    };",
        "",
        "    inline constexpr TerrainCliffShelfPoint kBattleshipShelfPoints[] = {",
    ]
    total = 0
    for index, group in enumerate(groups):
        taper = max(1.0, SHELF_TAPER * (len(group) - 1))
        for i, p in enumerate(group):
            weight = min(1.0, min(i, len(group) - 1 - i) / taper)
            lines.append(f"        {{{p[0]:.2f}, {p[2]:.2f}, {p[1]:.2f}f, {weight:.3f}f, {index}u}},")
            total += 1
    lines += ["    };", "} // namespace NorvesLib::Tools::AssetCook", ""]
    with open(SHELF_OUTPUT, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(lines))
    print(f"BATTLESHIP_SHELVES lines={len(groups)} points={total} shift_px={shift[0]:.0f},{shift[1]:.0f}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", required=True)
    parser.add_argument("--shelves", action="store_true")
    args = parser.parse_args()
    if args.shelves:
        write_shelves(args.capture)
        return
    camera = json.load(open(os.path.join(args.capture, "ref_cliff_01.aov.json"), encoding="utf-8-sig"))["camera"]
    depth = read_pfm(os.path.join(args.capture, "ref_cliff_01.aov.depth.pfm"))
    positions = BRg.world_positions(camera, depth)
    rays = BRg.pixel_rays(camera, SIZE[0], SIZE[1])
    origin = np.asarray(camera["position"], dtype=np.float64)
    annotations = json.load(open(os.path.join(REPO_ROOT, "Scripts", "Terrain", "ShapeAnnotations.json"), encoding="utf-8-sig"))
    cliff = [c for c in annotations["photos"]["cliff_01"]["cliffs"] if c["name"] == "Battleship Rock"][0]
    photo_columns, photo_tops, _ = SM.polygon_top(SM.to_pixels(cliff["outline"], SIZE), SIZE)
    photo = dict(zip(photo_columns.tolist(), photo_tops.tolist()))
    columns, tops = SM.capture_outline(depth, float(cliff["depth_max_m"]), (0, SIZE[0]))
    engine = dict(zip(columns.tolist(), tops.tolist()))

    first, last = int(COLUMNS[0] * SIZE[0]), int(COLUMNS[1] * SIZE[0])
    crest = {}
    for x in range(first - NEIGHBOR, last + NEIGHBOR + 1):
        if x in engine:
            point = positions[engine[x] + 1, x]
            if np.all(np.isfinite(point)):
                crest[x] = point
    distance = {x: float(np.hypot(p[0] - origin[0], p[2] - origin[2])) for x, p in crest.items()}

    stamps = []
    for x in range(first, last + 1, COLUMN_STEP):
        if x not in photo or x not in crest:
            continue
        near = min(distance[n] for n in range(x - NEIGHBOR, x + NEIGHBOR + 1) if n in distance)
        ray_top = rays[engine[x] + 1, x]
        horizontal = np.array([ray_top[0], ray_top[2]]) / np.hypot(ray_top[0], ray_top[2])
        reach = distance[x] if distance[x] - near <= FAR_JUMP else near
        crest_xz = origin[[0, 2]] + horizontal * reach
        crest_height = crest[x][1] if reach == distance[x] else None
        ray = rays[photo[x], x]
        target = origin[1] + ray[1] * reach / np.hypot(ray[0], ray[2])
        if crest_height is None:
            # 奥の尾根の代わりに置く列は、手前の距離で今の外形の行を通る視線の高さを今の高さにする
            crest_height = origin[1] + ray_top[1] * reach / np.hypot(ray_top[0], ray_top[2])
        delta = target - crest_height
        if abs(delta) < MIN_DELTA:
            continue
        center = crest_xz + horizontal * RADIUS
        stamps.append((x, crest_xz[0], crest_xz[1], center[0], center[1], target, 1 if delta > 0 else -1, delta, reach))

    lines = [
        "﻿#pragma once",
        "",
        "// Battleship Rock の尾根を写真の段に削る刻みの表（Scripts/Terrain/MakeBattleshipCarve.py が書く。手で直さない）。",
        "// 1 行が 1 つの刻み: 円の中心の X・Z（m）、目標の高さ（m）、向き（1 は上げる、-1 は下げる）。半径は kBattleshipCarveRadius",
        "",
        "namespace NorvesLib::Tools::AssetCook",
        "{",
        "    struct TerrainCliffCarveStamp",
        "    {",
        "        double CenterX;",
        "        double CenterZ;",
        "        float Height;",
        "        int Direction;",
        "    };",
        "",
        f"    inline constexpr double kBattleshipCarveRadius = {RADIUS:.1f};",
        "    inline constexpr TerrainCliffCarveStamp kBattleshipCarveStamps[] = {",
    ]
    for s in stamps:
        lines.append(f"        {{{s[3]:.2f}, {s[4]:.2f}, {s[5]:.2f}f, {s[6]}}}, // 列 {s[0]}、差 {s[7]:+.1f} m、距離 {s[8]:.0f} m")
    lines += ["    };", "} // namespace NorvesLib::Tools::AssetCook", ""]
    with open(OUTPUT, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(lines))
    raises = [s for s in stamps if s[6] > 0]
    lowers = [s for s in stamps if s[6] < 0]
    print(f"BATTLESHIP_CARVE stamps={len(stamps)} raise={len(raises)} lower={len(lowers)} "
          f"max_raise_m={max((s[7] for s in raises), default=0):.1f} max_lower_m={min((s[7] for s in lowers), default=0):.1f}")


if __name__ == "__main__":
    main()
