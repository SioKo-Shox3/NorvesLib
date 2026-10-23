# 地形が丸く粗く見える原因の切り分け（SCN-WL-SHAPE-CAUSE。ReferenceCheck.py --shape-cause から呼ぶ。単独では動かさない）。
#
#   python Scripts/Terrain/ReferenceCheck.py --shape-cause --capture-root <撮影の置き場> [--verify]
#
# 置き場の 3 組（どれも CaptureScene.ps1 -Hdr -Aov、共通の照明・灰色の材質・雲と草と砂利と建物と霞を外し岩は残す・FXAA）。
# 崖錐の岩は灰色の材質の撮影でも視点の近くに限らず、既定の描画と同じ数を置く（--wasteland-talus-limit=off。ログの
# WASTELAND_TALUS_GRAY_LIMIT off で確かめる）:
#   lod-default/      通常の LOD
#   lod-finest/       最も細かい LOD（--wasteland-finest-lod）
#   cliff-detail-off/ 最も細かい LOD で、崖の手続きの変形（Tools/AssetCook/TerrainCliffDetail.cpp の細分と押し出し）を焼かなかった
#                     区画に差し替えた版（--wasteland-cliff-detail=off。Assets/AssetSets/WastelandTerrainCliffOff.json）
# 視点ごとに、可視バッファの解決が書いた三角形の面の幾何の法線（<視点>.aov.geometric_normal.pfm。八面体の 12 + 12 ビット。
# 深度から戻した位置の微分は D32 の深度の量子化で遠景に偽の折れ目を作るので使わない）を読み、次を測る（位置は深度から戻し、
# 世界座標の範囲・距離・格子の段を引くのにだけ使う）:
#   - 折れ目の密度: 幾何の法線が 2 × kStride 画素の間で kCreaseDegrees を超えて曲がる画素の割合（丸いほど小さい）
#   - LOD の差: 通常の LOD と最も細かい LOD の幾何の法線の角の差が kDiffDegrees を超える画素の割合
#   - 崖の生成の差: 最も細かい LOD と崖の変形を切った版の差（同じ測り方）
#   - 格子の限界: 高さの格子の 1 升（近景 1 m・castle 2 m・mid 8 m・far 16 m・outer 32 m）が kGridPixels 画素以上に写る画素の
#     割合（升の中の形は格子からは来ず、崖の生成が足さなければ平らな面の継ぎ合わせか丸い補間になる）
#   - 材質の陰影: GBuffer の法線（陰影に使う頂点の法線）と幾何の法線の角の差が kShadingDegrees を超える画素の割合
# 画面を GRID_COLUMNS × GRID_ROWS の升に分け、升ごとの値と世界座標の範囲（5〜95 百分位）を <置き場>/shape_cause.json に書き、
# 視点ごとの並べた画（写真・撮影・3 組の幾何の法線の陰影・原因の重ね）を <置き場>/shape_cause/<視点>.png に書く。
# 落ちる: 組の撮影（metrics.json・inputs.json・視点の PNG と深度・法線・幾何の法線の AOV）が無い・起動引数が組の条件と違う・
#   幾何の法線が地形の画素の kGeometricCoverage 未満にしか無い・崖錐の岩を視点の近くに限った（ログ）・
#   組の Game.exe が揃わない・崖の変形を切った版で区画が差し替わっていない（ログの WASTELAND_CLIFF_DETAIL_OFF swapped）・
#   所見の文書（DOC）に所見の表（視点ごとの行と原因の列）が無い・--verify で入力が撮影の後に変わった。
import json
import os
import re

import numpy as np
from PIL import Image, ImageDraw

REPORT = "shape_cause.json"
PREVIEW_DIR = "shape_cause"
DOC = os.path.join("Docs", "RenderingValidation", "WastelandShape.md")
DOC_TABLE_HEADING = "## 所見の表"
VIEWS = ["ref_far_01", "ref_far_02", "ref_cliff_01", "ref_cliff_02", "ref_ground_02"]
# 視点 → 手本の写真（Assets/Reference/ValleyOfTheGods/）
PHOTOS = {"ref_far_01": "far_01", "ref_far_02": "far_02", "ref_cliff_01": "cliff_01", "ref_cliff_02": "cliff_02",
          "ref_ground_02": "ground_02"}
BASE_ARGS = ["--wasteland-common-light", "--wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog",
             "--wasteland-gray-material", "--wasteland-aa=fxaa", "--wasteland-talus-limit=off"]
SETS = {
    "lod-default": BASE_ARGS,
    "lod-finest": BASE_ARGS + ["--wasteland-finest-lod"],
    "cliff-detail-off": BASE_ARGS + ["--wasteland-finest-lod", "--wasteland-cliff-detail=off"],
}
# 所見の表の原因の列（文書の表の見出しにすべて要る）
CAUSES = ["格子の限界", "LOD の簡略化", "崖の生成", "遠景の格子の段", "材質の陰影"]
# 高さの格子の段（Assets/Terrain/ValleyOfTheGods/horizon.json の levels。内側の段から順に引く）
HORIZON_DESCRIPTOR = os.path.join("Assets", "Terrain", "ValleyOfTheGods", "horizon.json")
kStride = 2
kCreaseDegrees = 20.0
kDiffDegrees = 8.0
kGridPixels = 4.0
kShadingDegrees = 15.0
# 深度の段差（隣との相対の差がこれを超える画素は輪郭として法線を求めない）
kDepthJump = 0.02
# 深度の有効な画素のうち、幾何の法線が読める画素がこの割合に満たない視点は落とす
kGeometricCoverage = 0.95
GRID_COLUMNS = 4
GRID_ROWS = 3
PANEL = (640, 360)


def load_levels(RC):
    with open(os.path.join(RC.REPO_ROOT, HORIZON_DESCRIPTOR), encoding="utf-8-sig") as f:
        document = json.load(f)
    return [(level["name"], float(level["spacing_m"]), float(level["x_min"]), float(level["x_max"]),
             float(level["z_min"]), float(level["z_max"])) for level in document["levels"]]


def decode_geometric_normal(packed):
    """gbuffer_aov.frag の PackGeometricNormal（1 + u × 4096 + v）を戻す。(法線, 読める画素)"""
    readable = np.isfinite(packed) & (packed >= 1.0)
    value = np.where(readable, packed - 1.0, 0.0)
    u = np.floor(value / 4096.0)
    v = value - u * 4096.0
    px = u / 4095.0 * 2.0 - 1.0
    py = v / 4095.0 * 2.0 - 1.0
    pz = 1.0 - np.abs(px) - np.abs(py)
    lower = pz < 0.0
    fx = np.where(lower, (1.0 - np.abs(py)) * np.where(px >= 0.0, 1.0, -1.0), px)
    fy = np.where(lower, (1.0 - np.abs(px)) * np.where(py >= 0.0, 1.0, -1.0), py)
    normal = np.stack([fx, fy, pz], axis=2)
    normal /= np.maximum(np.linalg.norm(normal, axis=2), 1e-12)[:, :, None]
    return normal, readable


def view_geometry(RC, set_dir, view):
    """(位置, 幾何の法線, GBuffer の法線, 有効な画素, 1 画素の大きさ m, カメラ, 幾何の法線の被覆率)。読めなければ None と理由"""
    meta_path = os.path.join(set_dir, f"{view}.aov.json")
    depth_path = os.path.join(set_dir, f"{view}.aov.depth.pfm")
    normal_path = os.path.join(set_dir, f"{view}.aov.normal.pfm")
    geometric_path = os.path.join(set_dir, f"{view}.aov.geometric_normal.pfm")
    for path in (meta_path, depth_path, normal_path, geometric_path, os.path.join(set_dir, f"{view}.png")):
        if not os.path.isfile(path):
            return None, f"{os.path.relpath(path, RC.REPO_ROOT)} が無い"
    with open(meta_path, encoding="utf-8-sig") as f:
        camera = json.load(f)["camera"]
    depth = RC.read_pfm_any(depth_path)[:, :, 0].astype(np.float64)
    shading = RC.read_pfm_any(normal_path).astype(np.float64)
    height, width = depth.shape
    rays = RC.aov_pixel_rays(camera, width, height)
    finite = np.isfinite(depth) & (depth > 0.0)
    safe = np.where(finite, depth, 0.0)
    position = np.asarray(camera["position"], dtype=np.float64)[None, None, :] + rays * safe[:, :, None]
    geometric, readable = decode_geometric_normal(RC.read_pfm_any(geometric_path)[:, :, 0].astype(np.float64))
    coverage = float(np.count_nonzero(readable & finite)) / max(1, int(np.count_nonzero(finite)))
    # 隣の深度が飛ぶ画素（輪郭）と幾何の法線の無い画素は外す
    valid = finite & readable
    valid[:, :kStride] = valid[:, -kStride:] = False
    valid[:kStride, :] = valid[-kStride:, :] = False
    for axis in (0, 1):
        ahead = np.roll(safe, -kStride, axis=axis)
        behind = np.roll(safe, kStride, axis=axis)
        valid &= np.isfinite(np.roll(depth, -kStride, axis=axis)) & np.isfinite(np.roll(depth, kStride, axis=axis))
        valid &= np.abs(ahead - behind) <= kDepthJump * np.maximum(safe, 1e-6) + 0.05
    shading_length = np.linalg.norm(shading, axis=2)
    shading = shading / np.maximum(shading_length, 1e-12)[:, :, None]
    footprint = safe * 2.0 * np.tan(np.radians(float(camera["vertical_fov_degrees"])) / 2.0) / height
    return (position, geometric, shading, valid, footprint, camera, coverage), None


def angle_degrees(a, b):
    return np.degrees(np.arccos(np.clip(np.sum(a * b, axis=2), -1.0, 1.0)))


def crease_mask(normal, valid):
    """2 × kStride 画素離れた幾何の法線が kCreaseDegrees を超えて曲がる画素"""
    crease = np.zeros(valid.shape, dtype=bool)
    both = np.zeros(valid.shape, dtype=bool)
    for axis in (0, 1):
        ahead = np.roll(normal, -kStride, axis=axis)
        behind = np.roll(normal, kStride, axis=axis)
        pair = valid & np.roll(valid, -kStride, axis=axis) & np.roll(valid, kStride, axis=axis)
        crease |= pair & (angle_degrees(ahead, behind) > kCreaseDegrees)
        both |= pair
    return crease & both, both


def level_spacing(levels, position):
    """画素ごとの高さの格子の間隔（m）と段の番号（levels の並び）"""
    x = position[:, :, 0]
    z = position[:, :, 2]
    spacing = np.full(x.shape, levels[-1][1])
    index = np.full(x.shape, len(levels) - 1, dtype=np.int32)
    assigned = np.zeros(x.shape, dtype=bool)
    for number, (_, step, x0, x1, z0, z1) in enumerate(levels):
        inside = ~assigned & (x >= x0) & (x <= x1) & (z >= z0) & (z <= z1)
        spacing[inside] = step
        index[inside] = number
        assigned |= inside
    return spacing, index


def fraction(mask, within):
    count = int(np.count_nonzero(within))
    return (float(np.count_nonzero(mask & within)) / count) if count else None


def shade(normal, valid):
    """幾何の法線の陰影（左上の前からの光の Lambert。空と輪郭は黒）を 8 bit の灰色で"""
    light = np.array([-0.4, 0.75, 0.5])
    light /= np.linalg.norm(light)
    value = np.clip(np.sum(normal * light[None, None, :], axis=2), 0.0, 1.0) * 0.85 + 0.15
    value[~valid] = 0.0
    return Image.fromarray((value * 255.0).astype(np.uint8), "L").convert("RGB")


def measure_view(RC, levels, root, view):
    """視点の 3 組を測る。(記録, 並べた画, 失敗の一覧)"""
    failures = []
    data = {}
    for name in SETS:
        geometry, reason = view_geometry(RC, os.path.join(root, name), view)
        if geometry is None:
            failures.append(f"組 {name} の視点 {view}: {reason}")
            continue
        data[name] = geometry
    if failures:
        return None, None, failures
    shapes = {geometry[1].shape for geometry in data.values()}
    if len(shapes) != 1:
        return None, None, [f"視点 {view} の 3 組の大きさが違う: {sorted(shapes)}"]
    coverages = {name: data[name][6] for name in SETS}
    low = {name: value for name, value in coverages.items() if value < kGeometricCoverage}
    if low:
        return None, None, [f"視点 {view} の幾何の法線の被覆率が {kGeometricCoverage:.2f} 未満: "
                            + ", ".join(f"{name}={value:.3f}" for name, value in low.items())]
    position, default_normal, shading, default_valid, footprint, camera, _ = data["lod-default"]
    finest_normal, finest_valid = data["lod-finest"][1], data["lod-finest"][3]
    off_normal, off_valid = data["cliff-detail-off"][1], data["cliff-detail-off"][3]
    spacing, level_index = level_spacing(levels, position)
    common = default_valid & finest_valid & off_valid
    lod_diff = common & (angle_degrees(default_normal, finest_normal) > kDiffDegrees)
    cliff_diff = common & (angle_degrees(finest_normal, off_normal) > kDiffDegrees)
    grid_limited = default_valid & (footprint * kGridPixels <= spacing)
    shading_diff = default_valid & (angle_degrees(shading, default_normal) > kShadingDegrees)
    creases = {name: crease_mask(data[name][1], data[name][3]) for name in SETS}
    height, width = common.shape
    cells = []
    for row in range(GRID_ROWS):
        for column in range(GRID_COLUMNS):
            r0, r1 = row * height // GRID_ROWS, (row + 1) * height // GRID_ROWS
            c0, c1 = column * width // GRID_COLUMNS, (column + 1) * width // GRID_COLUMNS
            window = np.zeros(common.shape, dtype=bool)
            window[r0:r1, c0:c1] = True
            inside = window & default_valid
            if np.count_nonzero(inside) < 0.05 * (r1 - r0) * (c1 - c0):
                continue
            xs = position[:, :, 0][inside]
            zs = position[:, :, 2][inside]
            levels_seen, counts = np.unique(level_index[inside], return_counts=True)
            cell = {
                "cell": [row, column],
                "terrain_pixels": int(np.count_nonzero(inside)),
                "world_x": [round(float(np.percentile(xs, 5)), 1), round(float(np.percentile(xs, 95)), 1)],
                "world_z": [round(float(np.percentile(zs, 5)), 1), round(float(np.percentile(zs, 95)), 1)],
                "distance_median_m": round(float(np.median(np.linalg.norm(
                    position[inside] - np.asarray(camera["position"])[None, :], axis=1))), 1),
                "grid_spacing_m_median": round(float(np.median(spacing[inside])), 1),
                "pixel_m_median": round(float(np.median(footprint[inside])), 3),
                "levels": {levels[int(i)][0]: round(float(c) / float(np.count_nonzero(inside)), 3)
                           for i, c in zip(levels_seen, counts)},
                "grid_limited": fraction(grid_limited, inside),
                "lod_diff": fraction(lod_diff, window & common),
                "cliff_diff": fraction(cliff_diff, window & common),
                "shading_diff": fraction(shading_diff, inside),
                "crease": {name: fraction(creases[name][0], window & creases[name][1]) for name in SETS},
            }
            cells.append(cell)
    whole = default_valid
    record = {
        "view": view,
        "terrain_pixels": int(np.count_nonzero(whole)),
        "geometric_coverage": {name: round(value, 4) for name, value in coverages.items()},
        "grid_limited": fraction(grid_limited, whole),
        "lod_diff": fraction(lod_diff, common),
        "cliff_diff": fraction(cliff_diff, common),
        "shading_diff": fraction(shading_diff, whole),
        "crease": {name: fraction(creases[name][0], creases[name][1]) for name in SETS},
        "cells": cells,
    }
    # 並べた画: 上の段 写真・通常の LOD の撮影・原因の重ね、下の段 3 組の幾何の法線の陰影
    panels = []
    photo_path = os.path.join(RC.REPO_ROOT, "Assets", "Reference", "ValleyOfTheGods", PHOTOS[view] + ".jpg")
    if os.path.isfile(photo_path):
        photo = Image.open(photo_path).convert("RGB")
        scale = PANEL[0] / photo.width
        photo = photo.resize((PANEL[0], int(round(photo.height * scale))), Image.LANCZOS)
        top = max(0, (photo.height - PANEL[1]) // 2)
        panel = Image.new("RGB", PANEL, (0, 0, 0))
        panel.paste(photo.crop((0, top, PANEL[0], min(photo.height, top + PANEL[1]))), (0, max(0, (PANEL[1] - photo.height) // 2)))
        panels.append(("写真 " + PHOTOS[view], panel))
    else:
        panels.append(("写真なし", Image.new("RGB", PANEL, (0, 0, 0))))
    capture = Image.open(os.path.join(root, "lod-default", f"{view}.png")).convert("RGB")
    panels.append(("通常の LOD（撮影）", capture.resize(PANEL, Image.LANCZOS)))
    base = np.asarray(capture, dtype=np.float32) * 0.35
    overlay = base.copy()
    # 青: 格子の限界、黄: 材質の陰影、赤: LOD の差、緑: 崖の生成の差（後に塗る色が上）
    for mask, color in ((grid_limited, (40, 90, 255)), (shading_diff, (230, 200, 0)), (lod_diff, (255, 40, 40)),
                        (cliff_diff, (40, 230, 60))):
        overlay[mask] = overlay[mask] * 0.25 + np.asarray(color, dtype=np.float32) * 0.75
    panels.append(("重ね 青=格子 黄=陰影 赤=LOD 緑=崖", Image.fromarray(overlay.astype(np.uint8)).resize(PANEL, Image.LANCZOS)))
    for name, label in (("lod-default", "面の法線 通常の LOD"), ("lod-finest", "面の法線 最も細かい LOD"),
                        ("cliff-detail-off", "面の法線 崖の変形なし")):
        panels.append((label, shade(data[name][1], data[name][3]).resize(PANEL, Image.LANCZOS)))
    sheet = Image.new("RGB", (PANEL[0] * 3, PANEL[1] * 2), (0, 0, 0))
    draw = ImageDraw.Draw(sheet)
    try:
        from CompareLight import load_font
        font = load_font(20)
    except Exception:
        font = None
    for index, (label, panel) in enumerate(panels):
        x = (index % 3) * PANEL[0]
        y = (index // 3) * PANEL[1]
        sheet.paste(panel, (x, y))
        draw.rectangle((x, y, x + PANEL[0] - 1, y + 26), fill=(0, 0, 0))
        draw.text((x + 6, y + 3), label, fill=(255, 255, 255), font=font)
    for row in range(1, GRID_ROWS):
        for column in range(3):
            yy = PANEL[1] + row * PANEL[1] // GRID_ROWS
            draw.line((column * PANEL[0], yy, (column + 1) * PANEL[0], yy), fill=(90, 90, 90))
    for column in range(1, GRID_COLUMNS):
        for panel_column in range(3):
            xx = panel_column * PANEL[0] + column * PANEL[0] // GRID_COLUMNS
            draw.line((xx, PANEL[1], xx, PANEL[1] * 2), fill=(90, 90, 90))
    return record, sheet, []


def check_doc(RC):
    """所見の文書に、視点ごとの行と原因の列を持つ所見の表があるか。失敗の一覧"""
    path = os.path.join(RC.REPO_ROOT, DOC)
    if not os.path.isfile(path):
        return [f"所見の文書 {DOC} が無い"]
    with open(path, encoding="utf-8-sig") as f:
        text = f.read()
    start = text.find(DOC_TABLE_HEADING)
    if start < 0:
        return [f"{DOC} に「{DOC_TABLE_HEADING}」の節が無い"]
    section = text[start + len(DOC_TABLE_HEADING):]
    following = re.search(r"\n## ", section)
    section = section[:following.start()] if following else section
    rows = [line for line in section.splitlines() if line.strip().startswith("|")]
    if len(rows) < 3:
        return [f"{DOC} の所見の表が無い（表の行 {len(rows)}）"]
    failures = [f"{DOC} の所見の表の見出しに原因の列「{cause}」が無い" for cause in CAUSES if cause not in rows[0]]
    for view in VIEWS:
        if not any(view in row for row in rows[2:]):
            failures.append(f"{DOC} の所見の表に視点 {view} の行が無い")
    return failures


def run(RC, root, verify):
    failures = []
    if not root or not os.path.isdir(root):
        print(f"REFERENCE_CHECK result=fail reason=capture_root_missing path={root}")
        return 1
    levels = load_levels(RC)
    records = {}
    for name, game_args in SETS.items():
        set_dir = os.path.join(root, name)
        metrics_path = os.path.join(set_dir, "metrics.json")
        inputs_path = os.path.join(set_dir, "inputs.json")
        entry = {}
        records[name] = entry
        if not os.path.isfile(metrics_path) or not os.path.isfile(inputs_path):
            failures.append(f"組 {name} の撮影（metrics.json・inputs.json）が無い")
            continue
        with open(metrics_path, encoding="utf-8-sig") as f:
            metrics = json.load(f)
        with open(inputs_path, encoding="utf-8") as f:
            inputs = json.load(f)
        entry.update({"game_args": metrics.get("game_args"), "game_exe_sha256": inputs.get("game_exe_sha256"),
                      "files_sha256": inputs.get("files_sha256")})
        if sorted(metrics.get("game_args") or []) != sorted(game_args):
            failures.append(f"組 {name} の起動引数 {metrics.get('game_args')} が組の条件 {game_args} と違う")
        if metrics.get("failures"):
            failures.append(f"組 {name} の撮影が失敗を記録している: {metrics.get('failures')}")
        if verify:
            ok, reasons, _ = RC.check_inputs(set_dir)
            if not ok:
                failures.extend(f"組 {name}: {reason}" for reason in reasons)
        # 崖錐の岩を視点の近くに限らなかったこと（灰色の材質の撮影の制限を外した印）と、MegaGeometry の BVH のたどりの列の溢れ
        talus_off = None
        bvh_overflow = 0
        for view in VIEWS:
            log_name = RC.view_game_log(set_dir, view)
            log_path = os.path.join(set_dir, log_name) if log_name else ""
            if log_path and os.path.isfile(log_path):
                with open(log_path, encoding="utf-8", errors="replace") as f:
                    log_text = f.read()
                if "WASTELAND_TALUS_GRAY_LIMIT radius_m" in log_text:
                    talus_off = False
                elif talus_off is None and "WASTELAND_TALUS_GRAY_LIMIT off" in log_text:
                    talus_off = True
                bvh_overflow = max([bvh_overflow] + [int(value) for value in re.findall(r"bvh_overflow=(\d+)", log_text)])
                break
        entry["talus_limit_off"] = talus_off
        entry["bvh_overflow_max"] = bvh_overflow
        print(f"SHAPE_CAUSE_SET name={name} talus_limit_off={talus_off} bvh_overflow_max={bvh_overflow}")
        if not talus_off:
            failures.append(f"組 {name} のログに崖錐の岩の制限を外した印（WASTELAND_TALUS_GRAY_LIMIT off）が無い")
        if name == "cliff-detail-off":
            swapped = None
            for view in VIEWS:
                log_name = RC.view_game_log(set_dir, view)
                log_path = os.path.join(set_dir, log_name) if log_name else ""
                if log_path and os.path.isfile(log_path):
                    with open(log_path, encoding="utf-8", errors="replace") as f:
                        found = re.findall(r"WASTELAND_CLIFF_DETAIL_OFF swapped=(\d+)", f.read())
                    if found:
                        swapped = max(int(value) for value in found)
                        break
            entry["cliff_detail_off_swapped"] = swapped
            if not swapped:
                failures.append("組 cliff-detail-off のログに区画の差し替え（WASTELAND_CLIFF_DETAIL_OFF swapped>0）が無い")
    exes = {entry.get("game_exe_sha256") for entry in records.values() if entry}
    if len(exes) != 1 or None in exes:
        failures.append(f"3 組の Game.exe が揃っていない: {sorted(str(value) for value in exes)}")
    views = {}
    os.makedirs(os.path.join(root, PREVIEW_DIR), exist_ok=True)
    if not failures:
        for view in VIEWS:
            record, sheet, view_failures = measure_view(RC, levels, root, view)
            failures.extend(view_failures)
            if record is None:
                continue
            preview = os.path.join(root, PREVIEW_DIR, f"{view}.png")
            sheet.save(preview)
            record["preview"] = RC.rel_dir(preview)
            views[view] = record
            def pct(value):
                return "none" if value is None else f"{value * 100.0:.1f}%"
            print(f"SHAPE_CAUSE view={view} terrain={record['terrain_pixels']} grid_limited={pct(record['grid_limited'])} "
                  f"lod_diff={pct(record['lod_diff'])} cliff_diff={pct(record['cliff_diff'])} "
                  f"shading_diff={pct(record['shading_diff'])} crease_default={pct(record['crease']['lod-default'])} "
                  f"crease_finest={pct(record['crease']['lod-finest'])} "
                  f"crease_cliff_off={pct(record['crease']['cliff-detail-off'])}")
    failures.extend(check_doc(RC))
    report = {"sets": records, "views": views, "thresholds": {"crease_degrees": kCreaseDegrees,
              "diff_degrees": kDiffDegrees, "stride_pixels": kStride, "grid_pixels": kGridPixels, "shading_degrees": kShadingDegrees, "depth_jump": kDepthJump},
              "grid": [GRID_ROWS, GRID_COLUMNS], "failures": failures, "pass": not failures}
    with open(os.path.join(root, REPORT), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print("FAIL " + failure)
    print(f"REFERENCE_CHECK shape_cause views={len(views)} sets={len(SETS)} result={'pass' if not failures else 'fail'} "
          f"report={RC.rel_dir(os.path.join(root, REPORT))}")
    return 0 if not failures else 1
