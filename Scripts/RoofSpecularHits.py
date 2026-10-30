#!/usr/bin/env python3
"""屋根の鏡面光線の最後の標本を撮影し、当たりを集計する。"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parent.parent
ITEM = "SCN-RENDER-SPEC-ROOF-HITS"
DEFAULT_CAPTURE = ROOT / ".harness" / "runs" / "scene-capture" / ITEM
CAPTURE = DEFAULT_CAPTURE
BEFORE = ROOT / ".harness" / "runs" / "regression" / ITEM / "before"
LOG = ROOT / ".harness" / "unit-verify.txt"
SHADER = Path("Assets/Shaders/RTGI/DiffuseIndirect.comp")
AOV = Path("Assets/Shaders/gbuffer_aov.frag")
LIGHTING = Path("Assets/Shaders/lighting.frag")
Y = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)


def log(message: str) -> None:
    print(message, flush=True)
    LOG.parent.mkdir(parents=True, exist_ok=True)
    with LOG.open("a", encoding="utf-8") as stream:
        stream.write(message + "\n")


def run(command: list[str]) -> None:
    log("COMMAND: " + subprocess.list2cmdline(command))
    process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True,
                               encoding="utf-8", errors="replace")
    assert process.stdout is not None
    for line in process.stdout:
        log(line.rstrip("\r\n"))
    code = process.wait()
    log(f"EXIT={code}")
    if code:
        raise RuntimeError(f"コマンドが失敗しました: exit={code}")


def replace_once(source: str, before: str, after: str) -> str:
    count = source.count(before)
    if count != 1:
        raise RuntimeError(f"シェーダーの書き換え位置が一意ではありません: {count} / {before[:100]!r}")
    return source.replace(before, after, 1)


# 1 次面の三角形はカメラからの別の ray query で得る。鏡面光線は元の関数の結果を読む。
GLOBALS = """
bool gRoofRecording = false;
float gRoofDistance = 0.0;
float gRoofFront = 0.0;
float gRoofHit = 0.0;
float gRoofPrimary = 0.0;
float gRoofDot = 0.0;
float gRoofPlane = 0.0;
float gRoofDirect = 0.0;
float gRoofIndirect = 0.0;
float gRoofFlags = 0.0;
float gRoofPrimaryGap = 0.0;
vec3 gRoofPrimaryPoint = vec3(0.0);
vec3 gRoofPrimaryNormal = vec3(0.0);
"""

PRIMARY_QUERY = """
    vec3 cameraToSurface = worldPosition - parameters.cameraPosition.xyz;
    float cameraDistance = length(cameraToSurface);
    vec3 cameraDirection = cameraToSurface / cameraDistance;
    vec3 viewDirection = -cameraDirection;
    uint cameraMask = parameters.environmentParameters.w > 0.5
        ? (RayTracingInstanceMaskShadowCaster | RayTracingInstanceMaskNonShadowCaster)
        : RayTracingInstanceMaskShadowCaster;
    rayQueryEXT primaryQuery;
    rayQueryInitializeEXT(primaryQuery, sceneTopLevel, gl_RayFlagsOpaqueEXT, cameraMask,
                          parameters.cameraPosition.xyz, parameters.rayLimits.x,
                          cameraDirection, cameraDistance + 0.5);
    while (rayQueryProceedEXT(primaryQuery)) { }
    if (rayQueryGetIntersectionTypeEXT(primaryQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT)
    {
        uint primaryIndex = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(primaryQuery, true));
        uint primaryPrimitive = rayQueryGetIntersectionPrimitiveIndexEXT(primaryQuery, true);
        RTGIInstanceData primaryInstance;
        vec3 primaryNormal;
        if (TryGetInstanceData(primaryIndex, primaryInstance) &&
            TryGetWorldTriangleNormal(primaryQuery, primaryPrimitive, primaryInstance, primaryNormal))
        {
            if (dot(primaryNormal, viewDirection) < 0.0) primaryNormal = -primaryNormal;
            float primaryDistance = rayQueryGetIntersectionTEXT(primaryQuery, true);
            gRoofPrimary = primaryIndex < 2047u ? float(primaryIndex + 1u) : -1.0;
            gRoofPrimaryPoint = parameters.cameraPosition.xyz + cameraDirection * primaryDistance;
            gRoofPrimaryNormal = primaryNormal;
            gRoofPrimaryGap = primaryDistance - cameraDistance;
        }
    }
"""


def instrument(source: str, variant: str, sample_index: int | None = None) -> str:
    source = replace_once(source, "shared uint sharedHitStats[3];",
                          "shared uint sharedHitStats[3];\n" + GLOBALS)
    source = replace_once(source,
        "    if (dot(triangleNormal, rayDirection) > 0.0)\n    {\n        triangleNormal = -triangleNormal;\n    }",
        "    if (gRoofRecording)\n    {\n"
        "        gRoofDistance = hitDistance;\n"
        "        gRoofFront = rayQueryGetIntersectionFrontFaceEXT(query, true) ? 1.0 : 0.0;\n"
        "    }\n"
        "    if (dot(triangleNormal, rayDirection) > 0.0)\n    {\n        triangleNormal = -triangleNormal;\n    }")
    anchor = "            viewDistance = length(worldPosition - parameters.cameraPosition.xyz);"
    source = replace_once(source, anchor, anchor + "\n" + PRIMARY_QUERY)
    anchor = "    vec3 hitRadiance = TraceDiffuseRay("
    source = replace_once(source, anchor,
        "    gRoofFlags += 1.0;\n"
        "    gRoofRecording = true;\n" + anchor)
    anchor = "    if (hitCustomIndex == RTGI_NO_HIT)\n    {\n        // 空へ抜けた光線の空の光"
    replacement = """    gRoofRecording = false;
    if (gRoofPrimary > 0.0) gRoofDot = dot(rayDirection, gRoofPrimaryNormal);
    if (hitCustomIndex != RTGI_NO_HIT)
    {
        gRoofHit = hitCustomIndex < 2047u ? float(hitCustomIndex + 1u) : -1.0;
        if (!bHit) gRoofHit = -2.0;
    }
    if (bHit)
    {
        gRoofFlags += 2.0 * gRoofFront;
        gRoofPlane = dot(hitPosition - gRoofPrimaryPoint, gRoofPrimaryNormal);
    }
    if (hitCustomIndex == RTGI_NO_HIT)
    {
        // 空へ抜けた光線の空の光"""
    source = replace_once(source, anchor, replacement)
    anchor = """    vec3 screenIndirect;
    hitRadiance = SanitizeUnexposedRadiance(hitRadiance) +
                  (TryReadScreenIndirect(hitPosition, screenIndirect)
                       ? screenIndirect
                       : TraceSecondBounce(rayDirection, hitPosition, hitNormal, hitBaseColor, hitSpecularWeight,
                                           pixel, 16u, 25u, 1.0));
    return SanitizeUnexposedRadiance(hitRadiance * mix(1.0, wideWeight, wideLobe));"""
    replacement = """    vec3 screenIndirect;
    vec3 directRadiance = SanitizeUnexposedRadiance(hitRadiance);
    bool roofUsedScreen = TryReadScreenIndirect(hitPosition, screenIndirect);
    vec3 indirectRadiance = roofUsedScreen
        ? screenIndirect
        : TraceSecondBounce(rayDirection, hitPosition, hitNormal, hitBaseColor, hitSpecularWeight,
                            pixel, 16u, 25u, 1.0);
    if (roofUsedScreen) gRoofFlags += 4.0;
    float roofWeight = mix(1.0, wideWeight, wideLobe);
    const vec3 roofLuminance = vec3(0.2126, 0.7152, 0.0722);
    hitRadiance = directRadiance + indirectRadiance;
    vec3 roofTotal = SanitizeUnexposedRadiance(hitRadiance * roofWeight);
    vec3 roofScale = roofTotal / max(hitRadiance * roofWeight, vec3(1.0e-30));
    gRoofDirect = dot(directRadiance * roofWeight * roofScale, roofLuminance) / 16.0;
    gRoofIndirect = dot(indirectRadiance * roofWeight * roofScale, roofLuminance) / 16.0;
    if (roofUsedScreen) gRoofIndirect = -(gRoofIndirect + 0.00006103515625);
    return roofTotal;"""
    source = replace_once(source, anchor, replacement)
    encoded = {
        "geometry": "vec4(gRoofHit, gRoofPrimary, gRoofDistance, gRoofDot)",
        "radiance": "vec4(gRoofPlane, gRoofDirect, gRoofIndirect, gRoofDistance)",
        "flags": "vec4(parameters.sampleState.x < 256u ? float(parameters.sampleState.x * 8u) + gRoofFlags : -1.0, gRoofPrimaryGap, gRoofHit, gRoofDistance)",
    }[variant]
    output = "imageStore(diffuseIndirect, ivec2(pixel), " + encoded + ");"
    if sample_index is not None:
        output = (f"if (parameters.sampleState.x == {sample_index}u) {output}\n"
                  "    else imageStore(diffuseIndirect, ivec2(pixel), vec4(safeRadiance, outputAlpha));")
    return replace_once(source,
        "imageStore(diffuseIndirect, ivec2(pixel), vec4(safeRadiance, outputAlpha));",
        output)


def capture_scene(name: str, extra: list[str] | None = None) -> None:
    command = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
               "Scripts/CaptureScene.ps1", "-Scene", "rendering3dtest", "-Views", "overview",
               "-Configuration", "RelWithDebInfo", "-RequireAssets", "-Width", "1280", "-Height", "720",
               "-Hdr", "-Aov", "-OutDir", str(CAPTURE / name)]
    run(command + (extra or []))


def capture_unmodified(name: str, extra: list[str] | None = None) -> None:
    hashes = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in (SHADER, AOV)}
    try:
        capture_scene(name, extra)
    finally:
        for path, digest in hashes.items():
            if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != digest:
                raise RuntimeError(f"通常撮影中にシェーダーが変更されました: {path}")
        log(f"UNCHANGED original shader bytes after {name}: " +
            ", ".join(f"{path} sha256={digest}" for path, digest in hashes.items()))


def capture_variant(variant: str, sample_index: int | None = None) -> None:
    paths = [ROOT / SHADER, ROOT / AOV]
    original = {path: path.read_bytes() for path in paths}
    hashes = {path: hashlib.sha256(value).hexdigest() for path, value in original.items()}
    try:
        shader = original[ROOT / SHADER].decode("utf-8-sig").replace("\r\n", "\n")
        aov = original[ROOT / AOV].decode("utf-8-sig").replace("\r\n", "\n")
        shader = instrument(shader, variant, sample_index)
        aov = replace_once(aov, "outSurface = vec4(albedo.rgb, roughness);",
                           "outSurface = texelFetch(rtgiTemporal, pixel, 0);")
        for path, content in ((ROOT / SHADER, shader), (ROOT / AOV, aov)):
            prefix = b"\xef\xbb\xbf" if original[path].startswith(b"\xef\xbb\xbf") else b""
            path.write_bytes(prefix + content.encode("utf-8"))
        capture_scene("roof-diag-" + variant)
    finally:
        for path, value in original.items():
            path.write_bytes(value)
        for path in paths:
            if hashlib.sha256(path.read_bytes()).hexdigest() != hashes[path]:
                raise RuntimeError(f"シェーダーの復元に失敗しました: {path}")
        log(f"RESTORED original shader bytes after {variant}: " +
            ", ".join(f"{p.relative_to(ROOT)} sha256={hashes[p]}" for p in paths))


def capture_lighting_variant(variant: str) -> None:
    paths = [ROOT / SHADER, ROOT / LIGHTING]
    original = {path: path.read_bytes() for path in paths}
    changes = {
        "off-noao": [(LIGHTING, "return prefilteredColor * reflectance * specularAO * iblIntensity;",
                       "return prefilteredColor * reflectance * iblIntensity;")],
        "off-nossao": [(LIGHTING, "ao *= ssao;", "ao *= 1.0;")],
        "on-nohit": [(LIGHTING, "+ hitRadiance / hitNormalization);", "+ vec3(0.0));")],
        "on-nowide": [(path, "float wideLobe = EvaluateWideSpecularLobeBlend(roughness);",
                        "float wideLobe = 0.0;") for path in (LIGHTING, SHADER)],
    }
    try:
        for relative, before, after in changes[variant]:
            path = ROOT / relative
            value = path.read_bytes()
            if value.count(before.encode()) != 1:
                raise RuntimeError(f"変種の書き換え位置が一意ではありません: {relative} / {before}")
            path.write_bytes(value.replace(before.encode(), after.encode()))
        capture_scene("roof-" + variant, ["-GameArgs", "rtgi-specular=off"] if variant.startswith("off-") else [])
    finally:
        for path, value in original.items():
            path.write_bytes(value)
        for path, value in original.items():
            if path.read_bytes() != value:
                raise RuntimeError(f"シェーダーの復元に失敗しました: {path}")
        log(f"RESTORED original shader bytes after {variant}")


def pfm(path: Path) -> np.ndarray:
    with path.open("rb") as stream:
        kind = stream.readline().strip()
        width, height = map(int, stream.readline().split())
        scale = float(stream.readline())
        if kind not in (b"PF", b"Pf") or scale == 0:
            raise ValueError(f"不正な PFM: {path}")
        dtype = "<f4" if scale < 0 else ">f4"
        shape = (height, width, 3) if kind == b"PF" else (height, width)
        return (np.fromfile(stream, dtype=dtype).reshape(shape)[::-1] * abs(scale)).copy()


def aov(name: str, suffix: str) -> np.ndarray:
    return pfm(CAPTURE / name / f"overview.aov.{suffix}.pfm")


def roof_mask() -> np.ndarray:
    depth = aov("roof-on", "depth")
    packed = aov("roof-on", "geometric_normal")
    if depth.shape != (720, 1280) or packed.shape != depth.shape:
        raise ValueError("屋根選択の AOV の大きさが 1280×720 ではありません")
    v = packed.astype(np.int64) - 1
    x = (v // 4096) / 4095.0 * 2.0 - 1.0
    y = (v % 4096) / 4095.0 * 2.0 - 1.0
    normal = np.stack((x, y, 1.0 - np.abs(x) - np.abs(y)), axis=-1)
    fold = np.maximum(-normal[:, :, 2], 0.0)
    normal[:, :, :2] += np.where(normal[:, :, :2] >= 0, -fold[:, :, None], fold[:, :, None])
    normal /= np.maximum(np.linalg.norm(normal, axis=-1, keepdims=True), 1e-30)
    roi = np.zeros(depth.shape, dtype=bool)
    roi[95:150, 480:800] = True
    return roi & (normal[:, :, 1] > .9) & (normal[:, :, 2] > .4) & (depth > 21) & (depth < 25)


def packed_diagnostic(name: str) -> np.ndarray:
    rgb = aov("roof-diag-" + name, "albedo")
    alpha = aov("roof-diag-" + name, "roughness")
    return np.concatenate((rgb, alpha[:, :, None]), axis=-1)


def captured_sample_index(mask: np.ndarray) -> int:
    encoded = packed_diagnostic("flags")[mask, 0]
    if not np.isfinite(encoded).all() or np.any(encoded < 0) or np.any(encoded != np.rint(encoded)):
        raise ValueError("標本の系列番号を半精度で正確に記録できません")
    samples = np.unique(encoded.astype(np.int64) // 8)
    if len(samples) != 1 or not 0 < samples[0] < 256:
        raise ValueError(f"標本の系列番号が一意ではありません: {samples}")
    return int(samples[0])


def analyze(mapping_path: Path | None) -> dict:
    mask = roof_mask()
    count = int(mask.sum())
    if count == 0:
        raise ValueError("屋根の画素がありません")
    geometry_image = packed_diagnostic("geometry")
    geom = geometry_image[mask].astype(np.float64)
    rad, flags = [packed_diagnostic(name)[mask].astype(np.float64)
                  for name in ("radiance", "flags")]
    if not all(np.isfinite(value).all() for value in (geom, rad, flags)):
        raise ValueError("診断 AOV に非有限値があります")
    if not (np.array_equal(geom[:, 2], rad[:, 3]) and
            np.array_equal(geom[:, 2], flags[:, 3]) and
            np.array_equal(geom[:, 0], flags[:, 2])):
        raise ValueError("診断撮影の光線が一致しません（当たりの ID・距離を照合）")
    sample_index = captured_sample_index(mask)
    bits = np.rint(flags[:, 0]).astype(np.int64) % 8
    traced = (bits & 1) != 0
    hit = geom[:, 0] > 0
    if np.any(geom[:, 1] <= 0):
        raise ValueError(f"カメラ光線で三角形を取得できない屋根画素: {int(np.sum(geom[:, 1] <= 0))}")
    if np.any(hit & ~traced):
        raise ValueError("鏡面光線を引いていない画素に当たりがあります")
    if np.any(geom[:, :2] < 0) or np.any(geom[:, :2] > 2047) or np.any(geom[:, :2] != np.rint(geom[:, :2])):
        raise ValueError("instance ID が半精度で正確に表せる範囲を超えました")
    hit_id = np.rint(geom[:, 0]).astype(np.int64) - 1
    primary_id = np.rint(geom[:, 1]).astype(np.int64) - 1
    front = (bits & 2) != 0
    screen = (bits & 4) != 0
    if not np.array_equal(screen, rad[:, 2] < 0):
        raise ValueError("撮影間で画面の間接光の採用が一致しません")
    distance = geom[:, 2]
    cosine = geom[:, 3]
    plane = rad[:, 0]
    direct = rad[:, 1] * 16
    indirect = np.maximum(np.where(screen, -rad[:, 2] - 0.00006103515625, rad[:, 2]), 0) * 16
    radiance = direct + indirect
    if mapping_path:
        mapping = json.loads(mapping_path.read_text(encoding="utf-8"))
    else:
        # 既定の overview で手前の非金属の球の中央をカメラ光線で照合する。
        sphere_id = int(round(float(geometry_image[427, 406, 1]))) - 1
        sphere_roughness = float(aov("roof-on", "roughness")[427, 406])
        if sphere_id < 0 or not 0.68 < sphere_roughness < 0.72:
            raise ValueError("材質見本の球の画素を確認できません。--mapping で物体名を指定してください")
        mapping = {str(sphere_id): "材質見本の球（手前列の左から４個目）"}
        for x in (500, 550, 600, 650, 700, 750, 790):
            facade_id = int(round(float(geometry_image[200, x, 1]))) - 1
            if facade_id >= 0 and facade_id not in primary_id:
                mapping[str(facade_id)] = "家の別の部分（別の instance）"
    same = hit & (hit_id == primary_id)
    same_plane = same & (np.abs(plane) < .001)
    below = traced & (cosine <= 0)
    labels = np.full(count, "空", dtype=object)
    labels[hit] = "未対応"
    for number in np.unique(hit_id[hit & ~same]):
        labels[hit & ~same & (hit_id == number)] = mapping.get(str(number), f"instance {number}（未対応）")
    labels[same & ~same_plane] = "同じ instance の別の面"
    labels[same_plane & front] = "屋根自身・同じ平面・表"
    labels[same_plane & ~front] = "屋根自身・同じ平面・裏"
    hit_count = int(hit.sum())
    total_light = float(radiance[hit].sum())
    rows = []
    categories = ["屋根自身・同じ平面・表", "屋根自身・同じ平面・裏", "同じ instance の別の面",
                  "家の別の部分（別の instance）"]
    categories += sorted(set(labels[hit]) - set(categories))
    for name in categories:
        selected = hit & (labels == name)
        n = int(selected.sum())
        rows.append({"分類": name, "画素数": n,
                     "当たりに占める割合": n / hit_count if hit_count else None,
                     "当たりの光に占める割合": float(radiance[selected].sum()) / total_light if total_light > 0 else None,
                     "面の下からの当たり": int((selected & below).sum()),
                     "面の下の光線に占める割合": float((selected & below).sum() / below.sum()) if below.any() else None,
                     "直接光の Y 合計": float(direct[selected].sum()),
                     "間接光の Y 合計": float(indirect[selected].sum()),
                     "instance": sorted(map(int, np.unique(hit_id[selected])))})
    buckets = [(0, .01, "0.01 m 未満"), (.01, .1, "0.01〜0.1 m"),
               (.1, 1, "0.1〜1 m"), (1, 5, "1〜5 m"), (5, np.inf, "5 m 以上")]
    distribution = {name: float(np.mean((distance[hit] >= lo) & (distance[hit] < hi))) if hit_count else None
                    for lo, hi, name in buckets}
    percentiles = (list(map(float, np.percentile(distance[hit], [5, 50, 95])))
                   if hit_count else [None, None, None])
    suspect = hit & (same & ((distance < .1) | same_plane | ~front))
    result = {
        "roof_pixels": count, "sample_index": sample_index,
        "traced_count": int(traced.sum()), "not_traced_count": int((~traced).sum()),
        "sky_miss_count": int((traced & ~hit).sum()),
        "hit_count": hit_count, "hit_fraction": hit_count / count,
        "hdr_y_cd_m2": {name: float((pfm(CAPTURE / f"roof-{name}" / "overview.hdr.pfm")[mask] @ Y).mean())
                          for name in ("off", "on")},
        "rows": rows, "distance_fraction_of_hits": distribution,
        "distance_percentile_m": dict(zip(("p05", "p50", "p95"), percentiles)),
        "below_fraction": float(below.mean()),
        "below_fraction_of_traced": float(below.sum() / traced.sum()) if traced.any() else None,
        "below_hit_count": int((hit & below).sum()),
        "screen_indirect_fraction_of_hits": float(screen[hit].mean()) if hit_count else None,
        "direct_y_sum": float(direct[hit].sum()), "indirect_y_sum": float(indirect[hit].sum()),
        "primary_instance_counts": {str(k): int(v) for k, v in zip(*np.unique(primary_id, return_counts=True))},
        "primary_gap_percentile_m": dict(zip(("p05", "p50", "p95", "max_abs"),
            [*map(float, np.percentile(flags[:, 1], [5, 50, 95])), float(np.max(np.abs(flags[:, 1])))])),
        "primary_gap_over_1cm_count": int(np.sum(np.abs(flags[:, 1]) > .01)),
        "primary_gap_over_1cm_hit_count": int(np.sum((np.abs(flags[:, 1]) > .01) & hit)),
        "object_mapping": mapping,
        "self_intersection_suspect_count": int(suspect.sum()),
        "self_intersection_suspect_fraction_of_hits": float(suspect.sum() / hit_count) if hit_count else None,
        "decision": ("a" if suspect.sum() > hit_count / 2 else "b") if hit_count else "no_hits",
        "same_plane_tolerance_m": .001,
        "short_distance_m": .1,
    }
    hdr = result["hdr_y_cd_m2"]
    hdr["on_off_ratio"] = hdr["on"] / hdr["off"]
    if all((CAPTURE / f"roof-{name}" / "overview.hdr.pfm").is_file()
           for name in ("off-nossao", "off-noao", "on-nohit", "on-nowide")):
        values = {name: float((pfm(CAPTURE / f"roof-{name}" / "overview.hdr.pfm")[mask] @ Y).mean())
                  for name in ("off-nossao", "off-noao", "on-nohit", "on-nowide")}
        values.update({name: hdr[name] for name in ("off", "on")})
        result["finite_difference_y_cd_m2"] = {
            "values": values,
            "ssao_removed": values["off-nossao"] - values["off"],
            "remaining_specular_ao_removed": values["off-noao"] - values["off-nossao"],
            "sky_change": values["on-nohit"] - values["off-noao"],
            "hit_and_wide_restored": values["on"] - values["on-nohit"],
            "wide_difference": values["on"] - values["on-nowide"],
            "total": values["on"] - values["off"],
        }
        log("FINITE_DIFFERENCE " + json.dumps(result["finite_difference_y_cd_m2"], ensure_ascii=False))
    (CAPTURE / "roof-hits.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    log(f"ROOF pixels={count} sample_index={sample_index} traced={int(traced.sum())} "
        f"not_traced={int((~traced).sum())} hit={hit_count} fraction={hit_count / count:.6f}")
    log(f"HDR_Y cd/m2 off={result['hdr_y_cd_m2']['off']:.3f} on={result['hdr_y_cd_m2']['on']:.3f}")
    log("分類 | 画素数 | 当たり割合 | 光の割合 | 面の下からの当たり | instance")
    for row in rows:
        hit_share = row["当たりに占める割合"]
        light_share = row["当たりの光に占める割合"]
        hit_text = f"{hit_share:.6f}" if hit_share is not None else "未定義"
        light_text = f"{light_share:.6f}" if light_share is not None else "未定義"
        log(f"{row['分類']} | {row['画素数']} | {hit_text} | {light_text} | "
            f"{row['面の下からの当たり']} | {row['instance']}")
    log("DISTANCE " + json.dumps(distribution, ensure_ascii=False))
    log("PERCENTILES " + json.dumps(result["distance_percentile_m"], ensure_ascii=False))
    log(f"BELOW fraction={result['below_fraction']:.6f} "
        f"fraction_of_traced={result['below_fraction_of_traced']} hit_count={result['below_hit_count']}")
    log(f"SELF_SUSPECT fraction={result['self_intersection_suspect_fraction_of_hits']} decision=({result['decision']})")
    return result


def main() -> None:
    global CAPTURE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--analyze-only", action="store_true", help="撮影済みの AOV を集計する")
    parser.add_argument("--diagnostics-only", action="store_true", help="既存の on/off を使い診断だけを撮る")
    parser.add_argument("--reuse-before", action="store_true", help="撮影済みの before を使う")
    parser.add_argument("--skip-before", action="store_true", help="before を残したまま屋根の撮影だけを行う")
    parser.add_argument("--capture-dir", type=Path, default=DEFAULT_CAPTURE, help="撮影物と集計の置き場")
    parser.add_argument("--variants", action="store_true", help="項ごとの有限差分の変種も撮影する")
    parser.add_argument("--copy-to", type=Path, help="指定した runs ディレクトリへ撮影物を写す（既定は写さない）")
    parser.add_argument("--mapping", type=Path, help="custom index と物体名の対応を記した JSON")
    args = parser.parse_args()
    CAPTURE = args.capture_dir if args.capture_dir.is_absolute() else ROOT / args.capture_dir
    if not args.analyze_only:
        if not args.skip_before and not args.diagnostics_only:
            if not args.reuse_before:
                run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                     "Scripts/Terrain/CaptureRegression.ps1", "-Item", ITEM, "-Phase", "before"])
            if not (BEFORE / "capture.txt").is_file():
                raise FileNotFoundError(f"回帰の before がありません: {BEFORE}")
            if "all_ok=1 exit=0" not in (BEFORE / "capture.txt").read_text(encoding="utf-8-sig"):
                raise RuntimeError("回帰の before が完了していません")
            if args.copy_to:
                shutil.copytree(BEFORE, args.copy_to / "regression" / ITEM / "before", dirs_exist_ok=True)
        if not args.diagnostics_only:
            capture_unmodified("roof-on")
            capture_unmodified("roof-off", ["-GameArgs", "rtgi-specular=off"])
        capture_variant("flags")
        sample_index = captured_sample_index(roof_mask())
        log(f"SAMPLE_INDEX: {sample_index}")
        for variant in ("geometry", "radiance"):
            capture_variant(variant, sample_index)
        if args.variants:
            for variant in ("off-nossao", "off-noao", "on-nohit", "on-nowide"):
                capture_lighting_variant(variant)
    analyze(args.mapping)
    if args.copy_to:
        shutil.copytree(CAPTURE, args.copy_to / "scene-capture" / ITEM, dirs_exist_ok=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        log(f"ERROR: {error}")
        sys.exit(1)
