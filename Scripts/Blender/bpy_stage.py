"""Blender の各処理段で使う入口とシーン操作をまとめる。"""

from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass
import os
import sys
import traceback
from typing import NoReturn


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common

import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree


class StageError(Exception):
    """処理段を続けられない場合に送出する。"""


def _require_finished(result: set[str], operation: str) -> None:
    if "FINISHED" not in result:
        raise StageError(f"{operation}を完了できません。")


@dataclass
class StageResult:
    inputs: list[str]
    outputs: list[str]
    exit_code: int = 0
    message: str | None = None


def run_stage(
    stage_name: str,
    validate: Callable[[dict], dict],
    run: Callable[[dict], StageResult],
) -> NoReturn:
    try:
        try:
            raw = common.load_config()
            config = validate(raw)
        except common.ConfigError as error:
            print(f"設定の誤り: {error}", file=sys.stderr)
            sys.exit(2)

        try:
            result = run(config)
        except StageError as error:
            print(f"{stage_name} 段の失敗: {error}", file=sys.stderr)
            sys.exit(1)

        inputs = [path for path in result.inputs if os.path.exists(path)]
        outputs = [path for path in result.outputs if os.path.exists(path)]
        record_path = config["record"]
        record = common.make_record(stage_name, inputs, config, outputs)
        common.write_record(record_path, record)
        common.log_line(
            "STAGE_RESULT",
            {
                "stage": stage_name,
                "status": "ok" if result.exit_code == 0 else "failed",
                "exit_code": result.exit_code,
                "record": record_path,
            },
        )
        if result.message:
            print(result.message, file=sys.stderr)
    except Exception:
        traceback.print_exc()
        sys.exit(1)

    sys.exit(result.exit_code)


def reset_empty_scene() -> None:
    _require_finished(bpy.ops.wm.read_factory_settings(use_empty=True), "空のシーンへの初期化")


def open_blend(path: str) -> None:
    """ファイルを開く。開く前のオブジェクト参照は以後使用しない。"""
    _require_finished(bpy.ops.wm.open_mainfile(filepath=path), f"ファイル「{path}」の読み込み")


def import_model(path: str) -> list[bpy.types.Object]:
    extension = os.path.splitext(path)[1].lower()
    if extension not in (".glb", ".gltf", ".blend"):
        raise StageError(f"対応していないモデル形式です: {path}")

    scene = bpy.context.scene
    previous = {obj.as_pointer() for obj in scene.objects}
    if extension == ".blend":
        with bpy.data.libraries.load(path, link=False) as (source, target):
            target.objects = sorted(source.objects)
        loaded = sorted((obj for obj in target.objects if obj is not None), key=lambda obj: obj.name)
        for obj in loaded:
            scene.collection.objects.link(obj)
    else:
        _require_finished(bpy.ops.import_scene.gltf(filepath=path), f"モデル「{path}」の取り込み")

    return sorted(
        (obj for obj in scene.objects if obj.as_pointer() not in previous),
        key=lambda obj: obj.name,
    )


def load_scene_from(path: str) -> list[bpy.types.Object]:
    extension = os.path.splitext(path)[1].lower()
    if extension == ".blend":
        open_blend(path)
        return sorted(bpy.context.scene.objects, key=lambda obj: obj.name)
    if extension in (".glb", ".gltf"):
        reset_empty_scene()
        return import_model(path)
    raise StageError(f"対応していないモデル形式です: {path}")


def meshes(objects: Iterable[bpy.types.Object]) -> list[bpy.types.Object]:
    return sorted((obj for obj in objects if obj.type == "MESH"), key=lambda obj: obj.name)


def armatures(objects: Iterable[bpy.types.Object]) -> list[bpy.types.Object]:
    return sorted((obj for obj in objects if obj.type == "ARMATURE"), key=lambda obj: obj.name)


def pick_one(
    objects: Iterable[bpy.types.Object], name: str | None, label: str
) -> bpy.types.Object:
    candidates = sorted(objects, key=lambda obj: obj.name)
    names = ", ".join(obj.name for obj in candidates) or "なし"
    if name:
        for obj in candidates:
            if obj.name == name:
                return obj
        raise StageError(f"{label}「{name}」が見つかりません。候補: {names}")
    if len(candidates) != 1:
        raise StageError(f"{label}を一つに決められません。候補: {names}")
    return candidates[0]


def select_only(objects: Iterable[bpy.types.Object], active: bpy.types.Object) -> None:
    ordered = sorted(objects, key=lambda item: item.name)
    view_layer = bpy.context.view_layer
    for obj in sorted(view_layer.objects, key=lambda item: item.name):
        obj.select_set(False)
    for obj in ordered:
        obj.select_set(True)
    view_layer.objects.active = active


def apply_modifier(obj: bpy.types.Object, modifier: bpy.types.Modifier) -> None:
    modifier_name = modifier.name
    select_only([obj], obj)
    with bpy.context.temp_override(
        object=obj,
        active_object=obj,
        selected_objects=[obj],
        selected_editable_objects=[obj],
    ):
        bpy.ops.object.modifier_move_to_index(modifier=modifier_name, index=0)
        if not obj.modifiers or obj.modifiers[0].name != modifier_name:
            raise StageError(f"モディファイアを先頭へ移動できません: {modifier_name}")
        _require_finished(
            bpy.ops.object.modifier_apply(modifier=modifier_name),
            f"モディファイア「{modifier_name}」の適用",
        )


def apply_object_transform(obj: bpy.types.Object) -> None:
    select_only([obj], obj)
    with bpy.context.temp_override(
        object=obj,
        active_object=obj,
        selected_objects=[obj],
        selected_editable_objects=[obj],
    ):
        _require_finished(
            bpy.ops.object.transform_apply(location=True, rotation=True, scale=True),
            f"オブジェクト「{obj.name}」の変換の適用",
        )


def transform_object(obj: bpy.types.Object, matrix4: Sequence[Sequence[float]]) -> None:
    obj.matrix_world = Matrix(matrix4) @ obj.matrix_world


def join_meshes(objects: Iterable[bpy.types.Object]) -> bpy.types.Object:
    ordered = sorted(objects, key=lambda obj: obj.name)
    if not ordered:
        raise StageError("結合するメッシュがありません。")
    active = ordered[0]
    if len(ordered) == 1:
        return active

    select_only(ordered, active)
    with bpy.context.temp_override(
        active_object=active,
        object=active,
        selected_objects=ordered,
        selected_editable_objects=ordered,
    ):
        _require_finished(bpy.ops.object.join(), "メッシュの結合")
    return active


def world_vertices(obj: bpy.types.Object) -> list[tuple[float, float, float]]:
    return [tuple(obj.matrix_world @ vertex.co) for vertex in obj.data.vertices]


def world_bounds(
    points: Iterable[Sequence[float]],
) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    points = list(points)
    if not points:
        raise StageError("範囲を求める点がありません。")
    minimum = tuple(min(point[index] for point in points) for index in range(3))
    maximum = tuple(max(point[index] for point in points) for index in range(3))
    return minimum, maximum


def bvh_from_object(obj: bpy.types.Object) -> BVHTree:
    vertices = world_vertices(obj)
    polygons = [tuple(polygon.vertices) for polygon in obj.data.polygons]
    return BVHTree.FromPolygons(vertices, polygons, all_triangles=False, epsilon=0.0)


def nearest_distances(points: Iterable[Sequence[float]], bvh: BVHTree) -> list[float]:
    distances = []
    for point in points:
        _, _, _, distance = bvh.find_nearest(Vector(point))
        if distance is None:
            raise StageError("最近傍の面が見つかりません。")
        distances.append(distance)
    return distances


def remove_objects(objects: Iterable[bpy.types.Object]) -> None:
    ordered = sorted(objects, key=lambda obj: obj.name)
    mesh_data = {
        obj.data.name: obj.data
        for obj in ordered
        if obj.type == "MESH" and obj.data is not None
    }
    for obj in ordered:
        bpy.data.objects.remove(obj, do_unlink=True)
    for name in sorted(mesh_data):
        mesh = mesh_data[name]
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)


def save_blend(path: str) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    _require_finished(
        bpy.ops.wm.save_as_mainfile(filepath=path, compress=False, copy=False),
        f"ファイル「{path}」の保存",
    )


def mesh_counts(obj: bpy.types.Object) -> dict[str, int]:
    mesh = obj.data
    mesh.calc_loop_triangles()
    return {
        "vertices": len(mesh.vertices),
        "triangles": len(mesh.loop_triangles),
        "uv_layers": len(mesh.uv_layers),
        "materials": len(obj.material_slots),
        "vertex_groups": len(obj.vertex_groups),
    }
