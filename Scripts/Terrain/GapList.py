# 荒野の正解の絵の比較（SCN-WLR-*）の節目: 比較の結果を集めて、差を「描画の欠陥」「中身の欠陥」「条件不一致・未判定」に分けた
# 一覧を作る（ReferenceCheck.py --gap-list から呼ぶ。単独では動かさない）。
#
# 1. 揃いと鮮度: 比較ごと（GAP_COMPONENTS）に、保存した報告（<出力先>/<mode>_check.json）があり、--verify で合格した記録
#    （verify が真・failures が空）で、報告の後に撮影・描画の入力の記録（inputs.json）が書き換わっていないこと、撮影・Cycles・
#    Blender の入力の記録が今のファイルと一致すること（ReferenceCheck.py の各モードと同じ照らし方）を検査する。
#    大気の正解（SCN-WLR-ATMOS-REF）だけは、入力が今と違う（古い）ことを未判定の印として許す（--require-atmos では許さない）。
# 2. 一覧: 差の項目（GAP_ITEMS）ごとに、報告の値から差の大きさを求め、分類ごとに大きい順に並べた表を、
#    Docs/RenderingValidation/WastelandGapList.md の印（GAP_TABLE_BEGIN・GAP_TABLE_END）の間に書く。--verify では書かずに、
#    文書の表が今の報告から作った表と同じことを検査する。差の大きさは、比 r なら max(r, 1/r) − 1、遠景のぼけは正解の細部の量に
#    対する比、色度は相対の差。
# 3. 一覧の画像: 手本の 6 視点の 写真｜エンジン（既定の荒野）｜エンジン（地形だけ・共通の照明）｜Cycles（同じ条件）を 1 枚に
#    並べて <出力先>/gap_overview.png に書く。
# 4. 大気の追補（--atmos-addendum）: SCN-WLR-GAP-LIST の完了の時点の表（GAP_FROZEN）への大気の行の書き足しとして検査する。
#    大気以外の行は凍結した値のまま使い（後の描画の修正で大気以外の比較が古くなっても落ちない）、比較の揃いと鮮度は大気の正解
#    だけを照らす（古いことを許さない）。大気の行は今の報告から求め、未判定の印が表にも文書にも残らないことを検査する。
#    一覧の画像は凍結した時点のものを残し、書き直さない。
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw

GAP_DOC = os.path.join("Docs", "RenderingValidation", "WastelandGapList.md")
GAP_TABLE_BEGIN = "<!-- gap-table:begin（ReferenceCheck.py --gap-list が書く。手で直さない） -->"
GAP_TABLE_END = "<!-- gap-table:end -->"
GAP_OVERVIEW = "gap_overview.png"
GAP_OVERVIEW_PANEL = (480, 270)
GAP_OVERVIEW_LABEL_HEIGHT = 30

# 比較ごとの出力先（.harness/runs/scene-capture/ の下）と報告
GAP_DIRS = {
    "gray": "SCN-WLR-CYCLES-GRAY",
    "albedo": "SCN-WLR-MATERIAL-ALBEDO",
    "normal": "SCN-WLR-MATERIAL-NORMAL",
    "lit": "SCN-WLR-CYCLES-LIT",
    "atmos_ref": "SCN-WLR-ATMOS-DUMP",
    "blur_static": "SCN-WLR-FAR-BLUR-STATIC",
    "blur_motion": "SCN-WLR-FAR-BLUR-MOTION",
    "tonemap": "SCN-WLR-TONEMAP-EVAL",
}
GAP_TASKS = {
    "gray": "SCN-WLR-CYCLES-GRAY", "albedo": "SCN-WLR-MATERIAL-ALBEDO", "normal": "SCN-WLR-MATERIAL-NORMAL",
    "lit": "SCN-WLR-CYCLES-LIT", "atmos_ref": "SCN-WLR-ATMOS-REF", "blur_static": "SCN-WLR-FAR-BLUR-STATIC",
    "blur_motion": "SCN-WLR-FAR-BLUR-MOTION", "tonemap": "SCN-WLR-TONEMAP-EVAL",
}

GAP_CATEGORIES = (
    ("render", "描画の欠陥（入力が一致して、エンジン ≠ Cycles・数値の正解）"),
    ("content", "中身の欠陥（描画は入力どおりで、入力の値・設定が実際と食い違う）"),
    ("open", "条件不一致・未判定"),
)
GAP_POINTS = {
    "far_blur": "遠景のぼけ", "fog": "フォグ", "sky": "空", "clouds": "雲", "buildings": "建物の浮き", "plants": "植物",
    "other": "その他",
}
# B（大気の高次の散乱）に依る判断で、大気の正解の比較が古いときの印。前の印（B の直し待ち）も残っていないことを検査する
GAP_PENDING_ATMOS = "未判定（大気の正解の比較が古い。撮り直して --atmos-ref --verify を回す）"
GAP_PENDING_MARKERS = ("未判定（B の直し待ち", "未判定（大気の正解の比較が古い")
# SCN-WLR-GAP-LIST の完了の時点の表の行（--atmos-addendum が大気以外の行に使う）
GAP_FROZEN = os.path.join("Scripts", "Terrain", "GapListFrozen.json")


def _scene_dir(rc, key):
    return os.path.join(rc.REPO_ROOT, ".harness", "runs", "scene-capture", GAP_DIRS[key])


def _mtime(path):
    return os.path.getmtime(path) if os.path.isfile(path) else None


# ---- 比較ごとの今の入力の照らし方（ReferenceCheck.py の各モードと同じ関数を使う）。(理由の一覧, 入力の記録のファイル, 報告) ----

def _inputs_gray(rc, root):
    reasons, records = [], []
    main_ok, main_reasons, main_record = rc.check_inputs(root)
    reasons += main_reasons
    records.append(os.path.join(root, "inputs.json"))
    for case in rc.GRAY_ENGINE_CASES:
        case_dir = os.path.join(root, f"engine-{case}")
        _, case_reasons, _ = rc.check_inputs(case_dir)
        reasons += [f"engine-{case}: {r}" for r in case_reasons]
        records.append(os.path.join(case_dir, "inputs.json"))
    cycles_dir = os.path.join(root, rc.GRAY_CYCLES_DIR)
    _, cycles_reasons, _ = rc.check_cycles_gray_inputs(cycles_dir, (main_record or {}).get("files_sha256"))
    reasons += [f"Cycles: {r}" for r in cycles_reasons]
    records.append(os.path.join(cycles_dir, "inputs.json"))
    return reasons, records, "gray_check.json"


def _inputs_material(rc, root, mode, views, sub_dir):
    reasons, records = [], []
    _, main_reasons, main_record = rc.check_inputs(root)
    reasons += main_reasons
    records.append(os.path.join(root, "inputs.json"))
    blender_dir = os.path.join(root, sub_dir)
    _, blender_reasons, _ = rc.check_albedo_inputs(root, blender_dir, list(views),
                                                   (main_record or {}).get("files_sha256"), mode=mode)
    reasons += [f"Blender: {r}" for r in blender_reasons]
    records.append(os.path.join(blender_dir, "inputs.json"))
    return reasons, records, main_record


def _inputs_albedo(rc, root):
    reasons, records, _ = _inputs_material(rc, root, "albedo", rc.ALBEDO_VIEWS, rc.ALBEDO_RESULT_DIR)
    return reasons, records, "albedo_check.json"


def _inputs_normal(rc, root):
    reasons, records, _ = _inputs_material(rc, root, "normal", rc.ALBEDO_VIEWS, rc.NORMAL_RESULT_DIR)
    return reasons, records, "normal_check.json"


def _inputs_lit_dir(rc, root):
    reasons, records, main_record = _inputs_material(rc, root, "lit", rc.LIT_VIEWS, rc.LIT_MATERIAL_DIR)
    cycles_dir = os.path.join(root, rc.GRAY_CYCLES_DIR)
    _, cycles_reasons, _ = rc.check_cycles_lit_inputs(root, cycles_dir, (main_record or {}).get("files_sha256"),
                                                      list(rc.LIT_VIEWS))
    reasons += [f"Cycles: {r}" for r in cycles_reasons]
    records.append(os.path.join(cycles_dir, "inputs.json"))
    return reasons, records


def _inputs_lit(rc, root):
    reasons, records = _inputs_lit_dir(rc, root)
    return reasons, records, "lit_check.json"


def _inputs_atmos_ref(rc, root):
    reasons = []
    _, main_reasons, _ = rc.check_inputs(root)
    reasons += main_reasons
    convergence_path = os.path.join(rc.AR.DEFAULT_OUT, "atmosphere_reference_verify.json")
    records = [os.path.join(root, "inputs.json"), convergence_path]
    if not os.path.isfile(convergence_path):
        reasons.append(f"正解の収束の記録が無い（{convergence_path}）")
    else:
        with open(convergence_path, encoding="utf-8") as f:
            if json.load(f).get("failures"):
                reasons.append("正解の収束の確かめ（AtmosphereReference.py --verify）が合格していない")
    report = os.path.join(root, "atmos_ref_check.json")
    script = os.path.join(rc.REPO_ROOT, "Scripts", "Terrain", "AtmosphereReference.py")
    if _mtime(report) is not None and _mtime(script) > _mtime(report):
        reasons.append("AtmosphereReference.py が比較の後に変わった（古い結果）")
    return reasons, records, "atmos_ref_check.json"


def _inputs_blur_static(rc, root):
    reasons, records = [], []
    for name in [rc.BLUR_REFERENCE] + [case["name"] for case in rc.BLUR_CASES]:
        group_dir = os.path.join(root, name)
        _, group_reasons, _ = rc.check_inputs(group_dir)
        reasons += [f"{name}: {r}" for r in group_reasons]
        records.append(os.path.join(group_dir, "inputs.json"))
    return reasons, records, "blur_static_check.json"


def _inputs_blur_motion(rc, root):
    reasons, records = [], []
    for group in rc.MOTION_GROUPS:
        group_dir = os.path.normpath(os.path.join(root, group["dir"]))
        _, group_reasons, _ = rc.check_inputs(group_dir)
        reasons += [f"{group['name']}: {r}" for r in group_reasons]
        records.append(os.path.join(group_dir, "inputs.json"))
    return reasons, records, "blur_motion_check.json"


def _inputs_tonemap(rc, root):
    reasons = []
    _, main_reasons, _ = rc.check_inputs(root)
    reasons += main_reasons
    lit_reasons, lit_records = _inputs_lit_dir(rc, _scene_dir(rc, "lit"))
    reasons += [f"Cycles の撮影: {r}" for r in lit_reasons]
    return reasons, [os.path.join(root, "inputs.json")] + lit_records, "tonemap_check.json"


GAP_COMPONENTS = (
    ("gray", "gray", _inputs_gray),
    ("albedo", "albedo", _inputs_albedo),
    ("normal", "normal", _inputs_normal),
    ("lit", "lit", _inputs_lit),
    ("atmos_ref", "atmos_ref", _inputs_atmos_ref),
    ("blur_static", "blur_static", _inputs_blur_static),
    ("blur_motion", "blur_motion", _inputs_blur_motion),
    ("tonemap", "tonemap", _inputs_tonemap),
)


def check_component(rc, key, mode, inputs_fn):
    """比較 1 つの揃いと鮮度。(記録, 報告の中身)"""
    root = _scene_dir(rc, key)
    entry = {"task": GAP_TASKS[key], "dir": os.path.relpath(root, rc.REPO_ROOT).replace("\\", "/")}
    if not os.path.isdir(root):
        entry.update(state="missing", reasons=[f"出力先が無い（{root}）"])
        return entry, None
    stale, records, report_name = inputs_fn(rc, root)
    report_path = os.path.join(root, report_name)
    entry["report"] = report_name
    problems = []
    report = None
    if not os.path.isfile(report_path):
        problems.append(f"報告 {report_name} が無い")
    else:
        with open(report_path, encoding="utf-8") as f:
            report = json.load(f)
        if report.get("mode") != mode:
            problems.append(f"報告の mode が {mode} でない（{report.get('mode')}）")
        if not report.get("verify"):
            problems.append("報告が --verify で作られていない")
        newer = [os.path.relpath(p, root).replace("\\", "/") for p in records
                 if _mtime(p) is not None and _mtime(p) > _mtime(report_path)]
        if newer:
            stale.append(f"報告の後に入力の記録が書き換わった: {', '.join(newer)}")
        report_failures = report.get("failures") or []
        if report_failures and not stale:
            problems.append(f"報告に不合格が {len(report_failures)} 件ある（先頭: {report_failures[0]}）")
    entry["stale_reasons"] = stale
    entry["problems"] = problems
    entry["state"] = "fail" if problems else ("stale" if stale else "current")
    return entry, report


# ---- 差の大きさの求め方 ----

def _luma(rgb):
    return float(np.dot(np.asarray(rgb, dtype=np.float64), [0.2126, 0.7152, 0.0722]))


def _rel(ratio):
    """比の差の大きさ max(r, 1/r) − 1"""
    if ratio is None or not ratio > 0.0 or not math.isfinite(ratio):
        return None
    return max(ratio, 1.0 / ratio) - 1.0


def _region(reports, key, view, name):
    for result in (reports.get(key) or {}).get("view_results") or []:
        if result.get("view") == view:
            return (result.get("regions") or {}).get(name)
    return None


def _decided(entry):
    return entry is not None and str(entry.get("status", "")).startswith("判定")


def _decided_regions(reports, key, name):
    out = []
    for result in (reports.get(key) or {}).get("view_results") or []:
        entry = (result.get("regions") or {}).get(name)
        if _decided(entry):
            out.append((result["view"], entry))
    return out


def m_shadow_missing(reports):
    lit = _region(reports, "lit", "ref_cliff_02", "shadow_ground")
    gray = _region(reports, "gray", "ref_cliff_01", "shadow_ground")
    if not _decided(lit) or not _decided(gray):
        return None, "影の中の地面の領域が判定されていない"
    return _rel(lit["ratio"]), (f"実材質 ref_cliff_02 の影の中の地面 エンジン ÷ Cycles {lit['ratio']:.2f}、"
                                f"灰色 ref_cliff_01 の影の中の地面の太陽の分 {gray['sun_ratio']:.2f} 倍")


def m_sky_occlusion(reports):
    cliff = _region(reports, "gray", "ref_cliff_01", "shadow_cliff")
    far = _region(reports, "gray", "ref_far_01", "far")
    if not _decided(cliff) or not _decided(far):
        return None, "灰色の影の崖・遠景が判定されていない"
    return _rel(cliff["ratio"]), (f"灰色 ref_cliff_01 の影の崖 {cliff['ratio']:.2f}（空の分 {cliff['sky_ratio']:.2f}）、"
                                  f"ref_far_01 の遠景の空の分 {far['sky_ratio']:.2f}")


def m_material_ao(reports):
    rows = _decided_regions(reports, "lit", "sunlit_ground")
    if not rows:
        return None, "日なたの地面が判定されていない"
    ratios = [e["ratio"] for _, e in rows]
    corrected = [e["ratio_to_cycles_with_engine_ao"] for _, e in rows]
    return _rel(min(ratios)), (f"実材質の日なたの地面 {min(ratios):.2f}〜{max(ratios):.2f}（{len(rows)} 視点）、"
                               f"材質の AO を Cycles に掛けた見込みとの比 {min(corrected):.2f}〜{max(corrected):.2f}")


def _blur_cases(reports, case, region):
    out = []
    for view in (reports.get("blur_static") or {}).get("views") or []:
        entry = ((view.get("cases") or {}).get(case) or {}).get(region)
        if entry:
            out.append((view["view"], entry))
    return out


def m_far_aliasing(reports):
    # 荒野の既定の AA は TAA なので、常駐しきった TAA の組（aa-taa）で数える（resident は FXAA の対照）
    rows = _blur_cases(reports, "aa-taa", "far") + _blur_cases(reports, "aa-taa", "cliff")
    if not rows:
        return None, "遠景のぼけの aa-taa の組が無い"
    alias = [e["fine_alias"] for _, e in rows]
    loss = [e["fine_detail_loss"] for _, e in rows]
    return max(alias), (f"常駐しきった既定の TAA（aa-taa）の遠景・崖のエイリアス {min(alias):.2f}〜{max(alias):.2f}、"
                        f"細部の欠け {min(loss):.2f}〜{max(loss):.2f}（2 倍の解像度の正解の量に対する比）")


def _tonemap_metric(reports, source, exposure, candidate, metric):
    values = []
    for view in (reports.get("tonemap") or {}).get("views") or []:
        value = (((view.get(source) or {}).get(exposure) or {}).get(candidate) or {}).get("metrics", {}).get(metric)
        if value is not None:
            values.append(float(value))
    return values


def m_grading(reports):
    current = _tonemap_metric(reports, "engine", "as_is", "current", "ground_r_over_g")
    plain = _tonemap_metric(reports, "engine", "as_is", "neutral_plain", "ground_r_over_g")
    clip = _tonemap_metric(reports, "cycles", "as_is", "current", "clip")
    if not current or len(current) != len(plain):
        return None, "表示変換の候補の値が無い"
    ratio = float(np.mean(current)) / float(np.mean(plain))
    return ratio - 1.0, (f"同じ HDR で、今のグレーディングの地面の R/G {np.mean(current):.2f}（中立 {np.mean(plain):.2f}）、"
                         f"Cycles の HDR に掛けた白飛び {100.0 * np.mean(clip):.1f} %")


def m_grazing_sun(reports):
    far = _region(reports, "gray", "ref_far_01", "far")
    if not _decided(far):
        return None, "灰色の ref_far_01 の遠景が判定されていない"
    return _rel(far["sun_ratio"]), f"灰色 ref_far_01 の遠景の太陽の分 エンジン ÷ Cycles {far['sun_ratio']:.2f}"


def m_bounce(reports):
    cliff = _region(reports, "gray", "ref_cliff_01", "sunlit_cliff")
    if not _decided(cliff):
        return None, "灰色の日なたの崖が判定されていない"
    indirect = cliff["cycles_share"]["indirect"]
    return _rel(cliff["sun_ratio"]), (f"灰色 ref_cliff_01 の日なたの崖の太陽の分 {cliff['sun_ratio']:.2f}"
                                      f"（Cycles の間接光 {100.0 * indirect:.0f} %）")


def m_psi_grid(reports):
    psi = ((reports.get("atmos_ref") or {}).get("views") or [{}])[0].get("psi") or []
    ratios = [p["engine_over_A"] for p in psi if p.get("engine_over_A")]
    if not ratios:
        return None, "Ψ の表の比較が無い"
    return max(_rel(r) for r in ratios), f"多重散乱の表 Ψ エンジン ÷ 正解 A {min(ratios):.3f}〜{max(ratios):.3f}"


def m_taa_motion(reports):
    for row in (reports.get("blur_motion") or {}).get("history_effect") or []:
        if row.get("region") == "cliff" and row.get("offset") == max(r["offset"] for r in
                                                                       reports["blur_motion"]["history_effect"]):
            return row["detail_loss"], (f"TAA で振ったときの崖の細部の欠けの増え +{row['detail_loss']:.3f}"
                                        f"（{row['offset']} フレーム後。履歴の無い FXAA では 0.002 以内）")
    return None, "動きの切り分けが無い"


def m_lod(reports):
    resident = dict(_blur_cases(reports, "resident", "cliff"))
    finest = dict(_blur_cases(reports, "finest-lod", "cliff"))
    views = sorted(set(resident) & set(finest))
    if not views:
        return None, "最細の LOD の組が無い"
    loss = float(np.mean([resident[v]["fine_detail_loss"] - finest[v]["fine_detail_loss"] for v in views]))
    alias = float(np.mean([finest[v]["fine_alias"] - resident[v]["fine_alias"] for v in views]))
    return loss, f"最細の段に固定すると崖の細部の欠け −{loss:.3f}、エイリアス +{alias:.3f}"


def m_residency(reports):
    # 既定の取り分の base（TAA）は、取り分だけを広げた同じ TAA の aa-taa と比べる（AA の差を混ぜない）
    base = dict(_blur_cases(reports, "base", "far")).get("ref_cliff_02")
    resident = dict(_blur_cases(reports, "aa-taa", "far")).get("ref_cliff_02")
    if not base or not resident:
        return None, "既定の取り分の組が無い"
    loss = base["fine_detail_loss"] - resident["fine_detail_loss"]
    alias = base["fine_alias"] - resident["fine_alias"]
    return max(loss, alias), f"ref_cliff_02 の遠景で既定の取り分 1 の欠け +{loss:.3f}・エイリアス +{alias:.3f}"


def _atmos_view(reports):
    return ((reports.get("atmos_ref") or {}).get("views") or [{}])[0]


def m_sky_illuminance_a(reports):
    illum = _atmos_view(reports).get("illuminance") or {}
    if not illum.get("reference_sky_horizontal_A"):
        return None, "地表の照度の比較が無い"
    ratio = _luma(illum["engine_sky_horizontal"]) / _luma(illum["reference_sky_horizontal_A"])
    return _rel(ratio), f"水平面の空の照度 エンジン ÷ 正解 A {ratio:.3f}"


def m_sky_model_a(reports):
    sky = _atmos_view(reports).get("sky") or []
    ratios = [_luma(s["engine"]) / _luma(s["reference_A"]) for s in sky if s.get("reference_A")]
    if not ratios:
        return None, "空の輝度の比較が無い"
    return max(_rel(r) for r in ratios), f"空の方向別の輝度 エンジン ÷ 正解 A {min(ratios):.3f}〜{max(ratios):.3f}（{len(ratios)} 方向）"


def m_altitude(reports):
    illum = _atmos_view(reports).get("illuminance") or {}
    if not illum.get("reference_sun_transmittance_at_haze_origin"):
        return None, "太陽の透過率の比較が無い"
    ratio = _luma(illum["reference_sun_transmittance_at_haze_origin"]) / _luma(illum["engine_sun_ground_transmittance"])
    fog_sun = [_luma(r["engine"]["fog_sun_single"]) / _luma(r["same_media"]["fog_sun_fog_t_only"])
               for az in (0, 90, 180) for r in _atmos_distances(reports, "direction", az, 0)
               if r["same_media"].get("fog_sun_fog_t_only")]
    fog_text = (f"、霧の太陽の単一散乱 ÷ 霧の透過率だけで減らした正解 {min(fog_sun):.3f}〜{max(fog_sun):.3f}"
                f"（向き・距離に依らない）") if fog_sun else ""
    return ratio - 1.0, (f"谷床の高度（約 1474 m）の太陽の透過率 ÷ エンジンの地表（0 m）の透過率 {ratio:.3f}{fog_text}")


def _atmos_distances(reports, kind, azimuth, elevation=None, minimum_m=1000.0):
    rows = []
    for row in _atmos_view(reports).get("distances") or []:
        if row.get("kind") != kind or row.get("azimuth_offset_deg") != azimuth or row["distance_m"] < minimum_m:
            continue
        if elevation is not None and row.get("elevation_deg") != elevation:
            continue
        rows.append(row)
    return rows


def m_haze_mie(reports):
    rows = [r for az in (0, 90, 180) for r in _atmos_distances(reports, "ground", az)]
    ratios = [r["fog_plus_aerial_mie_over_physical_mie"] for r in rows if r.get("fog_plus_aerial_mie_over_physical_mie")]
    if not ratios:
        return None, "霞の光学的深さの内訳が無い"
    return _rel(min(ratios)), (f"地表への視線の (霧 ＋ 空気遠近の Mie) ÷ 空の大気の Mie の光学的深さ "
                               f"{min(ratios):.2f}〜{max(ratios):.2f}")


def m_sky_ms_b(reports):
    view = _atmos_view(reports)
    sky = view.get("sky") or []
    ratios = [_luma(s["engine"]) / _luma(s["reference_B"]) for s in sky if s.get("reference_B")]
    illum = view.get("illuminance") or {}
    if not ratios or not illum.get("reference_sky_horizontal_B"):
        return None, "空の輝度の比較が無い"
    a_over_b = _luma(illum["reference_sky_horizontal_A"]) / _luma(illum["reference_sky_horizontal_B"])
    return max(_rel(r) for r in ratios), (f"空の輝度 エンジン ÷ 正解 B {min(ratios):.2f}〜{max(ratios):.2f}（{len(ratios)} 方向。"
                                          f"太陽の側の地平線で暗く、太陽から離れた高い空で明るい）、"
                                          f"水平面の空の照度 正解 A ÷ B {a_over_b:.3f}（量でなく向きの配り方の差）")


def _atmos_far_rows(reports, distance_m):
    return [(az, r) for az in (0, 90, 180) for r in _atmos_distances(reports, "direction", az, 0)
            if r["distance_m"] == distance_m]


def m_fog_double(reports):
    rows = _atmos_far_rows(reports, 10000.0)
    if len(rows) != 3:
        return None, "霧の散乱光の内訳が無い"
    ratios = {az: _luma(r["engine"]["fog_sky_color_times_opacity"]) / _luma(r["same_media"]["fog_multiple_fog_t_only"])
              for az, r in rows}
    sun_a = _luma(rows[0][1]["engine"]["fog_sky_color_times_opacity"]) / _luma(rows[0][1]["same_media"]["fog_multiple_A"])
    return _rel(ratios[0]), (f"10 km の霧の空の色 × 不透明度 ÷ 霧の透過率だけで減らした正解 B 太陽の側 {ratios[0]:.3f}・"
                             f"横 {ratios[90]:.3f}・反対 {ratios[180]:.3f}。正解 A とは太陽の側 {sun_a:.2f} 倍（A の等方の仮定による）")


def m_fog_order(reports):
    rows = _atmos_far_rows(reports, 30000.0)
    if len(rows) != 3:
        return None, "霧の散乱光の内訳が無い"
    order = [_luma(r["same_media"]["fog_multiple_fog_t_only"]) / _luma(r["same_media"]["fog_multiple"]) for _, r in rows]
    term = [_luma(r["engine"]["fog_sky_color_times_opacity"]) / _luma(r["same_media"]["fog_multiple"]) for _, r in rows]
    fog_sum = [(_luma(r["engine"]["fog_sky_color_times_opacity"]) + _luma(r["engine"]["fog_sun_single"])) /
               (_luma(r["same_media"]["fog_sun"]) + _luma(r["same_media"]["fog_multiple"])) for _, r in rows]
    return max(_rel(o) for o in order), (f"30 km の霧の空の色の項 ÷ 正解 B {min(term):.2f}〜{max(term):.2f}（霧の透過率だけで減らした"
                                         f"正解 B とは距離で変わらない）、空気遠近の透過率で減らす分 {min(order):.2f}〜{max(order):.2f} 倍、"
                                         f"霧の散乱光の和 ÷ 正解 B {min(fog_sum):.2f}〜{max(fog_sum):.2f}")


def m_aerial_direction(reports):
    rows = [(az, r) for az in (0, 90, 180) for r in _atmos_distances(reports, "direction", az, 0)]
    if not rows:
        return None, "空気遠近の散乱光の内訳が無い"
    haze = {az: [r["engine_over_same_media_scattering"] for a, r in rows if a == az] for az in (0, 90, 180)}
    aerial = [_luma(r["engine"]["aerial_scattering_times_fog_t"]) /
              (_luma(r["same_media"]["air_sun"]) + _luma(r["same_media"]["air_multiple"])) for _, r in rows]
    return _rel(min(min(v) for v in haze.values())), (
        f"1 km より先の霞の散乱光の和 エンジン ÷ 正解 B 太陽の側 {min(haze[0]):.2f}〜{max(haze[0]):.2f}・"
        f"横 {min(haze[90]):.2f}〜{max(haze[90]):.2f}・反対 {min(haze[180]):.2f}〜{max(haze[180]):.2f}、"
        f"うち空気遠近 ÷ 正解 B {min(aerial):.2f}〜{max(aerial):.2f}")


def m_sky_blue_photo(reports):
    bands = (_atmos_view(reports).get("photo") or {}).get("bands") or []
    if not bands:
        return None, "写真の空の比較が無い"
    top, bottom = bands[0], bands[-1]
    photo_r = top["photo_chroma_rb"][0]
    engine_r = top["engine_radiance_chroma_rb"][0]
    model_r = top["reference_B_chroma_rb"][0]
    paper_r = top["reference_paper_chroma_rb"][0]
    ratio = {key: _luma(top[key]) / _luma(bottom[key]) for key in ("photo", "engine_radiance", "reference_paper")}
    return model_r / paper_r - 1.0, (
        f"仰角 {top['elevation_deg']:.1f}° の色度 r 写真 {photo_r:.2f}・エンジン {engine_r:.2f}・正解 B {model_r:.2f}・"
        f"論文の設定（Mie の係数 1/25）の正解 {paper_r:.2f}、帯の上下の明るさの比 写真 {ratio['photo']:.2f}・"
        f"論文の設定 {ratio['reference_paper']:.2f}・エンジン {ratio['engine_radiance']:.2f}。写真への寄りはモデル（B）"
        f"{engine_r - model_r:.2f}・設定 {model_r - paper_r:.2f}、残り {paper_r - photo_r:.2f} はカメラの処理（条件）。"
        f"大きさは設定の分（色度 r の正解 B ÷ 論文の設定）")


def m_photo_range(reports):
    photo = []
    for view in (reports.get("tonemap") or {}).get("views") or []:
        value = ((view.get("photo") or {}).get("metrics") or {}).get("spread")
        if value is not None:
            photo.append(float(value))
    cycles = _tonemap_metric(reports, "cycles", "matched", "neutral_plain", "spread")
    if not photo or not cycles:
        return None, "明るさの幅の値が無い"
    return _rel(float(np.mean(cycles)) / float(np.mean(photo))), (
        f"露出を合わせた明るさの幅 写真 {np.mean(photo):.2f}・Cycles {np.mean(cycles):.2f}（中立の表示変換）")


def m_photo_cliff_ground(reports):
    scores = ((reports.get("tonemap") or {}).get("scores") or {}).get("cycles_matched") or {}
    value = (scores.get("mean_abs_diff") or {}).get("neutral_plain", {}).get("log_cliff_over_ground")
    if value is None:
        return None, "崖 ÷ 地面の差の値が無い"
    return math.exp(value) - 1.0, f"崖 ÷ 地面の比の写真との差 対数の平均 {value:.2f}（Cycles・中立の表示変換）"


def m_cliff_blue_photo(reports):
    ratios = []
    for result in (reports.get("lit") or {}).get("view_results") or []:
        values = ((result.get("photo_comparison") or {}).get("values")) or {}
        photo = (values.get("photo") or {}).get("cliff", {}).get("b_over_g")
        cycles = (values.get("cycles") or {}).get("cliff", {}).get("b_over_g")
        if photo and cycles:
            ratios.append(photo / cycles)
    if not ratios:
        return None, "写真との色の比が無い"
    return _rel(float(np.mean(ratios))), f"崖の B/G 写真 ÷ Cycles {min(ratios):.2f}〜{max(ratios):.2f}（{len(ratios)} 視点）"


def m_unresolved(reports):
    count, total = 0, 0
    for key in ("gray", "lit"):
        for result in (reports.get(key) or {}).get("view_results") or []:
            for entry in (result.get("regions") or {}).values():
                total += 1
                count += 0 if _decided(entry) else 1
    return None, f"灰色・実材質の比較の領域 {total} のうち {count} が未判定（画素が足りない・収束しない）"


def m_not_compared(_):
    return None, "この段の比較に入っていない（地形だけ・wasteland-hide=all で比べた）"


# 差の項目。category は GAP_CATEGORIES、points は GAP_POINTS、needs_atmos_b は B（方向を保つ多重散乱）に依る判断で、
# 大気の正解の比較が古いときは未判定の印を付ける
GAP_ITEMS = (
    {"id": "shadow_missing", "category": "render", "points": ("buildings",), "source": ("lit", "gray"),
     "label": "落ちた影が欠ける・影に太陽が漏れる", "measure": m_shadow_missing},
    {"id": "sky_occlusion_missing", "category": "render", "points": ("buildings", "far_blur", "plants"),
     "source": ("gray",), "label": "空の遮蔽が無い（レイトレーシングのシーンに地形が入らない）", "measure": m_sky_occlusion},
    {"id": "material_ao_on_light", "category": "render", "points": ("other",), "source": ("lit",),
     "label": "材質の AO を直接光と空の光に掛ける", "measure": m_material_ao},
    {"id": "far_aliasing", "category": "render", "points": ("far_blur",), "source": ("blur_static",),
     "label": "遠景の画素あたりの標本の不足（エイリアス）", "measure": m_far_aliasing},
    {"id": "grading_current", "category": "render", "points": ("sky", "other"), "source": ("tonemap",),
     "label": "今のグレーディング（コントラスト 1.35・彩度 1.2・色温度 0.05）", "measure": m_grading},
    {"id": "grazing_sun_dark", "category": "render", "points": ("far_blur",), "source": ("gray",),
     "label": "遠くのかすめる日差しが暗い（遠景の自己影の疑い）", "measure": m_grazing_sun},
    {"id": "psi_grid", "category": "render", "points": ("sky", "fog"), "source": ("atmos_ref",),
     "label": "多重散乱の表の格子の粗さ（8 × 32）", "measure": m_psi_grid},
    {"id": "bounce_missing", "category": "render", "points": ("buildings", "plants"), "source": ("gray",),
     "label": "照り返しが無い", "measure": m_bounce},
    {"id": "taa_motion", "category": "render", "points": ("far_blur",), "source": ("blur_motion",),
     "label": "TAA で動くと履歴の読み直しで細部が消える（履歴の無い FXAA では出ない）", "measure": m_taa_motion},
    {"id": "lod_detail", "category": "render", "points": ("far_blur",), "source": ("blur_static",),
     "label": "既定の LOD の選び方で細部が欠ける", "measure": m_lod},
    {"id": "geometry_residency", "category": "render", "points": ("far_blur",), "source": ("blur_static",),
     "label": "既定のジオメトリの取り分で遠景のページが常駐しきらない", "measure": m_residency},
    {"id": "sky_illuminance_model", "category": "render", "points": ("sky",), "source": ("atmos_ref",),
     "label": "地表の空の照度の求め方", "measure": m_sky_illuminance_a},
    {"id": "sky_model_a", "category": "render", "points": ("sky",), "source": ("atmos_ref",),
     "label": "空の輝度（論文の近似 A との差）", "measure": m_sky_model_a},
    {"id": "haze_mie_thin", "category": "content", "points": ("fog",), "source": ("atmos_ref",),
     "label": "霞の Mie が空の大気の半分ほど（空と霞で同じ大気が別の濃さ）", "measure": m_haze_mie},
    {"id": "altitude_mismatch", "category": "content", "points": ("sky", "fog"), "source": ("atmos_ref",),
     "label": "太陽と地表の照度・霧の太陽を海抜 0 m で求める（谷床は約 1450 m）", "measure": m_altitude},
    {"id": "sky_multiple_scattering", "category": "render", "points": ("sky",), "source": ("atmos_ref",),
     "label": "空の高次の散乱を等方に配る（論文の近似の限界。濃い Mie で前方へ寄る光を配り直す）", "measure": m_sky_ms_b,
     "needs_atmos_b": True},
    {"id": "aerial_scatter_direction", "category": "render", "points": ("fog",), "source": ("atmos_ref",),
     "label": "霞の散乱光が正解より暗い（空気遠近の散乱光を空の LUT の輝度の割合で配る近似。太陽から離れるほど暗い）",
     "measure": m_aerial_direction, "needs_atmos_b": True},
    {"id": "fog_aerial_order", "category": "render", "points": ("fog",), "source": ("atmos_ref",),
     "label": "霧の散乱光を空気遠近の透過率で減らさない（霧と空気遠近の合成の順。遠くほど霧が明るい）",
     "measure": m_fog_order, "needs_atmos_b": True},
    {"id": "fog_sky_color_double", "category": "render", "points": ("fog",), "source": ("atmos_ref",),
     "label": "霧の周りの光に視線の向きの空の輝度を使う（太陽の側の上乗せ。二重の数えは小さい）", "measure": m_fog_double,
     "needs_atmos_b": True},
    {"id": "sky_blue_photo", "category": "content", "points": ("sky",), "source": ("atmos_ref",),
     "label": "空の Mie が濃い（写真の空がはるかに青く、上ほど暗い。残りはカメラの処理）", "measure": m_sky_blue_photo,
     "needs_atmos_b": True},
    {"id": "photo_range", "category": "open", "points": ("other",), "source": ("tonemap",),
     "label": "明るさの幅が写真より狭い", "measure": m_photo_range},
    {"id": "photo_cliff_ground", "category": "open", "points": ("other",), "source": ("tonemap",),
     "label": "崖 ÷ 地面の明るさの比が写真と違う", "measure": m_photo_cliff_ground},
    {"id": "photo_cliff_blue", "category": "open", "points": ("fog", "sky"), "source": ("lit",),
     "label": "写真の崖が青い（空の光・霞・天気）", "measure": m_cliff_blue_photo},
    {"id": "unresolved_regions", "category": "open", "points": ("other",), "source": ("gray", "lit"),
     "label": "未判定の領域", "measure": m_unresolved},
    {"id": "clouds_not_compared", "category": "open", "points": ("clouds",), "source": (),
     "label": "雲", "measure": m_not_compared},
    {"id": "buildings_not_compared", "category": "open", "points": ("buildings",), "source": (),
     "label": "建物", "measure": m_not_compared},
    {"id": "plants_not_compared", "category": "open", "points": ("plants",), "source": (),
     "label": "植物", "measure": m_not_compared},
)


def _is_atmos(item):
    return "atmos_ref" in item["source"]


def evaluate_items(reports, atmos_current, items=GAP_ITEMS):
    rows = []
    for item in items:
        magnitude, text = item["measure"](reports)
        pending = bool(item.get("needs_atmos_b")) and not atmos_current
        rows.append({"id": item["id"], "category": item["category"], "label": item["label"],
                     "points": [GAP_POINTS[p] for p in item["points"]],
                     "source": [GAP_TASKS[s] for s in item["source"]], "magnitude": magnitude, "value": text,
                     "pending_atmos": pending,
                     "note": GAP_PENDING_ATMOS if pending else ("" if atmos_current or "atmos_ref" not in item["source"]
                                                                else "大気の正解の記録は古いが、A（論文の近似）と光学的深さだけで決まる")})
    return rows


def table_markdown(rows):
    lines = []
    for category, title in GAP_CATEGORIES:
        selected = [r for r in rows if r["category"] == category]
        selected.sort(key=lambda r: (r["magnitude"] is None, -(r["magnitude"] or 0.0)))
        lines.append(f"### {title}")
        lines.append("")
        lines.append("| 順 | 差 | 大きさ | 測った値 | ユーザーの点 | 出典 | 記号 |")
        lines.append("|---|---|---|---|---|---|---|")
        for index, row in enumerate(selected, start=1):
            magnitude = "－" if row["magnitude"] is None else f"{row['magnitude']:.2f}"
            value = row["value"] + (f"。{row['note']}" if row["note"] else "")
            lines.append(f"| {index} | {row['label']} | {magnitude} | {value} | {'・'.join(row['points'])} | "
                         f"{'・'.join(row['source']) or '－'} | `{row['id']}` |")
        lines.append("")
    return "\n".join(lines).rstrip("\n")


def doc_table_section(doc_text):
    begin = doc_text.find(GAP_TABLE_BEGIN)
    end = doc_text.find(GAP_TABLE_END)
    if begin < 0 or end < begin:
        return None, None
    return begin + len(GAP_TABLE_BEGIN), end


def write_overview(rc, out_path):
    """写真｜エンジン（既定の荒野）｜エンジン（地形だけ・共通の照明）｜Cycles（同じ条件）を視点ごとに並べた 1 枚"""
    import CompareReference as CR
    from CompareLight import load_font
    font = load_font(16)
    width, height = GAP_OVERVIEW_PANEL
    views = list(rc.LIT_VIEWS)
    row_height = GAP_OVERVIEW_LABEL_HEIGHT + height
    header = 40
    sheet = Image.new("RGB", (width * 4, header + row_height * len(views)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    columns = ("写真", "エンジン（既定の荒野）", "エンジン（地形だけ・共通の照明）", "Cycles（地形だけ・共通の照明）")
    for index, label in enumerate(columns):
        draw.text((index * width + 10, 10), label, fill=(255, 210, 120), font=font)
    failures = []
    for row, view in enumerate(views):
        top = header + row * row_height
        entry = next((p for p in CR.PHOTOS if p["view"] == view), None)
        photo_path = os.path.join(CR.PHOTO_DIR, entry["id"] + ".jpg") if entry else None
        default_png = os.path.join(_scene_dir(rc, "tonemap"), f"{view}.png")
        lit_png = os.path.join(_scene_dir(rc, "lit"), f"lit_{view}.png")
        missing = [p for p in (photo_path, default_png, lit_png) if not p or not os.path.isfile(p)]
        if missing:
            failures.append(f"{view}: 一覧の画像の元が無い（{', '.join(str(m) for m in missing)}）")
            continue
        photo_panel, _, _ = CR.fit_photo(Image.open(photo_path).convert("RGB"))
        lit = Image.open(lit_png).convert("RGB")
        half = lit.width // 2
        panels = (photo_panel, Image.open(default_png).convert("RGB"), lit.crop((0, 0, half, lit.height)),
                  lit.crop((half, 0, lit.width, lit.height)))
        draw.text((10, top + 6), f"{view}（写真 {entry['id']}）", fill=(235, 235, 235), font=font)
        for column, panel in enumerate(panels):
            sheet.paste(panel.resize(GAP_OVERVIEW_PANEL, Image.LANCZOS), (column * width, top + GAP_OVERVIEW_LABEL_HEIGHT))
    sheet.save(out_path)
    return failures


def frozen_rows(rc, failures):
    """凍結した表（GAP_FROZEN）の大気以外の行。大気以外の項目の並びが今の GAP_ITEMS と同じことも確かめる"""
    path = os.path.join(rc.REPO_ROOT, GAP_FROZEN)
    if not os.path.isfile(path):
        failures.append(f"凍結した表 {GAP_FROZEN} が無い")
        return []
    with open(path, encoding="utf-8") as f:
        frozen = json.load(f)
    rows = [r for r in frozen.get("items") or [] if "SCN-WLR-ATMOS-REF" not in r["source"]]
    wanted = [item["id"] for item in GAP_ITEMS if not _is_atmos(item)]
    if [r["id"] for r in rows] != wanted:
        failures.append(f"凍結した表の大気以外の項目 {[r['id'] for r in rows]} が今の項目 {wanted} と違う")
    return rows


def run(rc, capture_dir, verify, require_atmos, atmos_addendum=False):
    failures = []
    require_atmos = require_atmos or atmos_addendum
    # 1 回の検査の中では入力のハッシュを種類ごとに 1 回だけ求める（どの比較も同じ今のファイルと照らす）
    original_hash = rc.hash_inputs
    cache = {}

    def cached_hash(kind):
        if kind not in cache:
            cache[kind] = original_hash(kind)
        return json.loads(json.dumps(cache[kind]))

    rc.hash_inputs = cached_hash
    try:
        components = {}
        reports = {}
        for key, mode, inputs_fn in GAP_COMPONENTS:
            # 大気の追補では大気以外の比較を照らさない（その行は凍結した表の値を使う）
            if atmos_addendum and key != "atmos_ref":
                continue
            entry, report = check_component(rc, key, mode, inputs_fn)
            components[key] = entry
            reports[key] = report
    finally:
        rc.hash_inputs = original_hash

    atmos_current = components["atmos_ref"]["state"] == "current"
    for key, entry in components.items():
        state = entry["state"]
        allowed_stale = key == "atmos_ref" and not require_atmos
        if state == "fail" or state == "missing" or (state == "stale" and not allowed_stale):
            reasons = entry.get("problems") or []
            reasons = reasons + (entry.get("stale_reasons") or []) + (entry.get("reasons") or [])
            failures.extend(f"{entry['task']}: {r}" for r in reasons)
        print(f"REFERENCE_CHECK gap_list component={entry['task']} state={state}"
              f"{' allowed=pending_atmos' if state == 'stale' and allowed_stale else ''} "
              f"stale_reasons={len(entry.get('stale_reasons') or [])} problems={len(entry.get('problems') or [])}")

    if atmos_addendum:
        rows = frozen_rows(rc, failures) + evaluate_items(reports, atmos_current,
                                                          [item for item in GAP_ITEMS if _is_atmos(item)])
    else:
        rows = evaluate_items(reports, atmos_current)
    for row in rows:
        if row["category"] != "open" and row["magnitude"] is None:
            failures.append(f"{row['id']}: 差の大きさを報告から求められない（{row['value']}）")
        magnitude = "none" if row["magnitude"] is None else f"{row['magnitude']:.3f}"
        print(f"REFERENCE_CHECK gap_list item={row['id']} category={row['category']} magnitude={magnitude} "
              f"pending_atmos={int(row['pending_atmos'])}")
    if require_atmos:
        pending = [r["id"] for r in rows if r["pending_atmos"]]
        if pending:
            failures.append(f"B の直し待ちの項目が残っている: {', '.join(pending)}")

    # 文書の表
    table = table_markdown(rows)
    doc_path = os.path.join(rc.REPO_ROOT, GAP_DOC)
    doc_state = "missing"
    if not os.path.isfile(doc_path):
        failures.append(f"{GAP_DOC} が無い")
    else:
        with open(doc_path, encoding="utf-8-sig", newline="") as f:
            doc_text = f.read()
        start, end = doc_table_section(doc_text)
        newline = "\r\n" if "\r\n" in doc_text else "\n"
        wanted = newline + table.replace("\n", newline) + newline
        if start is None:
            failures.append(f"{GAP_DOC} に表の印（{GAP_TABLE_BEGIN} と {GAP_TABLE_END}）が無い")
        elif doc_text[start:end] == wanted:
            doc_state = "same"
        elif verify:
            doc_state = "differs"
            failures.append(f"{GAP_DOC} の表が今の報告から作った表と違う（--verify を付けずに回して書き直す）")
        else:
            with open(doc_path, "w", encoding="utf-8-sig", newline="") as f:
                f.write(doc_text[:start] + wanted + doc_text[end:])
            doc_state = "written"
        if require_atmos:
            for marker in GAP_PENDING_MARKERS:
                if marker in doc_text:
                    failures.append(f"{GAP_DOC} に「{marker}」が残っている")

    os.makedirs(capture_dir, exist_ok=True)
    if not atmos_addendum:
        failures.extend(write_overview(rc, os.path.join(capture_dir, GAP_OVERVIEW)))

    report = {"mode": "gap_list", "verify": bool(verify), "require_atmos": bool(require_atmos),
              "atmos_addendum": bool(atmos_addendum), "frozen": GAP_FROZEN if atmos_addendum else None,
              "components": components, "atmos_current": atmos_current, "items": rows, "doc": GAP_DOC,
              "doc_table": doc_state, "overview": GAP_OVERVIEW, "failures": failures}
    # 大気の追補は凍結した時点の報告（gap_list_check.json）を上書きしない
    report_path = os.path.join(capture_dir, "gap_list_atmos_check.json" if atmos_addendum else "gap_list_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=gap_list atmos_addendum={int(bool(atmos_addendum))} "
          f"components={len(components)} "
          f"atmos_current={int(atmos_current)} items={len(rows)} doc_table={doc_state} report={report_path}")
    return 0 if not failures else 1
