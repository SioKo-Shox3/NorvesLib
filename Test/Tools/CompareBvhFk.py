# BVH の関節位置を名前とフレームで突き合わせ、許容差を超えた位置を報告する。
import argparse
import copy
import json
import math
import sys


class JapaneseArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        self.exit(2, f"引数が不正です: {message}\n")


def parse_tolerance(value):
    try:
        tolerance = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("許容差は正の有限値にしてください") from error
    if not math.isfinite(tolerance) or tolerance <= 0:
        raise argparse.ArgumentTypeError("許容差は正の有限値にしてください")
    return tolerance


def read_document(path):
    try:
        with open(path, encoding="utf-8-sig") as source:
            return json.load(source)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"JSON を読み取れません: path={path} {error}") from error


def index_document(document):
    if not isinstance(document, dict) or document.get("format") != "norves-bvh-fk":
        raise ValueError("JSON の format が norves-bvh-fk ではありません")
    version = document.get("version")
    if type(version) is not int or version != 1:
        raise ValueError("JSON の version が 1 ではありません")
    if not isinstance(document.get("producer"), str) or not isinstance(document.get("position_convention"), str):
        raise ValueError("JSON の producer または position_convention が文字列ではありません")
    frames = document.get("frames")
    if not isinstance(frames, list) or any(type(frame) is not int or frame < 0 for frame in frames):
        raise ValueError("JSON の frames が非負整数の配列ではありません")
    frame_set = set(frames)
    if len(frame_set) != len(frames):
        raise ValueError("JSON の frames に重複があります")
    positions = document.get("positions")
    if not isinstance(positions, list):
        raise ValueError("JSON の positions が配列ではありません")

    indexed = {}
    for position in positions:
        if not isinstance(position, dict):
            raise ValueError("JSON の関節位置がオブジェクトではありません")
        joint, frame = position.get("joint"), position.get("frame")
        if not isinstance(joint, str) or type(frame) is not int or frame < 0:
            raise ValueError("JSON の joint または frame が不正です")
        key = (joint, frame)
        if key in indexed:
            raise ValueError(f"JSON の関節とフレームが重複しています: joint={joint!r} frame={frame}")
        coordinates = []
        for axis in ("x", "y", "z"):
            value = position.get(axis)
            if type(value) not in (int, float):
                raise ValueError(f"JSON の座標が数値ではありません: joint={joint!r} frame={frame} axis={axis}")
            try:
                coordinate = float(value)
            except OverflowError as error:
                raise ValueError(f"JSON の座標が有限値ではありません: joint={joint!r} frame={frame} axis={axis}") from error
            if not math.isfinite(coordinate):
                raise ValueError(f"JSON の座標が有限値ではありません: joint={joint!r} frame={frame} axis={axis}")
            coordinates.append(coordinate)
        indexed[key] = tuple(coordinates)
    return frame_set, indexed


def compare_documents(first, second, tolerance):
    if not math.isfinite(tolerance) or tolerance <= 0:
        raise ValueError("許容差は正の有限値にしてください")
    first_frames, first_positions = index_document(first)
    second_frames, second_positions = index_document(second)
    first_keys, second_keys = set(first_positions), set(second_positions)
    common = sorted(first_keys & second_keys)
    maximum = None
    joint_maxima = {}
    for joint, frame in common:
        difference = math.dist(first_positions[(joint, frame)], second_positions[(joint, frame)])
        result = (difference, joint, frame)
        if maximum is None or difference > maximum[0]:
            maximum = result
        previous = joint_maxima.get(joint)
        if difference > tolerance and (previous is None or difference > previous[0]):
            joint_maxima[joint] = result

    only_first = sorted(first_keys - second_keys)
    only_second = sorted(second_keys - first_keys)
    frames_only_first = sorted(first_frames - second_frames)
    frames_only_second = sorted(second_frames - first_frames)
    over_tolerance = sorted(joint_maxima.values(), key=lambda item: (-item[0], item[1], item[2]))
    return {"matched_count": len(common), "maximum": maximum, "over_tolerance": over_tolerance,
            "only_a": only_first, "only_b": only_second,
            "frames_only_a": frames_only_first, "frames_only_b": frames_only_second,
            "failed": bool(over_tolerance or only_first or only_second or frames_only_first or frames_only_second)}


def display_comparison(first, second, result):
    def quoted(value):
        return json.dumps(value, ensure_ascii=False)

    print(f"比較元: producer_a={quoted(first['producer'])} position_convention_a={quoted(first['position_convention'])}")
    print(f"比較先: producer_b={quoted(second['producer'])} position_convention_b={quoted(second['position_convention'])}")
    print(f"照合した件数: matched_count={result['matched_count']}")
    maximum = result["maximum"]
    if maximum is None:
        print("最大の差: max_diff=該当なし joint=該当なし frame=該当なし")
    else:
        difference, joint, frame = maximum
        print(f"最大の差: max_diff={difference!r} joint={quoted(joint)} frame={frame}")
    print(f"許容差を超えた関節: exceeded_joint_count={len(result['over_tolerance'])}")
    for difference, joint, frame in result["over_tolerance"]:
        print(f"diff={difference!r} joint={quoted(joint)} frame={frame}")
    if result["frames_only_a"] or result["frames_only_b"]:
        only_a = json.dumps(result["frames_only_a"], separators=(",", ":"))
        only_b = json.dumps(result["frames_only_b"], separators=(",", ":"))
        print(f"フレームの食い違い: frames_only_a={only_a} frames_only_b={only_b}")
    print(f"片方にしかない関節とフレーム: only_a_count={len(result['only_a'])} only_b_count={len(result['only_b'])}")
    for side in ("a", "b"):
        for joint, frame in result["only_" + side]:
            print(f"side={side} joint={quoted(joint)} frame={frame}")


def self_test():
    example = {"format": "norves-bvh-fk", "version": 1, "producer": "test",
               "position_convention": "absolute", "frames": [0, 17],
               "positions": [{"joint": "Hips", "frame": 0, "x": 0, "y": 0, "z": 0},
                             {"joint": "Arm", "frame": 0, "x": 1, "y": 2, "z": 3},
                             {"joint": "Hips", "frame": 17, "x": 0, "y": 0, "z": 0},
                             {"joint": "Arm", "frame": 17, "x": 1, "y": 2, "z": 3}]}

    def require(condition, message):
        if not condition:
            raise AssertionError(message)

    try:
        result = compare_documents(example, copy.deepcopy(example), 1e-3)
        require(not result["failed"] and result["maximum"][0] == 0, "同じ位置の比較")

        changed = copy.deepcopy(example)
        changed["frames"].reverse()
        changed["positions"].reverse()
        result = compare_documents(example, changed, 1e-3)
        require(not result["failed"], "フレームと関節の順序が異なる比較")

        changed = copy.deepcopy(example)
        changed["positions"][1]["x"] += 2e-3
        result = compare_documents(example, changed, 1e-3)
        require(result["failed"] and result["over_tolerance"][0][1:] == ("Arm", 0), "許容差超過の比較")

        changed["positions"][1]["x"] = 1 + 5e-4
        result = compare_documents(example, changed, 1e-3)
        require(not result["failed"], "許容差以内の比較")

        changed = copy.deepcopy(example)
        changed["positions"][0]["x"] = 1e-3
        result = compare_documents(example, changed, 1e-3)
        require(not result["failed"], "許容差と等しい位置の比較")

        changed = copy.deepcopy(example)
        changed["positions"].pop(1)
        result = compare_documents(example, changed, 1e-3)
        require(result["failed"] and ("Arm", 0) in result["only_a"], "関節の欠落の比較")

        changed = copy.deepcopy(example)
        changed["frames"].append(33)
        result = compare_documents(example, changed, 1e-3)
        require(result["failed"] and result["frames_only_b"] == [33] and
                not result["only_a"] and not result["only_b"], "フレームの食い違いの比較")

        changed = copy.deepcopy(example)
        changed["format"] = "invalid"
        try:
            compare_documents(example, changed, 1e-3)
        except ValueError:
            pass
        else:
            raise AssertionError("不正な format の検出")

        changed = copy.deepcopy(example)
        changed["positions"].append(copy.deepcopy(changed["positions"][0]))
        try:
            compare_documents(example, changed, 1e-3)
        except ValueError as error:
            require("重複" in str(error), "関節とフレームの重複の検出")
        else:
            raise AssertionError("関節とフレームの重複の検出")
    except Exception as error:
        print(f"自己検証に失敗しました: {error}", file=sys.stderr)
        print("COMPARE_BVH_FK_SELF_TEST result=fail")
        return 1
    print("COMPARE_BVH_FK_SELF_TEST result=pass")
    return 0


def main():
    parser = JapaneseArgumentParser(description="BVH の FK を関節名とフレームで比較します")
    parser.add_argument("a", nargs="?", help="比較元の JSON")
    parser.add_argument("b", nargs="?", help="比較先の JSON")
    parser.add_argument("--tolerance", type=parse_tolerance, default=1e-3, help="元の BVH の単位で指定する許容差")
    parser.add_argument("--self-test", action="store_true", help="メモリ上の例で照合を確かめます")
    args = parser.parse_args()
    if args.self_test:
        if args.a is not None or args.b is not None:
            parser.error("自己検証に JSON ファイルは指定できません")
        return self_test()
    if args.a is None or args.b is None:
        parser.error("比較する JSON を 2 件指定してください")
    try:
        first, second = read_document(args.a), read_document(args.b)
        result = compare_documents(first, second, args.tolerance)
    except ValueError as error:
        print(f"比較できません: {error}", file=sys.stderr)
        return 2
    display_comparison(first, second, result)
    return 1 if result["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
