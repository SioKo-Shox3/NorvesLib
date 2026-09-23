# TinyEXRから独立したEXRヘッダとZIP scanlineの読戻し検証。
import hashlib
import math
from pathlib import Path
import struct
import subprocess
import sys
import zlib


def read_cstring(data, offset):
    end = data.index(0, offset)
    return data[offset:end].decode("ascii"), end + 1


def read_channels(data):
    channels = []
    offset = 0
    while data[offset] != 0:
        name, offset = read_cstring(data, offset)
        pixel_type, = struct.unpack_from("<i", data, offset)
        p_linear = data[offset + 4]
        x_sampling, y_sampling = struct.unpack_from("<ii", data, offset + 8)
        channels.append((name, pixel_type, p_linear, x_sampling, y_sampling))
        offset += 16
    assert offset + 1 == len(data), "channel listの終端が不正です"
    return channels


def decode_zip(payload, expected_size):
    if len(payload) == expected_size:
        return payload, False
    shuffled = bytearray(zlib.decompress(payload))
    assert len(shuffled) == expected_size, "ZIP展開後のサイズが不正です"
    for index in range(1, len(shuffled)):
        shuffled[index] = (shuffled[index - 1] + shuffled[index] - 128) & 255
    even_count = (len(shuffled) + 1) // 2
    decoded = bytearray(len(shuffled))
    decoded[::2] = shuffled[:even_count]
    decoded[1::2] = shuffled[even_count:]
    return decoded, True


def read_exr(path, expected_width, expected_height):
    data = path.read_bytes()
    assert len(data) >= 12, "EXRが短すぎます"
    magic, version = struct.unpack_from("<II", data)
    assert magic == 20000630 and version == 2, "単一partのEXRではありません"
    attributes = {}
    offset = 8
    while data[offset] != 0:
        name, offset = read_cstring(data, offset)
        kind, offset = read_cstring(data, offset)
        length, = struct.unpack_from("<I", data, offset)
        offset += 4
        assert name not in attributes, "EXR属性が重複しています"
        attributes[name] = (kind, data[offset:offset + length])
        offset += length
    offset += 1

    def attribute(name, kind):
        actual_kind, value = attributes[name]
        assert actual_kind == kind, f"{name}の型が不正です"
        return value

    channels = read_channels(attribute("channels", "chlist"))
    assert channels == [(name, 2, 0, 1, 1) for name in ("B", "G", "R")], (
        "RGB 32-bit float channelが一致しません")
    assert attribute("compression", "compression") == b"\x03", "ZIPではありません"
    assert attribute("lineOrder", "lineOrder") == b"\x00", "走査順が不正です"
    window = (0, 0, expected_width - 1, expected_height - 1)
    assert struct.unpack("<4i", attribute("dataWindow", "box2i")) == window, (
        "data windowが不正です")
    assert struct.unpack("<4i", attribute("displayWindow", "box2i")) == window, (
        "display windowが不正です")
    if "chromaticities" in attributes:
        chromaticities = struct.unpack(
            "<8f", attribute("chromaticities", "chromaticities"))
        reference = (0.64, 0.33, 0.30, 0.60, 0.15, 0.06, 0.3127, 0.3290)
        assert all(abs(a - b) < 1e-6 for a, b in zip(chromaticities, reference)), (
            "Rec.709/D65の色度が不正です")

    block_count = (expected_height + 15) // 16
    offsets = struct.unpack_from(f"<{block_count}Q", data, offset)
    planes = {name: [] for name in ("B", "G", "R")}
    compressed_blocks = 0
    for block_index, block_offset in enumerate(offsets):
        y, packed_size = struct.unpack_from("<ii", data, block_offset)
        assert y == block_index * 16 and packed_size > 0, "scanline blockが不正です"
        rows = min(16, expected_height - y)
        expected_size = rows * expected_width * 3 * 4
        payload = data[block_offset + 8:block_offset + 8 + packed_size]
        assert len(payload) == packed_size, "scanline blockが途切れています"
        raw, compressed = decode_zip(payload, expected_size)
        compressed_blocks += compressed
        cursor = 0
        for _ in range(rows):
            for name in ("B", "G", "R"):
                values = struct.unpack_from(f"<{expected_width}f", raw, cursor)
                planes[name].extend(values)
                cursor += expected_width * 4
    assert compressed_blocks > 0, "ZIP圧縮されたblockがありません"
    pixels = list(zip(planes["R"], planes["G"], planes["B"]))
    assert len(pixels) == expected_width * expected_height, "画素数が不正です"
    assert all(math.isfinite(value) for pixel in pixels for value in pixel), (
        "非有限のEXR画素があります")
    return pixels, compressed_blocks


def check_known(pixels):
    for y in range(17):
        for x in range(3):
            actual = pixels[y * 3 + x]
            expected = (0.25 * (x + 1) + 0.01 * y,
                        1.25 + 0.125 * y,
                        0.5 * (x + y))
            assert all(abs(a - b) < 1e-6 for a, b in zip(actual, expected)), (
                f"既知RGB値が不正です: ({x}, {y})")


def check_gpu(pixels):
    expected_blocks = (
        0.05, 0.42604, 0.412437, 0.05,
        0.0519434, 1.42298, 1.40161, 0.0509717,
        0.438672, 2.01668, 2.01279, 0.429927,
        1.17229, 1.7553, 1.7553, 1.14314,
    )
    for block_y in range(4):
        for block_x in range(4):
            total = sum(
                pixels[y * 32 + x][0]
                for y in range(block_y * 8, (block_y + 1) * 8)
                for x in range(block_x * 8, (block_x + 1) * 8)
            )
            actual = total / 64
            expected = expected_blocks[block_y * 4 + block_x]
            assert abs(actual - expected) <= 0.02, "PT画像のEXR読戻しが基準値と異なります"


def main():
    executable = Path(sys.argv[1])
    output_root = Path(sys.argv[2])
    result = subprocess.run([str(executable)], capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    if result.returncode != 0:
        return result.returncode

    name = "triangle_seed6a09e667_spp000032_frame000007.exr"
    first = output_root / "first" / name
    second = output_root / "second" / name
    first_bytes = first.read_bytes()
    assert first_bytes == second.read_bytes(), "固定条件の2出力がbyte一致しません"
    first_pixels, first_blocks = read_exr(first, 32, 32)
    second_pixels, second_blocks = read_exr(second, 32, 32)
    assert first_pixels == second_pixels, "固定条件のRGB画素が一致しません"
    check_gpu(first_pixels)

    known = output_root / "known" / "known_seed0000002a_spp000003_frame000009.exr"
    known_pixels, known_blocks = read_exr(known, 3, 17)
    check_known(known_pixels)
    assert not list(output_root.rglob("*.tmp")), "未完成の一時ファイルが残りました"
    print(f"exr_byte_equal=true sha256={hashlib.sha256(first_bytes).hexdigest()}")
    print(f"exr_independent_reader=true channels=BGR precision=float32 "
          f"compression=ZIP windows=32x32,3x17 "
          f"compressed_blocks={first_blocks},{second_blocks},{known_blocks}")
    print("exr_known_linear_rgb=true exr_gpu_golden=true exr_finite_scan=true")
    return 0


if __name__ == "__main__":
    sys.exit(main())
