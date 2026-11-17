"""Blender の土台メッシュから毛のマップ用の配列を取り出し、コアに渡す。

使い方: blender --background --factory-startup --python Scripts/Blender/fur_flow.py -- CONFIG [--paint-overlay TARGET=PATH ...]
CONFIG は設定 JSON のパス、または JSON オブジェクトの文字列。
設定の input と output_dir は必須。mesh_object・armature_object・uv_map は省略できる。
そのほかの欄は毛のマップのコアの設定に従う。
終了コードは 0 が成功、1 が入力または設定の誤り、2 が引数の誤り。
"""

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common
import fur_maps_core


class ArgumentParser(argparse.ArgumentParser):
    """引数の誤りを日本語で通知する。"""

    def error(self, message):
        self.print_usage(sys.stderr)
        self.exit(2, "引数が正しくありません。設定と --paint-overlay TARGET=PATH の指定を確認してください。\n")


def _text_field(config, key, required=False):
    if key not in config and not required:
        return None
    value = config.get(key)
    if not isinstance(value, str) or not value or "\0" in value:
        raise fur_maps_core.FurMapsError(f"{key} は空でない文字列で指定してください。")
    return value


def _absolute_path(path, base_dir, label, allow_empty=False):
    if not isinstance(path, str) or (not path and not allow_empty) or "\0" in path:
        raise fur_maps_core.FurMapsError(f"{label} は空でないパスの文字列で指定してください。")
    return os.path.abspath(os.path.join(base_dir, path))


def _read_config(argument, overrides):
    if argument.lstrip().startswith("{"):
        config_dir = os.getcwd()
        config = common.load_config(["--", argument])
    else:
        config_path = _absolute_path(argument, os.getcwd(), "設定 JSON")
        config_dir = os.path.dirname(config_path)
        config = common.read_json(config_path)

    config = fur_maps_core.normalize_config(config, extra_keys=fur_maps_core.BLENDER_CONFIG_KEYS)
    input_path = _absolute_path(_text_field(config, "input", required=True), config_dir, "input")
    output_dir = _absolute_path(_text_field(config, "output_dir", required=True), config_dir, "output_dir")
    for key in ("mesh_object", "armature_object", "uv_map"):
        _text_field(config, key)

    config["input"] = input_path
    config["output_dir"] = output_dir
    config["paint_overlays"] = {
        target: _absolute_path(path, config_dir, f"paint_overlays.{target}", allow_empty=True)
        for target, path in config["paint_overlays"].items()
    }
    overlays = dict(config["paint_overlays"])
    overlays.update(overrides)
    return config, input_path, output_dir, overlays


def _load_input(bpy, input_path):
    extension = os.path.splitext(input_path)[1].lower()
    if extension not in (".blend", ".glb", ".gltf"):
        raise fur_maps_core.FurMapsError("input の拡張子は .blend・.glb・.gltf のいずれかにしてください。")
    try:
        if extension == ".blend":
            result = bpy.ops.wm.open_mainfile(filepath=input_path)
        else:
            result = bpy.ops.wm.read_factory_settings(use_empty=True)
            if "FINISHED" not in result:
                raise fur_maps_core.FurMapsError("空の Blender シーンを作成できません。")
            result = bpy.ops.import_scene.gltf(filepath=input_path)
    except RuntimeError as error:
        raise fur_maps_core.FurMapsError(f"入力を読み込めません: {input_path}: {error}") from None
    if "FINISHED" not in result:
        raise fur_maps_core.FurMapsError(f"入力を読み込めません: {input_path}")


def _has_armature(obj):
    return (any(modifier.type == "ARMATURE" for modifier in obj.modifiers)
            or (obj.parent is not None and obj.parent.type == "ARMATURE"))


def _select_objects(bpy, config):
    mesh_name = config.get("mesh_object")
    if mesh_name is not None:
        obj = bpy.data.objects.get(mesh_name)
        if obj is None or obj.type != "MESH":
            raise fur_maps_core.FurMapsError(f"指定されたメッシュが見つかりません: {mesh_name}")
    else:
        candidates = [obj for obj in bpy.data.objects if obj.type == "MESH" and _has_armature(obj)]
        if len(candidates) != 1:
            names = "、".join(obj.name for obj in candidates) or "なし"
            raise fur_maps_core.FurMapsError(f"アーマチュアを持つメッシュを 1 個に特定できません。候補: {names}")
        obj = candidates[0]

    armature_name = config.get("armature_object")
    if armature_name is not None:
        armature = bpy.data.objects.get(armature_name)
        if armature is None or armature.type != "ARMATURE":
            raise fur_maps_core.FurMapsError(f"指定されたアーマチュアが見つかりません: {armature_name}")
    else:
        armature = next((modifier.object for modifier in obj.modifiers
                         if modifier.type == "ARMATURE" and modifier.object is not None
                         and modifier.object.type == "ARMATURE"), None)
        if armature is None and obj.parent is not None and obj.parent.type == "ARMATURE":
            armature = obj.parent
        if armature is None:
            raise fur_maps_core.FurMapsError(f"メッシュのアーマチュアが見つかりません: {obj.name}")
    return obj, armature


def _select_uv_layer(mesh, name):
    layers = mesh.uv_layers
    if not len(layers):
        raise fur_maps_core.FurMapsError("メッシュに UV 層がありません。")
    if name is not None:
        layer = layers.get(name)
        if layer is None:
            raise fur_maps_core.FurMapsError(f"指定された UV 層が見つかりません: {name}")
        return layer
    return next((layer for layer in layers if layer.active_render), layers[0])


def _mesh_geometry(obj, uv_layer):
    mesh = obj.data
    vertex_count = len(mesh.vertices)
    positions = np.empty((vertex_count, 3), dtype=np.float32)
    mesh.vertices.foreach_get("co", positions.ravel())
    normals = np.empty((vertex_count, 3), dtype=np.float32)
    vertex_normals = getattr(mesh, "vertex_normals", None)
    if vertex_normals is not None:
        vertex_normals.foreach_get("vector", normals.ravel())
    else:
        mesh.vertices.foreach_get("normal", normals.ravel())

    world = np.asarray(obj.matrix_world, dtype=np.float64)
    positions = positions @ world[:3, :3].T + world[:3, 3]
    try:
        normal_matrix = np.linalg.inv(world[:3, :3]).T
    except np.linalg.LinAlgError:
        raise fur_maps_core.FurMapsError(f"メッシュの変換行列が特異なため法線を変換できません: {obj.name}") from None
    normals = normals @ normal_matrix.T
    lengths = np.linalg.norm(normals, axis=1, keepdims=True)
    normals = np.divide(normals, lengths, out=np.zeros_like(normals), where=lengths > 0)

    mesh.calc_loop_triangles()
    triangle_count = len(mesh.loop_triangles)
    triangles = np.empty((triangle_count, 3), dtype=np.int32)
    triangle_loops = np.empty((triangle_count, 3), dtype=np.int32)
    mesh.loop_triangles.foreach_get("vertices", triangles.ravel())
    mesh.loop_triangles.foreach_get("loops", triangle_loops.ravel())
    uvs = np.empty((len(mesh.loops), 2), dtype=np.float32)
    uv_values = getattr(uv_layer, "uv", None)
    if uv_values is not None:
        uv_values.foreach_get("vector", uvs.ravel())
    else:
        uv_layer.data.foreach_get("uv", uvs.ravel())
    corner_uvs = uvs[triangle_loops]
    corner_uvs[:, :, 1] = 1.0 - corner_uvs[:, :, 1]
    return positions, normals, triangles, corner_uvs


def _bones(armature):
    bones = armature.data.bones
    bone_count = len(bones)
    deform = np.empty(bone_count, dtype=np.bool_)
    heads = np.empty((bone_count, 3), dtype=np.float32)
    tails = np.empty((bone_count, 3), dtype=np.float32)
    if bone_count:
        bones.foreach_get("use_deform", deform)
        bones.foreach_get("head_local", heads.ravel())
        bones.foreach_get("tail_local", tails.ravel())
    names = [bone.name for bone, use in zip(bones, deform) if use]
    world = np.asarray(armature.matrix_world, dtype=np.float64)
    heads = heads[deform] @ world[:3, :3].T + world[:3, 3]
    tails = tails[deform] @ world[:3, :3].T + world[:3, 3]
    return names, heads, tails


def _vertex_groups(obj, bone_names):
    vertex_count = len(obj.data.vertices)
    weights = np.zeros((vertex_count, len(bone_names)), dtype=np.float32)
    bone_columns = {}
    for column, name in enumerate(bone_names):
        group = obj.vertex_groups.get(name)
        if group is not None:
            bone_columns[group.index] = column

    groups = {}
    group_arrays = {}
    for name in ("fur_length", "fur_density", "fur_clump"):
        group = obj.vertex_groups.get(name)
        if group is not None:
            groups[name] = np.zeros(vertex_count, dtype=np.float32)
            group_arrays[group.index] = groups[name]

    if bone_columns or group_arrays:
        for vertex in obj.data.vertices:
            for member in vertex.groups:
                column = bone_columns.get(member.group)
                if column is not None:
                    weights[vertex.index, column] = member.weight
                values = group_arrays.get(member.group)
                if values is not None:
                    values[vertex.index] = member.weight
    return weights, groups


def _vertex_colors(mesh):
    vertex_count = len(mesh.vertices)
    colors = {}
    loop_vertices = None
    for name in ("fur_length", "fur_density", "fur_flow"):
        attribute = mesh.color_attributes.get(name)
        if attribute is None:
            continue
        if attribute.domain not in ("POINT", "CORNER"):
            raise fur_maps_core.FurMapsError(f"頂点カラー {name} の領域は POINT または CORNER にしてください。")
        if attribute.data_type not in ("BYTE_COLOR", "FLOAT_COLOR"):
            raise fur_maps_core.FurMapsError(f"頂点カラー {name} の型は BYTE_COLOR または FLOAT_COLOR にしてください。")

        values = np.empty((len(attribute.data), 4), dtype=np.float32)
        field = "color_srgb" if attribute.data_type == "BYTE_COLOR" else "color"
        attribute.data.foreach_get(field, values.ravel())
        if attribute.domain == "POINT":
            colors[name] = values
            continue

        if loop_vertices is None:
            loop_vertices = np.empty(len(mesh.loops), dtype=np.int32)
            mesh.loops.foreach_get("vertex_index", loop_vertices)
        sums = np.zeros((vertex_count, 4), dtype=np.float64)
        np.add.at(sums, loop_vertices, values)
        counts = np.bincount(loop_vertices, minlength=vertex_count)[:, None]
        colors[name] = np.divide(sums, counts, out=np.zeros_like(sums), where=counts > 0)
    return colors


def _extract_mesh(obj, armature, uv_layer):
    positions, normals, triangles, corner_uvs = _mesh_geometry(obj, uv_layer)
    bone_names, bone_heads, bone_tails = _bones(armature)
    weights, groups = _vertex_groups(obj, bone_names)
    return fur_maps_core.MeshInput(
        positions=positions, normals=normals, triangles=triangles, corner_uvs=corner_uvs,
        bone_names=bone_names, bone_heads=bone_heads, bone_tails=bone_tails,
        weights=weights, groups=groups, colors=_vertex_colors(obj.data))


def main():
    """設定を読み、Blender のデータをコアに渡して終了する。"""
    parser = ArgumentParser(
        description="土台メッシュから毛のマップを作成する。",
        usage="%(prog)s CONFIG [--paint-overlay TARGET=PATH ...]", add_help=False)
    parser._positionals.title = "位置引数"
    parser._optionals.title = "オプション"
    parser.add_argument("config", metavar="CONFIG", help="設定 JSON のパスまたは JSON 文字列")
    parser.add_argument("--paint-overlay", action="append", default=[], metavar="TARGET=PATH",
                        help="補正対象と PNG のパス")
    parser.add_argument("-h", "--help", action="help", help="使い方を表示して終了する")
    args = parser.parse_args(common.config_args(sys.argv))

    overrides = {}
    for item in args.paint_overlay:
        target, separator, path = item.partition("=")
        if not separator or target not in ("length", "density", "clump", "flow") or not path or "\0" in path:
            parser.error("補正マスクは TARGET=PATH で指定してください。")
        overrides[target] = os.path.abspath(path)

    try:
        config, input_path, output_dir, overlays = _read_config(args.config, overrides)
        input_entry = common.file_entry(input_path)
        try:
            import bpy
        except ImportError:
            raise fur_maps_core.FurMapsError("Blender に付属する Python で実行してください。") from None

        _load_input(bpy, input_path)
        bpy.context.view_layer.update()
        obj, armature = _select_objects(bpy, config)
        uv_layer = _select_uv_layer(obj.data, config.get("uv_map"))
        mesh = _extract_mesh(obj, armature, uv_layer)
        source = {
            "input": input_entry,
            "mesh_object": obj.name,
            "armature_object": armature.name,
            "uv_map": uv_layer.name,
            "blender_version": bpy.app.version_string,
        }
        fur_maps_core.save_mesh_npz(os.path.join(output_dir, "fur_mesh.npz"), mesh)
        result = fur_maps_core.bake_fur_maps(
            mesh, config, output_dir, overlays=overlays, source=source,
            extra_keys=fur_maps_core.BLENDER_CONFIG_KEYS)
        common.log_line("FUR_MAPS", {"output": output_dir, "resolution": result["resolution"], **result["stats"]})
    except (fur_maps_core.FurMapsError, common.ConfigError, OSError, RuntimeError) as error:
        print(f"毛のマップを作成できません: {error}", file=sys.stderr)
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
