"""AssetCookの0.2から、同じ描画を表せる旧NVSKEL 0.1の試験用包みを作る。"""
from pathlib import Path
import argparse
import json
import math
import struct
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def read(data, offset, code="I"):
    return struct.unpack_from("<" + code, data, offset)[0]


def align(value, alignment=16):
    return (value + alignment - 1) & -alignment


def section(data, offset, size, lower=0):
    require(lower <= offset <= len(data) and size <= len(data) - offset,
            "区間がファイルの範囲外です")
    return data[offset:offset + size]


def package_entries(data):
    require(len(data) >= 96 and data[:8] == b"NVPKGv1\0", "包みのヘッダが不正です")
    require(struct.unpack_from("<IHHII", data, 8) == (96, 1, 0, 0x01020304, 64)
            and read(data, 24, "Q") == len(data), "包みの版・長さが不正です")
    count, flags = struct.unpack_from("<II", data, 32)
    table, table_size, names, names_size, blob = struct.unpack_from("<5Q", data, 40)
    alignment = read(data, 80)
    require(flags == 0 and not any(data[84:96]) and alignment >= 8
            and alignment & (alignment - 1) == 0, "包みの予約領域・整列が不正です")
    require(table_size == count * 64 and table >= 96
            and table + table_size <= names and names + names_size <= blob <= len(data)
            and all(offset % alignment == 0 for offset in (table, names, blob)),
            "包みの表の配置が不正です")
    entries, seen = [], set()
    for index in range(count):
        record = table + index * 64
        name_offset, name_size, kind, compression, entry_flags, offset, size, unpacked, digest, reserved = (
            struct.unpack_from("<QI4sII5Q", data, record))
        require(name_size > 0 and names <= name_offset and name_offset + name_size <= names + names_size,
                "entry名の範囲が不正です")
        name = bytes(section(data, name_offset, name_size))
        require(b"\0" not in name and (name, kind) not in seen, "entry名が不正または重複しています")
        seen.add((name, kind))
        require(compression == entry_flags == reserved == 0 and size == unpacked
                and offset % alignment == 0, "entryの圧縮・予約領域・長さ・整列が不正です")
        payload = section(data, offset, size, blob)
        require(fnv(payload) == digest, "包みのentryのhashが一致しません")
        entries.append({"record": record, "name": name, "kind": kind, "offset": offset,
                        "size": size, "hash": digest, "payload": payload})
    return entries


def skeletal_entry(entries):
    found = [entry for entry in entries if entry["kind"] == b"Skl0"
             and entry["name"].endswith(b".nvskel")]
    require(len(found) == 1, "Skl0の.nvskel entryがちょうど1個必要です")
    return found[0]


def check_wire(data, minor):
    header = 320 if minor == 2 else 256
    require(len(data) >= header and data[:8] == b"NVSKELv0", "NVSKELのヘッダが不正です")
    require(struct.unpack_from("<IHH6I", data, 8) ==
            (header, 0, minor, 0x01020304, 64, 80, 32, 32, 32),
            f"NVSKEL 0.{minor}の版・ヘッダ長・レコード長が不正です")
    require(read(data, 40, "Q") == len(data), "NVSKELのファイル長が一致しません")
    counts = struct.unpack_from("<6I", data, 168)
    vertices, indices, joints, clips, channels, samples = counts
    require(all(counts) and indices % 3 == 0 and joints <= 128
            and clips <= channels <= samples, "NVSKELの数量が不正です")
    require(clips == 1, "複数clipは0.1で同じ描画を表せません")
    sections = [struct.unpack_from("<QQ", data, 48 + index * 16) for index in range(7)]
    sizes = [vertices * 64, indices * 4, joints * 80, clips * 32, channels * 32, samples * 32]
    if minor == 2:
        require(struct.unpack_from("<II", data, 256) == (64, 64)
                and not any(data[304:320]), "0.2の拡張レコード長・追加頂点情報が未対応です")
        submeshes, slots = struct.unpack_from("<II", data, 296)
        require(submeshes == slots == 1, "submesh・材質slotは各1個だけ0.1へ変換できます")
        sections[6:6] = [struct.unpack_from("<QQ", data, 264), struct.unpack_from("<QQ", data, 280)]
        sizes += [64, 64]
    cursor = header
    for index, (offset, size) in enumerate(sections):
        require(offset == cursor and offset % 16 == 0, "NVSKELの区間順序・整列が不正です")
        section(data, offset, size, header)
        if index < len(sizes):
            require(size == sizes[index], "NVSKELの区間長と数量が一致しません")
        end = offset + size
        cursor = align(end) if index + 1 < len(sections) else end
        require(cursor <= len(data) and not any(data[end:cursor]), "NVSKELの詰め物が不正です")
    require(cursor == len(data), "NVSKELの末尾が文字列表の末尾と一致しません")
    hashed = data[192:256] + (data[256:320] if minor == 2 else b"") + data[header:]
    require(fnv(hashed) == read(data, 160, "Q"), "NVSKELのhashが一致しません")

    strings = section(data, *sections[-1])

    def check_name(offset, length, legacy=True):
        value = section(strings, offset, length)
        require(b"\0" not in value, "名前にNULが含まれています")
        value.decode("utf-8")
        require(not legacy or all(0x20 <= byte <= 0x7e for byte in value),
                "joint・clip名は0.1で表せる印字可能ASCIIに限ります")

    for index in range(joints):
        record = sections[2][0] + index * 80
        check_name(read(data, record + 4), read(data, record + 8))
    clip = sections[3][0]
    check_name(read(data, clip, "Q"), read(data, clip + 8))
    require(read(data, clip + 16) == 0 and read(data, clip + 20) == channels,
            "単一clipのchannel範囲が全体を覆っていません")
    require(all(math.isfinite(value) for value in struct.unpack_from("<16f", data, 192)),
            "mesh-node変換に有限でない値があります")
    if minor == 2:
        submesh, slot = sections[6][0], sections[7][0]
        index_start, index_count, vertex_start, vertex_count, material, flags = struct.unpack_from("<6I", data, submesh)
        # readerではVertexCount=0が追加の頂点上限制限なしを表す。
        require((index_start, index_count, vertex_start, material, flags) == (0, indices, 0, 0, 0)
                and vertex_count in (0, vertices),
                "submeshが全頂点・全indexを覆わないか、slot・描画フラグを0.1で保持できません")
        bounds = struct.unpack_from("<4f", data, submesh + 24)
        require(all(math.isfinite(value) for value in bounds) and bounds[3] >= 0
                and not any(data[submesh + 40:submesh + 64]) and not any(data[slot + 12:slot + 64]),
                "submeshの境界・表の予約領域が不正です")
        check_name(read(data, slot, "Q"), read(data, slot + 8), legacy=False)
    return sections, counts


def legacy_wire(data):
    sections, _ = check_wire(data, 2)
    result = bytearray(data[:256])
    struct.pack_into("<IHH", result, 8, 256, 0, 1)
    # 文字列参照は表内相対値なので、slot名を含めて文字列表を保持する。
    for index, (offset, size) in enumerate(sections[:6] + sections[-1:]):
        result.extend(bytes(align(len(result)) - len(result)))
        struct.pack_into("<QQ", result, 48 + index * 16, len(result), size)
        result.extend(data[offset:offset + size])
    struct.pack_into("<Q", result, 40, len(result))
    struct.pack_into("<Q", result, 160, fnv(result[192:256] + result[256:]))
    check_wire(result, 1)
    return bytes(result)


def replace_entry(data, entries, target, payload):
    # ほかのentryレコードのoffsetも保持する。縮小後の空き領域はゼロで埋める。
    start, size = target["offset"], target["size"]
    require(len(payload) <= size, "旧版への変換でentryが大きくなっています")
    for entry in entries:
        require(entry is target or entry["size"] == 0
                or entry["offset"] + entry["size"] <= start or start + size <= entry["offset"],
                "変換対象とほかのentryのpayloadが重なっています")
    result = bytearray(data)
    result[start:start + size] = payload + bytes(size - len(payload))
    if start + size == len(result):
        del result[start + len(payload):]
    struct.pack_into("<QQQ", result, target["record"] + 32, len(payload), len(payload), fnv(payload))
    struct.pack_into("<Q", result, 24, len(result))
    package_entries(result)
    return bytes(result)


def manifest_records(manifest, manifest_path, package_path, target, counts, minor):
    require(isinstance(manifest, dict) and manifest.get("version") == 1
            and isinstance(manifest.get("assets"), list), "manifestの版・assetsが不正です")
    package_records, target_records = [], []
    for asset in manifest["assets"]:
        require(isinstance(asset, dict) and isinstance(asset.get("cooked_package"), str),
                "manifestの包み参照が不正です")
        if (manifest_path.parent / asset["cooked_package"]).resolve() != package_path.resolve():
            continue
        package_records.append(asset)
        if asset.get("entry_name") != target["name"].decode("utf-8") or asset.get("entry_type") != "Skl0":
            continue
        require(asset.get("kind") == "model" and asset.get("format") == "nvskel.v0.skinned.pnujiw.u32"
                and asset.get("cooked_version") == 0, "manifestの骨格形式が不正です")
        require(isinstance(asset.get("cooked_hash"), str) and asset["cooked_hash"].lower() == f'{target["hash"]:016x}',
                "manifestのcooked_hashがentryのhashと一致しません")
        metadata = asset.get("metadata")
        require(isinstance(metadata, dict), "manifestに骨格のmetadataが必要です")
        expected = dict(zip(("vertex_count", "index_count", "joint_count", "clip_count"), counts[:4]))
        if minor == 2:
            expected.update(submesh_count=1, material_slot_count=1)
        else:
            require("submesh_count" not in metadata and "material_slot_count" not in metadata,
                    "0.1のmanifestに表の数量が残っています")
        require(all(type(metadata.get(key)) is int and metadata[key] == value for key, value in expected.items()),
                "manifestのmetadataとNVSKELの数量が一致しません")
        target_records.append(asset)
    require(target_records, "manifestに入力包みの.nvskelを参照する記録がありません")
    return package_records, target_records


def self_check(package_path, manifest_path=None):
    target = skeletal_entry(package_entries(package_path.read_bytes()))
    _, counts = check_wire(target["payload"], 1)
    if manifest_path is not None:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
        manifest_records(manifest, manifest_path, package_path, target, counts, 1)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, help="入力する0.2の包み")
    parser.add_argument("--manifest", type=Path, help="入力または自己検査するmanifest")
    parser.add_argument("--out-package", type=Path, help="出力する0.1の包み")
    parser.add_argument("--out-manifest", type=Path, help="出力する0.1のmanifest")
    parser.add_argument("--self-check", type=Path, help="0.1の包みを読み直して検査する")
    args = parser.parse_args()
    try:
        if args.self_check:
            require(not any((args.package, args.out_package, args.out_manifest)),
                    "自己検査には--self-checkと任意の--manifestだけを指定してください")
            target = self_check(args.self_check, args.manifest)
        else:
            require(all((args.package, args.manifest, args.out_package, args.out_manifest)),
                    "--package・--manifest・--out-package・--out-manifestを指定してください")
            paths = [path.resolve() for path in (args.package, args.manifest, args.out_package, args.out_manifest)]
            require(len(set(paths)) == 4, "入力と出力には別々のパスを指定してください")
            # readerの論理パス制約に合わせ、出力はmanifestの配下に限定する。
            require(args.out_package.resolve().is_relative_to(args.out_manifest.resolve().parent),
                    "出力する包みは出力manifestの親ディレクトリの配下に置いてください")
            relative = args.out_package.resolve().relative_to(args.out_manifest.resolve().parent).as_posix()
            data = args.package.read_bytes()
            entries = package_entries(data)
            target = skeletal_entry(entries)
            converted = legacy_wire(target["payload"])
            _, counts = check_wire(target["payload"], 2)
            manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
            _, target_records = manifest_records(
                manifest, args.manifest, args.package, target, counts, 2)
            for asset in target_records:
                asset["cooked_package"] = relative
                asset["cooked_hash"] = f"{fnv(converted):016x}"
                asset["metadata"].pop("submesh_count")
                asset["metadata"].pop("material_slot_count")
            package = replace_entry(data, entries, target, converted)
            args.out_package.parent.mkdir(parents=True, exist_ok=True)
            args.out_manifest.parent.mkdir(parents=True, exist_ok=True)
            args.out_package.write_bytes(package)
            args.out_manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            target = self_check(args.out_package, args.out_manifest)
        print(f'検査成功: version=0.1 bytes={target["size"]} cooked_hash={target["hash"]:016x}')
    except (OSError, ValueError, TypeError, KeyError, struct.error) as error:
        print(f"変換・検査に失敗しました: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
