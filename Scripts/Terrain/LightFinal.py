# 共通の照明（地形だけ・霞なし）の直しの仕様をまとめて今の撮影で通す（SCN-WL-LIGHT-FINAL。
# ReferenceCheck.py --light-final から呼ぶ。単独では動かさない）。
#
#   python Scripts/Terrain/ReferenceCheck.py --light-final --engine-root <撮影の置き場> [--verify]
#
# 後の直しが前の直しを壊していないことを確かめるため、これまでの直しの仕様（CHECKS の ExpectSpecs）を、同じ Game.exe で
# 撮り直した 1 つの置き場の撮影で、仕様を書いたときと同じ比べ方（--gray・--lit・--bounce と同じ参照）で判定する。
# 置き場の組（SETS。どれも CaptureScene.ps1 -Hdr -Aov -LaunchPerView、wasteland-common-light・wasteland-hide=all・
# wasteland-finest-lod・wasteland-aa=fxaa）:
#   gray/（灰色の材質の太陽と空の両方。engine-sun/・engine-sky/ に太陽だけ・空だけ）: ref_cliff_01・ref_far_01
#   lit/（実材質。照り返しの入を兼ねる）: ref_cliff_01・ref_cliff_02・ref_ground_01・ref_far_01
#   bounce-off/（実材質で --rtgi-bounce=off）: ref_cliff_01
# 描画の経路を前の形へ戻す開発ビルドの起動引数（VARIANT_ARGS。例 --rtgi-specular=off）は、全部の組に同じものが付いていれば
#   組の条件に足して受け入れる（同じ Game.exe・同じ焼いた地形で変更の前後を比べる。まとめの variant_args に書く）。
# 落ちる: 組の撮影が無い・起動引数が組の条件と違う・組ごとの Game.exe とエンジンの実装が揃っていない・仕様の視点が組に無い・
#   仕様ごとの比べ方（--verify なら入力の照合を含む）のどれかが落ちる。
# 仕様ごとの比べ方の報告は <置き場>/check-<仕様>/（--gray・--lit）と <置き場>/lit/bounce_check.json（--bounce）、
# まとめは <置き場>/light_final_check.json。
import contextlib
import io
import json
import os

REPORT = "light_final_check.json"
SPEC_DIR = os.path.join("Scripts", "Terrain", "ExpectSpecs")
GRAY_REFERENCE = os.path.join(".harness", "runs", "scene-capture", "SCN-WLR-CYCLES-GRAY")
LIT_REFERENCE = os.path.join(".harness", "runs", "scene-capture", "SCN-WLR-CYCLES-LIT")
# AA は仕様を書いたときの既定（FXAA）に固定する。昼の荒野の既定が TAA になった後は、撮影の深度と HDR に TAA のジッタの
# 端数（0.1〜0.5 画素）が乗り、画素の中心で描いた Cycles の材質の標本の位置の照合（0.1 画素）が成り立たない
BASE_ARGS = ["--wasteland-common-light", "--wasteland-hide=all", "--wasteland-finest-lod", "--wasteland-aa=fxaa"]
# 組 → (起動引数〔順は問わない〕, 撮る視点)
SETS = {
    "gray": (BASE_ARGS + ["--wasteland-gray-material"], ("ref_cliff_01", "ref_far_01")),
    "gray/engine-sun": (BASE_ARGS + ["--common-light-case=sun", "--wasteland-gray-material"], ("ref_cliff_01", "ref_far_01")),
    "gray/engine-sky": (BASE_ARGS + ["--common-light-case=sky", "--wasteland-gray-material"], ("ref_cliff_01", "ref_far_01")),
    "lit": (BASE_ARGS, ("ref_cliff_01", "ref_cliff_02", "ref_ground_01", "ref_far_01")),
    "bounce-off": (BASE_ARGS + ["--rtgi-bounce=off"], ("ref_cliff_01",)),
}
# 全部の組に同じものが付いていれば受け入れる起動引数（描画の経路を前の形へ戻す開発ビルドの引数）
VARIANT_ARGS = ("--rtgi-specular=off",)
# 仕様 → (比べ方, 使う組〔--bounce は入・切の順〕, 参照)。視点は仕様の行から取る
CHECKS = (
    ("rt-tlas-range", "gray", ("gray",), GRAY_REFERENCE),
    ("shadow-leak", "gray", ("gray",), GRAY_REFERENCE),
    ("shadow-pages", "lit", ("lit",), LIT_REFERENCE),
    ("material-ao", "lit", ("lit",), LIT_REFERENCE),
    ("rtgi-color", "bounce", ("lit", "bounce-off"), LIT_REFERENCE),
)


def check_sets(root):
    """組の撮影の有無・起動引数・Game.exe とエンジンの実装の揃い。(組ごとの記録, 失敗の一覧)"""
    failures = []
    records = {}
    for name, (game_args, views) in SETS.items():
        set_dir = os.path.join(root, *name.split("/"))
        entry = {"dir": name, "views": list(views)}
        records[name] = entry
        metrics_path = os.path.join(set_dir, "metrics.json")
        inputs_path = os.path.join(set_dir, "inputs.json")
        if not os.path.isfile(metrics_path) or not os.path.isfile(inputs_path):
            failures.append(f"組 {name} の撮影（metrics.json・inputs.json）が無い")
            continue
        with open(metrics_path, encoding="utf-8-sig") as f:
            metrics = json.load(f)
        with open(inputs_path, encoding="utf-8-sig") as f:
            inputs = json.load(f)
        entry.update({"game_args": metrics.get("game_args"), "game_exe_sha256": inputs.get("game_exe_sha256"),
                      "files_sha256": inputs.get("files_sha256"), "git_commit": inputs.get("git_commit")})
        recorded = metrics.get("game_args") or []
        variant = sorted(arg for arg in recorded if arg in VARIANT_ARGS and arg not in game_args)
        entry["variant_args"] = variant
        if sorted(recorded) != sorted(game_args + variant):
            failures.append(f"組 {name} の起動引数 {metrics.get('game_args')} が組の条件 {game_args} と違う")
        if metrics.get("failures"):
            failures.append(f"組 {name} の撮影に失敗の記録がある")
        missing = [v for v in views if not os.path.isfile(os.path.join(set_dir, f"{v}.hdr.pfm"))]
        if missing:
            failures.append(f"組 {name} に視点 {'・'.join(missing)} の HDR が無い")
    for key in ("game_exe_sha256", "files_sha256"):
        values = {entry.get(key) for entry in records.values() if "game_args" in entry}
        if len(values) > 1:
            failures.append(f"組ごとの {key} が揃っていない（{len(values)} 通り）")
    variants = {tuple(entry.get("variant_args") or ()) for entry in records.values() if "game_args" in entry}
    if len(variants) > 1:
        failures.append(f"組ごとの前の形へ戻す起動引数が揃っていない（{sorted(variants)}）")
    return records, failures


def spec_views(rc, path, mode):
    """仕様の行の視点（出てくる順）と、行の比べ方が違うときの失敗"""
    import ExpectSpec
    rows, _description, errors = ExpectSpec.load_spec(path)
    views = []
    for row in rows:
        if isinstance(row, dict) and row.get("mode") != mode:
            errors.append(f"行の比べ方 {row.get('mode')} が {mode} でない")
        view = row.get("view") if isinstance(row, dict) else None
        if view and view not in views:
            views.append(view)
    return views, errors


def run_check(rc, root, spec, mode, set_names, reference, views, verify):
    """1 つの仕様の比べ方を走らせる。(終了コード, 報告, 出力の行)"""
    spec_path = os.path.join(rc.REPO_ROOT, SPEC_DIR, f"{spec}.json")
    engine_dirs = [os.path.join(root, *name.split("/")) for name in set_names]
    reference_root = os.path.join(rc.REPO_ROOT, reference)
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        if mode == "bounce":
            import RtgiBounce
            code = RtgiBounce.run(rc, engine_dirs, reference_root, views, verify, spec_path)
            report_path = os.path.join(engine_dirs[0], RtgiBounce.REPORT)
        else:
            out_dir = os.path.join(root, f"check-{spec}")
            os.makedirs(out_dir, exist_ok=True)
            runner = rc.run_gray if mode == "gray" else rc.run_lit
            code = runner(out_dir, views, verify, engine_dirs, reference_root, spec_path)
            report_path = os.path.join(out_dir, f"{mode}_check.json")
    report = {}
    if os.path.isfile(report_path):
        with open(report_path, encoding="utf-8") as f:
            report = json.load(f)
    return code, report, report_path, buffer.getvalue().splitlines()


def run(rc, root, verify):
    if not root or not os.path.isdir(root):
        print(f"REFERENCE_CHECK result=fail reason=engine_root_missing（--engine-root に撮影の置き場を指定する） path={root}")
        return 1
    failures = []
    sets, set_failures = check_sets(root)
    failures.extend(set_failures)
    report = {"mode": "light_final", "verify": bool(verify), "engine_root": rc.rel_dir(root), "sets": sets, "checks": []}
    for name, entry in sets.items():
        print(f"REFERENCE_CHECK light_final set={name} game_exe={str(entry.get('game_exe_sha256'))[:8]} "
              f"files={str(entry.get('files_sha256'))[:8]} commit={str(entry.get('git_commit'))[:8]} "
              f"args={','.join(entry.get('game_args') or [])}")
    for spec, mode, set_names, reference in CHECKS:
        spec_path = os.path.join(rc.REPO_ROOT, SPEC_DIR, f"{spec}.json")
        views, spec_errors = spec_views(rc, spec_path, mode)
        failures.extend(f"仕様 {spec}: {e}" for e in spec_errors)
        uncovered = [v for v in views if not all(v in SETS[name][1] for name in set_names)]
        if uncovered:
            failures.append(f"仕様 {spec} の視点 {'・'.join(uncovered)} が組 {'・'.join(set_names)} に無い")
            continue
        # 灰色の比べ方は組の下の engine-sun/・engine-sky/ も読む
        needed = set_names + (("gray/engine-sun", "gray/engine-sky") if mode == "gray" else ())
        if not all("game_args" in sets[name] for name in needed):
            failures.append(f"仕様 {spec} の組 {'・'.join(set_names)} の撮影が揃わないので判定しない")
            continue
        code, sub_report, report_path, lines = run_check(rc, root, spec, mode, set_names, reference, views, verify)
        for line in lines:
            print(f"LIGHT_FINAL spec={spec} | {line}")
        expect = sub_report.get("expect") or {}
        rows = [{"label": r.get("label"), "status": r.get("status"), "value": r.get("value"), "min": r.get("min"),
                 "max": r.get("max"), "pass": r.get("pass")} for r in expect.get("rows", [])]
        entry = {"spec": spec, "mode": mode, "sets": list(set_names), "reference": reference.replace("\\", "/"),
                 "views": views, "exit_code": code, "report": rc.rel_dir(report_path), "rows": rows,
                 "spec_sha256": expect.get("spec_sha256"), "failures": sub_report.get("failures", [])}
        report["checks"].append(entry)
        if code != 0:
            failures.append(f"仕様 {spec}（--{mode}）が落ちた: " + "／".join(entry["failures"][:5] or ["報告が無い"]))
        for row in rows:
            value = row["value"]
            print(f"REFERENCE_CHECK light_final spec={spec} row={row['label']} status={row['status']} "
                  f"value={'none' if value is None else f'{value:.4f}'} min={row['min']} max={row['max']} "
                  f"pass={int(bool(row['pass']))}")
        print(f"REFERENCE_CHECK light_final spec={spec} mode={mode} result={'pass' if code == 0 else 'fail'} "
              f"rows={len(rows)} failed={sum(1 for r in rows if not r['pass'])}")
    report["failures"] = failures
    report_path = os.path.join(root, REPORT)
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=light_final specs={len(report['checks'])}/{len(CHECKS)} "
          f"report={report_path}")
    return 0 if not failures else 1
