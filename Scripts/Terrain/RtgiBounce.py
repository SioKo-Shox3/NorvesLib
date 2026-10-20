# RTGI の照り返し（光線が当たった面からの光）の色と量を Cycles の間接光と比べる（SCN-WL-RTGI-COLOR。
# ReferenceCheck.py --bounce から呼ぶ。単独では動かさない）。
#
#   python Scripts/Terrain/ReferenceCheck.py --bounce --engine <入の撮影> --engine <切の撮影> --reference <Cycles の参照>
#       [--views ref_cliff_01] [--expect Scripts/Terrain/ExpectSpecs/rtgi-color.json] [--verify]
#
# エンジン: 同じ視点を照り返しの入（既定）と切（開発ビルドの --rtgi-bounce=off。当たりの色を 0 にし、外れた光線の空の光は
#   残す）で撮り、HDR の差（入 − 切）をエンジンの照り返しとする。RTGI を切ると IBL・DDGI へ替わるので RTGI の入・切は使わない。
#   2 つの撮影は同じ Game.exe・同じ起動引数（--rtgi-bounce=off を除く）・同じカメラであること、切の撮影のログに
#   RTGI_BOUNCE off があり入の撮影に無いことを確かめる。灰色の材質（--wasteland-gray-material）の組も同じ形で比べられる。
# Cycles: <参照>/cycles（本描画）と、あれば <参照>/shadow/cycles（影の切り出し。--require-judged と同じ置き場）。領域は
#   ReferenceCheck.py --lit と同じ定義（GrayRegions。エンジンの深度と Cycles の太陽の見え方・法線）で、切り出しが名指した領域は
#   切り出しの枠の中の画素と切り出しの収束で判定する（本描画ではその領域が収束しない）。比べる値は間接光の拡散の分
#   （<視点>.cycles.indirect_diffuse.pfm。エンジンの照り返しは拡散だけ）で、無ければ間接光の全体（indirect。鏡面の間接光を
#   含む）と比べ、どちらを使ったかを記録する。
# 領域ごとに、照り返しの平均の RGB と Cycles の平均の RGB、明るさの比（indirect_ratio）、色度の差（ExpectSpec の chroma_diff と
#   同じ (r, g) の差の大きい方）を書く。--expect の行（mode bounce、成分 indirect・chroma）で合否を決める。
# 結果は <入の撮影>/bounce_check.json と、入｜切｜照り返し（入 − 切。表示のために明るさを揃える）を並べた bounce_<視点>.png。
import json
import os

import numpy as np
from PIL import Image

REPORT = "bounce_check.json"
DEFAULT_VIEWS = ("ref_cliff_01",)
BOUNCE_OFF_ARG = "--rtgi-bounce=off"
BOUNCE_OFF_LOG = "RTGI_BOUNCE off source=env"
GRAY_MATERIAL_ARG = "--wasteland-gray-material"
CROP_DIR = "shadow"
# 比べる Cycles の値（先にあるものを使う）
CYCLES_PASSES = ("indirect_diffuse", "indirect")


def read_cycles(rc, cycles_dir, view):
    """Cycles の描画の記録と画。(記録, 画の辞書, 失敗の一覧)。無ければ (None, None, [])"""
    meta_path = os.path.join(cycles_dir, f"{view}.cycles.json")
    if not os.path.isfile(meta_path):
        return None, None, []
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    failures = []
    for name, digest in (meta.get("outputs_sha256") or {}).items():
        path = os.path.join(cycles_dir, name)
        if not os.path.isfile(path) or rc.sha256_file(path) != digest:
            failures.append(f"{rc.rel_dir(cycles_dir)}/{name} が無いか、記録のハッシュと違う")
    if failures:
        return meta, None, failures
    prefix = os.path.join(cycles_dir, view)
    images = {"seed_a": rc.read_pfm_any(prefix + ".cycles.seed_a.pfm").astype(np.float64),
              "seed_b": rc.read_pfm_any(prefix + ".cycles.seed_b.pfm").astype(np.float64),
              "visibility": rc.read_pfm_any(prefix + ".aux.visibility.pfm")[..., 0],
              "n_dot_l": rc.read_pfm_any(prefix + ".aux.n_dot_l.pfm")[..., 0],
              "normal": rc.read_pfm_any(prefix + ".aux.normal.pfm"),
              "alpha": rc.read_pfm_any(prefix + ".aux.alpha.pfm")[..., 0]}
    for name in CYCLES_PASSES:
        path = prefix + f".cycles.{name}.pfm"
        if os.path.isfile(path):
            images["pass"] = rc.read_pfm_any(path).astype(np.float64)
            images["pass_name"] = name
            break
    if "pass" not in images:
        failures.append(f"{rc.rel_dir(prefix)}.cycles.indirect.pfm が無い")
    return meta, images, failures


def engine_capture(rc, engine_dir, view, verify):
    """1 つのエンジンの撮影の HDR・深度・カメラ・起動引数・ログ。(値の辞書, 失敗の一覧)"""
    failures = []
    ok, reasons, record = rc.split_engine_check(engine_dir)
    if verify and not ok:
        failures.extend(f"{rc.rel_dir(engine_dir)}: {r}" for r in reasons)
    hdr, _meta, hdr_failures = rc.lit_engine_hdr(engine_dir, view)
    failures.extend(f"{rc.rel_dir(engine_dir)}: {f}" for f in hdr_failures)
    args = rc.plate_capture_args(engine_dir)
    log_name = rc.view_game_log(engine_dir, view)
    log_text = ""
    if log_name and os.path.isfile(os.path.join(engine_dir, log_name)):
        with open(os.path.join(engine_dir, log_name), encoding="utf-8", errors="replace") as f:
            log_text = f.read()
    else:
        failures.append(f"{rc.rel_dir(engine_dir)}: {view} の Game のログが無い")
    if f"WASTELAND_COMMON_LIGHT view={view} case=both " not in log_text:
        failures.append(f"{rc.rel_dir(engine_dir)}: Game のログに WASTELAND_COMMON_LIGHT view={view} case=both が無い")
    camera = None
    aov_path = os.path.join(engine_dir, f"{view}.aov.json")
    if os.path.isfile(aov_path):
        with open(aov_path, encoding="utf-8-sig") as f:
            camera = json.load(f).get("camera")
    else:
        failures.append(f"{rc.rel_dir(engine_dir)}: {view}.aov.json が無い（-Aov で撮る）")
    depth_path = os.path.join(engine_dir, f"{view}.aov.depth.pfm")
    depth = rc.read_pfm_any(depth_path)[..., 0] if os.path.isfile(depth_path) else None
    return {"dir": engine_dir, "record": record, "inputs_current": ok, "identity": (record or {}).get("identity"),
            "hdr": hdr, "depth": depth, "camera": camera, "args": args or [], "log": log_text,
            "log_name": log_name}, failures


def bounce_preview(path, on_dir, off_dir, view, bounce, surface):
    """入｜切｜照り返し（入 − 切の輝度を、表面の 99 パーセンタイルで 1 にして sRGB で）を並べた画を書く"""
    panels = []
    for directory in (on_dir, off_dir):
        png = os.path.join(directory, f"{view}.png")
        if not os.path.isfile(png):
            return None
        panels.append(np.asarray(Image.open(png).convert("RGB"), dtype=np.float64) / 255.0)
    positive = np.maximum(bounce, 0.0)
    scale = np.percentile(positive[surface].max(axis=-1), 99) if surface.any() else 1.0
    linear = np.clip(positive / max(scale, 1e-6), 0.0, 1.0)
    encoded = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1.0 / 2.4) - 0.055)
    if encoded.shape != panels[0].shape:
        return None
    panels.append(encoded)
    Image.fromarray((np.concatenate(panels, axis=1) * 255.0 + 0.5).astype(np.uint8)).save(path)
    return os.path.basename(path)


def check_view(rc, view, on, off, reference_root, out_dir):
    GR = rc.GR
    failures = []
    result = {"view": view, "regions": {}}
    bounce = on["hdr"] - off["hdr"]
    depth = on["depth"]
    if depth is None:
        return dict(result, failures=[f"{view}: 入の撮影の深度（aov.depth.pfm）が無い"], **{"pass": False})
    sources = []
    for name, cycles_dir in (("本描画", os.path.join(reference_root, rc.GRAY_CYCLES_DIR)),
                             ("切り出し", os.path.join(reference_root, CROP_DIR, rc.GRAY_CYCLES_DIR))):
        meta, images, source_failures = read_cycles(rc, cycles_dir, view)
        failures.extend(f"{view}: {f}" for f in source_failures)
        if meta is None or images is None:
            continue
        if images["alpha"].shape != depth.shape:
            failures.append(f"{view}: {name}の Cycles の大きさがエンジンと違う")
            continue
        failures.extend(f"{view}: {name}: {f}" for f in rc.gray_camera_match(on["camera"], meta["camera_engine"]))
        crop = meta.get("crop") or {}
        inside = np.ones(depth.shape, dtype=bool)
        if crop:
            x0, y0, x1, y1 = (int(v) for v in crop["border_px"])
            inside = np.zeros(depth.shape, dtype=bool)
            inside[y0:y1 + 1, x0:x1 + 1] = True
        masks = {r: m & inside for r, m in GR.region_masks(depth, images["normal"][..., 1], images["n_dot_l"],
                                                          images["visibility"], images["alpha"] > 0.5).items()}
        stats, _ = GR.convergence(images["seed_a"] @ rc.LUMA, images["seed_b"] @ rc.LUMA, masks)
        sources.append({"name": name, "dir": cycles_dir, "meta": meta, "images": images, "masks": masks,
                        "stats": stats, "regions": list(crop.get("regions") or []) if crop else None})
    if not sources:
        return dict(result, failures=failures + [f"{view}: Cycles の描画（{rc.GRAY_CYCLES_DIR}/{view}.cycles.json）が無い"],
                    **{"pass": False})
    main_source = sources[0]
    engine_y_on = on["hdr"] @ rc.LUMA
    engine_y_off = off["hdr"] @ rc.LUMA
    for region in GR.REGIONS:
        # 切り出しが名指した領域は切り出しで、ほかは本描画で比べる
        source = next((s for s in sources if s["regions"] and region in s["regions"]), main_source)
        if source["regions"] is None and source is not main_source:
            source = main_source
        mask = source["masks"][region]
        conv = source["stats"][region]
        spp = int(source["meta"].get("spp_per_seed") or 0)
        entry = {"label": GR.REGION_LABELS[region], "pixels": int(mask.sum()), "convergence": conv,
                 "cycles_source": f"{rc.rel_dir(source['dir'])}（{source['name']}）",
                 "cycles_pass": source["images"]["pass_name"], "spp_per_seed": spp}
        if conv["converged"] and spp >= rc.LIT_MIN_SPP_PER_SEED:
            entry["status"] = "判定"
        elif conv["pixels"] < GR.MIN_REGION_PX:
            entry["status"] = "未判定（画素が足りない）"
        elif spp < rc.LIT_MIN_SPP_PER_SEED:
            entry["status"] = "未判定（サンプルが下限に届かない）"
        else:
            entry["status"] = "未判定（収束しない）"
        if mask.any():
            e_rgb = bounce[mask].mean(axis=0)
            c_rgb = source["images"]["pass"][mask].mean(axis=0)
            e_y = float(e_rgb @ rc.LUMA)
            c_y = float(c_rgb @ rc.LUMA)
            e_sum, c_sum = float(e_rgb.sum()), float(c_rgb.sum())
            entry.update({
                "engine_rgb": e_rgb.tolist(), "cycles_rgb": c_rgb.tolist(),
                "engine_bounce_y": e_y, "cycles_y": c_y,
                "indirect_ratio": e_y / c_y if c_y > 0 else None,
                "engine_chroma": [float(e_rgb[0] / e_sum), float(e_rgb[1] / e_sum)] if e_sum > 0 else None,
                "cycles_chroma": [float(c_rgb[0] / c_sum), float(c_rgb[1] / c_sum)] if c_sum > 0 else None,
                "engine_on_y": float(engine_y_on[mask].mean()), "engine_off_y": float(engine_y_off[mask].mean()),
                "bounce_share_of_on": e_y / float(engine_y_on[mask].mean()) if engine_y_on[mask].mean() > 0 else None,
                "negative_bounce_pixels": float(np.mean((bounce[mask] @ rc.LUMA) < 0.0)),
            })
            if entry["engine_chroma"] and entry["cycles_chroma"]:
                entry["chroma_diff"] = max(abs(entry["engine_chroma"][0] - entry["cycles_chroma"][0]),
                                           abs(entry["engine_chroma"][1] - entry["cycles_chroma"][1]))
        result["regions"][region] = entry
    surface = (main_source["images"]["alpha"] > 0.5) & np.isfinite(depth)
    result["preview"] = bounce_preview(os.path.join(out_dir, f"bounce_{view}.png"), on["dir"], off["dir"], view,
                                       bounce, surface)
    result["failures"] = failures
    result["pass"] = not failures
    return result


def run(rc, engine_dirs, reference_root, views, verify, expect=None):
    failures = []
    views = views or list(DEFAULT_VIEWS)
    if len(engine_dirs) != 2:
        print("REFERENCE_CHECK result=fail reason=bounce_engine（--engine に入の撮影と切の撮影の 2 つをこの順で指定する）")
        return 1
    if not reference_root:
        print("REFERENCE_CHECK result=fail reason=bounce_reference（--reference に Cycles の参照の置き場を指定する）")
        return 1
    on_dir, off_dir = engine_dirs
    report = {"mode": "bounce", "verify": bool(verify), "views": views, "engine_on": rc.rel_dir(on_dir),
              "engine_off": rc.rel_dir(off_dir), "reference": rc.rel_dir(reference_root), "view_results": []}
    gray = None
    inputs_current = True
    for view in views:
        on, on_failures = engine_capture(rc, on_dir, view, verify)
        off, off_failures = engine_capture(rc, off_dir, view, verify)
        failures.extend(on_failures + off_failures)
        inputs_current = inputs_current and on["inputs_current"] and off["inputs_current"]
        if on["identity"] != off["identity"]:
            failures.append(f"{view}: 入と切の撮影のエンジンの実装（Game.exe・シェーダー）が違う")
        if BOUNCE_OFF_ARG in on["args"] or BOUNCE_OFF_ARG not in off["args"]:
            failures.append(f"{view}: 切の撮影だけが起動引数 {BOUNCE_OFF_ARG} を持つこと")
        if [a for a in off["args"] if a != BOUNCE_OFF_ARG] != on["args"]:
            failures.append(f"{view}: 入と切の撮影の起動引数が {BOUNCE_OFF_ARG} のほかでも違う")
        if BOUNCE_OFF_LOG in on["log"] or BOUNCE_OFF_LOG not in off["log"]:
            failures.append(f"{view}: 切の撮影のログだけに {BOUNCE_OFF_LOG} があること")
        if on["camera"] != off["camera"]:
            failures.append(f"{view}: 入と切の撮影のカメラが違う")
        view_gray = GRAY_MATERIAL_ARG in on["args"]
        if gray is not None and gray != view_gray:
            failures.append("視点ごとに材質（灰色・実材質）が違う")
        gray = view_gray
        if on["hdr"] is None or off["hdr"] is None or on["hdr"].shape != off["hdr"].shape:
            failures.append(f"{view}: 入と切の HDR が揃わない")
            continue
        result = check_view(rc, view, on, off, reference_root, on_dir)
        failures.extend(result["failures"])
        report["view_results"].append(result)
    report["material"] = "gray" if gray else "lit"
    ok, reasons, reference = rc.split_reference(reference_root, "gray" if gray else "lit", verify)
    report["cycles_inputs_current"] = ok
    report["reference_signature"] = (reference or {}).get("signature_sha256")
    failures.extend(reasons)
    report["inputs_current"] = inputs_current
    for result in report["view_results"]:
        for region, entry in result["regions"].items():
            print(f"REFERENCE_CHECK bounce view={result['view']} region={region} status={entry['status']} "
                  f"pixels={entry['pixels']} pass={entry['cycles_pass']} "
                  f"engine_rgb={np.round(entry.get('engine_rgb') or [], 2).tolist()} "
                  f"cycles_rgb={np.round(entry.get('cycles_rgb') or [], 2).tolist()} "
                  f"ratio={entry.get('indirect_ratio') or float('nan'):.3f} "
                  f"chroma_engine={np.round(entry.get('engine_chroma') or [], 4).tolist()} "
                  f"chroma_cycles={np.round(entry.get('cycles_chroma') or [], 4).tolist()} "
                  f"chroma_diff={entry.get('chroma_diff', float('nan')):.4f} "
                  f"bounce_share={entry.get('bounce_share_of_on') or float('nan'):.3f} "
                  f"source={entry['cycles_source']}")
    rc.apply_expect(report, expect, "bounce", failures)
    report["failures"] = failures
    report_path = os.path.join(on_dir, REPORT)
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=bounce material={report['material']} views={len(views)} "
          f"inputs_current={int(inputs_current)} cycles_inputs_current={int(ok)} "
          f"reference={report['reference_signature']} report={report_path}")
    return 0 if not failures else 1
