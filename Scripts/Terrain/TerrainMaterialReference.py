# 地表の材質（荒野の地形の 3 層の混ぜ合わせ）を Blender で再現するための値と画像を用意する（SCN-WLR-MATERIAL-*）。
# BlenderReference.py --albedo が使い、ReferenceCheck.py --albedo が同じ関数で記録を照らす。
#
# 値の表 terrain_material.json:
#   - material: エンジンが組み立てのときにログへ書いた WASTELAND_TERRAIN_MATERIAL の JSON（WastelandRoutine.cpp の定数から
#     描画へ渡す MegaTerrainBlend の値とテクスチャのパス、地表の分類の地図・遠景の地図の範囲）。撮影のログから取り出し、
#     全視点で同じであることを確かめる
#   - packed: それをシェーダーの TerrainBlendParams（vec4 × 10）へ詰めた値（Library/Core/Private/Rendering/TerrainBlendShading.h の
#     PackTerrainBlendParams と同じ式を float32 で）
#   - shader_constants: Assets/Shaders/Common/TerrainLayerBlend.glsl・TerrainStrata.glsl の const（式のまま）
#   - textures: 標本する画像（アルベド・AO・地表の分類の地図・遠景の地図）の元のファイル・大きさ・段の数・ミップの段の画像
# ミップの段: アセットのクック（Tools/AssetCook/TextureCooker.cpp の BuildMipChain）と同じ作り方で、元の画像から全段を作る
#   （2 で割った大きさの各画素が覆う元の画素の箱の平均。アルベド（sRGB）は線形へ戻して平均し 8 bit の sRGB へ丸め、
#   AO・地図は 8 bit の値の平均を四捨五入）。段 0 も PNG に書き直す（JPEG の復号の差を段ごとに持ち込まない）。
#   ORM のうち使うのは AO（R）だけ。地表の分類の地図と遠景の地図の RGBA は、OIIO が透明度を掛けた値に変えないよう RGB と A の
#   2 枚に分ける。エンジンはこれを BC7 で圧縮して張るが、圧縮は再現しない（ReferenceCheck.py --albedo の記録に書く）。
# OSL のシェーダー: Scripts/Terrain/TerrainAlbedo.osl の前に、値の表から生成した定数とテクスチャの段を引く関数を置いた
#   1 つのファイル terrain_albedo.osl を書く。
import hashlib
import json
import math
import os
import re

import numpy as np
from PIL import Image

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
MATERIAL_LOG_KEY = "WASTELAND_TERRAIN_MATERIAL "
SHADER_FILES = ("Assets/Shaders/Common/TerrainLayerBlend.glsl", "Assets/Shaders/Common/TerrainStrata.glsl")
PACKING_SOURCE = "Library/Core/Private/Rendering/TerrainBlendShading.h"
OSL_SOURCE = "Scripts/Terrain/TerrainAlbedo.osl"
# 地形のテクスチャのサンプラーの異方性の上限（Library/Core/Private/Rendering/VisibilityResolvePass.cpp の
# m_TerrainMaterialSampler・MegaGeometryPass.cpp の TerrainSamplerMaxAnisotropy）
MAX_ANISOTROPY = 16
# Game はクック済みの sRGB のアルベドを UNORM の形式で張り、シェーダーが標本の後に線形へ戻す（TerrainBlendShading.h の
# IsTerrainAlbedoSrgbDecodeNeeded）。3 層とも戻す
ALBEDO_SRGB_DECODE = {"sand": 1, "dirt": 1, "rock": 1}
MIP_FORMAT_VERSION = 1

Image.MAX_IMAGE_PIXELS = None


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 24), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_text(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def repo_path(rel):
    return os.path.join(REPO_ROOT, rel)


# ---- 値の表 ----

def material_from_logs(log_paths):
    """撮影のログから WASTELAND_TERRAIN_MATERIAL の JSON を取り出す。(JSON の文字列, 辞書, 失敗の一覧)"""
    found = []
    failures = []
    for path in log_paths:
        if not os.path.isfile(path):
            failures.append(f"Game のログが無い: {path}")
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = [line for line in f if MATERIAL_LOG_KEY in line]
        if not lines:
            failures.append(f"{os.path.basename(path)} に {MATERIAL_LOG_KEY.strip()} が無い")
            continue
        found.extend(line.split(MATERIAL_LOG_KEY, 1)[1].strip() for line in lines)
    distinct = sorted(set(found))
    if len(distinct) > 1:
        failures.append(f"ログの {MATERIAL_LOG_KEY.strip()} が視点で食い違う（{len(distinct)} 通り）")
    if not distinct:
        return None, None, failures or ["材質の値のログが無い"]
    return distinct[0], json.loads(distinct[0]), failures


def f32(value):
    return float(np.float32(value))


def pack_params(material):
    """PackTerrainBlendParams（地図あり・遠景の地図あり・有効・確認用の色分けなし）と同じ値を float32 で求める。
    名前 → 値（P_SLOPE_X など。GLSL の params.slope.x）"""
    deg = np.float32(3.14159265358979) / np.float32(180.0)

    def ranged(start, end):
        start = np.float32(start)
        end = np.float32(end)
        return [f32(start), f32(end if end > start + np.float32(1.0e-4) else start + np.float32(1.0e-4))]

    def inv(value):
        value = np.float32(value)
        return f32(np.float32(1.0) / value) if value > 0 else 0.0

    m = material
    out = []
    out += ranged(np.float32(m["dirt_slope_degrees"][0]) * deg, np.float32(m["dirt_slope_degrees"][1]) * deg)
    out += ranged(np.float32(m["rock_slope_degrees"][0]) * deg, np.float32(m["rock_slope_degrees"][1]) * deg)
    out += ranged(m["dirt_height_range"][0], m["dirt_height_range"][1])
    out.append(f32(np.float32(m["dirt_height_bias_degrees"]) * deg))
    out.append(f32(max(np.float32(m["blend_depth"]), np.float32(1.0e-3))))
    out.append(inv(m["noise_wavelengths"][0]))
    out.append(inv(m["noise_wavelengths"][1]))
    out.append(f32(np.float32(m["noise_amplitude_degrees"]) * deg))
    out.append(f32(m["sand_fill_height"]))
    out.append(1.0)
    out.append(f32(m["dirt_uv_scale"]))
    out.append(f32(m["rock_uv_scale"]))
    out.append(0.0)  # layers.w のビット（法線と sRGB の戻し）はヘッダの別の定義で与える
    out.append(inv(m["macro_wavelengths"][0]))
    out.append(inv(m["macro_wavelengths"][1]))
    out.append(f32(m["macro_brightness"]))
    out.append(f32(m["macro_hue"]))
    out.append(f32(max(np.float32(m["detail_start_distance"]), np.float32(1.0e-3))))
    out.append(f32(np.log2(np.float32(max(np.float32(m["detail_distance_ratio"]), np.float32(1.01))))))
    out.append(f32(max(np.float32(m["detail_max_level"]), np.float32(0.0))))
    out.append(f32(max(np.float32(m["tile_meters"]), np.float32(1.0e-3))))
    mask = m["surface_mask"]
    mask_w = np.float32(mask["max_xz"][0]) - np.float32(mask["min_xz"][0])
    mask_d = np.float32(mask["max_xz"][1]) - np.float32(mask["min_xz"][1])
    has_mask = bool(mask["enabled"]) and mask_w > 1.0e-3 and mask_d > 1.0e-3
    out += [f32(mask["min_xz"][0]) if has_mask else 0.0, f32(mask["min_xz"][1]) if has_mask else 0.0,
            inv(mask_w) if has_mask else 0.0, inv(mask_d) if has_mask else 0.0]
    out.append(f32(min(max(np.float32(m["surface_mask_gravel_to_dirt"]), np.float32(0.0)), np.float32(1.0))))
    out.append(f32(max(np.float32(m["surface_mask_edge_fade_meters"]), np.float32(1.0e-3))))
    out.append(f32(max(np.float32(m["surface_mask_warp_meters"]), np.float32(0.0))))
    out.append(0.0)
    far = m["far_map"]
    far_w = np.float32(far["max_xz"][0]) - np.float32(far["min_xz"][0])
    far_d = np.float32(far["max_xz"][1]) - np.float32(far["min_xz"][1])
    has_far = bool(far["enabled"]) and far_w > 1.0e-3 and far_d > 1.0e-3 and np.float32(far["texel_meters"]) > 1.0e-3
    out += [f32(far["min_xz"][0]) if has_far else 0.0, f32(far["min_xz"][1]) if has_far else 0.0,
            inv(far_w) if has_far else 0.0, inv(far_d) if has_far else 0.0]
    out.append(f32(far["texel_meters"]) if has_far else 0.0)
    for variance in m["detail_slope_variance"]:
        out.append(f32(variance) if math.isfinite(variance) and variance > 0 else 0.0)
    groups = ("SLOPE", "HEIGHT", "NOISE", "LAYERS", "MACRO", "DETAIL", "MASK", "MASKMIX", "FARMAP", "FARMIX")
    names = [f"P_{g}_{c}" for g in groups for c in "XYZW"]
    if len(out) != len(names):
        raise RuntimeError(f"詰めた値の数が {len(out)}（{len(names)} のはず）")
    return dict(zip(names, out))


CONST_RE = re.compile(r"^const\s+(float|vec2|vec3|uint|int)\s+(\w+)\s*=\s*(.+?);", re.MULTILINE | re.DOTALL)
VECTOR_NAMES = ("WAVELENGTHS", "WEIGHTS")


def shader_constants():
    """GLSL の const を読む。名前 → {type, expr, file}"""
    constants = {}
    for rel in SHADER_FILES:
        with open(repo_path(rel), encoding="utf-8-sig") as f:
            text = f.read()
        for kind, name, expr in CONST_RE.findall(text):
            constants[name] = {"type": kind, "expr": " ".join(expr.split()), "file": rel}
    return constants


def osl_constant_lines(constants):
    """GLSL の const を OSL の #define へ。vec2 は _X・_Y、vec3 は名前（vector か color）と _X・_Y・_Z"""
    lines = []
    for name, item in constants.items():
        expr = item["expr"]
        kind = item["type"]
        if kind in ("uint", "int"):
            lines.append(f"#define {name} ({expr.rstrip('uU')})")
        elif kind == "float":
            lines.append(f"#define {name} ({expr})")
        else:
            match = re.fullmatch(r"vec([23])\((.*)\)", expr)
            if not match:
                raise RuntimeError(f"{name} の値 {expr} を読めない")
            parts = [p.strip() for p in match.group(2).split(",")]
            if len(parts) != int(match.group(1)):
                raise RuntimeError(f"{name} の成分の数が型と合わない")
            for axis, part in zip("XYZ", parts):
                lines.append(f"#define {name}_{axis} ({part})")
            if kind == "vec3":
                triple = "vector" if any(key in name for key in VECTOR_NAMES) else "color"
                lines.append(f"#define {name} {triple}({', '.join(parts)})")
    return lines


# ---- テクスチャとミップの段 ----

SRGB_TO_LINEAR = np.array([(v / 255.0) / 12.92 if v / 255.0 <= 0.04045 else ((v / 255.0 + 0.055) / 1.055) ** 2.4
                           for v in range(256)], dtype=np.float64)


def linear_to_srgb8(linear):
    """TextureCooker.cpp の LinearToSrgb8（lround は 0 以上で floor(x + 0.5)）"""
    linear = np.clip(linear, 0.0, 1.0)
    srgb = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1.0 / 2.4) - 0.055)
    return np.floor(np.clip(srgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


def box_sums(values, target_w, target_h):
    """BuildNextMip の覆う範囲（ComputeCoverageStart・End）の和と画素の数。values は (高さ, 幅, 成分)"""
    h, w = values.shape[:2]
    xs = np.arange(target_w + 1, dtype=np.int64) * w // target_w
    ys = np.arange(target_h + 1, dtype=np.int64) * h // target_h
    xb, xe = xs[:-1], np.clip(xs[1:], 1, w)
    yb, ye = ys[:-1], np.clip(ys[1:], 1, h)
    cum_x = np.concatenate([np.zeros((h, 1) + values.shape[2:], values.dtype), np.cumsum(values, axis=1)], axis=1)
    row = cum_x[:, xe] - cum_x[:, xb]
    cum_y = np.concatenate([np.zeros((1,) + row.shape[1:], row.dtype), np.cumsum(row, axis=0)], axis=0)
    sums = cum_y[ye] - cum_y[yb]
    counts = ((ye - yb)[:, None] * (xe - xb)[None, :]).astype(np.int64)
    return sums, counts


def next_mip(image, srgb):
    """1 段下（8 bit の (高さ, 幅, 成分)）"""
    h, w = image.shape[:2]
    tw = w // 2 if w > 1 else 1
    th = h // 2 if h > 1 else 1
    if srgb:
        sums, counts = box_sums(SRGB_TO_LINEAR[image], tw, th)
        return linear_to_srgb8(sums / counts[:, :, None])
    sums, counts = box_sums(image.astype(np.int64), tw, th)
    return ((sums + counts[:, :, None] // 2) // counts[:, :, None]).astype(np.uint8)


def load_channels(path, mode):
    """画像を 8 bit で読む。mode: rgb（3 成分）・r（1 成分。灰色の画像はその値）・rgba"""
    with Image.open(path) as im:
        if mode == "rgb":
            return np.asarray(im.convert("RGB"), dtype=np.uint8)
        if mode == "r":
            # クックは 4 成分で読んで R を使う（灰色の画像は灰色の値）
            if im.mode == "L":
                return np.asarray(im, dtype=np.uint8)[:, :, None]
            return np.asarray(im.convert("RGB"), dtype=np.uint8)[:, :, :1]
        return np.asarray(im.convert("RGBA"), dtype=np.uint8)


def texture_sets(material):
    """標本する画像の組。名前 → (元のパス, 読み方, sRGB か, 分けたときの成分)"""
    textures = material["textures"]
    sets = {}
    for layer in ("sand", "dirt", "rock"):
        sets[f"{layer}_albedo"] = (textures[layer]["albedo"], "rgb", True, None)
    # ORM の AO（R）は元の AO の画像（Assets/AssetSets/WastelandTerrain.json の orm_ao）
    with open(repo_path("Assets/AssetSets/WastelandTerrain.json"), encoding="utf-8-sig") as f:
        asset_set = json.load(f)
    orm_ao = {t["logical_path"]: t.get("orm_ao") for t in asset_set["textures"] if t.get("cook_usage") == "orm"}
    for layer in ("sand", "dirt", "rock"):
        ao = orm_ao.get(textures[layer]["orm"])
        if not ao:
            raise RuntimeError(f"{textures[layer]['orm']} の AO の元の画像が資産の組に無い")
        sets[f"{layer}_ao"] = (ao, "r", False, None)
    sets["mask_rgb"] = (material["surface_mask"]["texture"], "rgba", False, (0, 3))
    sets["mask_a"] = (material["surface_mask"]["texture"], "rgba", False, (3, 4))
    sets["far_rgb"] = (material["far_map"]["texture"], "rgba", False, (0, 3))
    sets["far_a"] = (material["far_map"]["texture"], "rgba", False, (3, 4))
    return sets


def build_mips(material, mips_dir, log=print):
    """ミップの段の画像を作る（元の画像のハッシュが記録と同じなら作り直さない）。名前 → 記録"""
    os.makedirs(mips_dir, exist_ok=True)
    manifest_path = os.path.join(mips_dir, "mips.json")
    manifest = {}
    if os.path.isfile(manifest_path):
        with open(manifest_path, encoding="utf-8") as f:
            manifest = json.load(f)
    if manifest.get("format_version") != MIP_FORMAT_VERSION:
        manifest = {"format_version": MIP_FORMAT_VERSION, "textures": {}}
    decoded = {}
    for name, (source, mode, srgb, split) in texture_sets(material).items():
        source_sha = sha256_file(repo_path(source))
        entry = manifest["textures"].get(name)
        if entry and entry.get("source_sha256") == source_sha and all(
                os.path.isfile(os.path.join(mips_dir, level["file"])) for level in entry["levels"]):
            continue
        key = (source, mode)
        if key not in decoded:
            decoded.clear()
            decoded[key] = load_channels(repo_path(source), mode)
        image = decoded[key]
        if split is not None:
            image = np.ascontiguousarray(image[:, :, split[0]:split[1]])
        levels = []
        level = 0
        while True:
            file_name = f"{name}_{level}.png"
            pixels = image[:, :, 0] if image.shape[2] == 1 else image
            Image.fromarray(pixels).save(os.path.join(mips_dir, file_name), compress_level=1)
            levels.append({"file": file_name, "width": int(image.shape[1]), "height": int(image.shape[0])})
            if image.shape[0] == 1 and image.shape[1] == 1:
                break
            image = next_mip(image, srgb)
            level += 1
        manifest["textures"][name] = {"source": source, "source_sha256": source_sha, "mode": mode, "srgb": srgb,
                                      "channels": list(split) if split else None, "levels": levels}
        log(f"TERRAIN_MATERIAL mips name={name} levels={len(levels)} size={levels[0]['width']}x{levels[0]['height']}")
        with open(manifest_path, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
    return manifest


TEXTURE_IDS = ("sand_albedo", "sand_ao", "dirt_albedo", "dirt_ao", "rock_albedo", "rock_ao", "mask_rgb", "mask_a",
               "far_rgb", "far_a")


def osl_texture_lines(manifest, mips_dir):
    lines = [f"#define TEX_MAX_ANISOTROPY {float(MAX_ANISOTROPY)}"]
    for index, name in enumerate(TEXTURE_IDS):
        level0 = manifest["textures"][name]["levels"][0]
        lines.append(f"#define TEX_{name.upper()} {index}")
        lines.append(f"#define TEX_{name.upper()}_WIDTH {level0['width']}")
        lines.append(f"#define TEX_{name.upper()}_HEIGHT {level0['height']}")
    for layer, decode in ALBEDO_SRGB_DECODE.items():
        lines.append(f"#define {layer.upper()}_ALBEDO_SRGB_DECODE {int(decode)}")
    for function, key in (("tex_levels", None), ("tex_width", "width"), ("tex_height", "height")):
        lines.append(f"int {function}(int id)")
        lines.append("{")
        for index, name in enumerate(TEXTURE_IDS):
            levels = manifest["textures"][name]["levels"]
            value = len(levels) if key is None else levels[0][key]
            lines.append(f"    if (id == {index}) return {value};")
        lines.append("    return 1;")
        lines.append("}")
    lines.append("// 段 level の画像を (s, t) で引く（interp は linear か closest。s・t は 0〜1 の中で渡す）。")
    lines.append("// t は画像の上が 0（GPU と同じ）。Cycles の OSL の texture() は下が 0 なので裏返して渡す")
    lines.append("color tex_lookup(int id, int level, float s, float t, string interp)")
    lines.append("{")
    lines.append("    float tb = 1.0 - t;")
    for index, name in enumerate(TEXTURE_IDS):
        lines.append(f"    if (id == {index})")
        lines.append("    {")
        for level, item in enumerate(manifest["textures"][name]["levels"]):
            path = os.path.join(mips_dir, item["file"]).replace("\\", "/")
            lines.append(f"        if (level <= {level}) return texture(\"{path}\", s, tb, \"interp\", interp, "
                         f"\"width\", 0.0);")
        lines.append("        return color(0);")
        lines.append("    }")
    lines.append("    return color(1, 0, 1);")
    lines.append("}")
    return lines


def write_osl(out_path, packed, constants, manifest, mips_dir):
    """生成した定数・テクスチャの関数と TerrainAlbedo.osl の本体を 1 つの OSL のファイルに書く。書いた中身のハッシュ"""
    with open(repo_path(OSL_SOURCE), encoding="utf-8-sig") as f:
        body = f.read()
    include = '#include "terrain_albedo_generated.h"'
    if include not in body:
        raise RuntimeError(f"{OSL_SOURCE} に {include} が無い")
    header = ["// 生成: Scripts/Terrain/TerrainMaterialReference.py（値の表 terrain_material.json から）"]
    header += [f"#define {name} ({value!r})" for name, value in packed.items()]
    header += osl_constant_lines(constants)
    header += osl_texture_lines(manifest, mips_dir)
    text = body.replace(include, "\n".join(header))
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return sha256_text(text)


def write_material_table(out_dir, json_text, material, mips_dir, log=print):
    """値の表・ミップの段・OSL を書く。記録の辞書"""
    os.makedirs(out_dir, exist_ok=True)
    packed = pack_params(material)
    constants = shader_constants()
    manifest = build_mips(material, mips_dir, log)
    table = {
        "material_log_sha256": sha256_text(json_text),
        "material": material,
        "packed": packed,
        "albedo_srgb_decode": ALBEDO_SRGB_DECODE,
        "max_anisotropy": MAX_ANISOTROPY,
        "shader_constants": constants,
        "shader_sha256": {rel: sha256_file(repo_path(rel)) for rel in SHADER_FILES + (PACKING_SOURCE,)},
        "textures": manifest["textures"],
    }
    table_path = os.path.join(out_dir, "terrain_material.json")
    with open(table_path, "w", encoding="utf-8") as f:
        json.dump(table, f, indent=2, ensure_ascii=False)
    osl_path = os.path.join(out_dir, "terrain_albedo.osl")
    osl_sha = write_osl(osl_path, packed, constants, manifest, mips_dir)
    return {
        "table": table_path,
        "table_sha256": sha256_file(table_path),
        "osl": osl_path,
        "osl_sha256": osl_sha,
        "osl_source_sha256": sha256_file(repo_path(OSL_SOURCE)),
        "mips_manifest_sha256": sha256_file(os.path.join(mips_dir, "mips.json")),
    }
