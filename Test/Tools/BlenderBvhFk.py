# Blender の BVH インポーターで各フレームの関節位置を JSON に書き出す。
import argparse
import json
import math
import re
import sys


class JapaneseArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        self.exit(2, f"引数が不正です: {message}\n")


def parse_frames(value):
    if not re.fullmatch(r"[0-9]+(?:,[0-9]+)*", value):
        raise argparse.ArgumentTypeError("フレームは数字をカンマで区切って指定してください")
    try:
        frames = [int(part) for part in value.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("フレーム番号が大きすぎます") from error
    if len(set(frames)) != len(frames):
        raise argparse.ArgumentTypeError("同じフレームを複数指定できません")
    return frames


def parse_arguments(argv):
    parser = JapaneseArgumentParser(description="BVH の関節位置を Blender で書き出します")
    parser.add_argument("--bvh", required=True, help="入力する BVH ファイル")
    parser.add_argument("--frames", required=True, type=parse_frames, help="書き出すフレーム番号")
    parser.add_argument("--out", required=True, help="出力する JSON ファイル")
    arguments = argv[argv.index("--") + 1:] if "--" in argv else []
    return parser.parse_args(arguments)


def read_frame_count(path):
    with open(path, encoding="utf-8-sig", errors="replace") as source:
        for line in source:
            if re.match(r"\s*Frames\b", line):
                match = re.fullmatch(r"\s*Frames\s*:\s*([0-9]+)\s*", line)
                if not match:
                    raise ValueError("BVH の Frames 行が不正です")
                try:
                    return int(match.group(1))
                except ValueError as error:
                    raise ValueError("BVH のフレーム数が大きすぎます") from error
    raise ValueError("BVH に Frames 行がありません")


def check_identity(matrix):
    for row in range(4):
        for column in range(4):
            value = float(matrix[row][column])
            expected = 1.0 if row == column else 0.0
            if not math.isfinite(value) or abs(value - expected) > 1e-12:
                raise RuntimeError("アーマチュアのワールド行列が単位行列ではありません")


def main():
    args = parse_arguments(sys.argv)
    try:
        frame_count = read_frame_count(args.bvh)
    except (OSError, ValueError) as error:
        print(f"BVH のフレーム数を読み取れません: {error}", file=sys.stderr)
        return 2
    for frame in args.frames:
        if frame >= frame_count:
            print(f"フレームが範囲外です: frame={frame} frames={frame_count}", file=sys.stderr)
            return 2

    import bpy

    try:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        before = {obj.as_pointer() for obj in bpy.data.objects if obj.type == "ARMATURE"}
        # Blender と同じ軸を指定して軸変換を恒等にし、位置チャンネルは OFFSET を置き換える。
        bpy.ops.import_anim.bvh(
            filepath=args.bvh,
            target="ARMATURE",
            global_scale=1.0,
            frame_start=1,
            use_fps_scale=False,
            update_scene_fps=False,
            update_scene_duration=True,
            use_cyclic=False,
            rotate_mode="NATIVE",
            axis_forward="Y",
            axis_up="Z",
        )
        added = [obj for obj in bpy.data.objects
                 if obj.type == "ARMATURE" and obj.as_pointer() not in before]
        if len(added) != 1:
            raise RuntimeError(f"読み込んだアーマチュアが 1 個ではありません: count={len(added)}")
        armature = added[0]
        check_identity(armature.matrix_world)
        scene = bpy.context.scene
        positions = []
        # BVH の最初の行は Blender のフレーム 1 に置かれる。
        for frame in args.frames:
            scene.frame_set(1 + frame)
            check_identity(armature.matrix_world)
            for bone in armature.pose.bones:
                point = armature.matrix_world @ bone.head
                coordinates = [float(point.x), float(point.y), float(point.z)]
                if not all(math.isfinite(value) for value in coordinates):
                    raise RuntimeError(f"関節位置が有限値ではありません: joint={bone.name} frame={frame}")
                # Blender の bone 名は 63 バイトまでなので、長い名前は切り詰められる。
                positions.append({"joint": bone.name, "frame": frame,
                                  "x": coordinates[0], "y": coordinates[1], "z": coordinates[2]})

        result = {"format": "norves-bvh-fk", "version": 1,
                  "producer": "Blender " + bpy.app.version_string,
                  "position_convention": "blender_importer",
                  "frames": args.frames, "positions": positions}
        with open(args.out, "w", encoding="utf-8", newline="\n") as output:
            json.dump(result, output, ensure_ascii=False, allow_nan=False)
            output.write("\n")
    except Exception as error:
        print(f"BVH の取り込みまたは書き出しに失敗しました: {error}", file=sys.stderr)
        return 1
    print(f"BVH の FK を書き出しました: frames={len(args.frames)} positions={len(positions)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
