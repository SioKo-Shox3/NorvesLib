"""NVTRN 地形と NVPKG 内の地形を全域の標本配列として読む。"""

import math
from pathlib import Path
import struct
import sys

import numpy as np


_FNV_OFFSET = 14695981039346656037
_FNV_PRIME = 1099511628211
_U64_MASK = (1 << 64) - 1
_ENDIAN_MARKER = 0x01020304
_CHANNELS = {1: 1, 2: 2, 3: 4}


def _u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def _u64(data, offset):
    return struct.unpack_from("<Q", data, offset)[0]


def _range(data, offset, size, name):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError(f"{name}の区間がファイルの範囲外です。")
    return data[offset:offset + size]


def _fnv1a(data, value=_FNV_OFFSET):
    for byte in data:
        value = ((value ^ byte) * _FNV_PRIME) & _U64_MASK
    return value


def _package_terrain(data, entry_name):
    if len(data) < 96:
        raise ValueError("NVPKG のヘッダが短すぎます。")
    header_size, major, minor, endian, record_size = struct.unpack_from("<IHHII", data, 8)
    package_size = _u64(data, 24)
    entry_count, flags = struct.unpack_from("<II", data, 32)
    table_offset, table_size, names_offset, names_size, blobs_offset = struct.unpack_from("<5Q", data, 40)
    alignment, reserved0, reserved1 = struct.unpack_from("<IIQ", data, 80)
    if (header_size != 96 or (major, minor) != (1, 0) or endian != _ENDIAN_MARKER
            or record_size != 64 or package_size != len(data)):
        raise ValueError("NVPKG の版、バイト順、ヘッダまたはファイルサイズが不正です。")
    if (flags or reserved0 or reserved1 or alignment < 8 or alignment & (alignment - 1)
            or table_size != entry_count * 64):
        raise ValueError("NVPKG のヘッダまたはエントリ表が不正です。")
    _range(data, table_offset, table_size, "NVPKG のエントリ表")
    _range(data, names_offset, names_size, "NVPKG の名前表")
    _range(data, blobs_offset, 0, "NVPKG のデータ")
    if (table_offset < 96 or table_offset + table_size > names_offset
            or names_offset + names_size > blobs_offset
            or any(offset % alignment for offset in (table_offset, names_offset, blobs_offset))):
        raise ValueError("NVPKG の表またはデータの配置が不正です。")

    target_name = None if entry_name is None else entry_name.encode("utf-8")
    candidates = []
    seen = set()
    for index in range(entry_count):
        record = table_offset + index * 64
        name_offset, name_size, entry_type, compression, entry_flags = struct.unpack_from("<QIIII", data, record)
        data_offset, stored_size, unpacked_size, payload_hash, reserved = struct.unpack_from("<5Q", data, record + 24)
        if (not name_size or compression != 0 or entry_flags or reserved or stored_size != unpacked_size
                or data_offset < blobs_offset or data_offset % alignment):
            raise ValueError("NVPKG のエントリ形式またはデータの配置が不正です。")
        if (name_offset < names_offset or name_offset > names_offset + names_size
                or name_size > names_offset + names_size - name_offset):
            raise ValueError("NVPKG のエントリ名が名前表の範囲外です。")
        name = bytes(_range(data, name_offset, name_size, "NVPKG のエントリ名"))
        contents = _range(data, data_offset, stored_size, "NVPKG のエントリ")
        if b"\0" in name or (name, entry_type) in seen:
            raise ValueError("NVPKG のエントリ名が不正または重複しています。")
        seen.add((name, entry_type))
        if entry_type == int.from_bytes(b"Trn0", "little") and (target_name is None or target_name == name):
            candidates.append((contents, payload_hash))

    if len(candidates) != 1:
        raise ValueError("指定に一致する NVPKG の地形エントリがちょうど１件ではありません。")
    terrain, expected_hash = candidates[0]
    if _fnv1a(terrain) != expected_hash:
        raise ValueError("NVPKG の地形エントリのハッシュが一致しません。")
    return terrain


def _texture_pixels(data, side, expected_format, expected_color):
    if len(data) < 112 or bytes(data[:8]) != b"NVTEXv0\0":
        raise ValueError("埋め込み NVTEX の識別子またはヘッダが不正です。")
    header_size, major, minor, endian, record_size = struct.unpack_from("<IHHII", data, 8)
    if major != 0 or minor not in (0, 1):
        raise ValueError("埋め込み NVTEX は未対応の形式です。")
    if header_size != 112 or endian != _ENDIAN_MARKER or record_size != 32 or _u64(data, 24) != len(data):
        raise ValueError("NVTEX のバイト順、ヘッダまたはファイルサイズが不正です。")
    table_offset, table_size, payload_offset, payload_size, payload_hash = struct.unpack_from("<5Q", data, 32)
    width, height, layer_count, mip_count, pixel_format, color, flags, reserved0 = struct.unpack_from("<8I", data, 72)
    reserved1 = _u64(data, 104)
    if pixel_format not in _CHANNELS:
        raise ValueError("埋め込み NVTEX の画素は未対応の形式です。")
    if pixel_format != expected_format or color != expected_color:
        raise ValueError("NVTEX の画素形式または色空間がレイヤ表と一致しません。")
    if (color not in (1, 2) or (color == 2 and pixel_format != 3)
            or width != side or height != side or layer_count != 1
            or mip_count != max(width, height).bit_length() or flags or reserved0 or reserved1):
        raise ValueError("NVTEX の寸法、ミップ数、色空間または予約欄が不正です。")
    if table_offset < 112 or table_size != mip_count * 32:
        raise ValueError("NVTEX のミップ表が不正です。")
    _range(data, table_offset, table_size, "NVTEX のミップ表")
    payload = _range(data, payload_offset, payload_size, "NVTEX のペイロード")
    if payload_offset < table_offset + table_size or not payload_size:
        raise ValueError("NVTEX のペイロード配置が不正です。")

    channels = _CHANNELS[pixel_format]
    cursor = payload_offset
    first_offset = None
    for index in range(mip_count):
        offset, size, mip_width, mip_height, reserved_a, reserved_b = struct.unpack_from(
            "<QQIIII", data, table_offset + index * 32
        )
        if (reserved_a or reserved_b or mip_width != max(1, width >> index)
                or mip_height != max(1, height >> index)
                or size != mip_width * mip_height * channels or offset != cursor
                or offset > payload_offset + payload_size
                or size > payload_offset + payload_size - offset):
            raise ValueError("NVTEX のミップの寸法または区間が不正です。")
        if index == 0:
            first_offset = offset
        cursor += size
    if cursor != payload_offset + payload_size:
        raise ValueError("NVTEX のミップの配置が不正です。")
    if _fnv1a(payload) != payload_hash:
        raise ValueError("NVTEX のペイロードハッシュが一致しません。")
    return np.frombuffer(data, dtype=np.uint8, count=side * side * channels, offset=first_offset).reshape(
        side, side, channels
    )


def _read_terrain(data):
    if len(data) < 192 or bytes(data[:8]) != b"NVTRNv0\0":
        raise ValueError("NVTRN の識別子またはヘッダが不正です。")
    header_size, major, minor, endian, flags = struct.unpack_from("<IHHII", data, 8)
    file_size, payload_hash = struct.unpack_from("<QQ", data, 24)
    if (header_size != 192 or (major, minor) != (0, 0) or endian != _ENDIAN_MARKER
            or file_size != len(data)):
        raise ValueError("NVTRN の版、バイト順、ヘッダまたはファイルサイズが不正です。")
    if flags or _u32(data, 68) or _u32(data, 116) or _u64(data, 184):
        raise ValueError("NVTRN の予約欄が不正です。")
    if _fnv1a(data[40:], _fnv1a(data[:32])) != payload_hash:
        raise ValueError("NVTRN の全体ハッシュが一致しません。")

    cells_x, cells_z, tile_cells, tiles_x, tiles_z, lod_count = struct.unpack_from("<6I", data, 40)
    cell_size = struct.unpack_from("<f", data, 64)[0]
    origin_x, origin_y, origin_z = struct.unpack_from("<3d", data, 72)
    height_min, height_max = struct.unpack_from("<2f", data, 96)
    layer_count, layer_record_size, tile_record_size = struct.unpack_from("<3I", data, 104)
    if (not cells_x or not cells_z or not tile_cells or cells_x % tile_cells or cells_z % tile_cells
            or tiles_x != cells_x // tile_cells or tiles_z != cells_z // tile_cells
            or not all(math.isfinite(value) for value in (cell_size, origin_x, origin_y, origin_z, height_min, height_max))
            or cell_size <= 0 or height_min > height_max or not 1 <= lod_count <= 8):
        raise ValueError("NVTRN の寸法、原点、高さの範囲または LOD 数が不正です。")
    if not 1 <= layer_count <= 16 or layer_record_size != 32 or tile_record_size != 80 + layer_count * 24:
        raise ValueError("NVTRN のレイヤ数またはレコードサイズが不正です。")
    layer_offset, layer_size, tile_offset, tile_size, string_offset, string_size, payload_offset, payload_size = (
        struct.unpack_from("<8Q", data, 120)
    )
    tile_count = tiles_x * tiles_z
    if (layer_offset != 192 or layer_size != layer_count * 32 or tile_offset != layer_offset + layer_size
            or tile_size != tile_count * tile_record_size or string_offset != tile_offset + tile_size
            or payload_offset != (string_offset + string_size + 63) // 64 * 64
            or payload_offset + payload_size != file_size):
        raise ValueError("NVTRN の表またはペイロードの配置が不正です。")
    _range(data, layer_offset, layer_size, "NVTRN のレイヤ表")
    _range(data, tile_offset, tile_size, "NVTRN のタイル表")
    strings = _range(data, string_offset, string_size, "NVTRN の文字列表")
    _range(data, payload_offset, payload_size, "NVTRN のペイロード")
    if any(data[string_offset + string_size:payload_offset]):
        raise ValueError("NVTRN の表の後の余白が不正です。")

    layer_defs = []
    semantics = {}
    next_semantic = 0
    for index in range(layer_count):
        record = layer_offset + index * 32
        fourcc, encoding, pixel_format, color, semantic_offset, semantic_size, reserved = struct.unpack_from(
            "<4sIIIIIQ", data, record
        )
        name = fourcc.decode("latin-1")
        if (reserved or name in semantics or not 1 <= semantic_size <= 255 or semantic_offset != next_semantic
                or color not in (1, 2)):
            raise ValueError("NVTRN のレイヤまたは意味の文字列が不正です。")
        if index == 0:
            if (fourcc, encoding, pixel_format, color) != (b"HGT0", 1, 8, 1):
                raise ValueError("NVTRN の高さレイヤが不正です。")
        elif fourcc == b"HGT0" or encoding != 2:
            raise ValueError("NVTRN の埋め込みレイヤが不正です。")
        elif pixel_format not in _CHANNELS:
            raise ValueError("NVTRN の埋め込みレイヤは未対応の形式です。")
        semantic = bytes(_range(strings, semantic_offset, semantic_size, "NVTRN の意味の文字列"))
        if any(byte < 0x20 or byte > 0x7e for byte in semantic):
            raise ValueError("NVTRN の意味の文字列に不正な文字があります。")
        semantics[name] = semantic.decode("ascii")
        next_semantic += semantic_size
        layer_defs.append((name, pixel_format, color))
    if next_semantic != string_size:
        raise ValueError("NVTRN の文字列表の大きさが不正です。")

    side = tile_cells + 1
    records = []
    seen = set()
    previous_end = payload_offset
    for index in range(tile_count):
        record = tile_offset + index * tile_record_size
        tile_x, tile_z = struct.unpack_from("<2I", data, record)
        if tile_x >= tiles_x or tile_z >= tiles_z or (tile_x, tile_z) in seen:
            raise ValueError("NVTRN のタイル座標が範囲外または重複しています。")
        seen.add((tile_x, tile_z))
        bounds = struct.unpack_from("<8f", data, record + 8)
        lods = struct.unpack_from("<8f", data, record + 44)
        if (_u32(data, record + 40) or _u32(data, record + 76)
                or not all(math.isfinite(value) for value in bounds)
                or bounds[0] > bounds[1] or any(bounds[2 + i] > bounds[5 + i] for i in range(3))
                or any(not math.isfinite(value) or value < 0 for value in lods[:lod_count])
                or any(value != 0 for value in lods[lod_count:])):
            raise ValueError("NVTRN のタイル範囲、LOD 誤差または予約欄が不正です。")
        ranges = []
        for layer_index in range(layer_count):
            offset, size, expected_hash = struct.unpack_from("<3Q", data, record + 80 + layer_index * 24)
            if not size or offset < payload_offset or offset % 64:
                raise ValueError("NVTRN のレイヤ区間が不正です。")
            _range(data, offset, size, "NVTRN のレイヤ")
            expected_offset = (previous_end + 63) // 64 * 64
            if offset != expected_offset or any(data[previous_end:offset]):
                raise ValueError("NVTRN のレイヤの配置または余白が不正です。")
            if layer_index == 0 and size != side * side * 2:
                raise ValueError("NVTRN の高さ標本のバイト数が不正です。")
            ranges.append((offset, size, expected_hash))
            previous_end = offset + size
        records.append((tile_z, tile_x, ranges))
    if len(seen) != tile_count or previous_end != file_size:
        raise ValueError("NVTRN のタイルまたはペイロードが不足しています。")

    shape = (cells_z + 1, cells_x + 1)
    heights = np.empty(shape, dtype=np.uint16)
    layers = {name: np.empty((*shape, _CHANNELS[pixel_format]), dtype=np.uint8)
              for name, pixel_format, _ in layer_defs[1:]}
    for tile_z, tile_x, ranges in sorted(records):
        z = tile_z * tile_cells
        x = tile_x * tile_cells
        for index, (offset, size, expected_hash) in enumerate(ranges):
            chunk = data[offset:offset + size]
            name, pixel_format, color = layer_defs[index]
            if _fnv1a(chunk) != expected_hash:
                raise ValueError(f"タイル ({tile_x}, {tile_z}) の {name} のデータハッシュが一致しません。")
            if index == 0:
                samples = np.frombuffer(chunk, dtype="<u2").reshape(side, side)
                output = heights
            else:
                samples = _texture_pixels(chunk, side, pixel_format, color)
                output = layers[name]
            if tile_x and not np.array_equal(output[z:z + side, x], samples[:, 0]):
                raise ValueError(f"タイル ({tile_x}, {tile_z}) の {name} の左辺が一致しません。")
            if tile_z and not np.array_equal(output[z, x:x + side], samples[0, :]):
                raise ValueError(f"タイル ({tile_x}, {tile_z}) の {name} の上辺が一致しません。")
            output[z:z + side, x:x + side] = samples

    heights_m = heights.astype(np.float64)
    heights_m *= (height_max - height_min) / 65535.0
    heights_m += height_min
    return {
        "origin_x": origin_x,
        "origin_y": origin_y,
        "origin_z": origin_z,
        "cell_size_m": cell_size,
        "heights_m": heights_m,
        "height_min_m": height_min,
        "height_max_m": height_max,
        "layers": layers,
        "semantics": semantics,
    }


def load_terrain(path, entry_name=None) -> dict:
    """地形ファイルまたはパッケージから、高さとレイヤの全域を読む。"""
    data = memoryview(Path(path).read_bytes())
    magic = bytes(data[:8])
    if magic == b"NVPKGv1\0":
        data = _package_terrain(data, entry_name)
    elif magic != b"NVTRNv0\0":
        raise ValueError("NVPKG または NVTRN の識別子が不正です。")
    return _read_terrain(data)


def _main():
    if len(sys.argv) != 2:
        print("使い方: python Scripts/Map/nvtrn_reader.py <地形ファイル>", file=sys.stderr)
        return 2
    try:
        terrain = load_terrain(sys.argv[1])
    except ValueError as error:
        print(f"読み込み失敗: {error}", file=sys.stderr)
        return 1
    except OSError:
        print(f"地形ファイルを読めません: {sys.argv[1]}", file=sys.stderr)
        return 1

    heights = terrain["heights_m"]
    print(f"標本の寸法: {heights.shape[1]} × {heights.shape[0]}（列 × 行）")
    print(f"セルの大きさ: {terrain['cell_size_m']:.6g} m")
    print(f"原点: ({terrain['origin_x']:.6g}, {terrain['origin_y']:.6g}, {terrain['origin_z']:.6g}) m")
    print(f"高さの標本: 最小 {heights.min():.6f} m、最大 {heights.max():.6f} m")
    print(f"高さの写像範囲: {terrain['height_min_m']:.6f} ～ {terrain['height_max_m']:.6f} m")
    print("レイヤ:")
    for name, values in terrain["layers"].items():
        print(f"  {name}: {terrain['semantics'][name]}（{values.shape[2]} チャネル）")
        for channel in range(values.shape[2]):
            pixels = values[:, :, channel]
            print(f"    {channel}: 最小 {pixels.min()}、平均 {pixels.mean():.3f}、最大 {pixels.max()}")
    return 0


if __name__ == "__main__":
    sys.exit(_main())
