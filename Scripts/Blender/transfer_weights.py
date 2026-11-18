"""土台メッシュのスキンのウェイトを保持、またはドナーから転送して整える。"""

import os
import sys


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig_core
import bpy_stage
from common import log_line

import bpy


def validate(raw):
    """共通設定を正規化し、この段の設定であることを確かめる。"""
    config = rig_core.validate_stage_config(raw)
    if config["stage"] != "weights":
        raise rig_core.SettingsError("stage は weights にしてください。")
    return config


def _finished(result, label):
    if "FINISHED" not in result:
        raise bpy_stage.StageError(f"{label}を完了できません。")


def _target_mesh(objects, name):
    candidates = bpy_stage.meshes(objects)
    if name:
        return bpy_stage.pick_one(candidates, name, "対象のメッシュ")
    marked = [obj for obj in candidates if "norves_source_to_base" in obj]
    return bpy_stage.pick_one(
        marked if marked else candidates, None, "対象のメッシュ"
    )


def _base_armature(target):
    for modifier in target.modifiers:
        if modifier.type == "ARMATURE" and modifier.object is not None:
            if modifier.object.type == "ARMATURE":
                return modifier.object
    if target.parent is not None and target.parent.type == "ARMATURE":
        return target.parent
    return None


def _deform_names(armature):
    return {bone.name for bone in armature.data.bones if bone.use_deform}


def _has_action(armature):
    if armature is None:
        return False
    for owner in (armature, armature.data):
        animation = owner.animation_data
        if animation is not None and (
            animation.action is not None
            or any(strip.action is not None
                   for track in animation.nla_tracks for strip in track.strips)
        ):
            return True
    return False


def _align_donor(target, donor, armature):
    """親子の変換を二重に掛けず、取り込み元から土台の座標へ移す。"""
    if "norves_source_to_base" not in target:
        raise bpy_stage.StageError("対象のメッシュに norves_source_to_base がありません。")
    try:
        matrix = rig_core.flat_to_matrix4(target["norves_source_to_base"])
    except rig_core.RigCoreError as error:
        raise bpy_stage.StageError(f"土台の位置合わせの行列が不正です: {error}") from None
    if _has_action(armature):
        raise bpy_stage.StageError("ドナーの骨格にアクションがあり、変換を適用できません。")
    objects = [armature, donor] if armature is not None else [donor]
    worlds = [obj.matrix_world.copy() for obj in objects]
    for obj, world in zip(objects, worlds):
        obj.parent = None
        obj.matrix_world = world
    for obj in objects:
        bpy_stage.transform_object(obj, matrix)
        bpy_stage.apply_object_transform(obj)


def _rename_groups(donor, result, unmapped, armature=None, joint_map=None):
    """元の添字で重みを集計し、循環する改名や多対一の対応をまとめて扱う。"""
    mapping = dict(result.renames)
    if unmapped == "keep":
        mapping.update((name, name) for name in result.unmapped)
    destinations = {
        group.index: mapping[group.name]
        for group in donor.vertex_groups if group.name in mapping
    }
    weights = {name: {} for name in sorted(set(mapping.values()))}
    for vertex in donor.data.vertices:
        for membership in vertex.groups:
            name = destinations.get(membership.group)
            if name is not None and membership.weight > 0.0:
                totals = weights[name]
                totals[vertex.index] = min(
                    1.0, totals.get(vertex.index, 0.0) + membership.weight
                )
    # 骨名の変更が頂点グループにも伝わる前に、元の重みを確保する。
    if armature is not None:
        _rename_bones(armature, joint_map)
    donor.vertex_groups.clear()
    for name, values in weights.items():
        group = donor.vertex_groups.new(name=name)
        if group.name != name:
            raise bpy_stage.StageError(f"頂点グループ名を設定できません: {name}")
        for index, weight in values.items():
            group.add([index], weight, "REPLACE")


def _rename_bones(armature, joint_map):
    """骨名を一時名経由で付け替え、同じ行き先の骨は代表へまとめる。"""
    if joint_map is None:
        return
    bpy_stage.select_only([armature], armature)
    with bpy.context.temp_override(
        object=armature, active_object=armature,
        selected_objects=[armature], selected_editable_objects=[armature],
    ):
        _finished(bpy.ops.object.mode_set(mode="EDIT"), "骨格の編集モードへの切り替え")
        try:
            bones = armature.data.edit_bones
            buckets = {}
            for bone in sorted(bones, key=lambda item: item.name):
                buckets.setdefault(joint_map.get(bone.name, bone.name), []).append(bone)
            representatives = {}
            removed = []
            for name, sources in buckets.items():
                representative = next((bone for bone in sources if bone.name == name), sources[0])
                representative.use_deform = any(bone.use_deform for bone in sources)
                representatives[name] = representative
                removed.extend(bone for bone in sources if bone != representative)
            # 消す骨の子を代表へつなぐ。代表が子孫なら、祖先側へ逃がして循環を避ける。
            for bone in removed:
                replacement = representatives[joint_map.get(bone.name, bone.name)]
                for child in list(bone.children):
                    ancestor = replacement
                    while ancestor is not None and ancestor != child:
                        ancestor = ancestor.parent
                    child.use_connect = False
                    child.parent = replacement if ancestor is None else bone.parent
                bones.remove(bone)
            # Blender が自動で付ける連番を最終名へ混ぜない。
            for index, bone in enumerate(representatives.values()):
                temporary = f"__norves_joint_{index}"
                while temporary in bones or temporary in buckets:
                    temporary += "_"
                bone.name = temporary
            for name, bone in representatives.items():
                bone.name = name
                if bone.name != name:
                    raise bpy_stage.StageError(f"骨名を設定できません: {name}")
        finally:
            _finished(bpy.ops.object.mode_set(mode="OBJECT"), "骨格のオブジェクトモードへの切り替え")


def _bind_armature(target, armature):
    modifiers = [modifier for modifier in target.modifiers if modifier.type == "ARMATURE"]
    if not modifiers:
        modifiers = [target.modifiers.new(name="WeightsArmature", type="ARMATURE")]
    for modifier in modifiers:
        modifier.object = armature
    world = target.matrix_world.copy()
    target.parent = armature
    target.parent_type = "OBJECT"
    target.matrix_world = world


def _transfer(target, base_armature, settings):
    imported = bpy_stage.import_model(settings["donor"])
    donor_meshes = bpy_stage.meshes(imported)
    donor = bpy_stage.pick_one(
        donor_meshes if settings["donor_object"] else
        [obj for obj in donor_meshes if len(obj.vertex_groups) > 0],
        settings["donor_object"], "ドナーのメッシュ",
    )
    donor_armatures = bpy_stage.armatures(imported)
    donor_armature = None
    if donor_armatures or settings["armature"] == "donor":
        donor_armature = bpy_stage.pick_one(donor_armatures, None, "ドナーの骨格")
    if settings["donor_space"] == "source":
        _align_donor(target, donor, donor_armature)

    destination = base_armature if settings["armature"] == "base" else donor_armature
    known = _deform_names(destination)
    if settings["armature"] == "donor" and settings["joint_map"] is not None:
        known = settings["joint_map"].values()
    result = rig_core.apply_joint_map(
        sorted(group.name for group in donor.vertex_groups),
        settings["joint_map"], known_joints=known,
    )
    log_line("JOINT_MAP", {
        "renamed": len(result.renames), "unmapped": list(result.unmapped),
        "merged": result.merged, "unknown_targets": list(result.unknown_targets),
        "unused_keys": list(result.unused_keys),
    })
    if result.unmapped and settings["unmapped"] == "error":
        raise bpy_stage.StageError(f"対応表にない頂点グループがあります: {', '.join(result.unmapped)}")
    if settings["armature"] == "base" and result.unknown_targets:
        raise bpy_stage.StageError(f"土台の骨格にない変形の骨です: {', '.join(result.unknown_targets)}")
    _rename_groups(
        donor, result, settings["unmapped"],
        donor_armature if settings["armature"] == "donor" else None,
        settings["joint_map"],
    )

    bone_names = {bone.name for bone in destination.data.bones}
    for group in list(target.vertex_groups):
        if group.name in bone_names:
            target.vertex_groups.remove(group)
    modifier = target.modifiers.new(name="WeightsTransfer", type="DATA_TRANSFER")
    modifier.object = donor
    modifier.use_vert_data = True
    modifier.data_types_verts = {"VGROUP_WEIGHTS"}
    modifier.vert_mapping = "POLYINTERP_NEAREST"
    modifier.layers_vgroup_select_src = "ALL"
    modifier.layers_vgroup_select_dst = "NAME"
    modifier.mix_mode = "REPLACE"
    modifier.mix_factor = 1.0
    bpy_stage.select_only([target], target)
    with bpy.context.temp_override(
        object=target, active_object=target,
        selected_objects=[target], selected_editable_objects=[target],
    ):
        _finished(
            bpy.ops.object.datalayout_transfer(modifier=modifier.name),
            "転送先の頂点グループの作成",
        )
    bpy_stage.apply_modifier(target, modifier)

    if settings["armature"] == "donor":
        _bind_armature(target, donor_armature)
    cleanup = [donor]
    if settings["armature"] == "base" and donor_armature is not None:
        cleanup.append(donor_armature)
    bpy_stage.remove_objects(cleanup)
    if settings["armature"] == "donor" and base_armature is not None:
        users = bpy.data.user_map(subset={base_armature}).get(base_armature, set())
        if not any(isinstance(user, bpy.types.Object) for user in users):
            bpy_stage.remove_objects([base_armature])
    return destination


def _normalize(target, armature, settings):
    """変形用グループを掃除、正規化し、影響数を制限した後にも正規化する。"""
    deform = _deform_names(armature)
    groups = [group for group in target.vertex_groups if group.name in deform]
    if not groups:
        return
    locks = [(group, group.lock_weight) for group in groups]
    bpy_stage.select_only([target], target)
    try:
        for group, _ in locks:
            group.lock_weight = False
        with bpy.context.temp_override(
            object=target, active_object=target,
            selected_objects=[target], selected_editable_objects=[target],
        ):
            if settings["clean_threshold"] > 0.0:
                _finished(bpy.ops.object.vertex_group_clean(
                    group_select_mode="BONE_DEFORM", limit=settings["clean_threshold"],
                    keep_single=True,
                ), "微小なウェイトの除去")
            _finished(bpy.ops.object.vertex_group_normalize_all(
                group_select_mode="BONE_DEFORM", lock_active=False,
            ), "ウェイトの正規化")
            _finished(bpy.ops.object.vertex_group_limit_total(
                group_select_mode="BONE_DEFORM", limit=settings["max_influences"],
            ), "ウェイトの影響数の制限")
            _finished(bpy.ops.object.vertex_group_normalize_all(
                group_select_mode="BONE_DEFORM", lock_active=False,
            ), "影響数の制限後の正規化")
    finally:
        for group, locked in locks:
            group.lock_weight = locked


def _log_stats(target, armature, mode):
    deform = _deform_names(armature)
    indices = {group.index for group in target.vertex_groups if group.name in deform}
    zero_weight_vertices = 0
    max_influences = 0
    for vertex in target.data.vertices:
        weights = [membership.weight for membership in vertex.groups
                   if membership.group in indices and membership.weight > 0.0]
        zero_weight_vertices += sum(weights) == 0.0
        max_influences = max(max_influences, len(weights))
    counts = bpy_stage.mesh_counts(target)
    log_line("MESH_STATS", {
        "stage": "weights", "mode": mode, "vertices": counts["vertices"],
        "vertex_groups": counts["vertex_groups"], "deform_groups": len(indices),
        "zero_weight_vertices": zero_weight_vertices, "max_influences": max_influences,
    })


def run(config):
    """ウェイトを整え、後続の検査段へ渡す blend を保存する。"""
    settings = config["settings"]
    objects = bpy_stage.load_scene_from(config["input"])
    target = _target_mesh(objects, settings["target_object"])
    armature = _base_armature(target)
    if settings["armature"] == "base" and armature is None:
        raise bpy_stage.StageError("土台に骨格がありません。")
    if settings["mode"] == "transfer":
        armature = _transfer(target, armature, settings)
    _normalize(target, armature, settings)
    _log_stats(target, armature, settings["mode"])
    bpy_stage.save_blend(config["output"])
    inputs = [config["input"]]
    if settings["mode"] == "transfer":
        inputs.append(settings["donor"])
    return bpy_stage.StageResult(inputs=inputs, outputs=[config["output"]])


if __name__ == "__main__":
    bpy_stage.run_stage("weights", validate, run)
