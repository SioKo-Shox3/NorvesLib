# 荒野の正解の絵の比較（SCN-WLR-*、計画は Docs/Plans/WastelandRebuild.md）で、保存した撮影の結果を検査する。
# 重い描画（エンジンの撮影の組・Cycles）は反復の中で行って結果を保存し、verify はこのスクリプトで保存した結果を検査する。
#
# 比較の約束（全 SCN-WLR-* 共通）:
#   値は線形の Rec.709 の原色・白色点 D65、輝度 Y = 0.2126R + 0.7152G + 0.0722B、放射輝度は cd/m²
#   （エンジンのプリエクスポージャを外した値）。測光用の HDR はブルームの前で読む。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --record-inputs --kind engine --capture <出力先>
#       撮影の入力のハッシュを <出力先>/inputs.json に書く（CaptureScene.ps1 -Hdr が撮り終えたときに呼ぶ）。
#   python Scripts/Terrain/ReferenceCheck.py --hdr --capture <出力先> [--views a,b,...] [--verify]
#       CaptureScene.ps1 -Hdr の撮影（<視点>.png・<視点>.hdr.pfm・<視点>.hdr.json・metrics.json）を視点ごとに検査し、
#       結果を <出力先>/hdr_check.json に書く。--views を省くと metrics.json の全視点を見る。
#       --verify は入力のハッシュ（inputs.json）を今のファイルと照らし、違えば古い結果として失敗にする。
#
# 入力のハッシュ（inputs.json）: 種類ごとに決めたパス（INPUT_SETS）の、git が追跡するか無視されていないファイルの中身の SHA-256 を
# パスの順に並べたものの SHA-256。荒野の撮影では描画のコード・シェーダー・荒野の組み立て・視点の定義・資産の組の定義・撮影の台本・
# 共通の照明（Scripts/Terrain/CommonLight.json。無ければ無いことを記録）。git のコミットと、入力に未コミットの変更があったかも書く
# （記録のためで、照合には中身のハッシュを使う。撮影の後にコミットしても古い結果にはならない）。
#
# --hdr の検査（視点ごと）:
#   1. 大きさ: PFM の見出し・hdr.json・PNG の幅と高さが一致する。
#   2. 値の範囲: R・G・B に負・NaN・Inf が無い。輝度の最大が正（真っ黒でない）。輝度の分位（1・50・99 %）を書く。
#   3. PNG との照合: HDR にプリエクスポージャを掛けて、エンジンの表示変換（荒野のカメラの差し替え: NeutralLinear のトーンマップ、
#      知覚的なコントラスト・彩度・輝度を保つ色温度、ビネット、sRGB の符号化。Assets/Shaders/tonemapping.frag・vignette.frag と同じ式）
#      を近似で通した 8 bit の値を、同じ視点の PNG と比べる（見た目の 3D LUT・色収差・レンズダートが掛かるカメラは
#      見積もれないので失敗にする）。比べる画素は次をすべて満たすものだけ:
#        - 暗い画素: 露出後の最大の成分が DARK_PEAK 未満（NeutralLinear が明部を圧縮し始める 0.8 の手前で、
#          トーンマップが値をそのまま返す範囲。荒野の日なたの地面は露出後 0.5〜0.75 で、0.4 未満の画素は影の中の
#          縁と細かな模様にほぼ限られ、ブルームと FXAA を除くと数が足りない。0.4 未満だけの一致も記録する）
#        - ブルームの影響が小さい: 露出後の値と、その周り（ガウスの半径 BLOOM_SIGMAS 画素）の平均との差に、ブルームの混ぜ具合
#          （SceneView のブルーム 4 %）を掛けた見積もりが、BLOOM_RELATIVE × 値 + BLOOM_ABSOLUTE 以下
#        - 縁でない: PNG と HDR から作った値の 3×3 の輝度の幅が EDGE_LEVELS 段以下（FXAA が縁を混ぜるため）
#      合否: 比べる画素が MIN_SAMPLE_RATIO 以上あり、3 成分とも ±WITHIN_LEVELS 段に入る画素が WITHIN_RATIO 以上、
#      差の絶対値の中央値が MEDIAN_LEVELS 段以下。表示変換の厳密な照合（1 段以内）は SCN-WLR-DISPLAY-TRANSFORM で行う。
#      ブルームの前であることの確かめとして、ブルームの影響が大きい暗い画素の PNG − 見積もりの平均（正なら PNG にだけブルームが乗る）も書く。
#
# 終了コード: すべて合格なら 0、どれかが不合格なら 1。
import argparse
import hashlib
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# 種類ごとの入力（リポジトリのルートからのパス。フォルダはその下のすべて）
INPUT_SETS = {
    "engine": [
        "Assets/Shaders",
        "Library/Core/Private/Rendering",
        "Library/Core/Public/Rendering",
        "Game/GameModes/Wasteland",
        "Game/Scenes/SceneCatalog.h",
        "Assets/AssetSets/WastelandTerrain.json",
        "Assets/AssetSets/WastelandModels.json",
        "Assets/AssetSets/WastelandBuildingsModels.json",
        "Scripts/CaptureScene.ps1",
        "Scripts/Terrain/CommonLight.json",
    ],
}

# Rec.709 の輝度の重み
LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)

# PNG との照合の基準（上の説明を参照）
DARK_PEAK = 0.8
# 記録だけに使う、より暗い画素の上限
DEEP_DARK_PEAK = 0.4
BLOOM_INTENSITY = 0.04
BLOOM_SIGMAS = (8.0, 32.0, 96.0)
BLOOM_RELATIVE = 0.01
BLOOM_ABSOLUTE = 0.0002
EDGE_LEVELS = 6
MIN_SAMPLE_RATIO = 0.005
WITHIN_LEVELS = 2
WITHIN_RATIO = 0.95
MEDIAN_LEVELS = 1.0


def git_output(args):
    try:
        return subprocess.run(["git"] + args, cwd=REPO_ROOT, capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None


def list_input_files(paths):
    """パスの下の、git が追跡するか無視されていないファイル（リポジトリのルートからの / 区切りのパス、順に並べる）"""
    listed = git_output(["ls-files", "-z", "--cached", "--others", "--exclude-standard", "--"] + paths)
    if listed is None:
        raise RuntimeError("git ls-files を実行できない")
    files = sorted({f for f in listed.split("\0") if f and os.path.isfile(os.path.join(REPO_ROOT, f))})
    return files


def hash_inputs(kind):
    paths = INPUT_SETS[kind]
    files = list_input_files(paths)
    total = hashlib.sha256()
    per_path = {}
    for path in paths:
        per_path[path] = hashlib.sha256()
    absent = [p for p in paths if not os.path.exists(os.path.join(REPO_ROOT, p))]
    for rel in files:
        with open(os.path.join(REPO_ROOT, rel), "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        line = f"{rel}\0{digest}\n".encode("utf-8")
        total.update(line)
        for path in paths:
            if rel == path or rel.startswith(path.rstrip("/") + "/"):
                per_path[path].update(line)
    for path in absent:
        total.update(f"{path}\0absent\n".encode("utf-8"))
    return {
        "files_sha256": total.hexdigest(),
        "file_count": len(files),
        "paths": {p: ("absent" if p in absent else per_path[p].hexdigest()) for p in paths},
    }


def record_inputs(capture_dir, kind):
    hashed = hash_inputs(kind)
    commit = (git_output(["rev-parse", "HEAD"]) or "").strip()
    status = git_output(["status", "--porcelain", "--"] + INPUT_SETS[kind]) or ""
    record = {
        "kind": kind,
        "git_commit": commit,
        "inputs_dirty_at_capture": bool(status.strip()),
        **hashed,
    }
    path = os.path.join(capture_dir, "inputs.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    print(f"REFERENCE_CHECK record_inputs kind={kind} files={hashed['file_count']} sha256={hashed['files_sha256']} "
          f"commit={commit[:12]} dirty={int(record['inputs_dirty_at_capture'])}")
    return 0


def check_inputs(capture_dir):
    """inputs.json を今のファイルと照らす。(合格か, 理由の一覧, 記録)"""
    path = os.path.join(capture_dir, "inputs.json")
    if not os.path.isfile(path):
        return False, ["inputs.json が無い（撮影が入力のハッシュを書いていない）"], None
    with open(path, encoding="utf-8") as f:
        record = json.load(f)
    kind = record.get("kind")
    if kind not in INPUT_SETS:
        return False, [f"inputs.json の種類 {kind} を知らない"], record
    current = hash_inputs(kind)
    if current["files_sha256"] == record.get("files_sha256"):
        return True, [], record
    changed = [p for p, h in current["paths"].items() if record.get("paths", {}).get(p) != h]
    return False, [f"入力が撮影の後に変わった（古い結果）: {', '.join(changed) or '一覧'}"], record


def read_pfm(path):
    with open(path, "rb") as f:
        data = f.read()
    # 見出しは 3 つの空白区切りの語（"PF"、幅 高さ、スケール）の後に 1 つの区切り文字
    tokens = []
    pos = 0
    while len(tokens) < 4:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        if start == pos:
            raise ValueError("PFM の見出しが短い")
        tokens.append(data[start:pos].decode("ascii"))
    pos += 1
    if tokens[0] != "PF":
        raise ValueError(f"RGB の PFM でない（{tokens[0]}）")
    width, height, scale = int(tokens[1]), int(tokens[2]), float(tokens[3])
    dtype = "<f4" if scale < 0 else ">f4"
    count = width * height * 3
    pixels = np.frombuffer(data, dtype=dtype, count=count, offset=pos)
    if len(data) - pos != count * 4:
        raise ValueError(f"PFM の画素の量が見出しと合わない（{len(data) - pos} バイト、見出しから {count * 4} バイト）")
    # PFM は下の行から並ぶので、上下を返して画像の上を 0 行目にする
    return pixels.reshape(height, width, 3)[::-1].astype(np.float64)


def box_blur(image, radius):
    """縦横の箱の平均（端は端の値を延ばす）"""
    if radius <= 0:
        return image
    out = image
    for axis in (0, 1):
        pad = [(0, 0)] * out.ndim
        pad[axis] = (radius + 1, radius)
        padded = np.pad(out, pad, mode="edge")
        csum = np.cumsum(padded, axis=axis)
        n = out.shape[axis]
        upper = np.take(csum, np.arange(2 * radius + 1, 2 * radius + 1 + n), axis=axis)
        lower = np.take(csum, np.arange(0, n), axis=axis)
        out = (upper - lower) / (2 * radius + 1)
    return out


def gaussian_blur(image, sigma):
    """箱の平均を 3 回重ねたガウスの近似（半径は分散が sigma² に合うように選ぶ）"""
    radius = max(1, int(round((np.sqrt(4.0 * sigma * sigma + 1.0) - 1.0) / 2.0)))
    out = image
    for _ in range(3):
        out = box_blur(out, radius)
    return out


def neutral_linear(color):
    # tonemapping.frag の TonemapNeutralLinear と同じ式
    color = np.maximum(color, 0.0)
    peak = color.max(axis=-1, keepdims=True)
    start, desat = 0.8, 0.15
    d = 1.0 - start
    safe_peak = np.maximum(peak, 1e-12)
    new_peak = 1.0 - d * d / (peak + d - start)
    compressed = color * (new_peak / safe_peak)
    g = 1.0 - 1.0 / (desat * (peak - new_peak) + 1.0)
    compressed = compressed + (new_peak - compressed) * g
    return np.where(peak < start, color, compressed)


def contrast_perceptual(color, contrast, pivot_linear):
    pivot = np.clip(np.clip(pivot_linear, 0.0, 1.0) ** (1.0 / 2.2), 0.01, 0.99)
    x = np.clip(color, 0.0, 1.0) ** (1.0 / 2.2)
    lower = pivot * (x / pivot) ** contrast
    upper = 1.0 - (1.0 - pivot) * (np.maximum(1.0 - x, 0.0) / (1.0 - pivot)) ** contrast
    y = np.where(x >= pivot, upper, lower)
    return np.clip(y, 0.0, 1.0) ** 2.2


def saturation(color, amount):
    luma = (color * LUMA).sum(axis=-1, keepdims=True)
    return np.clip(luma + (color - luma) * amount, 0.0, 1.0)


def white_balance(color, temp):
    gain = np.array([1.0 + temp * 0.1, 1.0, 1.0 - temp * 0.1])
    gain = gain / (gain * LUMA).sum()
    return np.clip(color * gain, 0.0, 1.0)


def vignette(height, width, intensity, radius, softness):
    # 画素の中心の UV（vignette.frag の ComputeVignette と同じ式）
    v = (np.arange(height) + 0.5) / height - 0.5
    u = (np.arange(width) + 0.5) / width - 0.5
    dist = np.sqrt(u[None, :] ** 2 + v[:, None] ** 2)
    edge0, edge1 = radius - softness, radius
    t = np.clip((dist - edge0) / max(edge1 - edge0, 1e-9), 0.0, 1.0)
    s = t * t * (3.0 - 2.0 * t)
    return (1.0 + ((1.0 - s) - 1.0) * intensity)[..., None]


def encode_srgb(linear):
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1.0 / 2.4) - 0.055)


def predict_display(exposed, display):
    """露出後の HDR から、荒野のカメラの差し替えの表示変換を近似で通した 8 bit の値（0〜255 の実数）"""
    mapped = neutral_linear(exposed)
    graded = contrast_perceptual(mapped, display["contrast"], display["contrast_pivot"])
    graded = saturation(graded, display["saturation"])
    graded = white_balance(graded, display["temperature"])
    height, width = exposed.shape[:2]
    graded = graded * vignette(height, width, display["vignette_intensity"], display["vignette_radius"],
                               display["vignette_softness"])
    return encode_srgb(graded) * 255.0


def local_range(luma):
    """3×3 の窓の最大 − 最小"""
    padded = np.pad(luma, 1, mode="edge")
    h, w = luma.shape
    windows = [padded[dy:dy + h, dx:dx + w] for dy in range(3) for dx in range(3)]
    stack = np.stack(windows)
    return stack.max(axis=0) - stack.min(axis=0)


def check_hdr_view(capture_dir, view):
    result = {"view": view, "pass": False, "failures": []}
    fails = result["failures"]
    png_path = os.path.join(capture_dir, f"{view}.png")
    pfm_path = os.path.join(capture_dir, f"{view}.hdr.pfm")
    meta_path = os.path.join(capture_dir, f"{view}.hdr.json")
    for path in (png_path, pfm_path, meta_path):
        if not os.path.isfile(path):
            fails.append(f"{os.path.basename(path)} が無い")
    if fails:
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    try:
        hdr = read_pfm(pfm_path)
    except ValueError as error:
        fails.append(f"PFM を読めない: {error}")
        return result
    png = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.float64)

    # 1. 大きさ
    height, width = hdr.shape[:2]
    result["size"] = f"{width}x{height}"
    if (meta.get("width"), meta.get("height")) != (width, height):
        fails.append(f"hdr.json の大きさ {meta.get('width')}x{meta.get('height')} が PFM {width}x{height} と違う")
    if png.shape[:2] != (height, width):
        fails.append(f"PNG の大きさ {png.shape[1]}x{png.shape[0]} が HDR {width}x{height} と違う")
    if meta.get("units") != "cd/m2" or meta.get("source") != "scene_color_before_bloom":
        fails.append(f"hdr.json の単位・読み出し元が約束と違う（units={meta.get('units')} source={meta.get('source')}）")

    # 2. 値の範囲
    nan_count = int(np.isnan(hdr).sum())
    inf_count = int(np.isinf(hdr).sum())
    negative_count = int((hdr < 0.0).sum())
    result["nan_values"] = nan_count
    result["inf_values"] = inf_count
    result["negative_values"] = negative_count
    if nan_count or inf_count or negative_count:
        fails.append(f"負・NaN・Inf の値がある（負 {negative_count}・NaN {nan_count}・Inf {inf_count}）")
    finite = np.where(np.isfinite(hdr), hdr, 0.0)
    luminance = (finite * LUMA).sum(axis=-1)
    result["luminance_cd_m2"] = {
        "min": float(luminance.min()),
        "p01": float(np.percentile(luminance, 1)),
        "p50": float(np.percentile(luminance, 50)),
        "p99": float(np.percentile(luminance, 99)),
        "max": float(luminance.max()),
        "mean": float(luminance.mean()),
    }
    if not luminance.max() > 0.0:
        fails.append("輝度の最大が 0（真っ黒）")
    pre_exposure = float(meta.get("pre_exposure", 0.0))
    result["pre_exposure"] = pre_exposure
    if not pre_exposure > 0.0:
        fails.append(f"プリエクスポージャが正でない（{pre_exposure}）")
    if fails:
        return result

    # 3. PNG との照合
    display = meta.get("display", {})
    if not (display.get("neutral_linear") and display.get("grading_override")):
        fails.append("カメラの表示変換の差し替え（NeutralLinear とグレーディング）が無く、PNG を見積もれない"
                     "（View の既定の表示変換の再現は SCN-WLR-DISPLAY-TRANSFORM）")
        return result
    # 見た目の 3D LUT・色収差・レンズダートはこの見積もりに入れていない（起動画面のカメラなど。再現は SCN-WLR-DISPLAY-TRANSFORM）
    extras = [name for name, active in (
        ("見た目の 3D LUT", display.get("look_lut", False)),
        ("色収差", display.get("chromatic_aberration_pixels", 0.0) > 0.0),
        ("レンズダート", display.get("lens_dirt_intensity", 0.0) > 0.0)) if active]
    if extras:
        fails.append(f"表示変換に見積もりの外の処理（{'・'.join(extras)}）が掛かり、PNG を見積もれない"
                     "（再現は SCN-WLR-DISPLAY-TRANSFORM）")
        return result
    exposed = finite * pre_exposure
    predicted = predict_display(exposed, display)
    peak = exposed.max(axis=-1)
    dark = peak < DARK_PEAK
    bloom_estimate = np.zeros(peak.shape)
    for sigma in BLOOM_SIGMAS:
        blurred = gaussian_blur(exposed, sigma)
        bloom_estimate = np.maximum(bloom_estimate, (BLOOM_INTENSITY * np.abs(blurred - exposed)).max(axis=-1))
    low_bloom = bloom_estimate <= BLOOM_RELATIVE * peak + BLOOM_ABSOLUTE
    png_luma = (png * LUMA).sum(axis=-1)
    predicted_luma = (predicted * LUMA).sum(axis=-1)
    flat = (local_range(png_luma) <= EDGE_LEVELS) & (local_range(predicted_luma) <= EDGE_LEVELS)
    selected = dark & low_bloom & flat
    count = int(selected.sum())
    total = width * height
    difference = png - predicted
    result["png_match"] = {
        "dark_pixels": int(dark.sum()),
        "selected_pixels": count,
        "selected_ratio": count / total,
    }
    if count < MIN_SAMPLE_RATIO * total:
        fails.append(f"比べる画素が少ない（{count} 画素、{count / total:.4f}。下限 {MIN_SAMPLE_RATIO}）")
        return result
    selected_difference = difference[selected]
    worst = np.abs(selected_difference).max(axis=-1)
    within = float((worst <= WITHIN_LEVELS + 0.5).mean())
    median = float(np.median(np.abs(selected_difference)))
    result["png_match"].update({
        "within_levels": WITHIN_LEVELS,
        "within_ratio": within,
        "median_abs_levels": median,
        "mean_signed_levels_rgb": [float(v) for v in selected_difference.mean(axis=0)],
        "p99_abs_levels": float(np.percentile(worst, 99)),
    })
    deep = selected & (peak < DEEP_DARK_PEAK)
    result["png_match"]["deep_dark_pixels"] = int(deep.sum())
    if deep.any():
        result["png_match"]["deep_dark_within_ratio"] = float(
            (np.abs(difference[deep]).max(axis=-1) <= WITHIN_LEVELS + 0.5).mean())
        result["png_match"]["deep_dark_median_abs_levels"] = float(np.median(np.abs(difference[deep])))
    # ブルームの影響が大きい暗い画素（縁でない）での PNG − 見積もり。PNG にだけブルームが乗っていれば正になる
    halo = dark & ~low_bloom & flat
    if halo.any():
        result["png_match"]["bloom_halo_mean_signed_levels"] = float((difference[halo] * LUMA).sum(axis=-1).mean())
        result["png_match"]["bloom_halo_pixels"] = int(halo.sum())
    if within < WITHIN_RATIO:
        fails.append(f"±{WITHIN_LEVELS} 段に入る画素が {within:.4f}（下限 {WITHIN_RATIO}）")
    if median > MEDIAN_LEVELS:
        fails.append(f"差の絶対値の中央値が {median:.3f} 段（上限 {MEDIAN_LEVELS}）")
    result["pass"] = not fails
    return result


def views_from_metrics(capture_dir):
    path = os.path.join(capture_dir, "metrics.json")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8-sig") as f:
        metrics = json.load(f)
    views = metrics.get("views") or []
    if isinstance(views, dict):
        views = [views]
    return [v["view"] for v in views if isinstance(v, dict) and "view" in v]


def run_hdr(capture_dir, views, verify):
    failures = []
    inputs_ok, input_failures, record = check_inputs(capture_dir)
    if verify and not inputs_ok:
        failures.extend(input_failures)
    metric_views = views_from_metrics(capture_dir)
    if metric_views is None:
        failures.append("metrics.json が無い（CaptureScene.ps1 -Hdr の出力先でない）")
        metric_views = []
    targets = views if views else metric_views
    if not targets:
        failures.append("検査する視点が無い")
    for view in targets:
        if view not in metric_views:
            failures.append(f"{view}: metrics.json に無い視点")
    results = []
    for view in targets:
        result = check_hdr_view(capture_dir, view)
        results.append(result)
        lum = result.get("luminance_cd_m2", {})
        match = result.get("png_match", {})
        print(f"REFERENCE_CHECK hdr view={view} pass={int(result['pass'])} size={result.get('size')} "
              f"pre_exposure={result.get('pre_exposure')} "
              f"Y_p01={lum.get('p01', float('nan')):.4g} Y_p50={lum.get('p50', float('nan')):.4g} "
              f"Y_p99={lum.get('p99', float('nan')):.4g} Y_max={lum.get('max', float('nan')):.4g} "
              f"bad_values={result.get('negative_values', 0) + result.get('nan_values', 0) + result.get('inf_values', 0)} "
              f"selected={match.get('selected_pixels')} within={match.get('within_ratio')} "
              f"median_abs={match.get('median_abs_levels')} bias_rgb={match.get('mean_signed_levels_rgb')} "
              f"deep_dark={match.get('deep_dark_pixels')}/{match.get('deep_dark_within_ratio')} "
              f"bloom_halo={match.get('bloom_halo_mean_signed_levels')}")
        for failure in result["failures"]:
            failures.append(f"{view}: {failure}")
    report = {
        "mode": "hdr",
        "verify": bool(verify),
        "inputs_current": inputs_ok,
        "inputs_record": record,
        "criteria": {
            "dark_peak": DARK_PEAK, "deep_dark_peak": DEEP_DARK_PEAK, "bloom_intensity": BLOOM_INTENSITY, "bloom_sigmas": list(BLOOM_SIGMAS),
            "bloom_relative": BLOOM_RELATIVE, "bloom_absolute": BLOOM_ABSOLUTE, "edge_levels": EDGE_LEVELS,
            "min_sample_ratio": MIN_SAMPLE_RATIO, "within_levels": WITHIN_LEVELS, "within_ratio": WITHIN_RATIO,
            "median_levels": MEDIAN_LEVELS,
        },
        "views": results,
        "failures": failures,
    }
    with open(os.path.join(capture_dir, "hdr_check.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=hdr views={len(targets)} inputs_current={int(inputs_ok)} "
          f"report={os.path.join(capture_dir, 'hdr_check.json')}")
    return 0 if not failures else 1


def main():
    # 結果の行を保存して読むので、端末の符号に関わらず UTF-8 で書く
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="荒野の正解の絵の比較で、保存した撮影の結果を検査する")
    parser.add_argument("--capture", required=True, help="撮影の出力先（CaptureScene.ps1 の -OutDir）")
    parser.add_argument("--record-inputs", action="store_true", help="入力のハッシュを inputs.json に書く")
    parser.add_argument("--kind", default="engine", choices=sorted(INPUT_SETS), help="--record-inputs の入力の種類")
    parser.add_argument("--hdr", action="store_true", help="ブルーム前の HDR の撮影を検査する")
    parser.add_argument("--views", default="", help="検査する視点（カンマ区切り。省くと metrics.json の全視点）")
    parser.add_argument("--verify", action="store_true", help="入力のハッシュを今のファイルと照らし、古い結果を拒否する")
    args = parser.parse_args()
    capture_dir = args.capture if os.path.isabs(args.capture) else os.path.join(REPO_ROOT, args.capture)
    capture_dir = os.path.normpath(capture_dir)
    if not os.path.isdir(capture_dir):
        print(f"REFERENCE_CHECK result=fail reason=capture_dir_missing path={capture_dir}")
        return 1
    if args.record_inputs:
        return record_inputs(capture_dir, args.kind)
    if args.hdr:
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return run_hdr(capture_dir, views, args.verify)
    print("REFERENCE_CHECK result=fail reason=no_mode（--record-inputs か --hdr を指定する）")
    return 1


if __name__ == "__main__":
    sys.exit(main())
