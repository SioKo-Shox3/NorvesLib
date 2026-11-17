"""骨格とスキン付きメッシュをエンジン向けの glTF に書き出す。"""

import json
import os
import struct
import sys
from collections import Counter
from urllib.parse import unquote, urlsplit


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common
import rig_core
import bpy_stage

import bpy


def validate_config(raw):
    """設定を正規化し、書き出し段への指定であることを確かめる。"""
    config = rig_core.validate_stage_config(raw)
    if config["stage"] != "export":
        raise rig_core.SettingsError("stage は export にしてください。")
    return config


def check_version():
    """Blender の版と演算子のオプション名を照合する。"""
    version = tuple(bpy.app.version)
    version_text = ".".join(map(str, version))
    try:
        table = rig_core.gltf_table_key(version)
    except rig_core.UnsupportedBlenderVersion:
        raise bpy_stage.StageError(
            f"Blender {version_text} は書き出しのオプション名の表に無い版です。"
        ) from None

    common.log_line("BLENDER_VERSION", {
        "version": version_text,
        "table": ".".join(map(str, table)),
    })
    properties = bpy.ops.export_scene.gltf.get_rna_type().properties.keys()
    missing = sorted(set(rig_core.gltf_export_option_names(version)) - set(properties))
    if missing:
        raise bpy_stage.StageError(
            f"Blender {version_text} の書き出し演算子にオプションがありません: "
            + ", ".join(missing)
        )
    return version


def prepare_actions(armature, settings):
    """対象アクションを選び、必要なら静止したアクションを作る。"""
    actions = sorted(bpy.data.actions, key=lambda action: action.name)
    selected = settings["actions"]
    if selected is not None:
        missing = sorted(set(selected) - {action.name for action in actions})
        if missing:
            raise bpy_stage.StageError("指定されたアクションがありません: " + ", ".join(missing))
        selected_names = set(selected)
        for action in actions:
            if action.name not in selected_names:
                bpy.data.actions.remove(action)
        actions = sorted(bpy.data.actions, key=lambda action: action.name)

    made_rest = False
    if not actions and settings["rest_clip_if_empty"]:
        if not armature.pose.bones:
            raise bpy_stage.StageError("静止クリップを作るための骨がありません。")
        animation = armature.animation_data_create()
        animation.action = bpy.data.actions.new("rest")
        for bone in armature.pose.bones:
            bone.location = (0.0, 0.0, 0.0)
            bone.rotation_mode = "QUATERNION"
            bone.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
            bone.scale = (1.0, 1.0, 1.0)
            for frame in (0, 1):
                for path in ("location", "rotation_quaternion", "scale"):
                    if not bone.keyframe_insert(data_path=path, frame=frame):
                        raise bpy_stage.StageError(
                            f"骨「{bone.name}」の {path} にキーを打てません。"
                        )
        actions = [animation.action]
        made_rest = True
    elif not actions:
        common.log_line("WARNING", {"message": "アクションがないためクリップを作りません。"})

    if actions:
        # 単一骨格の未割り当てアクションも書き出し対象に含める。
        armature.animation_data_create()
    for action in actions:
        start, end = action.frame_range
        common.log_line("EXPORT_CLIP", {
            "name": action.name,
            "frame_start": float(start),
            "frame_end": float(end),
        })
    return actions, made_rest


def read_export(path, export_format):
    """書き出した glTF の JSON を読み、生成結果を確認できる形にする。"""
    if export_format == "gltf":
        with open(path, "r", encoding="utf-8-sig") as file:
            return json.load(file)

    with open(path, "rb") as file:
        header = file.read(20)
        if len(header) != 20:
            raise bpy_stage.StageError("書き出した GLB のヘッダーが不正です。")
        magic, version, size, json_size, chunk_type = struct.unpack("<4sIII4s", header)
        if magic != b"glTF" or version != 2 or chunk_type != b"JSON" or json_size > size - 20:
            raise bpy_stage.StageError("書き出した GLB の JSON チャンクが不正です。")
        data = file.read(json_size)
        if len(data) != json_size:
            raise bpy_stage.StageError("書き出した GLB の JSON チャンクが途中で切れています。")
        return json.loads(data.decode("utf-8"))


def output_files(path, export_format, document):
    """出力本体と、分離形式が参照するローカルファイルを列挙する。"""
    outputs = [path]
    if export_format == "glb":
        return outputs

    directory = os.path.dirname(os.path.abspath(path))
    referenced = set()
    for collection in ("buffers", "images"):
        for entry in document.get(collection, []):
            uri = entry.get("uri")
            if not uri:
                continue
            parsed = urlsplit(uri)
            relative = unquote(parsed.path)
            if parsed.scheme or parsed.netloc or os.path.isabs(relative):
                continue
            referenced.add(os.path.abspath(os.path.join(directory, relative)).replace("\\", "/"))
    outputs.extend(sorted(referenced - {os.path.abspath(path).replace("\\", "/")}))
    return outputs


def run(config):
    """シーンを読み、骨格とメッシュを選んで書き出す。"""
    version = check_version()
    settings = config["settings"]
    expected_extension = rig_core.export_extension(settings)
    if os.path.splitext(config["output"])[1].lower() != expected_extension:
        raise bpy_stage.StageError(
            f"出力ファイルの拡張子は {settings['format']} 形式に合わせて "
            f"{expected_extension} にしてください。"
        )

    spec_path = config["spec"]
    try:
        spec = common.read_json(spec_path)
    except (OSError, ValueError):
        raise bpy_stage.StageError(f"骨格の仕様を読み取れません: {spec_path}") from None
    allowed = spec.get("interpolations") if isinstance(spec, dict) else None
    if not isinstance(allowed, list) or not all(isinstance(value, str) for value in allowed):
        raise bpy_stage.StageError(
            f"骨格の仕様の interpolations は文字列の配列にしてください: {spec_path}"
        )

    objects = bpy_stage.load_scene_from(config["input"])
    armature = bpy_stage.pick_one(bpy_stage.armatures(objects), None, "骨格")
    bound = []
    unbound = []
    for obj in bpy_stage.meshes(objects):
        if any(mod.type == "ARMATURE" and mod.object == armature for mod in obj.modifiers):
            bound.append(obj)
        else:
            unbound.append(obj.name)
    if unbound:
        raise bpy_stage.StageError("骨格に結び付いていないメッシュがあります: " + ", ".join(unbound))
    if not bound:
        raise bpy_stage.StageError("骨格に結び付いたメッシュがありません。")

    joined_count = len(bound)
    mesh = bpy_stage.join_meshes(bound)
    actions, made_rest = prepare_actions(armature, settings)
    if made_rest and settings["frame_step"] != 1:
        # 静止クリップは常にフレーム 0 と 1 の両方をサンプリングする。
        settings = {**settings, "frame_step": 1}
        common.log_line("WARNING", {
            "message": "静止クリップの 2 サンプルを保つため frame_step を 1 にします。"
        })

    bpy_stage.select_only([mesh, armature], armature)
    os.makedirs(os.path.dirname(os.path.abspath(config["output"])), exist_ok=True)
    result = bpy.ops.export_scene.gltf(
        **rig_core.gltf_export_kwargs(version, settings, config["output"])
    )
    if "FINISHED" not in result or not os.path.isfile(config["output"]):
        raise bpy_stage.StageError("glTF の書き出しを完了できません。")

    document = read_export(config["output"], settings["format"])
    outputs = output_files(config["output"], settings["format"], document)
    exported = document.get("animations", [])
    common.log_line("MESH_STATS", {
        "stage": "export",
        "file": os.path.basename(config["output"]),
        "format": settings["format"],
        "meshes_joined": joined_count,
        "materials": bpy_stage.mesh_counts(mesh)["materials"],
        "deform_bones": sum(1 for bone in armature.data.bones if bone.use_deform),
        "clips": len(exported),
    })
    problems = []
    missing = [path for path in outputs if not os.path.isfile(path)]
    if missing:
        problems.append("書き出した glTF の参照ファイルがありません: " + ", ".join(missing))
    expected = Counter(action.name for action in actions)
    actual = Counter(clip.get("name", "") for clip in exported)
    if actual != expected:
        problems.append("書き出したクリップ名または個数が指定されたアクションと一致しません。")
    for clip in exported:
        interpolations = (
            sampler.get("interpolation", "LINEAR") for sampler in clip.get("samplers", [])
        )
        rejected = sorted({str(value) for value in interpolations if value not in allowed})
        if rejected:
            problems.append(
                f"骨格の仕様が許さない補間が含まれています: {clip.get('name', '')}"
                f"（{', '.join(rejected)}）"
            )
    if problems:
        return bpy_stage.StageResult(
            inputs=[config["input"]], outputs=outputs, exit_code=1,
            message="\n".join(problems),
        )
    return bpy_stage.StageResult(inputs=[config["input"]], outputs=outputs)


if __name__ == "__main__":
    bpy_stage.run_stage("export", validate_config, run)
