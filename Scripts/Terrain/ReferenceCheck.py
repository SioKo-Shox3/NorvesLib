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
# 共通の照明（Scripts/Terrain/CommonLight.json。無ければ無いことを記録）。資産の組（ASSET_SET_INPUTS）が参照するメッシュ・テクスチャの
# 実体（glTF の .bin・画像を含む）は git が無視していても中身をハッシュに入れる（無ければ無いことを記録）。
# git のコミットと、入力に未コミットの変更があったかも書く
# （記録のためで、照合には中身のハッシュを使う。撮影の後にコミットしても古い結果にはならない）。
#
# --hdr の検査（視点ごと）:
#   1. 大きさ: PFM の見出し・hdr.json・PNG の幅と高さが一致する。
#   2. 値の範囲: R・G・B に負・NaN・Inf が無い。輝度の最大が正（真っ黒でない）。輝度の分位（1・50・99 %）を書く。
#   3. PNG との照合: HDR にプリエクスポージャを掛けて、エンジンの表示変換（DisplayTransform.py。撮影が記録したポストのパスの値、
#      記録の無い古い撮影は荒野のカメラの差し替えの値）を通した 8 bit の値を、同じ視点の PNG と比べる（DisplayTransform.py が
#      再現していない処理と、ブルームに乗るレンズダートが掛かる撮影は見積もれないので失敗にする）。比べる画素は次をすべて満たすものだけ:
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
#   python Scripts/Terrain/ReferenceCheck.py --display --capture <出力先> [--verify]
#       表示変換の再現（DisplayTransform.py）を、Game の --display-check（ブルームと FXAA を外す）で撮った 3 組と照らし、
#       結果を <出力先>/display_check.json に書く。--verify は組ごとの inputs.json を今のファイルと照らす。
#       <出力先>/ramp-neutral: 荒野（カメラの差し替えの NeutralLinear とグレーディングの式 1）を --display-check=ramp で撮った 1 視点
#       <出力先>/ramp-aces: 差し替えの無いカメラのシーンを --tone-map=aces20-lut --display-check=ramp で撮った 1 視点
#           どちらもトーンマップの入力が色の階段（DisplayTransform.display_ramp。明るさ 12 段 × R・G・B・C・M・Y・灰）になる。
#           区画の縁から RAMP_BORDER 画素より内側のすべての画素で、PNG と再現の差が 3 成分とも RAMP_WITHIN_LEVELS 段以内なら合格。
#       <出力先>/scene: 荒野の SCN-WLR-BASELINE と同じ 8 視点を --display-check=scene -Hdr で撮ったもの。
#           視点ごとに、HDR に表示変換を掛けた 8 bit の値と PNG の差が 3 成分とも SCENE_WITHIN_LEVELS 段以内の画素が
#           SCENE_WITHIN_RATIO 以上なら合格。
#       どの組も、撮影の記録（hdr.json の post）でブルームと FXAA が外れ、色の階段の有無と演算子が組の約束どおりであることも見る。
#
#   python Scripts/Terrain/ReferenceCheck.py --plate --capture <出力先> [--verify]
#       共通の照明（Scripts/Terrain/CommonLight.json）の灰色の板の明るさを、入力だけから求めた期待の値と照らし、
#       結果を <出力先>/plate_check.json に書く。--verify は組ごとの inputs.json を今のファイルと照らす。
#       <出力先>/sun・<出力先>/sky: Game の --wasteland-common-light --wasteland-gray-plate --common-light-case=sun|sky を
#           CaptureScene.ps1 -Hdr で撮ったもの（metrics.json の game_args と、Game のログの WASTELAND_COMMON_LIGHT・
#           WASTELAND_GRAY_PLATE の行で、その指定で撮れたことを確かめる）。
#       期待の値（エンジンが計算した照度は使わない）: 板はアルベド ρ の鏡面の無い水平の面なので、
#           sun: ρ × E⊥ × cos(天頂角) / π（E⊥ は太陽に垂直な面の照度、cos(天頂角) = sin(視点の太陽の仰角)）
#           sky: ρ × 空の放射輝度（一様な空の下の水平のランバート面の放射輝度）
#       合否: 視点ごとに、画面の中心の PLATE_WINDOW（幅・高さに対する割合）の窓の HDR の輝度の平均が期待に PLATE_TOLERANCE 以内、
#           かつ窓の中の輝度の変動係数（標準偏差 / 平均）が PLATE_UNIFORMITY 以下（窓が板の中に収まり、影・ほかの物が入らない）。
#           R・G・B ごとの比も書く（sky は空の色の比が保たれるか）。
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import DisplayTransform as DT  # noqa: E402

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

# 種類ごとの資産の組の定義。参照する資産の実体（メッシュの source_path・extra_sources、glTF の buffers・images、
# テクスチャの source_path・orm_*）を、git が無視するファイルも含めて入力に加える
ASSET_SET_INPUTS = {
    "engine": [
        "Assets/AssetSets/WastelandTerrain.json",
        "Assets/AssetSets/WastelandModels.json",
        "Assets/AssetSets/WastelandBuildingsModels.json",
    ],
}
# 入力の一覧で、資産の組から辿ったファイルをまとめる名前
ASSET_SOURCES_KEY = "asset_sources"

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

# --display の基準（上の説明を参照）
RAMP_BORDER = 2
RAMP_WITHIN_LEVELS = 1
SCENE_WITHIN_LEVELS = 1
SCENE_WITHIN_RATIO = 0.99
# 組ごとの約束: (組の名前, 色の階段か, 演算子)。scene は荒野の基準の 8 視点を全部要る
DISPLAY_SETS = (
    ("ramp-neutral", True, DT.OPERATOR_NEUTRAL_LINEAR),
    ("ramp-aces", True, DT.OPERATOR_ACES20_LUT),
    ("scene", False, DT.OPERATOR_NEUTRAL_LINEAR),
)
DISPLAY_SCENE_VIEWS = ("ref_far_01", "ref_far_02", "ref_cliff_01", "ref_cliff_02", "ref_ground_01", "ref_ground_02",
                       "outpost", "ground")

# --plate の基準（上の説明を参照）
COMMON_LIGHT_PATH = "Scripts/Terrain/CommonLight.json"
PLATE_CASES = ("sun", "sky")
PLATE_WINDOW = 0.2
PLATE_TOLERANCE = 0.03
PLATE_UNIFORMITY = 0.01
PLATE_GAME_ARGS = ("--wasteland-common-light", "--wasteland-gray-plate")


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


def normalize_rel(path):
    return os.path.normpath(path).replace("\\", "/")


def gltf_references(rel):
    """glTF が外のファイルとして参照する buffers・images（リポジトリのルートからのパス）"""
    with open(os.path.join(REPO_ROOT, rel), encoding="utf-8-sig") as f:
        document = json.load(f)
    base = os.path.dirname(rel)
    refs = []
    for entry in (document.get("buffers") or []) + (document.get("images") or []):
        uri = entry.get("uri") if isinstance(entry, dict) else None
        if isinstance(uri, str) and uri and not uri.startswith("data:"):
            refs.append(normalize_rel(os.path.join(base, uri.replace("%20", " "))))
    return refs


def list_asset_sources(asset_sets):
    """資産の組が参照する資産の実体（git の無視に関わらず）。(あるファイルの一覧, 無いファイルの一覧)"""
    referenced = set()
    for asset_set in asset_sets:
        path = os.path.join(REPO_ROOT, asset_set)
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8-sig") as f:
            definition = json.load(f)
        for model in definition.get("models") or []:
            for source in [model.get("source_path")] + list(model.get("extra_sources") or []):
                if isinstance(source, str) and source:
                    referenced.add(normalize_rel(source))
        for texture in definition.get("textures") or []:
            for key, value in texture.items():
                if isinstance(value, str) and value and (key == "source_path" or key.startswith("orm_")):
                    referenced.add(normalize_rel(value))
    for rel in sorted(referenced):
        if rel.lower().endswith(".gltf") and os.path.isfile(os.path.join(REPO_ROOT, rel)):
            referenced.update(gltf_references(rel))
    present = sorted(r for r in referenced if os.path.isfile(os.path.join(REPO_ROOT, r)))
    missing = sorted(referenced - set(present))
    return present, missing


def hash_inputs(kind):
    paths = INPUT_SETS[kind]
    tracked = list_input_files(paths)
    asset_files, asset_missing = list_asset_sources(ASSET_SET_INPUTS.get(kind, []))
    files = sorted(set(tracked) | set(asset_files))
    asset_set = set(asset_files)
    total = hashlib.sha256()
    per_path = {}
    for path in paths:
        per_path[path] = hashlib.sha256()
    asset_hash = hashlib.sha256()
    absent = [p for p in paths if not os.path.exists(os.path.join(REPO_ROOT, p))]
    for rel in files:
        digest = hashlib.sha256()
        with open(os.path.join(REPO_ROOT, rel), "rb") as f:
            for block in iter(lambda: f.read(1 << 24), b""):
                digest.update(block)
        line = f"{rel}\0{digest.hexdigest()}\n".encode("utf-8")
        total.update(line)
        for path in paths:
            if rel == path or rel.startswith(path.rstrip("/") + "/"):
                per_path[path].update(line)
        if rel in asset_set:
            asset_hash.update(line)
    for path in absent:
        total.update(f"{path}\0absent\n".encode("utf-8"))
    for rel in asset_missing:
        line = f"{rel}\0absent\n".encode("utf-8")
        total.update(line)
        asset_hash.update(line)
    path_hashes = {p: ("absent" if p in absent else per_path[p].hexdigest()) for p in paths}
    path_hashes[ASSET_SOURCES_KEY] = asset_hash.hexdigest()
    return {
        "files_sha256": total.hexdigest(),
        "file_count": len(files),
        "asset_source_count": len(asset_files),
        "asset_sources_missing": asset_missing,
        "paths": path_hashes,
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
    print(f"REFERENCE_CHECK record_inputs kind={kind} files={hashed['file_count']} "
          f"asset_sources={hashed['asset_source_count']} asset_missing={len(hashed['asset_sources_missing'])} "
          f"sha256={hashed['files_sha256']} "
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
        hdr = DT.read_pfm(pfm_path).astype(np.float64)
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

    # 3. PNG との照合（表示変換は DisplayTransform.py）
    try:
        params = DT.params_from_capture(meta)
    except DT.UnsupportedDisplay as error:
        fails.append(f"表示変換を再現できず PNG を見積もれない: {error}")
        return result
    # レンズダートはブルームに乗るので、ブルームの影響の小さい画素の見積もりにも入らない
    display = meta.get("display", {})
    bloom_on = (meta.get("post") or {}).get("bloom", True)
    if bloom_on and display.get("lens_dirt_intensity", 0.0) > 0.0:
        fails.append("レンズダートが掛かり、PNG を見積もれない")
        return result
    exposed = DT.expose(finite, pre_exposure).astype(np.float64)
    _, predicted = DT.display_transform(exposed.astype(np.float32), params)
    predicted = predicted.astype(np.float64)
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


def capture_files(set_dir, view):
    """撮影の視点の PNG と hdr.json の場所（既定の視点は default.png に撮られる）"""
    base = os.path.join(set_dir, view)
    if not os.path.isfile(base + ".png") and os.path.isfile(os.path.join(set_dir, "default.png")):
        base = os.path.join(set_dir, "default")
    return base + ".png", base + ".hdr.pfm", base + ".hdr.json"


def check_display_record(meta, expect_ramp, expect_operator):
    """撮影の記録が組の約束どおりか。失敗の理由の一覧"""
    post = meta.get("post")
    if not post or not post.get("tone_map_recorded"):
        return ["hdr.json にポストのパスの記録（post）が無い"]
    fails = []
    if post.get("bloom") or post.get("fxaa"):
        fails.append(f"ブルームか FXAA が外れていない（bloom={post.get('bloom')} fxaa={post.get('fxaa')}。--display-check で撮る）")
    if bool(post.get("display_ramp")) != expect_ramp:
        fails.append(f"色の階段の入力が約束と違う（display_ramp={post.get('display_ramp')}、約束 {expect_ramp}）")
    if int(post.get("tone_map_operator", -1)) != expect_operator:
        fails.append(f"トーンマップの演算子が約束と違う（{post.get('tone_map_operator')}、約束 {expect_operator}）")
    if not (post.get("hardware_srgb_encode") or post.get("shader_srgb_encode")):
        fails.append("提示の sRGB の符号化の記録が無い")
    return fails


def check_ramp_view(set_dir, view, expect_operator, lut):
    result = {"view": view, "pass": False, "failures": []}
    fails = result["failures"]
    png_path, _, meta_path = capture_files(set_dir, view)
    for path in (png_path, meta_path):
        if not os.path.isfile(path):
            fails.append(f"{os.path.basename(path)} が無い")
    if fails:
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    fails.extend(check_display_record(meta, True, expect_operator))
    try:
        params = DT.params_from_capture(meta)
    except DT.UnsupportedDisplay as error:
        fails.append(str(error))
    if fails:
        return result
    png = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.int32)
    height, width = png.shape[:2]
    ramp, column_map, row_map = DT.display_ramp(width, height)
    predicted, encoded = DT.display_transform(ramp, params, lut)
    # 区画の縁から RAMP_BORDER 画素より内側
    x = np.arange(width)
    y = np.arange(height)
    column_edges = (np.arange(DT.RAMP_COLUMNS + 1) * width + DT.RAMP_COLUMNS - 1) // DT.RAMP_COLUMNS
    row_edges = (np.arange(DT.RAMP_ROWS + 1) * height + DT.RAMP_ROWS - 1) // DT.RAMP_ROWS
    inside_x = np.min(np.abs(x[:, None] - column_edges[None, :]), axis=1) >= RAMP_BORDER
    inside_y = np.min(np.abs(y[:, None] - row_edges[None, :]), axis=1) >= RAMP_BORDER
    inside = inside_y[:, None] & inside_x[None, :]
    difference = png - predicted.astype(np.int32)
    worst = np.abs(difference).max(axis=-1)
    selected = worst[inside]
    within = float((selected <= RAMP_WITHIN_LEVELS).mean())
    result.update({
        "size": f"{width}x{height}",
        "operator": params["operator"],
        "grading_mode": params["grading_mode"],
        "compared_pixels": int(inside.sum()),
        "within_levels": RAMP_WITHIN_LEVELS,
        "within_ratio": within,
        "exact_ratio": float((selected == 0).mean()),
        "max_abs_levels": int(selected.max()),
        "mean_signed_levels_rgb": [float(v) for v in difference[inside].mean(axis=0)],
    })
    # 区画ごと（行 × 列）の中央の画素の PNG・再現の値と、区画の中の最大の差
    patches = []
    for row in range(DT.RAMP_ROWS):
        for column in range(DT.RAMP_COLUMNS):
            mask = inside & (row_map == row) & (column_map == column)
            ys, xs = np.nonzero(mask)
            if len(ys) == 0:
                continue
            cy, cx = ys[len(ys) // 2], xs[len(xs) // 2]
            patches.append({
                "row": DT.RAMP_ROW_NAMES[row],
                "exposed": float(2.0 ** (column - DT.RAMP_EXPONENT_OFFSET)),
                "png_center": [int(v) for v in png[cy, cx]],
                "predicted_center": [int(v) for v in predicted[cy, cx]],
                "max_abs_levels": int(worst[mask].max()),
            })
    result["patches"] = patches
    if within < 1.0:
        bad = [p for p in patches if p["max_abs_levels"] > RAMP_WITHIN_LEVELS]
        fails.append(f"±{RAMP_WITHIN_LEVELS} 段を外れる画素がある（{within:.6f}、最大 {result['max_abs_levels']} 段、区画 "
                     + ", ".join(f"{p['row']}@{p['exposed']:g}:{p['max_abs_levels']}" for p in bad[:12]) + "）")
    result["pass"] = not fails
    return result


def check_scene_view(set_dir, view, expect_operator, lut):
    result = {"view": view, "pass": False, "failures": []}
    fails = result["failures"]
    png_path, pfm_path, meta_path = capture_files(set_dir, view)
    for path in (png_path, pfm_path, meta_path):
        if not os.path.isfile(path):
            fails.append(f"{os.path.basename(path)} が無い")
    if fails:
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    fails.extend(check_display_record(meta, False, expect_operator))
    try:
        params = DT.params_from_capture(meta)
    except DT.UnsupportedDisplay as error:
        fails.append(str(error))
    try:
        hdr = DT.read_pfm(pfm_path)
    except ValueError as error:
        fails.append(f"PFM を読めない: {error}")
        hdr = None
    pre_exposure = float(meta.get("pre_exposure", 0.0))
    if not pre_exposure > 0.0:
        fails.append(f"プリエクスポージャが正でない（{pre_exposure}）")
    if fails:
        return result
    if not np.isfinite(hdr).all() or (hdr < 0.0).any():
        fails.append("HDR に負・NaN・Inf がある")
        return result
    png = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.int32)
    if png.shape[:2] != hdr.shape[:2]:
        fails.append(f"PNG の大きさ {png.shape[1]}x{png.shape[0]} が HDR {hdr.shape[1]}x{hdr.shape[0]} と違う")
        return result
    predicted, _ = DT.display_transform(DT.expose(hdr, pre_exposure), params, lut)
    difference = png - predicted.astype(np.int32)
    worst = np.abs(difference).max(axis=-1)
    within = float((worst <= SCENE_WITHIN_LEVELS).mean())
    result.update({
        "size": f"{png.shape[1]}x{png.shape[0]}",
        "pre_exposure": pre_exposure,
        "operator": params["operator"],
        "grading_mode": params["grading_mode"],
        "within_levels": SCENE_WITHIN_LEVELS,
        "within_ratio": within,
        "exact_ratio": float((worst == 0).mean()),
        "over_2_levels_ratio": float((worst > 2).mean()),
        "p999_abs_levels": float(np.percentile(worst, 99.9)),
        "max_abs_levels": int(worst.max()),
        "mean_signed_levels_rgb": [float(v) for v in difference.reshape(-1, 3).mean(axis=0)],
    })
    if within < SCENE_WITHIN_RATIO:
        fails.append(f"±{SCENE_WITHIN_LEVELS} 段に入る画素が {within:.5f}（下限 {SCENE_WITHIN_RATIO}）")
    result["pass"] = not fails
    return result


def run_display(capture_dir, verify):
    failures = []
    lut = DT.load_aces_lut()
    sets = []
    for name, expect_ramp, expect_operator in DISPLAY_SETS:
        set_dir = os.path.join(capture_dir, name)
        entry = {"set": name, "display_ramp": expect_ramp, "operator": expect_operator, "views": []}
        sets.append(entry)
        if not os.path.isdir(set_dir):
            failures.append(f"{name}: 撮影の組が無い（{set_dir}）")
            continue
        inputs_ok, input_failures, record = check_inputs(set_dir)
        entry["inputs_current"] = inputs_ok
        entry["inputs_commit"] = (record or {}).get("git_commit")
        if verify and not inputs_ok:
            failures.extend(f"{name}: {failure}" for failure in input_failures)
        metric_views = views_from_metrics(set_dir)
        if metric_views is None:
            failures.append(f"{name}: metrics.json が無い（CaptureScene.ps1 -Hdr の出力先でない）")
            continue
        if expect_ramp:
            targets = metric_views[:1]
        else:
            targets = list(DISPLAY_SCENE_VIEWS)
            missing = [v for v in targets if v not in metric_views]
            if missing:
                failures.append(f"{name}: 基準の視点が撮られていない（{', '.join(missing)}）")
                targets = [v for v in targets if v in metric_views]
        if not targets:
            failures.append(f"{name}: 検査する視点が無い")
        for view in targets:
            if expect_ramp:
                result = check_ramp_view(set_dir, view, expect_operator, lut)
                print(f"REFERENCE_CHECK display set={name} view={view} pass={int(result['pass'])} "
                      f"operator={result.get('operator')} compared={result.get('compared_pixels')} "
                      f"within{RAMP_WITHIN_LEVELS}={result.get('within_ratio')} exact={result.get('exact_ratio')} "
                      f"max_abs={result.get('max_abs_levels')} bias_rgb={result.get('mean_signed_levels_rgb')}")
            else:
                result = check_scene_view(set_dir, view, expect_operator, lut)
                print(f"REFERENCE_CHECK display set={name} view={view} pass={int(result['pass'])} "
                      f"pre_exposure={result.get('pre_exposure')} within{SCENE_WITHIN_LEVELS}={result.get('within_ratio')} "
                      f"exact={result.get('exact_ratio')} over2={result.get('over_2_levels_ratio')} "
                      f"p99.9={result.get('p999_abs_levels')} max_abs={result.get('max_abs_levels')} "
                      f"bias_rgb={result.get('mean_signed_levels_rgb')}")
            entry["views"].append(result)
            failures.extend(f"{name}/{view}: {failure}" for failure in result["failures"])
    report = {
        "mode": "display",
        "verify": bool(verify),
        "criteria": {
            "ramp_border": RAMP_BORDER, "ramp_within_levels": RAMP_WITHIN_LEVELS,
            "scene_within_levels": SCENE_WITHIN_LEVELS, "scene_within_ratio": SCENE_WITHIN_RATIO,
            "lut_weight_steps": DT.LUT_WEIGHT_STEPS,
        },
        "sets": sets,
        "failures": failures,
    }
    report_path = os.path.join(capture_dir, "display_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=display sets={len(DISPLAY_SETS)} report={report_path}")
    return 0 if not failures else 1


def load_common_light():
    with open(os.path.join(REPO_ROOT, COMMON_LIGHT_PATH), encoding="utf-8-sig") as f:
        return json.load(f)


def plate_sun_altitude(light, view):
    for item in light.get("views", []):
        if item.get("name") == view:
            return float(item["altitude_degrees"])
    return float(light["default_sun"]["altitude_degrees"])


def plate_expected_rgb(light, case, view):
    """入力（CommonLight.json）だけから求めた板の放射輝度の期待（cd/m²、RGB）と、その元の値"""
    albedo = float(light["gray_plate"]["albedo"])
    if case == "sun":
        altitude = plate_sun_altitude(light, view)
        irradiance = float(light["sun_illuminance_normal_lux"]) * np.sin(np.radians(altitude))
        return np.full(3, albedo * irradiance / np.pi), {"sun_altitude_degrees": altitude,
                                                          "horizontal_illuminance_lux": float(irradiance)}
    sky = np.asarray(light["sky_radiance_rgb"], dtype=np.float64)
    return albedo * sky, {"sky_radiance_rgb": sky.tolist()}


def plate_capture_args(case_dir):
    path = os.path.join(case_dir, "metrics.json")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8-sig") as f:
        metrics = json.load(f)
    args = metrics.get("game_args") or []
    return [args] if isinstance(args, str) else list(args)


def check_plate_view(case_dir, case, view, light):
    result = {"view": view, "pass": False, "failures": []}
    fails = result["failures"]
    pfm_path = os.path.join(case_dir, f"{view}.hdr.pfm")
    meta_path = os.path.join(case_dir, f"{view}.hdr.json")
    log_path = os.path.join(case_dir, f"{view}.Game.log")
    for path in (pfm_path, meta_path):
        if not os.path.isfile(path):
            fails.append(f"{os.path.basename(path)} が無い")
    if fails:
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    if meta.get("units") != "cd/m2" or meta.get("source") != "scene_color_before_bloom":
        fails.append(f"hdr.json の単位・読み出し元が約束と違う（units={meta.get('units')} source={meta.get('source')}）")
    # Game がこの視点で共通の照明と板を実際に置いたこと（開発ビルドでない Game は起動引数を無視する）
    if os.path.isfile(log_path):
        with open(log_path, encoding="utf-8", errors="replace") as f:
            log_text = f.read()
        light_line = f"WASTELAND_COMMON_LIGHT view={view} case={case}"
        if light_line not in log_text:
            fails.append(f"Game のログに「{light_line}」が無い（共通の照明が掛かっていない）")
        if "WASTELAND_GRAY_PLATE " not in log_text:
            fails.append("Game のログに WASTELAND_GRAY_PLATE が無い（板が置かれていない）")
    else:
        fails.append(f"{os.path.basename(log_path)} が無い（共通の照明が掛かったかを確かめられない）")
    try:
        hdr = DT.read_pfm(pfm_path).astype(np.float64)
    except ValueError as error:
        fails.append(f"PFM を読めない: {error}")
        return result
    height, width = hdr.shape[:2]
    half_w = max(1, int(round(width * PLATE_WINDOW / 2.0)))
    half_h = max(1, int(round(height * PLATE_WINDOW / 2.0)))
    cy, cx = height // 2, width // 2
    window = hdr[cy - half_h:cy + half_h, cx - half_w:cx + half_w]
    if not np.isfinite(window).all() or (window < 0.0).any():
        fails.append("窓の中に負・NaN・Inf の値がある")
        return result
    luminance = (window * LUMA).sum(axis=-1)
    mean_rgb = window.reshape(-1, 3).mean(axis=0)
    mean_y = float(luminance.mean())
    cv = float(luminance.std() / mean_y) if mean_y > 0.0 else float("inf")
    expected_rgb, inputs = plate_expected_rgb(light, case, view)
    expected_y = float((expected_rgb * LUMA).sum())
    ratio = mean_y / expected_y if expected_y > 0.0 else float("inf")
    result.update({
        "window_pixels": f"{2 * half_w}x{2 * half_h}",
        "inputs": inputs,
        "albedo": float(light["gray_plate"]["albedo"]),
        "expected_luminance_cd_m2": expected_y,
        "measured_luminance_cd_m2": mean_y,
        "ratio": ratio,
        "expected_rgb_cd_m2": expected_rgb.tolist(),
        "measured_rgb_cd_m2": mean_rgb.tolist(),
        "ratio_rgb": (mean_rgb / np.maximum(expected_rgb, 1e-30)).tolist(),
        "luminance_cv": cv,
        "luminance_min_max": [float(luminance.min()), float(luminance.max())],
        "pre_exposure": float(meta.get("pre_exposure", 0.0)),
    })
    if not abs(ratio - 1.0) <= PLATE_TOLERANCE:
        fails.append(f"板の輝度 {mean_y:.3f} cd/m² が期待 {expected_y:.3f} cd/m² と {100.0 * (ratio - 1.0):+.2f} % 違う"
                     f"（許容 ±{100.0 * PLATE_TOLERANCE:.0f} %）")
    if not cv <= PLATE_UNIFORMITY:
        fails.append(f"窓の中の輝度の変動係数 {cv:.4f} が {PLATE_UNIFORMITY} を超える（窓が板からはみ出すか、影・模様がある）")
    result["pass"] = not fails
    return result


def run_plate(capture_dir, verify):
    failures = []
    light = load_common_light()
    cases = []
    for case in PLATE_CASES:
        case_dir = os.path.join(capture_dir, case)
        entry = {"case": case, "views": []}
        cases.append(entry)
        if not os.path.isdir(case_dir):
            failures.append(f"{case}: 撮影の組が無い（{case_dir}）")
            continue
        inputs_ok, input_failures, record = check_inputs(case_dir)
        entry["inputs_current"] = inputs_ok
        entry["inputs_commit"] = (record or {}).get("git_commit")
        if verify and not inputs_ok:
            failures.extend(f"{case}: {failure}" for failure in input_failures)
        game_args = plate_capture_args(case_dir)
        entry["game_args"] = game_args
        if game_args is None:
            failures.append(f"{case}: metrics.json が無い（CaptureScene.ps1 -Hdr の出力先でない）")
            continue
        wanted = list(PLATE_GAME_ARGS) + [f"--common-light-case={case}"]
        missing = [a for a in wanted if a not in game_args]
        if missing:
            failures.append(f"{case}: 撮影の起動引数に {', '.join(missing)} が無い（game_args={game_args}）")
        views = views_from_metrics(case_dir) or []
        if not views:
            failures.append(f"{case}: 検査する視点が無い")
        for view in views:
            result = check_plate_view(case_dir, case, view, light)
            print(f"REFERENCE_CHECK plate case={case} view={view} pass={int(result['pass'])} "
                  f"expected={result.get('expected_luminance_cd_m2')} measured={result.get('measured_luminance_cd_m2')} "
                  f"ratio={result.get('ratio')} ratio_rgb={result.get('ratio_rgb')} cv={result.get('luminance_cv')} "
                  f"window={result.get('window_pixels')} inputs_current={int(bool(inputs_ok))}")
            entry["views"].append(result)
            failures.extend(f"{case}/{view}: {failure}" for failure in result["failures"])
    report = {
        "mode": "plate",
        "verify": bool(verify),
        "common_light": COMMON_LIGHT_PATH,
        "criteria": {"window": PLATE_WINDOW, "tolerance": PLATE_TOLERANCE, "uniformity_cv": PLATE_UNIFORMITY},
        "cases": cases,
        "failures": failures,
    }
    report_path = os.path.join(capture_dir, "plate_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=plate cases={len(PLATE_CASES)} report={report_path}")
    return 0 if not failures else 1


def main():
    # 結果の行を保存して読むので、端末の符号に関わらず UTF-8 で書く
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="荒野の正解の絵の比較で、保存した撮影の結果を検査する")
    parser.add_argument("--capture", required=True, help="撮影の出力先（CaptureScene.ps1 の -OutDir）")
    parser.add_argument("--record-inputs", action="store_true", help="入力のハッシュを inputs.json に書く")
    parser.add_argument("--kind", default="engine", choices=sorted(INPUT_SETS), help="--record-inputs の入力の種類")
    parser.add_argument("--hdr", action="store_true", help="ブルーム前の HDR の撮影を検査する")
    parser.add_argument("--display", action="store_true", help="表示変換の再現（DisplayTransform.py）を照合の撮影と照らす")
    parser.add_argument("--plate", action="store_true", help="共通の照明の灰色の板の明るさを、入力から求めた期待と照らす")
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
    if args.display:
        return run_display(capture_dir, args.verify)
    if args.plate:
        return run_plate(capture_dir, args.verify)
    print("REFERENCE_CHECK result=fail reason=no_mode（--record-inputs か --hdr か --display か --plate を指定する）")
    return 1


if __name__ == "__main__":
    sys.exit(main())
