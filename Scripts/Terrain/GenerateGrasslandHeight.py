#!/usr/bin/env python3
"""草原の地形 cook を検証するため、決定的な 16bit 高さ画像を作る。

正規化座標上に、緩やかな器、曲がる谷、片側の尾根、曲線状の崖を重ねる。
崖の端と上の段は楕円の距離で滑らかに減衰し、丸く地面へつながる。
細かな起伏は整数ハッシュの値ノイズで作る。
高さは固定範囲 0〜64 m から 0〜65535 へ量子化する。
画像の列が x、行が z で、左上を (0, 0) とする。
乱数状態や Python の hash() を使わず、seed と格子座標から値を決める。
"""

import argparse
import hashlib
import math
from pathlib import Path
import struct
import zlib


HEIGHT_MIN_M = 0.0
HEIGHT_MAX_M = 64.0
TERRAIN_EXTENT_M = 512.0
CLIFF_TANGENT_X = 1.0 / math.sqrt(1.0 + 0.15 ** 2)
CLIFF_TANGENT_Z = 0.15 * CLIFF_TANGENT_X
DEFAULT_SEED = 89021
DEFAULT_SIZE = 513
DEFAULT_OUTPUT = Path("Assets/TerrainSpecs/Grassland/GrasslandHeight.png")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
UINT32_MASK = 0xFFFFFFFF


def smoothstep(value):
    value = max(0.0, min(1.0, value))
    return value * value * (3.0 - 2.0 * value)


def smoothstep_edges(edge0, edge1, value):
    return smoothstep((value - edge0) / (edge1 - edge0))


def bell(distance):
    return 1.0 - smoothstep(abs(distance))


def lattice_value(seed, x, z):
    value = (seed ^ (x * 0x8DA6B343) ^ (z * 0xD8163841)) & UINT32_MASK
    value ^= value >> 16
    value = (value * 0x7FEB352D) & UINT32_MASK
    value ^= value >> 15
    value = (value * 0x846CA68B) & UINT32_MASK
    value ^= value >> 16
    return value / 2147483647.5 - 1.0


def make_octaves(seed, size):
    octaves = []
    for index, (frequency, amplitude) in enumerate(((4, 0.6), (8, 0.3), (16, 0.15))):
        octave_seed = (seed + index * 0x9E3779B9) & UINT32_MASK
        grid = [
            [lattice_value(octave_seed, x, z) for x in range(frequency + 2)]
            for z in range(frequency + 2)
        ]
        axis = []
        for position in range(size):
            cell, remainder = divmod(position * frequency, size - 1)
            axis.append((cell, smoothstep(remainder / (size - 1))))
        octaves.append((amplitude, grid, axis))
    return octaves


def terrain_height(x, z, column, row, octaves):
    curve_t = 2.0 * x - 1.0
    valley_z = 0.52 + 0.085 * curve_t * (1.5 - 0.5 * curve_t * curve_t)
    bowl = 30.0 + 13.0 * ((x - 0.5) ** 2 + (z - 0.5) ** 2)
    valley = -9.5 * bell((z - valley_z) / 0.235)
    ridge = 6.0 * bell((z - valley_z + 0.20) / 0.09) * bell((x - 0.40) / 0.34)

    # メートル単位の局所座標で、曲がる崖と上の段の減衰を作る。
    cliff_x = x * TERRAIN_EXTENT_M - 0.70 * TERRAIN_EXTENT_M
    cliff_z = z * TERRAIN_EXTENT_M - 0.735 * TERRAIN_EXTENT_M
    along = cliff_x * CLIFF_TANGENT_X + cliff_z * CLIFF_TANGENT_Z
    across = -cliff_x * CLIFF_TANGENT_Z + cliff_z * CLIFF_TANGENT_X
    distance = across - along * along / 240.0
    radius = math.sqrt(along * along + (0.7 * max(distance, 0.0)) ** 2)
    mound = 1.0 - smoothstep_edges(22.0, 52.0, radius)
    face = smoothstep_edges(-1.0, 1.0, distance)
    cliff = 8.0 * face * mound

    detail = 0.0
    for amplitude, grid, axis in octaves:
        ix, tx = axis[column]
        iz, tz = axis[row]
        upper = grid[iz][ix] + (grid[iz][ix + 1] - grid[iz][ix]) * tx
        lower = grid[iz + 1][ix] + (grid[iz + 1][ix + 1] - grid[iz + 1][ix]) * tx
        detail += amplitude * (upper + (lower - upper) * tz)
    return bowl + valley + ridge + cliff + detail


def png_chunk(kind, payload):
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & UINT32_MASK)


def generate(size, seed):
    octaves = make_octaves(seed, size)
    raw = bytearray()
    previous_row = None
    min_sample = 65535
    max_sample = 0
    max_delta = 0
    clamped = 0
    mapping_range = HEIGHT_MAX_M - HEIGHT_MIN_M

    for row in range(size):
        raw.append(0)
        current_row = []
        z = row / (size - 1)
        for column in range(size):
            x = column / (size - 1)
            height = terrain_height(x, z, column, row, octaves)
            sample = round((height - HEIGHT_MIN_M) / mapping_range * 65535)
            if sample < 0 or sample > 65535:
                clamped += 1
                sample = max(0, min(65535, sample))
            min_sample = min(min_sample, sample)
            max_sample = max(max_sample, sample)
            if column:
                max_delta = max(max_delta, abs(sample - current_row[-1]))
            if previous_row is not None:
                max_delta = max(max_delta, abs(sample - previous_row[column]))
            current_row.append(sample)
            raw.extend(struct.pack(">H", sample))
        previous_row = current_row

    header = struct.pack(">IIBBBBB", size, size, 16, 0, 0, 0, 0)
    png = (
        PNG_SIGNATURE
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(raw, level=9))
        + png_chunk(b"IEND", b"")
    )
    min_m = HEIGHT_MIN_M + min_sample * mapping_range / 65535.0
    max_m = HEIGHT_MIN_M + max_sample * mapping_range / 65535.0
    spacing_m = TERRAIN_EXTENT_M / (size - 1)
    slope = math.degrees(math.atan(max_delta * mapping_range / 65535.0 / spacing_m))
    return png, min_m, max_m, clamped, slope


def main():
    parser = argparse.ArgumentParser(description="草原の高さ画像を生成する")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED, help="起伏を決める整数の種")
    parser.add_argument("--size", type=int, default=DEFAULT_SIZE, help="画像の一辺の標本数（2 以上）")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUTPUT, help="出力する PNG のパス")
    parser.add_argument("--print-mapping", action="store_true", help="高さの写像定数を表示する")
    args = parser.parse_args()
    if args.size < 2:
        parser.error("--size は 2 以上で指定してください")
    if args.print_mapping:
        print(f"HEIGHT_MIN_M={HEIGHT_MIN_M} HEIGHT_MAX_M={HEIGHT_MAX_M}")

    png, min_m, max_m, clamped, slope = generate(args.size, args.seed)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(png)
    print(
        f"GRASSLAND_HEIGHT size={args.size} seed={args.seed} "
        f"min_m={min_m:.3f} max_m={max_m:.3f} clamped={clamped} "
        f"max_slope_deg={slope:.3f} sha256={hashlib.sha256(png).hexdigest()}"
    )


if __name__ == "__main__":
    main()
