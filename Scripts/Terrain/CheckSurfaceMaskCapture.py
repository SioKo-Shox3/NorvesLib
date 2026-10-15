# 地表の分類の地図（TerrainMasks.py の surface.png）が、地形の描画に地図どおりに写っているかを撮影で確かめる。
# Game を環境変数 NORVES_TERRAIN_LAYER_COLORS=1 で撮ると、地形のアルベドが 3 層の重み（岩肌 = 赤・土 = 緑・砂 = 青）に
# 置き換わる（Library/Core/Private/Rendering/TerrainBlendShading.h）。このスクリプトは、同じカメラから DEM の格子
# （height.json・height.r32。1 m の全解像度）へ光線を飛ばして当たった位置の地図の重みを引き、画面の画素ごとに
# 「地図が示す層」を予測して、撮影の画素の層（赤・緑・青のいちばん強いもの）と比べる。
# 描画の地形は LOD の段で間引いた形なので、距離の帯ごとの一致の割合が、LOD の段が変わっても分類が動かないことの確かめになる。
#
# 使い方:
#   python Scripts/Terrain/CheckSurfaceMaskCapture.py --capture <撮影の出力先>
#          --view overview=-600,42.985,1100,170,-3 --view cliff=-1900,15.604,-500,-135,12 [--vfov 60] [--out-dir <dir>]
#   --view は 名前=X,Y,Z,Yaw,Pitch（Game.log の FREE_CAMERA_POSE の値。Yaw は SceneCatalog.h と同じく 方位 - 180°）。
#   撮影の <名前>.png を読み、予測と撮影を左右に並べた <名前>_mask_check.png を --out-dir（省略時は撮影の出力先）に書く。
# 結果は視点ごとに 1 行（SURFACE_MASK_CHECK ...）と、最後に result=pass|fail。
# 合格: 予測の層ごと（画素が 200 以上ある層）に、撮影の層が一致する割合が 0.75 以上で、全体の一致が 0.85 以上。
# 比べるのは、予測の層の重みが 0.6 以上（層の境の混ざった所を除く）で、地図の外縁の帯（32 m）の内側、距離 1500 m 以内の画素。
# 終了コード: 合格なら 0、そうでなければ 1。
import argparse
import json
import math
import os
import sys

import numpy as np
from PIL import Image

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GRID_DIR = os.path.join(REPO_ROOT, "Assets", "Terrain", "ValleyOfTheGods")
# Game の荒野の値（WastelandRoutine.cpp の kSurfaceMaskGravelToDirt・kSurfaceMaskEdgeFadeMeters）
GRAVEL_TO_DIRT = 0.5
EDGE_FADE_M = 32.0
# 比べる画素の条件と合格の閾値
SAMPLE_STEP_PX = 4
MAX_DISTANCE_M = 1500.0
DOMINANT_MIN = 0.6
MIN_CLASS_PIXELS = 200
CLASS_AGREEMENT_MIN = 0.75
OVERALL_AGREEMENT_MIN = 0.85
DISTANCE_BANDS_M = [(0.0, 150.0), (150.0, 500.0), (500.0, 1500.0)]
LAYER_NAMES = ["rock", "dirt", "sand"]
LAYER_COLORS = np.array([[220, 60, 60], [60, 200, 70], [60, 70, 220]], dtype=np.uint8)


class Terrain:
    def __init__(self):
        with open(os.path.join(GRID_DIR, "height.json"), encoding="utf-8-sig") as file:
            grid = json.load(file)
        self.columns = int(grid["width"])
        self.rows = int(grid["height"])
        self.first_x = float(grid["first_vertex_engine"]["x"])
        self.first_z = float(grid["first_vertex_engine"]["z"])
        self.heights = np.fromfile(os.path.join(GRID_DIR, grid["file"]), dtype="<f4").reshape(self.rows, self.columns)
        with open(os.path.join(GRID_DIR, "surface.json"), encoding="utf-8-sig") as file:
            mask = json.load(file)
        edges = mask["engine_edges"]
        self.mask_min = np.array([edges["x_min"], edges["z_min"]], dtype=np.float64)
        self.mask_size = np.array([edges["x_max"] - edges["x_min"], edges["z_max"] - edges["z_min"]], dtype=np.float64)
        self.mask = np.asarray(Image.open(os.path.join(GRID_DIR, mask["file"])).convert("RGBA"), dtype=np.float32) / 255.0

    def height(self, x, z):
        """格子の双線形の高さ（格子の外は NaN）"""
        u = x - self.first_x
        v = z - self.first_z
        inside = (u >= 0) & (u <= self.columns - 1) & (v >= 0) & (v <= self.rows - 1)
        u = np.clip(u, 0, self.columns - 1.001)
        v = np.clip(v, 0, self.rows - 1.001)
        c0 = u.astype(np.int64)
        r0 = v.astype(np.int64)
        fu = u - c0
        fv = v - r0
        h = (self.heights[r0, c0] * (1 - fu) * (1 - fv) + self.heights[r0, c0 + 1] * fu * (1 - fv)
             + self.heights[r0 + 1, c0] * (1 - fu) * fv + self.heights[r0 + 1, c0 + 1] * fu * fv)
        return np.where(inside, h, np.nan)

    def layer_weights(self, x, z):
        """地図の重みを 3 層（岩肌・土・砂）へ写した値（シェーダーの ComputeTerrainSurfaceMaskWeights と同じ写し方。
        位置のずらしは入れない）と、外縁の帯の内側か"""
        uv = (np.stack([x, z], axis=-1) - self.mask_min) / self.mask_size
        edge_m = np.minimum(uv, 1.0 - uv) * self.mask_size
        inside = np.minimum(edge_m[..., 0], edge_m[..., 1]) >= EDGE_FADE_M
        height, width = self.mask.shape[:2]
        # 画素の中心を基準にした双線形（テクスチャの標本と同じ）
        px = np.clip(uv[..., 0] * width - 0.5, 0, width - 1.001)
        py = np.clip(uv[..., 1] * height - 0.5, 0, height - 1.001)
        c0 = px.astype(np.int64)
        r0 = py.astype(np.int64)
        fu = (px - c0)[..., None]
        fv = (py - r0)[..., None]
        m = (self.mask[r0, c0] * (1 - fu) * (1 - fv) + self.mask[r0, c0 + 1] * fu * (1 - fv)
             + self.mask[r0 + 1, c0] * (1 - fu) * fv + self.mask[r0 + 1, c0 + 1] * fu * fv)
        # 4 つの重みの和が 1 に足りない分は地図の砂（沢底と吹きだまり）で、砂の枠に入る
        wash = np.clip(1.0 - m.sum(axis=-1), 0.0, 1.0)
        rock = m[..., 0]
        dirt = m[..., 1] + m[..., 3] * GRAVEL_TO_DIRT
        sand = m[..., 2] + m[..., 3] * (1.0 - GRAVEL_TO_DIRT) + wash
        return np.stack([rock, dirt, sand], axis=-1), inside


def march_distances():
    """視線に沿って高さを比べる距離（近くは細かく、遠くは距離の 0.2 % の刻み）"""
    distances = [0.5]
    while distances[-1] < MAX_DISTANCE_M + 50.0:
        distances.append(distances[-1] + max(0.25, distances[-1] * 0.002))
    return np.array(distances)


def predict(terrain, pose, vfov, width, height):
    """画素（SAMPLE_STEP_PX ごと）ごとの、DEM に当たった位置の 3 層の重み・外縁の内側か・距離"""
    x0, y0, z0, yaw, pitch = pose
    azimuth = math.radians(yaw + 180.0)
    p = math.radians(pitch)
    forward = np.array([math.sin(azimuth) * math.cos(p), math.sin(p), -math.cos(azimuth) * math.cos(p)])
    right = np.array([math.cos(azimuth), 0.0, math.sin(azimuth)])
    up = np.cross(right, forward)
    f = (height / 2) / math.tan(math.radians(vfov) / 2)
    us = np.arange(SAMPLE_STEP_PX // 2, width, SAMPLE_STEP_PX)
    vs = np.arange(SAMPLE_STEP_PX // 2, height, SAMPLE_STEP_PX)
    uu, vv = np.meshgrid(us, vs)
    dirs = (forward[None, None, :] + ((uu + 0.5 - width / 2) / f)[..., None] * right[None, None, :]
            - ((vv + 0.5 - height / 2) / f)[..., None] * up[None, None, :])
    dirs /= np.linalg.norm(dirs, axis=-1, keepdims=True)
    flat = dirs.reshape(-1, 3)
    distances = march_distances()
    hit = np.full(flat.shape[0], np.nan)
    chunk = 2048
    for start in range(0, flat.shape[0], chunk):
        d = flat[start:start + chunk]
        px = x0 + d[:, None, 0] * distances[None, :]
        py = y0 + d[:, None, 1] * distances[None, :]
        pz = z0 + d[:, None, 2] * distances[None, :]
        above = py - terrain.height(px, pz)
        below = above < 0
        first = np.argmax(below, axis=1)
        found = below[np.arange(below.shape[0]), first] & (first > 0)
        idx = np.nonzero(found)[0]
        k = first[idx]
        a0 = above[idx, k - 1]
        a1 = above[idx, k]
        t = a0 / np.maximum(a0 - a1, 1.0e-6)
        hit[start + idx] = distances[k - 1] + t * (distances[k] - distances[k - 1])
    valid = np.isfinite(hit)
    hx = x0 + flat[:, 0] * np.nan_to_num(hit)
    hz = z0 + flat[:, 2] * np.nan_to_num(hit)
    weights, inside = terrain.layer_weights(hx, hz)
    shape = uu.shape
    return (uu, vv, weights.reshape(shape + (3,)), (inside & valid).reshape(shape), hit.reshape(shape),
            valid.reshape(shape))


def check_view(terrain, capture_dir, out_dir, name, pose, vfov):
    image = np.asarray(Image.open(os.path.join(capture_dir, f"{name}.png")).convert("RGB"), dtype=np.float32)
    height, width = image.shape[:2]
    uu, vv, weights, inside, distance, valid = predict(terrain, pose, vfov, width, height)
    predicted = np.argmax(weights, axis=-1)
    dominant = np.max(weights, axis=-1)
    captured = np.argmax(image[vv, uu], axis=-1)  # 0 = R（岩肌）・1 = G（土）・2 = B（砂）
    compared = inside & (dominant >= DOMINANT_MIN) & (distance <= MAX_DISTANCE_M)
    match = compared & (predicted == captured)
    overall = match.sum() / max(compared.sum(), 1)
    parts = [f"SURFACE_MASK_CHECK view={name} compared={int(compared.sum())} agreement={overall:.3f}"]
    failures = []
    if compared.sum() < MIN_CLASS_PIXELS:
        failures.append("比べる画素が少なすぎます")
    if overall < OVERALL_AGREEMENT_MIN:
        failures.append(f"全体の一致 {overall:.3f} < {OVERALL_AGREEMENT_MIN}")
    for layer, layer_name in enumerate(LAYER_NAMES):
        selected = compared & (predicted == layer)
        count = int(selected.sum())
        rate = (match & selected).sum() / max(count, 1)
        parts.append(f"{layer_name}={rate:.3f}/{count}")
        if count >= MIN_CLASS_PIXELS and rate < CLASS_AGREEMENT_MIN:
            failures.append(f"{layer_name} の一致 {rate:.3f} < {CLASS_AGREEMENT_MIN}")
    for low, high in DISTANCE_BANDS_M:
        selected = compared & (distance >= low) & (distance < high)
        count = int(selected.sum())
        rate = (match & selected).sum() / max(count, 1)
        parts.append(f"band{int(low)}-{int(high)}m={rate:.3f}/{count}")
    print(" ".join(parts))
    for failure in failures:
        print(f"FAIL view={name} {failure}")

    # 予測（地図の層の色。比べない画素は暗く、空は黒）と撮影を左右に並べる
    panel = np.zeros(uu.shape + (3,), dtype=np.uint8)
    colors = LAYER_COLORS[predicted]
    panel[valid] = (colors[valid] * 0.45).astype(np.uint8)
    panel[compared] = colors[compared]
    prediction = Image.fromarray(panel).resize((width // 2, height // 2), Image.NEAREST)
    capture = Image.fromarray(image.astype(np.uint8)).resize((width // 2, height // 2), Image.BILINEAR)
    side = Image.new("RGB", (width, height // 2))
    side.paste(prediction, (0, 0))
    side.paste(capture, (width // 2, 0))
    side.save(os.path.join(out_dir, f"{name}_mask_check.png"))
    return not failures


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="地表の分類の地図が地形の描画に地図どおりに写っているかを撮影で確かめる")
    parser.add_argument("--capture", required=True, help="NORVES_TERRAIN_LAYER_COLORS=1 で撮った撮影の出力先")
    parser.add_argument("--view", action="append", required=True, help="名前=X,Y,Z,Yaw,Pitch")
    parser.add_argument("--vfov", type=float, default=60.0, help="縦の画角[°]（名前付き視点の既定は 60°）")
    parser.add_argument("--out-dir", default="", help="比較画像の出力先（省略時は撮影の出力先）")
    args = parser.parse_args()

    terrain = Terrain()
    out_dir = args.out_dir or args.capture
    os.makedirs(out_dir, exist_ok=True)
    passed = True
    for entry in args.view:
        name, values = entry.split("=", 1)
        pose = tuple(float(v) for v in values.split(","))
        if len(pose) != 5:
            print(f"FAIL view={name} --view は 名前=X,Y,Z,Yaw,Pitch で与えてください")
            passed = False
            continue
        passed &= check_view(terrain, args.capture, out_dir, name, pose, args.vfov)
    print("result=" + ("pass" if passed else "fail"))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
