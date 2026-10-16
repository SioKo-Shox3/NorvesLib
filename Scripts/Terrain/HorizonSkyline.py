# 荒野の視点ごとに、画面の各列で空と接する地形の点が、地形の内側の稜線か、地形のデータの端（DEM 4 枚の外・NoData）かを数える。
# 端が空と接する列では、地形の縁が空に浮いて見える（遠景の項目 SCN-WL-HORIZON の「地平線に実地形の稜線が見える」の確かめ）。
#
# 使い方: python Scripts/Terrain/HorizonSkyline.py
# 高さは FitReferenceView.py の HeightGrid（近景の格子と、遠景の段 castle・mid・far の高さの格子）を引く。
# 視点は Game/Scenes/SceneCatalog.h の kWastelandViews の値（位置・目の高さ・Yaw・Pitch）と、縦の画角（既定 60°、手本の視点は
# WastelandRoutine.cpp の kReferenceShots）。16:9 の横の画角の中を 400 列に分け、視線に沿って遠方面（kFarPlane と同じ値）まで引く。
# 列の判定: 遠方面までの範囲で、地形のデータの最後の点（データの外へ出る手前、または遠方面で切られる点）の仰角が、視線の仰角の
# 最大から 0.02° 以内なら「端」（地形の縁か遠方面の切り口が空と接する）、それ以外は「稜線」。
# 遠方面の外もデータの続く限り引き、空と接する点（仰角が最大の点）までの距離の最大を horizon_max_m として出す（遠方面を決める目安）。
# 出力: 視点ごとに 1 行（SKYLINE view=... ridge=<列数> edge=<列数> edge_headings=<端の列の方位の範囲> horizon_max_m=<m>）と、
# 全体の result（どの視点も edge=0 なら pass）。終了コード: pass なら 0、端があれば 1。
import math
import sys

import numpy as np

from FitReferenceView import HeightGrid

# 名前: (X, 地面からの目の高さ, Z, Yaw, Pitch, 縦の画角)。方位（北から時計回り）は Yaw + 180°
VIEWS = {
    "overview": (-600.0, 80.0, 1100.0, 170.0, -3.0, 60.0),
    "ground": (-300.0, 1.7, 250.0, 153.0, 4.0, 60.0),
    "ridge": (-1400.0, 2.0, -940.0, -50.0, -7.0, 60.0),
    "cliff": (-1900.0, 2.0, -500.0, -135.0, 12.0, 60.0),
    "outpost": (1197.0, 1.7, 100.5, 8.0, 2.0, 60.0),
    "ref_far_01": (-903.5, 1.7, -1608.5, 148.6, 3.5, 12.77),
}
COLUMNS = 400
FAR_PLANE_M = 400000.0
# 遠方面の外の地平線を探す距離（1/3 秒角の 2 枚の範囲を覆う）
TRACE_M = 160000.0
DISTANCES = np.concatenate([np.arange(5.0, 2000.0, 2.0), np.arange(2000.0, 20000.0, 8.0), np.arange(20000.0, TRACE_M, 32.0)])
WINDOW = DISTANCES <= FAR_PLANE_M
EDGE_TOLERANCE_DEG = 0.02


def classify(grid, x, up, z, yaw, vfov):
    eye = grid.eye(x, z) - 1.7 + up
    heading = yaw + 180.0
    hfov = math.degrees(2 * math.atan(math.tan(math.radians(vfov) / 2) * 16 / 9))
    ridge = 0
    edges = []
    horizon_max = 0.0
    for azimuth in np.linspace(heading - hfov / 2, heading + hfov / 2, COLUMNS):
        a = math.radians(azimuth)
        h = grid.sample(x + math.sin(a) * DISTANCES, z - math.cos(a) * DISTANCES)
        angle = np.degrees(np.arctan2(h - eye, DISTANCES))
        if np.isfinite(angle).any():
            horizon_max = max(horizon_max, float(DISTANCES[np.nanargmax(angle)]))
        seen = angle[WINDOW]
        valid = np.isfinite(seen)
        last = np.flatnonzero(valid)[-1] if valid.any() else -1
        if last < 0 or seen[last] >= np.nanmax(seen) - EDGE_TOLERANCE_DEG:
            edges.append(azimuth % 360.0)
        else:
            ridge += 1
    return heading % 360.0, hfov, ridge, edges, horizon_max


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    grid = HeightGrid()
    if not grid.horizon:
        print("SKYLINE result=fail reason=遠景の段の高さの格子が無い（HorizonDem.py --build）")
        return 1
    total_edges = 0
    for name, (x, up, z, yaw, pitch, vfov) in VIEWS.items():
        heading, hfov, ridge, edges, horizon_max = classify(grid, x, up, z, yaw, vfov)
        total_edges += len(edges)
        span = "%.1f〜%.1f" % (edges[0], edges[-1]) if edges else "-"
        print("SKYLINE view=%s heading=%.1f hfov=%.1f columns=%d ridge=%d edge=%d edge_headings=%s horizon_max_m=%.0f" % (
            name, heading, hfov, COLUMNS, ridge, len(edges), span, horizon_max))
    print("SKYLINE far_plane_m=%.0f result=%s" % (FAR_PLANE_M, "pass" if total_edges == 0 else "fail edge_columns=%d" % total_edges))
    return 0 if total_edges == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
