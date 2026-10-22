# 荒野の RTGI が小さな遮蔽（0.1〜2 m）を落としていないかの診断（SCN-WLR-OCCLUSION-DIAG。ReferenceCheck.py --occlusion-diag から
# 呼ぶ。単独では動かさない）。
#
# エンジンの撮影（CaptureScene.ps1 -Aov。開発ビルドの --capture-aov が RTGI の遮蔽の診断を頼む）の視点ごとに読む。
#   <視点>.aov.rtgi_temporal_code.pfm … RTGI の時間の蓄積の後の出力の a = 2 × 符号 + 空の分（Core の DiffuseIndirect.comp）。
#                                        符号 = 生の空（この画の 1 本目の光線が空へ抜けたら 1）+ 2 × 読み飛ばしの分類
#                                        （0 = 読み飛ばし無し、1 = 0.1 m 未満、2 = 0.1〜2 m、3 = 2 m 以上の自分の面への当たり）
#   <視点>.aov.rtgi_filtered_sky.pfm  … 空間のフィルタ（CrossBilateralDenoise.comp）の後の空の分
#   <視点>.aov.depth.pfm・.aov.normal.pfm・.aov.sun_visibility.pfm、<視点>.Game.log
# 空の分はコサイン標本の光線が空へ抜けた割合（拡散の空の光の遮蔽の割合）。生の値は 1 画 1 本の 0/1 なので、近傍の平均で比べる。
#   (1) TLAS: RT のシーンの選択の最後の行（RT_SCENE）と、崖錐の岩の散布（WASTELAND_TALUS。影を落とす岩とそれ以外）、
#       RTGI の光線が調べる instance の印（影を落とす物だけ。Common/RayTracingInstanceMask.glsl）
#   (2) 近い当たりの読み飛ばし: 分類ごとの画素の割合・読み飛ばして空へ抜けた割合と、距離の帯ごとの内訳・場所の画
#   (3) 光線の始点のずらし: シェーダーと LightingPass の定数（RAY_ORIGIN_OFFSET・RTGI_RAY_MINIMUM_DISTANCE）
#   (4) 生 → 時間の蓄積の後 → 空間のフィルタの後の空の分: 遮られた所（生の近傍の平均が低い画素）の平均と、各段の差
# 落ちる: 診断の画像が無い・読めない・RTGI の値が無い（-1）画素が地面の 10 % を超える、--verify で撮影の入力が今のファイルと違う。
# 書く: <撮影>/occlusion_diag_check.json と、視点ごとの <撮影>/occlusion_diag/<視点>.png（生の近傍の平均・時間の蓄積の後・
#       空間のフィルタの後・読み飛ばしの場所の 4 枚を並べた画）。
import json
import os
import re

import numpy as np
from PIL import Image

import GrayRegions as GR

REPORT = "occlusion_diag_check.json"
# 生の 1 画 1 本の値を近傍で平均する箱の大きさ（画素）。17 × 17 = 289 本で、空の分の標準誤差は 0.03 以下
RAW_BOX = 17
# 遮られた所の判定: 生の近傍の平均がこれより低い画素（溝の奥・岩の下・株の根元）
OCCLUDED_RAW = 0.6
# 距離の帯（m。視線方向の深度）
DISTANCE_BANDS = ((0.0, 20.0), (20.0, 100.0), (100.0, 1000.0), (1000.0, float("inf")))
SKIP_CLASS_NAMES = ("none", "below_0.1m", "0.1_to_2m", "above_2m")
RT_SCENE = re.compile(r"RT_SCENE instances=(\d+) excluded=(\d+) path=(\w+) mesh=(\d+) mega=(\d+) candidates=(\d+) "
                      r"out_of_range=(\d+) over_limit=(\d+) not_ready=(\d+)")
RTGI_HITS = re.compile(r"RTGI_HITS reason=(\w+) dispatches=(\d+) rays=(\d+) hits=(\d+) mesh_hits=(\d+) mega_hits=(\d+)")
TALUS = re.compile(r"WASTELAND_TALUS casters=(\d+) fragments_placed=(\d+)")
HIDE = re.compile(r"WASTELAND_HIDE [^\r\n]*")
DIAG_ON = "RTGI_OCCLUSION_DIAG on"


def last_match(pattern, lines):
    for line in reversed(lines):
        found = pattern.search(line)
        if found:
            return found
    return None


def box_mean(values, valid, size):
    """valid の画素だけの size × size の箱の平均（積分画像）。valid の無い箱は NaN"""
    pad = size // 2
    v = np.pad(np.where(valid, values, 0.0).astype(np.float64), pad, mode="constant")
    w = np.pad(valid.astype(np.float64), pad, mode="constant")
    def integral(a):
        s = np.zeros((a.shape[0] + 1, a.shape[1] + 1))
        s[1:, 1:] = a.cumsum(0).cumsum(1)
        return s[size:, size:] - s[:-size, size:] - s[size:, :-size] + s[:-size, :-size]
    total = integral(v)
    count = integral(w)
    with np.errstate(invalid="ignore", divide="ignore"):
        return np.where(count > 0, total / np.maximum(count, 1.0), np.nan)


def read_constants(repo_root):
    """光線の始点のずらしと読み飛ばしの定数をシェーダーと LightingPass から読む"""
    result = {}
    shader = os.path.join(repo_root, "Assets", "Shaders", "RTGI", "DiffuseIndirect.comp")
    with open(shader, encoding="utf-8-sig") as f:
        text = f.read()
    for name in ("RAY_ORIGIN_OFFSET", "RTGI_PROXY_SELF_HIT_HEIGHT_MAX", "RTGI_PROXY_SELF_HIT_HEIGHT_PER_METER",
                 "RTGI_PROXY_EXIT_HEIGHT_SCALE"):
        found = re.search(rf"const float {name} = ([0-9.eE+-]+);", text)
        result[name] = float(found.group(1)) if found else None
    lighting = os.path.join(repo_root, "Library", "Core", "Private", "Rendering", "LightingPass.cpp")
    with open(lighting, encoding="utf-8-sig") as f:
        found = re.search(r"RTGI_RAY_MINIMUM_DISTANCE = ([0-9.eE+-]+)f;", f.read())
    result["RTGI_RAY_MINIMUM_DISTANCE"] = float(found.group(1)) if found else None
    return result


def to_rgb8(values, low=0.0, high=1.0):
    scaled = np.clip((np.nan_to_num(values, nan=0.0) - low) / (high - low), 0.0, 1.0)
    gray = (scaled * 255.0 + 0.5).astype(np.uint8)
    return np.stack([gray, gray, gray], axis=-1)


def check_view(capture_dir, view):
    failures = []
    result = {"view": view}
    paths = {key: os.path.join(capture_dir, f"{view}.aov.{key}.pfm")
             for key in ("rtgi_temporal_code", "rtgi_filtered_sky", "depth", "normal", "sun_visibility")}
    for key, path in paths.items():
        if not os.path.isfile(path):
            failures.append(f"{os.path.basename(path)} が無い（開発ビルドの -Aov で撮る）")
    if failures:
        return {"view": view, "pass": False, "failures": failures}, None
    code_image = GR.read_pfm(paths["rtgi_temporal_code"])[..., 0]
    filtered = GR.read_pfm(paths["rtgi_filtered_sky"])[..., 0]
    depth = GR.read_pfm(paths["depth"])[..., 0]
    geometry = np.isfinite(depth) & (depth > 0.0)
    available = geometry & (code_image >= 0.0) & (filtered >= 0.0)
    unavailable_ratio = 1.0 - available.sum() / max(int(geometry.sum()), 1)
    result["geometry_pixels"] = int(geometry.sum())
    result["unavailable_ratio"] = float(unavailable_ratio)
    if unavailable_ratio > 0.10:
        failures.append(f"RTGI の診断の値が無い画素が地面の {unavailable_ratio:.3f}（RTGI が動いていないか診断が無効）")

    code = np.floor(np.maximum(code_image, 0.0) * 0.5)
    temporal = np.clip(code_image - 2.0 * code, 0.0, 1.0)
    raw = np.mod(code, 2.0)
    skip_class = np.floor(code * 0.5).astype(np.int32)
    raw_mean = box_mean(raw, available, RAW_BOX)

    stages = {}
    for name, values in (("raw", raw), ("raw_box", raw_mean), ("temporal", temporal), ("filtered", filtered)):
        stages[name] = float(np.nanmean(values[available])) if available.any() else None
    result["sky_mean"] = stages

    # 遮られた所（生の近傍の平均が低い）で、各段の空の分がどれだけ戻ってしまうか
    occluded = available & (raw_mean < OCCLUDED_RAW)
    result["occluded_pixels"] = int(occluded.sum())
    result["occluded_ratio"] = float(occluded.sum() / max(int(available.sum()), 1))
    if occluded.any():
        occluded_stats = {
            "raw_box": float(np.nanmean(raw_mean[occluded])),
            "temporal": float(temporal[occluded].mean()),
            "temporal_box": float(np.nanmean(box_mean(temporal, available, RAW_BOX)[occluded])),
            "filtered": float(filtered[occluded].mean()),
        }
        occluded_stats["temporal_minus_raw"] = occluded_stats["temporal"] - occluded_stats["raw_box"]
        occluded_stats["filtered_minus_temporal"] = occluded_stats["filtered"] - occluded_stats["temporal"]
        result["occluded_sky"] = occluded_stats

    # 読み飛ばしの分類の割合（全体と距離の帯ごと）と、読み飛ばして空へ抜けた割合
    def skip_stats(mask):
        count = int(mask.sum())
        stats = {"pixels": count}
        for index, name in enumerate(SKIP_CLASS_NAMES):
            in_class = mask & (skip_class == index)
            stats[name] = float(in_class.sum() / max(count, 1))
            if index > 0:
                stats[f"{name}_escaped"] = float((in_class & (raw > 0.5)).sum() / max(count, 1))
        stats["raw_sky"] = float(raw[mask].mean()) if count else None
        return stats

    result["skip"] = skip_stats(available)
    result["skip_by_distance"] = []
    for low, high in DISTANCE_BANDS:
        band = available & (depth >= low) & (depth < high)
        entry = {"from_m": low, "to_m": None if high == float("inf") else high}
        entry.update(skip_stats(band))
        result["skip_by_distance"].append(entry)
    if occluded.any():
        result["skip_in_occluded"] = skip_stats(occluded)

    # 読み飛ばしの高さの上限が距離の項で広がる画素（max(0.5 m, 5e-4 × 距離) が 0.5 m を越える = 1 km より遠い）
    result["distance_term_active_ratio"] = float((available & (depth > 1000.0)).sum() / max(int(available.sum()), 1))

    log_path = os.path.join(capture_dir, f"{view}.Game.log")
    lines = []
    if os.path.isfile(log_path):
        with open(log_path, encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
    scene = last_match(RT_SCENE, lines)
    if scene:
        keys = ("instances", "excluded", "path", "mesh", "mega", "candidates", "out_of_range", "over_limit", "not_ready")
        result["rt_scene"] = {k: (scene.group(i + 1) if k == "path" else int(scene.group(i + 1))) for i, k in enumerate(keys)}
    hits = last_match(RTGI_HITS, lines)
    if hits:
        keys = ("reason", "dispatches", "rays", "hits", "mesh_hits", "mega_hits")
        result["rtgi_hits"] = {k: (hits.group(i + 1) if k == "reason" else int(hits.group(i + 1))) for i, k in enumerate(keys)}
    talus = last_match(TALUS, lines)
    if talus:
        result["talus"] = {"shadow_casters": int(talus.group(1)), "non_casters": int(talus.group(2))}
    hide = last_match(HIDE, lines)
    result["hide"] = hide.group(0) if hide else None
    result["diag_logged"] = any(DIAG_ON in line for line in lines)
    if lines and not result["diag_logged"]:
        failures.append("Game.log に RTGI_OCCLUSION_DIAG on が無い")

    # 画: 生の近傍の平均・時間の蓄積の後・空間のフィルタの後・読み飛ばしの場所（灰色 = 地面、黄 = 0.1 m 未満、
    # 赤 = 0.1〜2 m、紫 = 2 m 以上。読み飛ばして空へ抜けた画素は明るく）を 2 × 2 に並べる
    panels = [to_rgb8(np.where(available, raw_mean, 0.0)), to_rgb8(np.where(available, temporal, 0.0)),
              to_rgb8(np.where(available, filtered, 0.0))]
    skip_image = np.where(geometry[..., None], 60, 0).astype(np.uint8) * np.ones((1, 1, 3), np.uint8)
    colors = {1: (255, 220, 0), 2: (255, 40, 40), 3: (200, 60, 255)}
    for index, color in colors.items():
        in_class = available & (skip_class == index)
        dim = np.array(color, np.float32) * 0.5
        skip_image[in_class & (raw < 0.5)] = dim.astype(np.uint8)
        skip_image[in_class & (raw > 0.5)] = np.array(color, np.uint8)
    panels.append(skip_image)
    top = np.concatenate(panels[:2], axis=1)
    bottom = np.concatenate(panels[2:], axis=1)
    image = Image.fromarray(np.concatenate([top, bottom], axis=0))
    image = image.resize((image.width // 2, image.height // 2), Image.BILINEAR)
    result["failures"] = failures
    result["pass"] = not failures
    return result, image


def run(rc, capture_dir, views, verify):
    views = list(views) or rc.views_from_metrics(capture_dir) or []
    failures = []
    report = {"mode": "occlusion_diag", "verify": bool(verify),
              "capture": os.path.relpath(capture_dir, rc.REPO_ROOT), "views": [],
              "raw_box_pixels": RAW_BOX, "occluded_raw_below": OCCLUDED_RAW,
              "constants": read_constants(rc.REPO_ROOT),
              "ray_mask": "RTGI の光線は影を落とす instance（RayTracingInstanceMaskShadowCaster）だけを調べる"}
    if not views:
        failures.append("視点が無い（metrics.json が無いか --views が空）")
    inputs_ok, input_failures, _ = rc.check_inputs(capture_dir)
    if verify:
        failures.extend(input_failures)
    report["inputs_current"] = inputs_ok
    image_dir = os.path.join(capture_dir, "occlusion_diag")
    os.makedirs(image_dir, exist_ok=True)
    for view in views:
        result, image = check_view(capture_dir, view)
        if image is not None:
            image_path = os.path.join(image_dir, f"{view}.png")
            image.save(image_path)
            result["image"] = os.path.relpath(image_path, rc.REPO_ROOT)
        report["views"].append(result)
        sky = result.get("sky_mean") or {}
        occ = result.get("occluded_sky") or {}
        skip = result.get("skip") or {}
        print(f"REFERENCE_CHECK occlusion_diag view={view} pass={int(result['pass'])} "
              f"unavailable={result.get('unavailable_ratio', float('nan')):.4f} "
              f"sky_raw={sky.get('raw')} sky_temporal={sky.get('temporal')} sky_filtered={sky.get('filtered')} "
              f"occluded_ratio={result.get('occluded_ratio')} occ_raw_box={occ.get('raw_box')} "
              f"occ_temporal={occ.get('temporal')} occ_filtered={occ.get('filtered')} "
              f"skip_small={skip.get('below_0.1m')} skip_0.1_2m={skip.get('0.1_to_2m')} "
              f"skip_0.1_2m_escaped={skip.get('0.1_to_2m_escaped')} skip_above_2m={skip.get('above_2m')}")
        failures.extend(f"{view}: {f}" for f in result["failures"])
    report["failures"] = failures
    report_path = os.path.join(capture_dir, REPORT)
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=occlusion_diag views={len(views)} "
          f"inputs_current={int(inputs_ok)} report={report_path}")
    return 0 if not failures else 1
