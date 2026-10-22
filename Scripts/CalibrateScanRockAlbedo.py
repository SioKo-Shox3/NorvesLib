# 荒野の足元の落石と崖の面の塊に使う Poly Haven の岩のスキャン（boulder_01・rock_09・rock_face_01・rock_face_02）の
# アルベドを崖の赤褐色の砂岩の色へ寄せた
# PNG（<id>_diff_lin_4k.png）と、粗さを乾いた砂岩の値へ寄せた ARM の PNG（<id>_arm_cal_4k.png）を書く。
# Assets/AssetSets/WastelandModels.json がこの 2 枚をアルベドと ORM として焼く。
#
# アルベドの PNG は sRGB でなく線形の値のまま 8 bit で書き、一覧では色空間を変換しない用途（cook_usage "orm"、BC7 の線形）で焼く。
# Game はクック済みの sRGB のアルベドを UNORM の形式で張り、MegaGeometry の材質のシェーダーは標本を線形へ戻さない
# （地形の 3 層だけがシェーダーで戻す。Library/Core/Private/Rendering/TerrainBlendShading.h）。sRGB のまま焼くと、標本が
# 符号化した値のままになり、暗い成分ほど明るく（G で 3 倍、B で 4 倍）写って岩が白く浮く。線形で書けば標本がそのまま
# 目標のアルベドになり、張る形式が sRGB に変わっても線形の用途のままなので色は変わらない。
#
# 使い方:
#   python Scripts/CalibrateScanRockAlbedo.py            較正した PNG を書き、平均の色を出す
#   python Scripts/CalibrateScanRockAlbedo.py --verify   書いた PNG の平均の色が目標に入るかだけを確かめる
# 元の画像は Scripts/FetchPolyHavenModels.ps1 -Set wasteland が落とす（git 管理外の Assets/Models/PolyHaven/）。
# 終了コード: すべての岩が目標に入れば 0、元の画像が無い・目標を外れたら 1。
#
# 較正: スキャンの色は灰褐色（線形の平均で R ÷ G 1.5、Y 0.13）で、地衣の緑と青灰の斑がある。画素ごとに線形の輝度の
# 明暗（平均に対する比）は LUMINANCE_CONTRAST の指数で縮めて残し、色味は目標の色へ寄せる（元の色味の偏りは CHROMA_KEEP の
# 指数だけ残す）。最後に 3 チャンネルに同じ倍率を掛けて、平均の輝度を目標にする。
# 明暗を縮めるのは、スキャンの暗い地衣の斑と汚れ（輝度の 5 百分位が平均の 0.31〜0.36 倍）がそのままだと砂岩の面に黒い斑として写るため。
# 手本の写真の落石の面は斑の無いほぼ一様な赤褐色で、明暗は形の陰影が受け持つ。--verify は 5 百分位が DARK_PERCENTILE_MIN
# 以上かも確かめる。
#
# 目標の色（線形）: 輝度 0.17・R ÷ G 2.1・G ÷ B 1.5。
#   - 色味: 手本の写真 ground_02 の日向の赤い砂岩の落石は R ÷ G 2.0〜2.1・G ÷ B 1.2〜1.5 のえんじ寄りの赤褐色
#     （同じ写真の土は G ÷ B 1.7、砂は 1.35）。崖の岩肌の素材（cliff_side の較正済み、Scripts/Terrain/TerrainMaterials.py）の
#     R ÷ G 2.33・G ÷ B 2.71 は橙で、そのまま当てると落石が黄土色に写るので、色味は写真の落石に合わせる。
#   - 輝度: 同じ写真の日向の赤い落石は砂の路面の 0.57 倍・土の 1.2 倍の明るさ。地形の較正（砂 0.18・土 0.16）に当てはめると
#     0.10〜0.19 で、落石の面は割れ目と節理の影を自分で持つので上の側に置く。cliff_side の較正 0.22 より暗い
#
# 粗さ: rock_09 の ARM の粗さ（G）は平均 0.51 で、磨かれた石の値。乾いた砂岩の崖（cliff_side の粗さ 0.86）に
# そろえ、明暗の模様は半分の振れ幅で残して平均を 0.86 にする（AO の R・金属の B はそのまま）。
import argparse
import os
import sys

import numpy
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
MODEL_ROOT = os.path.join(REPO_ROOT, "Assets", "Models", "PolyHaven")
RESOLUTION = "4k"
LUMINANCE = numpy.array([0.2126, 0.7152, 0.0722])

TARGET_LUMINANCE = 0.17
TARGET_RED_OVER_GREEN = 2.1
TARGET_GREEN_OVER_BLUE = 1.5
# 元の色味の偏り（画素の色度 ÷ 平均の色度）を残す指数。0 なら目標の色一色、1 なら元の色味の偏りをそのまま残す
CHROMA_KEEP = 0.35
# 元の輝度の明暗（画素の輝度 ÷ 平均の輝度）を残す指数。1 なら元の明暗のまま
LUMINANCE_CONTRAST = 0.5
# 岩ごとの明暗の指数（無ければ LUMINANCE_CONTRAST）。rock_face_01 は層の間の割れ目の陰が深く、0.5 では暗い斑の下限を割る
LUMINANCE_CONTRAST_BY_ID = {"rock_face_01": 0.45}
# 較正後の輝度の 5 百分位の、平均に対する下限（暗い斑が残っていないか）
DARK_PERCENTILE_MIN = 0.45
# 確かめるときの許容（平均の輝度の相対誤差・色の比の相対誤差）
LUMINANCE_TOLERANCE = 0.03
RATIO_TOLERANCE = 0.05
# 粗さの目標の平均・元の模様を残す振れ幅の倍率・下限と許容
TARGET_ROUGHNESS = 0.86
ROUGHNESS_CONTRAST = 0.5
ROUGHNESS_MIN = 0.6
ROUGHNESS_TOLERANCE = 0.02

# rock_face_01・02 は崖の面に貼る塊（Scripts/Terrain/CliffScanPlacements.json が置く）。色は落石と同じ目標へ寄せる
ROCK_IDS = ["boulder_01", "rock_09", "rock_face_01", "rock_face_02"]


def srgb_to_linear(encoded):
    return numpy.where(encoded <= 0.04045, encoded / 12.92, ((encoded + 0.055) / 1.055) ** 2.4)


def load_linear(path):
    with Image.open(path) as image:
        return srgb_to_linear(numpy.asarray(image.convert("RGB"), dtype=numpy.float32) / 255.0)


def mean_color(linear):
    mean = linear.reshape(-1, 3).mean(axis=0, dtype=numpy.float64)
    return mean, float(mean @ LUMINANCE)


def target_color():
    # G を 1 とした色を、輝度が目標になるよう縮める
    unit = numpy.array([TARGET_RED_OVER_GREEN, 1.0, 1.0 / TARGET_GREEN_OVER_BLUE])
    return unit * (TARGET_LUMINANCE / float(unit @ LUMINANCE))


def limit_to_one(scaled):
    # 最大の成分が 1 を超える画素は、画素ごとに最大の成分で割って 1 に収める（色相を保ち、白く飛ばさない）
    peak = numpy.max(scaled, axis=2, keepdims=True)
    return numpy.where(peak > 1.0, scaled / numpy.maximum(peak, 1.0e-6), scaled)


def source_path(rock_id):
    return os.path.join(MODEL_ROOT, rock_id, "textures", "%s_diff_%s.jpg" % (rock_id, RESOLUTION))


def calibrated_path(rock_id):
    return os.path.join(MODEL_ROOT, rock_id, "textures", "%s_diff_lin_%s.png" % (rock_id, RESOLUTION))


def load_linear_encoded(path):
    # 線形の値のまま書いた PNG を読む
    with Image.open(path) as image:
        return numpy.asarray(image.convert("RGB"), dtype=numpy.float32) / 255.0


def arm_source_path(rock_id):
    return os.path.join(MODEL_ROOT, rock_id, "textures", "%s_arm_%s.jpg" % (rock_id, RESOLUTION))


def arm_calibrated_path(rock_id):
    return os.path.join(MODEL_ROOT, rock_id, "textures", "%s_arm_cal_%s.png" % (rock_id, RESOLUTION))


def load_arm(path):
    with Image.open(path) as image:
        return numpy.asarray(image.convert("RGB"), dtype=numpy.float32) / 255.0


def calibrate_arm(arm):
    # 粗さは線形の値のまま扱う（ARM は色ではない）
    roughness = arm[..., 1]
    adjusted = TARGET_ROUGHNESS + (roughness - float(roughness.mean())) * ROUGHNESS_CONTRAST
    for _ in range(4):
        adjusted += TARGET_ROUGHNESS - float(numpy.clip(adjusted, ROUGHNESS_MIN, 1.0).mean())
    out = arm.copy()
    out[..., 1] = numpy.clip(adjusted, ROUGHNESS_MIN, 1.0)
    return numpy.clip(out * 255.0 + 0.5, 0.0, 255.0).astype(numpy.uint8)


def check_arm(rock_id, arm):
    roughness = float(arm[..., 1].mean())
    print("%s arm_calibrated roughness=%.3f" % (rock_id, roughness))
    if abs(roughness - TARGET_ROUGHNESS) > ROUGHNESS_TOLERANCE:
        print("FAIL %s 較正した粗さの平均が目標 %.2f を外れています" % (rock_id, TARGET_ROUGHNESS))
        return False
    return True


def calibrate(linear, contrast=LUMINANCE_CONTRAST):
    mean, _ = mean_color(linear)
    pixel_luminance = numpy.maximum(linear @ LUMINANCE.astype(numpy.float32), 1.0e-5)[..., None]
    # 色度（輝度で割った色）の、平均の色度に対する偏り
    mean_chroma = mean / float(mean @ LUMINANCE)
    deviation = numpy.clip((linear / pixel_luminance) / mean_chroma, 0.05, 20.0)
    target = target_color()
    target_chroma = target / TARGET_LUMINANCE
    chroma = target_chroma * numpy.power(deviation, CHROMA_KEEP)
    # 偏りを掛けた色度の輝度を 1 に戻し、画素の輝度の明暗は contrast の指数で縮めて残す
    chroma /= numpy.maximum(chroma @ LUMINANCE.astype(numpy.float32), 1.0e-5)[..., None]
    mean_luminance = float(mean @ LUMINANCE)
    shaped = chroma * (mean_luminance * numpy.power(pixel_luminance / mean_luminance, contrast))
    # 1 に収めた分だけ平均が下がるので、収めた後の平均が目標になるまで倍率を詰める
    _, shaped_luminance = mean_color(shaped)
    scale = TARGET_LUMINANCE / shaped_luminance
    for _ in range(4):
        _, limited_luminance = mean_color(limit_to_one(shaped * scale))
        scale *= TARGET_LUMINANCE / limited_luminance
    scaled = shaped * scale
    clipped = float(numpy.mean(numpy.max(scaled, axis=2) > 1.0))
    encoded = numpy.clip(limit_to_one(scaled) * 255.0 + 0.5, 0.0, 255.0).astype(numpy.uint8)
    return encoded, clipped


def describe(rock_id, label, linear):
    mean, luminance = mean_color(linear)
    print("%s %s mean=(%.4f, %.4f, %.4f) Y=%.4f R/G=%.2f G/B=%.2f" %
          (rock_id, label, mean[0], mean[1], mean[2], luminance, mean[0] / mean[1], mean[1] / mean[2]))
    return mean, luminance


def check(rock_id, linear):
    mean, luminance = describe(rock_id, "calibrated", linear)
    ok = abs(luminance / TARGET_LUMINANCE - 1.0) <= LUMINANCE_TOLERANCE
    ok = ok and abs((mean[0] / mean[1]) / TARGET_RED_OVER_GREEN - 1.0) <= RATIO_TOLERANCE
    ok = ok and abs((mean[1] / mean[2]) / TARGET_GREEN_OVER_BLUE - 1.0) <= RATIO_TOLERANCE
    if not ok:
        print("FAIL %s 較正した平均の色が目標（Y %.2f・R/G %.2f・G/B %.2f）を外れています" %
              (rock_id, TARGET_LUMINANCE, TARGET_RED_OVER_GREEN, TARGET_GREEN_OVER_BLUE))
    dark = float(numpy.percentile(linear @ LUMINANCE.astype(numpy.float32), 5.0)) / luminance
    print("%s calibrated dark_p5_over_mean=%.2f" % (rock_id, dark))
    if dark < DARK_PERCENTILE_MIN:
        print("FAIL %s 較正した輝度の 5 百分位が平均の %.2f 倍を下回り、暗い斑が残っています" % (rock_id, DARK_PERCENTILE_MIN))
        ok = False
    return ok


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    ok = True
    for rock_id in ROCK_IDS:
        if args.verify:
            path = calibrated_path(rock_id)
            if not os.path.exists(path):
                print("FAIL %s がありません（--verify を付けずに先に書く）" % os.path.relpath(path, REPO_ROOT))
                ok = False
                continue
            ok = check(rock_id, load_linear_encoded(path)) and ok
            arm_path = arm_calibrated_path(rock_id)
            if not os.path.exists(arm_path):
                print("FAIL %s がありません（--verify を付けずに先に書く）" % os.path.relpath(arm_path, REPO_ROOT))
                ok = False
                continue
            ok = check_arm(rock_id, load_arm(arm_path)) and ok
            continue
        path = source_path(rock_id)
        if not os.path.exists(path):
            print("FAIL %s がありません（Scripts/FetchPolyHavenModels.ps1 -Set wasteland で落とす）" %
                  os.path.relpath(path, REPO_ROOT))
            ok = False
            continue
        linear = load_linear(path)
        describe(rock_id, "source", linear)
        encoded, clipped = calibrate(linear, LUMINANCE_CONTRAST_BY_ID.get(rock_id, LUMINANCE_CONTRAST))
        out_path = calibrated_path(rock_id)
        Image.fromarray(encoded, "RGB").save(out_path)
        print("wrote %s clipped=%.4f" % (os.path.relpath(out_path, REPO_ROOT), clipped))
        ok = check(rock_id, encoded.astype(numpy.float32) / 255.0) and ok
        arm_path = arm_source_path(rock_id)
        if not os.path.exists(arm_path):
            print("FAIL %s がありません" % os.path.relpath(arm_path, REPO_ROOT))
            ok = False
            continue
        arm = load_arm(arm_path)
        print("%s arm_source roughness=%.3f" % (rock_id, float(arm[..., 1].mean())))
        arm_encoded = calibrate_arm(arm)
        arm_out = arm_calibrated_path(rock_id)
        Image.fromarray(arm_encoded, "RGB").save(arm_out)
        print("wrote %s" % os.path.relpath(arm_out, REPO_ROOT))
        ok = check_arm(rock_id, arm_encoded.astype(numpy.float32) / 255.0) and ok
    print("CALIBRATE_SCAN_ROCK result=%s" % ("pass" if ok else "fail"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
