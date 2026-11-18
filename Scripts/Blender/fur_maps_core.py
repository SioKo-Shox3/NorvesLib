"""毛のマップの設定、メッシュの頂点量、PNG を扱う。bpy に依らず numpy だけで動く。

使い方: python -B Scripts/Blender/fur_maps_core.py --mesh MESH.npz --config CONFIG.json [--out DIR] [--paint-overlay TARGET=PATH ...]
"""

import argparse
import copy
from dataclasses import dataclass, field
import hashlib
import math
from numbers import Integral, Real
import os
import re
import struct
import sys
import zipfile
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common


class FurMapsError(ValueError):
    """入力、設定、PNG の内容が不正な場合に送出する。"""


MAX_REGIONS = 8
SCALAR_NAMES = ("length", "density", "clump")
BLENDER_CONFIG_KEYS = ("input", "mesh_object", "armature_object", "uv_map", "output_dir")

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


def triangle_uv_flow(mesh, tangents):
    """三角形の UV ヤコビアンから角ごとの単位方向と有効性を求める。"""
    mesh = validate_mesh(mesh)
    tangents = _float_array(tangents, mesh.positions.shape, "tangents")
    positions = mesh.positions[mesh.triangles]
    e1, e2 = positions[:, 1] - positions[:, 0], positions[:, 2] - positions[:, 0]
    du1 = mesh.corner_uvs[:, 1] - mesh.corner_uvs[:, 0]
    du2 = mesh.corner_uvs[:, 2] - mesh.corner_uvs[:, 0]

    with np.errstate(over="ignore", divide="ignore", invalid="ignore"):
        det = du1[:, 0] * du2[:, 1] - du2[:, 0] * du1[:, 1]
        valid = np.isfinite(det) & (np.abs(det) > 1e-12)
        tangent = np.zeros_like(e1)
        bitangent = np.zeros_like(e1)
        np.divide(du2[:, 1, None] * e1 - du1[:, 1, None] * e2,
                  det[:, None], out=tangent, where=valid[:, None])
        np.divide(-du2[:, 0, None] * e1 + du1[:, 0, None] * e2,
                  det[:, None], out=bitangent, where=valid[:, None])
        tt = np.einsum("ij,ij->i", tangent, tangent)
        tb = np.einsum("ij,ij->i", tangent, bitangent)
        bb = np.einsum("ij,ij->i", bitangent, bitangent)
        metric_det = tt * bb - tb * tb
        valid &= np.isfinite(metric_det) & (metric_det > 1e-12)
        corner_tangents = tangents[mesh.triangles]
        rhs_t = np.einsum("tki,ti->tk", corner_tangents, tangent)
        rhs_b = np.einsum("tki,ti->tk", corner_tangents, bitangent)
        flow = np.zeros((len(mesh.triangles), 3, 2), dtype=np.float64)
        np.divide(bb[:, None] * rhs_t - tb[:, None] * rhs_b, metric_det[:, None],
                  out=flow[:, :, 0], where=valid[:, None])
        np.divide(tt[:, None] * rhs_b - tb[:, None] * rhs_t, metric_det[:, None],
                  out=flow[:, :, 1], where=valid[:, None])
        valid &= np.all(np.isfinite(flow), axis=(1, 2))
        flow[~valid] = 0
        scale = np.max(np.abs(flow), axis=2, keepdims=True)
        scaled = np.divide(flow, scale, out=np.zeros_like(flow), where=scale > 0)
        lengths = np.linalg.norm(scaled, axis=2, keepdims=True)
        threshold = np.divide(1e-12, lengths, out=np.full_like(lengths, np.inf), where=lengths > 0)
        flow = np.divide(scaled, lengths, out=np.zeros_like(flow), where=scale > threshold)
    return flow, valid


def rasterize(corner_uvs, uv_valid, resolution, smooth, ids):
    """texel の中心を重心補間し、領域 ID は最も近い角から選ぶ。"""
    resolution = _integer(resolution, 1, 8192, "resolution")
    corner_uvs = np.asarray(corner_uvs, dtype=np.float64)
    uv_valid = np.asarray(uv_valid, dtype=bool)
    smooth = np.asarray(smooth, dtype=np.float64)
    ids = np.asarray(ids, dtype=np.int64)
    triangle_count = len(corner_uvs)
    if (corner_uvs.shape != (triangle_count, 3, 2)
            or uv_valid.shape != (triangle_count,)
            or smooth.ndim != 3 or smooth.shape[:2] != (triangle_count, 3)
            or ids.shape != (triangle_count, 3)
            or not np.all(np.isfinite(corner_uvs))
            or not np.all(np.isfinite(smooth))):
        raise FurMapsError("ラスタライズの UV・有効性・角の量または領域 ID の形や値が不正です。")

    values = np.zeros((resolution, resolution, smooth.shape[2]), dtype=np.float64)
    id_image = np.zeros((resolution, resolution), dtype=np.int64)
    covered = np.zeros((resolution, resolution), dtype=bool)
    for index in range(triangle_count):
        if not uv_valid[index]:
            continue
        uv = corner_uvs[index]
        lower = np.maximum(np.min(uv, axis=0), 0.0)
        upper = np.minimum(np.max(uv, axis=0), 1.0)
        if np.any(lower > upper):
            continue
        first = np.maximum(np.ceil(lower * resolution - 0.5).astype(np.int64), 0)
        last = np.minimum(np.floor(upper * resolution - 0.5).astype(np.int64), resolution - 1)
        if np.any(first > last):
            continue

        du1, du2 = uv[1] - uv[0], uv[2] - uv[0]
        det = du1[0] * du2[1] - du2[0] * du1[1]
        if not np.isfinite(det) or abs(det) <= 1e-12:
            continue
        x = (np.arange(first[0], last[0] + 1, dtype=np.float64) + 0.5) / resolution - uv[0, 0]
        y = (np.arange(first[1], last[1] + 1, dtype=np.float64) + 0.5) / resolution - uv[0, 1]
        w1 = (du2[1] * x[None, :] - du2[0] * y[:, None]) / det
        w2 = (-du1[1] * x[None, :] + du1[0] * y[:, None]) / det
        weights = np.stack((1.0 - w1 - w2, w1, w2), axis=-1)
        inside = np.all(weights >= -1e-6, axis=-1)
        if not np.any(inside):
            continue
        area = np.s_[first[1]:last[1] + 1, first[0]:last[0] + 1]
        values[area][inside] = weights[inside] @ smooth[index]
        id_image[area][inside] = ids[index][np.argmax(weights[inside], axis=1)]
        covered[area][inside] = True
    return values, id_image, covered


def dilate(values, id_image, covered, radius):
    """反復前の 8 近傍だけから埋め、残りの ID は入力の既定値を保つ。"""
    radius = _integer(radius, 0, 256, "radius")
    values = np.array(values, dtype=np.float64, copy=True)
    id_image = np.array(id_image, dtype=np.int64, copy=True)
    filled = np.array(covered, dtype=bool, copy=True)
    if (values.ndim != 3 or values.shape[2] < 2
            or id_image.shape != values.shape[:2] or filled.shape != values.shape[:2]):
        raise FurMapsError("膨張の連続量・領域 ID・被覆の配列の形が不正です。")
    height, width = filled.shape
    initial_count = int(np.count_nonzero(filled))
    offsets = ((-1, 0), (1, 0), (0, -1), (0, 1),
               (-1, -1), (-1, 1), (1, -1), (1, 1))
    for _ in range(radius):
        if np.all(filled):
            break
        sums = np.zeros_like(values)
        counts = np.zeros((height, width), dtype=np.uint8)
        next_ids = id_image.copy()
        for dy, dx in offsets:
            target_y = slice(max(0, -dy), min(height, height - dy))
            target_x = slice(max(0, -dx), min(width, width - dx))
            source_y = slice(max(0, dy), min(height, height + dy))
            source_x = slice(max(0, dx), min(width, width + dx))
            target = (target_y, target_x)
            source = (source_y, source_x)
            neighbor = ~filled[target] & filled[source]
            count_view = counts[target]
            np.copyto(next_ids[target], id_image[source], where=neighbor & (count_view == 0))
            np.add(sums[target], values[source], out=sums[target], where=neighbor[:, :, None])
            count_view += neighbor
        newly_filled = ~filled & (counts > 0)
        if not np.any(newly_filled):
            break
        values[newly_filled] = sums[newly_filled] / counts[newly_filled, None]
        id_image[newly_filled] = next_ids[newly_filled]
        filled |= newly_filled

    values[~filled] = 0
    flow = values[:, :, :2]
    lengths = np.linalg.norm(flow, axis=2, keepdims=True)
    np.divide(flow, lengths, out=flow, where=lengths > 1e-6)
    flow[lengths[:, :, 0] <= 1e-6] = 0
    return values, id_image, filled, int(np.count_nonzero(filled)) - initial_count


def load_overlay(path, resolution):
    """PNG または配列の補正マスクを検査し、元の数値形式のまま返す。"""
    resolution = _integer(resolution, 1, 8192, "resolution")
    if isinstance(path, (str, os.PathLike)):
        mask = read_png(path)[:, :, 0]
    else:
        try:
            mask = np.asarray(path)
        except (TypeError, ValueError):
            raise FurMapsError("補正マスクは uint8 または 0〜1 の浮動小数の配列で指定してください。") from None
    if mask.shape != (resolution, resolution):
        raise FurMapsError(f"補正マスクの大きさは ({resolution}, {resolution}) が必要です。実際: {mask.shape}")
    if mask.dtype != np.uint8:
        if (mask.dtype.kind != "f" or not np.all(np.isfinite(mask))
                or np.any(mask < 0) or np.any(mask > 1)):
            raise FurMapsError("補正マスクは uint8 または有限な 0〜1 の浮動小数で指定してください。")
    return mask


def apply_overlay(target, values, mask):
    """5 成分の焼き込み結果に補正を適用し、同じ配列を返す。"""
    if target not in ("length", "density", "clump", "flow"):
        raise FurMapsError(f"補正マスクの対象が不正です: {target}")
    if (not isinstance(values, np.ndarray) or values.ndim != 3
            or values.shape[0] != values.shape[1] or values.shape[2] != 5
            or values.dtype.kind != "f"):
        raise FurMapsError("補正対象は (N,N,5) の浮動小数の配列で指定してください。")
    mask = load_overlay(mask, values.shape[0])
    if mask.dtype == np.uint8:
        delta = (mask.astype(np.float64) - 127.5) / 127.5
        delta[(mask == 127) | (mask == 128)] = 0
    else:
        delta = 2.0 * (mask.astype(np.float64) - 0.5)
    if target == "flow":
        angle = delta * math.pi
        cosine, sine = np.cos(angle), np.sin(angle)
        x, y = values[:, :, 0].copy(), values[:, :, 1].copy()
        values[:, :, 0] = cosine * x - sine * y
        values[:, :, 1] = sine * x + cosine * y
    else:
        channel = {"length": 2, "density": 3, "clump": 4}[target]
        values[:, :, channel] = np.clip(values[:, :, channel] + delta, 0, 1)
    return values


def encode_unorm(values):
    """0〜1 の量を四捨五入して 8bit に収める。"""
    return np.floor(np.clip(values, 0, 1) * 255.0 + 0.5).astype(np.uint8)


def mesh_sha256(mesh):
    """名前・形・固定エンディアンの値からメッシュのハッシュを求める。"""
    mesh = validate_mesh(mesh)
    digest = hashlib.sha256()

    def add_name(name):
        encoded = name.encode("utf-8")
        digest.update(struct.pack("<Q", len(encoded)))
        digest.update(encoded)

    def add_array(name, array, dtype):
        array = np.ascontiguousarray(array, dtype=dtype)
        add_name(name)
        digest.update(struct.pack("<Q", array.ndim))
        digest.update(np.asarray(array.shape, dtype="<i8").tobytes())
        digest.update(array.tobytes())

    for name in ("positions", "normals", "triangles", "corner_uvs",
                 "bone_heads", "bone_tails", "weights"):
        add_array(name, getattr(mesh, name), "<i8" if name == "triangles" else "<f8")
    add_name("bone_names")
    digest.update(struct.pack("<Q", len(mesh.bone_names)))
    for name in mesh.bone_names:
        add_name(name)
        digest.update(b"\0")
    for prefix, arrays in (("group_", mesh.groups), ("color_", mesh.colors)):
        for name in sorted(arrays):
            add_array(prefix + name, arrays[name], "<f8")
    return digest.hexdigest()


def bake_fur_maps(mesh, config, out_dir, overlays=None, source=None, extra_keys=()) -> dict:
    """頂点の量を UV マップに焼き、4 枚の PNG と情報を保存する。"""
    cfg = normalize_config(config, extra_keys)
    mesh = validate_mesh(mesh)
    directions = vertex_directions(mesh, cfg)
    tangents, tangent_valid = project_to_tangent(
        mesh.normals, directions, mesh.triangles, cfg["tangent_epsilon"], cfg["fill_iterations"])
    scalars = vertex_scalars(mesh, cfg)
    region_ids = vertex_regions(mesh, cfg)
    corner_flow, uv_valid = triangle_uv_flow(mesh, tangents)
    vertex_values = np.stack([scalars[name] for name in SCALAR_NAMES], axis=1)
    smooth = np.concatenate((corner_flow, vertex_values[mesh.triangles]), axis=2)
    values, id_image, covered = rasterize(
        mesh.corner_uvs, uv_valid, cfg["resolution"], smooth, region_ids[mesh.triangles])
    default_id = next(region["id"] for region in cfg["regions"]
                      if region["name"] == cfg["default_region"])
    id_image[~covered] = default_id
    values, id_image, filled, dilated_count = dilate(values, id_image, covered, cfg["dilation_px"])

    paint = dict(cfg["paint_overlays"])
    if overlays is not None:
        _check_keys(overlays, ("length", "density", "clump", "flow"), "overlays")
        paint.update(overlays)
    overlay_info = []
    for target in sorted(paint):
        overlay = paint[target]
        mask = load_overlay(overlay, cfg["resolution"])
        if isinstance(overlay, (str, os.PathLike)):
            try:
                digest = common.sha256_file(overlay)
            except OSError as error:
                raise FurMapsError(f"補正マスクのハッシュを計算できません: {overlay}: {error}") from None
            path = os.fsdecode(overlay)
        else:
            digest = common.sha256_bytes(np.ascontiguousarray(mask).tobytes())
            path = "<array>"
        overlay_info.append({"target": target, "path": path, "sha256": digest})
        apply_overlay(target, values, mask)

    zero = np.zeros(values.shape[:2], dtype=np.uint8)
    images = {
        "length": (encode_unorm(values[:, :, 2]), "fur_length.png", 1, "nvtex.v0.r8.linear",
                   "length = r（0 は毛なし）"),
        "flow": (np.stack((encode_unorm(0.5 + 0.5 * values[:, :, 0]),
                           encode_unorm(0.5 + 0.5 * values[:, :, 1]), zero), axis=2),
                 "fur_flow.png", 3, "nvtex.v0.rg8.linear",
                 "v = 2 * rg - 1（UV 空間・V 下向き・単位長さ）"),
        "density_clump": (np.stack((encode_unorm(values[:, :, 3]),
                                    encode_unorm(values[:, :, 4]), zero), axis=2),
                          "fur_density_clump.png", 3, "nvtex.v0.rg8.linear", "r = 密度、g = 束"),
        "region": (id_image.astype(np.uint8), "fur_region.png", 1, "nvtex.v0.r8.linear",
                   "領域 ID の整数のバイト（texelFetch で読む）"),
    }
    maps = {}
    for name, (pixels, filename, channels, cook_format, encoding) in images.items():
        path = os.path.join(out_dir, filename)
        write_png(path, pixels)
        try:
            digest = common.sha256_file(path)
        except OSError as error:
            raise FurMapsError(f"マップのハッシュを計算できません: {path}: {error}") from None
        maps[name] = {"file": filename, "png_channels": channels, "cook_format": cook_format,
                      "encoding": encoding, "sha256": digest}
    config_for_hash = {key: value for key, value in cfg.items()
                       if key != "paint_overlays" and key not in extra_keys}
    result = {
        "format": "norveslib.fur_maps.v1",
        "resolution": cfg["resolution"],
        "uv_set": cfg["uv_set"],
        "v_axis": "down",
        "dilation_px": cfg["dilation_px"],
        "maps": maps,
        "regions": cfg["regions"],
        "overlays": overlay_info,
        "inputs": {"mesh_sha256": mesh_sha256(mesh), "config_sha256": common.sha256_json(config_for_hash),
                   "source": source},
        "stats": {
            "vertices": len(mesh.positions), "triangles": len(mesh.triangles),
            "degenerate_uv_triangles": int(np.count_nonzero(~uv_valid)),
            "unresolved_vertices": int(np.count_nonzero(~tangent_valid)),
            "covered_texels": int(np.count_nonzero(covered)), "dilated_texels": dilated_count,
            "empty_texels": int(filled.size - np.count_nonzero(filled)),
        },
    }
    try:
        common.write_json(os.path.join(out_dir, "fur_maps.json"), result)
    except (OSError, TypeError, ValueError) as error:
        raise FurMapsError(f"マップの情報を保存できません: {error}") from None
    return result


def save_mesh_npz(path, mesh):
    """メッシュを安全に再読込できる名前付きの圧縮配列として保存する。"""
    mesh = validate_mesh(mesh)
    arrays = {name: getattr(mesh, name) for name in (
        "positions", "normals", "triangles", "corner_uvs", "bone_heads", "bone_tails", "weights")}
    arrays["bone_names"] = np.asarray(mesh.bone_names, dtype=np.str_)
    arrays.update({"group_" + name: value for name, value in mesh.groups.items()})
    arrays.update({"color_" + name: value for name, value in mesh.colors.items()})
    try:
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "wb") as file:
            np.savez_compressed(file, **arrays)
    except OSError as error:
        raise FurMapsError(f"メッシュの NPZ を保存できません: {path}: {error}") from None


def load_mesh_npz(path) -> MeshInput:
    """pickle を許可せずに NPZ を読み、メッシュの形と値を検査する。"""
    required = ("positions", "normals", "triangles", "corner_uvs", "bone_names",
                "bone_heads", "bone_tails", "weights")
    try:
        with np.load(path, allow_pickle=False) as archive:
            names = archive.files
            if (len(names) != len(set(names)) or any(name not in names for name in required)
                    or any(name not in required and not name.startswith(("group_", "color_"))
                           for name in names)):
                raise FurMapsError("メッシュの NPZ の鍵が不足、重複、または不正です。")
            bone_names = archive["bone_names"]
            if bone_names.ndim != 1 or bone_names.dtype.kind != "U":
                raise FurMapsError("メッシュの NPZ の bone_names は Unicode 文字列の一次元配列が必要です。")
            groups = {name[6:]: archive[name] for name in names if name.startswith("group_")}
            colors = {name[6:]: archive[name] for name in names if name.startswith("color_")}
            mesh = MeshInput(*(archive[name] for name in required[:4]), bone_names.tolist(),
                             *(archive[name] for name in required[5:]), groups, colors)
            return validate_mesh(mesh)
    except FurMapsError:
        raise
    except (OSError, ValueError, TypeError, KeyError, EOFError, zipfile.BadZipFile) as error:
        raise FurMapsError(f"メッシュの NPZ を読み取れません: {path}: {error}") from None


class ArgumentParser(argparse.ArgumentParser):
    """引数の誤りを日本語で通知する。"""

    def error(self, message):
        self.print_usage(sys.stderr)
        self.exit(2, "引数が正しくありません。--mesh・--config・--out・--paint-overlay の指定を確認してください。\n")


def main(argv=None):
    """NPZ と設定を読み、Blender を使わずに毛のマップを焼く。"""
    parser = ArgumentParser(
        description="メッシュの NPZ から毛のマップを焼く。",
        usage="%(prog)s --mesh MESH.npz --config CONFIG.json [--out DIR] [--paint-overlay TARGET=PATH ...]",
        add_help=False)
    parser._positionals.title = "位置引数"
    parser._optionals.title = "オプション"
    parser.add_argument("--mesh", required=True, metavar="PATH", help="入力メッシュの NPZ")
    parser.add_argument("--config", required=True, metavar="PATH", help="設定 JSON")
    parser.add_argument("--out", metavar="DIR", help="マップの出力先")
    parser.add_argument("--paint-overlay", action="append", default=[], metavar="TARGET=PATH",
                        help="補正対象と PNG のパス")
    parser.add_argument("-h", "--help", action="help", help="使い方を表示して終了する")
    args = parser.parse_args(argv)
    overrides = {}
    for item in args.paint_overlay:
        target, separator, path = item.partition("=")
        if not separator or target not in ("length", "density", "clump", "flow") or not path:
            parser.error("補正マスクは TARGET=PATH で指定してください。")
        overrides[target] = os.path.abspath(path)

    try:
        config_path = os.path.abspath(args.config)
        config_dir = os.path.dirname(config_path)
        cfg = normalize_config(common.read_json(config_path), BLENDER_CONFIG_KEYS)
        cfg["paint_overlays"] = {
            key: os.path.abspath(os.path.join(config_dir, path))
            for key, path in cfg["paint_overlays"].items()
        }
        if args.out:
            out_dir = os.path.abspath(args.out)
        else:
            output_dir = cfg.get("output_dir")
            if not isinstance(output_dir, str) or not output_dir:
                raise FurMapsError("--out または設定の output_dir を指定してください。")
            out_dir = os.path.abspath(os.path.join(config_dir, output_dir))
        mesh_path = os.path.abspath(args.mesh)
        mesh = load_mesh_npz(mesh_path)
        result = bake_fur_maps(mesh, cfg, out_dir, overlays=overrides,
                               source=mesh_path, extra_keys=BLENDER_CONFIG_KEYS)
        common.log_line("FUR_MAPS", {"output": out_dir, **result["stats"]})
        return 0
    except (FurMapsError, common.ConfigError) as error:
        print(f"毛のマップを作成できません: {error}", file=sys.stderr)
    except OSError as error:
        print(f"ファイルを読み書きできません: {error}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
