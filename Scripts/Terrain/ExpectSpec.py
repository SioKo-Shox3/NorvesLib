# 比較の検査（--gray・--lit・--blur-static・--blur-motion）に、領域・成分ごとの合否の閾値（仕様の JSON）を足す
# （SCN-WLR-EXPECT-MATCH。ReferenceCheck.py の --expect と --expect-selftest から呼ぶ。単独では動かさない）。
#
# 仕様の JSON（置き場は Scripts/Terrain/ExpectSpecs/）: {"description": "<説明>", "rows": [<行>, ...]}（行の並びだけでもよい）。
# 行の鍵（ほかの鍵は書き間違いとして拒む）:
#   mode: gray | lit | blur_static | blur_motion。走らせたモードと違う行があれば仕様の誤りとして落とす
#   view: 視点。region: 領域（gray・lit は GrayRegions.REGIONS、blur_* は far・cliff）
#   component: 成分（gray・lit だけ）。total・sun・sky・indirect・visibility・chroma（RGB の色度）
#   case: 組（blur_static は条件の組の名前、blur_motion は fxaa・taa・base）。tag: 撮影の名前（blur_motion だけ。static-0・motion-30 など）
#   static: 振りと止めの差で比べる止めの撮影（blur_motion だけ。省くと tag の motion-<k> に対する static-<k>）
#   quantity: 量
#     gray・lit: ratio（エンジン ÷ Cycles の比。成分 total・sun・sky・indirect）、chroma_diff（色度の差。成分 chroma）、
#       visibility_mismatch（可視率の食い違いの割合。成分 visibility）
#     blur_static: detail_loss（細部の欠け）、alias（エイリアス）、detail_loss_vs_resident・alias_vs_resident（その組 − resident。
#       base と resident の差は case=base で書く）
#     blur_motion: detail_loss・alias（tag の撮影の値）、motion_static_detail_loss・motion_static_alias（振りと止めの差。振り − 止め）
#   min・max: 下限・上限（両端を含む。どちらか一方は要る）
#   allow_undetermined: 未判定を許すか（既定は許さない）。note: 行の説明（判定には使わない）
#
# 判定: 値が min 以上 max 以下なら合格。値が求まらない（領域が未判定・画素が無い・その量をまだ測っていない）行は未判定で、
# allow_undetermined が真でなければ落とす。gray・lit の領域の判定・未判定は比較の検査の status（収束と画素の数）に従う。
# 色度は領域の線形の平均の RGB の (r, g) = (R, G) / (R + G + B) で、差は r と g の差の絶対値の大きい方。
# indirect（エンジンの間接光の分）と visibility（エンジンの太陽の可視率）は、比較の検査の領域に indirect_ratio・
# visibility_mismatch の値がある撮影でだけ求まる（今の撮影には無いので未判定になる）。
import contextlib
import hashlib
import io
import json
import os

MODES = ("gray", "lit", "blur_static", "blur_motion")
ROW_KEYS = ("mode", "view", "region", "component", "case", "tag", "static", "quantity", "min", "max",
            "allow_undetermined", "note")
LIGHT_QUANTITIES = {"total": ("ratio",), "sun": ("ratio",), "sky": ("ratio",), "indirect": ("ratio",),
                    "visibility": ("visibility_mismatch",), "chroma": ("chroma_diff",)}
# 成分ごとの比の鍵（比較の検査の領域の値）と、無いときの理由
LIGHT_RATIO_KEYS = {"total": ("ratio", "比が無い"),
                    "sun": ("sun_ratio", "太陽だけのエンジンの撮影（engine-sun）が無い"),
                    "sky": ("sky_ratio", "空だけのエンジンの撮影（engine-sky）が無い"),
                    "indirect": ("indirect_ratio", "エンジンの間接光を分けた撮影が無い")}
BLUR_REGIONS = ("far", "cliff")
BLUR_STATIC_QUANTITIES = ("detail_loss", "alias", "detail_loss_vs_resident", "alias_vs_resident")
BLUR_MOTION_QUANTITIES = ("detail_loss", "alias", "motion_static_detail_loss", "motion_static_alias")
BLUR_VALUE_KEYS = {"detail_loss": "fine_detail_loss", "alias": "fine_alias"}
JUDGED = "判定"

SPEC_DIR = os.path.join("Scripts", "Terrain", "ExpectSpecs")
SELFTEST_DIR = os.path.join(".harness", "runs", "scene-capture", "SCN-WLR-EXPECT-MATCH")
SELFTEST_CAPTURES = {"gray": "SCN-WLR-CYCLES-GRAY", "lit": "SCN-WLR-CYCLES-LIT",
                     "blur_static": "SCN-WLR-FAR-BLUR-STATIC", "blur_motion": "SCN-WLR-FAR-BLUR-MOTION"}


def load_spec(path):
    """仕様の JSON を読む。(行の並び, 説明, 誤りの一覧)"""
    try:
        with open(path, encoding="utf-8-sig") as f:
            spec = json.load(f)
    except (OSError, ValueError) as error:
        return [], "", [f"仕様を読めない（{error}）"]
    if isinstance(spec, list):
        return spec, "", []
    if not isinstance(spec, dict) or not isinstance(spec.get("rows"), list):
        return [], "", ["仕様は行の並びか、rows に行の並びを持つ表にする"]
    return spec["rows"], str(spec.get("description", "")), []


def validate_row(rc, row, mode):
    """行の誤り（鍵・語彙・閾値）の一覧"""
    if not isinstance(row, dict):
        return ["行が表でない"]
    errors = [f"知らない鍵 {key}" for key in row if key not in ROW_KEYS]
    if row.get("mode") not in MODES:
        return errors + [f"mode {row.get('mode')!r} は {'・'.join(MODES)} のどれでもない"]
    if row["mode"] != mode:
        errors.append(f"mode {row['mode']} は走らせたモード {mode} と違う")
    quantity = row.get("quantity")
    if row["mode"] in ("gray", "lit"):
        views = rc.GRAY_VIEWS if row["mode"] == "gray" else rc.LIT_VIEWS
        if row.get("view") not in views:
            errors.append(f"view {row.get('view')!r} は {row['mode']} の視点でない")
        if row.get("region") not in rc.GR.REGIONS:
            errors.append(f"region {row.get('region')!r} は領域でない（{'・'.join(rc.GR.REGIONS)}）")
        component = row.get("component")
        if component not in LIGHT_QUANTITIES:
            errors.append(f"component {component!r} は成分でない（{'・'.join(LIGHT_QUANTITIES)}）")
        elif quantity not in LIGHT_QUANTITIES[component]:
            errors.append(f"成分 {component} の量は {'・'.join(LIGHT_QUANTITIES[component])}（{quantity!r} でない）")
        errors.extend(f"{key} は blur_* の行だけの鍵" for key in ("case", "tag", "static") if key in row)
    else:
        if row.get("region") not in BLUR_REGIONS:
            errors.append(f"region {row.get('region')!r} は far か cliff")
        if "component" in row:
            errors.append("component は gray・lit の行だけの鍵")
        if row["mode"] == "blur_static":
            cases = [case["name"] for case in rc.BLUR_CASES]
            if row.get("view") not in rc.BLUR_VIEWS:
                errors.append(f"view {row.get('view')!r} は blur_static の視点でない")
            if row.get("case") not in cases:
                errors.append(f"case {row.get('case')!r} は条件の組でない（{'・'.join(cases)}）")
            if quantity not in BLUR_STATIC_QUANTITIES:
                errors.append(f"blur_static の量は {'・'.join(BLUR_STATIC_QUANTITIES)}（{quantity!r} でない）")
            errors.extend(f"{key} は blur_motion の行だけの鍵" for key in ("tag", "static") if key in row)
        else:
            tags = [f"static-{k}" for k in rc.MOTION_OFFSETS] + [rc.MOTION_FINAL] + \
                [f"motion-{k}" for k in rc.MOTION_OFFSETS]
            if row.get("view") != rc.MOTION_VIEW:
                errors.append(f"view {row.get('view')!r} は blur_motion の視点 {rc.MOTION_VIEW} でない")
            if row.get("case") not in rc.MOTION_AA_GROUPS:
                errors.append(f"case {row.get('case')!r} は組でない（{'・'.join(rc.MOTION_AA_GROUPS)}）")
            if quantity not in BLUR_MOTION_QUANTITIES:
                errors.append(f"blur_motion の量は {'・'.join(BLUR_MOTION_QUANTITIES)}（{quantity!r} でない）")
            elif quantity.startswith("motion_static_"):
                if not str(row.get("tag", "")).startswith("motion-") or row.get("tag") not in tags:
                    errors.append(f"振りと止めの差の tag は振りの撮影（{row.get('tag')!r} でない）")
                if "static" in row and (row["static"] not in tags or not row["static"].startswith("static-")):
                    errors.append(f"static {row['static']!r} は止めの撮影でない")
            else:
                if row.get("tag") not in tags:
                    errors.append(f"tag {row.get('tag')!r} は撮影の名前でない（{'・'.join(tags)}）")
                if "static" in row:
                    errors.append("static は振りと止めの差の行だけの鍵")
    bounds = [row.get(key) for key in ("min", "max")]
    if all(value is None for value in bounds):
        errors.append("min か max のどちらかが要る")
    for key in ("min", "max"):
        if key in row and (isinstance(row[key], bool) or not isinstance(row[key], (int, float))):
            errors.append(f"{key} が数でない")
    if not errors and None not in bounds and bounds[0] > bounds[1]:
        errors.append(f"min {bounds[0]} が max {bounds[1]} より大きい")
    if "allow_undetermined" in row and not isinstance(row["allow_undetermined"], bool):
        errors.append("allow_undetermined は真偽")
    return errors


def chromaticity(rgb):
    total = sum(rgb)
    return (rgb[0] / total, rgb[1] / total) if total > 0 else None


def light_value(report, row):
    """gray・lit の行の値。(値, 未判定の理由)"""
    result = next((r for r in report.get("view_results") or [] if r.get("view") == row["view"]), None)
    if result is None:
        return None, "視点が検査に無い"
    entry = (result.get("regions") or {}).get(row["region"])
    if entry is None:
        return None, "領域が検査に無い"
    if entry.get("status") != JUDGED:
        return None, entry.get("status") or "未判定"
    component = row["component"]
    if component in LIGHT_RATIO_KEYS:
        key, reason = LIGHT_RATIO_KEYS[component]
        value = entry.get(key)
        return (value, None) if value is not None else (None, reason)
    if component == "visibility":
        value = entry.get("visibility_mismatch")
        return (value, None) if value is not None else (None, "エンジンの太陽の可視率（AOV）が無い")
    engine = chromaticity(entry.get("engine_rgb") or [0.0, 0.0, 0.0])
    cycles = chromaticity(entry.get("cycles_rgb") or [0.0, 0.0, 0.0])
    if engine is None or cycles is None:
        return None, "領域の RGB が無い"
    return max(abs(engine[0] - cycles[0]), abs(engine[1] - cycles[1])), None


def blur_region_value(result, region, quantity):
    if result is None:
        return None
    side = result.get(region)
    return None if side is None else side.get(BLUR_VALUE_KEYS[quantity])


def blur_static_value(rc, report, row):
    view = next((v for v in report.get("views") or [] if v.get("view") == row["view"]), None)
    if view is None or "cases" not in view:
        return None, "視点を比べていない"
    cases = view["cases"]
    quantity = row["quantity"]
    base = quantity.replace("_vs_resident", "")
    value = blur_region_value(cases.get(row["case"]), row["region"], base)
    if value is None:
        return None, f"組 {row['case']} の {row['region']} の値が無い"
    if quantity.endswith("_vs_resident"):
        baseline = blur_region_value(cases.get(rc.BLUR_BASELINE), row["region"], base)
        if baseline is None:
            return None, f"組 {rc.BLUR_BASELINE} の {row['region']} の値が無い"
        value -= baseline
    return value, None


def blur_motion_value(report, row):
    if (report.get("view") or {}).get("view") != row["view"]:
        return None, "視点を比べていない"
    results = (report.get("results") or {}).get(row["case"])
    if results is None:
        return None, f"組 {row['case']} を比べていない"
    quantity = row["quantity"]
    if quantity.startswith("motion_static_"):
        static_tag = row.get("static") or "static-" + row["tag"].split("-", 1)[1]
        base = quantity.replace("motion_static_", "")
        motion = blur_region_value(results.get(row["tag"]), row["region"], base)
        static = blur_region_value(results.get(static_tag), row["region"], base)
        if motion is None or static is None:
            return None, f"{row['tag']} か {static_tag} の {row['region']} の値が無い"
        return motion - static, None
    value = blur_region_value(results.get(row["tag"]), row["region"], quantity)
    return (value, None) if value is not None else (None, f"{row['tag']} の {row['region']} の値が無い")


def row_label(row):
    parts = [row.get("mode"), row.get("view"), row.get("case"), row.get("tag"), row.get("region"), row.get("component"),
             row.get("quantity")]
    if row.get("static"):
        parts.append(f"static={row['static']}")
    return "/".join(str(p) for p in parts if p is not None)


def evaluate(rc, mode, report, rows):
    """行ごとの判定。{"rows": [...], "failures": [...], "pass": 真偽}"""
    failures = []
    results = []
    if not rows:
        failures.append("仕様に行が無い")
    for index, row in enumerate(rows):
        errors = validate_row(rc, row, mode)
        label = row_label(row) if isinstance(row, dict) else f"行 {index}"
        if errors:
            failures.extend(f"仕様の行 {index}（{label}）: {error}" for error in errors)
            results.append({"index": index, "label": label, "status": "仕様の誤り", "errors": errors, "pass": False})
            continue
        if mode in ("gray", "lit"):
            value, reason = light_value(report, row)
        elif mode == "blur_static":
            value, reason = blur_static_value(rc, report, row)
        else:
            value, reason = blur_motion_value(report, row)
        low, high = row.get("min"), row.get("max")
        entry = {"index": index, "label": label, "row": row, "value": value, "min": low, "max": high}
        if value is None:
            allowed = bool(row.get("allow_undetermined", False))
            entry.update({"status": "未判定", "reason": reason, "pass": allowed})
            if not allowed:
                failures.append(f"期待の行 {index}（{label}）が未判定（{reason}）で、未判定を許していない")
        else:
            ok = (low is None or value >= low) and (high is None or value <= high)
            entry.update({"status": JUDGED, "pass": ok})
            if not ok:
                failures.append(f"期待の行 {index}（{label}）の値 {value:.4f} が範囲 [{low}, {high}] の外")
        results.append(entry)
    return {"rows": results, "failures": failures, "pass": not failures}


def evaluate_file(rc, path, mode, report):
    rows, description, errors = load_spec(path)
    result = evaluate(rc, mode, report, rows) if not errors else {"rows": [], "failures": [], "pass": False}
    result["failures"] = [f"仕様 {rel(rc, path)}: {e}" for e in errors] + result["failures"]
    result["pass"] = not result["failures"]
    with open(path, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    result.update({"spec": rel(rc, path), "spec_sha256": digest, "description": description})
    return result


def rel(rc, path):
    return os.path.relpath(path, rc.REPO_ROOT).replace("\\", "/")


def print_result(result):
    for entry in result["rows"]:
        value = entry.get("value")
        print(f"REFERENCE_CHECK expect row={entry['index']} {entry['label']} status={entry['status']} "
              f"value={'none' if value is None else f'{value:.4f}'} min={entry.get('min')} max={entry.get('max')} "
              f"pass={int(entry['pass'])}" + (f" reason={entry['reason']}" if entry.get("reason") else ""))
    failed = sum(1 for entry in result["rows"] if not entry["pass"])
    print(f"REFERENCE_CHECK expect result={'pass' if result['pass'] else 'fail'} spec={result['spec']} "
          f"rows={len(result['rows'])} failed={failed}")


def apply(rc, report, path, mode, failures):
    """比較の検査の報告に仕様の判定を足す（report["expect"]）。外れた行を failures に足す"""
    if not path:
        return
    if not os.path.isfile(path):
        failures.append(f"仕様 {path} が無い")
        report["expect"] = {"spec": path, "pass": False, "rows": [], "failures": [f"仕様 {path} が無い"]}
        return
    result = evaluate_file(rc, path, mode, report)
    report["expect"] = result
    print_result(result)
    failures.extend(result["failures"])


# ---- --expect-selftest ----

def run_mode(rc, mode, capture_dir, verify, spec_path):
    """モードを --expect つきで走らせる。(終了コード, 報告, 出力の結果の行)"""
    runners = {"gray": lambda: rc.run_gray(capture_dir, [], verify, expect=spec_path),
               "lit": lambda: rc.run_lit(capture_dir, [], verify, expect=spec_path),
               "blur_static": lambda: rc.run_blur_static(capture_dir, verify, expect=spec_path),
               "blur_motion": lambda: rc.run_blur_motion(capture_dir, verify, expect=spec_path)}
    reports = {"gray": "gray_check.json", "lit": "lit_check.json", "blur_static": "blur_static_check.json",
               "blur_motion": "blur_motion_check.json"}
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = runners[mode]()
    lines = [line for line in buffer.getvalue().splitlines()
             if line.startswith(("REFERENCE_CHECK result=", "REFERENCE_CHECK expect", "REFERENCE_CHECK failure"))]
    with open(os.path.join(capture_dir, reports[mode]), encoding="utf-8") as f:
        return code, json.load(f), lines


def run_selftest(rc, verify):
    """保存した撮影で、今の値で通る仕様が通り、外れる行・許さない未判定・仕様の誤りがあると落ちることを確かめる。
    結果を SELFTEST_DIR/expect_selftest.json に書く"""
    spec_dir = os.path.join(rc.REPO_ROOT, SPEC_DIR)
    out_dir = os.path.join(rc.REPO_ROOT, SELFTEST_DIR)
    os.makedirs(out_dir, exist_ok=True)
    cases = []
    failures = []

    def expect(name, ok, want_ok, reasons, want_text=None):
        hit = (ok == want_ok) and (want_text is None or any(want_text in r for r in reasons))
        cases.append({"case": name, "ok": bool(ok), "expected_ok": want_ok, "want_text": want_text, "reasons": reasons,
                      "pass": hit})
        print(f"REFERENCE_CHECK expect_selftest case={name} ok={int(bool(ok))} expected={int(want_ok)} pass={int(hit)}"
              + (f" reason={reasons[0]}" if reasons else ""))
        if not hit:
            failures.append(f"{name}: 期待 {want_ok}、結果 {ok}（{'; '.join(reasons[:2])}）")

    spec = lambda name: os.path.join(spec_dir, name)
    reports = {}
    # 灰色: 外れる行のある仕様で走らせると落ち（落ちる側を先に走らせ、報告は通る仕様の結果を残す）、今の値の仕様で通る
    capture = os.path.join(rc.REPO_ROOT, ".harness", "runs", "scene-capture", SELFTEST_CAPTURES["gray"])
    code, report, lines = run_mode(rc, "gray", capture, verify, spec("selftest-fail.json"))
    for line in lines:
        print(f"  {line}")
    expect("gray_run_fail_spec", code == 0, False, (report.get("expect") or {}).get("failures") or [],
           "shadow_cliff/total/ratio")
    others = [f for f in report.get("failures") or [] if not f.startswith(("期待の行", "仕様"))]
    expect("gray_run_fail_spec_only_expect", not others, True, others)
    for mode, pass_spec, fail_spec, fail_text in (
            ("gray", "selftest-pass.json", "selftest-fail-undetermined.json", "未判定"),
            ("lit", "selftest-pass-lit.json", "selftest-fail-lit.json", "sunlit_ground/total/ratio"),
            ("blur_static", "selftest-pass-blur-static.json", "selftest-fail-blur-static.json",
             "detail_loss_vs_resident"),
            ("blur_motion", "selftest-pass-blur-motion.json", "selftest-fail-blur-motion.json",
             "motion_static_detail_loss")):
        capture = os.path.join(rc.REPO_ROOT, ".harness", "runs", "scene-capture", SELFTEST_CAPTURES[mode])
        code, report, lines = run_mode(rc, mode, capture, verify, spec(pass_spec))
        for line in lines:
            print(f"  {line}")
        reports[mode] = report
        expect(f"{mode}_run_pass_spec", code == 0, True, report.get("failures") or [])
        rows = (report.get("expect") or {}).get("rows") or []
        expect(f"{mode}_pass_spec_rows_judged", bool(rows) and all(r["pass"] for r in rows), True,
               [r["label"] for r in rows if not r["pass"]])
        # 落ちる仕様は、走らせた報告に同じ判定を掛けて確かめる（モードの経路は通る仕様の走行で通っている）
        result = evaluate_file(rc, spec(fail_spec), mode, report)
        expect(f"{mode}_fail_spec", result["pass"], False, result["failures"], fail_text)
    # 通る仕様の中の、未判定を許した行が未判定のまま合格になっていること
    gray_rows = (reports["gray"].get("expect") or {}).get("rows") or []
    allowed = [r for r in gray_rows if r["status"] == "未判定"]
    expect("gray_allowed_undetermined", bool(allowed) and all(r["pass"] for r in allowed), True,
           [r["label"] for r in allowed if not r["pass"]] or ([] if allowed else ["未判定を許した行が無い"]))
    # 今は測っていない量（エンジンの可視率）は未判定として落ちる
    unmeasured = evaluate(rc, "gray", reports["gray"], [
        {"mode": "gray", "view": "ref_cliff_01", "region": "sunlit_ground", "component": "visibility",
         "quantity": "visibility_mismatch", "max": 0.05}])
    expect("gray_unmeasured_visibility", unmeasured["pass"], False, unmeasured["failures"], "可視率")
    # 仕様の誤り（知らない量・別のモードの行・閾値の無い行・知らない鍵）は落ちる
    for name, row, text in (
            ("spec_unknown_quantity", {"mode": "gray", "view": "ref_cliff_01", "region": "far", "component": "total",
                                       "quantity": "brightness", "max": 1.0}, "の量は"),
            ("spec_other_mode", {"mode": "lit", "view": "ref_cliff_01", "region": "far", "component": "total",
                                 "quantity": "ratio", "max": 2.0}, "走らせたモード"),
            ("spec_no_bounds", {"mode": "gray", "view": "ref_cliff_01", "region": "far", "component": "total",
                                "quantity": "ratio"}, "min か max"),
            ("spec_unknown_key", {"mode": "gray", "view": "ref_cliff_01", "region": "far", "component": "total",
                                  "quantity": "ratio", "max": 2.0, "allow_undetermind": True}, "知らない鍵")):
        result = evaluate(rc, "gray", reports["gray"], [row])
        expect(name, result["pass"], False, result["failures"], text)
    empty = evaluate(rc, "gray", reports["gray"], [])
    expect("spec_empty", empty["pass"], False, empty["failures"], "行が無い")

    record = {"mode": "expect_selftest", "verify": bool(verify), "cases": cases, "failures": failures}
    report_path = os.path.join(out_dir, "expect_selftest.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=expect_selftest cases={len(cases)} "
          f"verify={int(bool(verify))} report={report_path}")
    return 0 if not failures else 1
