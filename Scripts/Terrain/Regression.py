# 回帰の撮影（Scripts/Terrain/CaptureRegression.ps1）の after を before と並べる（ReferenceCheck.py --regression から呼ぶ。単独では動かさない）。
#
# before・after の下にシーンごとの撮影（CaptureScene.ps1 の出力。metrics.json と PNG）が並ぶ。シーンごと・PNG ごとに:
#   落ちる: after にシーンか PNG が無い（撮影の欠け）、after の CaptureScene が失敗を書いた、真っ黒（輝度の平均が BLACK_MEAN 未満か
#           最大が BLACK_MAX 以下）、検証レイヤーのエラーが 1 件以上か記録が無い（metrics.json の vulkan_validation.error_count。
#           回帰の撮影は RelWithDebInfo に --validation-layer を付けて検証レイヤーを入れる）
#   記録する: before との差（輝度の PSNR〔dB〕と平均の差〔0〜255〕）。差は合否に使わない（変更の意図した差か見るため）
# 結果は <after>/regression_check.json に書く。
import json
import math
import os

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


def run(rc, after_dir, before_dir, verify):
    before_dir = before_dir or os.path.join(os.path.dirname(after_dir), "before")
    failures = []
    report = {"mode": "regression", "verify": bool(verify), "after": os.path.relpath(after_dir, rc.REPO_ROOT),
              "before": os.path.relpath(before_dir, rc.REPO_ROOT), "scenes": {}}
    before_scenes = scene_dirs(before_dir)
    if not before_scenes:
        failures.append(f"before の撮影が無い（{before_dir}）")
    for scene in sorted(set(before_scenes) | set(scene_dirs(after_dir))):
        entry = {"shots": {}}
        report["scenes"][scene] = entry
        after_scene = os.path.join(after_dir, scene)
        before_scene = os.path.join(before_dir, scene)
        if not os.path.isfile(os.path.join(after_scene, "metrics.json")):
            failures.append(f"{scene}: after の撮影が無い（撮影の欠け）")
            continue
        with open(os.path.join(after_scene, "metrics.json"), encoding="utf-8-sig") as f:
            metrics = json.load(f)
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
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=regression scenes={len(report['scenes'])} "
          f"report={report_path}")
    return 0 if not failures else 1
