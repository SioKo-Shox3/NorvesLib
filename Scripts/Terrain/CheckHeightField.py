# 荒野の DEM の格子（CropDem.py が書く height.json と height.r32）を、エンジンの高さ場（TerrainHeightField::LoadGrid。
# クッカーと Game が同じものを使う）が約束どおりに引くかを、ファイルの値と突き合わせて確かめる。
#
# 使い方:
#   python Scripts/Terrain/CheckHeightField.py [--asset-cook <AssetCook.exe>]
# AssetCook の `--sample-terrain-grid <x>,<z> ...` に点を渡し、返った高さ・法線を、このスクリプトがファイルから
# 別の求め方（升目の三角形の 3 頂点を通る平面）で求めた値と比べる。
#   - 格子の頂点（四隅・外周・前哨地・固定の乱数の点）: ファイルの値とビット単位で一致する
#   - 升目の内側（対角線の向き 2 通り × 三角形 2 つ・対角線の上）: 平面の値と 1e-4 m 以内で一致し、3 頂点の値の範囲を越えない
#   - 範囲の外（端の少し外・遠く・NaN）: out_of_range を返し、端の値を延ばさない
#   - 法線: 内側は中心差分、外周は片側の差分と一致し、範囲の外は NaN
# 終了コード: すべて満たせば 0、どれかが欠ければ 1。
import argparse
import json
import math
import os
import random
import subprocess
import sys

import numpy as np

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GRID_DIR = os.path.join(REPO_ROOT, "Assets", "Terrain", "ValleyOfTheGods")
JSON_PATH = os.path.join(GRID_DIR, "height.json")
DEFAULT_ASSET_COOK = os.path.join(REPO_ROOT, "build", "Tools", "AssetCook", "Debug", "AssetCook.exe")

# 升目の内側の値の許容[m]（エンジンは double で補間して float に丸める）
INTERIOR_TOLERANCE_M = 1.0e-4
# 法線の比較の許容（成分の差）と、エンジンが法線を引く差分の間隔[m]（--sample-terrain-grid の値）
NORMAL_TOLERANCE = 1.0e-4
NORMAL_SPACING_M = 0.5


def f32(value):
    """float32 に丸めた値（エンジンに渡す座標はこの値になる）"""
    return float(np.float32(value))


class Grid:
    def __init__(self, descriptor):
        self.columns = int(descriptor["width"])
        self.rows = int(descriptor["height"])
        self.first_x = float(descriptor["first_vertex_engine"]["x"])
        self.first_z = float(descriptor["first_vertex_engine"]["z"])
        self.spacing = float(descriptor["vertex_spacing_m"])
        raw = os.path.join(GRID_DIR, descriptor["file"])
        self.values = np.fromfile(raw, dtype="<f4").reshape(self.rows, self.columns)

    def vertex(self, column, row):
        return f32(self.first_x + column * self.spacing), f32(self.first_z + row * self.spacing)

    def inside(self, x, z):
        if math.isnan(x) or math.isnan(z):
            return False
        u = (x - self.first_x) / self.spacing
        v = (z - self.first_z) / self.spacing
        return 0.0 <= u <= self.columns - 1 and 0.0 <= v <= self.rows - 1

    def triangle(self, x, z):
        """(x,z) を含む三角形の 3 頂点 [(列, 行), ...]。メッシュの升目の分け方（列 + 行が偶数なら (0,0)〜(1,1) の対角線）"""
        u = (x - self.first_x) / self.spacing
        v = (z - self.first_z) / self.spacing
        column = min(int(math.floor(u)), self.columns - 2)
        row = min(int(math.floor(v)), self.rows - 2)
        fu = u - column
        fv = v - row
        c, r = column, row
        if (c + r) % 2 == 0:
            if fu >= fv:
                return [(c, r), (c + 1, r), (c + 1, r + 1)]
            return [(c, r), (c + 1, r + 1), (c, r + 1)]
        if fu + fv <= 1.0:
            return [(c, r), (c + 1, r), (c, r + 1)]
        return [(c + 1, r), (c + 1, r + 1), (c, r + 1)]

    def plane_height(self, x, z):
        """3 頂点を通る平面の (x,z) での高さ（範囲の外は None）と、3 頂点の値の最小・最大"""
        if not self.inside(x, z):
            return None, None, None
        corners = self.triangle(x, z)
        matrix = []
        heights = []
        for column, row in corners:
            vx = self.first_x + column * self.spacing
            vz = self.first_z + row * self.spacing
            matrix.append([vx, vz, 1.0])
            heights.append(float(self.values[row, column]))
        a, b, c = np.linalg.solve(np.array(matrix), np.array(heights))
        return a * x + b * z + c, min(heights), max(heights)

    def expected_normal(self, x, z, spacing):
        """エンジンの約束の法線（中心差分。片側が範囲の外ならその側は (x,z) の高さで置き換える）"""
        center, _, _ = self.plane_height(x, z)
        if center is None:
            return None

        def slope(minus, plus):
            span = 2.0 * spacing
            h_minus, _, _ = self.plane_height(*minus)
            h_plus, _, _ = self.plane_height(*plus)
            if h_minus is None:
                h_minus = center
                span -= spacing
            if h_plus is None:
                h_plus = center
                span -= spacing
            return (h_plus - h_minus) / span if span > 0.0 else 0.0

        e = f32(spacing)
        sx = slope((f32(x - e), z), (f32(x + e), z))
        sz = slope((x, f32(z - e)), (x, f32(z + e)))
        length = math.sqrt(sx * sx + 1.0 + sz * sz)
        return (-sx / length, 1.0 / length, -sz / length)


def make_points(grid):
    """確かめる点の一覧 [(種類, 名前, x, z)]"""
    points = []
    last_c = grid.columns - 1
    last_r = grid.rows - 1

    # 格子の頂点
    vertex_cells = [
        ("corner_nw", 0, 0), ("corner_ne", last_c, 0), ("corner_sw", 0, last_r), ("corner_se", last_c, last_r),
        ("edge_west", 0, last_r // 2), ("edge_east", last_c, last_r // 3), ("edge_north", last_c // 2, 0),
        ("edge_south", last_c // 3, last_r),
    ]
    # 前哨地 A に最も近い頂点（エンジンの X 1199.5・Z 150.5 の升目の北西の頂点）
    outpost_c = int(math.floor(1199.5 - grid.first_x))
    outpost_r = int(math.floor(150.5 - grid.first_z))
    vertex_cells.append(("outpost_a", outpost_c, outpost_r))
    rng = random.Random(20261015)
    for index in range(12):
        vertex_cells.append((f"random_{index}", rng.randrange(0, grid.columns), rng.randrange(0, grid.rows)))
    for name, column, row in vertex_cells:
        x, z = grid.vertex(column, row)
        points.append(("vertex", name, x, z))

    # 升目の内側。対角線の向きが 2 通りになるよう、列 + 行が偶数と奇数の升目を選ぶ
    interior_cells = [("even", 1000, 1000), ("odd", 1001, 1000), ("even_cliff", 3001, 2001), ("odd_cliff", 3000, 2001),
                      ("odd_outpost", outpost_c, outpost_r + 1), ("even_outpost", outpost_c, outpost_r)]
    for _ in range(6):
        interior_cells.append(("random", rng.randrange(0, grid.columns - 1), rng.randrange(0, grid.rows - 1)))
    offsets = [("a", 0.25, 0.75), ("b", 0.75, 0.25), ("center", 0.5, 0.5), ("diag", 0.3, 0.3), ("anti", 0.3, 0.7),
               ("near_far", 0.9, 0.85)]
    for label, column, row in interior_cells:
        for offset_name, du, dv in offsets:
            x = f32(grid.first_x + (column + du) * grid.spacing)
            z = f32(grid.first_z + (row + dv) * grid.spacing)
            points.append(("interior", f"{label}_{column}_{row}_{offset_name}", x, z))
    # 外周の頂点の間（端の升目の外側の辺の上）
    points.append(("interior", "east_edge_between", f32(grid.first_x + last_c * grid.spacing),
                   f32(grid.first_z + (last_r // 2 + 0.5) * grid.spacing)))
    points.append(("interior", "south_edge_between", f32(grid.first_x + (last_c // 2 + 0.25) * grid.spacing),
                   f32(grid.first_z + last_r * grid.spacing)))

    # 範囲の外
    x_min = grid.first_x
    x_max = grid.first_x + last_c * grid.spacing
    z_min = grid.first_z
    z_max = grid.first_z + last_r * grid.spacing
    mid_x = f32(grid.first_x + (last_c // 2) * grid.spacing)
    mid_z = f32(grid.first_z + (last_r // 2) * grid.spacing)
    outside = [
        ("west_0.001", f32(x_min - 0.001), mid_z), ("east_0.5", f32(x_max + 0.5), mid_z),
        ("north_1", mid_x, f32(z_min - 1.0)), ("south_0.01", mid_x, f32(z_max + 0.01)),
        ("corner_out", f32(x_max + 3.0), f32(z_max + 3.0)), ("far", 1.0e6, -1.0e6),
    ]
    for name, x, z in outside:
        points.append(("outside", name, x, z))
    points.append(("outside", "nan", float("nan"), mid_z))
    return points


def format_coord(value):
    return "nan" if math.isnan(value) else repr(value)


def run_asset_cook(asset_cook, points):
    args = [asset_cook, "--sample-terrain-grid"] + [f"{format_coord(x)},{format_coord(z)}" for _, _, x, z in points]
    completed = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if completed.returncode != 0:
        raise RuntimeError(f"AssetCook が失敗した（exit={completed.returncode}）: {completed.stderr.strip()}")
    values = {}
    for line in completed.stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    return values


def main():
    parser = argparse.ArgumentParser(description="DEM の格子の高さ場をファイルの値と突き合わせる")
    parser.add_argument("--asset-cook", default=DEFAULT_ASSET_COOK)
    options = parser.parse_args()

    with open(JSON_PATH, encoding="utf-8-sig") as handle:
        descriptor = json.load(handle)
    grid = Grid(descriptor)
    print(f"grid={grid.columns}x{grid.rows} first=({grid.first_x},{grid.first_z}) spacing={grid.spacing}")
    print(f"asset_cook={options.asset_cook}")

    points = make_points(grid)
    reported = run_asset_cook(options.asset_cook, points)
    failures = 0
    counts = {"vertex": 0, "interior": 0, "outside": 0, "normal": 0}
    max_interior_error = 0.0
    max_normal_error = 0.0

    for index, (kind, name, x, z) in enumerate(points):
        height_text = reported.get(f"point[{index}].height")
        normal_text = reported.get(f"point[{index}].normal")
        if height_text is None or normal_text is None:
            print(f"FAIL {kind} {name}: AssetCook の出力に点 {index} が無い")
            failures += 1
            continue
        normal = [float(value) for value in normal_text.split(",")]

        if kind == "outside":
            ok = height_text == "out_of_range" and all(math.isnan(value) for value in normal) and \
                reported.get(f"point[{index}].contains") == "false"
            print(f"{'OK  ' if ok else 'FAIL'} outside {name} x={format_coord(x)} z={format_coord(z)} "
                  f"height={height_text} normal={normal_text}")
            failures += 0 if ok else 1
            counts["outside"] += 1
            continue

        if height_text == "out_of_range":
            print(f"FAIL {kind} {name} x={x} z={z}: 範囲の内側なのに out_of_range")
            failures += 1
            continue
        engine = float(height_text)
        expected, low, high = grid.plane_height(x, z)
        if kind == "vertex":
            column = int(round((x - grid.first_x) / grid.spacing))
            row = int(round((z - grid.first_z) / grid.spacing))
            file_value = grid.values[row, column]
            ok = np.float32(engine) == file_value
            print(f"{'OK  ' if ok else 'FAIL'} vertex {name} col={column} row={row} x={x} z={z} "
                  f"file={repr(float(file_value))} SampleHeight={height_text}")
            counts["vertex"] += 1
        else:
            error = abs(engine - expected)
            max_interior_error = max(max_interior_error, error)
            within = low - INTERIOR_TOLERANCE_M <= engine <= high + INTERIOR_TOLERANCE_M
            ok = error <= INTERIOR_TOLERANCE_M and within
            print(f"{'OK  ' if ok else 'FAIL'} interior {name} x={x} z={z} plane={expected:.6f} "
                  f"SampleHeight={height_text} err={error:.2e} corners=[{low:.3f},{high:.3f}]")
            counts["interior"] += 1
        failures += 0 if ok else 1

        expected_normal = grid.expected_normal(x, z, NORMAL_SPACING_M)
        normal_error = max(abs(a - b) for a, b in zip(normal, expected_normal))
        max_normal_error = max(max_normal_error, normal_error)
        normal_ok = normal_error <= NORMAL_TOLERANCE and abs(math.sqrt(sum(v * v for v in normal)) - 1.0) <= 1.0e-5
        if not normal_ok:
            print(f"FAIL normal {name}: engine={normal_text} expected={expected_normal} err={normal_error:.2e}")
            failures += 1
        counts["normal"] += 1

    print(f"vertex={counts['vertex']} interior={counts['interior']} outside={counts['outside']} "
          f"normal={counts['normal']} max_interior_error={max_interior_error:.3e} "
          f"max_normal_error={max_normal_error:.3e}")
    print(f"fail={failures}")
    print(f"result={'pass' if failures == 0 else 'fail'}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
