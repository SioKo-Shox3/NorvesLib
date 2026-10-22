# 荒野の昼の表示の変換（トーンマップ・グレーディング・露出の補正）の候補を、保存した HDR にオフラインで掛けて並べ、
# 選んだ変換をエンジンの撮影と照らす（CompareReference.py --look-select）。記録は Docs/RenderingValidation/WastelandLookCamera.md。
#
# 候補（DisplayTransform.py の式。ブルームと FXAA は入れない。ビネットは撮影の値のまま）:
#   曲線 7 通り × 露出の補正 −1.0〜+1.5 EV の 0.5 段刻み 6 通り。露出の補正は今の荒野の値（kCameraOutputCompensationEV）からの差で、
#   HDR の撮影のプリエクスポージャに 2^Δ を掛ける。
# 距離（候補ごと。晴れの写真 far_01・cliff_01・ground_02 だけ）:
#   写真 p・領域 r（LookRegions.json の 青空・近景の日なたの地面・崖・3 km より先の地形の帯。写真と撮影の雲は合っていないので雲は使わない）ごとに
#     e(p, r) = |log2(Y_c / Y_p)| + |log2(RG_c / RG_p)| + |log2(BR_c / BR_p)|
#   Y・RG（R ÷ G）・BR（B ÷ R）は sRGB の 8 bit を線形へ戻した表示の値の領域の平均（LookMeasure.color_stats）。c は候補を掛けた撮影、p は写真。
#   重みは領域ごとに 1（地面と崖の明るさの差は植生・材質の差を含むので、空より重くしない）。
#   d(p) = 写真に在る領域の e の平均、距離 D = 3 写真の d の平均。
# 白飛び・黒つぶれ: 6 視点の画面全体で、どれかのチャンネルが 254 以上・すべてのチャンネルが 4 以下の画素の割合（視点の平均）。
# 選び方: 荒野の昼のカメラだけで選べる曲線（CAMERA_CURVES）で、白飛び・黒つぶれがどちらも今の設定（current・Δ 0）の値
#   + CLIP_TOLERANCE 以下の候補のうち D が最小の候補。
#   その D が今の設定の D の (1 − MIN_GAIN) 倍より大きければ、今の設定を選ぶ（差が小さいときは変えない）。
#   写真との距離は写真の現像・天気・材質の差を含むので、作風を選ぶ参考に留め、物理の正しさの証拠にしない。
#
# --capture <撮影の出力先>（--display-check=scene -Hdr で撮った 6 視点）を渡すと、選んだ候補をその HDR に掛けた 8 bit の値と PNG を照らす:
#   撮影の記録（hdr.json の post）でブルームと FXAA が外れ、演算子・グレーディングが選んだ候補と同じで、プリエクスポージャが
#   候補の HDR のプリエクスポージャ × 2^Δ と相対 1e-4 で合い、3 成分とも ±SCENE_WITHIN_LEVELS 段（8 bit）の中の画素が
#   SCENE_WITHIN_RATIO 以上（ReferenceCheck.py --display の scene の組と同じ基準）なら合格。
# 出力: <出力先>/look_select.json・look_select.md（文書に貼る表）・look_select_<写真>.png（写真と、今の設定・選んだ候補）。
# --verify: 6 視点の HDR と 3 写真の注釈がそろい、候補をすべて評価でき、--capture の照合が合格なら終了コード 0。
import json
import math
import os
import sys

import numpy as np
from PIL import Image

import DisplayTransform as DT
from CompareLight import load_font, srgb_to_linear
from CompareReference import PANEL_SIZE, PHOTO_DIR, PHOTOS, fit_photo
from LookMeasure import color_stats, load_regions, polygon_mask
from ReferenceCheck import SCENE_WITHIN_LEVELS, SCENE_WITHIN_RATIO, check_display_record

CLEAR_PHOTOS = ("far_01", "cliff_01", "ground_02")
DISTANCE_REGIONS = ("blue_sky", "ground", "cliff", "far_band")
EXPOSURE_OFFSETS = (-1.0, -0.5, 0.0, 0.5, 1.0, 1.5)
# 白飛び・黒つぶれの許し（画面の割合。数値の揺れの分だけ）と、今の設定から変える最小の改善（D の割合）
CLIP_TOLERANCE = 1e-4
MIN_GAIN = 0.02
CLIP_CODE = 254
CRUSH_CODE = 4
PRE_EXPOSURE_TOLERANCE = 1e-4
NEUTRAL_GRADING = {"grading_mode": 1, "contrast": 1.0, "saturation": 1.0, "temperature": 0.0}
# 曲線: 名前 → (説明, 演算子, グレーディングの差し替え〔None は LUT でグレーディングを掛けない〕)
# 荒野の昼のカメラだけで選べる曲線（CameraToneMapCurve の NeutralLinear と、View の既定の演算子 ACES Filmic を使う ViewDefault）。
# ほかの曲線は View の演算子（全シーン共通）を替えないと使えないので、並べるだけで選ばない
CAMERA_CURVES = ("current", "neutral_old_grade", "neutral_mild", "aces_filmic")
CURVES = {
    "current": ("NeutralLinear・中立のグレーディング（今の荒野）", DT.OPERATOR_NEUTRAL_LINEAR, NEUTRAL_GRADING),
    "neutral_old_grade": ("NeutralLinear・コントラスト 1.35・彩度 1.2・色温度 0.05（SCN-WLR-TONEMAP-EVAL の前の荒野）",
                          DT.OPERATOR_NEUTRAL_LINEAR,
                          {"grading_mode": 1, "contrast": 1.35, "saturation": 1.2, "temperature": 0.05}),
    "neutral_mild": ("NeutralLinear・コントラスト 1.15・彩度 1.1・色温度 0.05（WastelandToneMapEval.md の候補）",
                     DT.OPERATOR_NEUTRAL_LINEAR,
                     {"grading_mode": 1, "contrast": 1.15, "saturation": 1.1, "temperature": 0.05}),
    "aces20_lut": ("ACES 2.0 SDR の LUT（グレーディングを掛けない）", DT.OPERATOR_ACES20_LUT, None),
    "aces_filmic": ("ACES Filmic の近似（肩のある filmic）・中立", DT.OPERATOR_ACES_FILMIC, NEUTRAL_GRADING),
    "uncharted2": ("Uncharted 2（肩のある filmic、白 11.2）・中立", DT.OPERATOR_UNCHARTED2, NEUTRAL_GRADING),
    "reinhard": ("Reinhard（x ÷ (1 + x)）・中立", DT.OPERATOR_REINHARD, NEUTRAL_GRADING),
}
CURRENT = ("current", 0.0)


def candidate_name(curve, offset):
    return f"{curve}{offset:+.1f}EV"


def candidate_params(base, curve):
    """撮影の表示変換の値（ビネットなど）を元に、曲線とグレーディングだけを差し替える"""
    _, operator, grading = CURVES[curve]
    params = dict(base)
    params["operator"] = operator
    params["bypass"] = False
    params["color_filter"] = [1.0, 1.0, 1.0, 1.0]
    params["brightness"] = 0.0
    params["look_lut_intensity"] = 1.0 if operator == DT.OPERATOR_ACES20_LUT else 0.0
    if grading is not None:
        params.update(grading)
    return params


def render(hdr, pre_exposure, params, lut):
    codes, _ = DT.display_transform(DT.expose(hdr, pre_exposure), params, lut)
    return codes


def clip_crush(codes):
    peak = codes.max(axis=-1)
    return float((peak >= CLIP_CODE).mean()), float((peak <= CRUSH_CODE).mean())


def region_masks(annotation, size):
    masks = {}
    for name in DISTANCE_REGIONS:
        spec = annotation.get(name, {})
        if "absent" in spec or "polygon" not in spec:
            continue
        masks[name] = (polygon_mask(spec["polygon"], PANEL_SIZE), polygon_mask(spec.get("capture_polygon", spec["polygon"]), size))
    return masks


def region_error(capture, photo):
    terms = []
    for key in ("luminance", "r_over_g", "b_over_r"):
        c, p = capture.get(key), photo.get(key)
        if not c or not p or c <= 0 or p <= 0:
            return None
        terms.append(abs(math.log2(c / p)))
    return {"y": terms[0], "rg": terms[1], "br": terms[2], "sum": sum(terms)}


def load_view(hdr_dir, view):
    pfm_path = os.path.join(hdr_dir, view + ".hdr.pfm")
    meta_path = os.path.join(hdr_dir, view + ".hdr.json")
    with open(meta_path, encoding="utf-8") as file:
        meta = json.load(file)
    return DT.read_pfm(pfm_path), meta


def check_capture(capture_dir, hdr_metas, selected, lut):
    """--display-check=scene の撮影で、選んだ候補のオフラインの再現とエンジンの出力を照らす"""
    curve, offset = selected
    _, operator, grading = CURVES[curve]
    results = []
    for entry in PHOTOS:
        view = entry["view"]
        result = {"view": view, "pass": False, "failures": []}
        fails = result["failures"]
        png_path = os.path.join(capture_dir, view + ".png")
        pfm_path = os.path.join(capture_dir, view + ".hdr.pfm")
        meta_path = os.path.join(capture_dir, view + ".hdr.json")
        missing = [os.path.basename(p) for p in (png_path, pfm_path, meta_path) if not os.path.isfile(p)]
        if missing:
            fails.append("ファイルが無い: " + "・".join(missing))
            results.append(result)
            continue
        with open(meta_path, encoding="utf-8") as file:
            meta = json.load(file)
        fails.extend(check_display_record(meta, False, operator))
        post = meta.get("post", {})
        if grading is not None:
            for key in ("grading_mode", "contrast", "saturation", "temperature"):
                recorded = post.get(key)
                if recorded is None or abs(float(recorded) - float(grading[key])) > 1e-6:
                    fails.append(f"グレーディングの {key} が選んだ候補と違う（記録 {recorded}、候補 {grading[key]}）")
        expected_pre = float(hdr_metas[view]["pre_exposure"]) * 2.0 ** offset
        pre_exposure = float(meta.get("pre_exposure", 0.0))
        if not abs(pre_exposure / expected_pre - 1.0) <= PRE_EXPOSURE_TOLERANCE:
            fails.append(f"プリエクスポージャ {pre_exposure:.6e} が候補の {expected_pre:.6e}（{offset:+.1f} EV）と合わない")
        if fails:
            results.append(result)
            continue
        params = candidate_params(DT.params_from_capture(meta), curve)
        hdr = DT.read_pfm(pfm_path)
        png = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.int32)
        if png.shape[:2] != hdr.shape[:2]:
            fails.append(f"PNG の大きさが HDR と違う（{png.shape[1]}x{png.shape[0]}、{hdr.shape[1]}x{hdr.shape[0]}）")
            results.append(result)
            continue
        predicted = render(hdr, pre_exposure, params, lut).astype(np.int32)
        worst = np.abs(png - predicted).max(axis=-1)
        within = float((worst <= SCENE_WITHIN_LEVELS).mean())
        result.update({
            "within_levels": SCENE_WITHIN_LEVELS,
            "within_ratio": within,
            "exact_ratio": float((worst == 0).mean()),
            "max_abs_levels": int(worst.max()),
            "mean_signed_levels_rgb": [float(v) for v in (png - predicted).reshape(-1, 3).mean(axis=0)],
        })
        if within < SCENE_WITHIN_RATIO:
            fails.append(f"±{SCENE_WITHIN_LEVELS} 段の中の画素が {within:.4f}（{SCENE_WITHIN_RATIO} 以上が要る）")
        result["pass"] = not fails
        results.append(result)
    return results


def write_table(report, path):
    lines = [
        "| 順位 | 候補 | 曲線 | 露出の補正 [EV] | 距離 D | far_01 | cliff_01 | ground_02 | 白飛び | 黒つぶれ | カメラで選べる | 選べる |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for rank, item in enumerate(report["ranking"], start=1):
        per = item["per_photo"]
        lines.append(
            f"| {rank} | {item['name']} | {CURVES[item['curve']][0]} | {item['offset']:+.1f} | {item['distance']:.3f} "
            f"| {per['far_01']:.3f} | {per['cliff_01']:.3f} | {per['ground_02']:.3f} "
            f"| {100 * item['clipped']:.3f} % | {100 * item['crushed']:.3f} % | {'○' if item['camera'] else '×'} | {'○' if item['eligible'] else '×'} |"
        )
    with open(path, "w", encoding="utf-8") as file:
        file.write("\n".join(lines) + "\n")


def write_sheet(out_dir, photo_id, photo_panel, current_codes, selected_codes, selected_name):
    font = load_font(20)
    label_height = 30
    width, height = PANEL_SIZE
    sheet = Image.new("RGB", (width, (height + label_height) * 3), (24, 24, 24))
    rows = [(f"写真 {photo_id}", photo_panel), ("今の設定 current+0.0EV（オフライン）", Image.fromarray(current_codes)),
            (f"選んだ候補 {selected_name}（オフライン）", Image.fromarray(selected_codes))]
    from PIL import ImageDraw

    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(rows):
        top = index * (height + label_height)
        draw.text((8, top + 4), label, fill=(235, 235, 235), font=font)
        sheet.paste(image.resize(PANEL_SIZE, Image.LANCZOS), (0, top + label_height))
    sheet.save(os.path.join(out_dir, f"look_select_{photo_id}.png"))


def run_look_select(hdr_dir, capture_dir, out_dir, verify):
    """--look-select の本体。終了コードを返す"""
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    failures = []
    annotations = load_regions()
    lut = DT.load_aces_lut()
    views = {}
    for entry in PHOTOS:
        try:
            views[entry["id"]] = load_view(hdr_dir, entry["view"])
        except (OSError, ValueError) as error:
            failures.append(f"{entry['view']} の HDR を読めない: {error}")
    photo_stats = {}
    for photo_id in CLEAR_PHOTOS:
        annotation = annotations["photos"].get(photo_id)
        if annotation is None or photo_id not in views:
            failures.append(f"{photo_id} の注釈か HDR が無い")
            continue
        panel, (bLetterbox, band), _ = fit_photo(Image.open(os.path.join(PHOTO_DIR, photo_id + ".jpg")).convert("RGB"))
        codes = np.asarray(panel)
        valid = np.ones(codes.shape[:2], dtype=bool)
        if bLetterbox:
            valid[:band] = False
            valid[PANEL_SIZE[1] - band:] = False
        hdr, _ = views[photo_id]
        masks = region_masks(annotation, (hdr.shape[1], hdr.shape[0]))
        linear = srgb_to_linear(codes.astype(np.float64))
        photo_stats[photo_id] = {
            "panel": panel,
            "masks": masks,
            "stats": {name: color_stats(linear, photo_mask & valid) for name, (photo_mask, _) in masks.items()},
        }
    if failures:
        for failure in failures:
            print(f"COMPARE_REFERENCE_LOOK_SELECT failure={failure}")
        print(f"COMPARE_REFERENCE_LOOK_SELECT result=fail failures={len(failures)}")
        return 1

    candidates = []
    rendered = {}
    for curve in CURVES:
        for offset in EXPOSURE_OFFSETS:
            name = candidate_name(curve, offset)
            clipped, crushed = [], []
            per_photo, per_region = {}, {}
            for entry in PHOTOS:
                photo_id = entry["id"]
                hdr, meta = views[photo_id]
                params = candidate_params(DT.params_from_capture(meta), curve)
                codes = render(hdr, float(meta["pre_exposure"]) * 2.0 ** offset, params, lut)
                c, k = clip_crush(codes)
                clipped.append(c)
                crushed.append(k)
                if photo_id not in photo_stats:
                    continue
                if (curve, offset) == CURRENT:
                    rendered[("current", photo_id)] = codes
                rendered[(name, photo_id)] = codes
                linear = srgb_to_linear(codes.astype(np.float64))
                errors = {}
                for region, (_, capture_mask) in photo_stats[photo_id]["masks"].items():
                    error = region_error(color_stats(linear, capture_mask), photo_stats[photo_id]["stats"][region])
                    if error is not None:
                        errors[region] = error
                if not errors:
                    failures.append(f"{name} の {photo_id} で比べられる領域が無い")
                    continue
                per_region[photo_id] = errors
                per_photo[photo_id] = float(np.mean([e["sum"] for e in errors.values()]))
            candidates.append({
                "name": name, "curve": curve, "offset": offset,
                "distance": float(np.mean(list(per_photo.values()))) if per_photo else float("inf"),
                "per_photo": per_photo, "per_region": per_region,
                "clipped": float(np.mean(clipped)), "crushed": float(np.mean(crushed)),
            })
            print(f"COMPARE_REFERENCE_LOOK_SELECT candidate={name} distance={candidates[-1]['distance']:.4f} "
                  f"clipped={candidates[-1]['clipped']:.5f} crushed={candidates[-1]['crushed']:.5f}")
    current = next(c for c in candidates if (c["curve"], c["offset"]) == CURRENT)
    for item in candidates:
        item["camera"] = item["curve"] in CAMERA_CURVES
        item["eligible"] = (item["camera"] and item["clipped"] <= current["clipped"] + CLIP_TOLERANCE
                            and item["crushed"] <= current["crushed"] + CLIP_TOLERANCE)
    ranking = sorted(candidates, key=lambda c: c["distance"])
    best = min((c for c in candidates if c["eligible"]), key=lambda c: c["distance"])
    if best["distance"] > current["distance"] * (1.0 - MIN_GAIN):
        selected, reason = current, f"白飛び・黒つぶれの条件を満たす最小の距離 {best['name']}（{best['distance']:.3f}）が今の設定（{current['distance']:.3f}）の {100 * MIN_GAIN:.0f} % 未満の改善"
        if best is current:
            reason = "今の設定が白飛び・黒つぶれの条件を満たす候補の中で距離が最小"
    else:
        selected, reason = best, f"カメラで選べて白飛び・黒つぶれが今より増えない候補の中で距離が最小（今の設定 {current['distance']:.3f}）"
    report = {
        "hdr_dir": hdr_dir,
        "clear_photos": list(CLEAR_PHOTOS),
        "distance_regions": list(DISTANCE_REGIONS),
        "exposure_offsets": list(EXPOSURE_OFFSETS),
        "clip_tolerance": CLIP_TOLERANCE,
        "min_gain": MIN_GAIN,
        "current": current["name"],
        "selected": selected["name"],
        "selected_curve": selected["curve"],
        "selected_offset": selected["offset"],
        "reason": reason,
        "ranking": ranking,
    }
    for photo_id in CLEAR_PHOTOS:
        write_sheet(out_dir, photo_id, photo_stats[photo_id]["panel"], rendered[("current", photo_id)],
                    rendered[(selected["name"], photo_id)], selected["name"])
    print(f"COMPARE_REFERENCE_LOOK_SELECT selected={selected['name']} distance={selected['distance']:.4f} "
          f"current_distance={current['distance']:.4f} rank_of_current={ranking.index(current) + 1}/{len(ranking)} reason={reason}")

    if capture_dir:
        hdr_metas = {entry["view"]: views[entry["id"]][1] for entry in PHOTOS}
        checks = check_capture(capture_dir, hdr_metas, (selected["curve"], selected["offset"]), lut)
        report["capture"] = capture_dir
        report["capture_check"] = checks
        for check in checks:
            print(f"COMPARE_REFERENCE_LOOK_SELECT capture_view={check['view']} pass={int(check['pass'])} "
                  f"within{SCENE_WITHIN_LEVELS}={check.get('within_ratio')} exact={check.get('exact_ratio')} "
                  f"max={check.get('max_abs_levels')} mean_signed={check.get('mean_signed_levels_rgb')}")
            failures.extend(f"{check['view']}: {fail}" for fail in check["failures"])
    elif verify:
        failures.append("--verify には --capture（--display-check=scene -Hdr の撮影）が要る")

    with open(os.path.join(out_dir, "look_select.json"), "w", encoding="utf-8") as file:
        json.dump(report, file, ensure_ascii=False, indent=2)
    write_table(report, os.path.join(out_dir, "look_select.md"))
    if failures:
        for failure in failures:
            print(f"COMPARE_REFERENCE_LOOK_SELECT failure={failure}")
        print(f"COMPARE_REFERENCE_LOOK_SELECT result=fail failures={len(failures)}")
        return 1
    print(f"COMPARE_REFERENCE_LOOK_SELECT result=pass selected={selected['name']} candidates={len(candidates)} out={out_dir}"
          + (" verify=1" if verify else ""))
    return 0
