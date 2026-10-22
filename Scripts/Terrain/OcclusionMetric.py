# 小さな遮蔽と太陽の影の測り（SCN-WLR-OCCLUSION-METRIC）。ReferenceCheck.py --occlusion --regions <定義> から
# OcclusionCompare.py が視点ごとに呼ぶ。定義は Scripts/Terrain/ExpectSpecs/occlusion-regions.json。
#
# 空だけの組（照明の組 sky）:
#   - 画素は同じ面の画素（OcclusionCompare.same_surface_masks。書き出しの契約）だけを使う。
#   - 層: 傾斜（陰影の法線と上の角）× カメラの前方向の距離。共通の照明の空は一様な放射輝度なので、法線の方位では分けない。
#   - くぼみの領域（地形の画素）: 崖の柱の間の溝 groove（傾斜の下限以上で、画面の上で求めた幾何のくぼみ cavity が下限以上）、
#     岩塊と斜面の接する帯 contact（岩塊の面からの 3 次元の距離が上限以下）、株の根元 plant_root（株の置き場所からの水平の
#     距離が上限以下）。対照 control は同じ層の開けた地形の画素（cavity が上限以下で、地形でない物から離れている）。
#     cavity と距離は Cycles の深度から戻した位置で求めるので、エンジンと Cycles で同じ画素を使う。
#   - 量: 層ごとの（くぼみの Y の平均 ÷ 対照の Y の平均）のエンジン ÷ Cycles。
#   - 不合格: 定義の required の層が欠ける・くぼみか対照の画素数が下限に届かない・分母（対照の Cycles の Y、Cycles の比）が
#     下限に届かない・Cycles が未収束（種の間の Cycles の比の差が許容差の 1/3 を超える、または種の描画が無い）。
#     試しの描画（--trial）は収束を測って記録するだけにする（本描画ではないので）。
# 太陽だけの組（照明の組 sun）:
#   - エンジンの sun_visibility の AOV と Cycles の太陽の可視率（<視点>.cycles.sun_visibility.pfm。BlenderOcclusionReference.py）
#     を、同じ面の灰色の物体の画素で、両方とも太陽へ向く画素（Cycles の可視率が −1 でない）で比べる。
#   - 影の位置の一致（影 = 可視率が shadow_threshold 未満の IoU）、影の縁の幅（半影の世界の面積と可視率の世界の勾配の
#     面積分から余面積の式で求めた m。境の向きによらない）、漏れ（Cycles で可視率が umbra 未満なのにエンジンで
#     shadow_threshold 以上の画素の割合）。
# 種: Cycles の出力は参照の記録（inputs.json の視点の seed_outputs）のハッシュが合う今回の種だけを読み、
#   convergence.min_seeds 個に届かなければ試しの描画でも不合格にする（前の描画の種の残りで収束を通さない）。
#   - VSM の 1 texel の大きさ（段の式から求めた影の境の画素の texel の中央値）・バイアス・使ったページ・影の LOD を
#     撮影のログ（VSM_CLIPMAP・VSM_TEXEL・VSM_PAGES・megamesh_cluster_stats）から記録する。値の合否は直しの項目が決める。
# 書くもの: 結果は OcclusionCompare の視点ごとの結果の "metric" に入る（<参照の組>/occlusion_check.json）。重ね画像は
#   <参照の組>/occlusion_regions/<視点>.png（空: 左上 Cycles の HDR に領域の色、右上 層、左下 エンジンの HDR、右下 cavity。
#   太陽: 左上 Cycles の可視率、右上 エンジンの可視率、左下 影の一致、右下 半影と影の境）。
import hashlib
import json
import math
import os
import re

import numpy as np
from PIL import Image

import GrayRegions as GR
import PlantRegions as PR

OBJECT_IDS = {"terrain": 0, "talus": 1, "cliff_scan": 2, "other_prop": 3, "plant_branch": 4, "plant_leaf": 5}
GRAY_OBJECT_IDS = (0, 1, 2, 3)
OVERLAY_DIR = "occlusion_regions"
REGION_COLORS = {"groove": (230, 60, 50), "contact": (250, 160, 30), "plant_root": (220, 70, 220),
                 "control": (60, 120, 240)}


def load_spec(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def geometry(camera, depth):
    """(画素の位置, 視線の単位の向き, 画素の一辺の世界の大きさ m〔視線に垂直な面で〕, 焦点距離 px)"""
    height, width = depth.shape
    rays = PR.pixel_rays(camera, width, height)
    finite = np.isfinite(depth)
    positions = np.asarray(camera["position"], dtype=np.float64)[None, None, :] + \
        rays * np.where(finite, depth, 0.0)[..., None]
    focal = height / (2.0 * math.tan(math.radians(float(camera["vertical_fov_degrees"])) / 2.0))
    unit_rays = rays / np.linalg.norm(rays, axis=-1, keepdims=True)
    pixel_size = np.where(finite, depth, np.nan) / focal
    return positions, unit_rays, pixel_size, focal


def screen_samples(depth, focal, radius_m, angle, max_px):
    """世界の半径 radius_m を画面の距離へ直した、向き angle の標本の画素の添字 (行, 列)"""
    height, width = depth.shape
    rows, cols = np.mgrid[0:height, 0:width]
    offset = np.clip(radius_m * focal / np.where(np.isfinite(depth), depth, 1e30), 1.0, max_px)
    sample_cols = np.clip(np.rint(cols + math.cos(angle) * offset).astype(np.int64), 0, width - 1)
    sample_rows = np.clip(np.rint(rows + math.sin(angle) * offset).astype(np.int64), 0, height - 1)
    return sample_rows, sample_cols


def cavity_map(spec, depth, normal, positions, focal):
    """画面の上の幾何のくぼみ: 向きごとに、世界の半径の標本の点が面の接平面からどれだけ上に見えるか（仰角の sin の最大）を
    求め、向きで平均する（0 = 開けた平面、大きいほどくぼみの奥）。標本は 3 次元の距離が半径の reach_factor 倍以内だけ使う"""
    params = spec["cavity"]
    finite = np.isfinite(depth)
    directions = int(params["directions"])
    total = np.zeros(depth.shape, dtype=np.float64)
    for k in range(directions):
        angle = 2.0 * math.pi * k / directions
        best = np.zeros(depth.shape, dtype=np.float64)
        for radius in params["radii_m"]:
            rows, cols = screen_samples(depth, focal, float(radius), angle, float(params["max_offset_px"]))
            delta = positions[rows, cols] - positions
            length = np.linalg.norm(delta, axis=-1)
            usable = finite & finite[rows, cols] & (length > 1e-6) & (length <= float(params["reach_factor"]) * radius)
            elevation = np.where(usable, np.sum(delta * normal, axis=-1) / np.maximum(length, 1e-9), 0.0)
            best = np.maximum(best, elevation)
        total += best
    return np.where(finite, total / directions, np.nan)


def object_distance(spec, depth, positions, focal, target):
    """target の画素（物体の面）までの 3 次元の距離の画面の上の見込み（m。標本の中に無ければ +∞、target 自身は 0）"""
    params = spec["object_distance"]
    finite = np.isfinite(depth)
    distance = np.where(target, 0.0, np.inf)
    directions = int(params["directions"])
    for k in range(directions):
        angle = 2.0 * math.pi * k / directions
        for radius in params["radii_m"]:
            rows, cols = screen_samples(depth, focal, float(radius), angle, float(params["max_offset_px"]))
            hit = finite & target[rows, cols]
            length = np.linalg.norm(positions[rows, cols] - positions, axis=-1)
            distance = np.where(hit, np.minimum(distance, length), distance)
    return distance


def strata(spec, normal, depth):
    """(層の番号〔面でない画素は −1〕, 層の名前の一覧)。傾斜 × 距離"""
    slope_edges = spec["strata"]["slope_degrees"]
    distance_edges = spec["strata"]["distance_m"]
    slope = np.degrees(np.arccos(np.clip(normal[..., 1], -1.0, 1.0)))
    names = []
    label = np.full(depth.shape, -1, dtype=np.int32)
    for i in range(len(slope_edges) - 1):
        for j in range(len(distance_edges) - 1):
            names.append(f"slope{slope_edges[i]:g}-{slope_edges[i + 1]:g}_d{distance_edges[j]:g}-{distance_edges[j + 1]:g}")
            mask = (slope >= slope_edges[i]) & (slope < slope_edges[i + 1]) & \
                (depth >= distance_edges[j]) & (depth < distance_edges[j + 1])
            label[mask] = len(names) - 1
    return label, names


def region_masks(spec, set_kind, record, view, object_index, cavity, slope, near_object, positions):
    """組の種類のくぼみの領域と対照（どれも地形の画素）"""
    regions = spec["regions"]
    terrain = object_index == OBJECT_IDS["terrain"]
    masks = {}
    for name in spec["sky_set_regions"].get(set_kind, []):
        params = regions[name]
        if name == "groove":
            masks[name] = terrain & (slope >= float(params["min_slope_degrees"])) & \
                (cavity >= float(params["min_cavity"])) & (near_object > float(regions["control"]["min_clearance_m"]))
        elif name == "contact":
            masks[name] = terrain & (near_object <= float(params["max_distance_m"]))
        elif name == "plant_root":
            placement = ((record.get("views") or {}).get(view) or {}).get("plant_placement")
            if placement is None:
                masks[name] = np.zeros(terrain.shape, dtype=bool)
                continue
            x0, _, z0 = placement["position"]
            horizontal = np.hypot(positions[..., 0] - x0, positions[..., 2] - z0)
            masks[name] = terrain & (horizontal <= float(params["max_root_distance_m"]))
    control = regions["control"]
    taken = np.zeros(terrain.shape, dtype=bool)
    for mask in masks.values():
        taken |= mask
    masks["control"] = terrain & ~taken & (cavity <= float(control["max_cavity"])) & \
        (near_object >= float(control["min_clearance_m"]))
    return masks


def luminance(image):
    return image[..., :3].astype(np.float64) @ GR.LUMA


def sky_metric(spec, set_name, set_kind, record, view, ctx, trial):
    """空だけの組の層ごとのくぼみの比。(結果, 失敗の一覧)"""
    limits = spec["limits"]
    convergence = spec["convergence"]
    same = ctx["same"]
    masks = region_masks(spec, set_kind, record, view, ctx["object_index"], ctx["cavity"], ctx["slope"],
                         ctx["near_object"], ctx["positions"])
    label, names = ctx["strata"]
    engine_y = luminance(ctx["engine_hdr"])
    cycles_y = luminance(ctx["cycles_hdr"])
    seed_y = [luminance(image) for image in ctx["cycles_seed_hdr"]]
    surface_y = cycles_y[same]
    reference_level = float(np.percentile(surface_y, 99)) if surface_y.size else 0.0
    tolerance = float(spec["tolerance"])
    max_seed_difference = tolerance * float(convergence["fraction_of_tolerance"])
    result = {"regions": {}, "reference_level_y": reference_level, "seeds": 1 + len(seed_y),
              "max_seed_difference": round(max_seed_difference, 4)}
    for region in [name for name in masks if name != "control"]:
        per_stratum = {}
        for index, stratum in enumerate(names):
            in_stratum = same & (label == index)
            region_pixels = masks[region] & in_stratum
            control_pixels = masks["control"] & in_stratum
            entry = {"region_pixels": int(region_pixels.sum()), "control_pixels": int(control_pixels.sum())}
            if entry["region_pixels"] == 0:
                continue
            per_stratum[stratum] = entry
            if entry["control_pixels"] == 0:
                continue
            control_cycles = float(cycles_y[control_pixels].mean())
            ratio_engine = float(engine_y[region_pixels].mean()) / max(float(engine_y[control_pixels].mean()), 1e-30)
            ratio_cycles = float(cycles_y[region_pixels].mean()) / max(control_cycles, 1e-30)
            entry.update({"ratio_engine": round(ratio_engine, 4), "ratio_cycles": round(ratio_cycles, 4),
                          "engine_over_cycles": round(ratio_engine / max(ratio_cycles, 1e-30), 4),
                          "control_cycles_y_fraction": round(control_cycles / max(reference_level, 1e-30), 4)})
            seed_ratios = [ratio_cycles] + [float(y[region_pixels].mean()) / max(float(y[control_pixels].mean()), 1e-30)
                                            for y in seed_y]
            if len(seed_ratios) > 1:
                spread = (max(seed_ratios) - min(seed_ratios)) / max(float(np.mean(seed_ratios)), 1e-30)
                entry["seed_ratios"] = [round(r, 4) for r in seed_ratios]
                entry["seed_difference"] = round(spread, 4)
                # ノイズは 1/√spp で減るので、許容差の 1/3 に届く spp の見込み
                spp = int(record["views"][view].get("spp", 0))
                entry["spp_needed_estimate"] = int(math.ceil(spp * (spread / max(max_seed_difference, 1e-9)) ** 2))
        result["regions"][region] = per_stratum
    result["pixels"] = {name: int((mask & same).sum()) for name, mask in masks.items()}
    return result, masks


def required_failures(spec, set_name, view, result, trial):
    """定義の required の層の検査（空だけの組）。失敗の一覧"""
    failures = []
    limits = spec["limits"]
    for item in spec["required"]:
        if item["set"] != set_name or item["view"] != view:
            continue
        key = f"{item['region']}/{item['stratum']}"
        entry = ((result.get("regions") or {}).get(item["region"]) or {}).get(item["stratum"])
        if entry is None:
            failures.append(f"必須の層 {key} の画素が無い")
            continue
        if entry["region_pixels"] < int(limits["min_region_pixels"]):
            failures.append(f"必須の層 {key} のくぼみの画素 {entry['region_pixels']} が {limits['min_region_pixels']} に届かない")
        if entry["control_pixels"] < int(limits["min_control_pixels"]):
            failures.append(f"必須の層 {key} の対照の画素 {entry['control_pixels']} が {limits['min_control_pixels']} に届かない")
        if "ratio_cycles" not in entry:
            continue
        if entry["control_cycles_y_fraction"] < float(limits["min_control_y_fraction"]):
            failures.append(f"必須の層 {key} の対照の Cycles の Y が視点の 99 % 点の {entry['control_cycles_y_fraction']:.4f} 倍で "
                            f"下限 {limits['min_control_y_fraction']} に届かない")
        if entry["ratio_cycles"] < float(limits["min_cycles_ratio"]):
            failures.append(f"必須の層 {key} の Cycles のくぼみの比 {entry['ratio_cycles']:.4f} が下限 "
                            f"{limits['min_cycles_ratio']} に届かない")
        if trial:
            continue
        if "seed_difference" not in entry:
            failures.append(f"必須の層 {key} の Cycles の種が 1 つだけ（収束を確かめられない）")
        elif entry["seed_difference"] > result["max_seed_difference"]:
            failures.append(f"必須の層 {key} の Cycles が未収束（種の間の差 {entry['seed_difference']:.4f} が "
                            f"{result['max_seed_difference']:.4f} を超える）")
    return failures


def box_mean(image, radius):
    """(2r+1)² の箱の平均（端は内側だけ）"""
    if radius <= 0:
        return image
    padded = np.pad(image, radius, mode="edge")
    total = np.cumsum(np.cumsum(padded, axis=0), axis=1)
    total = np.pad(total, ((1, 0), (1, 0)))
    size = 2 * radius + 1
    height, width = image.shape
    return (total[size:size + height, size:size + width] - total[:height, size:size + width] -
            total[size:size + height, :width] + total[:height, :width]) / float(size * size)


def boundary(mask, domain):
    """影の境の画素（domain の中で mask の画素のうち、上下左右の隣に domain の非 mask がある画素）"""
    edge = np.zeros(mask.shape, dtype=bool)
    for dr, dc in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        neighbor = np.roll(mask, (dr, dc), axis=(0, 1))
        neighbor_domain = np.roll(domain, (dr, dc), axis=(0, 1))
        edge |= mask & domain & neighbor_domain & ~neighbor
    return edge


def surface_jacobian(camera, depth, normal, min_view_cos):
    """画素の隣への 1 画素の歩みの世界の変位（列の向き, 行の向き。(高さ, 幅, 3) の m）。深度から戻した位置の面の接平面
    （幾何の法線）の上で、隣の画素の視線が当たる点への変位の微分。視線と面の cos は min_view_cos まで"""
    height, width = depth.shape
    rays = PR.pixel_rays(camera, width, height)
    tan_half = math.tan(math.radians(float(camera["vertical_fov_degrees"])) / 2.0)
    right = np.asarray(camera["right"], dtype=np.float64) * (2.0 / width * tan_half * float(camera["aspect_ratio"]))
    down = -np.asarray(camera["up"], dtype=np.float64) * (2.0 / height * tan_half)
    ray_length = np.linalg.norm(rays, axis=-1)
    facing = np.sum(normal * rays, axis=-1)
    facing = np.where(facing < 0.0, -1.0, 1.0) * np.maximum(np.abs(facing), float(min_view_cos) * ray_length)
    t = np.where(np.isfinite(depth), depth, np.nan)[..., None]
    steps = []
    for step in (right, down):
        along = np.sum(normal * step, axis=-1) / facing
        steps.append(t * (step[None, None, :] - rays * along[..., None]))
    return steps[0], steps[1]


def screen_gradient(value, usable, positions, step, axis):
    """axis（1 = 列, 0 = 行）の向きの画素 1 つあたりの value の差（両隣が使えれば中央差分、片方だけなら片側、無ければ NaN）。
    隣が使えるのは、usable で、位置が 1 画素の歩みの 3 倍以内（面の切れ目を越えない）のとき"""
    reach = 3.0 * np.linalg.norm(step, axis=-1)
    diffs = []
    # shift −1 は次の画素（前向きの差）、+1 は前の画素（後ろ向きの差）。端で反対側へ回り込む隣は使わない
    for shift in (-1, 1):
        neighbor = np.roll(value, shift, axis=axis)
        ok = usable & np.roll(usable, shift, axis=axis) & \
            (np.linalg.norm(np.roll(positions, shift, axis=axis) - positions, axis=-1) <= reach)
        edge = [slice(None), slice(None)]
        edge[axis] = slice(-1, None) if shift == -1 else slice(0, 1)
        ok[tuple(edge)] = False
        diffs.append((np.where(ok, (neighbor - value) * -shift, 0.0), ok))
    (forward, forward_ok), (backward, backward_ok) = diffs
    return np.where(forward_ok & backward_ok, 0.5 * (forward + backward),
                    np.where(forward_ok, forward, np.where(backward_ok, backward, np.nan)))


def edge_width(visibility, domain, footprint, sun):
    """影の縁の幅（m）。半影（可視率 umbra〜lit）の世界の面積 A と、半影の上の可視率の世界の勾配の積分 ∫|∇v|dA から
    w = (lit − umbra)·A ÷ ∫|∇v|dA（余面積の式。幅 w で可視率が直線に変わる帯なら境の向きによらず w になる）。
    footprint は (画素の世界の面積, 列の向きの歩み, 行の向きの歩み, 位置)"""
    area, step_x, step_y, positions = footprint
    penumbra = domain & (visibility > float(sun["umbra"])) & (visibility < float(sun["lit"]))
    border = boundary(visibility < float(sun["shadow_threshold"]), domain)
    gx = screen_gradient(visibility, domain, positions, step_x, axis=1)
    gy = screen_gradient(visibility, domain, positions, step_y, axis=0)
    # 世界の勾配の大きさ² = g_screenᵀ (JᵀJ)⁻¹ g_screen（J = [列の歩み, 行の歩み]）
    a = np.sum(step_x * step_x, axis=-1)
    b = np.sum(step_x * step_y, axis=-1)
    c = np.sum(step_y * step_y, axis=-1)
    det = a * c - b * b
    with np.errstate(invalid="ignore", divide="ignore"):
        gradient = np.sqrt(np.maximum((c * gx * gx - 2.0 * b * gx * gy + a * gy * gy) / det, 0.0))
    usable = penumbra & np.isfinite(gradient) & np.isfinite(area) & (det > 0.0)
    flux = float((area[usable] * gradient[usable]).sum())
    width = (float(sun["lit"]) - float(sun["umbra"])) * float(area[usable].sum()) / flux if flux > 0.0 else math.nan
    return width, penumbra, border


def vsm_record(engine_dir, view, spec):
    """撮影のログの VSM と影の LOD の記録"""
    path = os.path.join(engine_dir, f"{view}.Game.log")
    record = {"clipmap": None, "texel": [], "pages_last_nonzero": None, "shadow_lod": {}}
    if not os.path.isfile(path):
        return record
    groups = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if "VSM_CLIPMAP " in line:
                record["clipmap"] = dict(re.findall(r"(\w+)=([-\d.]+)", line.split("VSM_CLIPMAP ", 1)[1]))
            elif "VSM_TEXEL " in line:
                record["texel"].append(dict(re.findall(r"(\w+)=([-\d.]+)", line.split("VSM_TEXEL ", 1)[1])))
            elif "VSM_PAGES " in line:
                values = dict(re.findall(r"(\w+)=(\w+)", line.split("VSM_PAGES ", 1)[1]))
                if values.get("requested", "0") != "0":
                    record["pages_last_nonzero"] = values
            elif "stage=megamesh_cluster_stats" in line and "shadow_lod=" in line:
                name = re.search(r'debug_name="([^"]*)"', line)
                values = dict(re.findall(r"(\w+)=(\d+)", line))
                group = re.sub(r"(_\d+)+(\.generated)?$", "", os.path.basename(name.group(1) if name else "?"))
                entry = groups.setdefault(group, {"meshes": 0, "shadow_lod": set(), "lod_levels": set(),
                                                  "lod0_triangles": 0, "shadow_triangles": 0})
                entry["meshes"] += 1
                entry["shadow_lod"].add(int(values.get("shadow_lod", -1)))
                entry["lod_levels"].add(int(values.get("lod_levels", -1)))
                entry["lod0_triangles"] += int(values.get("lod0_triangles", 0))
                entry["shadow_triangles"] += int(values.get("shadow_triangles", 0))
            elif "MEGA_LOD_ERROR_PX " in line or "WASTELAND_FINEST_LOD " in line:
                key = "MEGA_LOD_ERROR_PX" if "MEGA_LOD_ERROR_PX " in line else "WASTELAND_FINEST_LOD"
                record[key.lower()] = line.split(key + " ", 1)[1].strip()
    for group, entry in groups.items():
        record["shadow_lod"][group] = {"meshes": entry["meshes"], "shadow_lod": sorted(entry["shadow_lod"]),
                                       "lod_levels": sorted(entry["lod_levels"]),
                                       "shadow_over_lod0_triangles": round(entry["shadow_triangles"] /
                                                                          max(entry["lod0_triangles"], 1), 4)}
    return record


def receiver_texel(pixel_size, clipmap, spec):
    """段の選び方の式（VirtualShadowMapClipmap.h: texel が画素の大きさ × 2^bias 以下の最も粗い段）から求めた受け手の texel（m）"""
    if not clipmap:
        return np.full(pixel_size.shape, np.nan)
    texel0 = float(clipmap["first_width_m"]) / float(spec["sun"]["vsm_virtual_resolution"])
    levels = int(float(clipmap["levels"]))
    bias = float(clipmap["bias"])
    level = np.floor(np.log2(np.maximum(pixel_size * (2.0 ** bias) / texel0, 1e-30)))
    level = np.clip(level, 0, levels - 1)
    return texel0 * np.power(2.0, level)


def sun_metric(spec, engine_dir, view, ctx):
    """太陽だけの組の影の位置・縁・漏れ。(結果, 失敗の一覧, 重ね画像の材料)"""
    sun = spec["sun"]
    failures = []
    engine_visibility = ctx["engine_sun_visibility"]
    cycles_visibility = ctx["cycles_sun_visibility"]
    if engine_visibility is None or cycles_visibility is None:
        missing = "エンジンの sun_visibility の AOV" if engine_visibility is None else "Cycles の太陽の可視率"
        return {}, [f"{missing}が無い"], None
    radius = int(sun["filter_radius_px"])
    domain = ctx["same"] & np.isin(ctx["object_index"], GRAY_OBJECT_IDS) & (cycles_visibility >= 0.0) & \
        (engine_visibility >= 0.0)
    # 箱で均すときに domain の外を混ぜない
    def filtered(visibility):
        weight = box_mean(domain.astype(np.float64), radius)
        return np.where(domain, box_mean(np.where(domain, visibility, 0.0), radius) / np.maximum(weight, 1e-9), -1.0)
    engine_v = filtered(engine_visibility.astype(np.float64))
    cycles_v = filtered(cycles_visibility.astype(np.float64))
    threshold = float(sun["shadow_threshold"])
    engine_shadow = domain & (engine_v < threshold)
    cycles_shadow = domain & (cycles_v < threshold)
    union = int((engine_shadow | cycles_shadow).sum())
    iou = float((engine_shadow & cycles_shadow).sum()) / union if union else math.nan
    cycles_umbra = domain & (cycles_v < float(sun["umbra"]))
    leak = domain & cycles_umbra & (engine_v >= threshold)
    pixel_size = ctx["pixel_size"]
    step_x, step_y = surface_jacobian(ctx["camera"], ctx["depth"], ctx["geometric_normal"], float(sun["min_view_cos"]))
    area = np.where(domain, np.linalg.norm(np.cross(step_x, step_y), axis=-1), 0.0)
    footprint = (area, step_x, step_y, ctx["positions"])
    engine_width, engine_penumbra, engine_border = edge_width(engine_v, domain, footprint, sun)
    cycles_width, cycles_penumbra, cycles_border = edge_width(cycles_v, domain, footprint, sun)
    vsm = vsm_record(engine_dir, view, spec)
    texel = receiver_texel(pixel_size, vsm.get("clipmap"), spec)
    border_texel = texel[cycles_border & np.isfinite(texel)]
    result = {
        "domain_pixels": int(domain.sum()),
        "shadow_pixels": {"engine": int(engine_shadow.sum()), "cycles": int(cycles_shadow.sum())},
        "shadow_iou": round(iou, 4),
        "leak_fraction": round(float(leak.sum()) / max(int(cycles_umbra.sum()), 1), 4),
        "leak_pixels": int(leak.sum()), "cycles_umbra_pixels": int(cycles_umbra.sum()),
        "engine_only_shadow_fraction": round(float((engine_shadow & ~cycles_shadow).sum()) / max(union, 1), 4),
        "edge_width_m": {"engine": round(engine_width, 4), "cycles": round(cycles_width, 4)},
        "border_pixel_size_m_median": round(float(np.nanmedian(pixel_size[cycles_border])), 5)
        if cycles_border.any() else None,
        "vsm": vsm,
        "receiver_texel_m_median": round(float(np.median(border_texel)), 5) if border_texel.size else None,
        "filter_radius_px": radius,
    }
    if result["domain_pixels"] < int(sun["min_domain_pixels"]):
        failures.append(f"太陽へ向く同じ面の画素 {result['domain_pixels']} が {sun['min_domain_pixels']} に届かない")
    if int(cycles_shadow.sum()) < int(sun["min_shadow_pixels"]):
        failures.append(f"Cycles の影の画素 {int(cycles_shadow.sum())} が {sun['min_shadow_pixels']} に届かない")
    overlay = {"engine_v": engine_v, "cycles_v": cycles_v, "engine_shadow": engine_shadow,
               "cycles_shadow": cycles_shadow, "leak": leak, "domain": domain,
               "penumbra": (engine_penumbra, cycles_penumbra), "border": (engine_border, cycles_border)}
    return result, failures, overlay


def display(image, scale):
    linear = np.clip(image * scale, 0.0, 1.0)
    srgb = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1.0 / 2.4) - 0.055)
    return (srgb * 255.0 + 0.5).astype(np.uint8)


def gray_rgb(value):
    return np.repeat((np.clip(value, 0.0, 1.0)[..., None] * 255.0 + 0.5).astype(np.uint8), 3, axis=-1)


def save_grid(path, tiles):
    image = Image.fromarray(np.concatenate([np.concatenate(tiles[:2], axis=1), np.concatenate(tiles[2:], axis=1)], axis=0))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    image.resize((image.width // 2, image.height // 2), Image.BILINEAR).save(path)


def write_sky_overlay(path, ctx, masks):
    cycles_y = luminance(ctx["cycles_hdr"])
    surface = ctx["same"]
    scale = 0.5 / max(float(np.percentile(cycles_y[surface], 90)), 1e-30) if surface.any() else 1.0
    tinted = display(ctx["cycles_hdr"][..., :3].astype(np.float64), scale).astype(np.float64)
    for name, color in REGION_COLORS.items():
        mask = masks.get(name)
        if mask is not None:
            mask = mask & surface
            tinted[mask] = 0.45 * tinted[mask] + 0.55 * np.asarray(color, dtype=np.float64)
    label, names = ctx["strata"]
    palette = np.array([[(60 + 60 * i) % 256, (200 - 45 * j) % 256, (90 + 70 * (i + j)) % 256]
                        for i in range(8) for j in range(8)], dtype=np.uint8)
    slopes = len(ctx["spec"]["strata"]["slope_degrees"]) - 1
    distances = len(ctx["spec"]["strata"]["distance_m"]) - 1
    strata_rgb = np.zeros(label.shape + (3,), dtype=np.uint8)
    for index in range(len(names)):
        i, j = divmod(index, distances)
        strata_rgb[(label == index) & surface] = palette[(i * 8 + j) % len(palette)]
    del slopes
    cavity = np.nan_to_num(ctx["cavity"], nan=0.0)
    save_grid(path, [tinted.astype(np.uint8), strata_rgb,
                     display(ctx["engine_hdr"][..., :3].astype(np.float64), scale), gray_rgb(cavity / 0.6)])


def write_sun_overlay(path, overlay):
    domain = overlay["domain"]
    judge = np.zeros(domain.shape + (3,), dtype=np.uint8)
    judge[domain] = (235, 235, 235)
    both = overlay["engine_shadow"] & overlay["cycles_shadow"]
    judge[both] = (90, 90, 90)
    judge[overlay["cycles_shadow"] & ~overlay["engine_shadow"]] = (220, 50, 50)
    judge[overlay["engine_shadow"] & ~overlay["cycles_shadow"]] = (50, 90, 230)
    judge[overlay["leak"]] = (255, 0, 0)
    edges = np.zeros(domain.shape + (3,), dtype=np.uint8)
    edges[domain] = (30, 30, 30)
    edges[overlay["penumbra"][1]] = (200, 120, 40)
    edges[overlay["penumbra"][0]] = (60, 160, 230)
    edges[overlay["penumbra"][0] & overlay["penumbra"][1]] = (230, 230, 120)
    edges[overlay["border"][1]] = (255, 255, 255)
    save_grid(path, [gray_rgb(overlay["cycles_v"]), gray_rgb(overlay["engine_v"]), judge, edges])


def measure_view(RC, spec, set_name, record, engine_dir, reference_dir, view, data, trial):
    """1 視点の測り。(結果, 失敗の一覧)。data は OcclusionCompare.check_view が読んだ画と同じ面の画素"""
    case = (record.get("lighting") or {}).get("case")
    set_kind = record.get("set_kind") or "rocks"
    camera = record["views"][view]["camera"]
    depth = data["cycles_depth"]
    positions, _, pixel_size, focal = geometry(camera, depth)
    normal = data["cycles_normal"]
    ctx = {"spec": spec, "same": data["same"], "object_index": data["object_index"], "engine_hdr": data["engine_hdr"],
           "cycles_hdr": data["cycles_hdr"], "positions": positions, "pixel_size": pixel_size, "normal": normal,
           "camera": camera, "depth": depth, "geometric_normal": data["cycles_geometric_normal"]}
    prefix = os.path.join(reference_dir, view)
    overlay_path = os.path.join(reference_dir, OVERLAY_DIR, f"{view}.png")
    if case == "sun":
        aov = data["aov"]
        engine_visibility = None
        if "sun_visibility" in aov and os.path.isfile(os.path.join(engine_dir, aov["sun_visibility"]["file"])):
            engine_visibility = RC.read_pfm_any(os.path.join(engine_dir, aov["sun_visibility"]["file"]))[..., 0]
        sun_files, seed_failures = recorded_seed_files(record, reference_dir, view, "sun_visibility")
        ctx["engine_sun_visibility"] = engine_visibility
        ctx["cycles_sun_visibility"] = RC.read_pfm_any(sun_files[0])[..., 0] if sun_files else None
        result, failures, overlay = sun_metric(spec, engine_dir, view, ctx)
        failures = seed_failures + failures
        result["kind"] = "sun"
        if overlay is not None:
            write_sun_overlay(overlay_path, overlay)
            result["overlay"] = RC.rel_dir(overlay_path)
        return result, failures
    slope = np.degrees(np.arccos(np.clip(normal[..., 1], -1.0, 1.0)))
    ctx["slope"] = slope
    ctx["cavity"] = cavity_map(spec, depth, normal, positions, focal)
    non_terrain = np.isfinite(depth) & (data["object_index"] > OBJECT_IDS["terrain"])
    ctx["near_object"] = object_distance(spec, depth, positions, focal, non_terrain)
    ctx["strata"] = strata(spec, normal, depth)
    seed_files, seed_failures = recorded_seed_files(record, reference_dir, view, "hdr")
    ctx["cycles_seed_hdr"] = [RC.read_pfm_any(path)[..., :3] for path in seed_files[1:]]
    result, masks = sky_metric(spec, set_name, set_kind, record, view, ctx, trial)
    result["kind"] = "sky"
    min_seeds = int(spec["convergence"]["min_seeds"])
    if len(seed_files) < min_seeds:
        # 試しの描画でも、種の間の差を測れない描画は通さない
        seed_failures.append(f"今回の描画の記録に合う Cycles の種が {len(seed_files)} 個で {min_seeds} 個に届かない")
    failures = seed_failures + required_failures(spec, set_name, view, result, trial)
    write_sky_overlay(overlay_path, ctx, masks)
    result["overlay"] = RC.rel_dir(overlay_path)
    return result, failures


def recorded_seed_files(record, reference_dir, view, part):
    """参照の記録（inputs.json の視点の seed_outputs）にある今回の描画の種の出力のうち、ハッシュが合うものの道（種の順）と
    失敗の一覧。記録に無い種の出力（前の描画の残り）は読まない。ハッシュが合わない・無い種があればそこで打ち切る"""
    outputs = sorted((item for item in (((record.get("views") or {}).get(view) or {}).get("seed_outputs") or [])
                      if item.get("part") == part), key=lambda item: int(item["seed"]))
    if not outputs:
        return [], [f"参照の記録に {part} の種の出力のハッシュ（seed_outputs）が無い"]
    paths = []
    for expected, item in enumerate(outputs):
        path = os.path.join(reference_dir, item["file"])
        if int(item["seed"]) != expected:
            return paths, [f"参照の記録の {part} の種が 0 から続いていない（{expected} の代わりに {item['seed']}）"]
        if not os.path.isfile(path):
            return paths, [f"記録にある種の出力 {item['file']} が無い"]
        with open(path, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        if digest != item["sha256"]:
            return paths, [f"種の出力 {item['file']} が参照の記録のハッシュと合わない（今回の描画の出力でない）"]
        paths.append(path)
    return paths, []


def missing_required(spec, measured):
    """定義の required のうち、組か視点が測られなかったもの（組を外して通さない）。measured は (組, 視点) の集合"""
    return [f"必須の層 {item['set']}/{item['view']}/{item['region']}/{item['stratum']} の組か視点が測られていない"
            for item in spec["required"] if (item["set"], item["view"]) not in measured]
