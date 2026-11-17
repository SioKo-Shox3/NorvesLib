"""Blender に依存せず、リグの位置合わせ、関節対応、段の設定と書き出し表を扱う。"""

from dataclasses import dataclass
import math
import os
import sys


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common


class RigCoreError(ValueError):
    """リグの計算に渡された値が不正な場合に送出する。"""


class AlignmentError(RigCoreError):
    """点の対応や箱から位置合わせを求められない場合に送出する。"""


class SettingsError(common.ConfigError):
    """段の設定が不正な場合に送出する。"""


class UnsupportedBlenderVersion(RigCoreError):
    """書き出しのオプション表にない Blender の版に対して送出する。"""


def _is_number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return False
    try:
        return math.isfinite(value)
    except OverflowError:
        return False


def _vector3(value, error_type=RigCoreError, label="座標"):
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise error_type(f"{label} は有限の数を 3 個指定してください。")
    if not all(_is_number(item) for item in value):
        raise error_type(f"{label} は有限の数を 3 個指定してください。")
    return tuple(float(item) for item in value)


def _points(values, label):
    try:
        return tuple(_vector3(value, AlignmentError, label) for value in values)
    except TypeError:
        raise AlignmentError(f"{label} は座標の配列で指定してください。") from None


def _rotate(rotation, point):
    return tuple(math.fsum(row[j] * point[j] for j in range(3)) for row in rotation)


@dataclass(frozen=True)
class Similarity:
    """列ベクトルを一様な拡大、回転、平行移動で写す相似変換。"""

    scale: float
    rotation: tuple[tuple[float, float, float], ...]
    translation: tuple[float, float, float]
    rms_error: float = 0.0

    def apply(self, point) -> tuple[float, float, float]:
        """点を変換した座標を返す。"""
        rotated = _rotate(self.rotation, _vector3(point))
        return tuple(self.scale * rotated[i] + self.translation[i] for i in range(3))

    def matrix4(self) -> tuple[tuple[float, ...], ...]:
        """mathutils.Matrix に渡せる行優先の同次変換行列を返す。"""
        return tuple(
            tuple(self.scale * value for value in row) + (self.translation[i],)
            for i, row in enumerate(self.rotation)
        ) + ((0.0, 0.0, 0.0, 1.0),)


def euler_xyz_deg_to_matrix(deg) -> tuple[tuple[float, float, float], ...]:
    """度で受けた XYZ オイラー角を Rz @ Ry @ Rx の回転に変換する。"""
    x, y, z = (math.radians(value) for value in _vector3(deg, label="回転角"))
    cx, cy, cz = math.cos(x), math.cos(y), math.cos(z)
    sx, sy, sz = math.sin(x), math.sin(y), math.sin(z)
    return (
        (cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx),
        (sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx),
        (-sy, cy * sx, cy * cx),
    )


def matrix4_to_flat(m) -> list[float]:
    """4x4 行列を検査し、行優先の 16 要素に直す。"""
    try:
        rows = tuple(tuple(row) for row in m)
    except TypeError:
        raise RigCoreError("行列は 4x4 の有限の数で指定してください。") from None
    if len(rows) != 4 or any(len(row) != 4 for row in rows):
        raise RigCoreError("行列は 4x4 の有限の数で指定してください。")
    values = [value for row in rows for value in row]
    flat_to_matrix4(values)
    return [float(value) for value in values]


def flat_to_matrix4(values) -> tuple[tuple[float, ...], ...]:
    """有限の数 16 個を行優先の 4x4 行列に戻す。"""
    try:
        values = tuple(values)
    except TypeError:
        raise RigCoreError("行列には有限の数を 16 個指定してください。") from None
    if len(values) != 16 or not all(_is_number(value) for value in values):
        raise RigCoreError("行列には有限の数を 16 個指定してください。")
    return tuple(tuple(float(value) for value in values[i : i + 4]) for i in range(0, 16, 4))


def _symmetric_eigen(matrix):
    """対称行列を最大 100 掃きの巡回 Jacobi 法で対角化する。"""
    size = len(matrix)
    magnitude = max(abs(value) for row in matrix for value in row)
    vectors = [[float(i == j) for j in range(size)] for i in range(size)]
    if magnitude == 0.0:
        return [0.0] * size, vectors
    # 二乗和のあふれを避ける。固有ベクトルと相対収束条件は変わらない。
    a = [[value / magnitude for value in row] for row in matrix]
    for sweep in range(101):
        off_diagonal = 2.0 * math.fsum(
            a[p][q] ** 2 for p in range(size) for q in range(p + 1, size)
        )
        diagonal = math.fsum(a[p][p] ** 2 for p in range(size))
        if off_diagonal <= 1e-30 * diagonal:
            break
        if sweep == 100:
            raise AlignmentError("固有値の計算が 100 掃きで収束しませんでした。")
        for p in range(size - 1):
            for q in range(p + 1, size):
                if a[p][q] == 0.0:
                    continue
                angle = 0.5 * math.atan2(2.0 * a[p][q], a[q][q] - a[p][p])
                c, s = math.cos(angle), math.sin(angle)
                app, aqq, apq = a[p][p], a[q][q], a[p][q]
                a[p][p] = c * c * app - 2.0 * s * c * apq + s * s * aqq
                a[q][q] = s * s * app + 2.0 * s * c * apq + c * c * aqq
                a[p][q] = a[q][p] = 0.0
                for k in range(size):
                    if k != p and k != q:
                        akp, akq = a[k][p], a[k][q]
                        a[k][p] = a[p][k] = c * akp - s * akq
                        a[k][q] = a[q][k] = s * akp + c * akq
                    vkp, vkq = vectors[k][p], vectors[k][q]
                    vectors[k][p] = c * vkp - s * vkq
                    vectors[k][q] = s * vkp + c * vkq
    return [a[i][i] * magnitude for i in range(size)], vectors


def _center_points(points, label):
    center = tuple(math.fsum(point[i] / len(points) for point in points) for i in range(3))
    centered = tuple(tuple(point[i] - center[i] for i in range(3)) for point in points)
    covariance = tuple(
        tuple(math.fsum(point[i] * point[j] for point in centered) for j in range(3))
        for i in range(3)
    )
    eigenvalues, _ = _symmetric_eigen(covariance)
    first, second, _ = sorted(eigenvalues, reverse=True)
    if first <= 1e-24 or second <= 1e-12 * first:
        raise AlignmentError(f"{label} のランドマークが 1 点に重なるか一直線に並んでいます。")
    return center, centered


def _determinant3(m):
    return (
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])
    )


def procrustes_similarity(source_points, target_points) -> Similarity:
    """対応点を Horn の四元数法で最小二乗の相似変換に合わせる。

    同一平面上の対応点では鏡映と回転を区別できないため、鏡映として拒否しない。
    """
    source = _points(source_points, "source")
    target = _points(target_points, "target")
    if len(source) != len(target):
        raise AlignmentError("source と target のランドマークの点数が違います。")
    if len(source) < 3:
        raise AlignmentError("ランドマークは 3 点以上必要です。")
    source_center, source_delta = _center_points(source, "source")
    target_center, target_delta = _center_points(target, "target")
    h = tuple(
        tuple(math.fsum(s[i] * t[j] for s, t in zip(source_delta, target_delta)) for j in range(3))
        for i in range(3)
    )
    # 正の共通倍率で割っても、鏡映の判定と固有ベクトルは変わらない。
    magnitude = max(abs(value) for row in h for value in row)
    if magnitude == 0.0:
        raise AlignmentError("ランドマークの対応から正の拡大率を求められません。")
    h = tuple(tuple(value / magnitude for value in row) for row in h)
    frobenius = math.sqrt(math.fsum(value * value for row in h for value in row))
    if _determinant3(h) < -1e-6 * (frobenius / math.sqrt(3.0)) ** 3:
        raise AlignmentError("鏡映になる対応のため、回転では位置合わせできません。")
    xx, xy, xz = h[0]
    yx, yy, yz = h[1]
    zx, zy, zz = h[2]
    n = (
        (xx + yy + zz, yz - zy, zx - xz, xy - yx),
        (yz - zy, xx - yy - zz, xy + yx, zx + xz),
        (zx - xz, xy + yx, -xx + yy - zz, yz + zy),
        (xy - yx, zx + xz, yz + zy, -xx - yy + zz),
    )
    eigenvalues, vectors = _symmetric_eigen(n)
    index = max(range(4), key=lambda i: eigenvalues[i])
    quaternion = tuple(vectors[i][index] for i in range(4))
    norm = math.sqrt(math.fsum(value * value for value in quaternion))
    w, x, y, z = (value / norm for value in quaternion)
    rotation = (
        (1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - w * z), 2.0 * (x * z + w * y)),
        (2.0 * (x * y + w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - w * x)),
        (2.0 * (x * z - w * y), 2.0 * (y * z + w * x), 1.0 - 2.0 * (x * x + y * y)),
    )
    denominator = math.fsum(value * value for point in source_delta for value in point)
    numerator = math.fsum(
        t[i] * rotated[i]
        for s, t in zip(source_delta, target_delta)
        for rotated in (_rotate(rotation, s),)
        for i in range(3)
    )
    scale = numerator / denominator
    if not math.isfinite(scale) or scale <= 0.0:
        raise AlignmentError("対応から求めた拡大率が有限の正の数ではありません。")
    rotated_center = _rotate(rotation, source_center)
    translation = tuple(target_center[i] - scale * rotated_center[i] for i in range(3))
    transform = Similarity(scale, rotation, translation)
    rms_error = math.sqrt(math.fsum(
        math.fsum((a - b) ** 2 for a, b in zip(transform.apply(s), t))
        for s, t in zip(source, target)
    ) / len(source))
    return Similarity(scale, rotation, translation, rms_error)


def bbox_similarity(
    source_points, target_min, target_max, rotation_deg=(0, 0, 0),
    scale_axis="longest", anchor="center",
) -> Similarity:
    """回転後の軸並行の箱を、一様な拡大と中心または底面の移動で合わせる。"""
    source = _points(source_points, "source")
    if not source:
        raise AlignmentError("箱の位置合わせには source の点が 1 点以上必要です。")
    low = _vector3(target_min, AlignmentError, "target_min")
    high = _vector3(target_max, AlignmentError, "target_max")
    if any(high[i] < low[i] for i in range(3)):
        raise AlignmentError("target_max は target_min 以上にしてください。")
    try:
        rotation = euler_xyz_deg_to_matrix(rotation_deg)
    except RigCoreError as error:
        raise AlignmentError(str(error)) from None
    if scale_axis not in ("longest", "x", "y", "z"):
        raise AlignmentError("scale_axis は longest、x、y、z のいずれかにしてください。")
    if anchor not in ("center", "bottom"):
        raise AlignmentError("anchor は center または bottom にしてください。")
    rotated = tuple(_rotate(rotation, point) for point in source)
    source_low = tuple(min(point[i] for point in rotated) for i in range(3))
    source_high = tuple(max(point[i] for point in rotated) for i in range(3))
    source_edges = tuple(source_high[i] - source_low[i] for i in range(3))
    target_edges = tuple(high[i] - low[i] for i in range(3))
    if scale_axis == "longest":
        source_length, target_length = max(source_edges), max(target_edges)
    else:
        axis = ("x", "y", "z").index(scale_axis)
        source_length, target_length = source_edges[axis], target_edges[axis]
    if source_length <= 0.0 or target_length <= 0.0:
        raise AlignmentError("拡大率に使う箱の辺の長さが 0 です。")
    scale = target_length / source_length
    source_anchor = tuple((source_low[i] + source_high[i]) * 0.5 for i in range(3))
    target_anchor = tuple((low[i] + high[i]) * 0.5 for i in range(3))
    if anchor == "bottom":
        source_anchor = source_anchor[:2] + (source_low[2],)
        target_anchor = target_anchor[:2] + (low[2],)
    translation = tuple(target_anchor[i] - scale * source_anchor[i] for i in range(3))
    return Similarity(scale, rotation, translation)


def distance_stats(distances, body_length) -> dict:
    """非負の距離の最大値、最近順位の 95 百分位と体長比を返す。"""
    if not _is_number(body_length) or body_length <= 0:
        raise RigCoreError("body_length は有限の正の数にしてください。")
    try:
        values = list(distances)
    except TypeError:
        raise RigCoreError("距離は空でない配列で指定してください。") from None
    if not values or any(not _is_number(value) or value < 0 for value in values):
        raise RigCoreError("距離には有限の非負の数を 1 個以上指定してください。")
    values.sort()
    count = len(values)
    maximum = float(values[-1])
    p95 = float(values[math.ceil(0.95 * count) - 1])
    return {
        "count": count,
        "max": maximum,
        "p95": p95,
        "mean": math.fsum(value / count for value in values),
        "max_ratio": maximum / body_length,
        "p95_ratio": p95 / body_length,
    }


@dataclass(frozen=True)
class JointMapResult:
    """入力順の関節対応と、統合、実際に使った不明な行き先、未使用の鍵を保持する。"""

    renames: tuple[tuple[str, str], ...]
    unmapped: tuple[str, ...]
    merged: dict[str, tuple[str, ...]]
    unknown_targets: tuple[str, ...]
    unused_keys: tuple[str, ...]


def apply_joint_map(names, joint_map, known_joints=None) -> JointMapResult:
    """関節名を対応表で写し、実際に使った行き先のうち不明なものを返す。"""
    names = tuple(names)
    if any(not isinstance(name, str) or not name for name in names):
        raise RigCoreError("関節名は空でない文字列にしてください。")
    if len(set(names)) != len(names):
        raise RigCoreError("入力の関節名が重複しています。")
    if joint_map is not None and (
        not isinstance(joint_map, dict)
        or any(not isinstance(k, str) or not k or not isinstance(v, str) or not v
               for k, v in joint_map.items())
    ):
        raise RigCoreError("joint_map は空でない関節名どうしのオブジェクトにしてください。")
    mapping = {name: name for name in names} if joint_map is None else joint_map
    renames = tuple((name, mapping[name]) for name in names if name in mapping)
    destinations = {}
    for name, target in renames:
        destinations.setdefault(target, []).append(name)
    merged = {
        target: tuple(destinations[target]) for target in sorted(destinations)
        if len(destinations[target]) >= 2
    }
    known = None if known_joints is None else set(known_joints)
    return JointMapResult(
        renames=renames,
        unmapped=tuple(name for name in names if name not in mapping),
        merged=merged,
        unknown_targets=tuple(sorted({target for _, target in renames} - known)) if known is not None else (),
        unused_keys=tuple(sorted(set(mapping) - set(names))),
    )


STAGE_NAMES = ("rebuild", "weights", "export")
_REQUIRED = object()

# 項目は公開する鍵の順に並べる。数の範囲は両端を含み、正値だけは別の型で表す。
_STAGE_FIELDS = {
    "rebuild": {
        "base": ("path", _REQUIRED),
        "base_object": ("str?", None),
        "source_object": ("str?", None),
        "source_rotation_deg": ("vector", [0, 0, 0]),
        "landmarks": ("landmarks?", None),
        "bbox_scale_axis": ("choice", "longest", ("longest", "x", "y", "z")),
        "bbox_anchor": ("choice", "center", ("center", "bottom")),
        "wrap_method": ("choice", "NEAREST_SURFACEPOINT", ("NEAREST_SURFACEPOINT", "PROJECT")),
        "project_limit": ("number", 0.0, 0, None),
        "iterations": ("int", 3, 1, 100),
        "smooth_factor": ("number", 0.5, 0, 1),
        "smooth_repeat": ("int", 1, 0, 100),
        "bake_size": ("int", 1024, 16, 8192),
        "bake_samples": ("int", 16, 1, 4096),
        "bake_seed": ("int", 0, 0, 2147483647),
        "bake_margin": ("int", 16, 0, 64),
        "cage_extrusion": ("number", 0.0, 0, None),
        "max_ray_distance": ("number", 0.0, 0, None),
        "normal_presmooth_iterations": ("int", 0, 0, 1000),
        "normal_presmooth_factor": ("number", 0.5, 0, 1),
        "body_length": ("positive?", None),
        "max_distance_ratio": ("positive", 0.01),
        "distance_metric": ("choice", "residual", ("residual", "displacement")),
        "distance_statistic": ("choice", "max", ("max", "p95")),
    },
    "weights": {
        "mode": ("choice", "keep", ("keep", "transfer")),
        "target_object": ("str?", None),
        "max_influences": ("int", 4, 1, 8),
        "clean_threshold": ("number", 0.0, 0, 1),
        "donor": ("path?", None),
        "donor_object": ("str?", None),
        "donor_space": ("choice", "source", ("source", "base")),
        "armature": ("choice", "base", ("base", "donor")),
        "joint_map": ("mapping?", None),
        "unmapped": ("choice", "error", ("error", "drop", "keep")),
    },
    "export": {
        "format": ("choice", "glb", ("glb", "gltf")),
        "actions": ("strings?", None),
        "rest_clip_if_empty": ("bool", True),
        "frame_step": ("int", 1, 1, 100),
        "image_format": ("choice", "AUTO", ("AUTO", "NONE")),
        "armature_object_remove": ("bool", False),
    },
}


def _reject_unknown(values, allowed, label):
    unknown = [key for key in values if key not in allowed]
    if unknown:
        raise SettingsError(f"{label} に未知の鍵があります: {', '.join(sorted(map(str, unknown)))}")


def _setting_value(key, value, field, base_dir):
    kind = field[0]
    if kind.endswith("?"):
        if value is None:
            return None
        kind = kind[:-1]
    if kind in ("path", "str"):
        if not isinstance(value, str) or not value:
            raise SettingsError(f"{key} は空でない文字列にしてください。")
        if kind == "path":
            return os.path.abspath(os.path.join(base_dir, value)).replace("\\", "/")
        return value
    if kind == "bool":
        if not isinstance(value, bool):
            raise SettingsError(f"{key} は真偽値にしてください。")
    elif kind in ("int", "number", "positive"):
        valid = isinstance(value, int) and not isinstance(value, bool) if kind == "int" else _is_number(value)
        if not valid:
            expected = "整数" if kind == "int" else "有限の数"
            raise SettingsError(f"{key} は {expected}にしてください。")
        if kind == "positive":
            if value <= 0:
                raise SettingsError(f"{key} は 0 より大きくしてください。")
        elif value < field[2] or (field[3] is not None and value > field[3]):
            upper = "上限なし" if field[3] is None else str(field[3])
            raise SettingsError(f"{key} は {field[2]} 以上、{upper}の範囲にしてください。")
    elif kind == "choice":
        if not isinstance(value, str) or value not in field[2]:
            raise SettingsError(f"{key} は {', '.join(field[2])} のいずれかにしてください。")
    elif kind == "vector":
        if not isinstance(value, list):
            raise SettingsError(f"{key} は有限の数を 3 個含む配列にしてください。")
        return list(_vector3(value, SettingsError, key))
    elif kind == "landmarks":
        if not isinstance(value, list) or len(value) < 3:
            raise SettingsError(f"{key} のランドマークは 3 点以上の配列にしてください。")
        result = []
        names = set()
        for index, entry in enumerate(value):
            label = f"{key}[{index}]"
            if not isinstance(entry, dict):
                raise SettingsError(f"{label} はオブジェクトにしてください。")
            _reject_unknown(entry, ("name", "source", "target"), label)
            item = {}
            for name, item_kind in (("name", "str"), ("source", "vector"), ("target", "vector")):
                if name not in entry:
                    raise SettingsError(f"{label}.{name} は必須です。")
                item[name] = _setting_value(f"{label}.{name}", entry[name], (item_kind,), base_dir)
            if item["name"] in names:
                raise SettingsError(f"{label}.name のランドマーク名が重複しています: {item['name']}")
            names.add(item["name"])
            result.append(item)
        return result
    elif kind == "mapping":
        if not isinstance(value, dict) or any(
            not isinstance(k, str) or not k or not isinstance(v, str) or not v
            for k, v in value.items()
        ):
            raise SettingsError(f"{key} は空でない文字列どうしのオブジェクトにしてください。")
        return dict(value)
    elif kind == "strings":
        if not isinstance(value, list) or any(not isinstance(item, str) or not item for item in value):
            raise SettingsError(f"{key} は空でない文字列の配列にしてください。")
        if len(set(value)) != len(value):
            raise SettingsError(f"{key} の名前が重複しています。")
        return list(value)
    return value


def validate_stage_settings(stage, settings, base_dir=None) -> dict:
    """段の設定を検査し、既定値と絶対パスを補った新しい辞書を返す。"""
    if not isinstance(stage, str) or stage not in STAGE_NAMES:
        raise SettingsError(f"未知の段です: {stage}")
    if not isinstance(settings, dict):
        raise SettingsError(f"{stage} の settings はオブジェクトにしてください。")
    fields = _STAGE_FIELDS[stage]
    _reject_unknown(settings, fields, stage)
    base_dir = os.getcwd() if base_dir is None else base_dir
    result = {}
    for key, field in fields.items():
        value = settings.get(key, field[1])
        if value is _REQUIRED:
            raise SettingsError(f"{stage}.{key} は必須です。")
        result[key] = _setting_value(key, value, field, base_dir)
    if stage == "rebuild" and result["landmarks"] is not None:
        if any(value != 0 for value in result["source_rotation_deg"]):
            raise SettingsError("landmarks と非ゼロの source_rotation_deg は同時に指定できません。")
    if stage == "weights":
        if result["mode"] == "transfer" and result["donor"] is None:
            raise SettingsError("weights.mode が transfer のとき donor は必須です。")
        if result["mode"] == "keep":
            unused = [key for key in ("donor", "donor_object", "donor_space", "armature", "joint_map", "unmapped")
                      if result[key] != fields[key][1]]
            if unused:
                raise SettingsError(f"weights.mode が keep のとき既定値以外は使えません: {', '.join(unused)}")
    return result


def validate_stage_config(config, base_dir=None) -> dict:
    """段が受け取る JSON オブジェクトの必須項目と設定を正規化する。"""
    if not isinstance(config, dict):
        raise SettingsError("段の設定はオブジェクトにしてください。")
    keys = ("stage", "input", "output", "record", "spec", "settings")
    _reject_unknown(config, keys, "段の設定")
    if "stage" not in config:
        raise SettingsError("stage は必須です。")
    stage = config["stage"]
    if not isinstance(stage, str) or stage not in STAGE_NAMES:
        raise SettingsError(f"stage が未知の段です: {stage}")
    base_dir = os.getcwd() if base_dir is None else base_dir
    result = {"stage": stage}
    for key in ("input", "output", "record", "spec"):
        value = config.get(key, common.DEFAULT_SPEC_PATH if key == "spec" else _REQUIRED)
        if value is _REQUIRED:
            raise SettingsError(f"{key} は必須です。")
        result[key] = _setting_value(key, value, ("path",), base_dir)
    result["settings"] = validate_stage_settings(stage, config.get("settings", {}), base_dir)
    return result


def export_extension(settings) -> str:
    """正規化済みの書き出し設定に対応する拡張子を返す。"""
    return {"glb": ".glb", "gltf": ".gltf"}[settings["format"]]


GLTF_EXPORT_TABLE: dict[tuple[int, int], dict] = {
    (5, 2): {
        "use_selection": True,
        "export_apply": False,
        "export_yup": True,
        "export_texcoords": True,
        "export_normals": True,
        "export_materials": "EXPORT",
        "export_cameras": False,
        "export_lights": False,
        "export_extras": False,
        "export_skins": True,
        "export_def_bones": True,
        "export_all_influences": False,
        "export_morph": False,
        "export_animations": True,
        "export_animation_mode": "ACTIONS",
        "export_force_sampling": True,
        "export_optimize_animation_size": False,
        "export_format": "GLB",
        "export_frame_step": 1,
        "export_image_format": "AUTO",
        "export_armature_object_remove": False,
    },
}


def gltf_table_key(version) -> tuple[int, int]:
    """Blender の整数 3 要素の版から、対応する表の鍵を返す。"""
    if (
        not isinstance(version, tuple) or len(version) != 3
        or any(not isinstance(value, int) or isinstance(value, bool) or value < 0 for value in version)
        or version[:2] not in GLTF_EXPORT_TABLE
    ):
        label = ".".join(map(str, version)) if isinstance(version, tuple) else str(version)
        raise UnsupportedBlenderVersion(f"Blender {label} は未対応です。表にあるのは 5.2 系だけです。")
    return version[:2]


def gltf_export_kwargs(version, settings, filepath) -> dict:
    """正規化済みの設定から glTF 書き出し演算子の引数を作る。"""
    result = dict(GLTF_EXPORT_TABLE[gltf_table_key(version)])
    result.update({
        "export_format": {"glb": "GLB", "gltf": "GLTF_SEPARATE"}[settings["format"]],
        "export_frame_step": settings["frame_step"],
        "export_image_format": settings["image_format"],
        "export_armature_object_remove": settings["armature_object_remove"],
        "filepath": filepath,
    })
    return result


def gltf_export_option_names(version) -> tuple[str, ...]:
    """filepath を除く書き出しオプションの名前を名前順に返す。"""
    return tuple(sorted(GLTF_EXPORT_TABLE[gltf_table_key(version)]))
