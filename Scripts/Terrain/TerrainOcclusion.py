# 荒野の地表の素材（赤茶の土・礫の舗石・皮膜の付いた土・砂）の遮蔽の AO を、素材自身の起伏から焼き直し、土の枠・砂の枠の
# アトラスと同じ並びの遮蔽の画像（<アトラス>_occl_8k.png）を書く。
#
# 使い方:
#   python Scripts/Terrain/TerrainOcclusion.py --build    法線の地図から焼いた遮蔽の画像を書く
#   python Scripts/Terrain/TerrainOcclusion.py --verify   法線の地図が記録と一致し、遮蔽の画像が作り直しと一致し、素材ごとの平均が
#                                                         範囲に入るかを確かめる
# 入力と出力は git 管理外（Assets/Textures/PolyHaven/。TerrainMaterials.py --fetch・--build の後に回す）。
# 終了コード: すべて満たせば 0、どれかが欠ければ 1。
#
# なぜ焼き直すか: Poly Haven の AO の地図（ORM の R）は、素材自身の起伏から焼いた遮蔽より大幅に暗い（赤茶の土 0.558・礫の舗石 0.626。
# Docs/RenderingValidation/WastelandMaterialAO.md）。R は層の混ぜの高さ（くぼみほど低い）にも使うので値を変えずに残し、照明が遮蔽
# として読む値だけをこの画像にする（Assets/AssetSets/WastelandTerrain.json の orm_occlusion。クックで ORM の A に入る。
# 読む側は Assets/Shaders/Common/TerrainLayerBlend.glsl）。
#
# 焼き方: DirectX の法線の地図を BAKE_RESOLUTION 画素へ箱の平均で縮め、傾きをフーリエ変換で積分して高さ場にし（タイルの周期を使う）、
# 素材の実寸（TerrainMaterials.MATERIALS の size_m）で m にする。遮蔽は平らな面の上向きの法線に対する余弦の重みの見える割合で、
# 方位ごとの 1 − sin²（地平の仰角）の平均（BAKE_DIRECTIONS 方位、SEARCH_METERS までの距離で地平を探す）。焼いた値をタイルの周期で
# 双線形に元の地図の大きさへ広げ、8 bit にする。
#   - 刻みを 1024 画素（1 画素 1.5〜3 mm）にするのは、変位の地図を実寸に合わせて焼いた値（赤茶の土 0.945・礫の舗石 0.916）と
#     平均が揃う刻みだから。元の 4k の刻みでは JPEG の法線の雑音が高さに積もって暗くなる（赤茶の土 0.906・砂 0.968）
#   - 探す距離を 2 cm〜50 cm で変えても平均は 0.001 以内で同じ（遮蔽は数 mm〜1 cm の起伏で決まる）
import argparse
import json
import os
import sys

import numpy
from PIL import Image

import TerrainMaterials
from ReferencePhotos import REPO_ROOT

Image.MAX_IMAGE_PIXELS = None

BAKE_RESOLUTION = 1024
SEARCH_METERS = 0.05
BAKE_DIRECTIONS = 16
BAKE_STEPS = 40
# 出力の写像の接尾辞（TerrainMaterials.atlas_path の <写像>）
OCCLUSION_SUFFIX = "occl"
# 素材ごとの平均の遮蔽の範囲（起伏から焼いた値は 0.92〜0.98。地図の平均より明るいことも確かめる）
MEAN_RANGE = (0.90, 0.99)


def load_normal(path):
    with Image.open(path) as image:
        resized = image.convert("RGB").resize((BAKE_RESOLUTION, BAKE_RESOLUTION), Image.BOX)
    return numpy.asarray(resized, dtype=numpy.float64) / 255.0


def height_from_normal(normal, pixel_m):
    # 法線の傾きを最小二乗で積分した高さ（m）。DirectX は緑が下向きなので OpenGL の向きへ直す
    n = normal * 2.0 - 1.0
    nx, ny, nz = n[..., 0], -n[..., 1], numpy.maximum(n[..., 2], 0.05)
    # 列の向きの傾き dh/dx と、行の向き（画像の下へ進む = 上向きの逆）の傾き dh/d(行)
    gx = -nx / nz
    gy = ny / nz
    h, w = gx.shape
    fx = 2j * numpy.pi * numpy.fft.fftfreq(w)[None, :]
    fy = 2j * numpy.pi * numpy.fft.fftfreq(h)[:, None]
    denom = numpy.real(fx * numpy.conj(fx) + fy * numpy.conj(fy))
    denom[0, 0] = 1.0
    spectrum = (numpy.conj(fx) * numpy.fft.fft2(gx) + numpy.conj(fy) * numpy.fft.fft2(gy)) / denom
    spectrum[0, 0] = 0.0
    return numpy.real(numpy.fft.ifft2(spectrum)) * pixel_m


def bake_occlusion(height, pixel_m):
    steps = numpy.unique(numpy.round(numpy.geomspace(1, SEARCH_METERS / pixel_m, BAKE_STEPS)).astype(int))
    occlusion = numpy.zeros_like(height)
    for k in range(BAKE_DIRECTIONS):
        angle = 2.0 * numpy.pi * (k + 0.5) / BAKE_DIRECTIONS
        # 地平の仰角の tan（0 以上）
        horizon = numpy.zeros_like(height)
        for step in steps:
            dx, dy = int(round(numpy.cos(angle) * step)), int(round(numpy.sin(angle) * step))
            if dx == 0 and dy == 0:
                continue
            distance = numpy.hypot(dx, dy) * pixel_m
            shifted = numpy.roll(numpy.roll(height, -dy, axis=0), -dx, axis=1)
            horizon = numpy.maximum(horizon, (shifted - height) / distance)
        occlusion += 1.0 - horizon * horizon / (1.0 + horizon * horizon)
    return occlusion / BAKE_DIRECTIONS


def upsample_periodic(image, size):
    # タイルの周期で双線形に広げる（画素の中心どうしを合わせる）
    low = image.shape[0]
    coords = (numpy.arange(size) + 0.5) * low / size - 0.5
    base = numpy.floor(coords).astype(int)
    frac = coords - base
    i0, i1 = base % low, (base + 1) % low
    rows = image[i0, :] * (1.0 - frac)[:, None] + image[i1, :] * frac[:, None]
    return rows[:, i0] * (1.0 - frac)[None, :] + rows[:, i1] * frac[None, :]


def material_occlusion(material):
    """素材の遮蔽の画像（uint8、元の地図の大きさ）と、焼いた値の平均"""
    normal_path = TerrainMaterials.source_path(material, "nor_dx")
    with Image.open(normal_path) as image:
        size = image.size[1]
    pixel_m = material["size_m"] / BAKE_RESOLUTION
    baked = bake_occlusion(height_from_normal(load_normal(normal_path), pixel_m), pixel_m)
    encoded = numpy.clip(upsample_periodic(baked, size) * 255.0 + 0.5, 0.0, 255.0).astype(numpy.uint8)
    return encoded, float(baked.mean())


def map_mean(material):
    with Image.open(TerrainMaterials.source_path(material, "ao")) as image:
        return float(numpy.asarray(image.convert("L"), dtype=numpy.float64).mean() / 255.0)


def compose(atlas, baked):
    left, right = baked[atlas["left"]], baked[atlas["right"]]
    if left.shape != right.shape:
        raise ValueError("%s の左右の寸法が違います: %s と %s" % (atlas["id"], left.shape, right.shape))
    return numpy.concatenate([left, right], axis=1)


def atlas_materials():
    ids = []
    for atlas in TerrainMaterials.ATLASES:
        for side in ("left", "right"):
            if atlas[side] not in ids:
                ids.append(atlas[side])
    return [TerrainMaterials.material_by_id(material_id) for material_id in ids]


def build():
    baked = {}
    for material in atlas_materials():
        baked[material["id"]], mean = material_occlusion(material)
        print("baked %s（%s、%.1f m）焼いた平均 %.3f・地図の平均 %.3f" % (material["id"], material["layer"],
                                                                 material["size_m"], mean, map_mean(material)))
    for atlas in TerrainMaterials.ATLASES:
        path = TerrainMaterials.atlas_path(atlas, OCCLUSION_SUFFIX)
        image = compose(atlas, baked)
        Image.fromarray(image, "L").save(path, optimize=False, compress_level=6)
        print("wrote %s %dx%d（左 %s・右 %s）" % (os.path.relpath(path, REPO_ROOT), image.shape[1], image.shape[0],
                                                atlas["left"], atlas["right"]))
    return 0


def verify():
    failures = []

    def check(condition, message):
        print(("ok   " if condition else "FAIL ") + message)
        if not condition:
            failures.append(message)

    record = {}
    if os.path.exists(TerrainMaterials.RECORD_PATH):
        with open(TerrainMaterials.RECORD_PATH, encoding="utf-8") as stream:
            record = json.load(stream)
    recorded = {entry["id"]: entry for entry in record.get("materials", [])}
    baked = {}
    for material in atlas_materials():
        files = {file["name"]: file for file in (recorded.get(material["id"]) or {}).get("files", [])}
        for suffix in ("nor_dx", "ao"):
            path = TerrainMaterials.source_path(material, suffix)
            entry = files.get(os.path.basename(path))
            check(entry is not None and os.path.exists(path) and TerrainMaterials.md5_of(path) == entry["md5"],
                  "%s が配布元の md5 と一致" % os.path.basename(path))
        if not os.path.exists(TerrainMaterials.source_path(material, "nor_dx")):
            continue
        baked[material["id"]], mean = material_occlusion(material)
        source_mean = map_mean(material)
        check(MEAN_RANGE[0] <= mean <= MEAN_RANGE[1] and mean > source_mean,
              "%s（%s）の焼いた遮蔽の平均 %.3f が %.2f〜%.2f で、地図の平均 %.3f より明るい" % (
                  material["id"], material["layer"], mean, MEAN_RANGE[0], MEAN_RANGE[1], source_mean))
    for atlas in TerrainMaterials.ATLASES:
        if atlas["left"] not in baked or atlas["right"] not in baked:
            continue
        path = TerrainMaterials.atlas_path(atlas, OCCLUSION_SUFFIX)
        if not os.path.exists(path):
            check(False, "%s がある" % os.path.relpath(path, REPO_ROOT))
            continue
        with Image.open(path) as image:
            written = numpy.asarray(image.convert("L"))
        expected = compose(atlas, baked)
        check(written.shape == expected.shape and numpy.array_equal(written, expected),
              "%s（%dx%d）の左右が %s・%s の作り直しと一致" % (os.path.basename(path), written.shape[1], written.shape[0],
                                                             atlas["left"], atlas["right"]))
        with Image.open(TerrainMaterials.atlas_path(atlas, "ao")) as image:
            height_size = image.size
        check(written.shape[::-1] == height_size,
              "%s の大きさが AO の地図（高さ）のアトラス %dx%d と同じ（ORM の枠は同じ大きさにする）" % (
                  os.path.basename(path), height_size[0], height_size[1]))
    print("fail=%d" % len(failures))
    print("result=" + ("pass" if not failures else "fail"))
    return 0 if not failures else 1


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="荒野の地表の素材の遮蔽の AO を素材の起伏から焼き、アトラスの並びで書く")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--build", action="store_true")
    mode.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    if args.build:
        return build()
    return verify()


if __name__ == "__main__":
    sys.exit(main())
