"""地形 cook の試験と煙の確かめで使う 16bit 高さ画像を生成する。

値は u(x, z) = 1985*x + 62*z - ((7*x*z) % 61) とし、画像の行 r は z = r に対応する。
Test/Core/Asset/CookedTerrainTest.cpp と同じ式を使って比較するためのフィクスチャ。
"""

import argparse
from pathlib import Path
import struct
import zlib


def png_chunk(chunk_type, data):
    crc = zlib.crc32(chunk_type + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + chunk_type + data + struct.pack(">I", crc)


def build_png():
    rows = bytearray()
    for z in range(33):
        rows.append(0)
        for x in range(33):
            value = 1985 * x + 62 * z - ((7 * x * z) % 61)
            rows.extend(struct.pack(">H", value))

    header = struct.pack(">IIBBBBB", 33, 33, 16, 0, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(rows), level=9))
        + png_chunk(b"IEND", b"")
    )


def main():
    parser = argparse.ArgumentParser(description="地形の高さ画像を生成する")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path(__file__).with_name("height_formula_33x33_16bit.png"),
        help="PNG の出力先",
    )
    args = parser.parse_args()
    args.out.write_bytes(build_png())


if __name__ == "__main__":
    main()
