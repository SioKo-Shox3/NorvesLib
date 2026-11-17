"""毛のマップの設定、メッシュの頂点量、PNG を扱う。bpy に依らず numpy だけで動く。"""

import copy
from dataclasses import dataclass, field
import math
from numbers import Integral, Real
import os
import re
import struct
import sys
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common


class FurMapsError(ValueError):
    """入力、設定、PNG の内容が不正な場合に送出する。"""


MAX_REGIONS = 8
SCALAR_NAMES = ("length", "density", "clump")

# 骨は骨盤から外へ伸びる。背骨・首・頭・顎は頭へ、四肢は肉球へ、尾は先へ向く。
# 毛は胴と首が頭から尾へ、四肢と尾が根元から先へ、顔が鼻から後頭へ流れる。
# 頭を向く骨には符号 -1 を使う。向きの違う骨格では設定で符号を変える。
DEFAULT_CONFIG = {
    "version": 1,
    "resolution": 1024,
    "uv_set": "UV0",
    "dilation_px": 16,
    "tangent_epsilon": 1e-4,
    "fill_iterations": 32,
    "default_region": "body",
    "sources": {"flow": "auto", "length": "auto", "density": "auto", "clump": "auto"},
    "bones": [
        {"pattern": "root", "mode": "none"},
        {"pattern": "head", "mode": "bone", "sign": -1, "region": "face"},
        {"pattern": "jaw", "mode": "bone", "sign": -1, "region": "face"},
        {"pattern": r"neck_\d{2}", "mode": "bone", "sign": -1, "region": "body"},
        {"pattern": r"pelvis|spine_\d{2}", "mode": "bone", "sign": -1, "region": "body"},
        {"pattern": r"tail_\d{2}", "mode": "bone", "sign": 1, "region": "tail"},
        {"pattern": "(front|hind)_[LR]_paw", "mode": "bone", "sign": 1, "region": "paws"},
        {"pattern": "(front|hind)_[LR]_[a-z]+", "mode": "bone", "sign": 1, "region": "legs"},
    ],
    "regions": [
        {"name": "body", "id": 0, "length": 1.0, "density": 1.0, "clump": 0.5},
        {"name": "face", "id": 1, "length": 0.35, "density": 1.0, "clump": 0.3},
        {"name": "legs", "id": 2, "length": 0.6, "density": 1.0, "clump": 0.4},
        {"name": "paws", "id": 3, "length": 0.25, "density": 0.8, "clump": 0.3},
        {"name": "tail", "id": 4, "length": 1.0, "density": 1.0, "clump": 0.6},
    ],
    "paint_overlays": {},
}


def _check_keys(obj, allowed, label):
    if not isinstance(obj, dict):
        raise FurMapsError(f"{label} は辞書で指定してください。")
    for key in obj:
        if key not in allowed:
            raise FurMapsError(f"{label} に未知の鍵があります: {key}")


def _integer(value, low, high, label):
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, Integral):
        raise FurMapsError(f"{label} は {low}〜{high} の整数で指定してください。")
    if not low <= value <= high:
        raise FurMapsError(f"{label} は {low}〜{high} の整数で指定してください。")
    return int(value)


def _number(value, label):
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, Real):
        raise FurMapsError(f"{label} は有限の数で指定してください。")
    try:
        value = float(value)
    except (ValueError, OverflowError):
        raise FurMapsError(f"{label} は有限の数で指定してください。") from None
    if not math.isfinite(value):
        raise FurMapsError(f"{label} は有限の数で指定してください。")
    return value


def normalize_config(obj, extra_keys=()) -> dict:
    """既定値を補い、未知の鍵と各欄の不正な値を検出する。"""
    _check_keys(obj, set(DEFAULT_CONFIG) | set(extra_keys), "config")
    config = copy.deepcopy(DEFAULT_CONFIG)
    for key, value in obj.items():
        if key in ("sources", "paint_overlays"):
            _check_keys(value, DEFAULT_CONFIG["sources"], key)
            config[key].update(copy.deepcopy(value))
        elif key not in DEFAULT_CONFIG:
            config[key] = value
        else:
            config[key] = copy.deepcopy(value)

    config["version"] = _integer(config["version"], 1, 1, "version")
    for key, low, high in (("resolution", 16, 8192), ("dilation_px", 0, 256),
                           ("fill_iterations", 0, 1000)):
        config[key] = _integer(config[key], low, high, key)
    config["tangent_epsilon"] = _number(config["tangent_epsilon"], "tangent_epsilon")
    if config["tangent_epsilon"] <= 0:
        raise FurMapsError("tangent_epsilon は正の数で指定してください。")
    if not isinstance(config["uv_set"], str) or not config["uv_set"]:
        raise FurMapsError("uv_set は空でない文字列で指定してください。")
    for key, value in config["sources"].items():
        allowed = ("auto", "bones", "color") if key == "flow" else ("auto", "group", "color", "regions")
        if not isinstance(value, str) or value not in allowed:
            raise FurMapsError(f"sources.{key} の値が不正です。候補: {', '.join(allowed)}")

    regions = config["regions"]
    if not isinstance(regions, list) or not 1 <= len(regions) <= MAX_REGIONS:
        raise FurMapsError(f"regions は 1〜{MAX_REGIONS} 個の配列で指定してください。")
    names, ids = set(), set()
    for index, region in enumerate(regions):
        label = f"regions[{index}]"
        _check_keys(region, ("name", "id") + SCALAR_NAMES, label)
        name = region.get("name")
        if not isinstance(name, str) or not name or name in names:
            raise FurMapsError(f"{label}.name は重複のない空でない文字列で指定してください。")
        names.add(name)
        region["id"] = _integer(region.get("id"), 0, MAX_REGIONS - 1, f"{label}.id")
        if region["id"] in ids:
            raise FurMapsError(f"{label}.id が重複しています。")
        ids.add(region["id"])
        for key in SCALAR_NAMES:
            region[key] = _number(region.get(key), f"{label}.{key}")
            if not 0 <= region[key] <= 1:
                raise FurMapsError(f"{label}.{key} は 0〜1 の数で指定してください。")
    default = config["default_region"]
    if not isinstance(default, str) or default not in names:
        raise FurMapsError("default_region は regions にある名前で指定してください。")
    if not isinstance(config["bones"], list):
        raise FurMapsError("bones は規則の配列で指定してください。")
    for index, rule in enumerate(config["bones"]):
        label = f"bones[{index}]"
        _check_keys(rule, ("pattern", "mode", "sign", "vector", "region"), label)
        if not isinstance(rule.get("pattern"), str):
            raise FurMapsError(f"{label}.pattern は正規表現の文字列で指定してください。")
        try:
            re.compile(rule["pattern"])
        except re.error as error:
            raise FurMapsError(f"{label}.pattern の正規表現が不正です: {error}") from None
        if rule.get("mode") not in ("bone", "vector", "none"):
            raise FurMapsError(f"{label}.mode は bone・vector・none のいずれかで指定してください。")
        rule["sign"] = _integer(rule.get("sign", 1), -1, 1, f"{label}.sign")
        if rule["sign"] == 0:
            raise FurMapsError(f"{label}.sign は 1 または -1 で指定してください。")
        rule["region"] = rule.get("region", default)
        if not isinstance(rule["region"], str) or rule["region"] not in names:
            raise FurMapsError(f"{label}.region は regions にある名前で指定してください。")
        if rule["mode"] == "vector" or "vector" in rule:
            vector = rule.get("vector")
            if not isinstance(vector, (list, tuple)) or len(vector) != 3:
                raise FurMapsError(f"{label}.vector は 3 個の有限の数で指定してください。")
            rule["vector"] = [_number(value, f"{label}.vector") for value in vector]
            if not any(rule["vector"]):
                raise FurMapsError(f"{label}.vector の長さは 0 にできません。")
    for key, value in config["paint_overlays"].items():
        if not isinstance(value, str):
            raise FurMapsError(f"paint_overlays.{key} は PNG のパスの文字列で指定してください。")
    return config


@dataclass
class MeshInput:
    """同じ座標空間の頂点と骨を保持する。UV は glTF 規約で V が下向き、上端が 0。"""

    positions: np.ndarray
    normals: np.ndarray
    triangles: np.ndarray
    corner_uvs: np.ndarray
    bone_names: list[str]
    bone_heads: np.ndarray
    bone_tails: np.ndarray
    weights: np.ndarray
    groups: dict = field(default_factory=dict)
    colors: dict = field(default_factory=dict)


def _float_array(value, shape, label):
    try:
        source = np.asarray(value)
        if source.dtype.kind not in "biuf":
            raise ValueError
        array = np.ascontiguousarray(source, dtype=np.float64)
    except (TypeError, ValueError, OverflowError):
        raise FurMapsError(f"{label} は数値の配列で指定してください。") from None
    if array.shape != shape:
        raise FurMapsError(f"{label} の形は {shape} が必要です。実際: {array.shape}")
    if not np.all(np.isfinite(array)):
        raise FurMapsError(f"{label} に有限でない値があります。")
    return array


def _index_array(value, vertex_count, label):
    try:
        source = np.asarray(value)
    except (ValueError, TypeError):
        raise FurMapsError(f"{label} は整数の配列で指定してください。") from None
    if source.ndim != 2 or source.shape[1] != 3 or source.dtype.kind not in "iu":
        raise FurMapsError(f"{label} は (T,3) の整数の配列で指定してください。")
    if np.any(source < 0) or np.any(source >= vertex_count):
        raise FurMapsError(f"{label} の頂点番号は 0 以上 {vertex_count} 未満が必要です。")
    return np.ascontiguousarray(source, dtype=np.int64)


def validate_mesh(mesh) -> MeshInput:
    """形・値域・有限性を確かめ、C 連続の float64 / int64 配列にそろえる。"""
    if not isinstance(mesh, MeshInput):
        raise FurMapsError("mesh は MeshInput で指定してください。")
    try:
        shape = np.shape(mesh.positions)
    except (TypeError, ValueError):
        raise FurMapsError("positions は (V,3) の配列で指定してください。") from None
    if len(shape) != 2 or shape[1] != 3 or shape[0] == 0:
        raise FurMapsError("positions は V > 0 の (V,3) の配列で指定してください。")
    vertex_count = shape[0]
    positions = _float_array(mesh.positions, (vertex_count, 3), "positions")
    normals = _float_array(mesh.normals, (vertex_count, 3), "normals")
    triangles = _index_array(mesh.triangles, vertex_count, "triangles")
    if len(triangles) == 0:
        raise FurMapsError("triangles は三角形を 1 個以上含めてください。")
    corner_uvs = _float_array(mesh.corner_uvs, (len(triangles), 3, 2), "corner_uvs")
    if not isinstance(mesh.bone_names, list) or not all(isinstance(name, str) for name in mesh.bone_names):
        raise FurMapsError("bone_names は骨名の文字列の配列で指定してください。")
    bone_count = len(mesh.bone_names)
    bone_heads = _float_array(mesh.bone_heads, (bone_count, 3), "bone_heads")
    bone_tails = _float_array(mesh.bone_tails, (bone_count, 3), "bone_tails")
    weights = _float_array(mesh.weights, (vertex_count, bone_count), "weights")
    if np.any(weights < 0):
        raise FurMapsError("weights に負のウェイトがあります。")
    groups, colors = {}, {}
    for label, values in (("groups", mesh.groups), ("colors", mesh.colors)):
        if not isinstance(values, dict):
            raise FurMapsError(f"{label} は辞書で指定してください。")
        for name, value in values.items():
            if not isinstance(name, str):
                raise FurMapsError(f"{label} の鍵は文字列で指定してください。")
            if label == "groups":
                groups[name] = _float_array(value, (vertex_count,), f"groups.{name}")
            else:
                try:
                    color_shape = np.shape(value)
                except (ValueError, TypeError):
                    raise FurMapsError(f"colors.{name} は (V,3) または (V,4) の配列で指定してください。") from None
                if color_shape not in ((vertex_count, 3), (vertex_count, 4)):
                    raise FurMapsError(f"colors.{name} は (V,3) または (V,4) の配列で指定してください。")
                color = _float_array(value, color_shape, f"colors.{name}")
                if color_shape[1] == 3:
                    color = np.concatenate((color, np.ones((vertex_count, 1))), axis=1)
                colors[name] = color
    return MeshInput(positions, normals, triangles, corner_uvs, list(mesh.bone_names),
                     bone_heads, bone_tails, weights, groups, colors)


def _unit_vectors(vectors):
    """大きい値や小さい値でも長さの二乗を直接作らず正規化する。"""
    scale = np.max(np.abs(vectors), axis=-1, keepdims=True)
    scaled = np.divide(vectors, scale, out=np.zeros_like(vectors, dtype=np.float64), where=scale > 0)
    lengths = np.linalg.norm(scaled, axis=-1, keepdims=True)
    return np.divide(scaled, lengths, out=np.zeros_like(scaled), where=lengths > 0)


def bone_table(mesh, config):
    """骨の最初に一致した規則から方向と領域の並び番号を求める。"""
    indices = {region["name"]: index for index, region in enumerate(config["regions"])}
    dirs = np.zeros((len(mesh.bone_names), 3), dtype=np.float64)
    region_index = np.full(len(mesh.bone_names), indices[config["default_region"]], dtype=np.int64)
    for index, name in enumerate(mesh.bone_names):
        for rule in config["bones"]:
            if re.fullmatch(rule["pattern"], name) is None:
                continue
            region_index[index] = indices[rule.get("region", config["default_region"])]
            if rule["mode"] == "bone":
                direction = mesh.bone_tails[index] - mesh.bone_heads[index]
            elif rule["mode"] == "vector":
                direction = np.asarray(rule["vector"], dtype=np.float64)
            else:
                break
            dirs[index] = _unit_vectors(direction) * rule.get("sign", 1)
            break
    return dirs, region_index


def vertex_directions(mesh, config):
    """頂点カラーまたは骨のウェイト和から、射影前の方向を求める。"""
    source = config["sources"]["flow"]
    if source == "color" or (source == "auto" and "fur_flow" in mesh.colors):
        if "fur_flow" not in mesh.colors:
            raise FurMapsError("sources.flow が color ですが colors.fur_flow がありません。")
        return 2.0 * mesh.colors["fur_flow"][:, :3] - 1.0
    dirs, _ = bone_table(mesh, config)
    return mesh.weights @ dirs


def vertex_neighbors(triangles, vertex_count):
    """三角形から重複のない無向の辺を、頂点番号の昇順で求める。"""
    vertex_count = _integer(vertex_count, 0, np.iinfo(np.int64).max, "vertex_count")
    triangles = _index_array(triangles, vertex_count, "triangles")
    edges = np.concatenate((triangles[:, [0, 1]], triangles[:, [1, 2]], triangles[:, [2, 0]]))
    edges.sort(axis=1)
    edges = edges[edges[:, 0] != edges[:, 1]]
    return np.unique(edges, axis=0)


def project_to_tangent(normals, directions, triangles, epsilon, iterations):
    """接平面に射影し、無効な方向を有効な隣の方向から同時更新で埋める。"""
    try:
        shape = np.shape(normals)
    except (TypeError, ValueError):
        raise FurMapsError("normals は (V,3) の配列で指定してください。") from None
    if len(shape) != 2 or shape[1] != 3:
        raise FurMapsError("normals は (V,3) の配列で指定してください。")
    normals = _unit_vectors(_float_array(normals, shape, "normals"))
    directions = _float_array(directions, shape, "directions")
    epsilon = _number(epsilon, "epsilon")
    if epsilon <= 0:
        raise FurMapsError("epsilon は正の数で指定してください。")
    iterations = _integer(iterations, 0, 1000, "iterations")
    edges = vertex_neighbors(triangles, shape[0])

    def project(values):
        return values - np.sum(values * normals, axis=1, keepdims=True) * normals

    tangents = project(directions)
    valid = np.linalg.norm(tangents, axis=1) >= epsilon
    tangents = _unit_vectors(tangents)
    tangents[~valid] = 0
    for _ in range(iterations):
        if np.all(valid):
            break
        sums = np.zeros_like(tangents)
        counts = np.zeros(shape[0], dtype=np.int64)
        # 各反復の開始時点の valid だけを参照して、辺の両端から和を集める。
        for targets, sources in ((edges[:, 0], edges[:, 1]), (edges[:, 1], edges[:, 0])):
            take = valid[sources] & ~valid[targets]
            np.add.at(sums, targets[take], tangents[sources[take]])
            np.add.at(counts, targets[take], 1)
        means = np.divide(sums, counts[:, None], out=sums, where=counts[:, None] > 0)
        candidates = project(means)
        newly_valid = ~valid & (counts > 0) & (np.linalg.norm(candidates, axis=1) >= epsilon)
        if not np.any(newly_valid):
            break
        tangents[newly_valid] = _unit_vectors(candidates[newly_valid])
        valid |= newly_valid
    return tangents, valid


def _region_values(mesh, config):
    """領域の表の値を骨のウェイトで平均し、無ウェイトには既定領域を使う。"""
    _, region_index = bone_table(mesh, config)
    regions = config["regions"]
    default = next(region for region in regions if region["name"] == config["default_region"])
    # 頂点ごとにスケールを落とし、大きいウェイトの和の桁あふれを避ける。
    scale = np.max(mesh.weights, axis=1, keepdims=True, initial=0)
    weights = np.divide(mesh.weights, scale, out=np.zeros_like(mesh.weights), where=scale > 0)
    total = weights.sum(axis=1)
    result = {}
    for key in SCALAR_NAMES:
        table = np.array([region[key] for region in regions], dtype=np.float64)
        result[key] = np.divide(weights @ table[region_index], total,
                                out=np.full(len(total), default[key], dtype=np.float64), where=total > 0)
    return result


def vertex_scalars(mesh, config) -> dict:
    """グループ、カラー、領域の優先順で長さ・密度・束を求め、0〜1 に収める。"""
    result = {}
    region_values = None
    for key in SCALAR_NAMES:
        source = config["sources"][key]
        group_name = f"fur_{key}"
        color_name = "fur_density" if key == "clump" else group_name
        channel = 1 if key == "clump" else 0
        if source == "group" or (source == "auto" and group_name in mesh.groups):
            if group_name not in mesh.groups:
                raise FurMapsError(f"sources.{key} が group ですが groups.{group_name} がありません。")
            values = mesh.groups[group_name]
        elif source == "color" or (source == "auto" and color_name in mesh.colors):
            if color_name not in mesh.colors:
                raise FurMapsError(f"sources.{key} が color ですが colors.{color_name} がありません。")
            values = mesh.colors[color_name][:, channel]
        else:
            if region_values is None:
                region_values = _region_values(mesh, config)
            values = region_values[key]
        result[key] = np.clip(values, 0, 1)
    return result


def vertex_regions(mesh, config):
    """領域別の骨のウェイト和を比較し、同点は領域の並び順で ID を決める。"""
    _, region_index = bone_table(mesh, config)
    regions = config["regions"]
    scale = np.max(mesh.weights, axis=1, keepdims=True, initial=0)
    weights = np.divide(mesh.weights, scale, out=np.zeros_like(mesh.weights), where=scale > 0)
    membership = (region_index[:, None] == np.arange(len(regions))[None, :]).astype(np.float64)
    totals = weights @ membership
    ids = np.array([region["id"] for region in regions], dtype=np.int64)
    result = ids[np.argmax(totals, axis=1)]
    default_id = next(region["id"] for region in regions if region["name"] == config["default_region"])
    result[scale[:, 0] == 0] = default_id
    return result


_PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _png_chunk(kind, payload):
    crc = zlib.crc32(payload, zlib.crc32(kind)) & 0xffffffff
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)


def write_png(path, pixels):
    """8bit のグレーまたは RGB を、時刻を含まない決定的な PNG に保存する。"""
    if not isinstance(pixels, np.ndarray) or pixels.dtype != np.uint8:
        raise FurMapsError("PNG の pixels は uint8 の配列で指定してください。")
    if pixels.ndim == 3 and pixels.shape[2] == 1:
        pixels = pixels[:, :, 0]
    if pixels.ndim == 2:
        color_type, channels = 0, 1
    elif pixels.ndim == 3 and pixels.shape[2] == 3:
        color_type, channels = 2, 3
    else:
        raise FurMapsError("PNG の pixels の形は (H,W)・(H,W,1)・(H,W,3) にしてください。")
    height, width = pixels.shape[:2]
    if not 0 < height <= 0x7fffffff or not 0 < width <= 0x7fffffff:
        raise FurMapsError("PNG の幅と高さは 1〜2147483647 にしてください。")
    rows = np.empty((height, width * channels + 1), dtype=np.uint8)
    rows[:, 0] = 0
    rows[:, 1:] = pixels.reshape(height, width * channels)
    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    data = (_PNG_SIGNATURE + _png_chunk(b"IHDR", header)
            + _png_chunk(b"IDAT", zlib.compress(rows.tobytes(), 9)) + _png_chunk(b"IEND", b""))
    try:
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "wb") as file:
            file.write(data)
    except OSError as error:
        raise FurMapsError(f"PNG を保存できません: {path}: {error}") from None


def _unfilter_png(raw, width, height, channels):
    stride = width * channels
    rows = np.frombuffer(raw, dtype=np.uint8).reshape(height, stride + 1)
    pixels = np.empty((height, stride), dtype=np.uint8)
    previous = bytes(stride)
    for y in range(height):
        filter_type = int(rows[y, 0])
        row = rows[y, 1:]
        if filter_type == 0:
            pixels[y] = row
        elif filter_type == 1:
            # 各チャンネルの累積和で、前の画素への依存をまとめて戻す。
            pixels[y] = np.cumsum(row.reshape(width, channels), axis=0, dtype=np.uint64).astype(np.uint8).ravel()
        elif filter_type == 2:
            pixels[y] = row + np.frombuffer(previous, dtype=np.uint8)
        elif filter_type in (3, 4):
            decoded = bytearray(row.tobytes())
            for x in range(stride):
                left = decoded[x - channels] if x >= channels else 0
                above = previous[x]
                if filter_type == 3:
                    predictor = (left + above) // 2
                else:
                    upper_left = previous[x - channels] if x >= channels else 0
                    estimate = left + above - upper_left
                    dl, da, du = abs(estimate - left), abs(estimate - above), abs(estimate - upper_left)
                    predictor = left if dl <= da and dl <= du else (above if da <= du else upper_left)
                decoded[x] = (decoded[x] + predictor) & 255
            pixels[y] = np.frombuffer(decoded, dtype=np.uint8)
        else:
            raise FurMapsError(f"PNG の行 {y} に未知のフィルタがあります: {filter_type}")
        previous = pixels[y].tobytes()
    return pixels.reshape(height, width, channels)


def read_png(path) -> np.ndarray:
    """CRC と形式を確認し、PNG のフィルタ 0〜4 を戻して (H,W,C) の uint8 を返す。"""
    try:
        with open(path, "rb") as file:
            data = file.read()
    except OSError as error:
        raise FurMapsError(f"PNG を読み取れません: {path}: {error}") from None
    if not data.startswith(_PNG_SIGNATURE):
        raise FurMapsError("PNG の署名が正しくありません。")
    offset = len(_PNG_SIGNATURE)
    header = None
    compressed = []
    ended = False
    idat_closed = False
    while offset < len(data):
        if len(data) - offset < 12:
            raise FurMapsError("PNG のチャンクの長さまたは CRC が不足しています。")
        length = struct.unpack_from(">I", data, offset)[0]
        end = offset + 12 + length
        if length > 0x7fffffff or end > len(data):
            raise FurMapsError("PNG のチャンクの長さが不正です。")
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:end - 4]
        crc = struct.unpack_from(">I", data, end - 4)[0]
        if zlib.crc32(payload, zlib.crc32(kind)) & 0xffffffff != crc:
            raise FurMapsError(f"PNG のチャンク {kind!r} の CRC が一致しません。")
        if not all(65 <= value <= 90 or 97 <= value <= 122 for value in kind) or kind[2] & 32:
            raise FurMapsError("PNG のチャンクの種類が不正です。")
        offset = end
        if header is None and kind != b"IHDR":
            raise FurMapsError("PNG の先頭のチャンクは IHDR が必要です。")
        if kind == b"IHDR":
            if header is not None or length != 13:
                raise FurMapsError("PNG の IHDR の長さまたは個数が不正です。")
            header = struct.unpack(">IIBBBBB", payload)
            width, height, depth, color_type, compression, filtering, interlace = header
            if not 0 < width <= 0x7fffffff or not 0 < height <= 0x7fffffff:
                raise FurMapsError("PNG の幅または高さが不正です。")
            if depth != 8 or color_type == 3 or interlace != 0:
                raise FurMapsError("PNG は 8bit・パレットなし・インターレースなしが必要です。補正マスクは 8bit で描いてください。")
            if color_type not in (0, 4, 2, 6) or compression != 0 or filtering != 0:
                raise FurMapsError("PNG のカラー型、圧縮方式またはフィルタ方式に対応していません。")
        elif kind == b"IDAT":
            if idat_closed:
                raise FurMapsError("PNG の IDAT チャンクは連続している必要があります。")
            compressed.append(payload)
        elif kind == b"IEND":
            if length != 0 or not compressed:
                raise FurMapsError("PNG の IEND が不正か IDAT がありません。")
            ended = True
            break
        else:
            if compressed:
                idat_closed = True
            if kind == b"PLTE":
                if compressed or color_type in (0, 4) or not 0 < length <= 768 or length % 3:
                    raise FurMapsError("PNG の PLTE の位置、長さまたはカラー型が不正です。")
            elif not kind[0] & 32:
                raise FurMapsError(f"PNG の未知の必須チャンクには対応していません: {kind!r}")
    if not ended or offset != len(data):
        raise FurMapsError("PNG の IEND がないか、IEND の後に余分なデータがあります。")
    channels = {0: 1, 4: 2, 2: 3, 6: 4}[color_type]
    expected = height * (width * channels + 1)
    try:
        decoder = zlib.decompressobj()
        raw = decoder.decompress(b"".join(compressed), expected + 1)
    except (zlib.error, OverflowError) as error:
        raise FurMapsError(f"PNG の IDAT を展開できません: {error}") from None
    if len(raw) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise FurMapsError("PNG の展開後の長さまたは zlib データの終端が不正です。")
    return _unfilter_png(raw, width, height, channels)
