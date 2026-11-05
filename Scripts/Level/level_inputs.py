# オフラインの配置検証用に設定・地形・シーンの入力を読み込む。
from __future__ import annotations

from dataclasses import dataclass, field
import json
import math
from pathlib import Path
import re

import numpy as np
from PIL import Image


class LevelInputError(Exception):
    """入力ファイルの不足や形式の不備を表す。"""


def _fail(path, key, message):
    raise LevelInputError(f"{path}: {key}: {message}")


def _read_json(path):
    path = Path(path).resolve()
    try:
        with path.open(encoding="utf-8-sig") as stream:
            value = json.load(stream)
    except json.JSONDecodeError as error:
        _fail(path, "$", f"JSON の形式が不正です（行 {error.lineno}、列 {error.colno}）。")
    except UnicodeError:
        _fail(path, "$", "UTF-8 の JSON として読み込めません。")
    except OSError:
        _fail(path, "$", "ファイルが存在しないか、読み込めません。")
    return path, _object(value, path, "$")


def _object(value, path, key):
    if not isinstance(value, dict):
        _fail(path, key, "オブジェクトを指定してください。")
    return value


def _get(value, name, path, prefix=""):
    key = f"{prefix}.{name}" if prefix else name
    if name not in value:
        _fail(path, key, "必須キーがありません。")
    return value[name]


def _keys(value, names, path, prefix=""):
    _object(value, path, prefix or "$")
    for name in names:
        _get(value, name, path, prefix)
    for name in value:
        if name not in names:
            _fail(path, f"{prefix}.{name}" if prefix else name, "未知のキーです。")


def _array(value, path, key):
    if not isinstance(value, list):
        _fail(path, key, "配列を指定してください。")
    return value


def _text(value, path, key, allow_empty=False):
    if not isinstance(value, str) or (not allow_empty and not value.strip()):
        _fail(path, key, "文字列を指定してください。" if allow_empty else "空でない文字列を指定してください。")
    return value


def _number(value, path, key, minimum=None, maximum=None, open_min=False, open_max=False):
    try:
        valid = type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        valid = False
    if not valid:
        _fail(path, key, "有限の数値を指定してください。")
    if minimum is not None and (value < minimum or (open_min and value == minimum)):
        _fail(path, key, f"{minimum} {'より大きい値' if open_min else '以上の値'}を指定してください。")
    if maximum is not None and (value > maximum or (open_max and value == maximum)):
        _fail(path, key, f"{maximum} {'より小さい値' if open_max else '以下の値'}を指定してください。")
    return float(value)


def load_config(path) -> dict:
    """既定値を補わず、設定の全キーと値を検査する。"""
    path, config = _read_json(path)
    groups = {
        "traversal": ("max_slope_deg",),
        "territory": ("ground_tolerance_m", "max_slope_deg", "max_water_fraction"),
        "reachability": ("start_xz",),
        "travel": ("cruise_speed_mps", "slope_cost"),
        "poi_travel_time_band_s": ("min", "max"),
        "trail": ("sample_step_m",),
    }
    _keys(config, ("provisional", "owner_tag", "water", *groups), path)
    for name, names in groups.items():
        _keys(config[name], names, path, name)
    _text(config["owner_tag"], path, "owner_tag")
    water = _object(config["water"], path, "water")
    mode = _text(_get(water, "mode", path, "water"), path, "water.mode")
    water_keys = {
        "none": ("mode",),
        "below_height": ("mode", "level_m"),
        "mask": ("mode", "mask_path", "mask_threshold"),
    }
    if mode not in water_keys:
        _fail(path, "water.mode", "none・below_height・mask のいずれかを指定してください。")
    _keys(water, water_keys[mode], path, "water")
    if mode == "below_height":
        _number(water["level_m"], path, "water.level_m")
    elif mode == "mask":
        _text(water["mask_path"], path, "water.mask_path")
        _number(water["mask_threshold"], path, "water.mask_threshold", 0, 1)
    _number(config["traversal"]["max_slope_deg"], path, "traversal.max_slope_deg", 0, 90, True, True)
    for name, maximum in (("ground_tolerance_m", None), ("max_slope_deg", 90), ("max_water_fraction", 1)):
        _number(config["territory"][name], path, f"territory.{name}", 0, maximum)
    start = _array(config["reachability"]["start_xz"], path, "reachability.start_xz")
    if len(start) != 2:
        _fail(path, "reachability.start_xz", "x と z の数値を 2 つ指定してください。")
    for index, value in enumerate(start):
        _number(value, path, f"reachability.start_xz[{index}]")
    _number(config["travel"]["cruise_speed_mps"], path, "travel.cruise_speed_mps", 0, open_min=True)
    _number(config["travel"]["slope_cost"], path, "travel.slope_cost", 0)
    band = config["poi_travel_time_band_s"]
    minimum = _number(band["min"], path, "poi_travel_time_band_s.min", 0)
    _number(band["max"], path, "poi_travel_time_band_s.max", minimum, open_min=True)
    _number(config["trail"]["sample_step_m"], path, "trail.sample_step_m", 0, open_min=True)
    for index, key in enumerate(_array(config["provisional"], path, "provisional")):
        _text(key, path, f"provisional[{index}]")
        current = config
        for part in key.split("."):
            if not isinstance(current, dict) or part not in current:
                _fail(path, f"provisional[{index}]", f"キーのパス {key!r} は存在しません。")
            current = current[part]
    config["_base_dir"] = str(path.parent)
    return config


@dataclass
class Terrain:
    """行を z、列を x とするメートル単位の高さ格子。"""

    heights: np.ndarray
    cell_size_m: float
    origin_x: float = 0.0
    origin_z: float = 0.0
    spec_path: str = ""
    warnings: list[str] = field(default_factory=list)

    def size_x_m(self) -> float:
        return (self.heights.shape[1] - 1) * self.cell_size_m

    def size_z_m(self) -> float:
        return (self.heights.shape[0] - 1) * self.cell_size_m

    def in_bounds(self, x, z) -> bool:
        return (math.isfinite(x) and math.isfinite(z)
                and self.origin_x <= x <= self.origin_x + self.size_x_m()
                and self.origin_z <= z <= self.origin_z + self.size_z_m())

    def world_to_cell(self, x, z) -> tuple[float, float]:
        return ((z - self.origin_z) / self.cell_size_m,
                (x - self.origin_x) / self.cell_size_m)

    def cell_to_world(self, row, col) -> tuple[float, float]:
        return (self.origin_x + col * self.cell_size_m,
                self.origin_z + row * self.cell_size_m)

    def nearest_cell(self, x, z) -> tuple[int, int]:
        if not math.isfinite(x) or not math.isfinite(z):
            raise ValueError("座標には有限の数値を指定してください。")
        row, col = self.world_to_cell(x, z)
        row = min(max(row, 0), self.heights.shape[0] - 1)
        col = min(max(col, 0), self.heights.shape[1] - 1)
        # Python の偶数丸めではなく、非負の格子座標を四捨五入する。
        return math.floor(row + 0.5), math.floor(col + 0.5)

    def height_at(self, x, z) -> float:
        if not self.in_bounds(x, z):
            raise ValueError(f"座標 (x={x}, z={z}) は地形の範囲外です。")
        row, col = self.world_to_cell(x, z)
        row = min(max(row, 0.0), self.heights.shape[0] - 1)
        col = min(max(col, 0.0), self.heights.shape[1] - 1)
        r0, c0 = math.floor(row), math.floor(col)
        r1, c1 = min(r0 + 1, self.heights.shape[0] - 1), min(c0 + 1, self.heights.shape[1] - 1)
        tz, tx = row - r0, col - c0
        a = (1 - tx) * self.heights[r0, c0] + tx * self.heights[r0, c1]
        b = (1 - tx) * self.heights[r1, c0] + tx * self.heights[r1, c1]
        return float((1 - tz) * a + tz * b)


def _read_height_samples(source, path, fmt):
    name = _text(_get(source, "path", path, "height_source"), path, "height_source.path")
    height_path = (path.parent / name).resolve()
    try:
        data = height_path.read_bytes()
    except OSError:
        _fail(path, "height_source.path", f"高さ入力 {height_path} が存在しないか、読み込めません。")
    if fmt in ("raw16", "raw32f"):
        dimensions = []
        for key in ("width", "height"):
            value = _get(source, key, path, "height_source")
            if type(value) is not int or not 2 <= value <= 4294967295:
                _fail(path, f"height_source.{key}", "2 以上 4294967295 以下の整数を指定してください。")
            dimensions.append(value)
        endian = source.get("endian", "little")
        if endian not in ("little", "big"):
            _fail(path, "height_source.endian", "little または big を指定してください。")
        dtype = np.dtype(("<" if endian == "little" else ">") + ("u2" if fmt == "raw16" else "f4"))
        width, height = dimensions
        expected = width * height * dtype.itemsize
        if len(data) != expected:
            _fail(path, "height_source.path", f"高さ入力 {height_path} は {len(data)} バイトです。寸法と形式から {expected} バイト必要です。")
        return np.frombuffer(data, dtype=dtype).reshape(height, width).astype(np.float64)
    try:
        with Image.open(height_path) as image:
            if image.format != "PNG" or len(data) < 29 or data[:8] != b"\x89PNG\r\n\x1a\n":
                _fail(path, "height_source.path", f"高さ入力 {height_path} は PNG ではありません。")
            bits, color_type = data[24], data[25]
            if (fmt == "png16" and bits != 16) or (fmt == "png8" and bits == 16):
                _fail(path, "height_source.format", f"高さ入力 {height_path} のビット深度 {bits} と {fmt} が一致しません。")
            if bits == 16 and color_type != 0:
                _fail(path, "height_source.path", f"高さ入力 {height_path} の 16bit 多チャンネル PNG は未対応です。単チャンネルにしてください。")
            if image.mode in ("P", "1"):
                image = image.convert("RGB" if image.mode == "P" else "L")
            samples = np.asarray(image)
            if samples.ndim == 3:
                if samples.shape[2] >= 3 and not np.all(samples[:, :, :3] == samples[:, :, :1]):
                    _fail(path, "height_source.path", f"高さ入力 {height_path} の全標本を R=G=B にしてください。")
                samples = samples[:, :, 0]
            if samples.ndim != 2 or min(samples.shape) < 2:
                _fail(path, "height_source.path", f"高さ入力 {height_path} の幅と高さは 2 標本以上必要です。")
            return samples.astype(np.float64)
    except (OSError, ValueError, Image.DecompressionBombError):
        _fail(path, "height_source.path", f"高さ入力 {height_path} を PNG として復号できません。")


def load_terrain(spec_path) -> Terrain:
    """クッカーと同じ高さ写像・軸・原点で地形を読み込む。"""
    path, spec = _read_json(spec_path)
    source = _object(_get(spec, "height_source", path), path, "height_source")
    fmt = _text(_get(source, "format", path, "height_source"), path, "height_source.format")
    if fmt == "exr":
        _fail(path, "height_source.format", "exr の高さ入力は未対応です。")
    if fmt not in ("png16", "png8", "raw16", "raw32f"):
        _fail(path, "height_source.format", "png16・png8・raw16・raw32f を指定してください。")
    cell = _number(_get(spec, "cell_size_m", path), path, "cell_size_m", 0, open_min=True)
    tile = spec.get("tile_cells", 128)
    if type(tile) is not int or not 8 <= tile <= 1024 or tile & (tile - 1):
        _fail(path, "tile_cells", "8 以上 1024 以下の 2 の冪の整数を指定してください。")
    fit = spec.get("fit", "error")
    if fit not in ("error", "crop", "resample"):
        _fail(path, "fit", "error・crop・resample のいずれかを指定してください。")
    flip = source.get("flip_z", False)
    if type(flip) is not bool:
        _fail(path, "height_source.flip_z", "true または false を指定してください。")
    scale, offset = 1.0, 0.0
    mapping = spec.get("height_mapping", {}) if fmt == "raw32f" else _get(spec, "height_mapping", path)
    _object(mapping, path, "height_mapping")
    if fmt == "raw32f":
        for name in ("min_m", "max_m", "scale_m", "offset_m"):
            if name in mapping:
                _fail(path, f"height_mapping.{name}", "浮動小数点入力には scale・offset を指定してください。")
        scale = _number(mapping.get("scale", 1.0), path, "height_mapping.scale")
        offset = _number(mapping.get("offset", 0.0), path, "height_mapping.offset")
    else:
        for name in ("scale", "offset"):
            if name in mapping:
                _fail(path, f"height_mapping.{name}", "整数入力には min_m/max_m または scale_m/offset_m を指定してください。")
        minmax = "min_m" in mapping or "max_m" in mapping
        if minmax and ("scale_m" in mapping or "offset_m" in mapping):
            _fail(path, "height_mapping", "min_m/max_m と scale_m/offset_m は混在できません。")
        if minmax:
            offset = _number(_get(mapping, "min_m", path, "height_mapping"), path, "height_mapping.min_m")
            maximum = _number(_get(mapping, "max_m", path, "height_mapping"), path, "height_mapping.max_m", offset)
            divisor = 255.0 if fmt == "png8" else 65535.0
            difference = maximum - offset
            scale = difference / divisor if math.isfinite(difference) else maximum / divisor - offset / divisor
        else:
            scale = _number(_get(mapping, "scale_m", path, "height_mapping"), path, "height_mapping.scale_m")
            offset = _number(mapping.get("offset_m", 0.0), path, "height_mapping.offset_m")
    samples = _read_height_samples(source, path, fmt)
    if any(size <= tile or (size - 1) % tile for size in samples.shape):
        reason = "crop・resample による寸法の変更は未対応です。" if fit != "error" else "入力寸法が不正です。"
        _fail(path, "fit", f"標本数 {samples.shape} は tile_cells={tile} の tile_cells × N + 1（N ≥ 1）が必要です。{reason}")
    with np.errstate(over="ignore", invalid="ignore"):
        heights = offset + scale * samples
    if not np.isfinite(samples).all() or not np.isfinite(heights).all():
        _fail(path, "height_source.path / height_mapping", "入力標本または写像後の高さに非有限値があります。")
    if flip:
        heights = heights[::-1].copy()
    warnings = []
    sigma = _number(spec.get("smooth", 0.0), path, "smooth", 0)
    if sigma > 0:
        warnings.append(f"{path}: smooth: σ={sigma} m の平滑化は未適用のため、クック済み地形と高さが異なる可能性があります。")
    return Terrain(heights=heights, cell_size_m=cell, spec_path=str(path), warnings=warnings)


@dataclass
class SceneEntity:
    """シーン中の Entity と、その順序付きの子・コンポーネント。"""

    alias: str
    parent_alias: str
    name: str
    local_position: tuple[float, float, float]
    world_position: tuple[float, float, float]
    children: list[str]
    components: dict[str, dict[str, object]]
    root_index: int = 0


@dataclass
class Scene:
    """親が子より先に並ぶシーン。"""

    entities: list[SceneEntity]
    path: str

    def child_entities(self, entity) -> list[SceneEntity]:
        """同じルートの子を、保存された children の順に返す。"""
        entities = {child.alias: child for child in self.entities if child.root_index == entity.root_index}
        return [entities[alias] for alias in entity.children if alias in entities]

    def by_alias(self, alias) -> SceneEntity | None:
        """一致する最初の Entity を返す。alias は保存時のルートごとに局所的。"""
        return next((entity for entity in self.entities if entity.alias == alias), None)


_INTEGER = re.compile(r"[+-]?[0-9]+")
_FLOAT = re.compile(r"[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?")


def _parse_property(type_name, text):
    # RuntimeSchema.h の保存形式に従い、未知の型や読めない値はそのまま残す。
    short = type_name.rsplit("::", 1)[-1].removeprefix("enum ")
    stripped = text.strip()
    if short == "String":
        return text
    if short in ("Vector3", "Quaternion"):
        match = re.fullmatch(rf"{short}\((.*)\)", stripped)
        if match:
            parts = [part.strip() for part in match[1].split(",")]
            if len(parts) == (3 if short == "Vector3" else 4) and all(_FLOAT.fullmatch(part) for part in parts):
                values = tuple(float(part) for part in parts)
                if all(math.isfinite(value) for value in values):
                    return values
    elif short == "bool":
        if stripped in ("0", "1"):
            return stripped == "1"
    elif short in ("float", "double"):
        if _FLOAT.fullmatch(stripped):
            value = float(stripped)
            if math.isfinite(value):
                return value
    else:
        integer_type = re.fullmatch(r"(u?)int(8|16|32|64)(?:_t)?", short)
        enum_type = re.fullmatch(r"E[A-Z][A-Za-z0-9_]*", short)
        if _INTEGER.fullmatch(stripped) and (integer_type or enum_type):
            value = int(stripped)
            if integer_type:
                unsigned, bits = integer_type.groups()
                bits = int(bits)
                minimum, maximum = (0, 2**bits - 1) if unsigned else (-2**(bits - 1), 2**(bits - 1) - 1)
                if not minimum <= value <= maximum:
                    return text
            return value
    return text


def _properties(record, path, prefix):
    record = _object(record, path, prefix)
    class_name = _text(_get(record, "class", path, prefix), path, f"{prefix}.class")
    values = {}
    for index, prop in enumerate(_array(_get(record, "properties", path, prefix), path, f"{prefix}.properties")):
        key = f"{prefix}.properties[{index}]"
        prop = _object(prop, path, key)
        name = _text(_get(prop, "name", path, key), path, f"{key}.name")
        type_name = _text(_get(prop, "type", path, key), path, f"{key}.type")
        text = _text(_get(prop, "value", path, key), path, f"{key}.value", True)
        values[name] = _parse_property(type_name, text)
    return class_name, values


def _quaternion_multiply(lhs, rhs):
    x, y, z, w = lhs
    a, b, c, d = rhs
    return (w*a + x*d + y*c - z*b, w*b - x*c + y*d + z*a,
            w*c + x*b - y*a + z*d, w*d - x*a - y*b - z*c)


def _rotate(rotation, position):
    x, y, z, w = rotation
    # エンジンと同じ q * (v,0) * conjugate(q)。正規化は追加しない。
    return _quaternion_multiply(_quaternion_multiply(rotation, (*position, 0.0)), (-x, -y, -z, w))[:3]


def _transform_value(properties, name, default, path, prefix):
    value = properties.get(name, default)
    if not isinstance(value, tuple) or len(value) != len(default):
        _fail(path, f"{prefix}.object.properties.{name}", "変換の値を解釈できません。")
    return value


def _alias(record, name, path, prefix):
    value = _text(_get(record, name, path, prefix), path, f"{prefix}.{name}")
    if not re.fullmatch(r"[0-9]+", value):
        _fail(path, f"{prefix}.{name}", "十進数の文字列を指定してください。")
    return value


def load_scene(path) -> Scene:
    """保存された木の順序を保ち、親の変換からワールド位置を求める。"""
    path, document = _read_json(path)
    if type(_get(document, "formatVersion", path)) is not int or document["formatVersion"] != 3:
        _fail(path, "formatVersion", "対応するシーン形式は整数 3 です。")
    roots = _array(_get(document, "roots", path), path, "roots")
    scene = Scene(entities=[], path=str(path))
    identity = ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0), (1.0, 1.0, 1.0))
    for index, root in enumerate(roots):
        prefix = f"roots[{index}]"
        root = _object(root, path, prefix)
        node = _get(root, "root", path, prefix)
        # alias は各ルート内の番号なので、異なるルート間では重複を許す。
        seen = set()
        stack = [(node, f"{prefix}.root", identity, None)]
        while stack:
            node, key, parent_transform, expected_parent = stack.pop()
            node = _object(node, path, key)
            alias = _alias(node, "alias", path, key)
            parent_alias = _alias(node, "parentAlias", path, key)
            if alias in seen:
                _fail(path, f"{key}.alias", f"同一ルート内で alias={alias} が重複しています。")
            seen.add(alias)
            if expected_parent is not None and parent_alias != expected_parent:
                _fail(path, f"{key}.parentAlias", f"木の親 alias={expected_parent} と一致しません。")
            _, properties = _properties(_get(node, "object", path, key), path, f"{key}.object")
            local_position = _transform_value(properties, "Position", identity[0], path, key)
            local_rotation = _transform_value(properties, "Rotation", identity[1], path, key)
            local_scale = _transform_value(properties, "Scale", identity[2], path, key)
            if expected_parent is None:
                world_position, world_rotation, world_scale = local_position, local_rotation, local_scale
            else:
                parent_position, parent_rotation, parent_scale = parent_transform
                rotated = _rotate(parent_rotation, tuple(a*b for a, b in zip(parent_scale, local_position)))
                world_position = tuple(a+b for a, b in zip(parent_position, rotated))
                world_rotation = _quaternion_multiply(parent_rotation, local_rotation)
                world_scale = tuple(a*b for a, b in zip(parent_scale, local_scale))
            if not all(math.isfinite(value) for values in (world_position, world_rotation, world_scale) for value in values):
                _fail(path, f"{key}.object.properties", "変換の合成結果が非有限値になりました。")
            components = {}
            counts = {}
            for component_index, component in enumerate(_array(_get(node, "components", path, key), path, f"{key}.components")):
                component_key = f"{key}.components[{component_index}]"
                component = _object(component, path, component_key)
                class_name, values = _properties(_get(component, "object", path, component_key), path, f"{component_key}.object")
                count = counts.get(class_name, 0)
                components[class_name if count == 0 else f"{class_name}#{count}"] = values
                counts[class_name] = count + 1
            children = _array(_get(node, "children", path, key), path, f"{key}.children")
            child_aliases = [_alias(_object(child, path, f"{key}.children[{i}]"), "alias", path, f"{key}.children[{i}]") for i, child in enumerate(children)]
            name = properties.get("Name", "")
            if not isinstance(name, str):
                _fail(path, f"{key}.object.properties.Name", "名前は文字列で指定してください。")
            scene.entities.append(SceneEntity(alias, parent_alias, name, local_position, world_position, child_aliases, components, index))
            transform = (world_position, world_rotation, world_scale)
            for i in range(len(children) - 1, -1, -1):
                stack.append((children[i], f"{key}.children[{i}]", transform, alias))
    return scene
