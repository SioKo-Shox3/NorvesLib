# 荒野のグレーディングを中立にした変更（SCN-WL-GRADE-NEUTRAL）の検査（ReferenceCheck.py --grading から呼ぶ。単独では動かさない）。
#
# 1. 既定の荒野の撮影（--engine）の各視点で、撮影の表示変換の記録が候補 neutral_plain（NeutralLinear・コントラスト 1・
#    彩度 1・色温度 0）と同じ値であることと、HDR に neutral_plain を掛けた画が撮影の PNG と 8 bit で 1 段以内に合うこと
#    （--display の scene の組と同じ基準: 画面全体で SCENE_WITHIN_LEVELS 段以内の画素が SCENE_WITHIN_RATIO 以上）を検査する。
#    既定の撮影の PNG にはブルームと FXAA が乗るので、撮影の記録（hdr.json の post）どおりに PostEffects.py で再現して掛ける。
# 2. 同じ HDR に変更前の表示変換（候補 current）を掛けた画を「前」、neutral_plain の画を「後」として、地面の R/G・白飛びの
#    割合（写真の崖・地面の枠の地形の画素と、画面全体）を記録する。写真の値も並べるが、写真の現像は分からないので合否に使わない。
# 3. --capture に別の撮影先（6 視点を -Hdr で撮ったもの）を渡すと、その各視点も 1. と同じに検査し、写真｜前（HDR に current）｜
#    後（撮影の PNG）の比較画像 grading_sheet.png を書く。
# --verify は --engine の撮影の入力の記録を今のファイルと照らし、古い結果を拒否する。報告は <--engine の先頭>/grading_check.json。
import json
import os

import numpy as np
from PIL import Image

import PostEffects as PE

GRADING_VIEWS = ("ref_cliff_01", "ref_ground_02")
GRADING_CANDIDATE = "neutral_plain"
GRADING_BEFORE = "current"
GRADING_SHEET = "grading_sheet.png"
GRADING_SHEET_PANEL = (640, 360)


def candidate_by_name(rc, name):
    return next(c for c in rc.TONEMAP_CANDIDATES if c[0] == name)


def grading_mismatch(rc, capture_params, candidate):
    """撮影の表示変換の記録と候補の値の食い違い（無ければ空）"""
    expected = rc.tonemap_candidate_params(candidate, capture_params)
    mismatch = []
    for key in ("operator", "grading_mode", "bypass"):
        if capture_params[key] != expected[key]:
            mismatch.append(f"{key}={capture_params[key]}（期待 {expected[key]}）")
    for key in ("contrast", "saturation", "temperature", "brightness"):
        if abs(capture_params[key] - expected[key]) > rc.TONEMAP_PARAM_TOLERANCE:
            mismatch.append(f"{key}={capture_params[key]}（期待 {expected[key]}）")
    if any(abs(v - 1.0) > rc.TONEMAP_PARAM_TOLERANCE for v in capture_params["color_filter"]):
        mismatch.append(f"color_filter={capture_params['color_filter']}")
    if capture_params["tone_map_vignette"][0] > rc.TONEMAP_PARAM_TOLERANCE:
        mismatch.append(f"tone_map_vignette={capture_params['tone_map_vignette']}")
    return mismatch


def photo_regions(rc, view, size):
    """視点に対応する写真の、パネルの大きさの画像・崖と地面の枠・地形の画素。(写真の記録, 画像, 枠, 地形の画素) か None"""
    import CompareReference as CR
    entry = next((p for p in CR.PHOTOS if p["view"] == view), None)
    path = os.path.join(CR.PHOTO_DIR, entry["id"] + ".jpg") if entry else ""
    if not os.path.isfile(path):
        return None
    panel, (letterbox, band), map_y = CR.fit_photo(Image.open(path).convert("RGB"))
    boxes = {name: (box[0], map_y(box[1]), box[2], map_y(box[3])) for name, box in entry.items() if name in CR.REGION_COLORS}
    valid = np.ones((size[1], size[0]), dtype=bool)
    if letterbox:
        valid[:band] = False
        valid[size[1] - band:] = False
    image = np.asarray(panel)
    rows = np.arange(size[1])[:, None]
    terrain = (rows >= CR.find_terrain_top(image)[None, :]) & valid
    return entry, image, boxes, terrain


def full_frame_clip(rc, image):
    """画面全体で、どれかの成分が白飛びの値以上の画素の割合"""
    return float((image >= rc.TONEMAP_CLIP_LEVEL).any(axis=-1).mean())


def check_view(rc, capture_dir, view, lut):
    """1 視点の検査。(結果, 前の画面〔current を HDR に掛けた 8 bit〕, 撮影の PNG)"""
    import CompareReference as CR
    result = {"view": view, "pass": False, "failures": []}
    fails = result["failures"]
    hdr_result = rc.check_hdr_view(capture_dir, view)
    result["hdr_png_match"] = dict(hdr_result.get("png_match", {}), **{"pass": hdr_result["pass"]})
    fails.extend(hdr_result["failures"])
    hdr, meta, hdr_failures = rc.lit_engine_hdr(capture_dir, view)
    fails.extend(hdr_failures)
    png_path = os.path.join(capture_dir, f"{view}.png")
    if hdr is None or not os.path.isfile(png_path):
        if hdr is not None:
            fails.append(f"{view}.png が無い")
        return result, None, None
    try:
        capture_params = rc.DT.params_from_capture(meta)
    except rc.DT.UnsupportedDisplay as error:
        fails.append(str(error))
        return result, None, None
    after = candidate_by_name(rc, GRADING_CANDIDATE)
    before = candidate_by_name(rc, GRADING_BEFORE)
    mismatch = grading_mismatch(rc, capture_params, after)
    result["capture_display"] = {k: capture_params[k] for k in ("operator", "grading_mode", "contrast", "contrast_pivot",
                                                                "saturation", "temperature", "vignette")}
    if mismatch:
        fails.append(f"撮影の表示変換が候補 {GRADING_CANDIDATE} と違う（{', '.join(mismatch)}）")
    pre_exposure = float(meta["pre_exposure"])
    png = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.int32)
    if png.shape[:2] != hdr.shape[:2]:
        fails.append(f"PNG の大きさ {png.shape[1]}x{png.shape[0]} が HDR {hdr.shape[1]}x{hdr.shape[0]} と違う")
        return result, None, None

    # 1. HDR に neutral_plain を掛けた画（撮影の記録どおりにブルームと FXAA も再現する）と撮影の PNG（画面全体）
    height, width = hdr.shape[:2]
    hdr32 = hdr.astype(np.float32)
    try:
        scene = PE.scene_with_bloom(hdr32, meta)
    except rc.DT.UnsupportedDisplay as error:
        fails.append(str(error))
        return result, None, None
    result["post"] = {key: bool((meta.get("post") or {}).get(key)) for key in ("bloom", "fxaa")}
    predicted = PE.display_with_post(scene, meta, rc.tonemap_candidate_params(after, capture_params), lut)
    difference = png - predicted.astype(np.int32)
    worst = np.abs(difference).max(axis=-1)
    within = float((worst <= rc.SCENE_WITHIN_LEVELS).mean())
    result.update({
        "size": f"{width}x{height}", "pre_exposure": pre_exposure,
        "within_levels": rc.SCENE_WITHIN_LEVELS, "within_ratio": within, "exact_ratio": float((worst == 0).mean()),
        "max_abs_levels": int(worst.max()),
        "mean_signed_levels_rgb": [float(v) for v in difference.reshape(-1, 3).mean(axis=0)],
    })
    if within < rc.SCENE_WITHIN_RATIO:
        fails.append(f"±{rc.SCENE_WITHIN_LEVELS} 段に入る画素が {within:.5f}（下限 {rc.SCENE_WITHIN_RATIO}）")

    # 2. 前後（同じ HDR に current と neutral_plain）の地面の R/G と白飛びの割合
    before_full = PE.display_with_post(scene, meta, rc.tonemap_candidate_params(before, capture_params), lut)
    result["clip_full"] = {"before": full_frame_clip(rc, before_full), "after": full_frame_clip(rc, png.astype(np.uint8))}
    size = CR.PANEL_SIZE
    photo = photo_regions(rc, view, size)
    if photo is None:
        fails.append(f"{view}: 対応する写真が無い")
        return result, before_full, png.astype(np.uint8)
    photo_entry, photo_image, boxes, photo_terrain = photo
    small = rc.tonemap_downsample(hdr32, size)
    vignette = rc.DT.vignette_factor(size[1], size[0], *capture_params["vignette"])
    renders = {name: rc.tonemap_render(small, pre_exposure, rc.tonemap_candidate_params(candidate_by_name(rc, name), capture_params),
                                       vignette, lut) for name in (GRADING_BEFORE, GRADING_CANDIDATE)}
    # 地形の画素は変更前の絵の輪郭から 1 度だけ決め、前後で同じ画素を比べる（--tonemap と同じ）
    rows = np.arange(size[1])[:, None]
    terrain = rows >= CR.find_terrain_top(renders[GRADING_BEFORE])[None, :]
    masks = rc.tonemap_masks(terrain, boxes)
    measures = {name: rc.tonemap_measure(image, masks) for name, image in renders.items()}
    measures["photo"] = rc.tonemap_measure(photo_image, rc.tonemap_masks(photo_terrain, boxes))
    result["photo_id"] = photo_entry["id"]
    result["measures"] = measures
    for key in ("before", "after"):
        m = measures[GRADING_BEFORE if key == "before" else GRADING_CANDIDATE]
        if m.get("ground_r_over_g") is None or m.get("clip") is None:
            fails.append(f"{view}: {key} の地面の R/G か白飛びが測れない（枠の中に地形の画素が無い）")
    return result, before_full, png.astype(np.uint8)


def run(rc, engine_dirs, sheet_dir, views, verify):
    import CompareReference as CR
    from CompareLight import load_font
    failures = []
    views = views or list(GRADING_VIEWS)
    if not engine_dirs:
        print("REFERENCE_CHECK result=fail reason=engine_missing（--engine で撮影先を指定する）")
        return 1
    capture_dir = engine_dirs[0]
    report = {"mode": "grading", "verify": bool(verify), "candidate": GRADING_CANDIDATE, "before": GRADING_BEFORE,
              "criteria": {"within_levels": rc.SCENE_WITHIN_LEVELS, "within_ratio": rc.SCENE_WITHIN_RATIO},
              "views_requested": views, "views": []}
    if not os.path.isdir(capture_dir):
        print(f"REFERENCE_CHECK result=fail reason=engine_dir_missing path={capture_dir}")
        return 1
    lut = rc.DT.load_aces_lut()

    inputs_ok, input_failures, record = rc.check_inputs(capture_dir)
    report["inputs_current"] = inputs_ok
    report["inputs_record"] = record
    if verify and not inputs_ok:
        failures.extend(input_failures)
    game_args = rc.plate_capture_args(capture_dir)
    report["game_args"] = game_args
    if game_args is None:
        failures.append("metrics.json が無い（CaptureScene.ps1 -Hdr の出力先でない）")
    else:
        forbidden = [a for a in game_args if a.startswith(rc.TONEMAP_FORBIDDEN_ARG_PREFIXES)]
        if forbidden:
            failures.append(f"撮影が既定の荒野の表示でない（起動引数 {', '.join(forbidden)}）")

    def measure_line(view, result):
        m = result.get("measures")
        if not m:
            return
        b, a, p = m[GRADING_BEFORE], m[GRADING_CANDIDATE], m["photo"]
        print(f"REFERENCE_CHECK grading view={view} ground_rg before={b['ground_r_over_g']:.3f} after={a['ground_r_over_g']:.3f} "
              f"photo={p['ground_r_over_g']:.3f} clip_box before={b['clip']:.4f} after={a['clip']:.4f} photo={p['clip']:.4f} "
              f"clip_full before={result['clip_full']['before']:.4f} after={result['clip_full']['after']:.4f} "
              f"saturation before={b['saturation']:.3f} after={a['saturation']:.3f} photo={p['saturation']:.3f}")

    for view in views:
        result, _, _ = check_view(rc, capture_dir, view, lut)
        report["views"].append(result)
        print(f"REFERENCE_CHECK grading view={view} pass={int(not result['failures'])} within{rc.SCENE_WITHIN_LEVELS}="
              f"{result.get('within_ratio')} exact={result.get('exact_ratio')} max_abs={result.get('max_abs_levels')} "
              f"bias_rgb={result.get('mean_signed_levels_rgb')}")
        measure_line(view, result)
        failures.extend(f"{view}: {f}" for f in result["failures"])

    # 6 視点の比較画像
    if sheet_dir:
        report["sheet_capture"] = sheet_dir
        sheet_inputs_ok, sheet_input_failures, _ = rc.check_inputs(sheet_dir)
        report["sheet_inputs_current"] = sheet_inputs_ok
        if verify and not sheet_inputs_ok:
            failures.extend(f"6 視点の撮影: {f}" for f in sheet_input_failures)
        size = GRADING_SHEET_PANEL
        sheet_rows = []
        report["sheet_views"] = []
        for view in rc.LIT_VIEWS:
            result, before_image, after_image = check_view(rc, sheet_dir, view, lut)
            report["sheet_views"].append(result)
            print(f"REFERENCE_CHECK grading_sheet view={view} pass={int(not result['failures'])} "
                  f"within{rc.SCENE_WITHIN_LEVELS}={result.get('within_ratio')} max_abs={result.get('max_abs_levels')}")
            measure_line(view, result)
            failures.extend(f"6 視点 {view}: {f}" for f in result["failures"])
            photo = photo_regions(rc, view, CR.PANEL_SIZE)
            if before_image is None or photo is None:
                continue
            m = result.get("measures", {})
            def text(name, label):
                v = m.get(name)
                return f"{label} 地面R/G {v['ground_r_over_g']:.2f} 白飛び {v['clip']:.3f}" if v else label
            sheet_rows.append((view, [(text("photo", f"写真 {photo[0]['id']}"), photo[1]),
                                      (text(GRADING_BEFORE, "前（current）"), before_image),
                                      (text(GRADING_CANDIDATE, "後（中立）"), after_image)]))
        if sheet_rows:
            rc.write_tonemap_sheet(os.path.join(sheet_dir, GRADING_SHEET), sheet_rows, None, load_font(15), size,
                                   note="前 = 同じ HDR に変更前の表示変換（ブルーム・FXAA も再現）、後 = 撮影の PNG。地面R/G・白飛びは写真の崖・地面の枠の地形の画素")
            report["sheet"] = GRADING_SHEET
        else:
            failures.append("6 視点の比較画像を作れる視点が無い")

    report["failures"] = failures
    report_path = os.path.join(capture_dir, "grading_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=grading views={len(views)} inputs_current={int(inputs_ok)} report={report_path}")
    return 0 if not failures else 1
