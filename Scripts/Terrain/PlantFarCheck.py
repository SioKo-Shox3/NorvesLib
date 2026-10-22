# 低木 1 株の遠くでの見え方（SCN-WLR-PLANT-REF-FAR）の照合。ReferenceCheck.py --plant-far の本体。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --plant-far --capture <撮影先> [--verify]
#       <撮影先> の下の組（GROUPS のフォルダ）を、同じ視点 plant_far の 2 倍の解像度の正解（ref2x）と比べ、
#       <撮影先>/plant_far_check.json と、株の周りを拡大して並べた画 <撮影先>/plant_far.png を書く。
#
# 測り方は --blur-static（遠景のぼけの静止）と同じ:
#   正解: 同じ視点を 2 倍の解像度・AA なし（-Aov 付き。起動引数は --blur-static の正解と同じ BLUR_REFERENCE_ARGS）で撮り、
#     線形の光で 2×2 を平均して縮めた画（超標本化）。
#   表示の値（PNG の符号化のまま）の輝度を、隣り合うガウスの差の帯域（BLUR_SIGMAS）に分け、帯域ごとに
#     細部の残り = <組, 正解> / <正解, 正解>（1 − 残り = 細部の欠け）、エイリアス = |組 − 残り × 正解|² / |正解|²
#   を範囲の画素で求め、細かい帯域（BLUR_FINE_BANDS）を正解の量で重みを付けてまとめる（ReferenceCheck.blur_compare）。
# 範囲（1 倍の画素。正解の深度から決める）:
#   株の印: 正解の 2 倍の画素のうち、深度から戻した位置が株の根元から水平に 樹冠の半径 + PLANT_MASK_MARGIN_M 以内で、
#     根元の地面の高さより PLANT_MASK_MIN_HEIGHT_M 以上高いもの。1 倍の画素の株の覆い = 2×2 のうち株の印の割合。
#   plant（株の全体）: 覆いが 0 より大きい画素と、その 1 画素の周り。
#   edge（アルファの縁）: plant のうち、3×3 の中に覆いが 1 未満の画素を含むもの（株と背景が混ざる縁。株の模型はアルファの
#     マスクを使わないので、縁は葉の帯と枝の輪郭）。
#   inner（株の中）: plant のうち edge でないもの（記録だけ。遠くの株は隙間が多く画素が残らないことがある）。
#   ground（近くの地面。対照）: 株の外接枠を GROUND_BOX_PX 広げた枠の地面のうち、帯域ごとに plant と空・深度の無い画素から
#     2σ（帯域の粗い側）画素より離れたもの。株と同じ組・同じ距離の地形の欠けとエイリアスで、株の量からこの値を引いた差を
#     株に固有の分として記録する。
# 合否（改善の閾値は付けない。値は記録するだけ）:
#   1. 組（必須は ref2x・base。aa-none・aa-fxaa はあれば比べる）: 撮影があり、入力の記録（inputs.json のエンジンの実装）と
#      Game.exe のハッシュがある（--verify に関係なく）。--verify は入力（RefSplit）が今のファイルと合う。全部の組のエンジンの
#      実装と Game.exe のハッシュが同じ（同じ Game.exe で撮った組だけを比べる）。
#      条件を替える起動引数（BLUR_CONTROLLED_PREFIXES）とログの印が組の約束どおりで、約束にない差し替えの印が無い。
#      共通の照明で撮り、比べる株の配置の行が 1 行で、どの組も同じ株・同じ位置。仮想テクスチャを使っていない。撮った時点で
#      ジオメトリのページの進行中が 0 で追い出しが無く、描くクラスタが溢れていない。PNG の大きさが約束どおり。
#   2. 正解の深度・カメラ（aov.json）があり、範囲の画素が下限（REGION_MIN_PX）以上。
#   3. 較正: 株の全体で、正解をぼかした画が細部の欠けに、雑音を足した画がエイリアスに数えられる（--blur-static と同じ基準）。
#   4. 各組・各範囲（edge・plant・ground）の細部の欠けとエイリアスが有限（判定できる）。1 つでも未判定なら落ちる。
import json
import os
import re

import numpy as np
from PIL import Image

import PlantRegions as PR

VIEW = "plant_far"
REFERENCE = "ref2x"
COMMON_LIGHT_ARG = "--wasteland-common-light"
HIDE_ARG = "--wasteland-hide=all-but-plant"
HIDE_LOGS = ("WASTELAND_HIDE ", "reference_plant_only=1")
# 組（撮影先の下のフォルダ）。args は条件を替える起動引数（BLUR_CONTROLLED_PREFIXES の頭のもの）の約束、logs は Game のログに
# 要る行の一部。ref2x の args・logs は ReferenceCheck の BLUR_REFERENCE_ARGS・BLUR_REFERENCE_LOGS を足して作る（group_specs）
GROUPS = (
    {"name": "base", "factor": "既定（TAA）", "args": (HIDE_ARG,), "logs": HIDE_LOGS, "required": True},
    {"name": "aa-none", "factor": "AA なし（1 倍）", "args": (HIDE_ARG, "--wasteland-aa=none"),
     "logs": HIDE_LOGS + ("WASTELAND_AA_PASS fxaa=off", "WASTELAND_BLUR_CASE aa=none "), "required": False},
    {"name": "aa-fxaa", "factor": "FXAA", "args": (HIDE_ARG, "--wasteland-aa=fxaa"),
     "logs": HIDE_LOGS + ("WASTELAND_BLUR_CASE aa=fxaa ",), "required": False},
)
# 株の印（正解の深度から戻した位置）
PLANT_MASK_MARGIN_M = 0.1
# 正解の地形の段の誤差（4K で 0.5 画素 = 30 m で約 8 mm）より十分大きく、株の根元の 1 画素（約 3 cm）程度
PLANT_MASK_MIN_HEIGHT_M = 0.03
# 近くの地面の枠の、株の外接枠からの広げ[画素]
GROUND_BOX_PX = 32
# 帯域を求める切り出しの、株の外接枠からの広げ[画素]（GROUND_BOX_PX + 最も粗いガウスの 3σ より大きく）
CROP_PAD_PX = 64
# 範囲の画素の下限（株の全体・縁は 30 m で 1 株の数百画素、地面は帯域ごと）
REGION_MIN_PX = {"edge": 100, "plant": 100, "ground": 1000}
REQUIRED_REGIONS = ("edge", "plant", "ground")
REGION_LABELS = {"edge": "アルファの縁", "plant": "株の全体", "inner": "株の中", "ground": "近くの地面（対照）"}
# 並べた画の拡大と、外接枠からの余白[画素]
PREVIEW_SCALE = 8
PREVIEW_PAD_PX = 8
# 並べた画の名前の字（日本語の字を持つもの。無ければ既定の字）
LABEL_FONT_PATHS = ("C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/msgothic.ttc")


def group_specs(RC):
    """正解を先頭にした組の約束の並び"""
    reference = {"name": REFERENCE, "factor": "正解（2 倍の解像度・AA なし・2×2 の線形の平均）",
                 "args": (HIDE_ARG,) + tuple(RC.BLUR_REFERENCE_ARGS), "logs": HIDE_LOGS + tuple(RC.BLUR_REFERENCE_LOGS),
                 "required": True}
    return (reference,) + GROUPS


def dilate(mask, radius):
    """正方形（(2r+1)×(2r+1)）の膨張"""
    if radius <= 0:
        return mask.copy()
    padded = np.pad(mask, radius, mode="constant", constant_values=False)
    out = np.zeros_like(mask)
    h, w = mask.shape
    for dy in range(2 * radius + 1):
        for dx in range(2 * radius + 1):
            out |= padded[dy:dy + h, dx:dx + w]
    return out


def check_group(RC, group_dir, spec, size, verify):
    """組の撮影の検査。(失敗の一覧, 記録, 配置)"""
    failures = []
    record = {"dir": RC.rel_dir(group_dir), "factor": spec["factor"]}
    inputs_ok, input_failures, engine = RC.split_engine_check(group_dir)
    record["inputs_current"] = inputs_ok
    record["identity"] = (engine or {}).get("identity")
    record["game_exe_sha256"] = (engine or {}).get("game_exe_sha256")
    # 入力の記録と Game.exe のハッシュは --verify に関係なく要る（同じ Game.exe で撮ったかを比べる元）。
    # 今のファイルとの一致だけを --verify で確かめる
    if engine is None or (verify and not inputs_ok):
        failures.extend(input_failures)
    if engine is not None and not record["game_exe_sha256"]:
        failures.append("inputs.json に撮影に使った Game.exe のハッシュが無い")
    args = RC.plate_capture_args(group_dir)
    record["game_args"] = args
    if args is None:
        return failures + ["metrics.json が無い"], record, None
    if COMMON_LIGHT_ARG not in args:
        failures.append(f"起動引数に {COMMON_LIGHT_ARG} が無い")
    controlled = sorted(a for a in args if a.startswith(RC.BLUR_CONTROLLED_PREFIXES))
    if controlled != sorted(spec["args"]):
        failures.append(f"条件を替える起動引数が約束と違う: {controlled}（約束 {sorted(spec['args'])}）")
    log_name = RC.view_game_log(group_dir, VIEW)
    log_path = os.path.join(group_dir, log_name) if log_name else ""
    if not log_name or not os.path.isfile(log_path):
        return failures + [f"Game のログが無い: {log_name or '（metrics.json に game_log が無い）'}"], record, None
    with open(log_path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    expected = tuple(spec["logs"]) + (f"WASTELAND_COMMON_LIGHT view={VIEW} ",)
    missing = [n for n in expected if n not in text]
    if missing:
        failures.append(f"Game のログに条件の印が無い: {missing}")
    unexpected = [n for n in RC.blur_forbidden_logs(expected) if n in text]
    if unexpected:
        failures.append(f"Game のログに約束にない差し替えの印がある: {unexpected}")
    vt_used = [float(v) for v in re.findall(r"VRAM_POOLS .*?vt_used_mb=(\d+(?:\.\d+)?)", text)]
    record["vt_used_mb_max"] = max(vt_used) if vt_used else None
    if not vt_used:
        failures.append("VRAM_POOLS の行が無い（仮想テクスチャの使用を確かめられない）")
    elif max(vt_used) > 0.0:
        failures.append(f"仮想テクスチャが使われている（vt_used_mb の最大 {max(vt_used):g}）")
    single_view = VIEW if log_name == f"{VIEW}.Game.log" else None
    pages = RC.geometry_pages_at_capture(text.splitlines(), single_view).get(VIEW)
    record["geometry_pages_at_capture"] = pages
    if pages is None:
        failures.append("Game のログに撮影の保存の行か、その前の GEOMETRY_PAGES の行が無い")
    else:
        in_flight = {k: pages.get(k) for k in ("wanted", "reading", "ready", "uploading")}
        if any(in_flight.values()) or len(pages["settle_evicted"]) != 1:
            failures.append(f"撮った時点でジオメトリのページが常駐しきっていない（進行中 {in_flight}、"
                            f"追い出しの数 {pages['settle_evicted']}）")
    capacity = [(int(a), int(b)) for a, b in
                re.findall(r"MEGA_CAPACITY draw_dropped=(\d+) .*?visible_list_overflow=(\d+) ", text)]
    record["mega_dropped_max"] = [max(c[0] for c in capacity), max(c[1] for c in capacity)] if capacity else None
    if not capacity:
        failures.append("MEGA_CAPACITY の行が無い（描画の溢れを確かめられない）")
    elif any(record["mega_dropped_max"]):
        failures.append(f"描くクラスタが溢れた（draw_dropped・visible_list_overflow の最大 {record['mega_dropped_max']}）")
    placement, count = PR.parse_placement(text)
    record["placement"] = placement
    if placement is None or count != 1:
        failures.append(f"比べる株の配置の行が 1 行でない（{count} 行）")
        placement = None
    png = os.path.join(group_dir, f"{VIEW}.png")
    if not os.path.isfile(png):
        failures.append(f"{VIEW}.png が無い")
    elif Image.open(png).size != size:
        failures.append(f"{VIEW}.png の大きさ {Image.open(png).size} が {size} でない")
    return failures, record, placement


def plant_coverage(RC, reference_dir, placement):
    """正解の深度から、1 倍の画素ごとの株の覆い（0〜1）と、2×2 のどれかに深度が無い（空）画素の印。(覆い, 空, 記録)"""
    with open(os.path.join(reference_dir, f"{VIEW}.aov.json"), encoding="utf-8") as f:
        aov = json.load(f)
    depth = RC.read_pfm_any(os.path.join(reference_dir, aov["depth"]["file"]))[..., 0].astype(np.float64)
    depth = np.where(np.isfinite(depth) & (depth > 0.0), depth, np.inf)
    camera = aov["camera"]
    height, width = depth.shape
    safe = np.where(np.isfinite(depth), depth, np.nan)
    positions = np.asarray(camera["position"], dtype=np.float64)[None, None, :] + \
        PR.pixel_rays(camera, width, height) * safe[..., None]
    x0, _, z0 = placement["position"]
    with np.errstate(invalid="ignore"):
        horizontal = np.hypot(positions[..., 0] - x0, positions[..., 2] - z0)
        above = positions[..., 1] - placement["ground_y"]
        mask = (horizontal <= placement["crown_radius"] + PLANT_MASK_MARGIN_M) & (above >= PLANT_MASK_MIN_HEIGHT_M) & \
            (above <= placement["height"] + PLANT_MASK_MARGIN_M)
    s = RC.BLUR_REFERENCE_SCALE
    h, w = height // s, width // s
    coverage = mask[:h * s, :w * s].reshape(h, s, w, s).mean(axis=(1, 3))
    sky = (~np.isfinite(depth[:h * s, :w * s])).reshape(h, s, w, s).any(axis=(1, 3))
    plant_depth = depth[mask]
    record = {"camera": {k: camera[k] for k in ("position", "forward", "vertical_fov_degrees", "aspect_ratio")},
              "reference_px": int(mask.sum()),
              "depth_m": [float(plant_depth.min()), float(np.median(plant_depth)), float(plant_depth.max())]
              if plant_depth.size else None,
              "meters_per_px": float(2.0 * np.tan(np.radians(camera["vertical_fov_degrees"]) / 2.0) / h *
                                     (np.median(plant_depth) if plant_depth.size else np.nan))}
    return coverage, sky, record


def regions(RC, coverage, sky):
    """範囲の印。(名前 → 帯域ごとの印の並び, 切り出しの枠 (上, 下, 左, 右), 記録)"""
    h, w = coverage.shape
    present = coverage > 0.0
    plant = dilate(present, 1)
    edge = plant & dilate(coverage < 1.0, 1)
    inner = plant & ~edge
    ys, xs = np.nonzero(present)
    bbox = (int(ys.min()), int(ys.max()) + 1, int(xs.min()), int(xs.max()) + 1)
    top, bottom = max(bbox[0] - CROP_PAD_PX, 0), min(bbox[1] + CROP_PAD_PX, h)
    left, right = max(bbox[2] - CROP_PAD_PX, 0), min(bbox[3] + CROP_PAD_PX, w)
    box = np.zeros_like(present)
    box[max(bbox[0] - GROUND_BOX_PX, 0):bbox[1] + GROUND_BOX_PX, max(bbox[2] - GROUND_BOX_PX, 0):bbox[3] + GROUND_BOX_PX] = True
    bands = len(RC.BLUR_SIGMAS) - 1
    masks = {"edge": [edge] * bands, "plant": [plant] * bands, "inner": [inner] * bands, "ground": []}
    for k in range(bands):
        margin = int(np.ceil(2.0 * RC.BLUR_SIGMAS[k + 1]))
        masks["ground"].append(box & ~dilate(plant | sky, margin))
    crop = (top, bottom, left, right)
    cropped = {name: [m[top:bottom, left:right] for m in band_masks] for name, band_masks in masks.items()}
    record = {"bbox": {"top": bbox[0], "bottom": bbox[1], "left": bbox[2], "right": bbox[3]},
              "crop": {"top": top, "bottom": bottom, "left": left, "right": right},
              "coverage_partial_px": int(((coverage > 0.0) & (coverage < 1.0)).sum()),
              "coverage_full_px": int((coverage >= 1.0).sum()),
              "pixels": {name: [int(m.sum()) for m in band_masks] for name, band_masks in masks.items()}}
    return cropped, crop, record


def write_preview(path, tiles, masks, bbox):
    """株の周りを PREVIEW_SCALE 倍に広げて横に並べる（名前を左上に書く）。最後に範囲（縁 赤・中 緑・地面 青）"""
    from PIL import ImageDraw, ImageFont
    font_path = next((p for p in LABEL_FONT_PATHS if os.path.isfile(p)), None)
    font = ImageFont.truetype(font_path, 16) if font_path else ImageFont.load_default()
    top, bottom = max(bbox["top"] - PREVIEW_PAD_PX, 0), bbox["bottom"] + PREVIEW_PAD_PX
    left, right = max(bbox["left"] - PREVIEW_PAD_PX, 0), bbox["right"] + PREVIEW_PAD_PX
    images = []
    for name, display in tiles:
        crop = (np.clip(display[top:bottom, left:right], 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
        images.append((name, crop))
    base = images[0][1].astype(np.float64)
    overlay = base * 0.5
    for key, channel in (("edge", 0), ("inner", 1), ("ground", 2)):
        region = masks[key][top:bottom, left:right]
        overlay[..., channel] = np.where(region, 255.0, overlay[..., channel])
    images.append(("範囲", overlay.astype(np.uint8)))
    ch, cw = images[0][1].shape[:2]
    sheet = Image.new("RGB", (cw * PREVIEW_SCALE * len(images), ch * PREVIEW_SCALE), (0, 0, 0))
    for index, (name, crop) in enumerate(images):
        tile = Image.fromarray(crop).resize((cw * PREVIEW_SCALE, ch * PREVIEW_SCALE), Image.NEAREST)
        ImageDraw.Draw(tile).text((6, 6), name, fill=(255, 255, 0), font=font)
        sheet.paste(tile, (index * cw * PREVIEW_SCALE, 0))
    sheet.save(path)
    return {"top": top, "bottom": bottom, "left": left, "right": right, "scale": PREVIEW_SCALE}


def run(RC, capture_dir, verify):
    failures = []
    specs = group_specs(RC)
    reference_size = (RC.BLUR_SIZE[0] * RC.BLUR_REFERENCE_SCALE, RC.BLUR_SIZE[1] * RC.BLUR_REFERENCE_SCALE)
    groups = {}
    placements = {}
    present = []
    for spec in specs:
        group_dir = os.path.join(capture_dir, spec["name"])
        if not os.path.isfile(os.path.join(group_dir, "metrics.json")):
            if spec["required"]:
                failures.append(f"{spec['name']}: 撮影の組が無い（{RC.rel_dir(group_dir)}）")
            continue
        size = reference_size if spec["name"] == REFERENCE else RC.BLUR_SIZE
        group_failures, record, placement = check_group(RC, group_dir, spec, size, verify)
        failures.extend(f"{spec['name']}: {f}" for f in group_failures)
        groups[spec["name"]] = record
        placements[spec["name"]] = placement
        present.append(spec)
    identities = {r.get("identity") for r in groups.values()}
    exes = {r.get("game_exe_sha256") for r in groups.values()}
    if len(identities) > 1:
        failures.append(f"組ごとのエンジンの実装が揃っていない（{len(identities)} 通り。同じ実装で撮った組だけを比べる）")
    same_exe = len(exes) == 1 and None not in exes
    if len(exes) > 1:
        failures.append(f"組ごとの Game.exe のハッシュが揃っていない（{len(exes)} 通り。同じ Game.exe で撮った組だけを比べる）")
    reference_placement = placements.get(REFERENCE)
    for name, placement in placements.items():
        if placement is None or reference_placement is None:
            continue
        if placement["id"] != reference_placement["id"] or \
                np.max(np.abs(np.subtract(placement["position"], reference_placement["position"]))) > 1e-4:
            failures.append(f"{name}: 比べる株が正解と違う（{placement['id']} {placement['position']}）")
    report = {"mode": "plant_far", "verify": bool(verify), "view": VIEW,
              "criteria": {"sigmas": list(RC.BLUR_SIGMAS), "fine_bands": list(RC.BLUR_FINE_BANDS),
                           "reference": REFERENCE, "reference_scale": RC.BLUR_REFERENCE_SCALE,
                           "plant_mask_margin_m": PLANT_MASK_MARGIN_M, "plant_mask_min_height_m": PLANT_MASK_MIN_HEIGHT_M,
                           "ground_box_px": GROUND_BOX_PX, "region_min_px": REGION_MIN_PX},
              "groups": groups}
    results = {}
    reference_dir = os.path.join(capture_dir, REFERENCE)
    aov_ok = all(os.path.isfile(os.path.join(reference_dir, f"{VIEW}.{s}")) for s in ("aov.json", "aov.depth.pfm", "png"))
    if not aov_ok:
        failures.append(f"{REFERENCE}: 正解の深度・カメラ（-Aov）か PNG が無いので比べない")
    elif reference_placement is None:
        failures.append(f"{REFERENCE}: 比べる株の配置が読めないので比べない")
    else:
        coverage, sky, coverage_record = plant_coverage(RC, reference_dir, reference_placement)
        report["coverage"] = coverage_record
        if not (coverage > 0.0).any():
            failures.append(f"{REFERENCE}: 正解の深度に株の画素が無い")
        else:
            masks, (top, bottom, left, right), region_record = regions(RC, coverage, sky)
            report["regions"] = region_record
            for name, minimum in REGION_MIN_PX.items():
                if min(region_record["pixels"][name]) < minimum:
                    failures.append(f"{REGION_LABELS[name]}（{name}）の画素が {min(region_record['pixels'][name])} で "
                                    f"{minimum} に足りない")
            reference_display = RC.blur_load_display(os.path.join(reference_dir, f"{VIEW}.png"), RC.BLUR_REFERENCE_SCALE)
            reference_luma = (reference_display @ RC.LUMA)[top:bottom, left:right]
            reference_bands = RC.blur_bands(reference_luma)
            # 測り方の較正（ぼかした正解と、雑音を足した正解。株の全体で数える）
            plant_masks = masks["plant"]
            blurred = RC.blur_compare(reference_bands, RC.blur_bands(RC.blur_gaussian(reference_luma, RC.BLUR_CALIB_BLUR_SIGMA)),
                                      plant_masks)
            noise_rms = float(np.sqrt(np.mean(reference_bands[0][plant_masks[0]] ** 2)))
            rng = np.random.default_rng(RC.BLUR_CALIB_SEED)
            noisy = RC.blur_compare(reference_bands,
                                    RC.blur_bands(reference_luma + rng.normal(0.0, noise_rms, reference_luma.shape)),
                                    plant_masks)
            report["calibration"] = {"blur": blurred, "noise": noisy, "noise_rms": noise_rms}
            calibrated = blurred is not None and noisy is not None
            if not calibrated:
                failures.append("較正を求められない（正解の帯域が空）")
            else:
                if blurred["bands"][0]["detail_loss"] < RC.BLUR_CALIB_BLUR_MIN_LOSS or \
                        blurred["fine_alias"] > RC.BLUR_CALIB_BLUR_MAX_ALIAS:
                    calibrated = False
                    failures.append(f"較正（ぼかし）が細部の欠けに数えられない（帯域 0 の欠け "
                                    f"{blurred['bands'][0]['detail_loss']:.3f}・エイリアス {blurred['fine_alias']:.3f}）")
                if abs(noisy["bands"][0]["detail_kept"] - 1.0) > RC.BLUR_CALIB_NOISE_GAIN or \
                        noisy["bands"][0]["alias"] < RC.BLUR_CALIB_NOISE_MIN_ALIAS:
                    calibrated = False
                    failures.append(f"較正（雑音）がエイリアスに数えられない（帯域 0 の残り "
                                    f"{noisy['bands'][0]['detail_kept']:.3f}・エイリアス {noisy['bands'][0]['alias']:.3f}）")
                print(f"REFERENCE_CHECK plant_far calibration blur_loss0={blurred['bands'][0]['detail_loss']:.3f} "
                      f"blur_alias={blurred['fine_alias']:.3f} noise_kept0={noisy['bands'][0]['detail_kept']:.3f} "
                      f"noise_alias0={noisy['bands'][0]['alias']:.3f}")
            reference_linear = (RC.blur_decode_srgb(reference_display) @ RC.LUMA)[top:bottom, left:right][plant_masks[0]]
            displays = [(REFERENCE, reference_display)]
            for spec in present:
                if spec["name"] == REFERENCE:
                    continue
                png = os.path.join(capture_dir, spec["name"], f"{VIEW}.png")
                if not os.path.isfile(png) or Image.open(png).size != RC.BLUR_SIZE:
                    continue
                display = RC.blur_load_display(png, 1)
                displays.append((spec["name"], display))
                bands = RC.blur_bands((display @ RC.LUMA)[top:bottom, left:right])
                entry = {"factor": spec["factor"]}
                for region in ("edge", "plant", "inner", "ground"):
                    compared = RC.blur_compare(reference_bands, bands, masks[region]) \
                        if min(region_record["pixels"][region]) > 0 else None
                    judged = compared is not None and calibrated and \
                        min(region_record["pixels"][region]) >= REGION_MIN_PX.get(region, 1) and \
                        bool(np.isfinite([compared["fine_detail_loss"], compared["fine_alias"]]).all())
                    entry[region] = dict(compared or {}, status="判定" if judged else "未判定")
                    if region in REQUIRED_REGIONS and not judged:
                        failures.append(f"{spec['name']}: {REGION_LABELS[region]}（{region}）の細部の欠けとエイリアスを判定できない")
                # 株の全体の線形の輝度の平均の比（組 ÷ 正解。明るい地面の前の暗い株なので、1 より小さいほど株が濃く太い）
                linear = (RC.blur_decode_srgb(display) @ RC.LUMA)[top:bottom, left:right][plant_masks[0]]
                entry["plant_linear_ratio"] = float(linear.mean() / reference_linear.mean())
                if all(entry[r].get("fine_detail_loss") is not None for r in ("edge", "plant", "ground")):
                    entry["plant_specific"] = {
                        "edge_loss_minus_ground": entry["edge"]["fine_detail_loss"] - entry["ground"]["fine_detail_loss"],
                        "edge_alias_minus_ground": entry["edge"]["fine_alias"] - entry["ground"]["fine_alias"],
                        "plant_loss_minus_ground": entry["plant"]["fine_detail_loss"] - entry["ground"]["fine_detail_loss"],
                        "plant_alias_minus_ground": entry["plant"]["fine_alias"] - entry["ground"]["fine_alias"]}
                results[spec["name"]] = entry
                for region in ("edge", "plant", "inner", "ground"):
                    row = entry[region]
                    if "fine_detail_loss" not in row:
                        print(f"REFERENCE_CHECK plant_far group={spec['name']} region={region} status={row['status']}")
                        continue
                    print(f"REFERENCE_CHECK plant_far group={spec['name']} region={region} status={row['status']} "
                          f"pixels={min(region_record['pixels'][region])} loss={row['fine_detail_loss']:.3f} "
                          f"alias={row['fine_alias']:.3f} "
                          "kept_by_band=" + ",".join(f"{b['detail_kept']:.2f}" for b in row["bands"]))
                if "plant_specific" in entry:
                    print(f"REFERENCE_CHECK plant_far group={spec['name']} plant_specific " +
                          " ".join(f"{k}={v:+.3f}" for k, v in entry["plant_specific"].items()) +
                          f" plant_linear_ratio={entry['plant_linear_ratio']:.3f}")
            full_masks = {}
            for name in ("edge", "inner", "ground"):
                full = np.zeros(coverage.shape, dtype=bool)
                full[top:bottom, left:right] = masks[name][0]
                full_masks[name] = full
            preview_path = os.path.join(capture_dir, f"{VIEW}.png")
            report["preview"] = write_preview(preview_path, displays, full_masks, region_record["bbox"])
            report["preview_png"] = RC.rel_dir(preview_path)
            print(f"REFERENCE_CHECK plant_far regions edge={min(region_record['pixels']['edge'])} "
                  f"plant={min(region_record['pixels']['plant'])} inner={min(region_record['pixels']['inner'])} "
                  f"ground_min={min(region_record['pixels']['ground'])} meters_per_px={coverage_record['meters_per_px']:.4f} "
                  f"bbox={region_record['bbox']}")
    missing = [s["name"] for s in specs if s["required"] and s["name"] != REFERENCE and s["name"] not in results]
    if missing and aov_ok:
        failures.append(f"比べられなかった組がある: {missing}")
    report["results"] = results
    inputs_current = bool(groups) and all(r.get("inputs_current") for r in groups.values())
    report["inputs_current"] = inputs_current
    report["failures"] = failures
    report_path = os.path.join(capture_dir, "plant_far_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for name, record in groups.items():
        print(f"REFERENCE_CHECK plant_far group={name} inputs_current={int(bool(record.get('inputs_current')))} "
              f"game_exe={str(record.get('game_exe_sha256'))[:16]} args={record.get('game_args')}")
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=plant_far groups={len(groups)} "
          f"compared={len(results)} inputs_current={int(inputs_current)} same_game_exe={int(same_exe)} "
          f"report={report_path}")
    return 0 if not failures else 1
