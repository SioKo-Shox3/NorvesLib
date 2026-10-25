# 崖の面に貼るスキャンの塊（Scripts/Terrain/CliffScanPlacements.json の boulder_01・rock_09）のアルベドを、隣の地形の崖の面の
# アルベドと比べる（SCN-WL-SCAN-MATERIAL-MATCH）。照明を含まない GBuffer のアルベド（CaptureScene.ps1 -Aov の <視点>.aov.albedo.pfm）
# で比べるので、写真との比と違って撮影時の処理（露出・トーンマップ・影）に左右されない。
#
# 使い方:
#   python Scripts/Terrain/ScanMaterialMatch.py --capture <CaptureScene.ps1 -Aov の出力先> [--verify]
#   結果は <capture>/scan_material_match.json と、領域の重ね画 <capture>/scan_material_<視点>_<置き場>.png に書く。
#   --verify は下の合否を判定し、落ちたら終了コード 1。
#
# 測る領域（視点ごと・置き場ごと。置き場は配置表の名前の頭 battleship・franklin）:
#   塊: 地形の印（.aov.terrain_id.pfm）が「ほかの物」で、深度から戻した位置が、その置き場の boulder_01・rock_09 の塊の
#       ワールドの範囲（配置表の world_bounds、BOUNDS_MARGIN_M だけ広げる）のどれかの中にあり、日の当たる画素
#       （.aov.sun_visibility.pfm が SUNLIT_MIN 以上）。
#   隣の崖: 地形の印が「地形」で、塊の画素から RING_PX[0] より遠く RING_PX[1] 以内（画面の上の距離）、日の当たる、
#       法線（.aov.normal.pfm）の上向きの成分が CLIFF_MAX_UP 以下の崖の面で、位置がその置き場の塊の範囲の和を
#       RING_WORLD_MARGIN_M だけ広げた箱の中にある画素。
#   比べる量: 領域の線形のアルベドの平均の輝度の比（塊 ÷ 崖）と、色度 R÷G・B÷R の相対の差（塊 ÷ 崖 − 1）。
# 合否（--verify）: 視点と置き場の組 CHECK_PAIRS のすべてで、輝度の比が LUMINANCE_RATIO_RANGE、色度の差が CHROMA_TOLERANCE 以内。
#   どの組も領域の画素が MIN_REGION_PX 以上あること。
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))
sys.path.insert(0, SCRIPT_DIR)

import BuildingRegions  # noqa: E402
import ReferenceCheck  # noqa: E402

TABLE_PATH = os.path.join(REPO_ROOT, "Scripts", "Terrain", "CliffScanPlacements.json")
REPORT = "scan_material_match.json"
LUMINANCE = np.array([0.2126, 0.7152, 0.0722])

# 比べる素材（配置表の asset）。rock_face_01・02 は Battleship Rock の 3 個だけで、この比べには含めない
MATCH_ASSETS = ("boulder_01", "rock_09")
# 視点と置き場の組
CHECK_PAIRS = (("ref_cliff_01", "battleship"), ("ref_cliff_02", "franklin"), ("ref_ground_02", "franklin"))
BOUNDS_MARGIN_M = 0.3
SUNLIT_MIN = 0.9
RING_PX = (3, 12)
CLIFF_MAX_UP = 0.75
RING_WORLD_MARGIN_M = 25.0
MIN_REGION_PX = 500
LUMINANCE_RATIO_RANGE = (0.95, 1.05)
CHROMA_TOLERANCE = 0.05
# 地形の印の値（AovTerrainIdCheck.py と同じ）
ID_OTHER = 0.0
ID_TERRAIN = 1.0


def load_json(path):
    with open(path, encoding="utf-8-sig") as file:
        return json.load(file)


def site_boxes(table):
    """置き場ごとの、比べる素材の塊のワールドの範囲（(個数, 2, 3)）"""
    boxes = {}
    for placement in table["placements"]:
        if placement["asset"] not in MATCH_ASSETS:
            continue
        site = placement["name"].split("_")[0]
        lo = np.asarray(placement["world_bounds_min"], dtype=np.float64) - BOUNDS_MARGIN_M
        hi = np.asarray(placement["world_bounds_max"], dtype=np.float64) + BOUNDS_MARGIN_M
        boxes.setdefault(site, []).append((lo, hi))
    return {site: np.asarray(items) for site, items in boxes.items()}


def inside_any(points, boxes):
    """位置（(個数, 3)）が箱のどれかの中にあるか"""
    inside = np.zeros(len(points), dtype=bool)
    for lo, hi in boxes:
        inside |= np.all((points >= lo) & (points <= hi), axis=1)
    return inside


def dilate(mask, radius):
    """画面の上で、チェビシェフの距離 radius 以内に mask の画素がある画素"""
    padded = np.pad(mask, radius)
    height, width = mask.shape
    out = np.zeros_like(mask)
    # 行と列を分けて広げる（正方形の窓）
    rows = np.zeros((height + 2 * radius, width), dtype=bool)
    for dx in range(2 * radius + 1):
        rows |= padded[:, dx:dx + width]
    for dy in range(2 * radius + 1):
        out |= rows[dy:dy + height, :]
    return out


def region_stats(albedo, mask):
    count = int(mask.sum())
    if count == 0:
        return {"pixels": 0}
    rgb = albedo[mask].astype(np.float64).mean(axis=0)
    return {
        "pixels": count,
        "rgb": [round(float(v), 4) for v in rgb],
        "luminance": round(float(rgb @ LUMINANCE), 4),
        "r_over_g": round(float(rgb[0] / rgb[1]), 4),
        "b_over_r": round(float(rgb[2] / rgb[0]), 4),
    }


def write_overlay(path, png_path, block, ring):
    with Image.open(png_path) as image:
        base = np.asarray(image.convert("RGB"), dtype=np.float32)
    out = base.copy()
    out[block] = base[block] * 0.4 + np.array([255.0, 140.0, 0.0]) * 0.6
    out[ring] = base[ring] * 0.4 + np.array([0.0, 220.0, 255.0]) * 0.6
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(path)


def measure_view(capture, view, boxes_by_site):
    meta = load_json(os.path.join(capture, view + ".aov.json"))
    read = lambda key: ReferenceCheck.read_pfm_any(os.path.join(capture, meta[key]["file"]))  # noqa: E731
    albedo = read("albedo")
    depth = read("depth")[..., 0]
    terrain_id = read("terrain_id")[..., 0]
    sun = read("sun_visibility")[..., 0]
    normal = read("normal")
    positions = BuildingRegions.world_positions(meta["camera"], depth)
    finite = np.isfinite(depth)
    sunlit = finite & (sun >= SUNLIT_MIN)
    results = {}
    for site, boxes in boxes_by_site.items():
        other = finite & (terrain_id == ID_OTHER)
        candidate = np.flatnonzero(other)
        inside = inside_any(positions.reshape(-1, 3)[candidate], boxes)
        block_all = np.zeros(depth.size, dtype=bool)
        block_all[candidate[inside]] = True
        block_all = block_all.reshape(depth.shape)
        block = block_all & sunlit
        if int(block_all.sum()) == 0:
            results[site] = {"block": {"pixels": 0}, "cliff": {"pixels": 0}}
            continue
        ring_px = dilate(block_all, RING_PX[1]) & ~dilate(block_all, RING_PX[0])
        lo = boxes[:, 0, :].min(axis=0) - RING_WORLD_MARGIN_M
        hi = boxes[:, 1, :].max(axis=0) + RING_WORLD_MARGIN_M
        near = np.all((positions >= lo) & (positions <= hi), axis=2)
        ring = ring_px & sunlit & (terrain_id == ID_TERRAIN) & (normal[..., 1] <= CLIFF_MAX_UP) & near
        entry = {"block": region_stats(albedo, block), "cliff": region_stats(albedo, ring)}
        if entry["block"]["pixels"] and entry["cliff"]["pixels"]:
            b, c = entry["block"], entry["cliff"]
            entry["luminance_ratio"] = round(b["luminance"] / c["luminance"], 4)
            entry["r_over_g_diff"] = round(b["r_over_g"] / c["r_over_g"] - 1.0, 4)
            entry["b_over_r_diff"] = round(b["b_over_r"] / c["b_over_r"] - 1.0, 4)
        results[site] = entry
        overlay = os.path.join(capture, "scan_material_%s_%s.png" % (view, site))
        write_overlay(overlay, os.path.join(capture, meta["png"]), block, ring)
        entry["overlay"] = os.path.basename(overlay)
    return results


def main():
    parser = argparse.ArgumentParser(description="スキャンの崖の塊と隣の地形の崖のアルベドを AOV で比べる")
    parser.add_argument("--capture", required=True, help="CaptureScene.ps1 -Aov の出力先")
    parser.add_argument("--verify", action="store_true", help="合否を判定する")
    args = parser.parse_args()
    boxes_by_site = site_boxes(load_json(TABLE_PATH))
    views = sorted({view for view, _ in CHECK_PAIRS})
    report = {"capture": os.path.relpath(args.capture, REPO_ROOT).replace("\\", "/"), "views": {}, "failures": []}
    for view in views:
        if not os.path.isfile(os.path.join(args.capture, view + ".aov.json")):
            report["failures"].append("%s: AOV が無い（-Aov で撮る）" % view)
            continue
        report["views"][view] = measure_view(args.capture, view, boxes_by_site)
    for view, site in CHECK_PAIRS:
        entry = report["views"].get(view, {}).get(site)
        if entry is None:
            continue
        b, c = entry["block"], entry["cliff"]
        line = "SCAN_MATERIAL view=%s site=%s block_px=%d cliff_px=%d" % (view, site, b["pixels"], c["pixels"])
        if "luminance_ratio" in entry:
            line += " block_rgb=%s cliff_rgb=%s luminance_ratio=%.3f r_over_g_diff=%+.3f b_over_r_diff=%+.3f" % (
                b["rgb"], c["rgb"], entry["luminance_ratio"], entry["r_over_g_diff"], entry["b_over_r_diff"])
        print(line)
        if b["pixels"] < MIN_REGION_PX or c["pixels"] < MIN_REGION_PX:
            report["failures"].append("%s %s: 領域の画素が %d 未満（塊 %d・崖 %d）" %
                                      (view, site, MIN_REGION_PX, b["pixels"], c["pixels"]))
            continue
        ratio = entry["luminance_ratio"]
        if not LUMINANCE_RATIO_RANGE[0] <= ratio <= LUMINANCE_RATIO_RANGE[1]:
            report["failures"].append("%s %s: 輝度の比 %.3f が %.2f〜%.2f の外" % ((view, site, ratio) + LUMINANCE_RATIO_RANGE))
        for key, label in (("r_over_g_diff", "R÷G"), ("b_over_r_diff", "B÷R")):
            if abs(entry[key]) > CHROMA_TOLERANCE:
                report["failures"].append("%s %s: 色度 %s の差 %+.3f が ±%.2f の外" % (view, site, label, entry[key], CHROMA_TOLERANCE))
    with open(os.path.join(args.capture, REPORT), "w", encoding="utf-8") as file:
        json.dump(report, file, ensure_ascii=False, indent=2)
    for failure in report["failures"]:
        print("FAIL " + failure)
    ok = not report["failures"]
    print("SCAN_MATERIAL_MATCH result=%s failures=%d" % ("pass" if ok else "fail", len(report["failures"])))
    return 0 if ok or not args.verify else 1


if __name__ == "__main__":
    sys.exit(main())
