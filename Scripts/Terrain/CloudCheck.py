# 雲の比較（SCN-WLR-CLOUD-REF-BLENDER）の照合。ReferenceCheck.py --cloud の本体。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --cloud --engine <雲の書き出しの撮影> --reference <Cycles の書き先>
#          [--views cloud_sky] [--trial] [--verify] [--expect Scripts/Terrain/ExpectSpecs/cloud-ref.json]
#   python Scripts/Terrain/ReferenceCheck.py --cloud --selftest [--expect <仕様>]
#       <雲の書き出しの撮影>: CaptureScene.ps1 -Hdr -Views cloud_sky -GameArgs cloud-dump の撮影。<Cycles の書き先>:
#       BlenderReference.py --cloud が書いたフォルダ。結果を <Cycles の書き先>/cloud_check.json と、見比べの画像
#       <Cycles の書き先>/cloud_<視点>.png（左 エンジンの HDR、右 Cycles の雲の散乱光 + 透過率の画 × 空だけの画。同じ露出。視線の画素に
#       領域の色の枠）に書く。比べる値は画面の決めた画素の視線（書き出しの視線）ごとの値で、エンジンは GPU が描画と同じ式で評価した
#       霞の前の散乱光と透過率、Cycles は視線の画素の窓（CloudVolume.WINDOW_RADIUS_PX）の平均。
#
# 比較が成り立つ条件（満たさなければ落ちる。エンジンと Cycles の値の一致は求めず、表に書くだけ）:
#   1. エンジンの書き出し: ReferenceCheck.py --cloud-dump の検査（CloudDumpCheck.check_view。格子・定数・視線の透過率）に合格する。
#   2. Cycles の参照: 記録の種類、書き出しのファイルのハッシュが今の撮影と同じ。カメラ（書き出しの逆の視線行列から求め直した
#      向き・縦の画角）・格子・視線の領域が記録と同じ。対称な透視の投影で書き出しの視線を PROJECTION_TOLERANCE_PX 画素以内で
#      表せる（Blender のカメラが同じ視線を描く）。出力の画の大きさが撮影と同じ。
#   3. 空: 雲を隠して描いた空だけの画の、視線の画素の値が、書き出しの空の LUT を CloudDumpCheck.sample_sky で引いた値と相対
#      SKY_TOLERANCE 以内（同じ空・同じ向き）。
#   4. 体積: 視線ごとに、窓の Cycles の透過率の画（同じ格子の吸収だけの体積を光線の歩で積んだ 1 − α）が、書き出しの式を 4 m ごとに
#      積んだ透過率（窓の画素の視線の平均）と TRANSMITTANCE_TOLERANCE 以内（同じ密度場・同じ置き方・同じ消散係数）。本描画の
#      1 − α は散乱した標本で透明が切れるので粒が粗く、記録だけにする。
#   5. Cycles の収束: 1 本あたり MIN_SPP_PER_SEED サンプル以上で、領域ごとに乱数の種を変えた 2 本の差が GrayRegions の基準
#      （平均の差・画素の差の RMS）に収まる。--trial はこの条件だけを外す。
#   6. 領域（CloudVolume.REGIONS）にそれぞれ CloudVolume.MIN_REGION_RAYS 本以上の視線があり、表の値と量が有限。未判定の領域・量が
#      1 つでもあれば落ちる。
#   --verify は、エンジンの撮影の入力（inputs.json）と、Cycles の参照の入力（台本・CloudVolume.py・CloudDumpCheck.py・
#   GrayRegions.py。inputs.json）を今のファイルと照らす。
#
# 表（視線ごと）: エンジンの雲の透過率（GPU）・Cycles の透過率・式を積んだ透過率、エンジンの散乱光と Cycles の放射輝度の輝度 Y と
#   比（エンジン ÷ Cycles）、太陽との角・太陽の透過率・領域。
# 表（領域ごと）: 両方の輝度 Y の平均・比・不透明度の平均・Y / 不透明度の平均。
# 量（描画ごと）: 縁の明るさ = 縁の Y / 不透明度 ÷ 厚い雲の Y / 不透明度、銀の縁 = 銀の縁の Y / 不透明度 ÷ 縁の Y / 不透明度、
#   影の側の暗さ = 影の側の Y ÷ 日の当たる側の Y（それぞれ領域の平均の比）。
#
# 比の悪化の合否（--expect <仕様>。SCN-WL-CLOUD-CHECK-EXPECT）: 仕様の JSON は {"description": ..., "rows": [<行>, ...]}。行の鍵
#   （ほかの鍵は書き間違いとして拒む）: view（視点）、region（CloudVolume.REGIONS の領域）か quantity（QUANTITIES の量）のどちらか
#   1 つ、base_ratio（基準の組のエンジン ÷ Cycles。量は エンジンの量 ÷ Cycles の量）、tolerance（e の許容）、note（説明）。
#   誤差は e = |ln(比)|（1.48→1.60 も 0.72→0.60 も e が増える）で、e > |ln(base_ratio)| ＋ tolerance なら落ちる。
#   未知の領域・量、基準の欠け（判定した視点の領域・量に行が無い、base_ratio が正の有限でない）、収束していない領域（量は
#   使う 2 領域）、値が求まらない行、重なった行、結果の無い視点の行も落ちる。--trial でも収束は求める。
#   --selftest（run_selftest）は合成の結果でこの判定を確かめる。
import json
import math
import os
import tempfile

import numpy as np
from PIL import Image, ImageDraw

import CloudDumpCheck as CD
import CloudVolume as CV
import GrayRegions as GR

CLOUD_VIEWS = ("cloud_sky",)
REFERENCE_KIND = "cycles_cloud"
# 1 本あたりのサンプル数の下限（BlenderReference.MIN_SPP_PER_SEED と同じ。--gray の収束の基準）
MIN_SPP_PER_SEED = 512
# 空だけの画と LUT の相対差の上限（同じ texel を同じ線形の標本で引くので、差は画像の標本の精度だけ）
SKY_TOLERANCE = 0.01
# 透過率の差の上限（透過率の画は吸収だけの体積を光線の歩で積むので、標本の揺らぎはほとんど無い）: 16 m の格子で書き出しの視線の
# 透過率が式から最大 0.004 ずれる（CloudVolume の説明）。密度・消散係数が 5 % 違うと光学的深さ 1 前後の視線で 0.02 近く違う
TRANSMITTANCE_TOLERANCE = 0.01
# 対称な透視の投影の視線と書き出しの視線の差の上限[画素]（逆の視線行列の単精度の丸めで 0.2 画素ほどずれる）
PROJECTION_TOLERANCE_PX = 0.5
# 記録のカメラ・格子と求め直した値の差の上限（同じ計算なので丸めだけ）
RECORD_TOLERANCE = 1e-9
# 見比べの画像の縮小と、領域の枠の色
PREVIEW_SCALE = 0.5
REGION_COLORS = {"cloud_body": (255, 255, 255), "cloud_edge": (80, 200, 255), "silver_lining": (255, 210, 0),
                 "body_sunlit": (0, 220, 120), "body_shadow": (255, 60, 60)}
# 量 → (分子の領域, 分母の領域, 領域の表の値の鍵の後半)
QUANTITIES = {
    "edge_brightness": ("cloud_edge", "cloud_body", "y_per_opacity"),
    "silver_lining": ("silver_lining", "cloud_edge", "y_per_opacity"),
    "shadow_darkness": ("body_shadow", "body_sunlit", "y"),
}
# --cloud --selftest が仕様を省いたときに読む仕様（リポジトリの根からの道）
DEFAULT_EXPECT = "Scripts/Terrain/ExpectSpecs/cloud-ref.json"
EXPECT_ROW_KEYS = ("view", "region", "quantity", "base_ratio", "tolerance", "note")


def read_json(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def check_reference_inputs(RC, reference_dir, engine_dir, verify):
    """Cycles の参照の入力の記録を読み、書き出しのハッシュを照らす（verify なら台本も）。(記録, 失敗の一覧)"""
    path = os.path.join(reference_dir, "inputs.json")
    if not os.path.isfile(path):
        return None, [f"{RC.rel_dir(reference_dir)}/inputs.json が無い（BlenderReference.py --cloud を回していない）"]
    record = read_json(path)
    failures = []
    if record.get("kind") != REFERENCE_KIND:
        failures.append(f"参照の記録の種類が {REFERENCE_KIND} でない（{record.get('kind')}）")
    for name, digest in (record.get("dump_sha256") or {}).items():
        current = os.path.join(engine_dir, name)
        if not os.path.isfile(current) or RC.sha256_file(current) != digest:
            failures.append(f"参照が読んだ書き出し {name} が今の撮影 {RC.rel_dir(engine_dir)} に無いか、違う")
    if not record.get("dump_sha256"):
        failures.append("参照の記録に書き出しのハッシュが無い")
    if not verify:
        return record, failures
    script = os.path.join(RC.REPO_ROOT, record.get("script", ""))
    if not os.path.isfile(script) or not RC.blender_script_matches(RC.sha256_file(script), record.get("script_sha256")):
        failures.append(f"Blender の台本 {record.get('script')} が描いた後に変わった（古い結果）")
    for key, rel in (("cloud_volume_sha256", "Scripts/Terrain/CloudVolume.py"),
                     ("cloud_dump_check_sha256", "Scripts/Terrain/CloudDumpCheck.py"),
                     ("gray_regions_sha256", "Scripts/Terrain/GrayRegions.py")):
        if RC.sha256_file(os.path.join(RC.REPO_ROOT, rel)) != record.get(key):
            failures.append(f"{rel} が描いた後に変わった（古い結果）")
    return record, failures


def max_difference(a, b):
    """入れ子の数の並びの差の絶対値の最大（形が違えば無限大）"""
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    if a.shape != b.shape:
        return float("inf")
    return float(np.max(np.abs(a - b))) if a.size else 0.0


def camera_grid_mismatch(frame, grid, recorded_frame, recorded_grid):
    """求め直したカメラと格子が記録と違う理由の一覧"""
    reasons = []
    for key in ("forward", "right", "up", "vertical_fov_degrees", "position_frame"):
        if max_difference(frame[key], recorded_frame.get(key, [])) > RECORD_TOLERANCE:
            reasons.append(f"カメラの {key} が記録と違う")
    for key in ("voxel_size_m", "origin_frame", "dims"):
        if max_difference(grid[key], recorded_grid.get(key, [])) > RECORD_TOLERANCE:
            reasons.append(f"格子の {key} が記録と違う")
    for axis in ("i", "j", "k"):
        if max_difference(grid["axes"][axis], (recorded_grid.get("axes") or {}).get(axis, [])) > RECORD_TOLERANCE:
            reasons.append(f"格子の軸 {axis} が記録と違う")
    return reasons


def window_mean(image, pixel):
    height, width = image.shape[:2]
    window = image[CV.window_pixels(pixel, width, height)]
    return window.reshape(-1, *image.shape[2:]).astype(np.float64).mean(axis=0)


def region_mean(rays, indices, key):
    values = [rays[i][key] for i in indices if rays[i].get(key) is not None]
    return float(np.mean(values)) if values else None


def finite(*values):
    return all(v is not None and math.isfinite(v) for v in values)


def ratio(a, b):
    return a / b if finite(a, b) and b != 0.0 else None


def srgb(linear):
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1.0 / 2.4) - 0.055)


def write_preview(path, engine_hdr, cycles_composite, rays, members):
    """左 エンジン、右 Cycles（同じ露出。中間の灰色をエンジンの輝度の中央値に合わせ、x / (1 + x) で丸める）"""
    exposure = 0.18 / max(float(np.median(GR.luminance(engine_hdr))), 1e-6)
    panels = []
    for image in (engine_hdr, cycles_composite):
        scaled = np.asarray(image, dtype=np.float64) * exposure
        panels.append((srgb(scaled / (1.0 + scaled)) * 255.0 + 0.5).astype(np.uint8))
    height, width = engine_hdr.shape[:2]
    canvas = Image.new("RGB", (width * 2, height))
    canvas.paste(Image.fromarray(panels[0]), (0, 0))
    canvas.paste(Image.fromarray(panels[1]), (width, 0))
    draw = ImageDraw.Draw(canvas)
    region_of = {}
    # 細かい領域（影の側・日の当たる側・銀の縁）の色を厚い雲・縁より優先する
    for name in reversed(CV.REGIONS):
        for index in members.get(name, []):
            region_of.setdefault(index, name)
    for ray in rays:
        name = region_of.get(ray["index"])
        color = REGION_COLORS.get(name, (128, 128, 128))
        x, y = ray["pixel"]
        for offset in (0, width):
            draw.rectangle([x + offset - 8, y - 8, x + offset + 8, y + 8], outline=color, width=2)
    canvas = canvas.resize((int(width * 2 * PREVIEW_SCALE), int(height * PREVIEW_SCALE)), Image.LANCZOS)
    canvas.save(path)


def check_view(RC, engine_dir, reference_dir, view, record, game_args, trial):
    failures = []
    result = {"view": view}
    # 1. エンジンの書き出し
    dump = CD.check_view(RC, engine_dir, view, game_args)
    result["dump_check"] = {"pass": dump["pass"], "worst": dump.get("worst"), "failures": dump["failures"]}
    if not dump["pass"]:
        failures.append("雲の書き出しの検査（--cloud-dump）が落ちた: " + "; ".join(dump["failures"][:3]))
    try:
        meta, model, probe = CV.load_dump(engine_dir, view)
    except ValueError as error:
        return {"view": view, "pass": False, "failures": failures + [f"雲の書き出しを読めない（{error}）"]}
    target = CV.target_size(meta)
    width, height = target
    pixels = meta["probe"]["pixels"]

    # 2. カメラ・格子・領域が記録と同じ
    view_record = ((record or {}).get("views") or {}).get(view)
    if view_record is None:
        return {"view": view, "pass": False, "failures": failures + [f"参照の記録に視点 {view} が無い"]}
    frame = CV.camera_frame(model, target)
    grid = CV.grid_spec(model, target, frame)
    failures.extend(camera_grid_mismatch(frame, grid, view_record.get("camera") or {}, view_record.get("grid") or {}))
    projection_px = frame["projection_error_rad"] / frame["pixel_rad"]
    result["projection_error_px"] = projection_px
    if not projection_px <= PROJECTION_TOLERANCE_PX:
        failures.append(f"対称な透視の投影で書き出しの視線を表せない（最大 {projection_px:.3f} 画素 / {PROJECTION_TOLERANCE_PX}）")
    rays = CV.ray_table(model, probe, pixels, target)
    members = CV.region_members(rays)
    recorded_members = {name: [int(i) for i in v] for name, v in (view_record.get("regions") or {}).items()}
    if members != recorded_members:
        failures.append("視線の領域の分け方が記録と違う")

    # Cycles の出力
    cycles_meta_path = os.path.join(reference_dir, f"{view}.cycles.json")
    images = {}
    for part in ("seed_a", "seed_b", "alpha_a", "alpha_b"):
        path = os.path.join(reference_dir, f"{view}.cycles.{part}.pfm")
        if os.path.isfile(path):
            images[part] = GR.read_pfm(path)
    for part in ("sky", "transmittance"):
        path = os.path.join(reference_dir, f"{view}.aux.{part}.pfm")
        if os.path.isfile(path):
            images[part] = GR.read_pfm(path)
    missing = [p for p in ("seed_a", "seed_b", "alpha_a", "alpha_b", "sky", "transmittance") if p not in images]
    if missing or not os.path.isfile(cycles_meta_path):
        return {"view": view, "pass": False,
                "failures": failures + [f"Cycles の出力が無い（{', '.join(missing) or f'{view}.cycles.json'}）"]}
    cycles_meta = read_json(cycles_meta_path)
    for part, image in images.items():
        if image.shape[:2] != (height, width):
            failures.append(f"Cycles の {part} の大きさ {image.shape[1]}×{image.shape[0]} が撮影 {width}×{height} と違う")
    if failures and any("大きさ" in f for f in failures):
        return {"view": view, "pass": False, "failures": failures}
    # NaN・Inf の混じった画は差を測れない（max で集めると NaN が消えて合格に見える）ので、比べる前に落とす
    nonfinite = {part: int(np.size(image) - np.count_nonzero(np.isfinite(image))) for part, image in images.items()}
    nonfinite = {part: count for part, count in nonfinite.items() if count}
    result["nonfinite_values"] = nonfinite
    if nonfinite:
        return {**result, "pass": False, "failures": failures + [
            "Cycles の出力に有限でない値がある（" + "、".join(f"{p} {c} 個" for p, c in nonfinite.items()) + "）"]}
    rgb_a, rgb_b = images["seed_a"], images["seed_b"]
    alpha_a, alpha_b = images["alpha_a"][..., 0], images["alpha_b"][..., 0]
    rgb = 0.5 * (rgb_a + rgb_b)
    alpha = 0.5 * (alpha_a + alpha_b)

    # 3〜4. 空と体積
    # 最大は np.max で取る（組み込みの max は NaN を飛ばす）。NaN は下の `not ... <=` で落ちる
    sky_errors = []
    t_errors = []
    for ray in rays:
        pixel = ray["pixel"]
        direction = np.asarray(ray["direction"])
        expected_sky = CD.sample_sky(model.sky_lut, direction)
        # 空だけの画は視線の画素そのもの（窓で均すと空の勾配が混ざる）
        cycles_sky = images["sky"][int(math.floor(pixel[1])), int(math.floor(pixel[0]))].astype(np.float64)
        sky_error = float(np.max(np.abs(cycles_sky - expected_sky) / np.maximum(np.abs(expected_sky), CD.RADIANCE_FLOOR)))
        sky_errors.append(sky_error)
        fine = float(np.mean([CV.fine_transmittance(model, model.ray_direction(center, target))
                              for center in CV.window_centers(pixel, width, height)]))
        cycles_t = float(window_mean(images["transmittance"][..., 0], pixel))
        render_t = 1.0 - float(window_mean(alpha, pixel))
        cycles_rgb = window_mean(rgb, pixel)
        engine_y = float(GR.luminance(np.asarray(ray["engine_scattered_rgb"])))
        cycles_y = float(GR.luminance(cycles_rgb))
        t_error = abs(cycles_t - fine)
        t_errors.append(t_error)
        ray.update({"sky_relative_error": sky_error, "fine_transmittance": fine, "cycles_transmittance": cycles_t,
                    "transmittance_error": t_error, "cycles_render_transmittance": render_t, "engine_y": engine_y, "cycles_y": cycles_y,
                    "cycles_rgb": cycles_rgb.tolist(), "cycles_opacity": float(np.clip(1.0 - cycles_t, 0.0, 1.0)),
                    "ratio": ratio(engine_y, cycles_y)})
        ray["engine_y_per_opacity"] = engine_y / ray["opacity"] if ray["opacity"] >= CV.CLOUD_OPACITY else None
        ray["cycles_y_per_opacity"] = cycles_y / ray["cycles_opacity"] if ray["cycles_opacity"] >= CV.CLOUD_OPACITY else None
    worst_sky = float(np.max(sky_errors)) if sky_errors else math.nan
    worst_t = float(np.max(t_errors)) if t_errors else math.nan
    result["sky_max_relative_error"] = worst_sky
    result["transmittance_max_error"] = worst_t
    if not worst_sky <= SKY_TOLERANCE:
        failures.append(f"空だけの画が書き出しの空の LUT と合わない（最大の相対差 {worst_sky:.4f} / {SKY_TOLERANCE}）")
    if not worst_t <= TRANSMITTANCE_TOLERANCE:
        failures.append(f"Cycles の雲の透過率が書き出しの式を積んだ透過率と合わない（最大の差 {worst_t:.4f} / "
                        f"{TRANSMITTANCE_TOLERANCE}）")

    # 5. 収束
    spp = int(cycles_meta.get("spp_per_seed", 0))
    stats, all_converged = CV.convergence(GR.luminance(rgb_a), GR.luminance(rgb_b), pixels, members)
    result["convergence"] = {"spp_per_seed": spp, "regions": stats, "all_converged": all_converged,
                             "stop_reason": cycles_meta.get("stop_reason"), "trial": bool(cycles_meta.get("trial"))}
    if not trial:
        if spp < MIN_SPP_PER_SEED:
            failures.append(f"Cycles のサンプル数が足りない（1 本 {spp} / {MIN_SPP_PER_SEED}）")
        unconverged = [name for name in CV.REGIONS if not stats[name]["converged"]]
        if unconverged:
            failures.append(f"Cycles が収束していない領域: {', '.join(unconverged)}")

    # 6. 領域の表と量
    table = {}
    for name in CV.REGIONS:
        indices = members[name]
        entry = {"label": CV.REGION_LABELS[name], "rays": len(indices)}
        if len(indices) < CV.MIN_REGION_RAYS:
            failures.append(f"領域 {name}（{CV.REGION_LABELS[name]}）の視線が {len(indices)} 本（下限 {CV.MIN_REGION_RAYS}）で未判定")
        else:
            entry.update({
                "engine_y": region_mean(rays, indices, "engine_y"), "cycles_y": region_mean(rays, indices, "cycles_y"),
                "engine_opacity": region_mean(rays, indices, "opacity"),
                "cycles_opacity": region_mean(rays, indices, "cycles_opacity"),
                "engine_y_per_opacity": region_mean(rays, indices, "engine_y_per_opacity"),
                "cycles_y_per_opacity": region_mean(rays, indices, "cycles_y_per_opacity"),
            })
            entry["ratio"] = ratio(entry["engine_y"], entry["cycles_y"])
            if not finite(entry["engine_y"], entry["cycles_y"], entry["engine_y_per_opacity"],
                          entry["cycles_y_per_opacity"], entry["ratio"]):
                failures.append(f"領域 {name}（{CV.REGION_LABELS[name]}）の値が求まらない（未判定）")
        table[name] = entry

    def quantity(numerator, denominator, key):
        return {side: ratio((table[numerator] or {}).get(f"{side}_{key}"), (table[denominator] or {}).get(f"{side}_{key}"))
                for side in ("engine", "cycles")}

    quantities = {name: quantity(*spec) for name, spec in QUANTITIES.items()}
    for name, values in quantities.items():
        if not finite(values["engine"], values["cycles"]):
            failures.append(f"量 {name} が求まらない（未判定）")
    result.update({"regions": table, "quantities": quantities, "rays": rays, "members": members,
                   "cycles": {"spp_per_seed": spp, "stop_reason": cycles_meta.get("stop_reason"),
                              "device": cycles_meta.get("device"), "settings": cycles_meta.get("settings"),
                              "grid_record": cycles_meta.get("grid_record"),
                              "render_seconds": cycles_meta.get("render_seconds"),
                              "load_seconds": cycles_meta.get("load_seconds")}})

    # 見比べの画像
    engine_hdr_path = os.path.join(engine_dir, f"{view}.hdr.pfm")
    if os.path.isfile(engine_hdr_path):
        engine_hdr = GR.read_pfm(engine_hdr_path)
        composite = rgb + images["transmittance"] * images["sky"]
        preview = os.path.join(reference_dir, f"cloud_{view}.png")
        write_preview(preview, engine_hdr, composite, rays, members)
        result["preview"] = RC.rel_dir(preview)
    result["pass"] = not failures
    result["failures"] = failures
    return result


def expect_rows(spec_path):
    """仕様の行を読む。(行の並び, 失敗の一覧)"""
    if not os.path.isfile(spec_path):
        return None, [f"仕様 {spec_path} が無い"]
    try:
        spec = read_json(spec_path)
    except (OSError, ValueError) as error:
        return None, [f"仕様 {spec_path} が読めない: {error}"]
    rows = spec.get("rows") if isinstance(spec, dict) else spec
    if not isinstance(rows, list) or not rows:
        return None, [f"仕様 {spec_path} に行が無い"]
    return rows, []


def positive_finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value > 0.0


def evaluate_expect(spec_path, results, quiet=False):
    """視点の結果を仕様の行で判定する。(失敗の一覧, 行ごとの判定)"""
    rows, failures = expect_rows(spec_path)
    if rows is None:
        return failures, []
    by_view = {result["view"]: result for result in results}
    judged = []
    seen = set()
    for index, row in enumerate(rows):
        label = f"行 {index + 1}"
        if not isinstance(row, dict):
            failures.append(f"{label} が JSON のオブジェクトでない")
            continue
        unknown = [key for key in row if key not in EXPECT_ROW_KEYS]
        if unknown:
            failures.append(f"{label} に知らない鍵 {unknown}")
            continue
        view = row.get("view")
        if ("region" in row) == ("quantity" in row):
            failures.append(f"{label} は region か quantity のどちらか 1 つを持つ")
            continue
        kind = "region" if "region" in row else "quantity"
        name = row[kind]
        if kind == "region" and name not in CV.REGIONS:
            failures.append(f"{label} の領域 {name} が未知")
            continue
        if kind == "quantity" and name not in QUANTITIES:
            failures.append(f"{label} の量 {name} が未知")
            continue
        where = f"view={view} {kind}={name}"
        if (view, kind, name) in seen:
            failures.append(f"{label}（{where}）が前の行と重なる")
            continue
        seen.add((view, kind, name))
        base = row.get("base_ratio")
        tolerance = row.get("tolerance")
        if not positive_finite(base):
            failures.append(f"{label}（{where}）の基準の比 base_ratio が欠けているか正の有限でない（{base}）")
            continue
        if not (positive_finite(tolerance) or tolerance == 0):
            failures.append(f"{label}（{where}）の許容 tolerance が欠けているか 0 以上の有限でない（{tolerance}）")
            continue
        result = by_view.get(view)
        if result is None:
            failures.append(f"{label}（{where}）の視点の結果が無い")
            continue
        regions = (result.get("regions") or {})
        stats = ((result.get("convergence") or {}).get("regions") or {})
        needed = [name] if kind == "region" else list(QUANTITIES[name][:2])
        unconverged = [r for r in needed if not (stats.get(r) or {}).get("converged")]
        if kind == "region":
            value = (regions.get(name) or {}).get("ratio")
        else:
            values = (result.get("quantities") or {}).get(name) or {}
            value = ratio(values.get("engine"), values.get("cycles"))
        entry = {"view": view, kind: name, "base_ratio": base, "tolerance": tolerance, "ratio": value}
        judged.append(entry)
        if unconverged:
            entry["pass"] = False
            failures.append(f"{label}（{where}）の領域 {', '.join(unconverged)} が収束していない")
            if not quiet:
                print(f"REFERENCE_CHECK cloud expect {where} unconverged={','.join(unconverged)} result=fail")
            continue
        if not positive_finite(value):
            entry["pass"] = False
            failures.append(f"{label}（{where}）の比が求まらない（{value}）")
            if not quiet:
                print(f"REFERENCE_CHECK cloud expect {where} ratio=undetermined result=fail")
            continue
        error = abs(math.log(value))
        base_error = abs(math.log(base))
        limit = base_error + tolerance
        entry.update({"error": error, "base_error": base_error, "limit": limit, "pass": error <= limit})
        if not quiet:
            print(f"REFERENCE_CHECK cloud expect {where} ratio={value:.4f} error={error:.4f} base_ratio={base} "
                  f"base_error={base_error:.4f} tolerance={tolerance} limit={limit:.4f} "
                  f"result={'pass' if entry['pass'] else 'fail'}")
        if not entry["pass"]:
            failures.append(f"{label}（{where}）の比 {value:.4f} の隔たり {error:.4f} が基準 {base} の隔たり {base_error:.4f} ＋ "
                            f"許容 {tolerance} を超えた（悪化）")
    # 判定した視点の領域・量に行が無ければ基準の欠け
    for result in results:
        for kind, names in (("region", CV.REGIONS), ("quantity", tuple(QUANTITIES))):
            for name in names:
                if (result["view"], kind, name) not in seen:
                    failures.append(f"視点 {result['view']} の {kind} {name} の基準の行が仕様に無い（基準の欠け）")
    return failures, judged


def synthetic_result(rows, view):
    """仕様の基準の比をそのまま持つ合成の結果（全領域が収束）"""
    regions = {name: {"ratio": 1.0} for name in CV.REGIONS}
    quantities = {name: {"engine": 1.0, "cycles": 1.0} for name in QUANTITIES}
    for row in rows:
        if row.get("view") != view:
            continue
        if row.get("region") in regions:
            regions[row["region"]]["ratio"] = row["base_ratio"]
        if row.get("quantity") in quantities:
            quantities[row["quantity"]] = {"engine": row["base_ratio"], "cycles": 1.0}
    stats = {name: {"converged": True} for name in CV.REGIONS}
    return {"view": view, "regions": regions, "quantities": quantities, "convergence": {"regions": stats}}


def set_ratio(result, kind, name, value):
    if kind == "region":
        result["regions"][name]["ratio"] = value
    else:
        result["quantities"][name] = {"engine": value, "cycles": 1.0}


def run_selftest(spec_path):
    """合成の結果で --expect の判定を確かめる。基準のまま・1 へ近づけた・許容の内側は通り、1 から遠ざけた・未知・欠け・未収束は落ちる"""
    rows, failures = expect_rows(spec_path)
    if rows is None:
        for failure in failures:
            print(f"REFERENCE_CHECK failure: {failure}")
        print("REFERENCE_CHECK result=fail mode=cloud_selftest cases=0")
        return 1
    views = sorted({row.get("view") for row in rows if isinstance(row, dict)})
    if len(views) != 1:
        print(f"REFERENCE_CHECK failure: 仕様の視点が 1 つでない（{views}）")
        print("REFERENCE_CHECK result=fail mode=cloud_selftest cases=0")
        return 1
    view = views[0]
    base = synthetic_result(rows, view)
    cases = []

    def case(label, expect_pass, result, case_rows=None, must_name=None):
        """1 つの場合を判定する。case_rows があれば仕様を書き換えた写しで判定する"""
        path = spec_path
        temp = None
        if case_rows is not None:
            temp = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8")
            json.dump({"rows": case_rows}, temp, ensure_ascii=False)
            temp.close()
            path = temp.name
        try:
            case_failures, _ = evaluate_expect(path, [result], quiet=True)
        finally:
            if temp is not None:
                os.remove(temp.name)
        got_pass = not case_failures
        ok = got_pass == expect_pass
        if ok and must_name and not any(must_name in f for f in case_failures):
            ok = False
        cases.append(ok)
        print(f"REFERENCE_CHECK cloud selftest case={label} expected={'pass' if expect_pass else 'fail'} "
              f"got={'pass' if got_pass else 'fail'} result={'pass' if ok else 'fail'}"
              + (f" first_failure={case_failures[0]}" if case_failures else ""))

    def copy(result):
        return json.loads(json.dumps(result))

    case("base", True, base)
    for row in rows:
        kind = "region" if "region" in row else "quantity"
        name = row[kind]
        ratio_base = row["base_ratio"]
        tolerance = row["tolerance"]
        away = 1.0 if ratio_base >= 1.0 else -1.0
        # 1 から遠ざける（許容 ＋ 0.01 だけ隔たりを増やす）
        worse = copy(base)
        set_ratio(worse, kind, name, ratio_base * math.exp(away * (tolerance + 0.01)))
        case(f"{kind}-{name}-away", False, worse, must_name=f"{kind}={name}")
        # 許容の半分だけ遠ざける（揺れの内側）
        inside = copy(base)
        set_ratio(inside, kind, name, ratio_base * math.exp(away * 0.5 * tolerance))
        case(f"{kind}-{name}-inside-tolerance", True, inside)
        # 1 へ近づける（改善）
        better = copy(base)
        set_ratio(better, kind, name, ratio_base * math.exp(-away * min(abs(math.log(ratio_base)), 0.05)))
        case(f"{kind}-{name}-toward-one", True, better)
        # 1 の反対側へ同じ隔たりより遠くへ跳ぶ（向きを変えても隔たりで測る）
        flipped = copy(base)
        set_ratio(flipped, kind, name, ratio_base ** -1 * math.exp(-away * (tolerance + 0.01)))
        case(f"{kind}-{name}-flip-away", False, flipped, must_name=f"{kind}={name}")
    # 項目の例: 縁 1.48→1.60、影の側 0.72→0.60
    for name, value in (("cloud_edge", 1.60), ("body_shadow", 0.60)):
        example = copy(base)
        set_ratio(example, "region", name, value)
        case(f"{name}-example-{value}", False, example, must_name=f"region={name}")
    # 未知の領域・量
    case("unknown-region", False, base,
         rows + [{"view": view, "region": "cloud_top", "base_ratio": 1.0, "tolerance": 0.01}], must_name="未知")
    case("unknown-quantity", False, base,
         rows + [{"view": view, "quantity": "edge_contrast", "base_ratio": 1.0, "tolerance": 0.01}], must_name="未知")
    # 基準の欠け: 領域の行が無い・量の行が無い・base_ratio が無い・0
    first_region = next(r for r in rows if "region" in r)
    first_quantity = next(r for r in rows if "quantity" in r)
    case("missing-region-row", False, base, [r for r in rows if r is not first_region], must_name="基準の欠け")
    case("missing-quantity-row", False, base, [r for r in rows if r is not first_quantity], must_name="基準の欠け")
    no_base = [dict(r) for r in rows]
    del no_base[0]["base_ratio"]
    case("missing-base-ratio", False, base, no_base, must_name="base_ratio")
    zero_base = [dict(r) for r in rows]
    zero_base[0]["base_ratio"] = 0.0
    case("zero-base-ratio", False, base, zero_base, must_name="base_ratio")
    no_tolerance = [dict(r) for r in rows]
    del no_tolerance[0]["tolerance"]
    case("missing-tolerance", False, base, no_tolerance, must_name="tolerance")
    case("unknown-key", False, base, [dict(rows[0], margin=0.1)] + rows[1:], must_name="知らない鍵")
    case("duplicate-row", False, base, rows + [dict(rows[0])], must_name="重なる")
    case("other-view", False, base, rows + [dict(first_region, view="cloud_far")], must_name="視点の結果が無い")
    # 収束していない領域（領域の行と、その領域を使う量の行が落ちる）
    unconverged = copy(base)
    unconverged["convergence"]["regions"]["body_shadow"]["converged"] = False
    case("unconverged-region", False, unconverged, must_name="region=body_shadow")
    case("unconverged-quantity", False, unconverged, must_name="quantity=shadow_darkness")
    # 値が求まらない
    undetermined = copy(base)
    undetermined["regions"]["cloud_edge"]["ratio"] = None
    case("undetermined-ratio", False, undetermined, must_name="求まらない")
    passed = all(cases)
    print(f"REFERENCE_CHECK result={'pass' if passed else 'fail'} mode=cloud_selftest cases={len(cases)} "
          f"failed={cases.count(False)} spec={spec_path}")
    return 0 if passed else 1


def run(RC, engine_dirs, reference_dir, views, verify, trial, expect=None):
    failures = []
    if len(engine_dirs) != 1:
        print("REFERENCE_CHECK result=fail reason=engine_missing（--engine に雲の書き出しの撮影を 1 つ指定する）")
        return 1
    engine_dir = engine_dirs[0]
    if not reference_dir or not os.path.isdir(reference_dir):
        print(f"REFERENCE_CHECK result=fail reason=reference_missing（--reference に BlenderReference.py --cloud の書き先を指定する）"
              f" path={reference_dir}")
        return 1
    inputs_ok, input_failures, engine_record = RC.check_inputs(engine_dir)
    if verify and not inputs_ok:
        failures.extend(f"エンジンの撮影: {f}" for f in input_failures)
    game_args = RC.plate_capture_args(engine_dir)
    metric_views = RC.views_from_metrics(engine_dir)
    if game_args is None or metric_views is None:
        failures.append("エンジンの撮影に metrics.json が無い（CaptureScene.ps1 の出力先でない）")
        metric_views = []
    record, record_failures = check_reference_inputs(RC, reference_dir, engine_dir, verify)
    failures.extend(f"Cycles の参照: {f}" for f in record_failures)
    targets = views or list(CLOUD_VIEWS)
    results = []
    for view in targets:
        if view not in metric_views:
            failures.append(f"{view}: エンジンの撮影の metrics.json に無い視点")
            continue
        result = check_view(RC, engine_dir, reference_dir, view, record, game_args, trial)
        results.append(result)
        failures.extend(f"{view}: {f}" for f in result["failures"])
        conv = result.get("convergence", {})
        print(f"REFERENCE_CHECK cloud view={view} pass={int(result['pass'])} "
              f"dump_pass={int(bool((result.get('dump_check') or {}).get('pass')))} "
              f"projection_px={result.get('projection_error_px')} sky_max_rel={result.get('sky_max_relative_error')} "
              f"transmittance_max_err={result.get('transmittance_max_error')} spp_per_seed={conv.get('spp_per_seed')} "
              f"all_converged={int(bool(conv.get('all_converged')))} trial={int(bool(trial))}")
        for name, entry in (result.get("regions") or {}).items():
            print(f"REFERENCE_CHECK cloud view={view} region={name} rays={entry['rays']} engine_y={entry.get('engine_y')} "
                  f"cycles_y={entry.get('cycles_y')} ratio={entry.get('ratio')} engine_opacity={entry.get('engine_opacity')} "
                  f"cycles_opacity={entry.get('cycles_opacity')} engine_y_per_opacity={entry.get('engine_y_per_opacity')} "
                  f"cycles_y_per_opacity={entry.get('cycles_y_per_opacity')} "
                  f"converged={int(bool(conv.get('regions', {}).get(name, {}).get('converged')))}")
        for name, values in (result.get("quantities") or {}).items():
            print(f"REFERENCE_CHECK cloud view={view} quantity={name} engine={values['engine']} cycles={values['cycles']}")
    expect_judged = None
    if expect:
        expect_failures, expect_judged = evaluate_expect(expect, results)
        failures.extend(f"仕様 {RC.rel_dir(expect)}: {f}" for f in expect_failures)
    report = {
        "mode": "cloud",
        "verify": bool(verify),
        "trial": bool(trial),
        "engine_dir": RC.rel_dir(engine_dir),
        "reference_dir": RC.rel_dir(reference_dir),
        "inputs_current": inputs_ok,
        "engine_inputs": engine_record,
        "reference_inputs": {k: (record or {}).get(k) for k in ("kind", "script_sha256", "cloud_volume_sha256",
                                                                "cloud_dump_check_sha256", "dump_sha256", "git_commit",
                                                                "trial")},
        "criteria": dict(CV.criteria(), min_spp_per_seed=MIN_SPP_PER_SEED, sky_tolerance=SKY_TOLERANCE,
                         transmittance_tolerance=TRANSMITTANCE_TOLERANCE, projection_tolerance_px=PROJECTION_TOLERANCE_PX),
        "views": results,
        "expect": {"spec": RC.rel_dir(expect), "rows": expect_judged} if expect else None,
        "failures": failures,
    }
    report_path = os.path.join(reference_dir, "cloud_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False, default=float)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=cloud views={len(targets)} trial={int(bool(trial))} "
          f"inputs_current={int(bool(inputs_ok))} report={report_path}")
    return 0 if not failures else 1
