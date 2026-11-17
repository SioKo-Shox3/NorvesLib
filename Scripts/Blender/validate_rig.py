"""Blender が出力した glTF の骨格・重み・UV・アニメーションを共通仕様で検査する。

使い方: python -B Scripts/Blender/validate_rig.py input.gltf [--spec PATH] [--report PATH]
入力には GLB も指定できる。終了コードは 0 がエラーなし、1 が入力または仕様違反、
2 が引数または仕様ファイルの誤りを表す。
"""

import argparse
import base64
import binascii
from collections import Counter
import json
import math
from pathlib import Path
import re
import struct
import sys
from urllib.parse import unquote


sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import format_line, log_line, read_json, write_json, sha256_file
from skeleton_spec import load_spec, SpecError, DEFAULT_SPEC_PATH


COMPONENTS = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
DIMENSIONS = {
    "SCALAR": (1, 1), "VEC2": (1, 2), "VEC3": (1, 3), "VEC4": (1, 4),
    "MAT2": (2, 2), "MAT3": (3, 3), "MAT4": (4, 4),
}


class ReadError(ValueError):
    """入力の構造やバッファを読み取れない理由を保持する。"""


class ArgumentParser(argparse.ArgumentParser):
    """引数の誤りを日本語で通知する。"""

    def error(self, message):
        self.print_usage(sys.stderr)
        self.exit(2, "引数が正しくありません。入力ファイルと --spec PATH・--report PATH の指定を確認してください。\n")


def integer(value, label):
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise ReadError(f"{label} は 0 以上の整数でなければならない。")
    return value


def entry(items, index, label):
    index = integer(index, label)
    if index >= len(items):
        raise ReadError(f"{label} の参照先 {index} が要素数 {len(items)} の範囲外にある。")
    return items[index]


def objects(value, label):
    if not isinstance(value, list) or any(not isinstance(item, dict) for item in value):
        raise ReadError(f"{label} はオブジェクトの配列でなければならない。")
    return value


def mapping(value, label):
    if not isinstance(value, dict):
        raise ReadError(f"{label} はオブジェクトでなければならない。")
    return value


def sequence(value, label):
    if not isinstance(value, list):
        raise ReadError(f"{label} は配列でなければならない。")
    return value


def number_field(value):
    # 共通の行形式と JSON に、有限でない数をそのまま渡さない。
    return value if math.isfinite(value) else str(value)


class Gltf:
    """glTF と GLB のバッファと accessor を復号する。"""

    def __init__(self, path):
        self.path = path
        self.bin_chunk = None
        if path.suffix.lower() == ".glb":
            self.doc = self.read_glb(path.read_bytes())
        elif path.suffix.lower() == ".gltf":
            self.doc = read_json(path)
        else:
            raise ReadError("入力の拡張子は .gltf または .glb でなければならない。")
        mapping(self.doc, "glTF")
        for name in ("buffers", "bufferViews", "accessors", "nodes", "skins", "meshes", "materials", "animations"):
            setattr(self, name, objects(self.doc.get(name, []), name))
        self.buffer_data = [self.read_buffer(index, item) for index, item in enumerate(self.buffers)]
        self.views = [self.read_view(item) for item in self.bufferViews]
        self.values = [self.read_accessor(item) for item in self.accessors]
        self.parents = [set() for _ in self.nodes]
        self.selected_meshes = set()
        self.prepare_references()

    def read_glb(self, data):
        if len(data) < 12:
            raise ReadError("GLB のヘッダーが 12 バイトに満たない。")
        magic, version, length = struct.unpack_from("<4sII", data)
        if magic != b"glTF" or version != 2 or length != len(data):
            raise ReadError("GLB の識別子・バージョン 2・ファイル長のいずれかが正しくない。")
        offset = 12
        json_chunk = None
        chunk_index = 0
        while offset < length:
            if offset + 8 > length:
                raise ReadError("GLB のチャンクヘッダーが途中で切れている。")
            size, kind = struct.unpack_from("<II", data, offset)
            offset += 8
            if size % 4 or offset + size > length:
                raise ReadError("GLB のチャンク長が 4 バイト境界またはファイルの範囲に収まっていない。")
            chunk = data[offset:offset + size]
            offset += size
            if chunk_index == 0 and kind != 0x4E4F534A:
                raise ReadError("GLB の先頭チャンクが JSON ではない。")
            if kind == 0x4E4F534A:
                if json_chunk is not None:
                    raise ReadError("GLB の JSON チャンクが重複している。")
                json_chunk = chunk
            elif kind == 0x004E4942:
                if self.bin_chunk is not None:
                    raise ReadError("GLB の BIN チャンクが重複している。")
                self.bin_chunk = chunk
            chunk_index += 1
        if json_chunk is None:
            raise ReadError("GLB に JSON チャンクがない。")
        try:
            return json.loads(json_chunk.decode("utf-8-sig"))
        except (UnicodeError, ValueError):
            raise ReadError("GLB の JSON チャンクを UTF-8 の JSON として読み取れない。") from None

    def read_buffer(self, index, item):
        length = integer(item["byteLength"], "buffer.byteLength")
        if "uri" not in item:
            if index != 0 or self.bin_chunk is None:
                raise ReadError(f"buffer {index} に URI または対応する BIN チャンクがない。")
            data = self.bin_chunk
        else:
            uri = item["uri"]
            if not isinstance(uri, str):
                raise ReadError(f"buffer {index} の URI が文字列ではない。")
            if uri.startswith("data:"):
                header, separator, payload = uri.partition(",")
                if not separator or not header.endswith(";base64"):
                    raise ReadError(f"buffer {index} の data URI が base64 形式ではない。")
                try:
                    data = base64.b64decode(unquote(payload, errors="strict"), validate=True)
                except (ValueError, UnicodeError, binascii.Error):
                    raise ReadError(f"buffer {index} の base64 データを復号できない。") from None
            else:
                data = (self.path.parent / unquote(uri, errors="strict")).read_bytes()
        if len(data) < length:
            raise ReadError(f"buffer {index} のデータ {len(data)} バイトが宣言長 {length} に満たない。")
        return memoryview(data)[:length]

    def read_view(self, item):
        data = entry(self.buffer_data, item["buffer"], "bufferView.buffer")
        offset = integer(item.get("byteOffset", 0), "bufferView.byteOffset")
        length = integer(item["byteLength"], "bufferView.byteLength")
        if offset + length > len(data):
            raise ReadError("bufferView が buffer の宣言長の範囲外にある。")
        if "byteStride" in item:
            stride = integer(item["byteStride"], "bufferView.byteStride")
            if stride == 0:
                raise ReadError("bufferView.byteStride が 0 である。")
        return data[offset:offset + length]

    def layout(self, component, kind):
        if component not in COMPONENTS or kind not in DIMENSIONS:
            raise ReadError(f"accessor の componentType {component!r} または type {kind!r} が未知である。")
        code = COMPONENTS[component]
        columns, rows = DIMENSIONS[kind]
        column_size = struct.calcsize("<" + code) * rows
        # glTF の行列は列ごとに 4 バイト境界まで埋める。
        padding = (-column_size) % 4 if columns > 1 else 0
        return struct.Struct("<" + (code * rows + "x" * padding) * columns)

    def read_elements(self, view_index, offset, count, layout, use_stride=True):
        view = entry(self.views, view_index, "accessor.bufferView")
        definition = entry(self.bufferViews, view_index, "accessor.bufferView")
        offset = integer(offset, "accessor.byteOffset")
        stride = definition.get("byteStride", layout.size) if use_stride else layout.size
        if stride < layout.size:
            raise ReadError("bufferView.byteStride が accessor の要素幅より小さい。")
        end = offset + (count - 1) * stride + layout.size if count else offset
        if end > len(view):
            raise ReadError("accessor の要素が bufferView の範囲外にある。")
        if stride == layout.size:
            return list(layout.iter_unpack(view[offset:end]))
        return [layout.unpack_from(view, offset + index * stride) for index in range(count)]

    def read_accessor(self, item):
        component, kind = item["componentType"], item["type"]
        layout = self.layout(component, kind)
        count = integer(item["count"], "accessor.count")
        if "bufferView" in item:
            values = self.read_elements(item["bufferView"], item.get("byteOffset", 0), count, layout)
        else:
            columns, rows = DIMENSIONS[kind]
            values = [(0,) * (columns * rows)] * count
        if "sparse" in item:
            sparse = mapping(item["sparse"], "accessor.sparse")
            sparse_count = integer(sparse["count"], "accessor.sparse.count")
            if sparse_count > count:
                raise ReadError("sparse の要素数が accessor の要素数を超えている。")
            indices = mapping(sparse["indices"], "sparse.indices")
            sparse_values = mapping(sparse["values"], "sparse.values")
            if indices["componentType"] not in (5121, 5123, 5125):
                raise ReadError("sparse の添字の componentType が符号なし整数ではない。")
            index_layout = self.layout(indices["componentType"], "SCALAR")
            replacements = self.read_elements(sparse_values["bufferView"], sparse_values.get("byteOffset", 0), sparse_count, layout, False)
            positions = self.read_elements(indices["bufferView"], indices.get("byteOffset", 0), sparse_count, index_layout, False)
            for (position,), value in zip(positions, replacements):
                if position >= count:
                    raise ReadError(f"sparse の添字 {position} が accessor の要素数 {count} の範囲外にある。")
                values[position] = value
        if item.get("normalized", False) and component != 5126:
            divisor = {5120: 127, 5121: 255, 5122: 32767, 5123: 65535, 5125: 4294967295}[component]
            if component in (5120, 5122):
                values = [tuple(max(value / divisor, -1.0) for value in row) for row in values]
            else:
                values = [tuple(value / divisor for value in row) for row in values]
        return values

    def accessor(self, index):
        return entry(self.values, index, "accessor")

    def prepare_references(self):
        for index, node in enumerate(self.nodes):
            if not isinstance(node.get("name", ""), str):
                raise ReadError("ノードの名前が文字列ではない。")
            for child in sequence(node.get("children", []), "node.children"):
                entry(self.nodes, child, "node.children")
                self.parents[child].add(index)
            if "mesh" in node:
                entry(self.meshes, node["mesh"], "node.mesh")
            if "skin" in node:
                entry(self.skins, node["skin"], "node.skin")
                if "mesh" in node:
                    self.selected_meshes.add(node["mesh"])
        if not any("skin" in node for node in self.nodes):
            self.selected_meshes = set(range(len(self.meshes)))
        for skin in self.skins:
            for joint in sequence(skin["joints"], "skin.joints"):
                node = entry(self.nodes, joint, "skin.joints")
                if not isinstance(node.get("name", ""), str):
                    raise ReadError("関節ノードの名前が文字列ではない。")
        for mesh in self.meshes:
            for primitive in objects(mesh.get("primitives", []), "mesh.primitives"):
                attributes = mapping(primitive.get("attributes", {}), "primitive.attributes")
                for accessor in attributes.values():
                    self.accessor(accessor)
                if "indices" in primitive:
                    self.accessor(primitive["indices"])
                for target in objects(primitive.get("targets", []), "primitive.targets"):
                    for accessor in target.values():
                        self.accessor(accessor)
        for animation in self.animations:
            samplers = objects(animation.get("samplers", []), "animation.samplers")
            for sampler in samplers:
                self.accessor(sampler["input"])
                if self.accessors[sampler["input"]]["type"] != "SCALAR":
                    raise ReadError("アニメーションの入力時刻が SCALAR ではない。")
                self.accessor(sampler["output"])
                if not isinstance(sampler.get("interpolation", "LINEAR"), str):
                    raise ReadError("アニメーションの補間方式が文字列ではない。")
            for channel in objects(animation.get("channels", []), "animation.channels"):
                entry(samplers, channel["sampler"], "channel.sampler")
                target = mapping(channel["target"], "channel.target")
                if not isinstance(target.get("path", ""), str):
                    raise ReadError("アニメーションの対象の path が文字列ではない。")
                if "node" in target:
                    entry(self.nodes, target["node"], "channel.target.node")

    def stats(self):
        vertices = triangles = uv_layers = 0
        for mesh in self.meshes:
            for primitive in mesh.get("primitives", []):
                attributes = primitive.get("attributes", {})
                count = len(self.accessor(attributes["POSITION"])) if "POSITION" in attributes else 0
                vertices += count
                index_count = len(self.accessor(primitive["indices"])) if "indices" in primitive else count
                mode = primitive.get("mode", 4)
                if mode == 4:
                    triangles += index_count // 3
                elif mode in (5, 6):
                    triangles += max(0, index_count - 2)
                uv_layers = max(uv_layers, sum(re.fullmatch(r"TEXCOORD_\d+", name) is not None for name in attributes))
        return {
            "vertices": vertices, "triangles": triangles, "uv_layers": uv_layers,
            "materials": len(self.materials), "skins": len(self.skins),
            "joints": sum(len(skin["joints"]) for skin in self.skins), "animations": len(self.animations),
        }


class Validator:
    """共通骨格仕様に記載された規則だけを適用する。"""

    def __init__(self, gltf, spec):
        self.gltf = gltf
        self.spec = spec
        self.issues = []

    def issue(self, code, message, severity="error", **context):
        self.issues.append({"severity": severity, "code": code, "message": message, **context})

    def check(self):
        if not self.gltf.skins:
            self.issue("no_skin", "骨格を表す skin が 0 個である。")
        self.check_skins()
        for mesh_index in sorted(self.gltf.selected_meshes):
            mesh = self.gltf.meshes[mesh_index]
            for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
                self.check_primitive(primitive, {"mesh": mesh_index, "primitive": primitive_index})
        self.check_animations()
        return self.issues

    def check_skins(self):
        pattern = re.compile(self.spec["naming"]["joint_pattern"])
        limit = self.spec["limits"]["max_joints"]
        for skin_index, skin in enumerate(self.gltf.skins):
            names = [self.gltf.nodes[joint].get("name", "") for joint in skin["joints"]]
            if len(names) > limit:
                self.issue("joint_count", f"関節の数 {len(names)} が仕様の上限 {limit} を超えている。", skin=skin_index, joints=len(names), limit=limit)
            for name, count in Counter(names).items():
                if count > 1:
                    self.issue("duplicate_joint_name", f"関節名 {name!r} が同じ skin 内に {count} 個ある。", skin=skin_index, joint=name)
            for name in names:
                if pattern.fullmatch(name) is None:
                    self.issue("joint_name", f"関節名 {name!r} が仕様の命名規則 {pattern.pattern!r} に一致しない。", skin=skin_index, joint=name)
            present = set(names)
            for role in self.spec["required_roles"]:
                for name in self.spec["roles"][role]:
                    if name not in present:
                        self.issue("missing_role", f"必須の役割 {role!r} に必要な関節 {name!r} が skin にない。", skin=skin_index, role=role, joint=name)

    def check_primitive(self, primitive, context):
        attributes = primitive.get("attributes", {})
        if "WEIGHTS_0" not in attributes:
            skinned = any(node.get("mesh") == context["mesh"] and "skin" in node for node in self.gltf.nodes)
            if skinned:
                self.issue("missing_weights", "skin 付きのプリミティブに WEIGHTS_0 がない。", **context)
        weight_sets = []
        for name, accessor in attributes.items():
            match = re.fullmatch(r"WEIGHTS_(\d+)", name)
            if match and "JOINTS_" + match[1] in attributes:
                weights = self.gltf.accessor(accessor)
                joints = self.gltf.accessor(attributes["JOINTS_" + match[1]])
                if len(weights) != len(joints) or any(len(w) != len(j) for w, j in zip(weights, joints)):
                    raise ReadError("JOINTS と WEIGHTS の要素数または要素幅が一致しない。")
                weight_sets.append(weights)
        if weight_sets:
            self.check_weights(weight_sets, attributes, context)
        self.check_uv(attributes, context)
        targets = primitive.get("targets", [])
        if not self.spec["morph_targets"] and targets:
            self.issue("morph_targets", f"仕様で禁止されているモーフターゲットが {len(targets)} 個ある。", targets=len(targets), **context)

    def check_weights(self, weight_sets, attributes, context):
        count = len(self.gltf.accessor(attributes["POSITION"])) if "POSITION" in attributes else len(weight_sets[0])
        if any(len(values) != count for values in weight_sets):
            raise ReadError("WEIGHTS の頂点数が POSITION またはほかの WEIGHTS と一致しない。")
        limits = self.spec["limits"]
        limit, tolerance = limits["max_influences"], limits["weight_sum_tolerance"]
        excessive = zeros = bad_sums = max_influences = 0
        worst = 1.0
        worst_distance = 0.0
        for vertex in range(count):
            weights = [value for values in weight_sets for value in values[vertex]]
            influences = sum(value > 0 for value in weights)
            max_influences = max(max_influences, influences)
            excessive += influences > limit
            total = math.fsum(weights) if all(math.isfinite(value) for value in weights) else sum(weights)
            if total == 0:
                zeros += 1
                continue
            distance = abs(total - 1) if math.isfinite(total) else math.inf
            if distance > tolerance:
                bad_sums += 1
                if distance > worst_distance:
                    worst, worst_distance = total, distance
        if excessive:
            self.issue("influences", f"影響数が上限 {limit} を超える頂点が {excessive} 個あり、最大は {max_influences} である。", vertices=excessive, max=max_influences, limit=limit, **context)
        if zeros:
            self.issue("zero_weight_vertex", f"重みの和が 0 の頂点が {zeros} 個ある。", vertices=zeros, **context)
        if bad_sums:
            self.issue("weight_sum", f"重みの和が 1 から許容差 {tolerance} を超えて外れる頂点が {bad_sums} 個あり、最も外れた和は {worst} である。", vertices=bad_sums, worst=number_field(worst), tolerance=tolerance, **context)

    def check_uv(self, attributes, context):
        for name in attributes:
            match = re.fullmatch(r"TEXCOORD_(\d+)", name)
            if match and int(match[1]) >= 1 and name not in self.spec["uv"]["sets"]:
                self.issue("uv_extra_set", f"UV 属性 {name} が仕様で許可された集合に含まれていない。", attribute=name, **context)
        if "TEXCOORD_0" not in attributes:
            self.issue("uv_missing", "必須の UV 属性 TEXCOORD_0 がない。", **context)
            return
        values = [value for row in self.gltf.accessor(attributes["TEXCOORD_0"]) for value in row]
        lower, upper = self.spec["uv"]["min"], self.spec["uv"]["max"]
        outside = sum(not math.isfinite(value) or value < lower - 1e-6 or value > upper + 1e-6 for value in values)
        if outside:
            minimum, maximum = min(values), max(values)
            if any(math.isnan(value) for value in values):
                minimum = maximum = math.nan
            self.issue("uv_range", f"UV 成分が許容範囲 [{lower}, {upper}] から {outside} 個外れており、実際の最小値は {minimum}、最大値は {maximum} である。", values=outside, min=number_field(minimum), max=number_field(maximum), **context)

    def check_animations(self):
        ancestors = set()
        for skin in self.gltf.skins:
            joints = set(skin["joints"])
            visited = set(joints)
            pending = [parent for joint in joints for parent in self.gltf.parents[joint]]
            while pending:
                node = pending.pop()
                if node in visited:
                    continue
                visited.add(node)
                ancestors.add(node)
                pending.extend(self.gltf.parents[node])
        for animation_index, animation in enumerate(self.gltf.animations):
            for sampler_index, sampler in enumerate(animation.get("samplers", [])):
                context = {"animation": animation_index, "sampler": sampler_index}
                interpolation = sampler.get("interpolation", "LINEAR")
                if interpolation not in self.spec["interpolations"]:
                    self.issue("interpolation", f"補間方式 {interpolation!r} が仕様の許可一覧にない。", severity="warning", interpolation=interpolation, **context)
                times = self.gltf.accessor(sampler["input"])
                for index, (time,) in enumerate(times):
                    if not math.isfinite(time) or (index > 0 and time <= times[index - 1][0]):
                        self.issue("time_not_increasing", f"時刻列の位置 {index} の値 {time} が有限でないか、直前の時刻より大きくない。", index=index, **context)
                        break
            for channel_index, channel in enumerate(animation.get("channels", [])):
                target = channel["target"]
                node = target.get("node")
                if node in ancestors:
                    name = self.gltf.nodes[node].get("name", "")
                    path = target.get("path", "")
                    self.issue("animated_non_joint_ancestor", f"関節でない祖先ノード {node} ({name!r}) の {path!r} がアニメーションで変更される。", animation=animation_index, channel=channel_index, node=node, node_name=name, path=path)


def main():
    parser = ArgumentParser(description="glTF の骨格を共通仕様で検査する。", usage="%(prog)s INPUT [--spec PATH] [--report PATH]", add_help=False)
    parser._positionals.title = "位置引数"
    parser._optionals.title = "オプション"
    parser.add_argument("input", metavar="INPUT", help="検査する .gltf または .glb")
    parser.add_argument("--spec", metavar="PATH", default=DEFAULT_SPEC_PATH, help="共通骨格仕様のパス")
    parser.add_argument("--report", metavar="PATH", help="検査結果の JSON 出力先")
    parser.add_argument("-h", "--help", action="help", help="使い方を表示して終了する")
    args = parser.parse_args()
    input_path, spec_path = Path(args.input).resolve(), Path(args.spec).resolve()
    if args.report and Path(args.report).resolve() in (input_path, spec_path):
        parser.error("出力先が入力と同じ")
    spec = None
    stats = {}
    issues = []
    exit_code = 0
    input_hash = None
    try:
        input_hash = sha256_file(input_path)
    except OSError:
        pass
    try:
        spec = load_spec(spec_path)
    except SpecError as error:
        issues = [{"severity": "error", "code": "spec_error", "message": message} for message in error.errors]
        exit_code = 2
    if spec is not None:
        try:
            gltf = Gltf(input_path)
            stats = gltf.stats()
            issues = Validator(gltf, spec).check()
        except (OSError, ValueError, KeyError, TypeError, IndexError, OverflowError, struct.error, RecursionError) as error:
            if isinstance(error, ReadError):
                message = str(error)
            elif isinstance(error, FileNotFoundError):
                message = f"入力または参照バッファのファイル {error.filename!r} が見つからない。"
            elif isinstance(error, OSError):
                message = "入力ファイルまたは参照バッファを読み込めないため、パスとアクセス権を確認する必要がある。"
            elif isinstance(error, KeyError):
                message = f"入力データに読み取りに必要な鍵 {error.args[0]!r} がない。"
            else:
                message = "入力の JSON・参照・数値・バッファの形式が正しくないため読み取れない。"
            stats = {}
            issues = [{"severity": "error", "code": "read_error", "message": message}]
    status = "fail" if any(issue["severity"] == "error" for issue in issues) else "ok"
    if args.report:
        report = {
            "report_version": 1, "input": str(input_path), "input_sha256": input_hash,
            "spec": str(spec_path), "spec_name": spec["name"] if spec else None,
            "stats": stats, "issues": issues, "status": status,
        }
        try:
            write_json(args.report, report)
        except (OSError, ValueError):
            issues.append({"severity": "error", "code": "read_error", "message": "検査レポートを書き込めないため、出力先とアクセス権を確認する必要がある。"})
            status = "fail"
    if stats:
        log_line("MESH_STATS", {"file": input_path.name, **stats})
    for issue in issues:
        fields = {key: value for key, value in issue.items() if key != "message"}
        print(format_line("RIG_ISSUE", {**fields, "message": issue["message"]}))
    errors = sum(issue["severity"] == "error" for issue in issues)
    warnings = sum(issue["severity"] == "warning" for issue in issues)
    log_line("RIG_RESULT", {"status": status, "errors": errors, "warnings": warnings})
    return exit_code or int(errors > 0)


if __name__ == "__main__":
    raise SystemExit(main())
