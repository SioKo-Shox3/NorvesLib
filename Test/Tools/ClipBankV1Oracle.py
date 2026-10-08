"""独立literalから新ClipBank bytesとnative束縛結果だけを照合する。旧goldenは生成しない。"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import unittest


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def topology():
    return (b"NVSKEL_TOPOLOGY\0" + struct.pack("<II", 1, 2)
            + struct.pack("<I", 5) + b"Child" + struct.pack("<I", 1)
            + struct.pack("<I", 4) + b"Root" + struct.pack("<I", 0xffffffff))


def reference_bank():
    # 各値はM9 fixtureのsource default/TRS/trackから独立に固定する。
    joint_names = [b"Child", b"Root"]
    parents = [1, 0xffffffff]
    label = b"RigV1Fixture/rig.gltf"
    clip = b"Wave"
    strings = b"".join(joint_names) + label + clip
    joint_offsets = [0, 5]
    tjnt = b"".join(struct.pack("<QIIQ", offset, len(name), parent, fnv(name))
                    for offset, name, parent in zip(joint_offsets, joint_names, parents))
    transforms = [(0, 1, 0, 0, 0, 0, 1, 1, 1, 1), (0, 0, 0, 0, 0, 0, 1, 1, 1, 1)]
    rest_values = [struct.pack("<10f", *value) for value in transforms]
    arst = b"".join(value + bytes(8) for value in rest_values)
    rset = struct.pack("<IIQIIQdQ", 0, 2, 9, len(label), 1, fnv(b"".join(rest_values)), 1.0, 0)
    clips = struct.pack("<QII f III QQ", 9 + len(label), 4, 0, 2.0, 0, 2, 0, fnv(clip), 0)
    channels = (struct.pack("<QIIQIIIIQ", 0, 5, 0, fnv(b"Child"), 0, 0, 0, 2, 0)
                + struct.pack("<QIIQIIIIQ", 5, 4, 1, fnv(b"Root"), 1, 0, 2, 2, 0))
    samples = b"".join(struct.pack("<5f", *v) + bytes(12)
                       for v in [(0, 0, 1, 0, 0), (2, 0, 3, 0, 0), (0, 0, 0, 0, 1), (2, 0, 0, 1, 0)])
    sections = [(b"STRS", 1, strings), (b"TJNT", 24, tjnt), (b"RSET", 48, rset),
                (b"ARST", 48, arst), (b"CLIP", 48, clips), (b"CHAN", 48, channels), (b"SAMP", 32, samples)]
    result = bytearray(480)
    result[:8] = b"NVSKELv1"
    struct.pack_into("<IHHIIIIQ", result, 8, 256, 1, 0, 0x01020304, 3, 0, 7, 256)
    struct.pack_into("<QII", result, 56, fnv(topology()), 1, 1)
    for index, (fourcc, stride, data) in enumerate(sections):
        result.extend(bytes((-len(result)) % 16))
        struct.pack_into("<4sIQQII", result, 256 + index * 32, fourcc, 1, len(result), len(data), stride, len(data) // stride)
        result.extend(data)
    struct.pack_into("<QQ", result, 40, len(result), fnv(result[256:]))
    return bytes(result)


def check_bank(actual):
    expected = reference_bank()
    if actual != expected:
        raise ValueError("新v1 bytesが独立referenceと一致しません")
    return {"bytes": len(actual), "sha256": hashlib.sha256(actual).hexdigest()}


def check_binding(value):
    expected = {"schema": 1, "skeleton_id": "7498d74adc178547", "comparison_complete": True,
                "override_used": True, "exceeded_joints": 1, "translation_delta_m": 1.0,
                "author_child_y": 1.0, "target_child_y": 2.0, "sampled_child_y": 2.0}
    if set(value) != set(expected):
        raise ValueError("束縛reportの項目が不一致です")
    for key, wanted in expected.items():
        actual = value[key]
        if isinstance(wanted, bool):
            valid = type(actual) is bool and actual == wanted
        elif isinstance(wanted, float):
            valid = type(actual) in (int, float) and math.isfinite(actual) and abs(actual - wanted) <= 1e-6
        else:
            valid = type(actual) is type(wanted) and actual == wanted
        if not valid:
            raise ValueError("束縛reportの値が不一致です: " + key)


class OracleTests(unittest.TestCase):
    def test_topology_literal(self):
        self.assertEqual(len(topology()), 49)
        self.assertEqual(fnv(topology()), 0x7498d74adc178547)

    def test_determinism(self):
        self.assertEqual(reference_bank(), reference_bank())
        self.assertEqual(check_bank(reference_bank())["bytes"], len(reference_bank()))

    def test_header(self):
        bank = reference_bank()
        self.assertEqual(bank[:8], b"NVSKELv1")
        self.assertEqual(struct.unpack_from("<QQ", bank, 40), (len(bank), fnv(bank[256:])))

    def test_directory(self):
        bank = reference_bank()
        for i in range(7):
            _, flags, offset, size, stride, count = struct.unpack_from("<4sIQQII", bank, 256 + i * 32)
            self.assertEqual(flags, 1)
            self.assertEqual(offset % 16, 0)
            self.assertEqual(size, stride * count)
            self.assertLessEqual(offset + size, len(bank))

    def test_changed_byte_refused(self):
        bank = reference_bank()
        for position in range(len(bank)):
            altered = bytearray(bank)
            altered[position] ^= 1
            with self.assertRaises(ValueError):
                check_bank(altered)

    def test_truncation_refused(self):
        bank = reference_bank()
        for length in (0, 8, 255, 479, len(bank) - 1):
            with self.assertRaises(ValueError):
                check_bank(bank[:length])

    def test_binding_good(self):
        check_binding({"schema": 1, "skeleton_id": "7498d74adc178547", "comparison_complete": True,
                       "override_used": True, "exceeded_joints": 1, "translation_delta_m": 1.0,
                       "author_child_y": 1.0, "target_child_y": 2.0, "sampled_child_y": 2.0})

    def test_binding_bad(self):
        good = {"schema": 1, "skeleton_id": "7498d74adc178547", "comparison_complete": True,
                "override_used": True, "exceeded_joints": 1, "translation_delta_m": 1.0,
                "author_child_y": 1.0, "target_child_y": 2.0, "sampled_child_y": 2.0}
        for key, bad in [("sampled_child_y", 3), ("translation_delta_m", float("nan")), ("override_used", 1),
                         ("author_child_y", 2), ("skeleton_id", "0000000000000000")]:
            value = dict(good)
            value[key] = bad
            with self.assertRaises(ValueError):
                check_binding(value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--bank", action="append", default=[])
    parser.add_argument("--binding", action="append", default=[])
    args = parser.parse_args()
    if args.self_test:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(OracleTests))
        if not result.wasSuccessful():
            raise SystemExit(1)
    if not args.self_test and (not args.bank or not args.binding):
        parser.error("実nativeのbankとbinding reportを指定してください")
    for path in args.bank:
        check_bank(Path(path).read_bytes())
    for path in args.binding:
        check_binding(json.loads(Path(path).read_text(encoding="utf-8-sig")))
    if args.bank or args.binding:
        print(f"CLIPBANK_V1_ORACLE result=pass banks={len(args.bank)} binding_reports={len(args.binding)} legacy_regeneration=0")


if __name__ == "__main__":
    main()
