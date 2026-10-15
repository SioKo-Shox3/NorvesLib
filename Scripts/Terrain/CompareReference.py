# 荒野の手本の写真と、写真の構図に寄せた名前付き視点（SceneCatalog.h の ref_*）の撮影を左右に並べた比較画像を書く。
# 以後の荒野の項目は、この視点と比較画像で判断する（Docs/RenderingValidation/WastelandReference.md の「構図を合わせた比較」）。
#
# 使い方:
#   python Scripts/Terrain/CompareReference.py --capture <CaptureScene.ps1 の OutDir> [--photos far_01,cliff_01] [--out <出力先>]
#                                              [--verify] [--note <一覧の先頭の添え書き>]
# 写真ごとに <出力先>/reference_<写真>.png と <出力先>/reference_metrics.json を書き、数値を 1 行ずつ出す。
# 並べた写真をすべて縦に積んだ一覧（左に写真、右に撮影。輪郭と枠は重ねない）を <出力先>/reference_overview.png に書く
# （節目の確認でユーザーに渡す 1 枚）。
#
# 並べ方: 撮影は 16:9 で、横の画角を写真の横の画角に合わせてある（WastelandRoutine.cpp の kReferenceShots）。写真は横幅を
# 撮影にそろえ、16:9 より縦に長ければ上下の中央を切り、短ければ上下に帯を足す（どちらも同じ視野の範囲を並べる）。
# 写真の上には撮影の地形の輪郭（黄）、撮影の上には写真の地形の輪郭（水色）を重ね、配置と輪郭の対応を見る。
#
# 領域ごとの値（画素は sRGB を線形に戻した値。Y = 0.2126R + 0.7152G + 0.0722B）:
#   - 空: 地形の輪郭より上の画素（赤み（R - B > 15）のある画素か、列の上端の空の 0.6 倍より暗い画素が 4 行続いた所から下を地形とする。
#     陰に入った崖は灰色で赤みが無いので、明るさの判定を足してある）
#   - 崖: PHOTOS の崖の枠の中の地形の画素（主役のビュート・メサの壁と崖錐）
#   - 地面: PHOTOS の地面の枠の中の地形の画素（近景の谷床）
#   それぞれ線形の RGB の平均と Y を写真と撮影の両方で書く。枠は写真の幅・高さに対する割合で、撮影には同じ視野の位置に置く。
#   地表の材質の平均のアルベド: 地形の 3 層（砂・乾いた土・岩肌）のアルベドのテクスチャの線形の平均に、
#   WastelandRoutine.cpp の kTerrainAlbedoScale を掛けた値。
# 写真の加工とホワイトバランスは分からないので、値の一致を合否にしない（差の向きと大きさを見るためだけに使う）。
#
# --verify: 写真・撮影がそろい、撮影が 1920×1080 以上で、比較画像を書けたかを確かめる（数値の一致は見ない）。
# 終了コード: 書けたら 0、写真・撮影が無い・小さいときは 1。
import argparse
import json
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

from CompareLight import GROUND_RUN_ROWS, load_font, srgb_to_linear

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PHOTO_DIR = os.path.join(REPO_ROOT, "Assets", "Reference", "ValleyOfTheGods")
ROUTINE_PATH = os.path.join(REPO_ROOT, "Game", "GameModes", "Wasteland", "WastelandRoutine.cpp")
CATALOG_PATH = os.path.join(REPO_ROOT, "Game", "Scenes", "SceneCatalog.h")

# 写真と視点の対応と、崖・地面の枠（元の写真の幅・高さに対する割合 x0, y0, x1, y1）。outline は輪郭の扱い:
#   match = 切り出した範囲の中の主役の配置と輪郭を合わせる、horizon = 主役が範囲の外なので遠景（SCN-WL-HORIZON）で合わせる、
#   none = 地表と植生の比較用で輪郭は見ない
PHOTOS = [
    {"id": "far_01", "view": "ref_far_01", "outline": "match", "cliff": (0.30, 0.42, 0.68, 0.62), "ground": (0.0, 0.72, 1.0, 0.92)},
    {"id": "far_02", "view": "ref_far_02", "outline": "match", "cliff": (0.50, 0.28, 0.90, 0.55), "ground": (0.0, 0.65, 1.0, 0.87)},
    {"id": "cliff_01", "view": "ref_cliff_01", "outline": "match", "cliff": (0.22, 0.32, 0.82, 0.78), "ground": (0.0, 0.82, 1.0, 0.91)},
    {"id": "cliff_02", "view": "ref_cliff_02", "outline": "match", "cliff": (0.38, 0.32, 0.72, 0.78), "ground": (0.0, 0.82, 0.70, 0.97)},
    {"id": "ground_01", "view": "ref_ground_01", "outline": "none", "cliff": (0.0, 0.46, 0.70, 0.53), "ground": (0.0, 0.62, 1.0, 0.87)},
    {"id": "ground_02", "view": "ref_ground_02", "outline": "match", "cliff": (0.70, 0.33, 0.98, 0.58), "ground": (0.35, 0.75, 0.95, 0.92)},
]
OUTLINE_TEXT = {
    "match": "輪郭: 主役の配置と輪郭を合わせる",
    "none": "輪郭: 見ない（地表と植生の比較用）",
}
# 地形の素材の名前と、アルベドのテクスチャのパスを持つ定数（WastelandRoutine.cpp から読む）と、横 2 枚の並びの枠
# （0 = 左・1 = 右。None は 1 枚のテクスチャ。土の枠は左が礫の舗石・右が皮膜の付いた土の並び）
TERRAIN_ALBEDO = {
    "赤茶の土": ("kSandAlbedoPath", None),
    "礫の舗石": ("kDirtAlbedoPath", 0),
    "皮膜": ("kDirtAlbedoPath", 1),
    "砂岩": ("kRockAlbedoPath", None),
}
# 品質を見る撮影の最小の大きさ（1280×720 では粗さが判断を狂わせる）
MIN_CAPTURE_SIZE = (1920, 1080)
# 並べる絵の大きさ[px]（16:9）
PANEL_SIZE = (1280, 720)
# 地形の輪郭: 列の上端の何割の行の明るさの中央値を空の基準にし、その何倍より暗い画素を地形とするか。輪郭をならす列数
SKY_REFERENCE_ROWS = 0.05
TERRAIN_DARK_RATIO = 0.6
# 赤みの判定の R - B（sRGB）。地平線の近くの暖かい霞の空（12 前後）を外し、霞の中の淡いビュート（20 以上）は残す
TERRAIN_RED_MINUS_BLUE = 15
OUTLINE_MEDIAN_COLUMNS = 9
# 一覧の 1 枚の絵の大きさ[px]（16:9）と、行ごとの見出しの高さ[px]
OVERVIEW_PANEL_SIZE = (960, 540)
OVERVIEW_LABEL_HEIGHT = 34
REGIONS = (("sky", "空"), ("cliff", "崖"), ("ground", "地面"))
REGION_COLORS = {"cliff": (255, 140, 0), "ground": (0, 200, 80)}


def fit_photo(photo):
    """写真の横幅を絵にそろえ、上下を切るか帯を足して 16:9 にする。元の写真の y の割合を絵の y の割合へ移す関数も返す"""
    width, height = PANEL_SIZE
    scaled_height = int(round(photo.height * width / photo.width))
    scaled = photo.resize((width, scaled_height), Image.LANCZOS)
    offset = (scaled_height - height) // 2
    panel = Image.new("RGB", PANEL_SIZE, (0, 0, 0))
    if offset >= 0:
        panel.paste(scaled.crop((0, offset, width, offset + height)), (0, 0))
    else:
        panel.paste(scaled, (0, -offset))

    def map_y(fraction):
        return min(max((fraction * scaled_height - offset) / height, 0.0), 1.0)

    return panel, (offset < 0, -offset if offset < 0 else 0), map_y


def find_terrain_top(rgb):
    """列ごとの地形の始まりの行（見つからない列は画像の高さ）。CompareLight.py の find_horizon の赤みの判定（閾値は上げた）に、
    列の上端の空より暗い画素を足す（陰に入った崖は灰色で赤みが無い）。輪郭の形を残すため、ならしの列数は小さくする"""
    height, width, _ = rgb.shape
    values = rgb.astype(np.int32)
    luminance = values @ np.array([2126, 7152, 722]) // 10000
    sky_reference = np.median(luminance[: max(1, int(height * SKY_REFERENCE_ROWS))], axis=0)
    terrain = ((values[:, :, 0] - values[:, :, 2]) > TERRAIN_RED_MINUS_BLUE) | (luminance < sky_reference[None, :] * TERRAIN_DARK_RATIO)
    run = np.ones_like(terrain)
    for offset in range(GROUND_RUN_ROWS):
        shifted = np.zeros_like(terrain)
        shifted[: height - offset] = terrain[offset:]
        run &= shifted
    top = np.where(run.any(axis=0), run.argmax(axis=0), height)
    half = OUTLINE_MEDIAN_COLUMNS // 2
    windows = np.lib.stride_tricks.sliding_window_view(np.pad(top, half, mode="edge"), OUTLINE_MEDIAN_COLUMNS)
    return np.median(windows, axis=1).astype(np.int32)


def box_mask(shape, box):
    height, width = shape
    x0, y0, x1, y1 = box
    mask = np.zeros(shape, dtype=bool)
    mask[int(round(y0 * height)) : int(round(y1 * height)), int(round(x0 * width)) : int(round(x1 * width))] = True
    return mask


def region_stats(linear, mask):
    """領域の線形の RGB の平均と Y（画素が無ければ空）"""
    if not mask.any():
        return {"pixels": 0, "rgb": None, "luminance": None}
    rgb = linear[mask].mean(axis=0)
    return {
        "pixels": int(mask.sum()),
        "rgb": [float(v) for v in rgb],
        "luminance": float(rgb @ np.array([0.2126, 0.7152, 0.0722])),
    }


def measure(panel, boxes, valid):
    """空・崖・地面の領域と、その平均の色と明るさ"""
    rgb = np.asarray(panel)
    linear = srgb_to_linear(rgb.astype(np.float64))
    horizon = find_terrain_top(rgb)
    rows = np.arange(rgb.shape[0])[:, None]
    sky = (rows < horizon[None, :]) & valid
    terrain = ~sky & valid
    masks = {
        "sky": sky,
        "cliff": terrain & box_mask(sky.shape, boxes["cliff"]),
        "ground": terrain & box_mask(sky.shape, boxes["ground"]),
    }
    return {name: region_stats(linear, mask) for name, mask in masks.items()}, horizon


def terrain_albedo_scale():
    with open(ROUTINE_PATH, encoding="utf-8-sig") as file:
        match = re.search(r"kTerrainAlbedoScale\s*=\s*([0-9.]+)f", file.read())
    return float(match.group(1)) if match else 1.0


def terrain_albedo():
    """地形の素材（TERRAIN_ALBEDO）のアルベドの線形の平均（kTerrainAlbedoScale を掛けた値）"""
    scale = terrain_albedo_scale()
    with open(ROUTINE_PATH, encoding="utf-8-sig") as file:
        routine = file.read()
    result = {"scale": scale, "layers": {}}
    for name, (constant, half) in TERRAIN_ALBEDO.items():
        match = re.search(constant + r'\s*=\s*"([^"]+)"', routine)
        full = os.path.join(REPO_ROOT, match.group(1)) if match else ""
        if not os.path.isfile(full):
            result["layers"][name] = None
            continue
        # 符号化した値のまま平均すると線形の平均より明るく出るので、間引いた画素を線形へ戻してから平均する
        image = Image.open(full).convert("RGB")
        if half is not None:
            # 横 2 枚の並びなら枠の側だけ（並びでなければ素材が無いものとして扱う）
            if image.width != image.height * 2:
                result["layers"][name] = None
                continue
            image = image.crop((half * image.height, 0, (half + 1) * image.height, image.height))
        image = image.resize((1024, 1024), Image.NEAREST)
        rgb = srgb_to_linear(np.asarray(image).astype(np.float64)).reshape(-1, 3).mean(axis=0) * scale
        result["layers"][name] = {
            "rgb": [float(v) for v in rgb],
            "luminance": float(rgb @ np.array([0.2126, 0.7152, 0.0722])),
        }
    return result


def draw_outline(image, horizon, color):
    draw = ImageDraw.Draw(image)
    points = [(x, int(y)) for x, y in enumerate(horizon) if y < image.height]
    for start in range(len(points) - 1):
        if abs(points[start][1] - points[start + 1][1]) < 40:
            draw.line([points[start], points[start + 1]], fill=color, width=2)


def draw_boxes(image, boxes):
    draw = ImageDraw.Draw(image)
    for name, color in REGION_COLORS.items():
        x0, y0, x1, y1 = boxes[name]
        draw.rectangle(
            [x0 * image.width, y0 * image.height, x1 * image.width - 1, y1 * image.height - 1], outline=color, width=2
        )


def format_region(label, stats):
    if stats["rgb"] is None:
        return f"{label} －"
    r, g, b = stats["rgb"]
    return f"{label} Y {stats['luminance']:.3f}（R {r:.3f} G {g:.3f} B {b:.3f}）"


def describe(label, metrics):
    """ログの 1 行（機械が拾うキーは英語）"""
    parts = [label]
    for name, _ in REGIONS:
        stats = metrics[name]
        parts.append(f"{name}_y=" + ("nan" if stats["luminance"] is None else f"{stats['luminance']:.3f}"))
    return " ".join(parts)


def write_overview(rows, path, font, note=""):
    """写真｜撮影の組を縦に積んだ一覧を書く。rows は (見出し, 写真の絵, 撮影の絵) の並び。note は一覧の先頭に書く添え書き"""
    width, height = OVERVIEW_PANEL_SIZE
    row_height = OVERVIEW_LABEL_HEIGHT + height
    note_height = OVERVIEW_LABEL_HEIGHT if note else 0
    sheet = Image.new("RGB", (width * 2, note_height + row_height * len(rows)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    if note:
        draw.text((10, 6), note, fill=(255, 210, 120), font=font)
    for index, (label, photo, capture) in enumerate(rows):
        top = note_height + index * row_height
        draw.text((10, top + 6), label, fill=(235, 235, 235), font=font)
        sheet.paste(photo.resize(OVERVIEW_PANEL_SIZE, Image.LANCZOS), (0, top + OVERVIEW_LABEL_HEIGHT))
        sheet.paste(capture.resize(OVERVIEW_PANEL_SIZE, Image.LANCZOS), (width, top + OVERVIEW_LABEL_HEIGHT))
    sheet.save(path)


def catalog_views():
    with open(CATALOG_PATH, encoding="utf-8-sig") as file:
        return set(re.findall(r'\{"(ref_[a-z0-9_]+)"', file.read()))


def main():
    parser = argparse.ArgumentParser(description="荒野の手本の写真と、同じ構図に寄せた視点の撮影を並べて比べる")
    parser.add_argument("--capture", required=True, help="CaptureScene.ps1 の OutDir（<視点>.png がある）")
    parser.add_argument("--photos", default="", help="比べる写真の ID（カンマ区切り。省くとすべて）")
    parser.add_argument("--out", default="", help="出力先（省くと --capture）")
    parser.add_argument("--verify", action="store_true", help="写真・撮影がそろい、撮影が 1920×1080 以上かを確かめる")
    parser.add_argument("--note", default="", help="一覧の先頭に書く添え書き（節目の確認で渡すときの注記）")
    args = parser.parse_args()

    wanted = [p for p in args.photos.split(",") if p]
    photos = [p for p in PHOTOS if not wanted or p["id"] in wanted]
    out_dir = args.out or args.capture
    os.makedirs(out_dir, exist_ok=True)
    failures = []
    views = catalog_views()
    albedo = terrain_albedo()
    albedo_text = "地表の材質の平均のアルベド（線形、×{:.2f}）　".format(albedo["scale"]) + "　".join(
        f"{name} Y {layer['luminance']:.3f}（R {layer['rgb'][0]:.3f} G {layer['rgb'][1]:.3f} B {layer['rgb'][2]:.3f}）"
        if layer
        else f"{name} －"
        for name, layer in albedo["layers"].items()
    )
    print("COMPARE_REFERENCE albedo " + " ".join(
        f"{name}={layer['luminance']:.3f}" if layer else f"{name}=missing" for name, layer in albedo["layers"].items()
    ) + f" scale={albedo['scale']:.2f}")
    report = {"albedo": albedo, "photos": {}}
    font = load_font(18)
    overview_rows = []

    for entry in photos:
        photo_path = os.path.join(PHOTO_DIR, entry["id"] + ".jpg")
        capture_path = os.path.join(args.capture, entry["view"] + ".png")
        if entry["view"] not in views:
            failures.append(f"視点 {entry['view']} が SceneCatalog.h に無い")
        if not os.path.isfile(photo_path):
            failures.append(f"写真が無い: {photo_path}")
            continue
        if not os.path.isfile(capture_path):
            failures.append(f"撮影が無い: {capture_path}")
            continue
        capture_full = Image.open(capture_path).convert("RGB")
        if capture_full.width < MIN_CAPTURE_SIZE[0] or capture_full.height < MIN_CAPTURE_SIZE[1]:
            failures.append(f"撮影が小さい: {capture_path}（{capture_full.width}×{capture_full.height}）")

        photo_panel, (bLetterbox, band), map_y = fit_photo(Image.open(photo_path).convert("RGB"))
        boxes = {name: (box[0], map_y(box[1]), box[2], map_y(box[3])) for name, box in entry.items() if name in REGION_COLORS}
        valid = np.ones((PANEL_SIZE[1], PANEL_SIZE[0]), dtype=bool)
        if bLetterbox:
            valid[:band] = False
            valid[PANEL_SIZE[1] - band :] = False
        capture_panel = capture_full.resize(PANEL_SIZE, Image.LANCZOS)
        photo_metrics, photo_horizon = measure(photo_panel, boxes, valid)
        capture_metrics, capture_horizon = measure(capture_panel, boxes, np.ones_like(valid))
        report["photos"][entry["id"]] = {
            "view": entry["view"],
            "outline": entry["outline"],
            "capture_size": [capture_full.width, capture_full.height],
            "boxes": boxes,
            "photo": photo_metrics,
            "capture": capture_metrics,
        }
        print("COMPARE_REFERENCE " + describe(f"photo={entry['id']}", photo_metrics))
        print("COMPARE_REFERENCE " + describe(f"view={entry['view']}", capture_metrics))
        overview_rows.append(
            (f"左: 写真 {entry['id']}　右: 撮影 {entry['view']}（{OUTLINE_TEXT[entry['outline']]}）", photo_panel, capture_panel)
        )

        left = photo_panel.copy()
        right = capture_panel.copy()
        draw_outline(left, capture_horizon, (255, 230, 0))
        draw_outline(right, photo_horizon, (0, 230, 255))
        draw_boxes(left, boxes)
        draw_boxes(right, boxes)
        text_height = 140
        sheet = Image.new("RGB", (PANEL_SIZE[0] * 2, PANEL_SIZE[1] + text_height), (24, 24, 24))
        sheet.paste(left, (0, 0))
        sheet.paste(right, (PANEL_SIZE[0], 0))
        draw = ImageDraw.Draw(sheet)
        lines = [
            f"写真 {entry['id']}（黄: 撮影の地形の輪郭）　" + "　".join(format_region(l, photo_metrics[n]) for n, l in REGIONS),
            f"撮影 {entry['view']}（水色: 写真の地形の輪郭・{capture_full.width}×{capture_full.height}）　"
            + "　".join(format_region(l, capture_metrics[n]) for n, l in REGIONS),
            albedo_text,
            OUTLINE_TEXT[entry["outline"]]
            + "　枠: 橙 = 崖、緑 = 地面。値は線形の平均。写真の加工とホワイトバランスは分からないので、差の向きと大きさだけを見る",
        ]
        for index, line in enumerate(lines):
            draw.text((10, PANEL_SIZE[1] + 6 + index * 32), line, fill=(235, 235, 235), font=font)
        sheet.save(os.path.join(out_dir, f"reference_{entry['id']}.png"))

    with open(os.path.join(out_dir, "reference_metrics.json"), "w", encoding="utf-8") as file:
        json.dump(report, file, ensure_ascii=False, indent=2)
    if overview_rows:
        write_overview(overview_rows, os.path.join(out_dir, "reference_overview.png"), font, args.note)
    else:
        failures.append("一覧に並べる組が無い")
    if failures:
        for failure in failures:
            print(f"COMPARE_REFERENCE failure={failure}")
        print(f"COMPARE_REFERENCE result=fail failures={len(failures)}")
        return 1
    print(f"COMPARE_REFERENCE result=pass photos={len(photos)} out={out_dir}" + (" verify=1" if args.verify else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
