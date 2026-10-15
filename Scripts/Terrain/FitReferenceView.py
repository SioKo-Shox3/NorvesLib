# 手本の写真の地形の輪郭（空との境）と、DEM の格子（height.json・height.r32）から計算した稜線を照合し、
# （遠景の記述 horizon.json があれば、近景の格子の外は遠景の段 castle・mid・far の高さの格子を引く）
# 写真に合うカメラの位置・向き・画角を探す。位置の記録が無い写真の視点（SceneCatalog.h の ref_*）を決めるのに使う。
#
# 使い方:
#   python Scripts/Terrain/FitReferenceView.py --photo cliff_01 --target -1387.5,-954.5 --center -2560,-1560 --radius 40 --step 10
#                                              [--out <比較画像.png>] [--outside]
#   --target は主役の地形の位置（エンジンの X,Z [m]）。各位置から target の方位の ±35° の中で向きを探す。
#   --center・--radius・--step はカメラの位置を探す升目（エンジンの m）。--outside を付けると DEM の外の位置も探す
#   （外の位置の地面の高さは西の端の値で代える。視点を DEM の外に置くと足元の地形が欠けるので、視点に使うのは DEM の内側の結果）。
# 結果を 1 行（FIT_REFERENCE_VIEW ...）出し、写真に DEM の稜線を重ねた画像を --out に書く。
#
# 稜線の計算: カメラは地形から 1.7 m の目の高さ、ピンホール、16:9。比較画像（CompareReference.py）と同じく、写真は横幅を
# 1280 px にそろえて 16:9 の上下を切った絵で比べる。写真の輪郭は「赤み（R - B > 10）の画素が 4 行続いた所」の列ごとの上端
# （赤い岩と青空の写真に向く。空が白い・地形が赤くない写真には使えない）。誤差は列ごとの上端の行の差の平均[px]（1280×720 の絵で）。
# 向きの方位は北から時計回り。SceneCatalog.h の Yaw は 方位 - 180°。縦の画角は横の画角を 16:9 に換算した値（kReferenceShots）。
import argparse
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

from CompareReference import PANEL_SIZE, PHOTO_DIR, fit_photo

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GRID_DIR = os.path.join(REPO_ROOT, "Assets", "Terrain", "ValleyOfTheGods")
EYE_HEIGHT_M = 1.7
# 写真の地形の画素の判定（R - B）と、地形とみなす連続の行数・輪郭のならしの列数
PHOTO_RED_MINUS_BLUE = 10
PHOTO_RUN_ROWS = 4
PHOTO_MEDIAN_COLUMNS = 9
# 視線に沿って高さを引く距離[m]（近くは細かく）
DISTANCES = np.concatenate([np.arange(3, 200, 1.0), np.arange(200, 1000, 3.0), np.arange(1000, 6500, 8.0),
                            np.arange(6500, 16000, 16.0)])
# 向きを探す幅[°]と刻み、横の画角[°]・仰俯[°]の範囲
AZIMUTH_WINDOW_DEG = 35.0
AZIMUTH_STEP_DEG = 0.1
HFOV_RANGE_DEG = (14.0, 70.0, 1.0)
PITCH_RANGE_DEG = (-3.0, 15.0)
COLUMN_STEP = 4


class HeightGrid:
    def __init__(self):
        with open(os.path.join(GRID_DIR, "height.json"), encoding="utf-8-sig") as file:
            descriptor = json.load(file)
        self.columns = int(descriptor["width"])
        self.rows = int(descriptor["height"])
        self.first_x = float(descriptor["first_vertex_engine"]["x"])
        self.first_z = float(descriptor["first_vertex_engine"]["z"])
        self.values = np.fromfile(os.path.join(GRID_DIR, descriptor["file"]), dtype="<f4").reshape(self.rows, self.columns)
        # 遠景の段（細かい順）。(最初の X, 最初の Z, 間隔, 高さ) の並び
        self.horizon = []
        horizon_path = os.path.join(GRID_DIR, "horizon.json")
        if os.path.isfile(horizon_path):
            with open(horizon_path, encoding="utf-8-sig") as file:
                horizon = json.load(file)
            for level in horizon["levels"]:
                if "file" not in level:
                    continue
                values = np.fromfile(os.path.join(GRID_DIR, horizon["mesh_folder"], level["file"]), dtype="<f4")
                self.horizon.append((level["x_min"], level["z_min"], float(level["spacing_m"]),
                                     values.reshape(level["rows"], level["columns"])))

    def sample(self, x, z):
        """双線形の高さ（近景の格子の外は遠景の段、どれの外も NaN）"""
        h = self.sample_near(x, z)
        for first_x, first_z, spacing, values in self.horizon:
            missing = np.isnan(h)
            if not missing.any():
                break
            rows, columns = values.shape
            u = (x - first_x) / spacing
            v = (z - first_z) / spacing
            inside = (u >= 0) & (u <= columns - 1) & (v >= 0) & (v <= rows - 1)
            uc = np.clip(u, 0, columns - 1.001)
            vc = np.clip(v, 0, rows - 1.001)
            c0 = np.floor(uc).astype(int)
            r0 = np.floor(vc).astype(int)
            fu = uc - c0
            fv = vc - r0
            level_h = (values[r0, c0] * (1 - fu) * (1 - fv) + values[r0, c0 + 1] * fu * (1 - fv)
                       + values[r0 + 1, c0] * (1 - fu) * fv + values[r0 + 1, c0 + 1] * fu * fv)
            h = np.where(missing & inside, level_h, h)
        return h

    def sample_near(self, x, z):
        """近景の格子の双線形の高さ（格子の外は NaN）"""
        u = np.clip(x - self.first_x, 0, self.columns - 1.001)
        v = np.clip(z - self.first_z, 0, self.rows - 1.001)
        c0 = np.floor(u).astype(int)
        r0 = np.floor(v).astype(int)
        fu = u - c0
        fv = v - r0
        h = (
            self.values[r0, c0] * (1 - fu) * (1 - fv)
            + self.values[r0, c0 + 1] * fu * (1 - fv)
            + self.values[r0 + 1, c0] * (1 - fu) * fv
            + self.values[r0 + 1, c0 + 1] * fu * fv
        )
        inside = (x >= self.first_x) & (x <= self.first_x + self.columns - 1) & (z >= self.first_z) & (z <= self.first_z + self.rows - 1)
        return np.where(inside, h, np.nan)

    def contains(self, x, z):
        return self.first_x <= x <= self.first_x + self.columns - 1 and self.first_z <= z <= self.first_z + self.rows - 1

    def eye(self, x, z):
        """目の高さ（DEM の外は最も近い端の地面から）"""
        cx = min(max(x, self.first_x), self.first_x + self.columns - 1)
        cz = min(max(z, self.first_z), self.first_z + self.rows - 1)
        return float(self.sample(np.array([cx]), np.array([cz]))[0]) + EYE_HEIGHT_M


def photo_outline(photo_id):
    """比較画像と同じ 16:9 の絵と、列ごとの地形の上端の行"""
    panel, _, _ = fit_photo(Image.open(os.path.join(PHOTO_DIR, photo_id + ".jpg")).convert("RGB"))
    rgb = np.asarray(panel).astype(np.int32)
    terrain = (rgb[:, :, 0] - rgb[:, :, 2]) > PHOTO_RED_MINUS_BLUE
    height, width = terrain.shape
    run = np.ones_like(terrain)
    for offset in range(PHOTO_RUN_ROWS):
        shifted = np.zeros_like(terrain)
        shifted[: height - offset] = terrain[offset:]
        run &= shifted
    top = np.full(width, height, dtype=np.float64)
    for column in range(width):
        rows = np.flatnonzero(run[:, column])
        if rows.size > 0:
            top[column] = rows[0]
    half = PHOTO_MEDIAN_COLUMNS // 2
    windows = np.lib.stride_tricks.sliding_window_view(np.pad(top, half, mode="edge"), PHOTO_MEDIAN_COLUMNS)
    return panel, np.median(windows, axis=1)


def focal(hfov):
    return (PANEL_SIZE[0] / 2) / math.tan(math.radians(hfov) / 2)


def vertical_fov(hfov):
    return math.degrees(2 * math.atan(math.tan(math.radians(hfov) / 2) * PANEL_SIZE[1] / PANEL_SIZE[0]))


def skyline(grid, x, z, azimuth, pitch, hfov, columns):
    """列ごとの地形の上端の行（ピンホール。仰俯を含めて正確に投影する）"""
    width, height = PANEL_SIZE
    eye = grid.eye(x, z)
    f = focal(hfov)
    a = math.radians(azimuth)
    p = math.radians(pitch)
    forward = np.array([math.sin(a), -math.cos(a)])
    result = []
    for u in columns:
        direction = a + math.atan((u + 0.5 - width / 2) / f)
        px = x + math.sin(direction) * DISTANCES
        pz = z - math.cos(direction) * DISTANCES
        dy = grid.sample(px, pz) - eye
        depth = (px - x) * forward[0] + (pz - z) * forward[1]
        zc = depth * math.cos(p) + dy * math.sin(p)
        yc = dy * math.cos(p) - depth * math.sin(p)
        v = height / 2 - f * yc / zc
        v = np.where(np.isnan(v) | (zc <= 0), np.inf, v)
        result.append(v.min())
    return np.array(result)


def coarse_fit(grid, x, z, bearing, target_rows, columns):
    """位置 (x,z) で、方位・横の画角・仰俯の最良（仰俯は小さい角の近似で上下のずれとして解く）"""
    width, height = PANEL_SIZE
    offsets = np.arange(-AZIMUTH_WINDOW_DEG, AZIMUTH_WINDOW_DEG + 1e-6, AZIMUTH_STEP_DEG)
    azimuths = np.radians(bearing + offsets)
    px = x + np.sin(azimuths)[:, None] * DISTANCES[None]
    pz = z - np.cos(azimuths)[:, None] * DISTANCES[None]
    h = grid.sample(px, pz)
    slope = np.where(np.isnan(h), -np.inf, (h - grid.eye(x, z)) / DISTANCES[None])
    elevation = np.degrees(np.arctan(slope.max(axis=1)))
    count = len(elevation)
    best = None
    for hfov in np.arange(*HFOV_RANGE_DEG):
        f = focal(hfov)
        column_offsets = np.degrees(np.arctan((columns + 0.5 - width / 2) / f))
        index_offsets = np.round(column_offsets / AZIMUTH_STEP_DEG).astype(int)
        if -index_offsets[0] >= count // 2:
            continue
        centers = np.arange(-index_offsets[0], count - index_offsets[-1] - 1, 2)
        e = elevation[centers[:, None] + index_offsets[None]]
        rows = height / 2 - f * np.tan(np.radians(e)) / np.cos(np.radians(column_offsets))[None]
        difference = target_rows[None] - rows
        shift = np.median(difference, axis=1)
        pitch = np.degrees(np.arctan(shift / f))
        error = np.abs(difference - shift[:, None]).mean(axis=1)
        error = np.where((pitch < PITCH_RANGE_DEG[0]) | (pitch > PITCH_RANGE_DEG[1]), np.inf, error)
        i = int(error.argmin())
        if best is None or error[i] < best[0]:
            best = (float(error[i]), (bearing + (centers[i] - count // 2) * AZIMUTH_STEP_DEG) % 360.0, float(hfov), float(pitch[i]))
    return best


def refine(grid, x, z, azimuth, hfov, pitch, outline):
    """正確な投影で、方位・仰俯 ±1°（0.25° 刻み）と横の画角 ±1.5°（0.5° 刻み）を探す"""
    columns = np.arange(0, PANEL_SIZE[0], 8)
    best = None
    for h in np.arange(hfov - 1.5, hfov + 1.51, 0.5):
        for a in np.arange(azimuth - 1.0, azimuth + 1.01, 0.25):
            for p in np.arange(pitch - 1.0, pitch + 1.01, 0.25):
                rows = skyline(grid, x, z, a, p, h, columns)
                error = float(np.abs(outline[columns] - np.clip(rows, -50, PANEL_SIZE[1] + 50)).mean())
                if best is None or error < best[0]:
                    best = (error, float(a % 360.0), float(h), float(p))
    return best


def main():
    parser = argparse.ArgumentParser(description="手本の写真の地形の輪郭と DEM の稜線を照合し、視点の位置・向き・画角を探す")
    parser.add_argument("--photo", required=True, help="写真の ID（Assets/Reference/ValleyOfTheGods/<ID>.jpg）")
    parser.add_argument("--target", required=True, help="主役の地形の位置 X,Z（エンジンの m）")
    parser.add_argument("--center", required=True, help="カメラの位置を探す升目の中心 X,Z（エンジンの m）")
    parser.add_argument("--radius", type=float, default=40.0, help="升目の半幅[m]")
    parser.add_argument("--step", type=float, default=10.0, help="升目の間隔[m]")
    parser.add_argument("--outside", action="store_true", help="DEM の外の位置も探す")
    parser.add_argument("--out", default="", help="写真に DEM の稜線を重ねた画像の出力先")
    args = parser.parse_args()

    grid = HeightGrid()
    tx, tz = (float(v) for v in args.target.split(","))
    cx, cz = (float(v) for v in args.center.split(","))
    panel, outline = photo_outline(args.photo)
    columns = np.arange(0, PANEL_SIZE[0], COLUMN_STEP)
    candidates = []
    for x in np.arange(cx - args.radius, cx + args.radius + 1e-6, args.step):
        for z in np.arange(cz - args.radius, cz + args.radius + 1e-6, args.step):
            if not args.outside and not grid.contains(x, z):
                continue
            bearing = math.degrees(math.atan2(tx - x, -(tz - z))) % 360.0
            fit = coarse_fit(grid, x, z, bearing, outline[columns], columns)
            if fit is not None and math.isfinite(fit[0]):
                candidates.append((fit[0], float(x), float(z), fit[1], fit[2], fit[3]))
    if not candidates:
        print("FIT_REFERENCE_VIEW result=fail reason=no_candidate")
        return 1
    candidates.sort()
    _, x, z, azimuth, hfov, pitch = candidates[0]
    error, azimuth, hfov, pitch = refine(grid, x, z, azimuth, hfov, pitch, outline)
    distance = math.hypot(tx - x, tz - z)
    yaw = (azimuth - 180.0 + 180.0) % 360.0 - 180.0
    print(
        f"FIT_REFERENCE_VIEW photo={args.photo} x={x:.1f} z={z:.1f} inside={int(grid.contains(x, z))} azimuth={azimuth:.2f} "
        f"yaw={yaw:.2f} pitch={pitch:.2f} hfov={hfov:.1f} vfov={vertical_fov(hfov):.2f} distance={distance:.0f} error_px={error:.1f}"
    )
    if args.out:
        image = panel.copy()
        draw = ImageDraw.Draw(image)
        rows = skyline(grid, x, z, azimuth, pitch, hfov, np.arange(0, PANEL_SIZE[0], 2))
        draw.line([(int(u), int(min(max(r, -10), PANEL_SIZE[1] + 10))) for u, r in zip(range(0, PANEL_SIZE[0], 2), rows)],
                  fill=(255, 0, 255), width=2)
        draw.line([(u, int(r)) for u, r in enumerate(outline)], fill=(0, 230, 255), width=1)
        image.save(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
