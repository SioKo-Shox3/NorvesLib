"""正射影の撮影タイルと地形の格子から、陰影付きの地図を合成する。"""

import argparse
import hashlib
import json
from pathlib import Path
import sys

import cv2
import numpy as np


# 色は 0..1 の sRGB、混合の強さは 0..1。方位は +Z から +X へ時計回り。
DEFAULT_OPTIONS = {
    "light_azimuth_deg": 315.0,
    "light_altitude_deg": 45.0,
    "hillshade_strength": 0.35,
    "elevation_opacity": 0.16,
    "elevation_colors": [[0.20, 0.42, 0.22], [0.65, 0.52, 0.29], [0.96, 0.96, 0.94]],
    "contour_interval_m": 10.0,
    "major_contour_every": 5,
    "contour_opacity": 0.35,
    "major_contour_opacity": 0.65,
    "contour_color": [0.16, 0.13, 0.10],
    "water_threshold": 0.8,
    "water_feather": 0.05,
    "water_opacity": 0.7,
    "water_color": [0.16, 0.45, 0.68],
    "road_opacity": 0.7,
    "road_color": [0.70, 0.59, 0.41],
    "tiles_dir": None,
}
PNG_COMPRESSION = 9


def _integer(value, name, minimum=0):
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(f"{name} は {minimum} 以上の整数にしてください。")
    return value


def _finite(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not np.isfinite(value):
        raise ValueError(f"{name} には有限の数値を指定してください。")
    return float(value)


def _options(options):
    result = dict(DEFAULT_OPTIONS)
    if options is not None:
        unknown = set(options) - set(result)
        if unknown:
            raise ValueError(f"不明な設定です: {', '.join(sorted(unknown))}")
        result.update(options)
    for key in result:
        if key.endswith("_color") or key == "elevation_colors":
            color = np.asarray(result[key], dtype=np.float64)
            valid_shape = color.shape == (3,)
            if key == "elevation_colors":
                valid_shape = color.ndim == 2 and color.shape[0] >= 2 and color.shape[1] == 3
            if not valid_shape or not np.isfinite(color).all() or np.any((color < 0) | (color > 1)):
                raise ValueError(f"{key} は 0..1 の RGB 色にしてください。")
            result[key] = color.tolist()
        elif key == "tiles_dir":
            result[key] = None if result[key] is None else str(result[key])
        elif key == "major_contour_every":
            _integer(result[key], key, 1)
        else:
            result[key] = _finite(result[key], key)
            if (key.endswith("_opacity") or key in ("hillshade_strength", "water_threshold", "water_feather")):
                if not 0 <= result[key] <= 1:
                    raise ValueError(f"{key} は 0..1 にしてください。")
    if result["contour_interval_m"] <= 0:
        raise ValueError("等高線の間隔は正の値にしてください。")
    if not 0 <= result["light_altitude_deg"] <= 90:
        raise ValueError("光の仰角は 0..90 度にしてください。")
    return result


def _validate_transform(transform):
    if transform.get("format") != "norves.map_transform" or transform.get("version") != 1:
        raise ValueError("地図の座標変換の形式または版が対応していません。")
    rect = transform["world_rect"]
    for key in ("min_x", "min_z", "max_x", "max_z"):
        _finite(rect[key], key)
    size = _finite(transform["pixel_size_m"], "pixel_size_m")
    if size <= 0 or rect["max_x"] <= rect["min_x"] or rect["max_z"] <= rect["min_z"]:
        raise ValueError("地図の矩形と画素サイズは正の大きさにしてください。")
    width = _integer(transform["image_width"], "image_width", 1)
    height = _integer(transform["image_height"], "image_height", 1)
    axes = (transform["right_axis"], transform["up_axis"])
    if any(axis not in ("+x", "-x", "+z", "-z") for axis in axes) or axes[0][1] == axes[1][1]:
        raise ValueError("画像の上と右には異なる水平軸を指定してください。")
    for axis, count in zip(axes, (width, height)):
        extent = (rect[f"max_{axis[1]}"] - rect[f"min_{axis[1]}"]) / size
        if not np.isfinite(extent) or abs(extent - count) > 1e-3:
            raise ValueError("画像の寸法と地図の矩形の大きさが一致しません。")


def world_to_uv(transform, world_x, world_z):
    """world の X/Z を左上原点の UV に変換する。配列と外挿にも対応する。"""
    rect = transform["world_rect"]
    result = []
    for axis, vertical in ((transform["right_axis"], False), (transform["up_axis"], True)):
        value = np.asarray(world_x if axis[1] == "x" else world_z, dtype=np.float64)
        minimum, maximum = rect[f"min_{axis[1]}"], rect[f"max_{axis[1]}"]
        increasing = (axis[0] == "+") != vertical
        span = maximum - minimum
        offset = value - minimum if increasing else maximum - value
        if np.isfinite(span) and np.isfinite(offset).all():
            result.append(offset / span)
        else:
            # C++ と同じく、幅または外挿距離があふれる場合は半分にして割る。
            half_offset = value * 0.5 - minimum * 0.5 if increasing else maximum * 0.5 - value * 0.5
            result.append(half_offset / (maximum * 0.5 - minimum * 0.5))
    return tuple(result)


def uv_to_world(transform, u, v):
    """UV を world の X/Z に変換する。縦は画像の下向きに増加する。"""
    rect = transform["world_rect"]
    result = {}
    for axis, vertical, value in ((transform["right_axis"], False, u), (transform["up_axis"], True, v)):
        minimum, maximum = rect[f"min_{axis[1]}"], rect[f"max_{axis[1]}"]
        start, end = (minimum, maximum) if (axis[0] == "+") != vertical else (maximum, minimum)
        value = np.asarray(value, dtype=np.float64)
        # 符号の異なる両端でも、幅の差を先に作らず補間する。
        result[axis[1]] = (1.0 - value) * start + value * end
    return result["x"], result["z"]


def _terrain_heights(terrain):
    for key in ("origin_x", "origin_y", "origin_z", "cell_size_m"):
        _finite(terrain[key], key)
    heights = np.asarray(terrain["heights_m"], dtype=np.float64)
    if heights.ndim != 2 or min(heights.shape) < 1 or not np.isfinite(heights).all():
        raise ValueError("高さの格子には空でない有限の二次元配列を指定してください。")
    if terrain["cell_size_m"] <= 0:
        raise ValueError("地形の格子の間隔は正の値にしてください。")
    return heights


def _map_coordinates(terrain, transform):
    width, height = transform["image_width"], transform["image_height"]
    u = (np.arange(width, dtype=np.float64)[None, :] + 0.5) / width
    v = (np.arange(height, dtype=np.float64)[:, None] + 0.5) / height
    world_x, world_z = uv_to_world(transform, u, v)
    return np.broadcast_arrays((world_x - terrain["origin_x"]) / terrain["cell_size_m"],
                               (world_z - terrain["origin_z"]) / terrain["cell_size_m"])


def _sample(values, coordinates):
    """格子の外を端の値で延長し、倍精度の双線形補間を行う。"""
    x, z = coordinates
    x = np.clip(x, 0, values.shape[1] - 1)
    z = np.clip(z, 0, values.shape[0] - 1)
    x0, z0 = x.astype(np.intp), z.astype(np.intp)
    x1, z1 = np.minimum(x0 + 1, values.shape[1] - 1), np.minimum(z0 + 1, values.shape[0] - 1)
    tx, tz = x - x0, z - z0
    if values.ndim == 3:
        tx, tz = tx[..., None], tz[..., None]
    top = values[z0, x0] * (1 - tx) + values[z0, x1] * tx
    bottom = values[z1, x0] * (1 - tx) + values[z1, x1] * tx
    return top * (1 - tz) + bottom * tz


def _hillshade_grid(terrain, settings):
    heights = _terrain_heights(terrain)
    spacing = terrain["cell_size_m"]
    # 内部は中心差分、端は片側差分。一標本だけの軸は傾きゼロ。
    dz = np.gradient(heights, spacing, axis=0) if heights.shape[0] > 1 else np.zeros_like(heights)
    dx = np.gradient(heights, spacing, axis=1) if heights.shape[1] > 1 else np.zeros_like(heights)
    azimuth, altitude = np.deg2rad([settings["light_azimuth_deg"], settings["light_altitude_deg"]])
    light_x = np.sin(azimuth) * np.cos(altitude)
    light_z = np.cos(azimuth) * np.cos(altitude)
    illumination = np.clip((-dx * light_x + np.sin(altitude) - dz * light_z)
                           / np.sqrt(dx * dx + 1 + dz * dz), 0, 1)
    strength = settings["hillshade_strength"]
    return (1 - strength) + strength * illumination


def compute_hillshade(terrain, transform, options=None):
    """world 方向の光から、地図の全画素に対する陰影の係数を返す。"""
    _validate_transform(transform)
    return _sample(_hillshade_grid(terrain, _options(options)), _map_coordinates(terrain, transform))


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")


def _terrain_hash(terrain):
    """配列の型・形・値と原点を、ファイル名に依存せず入力の証拠にする。"""
    digest = hashlib.sha256()
    digest.update(_json_bytes({key: terrain[key] for key in ("origin_x", "origin_y", "origin_z", "cell_size_m")}))
    arrays = {"heights_m": terrain["heights_m"]}
    arrays.update({f"layers/{key}": value for key, value in terrain.get("layers", {}).items()})
    for key in sorted(arrays):
        array = np.ascontiguousarray(arrays[key])
        digest.update(_json_bytes({"name": key, "dtype": array.dtype.str, "shape": list(array.shape)}))
        digest.update(array.tobytes())
    return digest.hexdigest()


def _stitch(manifest, tiles_dir):
    grid = manifest["grid"]
    width = _integer(grid["map_width_px"], "map_width_px", 1)
    height = _integer(grid["map_height_px"], "map_height_px", 1)
    columns = _integer(grid["columns"], "columns", 1)
    rows = _integer(grid["rows"], "rows", 1)
    settings = manifest["settings"]
    buffer_width = _integer(settings["back_buffer_width"], "back_buffer_width", 1)
    buffer_height = _integer(settings["back_buffer_height"], "back_buffer_height", 1)
    image = np.empty((height, width, 3), dtype=np.uint8)
    coverage = np.zeros((height, width), dtype=bool)
    hashes, positions = {}, set()
    for tile in manifest["tiles"]:
        column = _integer(tile["column"], "column")
        row = _integer(tile["row"], "row")
        if column >= columns or row >= rows or (column, row) in positions:
            raise ValueError("タイルの列・行が範囲外、または重複しています。")
        positions.add((column, row))
        x = _integer(tile["map_pixel"]["x"], "map_pixel.x")
        y = _integer(tile["map_pixel"]["y"], "map_pixel.y")
        crop = tile["crop"]
        cx, cy = _integer(crop["x"], "crop.x"), _integer(crop["y"], "crop.y")
        cw, ch = _integer(crop["width"], "crop.width", 1), _integer(crop["height"], "crop.height", 1)
        if x + cw > width or y + ch > height or cx + cw > buffer_width or cy + ch > buffer_height:
            raise ValueError("タイルの配置先または切り出し矩形が画像の範囲外です。")
        data = (tiles_dir / tile["file"]).read_bytes()
        hashes[tile["file"]] = _sha256(data)
        decoded = cv2.imdecode(np.frombuffer(data, dtype=np.uint8), cv2.IMREAD_UNCHANGED)
        if decoded is None or decoded.dtype != np.uint8 or decoded.ndim != 3 or decoded.shape[2] not in (3, 4):
            raise ValueError(f"タイルは 8bit の RGB または RGBA 画像にしてください: {tile['file']}")
        if decoded.shape[:2] == (ch, cw):
            cropped = decoded
        elif decoded.shape[:2] == (buffer_height, buffer_width):
            cropped = decoded[cy:cy + ch, cx:cx + cw]
        else:
            raise ValueError(f"タイルの寸法が切り出し済み画像にも撮影画像にも一致しません: {tile['file']}")
        if coverage[y:y + ch, x:x + cw].any():
            raise ValueError("タイルの配置先が重なっています。")
        image[y:y + ch, x:x + cw] = cropped[..., :3][..., ::-1]
        coverage[y:y + ch, x:x + cw] = True
    if not coverage.all() or len(positions) != columns * rows:
        raise ValueError("タイルで覆われていない画素、または不足しているタイルがあります。")
    return image, hashes


def _srgb_to_linear(color):
    color = np.asarray(color, dtype=np.float64)
    return np.where(color <= 0.04045, color / 12.92, ((color + 0.055) / 1.055) ** 2.4)


def _linear_to_srgb(color):
    color = np.clip(color, 0, 1)
    return np.where(color <= 0.0031308, 12.92 * color, 1.055 * color ** (1 / 2.4) - 0.055)


def _r8(values):
    return np.rint(np.clip(values, 0, 1) * 255).astype(np.uint8)


def _normalize(values):
    minimum, maximum = float(values.min()), float(values.max())
    return np.zeros_like(values) if maximum == minimum else (values - minimum) / (maximum - minimum)


def _crossings(heights, interval):
    bands = np.floor(heights / interval)
    result = np.zeros(heights.shape, dtype=bool)
    horizontal = bands[:, 1:] != bands[:, :-1]
    vertical = bands[1:, :] != bands[:-1, :]
    result[:, 1:] |= horizontal
    result[:, :-1] |= horizontal
    result[1:, :] |= vertical
    result[:-1, :] |= vertical
    return result


def compose(manifest_path, terrain, out_dir, options=None):
    """タイルと地形を合成し、出力ファイル名と SHA-256、設定、注意事項を返す。"""
    settings = _options(options)
    manifest_path, out_dir = Path(manifest_path), Path(out_dir)
    manifest_data = manifest_path.read_bytes()
    manifest = json.loads(manifest_data.decode("utf-8-sig"))
    if manifest.get("format") != "norves.map_capture_manifest" or manifest.get("version") != 1:
        raise ValueError("撮影マニフェストの形式または版が対応していません。")
    transform = manifest["map_transform"]
    _validate_transform(transform)
    if (transform["image_width"], transform["image_height"]) != (manifest["grid"]["map_width_px"], manifest["grid"]["map_height_px"]):
        raise ValueError("地図の座標変換と結合先の画像寸法が一致しません。")
    tiles_dir = Path(settings["tiles_dir"]) if settings["tiles_dir"] is not None else manifest_path.parent
    rgb, tile_hashes = _stitch(manifest, tiles_dir)
    heights_grid = _terrain_heights(terrain)
    coordinates = _map_coordinates(terrain, transform)
    heights = _sample(heights_grid, coordinates) + terrain["origin_y"]
    height_normalized = _normalize(heights)
    shade = _sample(_hillshade_grid(terrain, settings), coordinates)
    color = _srgb_to_linear(rgb.astype(np.float64) / 255) * shade[..., None]
    palette = _srgb_to_linear(settings["elevation_colors"])
    stops = np.linspace(0, 1, len(palette))
    elevation = np.stack([np.interp(height_normalized, stops, palette[:, channel]) for channel in range(3)], axis=-1)
    color = color * (1 - settings["elevation_opacity"]) + elevation * settings["elevation_opacity"]
    warnings = []
    mask = terrain.get("layers", {}).get("MSK0")
    water, road = np.zeros_like(heights), np.zeros_like(heights)
    if mask is None:
        warnings.append("MSK0 が無いため、水と道の重ね合わせを省略しました。")
        print(warnings[-1])
    else:
        mask = np.asarray(mask)
        if mask.dtype != np.uint8 or mask.shape != heights_grid.shape + (4,):
            raise ValueError("MSK0 は高さの格子と同じ寸法の RGBA8 配列にしてください。")
        sampled_mask = _sample(mask.astype(np.float64) / 255, coordinates)
        wetness, road = sampled_mask[..., 0], sampled_mask[..., 2]
        threshold, feather = settings["water_threshold"], settings["water_feather"]
        if feather == 0:
            water = (wetness >= threshold).astype(np.float64)
        else:
            water = np.clip((wetness - threshold + feather) / (2 * feather), 0, 1)
            water = water * water * (3 - 2 * water)
        for coverage, prefix in ((water, "water"), (road, "road")):
            alpha = (coverage * settings[f"{prefix}_opacity"])[..., None]
            color = color * (1 - alpha) + _srgb_to_linear(settings[f"{prefix}_color"]) * alpha
    interval = settings["contour_interval_m"]
    minor = _crossings(heights, interval)
    major = _crossings(heights, interval * settings["major_contour_every"])
    contour_alpha = np.where(major, settings["major_contour_opacity"], minor * settings["contour_opacity"])[..., None]
    color = color * (1 - contour_alpha) + _srgb_to_linear(settings["contour_color"]) * contour_alpha
    rgba = np.concatenate((_r8(_linear_to_srgb(color)), np.full(heights.shape + (1,), 255, dtype=np.uint8)), axis=2)
    width, height = transform["image_width"], transform["image_height"]
    preview = rgba
    if max(width, height) > 2048:
        scale = 2048 / max(width, height)
        preview = cv2.resize(rgba, (max(1, round(width * scale)), max(1, round(height * scale))), interpolation=cv2.INTER_AREA)
    out_dir.mkdir(parents=True, exist_ok=True)
    outputs = {}
    images = {"map_full.png": rgba, "map_2048.png": preview, "height_r8.png": _r8(height_normalized),
              "water_r8.png": _r8(water), "road_r8.png": _r8(road)}
    for name, pixels in images.items():
        encoded_pixels = cv2.cvtColor(pixels, cv2.COLOR_RGBA2BGRA) if pixels.ndim == 3 else pixels
        if not cv2.imwrite(str(out_dir / name), encoded_pixels, [cv2.IMWRITE_PNG_COMPRESSION, PNG_COMPRESSION]):
            raise OSError(f"地図の画像を書き出せませんでした: {name}")
        outputs[name] = _sha256((out_dir / name).read_bytes())
    transform_data = _json_bytes(transform)
    (out_dir / "map_transform.json").write_bytes(transform_data)
    outputs["map_transform.json"] = _sha256(transform_data)
    report = {
        "format": "norves.map_compose_report", "version": 1,
        "inputs": {"manifest_sha256": _sha256(manifest_data), "terrain_sha256": _terrain_hash(terrain), "tiles_sha256": tile_hashes},
        "outputs_sha256": dict(outputs), "settings": settings, "warnings": warnings,
        "encoding": {"png_compression": PNG_COMPRESSION, "numpy_version": np.__version__, "opencv_version": cv2.__version__},
        "height_range_m": {"min": float(heights.min()), "max": float(heights.max())},
        "hash_note": "自己参照を避けるため、この報告自身のハッシュは返り値だけに含みます。",
    }
    report_data = _json_bytes(report)
    (out_dir / "compose_report.json").write_bytes(report_data)
    outputs["compose_report.json"] = _sha256(report_data)
    return {"files": outputs, "settings": settings, "warnings": warnings}


def _load_terrain(path):
    """同じディレクトリの地形リーダーを、CLI で必要になった時点で読み込む。"""
    module_dir = str(Path(__file__).resolve().parent)
    if module_dir not in sys.path:
        sys.path.insert(0, module_dir)
    from nvtrn_reader import load_terrain
    return load_terrain(path)


def main():
    parser = argparse.ArgumentParser(description="撮影タイルと地形から地図を合成します。")
    parser.add_argument("--manifest", required=True, help="撮影マニフェストの JSON")
    parser.add_argument("--terrain", required=True, help="地形の nvpkg または nvtrn")
    parser.add_argument("--out", required=True, help="出力ディレクトリ")
    parser.add_argument("--tiles-dir", help="撮影タイルの基準ディレクトリ")
    parser.add_argument("--options", help="合成設定を記述した JSON ファイル")
    args = parser.parse_args()
    try:
        options = json.loads(Path(args.options).read_text(encoding="utf-8-sig")) if args.options else {}
        if args.tiles_dir is not None:
            options["tiles_dir"] = args.tiles_dir
        result = compose(args.manifest, _load_terrain(args.terrain), args.out, options)
    except (OSError, ValueError, KeyError, TypeError, ImportError, cv2.error) as error:
        parser.exit(1, f"地図の合成に失敗しました: {error}\n")
    print(f"地図の合成が完了しました。出力ファイル数: {len(result['files'])}")


if __name__ == "__main__":
    main()
