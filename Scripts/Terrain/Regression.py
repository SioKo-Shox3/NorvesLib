# 回帰の撮影（Scripts/Terrain/CaptureRegression.ps1）の after を before と並べる（ReferenceCheck.py --regression から呼ぶ。単独では動かさない）。
#
# before・after の下にシーンごとの撮影（CaptureScene.ps1 の出力。metrics.json と PNG）が並ぶ。シーンごと・PNG ごとに:
#   落ちる: after にシーンか PNG が無い（撮影の欠け）、after の CaptureScene が失敗を書いた、真っ黒（輝度の平均が BLACK_MEAN 未満か
#           最大が BLACK_MAX 以下）、検証レイヤーのエラーが 1 件以上か記録が無い（metrics.json の vulkan_validation.error_count。
#           回帰の撮影は RelWithDebInfo に --validation-layer を付けて検証レイヤーを入れる）、before・after の保存時点で
#           ジオメトリのページが進行中、または落ち着きの基準から保存までに追い出しの数が変わった
#   記録する: before との差（輝度の PSNR〔dB〕と平均の差〔0〜255〕）。差は合否に使わない（変更の意図した差か見るため）
#   記録する: before・after の視点ごとのジオメトリのページの値と落ち着き（保存前に行が無ければ対象外）。
# 同じ置き場を before・after に指定すると、before のページの判定と記録だけを行う。
# 結果は <after>/regression_check.json に書く。
import json
import math
import os
import re

import numpy as np
from PIL import Image

BLACK_MEAN = 1.0
BLACK_MAX = 2.0
LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)


def scene_dirs(root):
    if not root or not os.path.isdir(root):
        return []
    return sorted(d for d in os.listdir(root) if os.path.isfile(os.path.join(root, d, "metrics.json")))


def load_luma(path):
    image = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    return image @ LUMA


def validation_errors(metrics):
    views = metrics.get("views") or []
    if isinstance(views, dict):
        views = [views]
    total = 0
    for view in views:
        info = (view or {}).get("vulkan_validation")
        if not info:
            return None
        total += int(info.get("error_count") or 0)
    return total


def check_geometry_pages(rc, scene_dir, scene, phase, failures):
    """各視点の保存時点のページの落ち着きを記録し、進行中か追い出しがあれば失敗にする。"""
    with open(os.path.join(scene_dir, "metrics.json"), encoding="utf-8-sig") as f:
        metrics = json.load(f)
    views = metrics.get("views") or []
    if isinstance(views, dict):
        views = [views]
    records = {}
    fields = ("resident", "uploading", "ready", "reading", "wanted", "evicted", "budget_blocked")
    for view_info in views:
        view = view_info["view"]
        log_name = rc.view_game_log(scene_dir, view)
        log_path = os.path.join(scene_dir, log_name) if log_name else None
        lines = []
        if log_path and os.path.isfile(log_path):
            with open(log_path, encoding="utf-8-sig", errors="replace") as f:
                lines = f.readlines()
        pages = rc.geometry_pages_at_capture(lines, single_view=view).get(view)
        record = dict.fromkeys(fields)
        record["settle_evicted"] = []
        if pages is None:
            record.update(status="not_applicable", reason="保存の前の GEOMETRY_PAGES の行が無い")
        else:
            record.update(pages)
            # ページの値と同じ保存の時点で、最後のページの行の予算による保留を採る。
            last_budget_blocked = None
            for line in lines:
                if rc.BLUR_PAGES_RE.search(line):
                    found = re.search(r"\bbudget_blocked=(\d+)", line)
                    last_budget_blocked = int(found.group(1)) if found else None
                found = re.search(r"CAPTURE_VIEWS view=(\S+) saved ", line)
                saved_view = found.group(1) if found else None
                if saved_view is None and "ApplicationProcessor capture_png saved " in line:
                    saved_view = view
                if saved_view == view:
                    record["budget_blocked"] = last_budget_blocked
            complete, _ = rc.geometry_pages_resident_complete(pages)
            record["status"] = "settled" if complete else "unsettled"
            if not complete:
                values = "・".join(f"{key}={record[key]}" for key in fields)
                failures.append(f"{scene}: {phase} の撮影でジオメトリのページが落ち着いていない"
                                f"（視点 {view}、{values}、基準から保存までの evicted {record['settle_evicted']}）")
        records[view] = record
        values = " ".join(f"{key}={record[key]}" for key in
                          ("resident", "wanted", "ready", "reading", "uploading", "evicted", "budget_blocked"))
        print(f"REFERENCE_CHECK regression_pages scene={scene} phase={phase} view={view} "
              f"status={record['status']} {values}")
    return records


def run(rc, after_dir, before_dir, verify):
    before_dir = before_dir or os.path.join(os.path.dirname(after_dir), "before")
    before_only = os.path.normcase(os.path.normpath(os.path.abspath(after_dir))) == os.path.normcase(
        os.path.normpath(os.path.abspath(before_dir)))
    failures = []
    report = {"mode": "regression-before" if before_only else "regression", "verify": bool(verify), "after": os.path.relpath(after_dir, rc.REPO_ROOT),
              "before": os.path.relpath(before_dir, rc.REPO_ROOT), "scenes": {}}
    before_scenes = scene_dirs(before_dir)
    if not before_scenes:
        failures.append(f"before の撮影が無い（{before_dir}）")
    for scene in sorted(set(before_scenes) | set(scene_dirs(after_dir))):
        entry = {"geometry_pages": {"before": {}, "after": {}}}
        report["scenes"][scene] = entry
        after_scene = os.path.join(after_dir, scene)
        before_scene = os.path.join(before_dir, scene)
        if scene in before_scenes:
            entry["geometry_pages"]["before"] = check_geometry_pages(rc, before_scene, scene, "before", failures)
        if before_only:
            continue
        entry["shots"] = {}
        if not os.path.isfile(os.path.join(after_scene, "metrics.json")):
            failures.append(f"{scene}: after の撮影が無い（撮影の欠け）")
            continue
        with open(os.path.join(after_scene, "metrics.json"), encoding="utf-8-sig") as f:
            metrics = json.load(f)
        entry["geometry_pages"]["after"] = check_geometry_pages(rc, after_scene, scene, "after", failures)
        if metrics.get("failures"):
            failures.extend(f"{scene}: 撮影の失敗: {item}" for item in metrics["failures"])
        errors = validation_errors(metrics)
        entry["validation_errors"] = errors
        if errors is None:
            failures.append(f"{scene}: 検証レイヤーの記録が無い（検証レイヤーを入れて撮っていない）")
        elif errors:
            failures.append(f"{scene}: 検証レイヤーのエラーが {errors} 件ある")
        before_pngs = sorted(n for n in os.listdir(before_scene) if n.endswith(".png")) if os.path.isdir(before_scene) else []
        after_pngs = sorted(n for n in os.listdir(after_scene) if n.endswith(".png"))
        if not after_pngs:
            failures.append(f"{scene}: after に PNG が無い（撮影の欠け）")
        for name in sorted(set(before_pngs) | set(after_pngs)):
            shot = {}
            entry["shots"][name] = shot
            after_png = os.path.join(after_scene, name)
            if not os.path.isfile(after_png):
                failures.append(f"{scene}/{name}: after に無い（撮影の欠け）")
                continue
            luma = load_luma(after_png)
            shot["mean_luminance"] = float(luma.mean())
            shot["max_luminance"] = float(luma.max())
            if shot["mean_luminance"] < BLACK_MEAN or shot["max_luminance"] <= BLACK_MAX:
                failures.append(f"{scene}/{name}: 真っ黒（平均 {shot['mean_luminance']:.2f}・最大 {shot['max_luminance']:.0f}）")
            before_png = os.path.join(before_scene, name)
            if not os.path.isfile(before_png):
                shot["before"] = None
                continue
            before = load_luma(before_png)
            if before.shape != luma.shape:
                shot["size_changed"] = [list(before.shape), list(luma.shape)]
                continue
            mse = float(np.mean((luma - before) ** 2))
            shot["psnr_db"] = float("inf") if mse == 0.0 else 10.0 * math.log10(255.0 ** 2 / mse)
            shot["mean_diff"] = float(luma.mean() - before.mean())
            print(f"REFERENCE_CHECK regression scene={scene} shot={name} mean={shot['mean_luminance']:.2f} "
                  f"psnr={shot['psnr_db']:.2f} mean_diff={shot['mean_diff']:+.3f}")
    report["failures"] = failures
    report_path = os.path.join(after_dir, "regression_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False, default=str)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode={report['mode']} scenes={len(report['scenes'])} "
          f"report={report_path}")
    return 0 if not failures else 1
