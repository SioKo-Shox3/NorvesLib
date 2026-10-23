# 室内の光のシーン（Game の interior-light）の床・レンガ・梁と窓の外の地面の Poly Haven の材質と、生成した家具（GLB に内蔵のアルベド）の
# アルベドを、チャンネルごとの倍率 k と「単位のアルベド」の画像に分けて書く。Game は画像を張り、物体の色（GBuffer がアルベドに掛け、
# RTGI の光線が当たった面の反射率にもする instance の色）に k を置く。
#
# 分ける理由: RTGI の光線はテクスチャを引かず、当たった面の反射率を instance の色だけで決める（テクスチャは白とみなす。
# Library/Core/Private/Rendering/LightingPass.cpp の RTGIInstanceData）。元のアルベドを instance の色 1 で張ると、GBuffer の色は
# 正しいが、光線の当たりは反射率 1 の白になり、日だまりの床の照り返しが数倍に明るく、色移りが白くなる。
# 線形のアルベドをチャンネルごとの 99 百分位 k で割った画像（上の 1 % は 1 で切る）を張り、instance の色を k にすると、
# GBuffer の色は元のまま（上の 1 % の明るい斑だけが k に揃う）で、光線の当たりの反射率は白でなく k（平均の 1.2〜2.5 倍。
# ラグは明るい模様の斑が 99 百分位を決めるので赤は約 5 倍）になる。
#
# 画像は sRGB で符号化した 8 bit の PNG で書き、Assets/AssetSets/InteriorLightTextures.json がアルベドの用途（sRGB）で焼く。
# Game はクック済みを sRGB の形式で張り（TextureColorEncoding::Srgb）、標本はハードが線形へ戻す。
# 倍率 k は Game/GameModes/InteriorLight/InteriorLightRoutine.cpp の表（部屋の材質の ObjectColor・kFurnitureKinds の AlbedoScale）に
# 写してある。元の画像を替えたら、このスクリプトを流して出た k をその表へ写す。--verify は書いた画像の k が表の値に合うかを確かめる。
#
# 使い方:
#   python Scripts/MakeInteriorLightAlbedo.py            単位のアルベドの PNG を書き、k を出す
#   python Scripts/MakeInteriorLightAlbedo.py --verify   PNG があり、元の画像から求めた k が表の値に合うかだけを確かめる
# 元の画像は Scripts/FetchPolyHavenTextures.ps1 -Set interior（床・レンガ・梁）・-Set startup（窓の外の地面）と Scripts/GenerateSceneModels.py（家具）が作る。
# 終了コード: すべて書けて表に合えば 0、元の画像が無い・k が表と違えば 1。
import argparse
import io
import json
import os
import struct
import sys

import numpy
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
POLYHAVEN_ROOT = os.path.join(REPO_ROOT, "Assets", "Textures", "PolyHaven")
GENERATED_ROOT = os.path.join(REPO_ROOT, "Assets", "Models", "Generated")
PERCENTILE = 99.0
# 家具のアトラスの空き（黒）を百分位から外す線形の RGB の和の下限
ATLAS_EMPTY_SUM = 0.003
# 表の k との許容（相対）
SCALE_TOLERANCE = 0.01

# Game の表に写した k（線形の RGB）。上の説明の手順で求めた値
EXPECTED_SCALES = {
    "wood_floor": (0.3185, 0.1746, 0.0823),
    "red_brick": (0.4452, 0.3095, 0.2423),
    "dark_wood": (0.2423, 0.0666, 0.0203),
    "forrest_ground_01": (0.6514, 0.5333, 0.3712),
    "wooden_table": (0.3372, 0.1812, 0.0742),
    "wooden_chair": (0.4397, 0.2051, 0.0742),
    "bookshelf": (0.5210, 0.3968, 0.2423),
    "sofa": (0.3325, 0.3467, 0.3613),
    "potted_plant": (0.6240, 0.3763, 0.1529),
    "floor_lamp": (0.9911, 0.9911, 1.0000),
    "rug": (0.9911, 0.5906, 0.3864),
}
POLYHAVEN_IDS = ("wood_floor", "red_brick", "dark_wood", "forrest_ground_01")
FURNITURE_IDS = ("wooden_table", "wooden_chair", "bookshelf", "sofa", "potted_plant", "floor_lamp", "rug")


def srgb_to_linear(values):
    return numpy.where(values <= 0.04045, values / 12.92, ((values + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(values):
    return numpy.where(values <= 0.0031308, values * 12.92, 1.055 * numpy.power(values, 1.0 / 2.4) - 0.055)


def read_glb_image(path, index):
    """GLB の BIN の塊から images[index] を読む"""
    with open(path, "rb") as stream:
        data = stream.read()
    json_length = struct.unpack("<I", data[12:16])[0]
    document = json.loads(data[20:20 + json_length])
    offset = 20 + json_length
    bin_length = struct.unpack("<I", data[offset:offset + 4])[0]
    binary = data[offset + 8:offset + 8 + bin_length]
    view = document["bufferViews"][document["images"][index]["bufferView"]]
    start = view.get("byteOffset", 0)
    return Image.open(io.BytesIO(binary[start:start + view["byteLength"]]))


def source_and_output(asset_id):
    if asset_id in POLYHAVEN_IDS:
        folder = os.path.join(POLYHAVEN_ROOT, asset_id)
        return os.path.join(folder, f"{asset_id}_diff_4k.jpg"), os.path.join(folder, f"{asset_id}_diff_unit_4k.png")
    folder = os.path.join(GENERATED_ROOT, asset_id)
    return os.path.join(folder, f"{asset_id}.glb"), os.path.join(folder, f"{asset_id}_albedo_unit.png")


def load_linear(asset_id, source):
    image = read_glb_image(source, 0) if asset_id in FURNITURE_IDS else Image.open(source)
    return srgb_to_linear(numpy.asarray(image.convert("RGB"), dtype=numpy.float64) / 255.0)


def measure_scale(asset_id, linear):
    pixels = linear.reshape(-1, 3)
    if asset_id in FURNITURE_IDS:
        pixels = pixels[pixels.sum(axis=1) > ATLAS_EMPTY_SUM]
    return numpy.percentile(pixels, PERCENTILE, axis=0)


def main():
    parser = argparse.ArgumentParser(description="室内の光の単位のアルベドを書く")
    parser.add_argument("--verify", action="store_true", help="書かずに、PNG があり k が表に合うかを確かめる")
    args = parser.parse_args()

    failures = []
    for asset_id in POLYHAVEN_IDS + FURNITURE_IDS:
        source, output = source_and_output(asset_id)
        if not os.path.isfile(source):
            failures.append(f"{asset_id}: 元の画像が無い（{os.path.relpath(source, REPO_ROOT)}）")
            continue
        linear = load_linear(asset_id, source)
        scale = measure_scale(asset_id, linear)
        expected = numpy.array(EXPECTED_SCALES[asset_id])
        if numpy.any(numpy.abs(scale - expected) / expected > SCALE_TOLERANCE):
            failures.append(f"{asset_id}: k {tuple(round(float(v), 4) for v in scale)} が表の "
                            f"{EXPECTED_SCALES[asset_id]} と違う")
        if args.verify:
            if not os.path.isfile(output):
                failures.append(f"{asset_id}: 単位のアルベドが無い（{os.path.relpath(output, REPO_ROOT)}）")
        else:
            unit = numpy.clip(linear / numpy.maximum(scale, 1.0e-4), 0.0, 1.0)
            encoded = numpy.round(linear_to_srgb(unit) * 255.0).astype(numpy.uint8)
            Image.fromarray(encoded, "RGB").save(output)
        print(f"INTERIOR_ALBEDO id={asset_id} k={scale[0]:.4f},{scale[1]:.4f},{scale[2]:.4f} "
              f"output={os.path.relpath(output, REPO_ROOT)}")
    for failure in failures:
        print(f"INTERIOR_ALBEDO failure: {failure}")
    print(f"INTERIOR_ALBEDO result={'pass' if not failures else 'fail'}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
