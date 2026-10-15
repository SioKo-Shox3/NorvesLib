# 荒野の地形の 3 層の素材（Poly Haven の CC0 の写真由来のテクスチャ）と土の皮膜の色の素材を落とし、アルベドを実在の値へ較正する。
#
# 使い方:
#   python Scripts/Terrain/TerrainMaterials.py --fetch    足りないテクスチャを Poly Haven から落とし、記録（materials.json）を書く
#   python Scripts/Terrain/TerrainMaterials.py --build    アルベドを較正した PNG（<id>_diff_cal_4k.png）を書く
#   python Scripts/Terrain/TerrainMaterials.py --verify   元の画像が記録と一致し、較正した画像の平均のアルベドが目標と範囲に入るかを確かめる
# テクスチャと記録は git 管理外（Assets/Textures/PolyHaven/）。終了コード: すべて満たせば 0、どれかが欠ければ 1。
#
# 較正: 元のアルベドの画像（sRGB）を線形へ戻し、線形の輝度（Rec.709）の平均が層の目標になる倍率を 3 チャンネルに同じだけ
# 掛けて（色味は元の写真のまま）、sRGB へ戻して 8 bit の PNG に書く。写真の素材の明るさは撮影と現像で決まり、物の反射率とは
# 限らないので、平均だけを実在の値へ寄せる（模様の明暗の比は残す）。
#
# 目標の平均のアルベド（線形の輝度）と根拠:
#   - 赤茶の土（谷床。red_mud_stones。締まった赤い土にひび割れと小石が散る）0.16: 赤い砂漠の土の範囲 0.15〜0.25 の下の側。
#     赤い土の分光反射率は青 0.05・緑 0.12・赤 0.25 前後で、目の感度（緑が重い）で見た輝度は広帯域の値より低い。
#     手本の谷床は日向でも暗い赤土（写真の地面 Y 0.14）
#   - 礫の舗石（崖錐と礫の地表。red_laterite_soil_stones。赤い土の面を数 mm〜数 cm の礫が詰まって覆う）0.16: 範囲 0.15〜0.30 の
#     下の側。手本の足元の写真の礫の舗石は、礫の間の土と同じ暗い赤茶で、土より明るく見えない
#   - 皮膜の付いた土（cracked_red_ground。こぶ状に盛り上がった暗い褐色の土の皮膜）0.11: 範囲 0.08〜0.14。コロラド高原の
#     地衣類・藍藻の土の皮膜は裸の土より 3〜5 割暗い。層の枠は 3 つしか無いので、皮膜は 1 層目の土の上に色の比
#     （皮膜の較正した平均 ÷ 土の較正した平均）を掛けて描く。比はシェーダーの TERRAIN_CRUST_TINT
#     （Assets/Shaders/Common/TerrainLayerBlend.glsl）に写し、--verify が一致を確かめる。アルベドの画像だけを落とす
#   - 赤褐色の砂岩（露岩。cliff_side）0.22: 範囲 0.20〜0.35 の下の側。Cedar Mesa 砂岩の赤褐色の崖（写真の日向の崖 Y 0.24、
#     露出の補正の後で土の 1.5〜1.8 倍）
import argparse
import hashlib
import json
import os
import sys
import urllib.request

import numpy
from PIL import Image

from ReferencePhotos import REPO_ROOT, ssl_context

Image.MAX_IMAGE_PIXELS = None

TEXTURE_ROOT = os.path.join(REPO_ROOT, "Assets", "Textures", "PolyHaven")
RECORD_PATH = os.path.join(TEXTURE_ROOT, "wasteland_terrain_materials.json")
FILES_API = "https://api.polyhaven.com/files/"
USER_AGENT = "NorvesLibTerrainMaterials/1.0"
LICENSE = "CC0 1.0（Poly Haven）"
RESOLUTION = "4k"
# 落とす写像（Poly Haven の files の API のキー → ファイル名の接尾辞）
MAPS = {"Diffuse": "diff", "nor_dx": "nor_dx", "Rough": "rough", "AO": "ao"}
# 色の比だけに使う素材（皮膜）が落とす写像
TINT_MAPS = {"Diffuse": "diff"}
LUMINANCE = numpy.array([0.2126, 0.7152, 0.0722])

# 層ごとの素材。size_m はテクスチャ 1 枚が覆う実寸（Poly Haven の記載の幅）、target は較正の目標、range は実在の値の範囲
MATERIALS = [
    {"layer": "赤茶の土", "id": "red_mud_stones", "name": "Red Mud Stones", "size_m": 1.5, "target": 0.16,
     "range": (0.15, 0.25)},
    {"layer": "礫の舗石", "id": "red_laterite_soil_stones", "name": "Red Laterite Soil Stones", "size_m": 2.0, "target": 0.16,
     "range": (0.15, 0.30)},
    {"layer": "赤褐色の砂岩", "id": "cliff_side", "name": "Cliff Side", "size_m": 1.8, "target": 0.22,
     "range": (0.20, 0.35)},
]
# 皮膜の付いた土（色の比だけ。base の層の土に掛ける）。比は TERRAIN_CRUST_TINT と一致させる
CRUST = {"layer": "皮膜の付いた土", "id": "cracked_red_ground", "name": "Cracked Red Ground", "size_m": 2.0, "target": 0.11,
         "range": (0.08, 0.14), "base": "red_mud_stones"}
SHADER_PATH = os.path.join(REPO_ROOT, "Assets", "Shaders", "Common", "TerrainLayerBlend.glsl")
# 較正で最大の成分が 1 を超え、1 に収めた画素の割合の上限（明るい礫・砂岩の縁のわずかな画素だけ）
MAX_CLIPPED_RATIO = 0.02


def source_path(material, suffix):
    return os.path.join(TEXTURE_ROOT, material["id"], "%s_%s_%s.jpg" % (material["id"], suffix, RESOLUTION))


def calibrated_path(material):
    return os.path.join(TEXTURE_ROOT, material["id"], "%s_diff_cal_%s.png" % (material["id"], RESOLUTION))


def md5_of(path):
    digest = hashlib.md5()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def http_json(url, context):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, context=context, timeout=120) as response:
        return json.loads(response.read().decode("utf-8"))


def download(url, path, context):
    partial = path + ".part"
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, context=context, timeout=300) as response, open(partial, "wb") as stream:
        for block in iter(lambda: response.read(1 << 20), b""):
            stream.write(block)
    os.replace(partial, path)


def srgb_to_linear(encoded):
    return numpy.where(encoded <= 0.04045, encoded / 12.92, ((encoded + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(linear):
    return numpy.where(linear <= 0.0031308, linear * 12.92, 1.055 * numpy.power(linear, 1.0 / 2.4) - 0.055)


def load_linear(path):
    with Image.open(path) as image:
        return srgb_to_linear(numpy.asarray(image.convert("RGB"), dtype=numpy.float32) / 255.0)


def mean_albedo(linear):
    mean = linear.reshape(-1, 3).mean(axis=0, dtype=numpy.float64)
    return mean, float(mean @ LUMINANCE)


def fetch():
    context = ssl_context()
    record = {"license": LICENSE, "source": "https://polyhaven.com/a/", "materials": []}
    for material in MATERIALS + [CRUST]:
        os.makedirs(os.path.join(TEXTURE_ROOT, material["id"]), exist_ok=True)
        files = http_json(FILES_API + material["id"], context)
        entry = {"layer": material["layer"], "id": material["id"], "name": material["name"],
                 "page": "https://polyhaven.com/a/" + material["id"], "size_m": material["size_m"], "files": []}
        for key, suffix in (TINT_MAPS if material is CRUST else MAPS).items():
            info = files[key][RESOLUTION]["jpg"]
            path = source_path(material, suffix)
            if not os.path.exists(path) or md5_of(path) != info["md5"]:
                print("fetch %s" % info["url"], flush=True)
                download(info["url"], path, context)
            if md5_of(path) != info["md5"]:
                print("FAIL md5 が配布元と違います: %s" % path)
                return 1
            entry["files"].append({"name": os.path.basename(path), "url": info["url"], "bytes": info["size"],
                                   "md5": info["md5"]})
        record["materials"].append(entry)
        print("ok %s（%s、%.1f m）%d 枚" % (material["id"], material["layer"], material["size_m"], len(entry["files"])))
    with open(RECORD_PATH, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(record, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print("wrote %s" % os.path.relpath(RECORD_PATH, REPO_ROOT))
    return 0


def limit_to_one(scaled):
    # 最大の成分が 1 を超える画素は、画素ごとに最大の成分で割って 1 に収める（色相を保ち、白く飛ばさない）
    peak = numpy.max(scaled, axis=2, keepdims=True)
    return numpy.where(peak > 1.0, scaled / numpy.maximum(peak, 1.0e-6), scaled)


def calibrate(material):
    linear = load_linear(source_path(material, "diff"))
    mean, luminance = mean_albedo(linear)
    # 1 に収めた分だけ平均が下がるので、収めた後の平均が目標になるまで倍率を詰める
    scale = material["target"] / luminance
    for _ in range(4):
        _, limited_luminance = mean_albedo(limit_to_one(linear * scale))
        scale *= material["target"] / limited_luminance
    scaled = linear * scale
    limited = float(numpy.mean(numpy.max(scaled, axis=2) > 1.0))
    encoded = numpy.clip(linear_to_srgb(limit_to_one(scaled)) * 255.0 + 0.5, 0.0, 255.0).astype(numpy.uint8)
    return encoded, {"source_mean": mean, "source_luminance": luminance, "scale": scale, "clipped": limited}


def calibrated_mean(material):
    # 較正した後の線形の平均（RGB）。画像を書かずに求める
    encoded, _ = calibrate(material)
    return mean_albedo(srgb_to_linear(encoded.astype(numpy.float32) / 255.0))


def crust_tint():
    # 皮膜の色の比（皮膜の較正した平均 ÷ 土の較正した平均）と、皮膜の較正した平均・輝度
    base = next(material for material in MATERIALS if material["id"] == CRUST["base"])
    crust_mean, crust_luminance = calibrated_mean(CRUST)
    base_mean, _ = calibrated_mean(base)
    return crust_mean / base_mean, crust_mean, crust_luminance


def shader_crust_tint():
    import re
    with open(SHADER_PATH, encoding="utf-8-sig") as stream:
        match = re.search(r"const vec3 TERRAIN_CRUST_TINT = vec3\(([^)]*)\);", stream.read())
    return numpy.array([float(value) for value in match.group(1).split(",")]) if match else None


def build():
    tint, crust_mean, crust_luminance = crust_tint()
    print("皮膜 %s の較正した平均 %s（輝度 %.3f）、土 %s に掛ける比 TERRAIN_CRUST_TINT = vec3(%.3f, %.3f, %.3f)" % (
        CRUST["id"], numpy.round(crust_mean, 3), crust_luminance, CRUST["base"], tint[0], tint[1], tint[2]))
    for material in MATERIALS:
        encoded, info = calibrate(material)
        Image.fromarray(encoded, "RGB").save(calibrated_path(material), optimize=False, compress_level=6)
        print("wrote %s 元の平均 %s（輝度 %.3f）× %.3f、1 に収めた画素 %.4f" % (
            os.path.relpath(calibrated_path(material), REPO_ROOT), numpy.round(info["source_mean"], 3),
            info["source_luminance"], info["scale"], info["clipped"]))
    return 0


def verify():
    failures = []

    def check(condition, message):
        print(("ok   " if condition else "FAIL ") + message)
        if not condition:
            failures.append(message)

    record = {}
    if os.path.exists(RECORD_PATH):
        with open(RECORD_PATH, encoding="utf-8") as stream:
            record = json.load(stream)
    check(record.get("license") == LICENSE, "記録の使用許諾が %s" % LICENSE)
    recorded = {entry["id"]: entry for entry in record.get("materials", [])}
    for material in MATERIALS:
        entry = recorded.get(material["id"])
        check(entry is not None and entry["size_m"] == material["size_m"],
              "%s の記録がある（名前・出典・実寸 %.1f m）" % (material["id"], material["size_m"]))
        for file in (entry or {}).get("files", []):
            path = os.path.join(TEXTURE_ROOT, material["id"], file["name"])
            check(os.path.exists(path) and md5_of(path) == file["md5"], "%s が配布元の md5 と一致" % file["name"])
        path = calibrated_path(material)
        if not os.path.exists(path):
            check(False, "%s がある" % os.path.relpath(path, REPO_ROOT))
            continue
        encoded, info = calibrate(material)
        with Image.open(path) as image:
            written = numpy.asarray(image.convert("RGB"))
        check(numpy.array_equal(written, encoded), "%s が作り直しと一致" % os.path.basename(path))
        mean, luminance = mean_albedo(srgb_to_linear(written.astype(numpy.float32) / 255.0))
        low, high = material["range"]
        check(abs(luminance - material["target"]) < 0.005 and low <= luminance <= high,
              "%s（%s）の平均のアルベド %.3f（RGB %s）が目標 %.2f・範囲 %.2f〜%.2f" % (
                  material["id"], material["layer"], luminance, numpy.round(mean, 3), material["target"], low, high))
        check(info["clipped"] <= MAX_CLIPPED_RATIO, "%s の較正で 1 に収めた画素 %.4f が %.3f 以下" % (
            material["id"], info["clipped"], MAX_CLIPPED_RATIO))
    entry = recorded.get(CRUST["id"])
    check(entry is not None and entry["size_m"] == CRUST["size_m"], "%s の記録がある（皮膜の色の比）" % CRUST["id"])
    for file in (entry or {}).get("files", []):
        path = os.path.join(TEXTURE_ROOT, CRUST["id"], file["name"])
        check(os.path.exists(path) and md5_of(path) == file["md5"], "%s が配布元の md5 と一致" % file["name"])
    if os.path.exists(source_path(CRUST, "diff")):
        tint, crust_mean, crust_luminance = crust_tint()
        low, high = CRUST["range"]
        check(abs(crust_luminance - CRUST["target"]) < 0.005 and low <= crust_luminance <= high,
              "%s（%s）の較正した平均のアルベド %.3f（RGB %s）が目標 %.2f・範囲 %.2f〜%.2f" % (
                  CRUST["id"], CRUST["layer"], crust_luminance, numpy.round(crust_mean, 3), CRUST["target"], low, high))
        written = shader_crust_tint()
        check(written is not None and numpy.max(numpy.abs(written - tint)) < 0.005,
              "シェーダーの TERRAIN_CRUST_TINT %s が皮膜 ÷ 土の比 %s と一致" % (
                  None if written is None else numpy.round(written, 3), numpy.round(tint, 3)))
    else:
        check(False, "%s がある" % os.path.relpath(source_path(CRUST, "diff"), REPO_ROOT))
    print("fail=%d" % len(failures))
    print("result=" + ("pass" if not failures else "fail"))
    return 0 if not failures else 1


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="荒野の地形の 3 層の素材を落とし、アルベドを較正する")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--fetch", action="store_true")
    mode.add_argument("--build", action="store_true")
    mode.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    if args.fetch:
        return fetch()
    if args.build:
        return build()
    return verify()


if __name__ == "__main__":
    sys.exit(main())
