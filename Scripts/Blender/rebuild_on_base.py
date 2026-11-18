"""共通の土台を AI メッシュに張り直し、色と法線を焼く。"""

import os
import sys


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy
import bpy_stage
import common
import rig_core


def validate(raw):
    config = rig_core.validate_stage_config(raw)
    if config["stage"] != "rebuild":
        raise rig_core.SettingsError("stage は rebuild にしてください。")
    return config


def load_source(path, name):
    imported = bpy_stage.import_model(path)
    imported_meshes = bpy_stage.meshes(imported)
    discarded = [obj for obj in imported if obj.type != "MESH"]
    if name is None:
        source = bpy_stage.join_meshes(imported_meshes)
    else:
        source = bpy_stage.pick_one(imported_meshes, name, "AI のメッシュ")
        discarded.extend(obj for obj in imported_meshes if obj != source)

    # 親を削除しても、取り込まれたメッシュのワールド座標を保つ。
    source_world = source.matrix_world.copy()
    bpy_stage.remove_objects(discarded)
    source.matrix_world = source_world
    bpy.context.view_layer.update()
    return source


def align_source(source, base, settings):
    landmarks = settings["landmarks"]
    try:
        if landmarks is not None:
            similarity = rig_core.procrustes_similarity(
                [landmark["source"] for landmark in landmarks],
                [landmark["target"] for landmark in landmarks],
            )
            common.log_line("ALIGN", {
                "mode": "landmarks",
                "points": len(landmarks),
                "scale": similarity.scale,
                "rms_error": similarity.rms_error,
            })
        else:
            base_bounds = bpy_stage.world_bounds(bpy_stage.world_vertices(base))
            similarity = rig_core.bbox_similarity(
                bpy_stage.world_vertices(source),
                *base_bounds,
                settings["source_rotation_deg"],
                settings["bbox_scale_axis"],
                settings["bbox_anchor"],
            )
            common.log_line("ALIGN", {"mode": "bbox", "scale": similarity.scale})
    except rig_core.AlignmentError as error:
        raise bpy_stage.StageError(str(error)) from error

    matrix = similarity.matrix4()
    bpy_stage.transform_object(source, matrix)
    bpy_stage.apply_object_transform(source)
    base["norves_source_to_base"] = rig_core.matrix4_to_flat(matrix)


def smooth_object(obj, factor, iterations):
    modifier = obj.modifiers.new(name="NorvesSmooth", type="SMOOTH")
    modifier.factor = factor
    modifier.iterations = iterations
    bpy_stage.apply_modifier(obj, modifier)


def rebuild_mesh(base, source, settings, body_length):
    source_bvh = bpy_stage.bvh_from_object(source)
    displacement = rig_core.distance_stats(
        bpy_stage.nearest_distances(bpy_stage.world_vertices(base), source_bvh),
        body_length,
    )
    for _ in range(settings["iterations"]):
        modifier = base.modifiers.new(name="NorvesShrinkwrap", type="SHRINKWRAP")
        modifier.target = source
        modifier.wrap_method = settings["wrap_method"]
        modifier.wrap_mode = "ON_SURFACE"
        if settings["wrap_method"] == "PROJECT":
            modifier.use_project_x = False
            modifier.use_project_y = False
            modifier.use_project_z = False
            modifier.use_negative_direction = True
            modifier.use_positive_direction = True
            modifier.project_limit = settings["project_limit"]
        bpy_stage.apply_modifier(base, modifier)
        if settings["smooth_repeat"] > 0:
            smooth_object(base, settings["smooth_factor"], settings["smooth_repeat"])

    residual = rig_core.distance_stats(
        bpy_stage.nearest_distances(bpy_stage.world_vertices(base), source_bvh),
        body_length,
    )
    base_counts = bpy_stage.mesh_counts(base)
    source_counts = bpy_stage.mesh_counts(source)
    common.log_line("MESH_STATS", {
        "stage": "rebuild",
        "base_vertices": base_counts["vertices"],
        "base_triangles": base_counts["triangles"],
        "source_vertices": source_counts["vertices"],
        "source_triangles": source_counts["triangles"],
        "body_length": body_length,
        "residual_max": residual["max"],
        "residual_p95": residual["p95"],
        "residual_max_ratio": residual["max_ratio"],
        "residual_p95_ratio": residual["p95_ratio"],
        "displacement_max": displacement["max"],
        "displacement_p95": displacement["p95"],
        "displacement_max_ratio": displacement["max_ratio"],
        "displacement_p95_ratio": displacement["p95_ratio"],
        "metric": settings["distance_metric"],
        "statistic": settings["distance_statistic"],
        "threshold_ratio": settings["max_distance_ratio"],
    })
    return {"residual": residual, "displacement": displacement}


def set_active_image(nodes, image_node):
    for node in nodes:
        node.select = False
    image_node.select = True
    nodes.active = image_node


def bake_to_image(base, source, nodes, image_node, options, **pass_options):
    set_active_image(nodes, image_node)
    bpy_stage.select_only([source, base], base)
    with bpy.context.temp_override(
        object=base,
        active_object=base,
        selected_objects=[source, base],
        selected_editable_objects=[source, base],
    ):
        result = bpy.ops.object.bake(**options, **pass_options)
    if "FINISHED" not in result:
        raise bpy_stage.StageError("画像のベイクを完了できません。")


def bake_images(base, source, settings, output):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = settings["bake_samples"]
    scene.cycles.seed = settings["bake_seed"]
    scene.cycles.use_denoising = False

    directory = os.path.dirname(os.path.abspath(output))
    stem = os.path.splitext(os.path.basename(output))[0]
    size = settings["bake_size"]
    color_name = f"{stem}_basecolor"
    normal_name = f"{stem}_normal"
    color_path = os.path.join(directory, f"{color_name}.png")
    normal_path = os.path.join(directory, f"{normal_name}.png")
    color_image = bpy.data.images.new(color_name, width=size, height=size)
    color_image.colorspace_settings.name = "sRGB"
    normal_image = bpy.data.images.new(normal_name, width=size, height=size)
    normal_image.colorspace_settings.name = "Non-Color"

    material = bpy.data.materials.new(name="NorvesBaked")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    output_node = nodes.new("ShaderNodeOutputMaterial")
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    color_node = nodes.new("ShaderNodeTexImage")
    color_node.image = color_image
    normal_node = nodes.new("ShaderNodeTexImage")
    normal_node.image = normal_image
    normal_map = nodes.new("ShaderNodeNormalMap")
    normal_map.uv_map = base.data.uv_layers[0].name
    links = material.node_tree.links
    links.new(shader.outputs["BSDF"], output_node.inputs["Surface"])

    if not base.material_slots:
        base.data.materials.append(material)
    else:
        for slot in base.material_slots:
            slot.material = material

    options = {
        "use_selected_to_active": True,
        "use_cage": True,
        "cage_object": "",
        "cage_extrusion": settings["cage_extrusion"],
        "max_ray_distance": settings["max_ray_distance"],
        "margin": settings["bake_margin"],
        "use_clear": True,
        "target": "IMAGE_TEXTURES",
        "save_mode": "INTERNAL",
        "uv_layer": base.data.uv_layers[0].name,
        "normal_r": "POS_X",
        "normal_g": "POS_Y",
        "normal_b": "POS_Z",
    }
    bake_to_image(
        base, source, nodes, color_node, options,
        type="DIFFUSE", pass_filter={"COLOR"},
    )

    normal_source = source
    presmoothed = None
    if settings["normal_presmooth_iterations"] > 0:
        presmoothed = source.copy()
        presmoothed.data = source.data.copy()
        scene.collection.objects.link(presmoothed)
        smooth_object(
            presmoothed,
            settings["normal_presmooth_factor"],
            settings["normal_presmooth_iterations"],
        )
        normal_source = presmoothed
    bake_to_image(
        base, normal_source, nodes, normal_node, options,
        type="NORMAL", normal_space="TANGENT",
    )

    links.new(color_node.outputs["Color"], shader.inputs["Base Color"])
    links.new(normal_node.outputs["Color"], normal_map.inputs["Color"])
    links.new(normal_map.outputs["Normal"], shader.inputs["Normal"])
    os.makedirs(directory, exist_ok=True)
    for image, path in ((color_image, color_path), (normal_image, normal_path)):
        image.filepath_raw = path
        image.file_format = "PNG"
        image.save()
        image.pack()

    common.log_line("BAKE", {
        "size": size,
        "samples": settings["bake_samples"],
        "seed": settings["bake_seed"],
        "device": "CPU",
        "presmooth_iterations": settings["normal_presmooth_iterations"],
    })
    return color_path, normal_path, presmoothed


def run(config):
    settings = config["settings"]
    base_objects = bpy_stage.load_scene_from(settings["base"])
    base = bpy_stage.pick_one(
        bpy_stage.meshes(base_objects), settings["base_object"], "土台のメッシュ",
    )
    if not base.data.uv_layers:
        raise bpy_stage.StageError("土台のメッシュに UV がありません。")
    # 共有メッシュの編集が、土台以外のオブジェクトに及ばないようにする。
    if base.data.users > 1:
        base.data = base.data.copy()
    base.data.uv_layers.active_index = 0
    base.data.uv_layers[0].active_render = True

    source = load_source(config["input"], settings["source_object"])
    align_source(source, base, settings)

    body_length = settings["body_length"]
    if body_length is None:
        minimum, maximum = bpy_stage.world_bounds(bpy_stage.world_vertices(source))
        body_length = max(high - low for low, high in zip(minimum, maximum))
        if body_length <= 0:
            raise bpy_stage.StageError("AI のメッシュの箱から体長を求められません。")

    statistics = rebuild_mesh(base, source, settings, body_length)
    color_path, normal_path, presmoothed = bake_images(
        base, source, settings, config["output"],
    )
    bpy_stage.remove_objects([source] + ([presmoothed] if presmoothed is not None else []))
    bpy_stage.save_blend(config["output"])

    result = bpy_stage.StageResult(
        inputs=[config["input"], settings["base"]],
        outputs=[config["output"], color_path, normal_path],
    )
    metric = settings["distance_metric"]
    statistic = settings["distance_statistic"]
    ratio = statistics[metric][f"{statistic}_ratio"]
    threshold = settings["max_distance_ratio"]
    if ratio > threshold:
        result.exit_code = 1
        result.message = (
            f"Shrinkwrap の後の距離が体長の比 {ratio} でしきい値 {threshold} を超えました"
            f"（metric={metric} statistic={statistic}）"
        )
    return result


if __name__ == "__main__":
    bpy_stage.run_stage("rebuild", validate, run)
