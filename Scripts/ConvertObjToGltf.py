# OBJ のメッシュを、エンジンの glTF 読み込み（GLTFAnalyzer）が扱える glTF 2.0（.gltf + .bin）へ変換する。
#
# 標準ライブラリだけで動く。頂点は (位置, UV, 法線) の組ごとに1つにまとめ、多角形は扇形に三角形へ分ける。
# OBJ の UV は左下が原点なので、glTF の左上原点へ v' = 1 - v で直す。
# 材質は1つで、基本色・法線の画像をそのまま参照し、AO・粗さ・金属の3枚の白黒画像から
# glTF の metallicRoughnessTexture（R=AO、G=粗さ、B=金属）を1枚にまとめて書き出す。
#
# 使い方（Cottage_Clean の例）:
#   python Scripts/ConvertObjToGltf.py Assets/Models/Cottage_Clean/Cottage_Clean.obj ^
#       --base-color Cottage_Clean_Base_Color.png --normal Cottage_Clean_Normal.png ^
#       --ao Cottage_Clean_AO.png --roughness Cottage_Clean_Roughness.png --metallic Cottage_Clean_Metallic.png ^
#       --arm-output Cottage_Clean_ARM.png --arm-size 1024
# 画像のパスは OBJ と同じフォルダからの相対パス。出力は OBJ と同じ名前の .gltf と .bin。
import argparse
import json
import math
import struct
import sys
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

# glTF の定数（機械が照合する値）
GL_FLOAT = 5126
GL_UNSIGNED_INT = 5125
GL_ARRAY_BUFFER = 34962
GL_ELEMENT_ARRAY_BUFFER = 34963
GL_LINEAR = 9729
GL_LINEAR_MIPMAP_LINEAR = 9987
GL_REPEAT = 10497


def parse_obj(obj_path):
    """OBJ を読み、重複を除いた頂点配列（位置・法線・UV）と三角形の添字を返す。"""
    positions = []
    texcoords = []
    normals = []
    vertex_keys = {}
    out_positions = []
    out_normals = []
    out_texcoords = []
    indices = []

    def resolve_index(token, count):
        value = int(token)
        # 負の添字は末尾からの相対
        return value - 1 if value > 0 else count + value

    with open(obj_path, "r", encoding="utf-8", errors="replace") as source:
        for line in source:
            parts = line.split()
            if not parts:
                continue
            tag = parts[0]
            if tag == "v":
                positions.append(tuple(float(x) for x in parts[1:4]))
            elif tag == "vt":
                texcoords.append((float(parts[1]), float(parts[2]) if len(parts) > 2 else 0.0))
            elif tag == "vn":
                normals.append(tuple(float(x) for x in parts[1:4]))
            elif tag == "f":
                corner_indices = []
                for corner in parts[1:]:
                    fields = corner.split("/")
                    if len(fields) < 3 or not fields[1] or not fields[2]:
                        raise ValueError(f"UV と法線の無い面は扱わない: {line.strip()}")
                    key = (resolve_index(fields[0], len(positions)),
                           resolve_index(fields[1], len(texcoords)),
                           resolve_index(fields[2], len(normals)))
                    index = vertex_keys.get(key)
                    if index is None:
                        index = len(out_positions)
                        vertex_keys[key] = index
                        out_positions.append(positions[key[0]])
                        u, v = texcoords[key[1]]
                        out_texcoords.append((u, 1.0 - v))
                        nx, ny, nz = normals[key[2]]
                        length = math.sqrt(nx * nx + ny * ny + nz * nz)
                        if length > 0.0:
                            nx, ny, nz = nx / length, ny / length, nz / length
                        out_normals.append((nx, ny, nz))
                    corner_indices.append(index)
                for i in range(1, len(corner_indices) - 1):
                    indices.extend((corner_indices[0], corner_indices[i], corner_indices[i + 1]))
    if not indices:
        raise ValueError(f"面が無い: {obj_path}")
    return out_positions, out_normals, out_texcoords, indices


def read_png_gray(png_path):
    """8bit の PNG を読み、(幅, 高さ, 1画素1byteの輝度) を返す。色付きの画像は R を使う。"""
    data = Path(png_path).read_bytes()
    if data[:8] != PNG_SIGNATURE:
        raise ValueError(f"PNG ではない: {png_path}")
    offset = 8
    idat = bytearray()
    width = height = bit_depth = color_type = interlace = None
    while offset < len(data):
        length, chunk_type = struct.unpack(">I4s", data[offset:offset + 8])
        chunk = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif chunk_type == b"IDAT":
            idat += chunk
        elif chunk_type == b"IEND":
            break
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color_type)
    if bit_depth != 8 or channels is None or interlace != 0:
        raise ValueError(f"8bit・非インターレースの白黒/RGB/RGBA だけを扱う: {png_path}")
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    previous = bytearray(stride)
    gray = bytearray(width * height)
    position = 0
    for y in range(height):
        filter_type = raw[position]
        row = bytearray(raw[position + 1:position + 1 + stride])
        position += 1 + stride
        if filter_type == 1:
            for i in range(channels, stride):
                row[i] = (row[i] + row[i - channels]) & 0xFF
        elif filter_type == 2:
            row = bytearray((a + b) & 0xFF for a, b in zip(row, previous))
        elif filter_type == 3:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                row[i] = (row[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                a = row[i - channels] if i >= channels else 0
                b = previous[i]
                c = previous[i - channels] if i >= channels else 0
                p = a + b - c
                pa = abs(p - a)
                pb = abs(p - b)
                pc = abs(p - c)
                predictor = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                row[i] = (row[i] + predictor) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"未知の PNG フィルタ {filter_type}: {png_path}")
        gray[y * width:(y + 1) * width] = row[0::channels]
        previous = row
    return width, height, gray


def downsample_gray(width, height, pixels, size):
    """整数倍の箱フィルタで size×size へ縮める。"""
    if width % size != 0 or height % size != 0:
        raise ValueError(f"{width}x{height} は {size} の整数倍でない")
    factor_x = width // size
    factor_y = height // size
    area = factor_x * factor_y
    result = bytearray(size * size)
    for out_y in range(size):
        sums = [0] * size
        for dy in range(factor_y):
            start = (out_y * factor_y + dy) * width
            row = pixels[start:start + width]
            for out_x in range(size):
                sums[out_x] += sum(row[out_x * factor_x:(out_x + 1) * factor_x])
        base = out_y * size
        for out_x in range(size):
            result[base + out_x] = (sums[out_x] + area // 2) // area
    return result


def write_png_rgb(png_path, size, red, green, blue):
    """3枚の白黒画像を R・G・B に入れた 8bit RGB の PNG を書く。"""
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        row = bytearray(size * 3)
        start = y * size
        row[0::3] = red[start:start + size]
        row[1::3] = green[start:start + size]
        row[2::3] = blue[start:start + size]
        rows += row

    def chunk(chunk_type, payload):
        body = chunk_type + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    Path(png_path).write_bytes(PNG_SIGNATURE + chunk(b"IHDR", header) +
                               chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def build_arm_texture(directory, args):
    """AO・粗さ・金属の画像から ARM（R=AO、G=粗さ、B=金属）の PNG を作る。"""
    channels = []
    for name in (args.ao, args.roughness, args.metallic):
        width, height, pixels = read_png_gray(directory / name)
        channels.append(downsample_gray(width, height, pixels, args.arm_size))
        print(f"read {name} {width}x{height} -> {args.arm_size}x{args.arm_size}")
    write_png_rgb(directory / args.arm_output, args.arm_size, *channels)


def write_gltf(obj_path, positions, normals, texcoords, indices, args):
    gltf_path = obj_path.with_suffix(".gltf")
    bin_path = obj_path.with_suffix(".bin")
    vertex_count = len(positions)

    position_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
    normal_bytes = b"".join(struct.pack("<3f", *n) for n in normals)
    texcoord_bytes = b"".join(struct.pack("<2f", *t) for t in texcoords)
    index_bytes = struct.pack(f"<{len(indices)}I", *indices)

    buffer = bytearray()
    buffer_views = []
    for payload, target in ((position_bytes, GL_ARRAY_BUFFER), (normal_bytes, GL_ARRAY_BUFFER),
                            (texcoord_bytes, GL_ARRAY_BUFFER), (index_bytes, GL_ELEMENT_ARRAY_BUFFER)):
        while len(buffer) % 4 != 0:
            buffer.append(0)
        buffer_views.append({"buffer": 0, "byteOffset": len(buffer), "byteLength": len(payload), "target": target})
        buffer += payload
    bin_path.write_bytes(bytes(buffer))

    minimum = [min(p[axis] for p in positions) for axis in range(3)]
    maximum = [max(p[axis] for p in positions) for axis in range(3)]
    images = [{"uri": args.base_color}, {"uri": args.normal}]
    pbr = {"baseColorTexture": {"index": 0}, "metallicFactor": 1.0, "roughnessFactor": 1.0}
    if args.ao:
        images.append({"uri": args.arm_output})
        pbr["metallicRoughnessTexture"] = {"index": 2}
    material = {"name": obj_path.stem, "normalTexture": {"index": 1}, "pbrMetallicRoughness": pbr}
    if args.ao:
        material["occlusionTexture"] = {"index": 2}
    document = {
        "asset": {"version": "2.0", "generator": "NorvesLib Scripts/ConvertObjToGltf.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": obj_path.stem}],
        "meshes": [{"name": obj_path.stem, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [material],
        "samplers": [{"magFilter": GL_LINEAR, "minFilter": GL_LINEAR_MIPMAP_LINEAR, "wrapS": GL_REPEAT,
                      "wrapT": GL_REPEAT}],
        "textures": [{"sampler": 0, "source": index} for index in range(len(images))],
        "images": images,
        "accessors": [
            {"bufferView": 0, "componentType": GL_FLOAT, "count": vertex_count, "type": "VEC3",
             "min": minimum, "max": maximum},
            {"bufferView": 1, "componentType": GL_FLOAT, "count": vertex_count, "type": "VEC3"},
            {"bufferView": 2, "componentType": GL_FLOAT, "count": vertex_count, "type": "VEC2"},
            {"bufferView": 3, "componentType": GL_UNSIGNED_INT, "count": len(indices), "type": "SCALAR"},
        ],
        "bufferViews": buffer_views,
        "buffers": [{"uri": bin_path.name, "byteLength": len(buffer)}],
    }
    gltf_path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {gltf_path} vertices={vertex_count} triangles={len(indices) // 3} "
          f"min={minimum} max={maximum}")


def main():
    parser = argparse.ArgumentParser(description="OBJ を glTF 2.0（.gltf + .bin）へ変換する")
    parser.add_argument("obj", type=Path)
    parser.add_argument("--base-color", required=True)
    parser.add_argument("--normal", required=True)
    parser.add_argument("--ao")
    parser.add_argument("--roughness")
    parser.add_argument("--metallic")
    parser.add_argument("--arm-output")
    parser.add_argument("--arm-size", type=int, default=1024)
    args = parser.parse_args()

    arm_inputs = (args.ao, args.roughness, args.metallic, args.arm_output)
    if any(arm_inputs) and not all(arm_inputs):
        parser.error("--ao・--roughness・--metallic・--arm-output はそろえて指定する")

    positions, normals, texcoords, indices = parse_obj(args.obj)
    if args.ao:
        build_arm_texture(args.obj.parent, args)
    write_gltf(args.obj, positions, normals, texcoords, indices, args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
