# 荒野の手本の写真と撮影の写りの差を、写真ごとの固定の領域（LookRegions.json の多角形）で診断する（CompareReference.py --look）。
# 確認 2 の差（地面と崖が写真より明るい・晴天の空が白い・遠景の霞が桃色）を、カメラの写り・物の明るさ・空と霞の入力に分けるための値を出す。
#
# 領域: 青空（雲を除く）・雲・近景の日なたの地面・崖・3 km より先の地形の帯。写真に写っていない領域は注釈の absent に理由を書く。
# 座標は 16:9 に合わせた絵（CompareReference.fit_photo）の割合で、写真と撮影の両方に同じ多角形を重ねる（構図が合っていない視点は
# capture_polygon で撮影の側だけ置き直す）。
#
# 値（領域ごと。写真・撮影の PNG・撮影の HDR の 3 つ）:
#   - 線形の平均の Y（= 0.2126R + 0.7152G + 0.0722B）と色度（R ÷ G・B ÷ R。平均の RGB から）
#   - 白飛び（sRGB 8 bit のどれかのチャンネルが 254 以上）と黒つぶれ（すべてのチャンネルが 4 以下）の割合。HDR には無い
#   - 地面 Y ÷ 青空 Y、崖 Y ÷ 地面 Y、遠景の帯と近景の地面の色度の差（帯 − 地面）
#   写真と撮影の PNG は sRGB を線形に戻した表示の値で、写真には撮影時のトーンカーブ・ホワイトバランス・局所補正が残る。
#   写真との比は診断の値で、合否にしない（表に「写真の処理を含む」と書く）。撮影の HDR は表示の変換の前の放射輝度（cd/m²）。
# 領域の向き（撮影の視点のカメラ〔SceneCatalog.h の Yaw・Pitch と WastelandRoutine.cpp の kReferenceShots の縦の画角〕から）:
#   仰角の範囲[度]と、太陽（kReferenceShots の高度・方位。写真の日時に合わせた値）からの角距離の最小・平均[度]。
#   写真の多角形と撮影の多角形（capture_polygon）で別々に求める。写真のカメラの向き・画角は記録が無いので、写真の側は
#   撮影の視点のカメラで近似した値（構図の合っていない ground_01 では写真の水平線の位置が違い、粗い近似）。
#
# --look-depth <撮影の出力先> を渡すと、そこの <視点>.aov.depth.pfm（-Aov の撮影）で撮影の領域の深度の中央値と 3 km より先の割合を書く。
# 出力: <出力先>/look_<写真>.png（上: 写真、下: 撮影。注釈を重ねる）、look_metrics.json、look_table.md（文書に貼る表）。
# --verify: 選んだ写真（--photos。省くと 6 写真）すべてで、各領域が写真・撮影の両方で空でないか、absent に理由があるかと、
# 撮影の PNG と HDR（-Hdr）がそろい 1920×1080 以上で同じ大きさかを確かめる。
# --expect <仕様>: look_metrics.json の写真の記録の量の行の合否（PhotoExpect.py）。-Hdr を付けずに撮った出力先（metrics.json の hdr が false。ブルームと AA を入れた
# 見た目の確認の撮影）は HDR を求めず、撮影 HDR の値を「－」にする。終了コード: 満たせば 0、満たさなければ 1。
import json
import math
import os
import re

import numpy as np
from PIL import Image, ImageDraw

from CompareLight import load_font, srgb_to_linear
from CompareReference import CATALOG_PATH, MIN_CAPTURE_SIZE, PANEL_SIZE, PHOTO_DIR, PHOTOS, ROUTINE_PATH, fit_photo
from DisplayTransform import read_pfm
from ReferenceCheck import read_pfm_any

REGIONS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "LookRegions.json")
LUMA = np.array([0.2126, 0.7152, 0.0722])
# 白飛び・黒つぶれの判定（sRGB 8 bit）
CLIP_CODE = 254
CRUSH_CODE = 4
# 遠景の帯の深度の下限[m]
FAR_BAND_MIN_DEPTH = 3000.0
REGION_COLORS = {
    "blue_sky": (0, 160, 255),
    "cloud": (255, 255, 255),
    "ground": (0, 220, 80),
    "cliff": (255, 140, 0),
    "far_band": (255, 0, 220),
}
SOURCES = (("photo", "写真"), ("png", "撮影 PNG"), ("hdr", "撮影 HDR"))
PHOTO_GEOMETRY_NOTE = (
    "写真の角度は、写真のカメラの記録が無いので撮影の視点のカメラで近似した値"
    "（構図の合っていない ground_01 では写真の水平線の位置が違い、粗い近似）"
)


def load_regions():
    with open(REGIONS_PATH, encoding="utf-8-sig") as file:
        return json.load(file)


def view_cameras():
    """視点の名前 → (yaw, pitch, 縦の画角, 太陽の高度, 太陽の方位)[度]"""
    with open(CATALOG_PATH, encoding="utf-8-sig") as file:
        catalog = file.read()
    with open(ROUTINE_PATH, encoding="utf-8-sig") as file:
        routine = file.read()
    constants = {name: float(value) for name, value in re.findall(r"constexpr float (k\w+)\s*=\s*(-?[0-9.]+)f;", routine)}
    cameras = {}
    number = r"\s*(-?[0-9.]+)f\s*"
    for name, yaw, pitch in re.findall(r'\{"(ref_[a-z0-9_]+)",\s*SceneViewKind::FreePose,' + number + "," + number + ",", catalog):
        cameras[name] = {"yaw": float(yaw), "pitch": float(pitch)}
    for name, vfov, altitude, azimuth in re.findall(r'\{"(ref_[a-z0-9_]+)",\s*([0-9.]+)f,\s*([\w.]+?)f?,\s*([\w.]+?)f?\}', routine):
        if name in cameras:
            cameras[name].update(
                vfov=float(vfov),
                sun_altitude=constants[altitude] if altitude in constants else float(altitude),
                sun_azimuth=constants[azimuth] if azimuth in constants else float(azimuth),
            )
    return cameras


def view_directions(camera, size):
    """画素の中心の視線の向き（単位ベクトル、(高さ, 幅, 3)）と太陽の向き。エンジンの FreePose と同じ向き
    （前 = (−sin yaw cos p, sin p, cos yaw cos p)、右 = (−cos yaw, 0, −sin yaw)、上 = 右 × 前）"""
    width, height = size
    yaw = math.radians(camera["yaw"])
    pitch = math.radians(camera["pitch"])
    forward = np.array([-math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch)])
    right = np.array([-math.cos(yaw), 0.0, -math.sin(yaw)])
    up = np.cross(right, forward)
    tan_half = math.tan(math.radians(camera["vfov"]) * 0.5)
    aspect = width / height
    x = ((np.arange(width) + 0.5) / width * 2.0 - 1.0) * tan_half * aspect
    y = (1.0 - (np.arange(height) + 0.5) / height * 2.0) * tan_half
    directions = forward[None, None, :] + x[None, :, None] * right[None, None, :] + y[:, None, None] * up[None, None, :]
    directions /= np.linalg.norm(directions, axis=2, keepdims=True)
    altitude = math.radians(camera["sun_altitude"])
    azimuth = math.radians(camera["sun_azimuth"])
    sun = np.array([math.cos(altitude) * math.cos(azimuth), math.sin(altitude), math.cos(altitude) * math.sin(azimuth)])
    return directions, sun


def polygon_mask(polygon, size):
    width, height = size
    image = Image.new("L", size, 0)
    ImageDraw.Draw(image).polygon([(x * width, y * height) for x, y in polygon], fill=255)
    return np.asarray(image) > 0


def color_stats(linear, mask, codes=None):
    """領域の線形の平均の Y・色度と、8 bit の符号値があれば白飛び・黒つぶれの割合"""
    if not mask.any():
        return {"pixels": 0}
    rgb = linear[mask].astype(np.float64).mean(axis=0)
    stats = {
        "pixels": int(mask.sum()),
        "rgb": [float(v) for v in rgb],
        "luminance": float(rgb @ LUMA),
        "r_over_g": float(rgb[0] / rgb[1]) if rgb[1] > 0 else None,
        "b_over_r": float(rgb[2] / rgb[0]) if rgb[0] > 0 else None,
    }
    if codes is not None:
        values = codes[mask]
        stats["clipped"] = float((values.max(axis=1) >= CLIP_CODE).mean())
        stats["crushed"] = float((values.max(axis=1) <= CRUSH_CODE).mean())
    return stats


def geometry_stats(directions, sun, mask):
    """領域の仰角の範囲と太陽からの角距離[度]"""
    if not mask.any():
        return None
    d = directions[mask]
    elevation = np.degrees(np.arcsin(np.clip(d[:, 1], -1.0, 1.0)))
    sun_angle = np.degrees(np.arccos(np.clip(d @ sun, -1.0, 1.0)))
    return {
        "elevation_min": float(elevation.min()),
        "elevation_max": float(elevation.max()),
        "sun_angle_min": float(sun_angle.min()),
        "sun_angle_mean": float(sun_angle.mean()),
    }


def ratio(numerator, denominator):
    if not numerator or not denominator or numerator.get("pixels", 0) == 0 or denominator.get("pixels", 0) == 0:
        return None
    return numerator["luminance"] / denominator["luminance"] if denominator["luminance"] > 0 else None


def chroma_difference(band, ground):
    if not band or not ground or band.get("pixels", 0) == 0 or ground.get("pixels", 0) == 0:
        return None
    return {
        "r_over_g": band["r_over_g"] - ground["r_over_g"],
        "b_over_r": band["b_over_r"] - ground["b_over_r"],
    }


def draw_regions(image, polygons, font):
    draw = ImageDraw.Draw(image)
    for name, polygon in polygons.items():
        points = [(x * image.width, y * image.height) for x, y in polygon]
        draw.line(points + [points[0]], fill=REGION_COLORS[name], width=3)
        draw.text((points[0][0] + 4, points[0][1] + 2), name, fill=REGION_COLORS[name], font=font)


def fmt(value, digits=3):
    return "－" if value is None else f"{value:.{digits}f}"


def write_table(report, region_labels, path):
    """文書に貼る表（Markdown）"""
    lines = [
        "| 写真 | 領域 | 仰角[度] 写真※ / 撮影 | 太陽からの角距離 最小・平均[度] 写真※ / 撮影 | 写真 Y | 写真 R÷G / B÷R | 撮影 PNG Y | 撮影 PNG R÷G / B÷R | 撮影 HDR Y[cd/m²] | 撮影 HDR R÷G / B÷R | 白飛び 写真/撮影 | 黒つぶれ 写真/撮影 |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for photo_id, entry in report["photos"].items():
        for name, label in region_labels.items():
            region = entry["regions"][name]
            if "absent" in region:
                lines.append(f"| {photo_id} | {label} | この写真に無い（{region['absent']}） |  |  |  |  |  |  |  |  |  |")
                continue
            gp, gc = region["geometry_photo"], region["geometry_capture"]
            p, c, h = region["photo"], region["png"], region["hdr"]
            lines.append(
                f"| {photo_id} | {label} | {gp['elevation_min']:.1f}〜{gp['elevation_max']:.1f} / {gc['elevation_min']:.1f}〜{gc['elevation_max']:.1f} "
                f"| {gp['sun_angle_min']:.0f}・{gp['sun_angle_mean']:.0f} / {gc['sun_angle_min']:.0f}・{gc['sun_angle_mean']:.0f} "
                f"| {fmt(p.get('luminance'))} | {fmt(p.get('r_over_g'), 2)} / {fmt(p.get('b_over_r'), 2)} "
                f"| {fmt(c.get('luminance'))} | {fmt(c.get('r_over_g'), 2)} / {fmt(c.get('b_over_r'), 2)} "
                f"| {fmt(h.get('luminance'), 0)} | {fmt(h.get('r_over_g'), 2)} / {fmt(h.get('b_over_r'), 2)} "
                f"| {100 * p.get('clipped', 0):.1f} % / {100 * c.get('clipped', 0):.1f} % "
                f"| {100 * p.get('crushed', 0):.1f} % / {100 * c.get('crushed', 0):.1f} % |"
            )
    lines += [
        "",
        f"※ {PHOTO_GEOMETRY_NOTE}",
        "",
        "| 写真 | 天気 | 地面 Y ÷ 青空 Y 写真/PNG/HDR | 崖 Y ÷ 地面 Y 写真/PNG/HDR | 遠景の帯 − 地面 の R÷G 写真/PNG/HDR | 遠景の帯 − 地面 の B÷R 写真/PNG/HDR |",
        "|---|---|---|---|---|---|",
    ]
    for photo_id, entry in report["photos"].items():
        derived = entry["derived"]

        def triple(key, part=None):
            values = []
            for source, _ in SOURCES:
                value = derived[source][key]
                if part is not None:
                    value = None if value is None else value[part]
                values.append(fmt(value, 2))
            return " / ".join(values)

        lines.append(
            f"| {photo_id} | {entry['weather']} | {triple('ground_over_blue_sky')} | {triple('cliff_over_ground')} "
            f"| {triple('far_minus_ground', 'r_over_g')} | {triple('far_minus_ground', 'b_over_r')} |"
        )
    with open(path, "w", encoding="utf-8") as file:
        file.write("\n".join(lines) + "\n")


def capture_hdr_flag(capture_dir):
    """撮影が -Hdr で撮られたか（CaptureScene.ps1 の metrics.json の hdr。記録が無ければ撮られたとみなす）"""
    path = os.path.join(capture_dir, "metrics.json")
    if not os.path.isfile(path):
        return True
    with open(path, encoding="utf-8-sig") as file:
        return bool(json.load(file).get("hdr", True))


def run_look(capture_dir, out_dir, wanted, depth_dir, verify, expect_path=""):
    """--look の本体。終了コードを返す"""
    annotations = load_regions()
    region_labels = annotations["regions"]
    cameras = view_cameras()
    font = load_font(18)
    failures = []
    report = {"regions": region_labels, "photos": {}}
    photos = [p for p in PHOTOS if not wanted or p["id"] in wanted]
    capture_has_hdr = capture_hdr_flag(capture_dir)
    for entry in photos:
        photo_id, view = entry["id"], entry["view"]
        annotation = annotations["photos"].get(photo_id)
        if annotation is None:
            failures.append(f"{photo_id} の注釈が LookRegions.json に無い")
            continue
        camera = cameras.get(view)
        if camera is None or "vfov" not in camera:
            failures.append(f"視点 {view} のカメラを SceneCatalog.h・WastelandRoutine.cpp から読めない")
            continue
        photo_path = os.path.join(PHOTO_DIR, photo_id + ".jpg")
        png_path = os.path.join(capture_dir, view + ".png")
        hdr_path = os.path.join(capture_dir, view + ".hdr.pfm")
        required = (photo_path, png_path, hdr_path) if capture_has_hdr else (photo_path, png_path)
        missing = [path for path in required if not os.path.isfile(path)]
        if missing:
            failures.extend(f"ファイルが無い: {path}" for path in missing)
            continue

        photo_panel, (bLetterbox, band), _ = fit_photo(Image.open(photo_path).convert("RGB"))
        photo_codes = np.asarray(photo_panel)
        photo_linear = srgb_to_linear(photo_codes.astype(np.float64))
        photo_valid = np.ones(photo_codes.shape[:2], dtype=bool)
        if bLetterbox:
            photo_valid[:band] = False
            photo_valid[PANEL_SIZE[1] - band :] = False
        capture = Image.open(png_path).convert("RGB")
        if capture.width < MIN_CAPTURE_SIZE[0] or capture.height < MIN_CAPTURE_SIZE[1]:
            failures.append(f"撮影が小さい: {png_path}（{capture.width}×{capture.height}）")
        png_codes = np.asarray(capture)
        png_linear = srgb_to_linear(png_codes.astype(np.float64))
        hdr = read_pfm(hdr_path) if capture_has_hdr else None
        if hdr is not None and hdr.shape[:2] != png_codes.shape[:2]:
            failures.append(f"{view} の HDR の大きさ {hdr.shape[1]}×{hdr.shape[0]} が PNG と違う")
            continue
        capture_size = (capture.width, capture.height)
        directions, sun = view_directions(camera, capture_size)
        photo_directions, _ = view_directions(camera, PANEL_SIZE)
        depth = None
        if depth_dir:
            depth_path = os.path.join(depth_dir, view + ".aov.depth.pfm")
            if os.path.isfile(depth_path):
                depth = read_pfm_any(depth_path)[..., 0]

        regions = {}
        photo_polygons = {}
        capture_polygons = {}
        for name in region_labels:
            spec = annotation.get(name)
            if spec is None:
                failures.append(f"{photo_id} の {name} の注釈が無い")
                continue
            if "absent" in spec:
                if not spec["absent"].strip():
                    failures.append(f"{photo_id} の {name} が absent だが理由が空")
                regions[name] = {"absent": spec["absent"]}
                continue
            photo_polygon = spec["polygon"]
            capture_polygon = spec.get("capture_polygon", photo_polygon)
            photo_polygons[name] = photo_polygon
            capture_polygons[name] = capture_polygon
            photo_mask = polygon_mask(photo_polygon, PANEL_SIZE) & photo_valid
            capture_mask = polygon_mask(capture_polygon, capture_size)
            region = {
                "photo_polygon": photo_polygon,
                "capture_polygon": capture_polygon,
                "clouds": spec.get("clouds"),
                "note": spec.get("note"),
                "geometry_photo": geometry_stats(photo_directions, sun, photo_mask),
                "geometry_photo_approximate": True,
                "geometry_capture": geometry_stats(directions, sun, capture_mask),
                "photo": color_stats(photo_linear, photo_mask, photo_codes),
                "png": color_stats(png_linear, capture_mask, png_codes),
                "hdr": color_stats(hdr, capture_mask) if hdr is not None else {"pixels": 0},
            }
            if depth is not None and depth.shape == capture_mask.shape:
                values = depth[capture_mask]
                finite = np.isfinite(values)
                region["depth"] = {
                    "median_m": float(np.median(values[finite])) if finite.any() else None,
                    "sky_fraction": float((~finite).mean()),
                    "beyond_3km_fraction": float(((values > FAR_BAND_MIN_DEPTH) & finite).mean()),
                }
            for source, label in (("photo", "写真"), ("png", "撮影")):
                if region[source]["pixels"] == 0:
                    failures.append(f"{photo_id} の {name} の領域が{label}で空")
            regions[name] = region

        derived = {}
        for source, _ in SOURCES:
            stats = {name: regions[name].get(source) for name in regions}
            derived[source] = {
                "ground_over_blue_sky": ratio(stats.get("ground"), stats.get("blue_sky")),
                "cliff_over_ground": ratio(stats.get("cliff"), stats.get("ground")),
                "far_minus_ground": chroma_difference(stats.get("far_band"), stats.get("ground")),
            }
        top_elevation = float(np.degrees(np.arcsin(directions[0, capture_size[0] // 2, 1])))
        report["photos"][photo_id] = {
            "view": view,
            "weather": annotation.get("weather", ""),
            "camera": camera,
            "top_elevation_deg": top_elevation,
            "regions": regions,
            "derived": derived,
        }
        present = [name for name in region_labels if "absent" not in regions.get(name, {"absent": ""})]
        print(
            f"COMPARE_REFERENCE_LOOK photo={photo_id} view={view} top_elevation={top_elevation:.1f} regions={','.join(present)} "
            + " ".join(
                f"{name}_y_photo={fmt(regions[name]['photo'].get('luminance'))} {name}_y_png={fmt(regions[name]['png'].get('luminance'))}"
                for name in present
            )
        )

        # 注釈を重ねた画像（上: 写真、下: 撮影の PNG）
        photo_image = photo_panel.copy()
        capture_image = capture.resize(PANEL_SIZE, Image.LANCZOS)
        draw_regions(photo_image, photo_polygons, font)
        draw_regions(capture_image, capture_polygons, font)
        text_lines = [f"上: 写真 {photo_id}（{annotation.get('weather', '')}）　下: 撮影 {view}（{capture.width}×{capture.height}）　上端の仰角 {top_elevation:.1f}°"]
        for name, label in region_labels.items():
            region = regions.get(name, {})
            if "absent" in region:
                text_lines.append(f"{name}（{label}）: この写真に無い（{region['absent']}）")
                continue
            gp, gc = region["geometry_photo"], region["geometry_capture"]
            text_lines.append(
                f"{name}（{label}）: 仰角 写真※ {gp['elevation_min']:.1f}〜{gp['elevation_max']:.1f}°・撮影 {gc['elevation_min']:.1f}〜{gc['elevation_max']:.1f}°"
                f"　太陽から平均 写真※ {gp['sun_angle_mean']:.0f}°・撮影 {gc['sun_angle_mean']:.0f}°"
            )
            text_lines.append(
                f"　　Y 写真 {fmt(region['photo'].get('luminance'))}・撮影 {fmt(region['png'].get('luminance'))}"
                f"　R÷G {fmt(region['photo'].get('r_over_g'), 2)}・{fmt(region['png'].get('r_over_g'), 2)}"
                f"　B÷R {fmt(region['photo'].get('b_over_r'), 2)}・{fmt(region['png'].get('b_over_r'), 2)}"
            )
        text_lines.append("値は線形の平均。写真の値は撮影時のトーンカーブ・ホワイトバランス・局所補正を含むので、合否にしない")
        head, _, tail = PHOTO_GEOMETRY_NOTE.partition("（")
        text_lines += [f"※ {head}", f"　（{tail}"]
        text_height = 12 + 28 * len(text_lines)
        sheet = Image.new("RGB", (PANEL_SIZE[0], PANEL_SIZE[1] * 2 + text_height), (24, 24, 24))
        sheet.paste(photo_image, (0, 0))
        sheet.paste(capture_image, (0, PANEL_SIZE[1]))
        draw = ImageDraw.Draw(sheet)
        for index, line in enumerate(text_lines):
            draw.text((10, PANEL_SIZE[1] * 2 + 6 + index * 28), line, fill=(235, 235, 235), font=font)
        sheet.save(os.path.join(out_dir, f"look_{photo_id}.png"))

    with open(os.path.join(out_dir, "look_metrics.json"), "w", encoding="utf-8") as file:
        json.dump(report, file, ensure_ascii=False, indent=2)
    if report["photos"]:
        write_table(report, region_labels, os.path.join(out_dir, "look_table.md"))
    if not photos:
        failures.append("診断する写真が無い（空の選択）")
    if verify and len(report["photos"]) != len(photos):
        failures.append(f"診断した写真が {len(report['photos'])} 枚（選んだ {len(photos)} 枚すべてが要る）")
    if expect_path:
        from PhotoExpect import evaluate

        failures.extend(evaluate(expect_path, report["photos"], {p["id"] for p in photos}, {p["id"] for p in PHOTOS},
                                 "COMPARE_REFERENCE_LOOK_EXPECT", False))
    if failures:
        for failure in failures:
            print(f"COMPARE_REFERENCE_LOOK failure={failure}")
        print(f"COMPARE_REFERENCE_LOOK result=fail failures={len(failures)}")
        return 1
    print(f"COMPARE_REFERENCE_LOOK result=pass photos={len(report['photos'])} hdr={int(capture_has_hdr)} out={out_dir}"
          + (" verify=1" if verify else ""))
    return 0
