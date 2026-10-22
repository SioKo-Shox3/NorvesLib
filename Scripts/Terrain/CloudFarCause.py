# 手本の視点で遠い雲が消える原因を、反実仮想で量に分ける（SCN-WL-CLOUD-FAR-CAUSE）。ReferenceCheck.py --cloud-far の本体。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --cloud-far --engine <撮影先> [--views ref_far_01,ref_far_02] [--verify]
#       [--expect Scripts/Terrain/ExpectSpecs/cloud-far-composite.json] [--flicker]
#       CaptureScene.ps1 -GameArgs cloud-dump の撮影（<視点>.cloud.json と書き出しのファイル、<視点>.png）を視点ごとに読み、
#       結果を <撮影先>/cloud_far_check.json に書く。--verify は入力のハッシュ（inputs.json）を今のファイルと照らす。
#       --expect は雲の合成（雲までの区間の空気遠近。SCN-WL-CLOUD-FAR-COMPOSITE）の期待値の行を仕様の閾値で判定する
#       （ExpectSpec.py の cloud_far）。
#
# 同じ密度場・時刻・視線（画素と歩の端数）のまま、雲の式（Common/CloudLayerCommon.glsl）の診断の切り替えを段ごとに足す:
#   段 0 = 今の描画、段 1 = 45 km の打ち切りを外す、段 2 = さらに 30〜45 km の距離の溶かしを外す、
#   （SCN-WL-CLOUD-FAR-RANGE で打ち切りと溶かしを外したので、今は段 1 = 細かい歩（最大 1024 歩）で積む、段 2 = 段 1 と同じ）、
#   段 3 = さらに雲までの区間の空気遠近を外す（SCN-WL-CLOUD-FAR-CAUSE の時は独自の 22 km の霞。SCN-WL-CLOUD-FAR-COMPOSITE で
#   地面と同じ空気遠近へ替えた。替える前の霞の値は legacy_haze として記録に併せて書く）。
# GPU の書き出しは格子の視線（横 16 × 縦 9）の後ろに、上の 7 行の視線を段 1・2・3 で評価した反実仮想の列を持つ。
# 写真と同じ画素の範囲（写真の地形の輪郭より上の空で、撮影でも空の画素）は、書き出しの格子と定数だけからエンジンの式の写し
# （CloudDumpCheck.CloudModel）で段ごとに求める。写しが GPU の反実仮想の列と合うことを合否で確かめる。
#
# 量（写真と同じ画素の範囲の視線の平均・割合。画素は撮影の 1920 × 1080 の画素の中心）:
#   - 地平線までの雲の密度の積分: 雲の層の区間の全部（打ち切りなし、透過率で止めない）の光学的深さ τ。1 − exp(−τ) が
#     CLOUD_OPACITY 以上の視線の割合を「その方向に雲がある」割合とする
#   - 段ごとの霞の前の雲の透過率（と不透明度が CLOUD_OPACITY 以上の割合）・雲の散乱光の Y（霞の前）・雲の放射輝度の Y（霞の後）
#   - 段ごとの最終の画素の雲のコントラスト: 合成の後の画素（雲の放射輝度 + 雲の後ろの空 × (1 − 不透明度 × 溶かす割合)）の Y を
#     雲の後ろの空の Y で割って 1 を引いた値。VISIBLE_CONTRAST 以上の視線の割合を「雲が見える」割合とする
#   - 段ごとの最終の画素の Y の 5 %・95 % 点 ÷ 中央値（空の明るさのばらつき。CloudPhotoStats.py の写真の値と同じ量）と、
#     切れ目（不透明度 0.5 未満）の割合。撮影の HDR（<視点>.hdr.pfm）の同じ画素の値を併記し、段 0 の写しが画と合うかを見る
#   - 原因の寄与（併存を認める）: (i) 中身 = 1 − 雲のある割合、(ii) 打ち切り = 見える割合の段 0 → 1 の増え、
#     (iii) 距離の溶かし = 段 1 → 2 の増え、(iv) 独自の霞 = 段 2 → 3 の増え、残り = 雲があるのに段 3 でも見えない割合
#     （雲の明るさが後ろの空に近い）。段の順に依らない見方として、打ち切りのまま霞だけ・溶かしと霞を外した値も書く
#   - 診断の併記: CloudPhotoStats.py の色の閾値（線形の R ÷ B ≥ 0.7）の被覆を、写真と撮影の PNG で求めた値
# 細かい歩との差（fine）: 写真と同じ画素の範囲の視線の、霞の後の雲の放射輝度の Y と不透明度の平均を段 0 と段 1 で比べた相対差。
# ちらつき（--flicker。flicker）: ref_far_01 の雲の範囲（写真と同じ画素の範囲で段 0 の不透明度が CLOUD_OPACITY 以上の画素）の、
#   連番 <撮影先>-flicker/before・after の <視点>.seq-<k>.hdr.pfm（Scripts/CameraScripts/wasteland-cloud-flicker.txt を -Hdr で撮る。
#   固定の時計で 8 フレームおきに 8 枚）の Y の時間方向の標準偏差 ÷ 平均を画素ごとに求めた平均と、後 ÷ 前の比。
# 合否（--verify）:
#   1. 書き出しがあり、撮影の起動引数に --cloud-dump がある。反実仮想の列が段 1・2・3 のどれも 1 本以上ある
#   2. 格子の視線と反実仮想の列の全部で、写しの雲の透過率が GPU と CloudDumpCheck.TRANSMITTANCE_TOLERANCE 以内、歩数が同じ
#   3. 写真と同じ画素の範囲に MIN_RANGE_RAYS 本以上の視線がある
#   4. 入力のハッシュが今のファイルと合う（--verify のとき）
# 量の値そのもの（被覆・寄与）は合否にしない（写真は撮影時の処理を含む）。
#
# 合成の期待値（composite。--expect の行が見る）: 格子の視線のうち空の画素で雲のある（不透明度 CLOUD_OPACITY 以上）視線ごとに、
# GPU の書き出しの雲の散乱光（霞の前）・透過率・雲までの距離・層に入る距離・向きを入力に、雲までの区間の空気遠近を
# EXPECT_AERIAL_STEPS 歩（GPU の 8 倍）の中点則で積み直し、期待値 (散乱光 × 区間の透過率 + 区間の散乱光 × 不透明度) × 溶かす割合を
# 作って、GPU の霞の後の雲の放射輝度と比べる（RGB の相対差の最大 max_rel）。手前の大気を二重に足す形（区間の散乱光を不透明度で
# 重みづけずに全部足す。後ろの空の画素がすでに持つ 1 − 不透明度の分を二重に数える）の値も同じ入力で作り、期待値との差の最大
# （double_add_max_rel）を書く。これが閾値より大きければ、二重に足す合成は max_rel の行で落ちる。
import json
import os

import numpy as np
from PIL import Image

import CloudDumpCheck as CD
from CloudPhotoStats import measure as photo_measure
from CompareReference import PANEL_SIZE, PHOTO_DIR, PHOTOS, find_terrain_top, fit_photo

DEFAULT_VIEWS = ("ref_far_01", "ref_far_02")
# 段の名前と診断の切り替え（GLSL の kCloudDiagnostic* の和）
STAGES = (
    ("current", 0),
    ("no_march_limit", CD.DIAGNOSTIC_NO_MARCH_LIMIT),
    ("no_fade", CD.DIAGNOSTIC_NO_MARCH_LIMIT | CD.DIAGNOSTIC_NO_FADE),
    ("no_haze", CD.DIAGNOSTIC_NO_MARCH_LIMIT | CD.DIAGNOSTIC_NO_FADE | CD.DIAGNOSTIC_NO_HAZE),
)
# 段の順に依らない見方（打ち切りのまま霞だけを外す・打ち切りのまま溶かしと霞を外す）
SINGLE_STAGES = (
    ("haze_only_removed", CD.DIAGNOSTIC_NO_HAZE),
    ("fade_and_haze_removed", CD.DIAGNOSTIC_NO_FADE | CD.DIAGNOSTIC_NO_HAZE),
)
CLOUD_OPACITY = 0.05
VISIBLE_CONTRAST = 0.10
# 記録に併せて書く薄いコントラストの閾値
FAINT_CONTRAST = 0.02
# 写真の空の画素を間引く間隔（写真の絵の画素。1280 × 720 の絵で 10 画素 = 撮影の 15 画素）
RANGE_STRIDE = 10
MIN_RANGE_RAYS = 100
LUMA = np.array([0.2126, 0.7152, 0.0722])
# 合成の期待値で雲までの区間の空気遠近を積む歩数（GPU の CD.AERIAL_STEPS とは別の離散化で求める）
EXPECT_AERIAL_STEPS = 256
# 替える前の独自の霞（雲の光の 1/e が空の色へ置き換わる距離。記録の legacy_haze だけに使う）
LEGACY_HAZE_DISTANCE = 22000.0
# SCN-WL-CLOUD-FAR-RANGE の前の打ち切りと溶かしの距離(m)。記録の層に入る距離の割合だけに使う
LEGACY_MAX_MARCH_DISTANCE = 45000.0
LEGACY_FADE_START_DISTANCE = 30000.0
# ちらつきの連番（Scripts/CameraScripts/wasteland-cloud-flicker.txt の capture=seq-<k>）の枚数と、撮影先の名前の後ろに付ける置き場
FLICKER_FRAMES = 8
FLICKER_SUFFIX = "-flicker"
FLICKER_VIEW = "ref_far_01"


def fine_difference(current, fine):
    """段 0（今の歩）と細かい歩（DIAGNOSTIC_MAX_PRIMARY_STEPS まで）の、写真と同じ画素の範囲の平均の相対差"""
    if not current or len(current) != len(fine):
        return None
    radiance = np.array([v["radiance_y"] for v in current])
    radiance_fine = np.array([v["radiance_y"] for v in fine])
    opacity = np.array([v["opacity"] for v in current])
    opacity_fine = np.array([v["opacity"] for v in fine])
    return {
        "rays": len(current),
        "radiance_mean": float(np.mean(radiance)), "radiance_mean_fine": float(np.mean(radiance_fine)),
        "radiance_mean_rel": float(abs(np.mean(radiance) - np.mean(radiance_fine)) / max(float(np.mean(radiance_fine)), 1e-6)),
        "opacity_mean": float(np.mean(opacity)), "opacity_mean_fine": float(np.mean(opacity_fine)),
        "opacity_mean_rel": float(abs(np.mean(opacity) - np.mean(opacity_fine)) / max(float(np.mean(opacity_fine)), 1e-6)),
    }


def read_pfm(path):
    """PFM（下から上の行）を上から下の (高さ, 幅, 3) の配列で読む。読めなければ None"""
    with open(path, "rb") as f:
        header = f.readline().strip()
        width, height = (int(v) for v in f.readline().split())
        scale = float(f.readline())
        data = np.fromfile(f, dtype="<f4" if scale < 0 else ">f4")
    if header != b"PF" or data.size != width * height * 3:
        return None
    return data.reshape(height, width, 3)[::-1].astype(np.float64)


def flicker_metric(sequence_dir, view, pixels):
    """連番の HDR（<視点>.seq-<k>.hdr.pfm。ブルームの前の放射輝度）の画素の Y の時間方向の標準偏差 ÷ 平均（画素ごと）の
    平均と 95 % 点。8 bit の PNG は空の画素の大半で時間の変化が量子化の 1 段より小さく 0 になるので使わない。揃わなければ (None, 理由)"""
    frames = []
    for k in range(FLICKER_FRAMES):
        path = os.path.join(sequence_dir, f"{view}.seq-{k}.hdr.pfm")
        image = read_pfm(path) if os.path.isfile(path) else None
        if image is None:
            return None, f"連番の HDR が無いか読めない {path}"
        frames.append(image @ LUMA)
    stack = np.stack(frames)
    xs = np.array([int(x) for x, _ in pixels])
    ys = np.array([int(y) for _, y in pixels])
    if stack.shape[1] <= ys.max() or stack.shape[2] <= xs.max():
        return None, f"連番の大きさ {stack.shape[2]}x{stack.shape[1]} が書き出しの画素と合わない"
    values = stack[:, ys, xs]
    ratio = values.std(axis=0) / np.maximum(values.mean(axis=0), 1e-6)
    return {"mean": float(np.mean(ratio)), "p95": float(np.percentile(ratio, 95)), "frames": FLICKER_FRAMES}, None


def measure_flicker(capture_dir, view, pixels, cloudy, failures):
    """雲の範囲の画素（写真と同じ画素の範囲のうち段 0 の不透明度が CLOUD_OPACITY 以上）の時間方向のちらつきを、変更の前と後の連番
    （<撮影先>-flicker/before・after）で比べる"""
    cloud_pixels = [p for p, c in zip(pixels, cloudy) if c]
    base = os.path.normpath(capture_dir) + FLICKER_SUFFIX
    result = {"pixels": len(cloud_pixels), "before_dir": base + "/before", "after_dir": base + "/after"}
    if not cloud_pixels:
        failures.append("ちらつき: 雲の範囲の画素が無い")
        return result
    for phase in ("before", "after"):
        value, reason = flicker_metric(os.path.join(base, phase), view, cloud_pixels)
        if value is None:
            failures.append(f"ちらつき（{phase}）: {reason}")
            return result
        result[phase] = value
    result["ratio"] = result["after"]["mean"] / max(result["before"]["mean"], 1e-9)
    return result


def luminance(rgb):
    return float(np.asarray(rgb, dtype=np.float64) @ LUMA)


def evaluate(model, march, direction, flags, legacy_haze=False):
    """1 本の視線の段の値。march は同じ視線を flags の打ち切りの扱いで進めた結果。legacy_haze は替える前の独自の霞で合成する"""
    sky = CD.sample_sky(model.sky_lut, direction)
    sky_y = luminance(sky)
    if not march["marched"]:
        return {"transmittance": 1.0, "opacity": 0.0, "scattered_y": 0.0, "radiance_y": 0.0, "contrast": 0.0,
                "fade": 1.0, "haze": 1.0, "red_blue": float(sky[0] / max(sky[2], 1e-6)), "sky_y": sky_y,
                "final_y": sky_y}
    opacity = float(np.clip(1.0 - march["transmittance"], 0.0, 1.0))
    # 距離の溶かしは SCN-WL-CLOUD-FAR-RANGE で外した
    fade = 1.0
    if legacy_haze:
        haze = float(np.exp(-march["cloud_distance"] / LEGACY_HAZE_DISTANCE))
        radiance = (march["scattered"] * haze + sky * opacity * (1.0 - haze)) * fade
    else:
        transmittance = np.ones(3)
        scattering = np.zeros(3)
        if not flags & CD.DIAGNOSTIC_NO_HAZE and model.aerial_enabled and march["cloud_distance"] > 0.0:
            transmittance, scattering = model.aerial(direction, march["cloud_distance"])
        haze = float(np.mean(transmittance))
        radiance = (march["scattered"] * transmittance + scattering * opacity) * fade
    final = radiance + sky * (1.0 - opacity * fade)
    final_y = luminance(final)
    return {"transmittance": float(march["transmittance"]), "opacity": opacity,
            "scattered_y": luminance(march["scattered"]), "radiance_y": luminance(radiance),
            "contrast": final_y / max(sky_y, 1e-6) - 1.0, "fade": fade, "haze": haze,
            "red_blue": float(final[0] / max(final[2], 1e-6)), "sky_y": sky_y, "final_y": final_y}


def read_hdr_luminance(capture_dir, view, pixels):
    """撮影の HDR（pfm、下から上の行、cd/m²）の画素の Y。無ければ None"""
    path = os.path.join(capture_dir, f"{view}.hdr.pfm")
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as f:
        header = f.readline().strip()
        width, height = (int(v) for v in f.readline().split())
        scale = float(f.readline())
        data = np.fromfile(f, dtype="<f4" if scale < 0 else ">f4")
    if header != b"PF" or data.size != width * height * 3:
        return None
    image = data.reshape(height, width, 3)[::-1]
    values = [float(image[int(y), int(x)] @ LUMA) for x, y in pixels]
    return np.array(values)


def spread(values):
    """Y の 5 %・95 % 点 ÷ 中央値"""
    values = np.asarray(values, dtype=np.float64)
    median = max(float(np.median(values)), 1e-6)
    return {"median": float(np.median(values)), "p5_over_median": float(np.percentile(values, 5) / median),
            "p95_over_median": float(np.percentile(values, 95) / median)}


def range_pixels(view, capture_dir, target):
    """写真と同じ画素の範囲（写真の空で、撮影でも空の画素）を間引いた撮影の画素の中心と、範囲の数え"""
    entry = next((p for p in PHOTOS if p["view"] == view), None)
    if entry is None:
        return None, {"reason": f"{view} に対応する手本の写真が CompareReference.PHOTOS に無い"}
    photo_path = os.path.join(PHOTO_DIR, entry["id"] + ".jpg")
    capture_path = os.path.join(capture_dir, f"{view}.png")
    for path in (photo_path, capture_path):
        if not os.path.isfile(path):
            return None, {"reason": f"ファイルが無い {path}"}
    photo_panel, _, _ = fit_photo(Image.open(photo_path).convert("RGB"))
    capture_panel = Image.open(capture_path).convert("RGB").resize(PANEL_SIZE, Image.LANCZOS)
    photo_stats, photo_sky, _ = photo_measure(photo_panel)
    capture_stats, _, _ = photo_measure(capture_panel)
    capture_top = find_terrain_top(np.asarray(capture_panel))
    rows = np.arange(PANEL_SIZE[1])[:, None]
    capture_sky = rows < (capture_top[None, :] - 2)
    both = photo_sky & capture_sky
    ys, xs = np.where(both)
    keep = (ys % RANGE_STRIDE == RANGE_STRIDE // 2) & (xs % RANGE_STRIDE == RANGE_STRIDE // 2)
    scale_x = target[0] / PANEL_SIZE[0]
    scale_y = target[1] / PANEL_SIZE[1]
    pixels = [(np.floor((x + 0.5) * scale_x) + 0.5, np.floor((y + 0.5) * scale_y) + 0.5)
              for y, x in zip(ys[keep], xs[keep])]
    info = {
        "photo": entry["id"],
        "photo_sky_pixels": int(photo_sky.sum()),
        "range_pixels": int(both.sum()),
        "photo_sky_not_capture_sky": int((photo_sky & ~capture_sky).sum()),
        "range_rows_panel": [int(ys.min()), int(ys.max())] if ys.size else None,
        "stride_panel": RANGE_STRIDE,
        "color_threshold_coverage": {
            "photo": photo_stats["coverage"] if photo_stats else None,
            "photo_bands": {k: v["coverage"] for k, v in photo_stats["coverage_bands"].items()} if photo_stats else None,
            "photo_y_p5_p95_over_median": [photo_stats["luminance_percentiles_over_median"][k] for k in ("5", "95")]
            if photo_stats else None,
            "capture": capture_stats["coverage"] if capture_stats else None,
            "capture_bands": {k: v["coverage"] for k, v in capture_stats["coverage_bands"].items()} if capture_stats else None,
        },
    }
    return pixels, info


def check_probe(model, probe, pixels, target, failures):
    """格子の視線と反実仮想の列を写しで求め直し、GPU と照らす"""
    flags_all = CD.probe_diagnostic_flags(probe)
    stats = {}
    for flags in sorted(set(int(f) for f in flags_all)):
        columns = np.where(flags_all == flags)[0]
        worst = 0.0
        step_mismatch = 0
        marched_mismatch = 0
        marched = 0
        cloudy = 0
        radiance_rel = 0.0
        for index in columns:
            pixel = pixels[index]
            direction = model.ray_direction(pixel, target)
            march = model.march(direction, model.jitter(pixel), flags=flags)
            gpu_t = float(probe[0, index, 0])
            gpu_marched = bool(int(round(probe[7, index, 3])) & 1)
            if gpu_marched != march["marched"]:
                marched_mismatch += 1
                continue
            worst = max(worst, abs(float(march["transmittance"]) - gpu_t))
            if march["marched"]:
                marched += 1
                if march["step_count"] != int(round(probe[7, index, 1])):
                    step_mismatch += 1
                if 1.0 - gpu_t >= CLOUD_OPACITY:
                    cloudy += 1
                    radiance_rel = max(radiance_rel, CD.relative_error(march["radiance"], probe[2, index, :3],
                                                                       CD.RADIANCE_FLOOR))
        stats[str(flags)] = {"rays": int(len(columns)), "marched": marched, "cloudy": cloudy,
                             "transmittance_max_abs": worst, "step_mismatch": step_mismatch,
                             "marched_mismatch": marched_mismatch, "radiance_max_rel": radiance_rel}
        label = "格子の視線" if flags == 0 else f"反実仮想の列（切り替え {flags}）"
        if marched_mismatch:
            failures.append(f"{label}: 雲の層を進めたかが GPU と違う視線が {marched_mismatch} 本")
        if step_mismatch:
            failures.append(f"{label}: 歩数が GPU と違う視線が {step_mismatch} 本")
        if not worst <= CD.TRANSMITTANCE_TOLERANCE:
            failures.append(f"{label}: 写しの雲の透過率が GPU と {CD.TRANSMITTANCE_TOLERANCE} を超えて違う（最大の差 {worst:.4f}）")
    for _, flags in STAGES[1:]:
        if str(flags) not in stats:
            failures.append(f"反実仮想の列（切り替え {flags}）が書き出しに無い")
    return stats


def check_composite(model, probe, pixels):
    """格子の視線の合成の期待値（上の説明の composite）。GPU の書き出しの霞の前の値を入力に、空気遠近を細かい歩で積み直す"""
    max_rel = 0.0
    double_add_max_rel = 0.0
    aerial_visibility_rel = 0.0
    rays = 0
    partial = 0
    worst = None
    for index in CD.grid_columns(probe):
        depth = float(probe[6, index, 3])
        sky_pixel = not (np.isfinite(depth) and depth < 0.999999)
        marched = bool(int(round(probe[7, index, 3])) & 1)
        opacity = float(np.clip(1.0 - probe[0, index, 0], 0.0, 1.0))
        if not sky_pixel or not marched or opacity < CLOUD_OPACITY:
            continue
        rays += 1
        partial += int(opacity <= 1.0 - CLOUD_OPACITY)
        direction = probe[3, index, :3].astype(np.float64)
        direction = direction / np.linalg.norm(direction)
        scattered = probe[1, index, :3].astype(np.float64)
        distance = float(probe[1, index, 3])
        fade = 1.0
        transmittance = np.ones(3)
        scattering = np.zeros(3)
        if model.aerial_enabled and distance > 0.0:
            transmittance, scattering = model.aerial(direction, distance, steps=EXPECT_AERIAL_STEPS)
        expected = (scattered * transmittance + scattering * opacity) * fade
        double_add = (scattered * transmittance + scattering) * fade
        gpu = probe[2, index, :3].astype(np.float64)
        error = CD.relative_error(gpu, expected, CD.RADIANCE_FLOOR)
        if error >= max_rel:
            max_rel = error
            worst = {"pixel": pixels[index], "opacity": opacity, "cloud_distance_m": distance, "gpu": gpu.tolist(),
                     "expected": expected.tolist()}
        double_add_max_rel = max(double_add_max_rel, CD.relative_error(double_add, expected, CD.RADIANCE_FLOOR))
        aerial_visibility_rel = max(aerial_visibility_rel,
                                    abs(float(probe[2, index, 3]) - float(np.mean(transmittance))) /
                                    max(float(np.mean(transmittance)), 1e-6))
    return {"rays": rays, "partial_rays": partial, "max_rel": max_rel if rays else None,
            "double_add_max_rel": double_add_max_rel if rays else None,
            "aerial_visibility_max_rel": aerial_visibility_rel if rays else None, "worst": worst,
            "aerial_enabled": bool(model.aerial_enabled), "expect_aerial_steps": EXPECT_AERIAL_STEPS}


def summarize(values):
    """段の値の一覧の平均と割合"""
    if not values:
        return {}
    opacity = np.array([v["opacity"] for v in values])
    contrast = np.array([v["contrast"] for v in values])
    cloudy = opacity >= CLOUD_OPACITY
    return {
        "transmittance_mean": float(np.mean([v["transmittance"] for v in values])),
        "cloud_fraction": float(np.mean(cloudy)),
        "scattered_y_mean_cloudy": float(np.mean([v["scattered_y"] for v, c in zip(values, cloudy) if c])) if cloudy.any() else None,
        "radiance_y_mean_cloudy": float(np.mean([v["radiance_y"] for v, c in zip(values, cloudy) if c])) if cloudy.any() else None,
        "sky_y_mean": float(np.mean([v["sky_y"] for v in values])),
        "contrast_mean": float(np.mean(contrast)),
        "contrast_p95": float(np.percentile(contrast, 95)),
        "visible_fraction": float(np.mean(contrast >= VISIBLE_CONTRAST)),
        "faint_fraction": float(np.mean(contrast >= FAINT_CONTRAST)),
        "fade_mean": float(np.mean([v["fade"] for v in values])),
        "haze_mean_cloudy": float(np.mean([v["haze"] for v, c in zip(values, cloudy) if c])) if cloudy.any() else None,
        "red_blue_cloud_fraction": float(np.mean([v["red_blue"] >= 0.7 for v in values])),
        "gap_fraction": float(np.mean(opacity < 0.5)),
        "final_y": spread([v["final_y"] for v in values]),
    }


def measure_view(rc, capture_dir, view, game_args, flicker=False):
    failures = []
    result = {"view": view}
    if CD.GAME_ARG not in (game_args or []):
        failures.append(f"撮影の起動引数に {CD.GAME_ARG} が無い")
    meta_path = os.path.join(capture_dir, f"{view}.cloud.json")
    if not os.path.isfile(meta_path):
        result.update({"pass": False, "failures": [f"{view}.cloud.json が無い"] + failures})
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    field = meta["density_field"]
    shape = CD.read_array(capture_dir, field["shape_noise"], np.uint8, failures, "基本の形の 3D ノイズ")
    detail = CD.read_array(capture_dir, field["detail_noise"], np.uint8, failures, "細部の 3D ノイズ")
    probe = CD.read_array(capture_dir, meta["probe"], "<f4", failures, "視線の値")
    lut = CD.read_array(capture_dir, meta["sky_radiance_lut"], "<f4", failures, "空の放射輝度 LUT")
    aerial = CD.read_array(capture_dir, meta.get("aerial_profile", {}), "<f4", failures, "空気遠近の高度ごとの表")
    if failures:
        result.update({"pass": False, "failures": failures})
        return result
    model = CD.CloudModel(meta, shape.astype(np.float64) / 255.0, detail.astype(np.float64) / 255.0,
                          lut.astype(np.float64), aerial)
    target = (int(meta["target"]["width"]), int(meta["target"]["height"]))
    result["frame_number"] = meta.get("frame_number")
    result["time_seconds"] = meta.get("time_seconds")
    result["probe"] = check_probe(model, probe, meta["probe"]["pixels"], target, failures)
    result["composite"] = check_composite(model, probe, meta["probe"]["pixels"])

    pixels, range_info = range_pixels(view, capture_dir, target)
    result["range"] = range_info
    if pixels is None:
        failures.append(f"写真と同じ画素の範囲を決められない（{range_info['reason']}）")
        result.update({"pass": False, "failures": failures})
        return result
    if len(pixels) < MIN_RANGE_RAYS:
        failures.append(f"写真と同じ画素の範囲の視線が {len(pixels)} 本で {MIN_RANGE_RAYS} 本に足りない")

    stage_values = {name: [] for name, _ in STAGES + SINGLE_STAGES}
    stage_values["legacy_haze"] = []
    full_depth = []
    start_distance = []
    for pixel in pixels:
        direction = model.ray_direction(pixel, target)
        jitter = model.jitter(pixel)
        limited = model.march(direction, jitter)
        unlimited = model.march(direction, jitter, flags=CD.DIAGNOSTIC_NO_MARCH_LIMIT)
        full_depth.append(unlimited.get("optical_depth_full", 0.0))
        start_distance.append(unlimited.get("start_distance", float("nan")))
        for name, flags in STAGES + SINGLE_STAGES:
            march = unlimited if flags & CD.DIAGNOSTIC_NO_MARCH_LIMIT else limited
            stage_values[name].append(evaluate(model, march, direction, flags))
        stage_values["legacy_haze"].append(evaluate(model, limited, direction, 0, legacy_haze=True))
    full_depth = np.array(full_depth)
    start_distance = np.array(start_distance)
    content_cloud = 1.0 - np.exp(-full_depth) >= CLOUD_OPACITY
    stages = {name: summarize(values) for name, values in stage_values.items()}
    hdr = read_hdr_luminance(capture_dir, view, pixels)
    if hdr is not None:
        result["hdr_final_y"] = spread(hdr)
        result["hdr_over_model_median"] = result["hdr_final_y"]["median"] / max(stages["current"]["final_y"]["median"], 1e-6)
    visible = [stages[name]["visible_fraction"] for name, _ in STAGES]
    cloud_fraction = float(np.mean(content_cloud))
    result.update({
        "rays": len(pixels),
        "content": {
            "cloud_fraction_to_horizon": cloud_fraction,
            "optical_depth_to_horizon_median": float(np.median(full_depth)),
            "optical_depth_to_horizon_p90": float(np.percentile(full_depth, 90)),
            "start_distance_km_median": float(np.nanmedian(start_distance) / 1000.0),
            "start_distance_km_p10_p90": [float(np.nanpercentile(start_distance, 10) / 1000.0),
                                          float(np.nanpercentile(start_distance, 90) / 1000.0)],
            "beyond_march_limit_fraction": float(np.mean(start_distance > LEGACY_MAX_MARCH_DISTANCE)),
            "within_fade_fraction": float(np.mean((start_distance > LEGACY_FADE_START_DISTANCE) &
                                                  (start_distance <= LEGACY_MAX_MARCH_DISTANCE))),
        },
        "fine": fine_difference(stage_values["current"], stage_values["no_march_limit"]),
        "stages": stages,
        "attribution": {
            "content_no_cloud": 1.0 - cloud_fraction,
            "march_limit": visible[1] - visible[0],
            "distance_fade": visible[2] - visible[1],
            "custom_haze": visible[3] - visible[2],
            "visible_current": visible[0],
            "visible_all_removed": visible[3],
            "cloud_but_invisible_after_all": cloud_fraction - visible[3],
        },
    })
    if flicker and view == FLICKER_VIEW:
        cloudy = [v["opacity"] >= CLOUD_OPACITY for v in stage_values["current"]]
        result["flicker"] = measure_flicker(capture_dir, view, pixels, cloudy, failures)
    result.update({"pass": not failures, "failures": failures})
    return result


def run(rc, capture_dir, views, verify, expect=None, flicker=False):
    failures = []
    inputs_ok, input_failures, record = rc.check_inputs(capture_dir)
    if verify and not inputs_ok:
        failures.extend(input_failures)
    metric_views = rc.views_from_metrics(capture_dir)
    game_args = rc.plate_capture_args(capture_dir)
    if metric_views is None or game_args is None:
        failures.append("metrics.json が無い（CaptureScene.ps1 の出力先でない）")
        metric_views = []
    targets = views if views else list(DEFAULT_VIEWS)
    results = []
    for view in targets:
        if view not in metric_views:
            failures.append(f"{view}: metrics.json に無い視点")
            continue
        result = measure_view(rc, capture_dir, view, game_args, flicker)
        results.append(result)
        failures.extend(f"{view}: {failure}" for failure in result["failures"])
        for flags, stats in result.get("probe", {}).items():
            print(f"REFERENCE_CHECK cloud_far view={view} probe_flags={flags} rays={stats['rays']} "
                  f"marched={stats['marched']} cloudy={stats['cloudy']} "
                  f"transmittance_max_abs={stats['transmittance_max_abs']:.4f} step_mismatch={stats['step_mismatch']} "
                  f"radiance_max_rel={stats['radiance_max_rel']:.4f}")
        composite = result.get("composite")
        if composite:
            fmt = lambda v: "none" if v is None else f"{v:.4f}"
            print(f"REFERENCE_CHECK cloud_far view={view} composite rays={composite['rays']} "
                  f"partial_rays={composite['partial_rays']} aerial_enabled={int(composite['aerial_enabled'])} "
                  f"max_rel={fmt(composite['max_rel'])} double_add_max_rel={fmt(composite['double_add_max_rel'])} "
                  f"aerial_visibility_max_rel={fmt(composite['aerial_visibility_max_rel'])}")
        if "content" not in result:
            continue
        content = result["content"]
        coverage = result["range"]["color_threshold_coverage"]
        print(f"REFERENCE_CHECK cloud_far view={view} rays={result['rays']} "
              f"range_pixels={result['range']['range_pixels']} "
              f"cloud_to_horizon={content['cloud_fraction_to_horizon']:.3f} "
              f"tau_median={content['optical_depth_to_horizon_median']:.3f} "
              f"start_km_median={content['start_distance_km_median']:.1f} "
              f"beyond_45km={content['beyond_march_limit_fraction']:.3f} "
              f"photo_rb_coverage={coverage['photo']:.3f} capture_rb_coverage={coverage['capture']:.3f}")
        for name, stage in result["stages"].items():
            print(f"REFERENCE_CHECK cloud_far view={view} stage={name} "
                  f"transmittance_mean={stage['transmittance_mean']:.3f} cloud_fraction={stage['cloud_fraction']:.3f} "
                  f"scattered_y={stage['scattered_y_mean_cloudy']} radiance_y={stage['radiance_y_mean_cloudy']} "
                  f"sky_y={stage['sky_y_mean']:.0f} contrast_mean={stage['contrast_mean']:.4f} "
                  f"contrast_p95={stage['contrast_p95']:.4f} visible={stage['visible_fraction']:.3f} "
                  f"faint={stage['faint_fraction']:.3f} rb_cloud={stage['red_blue_cloud_fraction']:.3f} "
                  f"gap={stage['gap_fraction']:.3f} y_p5={stage['final_y']['p5_over_median']:.3f} "
                  f"y_p95={stage['final_y']['p95_over_median']:.3f}")
        if "hdr_final_y" in result:
            hdr = result["hdr_final_y"]
            photo_spread = coverage.get("photo_y_p5_p95_over_median") or [float("nan")] * 2
            print(f"REFERENCE_CHECK cloud_far view={view} hdr_y_p5={hdr['p5_over_median']:.3f} "
                  f"hdr_y_p95={hdr['p95_over_median']:.3f} hdr_over_model_median={result['hdr_over_model_median']:.3f} "
                  f"photo_y_p5={photo_spread[0]:.3f} photo_y_p95={photo_spread[1]:.3f}")
        fine = result.get("fine")
        if fine:
            print(f"REFERENCE_CHECK cloud_far view={view} fine rays={fine['rays']} "
                  f"radiance_mean={fine['radiance_mean']:.1f} radiance_mean_fine={fine['radiance_mean_fine']:.1f} "
                  f"radiance_mean_rel={fine['radiance_mean_rel']:.4f} opacity_mean={fine['opacity_mean']:.4f} "
                  f"opacity_mean_fine={fine['opacity_mean_fine']:.4f} opacity_mean_rel={fine['opacity_mean_rel']:.4f}")
        flick = result.get("flicker")
        if flick and "ratio" in flick:
            print(f"REFERENCE_CHECK cloud_far view={view} flicker pixels={flick['pixels']} "
                  f"before_mean={flick['before']['mean']:.5f} after_mean={flick['after']['mean']:.5f} "
                  f"before_p95={flick['before']['p95']:.5f} after_p95={flick['after']['p95']:.5f} ratio={flick['ratio']:.4f}")
        attribution = result["attribution"]
        print(f"REFERENCE_CHECK cloud_far view={view} attribution " +
              " ".join(f"{key}={value:.3f}" for key, value in attribution.items()))
    report = {
        "mode": "cloud_far",
        "verify": bool(verify),
        "flicker": bool(flicker),
        "inputs_current": inputs_ok,
        "inputs_record": record,
        "game_args": game_args,
        "criteria": {
            "cloud_opacity": CLOUD_OPACITY, "visible_contrast": VISIBLE_CONTRAST, "faint_contrast": FAINT_CONTRAST,
            "range_stride_panel": RANGE_STRIDE, "min_range_rays": MIN_RANGE_RAYS,
            "transmittance_tolerance": CD.TRANSMITTANCE_TOLERANCE,
            "stages": {name: flags for name, flags in STAGES + SINGLE_STAGES},
        },
        "views": results,
        "failures": failures,
    }
    rc.apply_expect(report, expect, "cloud_far", failures)
    report["failures"] = failures
    report_path = os.path.join(capture_dir, "cloud_far_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False, default=float)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=cloud_far views={len(targets)} inputs_current={int(bool(inputs_ok))} "
          f"report={report_path}")
    return 0 if not failures else 1
